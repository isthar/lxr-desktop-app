/*
 * test_mixsplit.c
 *
 * LICHOMAT v0.5, review of plan v0.5 Task 7: the firmware's mixer, mixer_calcNextSampleBlock(),
 * with records posted at offset k (spec 4.2, S12-S15). test_voicesplit and test_vsched model
 * the mixer's calls; this runs them. Drum 1 on out 1 (DAC1 stereo) with an instant attack,
 * and LFO 1, retriggered by Drum 1, on Drum 1's pitch EG amount (PAR_MODAMNT1), which its
 * control step reads; the other voices are silent. The test makes the block windows itself
 * (vsched_blockDone() one block apart) and posts the records between them, as seq_tick()
 * posts them before the render. Each run is a child forked from one boot, so that all start
 * from the same state; it sends its output through a pipe. For k = 0, 2, ..., 62: hit 1 (a
 * pan p-lock 0) at offset k of block 4, hit 2 (pan 127) at offset k of block 7, and a run
 * with hit 1 alone.
 * 1. Silence before hit 1; from k, hit 1's 128 - k frames equal hit 1 at offset 0, shifted by
 *    k, bit for bit: its first block is v0.4's, with the LFO it retriggers dispatched again
 *    before its control step (spec 4.5, S15), and its second step ends at the block grid (S13).
 * 2. Block 7 before k equals the run with hit 1 alone: hit 1's tail with its pan latch (S14).
 * 3. From k, hit 2's pan: out 1's left channel is 0.
 * Each of the review's mutations fails it: the pan latch read after vsched_applySplit() (2),
 * the control step before the LFO re-dispatch (1), the control path's phase guards dropped,
 * or the one of *_calcAsync() (1). The filter's guard alone changes no sample: every write of
 * a filter's frequency sets its coefficient too, which SVF_recalcFreq() computes again.
 * The persistent note phase (the amendment of S13 after Task 11's kick measurement), from the
 * same boot: a longer note (the amp EG about 190 ms) over 170 blocks.
 * 4. Hit 1 alone at k equals hit 1 at offset 0 shifted by k, bit for bit, for the whole note,
 *    which ends within the run and sounds for more than 150 ms: every control step of the note
 *    comes at k, and the LFO it retriggers is read after as many dispatches as at offset 0.
 * 5. Its phase is k from its block for at least 150 ms, and 0 at the end: the mixer drops it
 *    once the voice is silent.
 * 6. A retrigger at k2 in block 9 over hit 1 at k1 (k2 below, equal to and above k1, and 0):
 *    up to k2 the output is hit 1's alone (its step at k1 comes first when k1 < k2); from k2 it
 *    is hit 1 at offset 0 shifted to k2, for the whole note; the phase is k2 after block 9.
 * From the final review of v0.5, from the same boot:
 * 7. Check 5 for each of the six voices (tracks 0-5), alone, a hit at k = 20 with the amp EG at
 *    about 190 ms: its phase is 20 while its amp EG runs, and 0 from 2 blocks after the EG has
 *    ended (stopped, or in its decay at 0, and its output, the gain of the voice's last control
 *    step, 0) to the end of the run. Measured: the drums 2 blocks (their gain ramps from the
 *    step before), the others 1; from the EG's state alone, which ends a step before its
 *    output, 3 and 2.
 * 8. A trigger at once, as trg blk's glue makes it in seq_tick() before the render (the
 *    p-locks, the slide mode, voiceControl_noteOn()), in block 9 over hit 1 at k1 = 20, as after
 *    a switch from smp to blk while the note sounds: the whole run equals the one with a record
 *    at offset 0 in block 9 (check 6's k2 = 0) bit for bit, and the phase is 0 from block 9.
 *    voiceControl_noteOn() calls vsched_triggerNow(); without it the voice keeps the phase 20,
 *    renders [0, 20) of the new note with the old step's gain, and starts its EG 20 frames late.
 *
 * The test links a copy of vsched.c whose seq_isInTick() is the test's emu_testInTick()
 * (emu/CMakeLists.txt): vsched_post() drops a record posted outside seq_tick() (P9), and the
 * firmware has no hook for it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "stm32f7xx_hal.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/SlopeEg2.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/vsched.h"
#include "emu_test.h"

_Static_assert(PAN1 == PAR_PAN1 + 1, "a p-lock names the CC, the parameter + 1");

#define MS_FRAMES 64
#define MS_BLOCKS 10
#define MS_HIT1 4u	/* the windows from block 2 on are whole: 0 re-seeds, 1 is the first period */
#define MS_HIT2 7u
#define MS_KS 32u	/* k = 0, 2, ..., 62 */

