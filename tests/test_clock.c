/*
 * test_clock.c
 *
 * T1: the internal master clock (spec 5, N1). The tempo error stays below
 * 0.01% and the drift below 1 tick over 10 minutes at 60-200 BPM; the position
 * stays continuous across tempo changes; the internal transport starts at the
 * next seqc_process() call, stops in place and rewinds before the next start.
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

/** the values of the logged transport() calls, in order; returns how many */
static size_t transports(const fake_host_t *h, uint8_t *out, size_t max)
{
	size_t n = 0, pos = 0;
	const fake_rec_t *r;
	while ((r = fake_next(h, FAKE_TRANSPORT, &pos)) != NULL)
	{
		if (n < max)
			out[n] = r->value;
		n++;
	}
	return n;
}

/** ticks of the master tempo after cycles */
static double idealTicks(double bpm, seqc_time_t cycles)
{
	return (double)cycles * bpm * SEQC_TICKS_PER_BEAT / 60.0 / SEQC_CPU_HZ;
}

/* Runs bpm for 10 minutes with a seqc_process() call every 0.05 ms and checks
 * every step boundary against the ideal grid. */
static void checkTempo(double bpm)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_BIT(FAKE_STEP_BOUNDARY);
	seqc_setTempo((float)bpm);
	fake_process(h, 0);
	const seqc_time_t t0 = fake_msToCycles(1.0);
	seqc_start();
	fake_process(h, t0);				/* step 0 is here */

	const seqc_time_t interval = fake_msToCycles(0.05);
	const seqc_time_t end = t0 + fake_msToCycles(10.0 * 60.0 * 1000.0);
	fake_runUntil(h, end, interval);

	const double step = fake_stepCycles(bpm);
	const uint32_t expected = (uint32_t)(idealTicks(bpm, end - t0) / SEQC_TICKS_PER_STEP) + 1;
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), expected);
	ASSERT_EQ(h->logLost, 0);

	size_t pos = 0;
	const fake_rec_t *r;
	const fake_rec_t *last = NULL;
	uint32_t n = 0;
	while ((r = fake_next(h, FAKE_STEP_BOUNDARY, &pos)) != NULL)
	{
		ASSERT_EQ(r->arg, n);
		ASSERT_NEAR(r->tick, (double)n * SEQC_TICKS_PER_STEP, 1e-9);
		/* handled by the first call at or after the ideal time */
		const double late = (double)(r->time - t0) - n * step;
		ASSERT_TRUE(late > -1.0);
		ASSERT_TRUE(late < (double)interval + 1.0);
		last = r;
		n++;
	}
	ASSERT_TRUE(last != NULL);

	/* tempo error below 0.01% */
	const double measured = 60.0 * SEQC_CPU_HZ * (double)last->arg / ((double)(last->time - t0) * 4.0);
	ASSERT_NEAR(measured / bpm, 1.0, 1e-4);
	/* drift below 1 tick: at the last boundary and at the end */
	ASSERT_TRUE(idealTicks(bpm, last->time - t0) - (double)last->arg * SEQC_TICKS_PER_STEP < 1.0);
	ASSERT_NEAR(seqc_position(), idealTicks(bpm, end - t0), 1e-3);
	ASSERT_EQ(h->errors, 0);
}

TEST(T1_tempo_accuracy_60_bpm) { checkTempo(60.0); }
TEST(T1_tempo_accuracy_120_bpm) { checkTempo(120.0); }
TEST(T1_tempo_accuracy_128_bpm) { checkTempo(128.0); }		/* fw 1.7 played 128.20 */
TEST(T1_tempo_accuracy_137_bpm) { checkTempo(137.0); }		/* fw 1.7 played 137.29 */
TEST(T1_tempo_accuracy_200_bpm) { checkTempo(200.0); }

TEST(T1_start_at_next_process)
{
	fake_host_t *h = fake_init();
	fake_process(h, 1000);
	seqc_start();
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_TRUE(seqc_isRunning());
	uint8_t tr[4];
	ASSERT_EQ(transports(h, tr, 4), 1);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_START);
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), 0);
	ASSERT_EQ(seqc_absStep(), -1);

	const seqc_time_t t = fake_msToCycles(7.0);
	fake_process(h, t);
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_STEP_BOUNDARY, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->arg, 0);
	ASSERT_EQ(r->time, t);
	ASSERT_NEAR(r->tick, 0.0, 1e-9);
	ASSERT_EQ(seqc_absStep(), 0);

	/* step 1 at 120 BPM is 125 ms later */
	fake_runUntil(h, t + fake_msToCycles(124.9), fake_msToCycles(0.1));
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), 1);
	fake_runUntil(h, t + fake_msToCycles(125.0), fake_msToCycles(0.1));
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), 2);
	ASSERT_EQ(seqc_absStep(), 1);
	ASSERT_EQ(h->errors, 0);
}

TEST(T1_start_before_the_first_process)
{
	fake_host_t *h = fake_init();
	seqc_start();					/* no time yet: acts at the first call */
	fake_process(h, 5000);
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_STEP_BOUNDARY, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->time, 5000);
	ASSERT_NEAR(seqc_position(), 0.0, 1e-9);
}

