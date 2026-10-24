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
/** per-track step counters */
typedef struct
{
	int8_t		index[SEQC_NUM_TRACKS];	/**< track step, -1 before the first step */
	uint8_t		master;			/**< master track of the last boundary */
} seqc_tracks_t;

//------------------------------------------------------------------------------
/* the state, one part per area */
static seqc_core_t seqc;
static seqc_tracks_t seqc_trk;

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
/** a raw track length as the sequencer plays it: 0 is 16, above 64 is 64 */
static uint8_t seqc_effectiveLength(uint8_t raw)
{
	if (raw == 0)
		return SEQC_DEFAULT_LENGTH;
	return raw > SEQC_NUM_STEPS ? SEQC_NUM_STEPS : raw;
}
//------------------------------------------------------------------------------
/** the effective length of a track */
static uint8_t seqc_trackLength(uint8_t pattern, uint8_t track)
{
	return seqc_effectiveLength(seqc.host.trackLength(seqc.host.ctx, pattern, track));
}
//------------------------------------------------------------------------------
/** the master track of a pattern, as fw 1.7 seq_getLongestTrack(): the largest
 * raw length, the lowest track on a tie */
static uint8_t seqc_longestTrack(uint8_t pattern)
{
	uint8_t master = 0;
	uint8_t longest = seqc.host.trackLength(seqc.host.ctx, pattern, 0);
	for (uint8_t t = 1; t < SEQC_NUM_TRACKS; t++)
	{
		const uint8_t len = seqc.host.trackLength(seqc.host.ctx, pattern, t);
		if (len > longest)
		{
			longest = len;
			master = t;
		}
	}
	return master;
}
//------------------------------------------------------------------------------
/** the track index after index on a track of length len (fw 1.7 seq_nextStep()).
 * The master track wraps when this is 0. */
static int8_t seqc_nextIndex(int8_t index, uint8_t len)
{
	const int next = index + 1;
	return (next >= len || (next & 0x3f) == 0) ? 0 : (int8_t)next;
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
	memset(seqc_trk.index, -1, sizeof seqc_trk.index);
	seqc_trk.master = 0;
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
/** boundary steps 1 and 2: advance the counters, and call patternEnd() if the
 * master track wraps. Returns false if the song ended there. */
static bool seqc_advanceCounters(void)
{
	const uint8_t pattern = seqc.host.activePattern(seqc.host.ctx);
	const uint8_t master = seqc_longestTrack(pattern);
	const bool wrap = seqc_nextIndex(seqc_trk.index[master], seqc_trackLength(pattern, master)) == 0;

	seqc.absStep++;
	seqc.atStart = false;
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		seqc_trk.index[t] = seqc_nextIndex(seqc_trk.index[t], seqc_trackLength(pattern, t));
	if (!wrap)
		return true;
	switch (seqc.host.patternEnd(seqc.host.ctx))
	{
	case SEQC_PAT_CHANGED:
		memset(seqc_trk.index, 0, sizeof seqc_trk.index);
		return true;
	case SEQC_PAT_STOP:
		seqc_stopTransport();
		seqc_rewindTransport();
		return false;
	default:
		return true;
	}
}
//------------------------------------------------------------------------------
/** the step boundary at tick (seq_core.h, "Processing order") */
static void seqc_boundary(seqc_tick_t tick)
{
	seqc.pos = (double)tick;
	if (!seqc_advanceCounters())
		return;
	const uint32_t n = (uint32_t)seqc.absStep;
	const uint8_t pattern = seqc.host.activePattern(seqc.host.ctx);

	seqc_trk.master = seqc_longestTrack(pattern);
	seqc.host.stepBoundary(seqc.host.ctx, n);
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
 * tolerance, 1e-12 of the target, is far above double rounding; it grows by about 2e-4
 * cycles per second of play, so it stays below one cycle for the first hour after a start. */
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
	memset(&seqc_trk, 0, sizeof seqc_trk);

	seqc.host = *host;
	seqc.cyclesPerSecond = cyclesPerSecond != 0 ? cyclesPerSecond : SEQC_CPU_HZ;
	seqc.masterBpm = SEQC_DEFAULT_BPM;
	seqc.ticksPerCycle = seqc_ticksPerCycle(seqc.masterBpm);
	seqc.cyclesPerTick = 1.0 / seqc.ticksPerCycle;
	seqc.state = SEQC_STOPPED;
	seqc.atStart = true;
	seqc.absStep = -1;
	memset(seqc_trk.index, -1, sizeof seqc_trk.index);
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
	if (track >= SEQC_NUM_TRACKS || seqc_trk.index[track] < 0)
		return SEQC_NO_STEP;
	return (uint8_t)seqc_trk.index[track];
}
//------------------------------------------------------------------------------
int32_t seqc_absStep(void)
{
	return seqc.absStep;
}
//------------------------------------------------------------------------------
uint8_t seqc_masterTrack(void)
{
	return seqc_trk.master;
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
