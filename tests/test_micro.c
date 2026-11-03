/*
 * test_micro.c
 *
 * LICHOMAT v0.6 (plan v0.6 Task 3, spec v0.6 10.1): a step's microtiming and the groove
 * templates in the timing core, and the place of a recorded note.
 * - micro_offsets: hit 0 at the grid + C + F at each of the 11 tempos for every coarse
 *   and fine part, P clamped to 7/8 of a step, the ratchets spaced as in v0.3, the swing,
 *   delay and humanize added after P; with fine() and tmpl() set but returning 0 and off,
 *   queue_equiv's random patterns give v0.5's events field for field (M7);
 * - templates: the table through tmpl(), the position q (track lengths, a pattern change,
 *   a song position, x2 and x3/4), the amounts, a negative offset in the early part, the
 *   rolls unchanged (M8);
 * - micro_choke: a step pushed past the next one is choked (M9), a ratchet tail, the
 *   note-offs, the shared hi-hat;
 * - record_place: seqc_recordPlace() (spec 7.1, M19, M20).
 * The fake host has fine() and tmpl() only after fake_useMicro(true) (M28); every test here
 * sets it as it needs and leaves it off.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <stdio.h>

#include "fake_host.h"
#include "queue_equiv.h"
#include "seqc_test.h"

/* ticks of a track step by tempo index, and spec 4.2's table, copied: the core's own copies
 * are not read here */
static const int stepTicks[GROOVE_TEMPO_COUNT] = { 384, 288, 192, 144, 128, 96, 72, 64, 48, 32, 24 };
static const int table[TEMPLATE_COUNT][TEMPLATE_STEPS] = {
	{ 0 },
	{ 0, 16, 0, 16, 0, 16, 0, 16, 0, 16, 0, 16, 0, 16, 0, 16 },	/* 1 swg */
	{ 0, 18, 0, 5, 0, 18, 0, 5, 0, 18, 0, 5, 0, 18, 0, 5 },		/* 2 lay */
	{ 0, 15, 0, 11, 0, 24, 0, 5, 0, 17, 0, 1, 0, 16, 0, 3 },	/* 3 lyb */
	{ 0, 15, 1, 3, 0, 16, 2, 22, 0, 14, 2, 16, 0, 9, 2, 18 },	/* 4 rlx */
	{ 0, 7, 3, 8, 0, 1, 2, 1, 0, 7, 3, 8, 0, 1, 2, 1 },		/* 5 2bt */
	{ 0, 0, -4, 0, 0, 0, -4, 0, 0, 0, -4, 0, 0, 0, -4, 0 },		/* 6 psh */
	{ 0, 0, 6, 0, 0, 0, 6, 0, 0, 0, 6, 0, 0, 0, 6, 0 },		/* 7 drg */
};
#define LAY 2
#define LYB 3
#define RLX 4
#define PSH 6

#define CYCLES_PER_TICK_120 281250u	/* 216 MHz x 60 / (120 x 384), exact */

/** a / b rounded half away from zero, through floor() (the core divides integers) */
static int rha(int a, int b)
{
	const double x = (double)a / (double)b;
	return x >= 0.0 ? (int)floor(x + 0.5) : -(int)floor(-x + 0.5);
}

/** G of spec 4.1: template id at position q, at percent, in a track step of len ticks */
static int templateTicks(int id, int q, int percent, int len)
{
	return rha(table[id][q % TEMPLATE_STEPS] * percent * len, 9600);
}

/** a fresh fake host with fine() and tmpl(), logging the triggers only */
static fake_host_t *microHost(void)
{
	fake_useMicro(true);
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER);
	return h;
}

/** start at time 0 (step 0 at tick 0) and run for ms */
static void play(fake_host_t *h, double ms)
{
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, fake_msToCycles(ms), fake_msToCycles(0.5));
}

/** every step of a track on */
static void allOn(fake_host_t *h, uint8_t pattern, uint8_t track)
{
	for (uint8_t s = 0; s < FAKE_NUM_STEPS; s++)
		fake_setStep(h, pattern, track, s, true);
}

//------------------------------------------------------------------------------
// micro_offsets
//------------------------------------------------------------------------------
TEST(micro_offsets_every_tempo_coarse_and_fine)
{
	for (uint8_t ti = 0; ti < GROOVE_TEMPO_COUNT; ti++)
	{
		const int L = stepTicks[ti], reach = L - L / 8;
		fake_host_t *h = microHost();
		seqc_setTempo(300.0f);
		for (uint8_t t = 0; t < FAKE_NUM_TRACKS; t++)
		{
			fake_setTempo(h, 0, t, ti);
			fake_setLength(h, 0, t, 64);
		}
		/* the 180 pairs, coarse -7..+7 and fine -6..+5, on steps 2, 4, ..., 62 of tracks
		 * 0-5: two steps apart, so no hit passes the next */
		for (int k = 0; k < 180; k++)
		{
			const uint8_t t = (uint8_t)(k / 31), s = (uint8_t)(2 + 2 * (k % 31));
			fake_setStep(h, 0, t, s, true);
			h->timing[0][t][s].shift = (uint8_t)(k / 12);
			h->fine[0][t][s] = (int8_t)(k % 12 - 6);
		}
		play(h, 65.0 * L / 96.0 * 50.0);		/* 65 track steps; a master step is 50 ms at 300 BPM */
		for (uint8_t t = 0; t < 6; t++)
		{
			const fake_rec_t *r[31];
			const size_t n = fake_triggers(h, t, r, 31);
			ASSERT_EQ(n, t < 5 ? 31 : 180 - 5 * 31);
			for (size_t j = 0; j < n; j++)
			{
				const int k = 31 * t + (int)j, s = 2 + 2 * (k % 31);
				const int cf = (k / 12 - 7) * (L / 8) + rha((k % 12 - 6) * L, 96);
				const int p = cf < -reach ? -reach : (cf > reach ? reach : cf);
				ASSERT_EQ(r[j]->ev.step, s);
				ASSERT_EQ(r[j]->ev.tick, (seqc_tick_t)s * L + p);
			}
		}
		ASSERT_TRUE(h->reads[FAKE_READ_FINE] > 0);
		ASSERT_EQ(h->errors, 0);
	}
	fake_useMicro(false);
}

