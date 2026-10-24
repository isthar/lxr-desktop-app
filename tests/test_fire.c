/*
 * test_fire.c
 *
 * Firing (spec 6.4, N2):
 * - T7: the choke works per track (D2): both replica scenarios of the spec,
 *   the removal of older steps' hits, and the same-tick rule;
 * - T9: muting a track drops its pending hits when they fire;
 * - T15: the note-off comes 4 ticks after the trigger, before a retrigger, and
 *   at once on a stop.
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

/* start at time 0 and run at bpm until just before the boundary of step end */
static void runSteps(fake_host_t *h, double bpm, uint32_t end)
{
	seqc_setTempo((float)bpm);
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(bpm) * end) - 1, fake_msToCycles(0.1));
}

static seqc_stats_t stats(void)
{
	seqc_stats_t s;
	seqc_stats(&s);
	return s;
}

TEST(T7_d2_snare_does_not_choke_the_hat)
{
	/* replica scenario 6: hat x4 on step 0; snare step 1 shifted -2 lands on
	 * the hat's 4th hit. fw 1.7 dropped that hit (global choke flags). */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 5, 0, true);
	fake_setTiming(h, 0, 5, 0, 3, 0);
	fake_setStep(h, 0, 3, 1, true);
	fake_setTiming(h, 0, 3, 1, 0, -2);
	runSteps(h, 130.0, 4);
	ASSERT_EQ(fake_triggers(h, 5, trig, 8), 4);
	for (int j = 0; j < 4; j++)
		ASSERT_EQ(trig[j]->ev.tick, j * 24);
	ASSERT_EQ(fake_triggers(h, 3, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.tick, 72);
	ASSERT_EQ(stats().chokeDrops, 0);
}

TEST(T7_d2_kick_does_not_choke_the_spill_over)
{
	/* replica scenario 6: hat step 2 x2 shifted +4; its second hit spills to
	 * tick 0 of step 3, where the kick plays */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 3, true);
	fake_setStep(h, 0, 5, 2, true);
	fake_setTiming(h, 0, 5, 2, 1, 4);
	runSteps(h, 130.0, 5);
	ASSERT_EQ(fake_triggers(h, 5, trig, 8), 2);
	ASSERT_EQ(trig[0]->ev.tick, 2 * 96 + 48);
	ASSERT_EQ(trig[1]->ev.tick, 3 * 96);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.tick, 3 * 96);
	ASSERT_EQ(stats().chokeDrops, 0);
}

TEST(T7_newer_step_chokes_the_older_hits)
{
	/* step 1 x8 +7 runs over step 2; the first hit of step 2 removes the rest */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 4, 1, true);
	fake_setTiming(h, 0, 4, 1, 5, 7);		/* 180, 192, ..., 264 */
	fake_setStep(h, 0, 4, 2, true);
	fake_setTiming(h, 0, 4, 2, 0, 1);		/* 204 */
	runSteps(h, 120.0, 5);
	ASSERT_EQ(fake_triggers(h, 4, trig, 16), 3);
	ASSERT_EQ(trig[0]->ev.tick, 180);
	ASSERT_EQ(trig[1]->ev.tick, 192);
	ASSERT_EQ(trig[2]->ev.tick, 204);
	ASSERT_EQ(trig[2]->ev.absStep, 2);
	ASSERT_EQ(stats().chokeDrops, 6);
	/* another track is not affected */
	ASSERT_EQ(h->errors, 0);
}

TEST(T7_same_tick_highest_step_wins)
{
	/* step 1 +7 x8: its 2nd hit is at 192 = step 2's own hit */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 1, true);
	fake_setTiming(h, 0, 2, 1, 5, 7);		/* 180, 192, ... */
	fake_setStep(h, 0, 2, 2, true);			/* 192 */
	fake_setStepData(h, 0, 2, 2, 77, 61, 100);
	runSteps(h, 120.0, 4);
	ASSERT_EQ(fake_triggers(h, 2, trig, 16), 2);
	ASSERT_EQ(trig[0]->ev.tick, 180);
	ASSERT_EQ(trig[1]->ev.tick, 192);
	ASSERT_EQ(trig[1]->ev.absStep, 2);
	ASSERT_EQ(trig[1]->ev.vel, 77);
}

TEST(T7_same_tick_on_other_tracks_fire_in_track_order)
{
	fake_host_t *h = fake_init();
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		fake_setStep(h, 0, (uint8_t)(6 - t), 1, true);
	runSteps(h, 120.0, 3);
	ASSERT_EQ(fake_triggers(h, -1, trig, 16), 7);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		ASSERT_EQ(trig[t]->ev.track, t);
}

static uint8_t muteAfter;
static void muteOnThirdHit(fake_host_t *h, const seqc_event_t *ev)
{
	if (ev->hit == muteAfter)
		h->muted[ev->track] = true;
}

