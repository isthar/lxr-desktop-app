/*
 * test_dsp.c
 *
 * Host tests of broken states of the sound engine that fw 1.7 did not leave
 * (task E4.9):
 *
 * - The resonant filter (Core/Src/Soundengine/DSPAudio/ResonantFilter.c) of the
 *   snare, the cymbal and the hi-hat. At a high cutoff with a high resonance
 *   (FREQ 122-126 with RESO 80-127, in every filter type) its state, s1 and s2,
 *   grows to Inf and NaN within a few blocks. SVF_calcBlockZDF() clears a NaN
 *   state at the start of a block, but the firmware's -ffast-math lets the
 *   compiler assume that no float is NaN, and in fw 1.7 the test x != x was
 *   removed: the state stayed NaN, so the voice stayed silent (its samples
 *   convert to 0) until power-off, whatever the filter was set to afterwards.
 *   Only Drum 1-3 reset their filter on a trigger.
 * - The pitch EG (Core/Src/Soundengine/DSPAudio/Decay.c) of Drum 1-3 and the
 *   snare. Its slope 127 (PAR_PITCH_SLOPE1-4, CC 79-82, a modulator at its top)
 *   made the curve's factor 2/0 = Inf, and slope 0 with the pitch decay at 127
 *   (decay 0: the EG holds at 1) gave 0/0: the curve was NaN, so the oscillator's
 *   pitch and FM were NaN and its phase increment 0, and the tone vanished.
 * - The oscillators (Core/Src/Soundengine/DSPAudio/Oscillator.c). The panel
 *   offers 6 waveforms, 0-5, but a MIDI CC or a kit sets any value 0-255. For a
 *   waveform of 6 or more the block functions wrote nothing, so the voice played
 *   what its buffer held: a stack buffer of the mixer that the voices share in
 *   turn (the previous voice's block) or the drum voice's modulation buffer.
 *
 * The firmware files are compiled as they are, against the stand-in headers in
 * tests/stubs/, with the firmware's floating-point options and -O3, as on the
 * device (tests/CMakeLists.txt), so that the compiler may remove what GCC
 * removes on the device.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include <math.h>

#include "Soundengine/DSPAudio/Decay.h"
#include "Soundengine/DSPAudio/Oscillator.h"
#include "Soundengine/DSPAudio/ResonantFilter.h"
#include "Soundengine/MIDI/valueShaper.h"

#define BLOCK OUTPUT_DMA_SIZE

//------------------------------------------------------------------------------
// The filter
//------------------------------------------------------------------------------
static uint32_t noiseState;

/** half-scale white noise, about the level of the hi-hat's oscillator (gain 0.5) */
static int16_t noise(void)
{
	noiseState = noiseState * 1664525u + 1013904223u;
	return (int16_t)((int32_t)(noiseState >> 16) - 0x8000) / 2;
}

/** the filter values as the CC handler sets them from the panel's 0-127 (HAT_FILTER_F,
 *  HAT_RESO and CC2_FILTER_DRIVE_6 in MidiParser.c, the same for the snare and cymbal) */
static void setFilter(ResonantFilter *filter, uint8_t freq, uint8_t reso, uint8_t drive)
{
	SVF_directSetFilterValue(filter, valueShaperF2F(freq / 127.f, FILTER_SHAPER));
	SVF_setReso(filter, reso / 127.f);
	SVF_setDrive(filter, drive / 127.f);
}

/** one block of noise through the filter; the number of output samples that differ
 *  from the sample before them: 0 for a silent output, or one held at a value */
static int filterBlock(ResonantFilter *filter, uint8_t type)
{
	int16_t buf[BLOCK];
	int changes = 0;
	for (int i = 0; i < BLOCK; i++)
		buf[i] = noise();
	SVF_calcBlockZDF(filter, type, buf, BLOCK);
	for (int i = 1; i < BLOCK; i++)
		changes += buf[i] != buf[i - 1];
	return changes;
}

static int stateIsFinite(const ResonantFilter *filter)
{
	return isfinite(filter->s1) && isfinite(filter->s2);
}

TEST(dsp_filter_recovers_from_a_nan_state)
{
	/* settings in the latch region of the sweep (the panel's FREQ, RESO, DRIVE) */
	static const uint8_t settings[][3] = { { 125, 100, 127 }, { 126, 120, 0 }, { 124, 120, 127 } };
	for (unsigned s = 0; s < sizeof settings / sizeof settings[0]; s++) {
		for (uint8_t type = FILTER_LP; type <= FILTER_PEAK; type++) {
			ResonantFilter filter;
			SVF_init(&filter);
			noiseState = 1;
			setFilter(&filter, settings[s][0], settings[s][1], settings[s][2]);
			int diverged = 0;
			for (int b = 0; b < 50; b++) {
				filterBlock(&filter, type);
				diverged |= !stateIsFinite(&filter);
			}
			/* the test reaches the broken state; if not, it tests nothing */
			ASSERT_TRUE(diverged);

			/* the filter turned back to a stable setting: the voice sounds again once a
			 * large but finite state has decayed (8 blocks on the host), and stays so */
			setFilter(&filter, 80, 0, 0);
			int b = 0;
			while (b < 20 && filterBlock(&filter, type) <= BLOCK / 2)
				b++;
			ASSERT_TRUE(b < 20);
			for (b = 0; b < 50; b++)
				ASSERT_TRUE(filterBlock(&filter, type) > BLOCK / 2);
			ASSERT_TRUE(stateIsFinite(&filter));
		}
	}
}

