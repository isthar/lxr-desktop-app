/*
 * fw17_adapter.h
 *
 * The fw 1.7 timing replica (tests/reference/fw17_replica.c) behind plain
 * types, for T20. The replica declares its own StepTiming, so it cannot share a
 * translation unit with seq_core.h; fw17_adapter.c compiles it on its own.
 *
 * The replica has one pattern of 7 tracks with 64 steps, no shuffle, prob 100,
 * and runs the internal clock branch of fw 1.7 seq_tick() at the TIM2 rate.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef FW17_ADAPTER_H_
#define FW17_ADAPTER_H_

/** one hit of the replica */
typedef struct
{
	int track;
	int step;	/**< the track step the hit belongs to */
	int seqStep;	/**< the step (frame) it fired in, counted from 0 at the start */
	int pulse;	/**< 0..23 within that step */
} fw17_hit_t;

/** Reset: every step off, length 16, flam 0, shift 7 (none), at bpm. With
 * perTrackChoke the choke flags are per track (the fix of D2); otherwise they
 * are global, as in fw 1.7. */
void fw17_reset(unsigned bpm, int perTrackChoke);
/** switch a step on, with its raw StepTiming values (flam 0..15, shift 0..15) */
void fw17_setStep(int track, int step, int flam, int shift);
/** the track length, 1..64 */
void fw17_setLength(int track, int length);
/** Run until step `steps` has begun. Stores up to max hits of the steps before
 * it in out, and returns how many there are. */
int fw17_run(int steps, fw17_hit_t *out, int max);

#endif /* FW17_ADAPTER_H_ */
