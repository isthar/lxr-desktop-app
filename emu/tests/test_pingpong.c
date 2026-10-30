/*
 * test_pingpong.c
 *
 * M16 (stage 1b spec 7, 12.1): the ping-pong delay's mode switch, the case in
 * which fw 1.7 wrote past FX_BUFFER (E4.8, 2bb5d80). The normal delay runs its
 * write pointer over the whole buffer (0..0x7FFF); the ping-pong delay writes its
 * left line at FX_BUFFER_SIZE/2 + write, so a switch at a write pointer of 0x4000
 * or more wrote past the buffer until E4.8 masked the pointers to one half. A
 * boot without a card, the FX bus on the delay, then twelve switches to ping-pong
 * and back: six at write pointers in [0x4000, 0x4400), whose first ping-pong write
 * lands up to 2 KB past the buffer without E4.8, and six below 0x3C00. Under ASan
 * (CI) a write past FX_BUFFER is a report; here the test checks that the scenario
 * reaches the case. The pointers of the twelve switches are printed.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdio.h>

#include "stm32f7xx_hal.h"
#include "Parameters.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/FX/FxRamBuffer.h"
#include "frontPanelParser_ui.h"
#include "emu_test.h"

static void bootNoCard(void)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	emu_testBoot(&cfg);
	emu_testRunMs(100);
}

/* a menu edit of a parameter from 128 up: frontPanel_sendData(CC_2, p - 128, v),
 * called directly at a pass boundary */
static uint8_t cc2Param, cc2Value;

static void cc2(void *arg)
{
	(void)arg;
	frontPanel_sendData(CC_2, cc2Param, cc2Value);
}

static void sendCC2(uint16_t p, uint8_t v)
{
	cc2Param = (uint8_t)(p - 128u);
	cc2Value = v;
	emu_directCall(cc2, NULL);
}

static uint64_t blocks(void)
{
	emu_counters_t c;
	emu_getCounters(&c);
	return c.blocks;
}

TEST(M16_pingpong)
{
	bootNoCard();
	sendCC2(PAR_FX_TYPE, 4);			/* delay (fxNames) */
	sendCC2(PAR_FX_DELAY_TYPE, 0);			/* normal (delayNames) */
	ASSERT_EQ(fxBus.fxType, FX_DELAY);
	ASSERT_EQ(fxBus.delay.delayType, DELAY_NORMAL);

	/* the pointer moves without input: one step per frame rendered */
	const uint32_t w0 = fxBus.delay.write;
	const uint64_t b0 = blocks();
	emu_testRunMs(10);
	const uint64_t rendered = (blocks() - b0) * EMU_BLOCK_FRAMES;
	ASSERT_TRUE(rendered > 0u);
	ASSERT_EQ((fxBus.delay.write - w0) & FX_BUFFER_SIZE, rendered & FX_BUFFER_SIZE);

	unsigned high = 0, low = 0;
	for (int round = 0; round < 12; round++)
	{
		const int wantHigh = round % 2 == 0;
		/* run one pass at a time until the write pointer is where this round wants it */
		for (int k = 0; k < 20000; k++)
		{
			const uint32_t w = fxBus.delay.write;
			if (wantHigh ? (w >= 0x4000u && w < 0x4400u) : (w < 0x3C00u))
				break;
			emu_runUntil(emu_now() + 1u);
		}
		const uint32_t w = fxBus.delay.write;
		ASSERT_TRUE(wantHigh ? (w >= 0x4000u && w < 0x4400u) : (w < 0x3C00u));
		if (wantHigh)
			high++;
		else
			low++;
		printf("M16 round %2d: switch to ping-pong at write 0x%04X\n", round + 1, (unsigned)w);
		sendCC2(PAR_FX_DELAY_TYPE, 1);		/* to ping-pong */
		ASSERT_EQ(fxBus.delay.delayType, DELAY_PINGPONG);
		emu_testRunMs(20);
		ASSERT_TRUE(fxBus.delay.write < 0x4000u);	/* ping-pong runs in one half */
		sendCC2(PAR_FX_DELAY_TYPE, 0);		/* back to normal */
		ASSERT_EQ(fxBus.delay.delayType, DELAY_NORMAL);
		emu_testRunMs(20);
	}
	ASSERT_EQ(high, 6);
	ASSERT_EQ(low, 6);
}

TEST_MAIN()
