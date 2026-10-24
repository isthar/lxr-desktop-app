/*
 * test_roll.c
 *
 * - T13: roll (spec 6.6): a roll event at every step s with s % rate == 0 of
 *   the track, on the swung grid, velocity 100, with the step's note; rolls play
 *   on muted tracks and lose to a note event at the same tick.
 * - T14: the recording phase (spec 6.7, swing-aware) and the skip-once rule
 *   (D6): a note recorded at half a step or later goes to the next step, which
 *   then does not play again in that occurrence.
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

/* run at 120 BPM until the time of tick (fine steps, for the phase checks) */
static void runToTick(fake_host_t *h, double tick)
{
	fake_runUntil(h, (seqc_time_t)(fake_tickCycles(120.0) * tick) + 1, fake_msToCycles(0.01));
}

static seqc_stats_t stats(void)
{
	seqc_stats_t s;
	seqc_stats(&s);
	return s;
}

TEST(T13_roll_at_every_rate)
{
	static const uint8_t rates[] = { 16, 8, 4, 2, 1 };
	for (size_t i = 0; i < sizeof rates; i++)
	{
		fake_host_t *h = fake_init();
		h->rollRate[3] = rates[i];
		fake_setStepData(h, 0, 3, 0, 127, 62, 100);	/* the roll takes the step's note */
		start(h);
		runToTick(h, 32 * 96 - 1);
		const size_t n = fake_triggers(h, 3, trig, 64);
		ASSERT_EQ(n, 32u / rates[i]);
		for (size_t k = 0; k < n; k++)
		{
			const seqc_event_t *ev = &trig[k]->ev;
			ASSERT_EQ(ev->absStep, k * rates[i]);
			ASSERT_EQ(ev->tick, (int64_t)(k * rates[i]) * 96);
			ASSERT_EQ(ev->flags, SEQC_EVF_ROLL);
			ASSERT_EQ(ev->vel, SEQC_ROLL_VELOCITY);
			ASSERT_EQ(ev->hit, 0);
			ASSERT_EQ(ev->note, ev->step == 0 ? 62 : FAKE_DEFAULT_NOTE);
		}
		ASSERT_EQ(h->errors, 0);
	}
}

TEST(T13_roll_carries_the_step_plocks)
{
	fake_host_t *h = fake_init();
	h->rollRate[3] = 4;
	fake_setPlocks(h, 0, 3, 4, 10, 20, 30, 40);
	start(h);
	runToTick(h, 5 * 96 - 1);
	ASSERT_EQ(fake_triggers(h, 3, trig, 8), 2);
	ASSERT_EQ(trig[0]->ev.absStep, 0);
	ASSERT_EQ(trig[0]->ev.p1Nr, FAKE_NO_AUTOMATION);
	ASSERT_EQ(trig[0]->ev.p2Nr, FAKE_NO_AUTOMATION);
	ASSERT_EQ(trig[1]->ev.absStep, 4);
	ASSERT_EQ(trig[1]->ev.p1Nr, 10);
	ASSERT_EQ(trig[1]->ev.p1Val, 20);
	ASSERT_EQ(trig[1]->ev.p2Nr, 30);
	ASSERT_EQ(trig[1]->ev.p2Val, 40);
}

TEST(T13_roll_is_swung)
{
	fake_host_t *h = fake_init();
	h->shuffle = 127;
	h->rollRate[0] = 1;
	start(h);
	runToTick(h, 4 * 96 - 1);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 4);
	ASSERT_EQ(trig[0]->ev.tick, 0);
	ASSERT_EQ(trig[1]->ev.tick, 96 + 64);
	ASSERT_EQ(trig[2]->ev.tick, 192);
	ASSERT_EQ(trig[3]->ev.tick, 288 + 64);
}

TEST(T13_roll_follows_the_track_step)
{
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 2, 6);
	h->rollRate[2] = 4;				/* track steps 0 and 4 */
	start(h);
	runToTick(h, 18 * 96 - 1);
	ASSERT_EQ(fake_triggers(h, 2, trig, 16), 6);
	static const uint32_t abs[] = { 0, 4, 6, 10, 12, 16 };
	for (int k = 0; k < 6; k++)
		ASSERT_EQ(trig[k]->ev.absStep, abs[k]);
}

TEST(T13_roll_plays_on_a_muted_track)
{
	fake_host_t *h = fake_init();
	h->rollRate[5] = 2;
	h->muted[5] = true;
	fake_setStep(h, 0, 5, 1, true);			/* a step event: muted */
	start(h);
	runToTick(h, 4 * 96 - 1);
	ASSERT_EQ(fake_triggers(h, 5, trig, 8), 2);
	ASSERT_EQ(trig[0]->ev.flags, SEQC_EVF_ROLL);
	ASSERT_EQ(trig[1]->ev.flags, SEQC_EVF_ROLL);
	ASSERT_EQ(stats().muteDrops, 1);
}

