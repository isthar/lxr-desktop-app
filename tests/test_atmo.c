/* test_atmo.c: the v0.4 reverb stage (plan A2, A7-A9)
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "seqc_test.h"
#include "Soundengine/DSPAudio/FX/Reverb.h"
#include "Soundengine/DSPAudio/FX/atmo.h"
#include "Soundengine/DSPAudio/FX/djfilter.h"
#include "Parameters.h"

/* plan A7: atmo.c is linked now and defines REVERB_RAM; the stand-ins of what it uses outside
 * the reverb files */
void djFilter_set(uint8_t pos, uint8_t reso) { (void)pos; (void)reso; }
uint8_t fxBus_delaySync;
uint8_t activeParameters[NUM_PARAMS];

#define RV_UNIT(u) (&REVERB_RAM[(u) * MAX_ALLPASS_SIZE])

static int rv_unitWritten(const __fp16 *unit)
{
	for(int i = 0; i < MAX_ALLPASS_SIZE; i++)
		if(unit[i] != 0)
			return 1;
	return 0;
}

TEST(RV1_every_unit_carries_signal)
{
	/* fw 1.7's loop 1 ran its allpass through apDelay[1], loop 2's unit (Reverb.c:375), so
	 * unit 0 (apDelay[0]) was never written although an output tap reads it. A pulse starts
	 * every block: the early reflections (unit 12) and the predelay (unit 13) are plain
	 * delays of about 2,000 samples, so a single pulse leaves them all zero again. */
	static Reverb r;
	int16_t in[2 * 64], out[2 * 64];
	memset(REVERB_RAM, 0, sizeof REVERB_RAM);
	Reverb_init(&r);
	Reverb_setFeedback(&r, 0.8f);
	Reverb_setComposition(&r, 0.5f);
	for(int b = 0; b < 700; b++)	/* about one second */
	{
		memset(in, 0, sizeof in);
		memset(out, 0, sizeof out);
		in[0] = in[1] = 16000;
		Reverb_process(&r, in, out, out + 1, 64, 2, 1.0f);
	}
	for(int u = 0; u < 14; u++)
		ASSERT_EQ(rv_unitWritten(RV_UNIT(u)), 1);
}

/* the number of non-finite outputs of rv_t60() and rv_corners(); the ASSERT macros return
 * from a void function, so the helpers count and the tests check */
static int rv_nonFinite;

static float rv_t60(uint8_t decim, uint8_t decay)
{
	/* the time the 50 ms energy of the wet L falls 30 dB below its loudest window, times 2.
	 * The early reflections are off: they are four plain copies of the pulse (gains 1, 0.5,
	 * 0.25, 0.125) within 25 ms, and their window is 10 dB above the late tail's loudest,
	 * so with them the measure gives 1.30 s (half rate 1.40 s) for any slope of the tail */
	static Reverb r;
	int16_t in[128];
	float wl[64], wr[64];
	double win[80] = {0};
	memset(REVERB_RAM, 0, sizeof REVERB_RAM);
	Reverb_init(&r);
	Reverb_setDecim(&r, decim);
	Reverb_setSize(&r, 1.f);
	Reverb_setDecay(&r, decay / 127.f);
	Reverb_setComposition(&r, 0.7f);
	Reverb_setVariation(&r, 0.f);	/* the early reflections' level */
	for(int b = 0; b < 2759; b++)	/* 4 s */
	{
		memset(in, 0, sizeof in);
		if(b == 0) in[0] = in[1] = 20000;
		Reverb_processWet(&r, in, wl, wr, 64);
		for(int i = 0; i < 64; i++)
		{
			rv_nonFinite += !(isfinite(wl[i]) && isfinite(wr[i]));
			win[(b * 64 + i) / 2207 < 80 ? (b * 64 + i) / 2207 : 79] += wl[i] * wl[i];
		}
	}
	int top = 0;
	for(int w = 1; w < 80; w++) if(win[w] > win[top]) top = w;
	for(int w = top; w < 80; w++)
		if(win[w] < win[top] * 0.001) return 2.f * 0.05f * (float)(w - top);
	return 99.f;
}

