/*
 * test_lookahead.c
 *
 * One step of lookahead (spec 6.2, 6.5, N3):
 * - T11: the early hits of the first step of the next pattern come from that
 *   pattern (peekNextPattern()); if the prediction no longer holds at the
 *   boundary, the late part follows the actual pattern with a new draw; at a
 *   song end no early part is generated; the late hits of the old pattern still
 *   play after the switch, with their own data.
 * - T12: an edit or an erase before a step's boundary applies to that step;
 *   only hits that play before their boundary use the state one step earlier.
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

static const fake_rec_t *trig[256];

static void start(fake_host_t *h)
{
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
}

/* run at 120 BPM until the time of tick (the boundary of step tick / 96 included) */
static void runToTick(fake_host_t *h, double tick)
{
	fake_runUntil(h, (seqc_time_t)(fake_tickCycles(120.0) * tick) + 1, fake_msToCycles(0.1));
}

TEST(T11_early_hit_of_the_next_pattern)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 1, 2, 0, true);			/* pattern 1, track 2, step 0 */
	fake_setTiming(h, 1, 2, 0, 0, -7);
	fake_setStepData(h, 1, 2, 0, 90, 64, 100);
	fake_setStep(h, 0, 2, 0, true);			/* pattern 0 would play at 1536 */
	start(h);
	runToTick(h, 2 * 96 + 10);
	fake_setNextPattern(h, 1);			/* pending from the next pattern end */
	runToTick(h, 18 * 96);
	const size_t n = fake_triggers(h, 2, trig, 8);
	ASSERT_EQ(n, 2);
	ASSERT_EQ(trig[0]->ev.tick, 0);			/* pattern 0, step 0 at the start */
	ASSERT_EQ(trig[1]->ev.tick, 16 * 96 - 84);
	ASSERT_EQ(trig[1]->ev.pattern, 1);
	ASSERT_EQ(trig[1]->ev.absStep, 16);
	ASSERT_EQ(trig[1]->ev.vel, 90);
	ASSERT_EQ(trig[1]->ev.note, 64);
	ASSERT_EQ(h->activePattern, 1);
	/* the peek came at boundary 15, where the master track wraps next */
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_PEEK, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->absStep, 15);
	ASSERT_EQ(r->pattern, 1);
	ASSERT_TRUE(r->flag);
	ASSERT_EQ(h->errors, 0);
}

TEST(T11_late_change_of_the_pending_pattern)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 1, 2, 0, true);			/* predicted: pattern 1, early hit */
	fake_setTiming(h, 1, 2, 0, 0, -7);
	fake_setStepData(h, 1, 2, 0, 100, 64, 100);
	fake_setStep(h, 2, 2, 0, true);			/* actual: pattern 2, x2 -2 */
	fake_setTiming(h, 2, 2, 0, 1, -2);
	fake_setStepData(h, 2, 2, 0, 100, 67, 100);
	start(h);
	runToTick(h, 2 * 96);
	fake_setNextPattern(h, 1);
	runToTick(h, 15 * 96 + 6);			/* after the peek at boundary 15 */
	const uint32_t draws = h->reads[FAKE_READ_RANDOM];
	fake_setNextPattern(h, 2);			/* changed during the last step */
	runToTick(h, 18 * 96);
	const size_t n = fake_triggers(h, 2, trig, 8);
	ASSERT_EQ(n, 2);
	/* the early hit was predicted at boundary 15 and played before the boundary */
	ASSERT_EQ(trig[0]->ev.tick, 16 * 96 - 84);
	ASSERT_EQ(trig[0]->ev.pattern, 1);
	ASSERT_EQ(trig[0]->ev.note, 64);
	/* from the boundary on, pattern 2 plays: its late hit, with a new draw */
	ASSERT_EQ(trig[1]->ev.tick, 16 * 96 - 24 + 48);
	ASSERT_EQ(trig[1]->ev.pattern, 2);
	ASSERT_EQ(trig[1]->ev.note, 67);
	ASSERT_EQ(trig[1]->ev.hit, 1);
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], draws + 1);
	ASSERT_EQ(h->activePattern, 2);
}

