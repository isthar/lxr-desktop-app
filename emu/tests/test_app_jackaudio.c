/*
 * test_app_jackaudio.c
 *
 * R5 (stage 1b spec 6.8, plan R5 F1, F5-F8): the clock-out jack's edges to
 * frames and levels, the input's edge detector with its hysteresis, an input
 * frame's host time, and the input callback's body with its warm-up lead.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <string.h>

#include "app_jackaudio.h"
#include "seqc_test.h"

static uint64_t cyc(uint64_t f) { return (uint64_t)((unsigned __int128)f * 552960u / 113u); }	/* spec 4.2 */

TEST(R5_clk_frame)
{
	const uint64_t cs = 216000000u;
	ASSERT_EQ(app_clkFrame(cs, cs - 1u), 0);	/* before the codec start */
	ASSERT_EQ(app_clkFrame(cs, cs + 4892u), 0);	/* frame 1 starts 4,893 cycles in */
	ASSERT_EQ(app_clkFrame(cs, cs + 4893u), 1);
	for (uint64_t f = 1; f < 2000000u; f += 997u)
	{
		ASSERT_EQ(app_clkFrame(cs, cs + cyc(f)), f);
		ASSERT_EQ(app_clkFrame(cs, cs + cyc(f) - 1u), f - 1u);
	}
	ASSERT_EQ(app_clkFrame(cs, cs + cyc(1000000000000u)), 1000000000000u);	/* 262 days: no overflow */
	ASSERT_EQ(app_clkFrame(cs, cs + cyc(64u * 7u)), 448);	/* block request 7's first frame */
}

TEST(R5_clk_render)
{
	static app_clkOut_t c;
	int16_t lv[64];
	app_clkEdge(&c, 100u, 1);
	app_clkEdge(&c, 230u, 0);
	app_clkEdge(&c, 250u, 1);
	app_clkEdge(&c, 251u, 0);	/* a pulse of one frame */
	app_clkEdge(&c, 400u, 1);
	app_clkRender(&c, 0u, 64u, lv);
	for (unsigned i = 0; i < 64u; i++)
		ASSERT_EQ(lv[i], 0);
	app_clkRender(&c, 64u, 64u, lv);
	for (unsigned i = 0; i < 64u; i++)
		ASSERT_EQ(lv[i], 64u + i >= 100u);	/* high from the edge's own frame */
	app_clkRender(&c, 128u, 64u, lv);
	for (unsigned i = 0; i < 64u; i++)
		ASSERT_EQ(lv[i], 1);
	app_clkRender(&c, 192u, 64u, lv);
	for (unsigned i = 0; i < 64u; i++)
		ASSERT_EQ(lv[i], 192u + i < 230u || 192u + i == 250u);
	ASSERT_EQ(c.n, 1);	/* the edge at 400 waits for its block */
	app_clkRender(&c, 384u, 64u, lv);	/* 256-383 were never rendered (an output closed) */
	for (unsigned i = 0; i < 64u; i++)
		ASSERT_EQ(lv[i], 384u + i >= 400u);
	app_clkEdge(&c, 10u, 0);	/* late: it sets the level from the block's first frame */
	app_clkRender(&c, 448u, 64u, lv);
	ASSERT_EQ(lv[0], 0);
	for (unsigned i = 0; i < APP_CLK_EDGES + 1u; i++)
		app_clkEdge(&c, 600u + i, (int)(i & 1u));
	ASSERT_EQ(c.dropped, 1);
}

TEST(R5_edge_step)
{
	app_edgeDet_t d = { 0.5f, 0 };
	ASSERT_EQ(app_edgeStep(&d, 0.5f), 0);	/* at the threshold is not above it */
	ASSERT_EQ(app_edgeStep(&d, 0.51f), 1);
	ASSERT_EQ(app_edgeStep(&d, 0.9f), 0);
	ASSERT_EQ(app_edgeStep(&d, 0.46f), 0);	/* within the hysteresis */
	ASSERT_EQ(app_edgeStep(&d, NAN), 0);
	ASSERT_EQ(app_edgeStep(&d, 0.44f), -1);
	ASSERT_EQ(app_edgeStep(&d, 0.49f), 0);	/* low until above the threshold again */
	ASSERT_EQ(app_edgeStep(&d, -1.0f), 0);	/* an AC-coupled input's undershoot is low */
	ASSERT_EQ(app_edgeStep(&d, 1.0f), 1);
	d.threshold = 0.1f;
	ASSERT_EQ(app_edgeStep(&d, 0.06f), 0);
	ASSERT_EQ(app_edgeStep(&d, 0.04f), -1);
}

