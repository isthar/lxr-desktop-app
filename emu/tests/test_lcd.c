/*
 * test_lcd.c
 *
 * M7 (stage 1b spec 4.7, 12.1): the LCD model, an HD44780 or WS0010 behind the
 * lcd.h API: commands, the entry direction, the clear, CGRAM, the end of a row,
 * and the codes 0xC5 and 0xC6, which reach the snapshot unchanged (the tiers
 * draw them as arrows, Q18). The LEDs: the latch of the 5 SPI bytes, LED 40 on
 * GPIOB->ODR bit 8, and a 50 ms voice pulse kept in the next snapshot. These
 * run on the core with the LCD and panel models alone; M7_fw_repaint boots the
 * firmware and times a full repaint through menu_tick().
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "lcd.h"
#include "menu/menu.h"
#include "emu_test.h"

/* the core with the LCD and panel models, and no firmware */
static void setup(void)
{
	emu_config_t cfg;
	emu_testCore(&cfg);
	emu_lcdReset(&cfg);
	emu_panelReset(&cfg);
}

static emu_lcdSnapshot_t snap(void)
{
	emu_lcdSnapshot_t s;
	memset(&s, 0, sizeof s);
	emu_lcdSnapshot(&s);
	return s;
}

/* 1 if row r holds text and spaces after it */
static int rowIs(const emu_lcdSnapshot_t *s, int r, const char *text)
{
	uint8_t want[16];
	memset(want, ' ', sizeof want);
	memcpy(want, text, strlen(text));
	return memcmp(s->chars[r], want, sizeof want) == 0;
}

TEST(M7_commands)
{
	setup();
	lcd_init();
	emu_lcdSnapshot_t s = snap();
	ASSERT_EQ(s.displayOn, 1);
	ASSERT_EQ(s.cursorOn, 0);
	/* lcd_init() ends with the counter in CGRAM after character 0, as the
	 * firmware's lcd.c leaves it: a byte now is row 0 of character 1. The
	 * firmware sends a home or a cursor first. */
	lcd_data(0x15);
	s = snap();
	ASSERT_EQ(s.cgram[8], 0x15);
	ASSERT_TRUE(rowIs(&s, 0, ""));
	lcd_home();
	lcd_string("AB");
	lcd_setcursor(3, 2);			/* column 3 of row 2: DDRAM 0x43 */
	lcd_data('C');
	lcd_command(LCD_SET_ENTRY);		/* I/D clear: decrement */
	lcd_data('D');
	s = snap();
	ASSERT_TRUE(rowIs(&s, 0, "AB"));
	/* 'C' at 0x43 moved the counter to 0x44, where 'D' went; the decrement moved
	 * it back to 0x43 */
	ASSERT_TRUE(rowIs(&s, 1, "   CD"));
	ASSERT_EQ(s.cursorRow, 1);
	ASSERT_EQ(s.cursorCol, 3);
	lcd_home();
	lcd_turnOn(1, 1);
	s = snap();
	ASSERT_EQ(s.cursorRow, 0);
	ASSERT_EQ(s.cursorCol, 0);
	ASSERT_EQ(s.displayOn, 1);
	ASSERT_EQ(s.cursorOn, 1);
	ASSERT_EQ(s.blinkOn, 0);
	ASSERT_TRUE(rowIs(&s, 0, "AB"));	/* the home moves the counter only */
	/* the blink bit, and the display off */
	lcd_command(LCD_SET_DISPLAY | LCD_DISPLAY_ON | LCD_BLINKING_ON);
	s = snap();
	ASSERT_EQ(s.displayOn, 1);
	ASSERT_EQ(s.cursorOn, 0);
	ASSERT_EQ(s.blinkOn, 1);
	lcd_turnOn(0, 0);
	s = snap();
	ASSERT_EQ(s.displayOn, 0);
	ASSERT_EQ(s.blinkOn, 0);
}

TEST(M7_clear)
{
	setup();
	lcd_init();
	lcd_setcursor(0, 1);
	lcd_string("0123456789ABCDEF");
	lcd_setcursor(0, 2);
	lcd_string("FEDCBA9876543210");
	emu_lcdSnapshot_t s = snap();
	ASSERT_TRUE(rowIs(&s, 0, "0123456789ABCDEF"));
	ASSERT_TRUE(rowIs(&s, 1, "FEDCBA9876543210"));
	lcd_command(LCD_SET_ENTRY);			/* decrement */
	lcd_command(LCD_SET_CGADR | 8u);	/* and the counter in CGRAM */
	lcd_clear();
	s = snap();
	ASSERT_TRUE(rowIs(&s, 0, ""));
	ASSERT_TRUE(rowIs(&s, 1, ""));
	ASSERT_EQ(s.cursorRow, 0);
	ASSERT_EQ(s.cursorCol, 0);
	/* the clear sets the counter to DDRAM 0 and the entry to increment again */
	lcd_data('a');
	lcd_data('b');
	s = snap();
	ASSERT_TRUE(rowIs(&s, 0, "ab"));
	ASSERT_EQ(s.cursorCol, 2);
	for (unsigned i = 0; i < sizeof s.cgram; i++)
		ASSERT_EQ(s.cgram[i], 0);
}

