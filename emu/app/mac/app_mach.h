/*
 * app_mach.h
 *
 * The real host of the engine (plan E3): mach_absolute_time(), Mach
 * semaphores that the callback may signal, the time-constraint policy
 * (spec 6.1). The output's open and close come from app_coreaudio.c (Task 7).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_MACH_H_
#define APP_MACH_H_

#include <mach/mach.h>
#include <stdint.h>

#include "app_engine.h"

typedef struct app_mach
{
	semaphore_t fwWake, midiWake;
	double ticksPerSec;
	app_engine_t *engine;
	void *output;		/**< the app_ca_t of Task 7; NULL: no audio (--no-audio) */
	void *owner;		/**< the shell, for the host functions that it overrides */
	_Atomic double period;	/**< s: the firmware thread's period, the output buffer's duration; the firmware thread writes it, the MIDI-out thread follows it */
	int realtime;		/**< 1 if thread_policy_set() took the policy (E29) */
} app_mach_t;

int app_machInit(app_mach_t *m, app_engine_t *e);
void app_machWake(void *ctx);	/**< the callback's wake: semaphore_signal(fwWake); ctx is the app_mach_t */
void app_machHost(app_mach_t *m, app_host_t *h);
void app_machWaitSem(semaphore_t sem, uint64_t until, double ticksPerSec);
/** THREAD_TIME_CONSTRAINT_POLICY for the calling thread: period, 25 % computation, constraint = period */
int app_threadRealtime(double periodSec);
/** The same for a thread whose period changes (the output's buffer): applies the policy when periodSec is not *applied, and
 * stores it there (0 at the thread's start). 1 applied, 0 already for that period, -1 the system refused it (the thread
 * runs at default priority; it is not tried again for the same period, E29) */
int app_threadRealtimeFollow(double *applied, double periodSec);
double app_wallSeconds(void);	/**< CLOCK_MONOTONIC */
uint64_t app_seedNow(void);		/**< E27 */

#endif /* APP_MACH_H_ */
