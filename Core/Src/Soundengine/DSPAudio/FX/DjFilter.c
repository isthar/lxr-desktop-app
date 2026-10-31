/*
 * DjFilter.c
 *
 * The LICHOMAT v0.4 DJ filter on the four output channels (output 1 L/R, output 2
 * L/R), after the FX bus (plan D39). The math is in djfilter.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "Soundengine/DSPAudio/FX/djfilter.h"
#include "Soundengine/config.h"	/* REAL_FS */
#include "stm32f7xx_hal.h"	/* __SSAT */

#define DJF_IN_SCALE (1.f / 32768.f)
#define DJF_OUT_SCALE 32768.f

/* the requested position and resonance, and those of djf_c; 64 is flat */
static uint8_t djf_pos = 64, djf_reso = 0;
static uint8_t djf_coefPos = 64, djf_coefReso = 0;
static DjSvfCoef djf_c = { 0.f, 0.f, 0.f, 2.f, DJF_FLAT };
/* output 1 L, output 1 R, output 2 L, output 2 R */
static DjSvfState djf_s[4];

void djFilter_set(uint8_t pos, uint8_t reso)
{
	djf_pos = (pos > 127) ? 127 : pos;
	djf_reso = (reso > 127) ? 127 : reso;
}

static inline int16_t djf_out(float y)
{
	return (int16_t)__SSAT((int32_t)(y * DJF_OUT_SCALE), 16);
}

/* the filter over one block; fade 0: filtered only, 1: from dry to filtered, 2: from
 * filtered to dry, with the weight (i + 1)/frames */
static void djf_run(int16_t *out1, int16_t *out2, uint32_t frames, const DjSvfCoef *c, uint8_t fade)
{
	int16_t *const ch[4] = { &out1[0], &out1[1], &out2[0], &out2[1] };
	const float step = 1.f / (float)frames;
	for(uint8_t k = 0; k < 4; k++)
	{
		int16_t *p = ch[k];
		DjSvfState *s = &djf_s[k];
		if(fade == 0)
		{
			for(uint32_t i = 0; i < frames; i++)
			{
				p[2 * i] = djf_out(djf_tick(s, c, (float)p[2 * i] * DJF_IN_SCALE));
			}
		}
		else
		{
			for(uint32_t i = 0; i < frames; i++)
			{
				const float x = (float)p[2 * i] * DJF_IN_SCALE;
				const float y = djf_tick(s, c, x);
				const float w = (float)(i + 1) * step;
				p[2 * i] = djf_out((fade == 1) ? (x + w * (y - x)) : (y + w * (x - y)));
			}
		}
	}
}

void djFilter_process(int16_t *out1, int16_t *out2, uint32_t frames)
{
	const DjSvfCoef old = djf_c;
	if((djf_pos != djf_coefPos) || (djf_reso != djf_coefReso))
	{
		djf_coef(&djf_c, djf_pos, djf_reso, REAL_FS);
		djf_coefPos = djf_pos;
		djf_coefReso = djf_reso;
	}
	if(old.mode == DJF_FLAT)
	{
		if(djf_c.mode == DJF_FLAT)
			return;	/* flat: not computed, bit for bit */
		/* entering the filter: from its rest state, crossfaded in from the dry signal */
		for(uint8_t k = 0; k < 4; k++)
			djf_s[k].ic1 = djf_s[k].ic2 = 0.f;
		djf_run(out1, out2, frames, &djf_c, 1);
	}
	else if(djf_c.mode == DJF_FLAT)
	{
		/* leaving it: the last coefficients, crossfaded out to the dry signal */
		djf_run(out1, out2, frames, &old, 2);
	}
	else if(djf_c.mode != old.mode)
	{
		/* from one side straight to the other (a p-lock, a CC jump, a square LFO): the output
		 * formula changes, which stepped by up to the signal itself. As the knob does, through
		 * the flat centre: out to the dry signal here, and the new side comes in from flat in
		 * the next block, from its rest state. Two blocks of one filter each, as entering and
		 * leaving cost, not two filters in one block. djf_coefPos 64 makes the next block
		 * compute the new side again, unless the knob is flat by then. */
		djf_run(out1, out2, frames, &old, 2);
		djf_c.mode = DJF_FLAT;
		djf_coefPos = 64;
	}
	else
	{
		djf_run(out1, out2, frames, &djf_c, 0);
	}
}
