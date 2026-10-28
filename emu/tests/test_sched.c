/*
 * test_sched.c
 *
 * M3 and M4 (stage 1b spec 4.2, 12.1) on the core alone, with fake bodies: the
 * periods and priorities of the sources, the order at equal times, PRIMASK,
 * time passing only in thread context, the HAL_GetTick() rule, the pass rule,
 * the tier hook, the watchdog, the NVIC model, the trail, and TIM2's counter.
 * Three tests pin fixes against the draft: a body that waits behind PRIMASK
 * while time passes, the grid pass after a direct call, and emu_runUntil() from
 * a body. test_boot.c and test_hook.c check the same rules with the firmware.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <unistd.h>

#include "stm32f7xx_hal.h"
#include "Soundengine/globals.h"
#include "emu_test.h"

extern TIM_HandleTypeDef htim2, htim14;

/* a log of the fake bodies: what ran and when */
static char lg[64][16];
static uint64_t lgTime[64];
static unsigned lgCount;

static void note(const char *what)
{
	if (lgCount < 64)
	{
		snprintf(lg[lgCount], sizeof lg[0], "%s", what);
		lgTime[lgCount] = emu_now();
	}
	lgCount++;
}

static void fakeSysTick(void) { note("systick"); }
static void fakeTim14(void) { note("tim14"); }
static void fakeTim2(void) { note("tim2"); }
static void fakeExti4(void) { note("exti4"); EXTI->PR &= ~(1u << 4); }

static void clockInput(const emu_input_t *in)
{
	(void)in;
	emu_extiRaise(4u);
}

static void setup(void)
{
	emu_config_t cfg;
	emu_testCore(&cfg);
	emu_fw.sysTick = fakeSysTick;
	emu_fw.tim14 = fakeTim14;
	emu_fw.tim2 = fakeTim2;
	emu_fw.exti4 = fakeExti4;
	lgCount = 0;
}

TEST(M3_periods)
{
	setup();
	HAL_Delay(2);
	HAL_TIM_Base_Start_IT(&htim14);	/* at 432,000 */
	HAL_Delay(1);
	ASSERT_EQ(lgCount, 3 + 3);		/* SysTick at 1, 2 and 3 ms; TIM14 3 times in 1 ms */
	ASSERT_EQ(strcmp(lg[0], "systick"), 0);
	ASSERT_EQ(lgTime[0], 216000);
	ASSERT_EQ(lgTime[1], 432000);
	ASSERT_EQ(lgTime[2], 432000 + 54004);
	ASSERT_EQ(lgTime[3], 432000 + 2 * 54004);
	ASSERT_EQ(lgTime[4], 432000 + 3 * 54004);
	ASSERT_EQ(strcmp(lg[5], "systick"), 0);
	ASSERT_EQ(lgTime[5], 648000);
	ASSERT_EQ(systick_ticks, 3);
}

/* one-shot fakes of USART3 and the block request, for the order test */
static void fakeUsart3(void) { note("usart3"); }
static void fakeBlock(void) { note("block"); }

static void usart3Once(void)
{
	emu_src[EMU_SRC_USART3].pending++;
	emu_src[EMU_SRC_USART3].due = EMU_NEVER;
}

static void blockOnce(void)
{
	emu_src[EMU_SRC_BLOCK].pending++;
	emu_src[EMU_SRC_BLOCK].due = EMU_NEVER;
}

TEST(M3_order_at_equal_times)
{
	setup();
	emu_inputHandler(EMU_IN_CLOCK, clockInput);
	HAL_NVIC_SetPriority(EXTI4_IRQn, 3, 0);
	HAL_NVIC_EnableIRQ(EXTI4_IRQn);
	HAL_Delay(1);					/* SysTick at 216,000 */
	emu_threadAdvance(161996u);		/* to 377,996 */
	HAL_TIM_Base_Start_IT(&htim14);	/* due at 432,000 */
	emu_threadAdvance(48000u);		/* to 425,996 */
	HAL_TIM_Base_Start_IT(&htim2);	/* due at 425,996 + 6,004 = 432,000 */
	emu_inputPush(432000u, EMU_IN_CLOCK, 0, 0, 0.0);
	emu_src[EMU_SRC_BLOCK] = (emu_source_t){ 432000u, blockOnce, fakeBlock, 0 };
	emu_src[EMU_SRC_USART3] = (emu_source_t){ 432000u, usart3Once, fakeUsart3, 0 };
	lgCount = 0;
	HAL_Delay(1);
	/* at 432,000: SysTick and TIM14 (priority 0, in the table's order), USART3 and
	 * EXTI4 (3, in the table's order), TIM2 (4), the block request (5) */
	static const char *const want[] = { "systick", "tim14", "usart3", "exti4", "tim2", "block" };
	ASSERT_TRUE(lgCount >= 6);
	for (unsigned i = 0; i < 6; i++)
	{
		ASSERT_EQ(strcmp(lg[i], want[i]), 0);
		ASSERT_EQ(lgTime[i], 432000);
	}
}

