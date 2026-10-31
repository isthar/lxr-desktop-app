/*
 * test_app_bind.c
 *
 * R4 (stage 1b spec 6.9, plan E28): which endpoint a DIN binding picks, the stored
 * unique ID first and then the stored name.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_bind.h"
#include "seqc_test.h"

TEST(R4_bind)
{
	const app_endpoint_t l[3] = { { 11, "Port A" }, { 22, "Port B" }, { 33, "Port A" } };
	ASSERT_EQ(app_bindPick(l, 3, 22, "Port A"), 1);	/* the stored unique ID first (spec 6.9) */
	ASSERT_EQ(app_bindPick(l, 3, 99, "Port A"), 0);	/* else the stored name */
	ASSERT_EQ(app_bindPick(l, 3, 99, "Gone"), -1);	/* 'not connected' */
	ASSERT_EQ(app_bindPick(l, 3, 0, ""), -1);
}

TEST_MAIN()
