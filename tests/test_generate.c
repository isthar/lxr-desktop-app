/*
 * test_generate.c
 *
 * Event generation (spec 6.1, N3):
 * - T2: the straight grid, and the step data each event carries (D5);
 * - T3: swing moves every hit of a swung step, ratchets included (D1);
 * - T4: shift -7..+7 on every ratchet type, early hits included;
 * - T3, T4: a shuffle change or a timing edit between the early and the late
 *   part of a step plays no hit twice and loses none (the early part fixes the
 *   timing, decision 9 of epic E1);
 * - T8: probability is deterministic with a seeded generator, and the hits of a
 *   step play all together or not at all.
 * The steps of one track never overlap in these tests, so the choke (T7) does
 * not take part.
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

static const fake_rec_t *trig[4096];

/* start at time 0 and run at 120 BPM until just before the boundary of step end */
static void runSteps(fake_host_t *h, uint32_t end)
{
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * end) - 1, fake_msToCycles(0.1));
}

/* the hit ticks of one step, as the spec defines them */
static int64_t hitTick(uint32_t n, int swing, int shiftUnits, int hit, int hits)
{
	return (int64_t)n * 96 + ((n & 1u) ? swing : 0) + shiftUnits * 12 + hit * 96 / hits;
}

TEST(T2_straight_grid)
{
	fake_host_t *h = fake_init();
	static const uint8_t steps[] = { 0, 3, 4, 9, 15 };
	for (size_t i = 0; i < sizeof steps; i++)
		fake_setStep(h, 0, 2, steps[i], true);
	runSteps(h, 32);
	ASSERT_EQ(fake_count(h, FAKE_TRIGGER), 10);
	const size_t n = fake_triggers(h, 2, trig, 64);
	ASSERT_EQ(n, 10);
	for (size_t i = 0; i < n; i++)
	{
		const seqc_event_t *ev = &trig[i]->ev;
		const uint32_t abs = (uint32_t)(i / 5 * 16 + steps[i % 5]);
		ASSERT_EQ(ev->absStep, abs);
		ASSERT_EQ(ev->tick, (int64_t)abs * 96);
		ASSERT_EQ(ev->step, steps[i % 5]);
		ASSERT_EQ(ev->track, 2);
		ASSERT_EQ(ev->pattern, 0);
		ASSERT_EQ(ev->hit, 0);
		ASSERT_EQ(ev->flags, 0);
		ASSERT_EQ(ev->vel, 127);
		ASSERT_EQ(ev->note, FAKE_DEFAULT_NOTE);
		/* fired by the first call at or after its time */
		const double late = (double)trig[i]->time - abs * fake_stepCycles(120.0);
		ASSERT_TRUE(late > -1.0);
		ASSERT_TRUE(late < fake_msToCycles(0.1));
	}
	ASSERT_EQ(h->errors, 0);
}

TEST(T2_event_carries_its_step_data)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 6, 5, true);
	fake_setStepData(h, 0, 6, 5, 90, 64, 100);
	h->steps[0][6][5].volume |= FAKE_STEP_ACTIVE_MASK;	/* the active bit is not velocity */
	fake_setPlocks(h, 0, 6, 5, 12, 34, 56, 78);
	runSteps(h, 8);
	ASSERT_EQ(fake_triggers(h, 6, trig, 4), 1);
	const seqc_event_t *ev = &trig[0]->ev;
	ASSERT_EQ(ev->vel, 90);
	ASSERT_EQ(ev->note, 64);
	ASSERT_EQ(ev->p1Nr, 12);
	ASSERT_EQ(ev->p1Val, 34);
	ASSERT_EQ(ev->p2Nr, 56);
	ASSERT_EQ(ev->p2Val, 78);
	ASSERT_EQ(ev->reserved, 0);
}

TEST(T2_flam_out_of_range_plays_one_hit)
{
	static const uint8_t flams[] = { 6, 9, 15 };	/* 6: the first value past the table */
	for (size_t i = 0; i < sizeof flams; i++)
	{
		fake_host_t *h = fake_init();
		fake_setStep(h, 0, 1, 2, true);
		fake_setTiming(h, 0, 1, 2, flams[i], 0);
		runSteps(h, 4);
		ASSERT_EQ(fake_triggers(h, 1, trig, 4), 1);
		ASSERT_EQ(trig[0]->ev.tick, 192);
	}
}

