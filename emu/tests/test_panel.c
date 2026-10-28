/*
 * test_panel.c
 *
 * M8 without the output jacks (stage 1b spec 4.6, 12.1): the panel's inputs at
 * the firmware's poll points. One button transition per scan; Gray changes 1 ms
 * apart; 'encoder +10' moves the load page's kit number by 10; a 40 ms push seen
 * in every sampling phase; 134 steps per knob turn on a normal parameter and 22
 * on a list parameter; no edit at boot with the knobs at 0 degrees; fader i
 * reaching adc_getFaderVolume(i-1).
 *
 * The knob counts are edits, the +1 and -1 calls of menu_parseKnobValue()
 * (adcPots.c:232), not changes of the parameter's value: COA starts at 31 and
 * stops at 127 after 96 edits, and the waveform list has 6 entries. The tests
 * hold the parameter in mid-range before each pass, so that its range does not
 * hide an edit.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <sys/wait.h>

#include "stm32f7xx_hal.h"
#include "menu/menu.h"
#include "IO/adcPots.h"
#include "Parameters.h"
#include "emu_test.h"

#define ENC_A (1u << 13)
#define ENC_B (1u << 14)

/* the core with the panel model, and no firmware */
static void setup(void)
{
	emu_config_t cfg;
	emu_testCore(&cfg);
	emu_panelReset(&cfg);
}

/* the firmware without a card: voice 1's oscillator page, 'COA fin wav pwm' */
static void bootNoCard(void)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	emu_testBoot(&cfg);
	emu_testRunMs(100);
}

static uint8_t button5(void)
{
	uint8_t bytes[5] = { 0 };
	HAL_SPI_Receive(NULL, bytes, 5, 100);
	return (uint8_t)((bytes[0] >> 5) & 1u);
}

TEST(M8_scan)
{
	setup();
	const uint64_t t = emu_now() + 1000u;
	emu_inButton(t, 5, 1);
	emu_inButton(t, 5, 0);
	emu_inButton(t, 5, 1);
	emu_inButton(t, 40, 1);
	emu_inButton(t, 40, 0);
	emu_inButton(t, 40, 1);
	ASSERT_EQ(button5(), 0);	/* before t nothing is released */
	ASSERT_EQ(GPIOB->IDR & (1u << 7), 0);
	HAL_Delay(1);
	/* one transition per button per scan; button 40 changes in the same scan */
	static const uint8_t want[4] = { 1, 0, 1, 1 };
	for (int i = 0; i < 4; i++)
	{
		ASSERT_EQ(button5(), want[i]);
		ASSERT_EQ((GPIOB->IDR >> 7) & 1u, want[i]);
	}
}

/* M8_gray: a SysTick body that steps the encoder, as the firmware's does, and
 * records each change of bits 13-14 as A<<1 | B */
static unsigned grayCount;
static uint8_t grayState[16];
static uint64_t grayTime[16];

static uint8_t grayNow(void)
{
	return (uint8_t)(((GPIOE->IDR & ENC_A) ? 2u : 0u) | ((GPIOE->IDR & ENC_B) ? 1u : 0u));
}

static void graySysTick(void)
{
	static uint8_t last;
	emu_panelEncoderStep();
	const uint8_t s = grayNow();
	if (s != last && grayCount < 16u)
	{
		grayState[grayCount] = s;
		grayTime[grayCount] = emu_now();
		grayCount++;
	}
	last = s;
}

TEST(M8_gray)
{
	setup();
	emu_fw.sysTick = graySysTick;
	ASSERT_EQ(grayNow(), 0);
	emu_inEncoder(0, 2);
	HAL_Delay(10);
	/* 4 changes per detent, one per SysTick, forward 00 -> 01 -> 11 -> 10 -> 00 */
	static const uint8_t forward[8] = { 1, 3, 2, 0, 1, 3, 2, 0 };
	ASSERT_EQ(grayCount, 8);
	for (unsigned i = 0; i < 8u; i++)
	{
		ASSERT_EQ(grayState[i], forward[i]);
		ASSERT_EQ(grayTime[i], (uint64_t)(i + 1u) * EMU_CYCLES_PER_MS);
	}
	emu_inEncoder(emu_now(), -1);
	HAL_Delay(10);
	static const uint8_t backward[4] = { 2, 3, 1, 0 };
	ASSERT_EQ(grayCount, 12);
	for (unsigned i = 0; i < 4u; i++)
	{
		ASSERT_EQ(grayState[8u + i], backward[i]);
		ASSERT_EQ(grayTime[8u + i], (uint64_t)(11u + i) * EMU_CYCLES_PER_MS);
	}
}

