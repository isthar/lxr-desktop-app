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
#include <string.h>

/* plan D31: in .bss (RAM), zero at boot */
__fp16 REVERB_RAM[REVERB_UNITS * MAX_ALLPASS_SIZE];
static Reverb atmo_reverb;

/* plan D33, D34: the five kit slots, then the seven entries of the kit block, 0..1 */
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

/* plan A7: the wet gain (level/127)^2, set by the main loop and read once per block; the gain the
 * last block ended on; whether the reverb runs (0: not computed, its buffer holds an old tail);
 * the blocks of REVERB_RAM cleared since waking, 8,192 B each */
#define ATMO_CLEAR_BLOCKS 7
#define ATMO_CLEAR_VALUES (REVERB_UNITS * MAX_ALLPASS_SIZE / ATMO_CLEAR_BLOCKS)
_Static_assert(ATMO_CLEAR_VALUES * ATMO_CLEAR_BLOCKS == REVERB_UNITS * MAX_ALLPASS_SIZE, "REVERB_RAM in 7 parts");
static volatile float atmo_target;
static float atmo_gain;
static uint8_t atmo_awake;
static uint8_t atmo_cleared;

/* a parameter 0..1 as 0..127 */
static uint8_t atmo_u7(float v)
{
	const long u = lroundf(v * 127.f);
	return (uint8_t)((u < 0) ? 0 : (u > 127) ? 127 : u);
}

void atmo_init(void)
{
	Reverb_init(&atmo_reverb);
	Reverb_setDecim(&atmo_reverb, ATMO_REVERB_DECIM);	/* plan D41, D42 */
	atmo_target = 0.f;
	atmo_gain = 0.f;
	atmo_awake = 0;
	atmo_cleared = 0;
	/* plan D33: the live DJ filter starts flat, before the first kit load keeps it */
	activeParameters[PAR_DJ_FILTER] = ATMO_DJ_FLAT;
	atmo_setParam(PAR_DJ_FILTER, ATMO_DJ_FLAT / 127.f);
	atmo_setParam(PAR_REV_DECAY, ATMO_DECAY_DEFAULT / 127.f);
	for(uint8_t i = 0; i < END_OF_KIT_BLOCK - END_OF_GLOBALS; i++)
		atmo_setParam(END_OF_GLOBALS + i, atmo_blockDefault[i] / 127.f);
}

/* a setter of the reverb from the main loop: the setters move the units' read pointers, which the
 * audio interrupt advances, so they run with interrupts masked (as Delay_setRangeAndTime()) */
static void atmo_setReverb(void (*set)(Reverb *, float), float v)
{
	const uint32_t primask = __get_PRIMASK();
	__disable_irq();
	set(&atmo_reverb, v);
	__set_PRIMASK(primask);
}

uint8_t atmo_active(void)
{
	return (atmo_target > 0.f) || atmo_awake;
}

void atmo_process(int16_t *bus, uint32_t frames)
{
	const float target = atmo_target;
	if(!atmo_awake)
	{	/* waking: clear the old tail first, the gain held at 0 */
		atmo_awake = 1;
		atmo_cleared = 0;
		atmo_gain = 0.f;
	}
	if(atmo_cleared < ATMO_CLEAR_BLOCKS)
	{
		memset(&REVERB_RAM[atmo_cleared * ATMO_CLEAR_VALUES], 0, ATMO_CLEAR_VALUES * sizeof(__fp16));
		if(++atmo_cleared == ATMO_CLEAR_BLOCKS)
			Reverb_clearState(&atmo_reverb);
		if(target <= 0.f)
			atmo_awake = 0;
		return;
	}
	float wetL[OUTPUT_DMA_SIZE], wetR[OUTPUT_DMA_SIZE];
	if(frames > OUTPUT_DMA_SIZE)
		frames = OUTPUT_DMA_SIZE;
	Reverb_processWet(&atmo_reverb, bus, wetL, wetR, (uint16_t)frames);
	/* the gain ramps linearly across the block; the float limit keeps the conversion defined */
	const float g0 = atmo_gain * 32768.f;
	const float dg = (target - atmo_gain) * 32768.f / (float)frames;
	for(uint32_t i = 0; i < frames; i++)
	{
		const float g = g0 + dg * (float)(i + 1);
		const int32_t l = (int32_t)fminf(fmaxf(wetL[i] * g, -65536.f), 65536.f);
		const int32_t r = (int32_t)fminf(fmaxf(wetR[i] * g, -65536.f), 65536.f);
		bus[2 * i] = (int16_t)__SSAT((int32_t)bus[2 * i] + l, 16);
		bus[2 * i + 1] = (int16_t)__SSAT((int32_t)bus[2 * i + 1] + r, 16);
	}
	atmo_gain = target;
	if(target <= 0.f)
		atmo_awake = 0;	/* the stage is off from the next block (plan D36: level 0 is not computed) */
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
	case PAR_REV_LEVEL:
		atmo_par[ATMO_REV_LEVEL] = v;
		{	/* plan D36: the wet gain (level/127)^2 */
			const float l = atmo_u7(v) / 127.f;
			atmo_target = l * l;
		}
		break;
	case PAR_REV_FREEZE:	atmo_par[ATMO_REV_FREEZE] = v;	break;
	case PAR_REV_DECAY:
		atmo_par[ATMO_REV_DECAY] = v;
		atmo_setReverb(Reverb_setDecay, v);	/* plan D36 */
		break;
	case PAR_REV_SIZE:
	case PAR_REV_TONE:
	case PAR_REV_PREDELAY:
	case PAR_REV_MOD:
	case PAR_REV_DUCK_SRC:
	case PAR_REV_DUCK_DEPTH:
	case PAR_FX_DELAY_SYNC:
		atmo_par[ATMO_BLOCK + (paramNr - PAR_REV_SIZE)] = v;
		/* plan D36: size, tone, predelay and modulation reach the reverb */
		if(paramNr == PAR_REV_SIZE)
			atmo_setReverb(Reverb_setSize, v);
		else if(paramNr == PAR_REV_TONE)
		{	/* composition 0 makes both tilt EQs a low-pass at 0 Hz, which holds its last output
			 * for ever (a0 0, b1 1): tone 0 plays as tone 1 */
			atmo_setReverb(Reverb_setComposition, (v > 1.f / 127.f) ? v : (1.f / 127.f));
		}
		else if(paramNr == PAR_REV_PREDELAY)
			atmo_setReverb(Reverb_setDelayTime, v);
		else if(paramNr == PAR_REV_MOD)
			atmo_setReverb(Reverb_setMod, v);
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
