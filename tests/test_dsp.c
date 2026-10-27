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

TEST_MAIN()
