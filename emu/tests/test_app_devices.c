/*
 * test_app_devices.c
 *
 * R4 (stage 1b spec 6.2, plan E22): the device glue against the real system. Each
 * probe runs in a forked child for at most 5 s and skips (exit 77) when there is
 * no output device, when the server refuses, or when there is no answer: CI has
 * no audio device and the sandbox may refuse or hang CoreAudio.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <mach/mach_time.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "app_coreaudio.h"
#include "emu_test.h"

/* fn in a forked child for at most 5 s: its exit code, or 77 without an answer (E22). The parent
 * never touches CoreAudio or CoreMIDI, so the child starts clean. */
static int probe(int (*fn)(void), const char *id)
{
	fflush(stdout);
	const pid_t pid = fork();
	if (pid == 0)
	{
		const int r = fn();
		fflush(stdout);	/* _exit() does not */
		_exit(r);
	}
	for (int i = 0; i < 100; i++)
	{
		int status;
		if (waitpid(pid, &status, WNOHANG) == pid)
			return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
		const struct timespec d = { 0, 50000000L };
		nanosleep(&d, NULL);
	}
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	printf("SKIP %s: no answer within 5 s\n", id);
	return 77;
}

#define PROBE(fn, id) do { const int r_ = probe(fn, id); if (r_ == 77) exit(77); ASSERT_EQ(r_, 0); } while (0)

static int coreaudioChild(void)
{
	static app_engine_t e;	/* its audio rings only */
	app_host_t h;
	memset(&h, 0, sizeof h);
	h.ticksPerSec = 24e6;
	if (app_engineInit(&e, &h) != 0)
		return 1;
	app_ca_t ca;
	memset(&ca, 0, sizeof ca);
	ca.engine = &e;
	const app_outputRequest_t req = { "", 0u, 2u };	/* the default output at its own buffer */
	app_outputInfo_t info;
	char err[256];
	if (app_caOpen(&ca, &e.audio, &req, &info, err, sizeof err) != 0)
	{
		printf("SKIP R4_dev_coreaudio: %s\n", err);
		return 77;
	}
	const struct timespec d = { 1, 0 };
	nanosleep(&d, NULL);
	app_caClose(&ca);
	const double want = info.bufferFrames * 44140.625 / info.deviceRate;
	const unsigned got = atomic_load(&e.audio.maxRequest);
	printf("%s: %.0f Hz, %u frames, client %.6f Hz, %s; %llu callbacks, largest request %u (expected %.1f)\n",
		info.name, info.deviceRate, info.bufferFrames, info.clientRate, info.format,
		(unsigned long long)atomic_load(&e.audio.callbacks), got, want);
	if (!(info.clientRate == 44140.625 && atomic_load(&e.audio.callbacks) >= 5u && fabs(got - want) <= 2.0))
		return 1;
	/* the engine reopens at a stall, a device change and a mix change: open and close must repeat cleanly. Four
	 * channels asked of a stereo device give two. */
	const app_outputRequest_t four = { "", 0u, 4u };
	for (int k = 0; k < 3; k++)
	{
		const uint64_t before = atomic_load(&e.audio.callbacks);
		if (app_caOpen(&ca, &e.audio, &four, &info, err, sizeof err) != 0)
		{
			printf("cycle %d: %s\n", k, err);
			return 1;
		}
		nanosleep(&(const struct timespec){ 0, 100000000L }, NULL);
		app_caClose(&ca);
		if (atomic_load(&e.audio.callbacks) < before + 3u || (info.channels != 2u && info.channels != 4u))
		{
			printf("cycle %d: %llu callbacks, %u channels\n", k,
				(unsigned long long)(atomic_load(&e.audio.callbacks) - before), info.channels);
			return 1;
		}
	}
	return 0;
}

TEST(R4_dev_coreaudio) { PROBE(coreaudioChild, "R4_dev_coreaudio"); }

TEST_MAIN()