/* the 16 corners of size, decay, tone and mod at 0 and 127, 0.5 s of noise each: every output
 * finite and |wet| < 4 (plan A7); returns the largest |wet| */
static float rv_corners(uint8_t decim)
{
	static Reverb r;
	int16_t in[128];
	float wl[64], wr[64];
	float top = 0.f;
	uint32_t seed = 15000u + decim;
	for(int c = 0; c < 16; c++)
	{
		memset(REVERB_RAM, 0, sizeof REVERB_RAM);
		Reverb_init(&r);
		Reverb_setDecim(&r, decim);
		Reverb_setSize(&r, (c & 1) ? 1.f : 0.f);
		Reverb_setDecay(&r, (c & 2) ? 1.f : 0.f);
		Reverb_setComposition(&r, (c & 4) ? 1.f : 0.f);
		Reverb_setMod(&r, (c & 8) ? 1.f : 0.f);
		for(int b = 0; b < 345; b++)	/* 0.5 s */
		{
			for(int i = 0; i < 128; i++)
			{
				seed = seed * 1664525u + 1013904223u;
				in[i] = (int16_t)((int32_t)(seed >> 16) - 32768);
			}
			Reverb_processWet(&r, in, wl, wr, 64);
			for(int i = 0; i < 64; i++)
			{
				rv_nonFinite += !(isfinite(wl[i]) && isfinite(wr[i]));
				top = fmaxf(top, fmaxf(fabsf(wl[i]), fabsf(wr[i])));
			}
		}
	}
	return top;
}

TEST(RV2_decay_is_rt60)
{
	/* decay 58: RT60 = 0.3 * 2^(6*58/127) = 2.0 s (plan D36), within 30 % */
	rv_nonFinite = 0;
	const float t = rv_t60(1, 58);
	printf("RV2: full rate, decay 58: T60 %.2f s\n", (double)t);
	ASSERT_EQ(rv_nonFinite, 0);
	ASSERT_EQ((t > 1.4f) && (t < 2.6f), 1);
}

TEST(RV2_corners_stay_bounded)
{
	rv_nonFinite = 0;
	const float top = rv_corners(1);
	printf("RV2: full rate, 16 corners: largest |wet| %.3f\n", (double)top);
	ASSERT_EQ(rv_nonFinite, 0);
	ASSERT_TRUE(top < 4.f);
}

TEST(RV2_tone_0_leaves_no_offset)
{
	/* fw 1.7's composition 0 sets both tilt EQs to a low-pass at 0 Hz (a0 0, b1 1), which holds
	 * its last output for ever: tone 0 during a tail would leave the wet signal at a constant
	 * offset. A7 played tone 0 as tone 1 (a low-pass at about 930 Hz); since tracker Q31 tone 0 is
	 * Reverb_setTone()'s one-pole low-pass at 1.5 kHz, and the test still holds. */
	int16_t bus[128];
	uint32_t seed = 15007u;
	atmo_init();
	/* tracker Q31: decay 0 (RT60 0.3 s), so that 2.5 s later only an offset can be left. At the
	 * default decay 64 (2.4 s) the new tone 0 (loop damping 0.25, A7's tone 1 had 0.008) leaves
	 * 1.11 LSB RMS of plain tail there; the limit stays 1 LSB */
	atmo_setParam(PAR_REV_DECAY, 0.f);
	atmo_setParam(PAR_REV_LEVEL, 1.f);
	atmo_setParam(PAR_REV_TONE, 0.25f);
	for(int b = 0; b < 352; b++)	/* the wake-up, then 0.5 s of noise */
	{
		for(int i = 0; i < 128; i++)
		{
			seed = seed * 1664525u + 1013904223u;
			bus[i] = (int16_t)(((int32_t)(seed >> 16) - 32768) / 4);
		}
		atmo_process(bus, 64);
	}
	atmo_setParam(PAR_REV_TONE, 0.f);
	double e = 0.;
	for(int b = 0; b < 2069; b++)	/* 3 s of silence, the last 0.5 s measured */
	{
		memset(bus, 0, sizeof bus);
		atmo_process(bus, 64);
		if(b >= 1724)
			for(int i = 0; i < 128; i++)
				e += (double)bus[i] * bus[i];
	}
	const double rms = sqrt(e / (128. * 345.));
	printf("RV2: tone 0 after noise: bus RMS %.2f LSB after 2.5 s\n", rms);
	ASSERT_TRUE(rms < 1.0);
	atmo_setParam(PAR_REV_LEVEL, 0.f);
}

