/*
 * test_shm.c
 *
 * P3: the shared memory between the plugin and the engine (plan E9.2 Task 3): the header check,
 * the event ring, the audio ring with its epoch and underruns, a new engine's epoch, and the two
 * rings between two processes, with a killed child seen as the end of a pipe.
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
	s->version = 2;
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "version 2") != NULL);
	plug_shmInit(s, 1000.0, 512);
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	plug_shmInit(s, 44100.0, 0);
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	s->magic = 0;
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "LCHP") != NULL);
	munmap(s, sizeof *s);
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

TEST(P3_shm_audio)
{
	plug_shm_t *s = anon();
	float lr[2 * 64], l[100], r[100];
	for (unsigned i = 0; i < 64; i++)
	{
		lr[2 * i] = (float)i;
		lr[2 * i + 1] = -(float)i;
	}
	plug_audioBegin(s, 100);
	plug_audioWrite(s, lr, 64);
	ASSERT_EQ(s->audioEnd, 164);
	/* not running: no underrun is counted */
	ASSERT_EQ(plug_audioRead(s, 90, 100, l, r), 64);
	ASSERT_EQ_DBL(l[9], 0.0);
	ASSERT_EQ_DBL(l[10], 0.0);
	ASSERT_EQ_DBL(l[11], 1.0);
	ASSERT_EQ_DBL(r[73], -63.0);
	ASSERT_EQ_DBL(l[74], 0.0);
	ASSERT_EQ(s->underruns, 0);
	/* running: the 26 frames from 164 on are missing; the 10 before the epoch are not */
	s->state = PLUG_ENGINE_RUNNING;
	ASSERT_EQ(plug_audioRead(s, 90, 100, l, r), 64);
	ASSERT_EQ(s->underruns, 26);
	munmap(s, sizeof *s);
}

TEST(P3_shm_new_engine)
{
	plug_shm_t *s = anon();
	float lr[2 * 1000], l[100], r[100];
	for (unsigned i = 0; i < 2000; i++)
		lr[i] = 1.0f;
	s->state = PLUG_ENGINE_RUNNING;
	plug_audioBegin(s, 100);
	plug_audioWrite(s, lr, 1000);
	/* a new engine starts at 5000: the old frames are gone, the gap is not an underrun */
	plug_audioBegin(s, 5000);
	ASSERT_EQ(plug_audioRead(s, 1000, 100, l, r), 0);
	ASSERT_EQ_DBL(l[0], 0.0);
	ASSERT_EQ(s->underruns, 0);
	plug_audioWrite(s, lr, 64);
	ASSERT_EQ(plug_audioRead(s, 4990, 100, l, r), 64);
	ASSERT_EQ_DBL(l[9], 0.0);
	ASSERT_EQ_DBL(l[10], 1.0);
	ASSERT_EQ(s->underruns, 26);
	munmap(s, sizeof *s);
}

TEST(P3_shm_overwritten)
{
	plug_shm_t *s = anon();
	static float lr[2 * PLUG_AUDIO_FRAMES];
	float l[8], r[8];
	for (unsigned i = 0; i < PLUG_AUDIO_FRAMES; i++)
		lr[2 * i] = lr[2 * i + 1] = (float)(i + 1);
	plug_audioBegin(s, 0);
	plug_audioWrite(s, lr, PLUG_AUDIO_FRAMES);
	plug_audioWrite(s, lr, 4);	/* frames 0..3 are overwritten by 32768..32771 */
	ASSERT_EQ(plug_audioRead(s, 0, 8, l, r), 4);
	ASSERT_EQ_DBL(l[3], 0.0);
	ASSERT_EQ_DBL(l[4], 5.0);
	munmap(s, sizeof *s);
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
			const float lr[2] = { (float)a, -(float)a };
			plug_audioWrite(s, lr, 1);
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
		const uint64_t burst = w + 300u > N_EVENTS ? N_EVENTS : w + 300u;
		while (w < burst)
		{
			const uint8_t b = (uint8_t)(0x80u | (w & 0x7Fu));
			if (plug_evPush(s, w, &b, 1) != 0)
				break;
			w++;
		}
		plug_publish(s, w);
		(void)!write(wake[1], "w", 1);
		static float l[N_EVENTS / 1000], r[N_EVENTS / 1000];
		const uint64_t end = plug_load(&s->audioEnd);
		while (checked < end)
		{
			const uint32_t n = end - checked > 2000u ? 2000u : (uint32_t)(end - checked);
			ASSERT_EQ(plug_audioRead(s, checked, n, l, r), n);
			for (uint32_t i = 0; i < n; i++)
				ASSERT_EQ_DBL(r[i], -(double)(checked + i));
			checked += n;
		}
	}
	const uint64_t dropped = s->evDropped;
	close(wake[1]);	/* the child ends */
	int status = 0;
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);
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
