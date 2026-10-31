/*
 * emu_notices.h
 *
 * The Appropriate Legal Notices of stage 1b spec 10.1, in one place for both
 * binaries (plan E26): lichomat-render prints them for --version, and LICHOMAT
 * Emu prints them for --version and shows them in its About box. Each line is
 * one C string, so that each is one run for M21's name scan; the lines that name
 * the maker, the product or the repository are on its allow-list
 * (emu/tests/name_allow.txt).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_NOTICES_H_
#define EMU_NOTICES_H_

#define EMU_TAGLINE "emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02"
/** the firmware changes that the emulator makes to the firmware it runs (spec 7); F13-F18 landed in R3 */
#define EMU_FIRMWARE_CHANGES "F1-F18 and F6b"

static const char *const emu_legal[] __attribute__((unused)) = {
	"Copyright (C) 2012-2026 Erica Synths",
	"Copyright (C) 2026 Tomasz Krzywicki",
	"Modified from the LXR firmware 1.7 by Erica Synths; LICHOMAT and its emulator by Tomasz Krzywicki, 2026",
	"Arm Optimized Routines: Copyright (c) 1999-2022, Arm Limited. MIT licence.",
	"FatFs ff.h and integer.h: Copyright (C) 2017, ChaN. FatFs licence.",
	"This program comes with ABSOLUTELY NO WARRANTY. It is free software under the GNU",
	"General Public License version 3 only, with the additional permission and terms",
	"in NOTICE.md. See LICENSE and NOTICE.md.",
	"Source: https://github.com/isthar/lxr-desktop-app at the commit above.",
	"Not affiliated with Erica Synths.",
};
#define EMU_LEGAL_COUNT (sizeof emu_legal / sizeof emu_legal[0])

#endif /* EMU_NOTICES_H_ */
