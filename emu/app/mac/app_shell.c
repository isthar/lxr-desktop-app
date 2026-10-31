/*
 * app_shell.c
 *
 * LICHOMAT Emu without its face (plan R4): the firmware thread and the watchdog
 * thread, and what the main thread does per frame. The struct is private; the
 * harness and the GUI see the opaque API of app_shell.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_shell.h"

#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_coreaudio.h"
#include "app_mach.h"

#define APP_SHELL_WARNINGS 64u
#define APP_SHELL_STACK ((size_t)8 << 20)	/* the main thread's size: the firmware's frames are deep */
#define APP_SHELL_HANG "hang: no progress for 10 s; the audio is stopped. Restart LICHOMAT Emu."

struct app_shell
{
	app_shellOptions_t o;
	app_engine_t engine;
	app_mach_t mach;
	app_engineConfig_t cfg;
	pthread_t firmware, watchdog;
	int haveFirmware, haveWatchdog;
	_Atomic int stopped, testFault;
	int hungShown, haveSnap;
	app_snapshot_t snap;
	app_ca_t ca;
	char outputErr[256];		/**< the last open failure told to the main thread; firmware thread */
	char card[PATH_MAX];
	char warnings[APP_SHELL_WARNINGS][255];
	unsigned warningsTotal;
};

app_shell_t *app_shellNew(void)
{
	return calloc(1u, sizeof(app_shell_t));
}

/* The host's output, on the firmware thread (spec 6.1): the open, then the thread's period is the buffer's duration.
 * A failure goes to the main thread as a notice, once per error text. */
static int app_shellOutputOpen(void *ctx, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info)
{
	app_mach_t *m = ctx;
	app_shell_t *s = m->owner;
	char err[256];
	if (app_caOpen(&s->ca, a, req, info, err, sizeof err) != 0)
	{
		if (strcmp(err, s->outputErr) != 0)
		{
			app_ack_t ack;
			memset(&ack, 0, sizeof ack);
			ack.kind = APP_MSG_NOTICE;
			snprintf(ack.text, sizeof ack.text, "audio output: %s", err);
			(void)app_ringPush(&s->engine.ack, &ack);
			snprintf(s->outputErr, sizeof s->outputErr, "%s", err);
		}
		return -1;
	}
	s->outputErr[0] = '\0';
	if (info->deviceRate > 0.0 && info->bufferFrames > 0u)
		m->period = (double)info->bufferFrames / info->deviceRate;
	m->realtime = app_threadRealtime(m->period) == 0;	/* E29 */
	s->engine.diag.realtime = m->realtime;
	return 0;
}

static void app_shellOutputClose(void *ctx)
{
	app_mach_t *m = ctx;
	app_caClose(&((app_shell_t *)m->owner)->ca);
}

/* the firmware thread: power-on, then one slice after the other until APP_CMD_STOP */
static void *app_shellFirmware(void *arg)
{
	app_shell_t *s = arg;
	s->mach.realtime = app_threadRealtime(s->mach.period) == 0;	/* E29: a refusal is not an error */
	s->engine.diag.realtime = s->mach.realtime;
	app_enginePowerOn(&s->engine, &s->cfg);
	while (!app_engineSlice(&s->engine))
		if (atomic_load(&s->testFault))
			raise(SIGSEGV);	/* Task 10's crash check, on this thread */
	if (s->engine.outputRunning)
		app_shellOutputClose(&s->mach);	/* the unit stops before the thread does */
	atomic_store(&s->stopped, 1);
	return NULL;
}

/* spec 4.2, 6.9: 10 s without progress outside the hook's waits stop the audio */
static void *app_shellWatchdog(void *arg)
{
	app_shell_t *s = arg;
	emu_watchdog_t w;
	memset(&w, 0, sizeof w);
	while (!atomic_load(&s->stopped))
	{
		const struct timespec d = { 0, 100000000L };
		(void)nanosleep(&d, NULL);
		if (emu_watchdogPoll(&w, app_wallSeconds()))
		{
			atomic_store(&s->engine.audio.mute, 1);
			atomic_store(&s->engine.hung, 1);
			break;
		}
	}
	return NULL;
}

static int app_shellFail(char *err, size_t n, const char *what, int code)
{
	if (err && n)
		snprintf(err, n, "app: %s%s%s", what, code ? ": " : "", code ? strerror(code) : "");
	return -1;
}

