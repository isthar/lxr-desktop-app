/*
 * panel_font.h
 *
 * The LCD's glyphs (plan E24): a 5x7 dot table written for this plan, the two arrows that the song
 * page uses and a hollow box. Plain C, so that a test can check them without a window.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PANEL_FONT_H_
#define PANEL_FONT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 0x20-0x7E: five column bytes per glyph, bit 0 the top row */
extern const uint8_t panel_font[95][5];
extern const uint8_t panel_arrowUp[5], panel_arrowDown[5], panel_box[5];

/** The 8 rows of the character c as the LCD shows it (spec 4.7): codes 0-7 from the CGRAM bytes of the snapshot, printable
 * ASCII from the table, 0xC5 and 0xC6 as arrows, anything else as a hollow box. Each row has 5 bits, bit 4 the left
 * column. Row 7 is the cursor's row, free in every table glyph. */
void panel_glyph(uint8_t c, const uint8_t cgram[64], uint8_t rows[8]);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_FONT_H_ */
