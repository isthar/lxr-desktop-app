/*
 * fake_host.h
 *
 * Fake host for the seq_core host tests. It implements every seqc_host_t callback
 * the way sequencer.c will:
 * - the pattern data in plain arrays (64 patterns x 7 tracks x 64 steps), with the
 *   track lengths, shuffle, mutes and roll rates;
 * - pattern sequencing and song mode with the fw 1.7 rules of
 *   seq_determineNextPattern(), seq_setPendingPatternActive() and
 *   seq_setNextPattern(): active and pending pattern, the reload flag, the song
 *   list with SONG_END and SONG_LOOP;
 * - the song position walk of seekPosition() (N6);
 * - the glue's erase at the step boundary and its recording (seq_addNote(), and
 *   roll notes at their own step);
 * - a seeded random generator, which can also play a script;
 * - a recorder that logs the output and sequencing callbacks with the host time
 *   and the core's tick, and counts every callback.
 *
 * Usage:
 *   fake_host_t *h = fake_init();          // reset + seqc_init(), at the start of each test
 *   fake_setStep(h, 0, 0, 4, true);        // pattern 0, track 0, step 4 on
 *   fake_process(h, 0);                    // the core's time starts at 0
 *   seqc_start();                          // step 0 at the next seqc_process(), 0.1 ms
 *   fake_runUntil(h, fake_msToCycles(600.0), fake_msToCycles(0.1));
 *   ASSERT_EQ(fake_count(h, FAKE_TRIGGER), 1);   // step 4 at 120 BPM: 500.1 ms
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef FAKE_HOST_H_
#define FAKE_HOST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "Soundengine/Sequencer/seq_core.h"

/* sizes and markers, the same values as sequencer.h */
#define FAKE_NUM_PATTERNS	64	/* NUM_PATTERN, MAX_PATTERN */
#define FAKE_NUM_TRACKS		7	/* NUM_TRACKS */
#define FAKE_NUM_STEPS		64	/* NUM_STEPS */
#define FAKE_SONG_LENGTH	64	/* MAX_SONG_LENGTH */
#define FAKE_SONG_END		64	/* SONG_END = MAX_PATTERN */
#define FAKE_SONG_LOOP		65	/* SONG_LOOP = SONG_END + 1 */
#define FAKE_DEFAULT_NOTE	60	/* SEQ_DEFAULT_NOTE */
#define FAKE_NO_AUTOMATION	0xFF	/* NO_AUTOMATION, MidiMessages.h */
#define FAKE_STEP_ACTIVE_MASK	0x80	/* STEP_ACTIVE_MASK */
#define FAKE_NO_SONG_POS	0xFF	/* seq_songPositionPointer before the first entry (-1) */

#define FAKE_LOG_SIZE		65536	/* log entries; later ones are only counted */

//------------------------------------------------------------------------------
// Time: the firmware time base, SEQC_CPU_HZ = 216 MHz
//------------------------------------------------------------------------------
static inline seqc_time_t fake_msToCycles(double ms) { return (seqc_time_t)(ms * (SEQC_CPU_HZ / 1000.0) + 0.5); }
static inline double fake_cyclesToMs(seqc_time_t cycles) { return (double)cycles / (SEQC_CPU_HZ / 1000.0); }
/** cycles of one step (a 16th) at bpm */
static inline double fake_stepCycles(double bpm) { return SEQC_CPU_HZ * 60.0 / (bpm * 4.0); }
/** cycles of one tick (1/96 step) at bpm */
static inline double fake_tickCycles(double bpm) { return SEQC_CPU_HZ * 60.0 / (bpm * SEQC_TICKS_PER_BEAT); }
/** cycles between two MIDI clocks (24 per quarter note) at bpm */
static inline double fake_clockCycles(double bpm) { return SEQC_CPU_HZ * 60.0 / (bpm * 24.0); }

