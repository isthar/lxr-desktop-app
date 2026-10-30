/*
 * fake_host.c
 *
 * Fake host for the seq_core host tests, see fake_host.h. The pattern sequencing
 * follows sequencer.c of fw 1.7: seq_determineNextPattern(),
 * seq_setPendingPatternActive(), seq_setNextPattern() and the pattern-end block
 * of seq_nextStep().
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "fake_host.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static fake_host_t fake_instance;

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------
__attribute__((format(printf, 2, 3)))
static void fake_error(fake_host_t *h, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(h->lastError, sizeof h->lastError, fmt, ap);
	va_end(ap);
	h->errors++;
}
//------------------------------------------------------------------------------
static bool fake_validStep(fake_host_t *h, const char *fn, uint8_t pattern, uint8_t track, uint8_t step)
{
	if (pattern < FAKE_NUM_PATTERNS && track < FAKE_NUM_TRACKS && step < FAKE_NUM_STEPS)
		return true;
	fake_error(h, "%s(pattern %u, track %u, step %u): out of range", fn, pattern, track, step);
	return false;
}
//------------------------------------------------------------------------------
static bool fake_validTrack(fake_host_t *h, const char *fn, uint8_t track)
{
	if (track < FAKE_NUM_TRACKS)
		return true;
	fake_error(h, "%s(track %u): out of range", fn, track);
	return false;
}
//------------------------------------------------------------------------------
/** a log entry for a callback of kind, or a scratch entry if it isn't stored */
static fake_rec_t *fake_log(fake_host_t *h, fake_kind_t kind)
{
	static fake_rec_t scratch;
	fake_rec_t *r = &scratch;

	h->calls[kind]++;
	if (h->logMask & FAKE_LOG_BIT(kind))
	{
		if (h->logCount < FAKE_LOG_SIZE)
			r = &h->log[h->logCount++];
		else
			h->logLost++;
	}
	memset(r, 0, sizeof *r);
	r->kind = kind;
	r->time = h->now;
	r->tick = seqc_position();
	r->absStep = seqc_absStep();
	return r;
}
//------------------------------------------------------------------------------
uint8_t fake_effectiveLength(uint8_t rawLength)
{
	if (rawLength == 0)
		return SEQC_DEFAULT_LENGTH;
	return rawLength > FAKE_NUM_STEPS ? FAKE_NUM_STEPS : rawLength;
}
//------------------------------------------------------------------------------
uint8_t fake_masterTrack(const fake_host_t *h, uint8_t pattern)
{
	uint8_t master = 0;
	for (uint8_t t = 1; t < FAKE_NUM_TRACKS; t++)
		if (h->length[pattern][t] > h->length[pattern][master])
			master = t;
	return master;
}
//------------------------------------------------------------------------------
static uint32_t fake_masterLength(const fake_host_t *h, uint8_t pattern)
{
	return fake_effectiveLength(h->length[pattern][fake_masterTrack(h, pattern)]);
}
//------------------------------------------------------------------------------
/** song[0] as a pattern; a song that starts with a marker plays pattern 0 */
static uint8_t fake_firstSongPattern(const fake_host_t *h)
{
	return h->song[0] < FAKE_NUM_PATTERNS ? h->song[0] : 0;
}