TEST(RV2_size_and_mod_reach_the_mod_delays)
{
	/* fw 1.7's modulated allpasses smooth their read position with setOnePoleFreq(0.00013), a
	 * time constant of about 20 minutes, so they start near 0 samples and neither the size nor
	 * the LFO reaches them. Plan D36 needs both. */
	static Reverb r;
	int16_t in[128] = {0};
	float wl[64], wr[64];
	memset(REVERB_RAM, 0, sizeof REVERB_RAM);
	Reverb_init(&r);
	Reverb_setSize(&r, 1.f);
	Reverb_setMod(&r, 0.f);
	for(int b = 0; b < 69; b++)	/* 0.1 s */
		Reverb_processWet(&r, in, wl, wr, 64);
	ASSERT_NEAR(r.apModDelay[0].delay.lp.b, 672.0, 1.0);
	ASSERT_NEAR(r.apModDelay[1].delay.lp.b, 908.0, 1.0);
	Reverb_setSize(&r, 0.f);	/* s = 0.4 */
	for(int b = 0; b < 69; b++)
		Reverb_processWet(&r, in, wl, wr, 64);
	ASSERT_NEAR(r.apModDelay[0].delay.lp.b, 0.4 * 672.0, 1.0);
	ASSERT_NEAR(r.apModDelay[1].delay.lp.b, 0.4 * 908.0, 1.0);
	/* modulation 1: the LFO at 1.5 Hz, 2 s are three periods */
	Reverb_setSize(&r, 1.f);
	Reverb_setMod(&r, 1.f);
	double lo = 1e9, hi = -1e9;
	for(int b = 0; b < 1379; b++)
	{
		Reverb_processWet(&r, in, wl, wr, 64);
		if(b >= 69)
		{
			lo = fmin(lo, r.apModDelay[0].delay.lp.b);
			hi = fmax(hi, r.apModDelay[0].delay.lp.b);
		}
	}
	printf("RV2: modulation 1: apModDelay[0] position %.1f..%.1f\n", lo, hi);
	ASSERT_TRUE(hi - lo > 16.0);
	ASSERT_TRUE(hi - lo < 40.0);
	ASSERT_TRUE(lo > 600.0 && hi < 750.0);
}