TEST(M3_primask_defers)
{
	setup();
	__disable_irq();
	HAL_Delay(2);
	ASSERT_EQ(lgCount, 0);			/* masked: nothing ran */
	ASSERT_EQ(emu_now(), 432000);	/* but time passed */
	__enable_irq();
	ASSERT_EQ(lgCount, 2);			/* both SysTicks run at the release */
	ASSERT_EQ(lgTime[1], 432000);
}

/* a TIM2 body that releases PRIMASK with SysTick, EXTI4 and a block pending */
static void tim2Releases(void)
{
	note("tim2 begin");
	const uint32_t primask = __get_PRIMASK();
	__disable_irq();
	emu_src[EMU_SRC_SYSTICK].pending++;
	emu_src[EMU_SRC_EXTI4].pending++;
	emu_src[EMU_SRC_BLOCK].pending++;
	__set_PRIMASK(primask);	/* the release runs only higher priorities */
	note("tim2 end");
}

TEST(M3_primask_in_body)
{
	setup();
	emu_fw.tim2 = tim2Releases;
	emu_src[EMU_SRC_BLOCK].body = fakeBlock;
	HAL_TIM_Base_Start_IT(&htim2);
	emu_threadAdvance(6004u);
	ASSERT_EQ(lgCount, 5);
	ASSERT_EQ(strcmp(lg[0], "tim2 begin"), 0);
	ASSERT_EQ(strcmp(lg[1], "systick"), 0);
	ASSERT_EQ(strcmp(lg[2], "exti4"), 0);
	ASSERT_EQ(strcmp(lg[3], "tim2 end"), 0);
	ASSERT_EQ(strcmp(lg[4], "block"), 0);	/* priority 5 waits for the body's end */
}

/* inside a body time stands still */
static void sysTickWaits(void)
{
	const uint64_t t = emu_now();
	HAL_Delay(5);
	for (int i = 0; i < 2000; i++)
		HAL_GetTick();
	if (emu_now() != t)
		note("time moved");
	else
		note("still");
}

TEST(M3_time_only_in_thread)
{
	setup();
	emu_fw.sysTick = sysTickWaits;
	HAL_Delay(1);
	ASSERT_EQ(lgCount, 1);
	ASSERT_EQ(strcmp(lg[0], "still"), 0);
	ASSERT_EQ(emu_now(), 216000);
}

static void spin(void *arg)
{
	for (int i = 0; i < *(int *)arg; i++)
		HAL_GetTick();
}

TEST(M3_tick_rule)
{
	setup();
	int calls = 512;
	emu_directCall(spin, &calls);
	ASSERT_EQ(emu_now(), 0);		/* the first 512 calls of a segment are free */
	calls = 1000;
	emu_directCall(spin, &calls);	/* a new segment: 488 calls of 1 us */
	ASSERT_EQ(emu_now(), 488u * 216u);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.tickCalls, 1000);
}

/* the pass rule with a fake boot, fake passes and fake block requests */
static uint64_t passTimes[64];
static unsigned passCount;
static int passDelayMs;

static void fakePass(void)
{
	if (passCount < 64)
		passTimes[passCount] = emu_now();
	passCount++;
	if (passDelayMs)
		HAL_Delay((uint32_t)passDelayMs);
}

static void blockFire(void)
{
	emu_src[EMU_SRC_BLOCK].pending++;
	emu_src[EMU_SRC_BLOCK].due += 100000u;
}

static void blockBody(void)
{
	emu_noteBlock();
}

