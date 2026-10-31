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

/* plan D40: rumble. The source 0 (off) or 1..6 (voice 0..5), the depth 0..1, the peak follower of
 * that voice's block (the audio interrupt's), and the duck gain the last block ended on */
static uint8_t atmo_duckSrc;
static float atmo_duckDepth;
static float atmo_env;
static float atmo_duckLast = 1.f;

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
	atmo_duckLast = 1.f;
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

//LICHOMAT v0.4: the peak follower of the voice that ducks the reverb (plan D40); mixer_decimateBlock()
//calls it for each voice's block, before FxBus_process() in the same audio block
void atmo_duckSense(uint8_t voiceNr, const int16_t *block)
{
	if((atmo_duckSrc == 0) || (voiceNr != atmo_duckSrc - 1))
		return;
	int32_t peak = 0;
	for(uint32_t i = 0; i < OUTPUT_DMA_SIZE; i++)
	{
		const int32_t a = (block[i] < 0) ? -block[i] : block[i];
		if(a > peak)
			peak = a;
	}
	const float p = (float)peak / 32768.f;
	atmo_env = (p > atmo_env) ? p : atmo_env * 0.982f;	/* 0.982 per block: an 80 ms release */
}

float atmo_duckGain(void)
{
	const float e = 2.f * atmo_env;
	return 1.f - atmo_duckDepth * ((e < 1.f) ? e : 1.f);
}

float atmo_duckEnv(void)
{
	return atmo_env;
}

uint8_t atmo_active(void)
{
	return (atmo_target > 0.f) || atmo_awake;
}

void atmo_process(int16_t *bus, uint32_t frames)
{
	const float target = atmo_target;
	const float duck = atmo_duckGain();	/* plan D40: 1 with the source off */
	const float duckLast = atmo_duckLast;
	atmo_duckLast = duck;
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
	/* the gain (level x duck, plan D40) ramps linearly across the block; the float limit keeps the
	 * conversion defined */
	const float g0 = atmo_gain * duckLast * 32768.f;
	const float dg = (target * duck - atmo_gain * duckLast) * 32768.f / (float)frames;
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
	case PAR_REV_FREEZE:
		atmo_par[ATMO_REV_FREEZE] = v;
		{	/* plan A8: on from 1 (DTYPE_ON_OFF sends 0 or 1, NRPN 99 0..127) */
			const uint32_t primask = __get_PRIMASK();
			__disable_irq();
			Reverb_setFreeze(&atmo_reverb, atmo_u7(v) >= 1);
			__set_PRIMASK(primask);
		}
		break;
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
		if(paramNr == PAR_REV_DUCK_SRC)
		{	/* plan D40: off dr1 dr2 dr3 snr cym hat; off resets the follower */
			const uint8_t src = atmo_u7(v);
			atmo_duckSrc = (src > 6) ? 6 : src;
			if(atmo_duckSrc == 0)
				atmo_env = 0.f;
		}
		else if(paramNr == PAR_REV_DUCK_DEPTH)
			atmo_duckDepth = atmo_u7(v) / 127.f;
		/* plan D36: size, tone, predelay and modulation reach the reverb */
		if(paramNr == PAR_REV_SIZE)
			atmo_setReverb(Reverb_setSize, v);
		else if(paramNr == PAR_REV_TONE)
			atmo_setReverb(Reverb_setTone, v);	/* tracker Q31: 64 is fw 1.7's composition 64/127 */
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

void atmo_toggleFreeze(void)
{
	activeParameters[PAR_REV_FREEZE] = activeParameters[PAR_REV_FREEZE] ? 0 : 1;
	atmo_setParam(PAR_REV_FREEZE, activeParameters[PAR_REV_FREEZE] / 127.f);
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
