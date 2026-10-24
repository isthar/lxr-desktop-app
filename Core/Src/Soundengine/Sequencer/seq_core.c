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
/** the probability draw of one step occurrence (N3), and the timing its early
 * part used */
typedef struct
{
	bool		valid;
	uint32_t	absStep;
	uint8_t		pattern;
	uint8_t		step;
	uint8_t		r;		/**< 1..100 */
	bool		timed;		/**< the early part was generated: hits and offset are set */
	uint8_t		hits;		/**< the ratchet hits it used */
	int16_t		offset;		/**< the swing + shift it used, ticks after absStep * 96 */
} seqc_draw_t;

//------------------------------------------------------------------------------
/** event generation */
typedef struct
{
	seqc_draw_t	draw[SEQC_NUM_TRACKS][2];	/**< N3: slots for n and n+1, by absStep & 1 */
	uint8_t		swing;				/**< swing ticks read at the last boundary */
} seqc_gen_t;

//------------------------------------------------------------------------------
/** the event queue, sorted so that events[count - 1] fires next (N2) */
typedef struct
{
	seqc_event_t	events[SEQC_QUEUE_SIZE];
	uint16_t	count;
} seqc_queue_t;

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
/** the skip-once marker (D6) */
typedef struct
{
	bool		valid;
	uint8_t		track;
	uint8_t		pattern;
	uint8_t		step;
	uint32_t	absStep;	/**< the occurrence it applies to */
} seqc_skip_t;

//------------------------------------------------------------------------------
/* the state, one part per area */
static seqc_core_t seqc;
static seqc_tracks_t seqc_trk;
static seqc_gen_t seqc_gen;
static seqc_queue_t seqc_queue;
static seqc_fire_t seqc_fire;
static seqc_outputs_t seqc_out;
static seqc_skip_t seqc_skip;

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
	static const uint8_t hits[6] = { 1, 2, 3, 4, 6, 8 };
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
// Event queue
//------------------------------------------------------------------------------
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
/** queue an event, in firing order: a binary search plus memmove (N2). A full
 * queue rejects it. */
static void seqc_queueInsert(const seqc_event_t *ev)
{
	if (seqc_queue.count >= SEQC_QUEUE_SIZE)
		return;
	/* the first event that fires before ev, and all above it, move up one */
	uint16_t lo = 0;
	uint16_t hi = seqc_queue.count;
	while (lo < hi)
	{
		const uint16_t mid = (uint16_t)((lo + hi) / 2u);
		if (seqc_firesBefore(&seqc_queue.events[mid], ev))
			hi = mid;
		else
			lo = (uint16_t)(mid + 1u);
	}
	memmove(&seqc_queue.events[lo + 1u], &seqc_queue.events[lo],
			(size_t)(seqc_queue.count - lo) * sizeof seqc_queue.events[0]);
	seqc_queue.events[lo] = *ev;
	seqc_queue.count++;
}
//------------------------------------------------------------------------------
/** choke: remove the queued events of track with an absolute step below absStep.
 * Returns how many were removed. */
static uint32_t seqc_queueChoke(uint8_t track, uint32_t absStep)
{
	uint16_t kept = 0;
	for (uint16_t i = 0; i < seqc_queue.count; i++)
	{
		const seqc_event_t *ev = &seqc_queue.events[i];
		if (ev->track == track && ev->absStep < absStep)
			continue;
		seqc_queue.events[kept++] = *ev;
	}
	const uint32_t removed = (uint32_t)(seqc_queue.count - kept);
	seqc_queue.count = kept;
	return removed;
}
//------------------------------------------------------------------------------
/** remove the queued step hits of one occurrence (skip-once) */
static void seqc_queueRemoveStep(uint8_t track, uint32_t absStep, uint8_t pattern, uint8_t step)
{
	uint16_t kept = 0;
	for (uint16_t i = 0; i < seqc_queue.count; i++)
	{
		const seqc_event_t *ev = &seqc_queue.events[i];
		if (ev->track == track && ev->absStep == absStep && ev->pattern == pattern && ev->step == step
				&& (ev->flags & SEQC_EVF_ROLL) == 0)
			continue;
		seqc_queue.events[kept++] = *ev;
	}
	seqc_queue.count = kept;
}

//------------------------------------------------------------------------------
// Firing
//------------------------------------------------------------------------------
/** send every pending note-off now (stop, rewind, seek) */
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
	const seqc_event_t ev = seqc_queue.events[--seqc_queue.count];
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
	seqc.stats.chokeDrops += seqc_queueChoke(t, ev.absStep);
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
/** the draw slot of occurrence (track, absStep) at (pattern, step), N3: r is
 * drawn once and kept with (pattern, step); a different step gets a new draw */
static seqc_draw_t *seqc_draw(uint8_t track, uint32_t absStep, uint8_t pattern, uint8_t step)
{
	seqc_draw_t *d = &seqc_gen.draw[track][absStep & 1u];
	if (!d->valid || d->absStep != absStep || d->pattern != pattern || d->step != step)
	{
		const uint32_t v = seqc.host.random(seqc.host.ctx);
		d->valid = true;
		d->absStep = absStep;
		d->pattern = pattern;
		d->step = step;
		d->r = (uint8_t)(1u + v % 100u);
		d->timed = false;
	}
	return d;
}
//------------------------------------------------------------------------------
/** queue the hits of occurrence (track, absStep) at (pattern, step) that belong to
 * one part (N3): the early part has the hits before absStep * 96, the late part
 * the rest.
 * The early part keeps the timing it used (swing + shift, ratchet hits) in the
 * draw slot. The late part reads the timing again, so an edit or a shuffle change
 * before the boundary applies, but it keeps the early part's timing instead if
 * the early part queued a hit, or if the timing read now would put a hit before
 * absStep * 96, which has passed. Either way every hit is generated once
 * (decision 9 of epic E1). */
