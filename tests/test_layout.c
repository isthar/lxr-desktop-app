/*
 * test_layout.c
 *
 * The step types keep the layout of the fw 1.7 pattern files (PatV03): Step is
 * stored raw (7 bytes), StepTiming as one byte with flam in bits 0-3 and shift
 * in bits 4-7. seqc_event_t is 24 bytes (N2). Including seq_core.h here also
 * shows that it builds without the HAL: no HAL directory is on the include path
 * of the host tests.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Preset/SeqStepTypes.h"
#include "Soundengine/Sequencer/seq_core.h"
#include "seqc_test.h"

TEST(layout_step)
{
	ASSERT_EQ(sizeof(Step), 7);
	ASSERT_EQ(offsetof(Step, volume), 0);
	ASSERT_EQ(offsetof(Step, prob), 1);
	ASSERT_EQ(offsetof(Step, note), 2);
	ASSERT_EQ(offsetof(Step, param1Nr), 3);
	ASSERT_EQ(offsetof(Step, param1Val), 4);
	ASSERT_EQ(offsetof(Step, param2Nr), 5);
	ASSERT_EQ(offsetof(Step, param2Val), 6);
}

TEST(layout_step_timing)
{
	ASSERT_EQ(sizeof(StepTiming), 1);
	const StepTiming t = { .flam = 5, .shift = 10 };
	uint8_t b;
	memcpy(&b, &t, 1);
	ASSERT_EQ(b, 0xA5);				/* flam in bits 0-3, shift in bits 4-7 */
}

TEST(layout_event)
{
	ASSERT_EQ(sizeof(seqc_event_t), 24);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.6 (plan v0.6 Task 2, spec 10.1 step_micro): the microtiming's split and the
// template's checks
//------------------------------------------------------------------------------
TEST(step_micro_sizes)
{
	ASSERT_EQ(sizeof(TrackTemplate), 2);
	ASSERT_EQ(offsetof(TrackTemplate, id), 0);
	ASSERT_EQ(offsetof(TrackTemplate, amount), 1);
	/* block 2: "LCHM", the block number, the track count, 7 templates, 7 tracks of nibbles */
	ASSERT_EQ(6 + 7 * sizeof(TrackTemplate) + 7 * STEP_FINE_BYTES, 244);
}

TEST(step_micro_split_round_trips_every_value)
{
	for (int micro = -STEP_MICRO_MAX; micro <= STEP_MICRO_MAX; micro++)
	{
		StepTiming t = { .flam = 5, .shift = 0 };
		const int8_t fine = stepMicro_split(micro, &t);
		ASSERT_TRUE(t.shift <= 14);
		ASSERT_TRUE(fine >= STEP_FINE_MIN && fine <= STEP_FINE_MAX);
		ASSERT_EQ(t.flam, 5);				/* the flam stays */
		ASSERT_EQ(stepMicro(t, fine), micro);
		/* the coarse part is the nearest unit, ties upwards: floor((T + 6) / 12) */
		const int c = (int)t.shift - 7;
		ASSERT_TRUE(12 * c - 6 <= micro && micro < 12 * c + 6);
	}
}

TEST(step_micro_split_edges_and_clamp)
{
	static const struct { int micro, shift, fine; } want[] = {
		{ 0, 7, 0 }, { 5, 7, 5 }, { 6, 8, -6 }, { 7, 8, -5 }, { -5, 7, -5 }, { -6, 7, -6 },
		{ -7, 6, 5 }, { 78, 14, -6 }, { 79, 14, -5 }, { 84, 14, 0 }, { -78, 1, -6 }, { -79, 0, 5 },
		{ -84, 0, 0 }, { 85, 14, 0 }, { 89, 14, 0 }, { -90, 0, 0 }, { 1000, 14, 0 },
	};
	for (unsigned k = 0; k < sizeof want / sizeof want[0]; k++)
	{
		StepTiming t = { .flam = 0, .shift = 7 };
		const int8_t fine = stepMicro_split(want[k].micro, &t);
		ASSERT_EQ(t.shift, want[k].shift);
		ASSERT_EQ(fine, want[k].fine);
	}
	/* the reading of a stored pair: a shift of 15 counts as 0, a fine part out of range as 0 */
	ASSERT_EQ(stepMicro((StepTiming){ .shift = 15 }, 3), 3);
	ASSERT_EQ(stepMicro((StepTiming){ .shift = 14 }, 5), 89);	/* a file's pair past the reach */
	ASSERT_EQ(stepMicro((StepTiming){ .shift = 0 }, -6), -90);
	ASSERT_EQ(stepMicro((StepTiming){ .shift = 9 }, 6), 24);
	ASSERT_EQ(stepMicro((StepTiming){ .shift = 9 }, -7), 24);
}

TEST(step_micro_nibbles)
{
	/* -8, -7, +6, +7 read as 0; -6..+5 as themselves */
	static const int8_t read[16] = { 0, 1, 2, 3, 4, 5, 0, 0, 0, 0, -6, -5, -4, -3, -2, -1 };
	for (uint8_t n = 0; n < 16; n++)
	{
		ASSERT_EQ(stepFine_decode(n), read[n]);
		ASSERT_EQ(stepFine_decode((uint8_t)(n | 0xA0u)), read[n]);	/* the high nibble is ignored */
	}
	uint8_t track[STEP_FINE_BYTES];
	memset(track, 0, sizeof track);
	stepFine_set(track, 6, -6);
	stepFine_set(track, 7, 5);
	stepFine_set(track, 63, -1);
	ASSERT_EQ(track[3], 0x5A);				/* step 6 in bits 0-3, step 7 in bits 4-7 */
	ASSERT_EQ(track[31], 0xF0);
	ASSERT_EQ(stepFine_get(track, 6), -6);
	ASSERT_EQ(stepFine_get(track, 7), 5);
	ASSERT_EQ(stepFine_get(track, 63), -1);
	ASSERT_EQ(stepFine_get(track, 62), 0);
	stepFine_set(track, 6, 0);
	ASSERT_EQ(track[3], 0x50);				/* the neighbour stays */
}

TEST(step_micro_template_checked)
{
	for (unsigned id = 0; id < 256; id++)
	{
		const TrackTemplate t = trackTemplate_checked((TrackTemplate){ .id = (uint8_t)id, .amount = 37 });
		ASSERT_EQ(t.id, id < TEMPLATE_COUNT ? id : 0);
		ASSERT_EQ(t.amount, 37);
	}
	for (int a = -128; a < 128; a++)
	{
		const TrackTemplate t = trackTemplate_checked((TrackTemplate){ .id = 3, .amount = (int8_t)a });
		ASSERT_EQ(t.amount, (a < -100 || a > 100) ? 0 : a);
		ASSERT_EQ(t.id, 3);
	}
}

TEST_MAIN()