TEST(RV2_level_wakes_on_a_clean_buffer)
{
	/* plan A7: level 0 is not computed; waking clears REVERB_RAM, 8,192 B per block for 7
	 * blocks, with the gain held at 0, then the wet signal reaches the bus */
	int16_t bus[128];
	atmo_init();
	ASSERT_EQ(atmo_active(), 0);
	for(int i = 0; i < REVERB_UNITS * MAX_ALLPASS_SIZE; i++)
		REVERB_RAM[i] = (__fp16)0.25f;	/* a stale tail */
	atmo_setParam(PAR_REV_LEVEL, 1.f);
	ASSERT_EQ(atmo_active(), 1);
	for(int b = 0; b < 7; b++)
	{
		memset(bus, 0, sizeof bus);
		atmo_process(bus, 64);
		for(int i = 0; i < 128; i++)
			ASSERT_EQ(bus[i], 0);
	}
	int stale = 0;
	for(int i = 0; i < REVERB_UNITS * MAX_ALLPASS_SIZE; i++)
		stale += (REVERB_RAM[i] != 0);
	ASSERT_EQ(stale, 0);
	/* a pulse now comes back as a tail on both channels */
	int32_t sumL = 0, sumR = 0;
	for(int b = 0; b < 100; b++)
	{
		memset(bus, 0, sizeof bus);
		if(b == 0) bus[0] = bus[1] = 20000;
		atmo_process(bus, 64);
		for(int i = (b == 0) ? 1 : 0; i < 64; i++)
		{
			sumL += abs(bus[2 * i]);
			sumR += abs(bus[2 * i + 1]);
		}
	}
	ASSERT_TRUE(sumL > 0 && sumR > 0);
	/* level 0: one block ramps the gain down, then the stage is off */
	atmo_setParam(PAR_REV_LEVEL, 0.f);
	ASSERT_EQ(atmo_active(), 1);
	memset(bus, 0, sizeof bus);
	atmo_process(bus, 64);
	ASSERT_EQ(atmo_active(), 0);
}

/* plan A8: 0.5 s of noise at decay 64, then freeze. The RMS of the wet L in 1.0-1.5 s is E1, in
 * 10.0-10.5 s E2; unfrozen at 10.5 s, 13.0-13.5 s is E3. Returns 10 log10(E2/E1) and, in *after,
 * 10 log10(E3/E1); counts non-finite outputs in rv_nonFinite */
static double rv_freeze(uint8_t decim, double *after)
{
	static Reverb r;
	int16_t in[128];
	float wl[64], wr[64];
	double e[3] = {0};
	const uint32_t fs = (uint32_t)REAL_FS;
	const uint32_t from[3] = { fs, 10 * fs, 13 * fs };
	uint32_t seed = 16000u + decim;
	memset(REVERB_RAM, 0, sizeof REVERB_RAM);
	Reverb_init(&r);
	Reverb_setDecim(&r, decim);
	Reverb_setSize(&r, 100 / 127.f);	/* the D35 defaults */
	Reverb_setDecay(&r, 64 / 127.f);
	Reverb_setComposition(&r, 64 / 127.f);
	Reverb_setMod(&r, 40 / 127.f);
	for(uint32_t b = 0; b < (27 * fs / 2) / 64 + 1; b++)	/* 13.5 s */
	{
		const uint32_t n0 = b * 64;
		if(n0 < fs / 2)
			for(int i = 0; i < 128; i++)
			{
				seed = seed * 1664525u + 1013904223u;
				in[i] = (int16_t)(((int32_t)(seed >> 16) - 32768) / 4);
			}
		else
			memset(in, 0, sizeof in);
		if(n0 == (fs / 2 + 63) / 64 * 64)
			Reverb_setFreeze(&r, 1);
		if(n0 == (21 * fs / 2 + 63) / 64 * 64)
			Reverb_setFreeze(&r, 0);
		Reverb_processWet(&r, in, wl, wr, 64);
		for(uint32_t i = 0; i < 64; i++)
		{
			rv_nonFinite += !(isfinite(wl[i]) && isfinite(wr[i]));
			for(int w = 0; w < 3; w++)
				if((n0 + i >= from[w]) && (n0 + i < from[w] + fs / 2))
					e[w] += (double)wl[i] * wl[i];
		}
	}
	*after = 10. * log10(e[2] / e[0]);
	return 10. * log10(e[1] / e[0]);
}

TEST(RV3_freeze_holds)
{
	for(uint8_t decim = 1; decim <= 2; decim++)
	{
		double after;
		rv_nonFinite = 0;
		const double drift = rv_freeze(decim, &after);
		printf("RV3: decim %u: frozen 1.0-1.5 s -> 10.0-10.5 s %+.3f dB, unfrozen 13.0-13.5 s %+.1f dB\n",
				decim, drift, after);
		ASSERT_EQ(rv_nonFinite, 0);
		ASSERT_TRUE((drift > -1.5) && (drift < 0.5));
		ASSERT_TRUE(after < -20.);
	}
}

