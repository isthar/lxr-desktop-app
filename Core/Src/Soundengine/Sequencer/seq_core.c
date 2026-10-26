/*
 * seq_core.c
 *
 * Timing core of the LICHOMAT sequencer, see seq_core.h for the interface and
 * its rules. Plain C11 without the HAL, interrupts or allocation, so the same
 * file builds in the firmware and in the host tests (tests/).
 *
 * Design: docs/superpowers/specs/2026-10-23-timing-core-design.md (the spec) and
 * docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md (N1-N21;
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
/* Entries of the event queue. 7 tracks cannot fill SEQC_QUEUE_SIZE: at most
 * about 170 events are pending (3 steps of 8 hits and a roll per track). The
 * host test of a full queue (T19) builds a copy of the core with a smaller one. */
#ifndef SEQC_QUEUE_CAPACITY
#define SEQC_QUEUE_CAPACITY	SEQC_QUEUE_SIZE
#endif
/* The most events one step boundary generates (seqc_boundary()), which the
 * queue's staging buffer holds (E1.18). Per track: one roll event; the late part
 * of step n, at most SEQC_MAX_HITS hits; and the early part of step n+1, its hits
 * before (n+1) * 96. The earliest hit is SEQC_MAX_SHIFT_TICKS before that (the
 * swing only delays), and k hits are 96 / k ticks apart, so at most
 * ceil(84 * k / 96) of them are early: 7 for x8, fewer for smaller k. */
#define SEQC_MAX_HITS		8u
#define SEQC_EARLY_HITS		((SEQC_MAX_SHIFT_TICKS * SEQC_MAX_HITS + SEQC_TICKS_PER_STEP - 1u) / SEQC_TICKS_PER_STEP)
#define SEQC_STAGE_SIZE		(SEQC_NUM_TRACKS * (1u + SEQC_MAX_HITS + SEQC_EARLY_HITS))
_Static_assert(SEQC_EARLY_HITS == 7u && SEQC_STAGE_SIZE == 112u, "a boundary generates at most 7 x (1 + 8 + 7) events");
_Static_assert(SEQC_STAGE_SIZE <= 255u, "the staging indices are 8 bits");
_Static_assert(SEQC_NUM_TRACKS <= 8u, "seqc_queue_t.chokeTracks has a bit per track");
//------------------------------------------------------------------------------
/* The tempo fit (N21), see the Tempo estimate section. The windows, the outlier
 * band and the restart counts are in seq_core.h. The tunables can be set with
 * -D for host measurements. */
/** ring points: a long window, the next point, and the SEQC_TEMPO_RESTART - 1 more
 * that a restart writes, so that a restart leaves the points of the fit before it
 * intact and a joining clock can undo it (seqc_tempoClock()) */
#define SEQC_TEMPO_RING		(SEQC_TEMPO_LONG_WINDOW + SEQC_TEMPO_RESTART + 1u)
#define SEQC_TEMPO_SPAN_S	16u	/**< the long window holds at most 16 s, so 32-bit stamps suffice in the ring */
#ifndef SEQC_TEMPO_BAND_STEP
#define SEQC_TEMPO_BAND_STEP	1.25	/**< the outlier band is at least this many step amplitudes */
#endif
#ifndef SEQC_TEMPO_SWITCH_STEP
#define SEQC_TEMPO_SWITCH_STEP	1.5	/**< the windows' slopes may differ by this many step bounds */
#endif
#ifndef SEQC_TEMPO_STEP_DECAY
#define SEQC_TEMPO_STEP_DECAY	(1.0 / 64.0)	/**< of the step amplitude, per point */
#endif
#ifndef SEQC_TEMPO_DRIFT_GAIN
#define SEQC_TEMPO_DRIFT_GAIN	0.25	/**< of the mean interval deviation, per point */
#endif
_Static_assert(SEQC_TEMPO_RING <= 255u, "the ring indexes are 8 bits");

//------------------------------------------------------------------------------
// State
//------------------------------------------------------------------------------
/** The items of the timeline. At equal ticks they are handled in this order
 * (seq_core.h, "Processing order"). */
typedef enum
{
	SEQC_ITEM_NONE = 0,
	SEQC_ITEM_TEMPO_STEP,	/**< tempoStep(), internal sync */
	SEQC_ITEM_BOUNDARY,	/**< the next step boundary */
	SEQC_ITEM_NOTE_OFF,	/**< a pending note-off */
	SEQC_ITEM_EVENT,	/**< the next queued event */
	SEQC_ITEM_PULSE,	/**< clockOutPulse(), while running */
	SEQC_ITEM_MIDI_CLOCK,	/**< midiClockOut(), internal sync */
} seqc_item_t;

//------------------------------------------------------------------------------
/** time base, musical clock and transport */
typedef struct
{
	seqc_host_t	host;
	uint32_t	cyclesPerSecond;
	bool		busy;		/**< inside a seqc_ call that can run callbacks */
	bool		haveTime;	/**< seqc_process() or seqc_syncEvent() has set the time */
	seqc_time_t	now;		/**< the core's current time */
	seqc_time_t	nextDue;	/**< N10: nothing is due before this time */

	seqc_sync_t	mode;
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
	uint8_t		master;			/**< master track of the pattern that plays from the last boundary on */
	bool		seeked;			/**< after a song position: the next boundary is absStep itself */
} seqc_tracks_t;

//------------------------------------------------------------------------------
/** one step occurrence (N3): its probability draw, and the timing and result of
 * its early part */
typedef struct
{
	bool		valid;		/**< the slot holds occurrence (absStep, pattern, step) */
	bool		drawn;		/**< r is drawn */
	bool		timed;		/**< the early part was evaluated: played, hits and offset are set */
	bool		played;		/**< the early part passed stepOn() and the draw */
	uint32_t	absStep;
	uint8_t		pattern;
	uint8_t		step;
	uint8_t		r;		/**< 1..100 */
	uint8_t		hits;		/**< the ratchet hits it read */
	int16_t		offset;		/**< the swing + shift it read, ticks after absStep * 96 */
} seqc_draw_t;

//------------------------------------------------------------------------------
/** event generation */
typedef struct
{
	seqc_draw_t	draw[SEQC_NUM_TRACKS][2];	/**< N3: slots for n and n+1, by absStep & 1 */
	uint8_t		swing;				/**< swing ticks read at the last boundary */
} seqc_gen_t;

//------------------------------------------------------------------------------
/** the event queue: a ring, earliest first (N2). The event at logical index i,
 * 0 being the next to fire, is in events[(head + i) % SEQC_QUEUE_CAPACITY]. */
typedef struct
{
	seqc_event_t	events[SEQC_QUEUE_CAPACITY];
	uint16_t	head;		/**< slot of the next event to fire */
	uint16_t	count;		/**< with the events of recorded chokes, until they are compacted */
	uint8_t		chokeTracks;	/**< bit t: track t has a recorded choke (seqc_queueChoke()) */
	uint32_t	chokeStep[SEQC_NUM_TRACKS];	/**< it removes the track's events below this absolute step */
} seqc_queue_t;

//------------------------------------------------------------------------------
/** the events of one step boundary, before they join the queue (E1.18) */
typedef struct
{
	seqc_event_t	events[SEQC_STAGE_SIZE];	/**< in the order they were generated */
	/** indices into events, in firing order, in order[first .. first + count - 1].
	 * It starts in the middle and grows at either end. */
	uint8_t		order[2 * SEQC_STAGE_SIZE];
	uint8_t		first;
	uint8_t		count;
} seqc_stage_t;

//------------------------------------------------------------------------------
/** per-track firing state: note-off and the same-tick rule (N2) */
typedef struct
{
	bool		offPending[SEQC_NUM_TRACKS];
	seqc_tick_t	offTick[SEQC_NUM_TRACKS];
	bool		fired[SEQC_NUM_TRACKS];		/**< lastTick and lastStep are valid */
	seqc_tick_t	lastTick[SEQC_NUM_TRACKS];
	uint32_t	lastStep[SEQC_NUM_TRACKS];
} seqc_fire_t;

//------------------------------------------------------------------------------
/** clock outputs and the tempo step (N8) */
typedef struct
{
	seqc_tick_t	nextMidiClock;	/**< internal timeline */
	seqc_tick_t	nextTempoStep;	/**< internal timeline */
	seqc_tick_t	nextPulse;	/**< position */
} seqc_outputs_t;

//------------------------------------------------------------------------------
/** a skip-once marker (D6); each track has one (N18.1) */
typedef struct
{
	bool		valid;
	uint8_t		pattern;
	uint8_t		step;
	uint32_t	absStep;	/**< the occurrence it applies to */
} seqc_skip_t;

//------------------------------------------------------------------------------
/** how an armed transport starts */
typedef enum
{
	SEQC_ARM_STEP_CLOCK = 0,	/**< local start: with the next clock that begins a step */
	SEQC_ARM_NEXT_CLOCK,		/**< MIDI Start or Continue: with the next clock */
} seqc_arm_t;

//------------------------------------------------------------------------------
/** a point of the tempo fit (N21): the last clock of a transfer, the clocks
 * that share a stamp */
typedef struct
{
	uint64_t	stamp;
	uint32_t	clock;		/**< the fit's own clock count */
} seqc_point_t;

//------------------------------------------------------------------------------
/** least-squares sums of points (x, y): x in clocks and y in cycles from the
 * base point. Every value is an integer below 2^53, so the sums are exact. */
typedef struct
{
	double		n, sx, sy, sxx, sxy;
} seqc_sums_t;

//------------------------------------------------------------------------------
/** what the fit did with the point of a transfer */
typedef enum
{
	SEQC_POINT_USED,	/**< in the fit, tested or not */
	SEQC_POINT_LEFT_OUT,	/**< an outlier, counted in intervalsExcluded */
	SEQC_POINT_RESTARTED,	/**< it completed a tempo jump: the fit restarted (N16) */
} seqc_pointFate_t;

/** what seqc_fitDoubleTrigger() did on the jack (E1.19) */
typedef enum
{
	SEQC_SPLIT_NONE,	/**< no double trigger */
	SEQC_SPLIT_MERGED,	/**< the pulse between left out, p's interval merged */
	SEQC_SPLIT_BOUNCE,	/**< p left out */
} seqc_split_t;

//------------------------------------------------------------------------------
/** the tempo fit (N9, N16, N21) */
typedef struct
{
	seqc_sums_t	lng;		/**< long window: the points of the last SEQC_TEMPO_LONG_WINDOW clocks */
	seqc_sums_t	sht;		/**< short window: the points of the last SEQC_TEMPO_WINDOW clocks */
	uint32_t	baseClock;	/**< the base point, x = y = 0: the oldest of the long window */
	uint64_t	baseStamp;
	uint8_t		head;		/**< ring index of the next point */
	uint8_t		tailL;		/**< ring index of the oldest point of the long window */
	uint8_t		tailS;		/**< ring index of the oldest point of the short window */
	bool		havePrev;
	seqc_point_t	prev;		/**< the newest point, in the windows or not */
	seqc_point_t	row[SEQC_TEMPO_RESTART];	/**< N16: the outliers in a row */
	uint8_t		rowCount;
	bool		rowLate;	/**< they lie after the fitted line (else before it) */
	double		rowResidual;	/**< residual of the row's first outlier, cycles */
	double		stepBeforeRow;	/**< the step amplitude before it */
	int64_t		phase;		/**< jack: cycles taken off the stamps, the deviations of the intervals left out */
	double		drift;		/**< mean of interval - slope x clocks over the recent points, cycles */
	double		lastOff;	/**< the newest point's interval - slope x clocks, less the drift */
	bool		haveLastOff;
	double		step;		/**< step amplitude: decaying peak of the steps, cycles */
	double		slope;		/**< the estimate, cycles per clock; 0 while there is none */
	double		split;		/**< jack: the newest interval less splitSlope if it may be part of a double trigger (< 0), else 0 */
	double		splitSlope;	/**< the slope it was tested against */
	bool		steady;		/**< jack: the newest interval was within a quarter band of the slope, not merged */
	uint32_t	extraClocks;	/**< spurious clocks the fit leaves out of its count (E1.19) */
} seqc_fit_t;

