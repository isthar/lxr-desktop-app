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

TEST_MAIN()