static void seqc_generate(uint8_t track, uint32_t absStep, uint8_t pattern, uint8_t step, bool early)
{
	if (!seqc.host.stepOn(seqc.host.ctx, pattern, track, step))
		return;
	seqc_draw_t *d = seqc_draw(track, absStep, pattern, step);
	const Step *data = seqc.host.step(seqc.host.ctx, pattern, track, step);
	if (d->r > data->prob)
		return;
	const StepTiming timing = seqc.host.timing(seqc.host.ctx, pattern, track, step);
	uint8_t hits = seqc_hitCount(timing.flam);
	int offset = ((absStep & 1u) ? seqc_gen.swing : 0) + seqc_shiftTicks(timing.shift);
	if (early)
	{
		d->timed = true;
		d->hits = hits;
		d->offset = (int16_t)offset;
	}
	else if (d->timed && (d->offset < 0 || offset < 0))
	{
		hits = d->hits;
		offset = d->offset;
	}
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
		seqc_queueInsert(&ev);
	}
}
//------------------------------------------------------------------------------
/** true if the skip-once marker drops occurrence (track, n) at its boundary. A
 * marker for n is cleared here, whether it matches or not. */
static bool seqc_skipLate(uint8_t track, uint32_t n, uint8_t pattern, uint8_t step)
{
	if (!seqc_skip.valid || seqc_skip.track != track || seqc_skip.absStep > n)
		return false;
	seqc_skip.valid = false;
	if (seqc_skip.absStep != n || seqc_skip.pattern != pattern || seqc_skip.step != step)
		return false;
	seqc.stats.skipDrops++;
	return true;
}
//------------------------------------------------------------------------------
/** true if the skip-once marker drops the early part of occurrence (track, n) */
static bool seqc_skipEarly(uint8_t track, uint32_t n, uint8_t pattern, uint8_t step)
{
	return seqc_skip.valid && seqc_skip.track == track && seqc_skip.absStep == n
			&& seqc_skip.pattern == pattern && seqc_skip.step == step;
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
		seqc_queueInsert(&ev);
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
 * state and the skip-once marker (rewind) */
static void seqc_clearRun(void)
{
	seqc_queue.count = 0;
	seqc_flushNoteOffs();
	memset(seqc_gen.draw, 0, sizeof seqc_gen.draw);
	memset(seqc_fire.fired, 0, sizeof seqc_fire.fired);
	seqc_skip.valid = false;
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
	seqc.pos = 0.0;
	seqc_out.nextPulse = 0;
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
	seqc_gen.swing = seqc_swingTicks(seqc.host.shuffle(seqc.host.ctx));
	seqc.host.stepBoundary(seqc.host.ctx, n);
	seqc_queueRolls(n, pattern);
	seqc_generateLate(n, pattern);
	seqc_generateEarly(n, pattern);
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
	seqc_consider(SEQC_ITEM_TEMPO_STEP, seqc_out.nextTempoStep, 0, &best, tick, track);
	seqc_consider(SEQC_ITEM_MIDI_CLOCK, seqc_out.nextMidiClock, 0, &best, tick, track);
	if (seqc.state == SEQC_RUNNING && !seqc.startPending)
	{
		seqc_consider(SEQC_ITEM_BOUNDARY, (seqc_tick_t)seqc_nextBoundaryStep() * SEQC_TICKS_PER_STEP, 0,
				&best, tick, track);
		for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
			if (seqc_fire.offPending[t])
				seqc_consider(SEQC_ITEM_NOTE_OFF, seqc_fire.offTick[t], t, &best, tick, track);
		if (seqc_queue.count > 0)
			seqc_consider(SEQC_ITEM_EVENT, seqc_queue.events[seqc_queue.count - 1u].tick, 0, &best, tick, track);
		seqc_consider(SEQC_ITEM_PULSE, seqc_out.nextPulse, 0, &best, tick, track);
	}
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
	memset(&seqc_gen, 0, sizeof seqc_gen);
	memset(&seqc_queue, 0, sizeof seqc_queue);
	memset(&seqc_fire, 0, sizeof seqc_fire);
	memset(&seqc_out, 0, sizeof seqc_out);
	memset(&seqc_skip, 0, sizeof seqc_skip);

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
	if (seqc.state == SEQC_STOPPED || track >= SEQC_NUM_TRACKS || step >= SEQC_NUM_STEPS)
		return;
	const uint8_t pattern = seqc.host.activePattern(seqc.host.ctx);
	const uint32_t n = seqc_nextBoundaryStep();

	seqc_skip.valid = true;
	seqc_skip.track = track;
	seqc_skip.pattern = pattern;
	seqc_skip.step = step;
	seqc_skip.absStep = n;
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
	return phase < 1.0 ? (float)phase : 0.999999f;
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