//------------------------------------------------------------------------------
/** slave modes: clock count, tempo estimate and the interpolation (N9) */
typedef struct
{
	uint8_t		ppq;		/**< clock in jack: 4 or 24 */
	seqc_arm_t	arm;
	uint32_t	clockCount;	/**< clocks since the last reset of the count */
	bool		haveStamp;
	seqc_time_t	lastStamp;	/**< stamp of the last clock, in any state */
	double		lastClockPos;	/**< position of the last clock while running, ticks */
	seqc_time_t	lastClockTime;	/**< its stamp */
	bool		droppedOut;	/**< no clock for the dropout time (N15): the position holds */
	double		bpm;		/**< the estimate, or the last known tempo */
	double		ticksPerCycle;	/**< of bpm */
	double		cyclesPerTick;
	uint32_t	fitClock;	/**< clocks counted by the tempo fit */
	bool		transferOpen;	/**< a transfer is being collected (N21) */
	seqc_point_t	transfer;	/**< its stamp, and its last clock so far */
	bool		joinable;	/**< a MIDI clock less than SEQC_TEMPO_JOIN_MS after the last transfer joins it */
	bool		joinLeftOut;	/**< that transfer was left out as an outlier */
	bool		joinRestarted;	/**< that transfer restarted the fit */
	bool		joinDropped;	/**< that transfer took a point of the fit out (seqc_fitDropNewest()) */
	uint8_t		dropSlot;	/**< the ring slot of the last point taken out, and what it held */
	uint32_t	dropClock;
	uint32_t	dropStamp;
	seqc_fit_t	fitBefore;	/**< the fit before the last MIDI transfer, for a clock that joins it; the jack: before a merged double trigger */
	bool		mergeTentative;	/**< the jack: the last pulse merged a double trigger, which the next one may undo (seqc_fitUndoMerge()) */
	seqc_point_t	mergePoint;	/**< that pulse, in the position's count */
	uint32_t	mergeExcluded;	/**< intervalsExcluded before it */
	seqc_fit_t	fit;		/**< the tempo fit (N9, N16, N21) */
	uint32_t	ringClock[SEQC_TEMPO_RING];	/**< the points of the long window */
	uint32_t	ringStamp[SEQC_TEMPO_RING];	/**< their stamps, low 32 bits */
} seqc_slave_t;
_Static_assert(SEQC_TEMPO_RESTART_4PPQ <= SEQC_TEMPO_RESTART, "row[] holds a row at either threshold (N16)");

//------------------------------------------------------------------------------
/* the state, one part per area */
static seqc_core_t seqc;
static seqc_tracks_t seqc_trk;
static seqc_gen_t seqc_gen;
static seqc_queue_t seqc_queue;
static seqc_stage_t seqc_stage;
static seqc_fire_t seqc_fire;
static seqc_outputs_t seqc_out;
static seqc_skip_t seqc_skip[SEQC_NUM_TRACKS];
static seqc_slave_t seqc_sl;

#ifdef SEQC_QUEUE_TEST
uint32_t seqc_queueMoves;
#define SEQC_QUEUE_MOVED(n)	(seqc_queueMoves += (uint32_t)(n))
#else
#define SEQC_QUEUE_MOVED(n)	((void)0)
#endif

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
	return (uint32_t)(seqc_trk.seeked ? seqc.absStep : seqc.absStep + 1);
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
/** swing ticks of a shuffle value: round(shuffle * 64 / 127) */
static uint8_t seqc_swingTicks(uint8_t shuffle)
{
	if (shuffle > SEQC_MAX_SHUFFLE)
		shuffle = SEQC_MAX_SHUFFLE;
	return (uint8_t)((shuffle * SEQC_MAX_SWING_TICKS + SEQC_MAX_SHUFFLE / 2) / SEQC_MAX_SHUFFLE);
}
//------------------------------------------------------------------------------
/** hits of StepTiming.flam: 0..5 give 1, 2, 3, 4, 6, 8; any other value 1 */
static uint8_t seqc_hitCount(uint8_t flam)
{
	static const uint8_t hits[6] = { 1, 2, 3, 4, 6, SEQC_MAX_HITS };
	return flam < 6 ? hits[flam] : 1;
}
//------------------------------------------------------------------------------
/** ticks of StepTiming.shift: 0..14 are -7..+7 units, 15 counts as 0 */
static int seqc_shiftTicks(uint8_t shift)
{
	if (shift > 14)
		return 0;
	return ((int)shift - (int)SEQC_SHIFT_CENTER) * (int)SEQC_SHIFT_UNIT_TICKS;
}
//------------------------------------------------------------------------------
/** floor() for ticks; seq_core does not use <math.h> (spec 4.1) */
static seqc_tick_t seqc_floor(double x)
{
	seqc_tick_t i = (seqc_tick_t)x;
	if ((double)i > x)
		i--;
	return i;
}
//------------------------------------------------------------------------------
/** the first multiple of m above x */
static seqc_tick_t seqc_nextMultiple(double x, seqc_tick_t m)
{
	const seqc_tick_t i = seqc_floor(x);
	seqc_tick_t q = i / m;
	if (i % m != 0 && i < 0)
		q--;
	return (q + 1) * m;
}
//------------------------------------------------------------------------------
/** ticks of one clock: a MIDI clock or a jack pulse */
static double seqc_ticksPerClock(void)
{
	if (seqc.mode == SEQC_SYNC_ANALOG && seqc_sl.ppq != 24)
		return SEQC_TICKS_PER_STEP;
	return SEQC_TICKS_PER_MIDI_CLOCK;
}
//------------------------------------------------------------------------------
/** clocks of one step: 6, or 1 for the jack at 4 PPQ */
static uint32_t seqc_clocksPerStep(void)
{
	if (seqc.mode == SEQC_SYNC_ANALOG && seqc_sl.ppq != 24)
		return 1;
	return SEQC_MIDI_CLOCKS_PER_STEP;
}
//------------------------------------------------------------------------------
/** the position before step 0: 0 in internal sync, minus one clock in the slave
 * modes, so that the next clock brings it to 0 */
static double seqc_startPosition(void)
{
	return seqc.mode == SEQC_SYNC_INTERNAL ? 0.0 : -seqc_ticksPerClock();
}
//------------------------------------------------------------------------------
/** the dropout time (N15): SEQC_DROPOUT_MS, or two expected clock intervals if
 * that is longer. Without an estimate the interval is the one at
 * SEQC_SLAVE_BPM_MIN, so that a slow clock can still build its first estimate. */
