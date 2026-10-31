/*
 * app_card.h
 *
 * The app's card folder (stage 1b spec 6.7): paths, the first run's seeding,
 * import, export card and export project, with copies that keep the order
 * (spec 4.8, plan E16) and backups named by local time (E17). Main thread,
 * only after 'card out applied'.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_CARD_H_
#define APP_CARD_H_

#include <stddef.h>
#include <stdint.h>

#include "emu_hostfs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_paths
{
	char home[EMU_HOST_PATH], card[EMU_HOST_PATH], backups[EMU_HOST_PATH], settings[EMU_HOST_PATH];
} app_paths_t;

typedef void (*app_warn_t)(void *ctx, const char *what);

/** $LICHOMAT_EMU_HOME, else $HOME/Library/Application Support/LICHOMAT Emu (E21), with Card,
 * Card backups and settings.txt in it; makes home and backups; 0 or -1 */
int app_pathsInit(app_paths_t *p);
/** the same for a given home folder */
int app_pathsAt(app_paths_t *p, const char *home);
int app_cardState(const char *dir);		/**< 1 missing or only dot entries; 0 other entries; -1 not a folder */
int app_cardNonDot(const char *dir, char *names, size_t n);	/**< entries that are not dot entries, names joined by ", "; -1 unreadable */
int app_cardSeed(const char *render, const char *card, char *err, size_t n);	/**< RENDER --seed-card CARD; 0 */
int app_cardCheck(const char *card, int *project);	/**< PROJ.IDX's project has PRJ.NFO: 1; not: 0; -1 */

typedef struct app_cardEntry
{
	char host[256], name[13];
	int isDir;
	int64_t birthSec;
	long birthNsec;
	unsigned seq;	/**< readdir() order */
} app_cardEntry_t;

int app_cardIsFat(const char *path);	/**< statfs: msdos or exfat */
void app_cardOrder(app_cardEntry_t *list, int n, int fat);
int app_cardList(const char *dir, int fat, app_cardEntry_t **out, app_warn_t warn, void *ctx);
int app_cardCopy(const char *src, const char *dst, app_warn_t warn, void *ctx);	/**< into an existing folder */
int app_backupName(const char *backups, int64_t localSec, const char *suffix, char *out, size_t n);
/** step 2 and 3 of spec 6.7: the card moves to a backup, src is copied in; a failed copy puts the card back (backup is
 * then empty). Never from the card's own folder, one inside it or one that holds it. 0 or -1; -2: the copy failed and the
 * card could not be put back, backup names the folder that holds it */
int app_cardImport(const app_paths_t *p, const char *src, int64_t localSec, char *backup, size_t bn, app_warn_t warn, void *ctx);
/** -2 dst is not empty (err lists what is in it); -3 dst is the card, one inside it or one that holds it (err says so) */
int app_cardExport(const char *card, const char *dst, char *err, size_t n, app_warn_t warn, void *ctx);
/** The new PROJnn is copied into a temporary 8.3 folder of dst, the one there is copied into Card backups and moved aside,
 * the new one is renamed in and the old one removed; a failure leaves dst's project where it was and err names the backup.
 * 0; 2 dst has PROJnn and replace is 0; -1 (also for a dst that is the card, one inside it or one that holds it) */
int app_cardExportProject(const app_paths_t *p, int project, const char *dst, int replace, int64_t localSec,
	char *err, size_t n, app_warn_t warn, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_CARD_H_ */
