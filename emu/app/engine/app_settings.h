/*
 * app_settings.h
 *
 * The settings of stage 1b spec 6.9 in settings.txt, as key = value lines (plan E19). Main thread.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_SETTINGS_H_
#define APP_SETTINGS_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_settings
{
	char outputUid[128];		/**< "" follows the default output */
	uint32_t bufferFrames;		/**< 0: the device's */
	char mix[8];				/**< sum, out1, out2, four */
	int32_t dinSrcUid, dinDstUid;
	char dinSrcName[128], dinDstName[128];
	int32_t usbSrcUid, usbDstUid;	/**< the virtual endpoints' unique IDs */
	int dAuto;
	double dMs;
	int window[4];
	double genBpm;
	int genPpq;
	char crashShown[64];
	char extra[4096];			/**< other keys' lines, kept as read */
} app_settings_t;

void app_settingsDefaults(app_settings_t *s);	/**< zeroes, then mix sum, dAuto 1, window 100 100 1100 760, 120 BPM, 24 PPQ */
int app_settingsLoad(app_settings_t *s, const char *path);	/**< 0, 1 no file, -1 unreadable */
int app_settingsSave(const app_settings_t *s, const char *path);	/**< PATH.tmp, fsync, rename; 0 or -1 */

#ifdef __cplusplus
}
#endif

#endif