/* one step per track, so that no two steps of a track overlap */
static void setUpSwing(fake_host_t *h, uint8_t shuffle)
{
	h->shuffle = shuffle;
	fake_setStep(h, 0, 0, 1, true);			/* odd: x4, +1 */
	fake_setTiming(h, 0, 0, 1, 3, 1);
	fake_setStep(h, 0, 1, 2, true);			/* even: x3, -1 */
	fake_setTiming(h, 0, 1, 2, 2, -1);
	fake_setStep(h, 0, 2, 5, true);			/* odd: one hit, -7 */
	fake_setTiming(h, 0, 2, 5, 0, -7);
	fake_setStep(h, 0, 3, 6, true);			/* even: x8 */
	fake_setTiming(h, 0, 3, 6, 5, 0);
	fake_setStep(h, 0, 4, 9, true);			/* odd: x8, +7: spills into step 10 */
	fake_setTiming(h, 0, 4, 9, 5, 7);
}

static void checkSwing(uint8_t shuffle, int swing)
{
	fake_host_t *h = fake_init();
	setUpSwing(h, shuffle);
	runSteps(h, 16);
	static const struct { uint8_t track, step; int shift, hits; } spec[] = {
		{ 0, 1, 1, 4 }, { 1, 2, -1, 3 }, { 2, 5, -7, 1 }, { 3, 6, 0, 8 }, { 4, 9, 7, 8 },
	};
	for (size_t i = 0; i < sizeof spec / sizeof spec[0]; i++)
	{
		const size_t n = fake_triggers(h, spec[i].track, trig, 16);
		ASSERT_EQ(n, (size_t)spec[i].hits);
		for (int j = 0; j < spec[i].hits; j++)
		{
			ASSERT_EQ(trig[j]->ev.absStep, spec[i].step);
			ASSERT_EQ(trig[j]->ev.hit, j);
			ASSERT_EQ(trig[j]->ev.tick, hitTick(spec[i].step, swing, spec[i].shift, j, spec[i].hits));
		}
	}
	ASSERT_EQ(h->errors, 0);
}

TEST(T3_swing_0) { checkSwing(0, 0); }
TEST(T3_swing_1) { checkSwing(1, 1); }			/* 0.504 rounds up, floor() gives 0 */
TEST(T3_swing_32) { checkSwing(32, 16); }		/* round(32 * 64 / 127) */
TEST(T3_swing_64) { checkSwing(64, 32); }
TEST(T3_swing_97) { checkSwing(97, 49); }		/* 48.88 rounds up */
TEST(T3_swing_126) { checkSwing(126, 63); }		/* 63.496 rounds down, not up to 64 */
TEST(T3_swing_127) { checkSwing(127, 64); }		/* 2/3 of a step */
TEST(T3_shuffle_above_127_counts_as_127) { checkSwing(200, 64); }

TEST(T3_parity_counts_from_the_start)
{
	fake_host_t *h = fake_init();
	h->shuffle = 127;
	fake_setLength(h, 0, 5, 7);			/* step 0 of a 7-step track: odd at 7, 21 */
	fake_setStep(h, 0, 5, 0, true);
	runSteps(h, 29);
	ASSERT_EQ(fake_triggers(h, 5, trig, 8), 5);
	ASSERT_EQ(trig[0]->ev.tick, 0);
	ASSERT_EQ(trig[1]->ev.tick, 7 * 96 + 64);
	ASSERT_EQ(trig[2]->ev.tick, 14 * 96);
	ASSERT_EQ(trig[3]->ev.tick, 21 * 96 + 64);
	ASSERT_EQ(trig[4]->ev.tick, 28 * 96);
}

TEST(T3_shuffle_is_read_at_every_boundary)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 1, true);
	fake_setStep(h, 0, 0, 3, true);
	h->shuffle = 64;
	runSteps(h, 2);					/* step 1 is generated at boundary 1 */
	h->shuffle = 127;
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 4), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 0, trig, 4), 2);
	ASSERT_EQ(trig[0]->ev.tick, 96 + 32);
	ASSERT_EQ(trig[1]->ev.tick, 3 * 96 + 64);
}

