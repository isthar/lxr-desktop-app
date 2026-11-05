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
#include <unistd.h>

#include "plug_clock.h"
#include "seqc_test.h"

#define MAXEV 32768

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
		if (out[i].offset >= frames)
			abort();	/* inside the block */
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

/* the loop [4, 8) at 44.1 kHz and 128 BPM (20,671.875 frames a quarter note, a pass 82,687.5
 * frames), from position 4, in blocks of size: the host reports the position at each block's
 * first frame, wrapped into the loop, and does not split its blocks at the loop's end. The
 * messages before frame total. */
static void wrapRun(unsigned size, uint64_t total)
{
	plug_clock_t c;
	plug_clockInit(&c, 44100.0);
	nev = 0;
	const double fpq = 44100.0 * 60.0 / 128.0;
	for (uint64_t at = 0; at < total; at += size)
	{
		double p = 4.0 + (double)at / fpq;
		if (p >= 8.0)
			p = 4.0 + fmod(p - 4.0, 4.0);
		plug_transport_t t = playing(128.0, p);
		t.cycle = 1;
		t.cycleStart = 4.0;
		t.cycleEnd = 8.0;
		block(&c, &t, at, size);
	}
	while (nev > 0 && evs[nev - 1].frame >= total)
		nev--;
}

TEST(P4_clock_wrap_blocks)
{
	/* Every pass plays its first 16th: each wrap is Stop, Song Position 16, Continue and the clock
	 * of position 4, in blocks of 1, 37, 128 and 4,096. Every other pass ends on a half frame; when
	 * that falls in a block's last half frame (before the fix, blocks of 1 at 56 of the 260 wraps,
	 * of 37 at 4, of 128 at the 257th), the wrap is the next block's, and the host reports that
	 * block a fraction of a frame after the loop's start: Song Position 16 and its clock at once,
	 * not 17. The frames are within 1 of the exact ones computed here, and of the blocks of 128:
	 * where an exact frame is a half (every other wrap, and clocks such as the 32nd of a pass,
	 * 27,562.5 frames in), the block sizes round it either way. */
	typedef struct
	{
		double f;
		uint8_t b0;
	} want_t;
	static want_t want[MAXEV];
	static ev_t ref[MAXEV];
	const double clock = 44100.0 * 60.0 / 128.0 / 24.0, pass = 96.0 * clock;
	const uint64_t total = 5250u * 4096u;	/* 260 wraps; the 261st would be at 21,581,437.5 */
	unsigned nw = 0;
	for (unsigned k = 0; k <= 260; k++)
	{
		const double x = pass * k;
		if (k > 0)
			want[nw++] = (want_t){ x, 0xFC };
		want[nw++] = (want_t){ x, 0xF2 };
		want[nw++] = (want_t){ x, 0xFB };
		for (unsigned j = 0; j < 96 && x + clock * j < (double)total; j++)
			want[nw++] = (want_t){ x + clock * j, 0xF8 };
	}
	ASSERT_EQ(nw, 25749);	/* 260 x 96 + 7 clocks, 260 Stops, 261 Song Positions and Continues */
	static const unsigned sizes[] = { 128, 1, 37, 4096 };
	for (unsigned s = 0; s < 4; s++)
	{
		wrapRun(sizes[s], total);
		unsigned spp = 0;
		for (unsigned i = 0; i < nev && i < nw; i++)
		{
			const double f = (double)evs[i].frame;
			const int bad = evs[i].b[0] != want[i].b0 || (evs[i].b[0] == 0xF2 && (evs[i].b[1] != 16 || evs[i].b[2] != 0))
				|| fabs(f - want[i].f) > 1.0 || (s > 0 && fabs(f - (double)ref[i].frame) > 1.0);
			if (bad)
				printf("blocks of %u, message %u: frame %llu %02X %u, want %.3f %02X\n", sizes[s], i,
					(unsigned long long)evs[i].frame, evs[i].b[0], evs[i].b[1], want[i].f, want[i].b0);
			ASSERT_FALSE(bad);
			spp += evs[i].b[0] == 0xF2;
		}
		ASSERT_EQ(nev, nw);
		ASSERT_EQ(spp, 261);
		if (s == 0)
			memcpy(ref, evs, nev * sizeof evs[0]);
	}
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

/* 120 BPM at 48 kHz from 0 in blocks of 512, the host's position d quarter notes off from block
 * 10 on */
static void shifted(double d)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 20; b++)
	{
		const plug_transport_t t = playing(120.0, (double)(b * 512u) / 24000.0 + (b >= 10 ? d : 0.0));
		block(&c, &t, b * 512u, 512);
	}
}

TEST(P4_clock_jump)
{
	/* a jump is a position more than half a clock (1/48 quarter note, 500 frames) from the one
	 * expected: 0.6 of a clock either way is one, at block 10's first frame; 0.444 is not */
	const double d[] = { 0.025, -0.025 };
	for (unsigned i = 0; i < 2; i++)
	{
		shifted(d[i]);
		ASSERT_EQ(count(0xFC), 1);
		unsigned k = 0;
		while (evs[k].b[0] != 0xFC)
			k++;
		ASSERT_EQ(evs[k].frame, 5120);
		ASSERT_EQ(evs[k + 1].b[0], 0xF2);
		ASSERT_EQ(evs[k + 1].b[1], 1);	/* 0.2383 or 0.1883 quarter notes: 16th 1 */
		ASSERT_EQ(evs[k + 2].b[0], 0xFB);
		shifted(d[i] * 0.74);
		ASSERT_EQ(count(0xFC) + count(0xF2) + count(0xFB), 0);
		ASSERT_EQ(count(0xF8), i == 0 ? 11 : 10);	/* clock 10 at 9,556, or clock 9 at 9,444 the last */
	}
}