TEST(M7_cgram)
{
	setup();
	/* CGRAM holds anything at power-on: lcd_init() programs character 0 blank,
	 * because a '\0' can reach the display */
	static const uint8_t junk[8] = { 0x1F, 0x11, 0x1F, 0x11, 0x1F, 0x11, 0x1F, 0x11 };
	lcd_generatechar(0, junk);
	ASSERT_EQ(snap().cgram[0], 0x1F);
	lcd_init();
	static const uint8_t rows[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	lcd_generatechar(2, rows);
	lcd_setcursor(0, 1);	/* back to DDRAM */
	lcd_data(2);
	emu_lcdSnapshot_t s = snap();
	for (unsigned i = 0; i < sizeof s.cgram; i++)
		ASSERT_EQ(s.cgram[i], i >= 16u && i < 24u ? i - 15u : 0u);
	ASSERT_EQ(s.chars[0][0], 2);
	ASSERT_EQ(s.chars[0][1], ' ');
}

TEST(M7_row_wrap)
{
	setup();
	lcd_init();
	lcd_setcursor(15, 1);
	lcd_string("XY");
	emu_lcdSnapshot_t s = snap();
	ASSERT_TRUE(rowIs(&s, 0, "               X"));
	/* 'Y' went to DDRAM 0x10, off screen: row 2 starts at 0x40 */
	ASSERT_TRUE(rowIs(&s, 1, ""));
	ASSERT_EQ(s.cursorRow, 0);
	ASSERT_EQ(s.cursorCol, 0x11);
	/* row 1 ends at 0x27, from where the counter goes on at 0x40 */
	for (int i = 0x11; i <= 0x27; i++)
		lcd_data('-');
	s = snap();
	ASSERT_TRUE(rowIs(&s, 1, ""));
	lcd_data('Z');
	s = snap();
	ASSERT_TRUE(rowIs(&s, 1, "Z"));
	ASSERT_EQ(s.cursorRow, 1);
	ASSERT_EQ(s.cursorCol, 1);
}

TEST(M7_arrows)
{
	setup();
	lcd_init();
	lcd_home();
	lcd_data(0xC5);
	lcd_data(0xC6);
	emu_lcdSnapshot_t s = snap();
	ASSERT_EQ(s.chars[0][0], 0xC5);
	ASSERT_EQ(s.chars[0][1], 0xC6);
}

TEST(M7_led_latch)
{
	setup();
	uint8_t bytes[5] = { 0x01, 0, 0, 0, 0x80 };	/* LED n is bit n%8 of byte n/8 */
	HAL_SPI_Transmit(NULL, bytes, 5, 100);
	GPIOB->ODR = 1u << 8;						/* LED 40, LED_43_Pin */
	ASSERT_EQ(emu_ledSnapshot(), (1ull << 0) | (1ull << 39) | (1ull << 40));
}

TEST(M7_led_pulse)
{
	setup();
	emu_ledSnapshot();	/* clears */
	uint8_t on[5] = { 0x02, 0, 0, 0, 0 }, off[5] = { 0 };
	HAL_SPI_Transmit(NULL, on, 5, 100);		/* voice LED 1 */
	HAL_Delay(50);
	HAL_SPI_Transmit(NULL, off, 5, 100);
	HAL_Delay(150);
	ASSERT_EQ(emu_now(), 200u * EMU_CYCLES_PER_MS);
	/* on at some time since the previous snapshot, then not any more */
	ASSERT_EQ(emu_ledSnapshot(), 1ull << 1);
	ASSERT_EQ(emu_ledSnapshot(), 0);
}

/* M7_fw_repaint: a wrapper around the TIM14 body that emu_powerOn() installed. It
 * counts the bodies after the repaint call and notes the last one that changed
 * the LCD. */
static void (*realTim14)(void);
static int watching;
static unsigned tim14Bodies, lastChangeBody;
static emu_lcdSnapshot_t bodySeen;

static void watchTim14(void)
{
	realTim14();
	if (!watching)
		return;
	tim14Bodies++;
	const emu_lcdSnapshot_t s = snap();
	if (memcmp(&s, &bodySeen, sizeof s) != 0)
	{
		bodySeen = s;
		lastChangeBody = tim14Bodies;
	}
}

/* the clear makes the repaint rebuild both rows from spaces */
static void repaint(void *arg)
{
	(void)arg;
	lcd_clear();
	menu_repaintAll();
}

TEST(M7_fw_repaint)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	emu_powerOn(&cfg);
	realTim14 = emu_fw.tim14;
	emu_fw.tim14 = watchTim14;
	emu_runUntil(1u);
	emu_testRunMs(100);
	const emu_lcdSnapshot_t before = snap();
	ASSERT_FALSE(rowIs(&before, 0, "") && rowIs(&before, 1, ""));	/* a screen to rebuild */
	const uint64_t call = emu_now();
	emu_directCall(repaint, NULL);
	emu_lcdSnapshot_t prev = snap();
	ASSERT_TRUE(rowIs(&prev, 0, "") && rowIs(&prev, 1, ""));
	bodySeen = prev;
	watching = 1;
	/* a snapshot after each pass for 10 ms; stop is the pass that first showed the
	 * final state */
	uint64_t stop = call;
	while (emu_now() < call + 10u * EMU_CYCLES_PER_MS)
	{
		emu_runUntil(emu_now() + 1u);
		const emu_lcdSnapshot_t s = snap();
		if (memcmp(&s, &prev, sizeof s) != 0)
		{
			prev = s;
			stop = emu_now();
		}
	}
	/* 100 menu_tick() calls (sendDisplayBuffer(), menu.c:867): 2 to start, 3 per
	 * cell for 32 cells, 2 for the cursor; 4 per TIM14 body, so the 25th body
	 * after the call makes the last change (spec 4.7). The range alone would let
	 * 24 or 26 bodies through. */
	ASSERT_EQ(lastChangeBody, 25);
	ASSERT_NEAR((double)(stop - call) / EMU_CYCLES_PER_MS, 6.25, 0.25);
	ASSERT_EQ(memcmp(prev.chars, before.chars, sizeof prev.chars), 0);
	ASSERT_EQ(memcmp(&prev, &before, sizeof prev), 0);	/* the cursor and flags too */
}

TEST_MAIN()
