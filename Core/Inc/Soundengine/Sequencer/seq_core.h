/*
 * seq_core.h
 *
 * Timing core of the LICHOMAT sequencer:
 * - the musical clock, 96 ticks per step, and the clock outputs;
 * - the per-track step counters and the pattern end of the master track;
 * - the event generator, with one step of lookahead (swing, shift, ratchet,
 *   probability, roll);
 * - the event queue, the per-track choke and the note-off timing;
 * - the sync inputs: MIDI clock and the clock in jack, with the tempo estimate,
 *   the interpolation between clocks, the transport and the song position;
 * - the recording phase and the skip-once markers.
 *
 * The core is plain C11. It has no HAL, CMSIS, menu, LED or preset code, no
 * interrupts and no allocation, so it builds in the firmware and on the host
 * (tests/). Pattern data, song mode and every output go through the callbacks in
 * seqc_host_t: sequencer.c implements them in the firmware, tests/fake_host.c on
 * the host.
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

#ifndef SEQ_CORE_H_
#define SEQ_CORE_H_

#include <stdint.h>
#include <stdbool.h>
#include "Preset/SeqStepTypes.h"

/*
 * Execution context (spec 4.3, N7)
 * --------------------------------
 * The core has a single context. In the firmware only seq_tick() calls it, and on
 * each call it:
 *   1. applies the mailbox of sequencer.c: seqc_setSyncMode(), seqc_setAnalogPpq(),
 *      seqc_setTempo(), seqc_stop(), seqc_rewind(), seqc_start(), and seqc_skipOnce()
 *      for the notes that seq_addNote() moved to the next step;
 *   2. drains the DIN, USB and jack sync queues, merged in stamp order, into
 *      seqc_syncEvent(), a queued Song Position Pointer into seqc_songPosition(),
 *      and a queued Program Change into the pattern switch, whose rewind while
 *      stopped goes through the mailbox, applied at once. It reads the time
 *      again before every entry, since handling one can load a kit, and stops
 *      when no entry is left;
 *   3. calls seqc_process() with the time of the drain's last read;
 *   4. parses MIDI input, which reaches the core through the mailbox, except for
 *      the seqc_skipOnce() of a recorded note: seq_postSkipOnce() calls it at once.
 * Other contexts (main loop, interrupts) never call a seqc_ function. They post
 * to the mailbox in sequencer.c instead.
 *
 * Callbacks run only from inside the core's own functions. Inside a callback,
 * the query functions and seqc_skipOnce() may be called. Any other seqc_ call
 * made from a callback is ignored and counted in seqc_stats_t.reentries. This
 * covers, for example, a kit autoload in patternEnd() that runs
 * soundengine_process() and so seq_tick() again.
 *
 * Time
 * ----
 * seqc_time_t counts CPU cycles from any origin. The firmware uses DWT->CYCCNT
 * extended to 64 bits, at SEQC_CPU_HZ. Times passed to the core must not go
 * backwards; a time earlier than the last one counts as the last one. A command
 * without a time argument acts at the core's current time, which is the time of
 * the last seqc_process() or seqc_syncEvent() call. Before the first such call
 * there is no current time, and a command acts at the time of that first call.
 *
 * Position
 * --------
 * The musical position is a double in ticks (N1): step n begins at tick n * 96,
 * and step 0 at tick 0. The absolute step n counts from 0 at a start from the
 * beginning, or from the SPP value after a song position.
 * - Internal sync: the position is computed from an anchor,
 *   pos = anchorPos + (now - anchorTime) * ticksPerCycle. The anchor moves only on
 *   a start, a tempo change, a switch to internal sync (at the position reached)
 *   and the core's first time (its first seqc_process() or seqc_syncEvent()
 *   call), so the position does not drift.
 * - Slave modes: the position follows the incoming clocks (spec 5, N9).
 *
 * Processing order (spec 6.2, 6.3; N3, N4)
 * ----------------------------------------
 * seqc_process(now) moves the position to its value at now. It then handles the
 * earliest item due at or before it, again and again, until nothing due is left.
 * The items are:
 * - on the transport timeline: step boundaries, queued events, note-offs and
 *   clock out pulses;
 * - on the free-running timeline (internal sync only): MIDI clocks and tempo
 *   steps.
 * At equal ticks the order is tempoStep(), the step boundary, noteOff(),
 * trigger(), clockOutPulse(), midiClockOut(). A due item is never dropped: after
 * a jump in a slave mode, the late items are handled in chronological order.
 *
 * At the boundary of step n the core:
 *   1. advances the absolute step to n and every track index by one. An index
 *      wraps to 0 at the track length (raw 0 means 16) or at 64;
 *   2. calls patternEnd() if the master track wrapped:
 *      - on SEQC_PAT_CHANGED, all track indices restart at 0;
 *      - on SEQC_PAT_STOP, the transport stops and rewinds, and steps 3 to 6
 *        are skipped;
 *   3. calls stepBoundary(n);
 *   4. queues the roll events of step n;
 *   5. generates the late part of step n: its hits at or after tick n * 96;
 *   6. generates the early part of step n+1: its hits before tick (n+1) * 96,
 *      which come from a negative shift. If the master track wraps at n+1, the
 *      pattern and indices come from peekNextPattern(); if it reports
 *      SEQC_NO_PATTERN (the song ends at n+1), no early part is generated.
 * The master track is the track of the active pattern with the largest raw length,
 * the lowest track number on a tie, as in fw 1.7 seq_getLongestTrack(). A raw 0
 * (16 steps) therefore loses to any non-zero length. The master track wraps when
 * ((index + 1) & 0x3f) == 0 or index + 1 >= its length.
 *
 * After a start from the beginning, every track index and the absolute step are
 * -1. At the first boundary, -1 + 1 = 0 counts as a wrap of the master track, so
 * patternEnd() is called at step 0 (N4). In song mode this moves the song pointer
 * from -1 to entry 0, as in fw 1.7. After a song position, the first boundary
 * does not advance anything (the counters are already set) and does not call
 * patternEnd(); steps 3 to 6 run as usual.
 *
 * Generation (spec 6.1)
 * ---------------------
 * For track t at absolute step n, the core reads the active pattern p and the
 * track step s.
 * - The step plays only if its main-step bit is set (stepOn()) and the
 *   probability draw r = 1 + random() % 100 is at most Step.prob.
 * - r is drawn once per (t, n), together with (p, s). If (p, s) differs between
 *   the early and the late part, a new draw is made.
 * - The early part keeps the swing and the StepTiming it read, whether the step
 *   played or not. The late part of the same (t, n, p, s) takes them instead of
 *   the values read at boundary n if they put a hit before tick n * 96, or if
 *   the values read now would. So a change of the shuffle or of the step's
 *   timing between the two boundaries plays no hit twice and loses none;
 *   otherwise an edit made before boundary n applies to step n.
 * - If the early part had a hit before tick n * 96, the late part follows it:
 *   it plays if the early part played, whatever stepOn() and Step.prob read at
 *   boundary n (the step data, velocity, note and p-locks, is read again), and
 *   otherwise not at all. If it had none, an edit of either made before
 *   boundary n applies, with the same draw. So the hits of a step play
 *   together or not at all, with two exceptions:
 *   - if (p, s) at boundary n differs from the early part's (a late change of
 *     the pending pattern, or of a track length), the early hits came from the
 *     prediction, and the late part is that of the step actually reached;
 *   - seqc_skipOnce() drops the hits that have not played yet.
 * - StepTiming.flam 0..5 gives k = 1, 2, 3, 4, 6 or 8 hits; any other value
 *   gives 1. Hit j (0..k-1) is at
 *       n * 96 + swing + shift + j * 96 / k
 *   where:
 *   - shift = (StepTiming.shift - 7) * 12 for 0..14, and 15 counts as 0;
 *   - swing = round(shuffle() * 64 / 127) on odd n, otherwise 0.
 * - Hits before tick 0 of a start, or before the first boundary after a song
 *   position, are never generated.
 * - Each event carries a copy of its step's data, so it still plays correctly
 *   after a pattern switch (D5).
 *
 * Firing (spec 6.4, N2)
 * ---------------------
 * - Mutes: an event of a muted track is dropped when it fires, without side
 *   effects. Roll events are exempt; they play on muted tracks, as in fw 1.7.
 * - Choke per track:
 *   - firing an event of (t, n) removes every queued event of track t with a
 *     lower absolute step;
 *   - of several events of one track due at the same tick, only the one with the
 *     highest n fires;
 *   - at the same n as well, only one fires, and a note event wins over a roll
 *     event.
 * - Queue order (N2): tick ascending, then absolute step descending, then note
 *   events before roll events, then track ascending. So events of different
 *   tracks with the same tick, absolute step and kind fire in track order; at
 *   one tick an event of a newer step fires first, whatever its track.
 * - Note-off: noteOff() comes 4 ticks after the trigger. If a track triggers again
 *   while its note-off is still pending, the core calls noteOff() right before the
 *   new trigger(). Stopping, rewinding and a slave dropout (N17) send all
 *   pending note-offs at once.
 */

