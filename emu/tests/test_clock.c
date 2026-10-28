/*
 * test_clock.c
 *
 * M2 (stage 1b spec 4.2, 12.1): the virtual clock. Frames convert to cycles
 * exactly; DWT->CYCCNT holds the low 32 bits of the clock; HAL_GetTick() is the
 * clock in ms; seqp_now() (seq_platform.c, unchanged) extends CYCCNT across its
 * 2^32 wrap.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include "emu_test.h"

/* floor(frames * 552,960 / 113) in 128 bits */
static uint64_t exact(uint64_t frames)
{
	return (uint64_t)((unsigned __int128)frames * 552960u / 113u);
}

TEST(M2_frames)
{
	ASSERT_EQ(emu_framesToCycles(0), 0);
	ASSERT_EQ(emu_framesToCycles(113), 552960);		/* 113 frames = 2.56 ms exactly */
	ASSERT_EQ(emu_framesToCycles(64), 313180);		/* one block: 313,180.88 cycles */
	ASSERT_EQ(emu_framesToCycles(44140), 215996941);
	for (uint64_t f = 0; f <= 1000000000u; f += 999983u)
		ASSERT_EQ(emu_framesToCycles(f), exact(f));
	ASSERT_EQ(emu_framesToCycles(1000000000u), exact(1000000000u));
	for (uint64_t f = 1; f < 2000; f++)
		ASSERT_TRUE(emu_framesToCycles(f) >= emu_framesToCycles(f - 1));
}

TEST(M2_cyccnt_and_tick)
{
	emu_config_t cfg;
	emu_testCore(&cfg);
	ASSERT_EQ(HAL_RCC_GetHCLKFreq(), 216000000);
	ASSERT_EQ(HAL_GetTick(), 0);
	HAL_Delay(1500);
	ASSERT_EQ(emu_now(), 1500ull * 216000u);
	ASSERT_EQ(HAL_GetTick(), 1500);
	ASSERT_EQ(DWT->CYCCNT, (uint32_t)emu_now());
	HAL_Delay(19000);		/* past 2^32 cycles (19.88 s) */
	ASSERT_TRUE(emu_now() > 0xFFFFFFFFull);
	ASSERT_EQ(DWT->CYCCNT, (uint32_t)emu_now());
	ASSERT_EQ(HAL_GetTick(), (uint32_t)(emu_now() / 216000u));
}

TEST(M2_seqp_now_wraps)
{
	emu_config_t cfg;
	emu_testCore(&cfg);
	HAL_Delay(1000);
	seqp_init();
	uint64_t last = seqp_now();
	ASSERT_EQ(last, emu_now());
	for (int i = 0; i < 6; i++)
	{
		HAL_Delay(9000);	/* well within the 19.9 s that seqp_now() allows between calls */
		const uint64_t now = seqp_now();
		ASSERT_TRUE(now > last);
		ASSERT_EQ(now, emu_now());
		last = now;
	}
	ASSERT_TRUE(last > 2u * 0xFFFFFFFFull);
}

TEST_MAIN()
