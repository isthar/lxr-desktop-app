/*
 * test_boot.c
 *
 * M6 (stage 1b spec 4.10, 12.1): the boot of the whole firmware. The splash; the
 * path without a card, with 'SD error: No Card!'; a minimal card; a card without
 * GLO.CFG, whose 'glo.cfg error' waits in HAL_Delay(1000) with 689 block requests
 * and 688 underruns. With them M3's codec start (216,000,000 cycles with a card,
 * 432,000,000 without, spec 4.2) and M5's firmware part: half 0 rendered by
 * soundengine_init(), out 1 from dma_buffer2 (spec 4.1).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "Soundengine/AudioCodecManager.h"
#include "emu_test.h"

static char rows[2][17];
static int seen;
static uint64_t when;

/* snapshot the LCD once, at the first hook at or after `when` */
static void snapAt(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	(void)why;
	if (!seen && now >= when)
	{
		emu_testLcdRow(0, rows[0]);
		emu_testLcdRow(1, rows[1]);
		seen = 1;
	}
}

static const int16_t *out1Seen, *out2Seen;
static void audio(void *ctx, uint64_t t, const int16_t *out1, const int16_t *out2)
{
	(void)ctx;
	(void)t;
	out1Seen = out1;
	out2Seen = out2;
}

TEST(M6_splash)
{
	char card[512];
	emu_testCard(card, sizeof card, 120);
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	cfg.hook = snapAt;
	when = 108000000u;	/* 0.5 s: inside the HAL_Delay(1000) before the codec start */
	emu_testBoot(&cfg);
	ASSERT_TRUE(seen);
	ASSERT_EQ(strncmp(rows[0], "LICHOMAT", 8), 0);
	ASSERT_EQ(strncmp(rows[1], "v0.2 unofficial", 15), 0);
}

TEST(M6_no_card)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	cfg.hook = snapAt;
	cfg.sinks.audio = audio;
	when = 108000000u;	/* inside the HAL_Delay(1000) after 'No Card!' (appMain.c, FR_NOT_READY) */
	emu_testBoot(&cfg);
	ASSERT_EQ(strncmp(rows[0], "SD error:", 9), 0);
	ASSERT_EQ(strncmp(rows[1], "No Card!", 8), 0);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.codecStart, 432000000u);	/* M3: two HAL_Delay(1000) without a card */
	ASSERT_EQ(c.bootEnd, 432000000u);
	emu_testRunMs(1000);
	emu_getCounters(&c);
	ASSERT_EQ(c.blocks, 689);
	ASSERT_EQ(c.underruns, 0);	/* M5: half 0 came from soundengine_init(), then every pass rendered */
	ASSERT_TRUE(c.passes > 3500);	/* a pass at every block and on the 250 us grid */
	ASSERT_TRUE(emu_testLcdHas("COA"));
	/* M5: out 1 is dma_buffer2 (DAC1), out 2 dma_buffer (DAC2) */
	ASSERT_TRUE(out1Seen == &dma_buffer2[0] || out1Seen == &dma_buffer2[128]);
	ASSERT_TRUE(out2Seen == &dma_buffer[0] || out2Seen == &dma_buffer[128]);
}

TEST(M6_card)
{
	char card[512];
	emu_testCard(card, sizeof card, 120);
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	emu_testBoot(&cfg);
	emu_counters_t c0, c;
	emu_getCounters(&c0);
	ASSERT_EQ(c0.codecStart, 216000000u);	/* M3: one HAL_Delay(1000) with a card */
	/* M3: no HAL_GetTick() call precedes the codec start, so the codec start alone
	 * does not see the rule; the project load's immediate kit load after it calls
	 * HAL_GetTick() about 250 times (spec 4.2), all free: the boot ends then too */
	ASSERT_TRUE(c0.tickCalls > 100u && c0.tickCalls <= 512u);
	ASSERT_EQ(c0.bootEnd, 216000000u);
	emu_testRunMs(1000);
	emu_getCounters(&c);
	ASSERT_EQ(c.underruns, c0.underruns);	/* none once the passes run */
	char path[1024];
	struct stat st;
	snprintf(path, sizeof path, "%s/TMP", card);
	ASSERT_EQ(stat(path, &st), 0);		/* the /TMP overlay (boot step 13) */
	snprintf(path, sizeof path, "%s/PROJ.IDX", card);
	ASSERT_EQ(stat(path, &st), 0);		/* written by the project load */
	ASSERT_EQ(st.st_size, 1);
	ASSERT_FALSE(emu_testLcdHas("error"));
}

static emu_counters_t atDelayEnd;
static int delayEndSeen;

/* the LCD inside the glo.cfg error's HAL_Delay(1000), the counters at its end */
static void gloHook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	snapAt(ctx, now, why);
	if (!delayEndSeen && now >= 432000000u)
	{
		emu_getCounters(&atDelayEnd);
		delayEndSeen = 1;
	}
}

TEST(M6_no_globals)
{
	char card[512];
	emu_testCard(card, sizeof card, 0);	/* PRJ.NFO without GLO.CFG */
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	cfg.hook = gloHook;
	when = 216000000u + 108000000u;		/* the delay runs from the codec start to 432,000,000 */
	emu_testBoot(&cfg);
	ASSERT_TRUE(strstr(rows[0], "glo.cfg") != NULL || strstr(rows[1], "glo.cfg") != NULL);
	ASSERT_TRUE(delayEndSeen);
	ASSERT_EQ(atDelayEnd.codecStart, 216000000u);
	ASSERT_EQ(atDelayEnd.blocks, 689);		/* all inside the HAL_Delay(1000): no pass, no TIM2 */
	ASSERT_EQ(atDelayEnd.underruns, 688);	/* only half 0 was rendered (spec 4.10, step 8) */
}

TEST_MAIN()
