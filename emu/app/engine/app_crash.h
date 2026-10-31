/*
 * app_crash.h
 *
 * The crash note of stage 1b spec 6.9, written with async-signal-safe calls only (plan E20).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_CRASH_H_
#define APP_CRASH_H_
#include <stddef.h>
#include <stdint.h>
void app_crashInit(const char *dir, uint64_t (*now)(void), void (*trail)(int fd, unsigned n));
void app_crashThread(void);		/**< each thread that may fault: its own alternate signal stack */
void app_crashSignals(void);	/**< SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP */
int app_crashWrite(const char *what);	/**< the note, once per process; 0 or -1 */
int app_crashLatest(const char *dir, const char *shown, char *name, size_t n);	/**< 1: a note newer than shown */
#endif
