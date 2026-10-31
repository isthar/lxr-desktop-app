/*
 * test_app_font.c
 *
 * R4 (stage 1b spec 4.7, 6.6, plan E24): the LCD's glyphs, and the tables that the
 * panel is laid out from. Both are plain C, so the headless tests can check what the
 * window shows without opening one.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <string.h>

#include "panel_font.h"
#include "panel_layout.h"
#include "seqc_test.h"

TEST(R4_panel_font)
{
	static const uint8_t cgram[64] = { [24] = 0x1F, [31] = 0x11 };
	uint8_t r[8];
	panel_glyph('A', cgram, r);
	const uint8_t a[8] = { 0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00 };
	ASSERT_EQ(memcmp(r, a, 8), 0);
	panel_glyph(3u, cgram, r);		/* codes 0-7 from CGRAM (spec 4.7) */
	ASSERT_EQ(r[0], 0x1F);
	ASSERT_EQ(r[7], 0x11);
	panel_glyph(0xC5u, cgram, r);	/* the song page's up arrow */
	ASSERT_EQ(r[0], 0x04);
	ASSERT_EQ(r[1], 0x0E);
	ASSERT_EQ(r[2], 0x15);
	panel_glyph(0x80u, cgram, r);	/* any other code: a hollow box */
	ASSERT_EQ(r[0], 0x1F);
	ASSERT_EQ(r[3], 0x11);
	ASSERT_EQ(r[6], 0x1F);
	for (unsigned c = 0x20u; c <= 0x7Eu; c++)
	{
		panel_glyph((uint8_t)c, cgram, r);
		ASSERT_EQ(r[7], 0);	/* the bottom row stays free for the cursor */
	}
}

/* The panel's groups hold each of the 41 buttons once, under the name that spec 4.6's numbering and the firmware's
 * functions give it, and each button has the LED of the same number (spec 4.7). E33: the owner's layout. */