static seqc_time_t seqc_dropoutCycles(void)
{
	const double cyclesPerTick = seqc_sl.fit.slope > 0.0 ? seqc_sl.cyclesPerTick
			: 1.0 / seqc_ticksPerCycle(SEQC_SLAVE_BPM_MIN);
	const double twoClocks = 2.0 * seqc_ticksPerClock() * cyclesPerTick;
	const double minimum = (double)seqc.cyclesPerSecond * SEQC_DROPOUT_MS / 1000.0;
	return (seqc_time_t)(twoClocks > minimum ? twoClocks : minimum);
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
/** slave modes, while running: the position at time t (N9). It is interpolated
 * from the last clock at the estimated tempo, clamped to the next clock and never
 * goes back. After a dropout it holds. */
static double seqc_slaveTarget(seqc_time_t t)
{
	if (seqc_sl.droppedOut)
		return seqc.pos;
	const double limit = seqc_sl.lastClockPos + seqc_ticksPerClock();
	double p = seqc_sl.lastClockPos;
	if (t > seqc_sl.lastClockTime)
		p += (double)(t - seqc_sl.lastClockTime) * seqc_sl.ticksPerCycle;
	if (p > limit)
		p = limit;
	return p > seqc.pos ? p : seqc.pos;
}
//------------------------------------------------------------------------------
/** the position at time t, at or after the core's current time */
static double seqc_positionAt(seqc_time_t t)
{
	if (seqc.state != SEQC_RUNNING || seqc.startPending)
		return seqc.pos;
	const double p = seqc.mode == SEQC_SYNC_INTERNAL ? seqc_internalTick(t) : seqc_slaveTarget(t);
	return p > seqc.pos ? p : seqc.pos;
}

//------------------------------------------------------------------------------
// Event queue
//------------------------------------------------------------------------------
/*
 * The queue (N2, amended for tasks E1.16 and E1.18) is a ring, earliest first,
 * and the events of a step boundary join it together:
 * - generation stages them (seqc_queueStage()), keeping an index of the staged
 *   events in firing order;
 * - at the end of the boundary, seqc_queueMerge() merges them into the ring in
 *   one pass from the nearer end, so a boundary writes O(new + pending)
 *   elements;
 * - a choke only records the step below which its track's events are removed;
 *   those events are dropped when they reach the head, and the ring is
 *   compacted once, from the nearer end, before the next events are staged and
 *   at the end of the call (seqc_leave()).
 * Every element is moved with a struct copy (newlib-nano's memmove copies
 * bytes).
 */
/** true if event a fires before event b (N2) */
static bool seqc_firesBefore(const seqc_event_t *a, const seqc_event_t *b)
{
	if (a->tick != b->tick)
		return a->tick < b->tick;
	if (a->absStep != b->absStep)
		return a->absStep > b->absStep;
	if ((a->flags & SEQC_EVF_ROLL) != (b->flags & SEQC_EVF_ROLL))
		return (a->flags & SEQC_EVF_ROLL) == 0;
	return a->track < b->track;
}
//------------------------------------------------------------------------------
/** the slot of logical index i, i <= SEQC_QUEUE_CAPACITY - 1 */
static uint16_t seqc_queueSlot(uint16_t i)
{
	const uint32_t s = (uint32_t)seqc_queue.head + i;
	return (uint16_t)(s >= SEQC_QUEUE_CAPACITY ? s - SEQC_QUEUE_CAPACITY : s);
}
//------------------------------------------------------------------------------
/** the event at logical index i */
static seqc_event_t *seqc_queueAt(uint16_t i)
{
	return &seqc_queue.events[seqc_queueSlot(i)];
}
//------------------------------------------------------------------------------
/** the slots after and before slot s */
static uint16_t seqc_slotAfter(uint16_t s)
{
	return s + 1u < SEQC_QUEUE_CAPACITY ? (uint16_t)(s + 1u) : 0u;
}
static uint16_t seqc_slotBefore(uint16_t s)
{
	return s > 0 ? (uint16_t)(s - 1u) : (uint16_t)(SEQC_QUEUE_CAPACITY - 1u);
}
//------------------------------------------------------------------------------
/** the number of pending events that fire before ev: ev goes after them, and
 * before the pending events with the same key */
static uint16_t seqc_queueFiresBefore(const seqc_event_t *ev)
{
	uint16_t lo = 0;
	uint16_t hi = seqc_queue.count;
	if (hi > 0 && seqc_firesBefore(seqc_queueAt((uint16_t)(hi - 1u)), ev))
		return hi;				/* the common case: after every pending one */
	while (lo < hi)
	{
		const uint16_t mid = (uint16_t)((lo + hi) / 2u);
		if (seqc_firesBefore(seqc_queueAt(mid), ev))
			lo = (uint16_t)(mid + 1u);
		else
			hi = mid;
	}
	return lo;
}
//------------------------------------------------------------------------------
/** Merge the staged events into the ring, in one pass from one end (N2):
 * - a merge from the tail moves every pending event from the first staged one's
 *   place to the tail (those that do not fire before it) towards the tail; in
 *   the common case that is none, and the merge is an append;
 * - a merge from the head moves every pending event before the last staged one's
 *   place (those that fire before it) towards the head;
 * - it takes whichever of the two groups is smaller.
 * Each element is written once. Of events with the same key, the staged ones
 * fire first, so the order is that of one insert per event in the order they
 * were staged (N2). */
static void seqc_queueMerge(void)
{
	const uint16_t k = seqc_stage.count;
	if (k == 0)
		return;
	seqc_stage.count = 0;
	const uint16_t m = seqc_queue.count;
	const seqc_event_t *const staged = seqc_stage.events;
	const uint8_t *const order = &seqc_stage.order[seqc_stage.first];
	/* pending events [0, before) fire before the first staged one, and events
	 * [after, m) not before the last one */
	const uint16_t before = seqc_queueFiresBefore(&staged[order[0]]);
	const uint16_t after = seqc_queueFiresBefore(&staged[order[k - 1u]]);

	if (m - before <= after)
	{
		/* from the tail: the latest of the two goes to the last free slot */
		uint16_t dst = seqc_queueSlot((uint16_t)(m + k - 1u));
		uint16_t src = seqc_queueSlot(m > 0 ? (uint16_t)(m - 1u) : 0u);
		uint16_t pending = (uint16_t)(m - before);
		uint16_t j = (uint16_t)(k - 1u);
		for (;;)
		{
			const seqc_event_t *ev = &staged[order[j]];
			if (pending > 0 && !seqc_firesBefore(&seqc_queue.events[src], ev))
			{
				seqc_queue.events[dst] = seqc_queue.events[src];
				src = seqc_slotBefore(src);
				pending--;
			}
			else
			{
				seqc_queue.events[dst] = *ev;
				if (j == 0)
					break;
				j--;
			}
			SEQC_QUEUE_MOVED(1u);
			dst = seqc_slotBefore(dst);
		}
		SEQC_QUEUE_MOVED(1u);
	}
	else
	{
		/* from the head: the ring starts k slots earlier, which are free */
		uint16_t src = seqc_queue.head;
		uint16_t dst = seqc_queueSlot((uint16_t)(SEQC_QUEUE_CAPACITY - k));
		seqc_queue.head = dst;
		uint16_t pending = after;
		uint16_t j = 0;
		for (;;)
		{
			const seqc_event_t *ev = &staged[order[j]];
			if (pending > 0 && seqc_firesBefore(&seqc_queue.events[src], ev))
			{
				seqc_queue.events[dst] = seqc_queue.events[src];
				src = seqc_slotAfter(src);
				pending--;
			}
			else
			{
				seqc_queue.events[dst] = *ev;
				if (++j == k)
					break;
			}
			SEQC_QUEUE_MOVED(1u);
			dst = seqc_slotAfter(dst);
		}
		SEQC_QUEUE_MOVED(1u);
	}
	seqc_queue.count = (uint16_t)(m + k);
}
//------------------------------------------------------------------------------
/** true if a choke that is not compacted yet removes ev (seqc_queueChoke()) */
static bool seqc_isChoked(const seqc_event_t *ev, const void *arg)
{
	(void)arg;
	return (seqc_queue.chokeTracks & (1u << ev->track)) != 0 && ev->absStep < seqc_queue.chokeStep[ev->track];
}
//------------------------------------------------------------------------------
/** Remove the events for which drop() is true, keeping the order of the rest,
 * from the end nearer to them: the kept events after the first removed one move
 * towards the head, or those before the last removed one towards the tail,
 * whichever are fewer. Nothing is written if nothing is removed. Returns how many
 * were removed. */
static uint16_t seqc_queueRemoveIf(bool (*drop)(const seqc_event_t *ev, const void *arg), const void *arg)
{
	const uint16_t m = seqc_queue.count;
	uint16_t first = 0;
	while (first < m && !drop(seqc_queueAt(first), arg))
		first++;
	if (first == m)
		return 0;
	uint16_t last = (uint16_t)(m - 1u);
	while (!drop(seqc_queueAt(last), arg))
		last--;
	uint16_t kept;
	if (m - 1u - first <= last)
	{
		kept = first;
		for (uint16_t i = (uint16_t)(first + 1u); i < m; i++)
		{
			const seqc_event_t *ev = seqc_queueAt(i);
			if (drop(ev, arg))
				continue;
			*seqc_queueAt(kept++) = *ev;
			SEQC_QUEUE_MOVED(1u);
		}
	}
	else
	{
		/* the kept events of [0, last) move to the end of [0, last], and the
		 * ring then starts at the first of them */
		uint16_t dst = last;
		for (uint16_t i = last; i-- > 0;)
		{
			const seqc_event_t *ev = seqc_queueAt(i);
			if (drop(ev, arg))
				continue;
			*seqc_queueAt(dst--) = *ev;
			SEQC_QUEUE_MOVED(1u);
		}
		kept = (uint16_t)(m - 1u - dst);
		seqc_queue.head = seqc_queueSlot((uint16_t)(dst + 1u));
	}
	seqc_queue.count = kept;
	return (uint16_t)(m - kept);
}
//------------------------------------------------------------------------------
/** compact the chokes recorded since the last compaction (seqc_queueChoke()) */
static void seqc_queueCompact(void)
{
	if (seqc_queue.chokeTracks == 0)
		return;
	seqc.stats.chokeDrops += seqc_queueRemoveIf(seqc_isChoked, NULL);
	seqc_queue.chokeTracks = 0;
}
//------------------------------------------------------------------------------
/** Stage ev, generated at a step boundary; seqc_queueMerge() queues it. A queue
 * that would be full with the staged events rejects it: the same events as one
 * insert per event would reject, the boundary's last ones in the order they
 * were generated. */
static void seqc_queueStage(const seqc_event_t *ev)
{
	seqc_queueCompact();			/* the choked events do not count */
	if (seqc_queue.count + seqc_stage.count >= SEQC_QUEUE_CAPACITY)
	{
		seqc.stats.queueOverflow++;
		return;
	}
	if (seqc_stage.count == SEQC_STAGE_SIZE)
		seqc_queueMerge();		/* more than a boundary generates (the test hook) */
	if (seqc_stage.count == 0)
		seqc_stage.first = SEQC_STAGE_SIZE;
	const uint8_t i = seqc_stage.count++;
	seqc_stage.events[i] = *ev;
	SEQC_QUEUE_MOVED(1u);
	/* its place in firing order: after the staged events that fire before it,
	 * before those with the same key. The indices on the shorter side of it
	 * move, as in the ring: a boundary generates the late hits of step n before
	 * the early hits of step n+1, which fire first. */
	uint8_t *const order = &seqc_stage.order[seqc_stage.first];
	uint8_t lo = 0;
	uint8_t hi = i;
	if (hi > 0 && seqc_firesBefore(&seqc_stage.events[order[hi - 1u]], ev))
		lo = hi;
	while (lo < hi)
	{
		const uint8_t mid = (uint8_t)((lo + hi) / 2u);
		if (seqc_firesBefore(&seqc_stage.events[order[mid]], ev))
			lo = (uint8_t)(mid + 1u);
		else
			hi = mid;
	}
	if (lo < i - lo)
	{
		/* first > 0: it started at SEQC_STAGE_SIZE, and at most that many are staged */
		for (uint8_t j = 0; j < lo; j++)
			order[j - 1] = order[j];
		order[lo - 1] = i;
		seqc_stage.first--;
	}
	else
	{
		for (uint8_t j = i; j > lo; j--)
			order[j] = order[j - 1u];
		order[lo] = i;
	}
	const uint16_t depth = (uint16_t)(seqc_queue.count + seqc_stage.count);
	if (depth > seqc.stats.queueMax)
		seqc.stats.queueMax = depth;
}
//------------------------------------------------------------------------------
/** the next event to fire, or NULL if the queue is empty; events a choke
 * removed are dropped here */
static const seqc_event_t *seqc_queueFront(void)
{
	while (seqc_queue.count > 0)
	{
		const seqc_event_t *ev = &seqc_queue.events[seqc_queue.head];
		if (!seqc_isChoked(ev, NULL))
			return ev;
		seqc_queue.head = seqc_queueSlot(1u);
		seqc_queue.count--;
		seqc.stats.chokeDrops++;
	}
	return NULL;
}
//------------------------------------------------------------------------------
/** take the next event; the queue is not empty */
static seqc_event_t seqc_queuePop(void)
{
	const seqc_event_t ev = seqc_queue.events[seqc_queue.head];
	seqc_queue.head = seqc_queueSlot(1u);
	seqc_queue.count--;
	return ev;
}
//------------------------------------------------------------------------------
/** choke of a fired event (track, absStep): the queued events of track with an
 * absolute step below absStep are removed. They are dropped when they reach the
 * head or at the next compaction (seqc_queueCompact()), and counted then. */
static void seqc_queueChoke(uint8_t track, uint32_t absStep)
{
	const uint8_t bit = (uint8_t)(1u << track);
	if ((seqc_queue.chokeTracks & bit) == 0 || absStep > seqc_queue.chokeStep[track])
		seqc_queue.chokeStep[track] = absStep;
	seqc_queue.chokeTracks |= bit;
}
//------------------------------------------------------------------------------
/** true if ev is a step hit of the occurrence (track, absStep, pattern, step) of
 * the event arg */
static bool seqc_isStepHit(const seqc_event_t *ev, const void *arg)
{
	const seqc_event_t *o = arg;
	return ev->track == o->track && ev->absStep == o->absStep && ev->pattern == o->pattern
			&& ev->step == o->step && (ev->flags & SEQC_EVF_ROLL) == 0;
}
//------------------------------------------------------------------------------
/** remove the queued step hits of one occurrence (skip-once). It can be called
 * from a callback, while chokes are recorded or events are staged. */
static void seqc_queueRemoveStep(uint8_t track, uint32_t absStep, uint8_t pattern, uint8_t step)
{
	seqc_event_t o;
	memset(&o, 0, sizeof o);
	o.track = track;
	o.absStep = absStep;
	o.pattern = pattern;
	o.step = step;
	seqc_queueCompact();
	seqc_queueMerge();
	seqc_queueRemoveIf(seqc_isStepHit, &o);
}
#ifdef SEQC_QUEUE_TEST
//------------------------------------------------------------------------------
void seqc_testQueueInsert(const seqc_event_t *ev)
{
	seqc_testQueueInsertBatch(ev, 1);
}
//------------------------------------------------------------------------------
void seqc_testQueueInsertBatch(const seqc_event_t *evs, uint16_t count)
{
	for (uint16_t i = 0; i < count; i++)
		seqc_queueStage(&evs[i]);
	seqc_queueMerge();
}
//------------------------------------------------------------------------------
bool seqc_testQueuePop(seqc_event_t *out)
{
	if (seqc_queue.count == 0)
		return false;
	*out = seqc_queuePop();
	return true;
}
//------------------------------------------------------------------------------
uint32_t seqc_testQueueChoke(uint8_t track, uint32_t absStep)
{
	const uint32_t drops = seqc.stats.chokeDrops;
	seqc_queueChoke(track, absStep);
	seqc_queueCompact();
	return seqc.stats.chokeDrops - drops;
}
//------------------------------------------------------------------------------
uint16_t seqc_testQueueHead(void)
{
	return seqc_queue.head;
}
#endif

//------------------------------------------------------------------------------
// Firing
//------------------------------------------------------------------------------
/** send every pending note-off now (stop, rewind, seek, slave dropout) */
static void seqc_flushNoteOffs(void)
{
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		if (seqc_fire.offPending[t])
		{
			seqc_fire.offPending[t] = false;
			seqc.host.noteOff(seqc.host.ctx, t);
		}
	}
}
//------------------------------------------------------------------------------
/** a pending note-off is due */
static void seqc_noteOffDue(uint8_t track)
{
	seqc.pos = (double)seqc_fire.offTick[track];
	seqc_fire.offPending[track] = false;
	seqc.host.noteOff(seqc.host.ctx, track);
}
//------------------------------------------------------------------------------
/** the next queued event is due: mutes, the same-tick rule, choke, note-off,
 * trigger (spec 6.4, N2) */
static void seqc_fireNext(void)
{
	const seqc_event_t ev = seqc_queuePop();
	const uint8_t t = ev.track;

	seqc.pos = (double)ev.tick;
	if ((ev.flags & SEQC_EVF_ROLL) == 0 && seqc.host.muted(seqc.host.ctx, t))
	{
		seqc.stats.muteDrops++;
		return;
	}
	if (seqc_fire.fired[t] && seqc_fire.lastTick[t] == ev.tick && ev.absStep <= seqc_fire.lastStep[t])
	{
		seqc.stats.chokeDrops++;
		return;
	}
	seqc_queueChoke(t, ev.absStep);
	if (seqc_fire.offPending[t])
		seqc.host.noteOff(seqc.host.ctx, t);
	seqc_fire.fired[t] = true;
	seqc_fire.lastTick[t] = ev.tick;
	seqc_fire.lastStep[t] = ev.absStep;
	seqc_fire.offPending[t] = true;
	seqc_fire.offTick[t] = ev.tick + SEQC_NOTE_OFF_TICKS;
	seqc.host.trigger(seqc.host.ctx, &ev);
}

//------------------------------------------------------------------------------
// Generation
//------------------------------------------------------------------------------
/** the slot of occurrence (track, absStep) at (pattern, step), N3. A slot that
 * holds another occurrence, or the same one at another (pattern, step), is
 * cleared for it, so a different step gets a new draw. */
static seqc_draw_t *seqc_slot(uint8_t track, uint32_t absStep, uint8_t pattern, uint8_t step)
{
	seqc_draw_t *d = &seqc_gen.draw[track][absStep & 1u];
	if (!d->valid || d->absStep != absStep || d->pattern != pattern || d->step != step)
	{
		memset(d, 0, sizeof *d);
		d->valid = true;
		d->absStep = absStep;
		d->pattern = pattern;
		d->step = step;
	}
	return d;
}
//------------------------------------------------------------------------------
/** the probability draw r of the occurrence in slot d, made once (N3) */
static uint8_t seqc_drawR(seqc_draw_t *d)
{
	if (!d->drawn)
	{
		const uint32_t v = seqc.host.random(seqc.host.ctx);
		d->r = (uint8_t)(1u + v % 100u);
		d->drawn = true;
	}
	return d->r;
}
//------------------------------------------------------------------------------
/** queue the hits of occurrence (track, absStep) at (pattern, step) that belong to
 * one part (N3): the early part has the hits before absStep * 96, the late part
 * the rest.
 * The early part keeps the timing it read (swing + shift, ratchet hits) in the
 * slot, and whether it played (stepOn() and the draw), also when the step did
 * not play. If that timing had a hit before absStep * 96, the late part follows
 * the early part: it plays if the early part played, whatever stepOn() and the
 * probability read now, and otherwise not at all. So the hits of a step play
 * together or not at all.
 * Otherwise the late part reads stepOn() and the probability again, with the
 * same draw, and reads the timing again, so an edit or a shuffle change before
 * the boundary applies; but it keeps the early part's timing if the timing read
 * now would put a hit before absStep * 96, which has passed. Either way every
 * hit is generated once (decision 9 of epic E1). */
