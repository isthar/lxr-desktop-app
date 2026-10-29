/*
 * render_fw.h
 *
 * Every firmware call of lichomat-render (stage 1b spec 5.3): the only file of
 * the renderer that includes firmware headers. Each function runs through
 * emu_directCall() at a pass boundary.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef RENDER_FW_H_
#define RENDER_FW_H_

#include <stdint.h>

/** --seed-card (spec 6.7): the steps of sd_formatRoutine() (appMain.c) after its
 * confirmation and f_mkfs(): default globals, kit 0 from preset_init(0),
 * project 0 named "Init", saved by preset_saveProject(0). res is an int that
 * receives the FRESULT. */
void rfw_seedCard(void *res);

/** preset_morphBusy() into *(uint8_t *)out */
void rfw_morphBusy(void *out);

#endif /* RENDER_FW_H_ */
