/*
 * test_voicesplit.c
 *
 * LICHOMAT v0.5 (plan Task 6, spec 9.1 "dsp_split", here voice_split): the six
 * voices rendered in two parts, as the mixer renders a voice at a record split or
 * a phase split, compiled from the firmware's DSP files with test_dsp's options.
 * 1. A block rendered as [0, k) + [k, 64) without a trigger gives the samples and
 *    leaves the state (oscillator phases, filter state, smoothers, the noise draws)
 *    of one 64-sample call: bit for bit for the snare, cymbal and hi-hat, within
 *    1 LSB for Drum 1-3 (whose gain ramp is split, not changed).
 * 2. A silent voice triggered at k: [0, k) stays silent, and from k on its samples
 *    are those of the same trigger at offset 0, for the 128 - k samples up to the
 *    next block boundary after its second control step (S13: the first step lasts a
 *    whole block, the second ends at the block grid).
 * 3. The snap at transient pitch 4 (+24 semitones for its first step, SnapEg 0.2 per
 *    block) is one of the sounds of 2, so its first step lasts 64 samples from k.
 *
 * The stand-ins: the LFOs, the modulation nodes and the faders (1.0); GetRngValue()
 * is a counter the test resets.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include <string.h>
#include "Soundengine/DSPAudio/voices.h"
#include "Soundengine/DSPAudio/Oscillator.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "IO/adcPots.h"
#include "Parameters.h"

#define BLOCK OUTPUT_DMA_SIZE

//------------------------------------------------------------------------------
// Stand-ins
//------------------------------------------------------------------------------
uint8_t activeParameters[NUM_PARAMS];
ModulationNode velocityModulators[6];
static uint32_t rng = 1;
uint32_t GetRngValue(void) { rng = rng * 1664525u + 1013904223u; return rng; }
float adc_getFaderVolume(uint8_t voice) { (void)voice; return 1.0f; }
void lfo_init(Lfo *lfo) { memset(lfo, 0, sizeof *lfo); }
void lfo_retrigger(uint8_t voiceNr) { (void)voiceNr; }
void lfo_dispatchNextValue(Lfo *lfo) { (void)lfo; }
void modNode_updateValue(ModulationNode *vm, float val) { (void)vm; (void)val; }
float midiParser_calcDetune(uint8_t value) { (void)value; return 1.0f; }

//------------------------------------------------------------------------------
// The voices
//------------------------------------------------------------------------------
typedef struct
{
	uint8_t snap;		/* transient waveform 0 (snap) at pitch 4, else no transient */
	uint8_t attack;		/* 1: instant attack (slopeEg2 attack 1), else a ramp of 4 blocks */
	uint8_t filter;		/* the drums' filter on (it resets at a trigger) */
} sound_t;

