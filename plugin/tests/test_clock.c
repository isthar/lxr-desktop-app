/*
 * test_clock.c
 *
 * P4: the host's transport as MIDI clock (plan E9.2 Task 4). Each case drives plug_clockBlock()
 * block by block with a transport the test computes, and checks the messages at their absolute
 * frames against values computed here.
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

#include "plug_clock.h"
#include "seqc_test.h"

#define MAXEV 20000

typedef struct
{
	uint64_t frame;
	uint8_t len, b[3];
} ev_t;

static ev_t evs[MAXEV];
static unsigned nev;

/* one block at absolute frame at, its messages appended to evs */
static void block(plug_clock_t *c, const plug_transport_t *t, uint64_t at, uint32_t frames)
{
	plug_midi_t out[512];
	const unsigned n = plug_clockBlock(c, t, frames, out, 512);
	for (unsigned i = 0; i < n && nev < MAXEV; i++)
	{
		if (i > 0 && out[i].offset < out[i - 1].offset)
			abort();	/* the messages come in frame order */
		evs[nev++] = (ev_t){ at + out[i].offset, out[i].len, { out[i].b[0], out[i].b[1], out[i].b[2] } };
	}
}

static plug_transport_t playing(double tempo, double ppq)
{
	return (plug_transport_t){ 1, 1, tempo, ppq, 0, 0.0, 0.0 };
}

/* the count of messages with status st */
static unsigned count(uint8_t st)
{
	unsigned n = 0;
	for (unsigned i = 0; i < nev; i++)
		n += evs[i].b[0] == st;
	return n;
}

TEST(P4_clock_start_at_zero)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 40; b++)
	{
		const plug_transport_t t = playing(120.0, (double)(b * 512u) / 24000.0);
		block(&c, &t, b * 512u, 512);
	}
	/* 1,000 frames a clock: Start and clock 0 at 0, then clocks 1..20 */
	ASSERT_EQ(nev, 22);
	ASSERT_EQ(evs[0].b[0], 0xFA);
	ASSERT_EQ(evs[0].frame, 0);
	for (unsigned k = 0; k <= 20; k++)
	{
		ASSERT_EQ(evs[k + 1].b[0], 0xF8);
		ASSERT_EQ(evs[k + 1].frame, 1000u * k);
	}
}

TEST(P4_clock_rounding_44k)
{
	plug_clock_t c;
	plug_clockInit(&c, 44100.0);
	nev = 0;
	const double fpq = 44100.0 * 60.0 / 120.0;
	for (unsigned b = 0; b < 3600; b++)
	{
		const plug_transport_t t = playing(120.0, (double)(b * 256u) / fpq);
		block(&c, &t, b * 256u, 256);
	}
	/* 918.75 frames a clock: clock k at the nearest frame (a half, 1,837.5 for clock 2, either way:
	 * the position is a double), each clock once */
	ASSERT_EQ(evs[0].b[0], 0xFA);
	ASSERT_EQ(count(0xF8), nev - 1);
	ASSERT_EQ(nev - 1, 1004);	/* the clocks before 921,600 = 3,600 x 256 */
	for (unsigned k = 0; k < nev - 1; k++)
		ASSERT_NEAR((double)evs[k + 1].frame, 918.75 * k, 0.5);
	ASSERT_EQ(evs[2].frame, 919);
	ASSERT_EQ(evs[4].frame, 2756);
	ASSERT_EQ(evs[5].frame, 3675);
}

TEST(P4_clock_start_later)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 20; b++)
	{
		const plug_transport_t t = playing(120.0, 1.1 + (double)(b * 512u) / 24000.0);
		block(&c, &t, b * 512u, 512);
	}
	/* Song Position 5 (ceil 4.4) and Continue at 0; clock 30 (position 1.25) at 0.15 x 24,000 */
	ASSERT_EQ(evs[0].b[0], 0xF2);
	ASSERT_EQ(evs[0].b[1], 5);
	ASSERT_EQ(evs[0].b[2], 0);
	ASSERT_EQ(evs[0].frame, 0);
	ASSERT_EQ(evs[1].b[0], 0xFB);
	ASSERT_EQ(evs[1].frame, 0);
	ASSERT_EQ(evs[2].b[0], 0xF8);
	ASSERT_EQ(evs[2].frame, 3600);
	ASSERT_EQ(evs[3].frame, 4600);
	ASSERT_EQ(count(0xFA), 0);
}

