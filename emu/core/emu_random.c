/*
 * emu_random.c
 *
 * The random streams of the LICHOMAT emulator (stage 1b spec 4.9). On the
 * device, GetRngValue() reads the hardware TRNG. Here HAL_RNG_GetRandomNumber()
 * returns the DSP stream (the noise oscillators, the LFO noise), and the
 * sequencer's probability draw takes the sequencer stream through SEQ_RANDOM()
 * (firmware change F5), so that a probability edit does not change the noise.
 * Each stream is xorshift32 with shifts 13, 17 and 5. The DSP stream starts from
 * the low 32 bits of SplitMix64(seed), the sequencer stream from those of
 * SplitMix64(seed + 1); a start value of 0 becomes 1.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "emu_seq_random.h"
#include "emu_internal.h"

static uint32_t emu_dspState, emu_seqState;

/* the first output of SplitMix64 from state x */
static uint64_t emu_splitMix64(uint64_t x)
{
	uint64_t z = x + 0x9E3779B97F4A7C15ull;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

static uint32_t emu_xorshift32(uint32_t *state)
{
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static uint32_t emu_start(uint64_t seed)
{
	const uint32_t s = (uint32_t)emu_splitMix64(seed);
	return s ? s : 1u;
}

uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef *hrng)
{
	(void)hrng;
	return emu_xorshift32(&emu_dspState);
}

uint32_t emu_seqRandom(void)
{
	return emu_xorshift32(&emu_seqState);
}

void emu_randomReset(const emu_config_t *cfg)
{
	emu_dspState = emu_start(cfg->seed);
	emu_seqState = emu_start(cfg->seed + 1u);
}
