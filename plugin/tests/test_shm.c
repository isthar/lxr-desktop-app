/*
 * test_shm.c
 *
 * P3: the shared memory between the plugin and the engine (plan E9.2 Task 3): the header check,
 * the event ring, the audio ring with its epoch and underruns, a new engine's epoch, and the two
 * rings between two processes, with a killed child seen as the end of a pipe. Plan E9.4: the
 * ring's four channels read apart or summed, and the engine's values under their sequence count.
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
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "plug_shm.h"
#include "seqc_test.h"

static plug_shm_t *anon(void)
{
	plug_shm_t *s = mmap(NULL, sizeof *s, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	if (s == MAP_FAILED)
		abort();
	plug_shmInit(s, 48000.0, 512);
	return s;
}

TEST(P3_shm_header)
{
	plug_shm_t *s = anon();
	char err[160];
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), 0);
	ASSERT_EQ(s->size, sizeof(plug_shm_t));
	/* the prototype's version 1 (two channels, no values): refused, both ways named */
	s->version = 1;
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "version 1") != NULL && strstr(err, "needs version 2") != NULL);
	plug_shmInit(s, 1000.0, 512);
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	plug_shmInit(s, 44100.0, 0);
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	s->magic = 0;
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "LCHP") != NULL);
	/* the version is this one, the size another layout's */
	plug_shmInit(s, 48000.0, 512);
	s->size = (uint32_t)sizeof(plug_shm_t) - 8u;
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	char want[40];
	snprintf(want, sizeof want, "version 2 size %u,", (unsigned)sizeof(plug_shm_t) - 8u);
	ASSERT_TRUE(strstr(err, want) != NULL);
	/* the bounds: 8,000-384,000 Hz, blocks of 1-8,192 frames */
	const struct
	{
		double rate;
		uint32_t block;
		int check;
	} b[] = { { 8000.0, 1, 0 }, { 384000.0, 8192, 0 }, { 7999.5, 512, -1 }, { 384000.5, 512, -1 },
		{ 48000.0, 8193, -1 }, { NAN, 512, -1 } };
	for (unsigned i = 0; i < sizeof b / sizeof b[0]; i++)
	{
		plug_shmInit(s, b[i].rate, b[i].block);
		ASSERT_EQ(plug_shmCheck(s, err, sizeof err), b[i].check);
	}
	munmap(s, sizeof *s);
}

TEST(P3_shm_create)
{
	char err[200], name[32];
	/* A smaller object, as a peer of another layout would make, with a header of this version: the
	 * map refuses it with the sizes, before anything reads past its end. */
	snprintf(name, sizeof name, "/lchpt.%d", (int)getpid());
	const int f = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	ASSERT_TRUE(f >= 0);
	shm_unlink(name);
	ASSERT_EQ(ftruncate(f, 4096), 0);
	uint32_t *h = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, f, 0);
	ASSERT_TRUE(h != MAP_FAILED);
	h[0] = PLUG_SHM_MAGIC;
	h[1] = PLUG_SHM_VERSION;
	h[2] = 4096;
	munmap(h, 4096);
	ASSERT_TRUE(plug_shmMap(f, err, sizeof err) == NULL);
	char want[40];
	snprintf(want, sizeof want, "size %zu", sizeof(plug_shm_t));
	ASSERT_TRUE(strstr(err, want) != NULL);
	close(f);
	/* Names this process's next create takes, left by a dead process of the same pid between its
	 * shm_open() and shm_unlink(): the create takes the one it meets over, once. */
	for (unsigned k = 0; k < 16; k++)
	{
		snprintf(name, sizeof name, "/lchp.%d.%u", (int)getpid(), k);
		const int g = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (g >= 0)
			close(g);
	}
	int fd = -1;
	plug_shm_t *s = plug_shmCreate(48000.0, 512, &fd, err, sizeof err);
	if (!s)
		printf("%s\n", err);
	for (unsigned k = 0; k < 16; k++)
	{
		snprintf(name, sizeof name, "/lchp.%d.%u", (int)getpid(), k);
		shm_unlink(name);
	}
	ASSERT_TRUE(s != NULL);
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), 0);
	plug_shmClose(s);
	close(fd);
}

