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

/* LICHOMAT v0.4: the reverb's rate by plan D41, PROVISIONAL until the owner's reading H12
 * (tracker Q30): H12 pk not read yet, R*_full 35.07 %, R*_half 18.03 %, R_dj 3.69 %
 * -> full rate if P + F + D <= 85 (pk <= 46.24), else half rate if P + H + D <= 85
 * (pk <= 63.28), else half rate and lean. Half rate until then; both rates are built and
 * tested (RV2, RV4), so the reading changes only these two lines. */
#ifndef ATMO_REVERB_DECIM
#define ATMO_REVERB_DECIM	2	/* 1 full rate, 2 half rate (plan D42) */
#endif
#ifndef ATMO_REVERB_LEAN
#define ATMO_REVERB_LEAN	0	/* 1: no early reflections, and the wet R is the wet L */
#endif

/* sets the reverb up on its own buffer; FxBus_init() calls it */
void atmo_init(void);

/** plan D32: 1 while the reverb stage runs (level > 0, or its gain not yet ramped to 0) */
uint8_t atmo_active(void);
/** plan D32: adds the reverb's wet L/R x gain to the stereo bus of the slot effect, frames <= 64 */
void atmo_process(int16_t *bus, uint32_t frames);

/** LICHOMAT v0.4: a parameter of the reverb stage, the delay sync or the DJ filter, 0..1 (plan D33, D34) */
void atmo_setParam(uint16_t paramNr, float v);
/** plan A8, D37: flips activeParameters[PAR_REV_FREEZE] 0/1 and applies it (SHIFT + step 3 in the
 * performance mode) */
void atmo_toggleFreeze(void);
/** the defaults of the kit block, and a value of it checked (plan D35) */
void atmo_kitBlockDefaults(uint8_t *values);
uint8_t atmo_kitBlockChecked(uint8_t idx, uint8_t value);
/** the D35 defaults of the five kit slots in a kit's parameter array */
void atmo_kitSlotDefaults(uint8_t *kit);

#endif /* ATMO_H_ */