/* the summed |bus - input| of 200 blocks of noise through atmo_process(), after the wake-up */
static int32_t rv_stageWet(uint32_t *seed)
{
	int16_t bus[128], dry[128];
	int32_t wet = 0;
	for(int k = 0; k < 207; k++)
	{
		for(int i = 0; i < 128; i++)
		{
			*seed = *seed * 1664525u + 1013904223u;
			bus[i] = dry[i] = (int16_t)(((int32_t)(*seed >> 16) - 32768) / 4);
		}
		atmo_process(bus, 64);
		for(int i = 0; i < 128; i++)
			wet += abs(bus[i] - dry[i]);
	}
	return wet;
}

TEST(RV3_toggle_and_nrpn_99_freeze)
{
	/* plan A8: atmo_toggleFreeze() flips activeParameters[PAR_REV_FREEZE] 0/1 and applies it;
	 * atmo_setParam(PAR_REV_FREEZE) (NRPN 99, the menu) freezes from 1/127. Frozen, the input
	 * does not reach the reverb (inGain 0), so a clean reverb adds nothing to the bus */
	uint32_t seed = 16011u;
	atmo_init();
	activeParameters[PAR_REV_FREEZE] = 0;
	atmo_toggleFreeze();
	ASSERT_EQ(activeParameters[PAR_REV_FREEZE], 1);
	atmo_setParam(PAR_REV_LEVEL, 1.f);	/* wakes on a cleared buffer */
	ASSERT_EQ(rv_stageWet(&seed), 0);
	atmo_toggleFreeze();
	ASSERT_EQ(activeParameters[PAR_REV_FREEZE], 0);
	ASSERT_TRUE(rv_stageWet(&seed) > 0);
	atmo_setParam(PAR_REV_LEVEL, 0.f);
	(void)rv_stageWet(&seed);		/* off */

	atmo_setParam(PAR_REV_FREEZE, 1 / 127.f);
	atmo_setParam(PAR_REV_LEVEL, 1.f);
	ASSERT_EQ(rv_stageWet(&seed), 0);
	atmo_setParam(PAR_REV_FREEZE, 0.f);
	ASSERT_TRUE(rv_stageWet(&seed) > 0);
	atmo_setParam(PAR_REV_LEVEL, 0.f);
	(void)rv_stageWet(&seed);
}

TEST(RV4_half_rate_keeps_rt60)
{
	rv_nonFinite = 0;
	const float t = rv_t60(2, 58);
	printf("RV4: half rate, decay 58: T60 %.2f s\n", (double)t);
	ASSERT_EQ(rv_nonFinite, 0);
	ASSERT_EQ((t > 1.4f) && (t < 2.6f), 1);
}

TEST(RV4_corners_stay_bounded)
{
	rv_nonFinite = 0;
	const float top = rv_corners(2);
	printf("RV4: half rate, 16 corners: largest |wet| %.3f\n", (double)top);
	ASSERT_EQ(rv_nonFinite, 0);
	ASSERT_TRUE(top < 4.f);
}