/* the copy of vsched.c calls this for seq_isInTick() */
static uint8_t msInTick;
uint8_t emu_testInTick(void);
uint8_t emu_testInTick(void)
{
	return msInTick;
}

typedef struct
{
	int16_t out[MS_BLOCKS][2][MS_FRAMES * 2];	/* [block][out 1, out 2][frame L, R] */
	unsigned applied;
	uint8_t k[2], late[2];
} msRun_t;

static msRun_t msRuns[2u * MS_KS];	/* [2 * (k / 2) + alone], from the children */
static msRun_t *msCur;

static void msTrace(const vsched_rec_t *r, uint32_t serial, uint8_t k, uint8_t late)
{
	(void)r;
	(void)serial;
	if (msCur->applied < 2u)
	{
		msCur->k[msCur->applied] = k;
		msCur->late[msCur->applied] = late;
	}
	msCur->applied++;
}

/* Drum 1 alone on out 1; LFO 1 mid-cycle at every retrigger's block, so that its value before a
 * retrigger is not the one after */
static void msSetup(void *arg)
{
	(void)arg;
	mixer_audioRouting[0] = MIXER_ROUTING_DAC1_STEREO;
	voiceArray[0].oscVolEg.attack = 1.0;	/* instant: the first step does not ramp from the old tail */
	voiceArray[0].oscVolEg.decay = 0.02;
	Lfo *l = &voiceArray[0].lfo;
	l->waveform = LFO_SAW_UP;
	l->retrigger = 1;			/* Drum 1 */
	l->phaseOffset = 0;
	lfo_setFreq(l, 0.5f);			/* 25.7 Hz, 0.037 of a cycle per block */
	l->modTarget.amount = 1.f;
	activeParameters[PAR_MODAMNT1] = 64;
	modNode_setDestination(&l->modTarget, PAR_MODAMNT1);
}

static void msPost(uint32_t t, uint8_t pan)
{
	const vsched_rec_t r = { .time = t, .track = 0, .note = SEQ_DEFAULT_NOTE, .vel = 127,
		.flags = VS_TIMED | VS_PLOCKS | (DRUM_SLIDE_OFF << VS_SLIDE_SHIFT),
		.p1Nr = PAN1, .p1Val = pan, .p2Nr = NO_AUTOMATION, .p2Val = 0 };
	msInTick = 1;
	vsched_post(&r);
	msInTick = 0;
}

typedef struct
{
	uint8_t k, both;
} msArg_t;

static void msScenario(void *arg)
{
	const msArg_t *a = arg;
	vsched_setTrace(msTrace);
	voiceArray[0].lfo.phase = 0x80000000u;
	const uint32_t t0 = 0x40000000u, d = (uint32_t)a->k * VSCHED_BLOCK_CYCLES / 64u;
	for (uint32_t b = 0; b < MS_BLOCKS; b++)
	{
		vsched_blockDone(t0 + b * VSCHED_BLOCK_CYCLES);	/* the window [t0 + (b - 1) L, t0 + b L) */
		const uint32_t w0 = t0 + (b - 1u) * VSCHED_BLOCK_CYCLES;
		if (b == MS_HIT1)
			msPost(w0 + d, 0);
		if (b == MS_HIT2 && a->both)
			msPost(w0 + d, 127);
		/* output: out 2 (dma_buffer), output2: out 1 (dma_buffer2) */
		mixer_calcNextSampleBlock(msCur->out[b][1], msCur->out[b][0]);
	}
}