static void toLoadPage(void *arg)
{
	(void)arg;
	menu_switchPage(LOAD_PAGE);
}

/* the kit number on the load page's row 2, '[ nn]Name' */
static int kitShown(void)
{
	char r1[17];
	emu_testLcdRow(1, r1);
	int n = -1;
	if (r1[0] != '[' || sscanf(r1 + 1, "%d", &n) != 1)
		return -1;
	return n;
}

TEST(M8_fw_encoder10)
{
	bootNoCard();
	/* voice 1's oscillator page has 4 items; the load page's encoder changes the
	 * kit number (menu_handleLoadSaveMenu()), which shows 10 steps */
	emu_directCall(toLoadPage, NULL);
	emu_testRunMs(100);
	ASSERT_TRUE(emu_testLcdHas("Load: Kit"));
	const int start = kitShown();
	ASSERT_TRUE(start >= 0);
	emu_inEncoder(emu_now(), 10);	/* 40 Gray changes, queued at once */
	emu_testRunMs(100);
	ASSERT_EQ(kitShown(), start + 10);
	char after10[2][17];
	emu_testLcdRow(0, after10[0]);
	emu_testLcdRow(1, after10[1]);
	emu_inEncoder(emu_now(), -10);
	emu_testRunMs(100);
	ASSERT_EQ(kitShown(), start);
	for (int i = 0; i < 10; i++)
	{
		emu_inEncoder(emu_now(), 1);
		emu_testRunMs(20);
	}
	emu_testRunMs(100);
	ASSERT_EQ(kitShown(), start + 10);
	char after1s[2][17];
	emu_testLcdRow(0, after1s[0]);
	emu_testLcdRow(1, after1s[1]);
	ASSERT_EQ(memcmp(after10, after1s, sizeof after10), 0);
}

TEST(M8_fw_push_phases)
{
	bootNoCard();
	ASSERT_TRUE(emu_testLcdHas("COA fin wav pwm"));
	unsigned toggles = 0;
	for (unsigned k = 0; k < 10u; k++)
	{
		/* k x 0.1 ms after a SysTick: every sampling phase of encoder.c */
		const uint64_t tick = (emu_now() / EMU_CYCLES_PER_MS + 1u) * EMU_CYCLES_PER_MS;
		emu_inPush(tick + k * (EMU_CYCLES_PER_MS / 10u), 40);
		emu_testRunMs(250);
		const int edit = (k % 2u) == 0;	/* each push toggles the edit mode */
		ASSERT_EQ(emu_testLcdHas("OscilltrCoarse"), edit);
		ASSERT_EQ(emu_testLcdHas("COA fin wav pwm"), !edit);
		toggles += (unsigned)(emu_testLcdHas("OscilltrCoarse") == edit);
		emu_testRunMs(250);
	}
	ASSERT_EQ(toggles, 10);
}

/* Turn knob 360 degrees (spec 4.6: the model ramps 1 degree per pass) and count
 * the edits of param over 500 passes. Before each pass the parameter is set to
 * mid, so that each edit shows as mid +1 or mid -1: a pass makes at most one edit,
 * because the knob moves once, at the pass's start, and only the first of the
 * pass's four adc_checkPots() calls sees that. */
static void knobTurn(unsigned knob, unsigned param, uint8_t mid, unsigned *up, unsigned *down, unsigned *lastEdit)
{
	bootNoCard();
	ASSERT_TRUE(emu_testLcdHas("COA fin wav pwm"));
	*up = *down = *lastEdit = 0;
	emu_inKnob(emu_now(), knob, 360);
	for (unsigned pass = 1; pass <= 500u; pass++)
	{
		activeParameters[param] = mid;	/* at a pass boundary, as a direct call */
		emu_runUntil(emu_now() + 1u);	/* one pass */
		const int d = (int)activeParameters[param] - (int)mid;
		ASSERT_TRUE(d >= -1 && d <= 1);
		if (d)
			*lastEdit = pass;
		if (d > 0)
			(*up)++;
		else if (d < 0)
			(*down)++;
	}
}

/* Under the wiper model of spec 4.6, wiper a moves 4095/180 = 22.75 counts per
 * degree, so its change exceeds HYSTERSIS (40, adcPots.c:20) every 2 degrees: 180
 * times in a turn. calcPosition() gives the angle, truncated; shifted by 1, it is
 * unchanged at 45 of them, and at 360 degrees it wraps to 0, a jump of 179 that
 * the abs(diff) < 40 window drops (adcPots.c:230). 180 - 45 - 1 = 134. The spec's
 * M8 allows 134 +- 5; the count is exact here. */