TEST(T11_ratchet_across_the_switch_keeps_its_draw)
{
	/* the prediction holds: the late hit of step 0 of pattern 1 belongs to the
	 * occurrence whose early hit was predicted at boundary 15, with its draw */
	fake_host_t *h = fake_init();
	static const uint32_t draws[] = { 0, 80 };	/* r = 1, then r = 81 */
	fake_setStep(h, 1, 2, 0, true);
	fake_setTiming(h, 1, 2, 0, 1, -2);		/* x2: -24 and +24 */
	fake_setStepData(h, 1, 2, 0, 100, 60, 50);
	start(h);
	runToTick(h, 2 * 96);
	fake_setNextPattern(h, 1);
	runToTick(h, 15 * 96 - 10);			/* before the peek at boundary 15 */
	fake_scriptRandom(h, draws, 2);
	runToTick(h, 18 * 96);
	ASSERT_EQ(fake_triggers(h, 2, trig, 8), 2);	/* both hits or neither */
	ASSERT_EQ(trig[0]->ev.tick, 16 * 96 - 24);
	ASSERT_EQ(trig[0]->ev.pattern, 1);
	ASSERT_EQ(trig[1]->ev.tick, 16 * 96 + 24);
	ASSERT_EQ(trig[1]->ev.pattern, 1);
	ASSERT_EQ(h->rngScriptPos, 1);			/* one draw, made with the early part */
}

TEST(T11_restart_takes_step_0_of_every_track)
{
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 1, 12);			/* in pattern 0, track 1 would be at step 4 */
	fake_setStep(h, 0, 1, 4, true);
	fake_setTiming(h, 0, 1, 4, 0, -1);
	fake_setStep(h, 1, 1, 0, true);
	fake_setTiming(h, 1, 1, 0, 0, -1);
	start(h);
	runToTick(h, 96);
	fake_setNextPattern(h, 1);
	runToTick(h, 17 * 96);
	ASSERT_EQ(fake_triggers(h, 1, trig, 8), 2);
	ASSERT_EQ(trig[0]->ev.absStep, 4);		/* pattern 0, step 4 at absStep 4 */
	ASSERT_EQ(trig[1]->ev.tick, 16 * 96 - 12);
	ASSERT_EQ(trig[1]->ev.pattern, 1);
	ASSERT_EQ(trig[1]->ev.step, 0);
}

TEST(T11_same_pattern_keeps_the_indices)
{
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 1, 12);
	fake_setStep(h, 0, 1, 4, true);			/* absStep 4, and 16 (16 mod 12) */
	fake_setTiming(h, 0, 1, 4, 0, -1);
	start(h);
	runToTick(h, 17 * 96);
	ASSERT_EQ(fake_triggers(h, 1, trig, 8), 2);
	ASSERT_EQ(trig[0]->ev.tick, 4 * 96 - 12);
	ASSERT_EQ(trig[1]->ev.tick, 16 * 96 - 12);
	ASSERT_EQ(trig[1]->ev.step, 4);
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_PEEK, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_FALSE(r->flag);
}

TEST(T11_reload_of_the_same_pattern_restarts_the_indices)
{
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 1, 12);			/* track 1 would be at step 4 at absStep 16 */
	fake_setStep(h, 0, 1, 0, true);
	fake_setTiming(h, 0, 1, 0, 0, -1);
	fake_setStep(h, 0, 1, 4, true);
	fake_setTiming(h, 0, 1, 4, 0, -1);
	start(h);
	runToTick(h, 96);
	fake_setNextPattern(h, 0);			/* reload: patternEnd() returns SEQC_PAT_CHANGED */
	runToTick(h, 17 * 96);
	ASSERT_EQ(fake_triggers(h, 1, trig, 8), 3);	/* absStep 4 (step 4), 12 (step 0), 16 (step 0) */
	ASSERT_EQ(trig[2]->ev.tick, 16 * 96 - 12);
	ASSERT_EQ(trig[2]->ev.pattern, 0);
	ASSERT_EQ(trig[2]->ev.step, 0);
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_PEEK, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->pattern, 0);
	ASSERT_TRUE(r->flag);
}

