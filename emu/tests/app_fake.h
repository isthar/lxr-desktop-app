/*
 * app_fake.h
 *
 * A fake host for the engine's tests (plan R4, E3): a clock in host ticks, a fake
 * output device that calls app_audioRender() as the CoreAudio callback does, the
 * test's own arrivals, and a card made from the init fixture. One thread.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_FAKE_H_
#define APP_FAKE_H_

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_engine.h"
#include "emu_hostfs.h"
#include "emu_test.h"

#define FAKE_TPS 24000000.0
#define FAKE_PERIOD 256000u	/* 512 frames at 48 kHz, in host ticks */
#define FAKE_LEAD 306160u	/* mHostTime - entry: (512 + 48) / 48 kHz + 1.09 ms (spec 12.1 M18) */

typedef struct fake
{
	uint64_t now;			/* host ticks */
	int device, running;	/* a device exists; the output is open */
	app_audio_t *audio;
	uint64_t nextCallback, callbacks;
	void (*event)(struct fake *f);	/* the test's arrivals; it sets the next nextEvent */
	uint64_t nextEvent;
	float buf[4096 * 4];
} fake_t;
static fake_t fk;

/* callback k's request: 512 frames at 48 kHz in 44,140.625 Hz frames, 470 or 471 */
static inline unsigned fakeRequest(uint64_t k)
{
	return (unsigned)((k + 1u) * 512u * 353125u / 384000u - k * 512u * 353125u / 384000u);
}

/* advance to until, running the callbacks and events due; with stop, return after one callback */
static inline void fakeAdvance(uint64_t until, int stop)
{
	for (;;)
	{
		const uint64_t cb = fk.running ? fk.nextCallback : UINT64_MAX;
		const uint64_t ev = fk.event ? fk.nextEvent : UINT64_MAX;
		const uint64_t next = cb <= ev ? cb : ev;
		if (next > until)
		{
			fk.now = until > fk.now ? until : fk.now;
			return;
		}
		fk.now = next > fk.now ? next : fk.now;
		if (next == cb)
		{
			const app_audioTime_t ts = { cb + FAKE_LEAD, cb, 1.0 };
			app_audioRender(fk.audio, &ts, fakeRequest(fk.callbacks++), fk.buf, 2u);
			fk.nextCallback += FAKE_PERIOD;
			if (stop)
				return;
		}
		else
			fk.event(&fk);
	}
}

static inline uint64_t fakeNow(void *ctx) { (void)ctx; return fk.now; }
static inline void fakeWait(void *ctx, uint64_t until) { (void)ctx; fakeAdvance(until, 1); }
static inline void fakeSleep(void *ctx, uint64_t until) { (void)ctx; fakeAdvance(until, 0); }
static inline void fakeClose(void *ctx) { (void)ctx; fk.running = 0; }
static inline void fakeMidiWake(void *ctx) { (void)ctx; }
/* spec 6.3's built-in output: 48 kHz, 512 frames, latencies 60 + 690 frames, safety offset 48 */
static inline int fakeOpen(void *ctx, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info)
{
	(void)ctx;
	(void)req;
	if (!fk.device)
		return -1;
	memset(info, 0, sizeof *info);
	info->deviceRate = 48000.0;
	info->clientRate = 44140.625;
	info->deviceLatency = 60u;
	info->streamLatency = 690u;
	info->safetyOffset = 48u;
	info->bufferFrames = 512u;
	info->channels = 2u;
	fk.audio = a;
	fk.running = 1;
	fk.nextCallback = fk.now + FAKE_PERIOD;
	return 0;
}

static inline void fakeHost(app_host_t *h, int device)
{
	memset(&fk, 0, sizeof fk);
	fk.device = device;
	fk.now = 1000u;
	fk.nextEvent = UINT64_MAX;
	*h = (app_host_t){ NULL, FAKE_TPS, fakeNow, fakeWait, fakeSleep, fakeOpen, fakeClose, fakeMidiWake };
}

/* the init fixture, copied, with GLO.CFG byte 0 set: a tempo, 0 EXT or 1 MID */
static inline void fakeCard(char *dir, size_t n, uint8_t sync)
{
	emu_testTempDir(dir, n);
	char path[1100];
	snprintf(path, sizeof path, "%s/PROJ00/GLO.CFG", dir);
	FILE *f = NULL;
	if (emu_hostCopyCard(EMU_TEST_CARDS "/init", dir, 0) != 0 || (f = fopen(path, "r+b")) == NULL)
	{
		perror(path);
		exit(2);
	}
	fputc(sync, f);
	fclose(f);
}

#endif /* APP_FAKE_H_ */