TEST(T3_shuffle_change_before_the_boundary_applies)
{
	/* odd step 3 without shift: boundary 2 queues no early hit, so the late part
	 * takes the shuffle read at boundary 3 itself (decision 9) */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 3, true);
	runSteps(h, 3);
	h->shuffle = 127;
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 5), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 0, trig, 4), 1);
	ASSERT_EQ(trig[0]->ev.tick, 3 * 96 + 64);
}

TEST(T4_shift_on_every_ratchet_type)
{
	for (uint8_t flam = 0; flam < 6; flam++)
	{
		const int hits = (int[]){ 1, 2, 3, 4, 6, 8 }[flam];
		for (int shift = -7; shift <= 8; shift++)
		{
			/* 8 stands for the raw value 15, which counts as 0 */
			fake_host_t *h = fake_init();
			fake_setStep(h, 0, 3, 4, true);
			if (shift <= 7)
				fake_setTiming(h, 0, 3, 4, flam, (int8_t)shift);
			else
				h->timing[0][3][4] = (StepTiming){ .flam = flam, .shift = 15 };
			runSteps(h, 8);
			const size_t n = fake_triggers(h, 3, trig, 16);
			ASSERT_EQ(n, (size_t)hits);
			for (int j = 0; j < hits; j++)
			{
				ASSERT_EQ(trig[j]->ev.tick, hitTick(4, 0, shift <= 7 ? shift : 0, j, hits));
				ASSERT_EQ(trig[j]->ev.absStep, 4);
				ASSERT_EQ(trig[j]->ev.hit, j);
			}
			ASSERT_EQ(h->errors, 0);
		}
	}
}

TEST(T4_negative_shift_on_step_0_of_the_next_loop)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 0, true);
	fake_setTiming(h, 0, 2, 0, 1, -3);		/* x2: -36 and +12 */
	fake_setStep(h, 0, 3, 0, true);
	fake_setTiming(h, 0, 3, 0, 0, -7);		/* one hit at -84 */
	runSteps(h, 40);
	/* after the start, step 0 has no early hits: they would be before tick 0 */
	size_t n = fake_triggers(h, 2, trig, 16);
	ASSERT_EQ(n, 5);
	ASSERT_EQ(trig[0]->ev.tick, 12);
	ASSERT_EQ(trig[0]->ev.hit, 1);
	ASSERT_EQ(trig[1]->ev.tick, 16 * 96 - 36);
	ASSERT_EQ(trig[1]->ev.absStep, 16);
	ASSERT_EQ(trig[1]->ev.step, 0);
	ASSERT_EQ(trig[1]->ev.hit, 0);
	ASSERT_EQ(trig[2]->ev.tick, 16 * 96 + 12);
	ASSERT_EQ(trig[3]->ev.tick, 32 * 96 - 36);
	ASSERT_EQ(trig[4]->ev.tick, 32 * 96 + 12);
	n = fake_triggers(h, 3, trig, 16);
	ASSERT_EQ(n, 2);
	ASSERT_EQ(trig[0]->ev.tick, 16 * 96 - 84);
	ASSERT_EQ(trig[0]->ev.absStep, 16);
	ASSERT_EQ(trig[1]->ev.tick, 32 * 96 - 84);
}

TEST(T4_late_hits_spill_into_the_next_steps)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 3, true);
	fake_setTiming(h, 0, 0, 3, 5, 7);		/* x8, +84: up to 168 ticks after step 3 */
	runSteps(h, 6);
	ASSERT_EQ(fake_triggers(h, 0, trig, 16), 8);
	ASSERT_EQ(trig[0]->ev.tick, 3 * 96 + 84);
	ASSERT_EQ(trig[7]->ev.tick, 3 * 96 + 168);
	ASSERT_EQ(trig[7]->ev.absStep, 3);
	/* inside trigger() the position is the event's own tick, also between
	 * boundaries */
	for (int j = 0; j < 8; j++)
		ASSERT_EQ_DBL(trig[j]->tick, (double)trig[j]->ev.tick);
}