/* frame f of a run, out 0 (out 1) or 1 (out 2), channel 0 (L) or 1 (R) */
static int16_t msAt(const msRun_t *r, unsigned f, unsigned out, unsigned ch)
{
	return r->out[f / MS_FRAMES][out][2u * (f % MS_FRAMES) + ch];
}

/* frames [f, f + n) of a equal frames [g, g + n) of b, both outs, both channels */
static int msEqual(const msRun_t *a, unsigned f, const msRun_t *b, unsigned g, unsigned n)
{
	for (unsigned i = 0; i < n; i++)
		for (unsigned o = 0; o < 2u; o++)
			for (unsigned c = 0; c < 2u; c++)
				if (msAt(a, f + i, o, c) != msAt(b, g + i, o, c))
					return 0;
	return 1;
}

static void mnNote(void);
static void mzSilence(void);
static void mnNow(void);

TEST(M25_mixer_split)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	emu_testBoot(&cfg);
	emu_testRunMs(100);
	emu_directCall(msSetup, NULL);
	emu_testRunMs(300);			/* the smoothers settle, the voices are silent */
	for (unsigned i = 0; i < 2u * MS_KS; i++)
	{
		int fd[2];
		ASSERT_EQ(pipe(fd), 0);
		const pid_t pid = fork();
		ASSERT_TRUE(pid >= 0);
		if (pid == 0)
		{
			close(fd[0]);
			msCur = &msRuns[i];
			msArg_t a = { (uint8_t)(2u * (i / 2u)), (uint8_t)!(i & 1u) };
			emu_directCall(msScenario, &a);
			_exit(write(fd[1], msCur, sizeof *msCur) == (ssize_t)sizeof *msCur ? 0 : 1);
		}
		close(fd[1]);
		size_t got = 0;
		for (ssize_t n; got < sizeof msRuns[i] && (n = read(fd[0], (char *)&msRuns[i] + got, sizeof msRuns[i] - got)) > 0;)
			got += (size_t)n;
		close(fd[0]);
		int status = 0;
		ASSERT_EQ(waitpid(pid, &status, 0), pid);
		ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		ASSERT_EQ(got, sizeof msRuns[i]);
	}

	/* the reference, hit 1 alone at offset 0: loud, on the left of out 1 only */
	const msRun_t *ref = &msRuns[1];
	int loud = 0, right = 0;
	for (unsigned f = MS_HIT1 * MS_FRAMES; f < (MS_HIT1 + 1u) * MS_FRAMES; f++)
	{
		loud |= msAt(ref, f, 0, 0) > 1000 || msAt(ref, f, 0, 0) < -1000;
		right |= msAt(ref, f, 0, 1) != 0;
	}
	ASSERT_TRUE(loud);
	ASSERT_FALSE(right);

	unsigned planned = 0, silent = 0, shifted = 0, tail = 0, tailHeard = 0, newPan = 0;
	for (unsigned i = 0; i < MS_KS; i++)
	{
		const unsigned k = 2u * i;
		const msRun_t *a = &msRuns[2u * i], *alone = &msRuns[2u * i + 1u];
		planned += a->applied == 2u && a->k[0] == k && a->k[1] == k && !a->late[0] && !a->late[1]
			&& alone->applied == 1u && alone->k[0] == k && !alone->late[0];
		int quiet = 1;
		for (unsigned f = 0; f < MS_HIT1 * MS_FRAMES + k; f++)
			for (unsigned o = 0; o < 2u; o++)
				for (unsigned c = 0; c < 2u; c++)
					quiet &= msAt(a, f, o, c) == 0;
		silent += quiet;
		shifted += msEqual(a, MS_HIT1 * MS_FRAMES + k, ref, MS_HIT1 * MS_FRAMES, 2u * MS_FRAMES - k);
		tail += msEqual(a, MS_HIT2 * MS_FRAMES, alone, MS_HIT2 * MS_FRAMES, k);
		int heard = 0, left = 0;
		for (unsigned j = 0; j < k; j++)
			heard |= msAt(alone, MS_HIT2 * MS_FRAMES + j, 0, 0) != 0;
		tailHeard += k == 0 || heard;
		for (unsigned j = k; j < MS_FRAMES; j++)
			left |= msAt(a, MS_HIT2 * MS_FRAMES + j, 0, 0) != 0;
		newPan += !left;
	}
	printf("M25: of %u offsets: planned at k %u, silent before hit 1 %u, hit 1 shifted by k %u, "
			"block 7 before k hit 1's tail %u (heard there %u), hit 2's pan from k %u\n",
			MS_KS, planned, silent, shifted, tail, tailHeard, newPan);
	ASSERT_EQ(planned, MS_KS);
	ASSERT_EQ(silent, MS_KS);
	ASSERT_EQ(tailHeard, MS_KS);
	ASSERT_EQ(shifted, MS_KS);
	ASSERT_EQ(tail, MS_KS);
	ASSERT_EQ(newPan, MS_KS);
	mnNote();
	mzSilence();
	mnNow();
}

