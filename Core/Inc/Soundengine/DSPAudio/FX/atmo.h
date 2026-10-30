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

/* sets the reverb up on its own buffer; FxBus_init() calls it */
void atmo_init(void);

#endif /* ATMO_H_ */