//------------------------------------------------------------------------------
// Recorder
//------------------------------------------------------------------------------
/** callbacks that are logged */
typedef enum
{
	FAKE_TRIGGER = 0,	/**< trigger(): ev */
	FAKE_NOTE_OFF,		/**< noteOff(): track */
	FAKE_STEP_BOUNDARY,	/**< stepBoundary(): arg = absStep */
	FAKE_TEMPO_STEP,	/**< tempoStep() */
	FAKE_MIDI_CLOCK,	/**< midiClockOut() */
	FAKE_CLOCK_PULSE,	/**< clockOutPulse(): value = pulseInStep */
	FAKE_TRANSPORT,		/**< transport(): value = seqc_transport_t */
	FAKE_PATTERN_END,	/**< patternEnd(): value = seqc_pat_t, pattern = active pattern after it */
	FAKE_PEEK,		/**< peekNextPattern(): pattern = *pattern, flag = result */
	FAKE_SEEK,		/**< seekPosition(): arg = absStep, result = steps, pattern = active pattern after it */
	FAKE_NUM_KINDS
} fake_kind_t;

/** callbacks that are only counted (pattern reads) */
typedef enum
{
	FAKE_READ_STEP_ON = 0,
	FAKE_READ_STEP,
	FAKE_READ_TIMING,
	FAKE_READ_LENGTH,
	FAKE_READ_SHUFFLE,
	FAKE_READ_MUTED,
	FAKE_READ_ROLL_RATE,
	FAKE_READ_ACTIVE_PATTERN,
	FAKE_READ_RANDOM,
	FAKE_NUM_READS
} fake_read_t;

#define FAKE_LOG_BIT(kind)	(1u << (kind))
#define FAKE_LOG_ALL		((1u << FAKE_NUM_KINDS) - 1u)
/** everything but the per-pulse and per-clock outputs, for long runs */
#define FAKE_LOG_NO_CLOCKS	(FAKE_LOG_ALL & ~(FAKE_LOG_BIT(FAKE_CLOCK_PULSE) | FAKE_LOG_BIT(FAKE_MIDI_CLOCK)))

/** one logged callback */
typedef struct fake_rec
{
	fake_kind_t	kind;
	seqc_time_t	time;		/**< host time of the core call it came from (fake_host_t.now) */
	double		tick;		/**< seqc_position() during the callback */
	int32_t		absStep;	/**< seqc_absStep() during the callback */
	uint8_t		track;		/**< NOTE_OFF */
	uint8_t		value;		/**< CLOCK_PULSE, TRANSPORT, PATTERN_END: see fake_kind_t */
	uint8_t		pattern;	/**< PATTERN_END, PEEK, SEEK */
	bool		flag;		/**< PEEK */
	uint32_t	arg;		/**< STEP_BOUNDARY, SEEK */
	uint32_t	result;		/**< SEEK */
	seqc_event_t	ev;		/**< TRIGGER */
} fake_rec_t;

