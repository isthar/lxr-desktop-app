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
	if (eng_resampleOpen(&r, rate, err, sizeof err) != 0)
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
	ASSERT_EQ(eng_resampleOpen(&r, 48000.0, err, sizeof err), 0);
	eng_resampleClose(&r);
	ASSERT_EQ(eng_resampleOpen(&r, -1.0, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "AudioConverterNew") != NULL);
}

TEST_MAIN()
