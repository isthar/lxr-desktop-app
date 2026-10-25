/*
 * test_overflow.c
 *
 * T19: a full event queue rejects the new event and counts it, and the core
 * keeps running (spec 11, N2); the statistics for the CPU page; calls from
 * inside a callback are ignored and counted.
 *
 * 7 tracks cannot fill the 256-entry queue of the firmware, so this file runs
 * against a copy of the core with a 16-entry queue (seq_core_small_queue in
 * tests/CMakeLists.txt, SEQC_QUEUE_CAPACITY in seq_core.c).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "fake_host.h"
#include "queue_equiv.h"
#include "seqc_test.h"

#define CAPACITY 16	/* SEQC_QUEUE_CAPACITY of seq_core_small_queue */

static const fake_rec_t *trig[256];

static seqc_stats_t stats(void)
{
	seqc_stats_t s;
	seqc_stats(&s);
	return s;
}

static void start(fake_host_t *h)
{
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
}

static void runToTick(fake_host_t *h, double tick)
{
	fake_runUntil(h, (seqc_time_t)(fake_tickCycles(120.0) * tick) + 1, fake_msToCycles(0.1));
}

/* x8 on step 1 of every track: 56 events at boundary 1 */
static void setUpFlood(fake_host_t *h)
{
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		fake_setStep(h, 0, t, 1, true);
		fake_setTiming(h, 0, t, 1, 5, 0);
	}
	fake_setStep(h, 0, 6, 5, true);
}

TEST(T19_full_queue_rejects_and_counts)
{
	fake_host_t *h = fake_init();
	setUpFlood(h);
	start(h);
	runToTick(h, 96);				/* boundary 1 */
	seqc_stats_t s = stats();
	ASSERT_EQ(s.queueDepth, CAPACITY - 2);		/* the hits of tracks 0 and 1 at 96 fired */
	ASSERT_EQ(s.queueMax, CAPACITY);
	ASSERT_EQ(s.queueOverflow, 7 * 8 - CAPACITY);
	/* the queue keeps what it accepted first: tracks 0 and 1 */
	runToTick(h, 6 * 96);
	ASSERT_EQ(fake_triggers(h, 0, trig, 16), 8);
	ASSERT_EQ(fake_triggers(h, 1, trig, 16), 8);
	ASSERT_EQ(fake_triggers(h, 2, trig, 16), 0);
	/* and the core keeps running */
	ASSERT_EQ(fake_triggers(h, 6, trig, 16), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 5);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ(stats().queueDepth, 0);
	ASSERT_EQ(h->errors, 0);
}

TEST(T19_queue_depth_is_live)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 3, 2, true);
	fake_setTiming(h, 0, 3, 2, 3, 0);		/* x4: 192, 216, 240, 264 */
	start(h);
	runToTick(h, 192 + 30);				/* two fired */
	seqc_stats_t s = stats();
	ASSERT_EQ(s.queueDepth, 2);
	ASSERT_EQ(s.queueMax, 4);
	seqc_resetStats();				/* every counter to 0, the depth stays live */
	s = stats();
	ASSERT_EQ(s.queueDepth, 2);
	ASSERT_EQ(s.queueMax, 0);
	runToTick(h, 300);
	s = stats();
	ASSERT_EQ(s.queueDepth, 0);
	ASSERT_EQ(s.queueMax, 0);			/* it only grows when an event is queued */
}

static int nestedCalls;
static void callBackIntoTheCore(fake_host_t *h, uint32_t absStep)
{
	if (absStep != 2 || nestedCalls++ > 0)
		return;
	/* a kit autoload, say, that runs seq_tick() again: all of these are ignored.
	 * seqc_process(0) is before the nextDue of the last call, so it is counted
	 * only because seqc_enter() clears nextDue for the time of the call. */
	seqc_process(h->now + 1000);
	seqc_process(0);
	seqc_stop();
	seqc_start();
	seqc_rewind();
	seqc_setTempo(60.0f);
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	seqc_setAnalogPpq(24);
	seqc_syncEvent(SEQC_EV_CLOCK, h->now);
	seqc_songPosition(9);
	seqc_resetStats();
	/* queries and seqc_skipOnce() are allowed */
	ASSERT_EQ(seqc_absStep(), 2);
	ASSERT_NEAR(seqc_position(), 192.0, 1e-9);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	seqc_skipOnce(4, 3);
}

