/*
 * plug_au.h
 *
 * The AU's view of the plugin (plan E9.7 Task 3): pure functions that the AU
 * (plugin/au/au_unit.cpp) and its tests share.
 *
 * Parameters. The AU's parameters are the VST3's: the read-only "Engine" (id 0) and the 36 of
 * plug_params.h, with their ids. An AU parameter's value is the number the device shows: the
 * firmware's value plus the parameter's offset (a pan -63..64, the pattern 1..64); a voice's output
 * (0..6) and the engine's state (0..3) are indexed, with their names.
 *
 * Out 2. AU v2 has no bus activation: Out 2 counts as used while the host renders it. Each render
 * cycle (a new time stamp) asks plug_auRender() whether Out 1 and Out 2 are the device's two
 * outputs (the host rendered Out 2 in one of the last PLUG_AU_OUT2_CYCLES cycles) or Out 1 is
 * their sum, as the VST3's Out 1 while the host has not activated Out 2.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_AU_H_
#define PLUG_AU_H_

#include <stdint.h>

#include "plug_params.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_AU_ENGINE 0u			/**< the id of the read-only parameter "Engine" */
#define PLUG_AU_OUT2_CYCLES 2u		/**< Out 2 stays split this many cycles after its last render */

/** the AU value of parameter i's value v: v + its offset */
float plug_auValue(unsigned i, uint8_t v);

/** parameter i's value of an AU value: the nearest, within 0..max; not a number is 0 */
uint8_t plug_auPlain(unsigned i, double value);

/** Out 2's use: the render cycles so far, and the cycle in which the host last rendered Out 2 */
typedef struct plug_auBuses
{
	uint64_t cycles;
	uint64_t pulled;	/**< 0: never */
} plug_auBuses_t;

/** a new render cycle: returns 1 when Out 1 and Out 2 are to be the device's outputs, 0 when Out 1
 * is their sum */
int plug_auRender(plug_auBuses_t *b);

/** the host renders Out 2 in the current cycle */
void plug_auPulled(plug_auBuses_t *b);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_AU_H_ */
