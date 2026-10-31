/*
 * panel_layout.h
 *
 * The tables that the panel is laid out from: the 41 buttons' names (spec 4.6's numbers) and their groups in the places
 * of the owner's layout (E33), the labels of the faders and jacks, and the LED of a button. Plain C, so that a test can
 * check them without a window. The names are plain function names; the firmware's own numbering decides which button
 * is which.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PANEL_LAYOUT_H_
#define PANEL_LAYOUT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_BUTTONS 41u			/**< buttons 0-40; LED n belongs to button n */
#define PANEL_LED_MASK 0x1FFFFFFFFFFull	/**< the 41 bits of the snapshot's leds */
#define PANEL_KNOBS 4u
#define PANEL_FADERS 6u
#define PANEL_LCD_COLS 16u			/**< knob i sits under the columns 4(i-1) to 4i-1 */
#define PANEL_BUTTON_SHIFT 32u
#define PANEL_BUTTON_START 33u		/**< START/STOP ("play"), which the space bar presses */

/** the label of button n; spec 4.6's numbering, the function names of the firmware's pages and modes */
extern const char *const panel_names[PANEL_BUTTONS];
/** the label of fader i (0-5) */
extern const char *const panel_faderNames[PANEL_FADERS];
/** under the step buttons 1-16: what SHIFT and the step do together (buttonHandler.h, SeqButtonFunctions) */
extern const char *const panel_shiftNames[16];
/** the output-jack switches, in the order of the jack strip, and the EMU_JACK_* bit of each. The main output is out 1,
 * the pair that every voice is routed to by default (mixer_audioRouting is 0, DAC1: L1 and R1) */
extern const char *const panel_jackNames[4];
extern const uint8_t panel_jackBits[4];

#define PANEL_GROUP_MAX 16u
typedef struct panel_group
{
	const char *title;
	uint8_t count, perRow;
	uint8_t button[PANEL_GROUP_MAX];	/**< in the order they are drawn, left to right */
} panel_group_t;
/** the arrows and the voices under the faders, then the three framed groups, then the row of steps (E33) */
enum { PANEL_GROUP_BAR_VOICES, PANEL_GROUP_TRANSPORT, PANEL_GROUP_MODES, PANEL_GROUP_PAGES, PANEL_GROUP_STEPS, PANEL_GROUPS };
extern const panel_group_t panel_groups[PANEL_GROUPS];

/** 1 if the snapshot's LED n is lit */
static inline int panel_ledOn(uint64_t leds, unsigned n)
{
	return n < PANEL_BUTTONS && ((leds >> n) & 1u) != 0u;
}

/** A fader is dragged relative to where it is: the mouse moving dy points down (screen y grows downward) over a track of
 * travel points moves the level by -dy / travel, held to 0-1. A press moves nothing, so a click on the track does not make
 * the level jump to the click */
static inline float panel_faderDrag(float level, float dy, float travel)
{
	if (!(travel > 0.0f))
		return level;
	const float v = level - dy / travel;
	return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

#ifdef __cplusplus
}
#endif

#endif /* PANEL_LAYOUT_H_ */