TEST(micro_offsets_ratchets_spaced_as_v03)
{
	/* x2, x3, x4, x6, x8 with coarse +1 or -1 and fine -6, -1 or +5: hit j at the first hit
	 * + floor(j * L / k); the negative ones straddle the boundary (early and late part) */
	static const int hitsOf[6] = { 1, 2, 3, 4, 6, 8 };
	for (uint8_t ti = 0; ti < GROOVE_TEMPO_COUNT; ti++)
	{
		const int L = stepTicks[ti];
		fake_host_t *h = microHost();
		seqc_setTempo(300.0f);
		static const int8_t fines[3] = { -6, -1, 5 };
		for (int k = 0; k < 30; k++)
		{
			const uint8_t t = (uint8_t)(k / 15), s = (uint8_t)(4 + 4 * (k % 15));
			fake_setTempo(h, 0, t, ti);
			fake_setLength(h, 0, t, 64);
			fake_setStep(h, 0, t, s, true);
			h->timing[0][t][s] = (StepTiming){ .flam = (uint8_t)(1 + k % 5), .shift = (uint8_t)(t == 0 ? 8 : 6) };
			h->fine[0][t][s] = fines[(k / 5) % 3];
		}
		play(h, 65.0 * L / 96.0 * 50.0);
		for (uint8_t t = 0; t < 2; t++)
		{
			const fake_rec_t *r[128];
			const size_t n = fake_triggers(h, t, r, 128);
			size_t i = 0;
			for (int k = 15 * t; k < 15 * (t + 1); k++)
			{
				const int s = 4 + 4 * (k % 15), hits = hitsOf[1 + k % 5];
				const int first = s * L + (t == 0 ? 1 : -1) * (L / 8) + rha(fines[(k / 5) % 3] * L, 96);
				for (int j = 0; j < hits; j++, i++)
				{
					ASSERT_TRUE(i < n);
					ASSERT_EQ(r[i]->ev.step, s);
					ASSERT_EQ(r[i]->ev.hit, j);
					ASSERT_EQ(r[i]->ev.tick, first + j * L / hits);
				}
			}
			ASSERT_EQ(i, n);
		}
		ASSERT_EQ(h->errors, 0);
	}
	fake_useMicro(false);
}

TEST(micro_offsets_swing_delay_and_humanize_after_the_clamp)
{
	/* 120 BPM, x1, shuffle 64: swing round(64 x 64 / 127) = 32 on odd steps; delay -15 ms is
	 * -12 ticks, +30 ms is +23 */
	fake_host_t *h = microHost();
	h->shuffle = 64;
	fake_setStep(h, 0, 0, 3, true);
	h->timing[0][0][3].shift = 14;		/* +7 units and fine +5: 89, clamped to 84 */
	h->fine[0][0][3] = 5;
	fake_setStep(h, 0, 1, 5, true);
	h->timing[0][1][5].shift = 0;		/* -7 units and fine -6: -90, clamped to -84 */
	h->fine[0][1][5] = -6;
	fake_setGroove(h, 0, 1, 0, 0, -15);
	fake_setStep(h, 0, 2, 4, true);
	h->timing[0][2][4].shift = 0;
	h->fine[0][2][4] = -6;
	fake_setGroove(h, 0, 2, 0, 0, -15);
	fake_setStep(h, 0, 3, 4, true);
	h->fine[0][3][4] = 5;
	fake_setGroove(h, 0, 3, 0, 0, 30);
	play(h, 800.0);
	const fake_rec_t *r[4];
	ASSERT_EQ(fake_triggers(h, 0, r, 4), 1);
	ASSERT_EQ(r[0]->ev.tick, 288 + 32 + 84);	/* the clamp first, then the swing on top */
	ASSERT_EQ(fake_triggers(h, 1, r, 4), 1);
	ASSERT_EQ(r[0]->ev.tick, 480 + 32 - 84 - 12);
	ASSERT_EQ(fake_triggers(h, 2, r, 4), 1);
	ASSERT_EQ(r[0]->ev.tick, 384 - 84);		/* -84 - 12, floored at -84 */
	ASSERT_EQ(fake_triggers(h, 3, r, 4), 1);
	ASSERT_EQ(r[0]->ev.tick, 384 + 5 + 23);

	/* humanize 8 ms (+-6 ticks) after P: r, then random() % 13 - 6 */
	h = microHost();
	fake_setStep(h, 0, 0, 4, true);
	h->fine[0][0][4] = 3;
	fake_setGroove(h, 0, 0, 8, 0, 0);
	static const uint32_t up[2] = { 0, 12 };	/* +6 */
	fake_scriptRandom(h, up, 2);
	play(h, 700.0);
	ASSERT_EQ(fake_triggers(h, 0, r, 4), 1);
	ASSERT_EQ(r[0]->ev.tick, 384 + 3 + 6);
	h = microHost();
	fake_setStep(h, 0, 0, 4, true);
	h->timing[0][0][4].shift = 0;
	h->fine[0][0][4] = -6;
	fake_setGroove(h, 0, 0, 8, 0, 0);
	static const uint32_t down[2] = { 0, 0 };	/* -6 */
	fake_scriptRandom(h, down, 2);
	play(h, 700.0);
	ASSERT_EQ(fake_triggers(h, 0, r, 4), 1);
	ASSERT_EQ(r[0]->ev.tick, 384 - 84);		/* -84 - 6, floored */
	ASSERT_EQ(h->errors, 0);

	/* the template inside the clamp, P = clamp(C + F + G) (review of Tasks 1-3): lyb at 200 %
	 * on step 5 is G = +48, and with mic +84 (shift +7 units, fine 0) P is 84, not 132; psh at
	 * 200 % on step 2 is G = -8, and with mic -84 P is -84, then the delay +13 ms (+10 ticks) */
	h = microHost();
	fake_setStep(h, 0, 0, 5, true);
	h->timing[0][0][5].shift = 14;
	fake_setTemplate(h, 0, 0, LYB, 200);
	fake_setStep(h, 0, 1, 2, true);
	h->timing[0][1][2].shift = 0;
	fake_setTemplate(h, 0, 1, PSH, 200);
	fake_setGroove(h, 0, 1, 0, 0, 13);
	play(h, 800.0);
	ASSERT_EQ(fake_triggers(h, 0, r, 4), 1);
	ASSERT_EQ(r[0]->ev.tick, 480 + 84);
	ASSERT_EQ(fake_triggers(h, 1, r, 4), 1);
	ASSERT_EQ(r[0]->ev.tick, 192 - 84 + 10);
	ASSERT_EQ(templateTicks(LYB, 5, 200, 96), 48);
	ASSERT_EQ(templateTicks(PSH, 2, 200, 96), -8);
	ASSERT_EQ(h->errors, 0);
	fake_useMicro(false);
}