TEST(P4_clock_stop)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 30; b++)
	{
		plug_transport_t t = playing(120.0, (double)(b * 512u) / 24000.0);
		t.playing = b < 10;
		block(&c, &t, b * 512u, 512);
	}
	ASSERT_EQ(count(0xFC), 1);
	ASSERT_EQ(evs[nev - 1].b[0], 0xFC);
	ASSERT_EQ(evs[nev - 1].frame, 5120);
	ASSERT_EQ(count(0xF8), 6);	/* clocks 0..5 before 5,120 */
}

/* a loop [cs, cs + 4) quarter notes at 120 BPM, 48 kHz (24,000 frames a quarter), from position
 * cs, in blocks of the given size; split 1 ends a block exactly at the loop's end */
static void loopRun(unsigned size, int split, double cs)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	uint64_t at = 0;
	while (at < 200000u)
	{
		uint32_t n = size;
		if (split && at < 96000u && at + n > 96000u)
			n = (uint32_t)(96000u - at);
		plug_transport_t t = playing(120.0, cs + fmod((double)at / 24000.0, 4.0));
		t.cycle = 1;
		t.cycleStart = cs;
		t.cycleEnd = cs + 4.0;
		block(&c, &t, at, n);
		at += n;
	}
}

TEST(P4_clock_loop)
{
	static ev_t inBlock[MAXEV];
	loopRun(700, 0, 4.0);	/* the block [95,900, 96,600) holds the loop's end */
	const unsigned n = nev;
	memcpy(inBlock, evs, sizeof evs);
	loopRun(700, 1, 4.0);
	ASSERT_EQ(nev, n);
	for (unsigned i = 0; i < n; i++)
	{
		ASSERT_EQ(evs[i].frame, inBlock[i].frame);
		ASSERT_EQ(evs[i].b[0], inBlock[i].b[0]);
		ASSERT_EQ(evs[i].b[1], inBlock[i].b[1]);
	}
	/* from 4: Song Position 16, Continue; at the loop's end, 96,000: Stop, 16, Continue, clock 96 */
	ASSERT_EQ(evs[0].b[0], 0xF2);
	ASSERT_EQ(evs[0].b[1], 16);
	unsigned i = 0;
	while (i < n && !(evs[i].b[0] == 0xFC))
		i++;
	ASSERT_TRUE(i + 3 < n);
	ASSERT_EQ(evs[i].frame, 96000);
	ASSERT_EQ(evs[i + 1].b[0], 0xF2);
	ASSERT_EQ(evs[i + 1].b[1], 16);
	ASSERT_EQ(evs[i + 2].b[0], 0xFB);
	ASSERT_EQ(evs[i + 3].b[0], 0xF8);
	ASSERT_EQ(evs[i + 3].frame, 96000);
	ASSERT_EQ(evs[i - 1].b[0], 0xF8);
	ASSERT_EQ(evs[i - 1].frame, 95000);	/* the last clock before the end, position 7 23/24 */
	ASSERT_EQ(count(0xFC), 2);			/* 96,000 and 192,000 */
}

