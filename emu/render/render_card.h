/*
 * render_card.h
 *
 * The card of lichomat-render (stage 1b spec 5.2): the copy it boots from, the
 * project rule, the PRJ.NFO check, --keep-card and the removal of the copy.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef RENDER_CARD_H_
#define RENDER_CARD_H_

#include <stddef.h>

#include "emu_hostfs.h"
#include "render_opts.h"

/** 1 missing, or a folder with nothing but dot entries; 0 a folder with other
 * entries; -1 not a folder (or unreadable) */
int render_cardEmpty(const char *dir);

/** Check that --card is a readable folder, copy it in the card's order into a
 * new $TMPDIR/lichomat-render-XXXXXX (copy), /TMP left out unless --as-booted,
 * pick the project (--project, else /PROJ.IDX's byte with 64-255 as 0, else 0)
 * and check that it has PROJnn/PRJ.NFO, names matched as FatFs does. With
 * --project, the copy's /PROJ.IDX holds that byte. Returns 0, or 2 with
 * "card DIR: problem" in err and no copy left. */
int render_cardPrepare(const render_opts_t *o, char copy[EMU_HOST_PATH], int *project, char *err, size_t n);

/** copy the card copy's final state into keep, created if missing; 0 or -1 */
int render_cardKeep(const char *copy, const char *keep);

/** remove a folder and everything in it */
void render_cardRemove(const char *dir);

#endif /* RENDER_CARD_H_ */