TEST(micro_offsets_bit_identity_with_zero_callbacks)
{
	/* M7: fine() and tmpl() set but returning 0 and off give v0.5's events, field for field,
	 * and the same counters, on 10^3 of queue_equiv's random patterns */
	for (uint64_t seed = 1; seed <= 1000; seed++)
	{
		seqc_stats_t a, b;
		fake_useMicro(false);
		const uint64_t without = queue_equivRun(seed, &a);
		fake_useMicro(true);
		const uint64_t with = queue_equivRun(seed, &b);
		if (with != without)
			printf("seed %llu: 0x%016llx with the callbacks, 0x%016llx without\n", (unsigned long long)seed,
					(unsigned long long)with, (unsigned long long)without);
		ASSERT_TRUE(with == without);
	}
	fake_useMicro(false);
}

//------------------------------------------------------------------------------
// templates
//------------------------------------------------------------------------------
TEST(templates_table_through_tmpl)
{
	/* ids 1..7 at 100 % on tracks 0..6, every step on, x1, two bars */
	fake_host_t *h = microHost();
	for (uint8_t t = 0; t < FAKE_NUM_TRACKS; t++)
	{
		allOn(h, 0, t);
		fake_setTemplate(h, 0, t, (uint8_t)(t + 1), 100);
	}
	play(h, 3990.0);
	for (uint8_t t = 0; t < FAKE_NUM_TRACKS; t++)
	{
		const fake_rec_t *r[40];
		ASSERT_EQ(fake_triggers(h, t, r, 40), 32);
		for (int k = 0; k < 32; k++)
			ASSERT_EQ(r[k]->ev.tick, 96 * k + table[t + 1][k % 16]);
	}
	ASSERT_TRUE(h->reads[FAKE_READ_TMPL] > 0);
	ASSERT_EQ(h->errors, 0);
	fake_useMicro(false);
}

TEST(templates_position_counts_the_tracks_steps)
{
	/* q = (loop x length + step) mod 16: occurrence k of a track with every step on is at
	 * position k mod 16, whatever its length */
	static const uint8_t lengths[4] = { 16, 12, 7, 64 };
	fake_host_t *h = microHost();
	for (uint8_t t = 0; t < 4; t++)
	{
		fake_setLength(h, 0, t, lengths[t]);
		allOn(h, 0, t);
		fake_setTemplate(h, 0, t, RLX, 100);
	}
	/* psh on a 7-step track at x1 and x2 (review of Tasks 1-3): the negative part of a loop's
	 * step 0 is generated in the early part, still in the loop before; occurrence 14 (step 0
	 * of loop 2, q 14) is at 96 x 14 - 4 = 1340 at x1 */
	for (uint8_t t = 4; t < 6; t++)
	{
		fake_setLength(h, 0, t, 7);
		allOn(h, 0, t);
		fake_setTemplate(h, 0, t, PSH, 100);
	}
	fake_setTempo(h, 0, 5, 8);
	play(h, 5990.0);			/* 48 steps */
	for (uint8_t t = 0; t < 4; t++)
	{
		const fake_rec_t *r[64];
		ASSERT_EQ(fake_triggers(h, t, r, 64), 48);
		for (int k = 0; k < 48; k++)
			ASSERT_EQ(r[k]->ev.tick, 96 * k + table[RLX][k % 16]);
	}
	for (uint8_t t = 4; t < 6; t++)
	{
		const int L = t == 4 ? 96 : 48;
		const fake_rec_t *r[128];
		ASSERT_EQ(fake_triggers(h, t, r, 128), (size_t)(t == 4 ? 48 : 96));
		for (int k = 0; k < (t == 4 ? 48 : 96); k++)
			ASSERT_EQ(r[k]->ev.tick, L * k + templateTicks(PSH, k, 100, L));
	}
	ASSERT_EQ(96 * 14 + templateTicks(PSH, 14, 100, 96), 1340);
	ASSERT_EQ(templateTicks(PSH, 14, 100, 48), -2);
	ASSERT_EQ(h->errors, 0);
	fake_useMicro(false);
}

TEST(templates_position_restarts_at_a_pattern_change)
{
	/* patterns 0 and 1 of 12 steps with lyb: at the change to pattern 1 at step 12 q restarts
	 * at 0; without it the 13th step is at q 12 */
	fake_host_t *h = microHost();
	for (uint8_t p = 0; p < 2; p++)
		for (uint8_t t = 0; t < FAKE_NUM_TRACKS; t++)
		{
			fake_setLength(h, p, t, 12);
			if (t == 0)
			{
				allOn(h, p, 0);
				fake_setTemplate(h, p, 0, LYB, 100);
			}
		}
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, fake_msToCycles(500.0), fake_msToCycles(0.5));
	fake_setNextPattern(h, 1);
	fake_runUntil(h, fake_msToCycles(2990.0), fake_msToCycles(0.5));
	const fake_rec_t *r[32];
	ASSERT_EQ(fake_triggers(h, 0, r, 32), 24);
	for (int k = 0; k < 24; k++)
	{
		ASSERT_EQ(r[k]->ev.pattern, k < 12 ? 0 : 1);
		ASSERT_EQ(r[k]->ev.tick, 96 * k + table[LYB][k < 12 ? k : k - 12]);
	}
	ASSERT_EQ(h->errors, 0);
	fake_useMicro(false);
}

