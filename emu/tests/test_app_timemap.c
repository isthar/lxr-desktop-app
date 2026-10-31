/*
 * test_app_timemap.c
 *
 * M18 (stage 1b spec 6.3, 6.4, 12.1): the time map between host time and
 * virtual time (the wall clock, a drifting device clock, a jump), L and D from
 * spec 6.4's table, the entry rule of an input event, and the order of a port's
 * messages across a re-anchor.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_timemap.h"
#include "seqc_test.h"

#define TPS 24000000.0	/* the mach timebase of this Mac, 125/3 ns per tick (spec 6.3) */

TEST(M18_map_wall)
{
	app_timemap_t m;
	app_tmInit(&m, TPS);
	ASSERT_EQ_DBL(m.nominal, 9.0);	/* 216 MHz over 24 MHz */
	app_tmWall(&m, 216e6, 1000u);
	ASSERT_NEAR(app_tmVirtual(&m, 1000u + 24000000u), 432e6, 1e-6);
	ASSERT_EQ(app_tmHost(&m, 324e6), 1000u + 12000000u);
	ASSERT_EQ(m.audio, 0);
}

/* a device clock 100 ppm fast against the host: callbacks of 512 frames at 48 kHz */
TEST(M18_map_drift)
{
	app_timemap_t m;
	app_tmInit(&m, TPS);
	const double ratio = 1.0001;
	for (unsigned k = 0; k < 400u; k++)	/* 4.3 s */
		ASSERT_EQ(app_tmPoint(&m, 1e9 + (double)k * 256000.0 * 9.0 * ratio, 5000000u + (uint64_t)k * 256000u), 0);
	ASSERT_NEAR(m.slope / 9.0, ratio, 1e-9);	/* the slope of the last 2 s */
	ASSERT_EQ(m.jumps, 0);
	const uint64_t h = 5000000u + 399u * 256000u + 1000u;
	ASSERT_NEAR((double)app_tmHost(&m, app_tmVirtual(&m, h)), (double)h, 1.0);
}

TEST(M18_map_jump)
{
	app_timemap_t m;
	app_tmInit(&m, TPS);
	uint64_t h = 1000000u;
	double c = 5e8;
	for (unsigned k = 0; k < 100u; k++, h += 256000u, c += 256000.0 * 9.0)
		app_tmPoint(&m, c, h);
	ASSERT_EQ(app_tmPoint(&m, c + 108000.0, h), 0);	/* 0.5 ms off the line: no re-anchor */
	h += 256000u;
	c += 256000.0 * 9.0 + 108000.0;
	h += 120000u;										/* the output stalled: the frame comes 5 ms late */
	ASSERT_EQ(app_tmPoint(&m, c, h), 1);
	ASSERT_EQ(m.jumps, 1);
	ASSERT_EQ(m.jumpHost, h);
	ASSERT_EQ_DBL(m.slope, 9.0);						/* a new line starts at the nominal slope */
	ASSERT_NEAR(app_tmVirtual(&m, h), c, 1e-6);
}

/* E32: the rate over the newest span of at least 2 s, for the diagnostics' block rate; none before there is one */
TEST(M18_map_rate)
{
	app_timemap_t m;
	app_tmInit(&m, TPS);
	const double ratio = 1.0001;
	for (unsigned k = 0; k < 400u; k++)
	{
		app_tmPoint(&m, 1e9 + (double)k * 256000.0 * 9.0 * ratio, 5000000u + (uint64_t)k * 256000u);
		if (k < 187u)	/* 187 periods are 1.995 s */
			ASSERT_EQ_DBL(m.rate, 0.0);
	}
	ASSERT_NEAR(m.rate / 9.0, ratio, 1e-9);
	ASSERT_NEAR(m.slope / 9.0, ratio, 1e-9);	/* the slope keeps its 2-s window */
	uint64_t h = 5000000u + 400u * 256000u;
	ASSERT_EQ(app_tmPoint(&m, 2e9, h), 1);		/* a jump starts a new line: no rate until it has 2 s */
	ASSERT_EQ_DBL(m.rate, 0.0);
	app_tmWall(&m, 3e9, h);
	ASSERT_EQ_DBL(m.rate, 0.0);
}

/* spec 6.4's table; the 128-frame rows: 2.67 ms buffer + 1.00 ms safety + 1.09 ms converter */
TEST(M18_latency_table)
{
	const app_latencyIn_t rows[4] = {
		{ 0.01274, 48000.0, 60u, 690u, 471u, 0 },
		{ 0.00476, 48000.0, 60u, 690u, 118u, 0 },
		{ 0.01274, 48000.0, 60u, 0u, 471u, 0 },
		{ 0.00476, 48000.0, 60u, 0u, 118u, 0 },
	};
	const double L[4] = { 57.0, 33.0, 42.6, 18.6 }, D[4] = { 58.0, 34.0, 44.0, 20.0 };
	for (unsigned i = 0; i < 4u; i++)
	{
		const double l = app_latencyL(&rows[i]) * 1e3;
		ASSERT_NEAR(l, L[i], 0.05);
		ASSERT_NEAR(l + APP_D_MARGIN * 1e3, D[i], 0.5);	/* "about" D */
	}
	app_latencyIn_t clk = rows[0];
	clk.clockOutAudio = 1;	/* R5's option holds each block back by one (spec 6.8) */
	ASSERT_NEAR((app_latencyL(&clk) - app_latencyL(&rows[0])) * 1e3, 1.44991, 0.0001);
}

TEST(M18_entry_rule)
{
	ASSERT_EQ(app_entryTime(1000.0, 500u, 900u, 0u), 1500u);	/* map(stamp) + D */
	ASSERT_EQ(app_entryTime(1000.0, 500u, 2000u, 0u), 2000u);	/* never before now */
	ASSERT_EQ(app_entryTime(1000.0, 500u, 900u, 1700u), 1700u);	/* never before the port's previous event */
	ASSERT_EQ(app_entryTime(-5e6, 0u, 77u, 0u), 77u);			/* a stamp before the map's origin */
}

/* spec 12.1 M18: a re-anchor that moves the map back by 5 ms keeps a port's messages in order */
TEST(M18_reanchor_order)
{
	app_timemap_t m;
	app_tmInit(&m, TPS);
	app_tmPoint(&m, 1e9, 10000000u);
	const uint64_t d = 58u * 216000u, now = 1000000000u;
	const uint64_t s1 = 10240000u, s2 = s1 + 24000u;	/* two clocks 1 ms apart */
	const uint64_t e1 = app_entryTime(app_tmVirtual(&m, s1), d, now, 0u);
	ASSERT_EQ(app_tmPoint(&m, 1e9 - 5.0 * 216000.0, 10000000u), 1);	/* the map moves back by 5 ms */
	ASSERT_TRUE(app_tmVirtual(&m, s2) + (double)d < (double)e1);	/* without the third term: out of order */
	ASSERT_EQ(app_entryTime(app_tmVirtual(&m, s2), d, now, e1), e1);
}

TEST_MAIN()