//------------------------------------------------------------------------------
// Pattern sequencing, as sequencer.c
//------------------------------------------------------------------------------
/** seq_determineNextPattern(): the raw song entry (a pattern or a marker) */
static uint8_t fake_determineNextPattern(fake_host_t *h, bool autoIncrement)
{
	if (!h->songMode)
		return h->activePattern;
	if (autoIncrement)
	{
		h->songPos++;		/* uint8_t: FAKE_NO_SONG_POS + 1 == 0 */
		if (h->songPos == FAKE_SONG_LENGTH)
			h->songPos = 0;
		return h->song[h->songPos];
	}
	uint8_t pos = (uint8_t)(h->songPos + 1);
	if (pos == FAKE_SONG_LENGTH)
		pos = 0;
	return h->song[pos];
}
//------------------------------------------------------------------------------
/** seq_setPendingPatternActive(): the switch with its side effects */
static seqc_pat_t fake_setPendingPatternActive(fake_host_t *h)
{
	if (h->activePattern != h->pendingPattern || h->reload)
	{
		h->reload = false;
		h->activePattern = h->pendingPattern;
		h->patternChanges++;
		return SEQC_PAT_CHANGED;
	}
	return SEQC_PAT_SAME;
}
//------------------------------------------------------------------------------
/** a marker is SONG_LOOP or anything else from SONG_END on; the rest is SONG_END */
static bool fake_isSongEnd(uint8_t entry)
{
	return entry >= FAKE_NUM_PATTERNS && entry != FAKE_SONG_LOOP;
}

//------------------------------------------------------------------------------
// Callbacks: pattern data
//------------------------------------------------------------------------------
static bool cb_stepOn(void *ctx, uint8_t pattern, uint8_t track, uint8_t step)
{
	fake_host_t *h = ctx;
	h->reads[FAKE_READ_STEP_ON]++;
	if (!fake_validStep(h, "stepOn", pattern, track, step))
		return false;
	return ((h->mainSteps[pattern][track] >> step) & 1u) != 0;
}
//------------------------------------------------------------------------------
static const Step *cb_step(void *ctx, uint8_t pattern, uint8_t track, uint8_t step)
{
	static const Step invalid = { 0, 0, FAKE_DEFAULT_NOTE, FAKE_NO_AUTOMATION, 0, FAKE_NO_AUTOMATION, 0 };
	fake_host_t *h = ctx;
	h->reads[FAKE_READ_STEP]++;
	if (!fake_validStep(h, "step", pattern, track, step))
		return &invalid;
	return &h->steps[pattern][track][step];
}
//------------------------------------------------------------------------------
static StepTiming cb_timing(void *ctx, uint8_t pattern, uint8_t track, uint8_t step)
{
	fake_host_t *h = ctx;
	h->reads[FAKE_READ_TIMING]++;
	if (!fake_validStep(h, "timing", pattern, track, step))
		return (StepTiming){ .flam = 0, .shift = SEQC_SHIFT_CENTER };
	return h->timing[pattern][track][step];
}
//------------------------------------------------------------------------------
static uint8_t cb_trackLength(void *ctx, uint8_t pattern, uint8_t track)
{
	fake_host_t *h = ctx;
	h->reads[FAKE_READ_LENGTH]++;
	if (!fake_validStep(h, "trackLength", pattern, track, 0))
		return 0;
	return h->length[pattern][track];
}
//------------------------------------------------------------------------------
static uint8_t cb_shuffle(void *ctx)
{
	fake_host_t *h = ctx;
	h->reads[FAKE_READ_SHUFFLE]++;
	return h->shuffle;
}
//------------------------------------------------------------------------------
static bool cb_muted(void *ctx, uint8_t track)
{
	fake_host_t *h = ctx;
	h->reads[FAKE_READ_MUTED]++;
	if (!fake_validTrack(h, "muted", track))
		return false;
	return h->muted[track];
}
//------------------------------------------------------------------------------
static uint8_t cb_rollRate(void *ctx, uint8_t track)
{
	fake_host_t *h = ctx;
	h->reads[FAKE_READ_ROLL_RATE]++;
	if (!fake_validTrack(h, "rollRate", track))
		return 0;
	return h->rollRate[track];
}

