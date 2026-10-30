/*
 * test_perform.c
 *
 * Performance and acid of LICHOMAT v0.3 in seq_core: roll rates (RL), the slide
 * flag (SL); plan P1, P2. RL1: the sub-step and triplet rolls at x1 and the roll
 * code; RL2: a triplet roll at x3/4 and the swing of sub-step hits.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "fake_host.h"
#include "seqc_test.h"

static const fake_rec_t *trig[64];

/* start at time 0 and run for ms at the fake host's 120 BPM (96 ticks = 125 ms) */
static void play(fake_host_t *h, double ms)
{
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, fake_msToCycles(ms), fake_msToCycles(0.1));
}

TEST(RL1_sub_step_and_triplet_rolls_at_x1)
{
	/* track 3 at x1, no steps: the roll hits of ticks 0..767 (8 steps) */
	static const struct { uint8_t rate; uint8_t hits; size_t n; uint16_t gap; } cases[] = {
		{ SEQC_ROLL(1, 2), 2, 16, 48 },		/* 1/32 */
		{ SEQC_ROLL(2, 3), 3, 12, 64 },		/* 1/24 */
		{ SEQC_ROLL(4, 3), 3, 6, 128 },		/* 1/12 */
		{ SEQC_ROLL(8, 3), 3, 3, 256 },		/* 1/6 */
		{ SEQC_ROLL(16, 3), 3, 2, 512 },	/* 1/3: 0 and 512 of 0, 512, 1024 */
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
	{
		fake_host_t *h = fake_init();
		h->rollRate[3] = cases[i].rate;
		play(h, 8 * 125.0 - 1.0);
		const size_t n = fake_triggers(h, 3, trig, 64);
		ASSERT_EQ(n, cases[i].n);
		for (size_t k = 0; k < n; k++)
		{
			const seqc_event_t *ev = &trig[k]->ev;
			ASSERT_EQ(ev->tick, (int64_t)(k * cases[i].gap));
			ASSERT_EQ(ev->step, (uint8_t)(k * cases[i].gap / 96));
			ASSERT_EQ(ev->hit, (uint8_t)(k % cases[i].hits));
			ASSERT_EQ(ev->flags, SEQC_EVF_ROLL);
			ASSERT_EQ(ev->vel, SEQC_ROLL_VELOCITY);
		}
		ASSERT_EQ(h->errors, 0);
	}
}

TEST(RL1_old_rates_are_span_with_one_hit)
{
	ASSERT_EQ(SEQC_ROLL(16, 1), 16);
	ASSERT_EQ(SEQC_ROLL(8, 1), 8);
	ASSERT_EQ(SEQC_ROLL(1, 1), 1);
	ASSERT_EQ(SEQC_ROLL_SPAN(SEQC_ROLL(16, 3)), 16);
	ASSERT_EQ(SEQC_ROLL_HITS(SEQC_ROLL(1, 2)), 2);
	ASSERT_TRUE(SEQC_ROLL(16, 8) != 0xFF);	/* never the one-shot code of sequencer.c */
}

TEST(RL2_triplet_roll_at_x3_4_is_exact)
{
	/* x3/4: L = 128 ticks; 1/12 = 3 hits every 4 steps (512 ticks) at floor(j * 512 / 3) */
	fake_host_t *h = fake_init();
	fake_setTempo(h, 0, 3, 4);
	h->rollRate[3] = SEQC_ROLL(4, 3);
	play(h, 8 * 125.0 * 4.0 / 3.0 - 1.0);	/* 8 own steps: ticks 0..1023 */
	static const int64_t want[6] = { 0, 170, 341, 512, 682, 853 };
	static const uint8_t step[6] = { 0, 1, 2, 4, 5, 6 };
	ASSERT_EQ(fake_triggers(h, 3, trig, 64), 6u);
	for (size_t k = 0; k < 6; k++)
	{
		ASSERT_EQ(trig[k]->ev.tick, want[k]);
		ASSERT_EQ(trig[k]->ev.step, step[k]);
	}
	ASSERT_EQ(h->errors, 0);
}

TEST(RL2_sub_step_hits_take_their_steps_swing)
{
	/* shuffle 63 at x1: odd steps move by 32 ticks; 1/32 hits at 0, 48 | 96+32, 144+32 | 192, 240.
	 * (At 127 the swing of 64 puts step 1's second hit at 208, after step 2's first hit at 192,
	 * which chokes it: the core's per-track choke.) */
	fake_host_t *h = fake_init();
	h->shuffle = 63;	/* the field T13_roll_is_swung sets */
	h->rollRate[3] = SEQC_ROLL(1, 2);
	play(h, 2 * 125.0 + 90.0);
	static const int64_t want[6] = { 0, 48, 128, 176, 192, 240 };
	ASSERT_EQ(fake_triggers(h, 3, trig, 64), 6u);
	for (size_t k = 0; k < 6; k++)
		ASSERT_EQ(trig[k]->ev.tick, want[k]);
}

TEST_MAIN()