TEST(T13_roll_plays_on_a_muted_step_with_a_note)
{
	fake_host_t *h = fake_init();
	h->rollRate[5] = 2;
	h->muted[5] = true;
	fake_setStep(h, 0, 5, 2, true);			/* muted, at the tick of the roll */
	start(h);
	runToTick(h, 4 * 96 - 1);
	/* the muted note does not take part in the same-tick rule */
	ASSERT_EQ(fake_triggers(h, 5, trig, 8), 2);
	ASSERT_EQ(trig[1]->ev.absStep, 2);
	ASSERT_EQ(trig[1]->ev.flags, SEQC_EVF_ROLL);
	ASSERT_EQ(stats().muteDrops, 1);
	ASSERT_EQ(stats().chokeDrops, 0);
}

TEST(T13_note_wins_over_a_roll_at_the_same_tick)
{
	fake_host_t *h = fake_init();
	h->rollRate[2] = 4;
	fake_setStep(h, 0, 2, 4, true);			/* same tick and step as the roll */
	fake_setStepData(h, 0, 2, 4, 80, 60, 100);
	start(h);
	runToTick(h, 9 * 96 - 1);
	ASSERT_EQ(fake_triggers(h, 2, trig, 8), 3);
	ASSERT_EQ(trig[0]->ev.flags, SEQC_EVF_ROLL);	/* step 0 */
	ASSERT_EQ(trig[1]->ev.absStep, 4);
	ASSERT_EQ(trig[1]->ev.flags, 0);		/* the note, not the roll */
	ASSERT_EQ(trig[1]->ev.vel, 80);
	ASSERT_EQ(trig[2]->ev.absStep, 8);
	ASSERT_EQ(trig[2]->ev.flags, SEQC_EVF_ROLL);
	ASSERT_EQ(stats().chokeDrops, 1);
}

TEST(T13_roll_hit_is_recorded_at_its_own_step)
{
	fake_host_t *h = fake_init();
	h->recordActive = true;
	h->rollRate[1] = 8;
	start(h);
	runToTick(h, 8 * 96 + 10);
	/* the glue records each roll hit at its step (N11): steps 0 and 8 */
	ASSERT_EQ(h->mainSteps[0][1], (1ull << 0) | (1ull << 8));
	ASSERT_EQ(h->steps[0][1][8].volume, SEQC_ROLL_VELOCITY | FAKE_STEP_ACTIVE_MASK);
	/* in the next loop the recorded step plays, and the roll on it is dropped */
	runToTick(h, 17 * 96 - 1);
	ASSERT_EQ(fake_triggers(h, 1, trig, 8), 3);
	ASSERT_EQ(trig[2]->ev.absStep, 16);
	ASSERT_EQ(trig[2]->ev.flags, 0);
}

TEST(T14_phase_is_linear_without_shuffle)
{
	fake_host_t *h = fake_init();
	ASSERT_EQ(seqc_stepPhase(), 0.0f);		/* stopped */
	start(h);
	static const double at[] = { 0.0, 12.0, 48.0, 72.0, 95.0 };
	for (size_t i = 0; i < sizeof at / sizeof at[0]; i++)
	{
		runToTick(h, 5 * 96 + at[i]);
		ASSERT_EQ(seqc_absStep(), 5);
		ASSERT_NEAR(seqc_stepPhase(), at[i] / 96.0, 0.002);
	}
}

TEST(T14_phase_with_shuffle_is_half_between_the_swung_hits)
{
	fake_host_t *h = fake_init();
	h->shuffle = 127;				/* swing 64: half at 80 */
	start(h);
	runToTick(h, 3 * 96 + 40);
	ASSERT_NEAR(seqc_stepPhase(), 0.25, 0.002);
	runToTick(h, 3 * 96 + 80);
	ASSERT_NEAR(seqc_stepPhase(), 0.5, 0.002);
	runToTick(h, 3 * 96 + 88);
	ASSERT_NEAR(seqc_stepPhase(), 0.75, 0.002);
	/* the same rule on an even step: halfway between 4 * 96 and the swung 5 */
	runToTick(h, 4 * 96 + 80);
	ASSERT_NEAR(seqc_stepPhase(), 0.5, 0.002);
}

TEST(T14_phase_is_zero_when_stopped)
{
	fake_host_t *h = fake_init();
	start(h);
	runToTick(h, 2 * 96 + 50);
	ASSERT_TRUE(seqc_stepPhase() > 0.5f);
	seqc_stop();
	ASSERT_EQ(seqc_stepPhase(), 0.0f);
}