static void setSound(const sound_t *s)
{
	memset(voiceArray, 0, sizeof voiceArray);
	memset(&snareVoice, 0, sizeof snareVoice);
	memset(&cymbalVoice, 0, sizeof cymbalVoice);
	memset(&hatVoice, 0, sizeof hatVoice);
	initDrumVoice();
	Snare_init();
	Cymbal_init();
	HiHat_init();
	rng = 1;
	for (int v = 0; v < 3; v++)
	{
		DrumVoice *d = &voiceArray[v];
		d->osc.waveform = (uint8_t)(v == 1 ? SAW : SINE);
		d->modOsc.waveform = SINE;
		d->fmModAmount = 0.3f;
		d->egPitchModAmount = 0.8f;
		DecayEg_setDecay(&d->oscPitchEg, 40);
		d->oscVolEg.attack = s->attack ? 1.0 : 0.25;
		d->oscVolEg.decay = 0.02;
		d->transGen.waveform = s->snap ? 0 : 1;
		d->transGen.pitch = 4.f;
		d->transGen.volume = s->snap ? 1.f : 0.f;
		d->filterType = s->filter ? 0x01 : FILTER_OFF;
		SVF_directSetFilterValue(&d->filter, 0.3f);
		d->volumeMod = 1;
	}
	/* the snare's noise off (mix 0) and no filter: the noise and the filter state do
	 * not reset at a trigger */
	snareVoice.mix = 0.f;
	snareVoice.filterType = FILTER_OFF;
	snareVoice.osc.waveform = SINE;
	snareVoice.oscVolEg.attack = 1.0;
	snareVoice.oscVolEg.decay = 0.03;
	snareVoice.transGen.waveform = s->snap ? 0 : 1;
	snareVoice.transGen.pitch = 4.f;
	snareVoice.transGen.volume = s->snap ? 1.f : 0.f;
	/* the cymbal's and the hi-hat's oscillators as sines: a noise waveform draws on at
	 * every sample, and its draws do not reset at a trigger */
	cymbalVoice.osc.waveform = cymbalVoice.modOsc.waveform = cymbalVoice.modOsc2.waveform = SINE;
	hatVoice.osc.waveform = hatVoice.modOsc.waveform = hatVoice.modOsc2.waveform = SINE;
	cymbalVoice.filterType = FILTER_OFF;
	cymbalVoice.oscVolEg.attack = 1.0;
	cymbalVoice.oscVolEg.decay = 0.03;
	cymbalVoice.transGen.waveform = 1;
	hatVoice.filterType = FILTER_OFF;
	hatVoice.decayClosed = 0.05f;
	hatVoice.decayOpen = 0.01f;
	hatVoice.oscVolEg.attack = 1.0;
	hatVoice.transGen.waveform = 1;
}

static void trigger(uint8_t v)
{
	switch (v)
	{
	case 0: case 1: case 2:	Drum_trigger(v, 110, 60); break;
	case 3:			Snare_trigger(110, 60); break;
	case 4:			Cymbal_trigger(110, 60); break;
	default:		HiHat_trigger(110, 0, 60); break;
	}
}

/** one block of voice v as the mixer runs it: the block's control step (unless a
 * phase phi is pending), then the render, split at k (with a trigger) or at phi */
static uint8_t block(uint8_t v, int16_t *out, uint8_t phi, uint8_t k, int trig)
{
	if (!phi)
		voices_control(v);
	if (k)
	{
		voices_render(v, out, k);
		if (trig)
			trigger(v);
		voices_control(v);
		voices_render(v, out + k, (uint8_t)(BLOCK - k));
		return k;
	}
	if (phi)
	{
		voices_render(v, out, phi);
		voices_control(v);
		voices_render(v, out + phi, (uint8_t)(BLOCK - phi));
		return 0;
	}
	voices_render(v, out, BLOCK);
	return 0;
}

static void *stateOf(uint8_t v, size_t *n)
{
	switch (v)
	{
	case 0: case 1: case 2:	*n = sizeof voiceArray[v]; return &voiceArray[v];
	case 3:			*n = sizeof snareVoice; return &snareVoice;
	case 4:			*n = sizeof cymbalVoice; return &cymbalVoice;
	default:		*n = sizeof hatVoice; return &hatVoice;
	}
}

//------------------------------------------------------------------------------
// 1: a split without a trigger
//------------------------------------------------------------------------------
static int maxDiff(const int16_t *a, const int16_t *b, int n)
{
	int m = 0;
	for (int i = 0; i < n; i++)
	{
		const int d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
		if (d > m)
			m = d;
	}
	return m;
}

/** a note: the smoothers settled, a trigger at offset 0; its first block (the test
 * block, loudest: its amp EG falls fast) is played whole (k 0) or as [0, k) + [k, 64)
 * with no trigger at k, then one block more */
