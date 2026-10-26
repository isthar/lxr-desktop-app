/*
 * test_cpumeter.c
 *
 * Host tests of the CPU meter (Core/Src/Soundengine/cpuMeter.c, spec 9.3): the
 * block load in percent of CPUMETER_BLOCK_CYCLES, the 1 s average and peak, the
 * nesting rule for a kit autoload, the clamp and the 32-bit counter wrap.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"
#include "Soundengine/cpuMeter.h"

/* CPUMETER_BLOCK_CYCLES = 313185 = 15 x 20879, so 20879 x k cycles is exactly
 * 20 x k / 3 % of a block: 62637 cycles are 20 %, 250548 cycles are 80 % */
#define PCT20 62637u
#define PCT80 250548u

/* One audio block period starting at *t: one call of `work` cycles that renders
 * the block. Returns what cpuMeter_end() returned. */
static uint8_t block(uint32_t *t, uint32_t work)
{
	cpuMeter_begin(*t);
	const uint8_t published = cpuMeter_end(*t + work, 1);
	*t += CPUMETER_BLOCK_CYCLES;
	return published;
}

/* blocks of `work` cycles until a reading is published; returns how many ran */
static uint32_t blocksUntilReading(uint32_t *t, uint32_t work)
{
	for (uint32_t n = 1; n <= 2000; n++)
		if (block(t, work))
			return n;
	return 0;
}

TEST(cpumeter_first_reading_after_one_second)
{
	cpuMeter_reset();
	uint32_t t = 1000;
	/* the window opens at the end of the first block and closes at the first
	 * block that ends 216000000 cycles later: 216000000 / 313185 = 689.7 */
	ASSERT_EQ(blocksUntilReading(&t, PCT20), 691);

	cpuMeter_reading_t r;
	cpuMeter_get(&r);
	ASSERT_EQ(r.avgPct, 20);
	ASSERT_EQ(r.peakPct, 20);
}

TEST(cpumeter_nothing_before_the_first_reading)
{
	cpuMeter_reset();
	uint32_t t = 0;
	for (int i = 0; i < 600; i++)
		ASSERT_EQ(block(&t, PCT80), 0);

	cpuMeter_reading_t r;
	cpuMeter_get(&r);
	ASSERT_EQ(r.avgPct, 0);
	ASSERT_EQ(r.peakPct, 0);
}

TEST(cpumeter_average_and_peak)
{
	cpuMeter_reset();
	uint32_t t = 0;
	ASSERT_EQ(block(&t, PCT80), 0);		/* one heavy block, then light ones */
	ASSERT_EQ(blocksUntilReading(&t, PCT20), 691);

	cpuMeter_reading_t r;
	cpuMeter_get(&r);
	/* (250548 + 691 x 62637) x 100 / (692 x 313185) = 20.09 */
	ASSERT_EQ(r.avgPct, 20);
	ASSERT_EQ(r.peakPct, 80);
}

TEST(cpumeter_peak_is_held_for_one_window)
{
	cpuMeter_reset();
	uint32_t t = 0;
	ASSERT_EQ(block(&t, PCT80), 0);
	ASSERT_TRUE(blocksUntilReading(&t, PCT20) > 0);
	ASSERT_TRUE(blocksUntilReading(&t, PCT20) > 0);	/* the next second is light only */

	cpuMeter_reading_t r;
	cpuMeter_get(&r);
	ASSERT_EQ(r.avgPct, 20);
	ASSERT_EQ(r.peakPct, 20);
}

TEST(cpumeter_calls_without_a_block_count_for_the_next_block)
{
	cpuMeter_reset();
	uint32_t t = 0;
	uint8_t published = 0;
	/* seq_tick() runs several times per block: three calls of 20 % each, and
	 * only the third renders, so every block carries 60 % */
	for (int i = 0; i < 2000 && !published; i++)
	{
		cpuMeter_begin(t);
		ASSERT_EQ(cpuMeter_end(t + PCT20, 0), 0);
		cpuMeter_begin(t + 100000);
		ASSERT_EQ(cpuMeter_end(t + 100000 + PCT20, 0), 0);
		cpuMeter_begin(t + 200000);
		published = cpuMeter_end(t + 200000 + PCT20, 1);
		t += CPUMETER_BLOCK_CYCLES;
	}
	ASSERT_TRUE(published);

	cpuMeter_reading_t r;
	cpuMeter_get(&r);
	ASSERT_EQ(r.avgPct, 60);
	ASSERT_EQ(r.peakPct, 60);
}

TEST(cpumeter_kit_autoload_outer_call_is_not_counted)
{
	cpuMeter_reset();
	uint32_t t = 0;
	ASSERT_EQ(block(&t, PCT20), 0);

	/* A pattern change with kit autoload: seq_tick() calls soundengine_process()
	 * again from inside, which renders blocks. The nested calls count; the
	 * outer call (44 ms in all, mostly the kit load) does not. */
	cpuMeter_begin(t);
	for (int i = 0; i < 30; i++)
	{
		cpuMeter_begin(t + 1000);
		ASSERT_EQ(cpuMeter_end(t + 1000 + PCT20, 1), 0);
		t += CPUMETER_BLOCK_CYCLES;
	}
	ASSERT_EQ(cpuMeter_end(t, 1), 0);

	ASSERT_TRUE(blocksUntilReading(&t, PCT20) > 0);
	cpuMeter_reading_t r;
	cpuMeter_get(&r);
	ASSERT_EQ(r.avgPct, 20);
	ASSERT_EQ(r.peakPct, 20);
}

TEST(cpumeter_clamps_to_three_digits)
{
	cpuMeter_reset();
	uint32_t t = 0;
	/* a stall of 20 blocks in one call: 2000 % */
	cpuMeter_begin(t);
	ASSERT_EQ(cpuMeter_end(t + 20u * CPUMETER_BLOCK_CYCLES, 1), 0);
	t += 21u * CPUMETER_BLOCK_CYCLES;
	ASSERT_TRUE(blocksUntilReading(&t, PCT20) > 0);

	cpuMeter_reading_t r;
	cpuMeter_get(&r);
	ASSERT_EQ(r.peakPct, CPUMETER_MAX_PCT);
}

TEST(cpumeter_counter_wrap)
{
	cpuMeter_reset();
	uint32_t t = 0xFFFFFFFFu - 100000000u;	/* DWT->CYCCNT wraps in this window */
	ASSERT_EQ(blocksUntilReading(&t, PCT20), 691);

	cpuMeter_reading_t r;
	cpuMeter_get(&r);
	ASSERT_EQ(r.avgPct, 20);
	ASSERT_EQ(r.peakPct, 20);
}

TEST(cpumeter_end_without_begin_is_ignored)
{
	cpuMeter_reset();
	ASSERT_EQ(cpuMeter_end(5000, 1), 0);
	uint32_t t = 10000;
	ASSERT_EQ(blocksUntilReading(&t, PCT20), 691);
}

TEST(cpumeter_queue_stats)
{
	cpuMeter_reset();
	cpuMeter_setQueueStats(12, 3);

	cpuMeter_reading_t r;
	cpuMeter_get(&r);
	ASSERT_EQ(r.queueMax, 12);
	ASSERT_EQ(r.queueOverflow, 3);
}

TEST_MAIN()
