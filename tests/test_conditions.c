/*
 * test_conditions.c
 *
 * TC: the trig conditions of LICHOMAT v0.2 in Step.prob (plan V5). Step.prob
 * 101..115 is a trig condition instead of a probability: A:B on the track's loop
 * count, FILL and !FILL on fill mode (seqc_setFill()), FIRST and !FIRST, PRE and
 * !PRE on the result of the track's latest conditional trig; 116..255 play like
 * 100. TC1: the loop count conditions and their reset at a pattern change; TC2:
 * PRE and !PRE; TC3: a track at another tempo and fill mode.
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

static void play(fake_host_t *h, double ms)
{
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, fake_msToCycles(ms), fake_msToCycles(0.1));
}

static uint32_t changeAt;
static void askPattern1(fake_host_t *h, uint32_t absStep)
{
	if (absStep == changeAt)
		fake_setNextPattern(h, 1);
}

TEST(TC1_loop_count_conditions)
{
	/* track 0, 4 steps, a trig on step 0 with the code; 24 steps: loops 0..5. The mask
	 * has bit l set if loop l plays */
	static const struct { uint8_t code; uint32_t mask; } cases[] = {
		{ 101, 0x15 }, { 102, 0x2A }, { 103, 0x09 }, { 104, 0x12 }, { 105, 0x24 },	/* 1:2 2:2 1:3 2:3 3:3 */
		{ 106, 0x11 }, { 107, 0x22 }, { 108, 0x04 }, { 109, 0x08 },			/* 1:4 2:4 3:4 4:4 */
		{ 110, 0x00 }, { 111, 0x3F }, { 112, 0x01 }, { 113, 0x3E },			/* FILL !FILL FIRST !FIRST */
		{ 116, 0x3F }, { 127, 0x3F }, { 100, 0x3F },					/* like 100 */
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
	{
		fake_host_t *h = fake_init();
		fake_setLength(h, 0, 0, 4);
		fake_setStep(h, 0, 0, 0, true);
		fake_setStepData(h, 0, 0, 0, 100, 60, cases[i].code);
		play(h, 2990.0);
		const fake_rec_t *r[8];
		const size_t n = fake_triggers(h, 0, r, 8);
		uint32_t mask = 0;
		for (size_t k = 0; k < n && k < 8; k++)
			mask |= 1u << (r[k]->ev.absStep / 4);
		ASSERT_EQ(mask, cases[i].mask);
	}
}

TEST(TC1_restart_resets_the_loop_count)
{
	fake_host_t *h = fake_init();
	for (uint8_t p = 0; p < 2; p++)
	{
		fake_setLength(h, p, 0, 4);
		fake_setStep(h, p, 0, 0, true);
		fake_setStepData(h, p, 0, 0, 100, 60, STEP_COND_FIRST);
	}
	changeAt = 6;
	h->onStepBoundary = askPattern1;	/* the change at abs 16 */
	play(h, 2990.0);
	const fake_rec_t *r[8];
	ASSERT_EQ(fake_triggers(h, 0, r, 8), 2);
	ASSERT_EQ(r[0]->ev.absStep, 0);
	ASSERT_EQ(r[1]->ev.tick, 1536);
	ASSERT_EQ(r[1]->ev.pattern, 1);
}

