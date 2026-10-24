/*
 * seq_core.c
 *
 * Timing core of the LICHOMAT sequencer, see seq_core.h for the interface and
 * its rules. Plain C11 without the HAL, interrupts or allocation, so the same
 * file builds in the firmware and in the host tests (tests/).
 *
 * Design: docs/superpowers/specs/2026-10-23-timing-core-design.md (the spec) and
 * docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md (N1-N11;
 * they win where the two differ).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */

#include "Soundengine/Sequencer/seq_core.h"

#include <string.h>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------
#define SEQC_NEVER		UINT64_MAX	/**< nextDue when nothing is scheduled */
#define SEQC_NO_TICK		INT64_MAX	/**< no item pending */

//------------------------------------------------------------------------------
// State
//------------------------------------------------------------------------------
/** The items of the timeline. At equal ticks they are handled in this order
 * (seq_core.h, "Processing order"). */
typedef enum
{
	SEQC_ITEM_NONE = 0,
	SEQC_ITEM_BOUNDARY,	/**< the next step boundary */
} seqc_item_t;

//------------------------------------------------------------------------------
/** time base, musical clock and transport */
typedef struct
{
	seqc_host_t	host;
	uint32_t	cyclesPerSecond;
	bool		busy;		/**< inside a seqc_ call that can run callbacks */
	bool		haveTime;	/**< seqc_process() has set the time */
	seqc_time_t	now;		/**< the core's current time */
	seqc_time_t	nextDue;	/**< N10: nothing is due before this time */

	double		masterBpm;
	double		ticksPerCycle;	/**< of the master tempo */
	double		cyclesPerTick;

	/* Internal sync (N1): the timeline is at tick
	 * anchorTick + (t - anchorTime) * ticksPerCycle at time t. It runs all the
	 * time. While the transport runs, the position is on it: a start re-anchors
	 * it at tick 0. */
	seqc_time_t	anchorTime;
	double		anchorTick;

	seqc_state_t	state;
	bool		startPending;	/**< internal start: step 0 at the next seqc_process() */
	bool		atStart;	/**< rewound, and nothing has played since */
	double		pos;		/**< the position handled so far, ticks */
	int32_t		absStep;	/**< n of the current step, -1 before the first */
	seqc_stats_t	stats;
} seqc_core_t;

//------------------------------------------------------------------------------
/* the state, one part per area */
static seqc_core_t seqc;

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------
/** ticks per cycle at bpm */
static double seqc_ticksPerCycle(double bpm)
{
	return bpm * SEQC_TICKS_PER_BEAT / 60.0 / (double)seqc.cyclesPerSecond;
}
//------------------------------------------------------------------------------
/** the absolute step of the next boundary */
static uint32_t seqc_nextBoundaryStep(void)
{
	return (uint32_t)(seqc.absStep + 1);
}

//------------------------------------------------------------------------------
// Position
//------------------------------------------------------------------------------
/** the internal timeline at time t (N1) */
static double seqc_internalTick(seqc_time_t t)
{
	return seqc.anchorTick + (double)(t - seqc.anchorTime) * seqc.ticksPerCycle;
}
//------------------------------------------------------------------------------
/** the time at which the internal timeline reaches tick (rounded down, so that
 * a process call at that time handles it or finds it just short) */
static seqc_time_t seqc_internalTime(seqc_tick_t tick)
{
	const double cycles = ((double)tick - seqc.anchorTick) * seqc.cyclesPerTick;
	if (cycles <= 0.0)
		return seqc.now;
	return seqc.anchorTime + (seqc_time_t)cycles;
}
//------------------------------------------------------------------------------
/** the position at time t, at or after the core's current time */
static double seqc_positionAt(seqc_time_t t)
{
	if (seqc.state != SEQC_RUNNING || seqc.startPending)
		return seqc.pos;
	const double p = seqc_internalTick(t);
	return p > seqc.pos ? p : seqc.pos;
}

//------------------------------------------------------------------------------
// Transport
//------------------------------------------------------------------------------
/** stop and keep the position (the caller has set it) */
static void seqc_stopTransport(void)
{
	seqc.state = SEQC_STOPPED;
	seqc.startPending = false;
	seqc.host.transport(seqc.host.ctx, SEQC_TRANSPORT_STOP);
}
//------------------------------------------------------------------------------
/** back to the start, before step 0 */
static void seqc_rewindTransport(void)
{
	const bool wasAtStart = seqc.atStart;

	seqc.absStep = -1;
	seqc.pos = 0.0;
	seqc.atStart = true;
	if (!wasAtStart)
		seqc.host.transport(seqc.host.ctx, SEQC_TRANSPORT_REWIND);
}
//------------------------------------------------------------------------------
/** local start (seqc_start()): from the beginning */
static void seqc_localStart(void)
{
	if (!seqc.atStart)
		seqc_rewindTransport();
	seqc.state = SEQC_RUNNING;
	seqc.startPending = true;
	seqc.host.transport(seqc.host.ctx, SEQC_TRANSPORT_START);
}

