/*
 * test_hook.c
 *
 * M3 and M4 with the firmware (stage 1b spec 3.3, 4.2, 12.1): the HAL_GetTick()
 * rule in a normal pass and an immediate kit load; the tier hook during the
 * long firmware calls, menu_showSdError()'s HAL_Delay(2000) and a screensaver
 * wake, and during the boot splash; TIM2 parked by every pass, and firing every
 * 6,004 cycles inside a HAL_Delay(). Each test boots without a card (fast, no
 * files) unless it says otherwise.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "menu/menu.h"
#include "menu/screensaver.h"
#include "Preset/PresetManager.h"
#include "emu_test.h"

/* a wrapper that counts TIM2 bodies around the real one that emu_powerOn() installed,
 * and sorts the intervals between them: 6,004 cycles, or 204 after a block request
 * (lastRequest, which the hook of M3_fw_sd_error sets) */
static void (*realTim2)(void);
static uint64_t tim2Bodies, tim2First, tim2Last, tim2Regular, tim2AfterRequest, lastRequest;
static void countTim2(void)
{
	const uint64_t now = emu_now();
	if (!tim2Bodies++)
		tim2First = now;
	else if (now - tim2Last == 6004u)
		tim2Regular++;
	else if (now == lastRequest + 204u)
		tim2AfterRequest++;
	tim2Last = now;
	realTim2();
}

static void bootNoCard(emu_hook_t hook)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	cfg.hook = hook;
	emu_powerOn(&cfg);
	realTim2 = emu_fw.tim2;
	emu_fw.tim2 = countTim2;
	emu_runUntil(1u);
}

/* the last pass started at the time it ended: no time passed in it */
static int lastPassTookNoTime(void)
{
	emu_trailEntry_t e[EMU_TRAIL_LENGTH];
	const unsigned n = emu_trail(e, EMU_TRAIL_LENGTH);
	for (unsigned i = n; i-- > 0;)
		if (e[i].kind == EMU_TRAIL_PASS)
			return e[i].t == emu_now();
	return 0;
}

extern TIM_HandleTypeDef htim2;
/* USE_INTERRUPT_ONLY_DURING_SAVE_PROJ (config.h:30): the boot leaves TIM2 off; the
 * save and load paths start it, as this does */
static void startTim2(void *arg) { (void)arg; HAL_TIM_Base_Start_IT(&htim2); }

TEST(M3_fw_normal_pass)
{
	bootNoCard(NULL);
	emu_directCall(startTim2, NULL);
	emu_testRunMs(100);
	tim2Bodies = 0;	/* none fired so far either: the first pass parked it at once */
	for (int i = 0; i < 2000; i++)
	{
		emu_runUntil(emu_now() + 1u);
		ASSERT_TRUE(lastPassTookNoTime());
		emu_counters_t c;
		emu_getCounters(&c);
		ASSERT_TRUE(c.tickCalls <= 512u);
	}
	ASSERT_EQ(tim2Bodies, 0);	/* M4: every pass parks TIM2 before it fires (CNT 1950) */
	/* and not because it stopped: it is still started, with its update interrupt */
	ASSERT_TRUE(htim2.Instance->CR1 & TIM_CR1_CEN);
	ASSERT_TRUE(htim2.Instance->DIER & TIM_IT_UPDATE);
}

static void kitLoad(void *arg) { (void)arg; preset_loadDrumsetRam(0, 0, 1); }

TEST(M3_fw_kit_load)
{
	bootNoCard(NULL);
	const uint64_t before = emu_now();
	emu_directCall(kitLoad, NULL);	/* the immediate load: soundengine_process() after every parameter */
	ASSERT_EQ(emu_now(), before);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_TRUE(c.tickCalls > 100u && c.tickCalls <= 512u);	/* about 250 (spec 4.2) */
}

/* the hook injects a DIN byte at its 100th block call after the test zeroes
 * blockCalls, and reads the counters at the 300th: the input arrived while the
 * firmware call ran */
static uint64_t blockCalls, dinAt300;
static void injectExtra(uint64_t now, emu_hookReason_t why)
{
	if (why != EMU_HOOK_BLOCK)
		return;
	lastRequest = now;
	if (++blockCalls == 100u)
	{
		static const uint8_t clock = 0xF8;
		emu_inDin(now + 1000u, &clock, 1);
	}
	if (blockCalls == 300u)
	{
		emu_counters_t c;
		emu_getCounters(&c);
		dinAt300 = c.dinIn;
	}
}
static const double blockSec = 64.0 / 44140.625;	/* a fake device at the audio rate */
static void sdError(void *arg) { (void)arg; menu_showSdError(FR_DISK_ERR); }