TEST(P4_clock_late_report)
{
	/* The host reports block 3 300 frames late (a rounding; less than half a clock): clock 2, at
	 * frame 2,000, then falls after that block, and block 4, reported on time, starts after it: it
	 * is due before the block and goes out at its first frame, 2,048. Each clock once, in order. */
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 8; b++)
	{
		const double late = b == 3 ? 300.0 : 0.0;
		const plug_transport_t t = playing(120.0, ((double)(b * 512u) - late) / 24000.0);
		block(&c, &t, b * 512u, 512);
	}
	ASSERT_EQ(count(0xFC) + count(0xF2) + count(0xFB), 0);
	ASSERT_EQ(nev, 6);	/* Start, clocks 0-4 before 4,096 */
	ASSERT_EQ(evs[1].frame, 0);
	ASSERT_EQ(evs[2].frame, 1000);
	ASSERT_EQ(evs[3].frame, 2048);
	ASSERT_EQ(evs[4].frame, 3000);
	ASSERT_EQ(evs[5].frame, 4000);
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

TEST(P4_clock_garbage)
{
	/* A tempo or a position that is not a number, a tempo outside 1-10,000 BPM, a position beyond
	 * +-1e9 quarter notes or a rate outside 1,000-10,000,000 Hz is no context: Stop once if
	 * running, then nothing. Each block must return: a tempo or a position of +inf, or a position
	 * of 1e300, made clocks() loop forever, and a NaN position or a tempo of 1e-320 converted NaN
	 * to an integer (UBSan). A hang fails the test by its alarm. */
	alarm(20);
	const double tempos[] = { INFINITY, -INFINITY, NAN, 0.0, -120.0, 1e-320, 0.999, 10000.5 };
	const double ppqs[] = { NAN, INFINITY, -INFINITY, 1e300, -1e300, 1.5e9, -1.5e9 };
	const unsigned nt = sizeof tempos / sizeof tempos[0], np = sizeof ppqs / sizeof ppqs[0];
	plug_midi_t out[64];
	plug_clock_t c;
	plug_transport_t t;
	for (unsigned i = 0; i < nt + np; i++)
	{
		plug_clockInit(&c, 48000.0);
		t = playing(120.0, 0.0);
		ASSERT_EQ(plug_clockBlock(&c, &t, 512, out, 64), 2);	/* Start and clock 0 */
		t = i < nt ? playing(tempos[i], 512.0 / 24000.0) : playing(120.0, ppqs[i - nt]);
		ASSERT_EQ(plug_clockBlock(&c, &t, 512, out, 64), 1);
		ASSERT_EQ(out[0].b[0], 0xFC);
		ASSERT_EQ(plug_clockBlock(&c, &t, 512, out, 64), 0);
		plug_clockInit(&c, 48000.0);	/* from the start: nothing */
		ASSERT_EQ(plug_clockBlock(&c, &t, 512, out, 64), 0);
	}
	/* the bounds are good: 1 and 10,000 BPM (12 frames a clock), positions of +-1e9 */
	plug_clockInit(&c, 48000.0);
	t = playing(1.0, 0.0);
	ASSERT_EQ(plug_clockBlock(&c, &t, 512, out, 64), 2);
	plug_clockInit(&c, 48000.0);
	t = playing(10000.0, 0.0);
	ASSERT_EQ(plug_clockBlock(&c, &t, 512, out, 64), 44);	/* Start and the clocks at 0, 12, ..., 504 */
	ASSERT_EQ(out[43].offset, 504);
	plug_clockInit(&c, 48000.0);
	t = playing(120.0, 1e9);
	ASSERT_EQ(plug_clockBlock(&c, &t, 512, out, 64), 3);	/* Song Position, Continue, the clock of 1e9 */
	ASSERT_EQ(out[0].b[0], 0xF2);
	plug_clockInit(&c, 48000.0);
	t = playing(120.0, -1e9);
	ASSERT_EQ(plug_clockBlock(&c, &t, 512, out, 64), 0);	/* a count-in, from far before 0 */
	/* rates outside 1,000-10,000,000 Hz: nothing */
	const double rates[] = { 0.0, -48000.0, NAN, INFINITY, 999.0, 2e7 };
	for (unsigned i = 0; i < sizeof rates / sizeof rates[0]; i++)
	{
		plug_clockInit(&c, rates[i]);
		t = playing(120.0, 0.0);
		ASSERT_EQ(plug_clockBlock(&c, &t, 512, out, 64), 0);
	}
	/* loop bounds that are not numbers within +-1e9 are no loop: from 4, Song Position 16, Continue
	 * and clock 96, as without a loop (a start of -inf or -1e300 wrapped at frame 240 to 0) */
	const double bounds[][2] = { { NAN, 8.0 }, { 4.0, NAN }, { -INFINITY, 4.01 }, { 4.0, INFINITY },
		{ -1e300, 4.01 }, { 4.0, 1e300 }, { -2e9, 4.01 } };
	for (unsigned i = 0; i < sizeof bounds / sizeof bounds[0]; i++)
	{
		plug_clockInit(&c, 48000.0);
		t = playing(120.0, 4.0);
		t.cycle = 1;
		t.cycleStart = bounds[i][0];
		t.cycleEnd = bounds[i][1];
		ASSERT_EQ(plug_clockBlock(&c, &t, 512, out, 64), 3);
		ASSERT_EQ(out[0].b[1], 16);
		ASSERT_EQ(out[2].b[0], 0xF8);
	}
	alarm(0);
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