static void seqc_generate(uint8_t track, uint32_t absStep, uint8_t pattern, uint8_t step, bool early)
{
	seqc_draw_t *d = seqc_slot(track, absStep, pattern, step);
	const bool followsEarly = !early && d->timed && d->offset < 0;
	if (followsEarly && !d->played)
		return;
	const Step *data = NULL;
	bool plays = followsEarly;
	if (!followsEarly && seqc.host.stepOn(seqc.host.ctx, pattern, track, step))
	{
		const uint8_t r = seqc_drawR(d);
		data = seqc.host.step(seqc.host.ctx, pattern, track, step);
		plays = r <= data->prob;
	}
	if (!plays && !early)
		return;
	const StepTiming timing = seqc.host.timing(seqc.host.ctx, pattern, track, step);
	uint8_t hits = seqc_hitCount(timing.flam);
	int offset = ((absStep & 1u) ? seqc_gen.swing : 0) + seqc_shiftTicks(timing.shift);
	if (early)
	{
		d->timed = true;
		d->played = plays;
		d->hits = hits;
		d->offset = (int16_t)offset;
		if (!plays)
			return;
	}
	else if (d->timed && (d->offset < 0 || offset < 0))
	{
		hits = d->hits;
		offset = d->offset;
	}
	if (data == NULL)
		data = seqc.host.step(seqc.host.ctx, pattern, track, step);
	const seqc_tick_t base = (seqc_tick_t)absStep * SEQC_TICKS_PER_STEP;
	const seqc_tick_t first = base + offset;

	for (uint8_t j = 0; j < hits; j++)
	{
		const seqc_tick_t tick = first + (seqc_tick_t)(j * SEQC_TICKS_PER_STEP / hits);
		if ((tick < base) != early)
			continue;
		const seqc_event_t ev = {
			.tick = tick, .absStep = absStep, .track = track, .pattern = pattern, .step = step,
			.hit = j, .vel = (uint8_t)(data->volume & 0x7f), .note = data->note, .flags = 0, .reserved = 0,
			.p1Nr = data->param1Nr, .p1Val = data->param1Val, .p2Nr = data->param2Nr, .p2Val = data->param2Val,
		};
		seqc_queueStage(&ev);
	}
}
//------------------------------------------------------------------------------
/** true if the track's skip-once marker drops occurrence (track, n) at its
 * boundary. A marker for n is cleared here, whether it matches or not. */
static bool seqc_skipLate(uint8_t track, uint32_t n, uint8_t pattern, uint8_t step)
{
	seqc_skip_t *m = &seqc_skip[track];
	if (!m->valid || m->absStep > n)
		return false;
	m->valid = false;
	if (m->absStep != n || m->pattern != pattern || m->step != step)
		return false;
	seqc.stats.skipDrops++;
	return true;
}
//------------------------------------------------------------------------------
/** true if the track's skip-once marker drops the early part of occurrence
 * (track, n) */
static bool seqc_skipEarly(uint8_t track, uint32_t n, uint8_t pattern, uint8_t step)
{
	const seqc_skip_t *m = &seqc_skip[track];
	return m->valid && m->absStep == n && m->pattern == pattern && m->step == step;
}
//------------------------------------------------------------------------------
/** boundary step 4: the roll events of step n (spec 6.6). They are at the swung
 * grid position of the step and carry the step's note and p-locks. */
static void seqc_queueRolls(uint32_t n, uint8_t pattern)
{
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		const uint8_t rate = seqc.host.rollRate(seqc.host.ctx, t);
		const uint8_t step = (uint8_t)seqc_trk.index[t];
		if (rate == 0 || step % rate != 0)
			continue;
		const Step *data = seqc.host.step(seqc.host.ctx, pattern, t, step);
		const seqc_event_t ev = {
			.tick = (seqc_tick_t)n * SEQC_TICKS_PER_STEP + ((n & 1u) ? seqc_gen.swing : 0),
			.absStep = n, .track = t, .pattern = pattern, .step = step, .hit = 0,
			.vel = SEQC_ROLL_VELOCITY, .note = data->note, .flags = SEQC_EVF_ROLL, .reserved = 0,
			.p1Nr = data->param1Nr, .p1Val = data->param1Val, .p2Nr = data->param2Nr, .p2Val = data->param2Val,
		};
		seqc_queueStage(&ev);
	}
}
//------------------------------------------------------------------------------
/** boundary step 5: the late part of step n */
static void seqc_generateLate(uint32_t n, uint8_t pattern)
{
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		const uint8_t step = (uint8_t)seqc_trk.index[t];
		if (seqc_skipLate(t, n, pattern, step))
			continue;
		seqc_generate(t, n, pattern, step, false);
	}
}
//------------------------------------------------------------------------------
/** boundary step 6: the early part of step n+1. If the master track wraps at
 * n+1, peekNextPattern() tells the pattern and whether the indices restart. */
static void seqc_generateEarly(uint32_t n, uint8_t pattern)
{
	const uint8_t m = seqc_trk.master;
	uint8_t next = pattern;
	bool restart = false;

	if (seqc_nextIndex(seqc_trk.index[m], seqc_trackLength(pattern, m)) == 0)
	{
		restart = seqc.host.peekNextPattern(seqc.host.ctx, &next);
		if (next == SEQC_NO_PATTERN)
			return;			/* the song ends at n+1 */
	}
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		const uint8_t step = restart ? 0 : (uint8_t)seqc_nextIndex(seqc_trk.index[t], seqc_trackLength(next, t));
		if (seqc_skipEarly(t, n + 1u, next, step))
			continue;
		seqc_generate(t, n + 1u, next, step, true);
	}
}

//------------------------------------------------------------------------------
// Transport
//------------------------------------------------------------------------------
/** stop and keep the position (the caller has set it) */
static void seqc_stopTransport(void)
{
	seqc.state = SEQC_STOPPED;
	seqc.startPending = false;
	seqc_flushNoteOffs();
	seqc.host.transport(seqc.host.ctx, SEQC_TRANSPORT_STOP);
}
//------------------------------------------------------------------------------
/** clear what belongs to the current run: queue, note-offs, draws, same-tick
 * state and the skip-once markers (rewind and seek). The events that a choke of
 * this call removed are counted first, as if compacted (seqc_queueChoke()). */
static void seqc_clearRun(void)
{
	if (seqc_queue.chokeTracks != 0)
	{
		for (uint16_t i = 0; i < seqc_queue.count; i++)
		{
			if (seqc_isChoked(seqc_queueAt(i), NULL))
				seqc.stats.chokeDrops++;
		}
	}
	seqc_queue.head = 0;
	seqc_queue.count = 0;
	seqc_queue.chokeTracks = 0;
	seqc_stage.count = 0;
	seqc_flushNoteOffs();
	memset(seqc_gen.draw, 0, sizeof seqc_gen.draw);
	memset(seqc_fire.fired, 0, sizeof seqc_fire.fired);
	memset(seqc_skip, 0, sizeof seqc_skip);
}
//------------------------------------------------------------------------------
/** back to the start, before step 0 */
static void seqc_rewindTransport(void)
{
	const bool wasAtStart = seqc.atStart;

	seqc_clearRun();
	seqc.absStep = -1;
	memset(seqc_trk.index, -1, sizeof seqc_trk.index);
	seqc_trk.master = 0;
	seqc_trk.seeked = false;
	seqc.pos = seqc_startPosition();
	seqc_sl.lastClockPos = seqc.pos;
	seqc_out.nextPulse = 0;
	seqc.atStart = true;
	if (!wasAtStart)
		seqc.host.transport(seqc.host.ctx, SEQC_TRANSPORT_REWIND);
}
//------------------------------------------------------------------------------
/** arm: playback starts with a later clock */
static void seqc_arm(seqc_arm_t how)
{
	seqc.state = SEQC_ARMED;
	seqc_sl.arm = how;
	seqc.host.transport(seqc.host.ctx, SEQC_TRANSPORT_ARM);
}
//------------------------------------------------------------------------------
/** local start (seqc_start(), jack reset release): from the beginning; a slave
 * arms */
static void seqc_localStart(void)
{
	if (!seqc.atStart)
		seqc_rewindTransport();
	if (seqc.mode == SEQC_SYNC_INTERNAL)
	{
		seqc.state = SEQC_RUNNING;
		seqc.startPending = true;
		seqc.host.transport(seqc.host.ctx, SEQC_TRANSPORT_START);
	}
	else
		seqc_arm(SEQC_ARM_STEP_CLOCK);
}
//------------------------------------------------------------------------------
/** A transport that waits for its first clock since a rewind or a song position
 * is placed where the sync mode starts it: in a slave mode one clock before
 * step 0, or before the located step, so that the first clock plays it; in
 * internal sync at that step. The clock length depends on the sync mode and the
 * PPQ, so a change of either moves it. */
static void seqc_placeWaitingPosition(void)
{
	if (seqc.atStart)
		seqc.pos = seqc_startPosition();
	else if (seqc_trk.seeked && seqc.state != SEQC_RUNNING)
		seqc.pos = (double)seqc.absStep * SEQC_TICKS_PER_STEP + seqc_startPosition();
	else
		return;
	seqc_sl.lastClockPos = seqc.pos;
}
//------------------------------------------------------------------------------
/** song position (N6): seek to step sixteenths */
static void seqc_seek(uint16_t sixteenths)
{
	seqc_clearRun();
	const uint32_t steps = seqc.host.seekPosition(seqc.host.ctx, sixteenths);
	const uint8_t pattern = seqc.host.activePattern(seqc.host.ctx);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		seqc_trk.index[t] = (int8_t)(steps % seqc_trackLength(pattern, t));
	seqc_trk.master = seqc_longestTrack(pattern);
	seqc_trk.seeked = true;
	seqc.absStep = sixteenths;
	seqc.atStart = false;
	seqc_placeWaitingPosition();			/* one clock before step sixteenths */
	seqc_out.nextPulse = (seqc_tick_t)sixteenths * SEQC_TICKS_PER_STEP;
	seqc_sl.clockCount = 0;
}

//------------------------------------------------------------------------------
// Step boundary
//------------------------------------------------------------------------------
/** boundary steps 1 and 2: advance the counters, and call patternEnd() if the
 * master track wraps. Returns false if the song ended there. */
static bool seqc_advanceCounters(void)
{
	if (seqc_trk.seeked)
	{
		/* the first boundary after a song position: the counters are set */
		seqc_trk.seeked = false;
		return true;
	}
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
	seqc_gen.swing = seqc_swingTicks(seqc.host.shuffle(seqc.host.ctx));
	seqc.host.stepBoundary(seqc.host.ctx, n);
	seqc_queueRolls(n, pattern);
	seqc_generateLate(n, pattern);
	seqc_generateEarly(n, pattern);
	seqc_queueMerge();			/* the boundary's events join the queue together */
}

