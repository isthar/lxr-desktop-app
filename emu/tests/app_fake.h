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
	/* a test's knobs for the device, all 0 for a well-behaved one */
	uint64_t openTicks;		/* the time an open takes; the watchdog is polled before and after it */
	uint64_t firstDelay;	/* the first callback comes this long after the open returns (0: one period) */
	int silent;				/* the unit opens and never calls back */
	int flushOnClose;		/* a callback that is in flight when the unit stops, and completes */
	unsigned earlyRequest;	/* a callback of this many frames before the open returns */
	emu_watchdog_t dog;		/* polled with the fake clock (the watchdog thread's job) */
	int dogFired;
	unsigned opens, closes;	/* successful opens, closes of an open unit */
	uint64_t openAt[64];	/* the fake clock when each open returned */
	char uid[128];			/* the last request */
	uint32_t bufferFrames;
	unsigned deviceChannels, channels;	/* the device's outputs (2 if 0); the open unit's (R5, F3) */
	void (*rendered)(const float *buf, unsigned frames, unsigned channels);	/* every callback's output, if set */
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
			const unsigned req = fakeRequest(fk.callbacks++);
			app_audioRender(fk.audio, &ts, req, fk.buf, fk.channels);
			if (fk.rendered)
				fk.rendered(fk.buf, req, fk.channels);
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
static inline void fakeCallback(unsigned frames)
{
	const app_audioTime_t ts = { fk.now + FAKE_LEAD, fk.now, 1.0 };
	app_audioRender(fk.audio, &ts, frames, fk.buf, fk.channels);
}
static inline void fakeClose(void *ctx)
{
	(void)ctx;
	if (fk.running && fk.flushOnClose)
		fakeCallback(fakeRequest(fk.callbacks++));
	fk.running = 0;
	fk.closes++;
}
static inline void fakePoll(void)
{
	if (emu_watchdogPoll(&fk.dog, (double)fk.now / FAKE_TPS))
		fk.dogFired = 1;
}
static inline void fakeMidiWake(void *ctx) { (void)ctx; }
/* spec 6.3's built-in output: 48 kHz, 512 frames, latencies 60 + 690 frames, safety offset 48 */
static inline int fakeOpen(void *ctx, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info)
{
	(void)ctx;
	snprintf(fk.uid, sizeof fk.uid, "%s", req->uid);
	fk.bufferFrames = req->bufferFrames;
	if (!fk.device)
		return -1;
	fakePoll();
	fk.now += fk.openTicks;
	fakePoll();
	memset(info, 0, sizeof *info);
	info->deviceRate = 48000.0;
	info->clientRate = 44140.625;
	info->deviceLatency = 60u;
	info->streamLatency = 690u;
	info->safetyOffset = 48u;
	info->bufferFrames = 512u;
	const unsigned dev = fk.deviceChannels ? fk.deviceChannels : 2u;
	fk.channels = req->channels < 2u ? 2u : req->channels > dev ? dev : req->channels;	/* as app_caOpen() (F3) */
	info->channels = fk.channels;
	info->deviceChannels = dev;
	fk.audio = a;
	fk.running = 1;
	fk.nextCallback = fk.silent ? UINT64_MAX : fk.now + (fk.firstDelay ? fk.firstDelay : FAKE_PERIOD);
	if (fk.earlyRequest)
		fakeCallback(fk.earlyRequest);	/* the unit was running before this function returned */
	if (fk.opens < sizeof fk.openAt / sizeof fk.openAt[0])
		fk.openAt[fk.opens] = fk.now;
	fk.opens++;
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