TEST(M3_pass_rule)
{
	setup();
	emu_fw.pass = fakePass;
	passCount = 0;
	passDelayMs = 0;
	emu_src[EMU_SRC_BLOCK] = (emu_source_t){ 100000u, blockFire, blockBody, 0 };
	emu_runUntil(250000u);
	/* boot at 0; passes at 0 (the first), the grid 54,000 and 108,000... and the
	 * block requests at 100,000 and 200,000 */
	const uint64_t want[] = { 0, 54000, 100000, 108000, 162000, 200000, 216000, 270000 };
	ASSERT_EQ(passCount, 8);
	for (unsigned i = 0; i < 8; i++)
		ASSERT_EQ(passTimes[i], want[i]);
	ASSERT_EQ(emu_now(), 270000);	/* the first pass boundary at or after 250,000 */
}

TEST(M3_pass_after_slow_pass)
{
	setup();
	emu_fw.pass = fakePass;
	passCount = 0;
	passDelayMs = 1;	/* each pass takes 1 ms; block requests inside it start no pass */
	emu_src[EMU_SRC_BLOCK] = (emu_source_t){ 100000u, blockFire, blockBody, 0 };
	emu_runUntil(500000u);
	/* the first pass ends at 216,000: the next is the first multiple of 54,000
	 * after it; that one ends at 486,000, and the block request at 500,000 comes
	 * before the grid's 540,000 */
	ASSERT_EQ(passCount, 3);
	ASSERT_EQ(passTimes[0], 0);
	ASSERT_EQ(passTimes[1], 270000);
	ASSERT_EQ(passTimes[2], 500000);
	ASSERT_EQ(emu_now(), 716000);
}

/* the tier hook */
static uint64_t hookTimes[4096];
static unsigned hookCount, hookBlocks;
static uint64_t delivered;

static void hook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	if (hookCount < 4096)
		hookTimes[hookCount] = now;
	hookCount++;
	if (why == EMU_HOOK_BLOCK)
		hookBlocks++;
	if (hookCount == 10)
		emu_inputPush(now + 1000u, EMU_IN_CLOCK, 0, 0, 0.0);	/* an input from the hook */
}

static void clockDelivered(const emu_input_t *in)
{
	(void)in;
	delivered = emu_now();
}

TEST(M3_hook)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.hook = hook;
	emu_corePowerOn(&cfg);
	emu_inputHandler(EMU_IN_CLOCK, clockDelivered);
	emu_src[EMU_SRC_BLOCK] = (emu_source_t){ 313180u, blockFire, blockBody, 0 };
	hookCount = hookBlocks = 0;
	delivered = 0;
	HAL_Delay(50);
	ASSERT_TRUE(hookCount >= 50);
	for (unsigned i = 1; i < hookCount && i < 4096; i++)
		ASSERT_TRUE(hookTimes[i] - hookTimes[i - 1] <= 216000u);	/* at least once per ms */
	ASSERT_EQ(hookBlocks, (50u * 216000u - 313180u) / 100000u + 1u);	/* at every block request */
	ASSERT_EQ(delivered, hookTimes[9] + 1000u);		/* the hook's input at its time */
}

TEST(M3_watchdog)
{
	emu_config_t cfg;
	emu_testCore(&cfg);
	emu_watchdog_t w = { 0, 0.0, 0 };
	ASSERT_EQ(emu_watchdogPoll(&w, 100.0), 0);
	HAL_Delay(1);
	ASSERT_EQ(emu_watchdogPoll(&w, 105.0), 0);		/* progress */
	ASSERT_EQ(emu_watchdogPoll(&w, 114.0), 0);		/* 9 s without progress */
	ASSERT_EQ(emu_watchdogPoll(&w, 115.5), 1);		/* 10.5 s: a hang */
	emu_waitBegin();								/* the hook waits for the audio device */
	ASSERT_EQ(emu_watchdogPoll(&w, 130.0), 0);
	ASSERT_EQ(emu_watchdogPoll(&w, 145.0), 0);		/* a 15 s wait is not a hang */
	emu_waitEnd();
	ASSERT_EQ(emu_watchdogPoll(&w, 150.0), 0);
	ASSERT_EQ(emu_watchdogPoll(&w, 160.5), 1);
}

