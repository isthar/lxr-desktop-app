/*
 * app_mach.c
 *
 * Host time, Mach semaphores and the real-time policy for the engine's threads
 * (stage 1b spec 6.1, plan E3, E27, E29).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_mach.h"

#include <errno.h>
#include <mach/mach_time.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <sys/event.h>
#include <time.h>
#include <unistd.h>

#include "app_shell.h"	/* app_waitPid() is declared there: app_mach.h cannot be included from C++ (_Atomic) */

int app_machInit(app_mach_t *m, app_engine_t *e)
{
	mach_timebase_info_data_t tb;
	memset(m, 0, sizeof *m);
	m->engine = e;
	if (mach_timebase_info(&tb) != KERN_SUCCESS || tb.numer == 0u)
		return -1;
	m->ticksPerSec = 1e9 * (double)tb.denom / (double)tb.numer;
	if (semaphore_create(mach_task_self(), &m->fwWake, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS)
		return -1;
	if (semaphore_create(mach_task_self(), &m->midiWake, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS)
	{
		semaphore_destroy(mach_task_self(), m->fwWake);
		return -1;
	}
	m->period = 512.0 / 48000.0;
	return 0;
}

void app_machWake(void *ctx)
{
	(void)semaphore_signal(((app_mach_t *)ctx)->fwWake);
}

void app_machWaitSem(semaphore_t sem, uint64_t until, double ticksPerSec)
{
	const uint64_t now = mach_absolute_time();
	if (until <= now)
		return;
	double sec = (double)(until - now) / ticksPerSec;
	if (sec > 1e6)
		sec = 1e6;	/* (unsigned) of a larger value is undefined */
	const mach_timespec_t ts = { (unsigned)sec, (clock_res_t)((sec - floor(sec)) * 1e9) };
	(void)semaphore_timedwait(sem, ts);	/* a relative timeout */
}

static uint64_t app_hostNow(void *ctx)
{
	(void)ctx;
	return mach_absolute_time();
}

static void app_hostWaitAudio(void *ctx, uint64_t until)
{
	const app_mach_t *m = ctx;
	app_machWaitSem(m->fwWake, until, m->ticksPerSec);
}

static void app_hostSleepUntil(void *ctx, uint64_t until)
{
	(void)ctx;
	(void)mach_wait_until(until);
}

static int app_hostOutputOpen(void *ctx, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info)
{
	(void)ctx;
	(void)a;
	(void)req;
	(void)info;
	return -1;	/* no audio until Task 7 */
}

static void app_hostOutputClose(void *ctx)
{
	(void)ctx;
}

static void app_hostMidiOutWake(void *ctx)
{
	(void)semaphore_signal(((app_mach_t *)ctx)->midiWake);
}

void app_machHost(app_mach_t *m, app_host_t *h)
{
	*h = (app_host_t){ m, m->ticksPerSec, app_hostNow, app_hostWaitAudio, app_hostSleepUntil, app_hostOutputOpen,
		app_hostOutputClose, app_hostMidiOutWake };
}

int app_threadRealtime(double periodSec)
{
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	thread_time_constraint_policy_data_t p;	/* in mach ticks (spec 6.1) */
	p.period = (uint32_t)(periodSec * 1e9 * (double)tb.denom / (double)tb.numer);
	p.computation = p.period / 4u;
	p.constraint = p.period;
	p.preemptible = 1;
	return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
		(thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS ? 0 : -1;
}

double app_wallSeconds(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

uint64_t app_seedNow(void)	/* E27: never time() */
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ((uint64_t)ts.tv_sec << 30) ^ (uint64_t)ts.tv_nsec ^ mach_absolute_time();
}

int app_waitPid(int pid, double sec)
{
	if (pid <= 0)
		return -1;
	const int kq = kqueue();
	if (kq < 0)
		return -1;
	struct kevent ev;
	EV_SET(&ev, (uintptr_t)pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, NULL);
	int rc;
	if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0)
		rc = errno == ESRCH ? 0 : -1;	/* ESRCH: it has already gone */
	else
	{
		if (!(sec > 0.0))
			sec = 0.0;
		if (sec > 3600.0)
			sec = 3600.0;
		const struct timespec ts = { (time_t)sec, (long)((sec - floor(sec)) * 1e9) };
		struct kevent out;
		rc = kevent(kq, NULL, 0, &out, 1, &ts) > 0 ? 0 : -1;
	}
	close(kq);
	return rc;
}