TEST(P3_shm_events)
{
	plug_shm_t *s = anon();
	plug_event_t e;
	ASSERT_EQ(plug_evPop(s, &e), 0);
	ASSERT_EQ(plug_evNextFrame(s), UINT64_MAX);
	for (unsigned i = 0; i < PLUG_EVENT_SLOTS; i++)
	{
		const uint8_t b[3] = { 0x90, (uint8_t)(i & 0x7F), 100 };
		ASSERT_EQ(plug_evPush(s, 10u * i, b, 3), 0);
	}
	const uint8_t f8 = 0xF8;
	ASSERT_EQ(plug_evPush(s, 99999, &f8, 1), -1);	/* full */
	ASSERT_EQ(s->evDropped, 1);
	ASSERT_EQ(plug_evPush(s, 99999, &f8, 0), -1);	/* no bytes */
	ASSERT_EQ(plug_evPush(s, 99999, &f8, 4), -1);	/* too many */
	ASSERT_EQ(s->evDropped, 3);
	ASSERT_EQ(plug_evNextFrame(s), 0);
	for (unsigned i = 0; i < 10; i++)
	{
		ASSERT_EQ(plug_evPop(s, &e), 1);
		ASSERT_EQ(e.frame, 10u * i);
		ASSERT_EQ(e.len, 3);
		ASSERT_EQ(e.b[1], i);
	}
	ASSERT_EQ(plug_evPush(s, 50000, &f8, 1), 0);	/* room again */
	plug_evSkipAll(s);
	ASSERT_EQ(plug_evPop(s, &e), 0);
	ASSERT_EQ(s->evRead, s->evWrite);
	plug_publish(s, 1234);
	ASSERT_EQ(plug_load(&s->hostFrames), 1234);
	munmap(s, sizeof *s);
}

/* frames of four channels: L1 = i, R1 = -i, L2 = 3i, R2 = -3i (the sum: 2i, -2i) */
static void fourOf(float *x, unsigned frames)
{
	for (unsigned i = 0; i < frames; i++)
	{
		x[4 * i] = (float)i;
		x[4 * i + 1] = -(float)i;
		x[4 * i + 2] = 3.0f * (float)i;
		x[4 * i + 3] = -3.0f * (float)i;
	}
}

TEST(P3_shm_audio)
{
	plug_shm_t *s = anon();
	float x[4 * 64], l[100], r[100];
	float *const sum[4] = { l, r, NULL, NULL };
	fourOf(x, 64);
	plug_audioBegin(s, 100);
	plug_audioWrite(s, x, 64);
	ASSERT_EQ(s->audioEnd, 164);
	/* not running: no underrun is counted */
	ASSERT_EQ(plug_audioRead(s, 90, 100, sum, 1), 64);
	ASSERT_EQ_DBL(l[9], 0.0);
	ASSERT_EQ_DBL(l[10], 0.0);
	ASSERT_EQ_DBL(l[11], 2.0);
	ASSERT_EQ_DBL(r[73], -126.0);
	ASSERT_EQ_DBL(l[74], 0.0);
	ASSERT_EQ(s->underruns, 0);
	/* running: the 26 frames from 164 on are missing; the 10 before the epoch are not */
	s->state = PLUG_ENGINE_RUNNING;
	ASSERT_EQ(plug_audioRead(s, 90, 100, sum, 1), 64);
	ASSERT_EQ(s->underruns, 26);
	munmap(s, sizeof *s);
}

