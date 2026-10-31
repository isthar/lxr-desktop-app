/*
 * djfilter.h
 *
 * The LICHOMAT v0.4 DJ filter: one knob, a low-pass to the left and a high-pass to
 * the right of a flat centre, on both stereo outputs (plan D39). The math is here,
 * the 4-channel block in DjFilter.c.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef DJFILTER_H_
#define DJFILTER_H_

#include <stdint.h>
#include <math.h>

/** LICHOMAT v0.4: the DJ filter, a TPT state-variable filter (plan D39) */
enum { DJF_FLAT, DJF_LP, DJF_HP };
typedef struct { float ic1, ic2; } DjSvfState;
typedef struct { float a1, a2, a3, k; uint8_t mode; } DjSvfCoef;

static inline void djf_coef(DjSvfCoef *c, uint8_t pos, uint8_t reso, float fs)
{
	float fc;
	if((pos >= 63) && (pos <= 65)) { c->mode = DJF_FLAT; return; }
	/* 2^x as expf(x ln 2) and tan as sinf/cosf: the emulator vendors expf, sinf and cosf,
	 * not exp2f or tanf (M19) */
	if(pos < 63) { c->mode = DJF_LP; fc = 20000.f * expf(-10.f * 0.69314718f * (float)(63 - pos) / 63.f); }
	else         { c->mode = DJF_HP; fc = 20.f * expf(10.f * 0.69314718f * (float)(pos - 65) / 62.f); }
	if(fc > 0.45f * fs)
		fc = 0.45f * fs;
	const float w = 3.14159265f * fc / fs;
	const float g = sinf(w) / cosf(w);
	c->k  = 2.f - 1.8f * (float)reso / 127.f;
	c->a1 = 1.f / (1.f + g * (g + c->k));
	c->a2 = g * c->a1;
	c->a3 = g * c->a2;
}

static inline float djf_tick(DjSvfState *s, const DjSvfCoef *c, float v0)
{
	const float v3 = v0 - s->ic2;
	const float v1 = c->a1 * s->ic1 + c->a2 * v3;
	const float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
	s->ic1 = 2.f * v1 - s->ic1;
	s->ic2 = 2.f * v2 - s->ic2;
	return (c->mode == DJF_LP) ? v2 : (v0 - c->k * v1 - v2);
}

/** LICHOMAT v0.4: the filter's position and resonance, 0..127 (atmo_setParam()) */
void djFilter_set(uint8_t pos, uint8_t reso);
/** filters output 1 and output 2, each an interleaved stereo buffer of frames frames,
 * in place (mixer_calcNextSampleBlock(), after FxBus_process()) */
void djFilter_process(int16_t *out1, int16_t *out2, uint32_t frames);

#endif /* DJFILTER_H_ */