//------------------------------------------------------------------------------
// The pitch EG
//------------------------------------------------------------------------------
static const uint8_t decays[] = { 0, 32, 64, 100, 120, 126, 127 };

/** fw 1.7's curve for a slope of 1-126, in double: the slope factor of
 *  DecayEg_setSlope(), the curve of DecayEg_calcSlopeValue() */
static double slopeCurve(uint8_t slope, double value)
{
	const double amount = (slope / 127.0 - 0.5) * 2;
	const double k = 2 * amount / (1 - amount);
	return (1 + k) * value / (1 + k * fabs(value));
}

TEST(dsp_pitch_eg_is_finite_at_every_slope_and_decay)
{
	for (unsigned slope = 0; slope <= 127; slope++) {
		for (unsigned d = 0; d < sizeof decays; d++) {
			DecayEg eg;
			DecayEg_init(&eg);
			DecayEg_setSlope(&eg, (uint8_t)slope);
			DecayEg_setDecay(&eg, decays[d]);
			DecayEg_trigger(&eg);
			for (int n = 0; n < 2000; n++) {
				const float v = DecayEg_calc(&eg);
				if (!isfinite(v))
					printf("slope %u decay %u: value %g after %d steps\n", slope, decays[d], v, n);
				ASSERT_TRUE(isfinite(v));
				ASSERT_TRUE(v >= -1.f && v <= 1.f);
			}
		}
	}
}

TEST(dsp_pitch_eg_curve_of_slopes_0_to_126_is_unchanged)
{
	for (unsigned slope = 0; slope <= 126; slope++) {
		for (unsigned d = 0; d < sizeof decays; d++) {
			if (slope == 0 && decays[d] == 127)
				continue;	/* fw 1.7: NaN */
			DecayEg eg;
			DecayEg_init(&eg);
			DecayEg_setSlope(&eg, (uint8_t)slope);
			DecayEg_setDecay(&eg, decays[d]);
			DecayEg_trigger(&eg);
			for (int n = 0; n < 2000; n++) {
				const float v = DecayEg_calc(&eg);
				if (slope == 0)		/* a flat curve: 0 for every value below 1 */
					ASSERT_EQ_DBL(v, 0.0);
				else
					ASSERT_NEAR(v, slopeCurve((uint8_t)slope, eg.value), 1e-5);
			}
		}
	}
}

//------------------------------------------------------------------------------
// The oscillators
//------------------------------------------------------------------------------
static uint32_t rngState;

/** an oscillator of a voice, at about 440 Hz */
static OscInfo oscillator(uint8_t waveform)
{
	OscInfo osc;
	memset(&osc, 0, sizeof osc);
	osc.waveform = waveform;
	osc.freq = 440.f;
	osc.pitchMod = 1.f;
	osc.modNodeValue = 1.f;
	osc.fmMod = 0.5f;
	osc.phaseInc = 0x00A00000u;
	rngState = 1;
	return osc;
}

/** one block of the oscillator (with FM from a sine block if fm) into a buffer that
 *  holds fill before; fills the block it gave */
static void oscBlock(uint8_t waveform, int fm, int16_t fill, int16_t out[BLOCK])
{
	OscInfo osc = oscillator(waveform);
	for (int i = 0; i < BLOCK; i++)
		out[i] = fill;
	if (fm) {
		OscInfo modOsc = oscillator(SINE);
		int16_t modBuf[BLOCK];
		calcNextOscSampleBlock(&modOsc, modBuf, BLOCK, 0.8f, 0.5f);
		calcNextOscSampleFmBlock(&osc, modBuf, out, BLOCK, 1.f, 0.5f);
	} else {
		calcNextOscSampleBlock(&osc, out, BLOCK, 1.f, 0.5f);
	}
}

TEST(dsp_oscillator_output_does_not_depend_on_the_old_buffer)
{
	/* PWM is left out: its read past the saw table (out of scope, Q13) */
	for (unsigned waveform = 0; waveform <= 255; waveform++) {
		if (waveform == PWM)
			continue;
		for (int fm = 0; fm <= 1; fm++) {
			int16_t a[BLOCK], b[BLOCK];
			oscBlock((uint8_t)waveform, fm, 0x5A5A, a);
			oscBlock((uint8_t)waveform, fm, -0x1234, b);
			for (int i = 0; i < BLOCK; i++) {
				if (a[i] != b[i])
					printf("waveform %u fm %d: sample %d is %d or %d\n", waveform, fm, i, a[i], b[i]);
				ASSERT_EQ(a[i], b[i]);
				/* an unknown waveform is silent */
				if (waveform >= NUM_OSC_WAVEFORMS)
					ASSERT_EQ(a[i], 0);
			}
		}
	}
}

TEST_MAIN()

//------------------------------------------------------------------------------
// Stand-ins for the functions that Oscillator.c calls
//------------------------------------------------------------------------------
uint32_t GetRngValue(void)
{
	rngState = rngState * 1664525u + 1013904223u;
	return rngState >> 16;
}

float midiParser_calcDetune(uint8_t value)
{
	(void)value;
	return 1.f;
}
