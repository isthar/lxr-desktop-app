/*
 * name_canary.c
 *
 * For M21_negative (stage 1b spec 10.2, 12.1): strings that the name scan must
 * reject, and clean ones that it must pass, in the three places a C string
 * lands: string literals in __TEXT,__cstring, a table of fixed-size names (as
 * menu/MenuText.h has) in __TEXT,__const, and a char array that is not const
 * in __DATA,__data. No binary links this object.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
const char *const emu_nameCanary[3] = { "Erica Synths", "LXR-02", "LICHOMAT Emu" };
const char emu_nameCanaryMenu[2][16] = { "LICHOMAT", "Sonic Potions" };
char emu_nameCanaryTitle[] = "lxr drum kit";