TEST(M3_fw_sd_error)
{
	emu_testTierReset(8u, blockSec);
	bootNoCard(emu_testTierHook);
	emu_counters_t c0, c1;
	emu_getCounters(&c0);
	blockCalls = 0;
	emu_tier.extra = injectExtra;
	const double wall0 = emu_tier.wall;
	const uint64_t start = emu_now();
	emu_directCall(sdError, NULL);	/* TIM2 started, HAL_Delay(2000) (menu.c:1996, :2041) */
	emu_getCounters(&c1);
	const double blocks = (double)(c1.blocks - c0.blocks);
	ASSERT_TRUE(emu_now() - start >= 2000u * EMU_CYCLES_PER_MS);
	ASSERT_NEAR(blocks, 1379.4, 1.5);					/* 1,379 blocks inside one call (spec 3.3: about 1,378) */
	ASSERT_NEAR((double)blockCalls, blocks, 0.5);		/* the hook ran at each of them */
	ASSERT_NEAR(emu_tier.wall - wall0, 2.0, 0.02);		/* paced by the ring in real time */
	ASSERT_EQ(c1.underruns, c0.underruns);				/* TIM2 renders every block */
	ASSERT_EQ(dinAt300, c0.dinIn + 1u);					/* the hook's input arrived during the call */
	ASSERT_EQ(c1.passes, c0.passes);					/* no pass inside a direct call */
	/* M4: TIM2 fires every 6,004 cycles inside the HAL_Delay(), and 204 after each request */
	ASSERT_TRUE(tim2Bodies > 70000u);
	ASSERT_NEAR((double)(tim2Last - tim2First) / (double)(tim2Bodies - 1u), 6004.0, 300.0);
	/* exactly: each interval is 6,004 cycles, or ends 204 after a request (the mean,
	 * 313,181 / 53, would be the same for any period from 5,906 to 6,018 cycles) */
	ASSERT_EQ(tim2Regular + tim2AfterRequest, tim2Bodies - 1u);
	ASSERT_NEAR((double)tim2AfterRequest, blocks, 1.0);
	ASSERT_FALSE(emu_tier.dogFired);
}

static void nearTimeout(void *arg) { (void)arg; screensaver_timer = 2u * 60u * 1000u - 50u; }

/* The longest pass, from a hook that copies the trail: the start of the pass that
 * runs now is the trail's last EMU_TRAIL_PASS entry, which the first hook in it
 * still finds; the pass lasts at least to the last hook in it. */
static uint64_t passStart, wakeStart, wakeLength, dinAtWakeEnd;
static void wakeExtra(uint64_t now, emu_hookReason_t why)
{
	emu_trailEntry_t e[EMU_TRAIL_LENGTH];
	const unsigned n = emu_trail(e, EMU_TRAIL_LENGTH);
	for (unsigned i = n; i-- > 0;)
		if (e[i].kind == EMU_TRAIL_PASS)
		{
			passStart = e[i].t;
			break;
		}
	injectExtra(now, why);
	if (emu_inPass() && now - passStart > wakeLength)
	{
		wakeStart = passStart;
		wakeLength = now - passStart;
		emu_counters_t c;
		emu_getCounters(&c);
		dinAtWakeEnd = c.dinIn;		/* at the last hook in the longest pass */
	}
}

TEST(M3_fw_screensaver)
{
	emu_testTierReset(8u, blockSec);
	bootNoCard(emu_testTierHook);
	emu_directCall(nearTimeout, NULL);	/* the test hook of spec 5.3: near its 2-minute timeout */
	emu_testRunMs(200);
	ASSERT_EQ(screensaver_Active, 1);	/* it started */
	emu_counters_t c0, c1;
	emu_getCounters(&c0);
	const uint64_t timeHooks0 = emu_tier.timeHooks;
	const double wall0 = emu_tier.wall;
	blockCalls = 0;
	emu_tier.extra = wakeExtra;
	const uint64_t pressAt = emu_now();
	emu_inButton(pressAt, 1u, 1);		/* voice 1 wakes it */
	emu_inButton(pressAt + 50u * EMU_CYCLES_PER_MS, 1u, 0);
	emu_testRunMs(1000);
	ASSERT_EQ(screensaver_Active, 0);
	/* The wake loop (screensaver.c:75) runs inside one pass until screensaver_process()
	 * changes state, at the end of its current SCREENSAVER_PROCESS_SPEED wait (500 ms).
	 * The screensaver started 50 ms after the boot, at the pass after the 50th
	 * SysTick; two passes later its CELL_OFF_WAIT began, still in that ms. So the
	 * wake runs from the press, 200 ms after the boot, to 550 ms after it: 350 ms.
	 * Its length is the rest of a 500 ms wait and depends on the phase of the
	 * press; spec 3.3's 361-465 ms were other presses. */
	ASSERT_TRUE(wakeStart >= pressAt && wakeStart - pressAt < EMU_CYCLES_PER_MS);	/* seen within 1 ms */
	ASSERT_EQ(wakeStart + wakeLength, c0.bootEnd + 550u * EMU_CYCLES_PER_MS);
	/* the hook ran once per ms throughout the wake, and nowhere else */
	ASSERT_NEAR((double)(emu_tier.timeHooks - timeHooks0), (double)wakeLength / EMU_CYCLES_PER_MS, 1.0);
	ASSERT_TRUE(emu_tier.wall - wall0 > 0.99);	/* the blocks were paced throughout */
	emu_getCounters(&c1);
	ASSERT_EQ(c1.underruns, c0.underruns);
	ASSERT_EQ(dinAt300, c0.dinIn + 1u);
	/* the byte injected at the 100th block, about 145 ms into the wake, arrived in it */
	ASSERT_EQ(dinAtWakeEnd, c0.dinIn + 1u);
	const uint64_t passes = c1.passes;
	emu_testRunMs(100);
	emu_getCounters(&c1);
	ASSERT_TRUE(c1.passes - passes > 300u);	/* the passes go on */
}

TEST(M3_fw_splash_paced)
{
	char card[512];
	emu_testCard(card, sizeof card, 120);
	emu_testTierReset(8u, blockSec);
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	cfg.hook = emu_testTierHook;
	emu_testBoot(&cfg);
	/* the wall clock followed the virtual clock to the codec start: the splash showed 1 s */
	ASSERT_NEAR(emu_tier.wall, 1.0, 0.002);
	ASSERT_FALSE(emu_tier.dogFired);
}

TEST_MAIN()