TEST(T19_calls_from_a_callback_are_ignored_and_counted)
{
	fake_host_t *h = fake_init();
	nestedCalls = 0;
	h->onStepBoundary = callBackIntoTheCore;
	fake_setStep(h, 0, 4, 3, true);			/* skipped by the nested skipOnce */
	fake_setStep(h, 0, 5, 3, true);
	start(h);
	runToTick(h, 4 * 96);
	ASSERT_EQ(nestedCalls, 1);
	seqc_stats_t s = stats();
	ASSERT_EQ(s.reentries, 11);
	ASSERT_EQ(s.skipDrops, 1);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-6);
	ASSERT_EQ(seqc_absStep(), 4);
	ASSERT_EQ(fake_triggers(h, 4, trig, 8), 0);
	ASSERT_EQ(fake_triggers(h, 5, trig, 8), 1);
}

static int skipCalls;
static void skipAndCallBack(fake_host_t *h, uint32_t absStep)
{
	(void)h;
	if (absStep != 4 || skipCalls++ > 0)
		return;
	seqc_stop();					/* ignored: a reentry */
	seqc_skipOnce(2, 5);				/* a skip drop */
}

TEST(T19_reset_stats_clears_every_counter)
{
	fake_host_t *h = fake_init();
	skipCalls = 0;
	h->onStepBoundary = skipAndCallBack;
	setUpFlood(h);					/* queue overflow and the queue maximum */
	h->muted[6] = true;				/* a mute drop */
	fake_setStep(h, 0, 2, 5, true);			/* skipped */
	fake_setStep(h, 0, 0, 3, true);
	fake_setTiming(h, 0, 0, 3, 5, 7);		/* x8 at 372..456, choked by step 4 at 384 */
	fake_setStep(h, 0, 0, 4, true);
	start(h);
	runToTick(h, 6 * 96);
	fake_sync(h, SEQC_EV_CLOCK, h->now);		/* ignored in internal sync */
	/* MIDI clock: outliers, a tempo restart and a dropout. The tempo fit tests
	 * no clock while it spans less than half its short window (N21), so 13
	 * clocks at 120 BPM come first; then SEQC_TEMPO_RESTART clocks 6 ms apart,
	 * each further before the fitted line: a row of outliers, a restart. The
	 * seqc_process() after the row evaluates the last of them, so the restart
	 * comes from the row itself, before the dropout clears the fit. */
	seqc_stop();
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	const seqc_time_t t0 = h->now + fake_msToCycles(1.0);
	for (int k = 0; k < 13; k++)
		fake_sync(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(k * fake_clockCycles(120.0)));
	const seqc_time_t row = t0 + (seqc_time_t)(12.0 * fake_clockCycles(120.0));
	for (unsigned k = 1; k <= SEQC_TEMPO_RESTART; k++)
		fake_sync(h, SEQC_EV_CLOCK, row + fake_msToCycles(6.0 * k));
	fake_process(h, row + fake_msToCycles(6.0 * SEQC_TEMPO_RESTART));
	ASSERT_EQ(stats().tempoRestarts, 1);
	fake_sync(h, SEQC_EV_CLOCK, t0 + fake_msToCycles(1100.0));
	seqc_stats_t s = stats();
	ASSERT_TRUE(s.queueMax > 0);
	ASSERT_TRUE(s.queueOverflow > 0);
	ASSERT_TRUE(s.chokeDrops > 0);
	ASSERT_TRUE(s.muteDrops > 0);
	ASSERT_TRUE(s.skipDrops > 0);
	ASSERT_EQ(s.syncIgnored, 1);
	ASSERT_TRUE(s.intervalsExcluded > 0);
	ASSERT_TRUE(s.dropouts > 0);
	ASSERT_TRUE(s.reentries > 0);
	ASSERT_TRUE(s.tempoRestarts > 0);
	seqc_resetStats();
	s = stats();
	ASSERT_EQ(s.queueMax, 0);
	ASSERT_EQ(s.queueOverflow, 0);
	ASSERT_EQ(s.chokeDrops, 0);
	ASSERT_EQ(s.muteDrops, 0);
	ASSERT_EQ(s.skipDrops, 0);
	ASSERT_EQ(s.syncIgnored, 0);
	ASSERT_EQ(s.intervalsExcluded, 0);
	ASSERT_EQ(s.dropouts, 0);
	ASSERT_EQ(s.reentries, 0);
	ASSERT_EQ(s.tempoRestarts, 0);
}

