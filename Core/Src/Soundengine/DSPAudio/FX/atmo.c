/*
 * atmo.c
 *
 * The LICHOMAT v0.4 reverb stage, see atmo.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "Soundengine/DSPAudio/FX/atmo.h"
#include "Soundengine/DSPAudio/FX/Reverb.h"

/* plan D31: in .bss (RAM), zero at boot */
__fp16 REVERB_RAM[REVERB_UNITS * MAX_ALLPASS_SIZE];
static Reverb atmo_reverb;

void atmo_init(void)
{
	Reverb_init(&atmo_reverb);
}