TEST(M8_fw_knob_normal)
{
	unsigned up, down, lastEdit;
	knobTurn(1, PAR_COARSE1, 63, &up, &down, &lastEdit);
	ASSERT_EQ(up, 134);
	ASSERT_EQ(down, 0);
	/* one pass per degree, the last edit at 356 degrees (at 358 the shifted
	 * position is unchanged): a faster ramp could give the same count (2 degrees
	 * per pass also gives 134), but in 178 passes */
	ASSERT_EQ(lastEdit, 356);
}

/* A list parameter (DTYPE_MENU) shifts the position by 4: a new value every 16
 * degrees, 22 in a turn, and the wrap's jump of 22, doubled to 44, is dropped. */
TEST(M8_fw_knob_list)
{
	unsigned up, down, lastEdit;
	knobTurn(3, PAR_OSC_WAVE_DRUM1, 2, &up, &down, &lastEdit);
	ASSERT_EQ(up, 22);
	ASSERT_EQ(down, 0);
	ASSERT_EQ(lastEdit, 352);
}

/* The boot's adc_checkPots() calls compare with adc_buf_old and adc_potValues,
 * which start at 0 (spec 4.6). At 40 degrees the first position, 40 >> 1 = 20, is
 * an edit of +1; at 0 degrees it is 0, no edit. One boot per process, so the
 * child boots at 40 degrees.
 *
 * The value is read inside the boot, in the HAL_Delay(1000) after 'No Card!': the
 * boot's later preset_init(0) (appMain.c:698) loads the init kit over any edit,
 * so after the boot both read 31.
 *
 * The child's edit is a firmware finding: the boot's first adc_checkPots()
 * (appMain.c:178) runs before morphParameters is set (appMain.c:356, :697), and
 * the edit's preset_getMorphValue() (menu.c:4304) indexes the NULL pointer
 * (presetManager.c:2263), which UBSan reports. On the device it reads ITCM near
 * address 0, and a morph of 0 discards the value read. Until the firmware sets the
 * morph target first (spec 7, further sanitizer findings), the child sets it as
 * the boot does later, so that the edit itself can be seen. */
static uint8_t inBoot[2];	/* the value inside the boot, then after it */
static int inBootSeen;

static void readInBoot(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	(void)why;
	if (!inBootSeen && now >= 108000000u)	/* 0.5 s, inside the delay */
	{
		inBoot[0] = activeParameters[PAR_COARSE1];
		inBootSeen = 1;
	}
}

static void bootAt(int knob1)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	cfg.knobs[0] = knob1;
	cfg.hook = readInBoot;
	emu_testBoot(&cfg);
	inBoot[1] = activeParameters[PAR_COARSE1];
}

TEST(M8_fw_no_edit_at_boot)
{
	int fds[2];
	ASSERT_EQ(pipe(fds), 0);
	const pid_t child = fork();
	ASSERT_TRUE(child >= 0);
	if (child == 0)
	{
		close(fds[0]);
		morphParameters = mainParameters[0];	/* the workaround above */
		bootAt(40);
		_exit(inBootSeen && write(fds[1], inBoot, 2) == 2 ? 0 : 1);
	}
	close(fds[1]);
	bootAt(0);
	ASSERT_TRUE(inBootSeen);
	uint8_t childValues[2] = { 0, 0 };
	ASSERT_EQ(read(fds[0], childValues, 2), 2);
	close(fds[0]);
	int status = 0;
	ASSERT_EQ(waitpid(child, &status, 0), child);
	ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	ASSERT_EQ(childValues[0], inBoot[0] + 1u);
	ASSERT_EQ(inBoot[0], 0);	/* activeParameters is still all zero there */
	ASSERT_EQ(childValues[1], inBoot[1]);
}

TEST(M8_fw_faders)
{
	bootNoCard();
	const double full = (4095.0 / 4096.0) * (4095.0 / 4096.0);
	const double half = (2047.0 / 4096.0) * (2047.0 / 4096.0);	/* floor(0.5 x 4095) = 2047 */
	for (unsigned f = 0; f < 6u; f++)
		ASSERT_NEAR(adc_getFaderVolume((uint8_t)f), full, 1e-6);
	for (unsigned i = 1; i <= 6u; i++)
	{
		emu_inFader(emu_now(), i, 0.5);
		emu_testRunMs(50);
		/* faders 1..i moved, the others still full */
		for (unsigned f = 1; f <= 6u; f++)
			ASSERT_NEAR(adc_getFaderVolume((uint8_t)(f - 1u)), f <= i ? half : full, 1e-6);
	}
}

TEST_MAIN()