//------------------------------------------------------------------------------
// Musical time
//------------------------------------------------------------------------------
#define SEQC_TICKS_PER_STEP         96u   /**< 384 PPQN; one step is a 16th note */
#define SEQC_TICKS_PER_BEAT        384u   /**< one quarter note */
#define SEQC_TICKS_PER_MIDI_CLOCK   16u   /**< 24 MIDI clocks per quarter note */
#define SEQC_TICKS_PER_PULSE         4u   /**< 96 PPQN grid of the clock out jack */
#define SEQC_PULSES_PER_STEP        24u
#define SEQC_MIDI_CLOCKS_PER_STEP    6u
#define SEQC_NOTE_OFF_TICKS          4u   /**< MIDI Note Off one pulse after the trigger */

//------------------------------------------------------------------------------
// Pattern data, sizes as in sequencer.h
//------------------------------------------------------------------------------
#define SEQC_NUM_TRACKS              7u
#define SEQC_NUM_STEPS              64u   /**< a track index wraps at 64 at the latest */
#define SEQC_NUM_PATTERNS           64u
#define SEQC_DEFAULT_LENGTH         16u   /**< length of a track whose raw length is 0 */
#define SEQC_SHIFT_CENTER            7u   /**< StepTiming.shift: 0..14 = -7..+7, 15 = 0 */
#define SEQC_SHIFT_UNIT_TICKS       12u   /**< +-7 units = +-84 ticks = +-7/8 step */
#define SEQC_MAX_SHIFT_TICKS        84u
#define SEQC_MAX_SHUFFLE           127u   /**< shuffle() range, PAR_SHUFFLE */
#define SEQC_MAX_SWING_TICKS        64u   /**< swing at shuffle 127: 2/3 of a step */
#define SEQC_ROLL_VELOCITY         100u   /**< velocity of roll events, ROLL_VOLUME in sequencer.h */

#define SEQC_NO_STEP              0xFFu   /**< seqc_trackStep() before the first step (fw 1.7: index -1) */
#define SEQC_NO_PATTERN           0xFFu   /**< peekNextPattern(): the song ends at the next pattern end */

//------------------------------------------------------------------------------
// Event queue
//------------------------------------------------------------------------------
#define SEQC_QUEUE_SIZE            256u   /**< events; 256 x 24 B = 6 KB (N2) */

