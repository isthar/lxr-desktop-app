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

/* LICHOMAT v0.4: the reverb's rate by plan D41 as amended by tracker Q34: R*_half 18.03 %,
 * R_dj 3.69 % -> half rate if P + H + D <= 85 (pk <= 63.28), else half rate and lean. D41's
 * full rate (R*_full 35.07 %, pk <= 46.24) is not used: there the tank's bass tail leans left
 * (branch v0.4-full keeps it). Branch v0.4-half (tracker E7.9): half rate. H12 pk not read
 * yet; the release commit fills it in. Both rates are built and tested (RV2, RV4). */
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

/** plan D40, rumble: the peak follower of voice voiceNr's block of OUTPUT_DMA_SIZE samples (only the
 * chosen source's counts), the wet gain 1 - depth min(1, 2 env), and the follower's value */
void atmo_duckSense(uint8_t voiceNr, const int16_t *block);
float atmo_duckGain(void);
float atmo_duckEnv(void);

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