//------------------------------------------------------------------------------
// Callbacks: pattern sequencing
//------------------------------------------------------------------------------
static uint8_t cb_activePattern(void *ctx)
{
	fake_host_t *h = ctx;
	h->reads[FAKE_READ_ACTIVE_PATTERN]++;
	return h->activePattern;
}
//------------------------------------------------------------------------------
/** the pattern-end block of fw 1.7 seq_nextStep(), then seq_setPendingPatternActive() */
static seqc_pat_t cb_patternEnd(void *ctx)
{
	fake_host_t *h = ctx;
	seqc_pat_t result;

	/* the song advances only when no other pattern is pending */
	if (h->activePattern == h->pendingPattern)
	{
		uint8_t next = fake_determineNextPattern(h, true);
		if (fake_isSongEnd(next))
		{
			/* The transport stops. The next start plays the song from entry 0:
			 * the core rewinds (transport REWIND puts the song pointer before
			 * entry 0), and with pending == active the first pattern end advances
			 * it to entry 0. fw 1.7 set pending = song[0] and the pointer to 0
			 * here, which played entry 0 twice after the usual stop press. */
			h->pendingPattern = h->activePattern;
			result = SEQC_PAT_STOP;
			goto done;
		}
		if (next == FAKE_SONG_LOOP)
		{
			/* reset to the song start; fw 1.7 did not clamp a marker in song[0] */
			next = fake_firstSongPattern(h);
			h->songPos = 0;
		}
		h->pendingPattern = next;
	}
	result = fake_setPendingPatternActive(h);

done:
	{
		fake_rec_t *r = fake_log(h, FAKE_PATTERN_END);
		r->value = (uint8_t)result;
		r->pattern = h->activePattern;
	}
	return result;
}
//------------------------------------------------------------------------------
/** cb_patternEnd() without side effects */
static bool cb_peekNextPattern(void *ctx, uint8_t *pattern)
{
	fake_host_t *h = ctx;
	uint8_t next = h->pendingPattern;
	bool restart;

	if (h->activePattern == h->pendingPattern)
	{
		next = fake_determineNextPattern(h, false);
		if (next == FAKE_SONG_LOOP)
			next = fake_firstSongPattern(h);
	}
	if (fake_isSongEnd(next))
	{
		*pattern = SEQC_NO_PATTERN;
		restart = false;
	}
	else
	{
		*pattern = next;
		restart = next != h->activePattern || h->reload;
	}

	fake_rec_t *r = fake_log(h, FAKE_PEEK);
	r->pattern = *pattern;
	r->flag = restart;
	return restart;
}
//------------------------------------------------------------------------------
/** N6: in song mode, walk the song to absStep and activate that entry */
static uint32_t cb_seekPosition(void *ctx, uint32_t absStep)
{
	fake_host_t *h = ctx;
	uint32_t steps = absStep;

	h->seekLengths = 0;
	if (h->songMode && h->song[0] < FAKE_NUM_PATTERNS)
	{
		/* E4.1 F33: a looping song repeats every period, one pass through it. A
		 * position two periods or more into it is walked from one period before its
		 * own, so that a run that crosses the wrap still finds its start: at most
		 * about three passes through the song instead of up to 16384 entries. A loop
		 * of one pattern is a single run, so its steps are those since step 0. */
		uint32_t period = 0;		/* steps of one pass through a looping song, 0 if it ends */
		bool uniform = true;		/* every entry of the loop is the same pattern */
		for (uint8_t i = 0; i < FAKE_SONG_LENGTH; i++)
		{
			const uint8_t entry = h->song[i];
			if (entry == FAKE_SONG_LOOP)
				break;
			if (fake_isSongEnd(entry))
			{
				period = 0;
				break;
			}
			period += fake_masterLength(h, entry);
			h->seekLengths++;
			uniform = uniform && entry == h->song[0];
		}
		const uint32_t skipped = (period && absStep >= 2 * period) ? (absStep / period - 1) * period : 0;
		const uint32_t walkStep = absStep - skipped;

		uint32_t entryStart = 0;	/* absolute step where entry idx begins */
		uint32_t runStart = 0;		/* absolute step where the run of identical entries began */
		uint8_t idx = 0;
		uint8_t prev = FAKE_SONG_END;	/* pattern of the entry before idx */
		uint8_t lastIdx = 0;		/* the last entry walked through completely */
		uint32_t lastEnd = 0, lastRunStart = 0;
		bool found = false;

		/* every pass either adds an entry of at least one step or wraps at
		 * SONG_LOOP to entry 0, which is a pattern; so this ends */
		for (;;)
		{
			const uint8_t entry = h->song[idx];
			if (entry == FAKE_SONG_LOOP)
			{
				idx = 0;
				continue;
			}
			if (fake_isSongEnd(entry))
				break;
			if (entry != prev)
				runStart = entryStart;
			const uint32_t len = fake_masterLength(h, entry);
			h->seekLengths++;
			if (walkStep < entryStart + len)
			{
				steps = walkStep - runStart + (uniform ? skipped : 0);
				found = true;
				break;
			}
			prev = entry;
			lastIdx = idx;
			lastRunStart = runStart;
			entryStart += len;
			lastEnd = entryStart;
			idx = (uint8_t)((idx + 1) % FAKE_SONG_LENGTH);
		}
		if (!found)
		{
			/* past SONG_END: the last step of the last entry */
			idx = lastIdx;
			steps = lastEnd - 1 - lastRunStart;
		}

		/* activate the entry with the normal pattern-change side effects. Inside
		 * the run of the active pattern nothing changes and nothing reloads, as at
		 * a pattern end of the song walk: no unsaved edit is lost, no kit loads. */
		h->songPos = idx;
		h->pendingPattern = h->song[idx];
		fake_setPendingPatternActive(h);
	}

	fake_rec_t *r = fake_log(h, FAKE_SEEK);
	r->arg = absStep;
	r->result = steps;
	r->pattern = h->activePattern;
	return steps;
}