//------------------------------------------------------------------------------
// Tempo estimate (slave modes)
//------------------------------------------------------------------------------
/** the tempo estimate from bpm, clamped (N9) */
static void seqc_setSlaveBpm(double bpm)
{
	if (bpm < SEQC_SLAVE_BPM_MIN)
		bpm = SEQC_SLAVE_BPM_MIN;
	if (bpm > SEQC_SLAVE_BPM_MAX)
		bpm = SEQC_SLAVE_BPM_MAX;
	seqc_sl.bpm = bpm;
	seqc_sl.ticksPerCycle = seqc_ticksPerCycle(bpm);
	seqc_sl.cyclesPerTick = 1.0 / seqc_sl.ticksPerCycle;
}
//------------------------------------------------------------------------------
/*
 * The estimate (N9, N16, N21) is a least-squares fit of the clock stamps against
 * the clock count. Its slope is the clock period.
 * - Transfers: USB stamps the clocks of one transfer once, when the transfer is
 *   in, so they share the stamp of the last of them. The clocks with one stamp
 *   form a transfer, which is one point of the fit, at its last clock. It is
 *   evaluated once it is complete: at the next clock with a later stamp, at any
 *   other sync event, or at the next seqc_process(). seq_tick() drains the
 *   queues until they are empty, reading the time again before every pop, and
 *   passes the time of the last pop to seqc_process(); the USB interrupt queues
 *   all the clocks of a transfer before seq_tick() can run. So every clock
 *   stamped up to that time is in before the transfer is evaluated.
 * - Bursts: a MIDI clock less than SEQC_TEMPO_JOIN_MS after the newest stamp
 *   of the last transfer joins it, and the transfer takes the later stamp. A
 *   DIN interface that forwards a late buffer sends its clocks back to back,
 *   0.32 ms apart, so each reaches its own seq_tick(): the head of the burst,
 *   late, is evaluated alone. Tested, it is left out as an outlier; in the
 *   first points, which are not tested, it goes into the fit. Either way the
 *   next clock takes the fit back to before it (a copy kept for that) and
 *   joins the transfer; an outlier is uncounted. Joining only outliers would
 *   leave an untested head in, as a step of size - 1 clocks for 4 beats, and
 *   joining only untested points would treat the clocks of one audio buffer
 *   differently in the first points and later, which tilts the line. Until
 *   the next clock comes the estimate includes the head alone, for less than
 *   SEQC_TEMPO_JOIN_MS. 2 ms takes in USB transfers in consecutive 1 ms frames
 *   and a few MIDI bytes between two clocks, and is a quarter of a clock at
 *   SEQC_SLAVE_BPM_MAX. A head that completed a tempo jump is taken back too:
 *   the restart is undone (the ring has room for its points beside the fit
 *   before it) and done again, or not, with the whole burst, so a host stall
 *   with a catch-up burst is no tempo jump. A transfer followed by another sync
 *   event takes no more clocks.
 * - Windows: the points of the last SEQC_TEMPO_WINDOW clocks (short) and of the
 *   last SEQC_TEMPO_LONG_WINDOW clocks (long; the 4PPQ values for the jack at
 *   4 PPQ), and at most SEQC_TEMPO_SPAN_S. The estimate is the slope of the long
 *   window. If the short window's slope differs from it by more than the steps
 *   of the stamps explain, the tempo is changing: the long window drops its older
 *   points and becomes the short one.
 * - Steps: how far a point's interval is from the slope; their amplitude is a
 *   decaying peak. Stamps quantized to audio buffers move in steps of up to one
 *   buffer, a clock in exact stamps only by its jitter.
 * - Outliers, MIDI: a point further from the long window's line than the band
 *   is left out. The band is SEQC_TEMPO_OUTLIER_PCT of the slope, and at least
 *   SEQC_TEMPO_BAND_STEP step amplitudes, so the steps of USB stamps stay in;
 *   a lasting step is a step of the stamps, and the line takes it in. An
 *   outlier whose next point is back near the line was a glitch: its step is
 *   forgotten.
 * - Outliers, clock in jack: the stamps are exact, so a step of the stamps can
 *   only be a lost or an extra pulse. The test is on the interval: one more
 *   than SEQC_TEMPO_OUTLIER_PCT off the slope is left out, and the line goes on
 *   from its point as if the interval had had the slope (N9).
 * - The points of the first half short window are taken without the test.
 * - A tempo jump (N16): SEQC_TEMPO_RESTART outliers in a row on the same side
 *   restart the fit from them alone, as after a dropout: the first of them is
 *   the first point. The interval into it may straddle the change, so it is
 *   not used. The step amplitude stays: it belongs to the stamps, not to the
 *   tempo.
 * - Spurious clocks (E1.19): the fit has its own count, which leaves them out
 *   (extraClocks); the position counts them. MIDI: a row of early outliers
 *   that one spurious clock explains is taken in instead of restarting the fit
 *   (seqc_fitExtraClock()). The jack: a double trigger is merged at the pulse
 *   after it (seqc_fitDoubleTrigger()), and the merge undone at the next pulse
 *   if that one shows a tempo jump (seqc_fitUndoMerge()).
 */
/** true for the jack at 4 PPQ, whose clock is a step */
static bool seqc_jack4(void)
{
	return seqc.mode == SEQC_SYNC_ANALOG && seqc_sl.ppq != 24;
}
//------------------------------------------------------------------------------
/** clocks of the short window of the fit */
static uint32_t seqc_shortWindow(void)
{
	return seqc_jack4() ? SEQC_TEMPO_WINDOW_4PPQ : SEQC_TEMPO_WINDOW;
}
//------------------------------------------------------------------------------
/** clocks of the long window of the fit */
static uint32_t seqc_longWindow(void)
{
	return seqc_jack4() ? SEQC_TEMPO_LONG_WINDOW_4PPQ : SEQC_TEMPO_LONG_WINDOW;
}
//------------------------------------------------------------------------------
/** the ring index after i */
static uint8_t seqc_ringNext(uint8_t i)
{
	return i + 1u < SEQC_TEMPO_RING ? (uint8_t)(i + 1u) : 0u;
}
//------------------------------------------------------------------------------
/** the ring index before i */
static uint8_t seqc_ringPrev(uint8_t i)
{
	return i > 0u ? (uint8_t)(i - 1u) : (uint8_t)(SEQC_TEMPO_RING - 1u);
}
//------------------------------------------------------------------------------
/** x of ring point i: clocks from the base point */
static double seqc_ringX(uint8_t i)
{
	return (double)(uint32_t)(seqc_sl.ringClock[i] - seqc_sl.fit.baseClock);
}
//------------------------------------------------------------------------------
/** y of ring point i: cycles from the base point. The window spans less than
 * 2^32 cycles, and no point is stamped before the base (seqc_fitAdd()). */
static double seqc_ringY(uint8_t i)
{
	return (double)(uint32_t)(seqc_sl.ringStamp[i] - (uint32_t)seqc_sl.fit.baseStamp);
}
//------------------------------------------------------------------------------
/** add (w = 1) or remove (w = -1) a point */
static void seqc_sumsAdd(seqc_sums_t *s, double x, double y, double w)
{
	s->n += w;
	s->sx += w * x;
	s->sy += w * y;
	s->sxx += w * x * x;
	s->sxy += w * x * y;
}
//------------------------------------------------------------------------------
/** move the origin of the sums to (dx, dy); exact, as all terms are integers */
static void seqc_sumsShift(seqc_sums_t *s, double dx, double dy)
{
	s->sxy -= dy * s->sx + dx * s->sy - s->n * dx * dy;
	s->sxx -= (2.0 * s->sx - s->n * dx) * dx;
	s->sx -= s->n * dx;
	s->sy -= s->n * dy;
}
//------------------------------------------------------------------------------
/** the fitted slope in cycles per clock; 0 with fewer than two points */
static double seqc_sumsSlope(const seqc_sums_t *s)
{
	if (s->n < 2.0)
		return 0.0;
	const double mx = s->sx / s->n;
	const double sxx = s->sxx - mx * s->sx;
	const double sxy = s->sxy - mx * s->sy;
	return sxx > 0.0 && sxy > 0.0 ? sxy / sxx : 0.0;
}
//------------------------------------------------------------------------------
/** the fitted line at x, with its slope */
static double seqc_sumsAt(const seqc_sums_t *s, double x, double slope)
{
	return (s->sy + slope * (x * s->n - s->sx)) / s->n;
}
//------------------------------------------------------------------------------
/** clocks from the oldest to the newest point of the long window */
static double seqc_fitSpan(void)
{
	const seqc_fit_t *f = &seqc_sl.fit;
	if (f->tailL == f->head)
		return 0.0;
	return seqc_ringX(seqc_ringPrev(f->head));
}
//------------------------------------------------------------------------------
/** the base point becomes the oldest point of the long window */
static void seqc_fitRebase(void)
{
	seqc_fit_t *f = &seqc_sl.fit;
	const double dx = seqc_ringX(f->tailL);
	const double dy = seqc_ringY(f->tailL);
	seqc_sumsShift(&f->lng, dx, dy);
	seqc_sumsShift(&f->sht, dx, dy);
	f->baseClock = seqc_sl.ringClock[f->tailL];
	f->baseStamp += (uint64_t)dy;
}
//------------------------------------------------------------------------------
/** empty both windows */
static void seqc_fitClearWindows(void)
{
	seqc_fit_t *f = &seqc_sl.fit;
	f->tailL = f->head;
	f->tailS = f->head;
	memset(&f->lng, 0, sizeof f->lng);
	memset(&f->sht, 0, sizeof f->sht);
}
//------------------------------------------------------------------------------
/** add point p to the ring and to both windows; the points that leave a window
 * go out of its sums */
static void seqc_fitAdd(seqc_point_t p)
{
	seqc_fit_t *f = &seqc_sl.fit;
	const uint32_t shortW = seqc_shortWindow();
	const uint32_t longW = seqc_longWindow();
	uint64_t span = (uint64_t)seqc.cyclesPerSecond * SEQC_TEMPO_SPAN_S;
	if (span > UINT32_MAX)
		span = UINT32_MAX;	/* the ring keeps 32 bits of each stamp */
	bool moved = false;
	while (f->tailL != f->head)
	{
		/* The oldest point leaves when it is out of the window. It leaves too if
		 * p is stamped before it: a stale MIDI clock, untested in the first
		 * points (a tested one that early is an outlier). The base, the oldest
		 * point, must not be later than p. */
		const uint64_t tail = f->baseStamp + (uint64_t)seqc_ringY(f->tailL);
		if (p.clock - seqc_sl.ringClock[f->tailL] <= longW && p.stamp >= tail && p.stamp - tail <= span)
			break;
		if (f->tailS == f->tailL)
		{
			seqc_sumsAdd(&f->sht, seqc_ringX(f->tailS), seqc_ringY(f->tailS), -1.0);
			f->tailS = seqc_ringNext(f->tailS);
		}
		seqc_sumsAdd(&f->lng, seqc_ringX(f->tailL), seqc_ringY(f->tailL), -1.0);
		f->tailL = seqc_ringNext(f->tailL);
		moved = true;
	}
	while (f->tailS != f->head && p.clock - seqc_sl.ringClock[f->tailS] > shortW)
	{
		seqc_sumsAdd(&f->sht, seqc_ringX(f->tailS), seqc_ringY(f->tailS), -1.0);
		f->tailS = seqc_ringNext(f->tailS);
	}
	if (f->tailL == f->head)
	{
		/* p is the only point: the base */
		seqc_fitClearWindows();
		f->baseClock = p.clock;
		f->baseStamp = p.stamp;
	}
	else
	{
		/* A stale clock in the fit (untested in the first points, or inside a
		 * wide USB band) can be stamped after the oldest point but before the
		 * newest one. It goes in at the newest one's stamp: the ring stays in
		 * stamp order, so a later base (the oldest point) is never after a point
		 * of the window, and seqc_ringY() never wraps. */
		const uint8_t newest = seqc_ringPrev(f->head);
		const uint64_t last = f->baseStamp + (uint64_t)seqc_ringY(newest);
		if (p.stamp < last)
			p.stamp = last;
		if (moved)
			seqc_fitRebase();
	}
	seqc_sl.ringClock[f->head] = p.clock;
	seqc_sl.ringStamp[f->head] = (uint32_t)p.stamp;
	f->head = seqc_ringNext(f->head);
	const double x = (double)(uint32_t)(p.clock - f->baseClock);
	const double y = (double)(p.stamp - f->baseStamp);
	seqc_sumsAdd(&f->lng, x, y, 1.0);
	seqc_sumsAdd(&f->sht, x, y, 1.0);
}
//------------------------------------------------------------------------------
/** take the newest point out of the ring and both windows; not the base point.
 * The points that left a window when it came in stay out. The next point goes
 * into its slot, which the fit before the transfer uses: the slot is kept for a
 * clock that joins the transfer (seqc_tempoClock()). */
static void seqc_fitDropNewest(void)
{
	seqc_fit_t *f = &seqc_sl.fit;
	f->head = seqc_ringPrev(f->head);
	seqc_sl.joinDropped = true;
	seqc_sl.dropSlot = f->head;
	seqc_sl.dropClock = seqc_sl.ringClock[f->head];
	seqc_sl.dropStamp = seqc_sl.ringStamp[f->head];
	seqc_sumsAdd(&f->lng, seqc_ringX(f->head), seqc_ringY(f->head), -1.0);
	seqc_sumsAdd(&f->sht, seqc_ringX(f->head), seqc_ringY(f->head), -1.0);
}
//------------------------------------------------------------------------------
/** the long window becomes the short one when their slopes differ by more than
 * the steps can explain: SEQC_TEMPO_SWITCH_STEP times the largest slope error
 * that steps of the amplitude can give the two windows. The amplitude includes
 * the offset of the newest point, which may be a step the next point confirms.
 * Returns the slope of the long window, or of the short one after a switch. */
static double seqc_fitSwitch(double newestOff)
{
	seqc_fit_t *f = &seqc_sl.fit;
	const double longSlope = seqc_sumsSlope(&f->lng);
	if (f->tailS == f->tailL || f->sht.n < 3.0)
		return longSlope;
	const double step = newestOff > f->step ? newestOff : f->step;
	const double shortSlope = seqc_sumsSlope(&f->sht);
	const double d = shortSlope - longSlope;
	const double allowed = SEQC_TEMPO_SWITCH_STEP * 1.5 * step
			* (1.0 / (double)seqc_shortWindow() + 1.0 / (double)seqc_longWindow());
	if (d <= allowed && -d <= allowed)
		return longSlope;
	f->lng = f->sht;
	f->tailL = f->tailS;
	seqc_fitRebase();
	f->drift = 0.0;		/* the slope changes: the offsets start again */
	f->haveLastOff = false;
	return shortSlope;
}
//------------------------------------------------------------------------------
/** the outlier band for the slope and the step amplitude step:
 * SEQC_TEMPO_OUTLIER_PCT of the slope, and at least SEQC_TEMPO_BAND_STEP step
 * amplitudes */