TEST(templates_position_after_a_song_position)
{
	/* MIDI slave, a track of 12 steps with lyb: song position 21 is its step 9 in loop 1, so
	 * q = 21 mod 16 = 5 (24 ticks), not 9 (17) */
	fake_useMicro(true);
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER);
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	fake_process(h, fake_msToCycles(1.0));
	fake_setLength(h, 0, 0, 12);
	allOn(h, 0, 0);
	fake_setTemplate(h, 0, 0, LYB, 100);
	seqc_songPosition(21);
	ASSERT_EQ(seqc_trackStep(0), 9);
	fake_sync(h, SEQC_EV_CONTINUE, h->now + 100);
	const double per = fake_clockCycles(120.0);
	fake_runClocks(h, SEQC_EV_CLOCK, h->now + (seqc_time_t)per, per, 30, fake_msToCycles(0.1));
	const fake_rec_t *r[8];
	ASSERT_TRUE(fake_triggers(h, 0, r, 8) >= 3);
	for (int k = 0; k < 3; k++)
		ASSERT_EQ(r[k]->ev.tick, 96 * (21 + k) + table[LYB][5 + k]);
	ASSERT_EQ(h->errors, 0);
	fake_useMicro(false);
}

TEST(templates_follow_the_track_tempo)
{
	/* lyb at x2 (48 ticks) and x3/4 (128): G = rha(v x L / 96), occurrence k at k x L + G */
	fake_host_t *h = microHost();
	static const uint8_t tempos[2] = { 8, 4 };
	for (uint8_t t = 0; t < 2; t++)
	{
		fake_setTempo(h, 0, t, tempos[t]);
		allOn(h, 0, t);
		fake_setTemplate(h, 0, t, LYB, 100);
	}
	/* psh at 50 % at x4 (24 ticks, review of Tasks 1-3): G = -0.5 at q 2, rounded away from
	 * zero to -1 */
	fake_setTempo(h, 0, 2, 10);
	allOn(h, 0, 2);
	fake_setTemplate(h, 0, 2, PSH, 50);
	play(h, 4010.0);
	for (uint8_t t = 0; t < 2; t++)
	{
		const int L = stepTicks[tempos[t]];
		const fake_rec_t *r[80];
		const size_t n = fake_triggers(h, t, r, 80);
		ASSERT_TRUE(n >= 24);
		for (int k = 0; k < 24; k++)
			ASSERT_EQ(r[k]->ev.tick, L * k + templateTicks(LYB, k, 100, L));
	}
	ASSERT_EQ(templateTicks(LYB, 1, 100, 48), 8);	/* 7.5 rounds up */
	ASSERT_EQ(templateTicks(LYB, 11, 100, 48), 1);	/* 0.5 */
	ASSERT_EQ(templateTicks(LYB, 3, 100, 128), 15);	/* 14.67 */
	{
		const fake_rec_t *r[160];
		const size_t n = fake_triggers(h, 2, r, 160);
		ASSERT_TRUE(n >= 96);
		for (int k = 0; k < (int)n; k++)
			ASSERT_EQ(r[k]->ev.tick, 24 * k + templateTicks(PSH, k, 50, 24));
	}
	ASSERT_EQ(templateTicks(PSH, 2, 50, 24), -1);	/* -0.5 */
	ASSERT_EQ(h->errors, 0);
	fake_useMicro(false);
}

TEST(templates_amounts_scale_and_round)
{
	/* lyb at 0, 50, 100, 150, 200 % on tracks 0-4, psh at 50 and 150 % on tracks 5 and 6 */
	static const int pct[7] = { 0, 50, 100, 150, 200, 50, 150 };
	fake_host_t *h = microHost();
	for (uint8_t t = 0; t < FAKE_NUM_TRACKS; t++)
	{
		allOn(h, 0, t);
		fake_setTemplate(h, 0, t, t < 5 ? LYB : PSH, pct[t]);
	}
	play(h, 1990.0);
	for (uint8_t t = 0; t < FAKE_NUM_TRACKS; t++)
	{
		const fake_rec_t *r[20];
		ASSERT_EQ(fake_triggers(h, t, r, 20), 16);
		for (int k = 0; k < 16; k++)
			ASSERT_EQ(r[k]->ev.tick, 96 * k + rha(table[t < 5 ? LYB : PSH][k] * pct[t], 100));
	}
	ASSERT_EQ(rha(15 * 50, 100), 8);		/* 7.5 */
	ASSERT_EQ(rha(11 * 150, 100), 17);		/* 16.5 */
	ASSERT_EQ(rha(-4 * 150, 100), -6);
	ASSERT_EQ(h->errors, 0);
	fake_useMicro(false);
}

TEST(templates_negative_offset_in_the_early_part)
{
	/* psh at 200 %: step 2 at -8, 184, generated at boundary 1 (a late part at boundary 2,
	 * tick 192, has no hit before it); every other step on the grid */
	fake_host_t *h = microHost();
	allOn(h, 0, 0);
	fake_setTemplate(h, 0, 0, PSH, 200);
	play(h, 600.0);
	const fake_rec_t *r[8];
	ASSERT_TRUE(fake_triggers(h, 0, r, 8) >= 4);
	ASSERT_EQ(r[1]->ev.tick, 96);
	ASSERT_EQ(r[2]->ev.tick, 184);
	ASSERT_EQ(r[2]->ev.step, 2);
	ASSERT_EQ(r[3]->ev.tick, 288);
	ASSERT_EQ(h->errors, 0);
	fake_useMicro(false);
}

