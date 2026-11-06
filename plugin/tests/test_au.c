/*
 * test_au.c
 *
 * P13: the AU's view of the plugin (plan E9.7 Task 3): its parameters' values as the device shows
 * them, and the rule for Out 2 (plug_au.h).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>

#include "plug_au.h"
#include "seqc_test.h"

/* indices in plug_params[] */
enum { I_PATTERN = 0, I_VOL1 = 1, I_PAN1 = 7, I_FX = 29, I_OUT1 = 30 };

TEST(P13_unit_values)
{
	/* every value of every parameter: shown as value + offset, and back */
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		for (unsigned v = 0; v <= plug_params[i].max; v++)
		{
			const float a = plug_auValue(i, (uint8_t)v);
			ASSERT_EQ_DBL(a, (double)((int)v + plug_params[i].offset));
			ASSERT_EQ(plug_auPlain(i, a), v);
			ASSERT_EQ(plug_auPlain(i, a + 0.49), v);
			ASSERT_EQ(plug_auPlain(i, a - 0.5), v);	/* half way: the upper one */
		}
	ASSERT_EQ_DBL(plug_auValue(I_PAN1, 0), -63.0);
	ASSERT_EQ_DBL(plug_auValue(I_PAN1, 63), 0.0);
	ASSERT_EQ_DBL(plug_auValue(I_PAN1, 127), 64.0);
	ASSERT_EQ_DBL(plug_auValue(I_PATTERN, 0), 1.0);
	ASSERT_EQ_DBL(plug_auValue(I_PATTERN, 63), 64.0);
	ASSERT_EQ_DBL(plug_auValue(I_OUT1, 6), 6.0);
	/* outside the range: the nearest end; not a number: 0 */
	ASSERT_EQ(plug_auPlain(I_PAN1, -200.0), 0);
	ASSERT_EQ(plug_auPlain(I_PAN1, 200.0), 127);
	ASSERT_EQ(plug_auPlain(I_PATTERN, 0.0), 0);
	ASSERT_EQ(plug_auPlain(I_PATTERN, 65.0), 63);
	ASSERT_EQ(plug_auPlain(I_FX, 101.0), 100);
	ASSERT_EQ(plug_auPlain(I_VOL1, NAN), 0);
	ASSERT_EQ(plug_auPlain(I_VOL1, INFINITY), 127);
	ASSERT_EQ(plug_auPlain(I_VOL1, -INFINITY), 0);
}

TEST(P13_unit_out2)
{
	/* never rendered: the sum */
	plug_auBuses_t b = {0, 0};
	for (int k = 0; k < 5; k++)
		ASSERT_EQ(plug_auRender(&b), 0);
	/* rendered in cycle 6 (after its render: Out 1 first): split from cycle 7 */
	ASSERT_EQ(plug_auRender(&b), 0);
	plug_auPulled(&b);
	ASSERT_EQ(plug_auRender(&b), 1);
	plug_auPulled(&b);
	ASSERT_EQ(plug_auRender(&b), 1);
	/* one cycle without it: still split; two: the sum again */
	ASSERT_EQ(plug_auRender(&b), 1);
	ASSERT_EQ(plug_auRender(&b), 0);
	ASSERT_EQ(plug_auRender(&b), 0);
	/* Out 2 rendered first in its cycle (the render happens on its pull): split from the next */
	ASSERT_EQ(plug_auRender(&b), 0);
	plug_auPulled(&b);
	ASSERT_EQ(plug_auRender(&b), 1);
	ASSERT_EQ(b.cycles, 13u);
}

TEST_MAIN()
