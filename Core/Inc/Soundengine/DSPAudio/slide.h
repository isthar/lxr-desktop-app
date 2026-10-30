/*
 * slide.h
 *
 * The slide (glide) of Drum 1-3 in LICHOMAT v0.3 (plan D24, D25): a pitch
 * factor per 64-sample audio block that starts at the previous note and
 * reaches 1 (the new note) linearly in semitones over the voice's slide time.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef SLIDE_H_
#define SLIDE_H_
#include <math.h>
#include <stdint.h>

#define SLIDE_RATE		44100u	/* the audio sample rate (SYNTH_FS, config.h) */
#define SLIDE_BLOCK		64u	/* samples per control block: OUTPUT_DMA_SIZE (config.h) */
#define SLIDE_MS_PER_VALUE	4u	/* PAR_SLIDE_TIMEx 0..127 -> 0..508 ms */

typedef struct
{
	float		semis;	/* the offset of the next block from the new note */
	float		step;	/* semitones per block */
	uint16_t	left;	/* blocks to go; 0 = no glide */
} slide_t;

/** the blocks of a slide time value, rounded */
static inline uint16_t slide_blocks(uint8_t value)
{
	const uint32_t samples = (uint32_t)value * SLIDE_MS_PER_VALUE * SLIDE_RATE / 1000u;
	return (uint16_t)((samples + SLIDE_BLOCK / 2u) / SLIDE_BLOCK);
}

static inline void slide_stop(slide_t *s)
{
	s->semis = 0.0f;
	s->step = 0.0f;
	s->left = 0;
}

/** the offset in semitones of the glide in progress, 0 without one */
static inline float slide_offset(const slide_t *s)
{
	return s->left ? s->semis : 0.0f;
}

/** a glide from semis (the previous pitch minus the new note) to 0 over blocks */
static inline void slide_start(slide_t *s, float semis, uint16_t blocks)
{
	if (blocks == 0 || fabsf(semis) < 1e-6f)
	{
		slide_stop(s);
		return;
	}
	s->semis = semis;
	s->step = semis / (float)blocks;
	s->left = blocks;
}

/** the pitch factor of the next block */
static inline float slide_next(slide_t *s)
{
	if (s->left == 0)
		return 1.0f;
	/* 2^(semis/12) with expf: the emulator vendors expf, not exp2f (M19) */
	const float f = expf(s->semis * (0.69314718f / 12.0f));
	s->semis -= s->step;
	if (--s->left == 0)
		s->semis = 0.0f;
	return f;
}
#endif