static double seqc_fitBand(double slope, double step)
{
	const double band = slope * (SEQC_TEMPO_OUTLIER_PCT / 100.0);
	return band < SEQC_TEMPO_BAND_STEP * step ? SEQC_TEMPO_BAND_STEP * step : band;
}
//------------------------------------------------------------------------------
/** MIDI: a row of early outliers that one spurious clock explains (E1.19). The
 * master's clocks after a spurious one are one clock early in the count, so
 * they leave the band in a row; counted one clock earlier, they lie on the line
 * again. The spurious clock is the row's first point (up to about half a clock
 * after a real one) or the newest point of the fit (later), the one of the two
 * further from the line at their common clock. If the row's other points,
 * counted one clock earlier, lie within a quarter of the band of the line, and
 * so does the nearer of those two, the fit counts one clock fewer, takes them
 * in and leaves the spurious clock out, instead of restarting. After a tempo
 * jump the clocks move further off the line at every clock, so they do not all
 * come back that close. The test needs stamps precise to that quarter band: a
 * step amplitude before the row of at most that. Returns true if it took the
 * row in. */
static bool seqc_fitExtraClock(void)
{
	seqc_fit_t *f = &seqc_sl.fit;
	const uint8_t newest = seqc_ringPrev(f->head);
	const double tol = 0.25 * seqc_fitBand(f->slope, f->stepBeforeRow);
	if (f->rowLate || f->stepBeforeRow > tol || f->tailL == newest
			|| f->row[0].clock - 1u != seqc_sl.ringClock[newest])
		return false;
	double r0 = 0.0;
	for (uint8_t i = 0; i < f->rowCount; i++)
	{
		/* signed: a stale MIDI clock may be stamped before the base */
		const double r = (double)(int64_t)(f->row[i].stamp - f->baseStamp)
				- seqc_sumsAt(&f->lng, (double)(uint32_t)(f->row[i].clock - 1u - f->baseClock), f->slope);
		if (i == 0)
			r0 = r < 0.0 ? -r : r;
		else if (r > tol || -r > tol)
			return false;
	}
	const double r = seqc_ringY(newest) - seqc_sumsAt(&f->lng, seqc_ringX(newest), f->slope);
	const double rn = r < 0.0 ? -r : r;
	if (r0 > tol && rn > tol)
		return false;
	uint8_t i = 1;
	if (rn > r0)
	{
		seqc_fitDropNewest();	/* the newest point was the spurious clock */
		i = 0;
	}
	f->extraClocks++;
	for (; i < f->rowCount; i++)
	{
		f->row[i].clock--;
		seqc_fitAdd(f->row[i]);
	}
	f->prev = f->row[f->rowCount - 1u];
	f->rowCount = 0;
	f->step = f->stepBeforeRow * (1.0 - SEQC_TEMPO_STEP_DECAY) * (1.0 - SEQC_TEMPO_STEP_DECAY);
	f->haveLastOff = false;
	f->slope = seqc_fitSwitch(0.0);
	return true;
}
//------------------------------------------------------------------------------
/** the jack: a double trigger (E1.19), a second pulse between two of the
 * master's, from a bouncing contact or a noisy cable. It splits an interval of
 * the slope into two short ones. The first is an outlier if the second pulse
 * came in the first half, the second if it came in the second half; within
 * about the jitter of half a pulse, both or neither may be. So the pulse
 * between the interval into p and the one before it (f->split, tested against
 * f->splitSlope) is left out when:
 * - the two add up to that slope within a sixteenth of its band, and p's is
 *   short against it (the second may have moved the fit's slope);
 * - the interval before them was steady, within a quarter band of the slope.
 * The fit then counts one clock fewer, and p's interval, *dev, becomes the
 * merged one. The jack decides at p, where MIDI decides at the end of a row
 * (seqc_fitExtraClock()), and the first two intervals after a jump to two to
 * three times the tempo (the straddling one and the first new one) can look the
 * same. So the merge is tentative: the fit before it is kept, and the next
 * pulse takes it back if it comes about half the slope after p, or earlier
 * (seqc_fitUndoMerge()).
 * The second pulse is the one of the two that is further from the line at
 * their common clock, as on MIDI (seqc_fitExtraClock()): the pulse between,
 * unless it lay nearer than p, as when p is a contact bounce just after a real
 * pulse. Then p is left out instead, and the fit stays as it was. Returns what
 * it did. */
static seqc_split_t seqc_fitDoubleTrigger(seqc_point_t *p, double *dev, double step)
{
	seqc_fit_t *f = &seqc_sl.fit;
	const double band = seqc_fitBand(f->splitSlope, step);
	const double tol = band / 16.0;
	const double merged = f->split + *dev + f->slope;
	const double second = *dev + f->slope - f->splitSlope;	/* p's interval less the slope of the first */
	const bool leftOut = f->rowCount == 1;	/* the pulse between was an outlier */
	if (merged > tol || -merged > tol || second > 0.0
			|| (!leftOut && f->tailL == seqc_ringPrev(f->head)))
		return SEQC_SPLIT_NONE;
	if (-f->split <= (merged < 0.0 ? -merged : merged))	/* never so if the pulse between was an outlier */
	{
		f->extraClocks++;	/* p was the second pulse */
		seqc.stats.intervalsExcluded++;
		return SEQC_SPLIT_BOUNCE;
	}
	/* for an undo: the fit before the merge, and the ring slot p goes into */
	const uint8_t slot = leftOut ? f->head : seqc_ringPrev(f->head);
	seqc_sl.fitBefore = *f;
	seqc_sl.dropSlot = slot;
	seqc_sl.dropClock = seqc_sl.ringClock[slot];
	seqc_sl.dropStamp = seqc_sl.ringStamp[slot];
	seqc_sl.mergeTentative = true;
	seqc_sl.mergePoint = (seqc_point_t){ p->stamp, p->clock + f->extraClocks };
	seqc_sl.mergeExcluded = seqc.stats.intervalsExcluded;
	if (leftOut)
		f->phase -= (int64_t)(f->split - 0.5);	/* the phase its interval added (seqc_fitPoint()) */
	else
	{
		seqc_fitDropNewest();	/* it was in the fit, and its offset was no step of the stamps */
		f->haveLastOff = false;
		f->drift = (f->drift - SEQC_TEMPO_DRIFT_GAIN * f->split) / (1.0 - SEQC_TEMPO_DRIFT_GAIN);	/* nor part of the drift */
		seqc.stats.intervalsExcluded++;
	}
	f->extraClocks++;
	p->clock--;
	*dev = merged;
	return SEQC_SPLIT_MERGED;
}
//------------------------------------------------------------------------------
/** the point of a complete transfer goes into the fit, is left out as an
 * outlier, or restarts the fit. On the jack it may complete a double trigger
 * (seqc_fitDoubleTrigger()) if mayMerge. */
static seqc_pointFate_t seqc_fitPoint(seqc_point_t p, bool mayMerge)
{
	seqc_fit_t *f = &seqc_sl.fit;
	const bool jack = seqc.mode == SEQC_SYNC_ANALOG;
	p.clock -= f->extraClocks;	/* the fit's own count */
	const bool tested = f->slope > 0.0 && seqc_fitSpan() >= (double)(seqc_shortWindow() / 2u);
	/* Steps of the stamps. How far an interval is from the slope, less the drift
	 * (the recent mean of that, which a ramp moves slowly), is its offset.
	 * Stamps quantized to buffers, or with jitter, stay near the true times: the
	 * next offset turns back or stops (opposite sign, or under half), and the
	 * offset was a step of the stamps. A tempo change makes the offsets after it
	 * go on to the same side, falling only as the drift follows. */
	double step = f->step * (1.0 - SEQC_TEMPO_STEP_DECAY);
	double dev = 0.0, off = 0.0;
	bool haveDev = false, merged = false;
	if (f->havePrev && f->slope > 0.0)
	{
		/* signed: a MIDI clock may be stamped before the newest point (DIN and
		 * USB clocks at once) */
		dev = (double)(int64_t)(p.stamp - f->prev.stamp) - f->slope * (double)(p.clock - f->prev.clock);
		const seqc_split_t dt = jack && mayMerge && tested && f->split < 0.0
				? seqc_fitDoubleTrigger(&p, &dev, step) : SEQC_SPLIT_NONE;
		if (dt == SEQC_SPLIT_BOUNCE)
			return SEQC_POINT_LEFT_OUT;	/* the fit stays as it was */
		merged = dt == SEQC_SPLIT_MERGED;
		off = dev - f->drift;
		if (f->haveLastOff)
		{
			const double a = off < 0.0 ? -off : off;
			const double b = f->lastOff < 0.0 ? -f->lastOff : f->lastOff;
			if (((off < 0.0) != (f->lastOff < 0.0) || a < 0.5 * b) && b > step)
				step = b;
		}
		haveDev = true;
	}
	f->step = step;
	f->split = 0.0;
	const bool steady = f->steady;	/* a double trigger splits an interval after a steady one */
	f->steady = false;
	if (tested)
	{
		/* MIDI: how far the point is from the long window's line. The jack: how
		 * far its interval, one pulse, is from the slope; its stamps are exact,
		 * so a point that is off the line after a good interval came after a
		 * lost or an extra pulse, and the line goes on from it. */
		double r = dev;
		if (!jack)
		{
			const double x = (double)(uint32_t)(p.clock - f->baseClock);
			/* signed: a stale MIDI clock may be stamped before the base */
			r = (double)(int64_t)(p.stamp - f->baseStamp) - seqc_sumsAt(&f->lng, x, f->slope);
		}
		const double band = seqc_fitBand(f->slope, f->step);
		if (r > band || -r > band)
		{
			const bool late = r > 0.0;
			if (f->rowCount == 0 || late != f->rowLate)
			{
				f->rowCount = 0;
				f->rowLate = late;
				f->rowResidual = r;
				f->stepBeforeRow = f->step;
			}
			f->row[f->rowCount++] = p;
			f->prev = p;
			f->lastOff = off;
			f->haveLastOff = !jack;		/* a jack outlier is no step of the stamps */
			if (f->rowCount < (seqc_jack4() ? SEQC_TEMPO_RESTART_4PPQ : SEQC_TEMPO_RESTART))
			{
				if (jack)	/* left out: the line goes on as if the interval had the slope */
					f->phase += (int64_t)(dev < 0.0 ? dev - 0.5 : dev + 0.5);
				if (jack && !late && f->rowCount == 1 && steady)
				{
					f->split = dev;	/* a short interval may be the first part of a double trigger */
					f->splitSlope = f->slope;
				}
				seqc.stats.intervalsExcluded++;
				return SEQC_POINT_LEFT_OUT;
			}
			if (!jack && seqc_fitExtraClock())
				return SEQC_POINT_USED;
			/* a tempo jump: the fit restarts from the row alone */
			const uint8_t count = f->rowCount;
			seqc_fitClearWindows();
			for (uint8_t i = 0; i < count; i++)
				seqc_fitAdd(f->row[i]);
			f->rowCount = 0;
			f->phase = 0;
			f->drift = 0.0;
			f->haveLastOff = false;
			f->slope = seqc_sumsSlope(&f->lng);
			seqc.stats.tempoRestarts++;
			return SEQC_POINT_RESTARTED;
		}
		f->steady = jack && !merged && r <= 0.25 * band && -r <= 0.25 * band;
		const double rr = f->rowResidual < 0.0 ? -f->rowResidual : f->rowResidual;
		if (f->rowCount > 0 && (r < 0.0 ? -r : r) < 0.5 * rr)
		{
			/* back near the line: the outliers before were a glitch, not a step of
			 * the stamps */
			f->step = f->stepBeforeRow * (1.0 - SEQC_TEMPO_STEP_DECAY) * (1.0 - SEQC_TEMPO_STEP_DECAY);
			haveDev = false;
		}
	}
	if (jack && tested && haveDev && dev < 0.0 && !merged && steady)
	{
		f->split = dev;		/* a short interval may be the first part of a double trigger; a merged one is not */
		f->splitSlope = f->slope;
	}
	f->lastOff = off;
	f->haveLastOff = haveDev;
	if (haveDev)
		f->drift += (dev - f->drift) * SEQC_TEMPO_DRIFT_GAIN;
	f->rowCount = 0;
	seqc_fitAdd((seqc_point_t){ p.stamp - (uint64_t)f->phase, p.clock });
	f->slope = seqc_fitSwitch(haveDev ? (off < 0.0 ? -off : off) : 0.0);
	f->havePrev = true;
	f->prev = p;
	return SEQC_POINT_USED;
}
//------------------------------------------------------------------------------
/** the jack: the pulse p after one that merged a double trigger
 * (seqc_fitDoubleTrigger()). After a double trigger p comes about one slope
 * after that pulse. If it comes more than half a band earlier than that (about
 * half a slope after it, or less), the two short intervals were the start of a
 * tempo jump: the fit, the ring slot that pulse
 * wrote and intervalsExcluded go back to before the merge, and the pulse goes
 * in again without one. The jump then goes through the fit (N16) as it would
 * have without the merge, one pulse later. */
