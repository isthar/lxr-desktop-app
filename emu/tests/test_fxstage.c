/*
 * test_fxstage.c
 *
 * v0.4 review of plan D32 (the slot is unchanged, the reverb only adds): the slot
 * effect sees the destination's channel count whether the reverb is on or not. With
 * a mono destination v0.3 runs the slot with one channel, and the compressor's
 * envelope follows the left input only; its stereo branch follows (L+R)/2, so a
 * stage that ran the slot in stereo made the reverb level change how the compressor
 * reacts. The FX bus on the compressor (fast attack, threshold -38 dB, ratio 8),
 * a stereo input whose right channel differs from its left (a steady sine on the
 * left, bursts on the right), FxBus_process() for 200 blocks with the reverb off,
 * then again with the reverb at level 127 and frozen on a tank its wake cleared:
 * frozen, its input gain is 0, so its wet is exactly 0 and the outputs must be equal
 * bit for bit. On DAC1 L (mono) and on DAC1 (stereo).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "stm32f7xx_hal.h"
#include "Parameters.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/FX/atmo.h"
#include "emu_test.h"

static void bootNoCard(void)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	emu_testBoot(&cfg);
	emu_testRunMs(100);
}

#define FS_BLOCKS 200
#define FS_FRAMES 64

/* [routing][reverb off, on]: DAC1's interleaved frames of every block */
static int16_t fsOut[2][2][FS_BLOCKS * FS_FRAMES * 2];
/* the left input, to see that the compressor works on it */
static int16_t fsDryL[FS_BLOCKS * FS_FRAMES];

static void fsInput(uint32_t b, int16_t *fx)
{
	for (uint32_t i = 0; i < FS_FRAMES; i++) {
		const uint32_t n = b * FS_FRAMES + i;
		const float t = (float)n / 44140.f;
		fx[2 * i] = (int16_t)(16000.f * sinf(6.2831853f * 110.f * t));
		/* bursts of 50 ms on the right, loud and quiet in turn */
		fx[2 * i + 1] = (int16_t)((((n / 2207u) & 1u) ? 30000.f : 300.f) * sinf(6.2831853f * 170.f * t));
		fsDryL[n] = fx[2 * i];
	}
}

static void fsRender(uint8_t routing, int16_t *dst)
{
	int16_t fx[FS_FRAMES * 2], dac2[FS_FRAMES * 2], dac1[FS_FRAMES * 2];
	fxBus.audioRouting = routing;
	fxBus.compressor.env = 0.0;
	for (uint32_t b = 0; b < FS_BLOCKS; b++) {
		fsInput(b, fx);
		memset(dac2, 0, sizeof dac2);
		memset(dac1, 0, sizeof dac1);
		FxBus_process(fx, dac2, dac2 + 1, dac1, dac1 + 1, FS_FRAMES);
		memcpy(&dst[b * FS_FRAMES * 2], dac1, sizeof dac1);
	}
}

static void fsScenario(void *arg)
{
	(void)arg;
	static const uint8_t routings[2] = { MIXER_ROUTING_DAC1_L, MIXER_ROUTING_DAC1_STEREO };
	fxBus.fxType = FX_COMPRESSOR;
	fxBus.dryWet = 1.f;
	fxBus.parameter[0] = 0.f;	/* attack 0.03 ms */
	fxBus.parameter[1] = 0.3f;	/* release 77.5 ms */
	fxBus.parameter[2] = 0.6f;	/* threshold -38.4 dB */
	fxBus.parameter[3] = 0.f;	/* make-up gain 1 */
	FxBus_updateParameter();
	fxBus.compressor.ratio = 0.125f;
	atmo_setParam(PAR_REV_FREEZE, 0.f);
	atmo_setParam(PAR_REV_LEVEL, 0.f);
	for (int r = 0; r < 2; r++)
		fsRender(routings[r], fsOut[r][0]);
	atmo_setParam(PAR_REV_FREEZE, 1 / 127.f);
	atmo_setParam(PAR_REV_LEVEL, 1.f);
	for (int r = 0; r < 2; r++)
		fsRender(routings[r], fsOut[r][1]);
	atmo_setParam(PAR_REV_LEVEL, 0.f);
	atmo_setParam(PAR_REV_FREEZE, 0.f);
}

TEST(M24_slot_channels_with_reverb)
{
	bootNoCard();
	ASSERT_EQ(atmo_active(), 0);
	emu_directCall(fsScenario, NULL);
	for (int r = 0; r < 2; r++) {
		uint32_t differ = 0, compressed = 0;
		for (uint32_t k = 0; k < FS_BLOCKS * FS_FRAMES * 2; k++)
			differ += (fsOut[r][0][k] != fsOut[r][1][k]);
		for (uint32_t n = 0; n < FS_BLOCKS * FS_FRAMES; n++)
			compressed += (fsOut[r][0][2 * n] != fsDryL[n]);
		printf("M24: %s: %u of %u samples differ with the reverb on (frozen, wet 0); the compressor changed %u of %u left samples\n",
				r ? "DAC1 stereo" : "DAC1 L (mono)", (unsigned)differ, (unsigned)(FS_BLOCKS * FS_FRAMES * 2),
				(unsigned)compressed, (unsigned)(FS_BLOCKS * FS_FRAMES));
		ASSERT_TRUE(compressed > FS_BLOCKS * FS_FRAMES / 2);
		ASSERT_EQ(differ, 0u);
	}
}

TEST_MAIN()