TEST(M3_nvic)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.fault = emu_testOnFault;
	emu_corePowerOn(&cfg);
	emu_fw.exti4 = fakeExti4;
	emu_inputHandler(EMU_IN_CLOCK, clockInput);
	lgCount = 0;
	emu_testFaults = 0;
	HAL_NVIC_SetPriority(EXTI4_IRQn, 3, 0);
	HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3, 0);
	ASSERT_EQ(emu_testFaults, 0);
	HAL_NVIC_SetPriority(EXTI4_IRQn, 2, 0);		/* not the model's priority */
	ASSERT_EQ(emu_testFaults, 1);
	ASSERT_TRUE(strstr(emu_testFault, "priority 2.0") != NULL);
	/* an edge before the line is enabled only sets the pending bit */
	emu_inputPush(1000u, EMU_IN_CLOCK, 0, 0, 0.0);
	emu_inputPush(2000u, EMU_IN_CLOCK, 0, 0, 0.0);
	HAL_Delay(1);
	ASSERT_EQ(EXTI->PR & (1u << 4), 1u << 4);
	ASSERT_EQ(lgCount, 0);
	HAL_NVIC_EnableIRQ(EXTI4_IRQn);				/* the body runs once, now */
	ASSERT_EQ(lgCount, 1);
	ASSERT_EQ(lgTime[0], 216000);
}

TEST(M3_trail)
{
	setup();
	HAL_Delay(3);
	emu_trailEntry_t e[8];
	ASSERT_EQ(emu_trail(e, 8), 3);
	for (unsigned i = 0; i < 3; i++)
	{
		ASSERT_EQ(e[i].kind, EMU_TRAIL_BODY);
		ASSERT_EQ(e[i].arg, EMU_SRC_SYSTICK);
		ASSERT_EQ(e[i].t, (i + 1u) * 216000u);
	}
	int fds[2];
	ASSERT_EQ(pipe(fds), 0);
	emu_trailWrite(fds[1], 2);
	close(fds[1]);
	char text[128] = { 0 };
	ASSERT_TRUE(read(fds[0], text, sizeof text - 1) > 0);
	close(fds[0]);
	ASSERT_EQ(strcmp(text, "432000 body 0\n648000 body 0\n"), 0);
}

/* a fire that leaves its due time where it was */
static void stuckFire(void)
{
	emu_src[EMU_SRC_BLOCK].pending++;
}

/* Fix 1: time passes in a PRIMASK section with a USB transfer waiting, as in
 * usb_startTransfer() (usb_manager.c:116-140). The draft rescheduled the second
 * transfer at its past time while the first one's body waited: the scheduler
 * looped (no loop, no fault). Then the guard: a source whose fire leaves its
 * due time behind is a fault, not a loop. */
TEST(M3_spin_under_primask)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.fault = emu_testOnFault;
	emu_corePowerOn(&cfg);
	emu_usbReset(&cfg);
	emu_testFaults = 0;
	static const uint8_t noteOn[4] = { 0x09, 0x90, 0x3C, 0x64 };
	ASSERT_EQ(emu_inUsb(1000u, noteOn, 4), 0);
	ASSERT_EQ(emu_inUsb(1000u, noteOn, 4), 0);
	__disable_irq();
	for (int i = 0; i < 2000; i++)		/* 1,488 spin calls of 1 us each */
		HAL_GetTick();
	ASSERT_EQ(emu_now(), 1488u * 216u);
	__enable_irq();						/* both transfers' bodies run now */
	ASSERT_EQ(emu_testFaults, 0);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.usbIn, 2);
	const uint64_t before = emu_now();
	emu_src[EMU_SRC_BLOCK] = (emu_source_t){ before + 1000u, stuckFire, NULL, 0 };
	HAL_Delay(1);
	ASSERT_EQ(emu_testFaults, 1);
	ASSERT_TRUE(strstr(emu_testFault, "source 8 did not move its due time") != NULL);
	ASSERT_EQ(emu_src[EMU_SRC_BLOCK].due, EMU_NEVER);
	ASSERT_EQ(emu_now(), before + 216000u);
}

static void delay3ms(void *arg) { (void)arg; HAL_Delay(3); }

/* Fix 2: after a direct call that took 3 ms, the overdue grid pass runs at once,
 * and the grid goes on from it */
TEST(M3_direct_call_grid)
{
	setup();
	emu_fw.pass = fakePass;
	passCount = 0;
	passDelayMs = 0;
	emu_runUntil(1u);					/* boot, the first pass at 0, the grid pass at 54,000 */
	ASSERT_EQ(passCount, 2);
	emu_directCall(delay3ms, NULL);		/* now 702,000; the grid's next pass was due at 108,000 */
	emu_runUntil(emu_now() + 1u);
	ASSERT_EQ(passTimes[2], 702000);	/* the draft turned the clock back to 108,000 */
	ASSERT_EQ(passTimes[3], 756000);	/* 702,000 is before t: the next grid pass ends the call */
	ASSERT_EQ(passCount, 4);
	ASSERT_EQ(emu_now(), 756000);
}