TEST(T14_late_note_goes_to_the_next_step_and_plays_once)
{
	fake_host_t *h = fake_init();
	h->recordActive = true;
	start(h);
	runToTick(h, 3 * 96 + 60);			/* phase 0.625 in step 3 */
	fake_addNote(h, 4, 100, 64);
	ASSERT_TRUE((h->mainSteps[0][4] >> 4) & 1u);	/* recorded on step 4 */
	runToTick(h, 21 * 96 - 1);
	/* the live hit was the note: step 4 does not play at 4, but at 20 */
	ASSERT_EQ(fake_triggers(h, 4, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 20);
	ASSERT_EQ(trig[0]->ev.note, 64);
	ASSERT_EQ(stats().skipDrops, 1);
}

TEST(T14_early_note_stays_on_the_current_step)
{
	fake_host_t *h = fake_init();
	h->recordActive = true;
	start(h);
	runToTick(h, 3 * 96 + 40);			/* phase 0.42 */
	fake_addNote(h, 4, 100, 64);
	ASSERT_TRUE((h->mainSteps[0][4] >> 3) & 1u);
	runToTick(h, 20 * 96 - 1);
	ASSERT_EQ(fake_triggers(h, 4, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 19);
	ASSERT_EQ(stats().skipDrops, 0);
}

TEST(T14_skip_once_removes_the_queued_early_hits)
{
	fake_host_t *h = fake_init();
	h->recordActive = true;
	fake_setStep(h, 0, 4, 4, true);
	fake_setTiming(h, 0, 4, 4, 1, -2);		/* 360 early, 408 late */
	start(h);
	runToTick(h, 3 * 96 + 50);			/* the early hit is queued */
	fake_addNote(h, 4, 100, 64);
	runToTick(h, 6 * 96);
	ASSERT_EQ(fake_triggers(h, 4, trig, 8), 0);
	ASSERT_EQ(stats().skipDrops, 1);
}

TEST(T14_marker_is_cleared_at_the_next_boundary)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 9, true);
	start(h);
	runToTick(h, 3 * 96 + 50);
	seqc_skipOnce(2, 9);				/* the track arrives at 4, not 9 */
	runToTick(h, 10 * 96);
	ASSERT_EQ(fake_triggers(h, 2, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 9);
	ASSERT_EQ(stats().skipDrops, 0);
}

TEST(T14_skip_once_is_ignored_when_stopped_or_out_of_range)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 0, true);
	seqc_skipOnce(2, 0);				/* stopped */
	start(h);
	runToTick(h, 17 * 96);
	ASSERT_EQ(fake_triggers(h, 2, trig, 8), 2);
	ASSERT_EQ(stats().skipDrops, 0);

	/* out of range: the valid marker is kept */
	h = fake_init();
	h->recordActive = true;
	start(h);
	runToTick(h, 3 * 96 + 60);
	fake_addNote(h, 4, 100, 64);			/* marker for track 4, step 4 */
	seqc_skipOnce(7, 4);
	seqc_skipOnce(4, 64);
	runToTick(h, 21 * 96 - 1);
	ASSERT_EQ(fake_triggers(h, 4, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 20);
	ASSERT_EQ(stats().skipDrops, 1);
}

static void skipStep4(fake_host_t *h, uint32_t absStep)
{
	(void)h;
	if (absStep == 3)
		seqc_skipOnce(4, 4);
}

TEST(T14_skip_once_from_the_step_boundary_drops_the_early_hits)
{
	fake_host_t *h = fake_init();
	h->onStepBoundary = skipStep4;			/* before the early part of step 4 */
	fake_setStep(h, 0, 4, 4, true);
	fake_setTiming(h, 0, 4, 4, 1, -2);		/* 360 early, 408 late */
	start(h);
	runToTick(h, 6 * 96);
	ASSERT_EQ(fake_triggers(h, 4, trig, 8), 0);
	ASSERT_EQ(stats().skipDrops, 1);
}

TEST(T14_marker_belongs_to_the_active_pattern)
{
	fake_host_t *h = fake_init();
	h->recordActive = true;
	fake_setStep(h, 1, 4, 0, true);
	fake_setTiming(h, 1, 4, 0, 1, -2);		/* 1512 early, 1560 late */
	start(h);
	fake_setNextPattern(h, 1);
	runToTick(h, 15 * 96 + 60);			/* the early hit of pattern 1 is queued */
	fake_addNote(h, 4, 100, 64);			/* marker for pattern 0, step 0 */
	runToTick(h, 17 * 96);
	ASSERT_EQ(fake_triggers(h, 4, trig, 8), 2);
	ASSERT_EQ(trig[0]->ev.pattern, 1);
	ASSERT_EQ(trig[0]->ev.tick, 1512);
	ASSERT_EQ(trig[1]->ev.pattern, 1);
	ASSERT_EQ(trig[1]->ev.tick, 1560);
	ASSERT_EQ(stats().skipDrops, 0);
}

TEST(T14_rewind_clears_the_marker)
{
	fake_host_t *h = fake_init();
	h->recordActive = true;
	start(h);
	runToTick(h, 3 * 96 + 60);
	fake_addNote(h, 4, 100, 64);			/* marker for step 4, absStep 4 */
	seqc_stop();
	seqc_rewind();					/* the stop button */
	fake_clearLog(h);
	seqc_start();
	fake_runUntil(h, h->now + (seqc_time_t)(fake_stepCycles(120.0) * 5), fake_msToCycles(0.01));
	ASSERT_EQ(fake_triggers(h, 4, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 4);
	ASSERT_EQ(stats().skipDrops, 0);
}

TEST_MAIN()
