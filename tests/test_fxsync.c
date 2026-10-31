/* test_fxsync.c: the v0.4 synced delay (plan A5) and DJ filter (plan A6); DJ3 and DJ4 run
 * djFilter_process() of Core/Src/Soundengine/DSPAudio/FX/DjFilter.c on two stereo outputs
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"
#include <math.h>
#include <string.h>
#include "Soundengine/DSPAudio/FX/fxsync.h"
#include "Soundengine/DSPAudio/FX/djfilter.h"
#include "Soundengine/config.h"	/* OUTPUT_DMA_SIZE */

#define FS 44140.f
#define MAXN 32703.f	/* FX_BUFFER_SIZE - 64 */
#define MAXP 16319.f	/* (FX_BUFFER_SIZE >> 1) - 64 */

TEST(DY1_divisions_halving_and_off)
{
	/* 120 BPM: a quarter is 0.5 s = 22070 samples at 44140 Hz */
	ASSERT_EQ(fxsync_samples(FXSYNC_OFF, 120.f, FS, MAXN) == 0.f, 1);
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_16, 120.f, FS, MAXN)), 5518);
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_8T, 120.f, FS, MAXN)), 7357);
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_4, 120.f, FS, MAXN)), 22070);
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_4D, 120.f, FS, MAXN)), 16553);	/* 33105 halved */
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_4D, 122.f, FS, MAXN)), 32562);	/* fits */
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_4, 120.f, FS, MAXP)), 11035);	/* ping-pong */
	ASSERT_EQ(fxsync_samples(FXSYNC_4, 0.f, FS, MAXN) == 0.f, 1);
	ASSERT_EQ(fxsync_samples(FXSYNC_COUNT, 120.f, FS, MAXN) == 0.f, 1);
	ASSERT_EQ(fxsync_moved(22070.f, 22070.4f), 0);
	ASSERT_EQ(fxsync_moved(22070.f, 22100.f), 1);
}

static float djf_gainDb(uint8_t pos, uint8_t reso, float f)
{
	DjSvfCoef c;
	DjSvfState s = { 0.f, 0.f };
	double in2 = 0, out2 = 0;
	djf_coef(&c, pos, reso, FS);
	for(int i = 0; i < 16384; i++)
	{
		const float x = sinf(6.2831853f * f * (float)i / FS);
		const float y = djf_tick(&s, &c, x);
		if(i >= 8192) { in2 += x * x; out2 += y * y; }
	}
	return 10.f * log10f((float)(out2 / in2));
}

TEST(DJ1_left_lowpass_right_highpass)
{
	DjSvfCoef c;
	djf_coef(&c, 64, 0, FS);
	ASSERT_EQ(c.mode, DJF_FLAT);
	djf_coef(&c, 63, 127, FS);
	ASSERT_EQ(c.mode, DJF_FLAT);
	ASSERT_EQ(djf_gainDb(0, 0, 1000.f) < -40.f, 1);	/* LP at 19.5 Hz */
	ASSERT_EQ(djf_gainDb(127, 0, 1000.f) < -40.f, 1);	/* HP at 19.9 kHz (limited) */
	ASSERT_EQ(fabsf(djf_gainDb(32, 0, 100.f)) < 1.f, 1);	/* LP at 660 Hz passes 100 Hz */
	ASSERT_EQ(djf_gainDb(32, 127, 660.f) > 10.f, 1);	/* resonance at the cutoff */
	ASSERT_EQ(fabsf(djf_gainDb(96, 0, 10000.f)) < 1.f, 1);	/* HP at 640 Hz passes 10 kHz */
}

TEST(DJ2_sweep_stays_finite)
{
	DjSvfCoef c = { 0.f, 0.f, 0.f, 2.f, DJF_FLAT };	/* set at i = 0; GCC cannot see it */
	DjSvfState s = { 0.f, 0.f };
	uint32_t rnd = 1;
	float peak = 0.f;
	for(int i = 0; i < 88280; i++)	/* 2 s, a sweep 0 -> 127 -> 0 at full resonance, per block */
	{
		if((i % 64) == 0)
		{
			const int b = i / 64, half = 88280 / 128;
			djf_coef(&c, (uint8_t)((b < half) ? (b * 127 / half) : ((2 * half - b) * 127 / half)), 127, FS);
		}
		rnd = rnd * 1664525u + 1013904223u;
		const float x = ((float)(rnd >> 9) / 4194304.f) - 1.f;
		const float y = (c.mode == DJF_FLAT) ? x : djf_tick(&s, &c, x);
		ASSERT_EQ(isfinite(y), 1);
		if(fabsf(y) > peak) peak = fabsf(y);
	}
	ASSERT_EQ(peak < 8.f, 1);
}

//------------------------------------------------------------------------------
// djFilter_process() on output 1 and output 2, each an interleaved stereo buffer of one block,
// as mixer_calcNextSampleBlock() passes them
//------------------------------------------------------------------------------
#define DJ_FRAMES OUTPUT_DMA_SIZE
/* The step bound. The normal change of one sample is the dry input's largest step, djDryStep.
 * A crossfade over one block between two signals inside full scale adds at most
 * 2 * 32768 / DJ_FRAMES = 1024 LSB a sample to that (a 1.45 ms ramp, not a click). A change of
 * the formula without a crossfade steps by up to the signal itself, 0.7 of full scale here. */
#define DJ_FADE_STEP (2.f * 32768.f / (float)DJ_FRAMES)