TEST(T8_draws_come_from_the_generator)
{
	fake_host_t *h = fake_init();
	for (uint8_t s = 0; s < 16; s += 4)
	{
		fake_setStep(h, 0, 0, s, true);
		fake_setStepData(h, 0, 0, s, 127, 60, 50);
	}
	/* r = 1 + value % 100, and the step plays when r <= prob */
	static const uint32_t script[] = { 49, 50, 100, 199 };
	fake_scriptRandom(h, script, 4);
	runSteps(h, 15);				/* step 16 would be drawn at boundary 15 */
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 4);	/* once per occurrence */
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 2);
	ASSERT_EQ(trig[0]->ev.absStep, 0);		/* r = 50 */
	ASSERT_EQ(trig[1]->ev.absStep, 8);		/* r = 1 */
}

/* every step of every track at prob 50, run 64 steps; returns the trigger count */
static size_t runRandom(fake_host_t *h, uint64_t seed, int64_t *ticks, size_t max)
{
	fake_seed(h, seed);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		for (uint8_t s = 0; s < 16; s++)
		{
			fake_setStep(h, 0, t, s, true);
			fake_setStepData(h, 0, t, s, 100, 60, 50);
		}
	runSteps(h, 64);
	const size_t n = fake_triggers(h, -1, trig, 4096);
	for (size_t i = 0; i < n && i < max; i++)
		ticks[i] = trig[i]->ev.tick * 8 + trig[i]->ev.track;
	return n;
}

TEST(T8_probability_is_deterministic)
{
	static int64_t a[512], b[512], c[512];
	const size_t na = runRandom(fake_init(), 1234, a, 512);
	const size_t nb = runRandom(fake_init(), 1234, b, 512);
	const size_t nc = runRandom(fake_init(), 99, c, 512);
	ASSERT_EQ(na, nb);
	for (size_t i = 0; i < na; i++)
		ASSERT_EQ(a[i], b[i]);
	/* about half of the 448 occurrences play */
	ASSERT_TRUE(na > 448 / 4 && na < 448 * 3 / 4);
	bool differs = na != nc;
	for (size_t i = 0; i < na && i < nc && !differs; i++)
		differs = a[i] != c[i];
	ASSERT_TRUE(differs);
}

TEST(T8_ratchet_hits_play_all_or_none)
{
	fake_host_t *h = fake_init();
	fake_seed(h, 7);
	fake_setLength(h, 0, 0, 4);
	fake_setStep(h, 0, 0, 2, true);
	fake_setStepData(h, 0, 0, 2, 100, 60, 50);
	fake_setTiming(h, 0, 0, 2, 5, -3);		/* x8, -36: 3 early hits, 5 late */
	runSteps(h, 400);
	const size_t n = fake_triggers(h, 0, trig, 4096);
	uint32_t hits[100] = { 0 };
	for (size_t i = 0; i < n; i++)
	{
		ASSERT_EQ(trig[i]->ev.absStep % 4, 2);
		hits[trig[i]->ev.absStep / 4]++;
	}
	int played = 0;
	for (int i = 0; i < 100; i++)
	{
		ASSERT_TRUE(hits[i] == 0 || hits[i] == 8);
		played += hits[i] == 8;
	}
	ASSERT_TRUE(played > 20 && played < 80);
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 100);
}

/* x8 at -3 units on even step 2: hits 0..2 (156, 168, 180)
 * are early, 3..7 (192..240) late. edit runs after the early hits played,
 * before boundary 2. The triggers of track 0 end up in trig[]. */
static void runAfterTheEarlyHits(void (*edit)(fake_host_t *h), size_t hits)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 2, true);
	fake_setTiming(h, 0, 0, 2, 5, -3);
	runSteps(h, 2);					/* until just before boundary 2 */
	ASSERT_EQ(fake_triggers(h, 0, trig, 16), 3);
	edit(h);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 4), fake_msToCycles(0.1));
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 1);	/* one draw for the occurrence */
	ASSERT_EQ(fake_triggers(h, 0, trig, 16), hits);
}
static void setProb0(fake_host_t *h) { fake_setStepData(h, 0, 0, 2, 127, 60, 0); }
static void switchOff(fake_host_t *h) { fake_setStep(h, 0, 0, 2, false); }

TEST(T8_begun_step_plays_all_its_hits_after_a_prob_edit)
{
	/* the early hits played: the occurrence has begun, and its late hits
	 * follow, whatever the probability reads at its boundary */
	runAfterTheEarlyHits(setProb0, 8);
	if (seqc_test_failed_)
		return;
	ASSERT_EQ(trig[7]->ev.tick, 2 * 96 + 48);
}