TEST(DK1_follower_and_gain)
{
	/* plan D40: env = max(peak, env * 0.982) on the chosen voice's block, g = 1 - depth min(1, 2 env) */
	int16_t blk[64];
	atmo_setParam(PAR_REV_DUCK_SRC, 0.f);	/* off: the follower at 0 */
	atmo_setParam(PAR_REV_DUCK_SRC, 1 / 127.f);	/* dr1 = voice 0 */
	atmo_setParam(PAR_REV_DUCK_DEPTH, 1.f);
	memset(blk, 0, sizeof blk);
	blk[10] = -26214;	/* peak 0.8 */
	atmo_duckSense(1, blk);	/* another voice: no change */
	ASSERT_EQ(atmo_duckEnv() == 0.f, 1);
	atmo_duckSense(0, blk);
	ASSERT_EQ(fabsf(atmo_duckEnv() - 0.8f) < 0.001f, 1);
	ASSERT_EQ(atmo_duckGain() == 0.f, 1);	/* 1 - 1 * min(1, 1.6) */
	memset(blk, 0, sizeof blk);
	for(int b = 0; b < 55; b++) atmo_duckSense(0, blk);	/* 80 ms */
	ASSERT_EQ((atmo_duckEnv() > 0.28f) && (atmo_duckEnv() < 0.31f), 1);
	ASSERT_EQ(fabsf(atmo_duckGain() - (1.f - 2.f * atmo_duckEnv())) < 0.001f, 1);
	/* the hat (6) is voice 5; source 0 resets the follower */
	atmo_setParam(PAR_REV_DUCK_SRC, 6 / 127.f);
	blk[3] = 32767;
	atmo_duckSense(4, blk);
	ASSERT_TRUE(atmo_duckEnv() < 0.31f);
	atmo_duckSense(5, blk);
	ASSERT_TRUE(atmo_duckEnv() > 0.99f);
	atmo_setParam(PAR_REV_DUCK_SRC, 0.f);
	ASSERT_EQ(atmo_duckEnv() == 0.f, 1);
	ASSERT_EQ(atmo_duckGain() == 1.f, 1);
}

TEST(DK1_ducks_the_wet_across_a_block)
{
	/* plan D40: atmo_process() ramps the wet gain linearly from the last block's duck gain to the
	 * new one; depth 1 and a held peak duck the wet to 0 from the block after the ramp */
	int16_t bus[128], dry[128], blk[64];
	uint32_t seed = 16021u;
	atmo_init();
	atmo_setParam(PAR_REV_DUCK_SRC, 1 / 127.f);
	atmo_setParam(PAR_REV_DUCK_DEPTH, 1.f);
	atmo_setParam(PAR_REV_LEVEL, 1.f);
	for(int b = 0; b < 300; b++)	/* the wake-up, then noise: a tail */
	{
		for(int i = 0; i < 128; i++)
		{
			seed = seed * 1664525u + 1013904223u;
			bus[i] = (int16_t)(((int32_t)(seed >> 16) - 32768) / 4);
		}
		atmo_process(bus, 64);
	}
	memset(blk, 0, sizeof blk);
	blk[0] = 16384;	/* peak 0.5: g = 0 */
	int wet[3] = {0};
	for(int b = 0; b < 3; b++)
	{
		if(b == 0)
			atmo_duckSense(0, blk);
		memset(bus, 0, sizeof bus);
		memset(dry, 0, sizeof dry);
		atmo_process(bus, 64);
		for(int i = 0; i < 128; i++)
			wet[b] += abs(bus[i] - dry[i]);
	}
	printf("DK1: the tail's |wet| per block while ducked: %d (ramp), %d, %d\n", wet[0], wet[1], wet[2]);
	ASSERT_TRUE(wet[0] > 0);
	ASSERT_EQ(wet[1], 0);
	ASSERT_EQ(wet[2], 0);
	atmo_setParam(PAR_REV_DUCK_DEPTH, 0.f);	/* depth 0: the tail again */
	int back = 0;
	for(int b = 0; b < 2; b++)
	{
		memset(bus, 0, sizeof bus);
		atmo_process(bus, 64);
		for(int i = 0; i < 128; i++)
			back += abs(bus[i]);
	}
	ASSERT_TRUE(back > wet[0]);
	atmo_setParam(PAR_REV_DUCK_SRC, 0.f);
	atmo_setParam(PAR_REV_LEVEL, 0.f);
	memset(bus, 0, sizeof bus);
	atmo_process(bus, 64);
}