TEST(TC2_pre_follows_the_last_condition)
{
	fake_host_t *h = fake_init();
	/* track 0: 1:2, PRE, !PRE on steps 0..2 of 4. Loops 0 and 2: 1:2 plays, so PRE
	 * plays and !PRE does not; loops 1 and 3 the other way round */
	fake_setLength(h, 0, 0, 4);
	static const uint8_t code[3] = { STEP_COND_1_2, STEP_COND_PRE, STEP_COND_NOT_PRE };
	for (uint8_t s = 0; s < 3; s++)
	{
		fake_setStep(h, 0, 0, s, true);
		fake_setStepData(h, 0, 0, s, 100, 60, code[s]);
	}
	/* track 2: PRE and !PRE without a condition before them: PRE is false */
	fake_setLength(h, 0, 2, 4);
	fake_setStep(h, 0, 2, 0, true);
	fake_setStepData(h, 0, 2, 0, 100, 60, STEP_COND_PRE);
	fake_setStep(h, 0, 2, 1, true);
	fake_setStepData(h, 0, 2, 1, 100, 60, STEP_COND_NOT_PRE);
	play(h, 1990.0);			/* steps 0..15 */
	const fake_rec_t *r[16];
	static const uint32_t want[6] = { 0, 1, 6, 8, 9, 14 };
	ASSERT_EQ(fake_triggers(h, 0, r, 16), 6);
	for (int k = 0; k < 6; k++)
		ASSERT_EQ(r[k]->ev.absStep, want[k]);
	ASSERT_EQ(fake_triggers(h, 2, r, 16), 4);
	for (int k = 0; k < 4; k++)
		ASSERT_EQ(r[k]->ev.step, 1);
}

TEST(TC2_probability_counts_as_a_condition)
{
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 0, 4);
	fake_setStep(h, 0, 0, 0, true);
	fake_setStepData(h, 0, 0, 0, 100, 60, 50);
	fake_setStep(h, 0, 0, 1, true);
	fake_setStepData(h, 0, 0, 1, 100, 60, STEP_COND_PRE);
	/* the draws: step 0 r = 1 (plays), step 1, step 4 r = 100 (does not), step 5 */
	static const uint32_t script[4] = { 0, 0, 99, 0 };
	fake_scriptRandom(h, script, 4);
	play(h, 990.0);				/* steps 0..7 */
	const fake_rec_t *r[8];
	ASSERT_EQ(fake_triggers(h, 0, r, 8), 2);
	ASSERT_EQ(r[0]->ev.absStep, 0);
	ASSERT_EQ(r[1]->ev.absStep, 1);
}

TEST(TC3_own_grid_loops_and_fill)
{
	/* track 1 at x2 (48 ticks), 4 steps: 1:2 on step 0 plays in loops 0 and 2, at 0 and 384 */
	fake_host_t *h = fake_init();
	fake_setTempo(h, 0, 1, 8);
	fake_setLength(h, 0, 1, 4);
	fake_setStep(h, 0, 1, 0, true);
	fake_setStepData(h, 0, 1, 0, 100, 60, STEP_COND_1_2);
	/* track 2: FILL on step 0 and !FILL on step 1, fill mode on */
	fake_setLength(h, 0, 2, 4);
	fake_setStep(h, 0, 2, 0, true);
	fake_setStepData(h, 0, 2, 0, 100, 60, STEP_COND_FILL);
	fake_setStep(h, 0, 2, 1, true);
	fake_setStepData(h, 0, 2, 1, 100, 60, STEP_COND_NOT_FILL);
	seqc_setFill(true);
	play(h, 590.0);				/* 453 ticks */
	const fake_rec_t *r[8];
	ASSERT_EQ(fake_triggers(h, 1, r, 8), 2);
	ASSERT_EQ(r[0]->ev.tick, 0);
	ASSERT_EQ(r[1]->ev.tick, 384);
	ASSERT_EQ(fake_triggers(h, 2, r, 8), 2);	/* FILL on steps 0 and 4 */
	ASSERT_EQ(r[1]->ev.absStep, 4);
	/* fill off (the default): !FILL plays, FILL does not */
	h = fake_init();
	fake_setLength(h, 0, 2, 4);
	fake_setStep(h, 0, 2, 0, true);
	fake_setStepData(h, 0, 2, 0, 100, 60, STEP_COND_FILL);
	fake_setStep(h, 0, 2, 1, true);
	fake_setStepData(h, 0, 2, 1, 100, 60, STEP_COND_NOT_FILL);
	play(h, 590.0);
	ASSERT_EQ(fake_triggers(h, 2, r, 8), 1);
	ASSERT_EQ(r[0]->ev.absStep, 1);
}

TEST_MAIN()