/* the persistent note phase: checks 4-6 */
#define MN_BLOCKS 170u		/* 247 ms */
#define MN_DECAY 0.0075		/* the amp EG falls 1/133 per control step: about 193 ms */
#define MN_HIT2 9u
#define MN_NONE 0xFFu
#define MN_150MS 6621u		/* frames */

typedef struct
{
	int16_t out[MN_BLOCKS][2][MS_FRAMES * 2];	/* [block][out 1, out 2][frame L, R] */
	uint8_t phase[MN_BLOCKS];			/* vsched_phase(0) after each block */
	unsigned applied;
	uint8_t k[2], late[2];
} mnRun_t;

typedef struct
{
	uint8_t k1, k2;		/* hit 1's offset in block 4; hit 2's in block 9, or MN_NONE */
} mnArg_t;

static uint8_t mnTriggerNow;	/* 1: in block 9 a trigger at once (check 8) */

static const mnArg_t mnArgs[] = {
	{ 0, MN_NONE }, { 2, MN_NONE }, { 4, MN_NONE }, { 6, MN_NONE }, { 8, MN_NONE }, { 10, MN_NONE },
	{ 12, MN_NONE }, { 14, MN_NONE }, { 16, MN_NONE }, { 18, MN_NONE }, { 20, MN_NONE }, { 22, MN_NONE },
	{ 24, MN_NONE }, { 26, MN_NONE }, { 28, MN_NONE }, { 30, MN_NONE }, { 32, MN_NONE }, { 34, MN_NONE },
	{ 36, MN_NONE }, { 38, MN_NONE }, { 40, MN_NONE }, { 42, MN_NONE }, { 44, MN_NONE }, { 46, MN_NONE },
	{ 48, MN_NONE }, { 50, MN_NONE }, { 52, MN_NONE }, { 54, MN_NONE }, { 56, MN_NONE }, { 58, MN_NONE },
	{ 60, MN_NONE }, { 62, MN_NONE },
	{ 20, 0 }, { 20, 10 }, { 20, 20 }, { 20, 40 }, { 62, 2 }, { 2, 62 }, { 0, 30 },
};
#define MN_RUNS (sizeof mnArgs / sizeof mnArgs[0])

static mnRun_t mnRuns[MN_RUNS];
static mnRun_t *mnCur;

static void mnTrace(const vsched_rec_t *r, uint32_t serial, uint8_t k, uint8_t late)
{
	(void)r;
	(void)serial;
	if (mnCur->applied < 2u)
	{
		mnCur->k[mnCur->applied] = k;
		mnCur->late[mnCur->applied] = late;
	}
	mnCur->applied++;
}