TEST(T11_peek_follows_the_master_track)
{
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 3, 24);			/* track 3 is the master track: the pattern ends at 24 */
	fake_setStep(h, 1, 2, 0, true);
	fake_setTiming(h, 1, 2, 0, 0, -7);
	start(h);
	runToTick(h, 96);
	fake_setNextPattern(h, 1);
	runToTick(h, 26 * 96);
	ASSERT_EQ(fake_triggers(h, 2, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.tick, 24 * 96 - 84);
	ASSERT_EQ(trig[0]->ev.pattern, 1);
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_PEEK, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->absStep, 23);			/* not at 15, where track 0 wraps */
	ASSERT_TRUE(fake_next(h, FAKE_PEEK, &pos) == NULL);
	ASSERT_EQ(h->errors, 0);
}

TEST(T11_no_early_hits_after_the_song_end)
{
	fake_host_t *h = fake_init();
	static const uint8_t song[] = { 1 };
	fake_setSong(h, song, 1);
	fake_setSongMode(h, true);
	fake_setStep(h, 1, 0, 0, true);
	fake_setTiming(h, 1, 0, 0, 0, -7);
	fake_setStep(h, 1, 0, 8, true);
	start(h);
	runToTick(h, 20 * 96);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);	/* only step 8 */
	ASSERT_EQ(trig[0]->ev.absStep, 8);
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_PEEK, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->pattern, SEQC_NO_PATTERN);
	ASSERT_EQ(h->errors, 0);
}

TEST(T11_late_hits_of_the_old_pattern_after_the_switch)
{
	/* spec 6.5: step 15 of pattern 0, x8 at +7, spills over the pattern end. Its
	 * hits after the boundary still play, with their own data, after pattern 1
	 * has taken over (D5); that track's step 0 is off in pattern 1, so nothing
	 * chokes them. */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 15, true);
	fake_setTiming(h, 0, 2, 15, 5, 7);		/* 15 * 96 + 84 + 12 j */
	fake_setStepData(h, 0, 2, 15, 90, 64, 100);
	fake_setPlocks(h, 0, 2, 15, 3, 40, 7, 99);
	fake_setPlocks(h, 1, 2, 15, 1, 1, 1, 1);	/* pattern 1 differs, and is never read */
	start(h);
	runToTick(h, 2 * 96);
	fake_setNextPattern(h, 1);
	runToTick(h, 18 * 96);
	ASSERT_EQ(h->activePattern, 1);
	size_t pos = 0;
	const fake_rec_t *end = fake_next(h, FAKE_PATTERN_END, &pos);
	ASSERT_TRUE(end != NULL);
	while (end != NULL && end->absStep != 16)
		end = fake_next(h, FAKE_PATTERN_END, &pos);
	ASSERT_TRUE(end != NULL);
	ASSERT_EQ(end->pattern, 1);
	ASSERT_EQ(fake_triggers(h, 2, trig, 16), 8);
	for (size_t j = 0; j < 8; j++)
	{
		ASSERT_EQ(trig[j]->ev.tick, 15 * 96 + 84 + 12 * (int64_t)j);
		ASSERT_EQ(trig[j]->ev.pattern, 0);
		ASSERT_EQ(trig[j]->ev.vel, 90);
		ASSERT_EQ(trig[j]->ev.p1Nr, 3);
		ASSERT_EQ(trig[j]->ev.p1Val, 40);
		ASSERT_EQ(trig[j]->ev.p2Nr, 7);
		ASSERT_EQ(trig[j]->ev.p2Val, 99);
		/* hits 1..7 come after the switch to pattern 1 */
		const fake_rec_t *r = trig[j];
		ASSERT_EQ(r > end, j >= 1);
	}
}

TEST(T12_step_switched_on_before_its_boundary_plays)
{
	fake_host_t *h = fake_init();
	start(h);
	runToTick(h, 4 * 96 + 50);
	fake_setStep(h, 0, 3, 5, true);
	runToTick(h, 7 * 96);
	ASSERT_EQ(fake_triggers(h, 3, trig, 4), 1);
	ASSERT_EQ(trig[0]->ev.tick, 5 * 96);
}