TEST(P3_shm_split)
{
	/* the four channels apart, each where the engine wrote it; the frames the ring does not hold 0 in
	 * all four; and the sum is plug_sum() of the two outputs, exact for the device's samples */
	plug_shm_t *s = anon();
	float x[4 * 64], l1[80], r1[80], l2[80], r2[80];
	float *const split[4] = { l1, r1, l2, r2 };
	fourOf(x, 64);
	plug_audioBegin(s, 1000);
	plug_audioWrite(s, x, 64);
	for (unsigned c = 0; c < 4; c++)
		for (unsigned i = 0; i < 80; i++)
			split[c][i] = 0.5f;
	ASSERT_EQ(plug_audioRead(s, 990, 80, split, 0), 64);
	ASSERT_EQ_DBL(l1[9], 0.0);
	ASSERT_EQ_DBL(r2[9], 0.0);
	ASSERT_EQ_DBL(l1[15], 5.0);
	ASSERT_EQ_DBL(r1[15], -5.0);
	ASSERT_EQ_DBL(l2[15], 15.0);
	ASSERT_EQ_DBL(r2[15], -15.0);
	ASSERT_EQ_DBL(l2[74], 0.0);
	ASSERT_EQ_DBL(r2[79], 0.0);
	for (int32_t a = -32768; a <= 32767; a += 257)
		for (int32_t b = -32768; b <= 32767; b += 263)
			ASSERT_EQ_DBL(plug_sum((float)a / 32768.0f, (float)b / 32768.0f), (double)(a + b) / 65536.0);
	munmap(s, sizeof *s);
}

TEST(P3_shm_values)
{
	plug_shm_t *s = anon();
	plug_values_t v;
	ASSERT_EQ(plug_valuesRead(s, &v), 0);	/* none written */
	const uint8_t a[3] = { 7, 127, 0 };
	plug_valuesWrite(s, 4321, a, 3, 15, 0x0F);
	ASSERT_EQ(plug_valuesRead(s, &v), 1);
	ASSERT_EQ(v.frame, 4321);
	ASSERT_EQ(v.values[0], 7);
	ASSERT_EQ(v.values[1], 127);
	ASSERT_EQ(v.values[2], 0);
	ASSERT_EQ(v.values[3], 0xFF);	/* past n */
	ASSERT_EQ(v.values[PLUG_VALUES - 1], 0xFF);
	ASSERT_EQ(v.channel, 15);
	ASSERT_EQ(v.rxFilter, 0x0F);
	ASSERT_EQ(s->valuesSeq, 2);
	/* a write in progress (the count odd) is not taken */
	s->valuesSeq = 3;
	ASSERT_EQ(plug_valuesRead(s, &v), 0);
	s->valuesSeq = 4;
	ASSERT_EQ(plug_valuesRead(s, &v), 1);
	munmap(s, sizeof *s);
}

TEST(P3_shm_new_engine)
{
	plug_shm_t *s = anon();
	float x[4 * 1000], l[100], r[100];
	float *const sum[4] = { l, r, NULL, NULL };
	for (unsigned i = 0; i < 4000; i++)
		x[i] = 1.0f;
	s->state = PLUG_ENGINE_RUNNING;
	plug_audioBegin(s, 100);
	plug_audioWrite(s, x, 1000);
	/* a new engine starts at 5000: the old frames are gone, the gap is not an underrun */
	plug_audioBegin(s, 5000);
	ASSERT_EQ(plug_audioRead(s, 1000, 100, sum, 1), 0);
	ASSERT_EQ_DBL(l[0], 0.0);
	ASSERT_EQ(s->underruns, 0);
	plug_audioWrite(s, x, 64);
	ASSERT_EQ(plug_audioRead(s, 4990, 100, sum, 1), 64);
	ASSERT_EQ_DBL(l[9], 0.0);
	ASSERT_EQ_DBL(l[10], 1.0);
	ASSERT_EQ(s->underruns, 26);
	munmap(s, sizeof *s);
}

