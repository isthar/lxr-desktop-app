/*
 * eng_run.c
 *
 * The engine of the LICHOMAT plugin (plan E9.2 Task 6); see eng_run.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "eng_run.h"

#include <dirent.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "emu_core.h"
#include "eng_resample.h"
#include "plug_time.h"
#include "render_fw.h"

#define ENG_C100 21600000ull	/* 100 ms, the renderer's settle (render_main.c:38) */
#define ENG_AHEAD 66u			/* engine frames past now whose events the next hook may need */
#define ENG_PACKETS 16u			/* USB-MIDI packets in one transfer (emu_core.h:143) */

static struct
{
	plug_shm_t *s;
	int wake;
	double rate;
	int native;
	eng_resample_t rs;
	uint64_t preludeEnd, busyLast, t0, epoch;
	int phase;				/* 0 the prelude, 1 waiting for t0, 2 playing */
	uint8_t pk[4u * ENG_PACKETS];
	unsigned npk;
	uint64_t pkFrame;
	uint64_t dropped;
} E;

static void eng_exit(int code) __attribute__((noreturn));
static void eng_exit(int code)
{
	fflush(NULL);
	_exit(code);
}

static void eng_fail(int code, const char *what) __attribute__((noreturn));
static void eng_fail(int code, const char *what)
{
	fprintf(stderr, "lichomat-engine: %s at cycle %llu\n", what, (unsigned long long)emu_now());
	fflush(stderr);
	emu_trailWrite(2, 32u);
	if (E.s)
		plug_store(&E.s->state, PLUG_ENGINE_FAILED);
	eng_exit(code);
}

static void eng_onFault(void *ctx, const char *what)
{
	(void)ctx;
	char line[512];
	snprintf(line, sizeof line, "firmware fault: %s", what);
	eng_fail(3, line);
}

static void eng_onWarn(void *ctx, const char *what)
{
	(void)ctx;
	fprintf(stderr, "lichomat-engine: warning: %s\n", what);
}

/* the time-constraint policy of the app's firmware thread (app_mach.c:106-117): a period of the host's
 * largest block, a quarter of it to compute; a refusal is not an error */
static int eng_threadRealtime(double periodSec)
{
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	thread_time_constraint_policy_data_t p;
	p.period = (uint32_t)(periodSec * 1e9 * (double)tb.denom / (double)tb.numer);
	p.computation = p.period / 4u;
	p.constraint = p.period;
	p.preemptible = 1;
	return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
		(thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS ? 0 : -1;
}

/* --- the output ------------------------------------------------------------------- */

static void eng_ring(void *ctx, const float *lr, uint32_t frames)
{
	(void)ctx;
	plug_audioWrite(E.s, lr, frames);
}

static int eng_t0Due(uint64_t t)
{
	return t - E.preludeEnd >= ENG_C100 && (E.busyLast == 0 || t - E.busyLast > ENG_C100);
}

/* the audio sink: t0 as the renderer finds it (render_main.c:355-380), then each block as the
 * stereo sum, (L1 + L2) / 65536 and (R1 + R2) / 65536: the renderer's --stereo sum before its
 * shift, exactly, and -6 dB of the device's two outputs added */
static void eng_sink(void *ctx, uint64_t t, const int16_t *o1, const int16_t *o2)
{
	(void)ctx;
	if (E.phase == 1 && eng_t0Due(t))
	{
		E.t0 = t;
		E.epoch = plug_load(&E.s->hostFrames);
		plug_evSkipAll(E.s);	/* a dead engine's events, and any written before this one ran */
		plug_audioBegin(E.s, E.epoch);
		plug_store(&E.s->epoch, E.epoch);
		plug_store(&E.s->frontier, E.epoch);
		plug_store(&E.s->state, PLUG_ENGINE_RUNNING);
		E.phase = 2;
		/* in real time only: a time-constraint thread that computes for longer than its share, as an
		 * engine fed offline does, is throttled */
		if (E.s->realtime && eng_threadRealtime((double)E.s->maxBlock / E.rate) != 0)
			fprintf(stderr, "lichomat-engine: the time-constraint policy was refused; the engine runs as a normal thread\n");
	}
	if (E.phase != 2)
		return;
	float lr[2u * EMU_BLOCK_FRAMES];
	for (unsigned i = 0; i < 2u * EMU_BLOCK_FRAMES; i++)
		lr[i] = (float)((int32_t)o1[i] + o2[i]) * (1.0f / 65536.0f);
	if (E.native)
		plug_audioWrite(E.s, lr, EMU_BLOCK_FRAMES);
	else if (eng_resamplePush(&E.rs, lr, eng_ring, NULL) != 0)
		eng_fail(4, "the sample-rate converter failed");
}

/* --- the input ----------------------------------------------------------------------- */

/* the USB-MIDI 1.0 code index number of a status byte, 0 for one the plugin never sends */
static uint8_t eng_cin(uint8_t st)
{
	if (st >= 0x80u && st < 0xF0u)
		return (uint8_t)(st >> 4);
	switch (st)
	{
	case 0xF1:
	case 0xF3:
		return 0x2;
	case 0xF2:
		return 0x3;
	case 0xF6:
		return 0x5;
	default:
		return st >= 0xF8u ? 0xFu : 0u;
	}
}

static void eng_flush(void)
{
	if (!E.npk)
		return;
	emu_inUsb(E.t0 + plug_framesToCycles(E.pkFrame - E.epoch, E.rate), E.pk, 4u * E.npk);
	E.npk = 0;
}

/* every published event before frame hf: those of one frame as one transfer, in ring order */
static void eng_inject(uint64_t hf)
{
	for (;;)
	{
		if (plug_evNextFrame(E.s) >= hf)
			break;
		plug_event_t e;
		(void)plug_evPop(E.s, &e);
		const uint8_t cin = eng_cin(e.b[0]);
		if (e.frame < E.epoch || cin == 0u)
		{
			E.dropped++;
			continue;
		}
		if (E.npk && (e.frame != E.pkFrame || E.npk == ENG_PACKETS))
			eng_flush();
		E.pkFrame = e.frame;
		uint8_t *p = &E.pk[4u * E.npk++];
		p[0] = cin;
		p[1] = e.b[0];
		p[2] = e.b[1];
		p[3] = e.b[2];
	}
	eng_flush();
	emu_counters_t c;
	emu_getCounters(&c);
	plug_store(&E.s->late, c.late);
	plug_store(&E.s->dropped, E.dropped);
	plug_store(&E.s->frontier, hf);
}

/* until the plugin has published host frame need: the wake pipe, the quit flag, the parent */
static void eng_waitFor(uint64_t need)
{
	if (plug_load(&E.s->hostFrames) >= need)
		return;
	emu_waitBegin();
	while (plug_load(&E.s->hostFrames) < need)
	{
		if (plug_load(&E.s->quit) || getppid() == 1)
			eng_exit(0);
		if (E.wake < 0)
		{
			nanosleep(&(const struct timespec){ 0, 200000L }, NULL);
			continue;
		}
		struct pollfd p = { E.wake, POLLIN, 0 };
		if (poll(&p, 1, 100) > 0)
		{
			char b[4096];
			if (read(E.wake, b, sizeof b) == 0)
				eng_exit(0);	/* the plugin closed its end */
		}
	}
	emu_waitEnd();
}

static void eng_hook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	(void)why;
	if (E.phase != 2)
		return;
	const uint64_t need = plug_framesToCycles(ENG_AHEAD, PLUG_ENGINE_RATE) + (now - E.t0);
	eng_waitFor(E.epoch + plug_framesCovering(need, E.rate));
	eng_inject(plug_load(&E.s->hostFrames));
}

