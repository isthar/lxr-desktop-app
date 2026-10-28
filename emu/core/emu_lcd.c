/*
 * emu_lcd.c
 *
 * The LCD model of the LICHOMAT emulator (stage 1b spec 4.7): the lcd.h API of
 * the firmware on a model of an HD44780 or WS0010 controller: DDRAM with row 1
 * at 0x00 and row 2 at 0x40, 64 bytes of CGRAM, the address counter
 * in DDRAM or CGRAM mode, the entry direction, the display, cursor and blink
 * bits, the font table and the power bit. Every call completes at once, and the
 * non-blocking calls return 1.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <string.h>

#include "lcd.h"
#include "emu_internal.h"

uint8_t lcd_process_display_interrupt;

static struct
{
	uint8_t ddram[128];		/* by address; the 2x16 window is 0x00-0x0F and 0x40-0x4F */
	uint8_t cgram[64];
	uint8_t address;		/* the address counter */
	uint8_t inCgram;		/* 1: the counter addresses CGRAM */
	int8_t step;			/* entry direction: +1 or -1 */
	uint8_t display, cursor, blink;
	uint8_t fontTable;		/* WS0010 FT1:FT0 of the function set */
	uint8_t power;			/* WS0010 PWR */
} emu_lcd;

/* the address counter after a data byte: in 2-line mode DDRAM row 1 is 0x00-0x27
 * and row 2 is 0x40-0x67, and the counter runs from one row into the other */
static void emu_lcdAdvance(void)
{
	if (emu_lcd.inCgram)
		emu_lcd.address = (uint8_t)((emu_lcd.address + emu_lcd.step) & 0x3Fu);
	else if (emu_lcd.step > 0)
		emu_lcd.address = emu_lcd.address == 0x27u ? 0x40u
			: emu_lcd.address == 0x67u ? 0x00u : (uint8_t)((emu_lcd.address + 1u) & 0x7Fu);
	else
		emu_lcd.address = emu_lcd.address == 0x40u ? 0x27u
			: emu_lcd.address == 0x00u ? 0x67u : (uint8_t)((emu_lcd.address - 1u) & 0x7Fu);
}

void lcd_command(uint8_t data)
{
	if (data & 0x80u)
	{
		emu_lcd.address = data & 0x7Fu;
		emu_lcd.inCgram = 0;
	}
	else if (data & 0x40u)
	{
		emu_lcd.address = data & 0x3Fu;
		emu_lcd.inCgram = 1;
	}
	else if (data & 0x20u)
		emu_lcd.fontTable = data & 0x03u;
	else if (data & 0x10u)
	{
		if ((data & 0x03u) == 0x03u)
			emu_lcd.power = (data >> 2) & 1u;	/* WS0010 mode and power */
		else if (!(data & 0x08u))
		{
			/* cursor shift; a display shift is not modelled (the firmware sends none) */
			const int8_t saved = emu_lcd.step;
			emu_lcd.step = (data & 0x04u) ? 1 : -1;
			emu_lcdAdvance();
			emu_lcd.step = saved;
		}
	}
	else if (data & 0x08u)
	{
		emu_lcd.display = (data >> 2) & 1u;
		emu_lcd.cursor = (data >> 1) & 1u;
		emu_lcd.blink = data & 1u;
	}
	else if (data & 0x04u)
		emu_lcd.step = (data & 0x02u) ? 1 : -1;
	else if (data & 0x02u)
	{
		emu_lcd.address = 0;
		emu_lcd.inCgram = 0;
	}
	else if (data & 0x01u)
	{
		memset(emu_lcd.ddram, ' ', sizeof emu_lcd.ddram);
		emu_lcd.address = 0;
		emu_lcd.inCgram = 0;
		emu_lcd.step = 1;
	}
}

uint8_t lcd_command_nonblock(uint8_t data)
{
	lcd_command(data);
	return 1;
}

void lcd_data(uint8_t data)
{
	if (emu_lcd.inCgram)
		emu_lcd.cgram[emu_lcd.address & 0x3Fu] = data;
	else
		emu_lcd.ddram[emu_lcd.address & 0x7Fu] = data;
	emu_lcdAdvance();
}