static void mnScenario(void *arg)
{
	const mnArg_t *a = arg;
	vsched_setTrace(mnTrace);
	voiceArray[0].lfo.phase = 0x80000000u;
	voiceArray[0].oscVolEg.decay = MN_DECAY;
	const uint32_t t0 = 0x40000000u;
	for (uint32_t b = 0; b < MN_BLOCKS; b++)
	{
		vsched_blockDone(t0 + b * VSCHED_BLOCK_CYCLES);
		const uint32_t w0 = t0 + (b - 1u) * VSCHED_BLOCK_CYCLES;
		if (b == MS_HIT1)
			msPost(w0 + (uint32_t)a->k1 * VSCHED_BLOCK_CYCLES / 64u, 0);
		if (b == MN_HIT2 && a->k2 != MN_NONE)
			msPost(w0 + (uint32_t)a->k2 * VSCHED_BLOCK_CYCLES / 64u, 0);
		if (b == MN_HIT2 && mnTriggerNow)
		{
			/* trg blk's seq_voiceSide() with msPost()'s p-lock and slide mode */
			msInTick = 1;
			seq_applyPlocks(0, PAN1, 0, NO_AUTOMATION, 0);
			Drum_setSlide(0, DRUM_SLIDE_OFF);
			voiceControl_noteOn(0, SEQ_DEFAULT_NOTE, 127);
			msInTick = 0;
		}
		mixer_calcNextSampleBlock(mnCur->out[b][1], mnCur->out[b][0]);
		mnCur->phase[b] = vsched_phase(0);
	}
}

static int16_t mnAt(const mnRun_t *r, unsigned f, unsigned out, unsigned ch)
{
	return r->out[f / MS_FRAMES][out][2u * (f % MS_FRAMES) + ch];
}

/* frames [f, f + n) of a equal frames [g, g + n) of b, both outs, both channels */
static int mnEqual(const mnRun_t *a, unsigned f, const mnRun_t *b, unsigned g, unsigned n)
{
	for (unsigned i = 0; i < n; i++)
		for (unsigned o = 0; o < 2u; o++)
			for (unsigned c = 0; c < 2u; c++)
				if (mnAt(a, f + i, o, c) != mnAt(b, g + i, o, c))
					return 0;
	return 1;
}

/* frames [0, n) of a are 0, both outs, both channels */
static int mnQuiet(const mnRun_t *a, unsigned n)
{
	for (unsigned f = 0; f < n; f++)
		for (unsigned o = 0; o < 2u; o++)
			for (unsigned c = 0; c < 2u; c++)
				if (mnAt(a, f, o, c) != 0)
					return 0;
	return 1;
}

/* the first frame from f on where a and b (shifted by g - f) differ, or 0 */
static unsigned mnFirstDiff(const mnRun_t *a, unsigned f, const mnRun_t *b, unsigned g, unsigned n)
{
	for (unsigned i = 0; i < n; i++)
		if (!mnEqual(a, f + i, b, g + i, 1u))
			return i;
	return 0;
}