//------------------------------------------------------------------------------
// Tempo and sync
//------------------------------------------------------------------------------
#define SEQC_CPU_HZ          216000000u   /**< cycles per second of the firmware time base (DWT->CYCCNT) */
#define SEQC_DEFAULT_BPM          120.0f  /**< tempo after seqc_init() */
#define SEQC_MASTER_BPM_MIN         1.0f  /**< seqc_setTempo() clamps to this range */
#define SEQC_MASTER_BPM_MAX       300.0f
#define SEQC_SLAVE_BPM_MIN         20.0f  /**< the slave tempo estimate is clamped to this range */
#define SEQC_SLAVE_BPM_MAX        300.0f
#define SEQC_TEMPO_WINDOW           24u   /**< short window of the tempo fit, clocks: MIDI clock, jack at 24 PPQ */
#define SEQC_TEMPO_WINDOW_4PPQ       4u   /**< short window of the tempo fit, clocks: jack at 4 PPQ */
#define SEQC_TEMPO_LONG_WINDOW      96u   /**< long window of the tempo fit, clocks: MIDI clock, jack at 24 PPQ (N21) */
#define SEQC_TEMPO_LONG_WINDOW_4PPQ  4u   /**< long window of the tempo fit, clocks: jack at 4 PPQ (the short one) */
#define SEQC_TEMPO_OUTLIER_PCT      50u   /**< a clock off the fitted line by more than this share of a clock is an outlier */
#define SEQC_TEMPO_JOIN_MS           2u   /**< a MIDI clock less than this after the last transfer joins it (see Slave position) */
#define SEQC_TEMPO_RESTART           4u   /**< outliers in a row on one side that restart the estimate (N16) */
#define SEQC_TEMPO_RESTART_4PPQ      2u   /**< the same for the jack at 4 PPQ */
#define SEQC_DROPOUT_MS            500u   /**< the shortest dropout time; slow clocks get longer (N15) */

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------
/** CPU cycles since an arbitrary origin, monotonic (the firmware: DWT->CYCCNT
 * extended to 64 bits) */
typedef uint64_t seqc_time_t;

/** musical position in ticks: step n begins at tick n * 96 */
typedef int64_t seqc_tick_t;

/** sync mode, the same values as seq_setExtSync() and seq_getExtSync() */
typedef enum
{
	SEQC_SYNC_INTERNAL = 0,		/**< master: the core's own tempo */
	SEQC_SYNC_ANALOG   = 1,		/**< slave to the clock in jack (seqc_setAnalogPpq()) */
	SEQC_SYNC_MIDI     = 2,		/**< slave to MIDI clock, DIN and USB */
} seqc_sync_t;

/** sync input events for seqc_syncEvent(). The MIDI events come from DIN and
 * USB, the PULSE and RESET events from the clock in jack. The table shows the
 * sync modes in which each one is accepted; in any other mode it is ignored and
 * counted in seqc_stats_t.syncIgnored (N4). */
typedef enum
{
	SEQC_EV_CLOCK = 0,	/**< MIDI Timing Clock, F8. Mode MIDI */
	SEQC_EV_START,		/**< MIDI Start, FA. Modes ANALOG and MIDI */
	SEQC_EV_CONTINUE,	/**< MIDI Continue, FB. Modes ANALOG and MIDI */
	SEQC_EV_STOP,		/**< MIDI Stop, FC. Modes ANALOG and MIDI */
	SEQC_EV_PULSE,		/**< clock in jack pulse. Mode ANALOG */
	SEQC_EV_RESET_ON,	/**< clock jack reset input became active. Mode ANALOG */
	SEQC_EV_RESET_OFF,	/**< clock jack reset input released. Mode ANALOG */
} seqc_sync_ev_t;
/* An event that does nothing in the current transport state is also counted as
 * ignored: a Continue while armed or running, a Stop while stopped, a reset
 * release while not stopped. */

/** transport state, see seqc_state() */
typedef enum
{
	SEQC_STOPPED = 0,	/**< nothing plays, the position is held */
	SEQC_ARMED,		/**< waiting for a clock to start (slave modes) */
	SEQC_RUNNING,		/**< playing */
} seqc_state_t;

/** what changed, passed to the transport() callback */
typedef enum
{
	SEQC_TRANSPORT_STOP = 0,	/**< playback stopped (it was running or armed); the position is kept */
	SEQC_TRANSPORT_START,		/**< playback starts now, at step 0 */
	SEQC_TRANSPORT_CONTINUE,	/**< playback resumes now, at the kept or SPP position */
	SEQC_TRANSPORT_ARM,		/**< armed: playback starts with a later clock */
	SEQC_TRANSPORT_REWIND,		/**< the position went back to the start (before step 0) */
} seqc_transport_t;

/** result of the patternEnd() callback */
typedef enum
{
	SEQC_PAT_SAME = 0,	/**< the same pattern continues; the track indices keep running */
	SEQC_PAT_CHANGED,	/**< a pattern was activated or reloaded; all track indices restart at 0 */
	SEQC_PAT_STOP,		/**< the song ended; the transport stops */
} seqc_pat_t;

/** seqc_event_t.flags */
#define SEQC_EVF_ROLL	0x01u	/**< a roll hit (spec 6.6), not a step hit */

/** One queued note event. It is self-contained: it plays the same way after a
 * pattern switch or an edit of its step (D5). 24 bytes (N2). */