//------------------------------------------------------------------------------
// Callbacks: outputs
//------------------------------------------------------------------------------
static void cb_trigger(void *ctx, const seqc_event_t *ev)
{
	fake_host_t *h = ctx;

	if (!fake_validStep(h, "trigger", ev->pattern, ev->track, ev->step))
		return;
	if (ev->hit >= 8 || (ev->flags & ~(SEQC_EVF_ROLL | SEQC_EVF_SLIDE)) != 0 || ev->reserved != 0)
		fake_error(h, "trigger: hit %u, flags 0x%02x, reserved %u", ev->hit, ev->flags, ev->reserved);

	fake_rec_t *r = fake_log(h, FAKE_TRIGGER);
	r->track = ev->track;
	r->ev = *ev;

	/* the glue records a roll hit at its own step (N11) */
	if ((ev->flags & SEQC_EVF_ROLL) && h->recordActive && seqc_isRunning())
		fake_recordNoteAtStep(h, ev->pattern, ev->track, ev->step, ev->vel, ev->note);

	if (h->onTrigger)
		h->onTrigger(h, ev);
}
//------------------------------------------------------------------------------
static void cb_noteOff(void *ctx, uint8_t track)
{
	fake_host_t *h = ctx;
	if (!fake_validTrack(h, "noteOff", track))
		return;
	fake_log(h, FAKE_NOTE_OFF)->track = track;
}
//------------------------------------------------------------------------------
static void cb_stepBoundary(void *ctx, uint32_t absStep)
{
	fake_host_t *h = ctx;

	if ((int32_t)absStep != seqc_absStep())
		fake_error(h, "stepBoundary(%u) but seqc_absStep() is %d", (unsigned)absStep, (int)seqc_absStep());
	fake_log(h, FAKE_STEP_BOUNDARY)->arg = absStep;

	/* the glue's erase (fw 1.7 gating): active track, not muted, main step on */
	if (h->eraseActive && h->activeTrack < FAKE_NUM_TRACKS && !h->muted[h->activeTrack])
	{
		const uint8_t s = seqc_trackStep(h->activeTrack);
		if (s < FAKE_NUM_STEPS)
			h->mainSteps[h->activePattern][h->activeTrack] &= ~(1ull << s);
	}

	if (h->onStepBoundary)
		h->onStepBoundary(h, absStep);
}
//------------------------------------------------------------------------------
static void cb_tempoStep(void *ctx)
{
	fake_log(ctx, FAKE_TEMPO_STEP);
}
//------------------------------------------------------------------------------
static void cb_midiClockOut(void *ctx)
{
	fake_log(ctx, FAKE_MIDI_CLOCK);
}
//------------------------------------------------------------------------------
static void cb_clockOutPulse(void *ctx, uint8_t pulseInStep)
{
	fake_host_t *h = ctx;
	if (pulseInStep >= SEQC_PULSES_PER_STEP)
		fake_error(h, "clockOutPulse(%u): out of range", pulseInStep);
	fake_log(h, FAKE_CLOCK_PULSE)->value = pulseInStep;
}
//------------------------------------------------------------------------------
static void cb_transport(void *ctx, seqc_transport_t change)
{
	fake_host_t *h = ctx;
	if ((unsigned)change > SEQC_TRANSPORT_REWIND)
		fake_error(h, "transport(%u): out of range", (unsigned)change);
	fake_log(h, FAKE_TRANSPORT)->value = (uint8_t)change;

	/* the glue's seq_resetPosition(): the song restarts before entry 0 */
	if (change == SEQC_TRANSPORT_REWIND)
		h->songPos = FAKE_NO_SONG_POS;
}
//------------------------------------------------------------------------------
static uint32_t cb_random(void *ctx)
{
	fake_host_t *h = ctx;
	h->reads[FAKE_READ_RANDOM]++;
	if (h->rngScript && h->rngScriptPos < h->rngScriptLen)
		return h->rngScript[h->rngScriptPos++];
	/* Knuth's MMIX LCG; the high half has the good bits */
	h->rngState = h->rngState * 6364136223846793005ull + 1442695040888963407ull;
	return (uint32_t)(h->rngState >> 32);
}
//------------------------------------------------------------------------------
static TrackGroove cb_groove(void *ctx, uint8_t pattern, uint8_t track)
{
	fake_host_t *h = ctx;
	h->reads[FAKE_READ_GROOVE]++;
	if (!fake_validStep(h, "groove", pattern, track, 0))
		return (TrackGroove){ 0, 0, 0, 0 };
	return h->groove[pattern][track];
}
//------------------------------------------------------------------------------
static void cb_trackStep(void *ctx, uint8_t track)
{
	fake_host_t *h = ctx;
	if (!fake_validTrack(h, "trackStep", track))
		return;
	if (seqc_trackOnMasterGrid(track))
		fake_error(h, "trackStep(%u) for a track on the master grid", track);
	fake_log(h, FAKE_TRACK_STEP)->track = track;
	if (h->onTrackStep)
		h->onTrackStep(h, track);
}