static void ringing(uint8_t v, uint8_t k, int16_t *out, uint8_t *state, uint32_t *rngAfter)
{
	static const sound_t s = { 0, 1, 1 };
	__attribute__((aligned(4))) int16_t buf[BLOCK];
	setSound(&s);
	for (int b = 0; b < 200; b++)
		block(v, buf, 0, 0, 0);
	trigger(v);
	voices_control(v);
	if (k)
	{
		voices_render(v, out, k);
		voices_render(v, out + k, (uint8_t)(BLOCK - k));
	}
	else
		voices_render(v, out, BLOCK);
	block(v, out + BLOCK, 0, 0, 0);
	size_t n;
	memcpy(state, stateOf(v, &n), n);
	*rngAfter = rng;
}

TEST(voice_split_without_trigger)
{
	static uint8_t whole[sizeof(DrumVoice) + sizeof(SnareVoice) + sizeof(CymbalVoice) + sizeof(HiHatVoice)];
	static uint8_t split[sizeof whole];
	__attribute__((aligned(4))) int16_t a[2 * BLOCK], b[2 * BLOCK];
	for (uint8_t v = 0; v < VOICES_COUNT; v++)
	{
		uint32_t ra, rb;
		ringing(v, 0, a, whole, &ra);
		int loud = 0;
		for (int i = 0; i < BLOCK; i++)
			loud |= a[i] > 1000 || a[i] < -1000;
		ASSERT_TRUE(loud);				/* the test hears the note */
		for (uint8_t k = 2; k < BLOCK; k += 2)
		{
			ringing(v, k, b, split, &rb);
			size_t n;
			stateOf(v, &n);
			ASSERT_EQ(rb, ra);
			ASSERT_EQ(memcmp(whole, split, n), 0);
			ASSERT_TRUE(maxDiff(a, b, 2 * BLOCK) <= (v < 3 ? 1 : 0));
		}
	}
}

//------------------------------------------------------------------------------
// 2, 3: a trigger at k
//------------------------------------------------------------------------------
/** a silent voice (settled smoothers), triggered at offset k of a block; three
 * blocks into out, as the mixer plays them */
static void hit(uint8_t v, const sound_t *s, uint8_t k, int16_t *out)
{
	__attribute__((aligned(4))) int16_t buf[BLOCK];
	setSound(s);
	for (int b = 0; b < 200; b++)			/* 12,800 samples: the one-pole smoothers settle */
		block(v, buf, 0, 0, 0);
	if (k == 0)
	{
		trigger(v);
		block(v, out, 0, 0, 0);
		block(v, out + BLOCK, 0, 0, 0);
	}
	else
	{
		const uint8_t phi = block(v, out, 0, k, 1);
		block(v, out + BLOCK, phi, 0, 0);
	}
	block(v, out + 2 * BLOCK, 0, 0, 0);
}

TEST(voice_split_trigger_at_k)
{
	static const sound_t sounds[] = { { 0, 1, 1 }, { 0, 0, 1 }, { 1, 1, 1 }, { 1, 0, 0 } };
	__attribute__((aligned(4))) int16_t a[3 * BLOCK], b[3 * BLOCK];
	for (unsigned si = 0; si < sizeof sounds / sizeof sounds[0]; si++)
		for (uint8_t v = 0; v < VOICES_COUNT; v++)
		{
			hit(v, &sounds[si], 0, a);
			int loud = 0;
			for (int i = 0; i < BLOCK; i++)
				loud |= a[i] > 1000 || a[i] < -1000;
			ASSERT_TRUE(loud);
			for (uint8_t k = 2; k < BLOCK; k += 2)
			{
				hit(v, &sounds[si], k, b);
				for (int i = 0; i < k; i++)
					ASSERT_EQ(b[i], 0);			/* silent before its onset */
				/* from k: the k = 0 render's first 128 - k samples (step 1 whole, step 2
				 * up to the block grid) */
				ASSERT_TRUE(maxDiff(b + k, a, 2 * BLOCK - k) <= 1);
			}
		}
}

TEST_MAIN()