static void seqc_fitUndoMerge(seqc_point_t p)
{
	if (!seqc_sl.mergeTentative)
		return;
	seqc_sl.mergeTentative = false;
	const seqc_fit_t *b = &seqc_sl.fitBefore;
	const seqc_point_t m = seqc_sl.mergePoint;
	const double d = (double)(int64_t)(p.stamp - m.stamp) - b->splitSlope * (double)(uint32_t)(p.clock - m.clock);
	if (d >= -0.5 * seqc_fitBand(b->splitSlope, b->step))
		return;
	seqc_sl.fit = seqc_sl.fitBefore;
	seqc_sl.ringClock[seqc_sl.dropSlot] = seqc_sl.dropClock;
	seqc_sl.ringStamp[seqc_sl.dropSlot] = seqc_sl.dropStamp;
	seqc.stats.intervalsExcluded = seqc_sl.mergeExcluded;
	(void)seqc_fitPoint(m, false);
}
//------------------------------------------------------------------------------
/** forget the fit and the transfer being collected (the last known tempo stays) */
static void seqc_clearEstimate(void)
{
	seqc_fit_t *f = &seqc_sl.fit;
	seqc_fitClearWindows();
	f->havePrev = false;
	f->rowCount = 0;
	f->phase = 0;
	f->step = 0.0;
	f->drift = 0.0;
	f->haveLastOff = false;
	f->slope = 0.0;
	f->split = 0.0;
	f->steady = false;
	f->extraClocks = 0;
	seqc_sl.mergeTentative = false;
	seqc_sl.transferOpen = false;
	seqc_sl.joinable = false;
}
//------------------------------------------------------------------------------
/** the transfer being collected is complete: its point goes into the fit, and
 * the estimate follows the slope. A MIDI transfer stays joinable: the fit before
 * it is kept, see seqc_tempoClock(). */
static void seqc_transferDone(void)
{
	const bool midi = seqc.mode == SEQC_SYNC_MIDI;
	seqc_sl.transferOpen = false;
	if (midi)
	{
		seqc_sl.fitBefore = seqc_sl.fit;
		seqc_sl.joinDropped = false;
	}
	seqc_fitUndoMerge(seqc_sl.transfer);
	const seqc_pointFate_t fate = seqc_fitPoint(seqc_sl.transfer, true);
	seqc_sl.joinable = midi;
	seqc_sl.joinLeftOut = fate == SEQC_POINT_LEFT_OUT;
	seqc_sl.joinRestarted = fate == SEQC_POINT_RESTARTED;
	if (seqc_sl.fit.slope > 0.0)
		seqc_setSlaveBpm(seqc_ticksPerClock() / seqc_sl.fit.slope * (double)seqc.cyclesPerSecond * 60.0
				/ SEQC_TICKS_PER_BEAT);
}
//------------------------------------------------------------------------------
/** true if a clock stamped at stamp belongs to the last transfer: not stamped
 * later than it, or, for MIDI clock, less than SEQC_TEMPO_JOIN_MS later (a
 * burst of clocks forwarded back to back) */
static bool seqc_inTransfer(seqc_time_t stamp)
{
	const seqc_time_t t = seqc_sl.transfer.stamp;
	return stamp <= t || (seqc.mode == SEQC_SYNC_MIDI
			&& stamp - t < (seqc_time_t)seqc.cyclesPerSecond * SEQC_TEMPO_JOIN_MS / 1000u);
}
//------------------------------------------------------------------------------
/** true if a sync event can be one more clock of the last transfer: a clock of
 * the sync mode that seqc_inTransfer() takes. It joins it only while that
 * transfer is open or joinable (seqc_tempoClock()). */
static bool seqc_sameTransfer(seqc_sync_ev_t ev, seqc_time_t stamp)
{
	const bool clock = (ev == SEQC_EV_CLOCK && seqc.mode == SEQC_SYNC_MIDI)
			|| (ev == SEQC_EV_PULSE && seqc.mode == SEQC_SYNC_ANALOG);
	return clock && seqc_inTransfer(stamp);
}
//------------------------------------------------------------------------------
/** the tempo part of a clock (N9, N15, N21). A clock with the stamp of the
 * transfer being collected, or an earlier one, is one more clock of it, and so
 * is a MIDI clock less than SEQC_TEMPO_JOIN_MS after it; any other clock begins
 * the next transfer. Such a clock also joins the last transfer after it was
 * evaluated, while it is joinable (see seqc_transferDone()): the fit then goes
 * back to before that transfer. After a dropout the fit starts again, and the
 * gap is not used. */
static void seqc_tempoClock(seqc_time_t stamp)
{
	if (seqc_sl.haveStamp)
	{
		const seqc_time_t dt = stamp > seqc_sl.lastStamp ? stamp - seqc_sl.lastStamp : 0u;
		if (seqc_sl.droppedOut || dt > seqc_dropoutCycles())
		{
			if (!seqc_sl.droppedOut)
				seqc.stats.dropouts++;
			seqc_clearEstimate();
		}
	}
	seqc_sl.haveStamp = true;
	seqc_sl.lastStamp = stamp;
	seqc_sl.droppedOut = false;

	const uint32_t clock = ++seqc_sl.fitClock;
	if (seqc_sl.joinable && seqc_inTransfer(stamp))
	{
		/* A burst: DIN interfaces forward a late buffer as back-to-back clocks,
		 * 0.32 ms apart, each in its own seq_tick(). The last transfer was its
		 * head: left out as an outlier, in the fit (untested or inside the
		 * band), or the end of a tempo jump that restarted the fit. The fit goes
		 * back to before it (the head wrote only ring slots that the fit before
		 * it does not use, but for the slot of a point it took out, which is
		 * put back), and the transfer takes this clock too. */
		seqc_sl.fit = seqc_sl.fitBefore;
		if (seqc_sl.joinDropped)
		{
			seqc_sl.ringClock[seqc_sl.dropSlot] = seqc_sl.dropClock;
			seqc_sl.ringStamp[seqc_sl.dropSlot] = seqc_sl.dropStamp;
		}
		if (seqc_sl.joinLeftOut && seqc.stats.intervalsExcluded > 0)
			seqc.stats.intervalsExcluded--;
		if (seqc_sl.joinRestarted && seqc.stats.tempoRestarts > 0)
			seqc.stats.tempoRestarts--;
		seqc_sl.transferOpen = true;
	}
	seqc_sl.joinable = false;
	if (seqc_sl.transferOpen)
	{
		/* one more clock of the transfer: seqc_syncEvent() completed it
		 * before this clock otherwise. Its stamp is that of its latest clock. */
		seqc_sl.transfer.clock = clock;
		if (stamp > seqc_sl.transfer.stamp)
			seqc_sl.transfer.stamp = stamp;
		return;
	}
	seqc_sl.transferOpen = true;
	seqc_sl.transfer = (seqc_point_t){ stamp, clock };
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
	if (seqc.mode == SEQC_SYNC_INTERNAL)
	{
		seqc_consider(SEQC_ITEM_TEMPO_STEP, seqc_out.nextTempoStep, 0, &best, tick, track);
		seqc_consider(SEQC_ITEM_MIDI_CLOCK, seqc_out.nextMidiClock, 0, &best, tick, track);
	}
	if (seqc.state == SEQC_RUNNING && !seqc.startPending)
	{
		seqc_consider(SEQC_ITEM_BOUNDARY, (seqc_tick_t)seqc_nextBoundaryStep() * SEQC_TICKS_PER_STEP, 0,
				&best, tick, track);
		for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
			if (seqc_fire.offPending[t])
				seqc_consider(SEQC_ITEM_NOTE_OFF, seqc_fire.offTick[t], t, &best, tick, track);
		const seqc_event_t *ev = seqc_queueFront();
		if (ev != NULL)
			seqc_consider(SEQC_ITEM_EVENT, ev->tick, 0, &best, tick, track);
		seqc_consider(SEQC_ITEM_PULSE, seqc_out.nextPulse, 0, &best, tick, track);
	}
	return best;
}
//------------------------------------------------------------------------------
/** The last tick that counts as due at target, the timeline at the call.
 * Rounding can leave the target a few ULPs short of the tick of an item that is
 * due at the call, a step boundary whose ideal time is the time of the call for
 * example; that tick still counts as due. The tolerance, 1e-12 x (1 + |target|)
 * ticks, is far above the rounding of a double. */
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
		case SEQC_ITEM_TEMPO_STEP:
			if (seqc.state == SEQC_RUNNING)
				seqc.pos = (double)tick;
			seqc_out.nextTempoStep += SEQC_TICKS_PER_STEP;
			seqc.host.tempoStep(seqc.host.ctx);
			break;
		case SEQC_ITEM_BOUNDARY:
			seqc_boundary(tick);
			break;
		case SEQC_ITEM_NOTE_OFF:
			seqc_noteOffDue(track);
			break;
		case SEQC_ITEM_EVENT:
			seqc_fireNext();
			break;
		case SEQC_ITEM_PULSE:
			seqc.pos = (double)tick;
			seqc_out.nextPulse += SEQC_TICKS_PER_PULSE;
			seqc.host.clockOutPulse(seqc.host.ctx,
					(uint8_t)((tick % SEQC_TICKS_PER_STEP) / SEQC_TICKS_PER_PULSE));
			break;
		case SEQC_ITEM_MIDI_CLOCK:
			if (seqc.state == SEQC_RUNNING)
				seqc.pos = (double)tick;
			seqc_out.nextMidiClock += SEQC_TICKS_PER_MIDI_CLOCK;
			seqc.host.midiClockOut(seqc.host.ctx);
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
		/* step 0 now; the MIDI clock out and the tempo steps are re-phased (N8) */
		seqc.startPending = false;
		seqc.anchorTime = seqc.now;
		seqc.anchorTick = 0.0;
		seqc.pos = 0.0;
		seqc_out.nextMidiClock = 0;
		seqc_out.nextTempoStep = 0;
		seqc_out.nextPulse = 0;
	}
	seqc_runTo(seqc_internalTick(seqc.now));
}
//------------------------------------------------------------------------------
/** slave modes: while running, the position follows the interpolation (N9).
 * After the dropout time (N15) without a clock it holds, the estimate is
 * cleared and the pending note-offs are sent (N17). */
static void seqc_runSlave(void)
{
	if (seqc.state != SEQC_RUNNING)
		return;
	const seqc_time_t limit = seqc_sl.lastClockTime + seqc_dropoutCycles();
	if (!seqc_sl.droppedOut && seqc.now > limit)
	{
		seqc_runTo(seqc_slaveTarget(limit));
		seqc_sl.droppedOut = true;
		seqc_clearEstimate();
		seqc.stats.dropouts++;
		seqc_flushNoteOffs();
		return;
	}
	seqc_runTo(seqc_slaveTarget(seqc.now));
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
		seqc_sl.lastClockTime = t;
	}
	else if (t > seqc.now)
		seqc.now = t;
	if (seqc.mode == SEQC_SYNC_INTERNAL)
		seqc_runInternal();
	else
		seqc_runSlave();
}
//------------------------------------------------------------------------------
/** slave modes: the time at which the next item falls due, capped at the clamp
 * time and the dropout timeout (N10) */
static seqc_time_t seqc_slaveDue(seqc_item_t item, seqc_tick_t tick)
{
	if (seqc.state != SEQC_RUNNING || seqc_sl.droppedOut)
		return SEQC_NEVER;
	seqc_time_t due = seqc_sl.lastClockTime + seqc_dropoutCycles();
	double until = seqc_sl.lastClockPos + seqc_ticksPerClock();
	if (item != SEQC_ITEM_NONE && (double)tick < until)
		until = (double)tick;
	if (until > seqc.pos)
	{
		const double cycles = (until - seqc_sl.lastClockPos) * seqc_sl.cyclesPerTick;
		const seqc_time_t t = seqc_sl.lastClockTime + (cycles > 0.0 ? (seqc_time_t)cycles : 0u);
		if (t < due)
			due = t;
	}
	return due;
}
//------------------------------------------------------------------------------
/** work out nextDue (N10). While a transfer is being collected, the next
 * seqc_process() evaluates it (N21). */