//------------------------------------------------------------------------------
// Setup
//------------------------------------------------------------------------------
void fake_reset(fake_host_t *h)
{
	memset(h, 0, sizeof *h);

	for (int p = 0; p < FAKE_NUM_PATTERNS; p++)
		for (int t = 0; t < FAKE_NUM_TRACKS; t++)
		{
			h->length[p][t] = SEQC_DEFAULT_LENGTH;
			for (int s = 0; s < FAKE_NUM_STEPS; s++)
			{
				/* seq_resetNote() and seq_clearTrack() */
				h->steps[p][t][s] = (Step){ .volume = 127, .prob = 100, .note = FAKE_DEFAULT_NOTE,
						.param1Nr = FAKE_NO_AUTOMATION, .param1Val = 0,
						.param2Nr = FAKE_NO_AUTOMATION, .param2Val = 0 };
				h->timing[p][t][s] = (StepTiming){ .flam = 0, .shift = SEQC_SHIFT_CENTER };
			}
		}

	/* seq_init(): every song entry SONG_END but the first */
	memset(h->song, FAKE_SONG_END, sizeof h->song);
	h->song[0] = 0;
	h->songPos = FAKE_NO_SONG_POS;

	h->logMask = FAKE_LOG_ALL;
	fake_seed(h, 1);

	h->host = (seqc_host_t){
		.ctx = h,
		.stepOn = cb_stepOn,
		.step = cb_step,
		.timing = cb_timing,
		.trackLength = cb_trackLength,
		.shuffle = cb_shuffle,
		.muted = cb_muted,
		.rollRate = cb_rollRate,
		.activePattern = cb_activePattern,
		.patternEnd = cb_patternEnd,
		.peekNextPattern = cb_peekNextPattern,
		.seekPosition = cb_seekPosition,
		.trigger = cb_trigger,
		.noteOff = cb_noteOff,
		.stepBoundary = cb_stepBoundary,
		.tempoStep = cb_tempoStep,
		.midiClockOut = cb_midiClockOut,
		.clockOutPulse = cb_clockOutPulse,
		.transport = cb_transport,
		.random = cb_random,
		.groove = cb_groove,
		.trackStep = cb_trackStep,
	};
}
//------------------------------------------------------------------------------
fake_host_t *fake_init(void)
{
	fake_host_t *h = &fake_instance;
	fake_reset(h);
	seqc_init(&h->host, SEQC_CPU_HZ);
	return h;
}
//------------------------------------------------------------------------------
void fake_seed(fake_host_t *h, uint64_t seed)
{
	h->rngState = seed;
	h->rngScript = NULL;
	h->rngScriptLen = h->rngScriptPos = 0;
}
//------------------------------------------------------------------------------
void fake_scriptRandom(fake_host_t *h, const uint32_t *values, size_t count)
{
	h->rngScript = values;
	h->rngScriptLen = count;
	h->rngScriptPos = 0;
}

