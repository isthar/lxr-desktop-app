/*
 * test_random.c
 *
 * M13 (stage 1b spec 4.9, 12.1): the two random streams. HAL_RNG_GetRandomNumber()
 * returns the DSP stream and SEQ_RANDOM() the sequencer stream, each xorshift32
 * from the low 32 bits of SplitMix64(seed) and SplitMix64(seed + 1). The first
 * values for seeds 1 and 2 are known, and draws from the sequencer stream leave
 * the DSP stream's draws unchanged.
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
#include "emu_test.h"

static void seedWith(uint64_t seed)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.seed = seed;
	emu_corePowerOn(&cfg);
	emu_randomReset(&cfg);
}

TEST(M13_first_values)
{
	static const uint32_t dsp1[] = { 0x91D9AC0Cu, 0x2F57BA20u, 0xDA691309u, 0xF901AEADu };
	static const uint32_t seq1[] = { 0x3F9350C9u, 0xE4C11B8Cu, 0x31AFF2D4u, 0x3157B0EEu };
	static const uint32_t seq2[] = { 0xB57708F3u, 0xD9411A27u, 0xBAB56385u, 0xCE7CD407u };
	seedWith(1);
	for (int i = 0; i < 4; i++)
		ASSERT_EQ(HAL_RNG_GetRandomNumber(NULL), dsp1[i]);
	for (int i = 0; i < 4; i++)
		ASSERT_EQ(SEQ_RANDOM(), seq1[i]);
	seedWith(2);	/* the DSP stream of seed 2 starts from SplitMix64(2), as the sequencer stream of seed 1 */
	for (int i = 0; i < 4; i++)
		ASSERT_EQ(HAL_RNG_GetRandomNumber(NULL), seq1[i]);
	for (int i = 0; i < 4; i++)
		ASSERT_EQ(SEQ_RANDOM(), seq2[i]);
}

/* a probability edit draws more sequencer numbers; the noise stays the same */
TEST(M13_streams_apart)
{
	uint32_t plain[64], mixed[64];
	seedWith(1);
	for (int i = 0; i < 64; i++)
		plain[i] = HAL_RNG_GetRandomNumber(NULL);
	seedWith(1);
	for (int i = 0; i < 64; i++)
	{
		for (int k = 0; k < i % 5; k++)
			(void)SEQ_RANDOM();
		mixed[i] = HAL_RNG_GetRandomNumber(NULL);
	}
	ASSERT_EQ(memcmp(plain, mixed, sizeof plain), 0);
}

TEST_MAIN()