/* tracker Q31: the wet level (dB, L and R) of a reverb at the D35 defaults and the given tone, fed
 * for 3 s with pink noise (sine 0) or a sine of hz at -12 dBFS, measured over 1.5-3.0 s. Pink:
 * Kellet's filter on the LCG's white noise, about -14 dBFS RMS. Counts non-finite outputs. */
static double rv_toneLevel(uint8_t decim, uint8_t tone, float hz)
{
	static Reverb r;
	int16_t in[128];
	float wl[64], wr[64];
	double e = 0.;
	float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, h1 = 0, h2 = 0;
	uint32_t seed = 16031u;
	const uint32_t fs = (uint32_t)REAL_FS;
	memset(REVERB_RAM, 0, sizeof REVERB_RAM);
	memset(&r, 0, sizeof r);	/* Reverb_init() leaves the filters' states as they were */
	Reverb_init(&r);
	Reverb_setDecim(&r, decim);
	Reverb_setSize(&r, 100 / 127.f);
	Reverb_setDecay(&r, 64 / 127.f);
	Reverb_setMod(&r, 40 / 127.f);
	Reverb_setTone(&r, tone / 127.f);
	for(uint32_t b = 0; b < 3 * fs / 64; b++)
	{
		for(int i = 0; i < 64; i++)
		{
			float x;
			if(hz > 0.f)
				x = 0.25f * sinf(6.2831853f * hz * (float)(b * 64 + (uint32_t)i) / REAL_FS);
			else
			{
				seed = seed * 1664525u + 1013904223u;
				const float w = ((float)(seed >> 8) / 8388608.f - 1.f);
				b0 = 0.99886f * b0 + w * 0.0555179f;
				b1 = 0.99332f * b1 + w * 0.0750759f;
				b2 = 0.96900f * b2 + w * 0.1538520f;
				b3 = 0.86650f * b3 + w * 0.3104856f;
				b4 = 0.55000f * b4 + w * 0.5329522f;
				b5 = -0.7616f * b5 - w * 0.0168980f;
				x = 0.11f * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f);
				b6 = w * 0.115926f;
				/* two one-pole high-passes at 20 Hz: pink noise of 20 Hz-20 kHz */
				h1 += 0.0028429f * (x - h1);
				x -= h1;
				h2 += 0.0028429f * (x - h2);
				x -= h2;
			}
			const int32_t v = (int32_t)(x * 32767.f);
			in[2 * i] = in[2 * i + 1] = (int16_t)((v > 32767) ? 32767 : (v < -32768) ? -32768 : v);
		}
		Reverb_processWet(&r, in, wl, wr, 64);
		for(int i = 0; i < 64; i++)
		{
			rv_nonFinite += !(isfinite(wl[i]) && isfinite(wr[i]));
			if(b * 64 >= 3 * fs / 2)
				e += (double)wl[i] * wl[i] + (double)wr[i] * wr[i];
		}
	}
	return 10. * log10(e / (2. * (3 * fs / 2)));
}

/* tracker Q31: the level at the ends of the tone knob. The bare one-pole filters at the asked
 * corners cost 2.05 dB (low-pass 1.5 kHz) and 2.47 dB (high-pass 400 Hz) of pink noise of
 * 20 Hz-20 kHz, computed; the tank tilts the wet spectrum (its loop damping, an octave lower at half
 * rate) and moves that by up to about 1 dB. So the ends stay within 3.5 dB of tone 64. fw 1.7's
 * mapping gave -4.1 dB at tone 1 and -36.6 dB at tone 127 on the same measure (full rate). */
#define RV5_LEVEL_BOUND 3.5

TEST(RV5_tone_ends_keep_the_level)
{
	for(uint8_t decim = 1; decim <= 2; decim++)
	{
		rv_nonFinite = 0;
		const double mid = rv_toneLevel(decim, 64, 0.f);
		const double lo = rv_toneLevel(decim, 0, 0.f) - mid;
		const double hi = rv_toneLevel(decim, 127, 0.f) - mid;
		printf("RV5: decim %u, pink 20 Hz-20 kHz: tone 64 %.1f dB, tone 0 %+.2f dB, tone 127 %+.2f dB\n",
				decim, mid, lo, hi);
		ASSERT_EQ(rv_nonFinite, 0);
		ASSERT_TRUE(fabs(lo) <= RV5_LEVEL_BOUND);
		ASSERT_TRUE(fabs(hi) <= RV5_LEVEL_BOUND);
	}
}

