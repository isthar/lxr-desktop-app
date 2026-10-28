/*
 * test_codec.c
 *
 * M5 and M4 (stage 1b spec 4.1, 4.2, 12.1): the codec model with buffers of
 * the test. Block request k comes 64·k frames after the codec start; it counts
 * an underrun when no block was rendered, passes the half that starts playing
 * to the sink (out 1 from dma_buffer2, out 2 from dma_buffer), frees the other
 * half, and sets TIM2's counter to 1450, so TIM2 fires 204 cycles later.
 * M3's tier hook with the fake tier of emu_test.h: wall-clock pacing before
 * the codec start, ring pacing after it. test_boot.c checks that
 * soundengine_init() renders half 0.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "emu_test.h"

void codec_initCsCodec(uint32_t Addr1, uint32_t Size1, uint32_t Addr2, uint32_t Size2);
extern TIM_HandleTypeDef htim2;

static volatile uint8_t valid;
static int16_t dacOut2[256], dacOut1[256];	/* dma_buffer, dma_buffer2 */

static struct { uint64_t t; int16_t out1, out2; } seen[16];
static unsigned seenCount;

static void audio(void *ctx, uint64_t t, const int16_t *out1, const int16_t *out2)
{
	(void)ctx;
	if (seenCount < 16)
		seen[seenCount] = (typeof(seen[0])){ t, out1[0], out2[0] };
	seenCount++;
}

static void setup(emu_hook_t hook)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.sinks.audio = audio;
	cfg.hook = hook;
	emu_corePowerOn(&cfg);
	emu_fw.sampleValid = &valid;
	emu_fw.dac2Buffer = dacOut2;
	emu_fw.dac1Buffer = dacOut1;
	emu_codecReset(&cfg);
	/* mark the halves: out 1 half h holds 100 + h, out 2 half h 200 + h */
	for (int h = 0; h < 2; h++)
	{
		dacOut1[h * 128] = (int16_t)(100 + h);
		dacOut2[h * 128] = (int16_t)(200 + h);
	}
	seenCount = 0;
}

/* a render that takes the free half and marks it valid, as
 * soundengine_calcNextSampleBlock() does */
static void render(void)
{
	if (valid != 0xFFu)
		valid = 0xFFu;
}

TEST(M5_requests)
{
	setup(NULL);
	HAL_Delay(10);
	ASSERT_EQ(seenCount, 0);			/* no request before the codec start */
	codec_initCsCodec(0, 128, 0, 128);	/* at 2,160,000 */
	valid = 0xFFu;						/* soundengine_init() rendered half 0 */
	emu_threadAdvance(emu_framesToCycles(64 * 4));
	ASSERT_EQ(seenCount, 4);
	for (unsigned k = 1; k <= 4; k++)
		ASSERT_EQ(seen[k - 1].t, 2160000u + emu_framesToCycles(64u * k));
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.codecStart, 2160000);
	ASSERT_EQ(c.blocks, 4);
	ASSERT_EQ(c.underruns, 3);			/* nobody rendered after the first request */
}

TEST(M5_halves)
{
	setup(NULL);
	codec_initCsCodec(0, 128, 0, 128);
	valid = 0xFFu;
	for (int k = 1; k <= 6; k++)
	{
		emu_threadAdvance(emu_framesToCycles(64u * (uint64_t)k) - emu_now());
		ASSERT_EQ(valid, k % 2 == 1 ? 0 : 1);	/* the other half is free */
		render();
	}
	/* request k plays half k & 1: halves 1, 0, 1, 0, ...; out 1 is dma_buffer2 */
	for (unsigned k = 1; k <= 6; k++)
	{
		ASSERT_EQ(seen[k - 1].out1, 100 + (k & 1u));
		ASSERT_EQ(seen[k - 1].out2, 200 + (k & 1u));
	}
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.underruns, 0);
}

static unsigned tim2Runs;
static uint64_t tim2Time;

static void tim2(void)
{
	tim2Runs++;
	tim2Time = emu_now();
	__HAL_TIM_SetCounter(&htim2, 1950);	/* soundengine_process() parks it */
}

TEST(M4_tim2_after_request)
{
	setup(NULL);
	emu_fw.tim2 = tim2;
	codec_initCsCodec(0, 128, 0, 128);
	valid = 0xFFu;
	HAL_TIM_Base_Start_IT(&htim2);
	__HAL_TIM_SetCounter(&htim2, 1950);
	emu_threadAdvance(emu_framesToCycles(64));
	ASSERT_EQ(tim2Runs, 0);
	emu_threadAdvance(204u);
	ASSERT_EQ(tim2Runs, 1);
	ASSERT_EQ(tim2Time, emu_framesToCycles(64) + 204u);	/* 51 counts after CNT 1450 */
}

static unsigned blockHooks;

static void hook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	(void)now;
	if (why == EMU_HOOK_BLOCK)
		blockHooks++;
}

TEST(M5_hook_per_block)
{
	setup(hook);
	blockHooks = 0;
	codec_initCsCodec(0, 128, 0, 128);
	HAL_Delay(1000);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.blocks, 689);		/* 1 s holds 689.7 blocks */
	ASSERT_EQ(blockHooks, 689);
}

/* M3, the tier hook: the clock before the codec start follows the wall clock; after
 * it, a full ring paces the blocks; the waits never count for the watchdog */
TEST(M3_paced_by_ring)
{
	emu_testTierReset(8u, 0.020);	/* a slow device: one block per 20 ms */
	setup(emu_testTierHook);
	HAL_Delay(1000);				/* before the codec start: sleeps to the wall clock */
	ASSERT_NEAR(emu_tier.wall, 1.0, 1e-9);
	codec_initCsCodec(0, 128, 0, 128);
	valid = 0xFFu;
	HAL_Delay(1000);				/* 689 blocks; the ring holds 8 */
	ASSERT_EQ(emu_tier.blockHooks, 689);
	ASSERT_NEAR(emu_tier.wall, 1.0 + (689 - 8) * 0.020, 1e-6);	/* 13.62 s of wall time in waits */
	ASSERT_FALSE(emu_tier.dogFired);
	/* one wait longer than the watchdog's 10 s: the device stalls for 12 s on the
	 * next block. The bracket keeps it from counting; without it, the poll at the
	 * wait's end would see 12 s without progress. */
	emu_tier.blockSec = 12.0;
	emu_tier.maxWait = 0.0;
	emu_threadAdvance(emu_framesToCycles(64u));	/* at least one block request */
	ASSERT_TRUE(emu_tier.maxWait >= 12.0);
	ASSERT_FALSE(emu_tier.dogFired);
	ASSERT_EQ(emu_watchdogPoll(&emu_tier.dog, emu_tier.wall + 1.0), 0);		/* sees the last progress */
	ASSERT_EQ(emu_watchdogPoll(&emu_tier.dog, emu_tier.wall + 10.0), 0);	/* 9 s without progress */
	ASSERT_EQ(emu_watchdogPoll(&emu_tier.dog, emu_tier.wall + 11.5), 1);	/* 10.5 s outside a wait: a hang */
}

TEST_MAIN()