TEST(T1_stop_keeps_the_position_and_start_rewinds)
{
	fake_host_t *h = fake_init();
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, fake_msToCycles(300.0), fake_msToCycles(0.1));	/* 2.4 steps */
	ASSERT_EQ(seqc_absStep(), 2);
	seqc_stop();
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	ASSERT_FALSE(seqc_isRunning());
	const double held = seqc_position();
	ASSERT_NEAR(held, idealTicks(120.0, fake_msToCycles(300.0)), 1e-6);

	fake_runUntil(h, fake_msToCycles(900.0), fake_msToCycles(0.1));
	ASSERT_TRUE(seqc_position() == held);		/* the position holds */
	ASSERT_EQ(seqc_absStep(), 2);
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), 3);
	uint8_t tr[4];
	seqc_stop();					/* ignored while stopped */
	ASSERT_EQ(transports(h, tr, 4), 2);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_STOP);

	fake_clearLog(h);
	seqc_start();					/* rewinds first */
	ASSERT_EQ(transports(h, tr, 4), 2);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_REWIND);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_START);
	ASSERT_EQ(seqc_absStep(), -1);
	ASSERT_NEAR(seqc_position(), 0.0, 1e-9);
	fake_process(h, fake_msToCycles(901.0));
	ASSERT_EQ(seqc_absStep(), 0);
	seqc_start();					/* ignored while running */
	ASSERT_EQ(transports(h, tr, 4), 2);
	ASSERT_EQ(h->errors, 0);
}

TEST(T1_rewind_only_when_stopped)
{
	fake_host_t *h = fake_init();
	fake_process(h, 0);
	seqc_rewind();					/* already at the start: no callback */
	ASSERT_EQ(fake_count(h, FAKE_TRANSPORT), 0);
	seqc_start();
	fake_process(h, 10);
	seqc_rewind();					/* ignored while running */
	ASSERT_EQ(seqc_absStep(), 0);
	seqc_stop();
	seqc_rewind();
	ASSERT_EQ(seqc_absStep(), -1);
	ASSERT_NEAR(seqc_position(), 0.0, 1e-9);
	uint8_t tr[4];
	ASSERT_EQ(transports(h, tr, 4), 3);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_START);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_STOP);
	ASSERT_EQ(tr[2], SEQC_TRANSPORT_REWIND);
}

TEST(T1_tempo_change_is_continuous)
{
	fake_host_t *h = fake_init();
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	/* in the middle of step 8 at 120 BPM: the last handled position lags here */
	const seqc_time_t t1 = fake_msToCycles(1062.5);
	fake_runUntil(h, t1, fake_msToCycles(0.1));
	const double before = seqc_position();
	ASSERT_NEAR(before, 816.0, 1e-6);
	seqc_setTempo(90.0f);
	ASSERT_NEAR(seqc_currentBpm(), 90.0, 1e-6);
	ASSERT_NEAR(seqc_position(), before, 1e-9);
	/* step 9 is 48 ticks later, at 90 BPM 83.33 ms */
	fake_runUntil(h, t1 + fake_msToCycles(83.2), fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 8);
	fake_runUntil(h, t1 + fake_msToCycles(83.4), fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 9);
	ASSERT_NEAR(seqc_position(), before + idealTicks(90.0, fake_msToCycles(83.4)), 1e-6);
}

TEST(T1_tempo_is_clamped)
{
	fake_host_t *h = fake_init();
	ASSERT_NEAR(seqc_currentBpm(), SEQC_DEFAULT_BPM, 1e-6);
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	/* the clock runs at the clamped tempo, not only reports it */
	seqc_setTempo(0.25f);
	ASSERT_NEAR(seqc_currentBpm(), SEQC_MASTER_BPM_MIN, 1e-6);
	fake_process(h, fake_msToCycles(1000.0));
	ASSERT_NEAR(seqc_position(), idealTicks(SEQC_MASTER_BPM_MIN, fake_msToCycles(1000.0)), 1e-6);
	seqc_setTempo(1000.0f);
	ASSERT_NEAR(seqc_currentBpm(), SEQC_MASTER_BPM_MAX, 1e-6);
	const double p = seqc_position();
	fake_process(h, fake_msToCycles(1010.0));
	ASSERT_NEAR(seqc_position() - p, idealTicks(SEQC_MASTER_BPM_MAX, fake_msToCycles(10.0)), 1e-6);
	/* the next boundary moves earlier too: 89.6 ticks at 300 BPM, 46.67 ms after the change */
	fake_runUntil(h, fake_msToCycles(1046.6), fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 0);
	fake_runUntil(h, fake_msToCycles(1046.8), fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 1);
	seqc_setTempo(133.5f);				/* fractional tempos are exact as well */
	ASSERT_NEAR(seqc_currentBpm(), 133.5, 1e-6);
}

TEST(T1_time_never_goes_back)
{
	fake_host_t *h = fake_init();
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_process(h, fake_msToCycles(100.0));
	const double p = seqc_position();
	fake_process(h, fake_msToCycles(50.0));		/* earlier: counts as the last time */
	ASSERT_TRUE(seqc_position() == p);
	fake_process(h, fake_msToCycles(130.0));
	ASSERT_EQ(seqc_absStep(), 1);
	/* one late call handles every boundary due by then, none dropped */
	fake_process(h, fake_msToCycles(1000.0));
	ASSERT_EQ(seqc_absStep(), 8);
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), 9);
	size_t pos = 0;
	const fake_rec_t *r;
	uint32_t n = 0;
	while ((r = fake_next(h, FAKE_STEP_BOUNDARY, &pos)) != NULL)
	{
		ASSERT_EQ(r->arg, n);
		ASSERT_NEAR(r->tick, (double)n * SEQC_TICKS_PER_STEP, 1e-9);
		n++;
	}
	ASSERT_NEAR(seqc_position(), 768.0, 1e-6);

	/* the same when no early exit applies: a start is pending */
	seqc_stop();
	seqc_start();
	fake_process(h, fake_msToCycles(990.0));	/* earlier: step 0 at 1000 ms */
	ASSERT_EQ(seqc_absStep(), 0);
	fake_process(h, fake_msToCycles(1010.0));
	ASSERT_NEAR(seqc_position(), idealTicks(120.0, fake_msToCycles(10.0)), 1e-6);
}

TEST_MAIN()