//------------------------------------------------------------------------------
// Step boundary
//------------------------------------------------------------------------------
/** the step boundary at tick (seq_core.h, "Processing order") */
static void seqc_boundary(seqc_tick_t tick)
{
	seqc.pos = (double)tick;
	seqc.absStep++;
	seqc.atStart = false;
	seqc.host.stepBoundary(seqc.host.ctx, (uint32_t)seqc.absStep);
}

//------------------------------------------------------------------------------
// Processing
//------------------------------------------------------------------------------
/** keep the earlier of two candidates; at equal ticks the lower item */
static void seqc_consider(seqc_item_t item, seqc_tick_t tick, uint8_t track,
		seqc_item_t *best, seqc_tick_t *bestTick, uint8_t *bestTrack)
{
	if (tick < *bestTick || (tick == *bestTick && item < *best))
	{
		*best = item;
		*bestTick = tick;
		*bestTrack = track;
	}
}
//------------------------------------------------------------------------------
/** the next item of the timeline: its kind, tick and track (note-offs) */
static seqc_item_t seqc_nextItem(seqc_tick_t *tick, uint8_t *track)
{
	seqc_item_t best = SEQC_ITEM_NONE;
	*tick = SEQC_NO_TICK;
	*track = 0;
	if (seqc.state == SEQC_RUNNING && !seqc.startPending)
		seqc_consider(SEQC_ITEM_BOUNDARY, (seqc_tick_t)seqc_nextBoundaryStep() * SEQC_TICKS_PER_STEP, 0,
				&best, tick, track);
	return best;
}
//------------------------------------------------------------------------------
/* A tick that rounding left a few ULPs short of the target still counts as due. The
 * tolerance is far above double rounding and far below one cycle in a long session. */
static inline double seqc_dueLimit(double target)
{
	return target + 1e-12 * (1.0 + (target > 0.0 ? target : -target));
}
//------------------------------------------------------------------------------
/** handle every item due at or before target, earliest first (spec 6.3) */
static void seqc_runTo(double target)
{
	for (;;)
	{
		seqc_tick_t tick;
		uint8_t track;
		const seqc_item_t item = seqc_nextItem(&tick, &track);
		if (item == SEQC_ITEM_NONE || (double)tick > seqc_dueLimit(target))
			break;
		switch (item)
		{
		case SEQC_ITEM_BOUNDARY:
			seqc_boundary(tick);
			break;
		default:
			return;
		}
	}
	if (seqc.state == SEQC_RUNNING && target > seqc.pos)
		seqc.pos = target;
}
//------------------------------------------------------------------------------
/** internal sync: a pending start begins here, then the timeline runs to now */
static void seqc_runInternal(void)
{
	if (seqc.startPending)
	{
		/* step 0 now */
		seqc.startPending = false;
		seqc.anchorTime = seqc.now;
		seqc.anchorTick = 0.0;
		seqc.pos = 0.0;
	}
	seqc_runTo(seqc_internalTick(seqc.now));
}
//------------------------------------------------------------------------------
/** move the core's time to t (never back) and handle everything due up to it */
static void seqc_advance(seqc_time_t t)
{
	if (!seqc.haveTime)
	{
		/* the first time: the internal timeline starts here */
		seqc.haveTime = true;
		seqc.now = t;
		seqc.anchorTime = t;
	}
	else if (t > seqc.now)
		seqc.now = t;
	seqc_runInternal();
}
//------------------------------------------------------------------------------
/** work out nextDue (N10) */
static void seqc_schedule(void)
{
	if (!seqc.haveTime || seqc.startPending)
	{
		seqc.nextDue = 0;
		return;
	}
	seqc_tick_t tick;
	uint8_t track;
	const seqc_item_t item = seqc_nextItem(&tick, &track);
	if (item == SEQC_ITEM_NONE)
		seqc.nextDue = SEQC_NEVER;
	else
		seqc.nextDue = seqc_internalTime(tick);
}

//------------------------------------------------------------------------------
// Interface
//------------------------------------------------------------------------------
/** Start of a seqc_ call that changes the state. A call from inside a callback
 * is ignored and counted (seq_core.h, "Execution context"). */
