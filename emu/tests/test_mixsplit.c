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
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/MIDI/MidiMessages.h"
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
}

TEST_MAIN()
