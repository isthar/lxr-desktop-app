/*
 * app_bind.c
 *
 * Which system endpoint a DIN binding picks (stage 1b spec 6.9, plan E28).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_bind.h"

#include <string.h>

int app_bindPick(const app_endpoint_t *list, int n, int32_t uid, const char *name)
{
	for (int i = 0; uid != 0 && i < n; i++)
		if (list[i].uid == uid)
			return i;
	for (int i = 0; name && name[0] && i < n; i++)
		if (!strcmp(list[i].name, name))
			return i;
	return -1;
}
