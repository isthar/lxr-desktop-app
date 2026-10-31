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
	 * offset. The stage plays tone 0 as tone 1 (a low-pass at about 930 Hz). */
	int16_t bus[128];
	uint32_t seed = 15007u;
	atmo_init();
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

TEST_MAIN()
