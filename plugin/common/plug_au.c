/*
 * plug_au.c
 *
 * The AU's view of the plugin (plan E9.7 Task 3); see plug_au.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_au.h"

#include <math.h>

float plug_auValue(unsigned i, uint8_t v)
{
	return (float)((int)v + plug_params[i].offset);
}

uint8_t plug_auPlain(unsigned i, double value)
{
	const double v = floor(value - plug_params[i].offset + 0.5);
	if (!(v > 0.0))
		return 0;	/* also not a number */
	return (uint8_t)(v >= plug_params[i].max ? plug_params[i].max : v);
}

int plug_auRender(plug_auBuses_t *b)
{
	b->cycles++;
	return b->pulled != 0 && b->cycles - b->pulled <= PLUG_AU_OUT2_CYCLES;
}

void plug_auPulled(plug_auBuses_t *b)
{
	b->pulled = b->cycles;
}