//------------------------------------------------------------------------------
// The fake host. Tests may read and write every field.
//------------------------------------------------------------------------------
typedef struct fake_host
{
	seqc_host_t	host;		/**< the callback table, ctx = this host */
	seqc_time_t	now;		/**< host time of the current core call, set by fake_process() and fake_sync() */

	/* pattern data (seq_patternSet) */
	Step		steps[FAKE_NUM_PATTERNS][FAKE_NUM_TRACKS][FAKE_NUM_STEPS];
	StepTiming	timing[FAKE_NUM_PATTERNS][FAKE_NUM_TRACKS][FAKE_NUM_STEPS];
	uint64_t	mainSteps[FAKE_NUM_PATTERNS][FAKE_NUM_TRACKS];	/**< bit s = main step s is on */
	uint8_t		length[FAKE_NUM_PATTERNS][FAKE_NUM_TRACKS];	/**< raw: 0 means 16 */
	uint8_t		shuffle;					/**< PAR_SHUFFLE 0..127 */
	bool		muted[FAKE_NUM_TRACKS];
	uint8_t		rollRate[FAKE_NUM_TRACKS];			/**< what rollRate() returns: 0, or 16/8/4/2/1 */

	/* pattern sequencing (sequencer.c) */
	uint8_t		activePattern;			/**< seq_activePattern */
	uint8_t		pendingPattern;			/**< seq_pendingPattern */
	bool		reload;				/**< seq_loadPendigFlag */
	bool		songMode;			/**< seq_songModeEnabled */
	uint8_t		song[FAKE_SONG_LENGTH];		/**< the active song: patterns, FAKE_SONG_END, FAKE_SONG_LOOP */
	uint8_t		songPos;			/**< seq_songPositionPointer, FAKE_NO_SONG_POS before the first entry */
	uint32_t	patternChanges;			/**< activations with the pattern-change side effects */
	uint32_t	seekLengths;			/**< master lengths the last seekPosition() read (E4.1 F33) */

	/* glue behaviour */
	bool		recordActive;			/**< seq_recordActive */
	bool		eraseActive;			/**< seq_eraseActive */
	uint8_t		activeTrack;			/**< frontParser_activeTrack: the track erase works on */
	void		(*onStepBoundary)(struct fake_host *h, uint32_t absStep);	/**< test hook, after the erase */
	void		(*onTrigger)(struct fake_host *h, const seqc_event_t *ev);	/**< test hook, after logging */

	/* random(): a 64-bit LCG, or a script that is played first */
	uint64_t	rngState;
	const uint32_t	*rngScript;
	size_t		rngScriptLen;
	size_t		rngScriptPos;

	/* recorder */
	uint32_t	logMask;			/**< FAKE_LOG_BIT()s of the kinds that are logged */
	size_t		logCount;			/**< entries in log */
	uint32_t	logLost;			/**< entries not stored because the log was full */
	uint32_t	calls[FAKE_NUM_KINDS];		/**< every call, logged or not */
	uint32_t	reads[FAKE_NUM_READS];
	uint32_t	errors;				/**< callbacks with an argument out of range */
	char		lastError[160];
	fake_rec_t	log[FAKE_LOG_SIZE];
} fake_host_t;

//------------------------------------------------------------------------------
// Setup
//------------------------------------------------------------------------------
/** reset the fake host to its defaults and call seqc_init() with it at
 * SEQC_CPU_HZ; call this at the start of every test */
fake_host_t *fake_init(void);
/** The defaults, as fw 1.7 seq_init() and seq_clearPattern():
 * - every step off, volume 127, prob 100, note 60, no p-locks, flam 0, shift 7;
 * - every track length 16, shuffle 0, no mutes, no roll;
 * - pattern 0 active and pending, pattern mode;
 * - song = { 0, SONG_END, ... } and song pointer FAKE_NO_SONG_POS;
 * - random seed 1, every kind logged, time 0.
 * seqc_init() is not called. */
void fake_reset(fake_host_t *h);
/** restart the random generator from seed; ends a script */
void fake_seed(fake_host_t *h, uint64_t seed);
/** random() returns these values first, then continues with the generator.
 * The array must stay valid while it plays. */
void fake_scriptRandom(fake_host_t *h, const uint32_t *values, size_t count);

//------------------------------------------------------------------------------
// Pattern data
//------------------------------------------------------------------------------
/** set or clear the main-step bit */
void fake_setStep(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t step, bool on);
/** volume (0..127; the active bit is kept), note and probability (0..100) */
void fake_setStepData(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t step,
		uint8_t vel, uint8_t note, uint8_t prob);
/** the two p-locks */
void fake_setPlocks(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t step,
		uint8_t p1Nr, uint8_t p1Val, uint8_t p2Nr, uint8_t p2Val);
/** flam (raw StepTiming.flam, 0..5 = 1, 2, 3, 4, 6, 8 hits) and shift in units,
 * -7..+7 (stored as shift + 7) */