static void seqc_schedule(void)
{
	if (!seqc.haveTime || seqc.startPending || seqc_sl.transferOpen)
	{
		seqc.nextDue = 0;
		return;
	}
	seqc_tick_t tick;
	uint8_t track;
	const seqc_item_t item = seqc_nextItem(&tick, &track);
	if (seqc.mode != SEQC_SYNC_INTERNAL)
		seqc.nextDue = seqc_slaveDue(item, tick);
	else if (item == SEQC_ITEM_NONE)
		seqc.nextDue = SEQC_NEVER;
	else
		seqc.nextDue = seqc_internalTime(tick);
}

//------------------------------------------------------------------------------
// Slave clocks
//------------------------------------------------------------------------------
/** a MIDI clock or a jack pulse in its sync mode (seq_core.h, "Slave position") */
static void seqc_slaveClock(seqc_time_t stamp)
{
	if (seqc.state == SEQC_ARMED && seqc_sl.arm == SEQC_ARM_NEXT_CLOCK)
	{
		/* a Start or Continue resumes with this clock: the count takes the step
		 * phase of the position the clock reaches, as the master's song
		 * position does, so that tempoStep() and a later local start follow
		 * the steps that play (a Start and a song position give 0 anyway) */
		const double p = seqc_sl.lastClockPos + seqc_ticksPerClock();
		const seqc_tick_t k = seqc_floor(p / seqc_ticksPerClock());
		const seqc_tick_t cps = (seqc_tick_t)seqc_clocksPerStep();
		seqc_sl.clockCount = (uint32_t)(((k % cps) + cps) % cps);
	}
	const bool stepClock = seqc_sl.clockCount % seqc_clocksPerStep() == 0;

	seqc_sl.clockCount++;
	seqc_tempoClock(stamp);
	if (seqc.state == SEQC_ARMED && (stepClock || seqc_sl.arm == SEQC_ARM_NEXT_CLOCK))
	{
		seqc.state = SEQC_RUNNING;
		seqc.host.transport(seqc.host.ctx, seqc.atStart ? SEQC_TRANSPORT_START : SEQC_TRANSPORT_CONTINUE);
	}
	if (stepClock)
		seqc.host.tempoStep(seqc.host.ctx);
	if (seqc.state == SEQC_RUNNING)
	{
		/* the position jumps to the clock, and everything due on the way is handled */
		seqc_sl.lastClockPos += seqc_ticksPerClock();
		seqc_sl.lastClockTime = stamp;
		seqc_runTo(seqc_sl.lastClockPos);
	}
}
//------------------------------------------------------------------------------
/** MIDI Start, or the clock jack reset input: stop, rewind and reset the count */
static void seqc_syncReset(void)
{
	if (seqc.state != SEQC_STOPPED)
		seqc_stopTransport();
	seqc_rewindTransport();
	seqc_sl.clockCount = 0;
}

//------------------------------------------------------------------------------
// Interface
//------------------------------------------------------------------------------
/** Start of a seqc_ call that changes the state. A call from inside a callback
 * is ignored and counted (seq_core.h, "Execution context"). */
static bool seqc_enter(void)
{
	if (seqc.busy)
	{
		seqc.stats.reentries++;
		return false;
	}
	seqc.busy = true;
	seqc.nextDue = 0;	/* so that a nested seqc_process() reaches seqc_enter() */
	return true;
}
//------------------------------------------------------------------------------
/** end of such a call: the choked events leave the queue (seqc_queueChoke()) */
static void seqc_leave(void)
{
	seqc_queueCompact();
	seqc_schedule();
	seqc.busy = false;
}
//------------------------------------------------------------------------------
void seqc_init(const seqc_host_t *host, uint32_t cyclesPerSecond)
{
	if (seqc.busy)
	{
		seqc.stats.reentries++;	/* from a callback: ignored (N13.9) */
		return;
	}
	memset(&seqc, 0, sizeof seqc);
	memset(&seqc_trk, 0, sizeof seqc_trk);
	memset(&seqc_gen, 0, sizeof seqc_gen);
	memset(&seqc_queue, 0, sizeof seqc_queue);
	memset(&seqc_stage, 0, sizeof seqc_stage);
	memset(&seqc_fire, 0, sizeof seqc_fire);
	memset(&seqc_out, 0, sizeof seqc_out);
	memset(&seqc_skip, 0, sizeof seqc_skip);
	memset(&seqc_sl, 0, sizeof seqc_sl);

	seqc.host = *host;
	seqc.cyclesPerSecond = cyclesPerSecond != 0 ? cyclesPerSecond : SEQC_CPU_HZ;
	seqc.mode = SEQC_SYNC_INTERNAL;
	seqc.masterBpm = SEQC_DEFAULT_BPM;
	seqc.ticksPerCycle = seqc_ticksPerCycle(seqc.masterBpm);
	seqc.cyclesPerTick = 1.0 / seqc.ticksPerCycle;
	seqc.state = SEQC_STOPPED;
	seqc.atStart = true;
	seqc.absStep = -1;
	memset(seqc_trk.index, -1, sizeof seqc_trk.index);
	seqc_sl.ppq = 4;
	seqc_setSlaveBpm(seqc.masterBpm);
}
//------------------------------------------------------------------------------
void seqc_setSyncMode(seqc_sync_t mode)
{
	if (!seqc_enter())
		return;
	if (mode != seqc.mode && (mode == SEQC_SYNC_INTERNAL || mode == SEQC_SYNC_ANALOG || mode == SEQC_SYNC_MIDI))
	{
		const bool pendingStart = seqc.startPending;
		seqc.pos = seqc_positionAt(seqc.now);
		seqc.startPending = false;
		seqc.mode = mode;
		seqc_clearEstimate();
		seqc_sl.clockCount = 0;
		seqc_sl.haveStamp = false;
		seqc_sl.droppedOut = false;
		seqc_setSlaveBpm(seqc.masterBpm);
		seqc_placeWaitingPosition();
		if (mode == SEQC_SYNC_INTERNAL)
		{
			/* the position continues at the master tempo; an armed transport starts */
			seqc.anchorTime = seqc.now;
			seqc.anchorTick = seqc.pos;
			seqc_out.nextMidiClock = seqc_nextMultiple(seqc.pos, SEQC_TICKS_PER_MIDI_CLOCK);
			seqc_out.nextTempoStep = seqc_nextMultiple(seqc.pos, SEQC_TICKS_PER_STEP);
			if (seqc.state == SEQC_ARMED)
			{
				if (seqc_trk.seeked)
				{
					/* at the located step (seqc_placeWaitingPosition()): its
					 * MIDI clock and tempo step go out with it, as a start
					 * re-phases them to step 0 (N8) */
					seqc_out.nextMidiClock = (seqc_tick_t)seqc.absStep * SEQC_TICKS_PER_STEP;
					seqc_out.nextTempoStep = seqc_out.nextMidiClock;
				}
				seqc.state = SEQC_RUNNING;
				seqc.startPending = seqc.atStart;
				seqc.host.transport(seqc.host.ctx, seqc.atStart ? SEQC_TRANSPORT_START : SEQC_TRANSPORT_CONTINUE);
			}
		}
		else
		{
			/* the next clock adds one clock to the position */
			seqc_sl.lastClockPos = seqc.pos;
			seqc_sl.lastClockTime = seqc.now;
			/* an internal start that has not begun is a local start of a slave:
			 * its START was reported, so it stops (STOP) and is armed until the
			 * clock that begins a step, which reports START again */
			if (pendingStart)
			{
				seqc_stopTransport();
				seqc_arm(SEQC_ARM_STEP_CLOCK);
			}
		}
	}
	seqc_leave();
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
	if (seqc.mode != SEQC_SYNC_INTERNAL)
		seqc.pos = seqc_positionAt(seqc.now);	/* the slave rate may change: keep the position reached */
	seqc.masterBpm = b;
	seqc.ticksPerCycle = seqc_ticksPerCycle(b);
	seqc.cyclesPerTick = 1.0 / seqc.ticksPerCycle;
	if (seqc_sl.fit.slope <= 0.0)
		seqc_setSlaveBpm(b);	/* the first estimate while nothing is measured */
	seqc_leave();
}
//------------------------------------------------------------------------------
void seqc_setAnalogPpq(uint8_t ppq)
{
	if (!seqc_enter())
		return;
	if (seqc.mode == SEQC_SYNC_ANALOG)
		seqc.pos = seqc_positionAt(seqc.now);	/* the clamp may shrink: keep the position reached */
	seqc_sl.ppq = ppq == 24 ? 24 : 4;
	if (seqc.mode == SEQC_SYNC_ANALOG)
	{
		/* only the jack's count and estimate depend on the PPQ; in the other
		 * modes it is kept, and the switch to analog sync clears both */
		seqc_clearEstimate();
		seqc_sl.clockCount = 0;
		seqc_sl.haveStamp = false;
		seqc_placeWaitingPosition();
	}
	seqc_leave();
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
	if (!seqc_enter())
		return;
	if (!seqc_sameTransfer(ev, stamp))
	{
		if (seqc_sl.transferOpen)
			seqc_transferDone();	/* the transfer being collected is complete (N21) */
		seqc_sl.joinable = false;	/* and the last one takes no more clocks */
	}
	seqc_advance(stamp);

	const bool midi = seqc.mode == SEQC_SYNC_MIDI;
	const bool analog = seqc.mode == SEQC_SYNC_ANALOG;
	bool used = false;
	switch (ev)
	{
	case SEQC_EV_CLOCK:
	case SEQC_EV_PULSE:
		used = ev == SEQC_EV_CLOCK ? midi : analog;
		if (used)
			seqc_slaveClock(stamp);
		break;
	case SEQC_EV_START:
		used = midi || analog;
		if (used)
		{
			seqc_syncReset();
			seqc_arm(SEQC_ARM_NEXT_CLOCK);
		}
		break;
	case SEQC_EV_CONTINUE:
		used = (midi || analog) && seqc.state == SEQC_STOPPED;
		if (used)
			seqc_arm(SEQC_ARM_NEXT_CLOCK);
		break;
	case SEQC_EV_STOP:
		used = (midi || analog) && seqc.state != SEQC_STOPPED;
		if (used)
		{
			seqc.pos = seqc_positionAt(seqc.now);
			seqc_stopTransport();
		}
		break;
	case SEQC_EV_RESET_ON:
		used = analog;
		if (used)
			seqc_syncReset();
		break;
	case SEQC_EV_RESET_OFF:
		used = analog && seqc.state == SEQC_STOPPED;
		if (used)
			seqc_localStart();
		break;
	default:
		break;
	}
	if (!used)
		seqc.stats.syncIgnored++;
	seqc_leave();
}
//------------------------------------------------------------------------------
void seqc_songPosition(uint16_t sixteenths)
{
	if (!seqc_enter())
		return;
	if (seqc.mode == SEQC_SYNC_INTERNAL || seqc.state == SEQC_RUNNING)
		seqc.stats.syncIgnored++;
	else
		seqc_seek(sixteenths);
	seqc_leave();
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
	if (seqc_sl.transferOpen)
		seqc_transferDone();	/* seq_tick() drains every clock stamped up to now first (N21) */
	seqc_advance(now);
	seqc_leave();
}
//------------------------------------------------------------------------------
void seqc_skipOnce(uint8_t track, uint8_t step)
{
	if (seqc.state == SEQC_STOPPED || track >= SEQC_NUM_TRACKS || step >= SEQC_NUM_STEPS)
		return;
	const uint8_t pattern = seqc.host.activePattern(seqc.host.ctx);
	const uint32_t n = seqc_nextBoundaryStep();
	seqc_skip_t *m = &seqc_skip[track];	/* N18.1: only this track's marker */

	m->valid = true;
	m->pattern = pattern;
	m->step = step;
	m->absStep = n;
	seqc_queueRemoveStep(track, n, pattern, step);
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
	if (seqc.state != SEQC_RUNNING || seqc.absStep < 0)
		return 0.0f;
	const double x = seqc_position() - (double)seqc.absStep * SEQC_TICKS_PER_STEP;
	if (x <= 0.0)
		return 0.0f;
	/* 0.5 halfway between the swung grid times of steps n and n+1 (spec 6.7) */
	const double half = (SEQC_TICKS_PER_STEP + seqc_gen.swing) / 2.0;
	const double phase = x < half ? 0.5 * x / half
			: 0.5 + 0.5 * (x - half) / (SEQC_TICKS_PER_STEP - half);
	/* clamped after the conversion: a phase within 2^-25 of 1 rounds to 1.0f */
	const float f = (float)phase;
	return f < 1.0f ? f : 0.99999994f;
}
//------------------------------------------------------------------------------
float seqc_currentBpm(void)
{
	return (float)(seqc.mode == SEQC_SYNC_INTERNAL ? seqc.masterBpm : seqc_sl.bpm);
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
	out->queueDepth = (uint16_t)(seqc_queue.count + seqc_stage.count);
}
//------------------------------------------------------------------------------
void seqc_resetStats(void)
{
	if (seqc.busy)
	{
		seqc.stats.reentries++;
		return;
	}
	memset(&seqc.stats, 0, sizeof seqc.stats);
}