/* --- the prelude ---------------------------------------------------------------------- */

/* a pass of the renderer's prelude (render_main.c:787-795) */
static void eng_pass(void)
{
	uint8_t busy = 0;
	emu_runUntil(emu_now() + 1u);
	emu_directCall(rfw_morphBusy, &busy);
	if (busy)
		E.busyLast = emu_now();
}

void eng_run(plug_shm_t *s, int wake, const char *card)
{
	memset(&E, 0, sizeof E);
	E.s = s;
	E.wake = wake;
	E.rate = s->rate;
	E.native = plug_isNative(s->rate);
	plug_store(&s->pid, (uint64_t)getpid());
	plug_store(&s->state, PLUG_ENGINE_BOOTING);
	char err[256];
	if (!E.native && eng_resampleOpen(&E.rs, s->rate, err, sizeof err) != 0)
		eng_fail(4, err);
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	cfg.cardIn = 1;
	cfg.sinks.audio = eng_sink;
	cfg.hook = eng_hook;
	cfg.fault = eng_onFault;
	cfg.warn = eng_onWarn;
	emu_powerOn(&cfg);
	/* the renderer's prelude for --no-start --bpm mid (render_main.c:1005-1047), call for call:
	 * a direct call starts a HAL_GetTick() segment (emu_sched.c:522-533), so the same calls give
	 * the same firmware timing */
	emu_runUntil(1u);
	E.preludeEnd = emu_now();
	while (!(emu_now() - E.preludeEnd >= ENG_C100 && (E.busyLast == 0 || emu_now() - E.busyLast > ENG_C100)))
		eng_pass();
	uint8_t mid = 1;
	emu_directCall(rfw_bpm, &mid);
	E.preludeEnd = emu_now();
	render_selected_t sel;
	emu_directCall(rfw_selected, &sel);
	E.phase = 1;
	while (E.phase == 1)
		eng_pass();
	for (;;)
		emu_runUntil(emu_now() + EMU_CPU_HZ);
}

/* --- the card --------------------------------------------------------------------------- */

int eng_cardEmpty(const char *dir)
{
	struct stat st;
	if (stat(dir, &st) != 0)
		return errno == ENOENT ? 1 : -1;
	if (!S_ISDIR(st.st_mode))
		return -1;
	DIR *d = opendir(dir);
	if (!d)
		return -1;
	int empty = 1;
	for (const struct dirent *e; (e = readdir(d)) != NULL;)
		if (e->d_name[0] != '.')
			empty = 0;
	closedir(d);
	return empty;
}

int eng_seed(const char *dir)
{
	const int empty = eng_cardEmpty(dir);
	if (empty != 1)
	{
		fprintf(stderr, "lichomat-engine: --seed-card: %s %s\n", dir,
			empty == 0 ? "holds entries" : "is not a folder that can be read");
		return 2;
	}
	if (mkdir(dir, 0755) != 0 && errno != EEXIST)
	{
		fprintf(stderr, "lichomat-engine: --seed-card: cannot make %s: %s\n", dir, strerror(errno));
		return 2;
	}
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = dir;
	cfg.cardIn = 1;
	cfg.fault = eng_onFault;
	cfg.warn = eng_onWarn;
	emu_powerOn(&cfg);
	emu_runUntil(1u);
	emu_runUntil(emu_now() + 100u * EMU_CYCLES_PER_MS);
	int res = -1;
	emu_directCall(rfw_seedCard, &res);
	if (res != 0)
	{
		fprintf(stderr, "lichomat-engine: --seed-card: the project save failed with FatFs result %d\n", res);
		return 2;
	}
	return 0;
}