static bool seqc_enter(void)
{
	if (seqc.busy)
		return false;
	seqc.busy = true;
	seqc.nextDue = 0;	/* so that a nested seqc_process() reaches seqc_enter() */
	return true;
}
//------------------------------------------------------------------------------
/** end of such a call */
static void seqc_leave(void)
{
	seqc_schedule();
	seqc.busy = false;
}
//------------------------------------------------------------------------------
void seqc_init(const seqc_host_t *host, uint32_t cyclesPerSecond)
{
	memset(&seqc, 0, sizeof seqc);

	seqc.host = *host;
	seqc.cyclesPerSecond = cyclesPerSecond != 0 ? cyclesPerSecond : SEQC_CPU_HZ;
	seqc.masterBpm = SEQC_DEFAULT_BPM;
	seqc.ticksPerCycle = seqc_ticksPerCycle(seqc.masterBpm);
	seqc.cyclesPerTick = 1.0 / seqc.ticksPerCycle;
	seqc.state = SEQC_STOPPED;
	seqc.atStart = true;
	seqc.absStep = -1;
}
//------------------------------------------------------------------------------
void seqc_setSyncMode(seqc_sync_t mode)
{
	(void)mode;			/* internal sync only, for now */
}
//------------------------------------------------------------------------------
void seqc_setTempo(float bpm)
{
	if (!seqc_enter())
		return;
	double b = bpm;
	if (b < SEQC_MASTER_BPM_MIN)
		b = SEQC_MASTER_BPM_MIN;
	if (b > SEQC_MASTER_BPM_MAX)
		b = SEQC_MASTER_BPM_MAX;
	if (seqc.haveTime)
	{
		/* re-anchor, so that the timeline and the position stay continuous */
		seqc.anchorTick = seqc_internalTick(seqc.now);
		seqc.anchorTime = seqc.now;
	}
	seqc.masterBpm = b;
	seqc.ticksPerCycle = seqc_ticksPerCycle(b);
	seqc.cyclesPerTick = 1.0 / seqc.ticksPerCycle;
	seqc_leave();
}
//------------------------------------------------------------------------------
void seqc_setAnalogPpq(uint8_t ppq)
{
	(void)ppq;			/* internal sync only, for now */
}
//------------------------------------------------------------------------------
void seqc_start(void)
{
	if (!seqc_enter())
		return;
	if (seqc.state == SEQC_STOPPED)
		seqc_localStart();
	seqc_leave();
}
//------------------------------------------------------------------------------
void seqc_stop(void)
{
	if (!seqc_enter())
		return;
	if (seqc.state != SEQC_STOPPED)
	{
		seqc.pos = seqc_positionAt(seqc.now);
		seqc_stopTransport();
	}
	seqc_leave();
}
//------------------------------------------------------------------------------
void seqc_rewind(void)
{
	if (!seqc_enter())
		return;
	if (seqc.state != SEQC_RUNNING)
		seqc_rewindTransport();
	seqc_leave();
}
//------------------------------------------------------------------------------
void seqc_syncEvent(seqc_sync_ev_t ev, seqc_time_t stamp)
{
	(void)ev;			/* internal sync only, for now */
	(void)stamp;
}
//------------------------------------------------------------------------------
void seqc_songPosition(uint16_t sixteenths)
{
	(void)sixteenths;		/* not handled yet */
}
//------------------------------------------------------------------------------
void seqc_process(seqc_time_t now)
{
	if (now < seqc.nextDue)
	{
		/* N10: nothing is due; the time is kept for the queries */
		if (now > seqc.now)
			seqc.now = now;
		return;
	}
	if (!seqc_enter())
		return;
	seqc_advance(now);
	seqc_leave();
}
//------------------------------------------------------------------------------
void seqc_skipOnce(uint8_t track, uint8_t step)
{
	(void)track;			/* not handled yet */
	(void)step;
}
//------------------------------------------------------------------------------
uint8_t seqc_trackStep(uint8_t track)
{
	(void)track;			/* no track counters yet */
	return SEQC_NO_STEP;
}
//------------------------------------------------------------------------------
int32_t seqc_absStep(void)
{
	return seqc.absStep;
}
//------------------------------------------------------------------------------
uint8_t seqc_masterTrack(void)
{
	return 0;			/* no track counters yet */
}
//------------------------------------------------------------------------------
double seqc_position(void)
{
	return seqc.busy ? seqc.pos : seqc_positionAt(seqc.now);
}
//------------------------------------------------------------------------------
float seqc_stepPhase(void)
{
	return 0.0f;			/* not handled yet */
}
//------------------------------------------------------------------------------
float seqc_currentBpm(void)
{
	return (float)seqc.masterBpm;
}
//------------------------------------------------------------------------------
seqc_state_t seqc_state(void)
{
	return seqc.state;
}
//------------------------------------------------------------------------------
bool seqc_isRunning(void)
{
	return seqc.state != SEQC_STOPPED;
}
//------------------------------------------------------------------------------
void seqc_stats(seqc_stats_t *out)
{
	*out = seqc.stats;
}
//------------------------------------------------------------------------------
void seqc_resetStats(void)
{
	if (seqc.busy)
		return;
	memset(&seqc.stats, 0, sizeof seqc.stats);
}