TEST(T8_begun_step_plays_all_its_hits_after_it_is_switched_off)
{
	runAfterTheEarlyHits(switchOff, 8);
}

TEST(T8_prob_edit_before_the_boundary_applies_with_the_same_draw)
{
	/* no early hit: an edit before the boundary applies to the step, and the
	 * draw made with the early part stays (r = 40) */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 2, true);
	fake_setStepData(h, 0, 0, 2, 127, 60, 50);
	static const uint32_t script[] = { 39 };
	fake_scriptRandom(h, script, 1);
	runSteps(h, 2);					/* drawn at boundary 1: r = 40 */
	fake_setStepData(h, 0, 0, 2, 127, 60, 30);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 3), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 0, trig, 4), 0);
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 1);
}

/* x8 at -3 units on even step 2, as in runAfterTheEarlyHits(), but the early
 * part did not play: the step is off (no draw is made), or on with prob 30,
 * where the draw r = 40 fails. edit enables the step before boundary 2. The late
 * part follows the early part, so none of its hits play. */
static void runAfterDroppedEarlyHits(bool on, void (*edit)(fake_host_t *h), size_t draws)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 2, on);
	fake_setStepData(h, 0, 0, 2, 127, 60, 30);
	fake_setTiming(h, 0, 0, 2, 5, -3);
	static const uint32_t script[] = { 39 };
	fake_scriptRandom(h, script, 1);
	runSteps(h, 2);					/* until just before boundary 2 */
	ASSERT_EQ(fake_triggers(h, 0, trig, 16), 0);
	edit(h);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 4), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 0, trig, 16), 0);
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], draws);
}
static void setProb100(fake_host_t *h) { fake_setStepData(h, 0, 0, 2, 127, 60, 100); }
static void switchOnProb100(fake_host_t *h) { fake_setStep(h, 0, 0, 2, true); setProb100(h); }

TEST(T8_failed_draw_drops_the_late_hits_after_a_prob_edit)
{
	runAfterDroppedEarlyHits(true, setProb100, 1);
}

TEST(T8_early_part_of_an_off_step_drops_the_late_hits_after_it_is_switched_on)
{
	/* no draw: the step was off at boundary 1, and the late part does not
	 * evaluate it again */
	runAfterDroppedEarlyHits(false, switchOnProb100, 0);
}

TEST(T8_failed_draw_keeps_its_timing_when_an_edit_makes_it_early)
{
	/* x8 at shift 0, prob 30 with r = 40: no early hit, the draw fails. Before
	 * boundary 2 the step gets prob 100 and shift -3, which would put hits
	 * 0..2 before tick 192: the late part keeps the early part's timing and
	 * plays all 8 hits */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 2, true);
	fake_setStepData(h, 0, 0, 2, 127, 60, 30);
	fake_setTiming(h, 0, 0, 2, 5, 0);
	static const uint32_t script[] = { 39 };
	fake_scriptRandom(h, script, 1);
	runSteps(h, 2);
	ASSERT_EQ(fake_triggers(h, 0, trig, 16), 0);
	setProb100(h);
	fake_setTiming(h, 0, 0, 2, 5, -3);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 4), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 0, trig, 16), 8);
	for (int j = 0; j < 8; j++)
		ASSERT_EQ(trig[j]->ev.tick, 2 * 96 + j * 12);
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 1);
}

/* The early part of a step fixes its timing (seqc_generate()). A change of the
 * shuffle or of the step's timing between the boundary that makes the early part
 * and the step's own boundary neither plays a hit twice nor loses one. */
TEST(T3_shuffle_change_after_the_early_part_plays_once)
{
	/* odd step 1 at -1 unit: at shuffle 0 its hit is at 84, before boundary 1,
	 * so boundary 0 queues it. Shuffle 127 would put it at 148: the late part
	 * keeps the early part's timing and does not play it again. */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 1, true);
	fake_setTiming(h, 0, 2, 1, 0, -1);
	runSteps(h, 1);					/* the hit played at 84 */
	h->shuffle = 127;
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 4), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 2, trig, 4), 1);
	ASSERT_EQ(trig[0]->ev.tick, 96 - 12);
}