TEST(templates_leave_the_rolls)
{
	/* M8: a roll (two hits a step) on a track with lay and shuffle 40 stays at the boundary
	 * plus the swing, as with the template off */
	seqc_tick_t ticks[2][32];
	for (int on = 0; on < 2; on++)
	{
		fake_host_t *h = microHost();
		h->shuffle = 40;
		h->rollRate[0] = SEQC_ROLL(1, 2);
		if (on)
			fake_setTemplate(h, 0, 0, LAY, 200);
		play(h, 990.0);
		const fake_rec_t *r[32];
		ASSERT_EQ(fake_triggers(h, 0, r, 32), 16);
		for (int k = 0; k < 16; k++)
		{
			ASSERT_TRUE((r[k]->ev.flags & SEQC_EVF_ROLL) != 0);
			ticks[on][k] = r[k]->ev.tick;
			ASSERT_EQ(ticks[on][k], 96 * (k / 2) + 48 * (k % 2) + ((k / 2) % 2 ? 20 : 0));
		}
		ASSERT_EQ(h->errors, 0);
	}
	ASSERT_EQ(memcmp(ticks[0], ticks[1], 16 * sizeof ticks[0][0]), 0);
	fake_useMicro(false);
}

//------------------------------------------------------------------------------
// micro_choke
//------------------------------------------------------------------------------
TEST(micro_choke_swing_pushes_a_step_past_the_next)
{
	/* step 3 (odd) at +84 with swing 64 (shuffle 127): 288 + 64 + 84 = 436, past step 4 at
	 * 384; step 4 fires first and its choke drops step 3 (M9) */
	fake_host_t *h = microHost();
	h->shuffle = 127;
	fake_setStep(h, 0, 0, 3, true);
	fake_setMicro(h, 0, 0, 3, 84);
	fake_setStep(h, 0, 0, 4, true);
	play(h, 700.0);
	const fake_rec_t *r[4];
	ASSERT_EQ(fake_triggers(h, 0, r, 4), 1);
	ASSERT_EQ(r[0]->ev.step, 4);
	ASSERT_EQ(r[0]->ev.tick, 384);
	seqc_stats_t st;
	seqc_stats(&st);
	ASSERT_EQ(st.chokeDrops, 1);
	ASSERT_EQ(h->errors, 0);
	fake_useMicro(false);
}

TEST(micro_choke_both_play_12_ticks_apart)
{
	/* step 2 at +84 without swing: 276, then step 3 at 288; the note-off of step 2 comes
	 * 4 ticks after it, before step 3 */
	fake_host_t *h = microHost();
	h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER) | FAKE_LOG_BIT(FAKE_NOTE_OFF);
	fake_setStep(h, 0, 0, 2, true);
	fake_setMicro(h, 0, 0, 2, 84);
	fake_setStep(h, 0, 0, 3, true);
	play(h, 600.0);
	size_t pos = 0;
	const fake_rec_t *a = fake_next(h, FAKE_TRIGGER, &pos);
	ASSERT_TRUE(a != NULL);
	ASSERT_EQ(a->ev.tick, 276);
	size_t off = 0;
	const fake_rec_t *o = fake_next(h, FAKE_NOTE_OFF, &off);
	ASSERT_TRUE(o != NULL);
	ASSERT_EQ_DBL(o->tick, 280.0);
	const fake_rec_t *b = fake_next(h, FAKE_TRIGGER, &pos);
	ASSERT_TRUE(b != NULL);
	ASSERT_EQ(b->ev.tick, 288);
	ASSERT_TRUE(off <= pos);			/* the note-off is logged before the second trigger */
	seqc_stats_t st;
	seqc_stats(&st);
	ASSERT_EQ(st.chokeDrops, 0);
	fake_useMicro(false);
}

TEST(micro_choke_drops_a_ratchet_tail)
{
	/* step 2 x4 at +60: 252, 276, 300, 324; step 3 at 288 removes the last two */
	fake_host_t *h = microHost();
	fake_setStep(h, 0, 0, 2, true);
	fake_setTiming(h, 0, 0, 2, 3, 0);
	fake_setMicro(h, 0, 0, 2, 60);
	fake_setStep(h, 0, 0, 3, true);
	play(h, 600.0);
	const fake_rec_t *r[8];
	ASSERT_EQ(fake_triggers(h, 0, r, 8), 3);
	ASSERT_EQ(r[0]->ev.tick, 252);
	ASSERT_EQ(r[1]->ev.tick, 276);
	ASSERT_EQ(r[2]->ev.tick, 288);
	ASSERT_EQ(r[2]->ev.step, 3);
	seqc_stats_t st;
	seqc_stats(&st);
	ASSERT_EQ(st.chokeDrops, 2);
	fake_useMicro(false);
}

TEST(micro_choke_note_offs_before_each_trigger)
{
	/* microtiming on every step, crossings included: on each track every trigger after the
	 * first has the track's note-off before it, at most at its tick */
	static const int micro[16] = { 0, 84, -84, 40, -40, 84, 0, -84, 77, -77, 5, -6, 84, 84, -84, -84 };
	fake_host_t *h = microHost();
	h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER) | FAKE_LOG_BIT(FAKE_NOTE_OFF);
	h->shuffle = 90;
	for (uint8_t s = 0; s < 16; s++)
	{
		fake_setStep(h, 0, 0, s, true);
		fake_setTiming(h, 0, 0, s, (uint8_t)(s % 6), 0);
		fake_setMicro(h, 0, 0, s, micro[s]);
	}
	play(h, 4010.0);
	int pending = 0, triggers = 0;
	double lastOff = -1.0;
	for (size_t i = 0; i < h->logCount; i++)
	{
		const fake_rec_t *e = &h->log[i];
		if (e->kind == FAKE_NOTE_OFF && e->track == 0)
		{
			pending = 0;
			lastOff = e->tick;
		}
		else if (e->kind == FAKE_TRIGGER && e->ev.track == 0)
		{
			if (triggers++ > 0)
			{
				ASSERT_EQ(pending, 0);
				ASSERT_TRUE(lastOff <= (double)e->ev.tick);
			}
			pending = 1;
		}
	}
	ASSERT_TRUE(triggers > 16);
	fake_useMicro(false);
}