/* 4-6, from the boot of M25_mixer_split (one boot per process) */
static void mnNote(void)
{
	for (unsigned i = 0; i < MN_RUNS; i++)
	{
		int fd[2];
		ASSERT_EQ(pipe(fd), 0);
		const pid_t pid = fork();
		ASSERT_TRUE(pid >= 0);
		if (pid == 0)
		{
			close(fd[0]);
			mnCur = &mnRuns[i];
			mnArg_t a = mnArgs[i];
			emu_directCall(mnScenario, &a);
			const char *p = (const char *)mnCur;
			size_t left = sizeof *mnCur;
			while (left)
			{
				const ssize_t n = write(fd[1], p, left);
				if (n <= 0)
					_exit(1);
				p += n;
				left -= (size_t)n;
			}
			_exit(0);
		}
		close(fd[1]);
		size_t got = 0;
		for (ssize_t n; got < sizeof mnRuns[i] && (n = read(fd[0], (char *)&mnRuns[i] + got, sizeof mnRuns[i] - got)) > 0;)
			got += (size_t)n;
		close(fd[0]);
		int status = 0;
		ASSERT_EQ(waitpid(pid, &status, 0), pid);
		ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		ASSERT_EQ(got, sizeof mnRuns[i]);
	}

	/* the reference, hit 1 alone at offset 0: loud, still sounding 150 ms on, ended before the end */
	const mnRun_t *ref = &mnRuns[0];
	const unsigned on = MS_HIT1 * MS_FRAMES, end = MN_BLOCKS * MS_FRAMES;
	int loud = 0, late = 0, ended = 1;
	for (unsigned f = on; f < on + MS_FRAMES; f++)
		loud |= mnAt(ref, f, 0, 0) > 1000 || mnAt(ref, f, 0, 0) < -1000;
	for (unsigned f = on + MN_150MS - MS_FRAMES; f < on + MN_150MS; f++)
		late |= mnAt(ref, f, 0, 0) > 100 || mnAt(ref, f, 0, 0) < -100;
	for (unsigned f = end - 4u * MS_FRAMES; f < end; f++)
		ended &= mnAt(ref, f, 0, 0) == 0 && mnAt(ref, f, 0, 1) == 0;
	ASSERT_TRUE(loud);
	ASSERT_TRUE(late);
	ASSERT_TRUE(ended);

	unsigned planned = 0, silent = 0, whole = 0, kept = 0, dropped = 0, tails = 0, renotes = 0, rephase = 0;
	unsigned retriggers = 0;
	for (unsigned i = 0; i < MN_RUNS; i++)
	{
		const mnRun_t *r = &mnRuns[i];
		const mnArg_t *a = &mnArgs[i];
		const unsigned n = a->k2 == MN_NONE ? 1u : 2u;
		planned += r->applied == n && r->k[0] == a->k1 && !r->late[0]
			&& (n == 1u || (r->k[1] == a->k2 && !r->late[1]));
		silent += mnQuiet(r, on + a->k1);
		if (a->k2 == MN_NONE)
		{
			const unsigned k = a->k1;
			const int same = mnEqual(r, on + k, ref, on, end - on - k);
			whole += same;
			if (!same)
				printf("M25 note: k %u departs from the shifted k = 0 hit at note frame %u\n",
						k, mnFirstDiff(r, on + k, ref, on, end - on - k));
			int keep = 1;
			for (unsigned b = MS_HIT1; b < MS_HIT1 + (MN_150MS + MS_FRAMES - 1u) / MS_FRAMES + 1u; b++)
				keep &= r->phase[b] == k;
			kept += keep;
			dropped += r->phase[MN_BLOCKS - 1u] == 0;
			continue;
		}
		retriggers++;
		/* the run with hit 1 alone at k1 */
		const mnRun_t *alone = &mnRuns[a->k1 / 2u];
		const unsigned on2 = MN_HIT2 * MS_FRAMES + a->k2;
		tails += mnEqual(r, 0, alone, 0, on2);
		const int same = mnEqual(r, on2, ref, on, end - on2);
		renotes += same;
		if (!same)
			printf("M25 note: the retrigger at k2 %u over k1 %u departs at note frame %u\n",
					a->k2, a->k1, mnFirstDiff(r, on2, ref, on, end - on2));
		rephase += r->phase[MN_HIT2] == a->k2 && r->phase[MN_HIT2 + 50u] == a->k2;
	}
	printf("M25 note: of %u runs: planned %u, silent before hit 1 %u; of %u notes alone: hit 1 shifted by k "
			"for the whole note %u, phase k for 150 ms %u, dropped at the end %u; of %u retriggers: "
			"hit 1 alone before k2 %u, the note from k2 %u, phase k2 %u\n",
			(unsigned)MN_RUNS, planned, silent, (unsigned)MS_KS, whole, kept, dropped, retriggers,
			tails, renotes, rephase);
	ASSERT_EQ(planned, MN_RUNS);
	ASSERT_EQ(silent, MN_RUNS);
	ASSERT_EQ(whole, MS_KS);
	ASSERT_EQ(kept, MS_KS);			/* k = 0: the block grid, phase 0 */
	ASSERT_EQ(dropped, MS_KS);
	ASSERT_EQ(tails, retriggers);
	ASSERT_EQ(renotes, retriggers);
	ASSERT_EQ(rephase, retriggers);
}