TEST(T3_shuffle_change_after_the_early_part_loses_nothing)
{
	/* the other way round: at shuffle 127 the hit of step 1 is at 148, so
	 * boundary 0 queues nothing. Shuffle 0 would put it at 84, which has passed
	 * when boundary 1 makes the late part: it keeps the early part's timing. */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 1, true);
	fake_setTiming(h, 0, 2, 1, 0, -1);
	h->shuffle = 127;
	runSteps(h, 1);
	h->shuffle = 0;
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 4), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 2, trig, 4), 1);
	ASSERT_EQ(trig[0]->ev.tick, 96 + 64 - 12);
}

TEST(T4_shift_edit_after_the_early_part_plays_each_hit_once)
{
	/* even step 2, x2 at -1 unit: hit 0 at 180 is early, hit 1 at 228 late. The
	 * shift goes to +1 after hit 0 played: the late part keeps the early part's
	 * timing, so hit 0 does not play again at 204. */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 2, true);
	fake_setTiming(h, 0, 2, 2, 1, -1);
	runSteps(h, 2);					/* hit 0 played at 180 */
	fake_setTiming(h, 0, 2, 2, 1, 1);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 5), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 2, trig, 4), 2);
	ASSERT_EQ(trig[0]->ev.tick, hitTick(2, 0, -1, 0, 2));
	ASSERT_EQ(trig[0]->ev.hit, 0);
	ASSERT_EQ(trig[1]->ev.tick, hitTick(2, 0, -1, 1, 2));
	ASSERT_EQ(trig[1]->ev.hit, 1);
}

TEST(T4_shift_edit_into_the_past_loses_nothing)
{
	/* even step 2 without shift: boundary 1 queues nothing early. The shift goes
	 * to -1 after tick 180, where the hit would now be: the late part keeps the
	 * early part's timing and plays it at 192. */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 2, true);
	runSteps(h, 2);
	fake_setTiming(h, 0, 2, 2, 0, -1);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 5), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 2, trig, 4), 1);
	ASSERT_EQ(trig[0]->ev.tick, 2 * 96);
}

TEST(T4_timing_edit_before_the_boundary_applies)
{
	/* an edit that leaves every hit at or after the boundary applies to the
	 * step: step 2 becomes x4 at +1 unit during step 1 */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 2, true);
	runSteps(h, 2);
	fake_setTiming(h, 0, 2, 2, 3, 1);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 5), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 2, trig, 8), 4);
	for (int j = 0; j < 4; j++)
		ASSERT_EQ(trig[j]->ev.tick, hitTick(2, 0, 1, j, 4));
}

TEST(T4_ratchet_edit_after_the_early_part_keeps_its_hits)
{
	/* even step 2, x2 at -3 units: hit 0 at 156 is early, hit 1 at 204 late. The
	 * ratchet goes to x8 after hit 0 played: the late part keeps the early
	 * part's hit count, so it plays hit 1 of 2 and no hits of x8. */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 2, true);
	fake_setTiming(h, 0, 2, 2, 1, -3);
	runSteps(h, 2);					/* hit 0 played at 156 */
	fake_setTiming(h, 0, 2, 2, 5, -3);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 5), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 2, trig, 16), 2);
	ASSERT_EQ(trig[0]->ev.tick, hitTick(2, 0, -3, 0, 2));
	ASSERT_EQ(trig[1]->ev.tick, hitTick(2, 0, -3, 1, 2));
	ASSERT_EQ(trig[1]->ev.hit, 1);
}

TEST(T2_restart_drops_the_old_run)
{
	/* step 3 at x8, +7 units: hits at 372 + 12j. A stop after hit 0 and a start
	 * from stopped rewind, which clears the queue and the draws: the old run's
	 * 7 pending hits do not fire in the new run. */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 3, true);
	fake_setTiming(h, 0, 0, 3, 5, 7);
	runSteps(h, 4);					/* hit 0 played at 372 */
	seqc_stop();
	seqc_start();
	fake_runUntil(h, h->now + (seqc_time_t)(fake_stepCycles(120.0) * 6), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 0, trig, 32), 9);
	ASSERT_EQ(trig[0]->ev.tick, hitTick(3, 0, 7, 0, 8));
	for (int i = 1; i < 9; i++)
		ASSERT_EQ(trig[i]->ev.tick, hitTick(3, 0, 7, i - 1, 8));
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 2);	/* the new run draws step 3 again */
}

TEST_MAIN()