typedef struct seqc_event
{
	seqc_tick_t	tick;		/**< when it fires, in ticks */
	uint32_t	absStep;	/**< n, the absolute step it belongs to */
	uint8_t		track;		/**< 0..6 */
	uint8_t		pattern;	/**< pattern the step was read from */
	uint8_t		step;		/**< track step s, 0..63 */
	uint8_t		hit;		/**< ratchet hit j, 0 = the step's first hit; 0 for a roll */
	uint8_t		vel;		/**< Step.volume & 0x7f, or SEQC_ROLL_VELOCITY for a roll */
	uint8_t		note;		/**< Step.note */
	uint8_t		flags;		/**< SEQC_EVF_* */
	uint8_t		reserved;	/**< 0 */
	uint8_t		p1Nr;		/**< copy of the step's p-locks: Step.param1Nr */
	uint8_t		p1Val;		/**< Step.param1Val */
	uint8_t		p2Nr;		/**< Step.param2Nr */
	uint8_t		p2Val;		/**< Step.param2Val */
} seqc_event_t;

_Static_assert(sizeof(seqc_event_t) == 24, "seqc_event_t is 24 bytes (N2)");

/** counters for the CPU page and the tests, see seqc_stats() */
typedef struct seqc_stats
{
	uint16_t queueDepth;		/**< events queued now; inside a callback it can still count events that a choke of the same call removed (N2) */
	uint16_t queueMax;		/**< largest queue depth since seqc_init() or seqc_resetStats() */
	uint32_t queueOverflow;		/**< events rejected because the queue was full */
	uint32_t chokeDrops;		/**< queued events removed by the choke or the same-tick rule; inside a callback it can lag: events that a choke of the same call removed are counted when they reach the head, at the end of the call, or when the queue is cleared (N2) */
	uint32_t muteDrops;		/**< events dropped when they fired because their track was muted */
	uint32_t skipDrops;		/**< step occurrences not played because of seqc_skipOnce() */
	uint32_t syncIgnored;		/**< sync events and song positions that did nothing (seqc_sync_ev_t) */
	uint32_t intervalsExcluded;	/**< outlier transfers left out of the tempo fit, also those of a row that then restarts it or that a spurious clock explains, but not the outlier that ends that row; a jack double trigger counts once (N16, N21) */
	uint32_t dropouts;		/**< slave clock dropouts: no clock for the dropout time (N15) */
	uint32_t reentries;		/**< calls ignored because they came from inside a callback */
	uint32_t tempoRestarts;		/**< slave tempo estimates restarted after a tempo jump (N16) */
} seqc_stats_t;

/** The host: pattern data, pattern sequencing and outputs (N5). Every pointer
 * must be set. ctx is passed back to each callback unchanged. */
typedef struct seqc_host
{
	void *ctx;

	/* -- pattern data, read only -- */
	/** true if the main-step bit of the step is set (seq_intIsMainStepActive) */
	bool		(*stepOn)(void *ctx, uint8_t pattern, uint8_t track, uint8_t step);
	/** the step's data: volume, prob, note and p-locks. Never NULL. It is read
	 * when the step's events are generated, and copied into them */
	const Step	*(*step)(void *ctx, uint8_t pattern, uint8_t track, uint8_t step);
	/** the step's ratchet (flam) and shift */
	StepTiming	(*timing)(void *ctx, uint8_t pattern, uint8_t track, uint8_t step);
	/** the raw length as stored in seq_patternSet.length. 0 means 16, and above 64
	 * the track wraps at 64 */
	uint8_t		(*trackLength)(void *ctx, uint8_t pattern, uint8_t track);
	/** PAR_SHUFFLE, 0..127; read at every step boundary. Larger values count as 127 */
	uint8_t		(*shuffle)(void *ctx);
	/** true if the track is muted; read when one of its events fires */
	bool		(*muted)(void *ctx, uint8_t track);
	/** 0 = no roll; otherwise the roll period in steps (16, 8, 4, 2 or 1).
	 * Read at every step boundary. The one-shot roll stays in seq_setRoll() */
	uint8_t		(*rollRate)(void *ctx, uint8_t track);

	/* -- pattern sequencing: song mode and pending patterns stay in sequencer.c -- */
	/** the pattern that plays now (seq_activePattern) */
	uint8_t		(*activePattern)(void *ctx);
	/** Called at the step boundary where the master track wraps, after the
	 * indices advanced, so the master track is at 0. It commits the next
	 * pattern (song mode, or a pending pattern) with the side effects of
	 * seq_setPendingPatternActive(): backup buffer, kit autoload, Program Change,
	 * LEDs, all notes off. When the same pattern continues it still refreshes the
	 * UI. Returns:
	 * - SEQC_PAT_CHANGED if a pattern was activated or reloaded;
	 * - SEQC_PAT_SAME if the same pattern continues;
	 * - SEQC_PAT_STOP at a song end. */
	seqc_pat_t	(*patternEnd)(void *ctx);
	/** What patternEnd() will do at the next pattern end, without side effects.
	 * Sets *pattern to the pattern that will play next, or to SEQC_NO_PATTERN if
	 * the song ends there. Returns true if the track indices will restart at 0
	 * (patternEnd() will return SEQC_PAT_CHANGED). Called at a boundary when the
	 * master track wraps at the next one, to generate the early hits of the next
	 * pattern's first step. */
	bool		(*peekNextPattern)(void *ctx, uint8_t *pattern);
	/** Song position (N6), called while stopped or armed. Puts the song where
	 * absolute step absStep lies, and returns the number of steps since the run
	 * of the pattern that plays there started. The core then sets each track
	 * index to steps mod length.
	 * - Pattern mode: returns absStep, with no side effects.
	 * - Song mode:
	 *   - walks the song from entry 0; each entry lasts the master length of its
	 *     pattern, and consecutive identical entries form one run;
	 *   - SONG_LOOP wraps the walk to entry 0;
	 *   - past SONG_END, the position is the last step of the last entry;
	 *   - activates the entry's pattern with the normal pattern-change side
	 *     effects and sets the song pointer to that entry. */
	uint32_t	(*seekPosition)(void *ctx, uint32_t absStep);

	/* -- outputs -- */
	/** An event fires. The glue applies the event's p-locks, triggers the voice
	 * and sends exactly one MIDI Note On (D4, D5): on the track's channel, with
	 * ev->vel, and with the PAR_MIDI_NOTE1+t override applied */
	void		(*trigger)(void *ctx, const seqc_event_t *ev);
	/** MIDI Note Off for the track's last note */
	void		(*noteOff)(void *ctx, uint8_t track);
	/** Step boundary n, after the counters advanced and after patternEnd(), and
	 * before step n is generated: LEDs, follow-bar, chase light, erase and the MTC
	 * check. A step erased here does not play, unless its early part already
	 * played hits before n * 96; its late hits then follow it (see Generation).
	 * It is not called at a song end */
	void		(*stepBoundary)(void *ctx, uint32_t absStep);
	/** Every step of the tempo, also while stopped (N8). It drives the tempo blink
	 * (led_blinkHandler()).
	 * - Internal sync: every 96 ticks of the free-running timeline; while
	 *   running, at each step boundary.
	 * - Slave modes: on every clock that begins a step of the clock count. */
	void		(*tempoStep)(void *ctx);
	/** Internal sync only: send one MIDI clock (F8). Called every 16 ticks of the
	 * free-running timeline, also while stopped. A start re-phases it: the first
	 * clock after transport(START) is at step 0 (N8), and the first one after a
	 * switch to internal sync that continues at a located step is at that step */
	void		(*midiClockOut)(void *ctx);
	/** While running, every 4 ticks of the position: one pulse of the clock out
	 * jack. pulseInStep is 0..23, and 0 at the step boundary. It is straight
	 * whatever the swing */
	void		(*clockOutPulse)(void *ctx, uint8_t pulseInStep);
	/** A transport change, see "Transport" at seqc_start(). The glue sends MIDI
	 * Start/Stop, calls trigger_reset(), resets the song pointer on REWIND and
	 * mirrors the state for seq_isRunning() */
	void		(*transport)(void *ctx, seqc_transport_t change);
	/** A random number for the probability draw, 1 + random() % 100. It is
	 * called once per generated step occurrence (track, absolute step) whose
	 * main-step bit is set, and again only if that step changed (see Generation) */
	uint32_t	(*random)(void *ctx);
} seqc_host_t;