TEST(R5_in_host_time)
{
	ASSERT_EQ(app_inHostTime(1000000u, 0u, 40u, 500.0), 980000);	/* 40 frames of latency, 48 kHz on 24 MHz */
	ASSERT_EQ(app_inHostTime(1000000u, 40u, 40u, 500.0), 1000000);
	ASSERT_EQ(app_inHostTime(1000000u, 511u, 40u, 500.05), 1235524);	/* mRateScalar 1.0001 */
	ASSERT_EQ(app_inHostTime(1000u, 0u, 40u, 500.0), 0);			/* before the host clock's start */
	ASSERT_EQ(app_inHostTime(60000000000000u, 1u, 0u, 500.0), 60000000000500u);	/* 29 days up: exact */
}

TEST(R5_jackin_render)
{
	static app_jackIn_t in;
	static float x[64 * 2];
	ASSERT_EQ(app_ringInit(&in.edges, sizeof(app_panelEvent_t), 64u), 0);
	const app_jackInConfig_t cfg = { 24e6, 48000.0, 40u, 2u, 0u, 1u, 0.5f, 0.5f };
	app_jackInStart(&in, &cfg);
	for (unsigned i = 10; i < 15u; i++)
		x[2u * i] = 0.6f;	/* a clock pulse from frame 10 */
	x[2u * 15u] = 0.47f;	/* within the hysteresis */
	x[2u * 20u] = 0.52f;	/* the next pulse */
	x[2u * 25u] = NAN;
	for (unsigned i = 12; i < 40u; i++)
		x[2u * i + 1u] = 0.9f;	/* the reset high, frames 12-39 */
	const uint64_t host = 1000000u, entry = host + 280000u;	/* mHostTime (512 + 48) frames before the callback */
	app_jackInRender(&in, host, entry, 1.0, x, 64u);
	static const struct { uint8_t kind; int32_t value; unsigned frame; } want[4] = {
		{ APP_P_CLOCK, 1, 10u }, { APP_P_RESET, 1, 12u }, { APP_P_CLOCK, 1, 20u }, { APP_P_RESET, 0, 40u } };
	ASSERT_EQ(app_ringCount(&in.edges), 4);
	for (unsigned k = 0; k < 4u; k++)
	{
		const app_panelEvent_t *e = app_ringPeek(&in.edges, k);
		ASSERT_EQ(e->kind, want[k].kind);
		ASSERT_EQ(e->value, want[k].value);	/* a high jack is the reset on (spec 4.5) */
		ASSERT_EQ(e->host, host + 500u * want[k].frame - 20000u);
	}
	app_ringPop(&in.edges, 4u);
	memset(x, 0, sizeof x);
	x[0] = x[1] = 0.8f;	/* a pulse and the reset at one frame: the reset first (F6) */
	app_jackInRender(&in, host, entry, 1.0, x, 1u);
	ASSERT_EQ(((const app_panelEvent_t *)app_ringPeek(&in.edges, 0u))->kind, APP_P_RESET);
	ASSERT_EQ(((const app_panelEvent_t *)app_ringPeek(&in.edges, 1u))->kind, APP_P_CLOCK);
	app_ringPop(&in.edges, 2u);
	ASSERT_EQ(atomic_load(&in.lead), 0);	/* F7: fixed after 32 callbacks */
	for (unsigned k = 2; k < APP_IN_WARMUP; k++)
		app_jackInRender(&in, host, entry, 1.0, x, 0u);
	ASSERT_EQ(atomic_load(&in.lead), 300000);	/* 280,000 ticks + 40 frames */
	app_jackInRender(&in, host, entry + 100000u, 1.0, x, 0u);
	ASSERT_EQ(atomic_load(&in.lead), 300000);	/* a later, longer lead does not move D */
	app_jackInStop(&in, 5555u);	/* stopped with the reset high: it is let go (F11) */
	const app_panelEvent_t *e = app_ringPeek(&in.edges, 0u);
	ASSERT_TRUE(e != NULL && e->kind == APP_P_RESET && e->value == 0 && e->host == 5555u);
	ASSERT_EQ(atomic_load(&in.lead), 0);
	app_ringFree(&in.edges);
}

TEST_MAIN()