TEST(P4_clock_far_loop)
{
	/* the loop [4,396, 4,400), past bar 1,024, where Song Position (14 bits) wraps: the clocks follow
	 * the real 16th, so the messages are those of the loop [4, 8) at the same frames, and only the
	 * Song Positions differ: 17,584 mod 16,384 = 1,200 = 9 x 128 + 48 for 16 */
	static ev_t near[MAXEV];
	loopRun(700, 0, 4.0);
	const unsigned n = nev;
	memcpy(near, evs, sizeof evs);
	loopRun(700, 0, 4396.0);
	ASSERT_EQ(nev, n);
	unsigned spp = 0;
	for (unsigned i = 0; i < n; i++)
	{
		ASSERT_EQ(evs[i].frame, near[i].frame);
		ASSERT_EQ(evs[i].b[0], near[i].b[0]);
		if (evs[i].b[0] == 0xF2)
		{
			ASSERT_EQ(near[i].b[1], 16);
			ASSERT_EQ(near[i].b[2], 0);
			ASSERT_EQ(evs[i].b[1], 0x30);
			ASSERT_EQ(evs[i].b[2], 0x09);
			spp++;
		}
	}
	ASSERT_EQ(spp, 3);	/* the start and the wraps at 96,000 and 192,000 */
}

TEST(P4_clock_locate)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 40; b++)
	{
		/* at block 20 the host jumps from 10.24 to 2.3 */
		const double p = b < 20 ? 10.0 + (double)(b * 512u) / 24000.0 : 2.3 + (double)((b - 20u) * 512u) / 24000.0;
		const plug_transport_t t = playing(120.0, p);
		block(&c, &t, b * 512u, 512);
	}
	unsigned i = 0;
	while (i < nev && evs[i].b[0] != 0xFC)
		i++;
	ASSERT_EQ(evs[i].frame, 20u * 512u);
	ASSERT_EQ(evs[i + 1].b[0], 0xF2);
	ASSERT_EQ(evs[i + 1].b[1], 10);	/* ceil(9.2) */
	ASSERT_EQ(evs[i + 2].b[0], 0xFB);
	ASSERT_EQ(evs[i + 3].b[0], 0xF8);
	ASSERT_EQ(evs[i + 3].frame, 20u * 512u + 4800u);	/* clock 60, position 2.5 */
	ASSERT_EQ(count(0xFC), 1);
}

TEST(P4_clock_tempo)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	double p = 0.0;
	for (unsigned b = 0; b < 100; b++)
	{
		const double tempo = b < 50 ? 120.0 : 150.0;
		const plug_transport_t t = playing(tempo, p);
		block(&c, &t, b * 480u, 480);
		p += 480.0 * tempo / (60.0 * 48000.0);
	}
	ASSERT_EQ(count(0xFC) + count(0xF2) + count(0xFB), 0);
	/* 24,000 frames from 0 at 120 (24 clocks, the 24th at 24,000 is the next block's), then 800 a clock */
	ASSERT_EQ(evs[1].frame, 0);
	ASSERT_EQ(evs[24].frame, 23000);
	ASSERT_EQ(evs[26].frame, 24800);
	ASSERT_EQ(evs[27].frame, 25600);
}

TEST(P4_clock_no_context)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 20; b++)
	{
		plug_transport_t t = playing(120.0, (double)(b * 512u) / 24000.0);
		t.valid = b < 5 || b >= 10;
		if (b >= 10)
			t.playing = 0;
		block(&c, &t, b * 512u, 512);
	}
	ASSERT_EQ(count(0xFC), 1);
	ASSERT_EQ(evs[nev - 1].frame, 5u * 512u);
	plug_transport_t t = { 0 };
	nev = 0;
	plug_clockInit(&c, 48000.0);
	block(&c, &t, 0, 512);
	ASSERT_EQ(nev, 0);
}

TEST(P4_clock_count_in)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 60; b++)
	{
		const plug_transport_t t = playing(120.0, -1.0 + (double)(b * 512u) / 24000.0);
		block(&c, &t, b * 512u, 512);
	}
	ASSERT_EQ(evs[0].b[0], 0xFA);
	ASSERT_EQ(evs[0].frame, 24000);
	ASSERT_EQ(evs[1].b[0], 0xF8);
	ASSERT_EQ(evs[1].frame, 24000);
	ASSERT_EQ(evs[2].frame, 25000);
}