TEST(P3_shm_overwritten)
{
	plug_shm_t *s = anon();
	static float x[4 * PLUG_AUDIO_FRAMES];
	float l[8], r[8];
	float *const sum[4] = { l, r, NULL, NULL };
	for (unsigned i = 0; i < 4 * PLUG_AUDIO_FRAMES; i++)
		x[i] = (float)(i / 4 + 1);
	plug_audioBegin(s, 0);
	plug_audioWrite(s, x, PLUG_AUDIO_FRAMES);
	plug_audioWrite(s, x, 4);	/* frames 0..3 are overwritten by 32768..32771 */
	/* and frame 4 shares its slot with frame 32772, the next the engine writes: the ring holds the
	 * 32,767 frames before audioEnd */
	ASSERT_EQ(plug_audioRead(s, 0, 8, sum, 1), 3);
	ASSERT_EQ_DBL(l[3], 0.0);
	ASSERT_EQ_DBL(l[4], 0.0);
	ASSERT_EQ_DBL(l[5], 6.0);
	ASSERT_EQ_DBL(r[7], 8.0);
	munmap(s, sizeof *s);
}

TEST(P3_shm_overrun)
{
	/* A child writes frames as fast as it can, in blocks of 1,024, frame f's samples f mod 2^24;
	 * the parent reads the oldest 1,024 frames the ring holds, the first of which the child writes
	 * over meanwhile. A frame plug_audioRead() copies must be that frame, not one written over it
	 * while it was copied: the copied frames are the last n of the 1,024 (n its result), the others
	 * 0. The large blocks make a wide window for an audioEnd published only at a block's end. */
	char err[160];
	int fd = -1;
	plug_shm_t *s = plug_shmCreate(48000.0, 512, &fd, err, sizeof err);
	ASSERT_TRUE(s != NULL);
	plug_audioBegin(s, 0);
	const pid_t pid = fork();
	if (pid == 0)
	{
		alarm(30);
		plug_shm_t *c = plug_shmMap(fd, err, sizeof err);
		if (!c)
			_exit(3);
		static float x[4 * 1024];
		uint64_t f = 0;
		while (!plug_load(&c->quit))
		{
			for (unsigned i = 0; i < 4 * 1024; i++)
				x[i] = (float)((f + i / 4) & 0xFFFFFFu);
			plug_audioWrite(c, x, 1024);
			f += 1024;
		}
		_exit(0);
	}
	alarm(30);
	static float l[1024], r[1024];
	float *const sum[4] = { l, r, NULL, NULL };
	uint64_t reads = 0, copied = 0, wrong = 0;
	while (reads < 20000u)
	{
		const uint64_t end = plug_load(&s->audioEnd);
		if (end < PLUG_AUDIO_FRAMES + 1024u)
			continue;	/* the ring fills */
		const uint64_t from = end - PLUG_AUDIO_FRAMES;
		const uint32_t n = plug_audioRead(s, from, 1024, sum, 1);
		for (uint32_t i = 0; i < 1024; i++)
		{
			const float want = i >= 1024u - n ? (float)((from + i) & 0xFFFFFFu) : 0.0f;
			wrong += l[i] != want || r[i] != want;
		}
		copied += n;
		reads++;
	}
	plug_store(&s->quit, 1);
	int status = 0;
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	alarm(0);
	printf("P3_shm_overrun: %llu reads, %llu frames copied, %llu wrong, %llu frames written\n",
		(unsigned long long)reads, (unsigned long long)copied, (unsigned long long)wrong,
		(unsigned long long)plug_load(&s->audioEnd));
	ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	ASSERT_EQ(wrong, 0);
	ASSERT_TRUE(copied > 0);
	plug_shmClose(s);
	close(fd);
}

#define N_EVENTS 2000000u

/* the child: each event in order, one audio frame per published host frame */
static int child(plug_shm_t *s, int wake)
{
	uint64_t a = 0, k = 0;
	char b[256];
	for (;;)
	{
		plug_event_t e;
		int any = 0;
		while (plug_evPop(s, &e))
		{
			if (e.frame != k || e.b[0] != (uint8_t)(0x80u | (k & 0x7Fu)))
				return 4;
			k++;
			any = 1;
		}
		const uint64_t hf = plug_load(&s->hostFrames);
		while (a < hf)
		{
			const float x[4] = { (float)a, -(float)a, (float)a, -(float)a };
			plug_audioWrite(s, x, 1);
			a++;
		}
		if (!any && read(wake, b, sizeof b) == 0)
			return k == N_EVENTS ? 0 : 5;	/* the parent closed the pipe */
	}
}