/* The fire log of random runs with this 16-entry queue, which overflows in
 * every run, is the one recorded from the E1.16 queue, which queued every event
 * on its own (task E1.18): the same events fire, and the same ones are rejected
 * and counted. queue_equiv.c has the runs; test_queue.c has the same test with
 * the firmware's queue. */
static const uint64_t equivHashes[] = {
	0xb485291af3a25d97ull,	0xee331dc28ee1527dull,	0x202e35ed97d7a32bull,
	0x1f0c319469a20d37ull,	0x3ff387a9f8def90bull,	0x54203bbcc16c11d8ull,
	0x1f85b9a6ae17432cull,	0xd81c24eb101ff4deull,	0xb5c0a08418d0034cull,
	0xd5d2f2e67eb50016ull,	0xaae70d9b71e65125ull,	0x5edb1c23b5632776ull,
	0xe5acd1e6cec1c883ull,	0x729b64ea15461480ull,	0xbe3722f33d634051ull,
	0x462ab9d0aee5a4c9ull,	0x530df8f85321e878ull,	0x62799bdd40ef275dull,
	0x789c3ea83530ea0dull,	0x91ef8750f6cd1205ull,	0x8c9a3e572f68fc6eull,
	0x512414b5387ed22cull,	0xe8ffcef257e28bb9ull,	0xbe7ff73fbf933ce9ull,
	0x2da44d4f69d8fa29ull,	0x94dc49862123cddfull,	0x8979d6691ff90275ull,
	0xa16d76d7e70be365ull,	0x17509e5bbf833fdcull,	0xb23b19d435cc1164ull,
	0x897b79209d88e39full,	0x0565f069da012979ull,	0x542dc0f02f611618ull,
	0x92a1fe193356e864ull,	0x6fd7eb6f1e7dde9bull,	0x35f57c8be8ef136cull,
	0xc35e695fb84a3651ull,	0x24f0d3a5e09138bfull,	0xed8adf3b6a327e20ull,
	0x0bd163f2e5b25eedull,	0x8bfccbf7f9768845ull,	0x664bffceca49384cull,
	0xff793620aa0733aeull,	0x8bcafaf9c974ca53ull,	0x0069a9dfcaafb237ull,
	0x38fccdd1493ad295ull,	0x4e8b4909849667daull,	0xac1b8e9bd8534224ull,
	0xdfc27460dddbf24dull,	0x6c81049796fbca82ull,	0x3f0672f6568f26d9ull,
	0x003e76ec3fe327a0ull,	0x54cb7a2468c48373ull,	0x49a7fdff01009541ull,
	0xeb22c9e8316d0346ull,	0x84a71e078ac073ecull,	0x3e4941b45fc16b6dull,
	0x264f9878b548f9adull,	0x192f0f3ee8b8b9d1ull,	0xb8139505e608f3bfull,
	0xe5ce2b010c6dab91ull,	0x9780677538099f11ull,	0x8162e2874bd9fb4full,
	0x3b0131bd45eef6e2ull,
};

TEST(T19_fire_log_matches_the_per_event_inserts)
{
	const size_t n = sizeof equivHashes / sizeof equivHashes[0];
	size_t overflowing = 0;
	for (size_t i = 0; i < n; i++)
	{
		seqc_stats_t s;
		const uint64_t got = queue_equivRun(i + 1, &s);
		if (got != equivHashes[i])
			printf("seed %zu: hash 0x%016llx, recorded 0x%016llx\n", i + 1, (unsigned long long)got,
					(unsigned long long)equivHashes[i]);
		ASSERT_TRUE(got == equivHashes[i]);
		overflowing += s.queueOverflow > 0;
	}
	ASSERT_EQ(overflowing, n);
}

TEST_MAIN()