/* a child of the boot: fn(arg) in a direct call, then n bytes from out through a pipe into the
 * parent's out */
static void msFork(void (*fn)(void *), void *arg, void *out, size_t n)
{
	int fd[2];
	ASSERT_EQ(pipe(fd), 0);
	const pid_t pid = fork();
	ASSERT_TRUE(pid >= 0);
	if (pid == 0)
	{
		close(fd[0]);
		emu_directCall(fn, arg);
		const char *p = out;
		size_t left = n;
		while (left)
		{
			const ssize_t w = write(fd[1], p, left);
			if (w <= 0)
				_exit(1);
			p += w;
			left -= (size_t)w;
		}
		_exit(0);
	}
	close(fd[1]);
	size_t got = 0;
	for (ssize_t r; got < n && (r = read(fd[0], (char *)out + got, n - got)) > 0;)
		got += (size_t)r;
	close(fd[0]);
	int status = 0;
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	ASSERT_EQ(got, n);
}

/* 7: each voice alone, its phase until its amp EG ends */
#define MZ_BLOCKS 240u
#define MZ_K 20u

typedef struct
{
	uint8_t phase[MZ_BLOCKS];	/* vsched_phase(v) after each block */
	uint8_t ended[MZ_BLOCKS];	/* the voice's amp EG has ended after the block, its output 0 */
	uint8_t state[MZ_BLOCKS];	/* the EG's state alone has ended (a step before its output) */
	unsigned applied;
	uint8_t k;
} mzRun_t;

static mzRun_t mzRuns[6];
static mzRun_t *mzCur;

static SlopeEg2 *mzAmpEg(uint8_t v)
{
	switch (v)
	{
	case 0: case 1: case 2:	return &voiceArray[v].oscVolEg;
	case 3:			return &snareVoice.oscVolEg;
	case 4:			return &cymbalVoice.oscVolEg;
	default:		return &hatVoice.oscVolEg;
	}
}

static void mzTrace(const vsched_rec_t *r, uint32_t serial, uint8_t k, uint8_t late)
{
	(void)r;
	(void)serial;
	(void)late;
	mzCur->k = k;
	mzCur->applied++;
}

static void mzScenario(void *arg)
{
	const uint8_t v = *(const uint8_t *)arg;	/* the voice, and its track (5: the closed hat) */
	vsched_setTrace(mzTrace);
	SlopeEg2 *eg = mzAmpEg(v);
	eg->attack = 1.0;
	if (v == 5)
		hatVoice.decayClosed = (float)MN_DECAY;	/* HiHat_trigger() sets the EG's decay from it */
	else
		eg->decay = MN_DECAY;
	static int16_t out1[MS_FRAMES * 2], out2[MS_FRAMES * 2];
	const uint32_t t0 = 0x40000000u;
	for (uint32_t b = 0; b < MZ_BLOCKS; b++)
	{
		vsched_blockDone(t0 + b * VSCHED_BLOCK_CYCLES);
		if (b == MS_HIT1)
		{
			const vsched_rec_t r = { .time = t0 + (b - 1u) * VSCHED_BLOCK_CYCLES + MZ_K * VSCHED_BLOCK_CYCLES / 64u,
				.track = v, .note = SEQ_DEFAULT_NOTE, .vel = 127,
				.flags = VS_TIMED | (DRUM_SLIDE_OFF << VS_SLIDE_SHIFT) };
			msInTick = 1;
			vsched_post(&r);
			msInTick = 0;
		}
		mixer_calcNextSampleBlock(out2, out1);
		mzCur->phase[b] = vsched_phase(v);
		const int egEnd = eg->state == EG_STOPPED || (eg->state == EG_D && eg->value <= 0.0);
		const double gain = v < 3u ? (double)voiceArray[v].ampFilterInput
			: (double)(v == 3u ? snareVoice.egValueOscVol : v == 4u ? cymbalVoice.egValueOscVol : hatVoice.egValueOscVol);
		mzCur->ended[b] = egEnd && gain == 0.0;
		mzCur->state[b] = (uint8_t)egEnd;
	}
}