TEST(micro_choke_shared_hihat_order_is_the_tick_order)
{
	/* tracks 5 and 6 share the hi-hat voice: on step 2, track 6 at -10 and track 5 at +10
	 * trigger in that order; swapped, the other way round */
	for (int swap = 0; swap < 2; swap++)
	{
		fake_host_t *h = microHost();
		fake_setStep(h, 0, 5, 2, true);
		fake_setStep(h, 0, 6, 2, true);
		fake_setMicro(h, 0, 5, 2, swap ? -10 : 10);
		fake_setMicro(h, 0, 6, 2, swap ? 10 : -10);
		play(h, 400.0);
		const fake_rec_t *r[4];
		ASSERT_EQ(fake_triggers(h, -1, r, 4), 2);
		ASSERT_EQ(r[0]->ev.track, swap ? 5 : 6);
		ASSERT_EQ(r[0]->ev.tick, 182);
		ASSERT_EQ(r[1]->ev.track, swap ? 6 : 5);
		ASSERT_EQ(r[1]->ev.tick, 202);
	}
	fake_useMicro(false);
}

//------------------------------------------------------------------------------
// record_place: seqc_recordPlace()
//------------------------------------------------------------------------------
/** the reference tick of step s at x1 (spec 7.1): grid, swing on odd s, template, delay */
static int refTick(int s, int swing, int id, int percent, int delayTicks)
{
	const int g = id ? templateTicks(id, s, percent, 96) : 0;
	return 96 * s + ((s & 1) ? swing : 0) + g + delayTicks;
}

TEST(record_place_sweep_around_a_boundary)
{
	/* 120 BPM: a note 1000 cycles after each tick from 60 before to 60 after step 4's grid
	 * tick; the target is the nearer of the current and the next step, the offset rounded */
	static const uint8_t shuffles[2] = { 0, 40 };
	for (int sh = 0; sh < 2; sh++)
	{
		const int swing = sh ? 20 : 0;		/* round(40 x 64 / 127) */
		fake_host_t *h = fake_init();
		h->logMask = 0;
		h->shuffle = shuffles[sh];
		play(h, 400.0);
		for (int k = -60; k <= 60; k++)
		{
			const seqc_time_t t = (seqc_time_t)(384 + k) * CYCLES_PER_TICK_120 + 1000u;
			fake_process(h, t);
			seqc_recPlace_t p;
			ASSERT_TRUE(seqc_recordPlace(0, t, &p));
			const double x = 384 + k + 1000.0 / CYCLES_PER_TICK_120;
			const int c = k < 0 ? 3 : 4;
			const int r0 = refTick(c, swing, 0, 0, 0), r1 = refTick(c + 1, swing, 0, 0, 0);
			const bool ahead = x - r0 >= r1 - x;
			const double d = x - (ahead ? r1 : r0);
			ASSERT_EQ(p.ahead, ahead);
			ASSERT_EQ(p.step, ahead ? c + 1 : c);
			ASSERT_EQ(p.micro, d >= 0 ? (int)floor(d + 0.5) : -(int)floor(-d + 0.5));
		}
	}
}

TEST(record_place_tie_goes_to_the_next_step)
{
	/* at 33,750,000 / 2^18 BPM a tick is 2^18 cycles, so the position of a cycle count is
	 * exact: halfway between the reference ticks of steps 4 and 5 is a tie, and goes to 5 */
	static const uint8_t shuffles[2] = { 0, 40 };
	for (int sh = 0; sh < 2; sh++)
	{
		fake_host_t *h = fake_init();
		h->logMask = 0;
		h->shuffle = shuffles[sh];
		seqc_setTempo(128.74603271484375f);
		play(h, 100.0);
		/* the rounding: 2.5 ticks after step 4 is +3 */
		seqc_recPlace_t p;
		const seqc_time_t late = ((seqc_time_t)386 << 18) + (1u << 17);
		fake_process(h, late);
		ASSERT_TRUE(seqc_recordPlace(0, late, &p));
		ASSERT_FALSE(p.ahead);
		ASSERT_EQ(p.micro, 3);
		const seqc_tick_t mid = sh ? 442 : 432;	/* (384 + 480 + swing 20) / 2 */
		const seqc_time_t t = (seqc_time_t)mid << 18;
		fake_process(h, t);
		ASSERT_TRUE(seqc_recordPlace(0, t, &p));
		ASSERT_TRUE(p.ahead);
		ASSERT_EQ(p.step, 5);
		ASSERT_EQ(p.micro, sh ? -58 : -48);
		/* 2.5 before step 5's grid tick: -3, or -22.5 before its swung reference: -23 */
		const seqc_time_t early = ((seqc_time_t)477 << 18) + (1u << 17);
		fake_process(h, early);
		ASSERT_TRUE(seqc_recordPlace(0, early, &p));
		ASSERT_TRUE(p.ahead);
		ASSERT_EQ(p.micro, sh ? -23 : -3);
	}
}

TEST(record_place_leaves_out_template_and_delay)
{
	/* lay at 100 and 150 %, delay +10 ms (8 ticks at 120 BPM): a note played at a step's
	 * reference + d stores rha(d); written back as the step's microtiming, the next loop
	 * plays it within half a tick of where it was played */
	static const double d[16] = { 3.4, -7.6, 12.2, -0.5, 30.0, -25.3, 0.49, 40.0,
				      -40.0, 17.7, -12.5, 6.6, -33.3, 22.0, -1.2, 9.9 };
	static const int pct[2] = { 100, 150 };
	for (int a = 0; a < 2; a++)
	{
		fake_host_t *h = microHost();
		fake_setTemplate(h, 0, 0, LAY, pct[a]);
		fake_setGroove(h, 0, 0, 0, 0, 10);
		fake_process(h, 0);
		seqc_start();
		fake_process(h, 0);
		double played[16];
		for (int s = 0; s < 16; s++)
		{
			const double x = refTick(s, 0, LAY, pct[a], 8) + d[s];
			const seqc_time_t t = (seqc_time_t)(x * CYCLES_PER_TICK_120);
			fake_runUntil(h, t, fake_msToCycles(0.5));
			seqc_recPlace_t p;
			ASSERT_TRUE(seqc_recordPlace(0, t, &p));
			ASSERT_EQ(p.step, s);
			ASSERT_EQ(p.micro, rha((int)lround(d[s] * 1000.0), 1000));
			played[p.step] = (double)t / CYCLES_PER_TICK_120;
			fake_setMicro(h, 0, 0, p.step, p.micro);
			fake_setStep(h, 0, 0, p.step, true);
		}
		fake_runUntil(h, fake_msToCycles(4100.0), fake_msToCycles(0.5));
		const fake_rec_t *r[64];
		const size_t n = fake_triggers(h, 0, r, 64);
		int replayed = 0;
		for (size_t i = 0; i < n; i++)
		{
			if (r[i]->ev.tick < 1536 || r[i]->ev.tick >= 3072)
				continue;
			const double back = (double)(r[i]->ev.tick - 1536);
			ASSERT_TRUE(fabs(back - played[r[i]->ev.step]) <= 0.5 + 1e-6);
			replayed++;
		}
		ASSERT_EQ(replayed, 16);
		ASSERT_EQ(h->errors, 0);
	}
	fake_useMicro(false);
}