//------------------------------------------------------------------------------
// Setup
//------------------------------------------------------------------------------
/** Reset the core and start it with this host. The host table is copied. The
 * core starts stopped and rewound, in internal sync at SEQC_DEFAULT_BPM, with the
 * analog PPQ at 4, an empty queue and zeroed stats. cyclesPerSecond is the rate
 * of seqc_time_t: SEQC_CPU_HZ in the firmware and in the host tests. No callback
 * is called. Called from inside a callback it is ignored and counted in
 * seqc_stats_t.reentries, like the other mutating calls (N13.9). */
void seqc_init(const seqc_host_t *host, uint32_t cyclesPerSecond);
//------------------------------------------------------------------------------
/** Set the sync mode (seq_setExtSync()). The transport state and the position
 * are kept, the slave tempo estimate and the clock count are cleared, and the
 * slave tempo starts again from the master tempo. A call with the mode in use,
 * or with a value other than 0..2, is ignored.
 * - Internal to slave: the position continues from where it is, and the next
 *   clock adds one clock's worth of ticks. A start that has not begun yet
 *   (seqc_start() with no seqc_process() or seqc_syncEvent() since) becomes the
 *   local start of a slave: its START was reported, so it stops (STOP) and is
 *   armed (ARM) until the clock that begins a step, which reports START again.
 * - Slave to internal: the position continues at the master tempo. An armed
 *   transport starts at once (transport START or CONTINUE). Armed at a located
 *   step, it plays that step at once, with its MIDI clock and tempoStep(): they
 *   are re-phased to it, as a start re-phases them to step 0.
 * - A transport that waits for its first clock after a rewind or a song
 *   position is moved to where the new mode starts it: one clock of the new
 *   mode before step 0 or the located step in a slave mode, so that the first
 *   clock plays that step, and at that step in internal sync. */
void seqc_setSyncMode(seqc_sync_t mode);
//------------------------------------------------------------------------------
/** Set the master tempo in BPM, clamped to SEQC_MASTER_BPM_MIN..MAX. The
 * transport position and the free-running timeline are re-anchored at the core's
 * current time, so both stay continuous. In a slave mode the value is kept for
 * internal sync. A call made while the fit has no slope (before its second
 * point, also after a dropout) also sets the tempo used until it has one; a
 * measured estimate stays, and a dropout keeps it as the last known tempo. */
void seqc_setTempo(float bpm);
//------------------------------------------------------------------------------
/** Set the PPQ of the clock in jack, 4 or 24 (PAR_PRESCALER_CLOCK_IN). Any other
 * value counts as 4.
 * - 4 PPQ: one pulse is 96 ticks and one step; the tempo fit uses the last
 *   4 intervals.
 * - 24 PPQ: one pulse is 16 ticks, 6 pulses make a step; the tempo fit uses
 *   windows of 24 and 96 intervals, as for MIDI clock.
 * The change applies from the next pulse. In analog sync the tempo estimate is
 * cleared and the clock count restarts, also when the value is the same, and a
 * transport that waits for its first pulse after a rewind or a song position
 * stays one pulse before its step. In the other modes the value is only kept for
 * analog sync: the MIDI clock count and the estimate stay. */
void seqc_setAnalogPpq(uint8_t ppq);