TEST(RV5_tone_is_a_low_pass_and_a_high_pass)
{
	/* sines at -12 dBFS through the reverb, against tone 64: tone 0 is a one-pole low-pass at
	 * 1.5 kHz (theory: -3 dB at 1.5 kHz, -12.3 dB at 6 kHz, 0 at 200 Hz), tone 127 a one-pole
	 * high-pass at 400 Hz (-3 dB at 400 Hz, -12.3 dB at 100 Hz, 0 at 3 kHz); the tank's comb
	 * response moves a single sine by about a dB */
	static const struct { uint8_t tone; float hz; double lo, hi; } c[] = {
		{ 0, 6000.f, -99., -9. }, { 0, 1500.f, -4.5, -1.5 }, { 0, 200.f, -1., 1. },
		{ 127, 100.f, -99., -9. }, { 127, 400.f, -4.5, -1.5 }, { 127, 3000.f, -1.5, 1.5 },
	};
	for(uint8_t decim = 1; decim <= 2; decim++)
		for(unsigned k = 0; k < sizeof c / sizeof c[0]; k++)
		{
			rv_nonFinite = 0;
			const double d = rv_toneLevel(decim, c[k].tone, c[k].hz) - rv_toneLevel(decim, 64, c[k].hz);
			printf("RV5: decim %u, tone %3u, %5.0f Hz: %+.2f dB\n", decim, c[k].tone, (double)c[k].hz, d);
			ASSERT_EQ(rv_nonFinite, 0);
			ASSERT_TRUE((d >= c[k].lo) && (d <= c[k].hi));
		}
}

TEST(RV5_tone_64_after_a_side_leaves_no_offset)
{
	/* tone 64's output high-pass is at 0.01 Hz: entering it from either side clears the output
	 * filters' state, else that state stays on the bus as an offset (seen failing first without
	 * the clearing: 127 -> 64 left 493.22 LSB RMS) */
	static const uint8_t from[] = { 127, 30 };
	for(unsigned k = 0; k < sizeof from; k++)
	{
		int16_t bus[128];
		uint32_t seed = 16041u;
		atmo_init();
		atmo_setParam(PAR_REV_DECAY, 0.f);	/* RT60 0.3 s: after 2.5 s only an offset is left */
		atmo_setParam(PAR_REV_LEVEL, 1.f);
		atmo_setParam(PAR_REV_TONE, from[k] / 127.f);
		for(int b = 0; b < 352; b++)	/* the wake-up, then 0.5 s of noise */
		{
			for(int i = 0; i < 128; i++)
			{
				seed = seed * 1664525u + 1013904223u;
				bus[i] = (int16_t)(((int32_t)(seed >> 16) - 32768) / 4);
			}
			atmo_process(bus, 64);
		}
		atmo_setParam(PAR_REV_TONE, 64 / 127.f);
		double e = 0.;
		for(int b = 0; b < 2069; b++)	/* 3 s of silence, the last 0.5 s measured */
		{
			memset(bus, 0, sizeof bus);
			atmo_process(bus, 64);
			if(b >= 1724)
				for(int i = 0; i < 128; i++)
					e += (double)bus[i] * bus[i];
		}
		const double rms = sqrt(e / (128. * 345.));
		printf("RV5: tone %u -> 64 after noise: bus RMS %.2f LSB after 2.5 s\n", from[k], rms);
		ASSERT_TRUE(rms < 1.0);
		atmo_setParam(PAR_REV_LEVEL, 0.f);
		atmo_process(bus, 64);
	}
}

TEST_MAIN()