TEST(P4_clock_jitter)
{
	plug_clock_t c;
	plug_clockInit(&c, 44100.0);
	nev = 0;
	srand(7);
	const double fpq = 44100.0 * 60.0 / 137.0;
	for (unsigned b = 0; b < 10000; b++)
	{
		const double noise = ((double)rand() / RAND_MAX - 0.5) * 2e-9;
		const plug_transport_t t = playing(137.0, (double)(b * 128u) / fpq + noise);
		block(&c, &t, b * 128u, 128);
	}
	/* clock k at the frame nearest to k x per: the clocks before frame 1,280,000 */
	const double per = fpq / 24.0;
	const unsigned clocks = count(0xF8);
	ASSERT_EQ(clocks, (unsigned)floor((1280000.0 - 0.5) / per) + 1u);
	for (unsigned k = 0; k < clocks; k++)
		ASSERT_NEAR((double)evs[k + 1].frame, k * per, 1.0);
	ASSERT_EQ(count(0xFC) + count(0xF2) + count(0xFB), 0);
}

TEST(P4_clock_reset)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 20; b++)
	{
		if (b == 10)
			plug_clockReset(&c);	/* a new engine */
		const plug_transport_t t = playing(120.0, 3.0 + (double)(b * 512u) / 24000.0);
		block(&c, &t, b * 512u, 512);
	}
	ASSERT_EQ(count(0xF2), 2);
	ASSERT_EQ(count(0xFC), 0);	/* a new engine is stopped already */
	unsigned i = 1;
	while (evs[i].b[0] != 0xF2)
		i++;
	ASSERT_EQ(evs[i].frame, 5120);
	ASSERT_EQ(evs[i].b[1], 13);	/* ceil(4 x 3.2133) */
	ASSERT_EQ(evs[i + 1].b[0], 0xFB);
}

TEST(P4_clock_spp_bytes)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	plug_transport_t t = playing(120.0, 700.0);
	block(&c, &t, 0, 64);
	/* the first block holds Song Position, Continue and the clock of position 700 at frame 0 only */
	ASSERT_EQ(nev, 3);
	ASSERT_EQ(evs[0].b[0], 0xF2);
	ASSERT_EQ(evs[0].b[1], 0x70);	/* 2,800 = 21 x 128 + 112 */
	ASSERT_EQ(evs[0].b[2], 0x15);
	ASSERT_EQ(evs[1].b[0], 0xFB);
	ASSERT_EQ(evs[2].b[0], 0xF8);
	ASSERT_EQ(evs[2].frame, 0);
	plug_clockInit(&c, 48000.0);
	nev = 0;
	t = playing(120.0, 5000.0);
	block(&c, &t, 0, 64);
	/* 20,000 is past 16,383: the two bytes wrap, 20,000 mod 16,384 = 3,616 = 28 x 128 + 32 (a pattern
	 * whose length is a power of two stays in line); the clocks follow the real 16th, so the block
	 * holds the same three messages, not the clocks from 16th 16,383 on */
	ASSERT_EQ(nev, 3);
	ASSERT_EQ(evs[0].b[0], 0xF2);
	ASSERT_EQ(evs[0].b[1], 0x20);
	ASSERT_EQ(evs[0].b[2], 0x1C);
	ASSERT_EQ(evs[1].b[0], 0xFB);
	ASSERT_EQ(evs[2].b[0], 0xF8);
	ASSERT_EQ(evs[2].frame, 0);
}

TEST(P4_clock_max)
{
	plug_clock_t c;
	plug_clockInit(&c, 8000.0);
	plug_midi_t out[4];
	const plug_transport_t t = playing(300.0, 0.0);
	ASSERT_EQ(plug_clockBlock(&c, &t, 8192, out, 4), 4);	/* 125 clocks would come */
	ASSERT_EQ(out[0].b[0], 0xFA);
}

TEST_MAIN()