static void mzSilence(void)
{
	unsigned planned = 0, ended = 0, kept = 0, dropped = 0;
	char lags[64] = "";
	for (uint8_t v = 0; v < 6u; v++)
	{
		mzCur = &mzRuns[v];
		uint8_t arg = v;
		msFork(mzScenario, &arg, mzCur, sizeof *mzCur);
		const mzRun_t *r = &mzRuns[v];
		planned += r->applied == 1u && r->k == MZ_K;
		unsigned e = MS_HIT1;
		while (e < MZ_BLOCKS && !r->ended[e])
			e++;
		if (e + 3u > MZ_BLOCKS)
			continue;			/* the EG has not ended within the run */
		ended++;
		int keep = 1, drop = 1;
		for (unsigned b = MS_HIT1; b < e; b++)
			keep &= r->phase[b] == MZ_K;
		for (unsigned b = e + 2u; b < MZ_BLOCKS; b++)
			drop &= r->phase[b] == 0;
		kept += keep;
		dropped += drop;
		unsigned lag = e;
		while (lag < MZ_BLOCKS && r->phase[lag])
			lag++;
		unsigned es = MS_HIT1;
		while (es < MZ_BLOCKS && !r->state[es])
			es++;
		snprintf(lags + strlen(lags), sizeof lags - strlen(lags), " %u/%u(%u)", e - MS_HIT1, lag - e, lag - es);
	}
	printf("M25 silence: of 6 voices: planned at k %u, amp EG ended within the run %u, phase k while it runs %u, "
			"phase 0 from 2 blocks after its end %u; blocks from the hit to the EG's end / from there to "
			"phase 0 (from the EG's state):%s\n",
			planned, ended, kept, dropped, lags);
	ASSERT_EQ(planned, 6);
	ASSERT_EQ(ended, 6);
	ASSERT_EQ(kept, 6);
	ASSERT_EQ(dropped, 6);
}

/* 8: a trigger at once over hit 1 at 20, against the record at offset 0 of check 6 */
static mnRun_t mnNowRun;

static void mnNow(void)
{
	unsigned ref = MN_RUNS;
	for (unsigned i = 0; i < MN_RUNS; i++)
		if (mnArgs[i].k1 == 20u && mnArgs[i].k2 == 0u)
			ref = i;
	ASSERT_TRUE(ref < MN_RUNS);
	mnCur = &mnNowRun;
	mnArg_t a = { 20, MN_NONE };
	mnTriggerNow = 1;
	msFork(mnScenario, &a, mnCur, sizeof *mnCur);
	mnTriggerNow = 0;
	const mnRun_t *r = &mnNowRun, *rec = &mnRuns[ref];
	const unsigned end = MN_BLOCKS * MS_FRAMES, at = MN_HIT2 * MS_FRAMES;
	const int same = mnEqual(r, 0, rec, 0, end);
	int phase = r->phase[MN_HIT2 - 1u] == 20u;
	for (unsigned b = MN_HIT2; b < MN_BLOCKS; b++)
		phase &= r->phase[b] == 0;
	printf("M25 now: a trigger at once over hit 1 at 20: applied %u record(s), the run equals the record at "
			"offset 0 %d, phase 20 before it and 0 from its block %d\n", r->applied, same, phase);
	if (!same)
		printf("M25 now: it departs from the record at offset 0 at frame %u from block 9's start\n",
				mnEqual(r, 0, rec, 0, at) ? mnFirstDiff(r, at, rec, at, end - at) : 0u);
	ASSERT_EQ(r->applied, 1);
	ASSERT_TRUE(same);
	ASSERT_TRUE(phase);
}

TEST_MAIN()
