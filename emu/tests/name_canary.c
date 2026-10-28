/*
 * name_canary.c
 *
 * For M21_negative (stage 1b spec 10.2, 12.1): two C strings that the name scan
 * must reject, a maker's name and a product name, and one that it must pass. No
 * binary links this object.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
const char *const emu_nameCanary[3] = { "Erica Synths", "LXR-02", "LICHOMAT Emu" };
