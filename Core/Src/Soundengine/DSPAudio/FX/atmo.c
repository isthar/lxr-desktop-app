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
#include "Parameters.h"
#include "menu/menu.h"	/* activeParameters */
#include "Soundengine/FxBus.h"	/* fxBus_delaySync */
#include "Soundengine/DSPAudio/FX/fxsync.h"
#include "Soundengine/DSPAudio/FX/djfilter.h"
#include <math.h>

/* plan D31: in .bss (RAM), zero at boot */
__fp16 REVERB_RAM[REVERB_UNITS * MAX_ALLPASS_SIZE];
static Reverb atmo_reverb;

/* plan D33, D34: the five kit slots, then the seven entries of the kit block, 0..1; nothing
 * reads them yet (A3) */
enum
{
	ATMO_DJ_FILTER,
	ATMO_DJ_RESO,
	ATMO_REV_LEVEL,
	ATMO_REV_FREEZE,
	ATMO_REV_DECAY,
	ATMO_BLOCK,		/* PAR_REV_SIZE .. PAR_FX_DELAY_SYNC */
	ATMO_PARAMS = ATMO_BLOCK + (END_OF_KIT_BLOCK - END_OF_GLOBALS)
};
static float atmo_par[ATMO_PARAMS];

/* plan D35: size, tone, predelay, modulation, duck source, duck depth, delay sync */
static const uint8_t atmo_blockDefault[END_OF_KIT_BLOCK - END_OF_GLOBALS] = { 100, 64, 10, 40, 0, 100, 0 };
static const uint8_t atmo_blockMax[END_OF_KIT_BLOCK - END_OF_GLOBALS] = { 127, 127, 127, 127, 6, 127, 9 };

#define ATMO_DJ_FLAT 64		/* plan D39: 63..65 is flat */
#define ATMO_DECAY_DEFAULT 64

/* a parameter 0..1 as 0..127 */
static uint8_t atmo_u7(float v)
{
	const long u = lroundf(v * 127.f);
	return (uint8_t)((u < 0) ? 0 : (u > 127) ? 127 : u);
}

void atmo_init(void)
{
	Reverb_init(&atmo_reverb);
	/* plan D33: the live DJ filter starts flat, before the first kit load keeps it */
	activeParameters[PAR_DJ_FILTER] = ATMO_DJ_FLAT;
	atmo_setParam(PAR_DJ_FILTER, ATMO_DJ_FLAT / 127.f);
	atmo_setParam(PAR_REV_DECAY, ATMO_DECAY_DEFAULT / 127.f);
	for(uint8_t i = 0; i < END_OF_KIT_BLOCK - END_OF_GLOBALS; i++)
		atmo_setParam(END_OF_GLOBALS + i, atmo_blockDefault[i] / 127.f);
}

void atmo_setParam(uint16_t paramNr, float v)
{
	switch(paramNr)
	{
	case PAR_DJ_FILTER:
	case PAR_DJ_RESO:
		atmo_par[(paramNr == PAR_DJ_FILTER) ? ATMO_DJ_FILTER : ATMO_DJ_RESO] = v;
		/* plan D39: the DJ filter takes both, 0..127 */
		djFilter_set(atmo_u7(atmo_par[ATMO_DJ_FILTER]), atmo_u7(atmo_par[ATMO_DJ_RESO]));
		break;
	case PAR_REV_LEVEL:		atmo_par[ATMO_REV_LEVEL] = v;	break;
	case PAR_REV_FREEZE:	atmo_par[ATMO_REV_FREEZE] = v;	break;
	case PAR_REV_DECAY:		atmo_par[ATMO_REV_DECAY] = v;	break;
	case PAR_REV_SIZE:
	case PAR_REV_TONE:
	case PAR_REV_PREDELAY:
	case PAR_REV_MOD:
	case PAR_REV_DUCK_SRC:
	case PAR_REV_DUCK_DEPTH:
	case PAR_FX_DELAY_SYNC:
		atmo_par[ATMO_BLOCK + (paramNr - PAR_REV_SIZE)] = v;
		if(paramNr == PAR_FX_DELAY_SYNC)
		{	/* plan D38: the division 0..9, which FxBus_process() reads once per block */
			const long d = lroundf(v * 127.f);
			fxBus_delaySync = (uint8_t)((d < 0) ? 0 : (d > FXSYNC_COUNT - 1) ? (FXSYNC_COUNT - 1) : d);
		}
		break;
	default:
		break;
	}
}

void atmo_kitBlockDefaults(uint8_t *values)
{
	for(uint8_t i = 0; i < END_OF_KIT_BLOCK - END_OF_GLOBALS; i++)
		values[i] = atmo_blockDefault[i];
}

uint8_t atmo_kitBlockChecked(uint8_t idx, uint8_t value)
{
	if(idx >= END_OF_KIT_BLOCK - END_OF_GLOBALS)
		return 0;
	return (value <= atmo_blockMax[idx]) ? value : atmo_blockDefault[idx];
}

void atmo_kitSlotDefaults(uint8_t *kit)
{
	kit[PAR_DJ_FILTER] = ATMO_DJ_FLAT;
	kit[PAR_DJ_RESO] = 0;
	kit[PAR_REV_LEVEL] = 0;
	kit[PAR_REV_FREEZE] = 0;
	kit[PAR_REV_DECAY] = ATMO_DECAY_DEFAULT;
}