TEST(T12_step_switched_off_before_its_boundary_does_not_play)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 3, 5, true);
	start(h);
	runToTick(h, 4 * 96 + 50);
	fake_setStep(h, 0, 3, 5, false);
	runToTick(h, 7 * 96);
	ASSERT_EQ(fake_triggers(h, 3, trig, 4), 0);
}

TEST(T12_early_hits_use_the_state_one_step_earlier)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 3, 5, true);
	fake_setTiming(h, 0, 3, 5, 1, -2);		/* x2: 456 early, 504 late */
	fake_setStepData(h, 0, 3, 5, 80, 60, 100);
	start(h);
	runToTick(h, 4 * 96 + 10);			/* the early hit is queued */
	fake_setStepData(h, 0, 3, 5, 40, 62, 100);
	runToTick(h, 7 * 96);
	ASSERT_EQ(fake_triggers(h, 3, trig, 4), 2);
	ASSERT_EQ(trig[0]->ev.tick, 456);
	ASSERT_EQ(trig[0]->ev.vel, 80);			/* the step as it was at boundary 4 */
	ASSERT_EQ(trig[1]->ev.tick, 504);
	ASSERT_EQ(trig[1]->ev.vel, 40);			/* the step as it is at boundary 5 */
	ASSERT_EQ(trig[1]->ev.note, 62);
	/* one draw for the occurrence, made with the early part */
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 1);
}

TEST(T12_erase_at_the_boundary_wins)
{
	fake_host_t *h = fake_init();
	for (uint8_t s = 0; s < 16; s++)
		fake_setStep(h, 0, 3, s, true);
	fake_setStep(h, 0, 4, 6, true);
	h->eraseActive = true;
	h->activeTrack = 3;
	start(h);
	runToTick(h, 8 * 96);
	ASSERT_EQ(fake_triggers(h, 3, trig, 16), 0);	/* every step erased before it played */
	ASSERT_EQ(h->mainSteps[0][3], 0xFE00u);		/* steps 0..8 erased, 9..15 kept */
	ASSERT_EQ(fake_triggers(h, 4, trig, 16), 1);	/* other tracks play */
}

TEST(T12_erase_of_a_begun_step_plays_its_late_hits)
{
	/* x8 at -7 units on step 5: hits 0..6 play before tick 480, hit 7 at 480.
	 * The glue's erase clears step 5 at its boundary, after the early hits
	 * played: the late hit follows them, so all 8 play */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 3, 5, true);
	fake_setTiming(h, 0, 3, 5, 5, -7);
	start(h);
	runToTick(h, 4 * 96 + 10);			/* the early hits are queued */
	h->eraseActive = true;
	h->activeTrack = 3;
	runToTick(h, 7 * 96);
	ASSERT_EQ(h->mainSteps[0][3] & (1u << 5), 0);	/* erased at boundary 5 */
	ASSERT_EQ(fake_triggers(h, 3, trig, 16), 8);
	for (uint8_t j = 0; j < 8; j++)
	{
		ASSERT_EQ(trig[j]->ev.hit, j);
		ASSERT_EQ(trig[j]->ev.tick, 396 + 12 * j);
	}
}

TEST(T12_track_length_change_before_the_boundary)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 1, 0, true);
	fake_setStep(h, 0, 1, 4, true);
	start(h);
	runToTick(h, 3 * 96 + 50);			/* step 4 is predicted at boundary 3 */
	fake_setLength(h, 0, 1, 4);			/* now track 1 wraps to step 0 at 4 */
	runToTick(h, 6 * 96);
	ASSERT_EQ(fake_triggers(h, 1, trig, 4), 2);
	ASSERT_EQ(trig[1]->ev.absStep, 4);
	ASSERT_EQ(trig[1]->ev.step, 0);
	/* steps 0 (absStep 0), 4 (predicted at boundary 3) and 0 (actual): 3 draws */
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 3);
}

TEST_MAIN()