//------------------------------------------------------------------------------
// Transport (spec 5, N4)
//------------------------------------------------------------------------------
/*
 * Here a clock is a MIDI clock in MIDI sync and a jack pulse in analog sync.
 *
 * Each change is reported with the transport() callback, in this order: STOP,
 * then REWIND, then ARM, START or CONTINUE. REWIND is reported only when the
 * position was not at the start already. START and CONTINUE come before every
 * other callback of the step that begins playback, so the glue's MIDI Start and
 * trigger_reset(1) precede its first note, clock out pulse and MIDI clock.
 *
 * Local commands (the mailbox in sequencer.c):
 * - seqc_start() is a local start; it always starts from the beginning.
 *   - From stopped: rewind if the position is not at the start (REWIND).
 *   - Internal sync: start at once (START). Step 0 is at the time of the next
 *     seqc_process() or seqc_syncEvent() call, which handles it. MIDI clock out
 *     is re-phased, so its next clock is at step 0.
 *   - Slave modes: arm (ARM). Playback starts at step 0 (START) with the next
 *     clock that begins a step of the clock count: clock count % clocksPerStep
 *     == 0, counted before that clock. clocksPerStep is 6 for MIDI and for the
 *     jack at 24 PPQ, and 1 for the jack at 4 PPQ. The count runs all the time,
 *     and is reset by SEQC_EV_START, a song position and SEQC_EV_RESET_ON, so a
 *     local start lands on the master's 16th grid, as fw 1.7 sync_clockCnt did.
 *     The clock that resumes playback after a Continue gives the count the step
 *     phase of the position it reaches, as the master's song position resumes
 *     there too.
 *   - Ignored while armed or running.
 * - seqc_stop() stops and keeps the position (STOP). The queue and the skip-once
 *   markers are kept for a later continue, and pending note-offs are sent now.
 *   Ignored while stopped.
 * - seqc_rewind(), while stopped or armed: back to the start (REWIND, only if the
 *   position was not at the start). The queue, the pending note-offs, the
 *   probability draws and the skip-once markers are cleared. Ignored while
 *   running.
 *   - The local stop button is seqc_stop() then seqc_rewind(), as fw 1.7
 *     seq_resetPosition() plus seq_setRunning(0).
 *   - sequencer.c also rewinds after a pattern switch while stopped, as fw 1.7
 *     seq_setStepIndexToStart() did.
 *
 * Sync events (seqc_syncEvent()):
 * - SEQC_EV_START: stop if running or armed (STOP), rewind (REWIND), reset the
 *   clock count, arm (ARM). The next clock starts step 0 (START).
 * - SEQC_EV_CONTINUE, while stopped: arm (ARM). The next clock resumes playback
 *   (CONTINUE) one clock after the position of the last clock, which was kept
 *   at the stop or set by a song position. At the start position (after a
 *   rewind) it starts step 0 instead (START). After a song position 0 it is a
 *   CONTINUE at step 0, with no patternEnd(). Ignored while armed or running.
 * - SEQC_EV_STOP: like seqc_stop().
 * - SEQC_EV_RESET_ON: stop if running or armed (STOP), rewind (REWIND), reset the
 *   clock count. In fw 1.7 the reset input is a run gate, not a reset pulse (N4).
 * - SEQC_EV_RESET_OFF, while stopped: like seqc_start() in a slave mode. Playback
 *   starts at step 0 with the next pulse that begins a step.
 *
 * A song position (seqc_songPosition()) is accepted in the slave modes, while
 * stopped or armed. It seeks at once (seekPosition()), so a kit autoload in song
 * mode happens at the locate, not at the Continue, and it resets the clock count.
 * A Continue then resumes there.
 *
 * Slave position (spec 5, N9):
 * - While stopped, clocks only feed the clock count, the tempo estimate and
 *   tempoStep(); the position holds.
 * - While running, each clock sets a target: the position of the last clock plus
 *   one clock (16 ticks for MIDI and for the jack at 24 PPQ, 96 for the jack at
 *   4 PPQ). If the position is behind the target, it jumps there, and everything
 *   due in between is handled in order.
 * - Between clocks, the position advances at the estimated tempo. It is clamped
 *   to the next target, so it never runs ahead of the incoming clock, and it
 *   never goes back.
 * - The estimate (N21) is the slope of a least-squares fit of the clock stamps
 *   against the clock count: the clock period. It is clamped to
 *   SEQC_SLAVE_BPM_MIN..MAX. Without a fitted slope (fewer than two points),
 *   the last known tempo is used.
 * - Clocks that share a stamp, as the clocks of one USB transfer do, form a
 *   transfer: one point of the fit, at its last clock. A transfer is evaluated
 *   once it is complete, at the next clock with a later stamp, the next other
 *   sync event or the next seqc_process(), so seqc_currentBpm() includes the
 *   clocks given to seqc_syncEvent() from the next seqc_process() on.
 * - A MIDI clock less than SEQC_TEMPO_JOIN_MS after the newest stamp of the
 *   last transfer joins it, whether that transfer is still open or was
 *   evaluated (left out as an outlier, in the fit, or the end of a tempo jump
 *   that restarted the fit); the transfer then takes the clock's later stamp.
 *   So a burst of clocks forwarded back to back (a DIN interface sends one 0xF8
 *   per 0.32 ms) is one transfer, even when the core sees its clocks one by
 *   one, also among the first clocks of the fit, which are not tested. For a
 *   transfer already evaluated, the fit goes back to before it, and
 *   intervalsExcluded and tempoRestarts do not count it; until the next clock
 *   joins it, for less than SEQC_TEMPO_JOIN_MS, the estimate includes it. A
 *   transfer followed by another sync event takes no more clocks.
 * - The fit has two windows: the points of the last SEQC_TEMPO_WINDOW clocks
 *   and of the last SEQC_TEMPO_LONG_WINDOW clocks (SEQC_TEMPO_WINDOW_4PPQ and
 *   SEQC_TEMPO_LONG_WINDOW_4PPQ for the jack at 4 PPQ, where both are the same).
 *   The estimate is the slope of the long window. When the short window's
 *   slope differs from it by more than the steps of the stamps can explain, the
 *   long window drops its older points and becomes the short one, so a tempo
 *   change is followed within the short window.
 * - MIDI clock: a point further from the long window's fitted line than
 *   SEQC_TEMPO_OUTLIER_PCT of a clock is an outlier and is left out, unless it
 *   completes a tempo jump. Where the stamps move in steps (quantized to the
 *   host's audio buffers), the band is wider: about the size of the steps. A
 *   lost clock is a lasting step, which the line takes in: 1.5% off for about
 *   4 beats (N21).
 * - A spurious MIDI clock (E1.19, N21): the clocks after it are one clock early
 *   in the count. If it came 0.2 to 0.85 of a clock after a real one, they
 *   leave the band in a row; instead of a restart, the fit counts one clock
 *   fewer and leaves the spurious clock out, so the estimate stays within
 *   0.03% (with stamps precise to 1/8 of a clock; else it restarts, as
 *   before). Closer to a real clock the line takes it in as a step, as a lost
 *   clock: 1.57% for about 4 beats. tempoRestarts does not count it. The
 *   position counts it: a slave cannot tell it from a real clock.
 * - Clock in jack: an interval more than SEQC_TEMPO_OUTLIER_PCT off the fitted
 *   slope is an outlier and is left out, unless it completes a tempo jump; the
 *   fit goes on as if it had had the slope, so a lost pulse does not move the
 *   estimate. A double trigger (E1.19, N21), a second pulse between two of the
 *   master's, splits an interval into two short ones that add up to the
 *   slope: the fit leaves the second pulse out and counts one pulse fewer, so
 *   from the next real pulse on the estimate is as exact as after a lost pulse
 *   (it was 1.5% to 42.9% off for 3 to 5 pulses at 4 PPQ, and up to 5.0% at
 *   24 PPQ, before). The second pulse is the one of the two further from the
 *   line, so a contact bounce just after a real pulse is left out, not the
 *   real pulse. Between the two the estimate may include the second pulse,
 *   for one pulse. The two intervals must add up within 1/32 of a pulse: with
 *   stamps less precise, such as ±1 ms at 24 PPQ and 120 BPM, some double
 *   triggers cost what they did before (up to 5.0%). The position counts the
 *   second pulse.
 * - The points of the first half short window after a start of the fit are not
 *   tested.
 * - A tempo jump (N16): after SEQC_TEMPO_RESTART outliers in a row on the same
 *   side, all late or all early (SEQC_TEMPO_RESTART_4PPQ for the jack at
 *   4 PPQ), the fit restarts from those outliers alone, as after a dropout. A
 *   point inside the band ends the row; an outlier on the other side ends it
 *   and starts a new row of one. For MIDI clock, a tempo change that takes the
 *   clocks out of the band within a few clocks (about 15% or more) restarts the
 *   fit this way too. On the jack, the first two intervals after a jump to two
 *   to three times the tempo can add up to one old pulse, as after a double
 *   trigger; the pulse after them, half an old pulse or less later, undoes that
 *   merge, and the jump goes on as without it, one pulse later (E1.19, N21).
 * - The dropout time is SEQC_DROPOUT_MS, or two expected clock intervals if
 *   that is longer (N15). The interval comes from the estimate, or from
 *   SEQC_SLAVE_BPM_MIN while there is none, so a slow jack clock at 4 PPQ can
 *   build its first estimate. After the dropout time without a clock, the
 *   estimate is cleared, the pending note-offs are sent (N17) and the position
 *   holds. The next clock resumes playback, and its interval is not used for
 *   the estimate.
 */