TEST(record_place_in_the_tracks_own_step)
{
	/* x3/4 (128 ticks) and x2 (48): the offset in 1/96 of the track's step */
	static const struct { uint8_t tempo; int at, step, micro; bool ahead; } want[] = {
		{ 4, 4 * 128 + 20, 4, 15, false },	/* 20 x 96 / 128 */
		{ 4, 4 * 128 - 10, 4, -8, true },	/* -7.5: away from zero; still step 3's boundary */
		{ 4, 4 * 128 + 70, 5, -44, true },	/* 58 before step 5: -43.5 */
		{ 8, 4 * 48 + 10, 4, 20, false },
		{ 8, 4 * 48 - 13, 4, -26, true },
	};
	for (unsigned k = 0; k < sizeof want / sizeof want[0]; k++)
	{
		fake_host_t *h = fake_init();
		h->logMask = 0;
		fake_setTempo(h, 0, 0, want[k].tempo);
		play(h, 1.0);
		const seqc_time_t t = (seqc_time_t)want[k].at * CYCLES_PER_TICK_120;
		fake_runUntil(h, t, fake_msToCycles(0.5));
		seqc_recPlace_t p;
		ASSERT_TRUE(seqc_recordPlace(0, t, &p));
		ASSERT_EQ(p.step, want[k].step);
		ASSERT_EQ(p.ahead, want[k].ahead);
		ASSERT_EQ(p.micro, want[k].micro);
	}
}

TEST(record_place_clamps_at_the_reach)
{
	/* shuffle 127 (swing 64), lyb at 200 % and delay -15 ms (-12 ticks): step 4's reference
	 * is 384 - 12 = 372, step 5's 480 + 64 + 48 - 12 = 580. A note at 470 is step 4's, 98
	 * late; one at 478 (still before step 5's boundary) is step 5's, 102 early */
	fake_host_t *h = microHost();
	h->logMask = 0;
	h->shuffle = 127;
	fake_setTemplate(h, 0, 0, LYB, 200);
	fake_setGroove(h, 0, 0, 0, 0, -15);
	play(h, 1.0);
	seqc_recPlace_t p;
	seqc_time_t t = (seqc_time_t)470 * CYCLES_PER_TICK_120;
	fake_runUntil(h, t, fake_msToCycles(0.5));
	ASSERT_TRUE(seqc_recordPlace(0, t, &p));
	ASSERT_EQ(p.step, 4);
	ASSERT_FALSE(p.ahead);
	ASSERT_EQ(p.micro, 84);
	t = (seqc_time_t)478 * CYCLES_PER_TICK_120;
	fake_runUntil(h, t, fake_msToCycles(0.5));
	ASSERT_TRUE(seqc_recordPlace(0, t, &p));
	ASSERT_EQ(p.step, 5);
	ASSERT_TRUE(p.ahead);
	ASSERT_EQ(p.micro, -84);
	fake_useMicro(false);
}

TEST(record_place_stamp_before_the_boundary)
{
	/* the core is past step 4's boundary; a note stamped 2 ms (1.536 ticks) before it is
	 * step 4's, early by 2 ticks, not step 3's and not held at the core's position */
	fake_host_t *h = fake_init();
	h->logMask = 0;
	play(h, 1.0);
	fake_runUntil(h, (seqc_time_t)389 * CYCLES_PER_TICK_120, fake_msToCycles(0.5));
	ASSERT_EQ(seqc_trackStep(0), 4);
	seqc_setTempo(120.0f);			/* re-anchors the timeline now: the stamp lies before the anchor */
	seqc_recPlace_t p;
	ASSERT_TRUE(seqc_recordPlace(0, (seqc_time_t)384 * CYCLES_PER_TICK_120 - fake_msToCycles(2.0), &p));
	ASSERT_EQ(p.step, 4);
	ASSERT_FALSE(p.ahead);
	ASSERT_EQ(p.micro, -2);
}

TEST(record_place_follows_the_slave_interpolation)
{
	/* MIDI clock at 120 BPM: clock m is at tick 16 m. After clock 30 (tick 480, step 5) a
	 * note 0.4 clocks later is +6.4 ticks, one 0.3 clocks before clock 30 is -4.8 */
	fake_host_t *h = fake_init();
	h->logMask = 0;
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	fake_process(h, fake_msToCycles(1.0));
	fake_sync(h, SEQC_EV_START, h->now + 100);
	const double per = fake_clockCycles(120.0);
	const seqc_time_t first = h->now + (seqc_time_t)per;
	fake_runClocks(h, SEQC_EV_CLOCK, first, per, 31, fake_msToCycles(0.1));
	const seqc_time_t clock30 = first + (seqc_time_t)(30.0 * per + 0.5);
	ASSERT_EQ(seqc_trackStep(0), 5);
	seqc_recPlace_t p;
	ASSERT_TRUE(seqc_recordPlace(0, clock30 + (seqc_time_t)(0.4 * per), &p));
	ASSERT_EQ(p.step, 5);
	ASSERT_EQ(p.micro, 6);
	ASSERT_TRUE(seqc_recordPlace(0, clock30 - (seqc_time_t)(0.3 * per), &p));
	ASSERT_EQ(p.step, 5);
	ASSERT_EQ(p.micro, -5);
}

