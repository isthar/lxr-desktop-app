/*
 * app_fw.h
 *
 * The app's only firmware calls (plan E12), made through emu_directCall() on the
 * firmware thread, between two slices.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_FW_H_
#define APP_FW_H_

void app_fwExtSync(void *out);	/**< seq_getExtSync() into *(uint8_t *)out */
void app_fwCounters(void *out);	/**< into an app_fwCounters_t */

#endif /* APP_FW_H_ */
