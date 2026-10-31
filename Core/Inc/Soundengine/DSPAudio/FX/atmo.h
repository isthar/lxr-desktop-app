/*
 * atmo.h
 *
 * The LICHOMAT v0.4 reverb stage ("atmosphere"), which runs after the FX bus's slot
 * effect (plan D32). Its Reverb object and its 14-unit buffer REVERB_RAM (Reverb.h)
 * live in RAM, not in DTCM, and the delay keeps FX_BUFFER to itself (plan D31).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef ATMO_H_
#define ATMO_H_

#include <stdint.h>

/* sets the reverb up on its own buffer; FxBus_init() calls it */
void atmo_init(void);

/** LICHOMAT v0.4: a parameter of the reverb stage, the delay sync or the DJ filter, 0..1 (plan D33, D34) */
void atmo_setParam(uint16_t paramNr, float v);
/** the defaults of the kit block, and a value of it checked (plan D35) */
void atmo_kitBlockDefaults(uint8_t *values);
uint8_t atmo_kitBlockChecked(uint8_t idx, uint8_t value);
/** the D35 defaults of the five kit slots in a kit's parameter array */
void atmo_kitSlotDefaults(uint8_t *kit);

#endif /* ATMO_H_ */