TEST(P3_shm_processes)
{
	char err[160];
	int fd = -1;
	plug_shm_t *s = plug_shmCreate(48000.0, 512, &fd, err, sizeof err);
	ASSERT_TRUE(s != NULL);
	int wake[2], life[2];
	ASSERT_EQ(pipe(wake), 0);
	ASSERT_EQ(pipe(life), 0);
	ASSERT_EQ(fcntl(wake[1], F_SETFL, O_NONBLOCK), 0);
	/* a child that ends early closes the wake pipe: the parent then gets EPIPE, stops, and the test
	 * reports the child's exit code instead of dying of SIGPIPE */
	void (*const pipeWas)(int) = signal(SIGPIPE, SIG_IGN);
	const pid_t pid = fork();
	if (pid == 0)
	{
		close(wake[1]);
		close(life[0]);
		plug_shm_t *c = plug_shmMap(fd, err, sizeof err);	/* a mapping of its own, as the engine's */
		_exit(c && plug_shmCheck(c, err, sizeof err) == 0 ? child(c, wake[0]) : 3);
	}
	close(wake[0]);
	close(life[1]);
	uint64_t w = 0, checked = 0;
	while (checked < N_EVENTS)
	{
		uint64_t burst = w + 300u > N_EVENTS ? N_EVENTS : w + 300u;
		/* hostFrames at most 8,192 frames ahead of the frames checked, as the plugin keeps the
		 * engine (it publishes one block ahead of the frames it reads): the child writes one frame
		 * per published frame, and more than PLUG_AUDIO_FRAMES ahead it would overwrite frames
		 * not checked yet */
		if (burst > checked + 8192u)
			burst = checked + 8192u;
		while (w < burst)
		{
			const uint8_t b = (uint8_t)(0x80u | (w & 0x7Fu));
			if (plug_evPush(s, w, &b, 1) != 0)
				break;
			w++;
		}
		plug_publish(s, w);
		if (write(wake[1], "w", 1) < 0 && errno == EPIPE)
			break;	/* the child is gone */
		static float l[N_EVENTS / 1000], r[N_EVENTS / 1000];
		float *const sum[4] = { l, r, NULL, NULL };
		const uint64_t end = plug_load(&s->audioEnd);
		while (checked < end)
		{
			const uint32_t n = end - checked > 2000u ? 2000u : (uint32_t)(end - checked);
			ASSERT_EQ(plug_audioRead(s, checked, n, sum, 1), n);
			for (uint32_t i = 0; i < n; i++)
				ASSERT_EQ_DBL(r[i], -(double)(checked + i));
			checked += n;
		}
	}
	const uint64_t dropped = s->evDropped;
	close(wake[1]);	/* the child ends */
	int status = 0;
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	signal(SIGPIPE, pipeWas);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);	/* 3: no mapping, 4: an event out of order, 5: events missing */
	ASSERT_EQ(checked, N_EVENTS);
	/* the child held life[1]; it is closed now: the read end gives EOF */
	char c;
	ASSERT_EQ(read(life[0], &c, 1), 0);
	printf("P3_shm_processes: %u events and frames, %llu pushes refused while full\n", N_EVENTS,
		(unsigned long long)dropped);
	plug_shmClose(s);
	close(fd);
}

TEST(P3_shm_killed)
{
	int life[2];
	ASSERT_EQ(pipe(life), 0);
	const pid_t pid = fork();
	if (pid == 0)
	{
		close(life[0]);
		alarm(30);	/* the parent kills it; if the test fails before, it ends by itself */
		for (;;)
			pause();
	}
	close(life[1]);
	struct pollfd p = { life[0], POLLIN, 0 };
	ASSERT_EQ(poll(&p, 1, 50), 0);	/* alive: nothing to read */
	ASSERT_EQ(kill(pid, SIGKILL), 0);
	ASSERT_EQ(poll(&p, 1, 2000), 1);
	char c;
	ASSERT_EQ(read(life[0], &c, 1), 0);	/* EOF: the child is gone */
	int status = 0;
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
}

TEST_MAIN()