//------------------------------------------------------------------------------
// Pattern data
//------------------------------------------------------------------------------
void fake_setStep(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t step, bool on)
{
	if (!fake_validStep(h, "fake_setStep", pattern, track, step))
		return;
	if (on)
		h->mainSteps[pattern][track] |= 1ull << step;
	else
		h->mainSteps[pattern][track] &= ~(1ull << step);
}
//------------------------------------------------------------------------------
void fake_setStepData(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t step,
		uint8_t vel, uint8_t note, uint8_t prob)
{
	if (!fake_validStep(h, "fake_setStepData", pattern, track, step))
		return;
	Step *s = &h->steps[pattern][track][step];
	s->volume = (uint8_t)((s->volume & FAKE_STEP_ACTIVE_MASK) | (vel & 0x7F));
	s->note = note;
	s->prob = prob;
}
//------------------------------------------------------------------------------
void fake_setPlocks(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t step,
		uint8_t p1Nr, uint8_t p1Val, uint8_t p2Nr, uint8_t p2Val)
{
	if (!fake_validStep(h, "fake_setPlocks", pattern, track, step))
		return;
	Step *s = &h->steps[pattern][track][step];
	s->param1Nr = p1Nr;
	s->param1Val = p1Val;
	s->param2Nr = p2Nr;
	s->param2Val = p2Val;
}
//------------------------------------------------------------------------------
void fake_setTiming(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t step,
		uint8_t flam, int8_t shift)
{
	if (!fake_validStep(h, "fake_setTiming", pattern, track, step))
		return;
	if (flam > 15 || shift < -7 || shift > 7)
	{
		fake_error(h, "fake_setTiming(flam %u, shift %d): out of range", flam, shift);
		return;
	}
	h->timing[pattern][track][step] = (StepTiming){ .flam = flam & 0x0F,
			.shift = (uint8_t)(shift + (int8_t)SEQC_SHIFT_CENTER) & 0x0F };
}
//------------------------------------------------------------------------------
void fake_setLength(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t rawLength)
{
	if (!fake_validStep(h, "fake_setLength", pattern, track, 0))
		return;
	h->length[pattern][track] = rawLength;
}
//------------------------------------------------------------------------------
void fake_setTempo(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t index)
{
	if (fake_validStep(h, "fake_setTempo", pattern, track, 0))
		h->groove[pattern][track].tempo = (int8_t)(index - GROOVE_TEMPO_X1);
}
//------------------------------------------------------------------------------
void fake_setGroove(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t humTime, uint8_t humVol, int8_t delay)
{
	if (!fake_validStep(h, "fake_setGroove", pattern, track, 0))
		return;
	h->groove[pattern][track].humTime = humTime;
	h->groove[pattern][track].humVol = humVol;
	h->groove[pattern][track].delay = delay;
}