/** local start, see Transport */
void seqc_start(void);
//------------------------------------------------------------------------------
/** stop and keep the position, see Transport. The queue and the skip-once
 * markers are kept for a later continue. */
void seqc_stop(void);
//------------------------------------------------------------------------------
/** go back to the start while stopped or armed, see Transport */
void seqc_rewind(void);
//------------------------------------------------------------------------------
/** A timestamped sync event, from the sync queues of seq_platform (spec 7.2).
 * Everything due before the stamp is processed first, so callbacks can run from
 * here; then the event is applied at the stamp.
 * - The caller unwraps the stamp to 64 bits; a stamp later than seqp_now()
 *   counts as now (spec 4.4).
 * - A stamp earlier than the core's current time is handled at the current time,
 *   since the core never goes back. Its own value is still used for the tempo
 *   estimate and as the anchor of the interpolation.
 * - The caller applies the RX filter (MIDI_FILTER_SYSTEM). The sync-mode gating
 *   is applied here, see seqc_sync_ev_t. */
void seqc_syncEvent(seqc_sync_ev_t ev, seqc_time_t stamp);
//------------------------------------------------------------------------------
/** MIDI Song Position Pointer, in 16ths (0..16383); one 16th is one step. It is
 * accepted in the slave modes while stopped or armed. It seeks at once:
 * - the absolute step becomes sixteenths, so swing parity follows the master;
 * - the track indices come from seekPosition() (N6), and the master track is
 *   that of the pattern it activates;
 * - the last clock position is set to sixteenths * 96 minus one clock, so the
 *   next clock plays that step (also after a later change of the sync mode or
 *   the PPQ, see seqc_setSyncMode());
 * - as at a rewind, the queue, the pending note-offs, the probability draws,
 *   the same-tick state and the skip-once markers are cleared;
 * - the clock count is reset.
 * While running, or in internal sync, it is ignored and counted in
 * seqc_stats_t.syncIgnored (the MIDI spec only expects SPP while stopped). */
