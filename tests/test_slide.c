/*
 * test_slide.c
 *
 * GL: the glide of LICHOMAT v0.3's slide (plan P3). GL1: the blocks of a slide
 * time value, the pitch factor per block from an octave below, a new slide that
 * starts where the glide is, and no glide without a time or an interval
 * (Core/Inc/Soundengine/DSPAudio/slide.h).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include "seqc_test.h"
#include "Soundengine/DSPAudio/slide.h"

TEST(GL1_blocks_of_the_slide_time)
{
	/* value * 4 ms at 44.1 kHz in 64-sample blocks, rounded */
	ASSERT_EQ(slide_blocks(0), 0);
	ASSERT_EQ(slide_blocks(1), 3);		/* 176 samples */
	ASSERT_EQ(slide_blocks(15), 41);	/* 60 ms: 2646 */
	ASSERT_EQ(slide_blocks(127), 350);	/* 508 ms: 22402 */
}

TEST(GL1_glide_from_an_octave_below)
{
	slide_t s;
	slide_stop(&s);
	ASSERT_NEAR(slide_next(&s), 1.0, 0.0);
	slide_start(&s, -12.0f, 4);
	static const double want[6] = { 0.5, 0.594604, 0.707107, 0.840896, 1.0, 1.0 };
	for (int k = 0; k < 6; k++)
		ASSERT_NEAR(slide_next(&s), want[k], 1e-5);
	ASSERT_NEAR(slide_offset(&s), 0.0, 0.0);
}

TEST(GL1_a_new_slide_starts_where_the_glide_is)
{
	slide_t s;
	slide_start(&s, -12.0f, 4);
	(void)slide_next(&s);
	(void)slide_next(&s);
	ASSERT_NEAR(slide_offset(&s), -6.0, 1e-6);
	slide_start(&s, -7.0f + slide_offset(&s), 2);	/* 60 -> 67 while 6 semitones low */
	ASSERT_NEAR(slide_next(&s), 0.471937, 1e-5);	/* 2^(-13/12) */
	ASSERT_NEAR(slide_next(&s), 0.686977, 1e-5);	/* 2^(-6.5/12) */
	ASSERT_NEAR(slide_next(&s), 1.0, 0.0);
}

TEST(GL1_no_time_or_no_interval_does_not_glide)
{
	slide_t s;
	slide_start(&s, -12.0f, 0);
	ASSERT_NEAR(slide_next(&s), 1.0, 0.0);
	slide_start(&s, 0.0f, 10);
	ASSERT_NEAR(slide_next(&s), 1.0, 0.0);
	ASSERT_NEAR(slide_offset(&s), 0.0, 0.0);
}

TEST_MAIN()
