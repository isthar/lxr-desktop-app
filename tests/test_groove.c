/*
 * test_groove.c
 *
 * HD: humanize and track delay of LICHOMAT v0.2 (plan V4). TrackGroove.delay and
 * the humanize time move a step occurrence's hits by whole ticks at the current
 * tempo, at most one track step later and, with swing and shift, at most 7/8 of a
 * track step earlier; the humanize volume scales the velocity. HD1: the delay and
 * its limits; HD2: the humanize time and the order of the draws; HD3: the humanize
 * volume; HD4: the worst case of the queue.
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

/* the tick of track's first trigger, or -1 */
static seqc_tick_t firstTick(const fake_host_t *h, int track)
{
	const fake_rec_t *r[1];
	return fake_triggers(h, track, r, 1) > 0 ? r[0]->ev.tick : -1;
}

TEST(HD1_delay_in_ticks_with_its_limits)
{
	/* 120 BPM: 0.768 ticks a ms */
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 4, true);
	fake_setGroove(h, 0, 0, 0, 0, 30);	/* +23.04: 384 + 23 */
	fake_setStep(h, 0, 1, 4, true);
	fake_setGroove(h, 0, 1, 0, 0, -15);	/* -11.52: 384 - 12, an early hit */
	fake_setStep(h, 0, 2, 4, true);
	fake_setGroove(h, 0, 2, 0, 0, -15);
	fake_setTiming(h, 0, 2, 4, 0, -7);	/* -84 - 12 is past -84: 384 - 84 */
	play(h, 800.0);
	ASSERT_EQ(firstTick(h, 0), 407);
	ASSERT_EQ(firstTick(h, 1), 372);
	ASSERT_EQ(firstTick(h, 2), 300);

	/* 300 BPM, the core's top, at x4 (24 ticks a step): 1.92 ticks a ms */
	h = fake_init();
	seqc_setTempo(300.0f);
	for (uint8_t t = 3; t <= 5; t++)
	{
		fake_setTempo(h, 0, t, 10);
		fake_setStep(h, 0, t, 8, true);	/* tick 192 */
	}
	fake_setGroove(h, 0, 3, 0, 0, -15);	/* -28.8: -29 */
	fake_setTiming(h, 0, 3, 8, 0, -7);	/* -21 - 29, past -21: 192 - 21 */
	fake_setGroove(h, 0, 4, 0, 0, 30);	/* +57.6: +58, more than a step: +24 */
	fake_setGroove(h, 0, 5, 0, 0, -15);	/* -29 alone: -21 */
	play(h, 300.0);				/* 576 ticks */
	ASSERT_EQ(firstTick(h, 3), 171);	/* 3 ticks after the boundary at 168 that generates it */
	ASSERT_EQ(firstTick(h, 4), 216);
	ASSERT_EQ(firstTick(h, 5), 171);
	ASSERT_EQ(h->errors, 0);
}

TEST(HD2_humanize_time_draws_after_the_probability)
{
	/* 8 ms at 120 BPM: 6.144, a span of +-6 ticks; each occurrence draws r, then the
	 * time, random() % 13 - 6 */
	fake_host_t *h = fake_init();
	for (uint8_t s = 1; s <= 4; s++)
		fake_setStep(h, 0, 0, s, true);
	fake_setGroove(h, 0, 0, 8, 0, 0);
	static const uint32_t script[8] = { 0, 0, 0, 12, 0, 6, 0, 13 };	/* -6, +6, 0, -6 */
	fake_scriptRandom(h, script, 8);
	play(h, 700.0);
	const fake_rec_t *r[8];
	ASSERT_EQ(fake_triggers(h, 0, r, 8), 4);
	static const seqc_tick_t want[4] = { 96 - 6, 192 + 6, 288, 384 - 6 };
	for (int k = 0; k < 4; k++)
		ASSERT_EQ(r[k]->ev.tick, want[k]);
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 8);
	/* without humanize one draw per occurrence, as in v0.1 */
	h = fake_init();
	for (uint8_t s = 1; s <= 4; s++)
		fake_setStep(h, 0, 0, s, true);
	play(h, 700.0);
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 4);
}

TEST(HD3_humanize_volume_scales_the_velocity)
{
	fake_host_t *h = fake_init();
	static const uint8_t vel[5] = { 0, 100, 100, 127, 1 };
	for (uint8_t s = 1; s <= 4; s++)
	{
		fake_setStep(h, 0, 0, s, true);
		fake_setStepData(h, 0, 0, s, vel[s], 60, 100);
	}
	fake_setTiming(h, 0, 0, 4, 2, 0);	/* x3: every hit of the step the same velocity */
	fake_setGroove(h, 0, 0, 0, 25, 0);
	/* r, then the volume: random() % 51 - 25 */
	static const uint32_t script[8] = { 0, 0, 0, 50, 0, 50, 0, 0 };	/* -25, +25, +25, -25 */
	fake_scriptRandom(h, script, 8);
	play(h, 700.0);
	const fake_rec_t *r[8];
	ASSERT_EQ(fake_triggers(h, 0, r, 8), 6);
	ASSERT_EQ(r[0]->ev.vel, 75);		/* (100 * 75 + 50) / 100 */
	ASSERT_EQ(r[1]->ev.vel, 125);
	ASSERT_EQ(r[2]->ev.vel, 127);		/* 127 * 1.25, at most 127 */
	for (int k = 3; k < 6; k++)
		ASSERT_EQ(r[k]->ev.vel, 1);	/* (1 * 75 + 50) / 100 */
}

TEST(HD4_worst_case_fits_the_queue)
{
	/* 7 tracks at x4 and 300 BPM, x8 ratchets on all 64 steps, shift +7, shuffle 127,
	 * delay +30 ms (limited to one step) and humanize time 8 ms */
	fake_host_t *h = fake_init();
	h->logMask = 0;
	seqc_setTempo(300.0f);
	h->shuffle = 127;
	for (uint8_t t = 0; t < FAKE_NUM_TRACKS; t++)
	{
		fake_setTempo(h, 0, t, 10);
		fake_setLength(h, 0, t, 64);
		fake_setGroove(h, 0, t, 8, 0, 30);
		for (uint8_t s = 0; s < FAKE_NUM_STEPS; s++)
		{
			fake_setStep(h, 0, t, s, true);
			fake_setTiming(h, 0, t, s, 5, 7);
		}
	}
	play(h, 4000.0);
	seqc_stats_t st;
	seqc_stats(&st);
	printf("HD4: queueMax %u\n", st.queueMax);
	ASSERT_EQ(st.queueOverflow, 0);
	ASSERT_TRUE(st.queueMax < SEQC_QUEUE_SIZE);
	ASSERT_EQ(h->errors, 0);
}

TEST_MAIN()