//------------------------------------------------------------------------------
// Pattern sequencing
//------------------------------------------------------------------------------
void fake_setNextPattern(fake_host_t *h, uint8_t pattern)
{
	if (pattern >= FAKE_NUM_PATTERNS)
		return;
	h->pendingPattern = pattern;
	h->reload = true;
	if (!seqc_isRunning())
	{
		/* immediately change pattern, and restart it (seq_setStepIndexToStart()) */
		fake_setPendingPatternActive(h);
		seqc_rewind();
	}
}
//------------------------------------------------------------------------------
void fake_setSongMode(fake_host_t *h, bool on)
{
	h->songMode = on;
	if (on)
		h->songPos = FAKE_NO_SONG_POS;	/* because the first pattern end increments it */
}
//------------------------------------------------------------------------------
void fake_setSong(fake_host_t *h, const uint8_t *entries, size_t count)
{
	memset(h->song, FAKE_SONG_END, sizeof h->song);
	if (count > FAKE_SONG_LENGTH)
		count = FAKE_SONG_LENGTH;
	if (count > 0)
		memcpy(h->song, entries, count);
}

//------------------------------------------------------------------------------
// Recording
//------------------------------------------------------------------------------
void fake_recordNoteAtStep(fake_host_t *h, uint8_t pattern, uint8_t track, uint8_t step,
		uint8_t vel, uint8_t note)
{
	if (!fake_validStep(h, "fake_recordNoteAtStep", pattern, track, step))
		return;
	Step *s = &h->steps[pattern][track][step];
	s->note = note;
	s->volume = (uint8_t)(vel | FAKE_STEP_ACTIVE_MASK);
	s->prob = 100;
	h->mainSteps[pattern][track] |= 1ull << step;
}
//------------------------------------------------------------------------------
void fake_addNote(fake_host_t *h, uint8_t track, uint8_t vel, uint8_t note)
{
	if (track >= FAKE_NUM_TRACKS || !h->recordActive || !seqc_isRunning())
		return;
	const uint8_t step = seqc_trackStep(track);
	if (step == SEQC_NO_STEP)
		return;
	const uint8_t pattern = h->activePattern;
	/* fw 1.7 took the raw length here, a modulo by zero for a raw 0 */
	const uint8_t len = fake_effectiveLength(h->length[pattern][track]);
	/* from half the step on, the note belongs to the next step, which on a track
	 * of length 1 is the same step again */
	const bool late = seqc_trackPhase(track) >= 0.5f;
	const uint8_t target = late ? (uint8_t)((step + 1) % len) : step;
	if (late)
		seqc_skipOnce(track, target);	/* D6: the sequencer must not play it again */
	fake_recordNoteAtStep(h, pattern, track, target, vel, note);
}

//------------------------------------------------------------------------------
// Driving the core
//------------------------------------------------------------------------------
void fake_process(fake_host_t *h, seqc_time_t now)
{
	h->now = now;
	seqc_process(now);
}
//------------------------------------------------------------------------------
void fake_sync(fake_host_t *h, seqc_sync_ev_t ev, seqc_time_t stamp)
{
	h->now = stamp;
	seqc_syncEvent(ev, stamp);
}
//------------------------------------------------------------------------------
void fake_runUntil(fake_host_t *h, seqc_time_t until, seqc_time_t interval)
{
	if (interval == 0)
	{
		fake_error(h, "fake_runUntil: interval 0");
		return;
	}
	for (seqc_time_t t = h->now + interval; t <= until; t += interval)
		fake_process(h, t);
	if (h->now < until)
		fake_process(h, until);
}
//------------------------------------------------------------------------------
void fake_runClocks(fake_host_t *h, seqc_sync_ev_t ev, seqc_time_t first, double period,
		uint32_t count, seqc_time_t interval)
{
	if (interval == 0)
	{
		fake_error(h, "fake_runClocks: interval 0");
		return;
	}
	seqc_time_t t = h->now;
	uint32_t i = 0;
	while (i < count)
	{
		t += interval;
		/* like seq_tick(): the events stamped up to now first, then process */
		for (; i < count; i++)
		{
			const seqc_time_t stamp = first + (seqc_time_t)((double)i * period + 0.5);
			if (stamp > t)
				break;
			fake_sync(h, ev, stamp);
		}
		fake_process(h, t);
	}
}