uint8_t lcd_data_nonBlock(uint8_t data)
{
	lcd_data(data);
	return 1;
}

void lcd_init(void)
{
	lcd_command(LCD_SET_SHIFT | 0x03u);			/* power off */
	lcd_command(LCD_SET_SHIFT | 0x07u);			/* power on */
	lcd_command(LCD_SET_FUNCTION | 0x08u | 0x03u);	/* 2 lines, font table 3 */
	lcd_command(LCD_SET_DISPLAY);
	lcd_clear();
	lcd_command(LCD_SET_ENTRY | LCD_ENTRY_INCREASE);
	lcd_command(LCD_SET_DISPLAY | LCD_DISPLAY_ON);
	lcd_clear();
	/* user character 0 blank: a '\0' can reach the display */
	lcd_command(LCD_SET_CGADR);
	for (int i = 0; i < 8; i++)
		lcd_data(0);
}

/* the device shows nothing here either */
void lcd_showSplash(void)
{
}

void lcd_clear(void)
{
	lcd_command(LCD_CLEAR_DISPLAY);
}

uint8_t lcd_clear_nonblock(void)
{
	lcd_clear();
	return 1;
}

void lcd_home(void)
{
	lcd_command(LCD_CURSOR_HOME);
}

void lcd_setcursor(uint8_t spalte, uint8_t zeile)
{
	static const uint8_t rows[4] = { LCD_DDADR_LINE1, LCD_DDADR_LINE2, LCD_DDADR_LINE3, LCD_DDADR_LINE4 };
	if (zeile < 1u || zeile > 4u)
		return;		/* no such row: nothing changes */
	lcd_command((uint8_t)(LCD_SET_DDADR + rows[zeile - 1u] + spalte));
}

uint8_t lcd_setcursor_nonBlock(uint8_t spalte, uint8_t zeile)
{
	lcd_setcursor(spalte, zeile);
	return 1;
}

void lcd_string(const char *data)
{
	while (*data)
		lcd_data((uint8_t)*data++);
}

void lcd_turnOn(uint8_t isOn, uint8_t cursorOn)
{
	lcd_command((uint8_t)(LCD_SET_DISPLAY | (isOn ? LCD_DISPLAY_ON : 0u) | (cursorOn ? LCD_CURSOR_ON : 0u)));
}

uint8_t lcd_turnOn_nonblock(uint8_t isOn, uint8_t cursorOn)
{
	lcd_turnOn(isOn, cursorOn);
	return 1;
}

void lcd_generatechar(uint8_t code, const uint8_t *data)
{
	lcd_command((uint8_t)(LCD_SET_CGADR | ((code & 7u) << 3)));
	for (int i = 0; i < 8; i++)
		lcd_data(data[i]);
}

uint8_t lcd_generatechar_nonBlock(uint8_t code, const uint8_t *data)
{
	lcd_generatechar(code, data);
	return 1;
}

void emu_lcdSnapshot(emu_lcdSnapshot_t *s)
{
	memcpy(s->chars[0], &emu_lcd.ddram[0x00], 16);
	memcpy(s->chars[1], &emu_lcd.ddram[0x40], 16);
	memcpy(s->cgram, emu_lcd.cgram, sizeof s->cgram);
	s->displayOn = emu_lcd.display;
	s->cursorOn = emu_lcd.cursor;
	s->blinkOn = emu_lcd.blink;
	s->cursorRow = (emu_lcd.address & 0x40u) ? 1u : 0u;
	s->cursorCol = (uint8_t)(emu_lcd.address & 0x3Fu);
}

void emu_lcdReset(const emu_config_t *cfg)
{
	(void)cfg;
	memset(&emu_lcd, 0, sizeof emu_lcd);
	memset(emu_lcd.ddram, ' ', sizeof emu_lcd.ddram);
	emu_lcd.step = 1;
	lcd_process_display_interrupt = 0;
}
