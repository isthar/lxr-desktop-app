/*
 * lcd.h
 *
 * The LCD API of the firmware, declared fresh for the LICHOMAT emulator (stage 1b
 * spec 4.7): Core/Src/lcd.c and Core/Inc/lcd.h carry no licence, so the LCD
 * model (emu/core/emu_lcd.c) implements these functions from scratch. The
 * command bytes are those of the HD44780 and WS0010 controllers.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_LCD_H_
#define EMU_LCD_H_
#include <stdint.h>

/** the display process in TIM14 (menu_tick()) runs while this is set */
extern uint8_t lcd_process_display_interrupt;

/* controller commands */
#define LCD_CLEAR_DISPLAY 0x01
#define LCD_CURSOR_HOME 0x02
#define LCD_SET_ENTRY 0x04
#define LCD_ENTRY_INCREASE 0x02
#define LCD_SET_DISPLAY 0x08
#define LCD_DISPLAY_ON 0x04
#define LCD_CURSOR_ON 0x02
#define LCD_BLINKING_ON 0x01
#define LCD_SET_SHIFT 0x10
#define LCD_SET_FUNCTION 0x20
#define LCD_SET_CGADR 0x40
#define LCD_SET_DDADR 0x80
#define LCD_DDADR_LINE1 0x00
#define LCD_DDADR_LINE2 0x40
#define LCD_DDADR_LINE3 0x10
#define LCD_DDADR_LINE4 0x50

void lcd_init(void);
void lcd_showSplash(void);
void lcd_clear(void);
uint8_t lcd_clear_nonblock(void);
void lcd_home(void);
void lcd_setcursor(uint8_t spalte, uint8_t zeile);
uint8_t lcd_setcursor_nonBlock(uint8_t spalte, uint8_t zeile);
void lcd_data(uint8_t data);
uint8_t lcd_data_nonBlock(uint8_t data);
void lcd_string(const char *data);
void lcd_command(uint8_t data);
uint8_t lcd_command_nonblock(uint8_t data);
void lcd_turnOn(uint8_t isOn, uint8_t cursorOn);
uint8_t lcd_turnOn_nonblock(uint8_t isOn, uint8_t cursorOn);
void lcd_generatechar(uint8_t code, const uint8_t *data);
uint8_t lcd_generatechar_nonBlock(uint8_t code, const uint8_t *data);

#endif /* EMU_LCD_H_ */
