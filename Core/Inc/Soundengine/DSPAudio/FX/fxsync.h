/*
 * fxsync.h
 *
 * The LICHOMAT v0.4 tempo-synced delay: the delay time of a note division at the
 * current tempo (plan D38).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef FXSYNC_H_
#define FXSYNC_H_

#include <stdint.h>
#include <math.h>

/** LICHOMAT v0.4: tempo-synced delay times (plan D38) */
enum { FXSYNC_OFF, FXSYNC_16T, FXSYNC_16, FXSYNC_8T, FXSYNC_16D, FXSYNC_8, FXSYNC_4T, FXSYNC_8D,
	FXSYNC_4, FXSYNC_4D, FXSYNC_COUNT };

/** the delay in samples of a division at bpm, halved until it is at most maxSamples; 0 = free time */
static inline float fxsync_samples(uint8_t division, float bpm, float fs, float maxSamples)
{
	static const float beats[FXSYNC_COUNT] = { 0.f, 1.f/6, 0.25f, 1.f/3, 0.375f, 0.5f, 2.f/3, 0.75f, 1.f, 1.5f };
	if((division == FXSYNC_OFF) || (division >= FXSYNC_COUNT) || !(bpm > 0.f))
		return 0.f;
	float s = beats[division] * (60.f * fs) / bpm;
	while(s > maxSamples)
		s *= 0.5f;
	return s;
}

/** whether a new synced time differs enough from the current one to re-apply it */
static inline uint8_t fxsync_moved(float current, float next)
{
	return fabsf(next - current) > (0.001f * next + 0.5f);
}

#endif /* FXSYNC_H_ */