TEST(T9_mute_during_a_ratchet_drops_the_rest)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 1, 2, true);
	fake_setTiming(h, 0, 1, 2, 5, 0);		/* x8 */
	fake_setStep(h, 0, 1, 6, true);
	muteAfter = 2;
	h->onTrigger = muteOnThirdHit;
	runSteps(h, 120.0, 4);
	ASSERT_EQ(fake_triggers(h, 1, trig, 16), 3);
	ASSERT_EQ(stats().muteDrops, 5);
	/* a muted drop has no side effects: one note-off, for the last hit played */
	ASSERT_EQ(fake_count(h, FAKE_NOTE_OFF), 3);
	/* unmuted again, the next step plays */
	h->onTrigger = NULL;
	h->muted[1] = false;
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 8), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 1, trig, 16), 4);
	ASSERT_EQ(trig[3]->ev.tick, 6 * 96);
}

TEST(T9_mute_is_read_when_the_event_fires)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 1, true);
	fake_setTiming(h, 0, 0, 1, 0, -7);		/* early: queued at boundary 0, fires at 12 */
	h->muted[0] = true;
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);				/* boundary 0 queues it while muted */
	h->muted[0] = false;				/* unmuted before it fires */
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 2), fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 0, trig, 4), 1);
	ASSERT_EQ(stats().muteDrops, 0);
}

TEST(T15_note_off_4_ticks_after_the_trigger)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 3, 0, true);
	fake_setStep(h, 0, 3, 5, true);
	fake_setTiming(h, 0, 3, 5, 1, 0);		/* x2: 480, 528 */
	runSteps(h, 120.0, 8);
	size_t pos = 0;
	const fake_rec_t *r;
	static const int64_t expect[] = { 4, 484, 532 };
	int i = 0;
	while ((r = fake_next(h, FAKE_NOTE_OFF, &pos)) != NULL)
	{
		ASSERT_TRUE(i < 3);
		ASSERT_EQ(r->track, 3);
		ASSERT_NEAR(r->tick, (double)expect[i], 1e-9);
		i++;
	}
	ASSERT_EQ(i, 3);
}

TEST(T15_note_off_comes_right_before_a_retrigger)
{
	/* shuffle 4 is a swing of 2 ticks: step 2 at 192 + 84 = 276, step 3 at
	 * 288 + 2 - 12 = 278, only 2 ticks later */
	fake_host_t *h = fake_init();
	h->shuffle = 4;
	fake_setStep(h, 0, 0, 2, true);
	fake_setTiming(h, 0, 0, 2, 0, 7);
	fake_setStep(h, 0, 0, 3, true);
	fake_setTiming(h, 0, 0, 3, 0, -1);
	runSteps(h, 120.0, 5);
	ASSERT_EQ(fake_triggers(h, 0, trig, 4), 2);
	ASSERT_EQ(trig[0]->ev.tick, 276);
	ASSERT_EQ(trig[1]->ev.tick, 278);
	/* trigger 276, note-off at 278, trigger 278, note-off at 282 */
	size_t pos = 0;
	const fake_rec_t *r;
	int seq = 0;
	for (size_t i = 0; i < h->logCount; i++)
	{
		r = &h->log[i];
		if (r->kind == FAKE_TRIGGER || r->kind == FAKE_NOTE_OFF)
		{
			static const fake_kind_t order[] = { FAKE_TRIGGER, FAKE_NOTE_OFF, FAKE_TRIGGER, FAKE_NOTE_OFF };
			ASSERT_TRUE(seq < 4);
			ASSERT_EQ(r->kind, order[seq]);
			seq++;
		}
	}
	ASSERT_EQ(seq, 4);
	r = fake_next(h, FAKE_NOTE_OFF, &pos);
	ASSERT_NEAR(r->tick, 278.0, 1e-9);
	r = fake_next(h, FAKE_NOTE_OFF, &pos);
	ASSERT_NEAR(r->tick, 282.0, 1e-9);
}

TEST(T15_stop_sends_the_pending_note_offs)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 2, 0, true);
	fake_setStep(h, 0, 4, 0, true);
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);				/* both trigger at tick 0 */
	ASSERT_EQ(fake_count(h, FAKE_TRIGGER), 2);
	ASSERT_EQ(fake_count(h, FAKE_NOTE_OFF), 0);
	fake_clearLog(h);
	seqc_stop();
	ASSERT_EQ(fake_count(h, FAKE_NOTE_OFF), 2);
	ASSERT_EQ(h->log[0].kind, FAKE_NOTE_OFF);	/* before the STOP */
	ASSERT_EQ(h->log[0].track, 2);
	ASSERT_EQ(h->log[1].track, 4);
	ASSERT_EQ(h->log[2].kind, FAKE_TRANSPORT);
	ASSERT_EQ(h->log[2].value, SEQC_TRANSPORT_STOP);
	fake_runUntil(h, fake_msToCycles(100.0), fake_msToCycles(0.1));
	ASSERT_EQ(fake_count(h, FAKE_NOTE_OFF), 2);	/* not again */
}

TEST_MAIN()