void fake_setTiming(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t step,
		uint8_t flam, int8_t shift);
/** the raw track length, 0 means 16 */
void fake_setLength(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t rawLength);
/** the effective length of a raw length: 0 is 16, above 64 is 64 */
uint8_t fake_effectiveLength(uint8_t rawLength);
/** the master track of a pattern: the largest raw length, lowest track on a tie */
uint8_t fake_masterTrack(const fake_host_t *h, uint8_t pattern);

//------------------------------------------------------------------------------
// Pattern sequencing, as the public functions of sequencer.c
//------------------------------------------------------------------------------
/** seq_setNextPattern(): pending = pattern and the reload flag set. When the core
 * is stopped, the pattern switches at once and the core rewinds (seqc_rewind()) */
void fake_setNextPattern(fake_host_t *h, uint8_t pattern);
/** seq_setSongModeEnabled(): switching it on puts the song pointer before entry 0 */
void fake_setSongMode(fake_host_t *h, bool on);
/** the song list; the entries after count are FAKE_SONG_END */
void fake_setSong(fake_host_t *h, const uint8_t *entries, size_t count);

//------------------------------------------------------------------------------
// Recording, as the glue
//------------------------------------------------------------------------------
/** seq_addNote(): while running with recordActive, record the note at the
 * track's current step, or at the next one from phase 0.5 on. In the second
 * case it calls seqc_skipOnce() (D6). The step gets the note, volume vel | 0x80,
 * prob 100 and its main-step bit. Before the first step nothing is recorded. */
void fake_addNote(fake_host_t *h, uint8_t track, uint8_t vel, uint8_t note);
/** seq_recordNoteAtStep() (N11): write a recorded note into a given step */
void fake_recordNoteAtStep(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t step,
		uint8_t vel, uint8_t note);

//------------------------------------------------------------------------------
// Driving the core. Each sets h->now first, so the log has the right times.
//------------------------------------------------------------------------------
/** seqc_process(now) */
void fake_process(fake_host_t *h, seqc_time_t now);
/** seqc_syncEvent(ev, stamp) */
void fake_sync(fake_host_t *h, seqc_sync_ev_t ev, seqc_time_t stamp);
/** seqc_process() every interval cycles after h->now, and once more at until if
 * the last call fell short of it. interval must not be 0 */
void fake_runUntil(fake_host_t *h, seqc_time_t until, seqc_time_t interval);
/** count sync events ev, stamped first, first + period, ... (period in cycles,
 * rounded per event), delivered the way seq_tick() does it: every interval cycles
 * after h->now, the events stamped up to that time go to seqc_syncEvent() and
 * then seqc_process() runs at that time. It ends with the seqc_process() call
 * that follows the last event. */
void fake_runClocks(fake_host_t *h, seqc_sync_ev_t ev, seqc_time_t first, double period,
		uint32_t count, seqc_time_t interval);

//------------------------------------------------------------------------------
// Log
//------------------------------------------------------------------------------
/** forget the logged entries and logLost; calls, reads and errors keep counting */
void fake_clearLog(fake_host_t *h);
/** number of logged entries of kind */
size_t fake_count(const fake_host_t *h, fake_kind_t kind);
/** the next logged entry of kind at or after index *pos, or NULL. On a hit, *pos
 * is set past it, so a loop finds them all */
const fake_rec_t *fake_next(const fake_host_t *h, fake_kind_t kind, size_t *pos);
/** the logged triggers of track (all tracks if track < 0), in firing order. It
 * returns how many there are, and stores up to max pointers in out */
size_t fake_triggers(const fake_host_t *h, int track, const fake_rec_t **out, size_t max);
/** print log entries from .. to-1 (to is clamped to the log) */
void fake_printLog(const fake_host_t *h, size_t from, size_t to);

#endif /* FAKE_HOST_H_ */