void seqc_songPosition(uint16_t sixteenths);

//------------------------------------------------------------------------------
// Processing
//------------------------------------------------------------------------------
/** Advance to now and handle everything due up to there, see Processing order.
 * When nothing is due yet it returns at once (N10): a 64-bit compare, and now
 * is kept as the core's current time for the queries. */
void seqc_process(seqc_time_t now);

//------------------------------------------------------------------------------
// Recording (spec 6.7)
//------------------------------------------------------------------------------
/** D6: the next occurrence of step on track does not play. seq_addNote() calls
 * this when it moves a recorded note forward to the next step while running.
 * - The marker belongs to the active pattern at the time of the call.
 * - Hits of that occurrence that are already queued (early hits from a negative
 *   shift) are removed at once.
 * - At the track's next step boundary, if the track arrives at (pattern, step),
 *   the occurrence is not generated. The marker is cleared at that boundary
 *   whether or not it matched.
 * - Each track has its own marker (N18.1), so notes recorded on several tracks
 *   in the same step, a chord for example, each skip once. A new call replaces
 *   the marker of its own track only.
 * - Ignored while stopped, or with track >= 7 or step >= 64. It may be called
 *   from a callback. */
void seqc_skipOnce(uint8_t track, uint8_t step);

//------------------------------------------------------------------------------
// Queries. Pure reads, allowed at any time in the core's context, also inside
// callbacks; there they show the state being processed.
//------------------------------------------------------------------------------
/** The track's step index 0..63 (replaces seq_stepIndex[]). SEQC_NO_STEP before
 * the first step after a start, or for track >= 7 */
uint8_t seqc_trackStep(uint8_t track);
//------------------------------------------------------------------------------
/** The absolute step n of the current step; -1 before the first step after a
 * start. After a song position it is the SPP value. */
int32_t seqc_absStep(void);
//------------------------------------------------------------------------------
/** The master track of the pattern that plays from the last step boundary on,
 * chosen there after patternEnd(): at a pattern change, that of the new pattern
 * (the wrap was decided with the old one). After a song position it is the
 * master track of the pattern that the song position activated. 0 after
 * seqc_init() and after a rewind, until the first boundary. */
uint8_t seqc_masterTrack(void);
//------------------------------------------------------------------------------
/** The musical position in ticks: its value at the core's current time, or,
 * inside a callback, the tick of the item being handled. At the start (before
 * step 0) it is 0 in internal sync, and minus one clock in the slave modes, so
 * that the next clock brings it to 0. The free-running timeline is not shown
 * here. */
double seqc_position(void);
//------------------------------------------------------------------------------
/** Recording phase, 0..1, of the current step n at the current position.
 * seq_getElapsedStepTimePercent() returns it x 100, and seq_addNote() moves a
 * note to the next step at 0.5 or more (spec 6.7).
 * - 0 at the step boundary n * 96.
 * - 0.5 at n * 96 + (96 + swing) / 2, where swing = round(shuffle * 64 / 127)
 *   whatever the parity of n. That is halfway between the swung grid times of
 *   steps n and n+1, so the 0.5 rule picks the nearer step, also with shuffle.
 * - Linear in between, and on towards 1 at the next boundary.
 * - With shuffle 0 it is (pos - n * 96) / 96.
 * - 0 while stopped or armed, and before the first step. */
float seqc_stepPhase(void);
//------------------------------------------------------------------------------
/** The tempo in BPM: the master tempo in internal sync, the estimate in the
 * slave modes, or the last known tempo while there is no estimate: the last
 * estimate, or the master tempo after seqc_init(), after a switch of the sync
 * mode or after a seqc_setTempo() call while the fit has no slope. sequencer.c
 * rounds it for seq_getBpm() and lfo_recalcSync() (N9). */
float seqc_currentBpm(void);
//------------------------------------------------------------------------------
/** stopped, armed or running */
seqc_state_t seqc_state(void);
//------------------------------------------------------------------------------
/** true while armed or running: the fw 1.7 meaning of seq_isRunning() (N7) */
bool seqc_isRunning(void);
//------------------------------------------------------------------------------
/** copy the counters into out */
void seqc_stats(seqc_stats_t *out);

//------------------------------------------------------------------------------
// Statistics reset
//------------------------------------------------------------------------------
/** Reset queueMax and every counter to 0; queueDepth stays live. Not a query:
 * like the other mutating calls, it is ignored inside a callback and counted in
 * seqc_stats_t.reentries (N13.9). */
void seqc_resetStats(void);

#ifdef SEQC_QUEUE_TEST
//------------------------------------------------------------------------------
// Test only: the event queue of a core built with -DSEQC_QUEUE_TEST
// (tests/test_queue.c). The firmware never defines it.
//------------------------------------------------------------------------------
/** queue elements written since the program started: the staging and the merge
 * of new events, and the compaction of the choke and of skip-once (N2);
 * seqc_init() keeps it (the tests take differences) */
extern uint32_t seqc_queueMoves;
/** queue ev on its own, as one insert outside a step boundary */
void seqc_testQueueInsert(const seqc_event_t *ev);
/** queue the count events of evs together, in this order, as the event
 * generator queues the events of one step boundary */
void seqc_testQueueInsertBatch(const seqc_event_t *evs, uint16_t count);
/** take the next event; false if the queue is empty */
bool seqc_testQueuePop(seqc_event_t *out);
/** the choke of a fired event (track, absStep), compacted at once; returns how
 * many were removed */
uint32_t seqc_testQueueChoke(uint8_t track, uint32_t absStep);
/** the slot of the next event to fire in the queue's ring */
uint16_t seqc_testQueueHead(void);
#endif

#endif /* SEQ_CORE_H_ */