TEST(R4_panel_layout)
{
	static const char *const voices[7] = { "drum 1", "drum 2", "drum 3", "snare", "clp/cym", "cl hh", "op hh" };
	static const char *const pages[8] = { "osc", "aeg", "mod", "fm", "click", "filter", "lfo", "mix" };	/* select 1-8 */
	/* buttonHandler.h's SeqButtonFunctions, in the order of the steps 1 to 16 (buttonHandler_seqButtonPressed(n)) */
	static const char *const shifted[16] = { "drum 1", "drum 2", "drum 3", "snare", "clp/cym", "cl hh", "op hh", "kit", "morph",
		"song", "project", "config", "reload", "last step", "fx", "pattern" };
	const char *want[PANEL_BUTTONS];
	char step[PANEL_BUTTONS][16];
	memset(want, 0, sizeof want);
	want[0] = "bar+";
	want[40] = "bar-";
	for (unsigned i = 0; i < 7; i++)
		want[1 + i] = voices[i];
	for (unsigned i = 0; i < 4; i++)
	{
		want[16 + i] = pages[i];		/* select 1-4 */
		want[8 + i] = pages[4 + i];	/* select 5-8 */
		snprintf(step[12 + i], sizeof step[0], "step %u", 16 - i);
		snprintf(step[20 + i], sizeof step[0], "step %u", 12 - i);
		snprintf(step[28 + i], sizeof step[0], "step %u", 8 - i);
		snprintf(step[36 + i], sizeof step[0], "step %u", 4 - i);
		want[12 + i] = step[12 + i];
		want[20 + i] = step[20 + i];
		want[28 + i] = step[28 + i];
		want[36 + i] = step[36 + i];
	}
	want[24] = "voice";	/* MODE1-4 */
	want[25] = "perf";
	want[26] = "load";
	want[27] = "save";
	want[32] = "shift";
	want[33] = "play";	/* START/STOP */
	want[34] = "rec";
	want[35] = "copy/clr";
	for (unsigned n = 0; n < PANEL_BUTTONS; n++)
		ASSERT_EQ(strcmp(panel_names[n], want[n]), 0);

	unsigned seen[PANEL_BUTTONS];
	memset(seen, 0, sizeof seen);
	unsigned total = 0;
	for (unsigned g = 0; g < PANEL_GROUPS; g++)
	{
		const panel_group_t *grp = &panel_groups[g];
		ASSERT_TRUE(grp->title != NULL && grp->title[0] != '\0');
		ASSERT_TRUE(grp->count > 0u && grp->count <= PANEL_GROUP_MAX);
		for (unsigned i = 0; i < grp->count; i++)
		{
			ASSERT_TRUE(grp->button[i] < PANEL_BUTTONS);
			seen[grp->button[i]]++;
			total++;
		}
	}
	ASSERT_EQ(total, PANEL_BUTTONS);
	for (unsigned n = 0; n < PANEL_BUTTONS; n++)
		ASSERT_EQ(seen[n], 1u);

	/* 9 + 4 + 4 + 8 + 16: the bar buttons and the voices, the transport, the modes, the pages, the steps */
	ASSERT_EQ(PANEL_GROUPS, 5u);
	ASSERT_EQ(panel_groups[PANEL_GROUP_BAR_VOICES].count, 9u);
	ASSERT_EQ(panel_groups[PANEL_GROUP_TRANSPORT].count, 4u);
	ASSERT_EQ(panel_groups[PANEL_GROUP_MODES].count, 4u);
	ASSERT_EQ(panel_groups[PANEL_GROUP_PAGES].count, 8u);
	ASSERT_EQ(panel_groups[PANEL_GROUP_STEPS].count, 16u);
	const panel_group_t *bv = &panel_groups[PANEL_GROUP_BAR_VOICES];	/* the arrows, then the voices, left to right */
	ASSERT_EQ(bv->button[0], 40u);
	ASSERT_EQ(bv->button[1], 0u);
	for (unsigned i = 0; i < 7; i++)
		ASSERT_EQ(strcmp(panel_names[bv->button[2 + i]], voices[i]), 0);
	static const char *const transport[4] = { "shift", "play", "rec", "copy/clr" };
	static const char *const modes[4] = { "voice", "perf", "load", "save" };
	for (unsigned i = 0; i < 4; i++)
	{
		ASSERT_EQ(strcmp(panel_names[panel_groups[PANEL_GROUP_TRANSPORT].button[i]], transport[i]), 0);
		ASSERT_EQ(strcmp(panel_names[panel_groups[PANEL_GROUP_MODES].button[i]], modes[i]), 0);
	}
	for (unsigned i = 0; i < 8; i++)
		ASSERT_EQ(strcmp(panel_names[panel_groups[PANEL_GROUP_PAGES].button[i]], pages[i]), 0);
	/* the steps run 1 to 16, and the labels under them are the SHIFT functions of the firmware */
	const panel_group_t *steps = &panel_groups[PANEL_GROUP_STEPS];
	for (unsigned k = 0; k < 16u; k++)
	{
		char name[16];
		snprintf(name, sizeof name, "step %u", k + 1u);
		ASSERT_EQ(strcmp(panel_names[steps->button[k]], name), 0);
		ASSERT_EQ(strcmp(panel_shiftNames[k], shifted[k]), 0);
	}

	/* 41 LEDs: LED n is bit n of the snapshot's mask, LED 40 included */
	uint64_t all = 0;
	for (unsigned n = 0; n < PANEL_BUTTONS; n++)
	{
		ASSERT_TRUE(panel_ledOn(1ull << n, n));
		ASSERT_FALSE(panel_ledOn(~(1ull << n), n));
		all |= 1ull << n;
	}
	ASSERT_EQ(all, PANEL_LED_MASK);
	ASSERT_EQ(PANEL_LED_MASK, 0x1FFFFFFFFFFull);

	/* knobs under the 4 columns of the LCD; the faders the firmware reads, by their voices */
	ASSERT_EQ(PANEL_LCD_COLS / PANEL_KNOBS, 4);
	static const char *const faders[6] = { "drum 1", "drum 2", "drum 3", "snare", "clp/cym", "hihats" };
	for (unsigned i = 0; i < PANEL_FADERS; i++)
		ASSERT_EQ(strcmp(panel_faderNames[i], faders[i]), 0);
	/* the jacks, left to right as the strip shows them; the main output is the pair that the firmware routes to by
	 * default (mixer_audioRouting is 0, DAC1: out 1, jacks L1 and R1) */
	static const char *const jacks[4] = { "out 2 L", "out 2 R", "main out L", "main out R" };
	static const unsigned jackBits[4] = { 4u, 8u, 1u, 2u };
	for (unsigned i = 0; i < 4; i++)
	{
		ASSERT_EQ(strcmp(panel_jackNames[i], jacks[i]), 0);
		ASSERT_EQ(panel_jackBits[i], jackBits[i]);
	}
}

TEST_MAIN()