static int16_t djOut1[2 * DJ_FRAMES], djOut2[2 * DJ_FRAMES];
static int16_t djDry[2 * DJ_FRAMES];
static long djFrame;		/* the input's frame counter */
static float djLast[4];		/* the last sample of each channel, for the steps */
static float djMaxStep, djDryStep;
/* since djMeasure(): the output's RMS and the RMS of its steps (the 1.5 kHz part dominates
 * these: 753 LSB dry, about 120 through the low-pass at 175 Hz) */
static double djAcc, djStepAcc;
static long djCount;

static void djMeasure(void) { djAcc = djStepAcc = 0; djCount = 0; }
static float djRms(void) { return (float)sqrt(djAcc / (double)djCount); }
static float djStepRms(void) { return (float)sqrt(djStepAcc / (double)djCount); }

/* bass-heavy: 0.7 of full scale at 55 Hz and 0.15 at 1.5 kHz, R in another phase than L */
static int16_t djInput(long n, int ch)
{
	const float t = (float)n / FS, ph = ch ? 1.047f : 0.f;
	return (int16_t)lrintf(32767.f * (0.7f * sinf(6.2831853f * 55.f * t + ph) + 0.15f * sinf(6.2831853f * 1500.f * t + 2.f * ph)));
}

/** the filter back to flat, then the step trackers from the next block on */
static void djStart(long frame)
{
	djFilter_set(64, 0);
	for(int b = 0; b < 2; b++)
		djFilter_process(djOut1, djOut2, DJ_FRAMES);	/* out of the last setting, then flat */
	djFrame = frame;
	djMaxStep = djDryStep = 0.f;
	for(int c = 0; c < 4; c++)
		djLast[c] = (float)djInput(djFrame - 1, c & 1);
}

/** one block at pos: both outputs get the same input; output 2 is filtered like output 1,
 * bit for bit; the steps of the four channels and the measures */
static void djBlock(uint8_t pos)
{
	for(int i = 0; i < DJ_FRAMES; i++, djFrame++)
		for(int c = 0; c < 2; c++)
			djDry[2 * i + c] = djInput(djFrame, c);
	memcpy(djOut1, djDry, sizeof djDry);
	memcpy(djOut2, djDry, sizeof djDry);
	djFilter_set(pos, 0);
	djFilter_process(djOut1, djOut2, DJ_FRAMES);
	ASSERT_EQ(memcmp(djOut1, djOut2, sizeof djOut1), 0);
	for(int i = 0; i < DJ_FRAMES; i++)
		for(int c = 0; c < 4; c++)
		{
			const float y = (float)((c < 2) ? djOut1 : djOut2)[2 * i + (c & 1)];
			const float d = (float)djDry[2 * i + (c & 1)];
			const float dPrev = (i > 0) ? (float)djDry[2 * (i - 1) + (c & 1)] : (float)djInput(djFrame - DJ_FRAMES - 1, c & 1);
			if(fabsf(y - djLast[c]) > djMaxStep) djMaxStep = fabsf(y - djLast[c]);
			if(fabsf(d - dPrev) > djDryStep) djDryStep = fabsf(d - dPrev);
			djStepAcc += (double)(y - djLast[c]) * (y - djLast[c]);
			djAcc += (double)y * y;
			djCount++;
			djLast[c] = y;
		}
}

TEST(DJ3_process_both_outputs_flat_and_fades)
{
	static const uint8_t sides[2] = { 20, 100 };	/* LP at 175 Hz, HP at 1 kHz */
	djStart(0);
	for(int s = 0; s < 2; s++)
	{
		/* flat, and the flat zone around it: all four bit for bit */
		static const uint8_t flat[3] = { 64, 63, 65 };
		for(int b = 0; b < 6; b++)
		{
			djBlock(flat[b % 3]);
			ASSERT_EQ(memcmp(djOut1, djDry, sizeof djDry), 0);
		}
		/* into the filter, steady, out to flat */
		for(int b = 0; b < 30; b++)
		{
			if(b == 4) djMeasure();
			djBlock(sides[s]);
		}
		if(s == 0) ASSERT_TRUE(djStepRms() < 300.f);	/* the 1.5 kHz is out */
		else ASSERT_TRUE(djRms() < 4000.f);		/* the bass is out (16590 dry) */
		djBlock(64);
		djBlock(64);
		ASSERT_EQ(memcmp(djOut1, djDry, sizeof djDry), 0);
	}
	ASSERT_TRUE(djDryStep > 1000.f && djDryStep < 1300.f);
	ASSERT_TRUE(djMaxStep <= djDryStep + DJ_FADE_STEP);
}

TEST(DJ4_side_jump_stays_under_the_step_bound)
{
	/* straight from the low-pass to the high-pass side and back in one block (a p-lock, a CC
	 * 10 -> 100, a square LFO), at 13 block phases of the 55 Hz bass (one period is 12.6 blocks) */
	static const uint8_t lp[2] = { 10, 20 }, hp[2] = { 100, 120 };
	for(int k = 0; k < 13; k++)
	{
		djStart(k * DJ_FRAMES);
		for(int b = 0; b < 24; b++)
			djBlock(lp[k & 1]);			/* steady low-pass */
		for(int b = 0; b < 30; b++)
		{
			if(b == 4) djMeasure();
			djBlock(hp[k & 1]);			/* the jump, then the high-pass */
		}
		ASSERT_TRUE(djRms() < 4000.f);			/* the bass is out */
		for(int b = 0; b < 30; b++)
		{
			if(b == 4) djMeasure();
			djBlock(lp[k & 1]);			/* and back */
		}
		ASSERT_TRUE(djStepRms() < 300.f);		/* the 1.5 kHz is out */
		ASSERT_TRUE(djMaxStep <= djDryStep + DJ_FADE_STEP);
	}
}

TEST_MAIN()
