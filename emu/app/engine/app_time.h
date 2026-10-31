/*
 * app_time.h
 *
 * Local time for the names of backups and crash notes (stage 1b spec 6.7, 11, plan
 * E17, E20). app_stampName() and app_localSeconds() are async-signal-safe.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_TIME_H_
#define APP_TIME_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_civil(int64_t days, int *y, unsigned *m, unsigned *d);	/**< day 0 is 1970-01-01 */
/** PREFIX YYYYMMDD-HHMMSS SUFFIX into out; its length, or 0 if it does not fit */
size_t app_stampName(int64_t localSec, const char *prefix, const char *suffix, char *out, size_t n);
void app_timeZoneRefresh(void);	/**< main thread: at start and every minute */
int64_t app_localSeconds(void);	/**< CLOCK_REALTIME plus the stored UTC offset */

#ifdef __cplusplus
}
#endif

#endif /* APP_TIME_H_ */
