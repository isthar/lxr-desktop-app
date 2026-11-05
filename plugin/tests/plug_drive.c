/*
 * plug_drive.c
 *
 * The engine's test driver (plan E9.2 Task 6): the plugin's side of the shared memory without
 * the VST 3 layer. It starts lichomat-engine on a card, waits until it runs (epoch 0), then
 * plays a script of lichomat-render's usb lines ("<frame>f usb XX ..", host frames) in blocks,
 * and writes the ring's frames [0, FRAMES) as a 16-bit stereo WAV.
 *
 *   plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll]
 *
 * Without realtime it waits for each frame; with it, it paces the blocks by the wall clock,
 * reads each block plug_readOffset(RATE, BLOCK, MARGIN) frames behind as the plugin does, and
 * counts the underruns.
 * With preroll it publishes while the engine boots, as the plugin does in a host: a block about
 * every 0.5 ms from before the engine starts, with a Note On in the block before the start and
 * one in the first block after the engine reports booting, which the new engine must leave out.
 * Once the engine runs it pushes a Note On at epoch - 1, which the engine must drop, and plays
 * the script from the epoch, script frame f at host frame epoch + f, and the WAV holds the
 * frames [epoch, epoch + FRAMES). The pre-roll's blocks are at most 64 frames: the engine's
 * first hook waits for the events up to epoch + 66 (eng_run.c), so a block published between
 * the epoch and the driver seeing it leaves the script's first frames still to come.
 * At 44,140.625 Hz a sample is floor(x x 32,768), the renderer's --stereo sum exactly.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "plug_shm.h"
#include "plug_spawn.h"
#include "plug_time.h"

typedef struct
{
	uint64_t frame;
	unsigned n;
	uint8_t b[64];
} line_t;

static double wall(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static unsigned msgLen(uint8_t st)
{
	if (st >= 0x80u && st < 0xF0u)
		return ((st >> 4) == 0xC || (st >> 4) == 0xD) ? 2u : 3u;
	if (st == 0xF2u)
		return 3u;
	if (st == 0xF1u || st == 0xF3u)
		return 2u;
	return 1u;
}

static void put16(FILE *f, unsigned v)
{
	fputc((int)(v & 255u), f);
	fputc((int)((v >> 8) & 255u), f);
}

static void put32(FILE *f, uint32_t v)
{
	put16(f, v & 0xFFFFu);
	put16(f, v >> 16);
}

static int16_t sample(float x, int native)
{
	const double v = native ? floor((double)x * 32768.0) : floor((double)x * 32768.0 + 0.5);
	return (int16_t)(v > 32767.0 ? 32767.0 : v < -32768.0 ? -32768.0 : v);
}

int main(int argc, char **argv)
{
	const int realtime = argc == 10 && !strcmp(argv[8], "realtime");
	const int preroll = argc == 9 && !strcmp(argv[8], "preroll");
	if (argc != 8 && !realtime && !preroll)
	{
		fprintf(stderr, "usage: plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll]\n");
		return 1;
	}
	const double rate = strtod(argv[3], NULL);
	const uint32_t block = (uint32_t)strtoul(argv[4], NULL, 10);
	const uint64_t frames = strtoull(argv[6], NULL, 10);
	const uint32_t margin = realtime ? (uint32_t)strtoul(argv[9], NULL, 10) : 0u;
	const int native = plug_isNative(rate);
	/* the script */
	FILE *sf = fopen(argv[5], "r");
	if (!sf)
	{
		perror(argv[5]);
		return 1;
	}
	line_t *lines = NULL;
	unsigned nlines = 0;
	char text[1024];
	while (fgets(text, sizeof text, sf))
	{
		unsigned long long f;
		char *p = strstr(text, "f usb");
		if (text[0] == '#' || !p || sscanf(text, "%llu", &f) != 1)
			continue;
		lines = realloc(lines, (nlines + 1u) * sizeof *lines);
		line_t *l = &lines[nlines++];
		l->frame = f;
		l->n = 0;
		p += 5;
		unsigned v;
		int k;
		while (l->n < sizeof l->b && sscanf(p, " %x%n", &v, &k) == 1)
		{
			l->b[l->n++] = (uint8_t)v;
			p += k;
		}
	}
	fclose(sf);
	/* the shared memory, the pipes, the engine */
	char err[256];
	int shmFd = -1;
	plug_shm_t *s = plug_shmCreate(rate, block, &shmFd, err, sizeof err);
	if (!s)
	{
		fprintf(stderr, "plug_drive: %s\n", err);
		return 1;
	}
	s->realtime = (uint32_t)realtime;
	int wake[2], life[2];
	if (pipe(wake) || pipe(life) || fcntl(wake[1], F_SETFL, O_NONBLOCK))
		return 1;
	signal(SIGPIPE, SIG_IGN);
	/* the pre-roll: blocks of at most 64 frames, the first one, with a Note On, before the engine
	 * starts */
	static const uint8_t junk[3] = { 0x90, 0x30, 0x7F };
	const uint32_t preBlock = block < 64u ? block : 64u;
	uint64_t n = 0;
	if (preroll)
	{
		(void)plug_evPush(s, n, junk, 3);
		n += preBlock;
		plug_publish(s, n);
	}
	const double w0 = wall();
	const pid_t pid = plug_spawnEngine(argv[1], argv[2], wake[0], life[1], shmFd, NULL, err, sizeof err);
	if (pid < 0)
	{
		fprintf(stderr, "plug_drive: %s\n", err);
		return 1;
	}
	close(wake[0]);
	close(life[1]);
	int junkBooting = 0;
	double lastPub = w0;
	while (plug_load(&s->state) != PLUG_ENGINE_RUNNING)
	{
		if (preroll && wall() - lastPub >= 0.0005)
		{
			/* a Note On in the first block after the engine reports booting: it boots for more than
			 * 10 ms after that (the firmware's boot and the renderer's settle), so the block is
			 * published, and the Note On is before the epoch, long before the engine takes it */
			if (!junkBooting && plug_load(&s->state) == PLUG_ENGINE_BOOTING)
			{
				(void)plug_evPush(s, n, junk, 3);
				junkBooting = 1;
			}
			n += preBlock;
			plug_publish(s, n);
			(void)!write(wake[1], "w", 1);
			lastPub = wall();
		}
		struct pollfd p = { life[0], POLLIN, 0 };
		if (poll(&p, 1, preroll ? 0 : 5) > 0 || plug_load(&s->state) == PLUG_ENGINE_FAILED || wall() - w0 > 60.0)
		{
			fprintf(stderr, "plug_drive: the engine did not start (state %llu)\n", (unsigned long long)s->state);
			return 2;
		}
		if (preroll)
			nanosleep(&(const struct timespec){ 0, 50000L }, NULL);
	}
	const double wRun = wall();
	/* base: the host frame of script frame 0 and of the WAV's first frame, the epoch */
	const uint64_t base = preroll ? plug_load(&s->epoch) : 0u;
	if (preroll)
	{
		if (base == 0 || n > base + 64u)
		{
			fprintf(stderr, "plug_drive: pre-roll: epoch %llu with %llu frames published\n", (unsigned long long)base,
				(unsigned long long)n);
			return 5;
		}
		(void)plug_evPush(s, base - 1u, junk, 3);	/* after the engine's start, before its epoch */
		for (unsigned i = 0; i < nlines; i++)
			lines[i].frame += base;
	}
	/* play */
	float *l = calloc(frames + block, sizeof *l), *r = calloc(frames + block, sizeof *r);
	uint64_t got = 0, firstMiss = 0, lastMiss = 0, lastEnd = 0;
	double lastMove = wall();
	unsigned next = 0;
	const uint32_t lp = plug_readOffset(rate, block, margin);
	while (got < frames)
	{
		/* waiting: at most 8,192 frames ahead of the engine's audio, so that neither ring overflows
		 * and the engine has no more to play than the frames asked for */
		while (!realtime && n + block > plug_load(&s->audioEnd) + 8192u)
		{
			struct pollfd p = { life[0], POLLIN, 0 };
			if (poll(&p, 1, 0) > 0)
				break;
			const uint64_t end = plug_load(&s->audioEnd);
			if (end > base + got)
			{
				const uint64_t to = end - base < frames ? end - base : frames;
				(void)plug_audioRead(s, base + got, (uint32_t)(to - got), &l[got], &r[got]);
				got = to;
			}
			if (end != lastEnd)
			{
				lastEnd = end;
				lastMove = wall();
			}
			else if (wall() - lastMove > 10.0)
			{
				fprintf(stderr, "plug_drive: the engine's audio stopped at host frame %llu, %llu published\n",
					(unsigned long long)end, (unsigned long long)n);
				return 4;
			}
			nanosleep(&(const struct timespec){ 0, 50000L }, NULL);
		}
		while (next < nlines && lines[next].frame < n + block)
		{
			const line_t *li = &lines[next++];
			for (unsigned i = 0; i < li->n;)
			{
				const unsigned len = msgLen(li->b[i]);
				(void)plug_evPush(s, li->frame, &li->b[i], (uint8_t)len);
				i += len;
			}
		}
		plug_publish(s, n + block);
		(void)!write(wake[1], "w", 1);
		if (realtime)
		{
			/* the plugin's read of the block [n, n + block): plug_readOffset() frames behind */
			if (n >= lp)
			{
				const uint64_t from = n - lp;
				const uint32_t take = from + block > frames ? (uint32_t)(frames > from ? frames - from : 0u) : block;
				const uint64_t before = s->underruns;
				if (take)
					(void)plug_audioRead(s, from, take, &l[from], &r[from]);
				if (s->underruns != before)
				{
					if (!firstMiss)
						firstMiss = from;
					lastMiss = from;
				}
				got = from + take;
			}
			const double due = wRun + (double)(n + block) / rate;
			while (wall() < due)
				nanosleep(&(const struct timespec){ 0, 100000L }, NULL);
		}
		else
		{
			const uint64_t end = plug_load(&s->audioEnd);
			if (end > base + got)
			{
				const uint64_t to = end - base < frames ? end - base : frames;
				(void)plug_audioRead(s, base + got, (uint32_t)(to - got), &l[got], &r[got]);
				got = to;
			}
		}
		n += block;
		struct pollfd p = { life[0], POLLIN, 0 };
		if (poll(&p, 1, 0) > 0)
		{
			fprintf(stderr, "plug_drive: the engine died at host frame %llu\n", (unsigned long long)n);
			return 3;
		}
	}
	const double wEnd = wall();
	plug_store(&s->quit, 1);
	close(wake[1]);
	int status = 0;
	waitpid(pid, &status, 0);
	FILE *o = fopen(argv[7], "wb");
	const uint32_t data = (uint32_t)(frames * 4u);
	fwrite("RIFF", 1, 4, o);
	put32(o, 36u + data);
	fwrite("WAVEfmt ", 1, 8, o);
	put32(o, 16);
	put16(o, 1);
	put16(o, 2);
	const uint32_t hz = native ? 44141u : (uint32_t)rate;
	put32(o, hz);
	put32(o, hz * 4u);
	put16(o, 4);
	put16(o, 16);
	fwrite("data", 1, 4, o);
	put32(o, data);
	for (uint64_t i = 0; i < frames; i++)
	{
		put16(o, (uint16_t)sample(l[i], native));
		put16(o, (uint16_t)sample(r[i], native));
	}
	fclose(o);
	printf("plug_drive: epoch %llu, %llu frames, late %llu, dropped %llu, ring full %llu, underruns %llu (blocks from "
		"%llu to %llu); start %.3f s, play %.3f s (%.1fx real time), engine exit %d\n",
		(unsigned long long)s->epoch, (unsigned long long)frames, (unsigned long long)s->late,
		(unsigned long long)s->dropped, (unsigned long long)s->evDropped, (unsigned long long)s->underruns,
		(unsigned long long)firstMiss, (unsigned long long)lastMiss, wRun - w0, wEnd - wRun,
		(double)frames / rate / (wEnd - wRun), WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	return 0;
}
