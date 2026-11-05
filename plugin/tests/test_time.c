/*
 * test_time.c
 *
 * P5: host frames, cycles and the latency (plan E9.2 Task 5).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdlib.h>
#include <unistd.h>

#include "plug_time.h"
#include "seqc_test.h"

TEST(P5_time_native)
{
	ASSERT_TRUE(plug_isNative(44140.625));
	ASSERT_FALSE(plug_isNative(44100.0));
	ASSERT_FALSE(plug_isNative(44140.0));
	ASSERT_FALSE(plug_isNative(44140.5));	/* exactly, not nearly */
	/* emu_framesToCycles(): floor(frames x 552,960 / 113) */
	ASSERT_EQ(plug_framesToCycles(0, PLUG_ENGINE_RATE), 0);
	ASSERT_EQ(plug_framesToCycles(1, PLUG_ENGINE_RATE), 4893);
	ASSERT_EQ(plug_framesToCycles(64, PLUG_ENGINE_RATE), 313180);	/* 313,180.9 */
	ASSERT_EQ(plug_framesToCycles(113, PLUG_ENGINE_RATE), 552960);
	ASSERT_EQ(plug_framesToCycles(353125, PLUG_ENGINE_RATE), 8u * 216000000u);
	for (uint64_t f = 0; f < 100000; f += 7)
		ASSERT_EQ(plug_framesToCycles(f, PLUG_ENGINE_RATE), f * 552960u / 113u);
}

TEST(P5_time_integer)
{
	ASSERT_EQ(plug_framesToCycles(1, 48000.0), 4500);
	ASSERT_EQ(plug_framesToCycles(48000, 48000.0), 216000000u);
	ASSERT_EQ(plug_framesToCycles(1, 44100.0), 4897);	/* 4,897.96 */
	ASSERT_EQ(plug_framesToCycles(44100, 44100.0), 216000000u);
	ASSERT_EQ(plug_framesToCycles(10000000000ull, 96000.0), 22500000000000ull);
	ASSERT_EQ(plug_framesToCycles(2, 44100.5), 9795);	/* not an integer rate: 9,795.9 */
}

TEST(P5_time_covering)
{
	const double rates[] = { PLUG_ENGINE_RATE, 44100.0, 48000.0, 96000.0, 44100.5 };
	for (unsigned i = 0; i < 5; i++)
		for (uint64_t c = 0; c < 3000000; c += 997)
		{
			const uint64_t m = plug_framesCovering(c, rates[i]);
			ASSERT_TRUE(plug_framesToCycles(m, rates[i]) >= c);
			ASSERT_TRUE(m == 0 || plug_framesToCycles(m - 1, rates[i]) < c);
		}
	ASSERT_EQ(plug_framesCovering(0, 48000.0), 0);
	ASSERT_EQ(plug_framesCovering(1, 48000.0), 1);
	ASSERT_EQ(plug_framesCovering(4500, 48000.0), 1);
	ASSERT_EQ(plug_framesCovering(4501, 48000.0), 2);
}

TEST(P5_time_latency)
{
	ASSERT_EQ(plug_soundDelay(PLUG_ENGINE_RATE), 128);
	ASSERT_EQ(plug_readOffset(PLUG_ENGINE_RATE, 128, 128), 257);
	ASSERT_EQ(plug_latency(PLUG_ENGINE_RATE, 128, 128), 385);
	/* round(176 x rate / 44,140.625) */
	ASSERT_EQ(plug_soundDelay(44100.0), 176);	/* 175.84 */
	ASSERT_EQ(plug_soundDelay(48000.0), 191);	/* 191.39 */
	ASSERT_EQ(plug_soundDelay(88200.0), 352);	/* 351.68 */
	ASSERT_EQ(plug_soundDelay(96000.0), 383);	/* 382.78 */
	ASSERT_EQ(plug_readOffset(48000.0, 512, 128), 643);	/* the lag: ceil(1.087) + 1 */
	ASSERT_EQ(plug_readOffset(192000.0, 512, 128), 646);	/* ceil(4.35) + 1 */
	ASSERT_EQ(plug_latency(44100.0, 128, 128), 434);	/* 9.84 ms */
	ASSERT_EQ(plug_latency(48000.0, 256, 128), 578);
}

TEST(P5_time_margin)
{
	unsetenv("LICHOMAT_PLUGIN_MARGIN");
	ASSERT_EQ(plug_marginFromEnv(), 128);
	setenv("LICHOMAT_PLUGIN_MARGIN", "64", 1);
	ASSERT_EQ(plug_marginFromEnv(), 64);
	setenv("LICHOMAT_PLUGIN_MARGIN", "4096", 1);
	ASSERT_EQ(plug_marginFromEnv(), 4096);
	setenv("LICHOMAT_PLUGIN_MARGIN", "8", 1);
	ASSERT_EQ(plug_marginFromEnv(), 128);
	setenv("LICHOMAT_PLUGIN_MARGIN", "16", 1);	/* the bounds: 16-4,096 */
	ASSERT_EQ(plug_marginFromEnv(), 16);
	setenv("LICHOMAT_PLUGIN_MARGIN", "15", 1);
	ASSERT_EQ(plug_marginFromEnv(), 128);
	setenv("LICHOMAT_PLUGIN_MARGIN", "4097", 1);
	ASSERT_EQ(plug_marginFromEnv(), 128);
	setenv("LICHOMAT_PLUGIN_MARGIN", "12x", 1);
	ASSERT_EQ(plug_marginFromEnv(), 128);
	unsetenv("LICHOMAT_PLUGIN_MARGIN");
}

TEST(P5_time_wide)
{
	/* products beyond 64 bits: the 128-bit paths of plug_framesToCycles() and plug_framesCovering()
	 * (frames x 216,000,000 past 2^64 from 8.5e10 frames; cycles x 113 past it from 1.6e17). A
	 * 64-bit product makes plug_framesCovering() step for ages: the alarm fails the test. */
	alarm(20);
	ASSERT_EQ(plug_framesToCycles(100000000000ull, 96000.0), 225000000000000ull);
	ASSERT_EQ(plug_framesToCycles(100000000000ull, 44100.0), 489795918367346ull);
	ASSERT_EQ(plug_framesCovering(22500000000000000ull, 96000.0), 10000000000000ull);
	ASSERT_EQ(plug_framesToCycles(1000000000000000ull, PLUG_ENGINE_RATE), 4893451327433628318ull);
	ASSERT_EQ(plug_framesCovering(4893451327433628318ull, PLUG_ENGINE_RATE), 1000000000000000ull);
	alarm(0);
}

TEST_MAIN()
