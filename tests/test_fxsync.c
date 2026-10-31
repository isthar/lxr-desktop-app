/* test_fxsync.c: the v0.4 synced delay (plan A5) and DJ filter (plan A6)
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
#include "Soundengine/DSPAudio/FX/fxsync.h"
#include "Soundengine/DSPAudio/FX/djfilter.h"

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

TEST_MAIN()
