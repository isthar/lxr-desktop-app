/*
 * panel_layout.c
 *
 * The panel's tables: see panel_layout.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "panel_layout.h"

const char *const panel_names[PANEL_BUTTONS] = {
	"bar+", "drum 1", "drum 2", "drum 3", "snare", "clp/cym", "cl hh", "op hh",
	"click", "filter", "lfo", "mix", "step 16", "step 15", "step 14", "step 13",
	"osc", "aeg", "mod", "fm", "step 12", "step 11", "step 10", "step 9",
	"voice", "perf", "load", "save", "step 8", "step 7", "step 6", "step 5",
	"shift", "play", "rec", "copy/clr", "step 4", "step 3", "step 2", "step 1", "bar-",
};

const char *const panel_faderNames[PANEL_FADERS] = { "drum 1", "drum 2", "drum 3", "snare", "clp/cym", "hihats" };

const char *const panel_shiftNames[16] = { "drum 1", "drum 2", "drum 3", "snare", "clp/cym", "cl hh", "op hh", "kit", "morph",
	"song", "project", "config", "reload", "last step", "fx", "pattern" };

const char *const panel_jackNames[4] = { "out 2 L", "out 2 R", "main out L", "main out R" };
const uint8_t panel_jackBits[4] = { 4u, 8u, 1u, 2u };

const panel_group_t panel_groups[PANEL_GROUPS] = {
	[PANEL_GROUP_BAR_VOICES] = { "bar and voices", 9, 9, { 40, 0, 1, 2, 3, 4, 5, 6, 7 } },
	[PANEL_GROUP_TRANSPORT] = { "transport", 4, 4, { 32, 33, 34, 35 } },
	[PANEL_GROUP_MODES] = { "modes", 4, 4, { 24, 25, 26, 27 } },
	[PANEL_GROUP_PAGES] = { "pages", 8, 8, { 16, 17, 18, 19, 8, 9, 10, 11 } },
	[PANEL_GROUP_STEPS] = { "steps", 16, 16, { 39, 38, 37, 36, 31, 30, 29, 28, 23, 22, 21, 20, 15, 14, 13, 12 } },
};
