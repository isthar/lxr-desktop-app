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
 *    are those of the same trigger at offset 0 for the whole note, until it has ended
 *    (the persistent note phase: every control step of the note comes at k and lasts
 *    64 samples, its two parts in two blocks).
 * 3. The snap at transient pitch 4 (+24 semitones for its first step, SnapEg 0.2 per
 *    block) is one of the sounds of 2, so its first step lasts 64 samples from k.
 * 4. A retrigger at k over a ringing note: [0, k) is the old note's, and a drum's new step
 *    ramps from the old step's gain at its last sample (review of Task 7).
 * 5. A retrigger at k2 over a note whose phase is phi, two blocks after its onset: [0, k2) is
 *    the note's as without the retrigger; a drum's new step ramps from the gain the old step
 *    reached: k2 < phi cuts the step that began at phi in the block before at 64 - phi + k2
 *    samples, k2 = phi ends it whole, k2 > phi cuts the step that began at phi at k2 - phi.
 * 6. voices_silent(): once it holds at a block's start, the voice renders only zeros until its
 *    next trigger, with its phase kept or dropped; it holds within 2 blocks of the amp EG's end.
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
#include "Soundengine/DSPAudio/BufferTools.h"
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

/** one block of voice v as the mixer runs it, with the voice's phase phi (0: the block
 * grid) and a record split at k (a trigger there if trig): the block's control step on the
 * block grid; with a phase, the note's control step at phi, unless the split at k <= phi
 * cuts the step that began at phi in the block before; the split's control step at k.
 * Returns the voice's phase after the block */
