/*
 * app_bind.h
 *
 * Which system endpoint a DIN binding picks (stage 1b spec 6.9, plan E28). Plain C,
 * safe for the C++ GUI.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_BIND_H_
#define APP_BIND_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_endpoint { int32_t uid; char name[128]; } app_endpoint_t;

/** the endpoint to bind: the stored unique ID, else the first with the stored name; -1 none */
int app_bindPick(const app_endpoint_t *list, int n, int32_t uid, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* APP_BIND_H_ */
