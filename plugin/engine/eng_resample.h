/*
 * eng_resample.h
 *
 * The engine's sample-rate conversion (plan E9.2 Task 6): Apple's AudioConverter from
 * 44,140.625 Hz to the host's rate, float32 interleaved, 2 channels or 4 (the engine's: the
 * device's two stereo outputs, plan E9.4), Normal complexity at maximum quality (the app's choice,
 * app_coreaudio.c:332-341), prime method None, so that every input frame gives its output at once
 * and the delay is a constant 48 input frames.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef ENG_RESAMPLE_H_
#define ENG_RESAMPLE_H_

#include <stddef.h>
#include <stdint.h>

#define ENG_RESAMPLE_IN 64u		/**< engine frames per push: one block */
#define ENG_RESAMPLE_OUT 1024u	/**< output frames per pull, more than 64 frames at 384 kHz */
#define ENG_RESAMPLE_CHANNELS 4u	/**< the most channels */

typedef struct eng_resample
{
	void *conv;		/**< AudioConverterRef */
	unsigned channels;
	float in[ENG_RESAMPLE_CHANNELS * ENG_RESAMPLE_IN];
	unsigned have, used;
	float out[ENG_RESAMPLE_CHANNELS * ENG_RESAMPLE_OUT];
} eng_resample_t;

/** channels 2 or 4; 0, or -1 with the problem in err */
int eng_resampleOpen(eng_resample_t *r, double rate, unsigned channels, char *err, size_t n);

/** Convert one block of 64 interleaved frames; fn receives the output frames (any count, also 0)
 * with ctx. Returns 0, or the converter's error. */
int eng_resamplePush(eng_resample_t *r, const float *x, void (*fn)(void *ctx, const float *x, uint32_t frames),
	void *ctx);

void eng_resampleClose(eng_resample_t *r);

#endif /* ENG_RESAMPLE_H_ */