static void spinWait(void *arg)
{
	unsigned *it = arg;
	const uint32_t start = HAL_GetTick();	/* call 1 of the segment */
	while (HAL_GetTick() - start < 2u)		/* two calls per iteration, as screensaver.c:383 */
	{
		(void)HAL_GetTick();
		(*it)++;
	}
}

/* M3: a HAL_GetTick() wait ends: 512 free calls, then 432 cycles per iteration */
TEST(M3_spin_wait)
{
	setup();
	unsigned it = 0;
	emu_directCall(spinWait, &it);
	ASSERT_EQ(emu_now(), 432000);	/* call 2,512 is the first to read tick 2 */
	ASSERT_EQ(it, 1255);
}

static void reenter(void) { emu_runUntil(emu_now() + 100u); }
static void nothing(void *arg) { (void)arg; }

static void hookCallsDirect(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	(void)now;
	(void)why;
	emu_directCall(nothing, NULL);
}

/* Fix 3: emu_runUntil() and emu_directCall() refuse calls from a body and from
 * the tier hook (spec 3.3) */
TEST(M3_no_run_from_a_body)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.fault = emu_testOnFault;
	emu_corePowerOn(&cfg);
	emu_fw.sysTick = reenter;
	emu_testFaults = 0;
	HAL_Delay(1);
	ASSERT_EQ(emu_testFaults, 1);
	ASSERT_TRUE(strstr(emu_testFault, "emu_runUntil/emu_directCall") != NULL);
	ASSERT_FALSE(emu_booted());		/* the draft booted and ran passes inside the body */
	ASSERT_EQ(emu_now(), 216000);
	emu_fw.sysTick = NULL;
	emu_cfg.hook = hookCallsDirect;
	HAL_Delay(1);					/* the hook at 432,000 */
	ASSERT_EQ(emu_testFaults, 2);
}

/* M4: TIM2's counter */
TEST(M4_tim2_counter)
{
	setup();
	HAL_TIM_Base_Start_IT(&htim2);
	HAL_Delay(1);					/* overflows every 6,004 cycles inside HAL_Delay() */
	ASSERT_EQ(lgCount, 35 + 1);		/* 216,000 / 6,004 = 35.98, and SysTick */
	ASSERT_EQ(lgTime[0], 6004);
	ASSERT_EQ(lgTime[34], 35u * 6004u);
	__HAL_TIM_SetCounter(&htim2, 1450);	/* fires 51 counts later */
	lgCount = 0;
	emu_threadAdvance(204u);
	ASSERT_EQ(lgCount, 1);
	ASSERT_EQ(lgTime[0], 216000 + 204);
	__HAL_TIM_SetCounter(&htim2, 1950);	/* above ARR: parked until the 2^32 wrap */
	lgCount = 0;
	HAL_Delay(100);
	ASSERT_EQ(lgCount, 100);			/* SysTick only */
	ASSERT_EQ(strcmp(lg[0], "systick"), 0);
}

TEST(M4_tim2_stop)
{
	setup();
	HAL_TIM_Base_Start_IT(&htim2);
	emu_threadAdvance(3000u);			/* half a period */
	HAL_TIM_Base_Stop(&htim2);			/* CEN clear, UIE stays set */
	ASSERT_EQ(TIM2->DIER & TIM_IT_UPDATE, TIM_IT_UPDATE);
	ASSERT_EQ(TIM2->CR1 & TIM_CR1_CEN, 0);
	emu_threadAdvance(100000u);
	ASSERT_EQ(lgCount, 0);
	HAL_TIM_Base_Start_IT(&htim2);		/* counts on from 750 */
	emu_threadAdvance(3004u);
	ASSERT_EQ(lgCount, 1);
	ASSERT_EQ(lgTime[0], 103000u + 3004u);
	HAL_TIM_Base_Stop_IT(&htim2);		/* both clear */
	ASSERT_EQ(TIM2->DIER & TIM_IT_UPDATE, 0);
	__HAL_TIM_DISABLE_IT(&htim2, TIM_IT_UPDATE);
	HAL_Delay(1);
	ASSERT_EQ(strcmp(lg[lgCount - 1], "systick"), 0);
}

TEST_MAIN()