static uint8_t block(uint8_t v, int16_t *out, uint8_t phi, uint8_t k, int trig)
{
	uint8_t pos = 0;
	if (!phi)
		voices_control(v);
	else if (!k || phi < k)
	{
		voices_render(v, out, phi);
		voices_control(v);
		pos = phi;
	}
	if (k)
	{
		voices_render(v, out + pos, (uint8_t)(k - pos));
		if (trig)
			trigger(v);
		voices_control(v);
		pos = k;
	}
	voices_render(v, out + pos, (uint8_t)(BLOCK - pos));
	return k ? k : phi;
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
#define NOTE_BLOCKS 64	/* 93 ms: every note of setSound() ends within it */

/** a silent voice (settled smoothers), triggered at offset k of a block; NOTE_BLOCKS
 * blocks into out, as the mixer plays them */
static void hit(uint8_t v, const sound_t *s, uint8_t k, int16_t *out)
{
	__attribute__((aligned(4))) int16_t buf[BLOCK];
	setSound(s);
	for (int b = 0; b < 200; b++)			/* 12,800 samples: the one-pole smoothers settle */
		block(v, buf, 0, 0, 0);
	if (k == 0)
		trigger(v);
	uint8_t phi = block(v, out, 0, k, k != 0);
	for (int b = 1; b < NOTE_BLOCKS; b++)
		phi = block(v, out + b * BLOCK, phi, 0, 0);
}

TEST(voice_split_trigger_at_k)
{
	static const sound_t sounds[] = { { 0, 1, 1 }, { 0, 0, 1 }, { 1, 1, 1 }, { 1, 0, 0 } };
	static __attribute__((aligned(4))) int16_t a[NOTE_BLOCKS * BLOCK], b[NOTE_BLOCKS * BLOCK];
	for (unsigned si = 0; si < sizeof sounds / sizeof sounds[0]; si++)
		for (uint8_t v = 0; v < VOICES_COUNT; v++)
		{
			hit(v, &sounds[si], 0, a);
			int loud = 0, ended = 1;
			for (int i = 0; i < BLOCK; i++)
				loud |= a[i] > 1000 || a[i] < -1000;
			for (int i = (NOTE_BLOCKS - 2) * BLOCK; i < NOTE_BLOCKS * BLOCK; i++)
				ended &= a[i] == 0;
			ASSERT_TRUE(loud);
			ASSERT_TRUE(ended);				/* the comparison covers the whole note */
			for (uint8_t k = 2; k < BLOCK; k += 2)
			{
				hit(v, &sounds[si], k, b);
				for (int i = 0; i < k; i++)
					ASSERT_EQ(b[i], 0);			/* silent before its onset */
				/* from k: the k = 0 render, the whole note */
				ASSERT_TRUE(maxDiff(b + k, a, NOTE_BLOCKS * BLOCK - k) <= 1);
			}
		}
}

//------------------------------------------------------------------------------
// 4, 5: a retrigger over a ringing note, a first step past 64 samples (review of Task 7)
//------------------------------------------------------------------------------
static const sound_t rampAttack = { 0, 0, 1 };	/* the attack ramps over 4 blocks */

/** the drum's gain after a step cut at its sample n (drum_endStep(), from the step's lastGain and
 * target as floats) */
static double cutGain(float last, float target, unsigned n)
{
	return last + ((n - 1u) / (BLOCK - 1.f)) * (double)(target - last);
}

TEST(voice_split_ringing_tail)
{
	/* a note in its second block (a drum's attack ramp), retriggered at k: [0, k) is the old note's
	 * as without the retrigger, bit for bit (S14); a drum's new step ramps from the old step's gain
	 * at its last sample before k (P5) */
	__attribute__((aligned(4))) int16_t buf[BLOCK], a[BLOCK], b[BLOCK];
	for (uint8_t v = 0; v < VOICES_COUNT; v++)
		for (uint8_t k = 2; k < BLOCK; k += 2)
		{
			double last = 0., target = 0.;
			for (int retrigger = 0; retrigger < 2; retrigger++)
			{
				setSound(&rampAttack);
				for (int n = 0; n < 200; n++)
					block(v, buf, 0, 0, 0);
				trigger(v);
				block(v, buf, 0, 0, 0);
				voices_control(v);
				if (v < 3)
				{
					last = voiceArray[v].lastGain;
					target = voiceArray[v].ampFilterInput;
				}
				if (!retrigger)
				{
					voices_render(v, a, BLOCK);
					continue;
				}
				voices_render(v, b, k);
				trigger(v);
				voices_control(v);
				voices_render(v, b + k, (uint8_t)(BLOCK - k));
			}
			int heard = 0;
			for (int i = 0; i < BLOCK; i++)
				heard |= a[i] != 0;
			ASSERT_TRUE(heard);				/* the tail rings (peak 52 for the hi-hat) */
			ASSERT_TRUE(maxDiff(a, b, k) == 0);
			if (v < 3)
			{
				ASSERT_TRUE(target > last);			/* the old step ramps */
				ASSERT_NEAR(voiceArray[v].lastGain, cutGain(last, target, k), 1e-6);
			}
		}
}

TEST(voice_split_retrigger_phase)
{
	/* a note at phi, retriggered at k2 two blocks after its onset (spec 4.2 amended, the persistent
	 * note phase): [0, k2) is the note's as without the retrigger, bit for bit, for every voice; a
	 * drum's new step ramps from the gain at the last sample of the step that k2 cuts: k2 < phi, the
	 * step from phi of the block before at its sample 64 - phi + k2; k2 = phi, that step whole, its
	 * target; k2 > phi, the step from phi of this block at its sample k2 - phi */
	__attribute__((aligned(4))) int16_t buf[BLOCK], a[BLOCK], b[BLOCK];
	static const uint8_t phis[] = { 2, 20, 40, 62 };
	for (uint8_t v = 0; v < VOICES_COUNT; v++)
		for (unsigned p = 0; p < sizeof phis; p++)
			for (uint8_t k2 = 2; k2 < BLOCK; k2 += 2)
			{
				const uint8_t phi = phis[p];
				double last = 0., target = 0., lastAt = 0., targetAt = 0.;
				for (int retrigger = 0; retrigger < 2; retrigger++)
				{
					setSound(&rampAttack);
					for (int n = 0; n < 200; n++)
						block(v, buf, 0, 0, 0);
					uint8_t ph = block(v, buf, 0, phi, 1);	/* the note at phi: its first step */
					ph = block(v, buf, ph, 0, 0);		/* its second step, from phi */
					ASSERT_EQ(ph, phi);
					if (v < 3)
					{
						last = voiceArray[v].lastGain;
						target = voiceArray[v].ampFilterInput;
					}
					if (!retrigger)
					{
						block(v, a, ph, 0, 0);		/* its third step, from phi */
						if (v < 3)
						{
							lastAt = voiceArray[v].lastGain;
							targetAt = voiceArray[v].ampFilterInput;
						}
						continue;
					}
					ASSERT_EQ(block(v, b, ph, k2, 1), k2);
				}
				ASSERT_TRUE(maxDiff(a, b, k2) == 0);
				if (v >= 3)
					continue;
				ASSERT_TRUE(target > last);			/* the steps ramp (the attack) */
				ASSERT_TRUE(targetAt > lastAt);
				if (k2 < phi)
					ASSERT_NEAR(voiceArray[v].lastGain, cutGain((float)last, (float)target, (unsigned)(BLOCK - phi + k2)), 1e-6);
				else if (k2 == phi)
					ASSERT_EQ_DBL(voiceArray[v].lastGain, target);
				else
					ASSERT_NEAR(voiceArray[v].lastGain, cutGain((float)lastAt, (float)targetAt, (unsigned)(k2 - phi)), 1e-6);
			}
	/* the ramp past its last sample holds its value there (a step is never longer than 64
	 * samples now; the clamp stays) */
	int16_t r[30];
	for (int i = 0; i < 30; i++)
		r[i] = 10000;
	bufferTool_addGainRamp(r, 0.5f, 0.1f, 50, 30);
	ASSERT_TRUE(r[0] < r[13]);
	for (int i = 13; i < 30; i++)				/* samples 63 to 79 of the step */
		ASSERT_EQ(r[i], r[13]);
}

//------------------------------------------------------------------------------
// 6: voices_silent()
//------------------------------------------------------------------------------
static const SlopeEg2 *egOf(uint8_t v)
{
	switch (v)
	{
	case 0: case 1: case 2:	return &voiceArray[v].oscVolEg;
	case 3:			return &snareVoice.oscVolEg;
	case 4:			return &cymbalVoice.oscVolEg;
	default:		return &hatVoice.oscVolEg;
	}
}

TEST(voice_split_silent)
{
	/* each voice and sound, a note at k (0, or a phase): voices_silent() is false at the start of
	 * its first blocks; from the first block where it holds, the voice renders only zeros, with
	 * its phase kept or dropped (the mixer drops it), for 40 blocks; it holds within 2 blocks of
	 * the amp EG's end (the gain of the step at the end, then lastGain, reach 0). The shaped EG
	 * renders zeros earlier than that: a drum of decay 0.02 from about block 19, its EG ends at
	 * 50; the predicate waits for the EG */
	static const sound_t sounds[] = { { 0, 1, 1 }, { 0, 0, 1 }, { 1, 1, 1 }, { 1, 0, 0 } };
	static const uint8_t ks[] = { 0, 2, 34, 62 };
	__attribute__((aligned(4))) int16_t buf[BLOCK];
	for (unsigned si = 0; si < sizeof sounds / sizeof sounds[0]; si++)
		for (uint8_t v = 0; v < VOICES_COUNT; v++)
			for (unsigned ki = 0; ki < sizeof ks; ki++)
				for (int drop = 0; drop < 2; drop++)
				{
					const uint8_t k = ks[ki];
					setSound(&sounds[si]);
					for (int n = 0; n < 200; n++)
						block(v, buf, 0, 0, 0);
					if (k == 0)
						trigger(v);
					uint8_t phi = block(v, buf, 0, k, k != 0);
					int egEnd = 0, b = 1;
					for (; b < 200 && !voices_silent(v); b++)
					{
						if (!egEnd && voices_egEnded(egOf(v)))
							egEnd = b;
						phi = block(v, buf, phi, 0, 0);
					}
					ASSERT_TRUE(b < 200);
					ASSERT_TRUE(b > 4);			/* not at the note's start */
					ASSERT_TRUE(egEnd > 0 || voices_egEnded(egOf(v)));
					ASSERT_TRUE(b - (egEnd ? egEnd : b) <= 2);
					if (drop)
						phi = 0;
					int zeros = 1;
					for (int n = 0; n < 40; n++)
					{
						ASSERT_TRUE(voices_silent(v));
						phi = block(v, buf, phi, 0, 0);
						for (int i = 0; i < BLOCK; i++)
							zeros &= buf[i] == 0;
					}
					ASSERT_TRUE(zeros);
				}
}

TEST_MAIN()