static bool placedInCallback;
static void tryPlace(fake_host_t *h, const seqc_event_t *ev)
{
	(void)ev;
	seqc_recPlace_t p;
	placedInCallback |= seqc_recordPlace(0, h->now, &p);
}

TEST(record_place_false_when_it_cannot_place)
{
	fake_host_t *h = fake_init();
	h->logMask = 0;
	seqc_recPlace_t p = { .step = 77, .ahead = true, .micro = 33 };
	fake_process(h, 0);
	ASSERT_FALSE(seqc_recordPlace(0, 0, &p));	/* stopped */
	seqc_start();
	ASSERT_FALSE(seqc_recordPlace(0, 0, &p));	/* no step yet */
	ASSERT_EQ(p.step, 77);
	ASSERT_EQ(p.micro, 33);				/* *out not written */
	fake_process(h, 0);
	ASSERT_TRUE(seqc_recordPlace(0, 0, &p));
	ASSERT_EQ(p.step, 0);
	ASSERT_FALSE(seqc_recordPlace(7, 0, &p));	/* no such track */
	ASSERT_FALSE(seqc_recordPlace(0, 0, NULL));
	fake_setStep(h, 0, 0, 2, true);
	placedInCallback = false;
	h->onTrigger = tryPlace;
	fake_runUntil(h, fake_msToCycles(400.0), fake_msToCycles(0.5));
	ASSERT_EQ(h->calls[FAKE_TRIGGER], 1);
	ASSERT_FALSE(placedInCallback);			/* inside a callback */
}

TEST(record_place_false_while_armed)
{
	/* review of Task 7: MIDI slave, a song position to step 8 while stopped, then Continue: the
	 * core is armed (the track index is set) until the next clock, and places nothing */
	fake_host_t *h = fake_init();
	h->logMask = 0;
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	fake_process(h, fake_msToCycles(1.0));
	seqc_songPosition(8);
	ASSERT_EQ(seqc_trackStep(0), 8);
	fake_sync(h, SEQC_EV_CONTINUE, h->now + 100);
	fake_process(h, h->now + fake_msToCycles(1.0));
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	seqc_recPlace_t p = { .step = 77, .ahead = true, .micro = 33 };
	ASSERT_FALSE(seqc_recordPlace(0, h->now, &p));
	ASSERT_EQ(p.step, 77);
}

TEST(record_place_template_across_the_loop_wrap)
{
	/* review of Task 7: 13 steps with lay; at step 12 of loop 0 (q 12, 0 ticks) the next step is
	 * step 0 of loop 1, q 13 (+18): its reference is 1248 + 18, so a note at 1240 is 26 early
	 * there (q 0 would read it as 8 early) */
	fake_host_t *h = microHost();
	h->logMask = 0;
	fake_setLength(h, 0, 0, 13);
	fake_setTemplate(h, 0, 0, LAY, 100);
	play(h, 1.0);
	const seqc_time_t t = (seqc_time_t)1240 * CYCLES_PER_TICK_120;
	fake_runUntil(h, t, fake_msToCycles(0.5));
	ASSERT_EQ(seqc_trackStep(0), 12);
	seqc_recPlace_t p;
	ASSERT_TRUE(seqc_recordPlace(0, t, &p));
	ASSERT_TRUE(p.ahead);
	ASSERT_EQ(p.step, 0);
	ASSERT_EQ(p.micro, -26);
	fake_useMicro(false);
}

TEST(record_place_slave_cap_and_dropout)
{
	/* review of Task 7: MIDI clock at 120 BPM, after clock 30 (tick 480, step 5). A stamp 1.5
	 * clocks later reads the next clock's position, 496: +16, not +24. After a dropout (no clock
	 * for 600 ms) the position is seqc.pos, which held at 496, for any stamp: also for one 0.3
	 * clocks before clock 30, where the interpolation reads 475.2 (-5) */
	fake_host_t *h = fake_init();
	h->logMask = 0;
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	fake_process(h, fake_msToCycles(1.0));
	fake_sync(h, SEQC_EV_START, h->now + 100);
	const double per = fake_clockCycles(120.0);
	const seqc_time_t first = h->now + (seqc_time_t)per;
	fake_runClocks(h, SEQC_EV_CLOCK, first, per, 31, fake_msToCycles(0.1));
	const seqc_time_t clock30 = first + (seqc_time_t)(30.0 * per + 0.5);
	ASSERT_EQ(seqc_trackStep(0), 5);
	seqc_recPlace_t p;
	ASSERT_TRUE(seqc_recordPlace(0, clock30 + (seqc_time_t)(1.5 * per), &p));
	ASSERT_EQ(p.step, 5);
	ASSERT_FALSE(p.ahead);
	ASSERT_EQ(p.micro, 16);
	fake_runUntil(h, clock30 + fake_msToCycles(600.0), fake_msToCycles(1.0));
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_TRUE(seqc_recordPlace(0, clock30 - (seqc_time_t)(0.3 * per), &p));
	ASSERT_EQ(p.step, 5);
	ASSERT_EQ(p.micro, 16);
}

TEST(record_place_keeps_mu_within_the_reach_of_the_template)
{
	/* review of Task 7 (M19): playback clamps P = mu + G to +-84 at x1, so the stored mu is held
	 * to [-84 - G, 84 - G]. Shuffle 127 (swing 64) and drg at 200 %: step 2's G is +12, step 3's
	 * reference 288 + 64 = 352. A note at 192 + 12 + 73 = 277 is step 2's (73 < 75): mu 72 */
	fake_host_t *h = microHost();
	h->logMask = 0;
	h->shuffle = 127;
	fake_setTemplate(h, 0, 0, 7, 200);
	play(h, 1.0);
	const seqc_time_t t = (seqc_time_t)277 * CYCLES_PER_TICK_120;
	fake_runUntil(h, t, fake_msToCycles(0.5));
	seqc_recPlace_t p;
	ASSERT_TRUE(seqc_recordPlace(0, t, &p));
	ASSERT_EQ(p.step, 2);
	ASSERT_FALSE(p.ahead);
	ASSERT_EQ(p.micro, 72);
	fake_useMicro(false);
}

TEST_MAIN()