int app_shellStart(app_shell_t *s, const app_shellOptions_t *o, char *err, size_t n)
{
	app_host_t host;
	s->o = *o;
	if (o->cardDir)
	{
		if (snprintf(s->card, sizeof s->card, "%s", o->cardDir) >= (int)sizeof s->card)
			return app_shellFail(err, n, "the card folder's path is too long", 0);
		s->o.cardDir = s->card;
	}
	if (app_machInit(&s->mach, &s->engine) != 0)
		return app_shellFail(err, n, "the Mach host did not start", 0);
	s->mach.owner = s;
	app_machHost(&s->mach, &host);
	if (!o->noAudio)
	{
		s->mach.output = &s->ca;
		s->ca.engine = &s->engine;
		host.outputOpen = app_shellOutputOpen;	/* the host is copied by app_engineInit() */
		host.outputClose = app_shellOutputClose;
	}
	if (app_engineInit(&s->engine, &host) != 0)	/* it zeroes the engine */
		return app_shellFail(err, n, "out of memory for the rings", 0);
	s->engine.audio.wake = app_machWake;
	s->engine.audio.wakeCtx = &s->mach;
	s->cfg = (app_engineConfig_t){ .cardDir = s->o.cardDir, .seed = app_seedNow(), .dAuto = 1, .dSec = 0.0,
		.mix = APP_MIX_SUM, .output = { "", o->bufferFrames, 2u }, .fault = NULL, .faultCtx = NULL };
	if (o->deviceUid)
		snprintf(s->cfg.output.uid, sizeof s->cfg.output.uid, "%s", o->deviceUid);
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, APP_SHELL_STACK);
	int e = pthread_create(&s->firmware, &attr, app_shellFirmware, s);
	if (e != 0)
	{
		pthread_attr_destroy(&attr);
		return app_shellFail(err, n, "the firmware thread did not start", e);
	}
	s->haveFirmware = 1;
	e = pthread_create(&s->watchdog, NULL, app_shellWatchdog, s);
	pthread_attr_destroy(&attr);
	if (e != 0)
	{
		app_shellStop(s);
		return app_shellFail(err, n, "the watchdog thread did not start", e);
	}
	s->haveWatchdog = 1;
	return 0;
}

static void app_shellSay(app_shell_t *s, const char *text)
{
	if (s->o.message)
		s->o.message(s->o.ctx, text);
}

int app_shellFrame(app_shell_t *s)
{
	app_ack_t a;
	char line[300];
	while (app_engineAck(&s->engine, &a))
	{
		if (a.kind == APP_MSG_WARNING)
		{
			snprintf(s->warnings[s->warningsTotal++ % APP_SHELL_WARNINGS], sizeof s->warnings[0], "%s", a.text);
			snprintf(line, sizeof line, "warning: %s", a.text);
			app_shellSay(s, line);
		}
		else if (a.kind == APP_MSG_NOTICE)
		{
			snprintf(line, sizeof line, "notice: %s", a.text);
			app_shellSay(s, line);
		}
	}
	const int fresh = app_engineSnapshot(&s->engine, &s->snap);
	if (fresh)
		s->haveSnap = 1;
	if (!s->hungShown && atomic_load(&s->engine.hung))
	{
		s->hungShown = 1;
		app_shellSay(s, APP_SHELL_HANG);
	}
	return fresh;
}

const app_snapshot_t *app_shellSnapshot(const app_shell_t *s)
{
	return s->haveSnap ? &s->snap : NULL;
}

int app_shellPanel(app_shell_t *s, const app_panelEvent_t *ev)
{
	return app_enginePanel(&s->engine, ev) == 0 ? 0 : -1;
}

int app_shellCommand(app_shell_t *s, const app_cmd_t *c)
{
	return app_engineCommand(&s->engine, c) == 0 ? 0 : -1;
}

int app_shellDevices(app_shell_t *s, app_outputDevice_t *out, int max)
{
	(void)s;
	return app_caList(out, max);
}

int app_shellHung(const app_shell_t *s)
{
	return atomic_load(&s->engine.hung);
}

int app_shellWarnings(const app_shell_t *s)
{
	return (int)(s->warningsTotal < APP_SHELL_WARNINGS ? s->warningsTotal : APP_SHELL_WARNINGS);
}

const char *app_shellWarning(const app_shell_t *s, int i)
{
	const int n = app_shellWarnings(s);
	if (i < 0 || i >= n)
		return "";
	return s->warnings[(s->warningsTotal - (unsigned)n + (unsigned)i) % APP_SHELL_WARNINGS];
}

void app_shellTestFault(app_shell_t *s)
{
	atomic_store(&s->testFault, 1);
}

void app_shellStop(app_shell_t *s)
{
	if (!s->haveFirmware)
		return;
	app_cmd_t c;
	memset(&c, 0, sizeof c);
	c.kind = APP_CMD_STOP;
	for (int i = 0; i < 200 && app_engineCommand(&s->engine, &c) != 0; i++)
	{
		const struct timespec d = { 0, 5000000L };	/* a full command ring: the thread drains it */
		(void)nanosleep(&d, NULL);
	}
	for (int i = 0; i < 400 && !atomic_load(&s->stopped); i++)	/* up to 2 s */
	{
		const struct timespec d = { 0, 5000000L };
		(void)nanosleep(&d, NULL);
	}
	if (!atomic_load(&s->stopped))
		return;	/* a stuck thread: the process ends without it */
	(void)pthread_join(s->firmware, NULL);
	s->haveFirmware = 0;
	if (s->haveWatchdog)
	{
		(void)pthread_join(s->watchdog, NULL);	/* it looks at stopped every 100 ms */
		s->haveWatchdog = 0;
	}
}