//------------------------------------------------------------------------------
// Log
//------------------------------------------------------------------------------
void fake_clearLog(fake_host_t *h)
{
	h->logCount = 0;
	h->logLost = 0;
}
//------------------------------------------------------------------------------
size_t fake_count(const fake_host_t *h, fake_kind_t kind)
{
	size_t n = 0;
	for (size_t i = 0; i < h->logCount; i++)
		if (h->log[i].kind == kind)
			n++;
	return n;
}
//------------------------------------------------------------------------------
const fake_rec_t *fake_next(const fake_host_t *h, fake_kind_t kind, size_t *pos)
{
	for (size_t i = *pos; i < h->logCount; i++)
		if (h->log[i].kind == kind)
		{
			*pos = i + 1;
			return &h->log[i];
		}
	*pos = h->logCount;
	return NULL;
}
//------------------------------------------------------------------------------
size_t fake_triggers(const fake_host_t *h, int track, const fake_rec_t **out, size_t max)
{
	size_t n = 0;
	for (size_t i = 0; i < h->logCount; i++)
	{
		const fake_rec_t *r = &h->log[i];
		if (r->kind != FAKE_TRIGGER || (track >= 0 && r->ev.track != track))
			continue;
		if (n < max)
			out[n] = r;
		n++;
	}
	return n;
}
//------------------------------------------------------------------------------
void fake_printLog(const fake_host_t *h, size_t from, size_t to)
{
	static const char *const names[FAKE_NUM_KINDS] = {
		"trigger", "noteOff", "boundary", "tempoStep", "midiClock", "pulse",
		"transport", "patternEnd", "peek", "seek", "trackStep" };
	if (to > h->logCount)
		to = h->logCount;
	for (size_t i = from; i < to; i++)
	{
		const fake_rec_t *r = &h->log[i];
		printf("%6zu %10.4f ms  tick %10.3f  step %4d  %-10s", i, fake_cyclesToMs(r->time), r->tick,
				(int)r->absStep, names[r->kind]);
		switch (r->kind)
		{
		case FAKE_TRIGGER:
			printf(" track %u pattern %u step %u hit %u tick %lld abs %u vel %u note %u%s", r->ev.track,
					r->ev.pattern, r->ev.step, r->ev.hit, (long long)r->ev.tick, (unsigned)r->ev.absStep,
					r->ev.vel, r->ev.note, (r->ev.flags & SEQC_EVF_ROLL) ? " roll" : "");
			break;
		case FAKE_NOTE_OFF:
			printf(" track %u", r->track);
			break;
		case FAKE_TRACK_STEP:
			printf(" track %u", r->track);
			break;
		case FAKE_STEP_BOUNDARY:
			printf(" %u", (unsigned)r->arg);
			break;
		case FAKE_CLOCK_PULSE:
		case FAKE_TRANSPORT:
			printf(" %u", r->value);
			break;
		case FAKE_PATTERN_END:
			printf(" result %u pattern %u", r->value, r->pattern);
			break;
		case FAKE_PEEK:
			printf(" pattern %u restart %d", r->pattern, (int)r->flag);
			break;
		case FAKE_SEEK:
			printf(" absStep %u steps %u pattern %u", (unsigned)r->arg, (unsigned)r->result, r->pattern);
			break;
		default:
			break;
		}
		printf("\n");
	}
}
