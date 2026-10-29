/*
 * render_fw.c
 *
 * Every firmware call of lichomat-render (stage 1b spec 5.3); see render_fw.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "render_fw.h"

#include <stdio.h>

#include "main.h"
#include "Preset/PresetManager.h"

void rfw_seedCard(void *res)
{
	/* sd_formatRoutine() after f_mkfs() and f_mount(): the card is mounted at boot */
	preset_createGlobals();
	preset_init(0);
	sprintf(savefileNames[0][PROJECT_NAMES], "Init");
	*(int *)res = (int)preset_saveProject(0);
}

void rfw_morphBusy(void *out)
{
	*(uint8_t *)out = preset_morphBusy();
}
