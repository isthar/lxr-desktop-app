/*
 * test_resample.c
 *
 * P7: the engine's sample-rate conversion (plan E9.2 Task 6): the converter's delay, which
 * plug_soundDelay() reports, and its frame count, at the rates Live uses.
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

#include "eng_resample.h"
#include "plug_time.h"
#include "seqc_test.h"

#define BLOCKS 400u

static float *out;
static uint32_t nout;

static void collect(void *ctx, const float *lr, uint32_t frames)
{
	(void)ctx;
	memcpy(&out[2u * nout], lr, 2u * frames * sizeof *lr);
	nout += frames;
}

/* an impulse at input frame 1,000 through the converter, 64 frames a push: the output's peak
 * (between frames, by a parabola) less 1,000 x r, in output frames */
static double delayAt(double rate)
{
	eng_resample_t r;
	char err[160];
	if (eng_resampleOpen(&r, rate, 2, err, sizeof err) != 0)
		return -1.0;
	out = calloc(2u * 8u * 64u * BLOCKS, sizeof *out);
	nout = 0;
	for (unsigned b = 0; b < BLOCKS; b++)
	{
		float in[128] = { 0 };
		if (b == 1000u / 64u)
			in[2u * (1000u % 64u)] = in[2u * (1000u % 64u) + 1u] = 1.0f;
		if (eng_resamplePush(&r, in, collect, NULL) != 0)
			return -1.0;
	}
	eng_resampleClose(&r);
	uint32_t peak = 1;
	for (uint32_t i = 1; i + 1 < nout; i++)
		if (fabsf(out[2u * i]) > fabsf(out[2u * peak]))
			peak = i;
	const double a = out[2u * (peak - 1u)], b = out[2u * peak], c = out[2u * (peak + 1u)];
	const double frac = 0.5 * (a - c) / (a - 2.0 * b + c);
	const double ratio = rate / PLUG_ENGINE_RATE;
	printf("P7_converter: %.0f Hz: %u frames out of %u (%.1f), delay %.2f output frames = %.2f input frames\n", rate,
		nout, BLOCKS * 64u, BLOCKS * 64u * ratio, (double)peak + frac - 1000.0 * ratio,
		((double)peak + frac - 1000.0 * ratio) / ratio);
	const double expectFrames = BLOCKS * 64u * ratio;
	free(out);
	if (fabs((double)nout - expectFrames) > 1.0)
		return -2.0;
	return ((double)peak + frac - 1000.0 * ratio) / ratio;
}

TEST(P7_converter_delay)
{
	const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
	for (unsigned i = 0; i < 4; i++)
	{
		const double d = delayAt(rates[i]);
		ASSERT_TRUE(d >= (double)PLUG_CONVERTER_DELAY - 0.5 && d <= (double)PLUG_CONVERTER_DELAY + 0.5);
	}
}

TEST(P7_converter_open)
{
	eng_resample_t r;
	char err[160];
	ASSERT_EQ(eng_resampleOpen(&r, 48000.0, 2, err, sizeof err), 0);
	eng_resampleClose(&r);
	ASSERT_EQ(eng_resampleOpen(&r, -1.0, 2, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "AudioConverterNew") != NULL);
	ASSERT_EQ(eng_resampleOpen(&r, 48000.0, 3, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "2 or 4 channels") != NULL);
}

/* the frames a converter gives, into a buffer of its own */
typedef struct
{
	float *x;
	uint32_t frames;
	unsigned channels;
} sink_t;

static void into(void *ctx, const float *x, uint32_t frames)
{
	sink_t *k = ctx;
	memcpy(&k->x[k->channels * k->frames], x, k->channels * frames * sizeof *x);
	k->frames += frames;
}

TEST(P7_converter_channels)
{
	/* plan E9.4: four channels converted at once (the engine's: the device's two outputs) are each pair
	 * converted apart (plug_resample_wav's, two at a time), sample for sample, at 44.1, 48 and 96 kHz */
	const double rates[] = { 44100.0, 48000.0, 96000.0 };
	for (unsigned k = 0; k < 3; k++)
	{
		eng_resample_t r4, ra, rb;
		char err[160];
		ASSERT_EQ(eng_resampleOpen(&r4, rates[k], 4, err, sizeof err), 0);
		ASSERT_EQ(eng_resampleOpen(&ra, rates[k], 2, err, sizeof err), 0);
		ASSERT_EQ(eng_resampleOpen(&rb, rates[k], 2, err, sizeof err), 0);
		sink_t s4 = { calloc(4u * 4u * 64u * BLOCKS, sizeof(float)), 0, 4 };
		sink_t sa = { calloc(2u * 4u * 64u * BLOCKS, sizeof(float)), 0, 2 };
		sink_t sb = { calloc(2u * 4u * 64u * BLOCKS, sizeof(float)), 0, 2 };
		uint32_t seed = 12345u;
		for (unsigned b = 0; b < BLOCKS; b++)
		{
			float in4[4 * 64], ina[2 * 64], inb[2 * 64];
			for (unsigned i = 0; i < 4 * 64; i++)
			{
				seed = seed * 1664525u + 1013904223u;
				in4[i] = (float)(int16_t)(seed >> 16) / 32768.0f;	/* a device sample */
			}
			for (unsigned i = 0; i < 64; i++)
			{
				ina[2 * i] = in4[4 * i];
				ina[2 * i + 1] = in4[4 * i + 1];
				inb[2 * i] = in4[4 * i + 2];
				inb[2 * i + 1] = in4[4 * i + 3];
			}
			ASSERT_EQ(eng_resamplePush(&r4, in4, into, &s4), 0);
			ASSERT_EQ(eng_resamplePush(&ra, ina, into, &sa), 0);
			ASSERT_EQ(eng_resamplePush(&rb, inb, into, &sb), 0);
		}
		eng_resampleClose(&r4);
		eng_resampleClose(&ra);
		eng_resampleClose(&rb);
		uint32_t differ = 0;
		for (uint32_t i = 0; i < s4.frames && i < sa.frames; i++)
			differ += s4.x[4 * i] != sa.x[2 * i] || s4.x[4 * i + 1] != sa.x[2 * i + 1] || s4.x[4 * i + 2] != sb.x[2 * i]
				|| s4.x[4 * i + 3] != sb.x[2 * i + 1];
		printf("P7_converter_channels: %.0f Hz: %u frames of four channels, %u and %u of two, %u differ\n", rates[k],
			s4.frames, sa.frames, sb.frames, differ);
		ASSERT_TRUE(s4.frames == sa.frames && sa.frames == sb.frames);
		ASSERT_TRUE(fabs((double)s4.frames - BLOCKS * 64.0 * rates[k] / PLUG_ENGINE_RATE) <= 1.0);
		ASSERT_EQ(differ, 0);
		free(s4.x);
		free(sa.x);
		free(sb.x);
	}
}

TEST_MAIN()
