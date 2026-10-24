/*
 * test_slave.c
 *
 * Slave modes (spec 5, N4, N9):
 * - T16: MIDI clock: a steady clock, USB-like jitter of +-1 ms, a tempo ramp
 *   from 120 to 140 BPM, early, late and bunched clocks without a lost event,
 *   the clamp to the next clock, outliers, a dropout and the resume, the local
 *   start on the master's step grid, and the sync mode switches;
 * - T18: the clock in jack at 4 and 24 PPQ.
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

#define MAX_CLOCKS 1024

static const fake_rec_t *trig[1024];
static seqc_time_t stamps[MAX_CLOCKS];

static seqc_stats_t stats(void)
{
	seqc_stats_t s;
	seqc_stats(&s);
	return s;
}

/* deliver count clocks with the given stamps (ascending) the way seq_tick() does:
 * every interval cycles, the clocks stamped up to then, then seqc_process() */
static void deliver(fake_host_t *h, seqc_sync_ev_t ev, const seqc_time_t *at, size_t count, seqc_time_t interval)
{
	size_t i = 0;
	seqc_time_t t = h->now;
	while (i < count)
	{
		t += interval;
		while (i < count && at[i] <= t)
			fake_sync(h, ev, at[i++]);
		fake_process(h, t);
	}
}

/* a slave in mode, with every 16th of track 0 on, armed by a local start */
static fake_host_t *slaveWithGrid(seqc_sync_t mode)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	for (uint8_t s = 0; s < 16; s++)
		fake_setStep(h, 0, 0, s, true);
	seqc_setSyncMode(mode);
	fake_process(h, fake_msToCycles(1.0));
	return h;
}

/* a deterministic jitter in [-1, +1] ms */
static uint64_t lcg = 12345;
static double jitterMs(void)
{
	lcg = lcg * 6364136223846793005ull + 1442695040888963407ull;
	return (double)(lcg >> 11) / (double)(1ull << 53) * 2.0 - 1.0;
}

TEST(T16_steady_clock)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	for (uint8_t s = 0; s < 16; s += 4)
		fake_setStep(h, 0, 0, s, true);
	fake_setStep(h, 0, 1, 2, true);
	fake_setTiming(h, 0, 1, 2, 3, 0);		/* x4: 192, 216, 240, 264 */
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	ASSERT_EQ(seqc_position(), -16.0);		/* one clock before step 0 */
	fake_process(h, fake_msToCycles(1.0));
	seqc_start();
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	ASSERT_TRUE(seqc_isRunning());			/* armed counts as running (N7) */
	const double per = fake_clockCycles(125.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 6 * 32 + 1, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ(seqc_absStep(), 32);

	/* every boundary at the stamp of its clock */
	size_t pos = 0;
	const fake_rec_t *r;
	uint32_t n = 0;
	while ((r = fake_next(h, FAKE_STEP_BOUNDARY, &pos)) != NULL)
	{
		ASSERT_EQ(r->arg, n);
		ASSERT_EQ(r->time, t0 + (seqc_time_t)(6.0 * n * per + 0.5));
		n++;
	}
	ASSERT_EQ(n, 33);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 9);
	for (int k = 0; k < 9; k++)
		ASSERT_EQ(trig[k]->time, t0 + (seqc_time_t)(24.0 * k * per + 0.5));
	/* the ratchet hits between clocks come from the interpolation */
	ASSERT_EQ(fake_triggers(h, 1, trig, 64), 8);
	for (int k = 0; k < 4; k++)
	{
		const double ideal = (double)t0 + (192.0 + 24.0 * k) / 16.0 * per;
		ASSERT_NEAR((double)trig[k]->time, ideal, fake_msToCycles(0.1) + 2.0);
	}
	ASSERT_NEAR(seqc_currentBpm(), 125.0, 1e-3);
	ASSERT_EQ(h->calls[FAKE_MIDI_CLOCK], 0);	/* no MIDI clock out in slave modes */
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], 33);	/* one per step of the clock count */
	ASSERT_EQ(h->errors, 0);
}

TEST(T16_usb_like_jitter)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(125.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	const size_t count = 6 * 64 + 1;
	lcg = 12345;
	for (size_t i = 0; i < count; i++)
		stamps[i] = (seqc_time_t)((double)t0 + (double)i * per + jitterMs() * (double)fake_msToCycles(1.0));
	deliver(h, SEQC_EV_CLOCK, stamps, count, fake_msToCycles(0.1));
	/* no step lost, in order, each within the jitter of its ideal time */
	const size_t n = fake_triggers(h, 0, trig, 1024);
	ASSERT_EQ(n, 65);
	for (size_t k = 0; k < n; k++)
	{
		ASSERT_EQ(trig[k]->ev.absStep, k);
		const double ideal = (double)t0 + 6.0 * (double)k * per;
		ASSERT_NEAR((double)trig[k]->time, ideal, fake_msToCycles(1.1));
	}
	ASSERT_NEAR(seqc_currentBpm(), 125.0, 0.5);
	ASSERT_EQ(stats().intervalsExcluded, 0);
}

TEST(T16_estimate_uses_24_intervals)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, fake_clockCycles(120.0), 30, fake_msToCycles(0.1));
	const seqc_time_t t1 = t0 + (seqc_time_t)(29.0 * fake_clockCycles(120.0) + 0.5)
			+ (seqc_time_t)fake_clockCycles(100.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t1, fake_clockCycles(100.0), 12, fake_msToCycles(0.1));
	/* 12 intervals at 120 and 12 at 100: (12 x 20.833 + 12 x 25) / 24 ms */
	ASSERT_NEAR(seqc_currentBpm(), 60.0 / 0.55, 5e-3);
	ASSERT_EQ(stats().intervalsExcluded, 0);
}

TEST(T16_tempo_ramp_120_to_140)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	/* 96 clocks (one bar) ramping from 120 to 140 BPM, then 48 clocks at 140 */
	const size_t count = 96 + 48 + 1;
	double t = (double)fake_msToCycles(10.0);
	for (size_t i = 0; i < count; i++)
	{
		stamps[i] = (seqc_time_t)(t + 0.5);
		const double bpm = i < 96 ? 120.0 + 20.0 * (double)i / 96.0 : 140.0;
		t += fake_clockCycles(bpm);
	}
	deliver(h, SEQC_EV_CLOCK, stamps, count, fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 0, trig, 1024), count / 6 + 1);
	for (size_t k = 0; k <= count / 6; k++)
		ASSERT_EQ(trig[k]->time, stamps[6 * k]);	/* each step at its clock */
	ASSERT_NEAR(seqc_currentBpm(), 140.0, 0.01);
	ASSERT_EQ(stats().intervalsExcluded, 0);
}

TEST(T16_bunched_clocks_lose_no_event)
{
	/* USB: three clocks in one packet share a stamp; the gaps in between are
	 * three intervals long. The position follows every clock. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	fake_setStep(h, 0, 1, 1, true);
	fake_setTiming(h, 0, 1, 1, 5, 3);		/* x8 between the clocks */
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	const size_t count = 6 * 16 + 1;
	for (size_t i = 0; i < count; i++)
		stamps[i] = t0 + (seqc_time_t)((double)(i / 3 * 3 + 2) * per);
	size_t i = 0;
	seqc_time_t t = h->now;
	double last = -1e9;
	while (i < count)
	{
		t += fake_msToCycles(0.1);
		while (i < count && stamps[i] <= t)
			fake_sync(h, SEQC_EV_CLOCK, stamps[i++]);
		fake_process(h, t);
		ASSERT_TRUE(seqc_position() >= last);	/* never back */
		last = seqc_position();
	}
	ASSERT_EQ(fake_triggers(h, 0, trig, 1024), 17);
	const size_t n = fake_triggers(h, 1, trig, 1024);
	ASSERT_EQ(n, 8);
	for (size_t k = 1; k < n; k++)
		ASSERT_TRUE(trig[k]->ev.tick > trig[k - 1]->ev.tick);
	ASSERT_TRUE(stats().intervalsExcluded > 0);
}

TEST(T16_position_is_clamped_to_the_next_clock)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 21, fake_msToCycles(0.1));	/* clock 20: tick 320 */
	/* the next clock is late: three intervals instead of one */
	fake_runUntil(h, t0 + (seqc_time_t)(23.0 * per), fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_position(), 336.0, 1e-9);	/* clamped to 320 + 16 */
	ASSERT_EQ(seqc_absStep(), 3);
	fake_sync(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(23.0 * per));
	ASSERT_NEAR(seqc_position(), 336.0, 1e-9);	/* the clock confirms the clamp */
	fake_runClocks(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(24.0 * per), per, 3, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 4);			/* 384 with the third clock */
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 5);
}

TEST(T16_outlier_interval_is_left_out)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	const size_t count = 60;
	for (size_t i = 0; i < count; i++)
		stamps[i] = t0 + (seqc_time_t)((double)i * per + (i == 40 ? 0.8 * per : 0.0));
	deliver(h, SEQC_EV_CLOCK, stamps, count, fake_msToCycles(0.1));
	/* 1.8 and 0.2 intervals around clock 40: both left out */
	ASSERT_EQ(stats().intervalsExcluded, 2);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 10);	/* steps 0..9 */
}

TEST(T16_interval_inside_the_band_is_kept)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 30, fake_msToCycles(0.1));
	fake_sync(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(29.0 * per + 1.4 * per));
	/* 40% long, inside the 50% band: used */
	ASSERT_EQ(stats().intervalsExcluded, 0);
	ASSERT_NEAR(seqc_currentBpm(), 120.0 * 24.0 / 24.4, 1e-2);
}

TEST(T16_dropout_and_resume)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 48, fake_msToCycles(0.1));	/* clock 47: tick 752 */
	const seqc_time_t lastClock = t0 + (seqc_time_t)(47.0 * per + 0.5);
	/* no clock for 800 ms */
	fake_runUntil(h, lastClock + fake_msToCycles(499.0), fake_msToCycles(0.1));
	ASSERT_EQ(stats().dropouts, 0);
	ASSERT_NEAR(seqc_position(), 768.0, 1e-9);	/* the clamp includes boundary 8 */
	ASSERT_EQ(seqc_absStep(), 8);
	fake_runUntil(h, lastClock + fake_msToCycles(800.0), fake_msToCycles(0.1));
	ASSERT_EQ(stats().dropouts, 1);
	ASSERT_NEAR(seqc_position(), 768.0, 1e-9);	/* holds */
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);	/* the last known tempo */
	/* resume: the gap is not used for the estimate */
	const seqc_time_t t1 = lastClock + fake_msToCycles(800.0) + 1000;
	fake_sync(h, SEQC_EV_CLOCK, t1);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_NEAR(seqc_position(), 768.0, 1e-9);	/* this clock is the one at 768 */
	fake_runClocks(h, SEQC_EV_CLOCK, t1 + (seqc_time_t)per, per, 12, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 10);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 11);	/* steps 0..10, none lost or doubled */
	for (size_t k = 0; k < 11; k++)
		ASSERT_EQ(trig[k]->ev.absStep, k);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(stats().dropouts, 1);
}

TEST(T16_dropout_clears_the_estimate)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	fake_runClocks(h, SEQC_EV_CLOCK, fake_msToCycles(10.0), fake_clockCycles(100.0), 48, fake_msToCycles(0.1));
	fake_runUntil(h, h->now + fake_msToCycles(800.0), fake_msToCycles(0.1));
	ASSERT_EQ(stats().dropouts, 1);
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-3);	/* the last known tempo, not the master's 120 */
	/* resume at 50 BPM: twice the interval, outside the band of the old estimate */
	fake_runClocks(h, SEQC_EV_CLOCK, h->now + 1000, fake_clockCycles(50.0), 30, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 50.0, 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, 0);
}

TEST(T16_local_start_waits_for_the_masters_step)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 3, fake_msToCycles(0.1));	/* count 3 */
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], 1);	/* clock 0 began a step */
	seqc_start();
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	fake_runClocks(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(3.0 * per), per, 3, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_ARMED);		/* clocks 3, 4, 5 */
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), 0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(6.0 * per), per, 1, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);		/* clock 6 starts step 0 */
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_STEP_BOUNDARY, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->arg, 0);
	ASSERT_EQ(r->time, t0 + (seqc_time_t)(6.0 * per + 0.5));
	pos = 0;
	uint8_t tr[4];
	size_t n = 0;
	while ((r = fake_next(h, FAKE_TRANSPORT, &pos)) != NULL)
		if (n < 4)
			tr[n++] = r->value;
	ASSERT_EQ(n, 2);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_ARM);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_START);
}

TEST(T16_tempo_estimate_is_clamped)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	fake_runClocks(h, SEQC_EV_CLOCK, fake_msToCycles(10.0), fake_clockCycles(400.0), 30, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), SEQC_SLAVE_BPM_MAX, 1e-3);
	h = slaveWithGrid(SEQC_SYNC_MIDI);
	fake_runClocks(h, SEQC_EV_CLOCK, fake_msToCycles(10.0), fake_clockCycles(12.0), 30, fake_msToCycles(0.5));
	ASSERT_NEAR(seqc_currentBpm(), SEQC_SLAVE_BPM_MIN, 1e-3);
}

TEST(T16_clock_only_in_midi_sync)
{
	fake_host_t *h = fake_init();
	fake_process(h, 0);
	fake_sync(h, SEQC_EV_CLOCK, 100);		/* internal sync */
	ASSERT_EQ(stats().syncIgnored, 1);
	seqc_setSyncMode(SEQC_SYNC_ANALOG);
	fake_sync(h, SEQC_EV_CLOCK, 200);
	ASSERT_EQ(stats().syncIgnored, 2);
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	fake_sync(h, SEQC_EV_CLOCK, 300);
	fake_sync(h, SEQC_EV_PULSE, 400);
	ASSERT_EQ(stats().syncIgnored, 3);
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], 2);	/* the internal one at time 0, and the MIDI clock's */
}

TEST(T16_switch_clears_the_estimate_and_the_count)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_setAnalogPpq(24);				/* only kept in MIDI sync */
	const double per = fake_clockCycles(100.0);
	fake_runClocks(h, SEQC_EV_CLOCK, fake_msToCycles(10.0), per, 4, fake_msToCycles(0.1));	/* count 4 */
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-3);
	seqc_setSyncMode(SEQC_SYNC_ANALOG);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);	/* no estimate: the master tempo */
	const uint32_t steps = h->calls[FAKE_TEMPO_STEP];
	const double per50 = fake_clockCycles(50.0);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per50, per50, 8, fake_msToCycles(0.1));
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], steps + 2);	/* pulses 0 and 6 of the new count */
	ASSERT_NEAR(seqc_currentBpm(), 50.0, 1e-3);	/* the old intervals are gone */
	ASSERT_EQ(stats().intervalsExcluded, 0);
}

TEST(T16_switch_from_internal_while_running)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, (seqc_time_t)(fake_tickCycles(120.0) * 100.0), fake_msToCycles(0.1));
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	const double at = seqc_position();
	ASSERT_NEAR(at, 100.0, 0.1);
	/* the next clock adds 16 ticks to the position */
	const double per = fake_clockCycles(120.0);
	fake_runClocks(h, SEQC_EV_CLOCK, h->now + 1000, per, 6, fake_msToCycles(0.1));
	/* 6 clocks: 96 ticks, plus the interpolation up to the last process call */
	ASSERT_TRUE(seqc_position() >= at + 96.0 && seqc_position() < at + 96.1);
	ASSERT_EQ(seqc_absStep(), 2);
}

TEST(T16_switch_to_internal_starts_an_armed_transport)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	seqc_setSyncMode(SEQC_SYNC_INTERNAL);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ(seqc_position(), 0.0);
	fake_process(h, h->now + 10);
	ASSERT_EQ(seqc_absStep(), 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 4), 1);
	size_t pos = 0, n = 0;
	const fake_rec_t *r;
	uint8_t tr[4];
	while ((r = fake_next(h, FAKE_TRANSPORT, &pos)) != NULL)
		if (n < 4)
			tr[n++] = r->value;
	ASSERT_EQ(n, 2);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_ARM);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_START);
}

TEST(T18_jack_4ppq_one_pulse_per_step)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	ASSERT_EQ(seqc_position(), -96.0);		/* one pulse is a step */
	fake_setStep(h, 0, 1, 1, true);
	fake_setTiming(h, 0, 1, 1, 3, 0);		/* x4 between two pulses */
	seqc_start();
	const double per = fake_stepCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_PULSE, t0, per, 9, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 8);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 9);
	for (int k = 0; k < 9; k++)
		ASSERT_EQ(trig[k]->time, t0 + (seqc_time_t)(k * per + 0.5));
	ASSERT_EQ(fake_triggers(h, 1, trig, 64), 4);
	for (int k = 0; k < 4; k++)
		ASSERT_NEAR((double)trig[k]->time, (double)t0 + per * (1.0 + k / 4.0), fake_msToCycles(0.1) + 2.0);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], 9);	/* every pulse begins a step */
}

TEST(T18_jack_4ppq_estimate_uses_4_intervals)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_PULSE, t0, fake_stepCycles(120.0), 9, fake_msToCycles(0.1));
	const seqc_time_t t1 = t0 + (seqc_time_t)(9.0 * fake_stepCycles(120.0));
	fake_runClocks(h, SEQC_EV_PULSE, t1, fake_stepCycles(100.0), 3, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 15.0 / 0.1375, 1e-3);	/* 2 of 4 intervals at 100: (125 + 125 + 150 + 150) / 4 ms */
	fake_runClocks(h, SEQC_EV_PULSE, t1 + (seqc_time_t)(3.0 * fake_stepCycles(100.0)),
			fake_stepCycles(100.0), 2, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-3);	/* 4 intervals at 100 */
}

TEST(T18_jack_24ppq_six_pulses_per_step)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_setAnalogPpq(24);
	ASSERT_EQ(seqc_position(), -16.0);
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_PULSE, t0, per, 4, fake_msToCycles(0.1));	/* count 4 */
	seqc_start();
	fake_runClocks(h, SEQC_EV_PULSE, t0 + (seqc_time_t)(4.0 * per), per, 2, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	fake_runClocks(h, SEQC_EV_PULSE, t0 + (seqc_time_t)(6.0 * per), per, 6 * 4 + 1, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 4);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 5);
	ASSERT_EQ(trig[0]->time, t0 + (seqc_time_t)(6.0 * per + 0.5));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
}

TEST(T18_ppq_change_restarts_the_count)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_setAnalogPpq(24);
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_PULSE, t0, per, 3, fake_msToCycles(0.1));	/* count 3 */
	seqc_setAnalogPpq(24);				/* count 0 again */
	seqc_start();
	fake_runClocks(h, SEQC_EV_PULSE, t0 + (seqc_time_t)(3.0 * per), per, 1, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ(seqc_absStep(), 0);
	seqc_setAnalogPpq(7);				/* any other value counts as 4 */
	fake_runClocks(h, SEQC_EV_PULSE, t0 + (seqc_time_t)(4.0 * per), per, 1, fake_msToCycles(0.1));
	/* this pulse is a whole step, plus the interpolation up to the last process call */
	ASSERT_TRUE(seqc_position() >= 96.0 && seqc_position() < 96.1);
}

TEST(T18_ppq_change_clears_the_estimate)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_setAnalogPpq(24);
	fake_runClocks(h, SEQC_EV_PULSE, fake_msToCycles(10.0), fake_clockCycles(120.0), 6, fake_msToCycles(0.1));
	seqc_setAnalogPpq(4);
	const double per = fake_stepCycles(100.0);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 3, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-3);	/* the 24 PPQ intervals are gone */
	ASSERT_EQ(stats().intervalsExcluded, 0);
}

TEST(T18_slow_4ppq_clock_is_not_a_dropout)
{
	/* N15: at 25 BPM the pulses are 600 ms apart, longer than SEQC_DROPOUT_MS.
	 * The dropout time is two expected intervals: 1.5 s before the first
	 * estimate (20 BPM), then 1.2 s. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_start();
	const double per = fake_stepCycles(25.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_PULSE, t0, per, 6, fake_msToCycles(1.0));
	ASSERT_EQ(stats().dropouts, 0);
	ASSERT_NEAR(seqc_currentBpm(), 25.0, 1e-3);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 6);
	const seqc_time_t last = t0 + (seqc_time_t)(5.0 * per + 0.5);
	fake_runUntil(h, last + fake_msToCycles(1150.0), fake_msToCycles(1.0));
	ASSERT_EQ(stats().dropouts, 0);
	fake_runUntil(h, last + fake_msToCycles(1250.0), fake_msToCycles(1.0));
	ASSERT_EQ(stats().dropouts, 1);
}

TEST(T18_dropout_time_without_an_estimate)
{
	/* N15: before the first interval the 4 PPQ dropout time is 1.5 s, two
	 * steps at 20 BPM */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_start();
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_sync(h, SEQC_EV_PULSE, t0);			/* step 0, no interval yet */
	fake_runUntil(h, t0 + fake_msToCycles(1450.0), fake_msToCycles(1.0));
	ASSERT_EQ(stats().dropouts, 0);
	fake_runUntil(h, t0 + fake_msToCycles(1550.0), fake_msToCycles(1.0));
	ASSERT_EQ(stats().dropouts, 1);
}

TEST(T18_pulse_only_in_analog_sync)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	fake_sync(h, SEQC_EV_CLOCK, fake_msToCycles(2.0));
	ASSERT_EQ(stats().syncIgnored, 1);
	fake_sync(h, SEQC_EV_PULSE, fake_msToCycles(3.0));
	ASSERT_EQ(stats().syncIgnored, 1);
}

/* D7: the hits of a swung step that run past the next clocks and the next step
 * boundaries all play, in order. fw 1.7 dropped the pulses that the next clock's
 * step left unprocessed. */
static void checkSwungSpillOver(bool bunched)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	h->shuffle = 127;				/* swing 64 on odd steps */
	fake_setStep(h, 0, 1, 1, true);
	fake_setTiming(h, 0, 1, 1, 5, 7);		/* x8 at +7: 96 + 64 + 84 + 12 j, up to 328 */
	seqc_start();
	const double per = fake_clockCycles(100.0);	/* not the internal 120 BPM */
	const seqc_time_t t0 = fake_msToCycles(10.0);
	const size_t count = 6 * 6 + 1;			/* up to step 6 */
	for (size_t i = 0; i < count; i++)
		stamps[i] = t0 + (seqc_time_t)((double)(bunched ? i / 3 * 3 + 2 : i) * per);
	deliver(h, SEQC_EV_CLOCK, stamps, count, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 6);
	const size_t n = fake_triggers(h, 1, trig, 64);
	ASSERT_EQ(n, 8);
	for (size_t j = 0; j < n; j++)
	{
		ASSERT_EQ(trig[j]->ev.absStep, 1);
		ASSERT_EQ(trig[j]->ev.hit, j);
		ASSERT_EQ(trig[j]->ev.tick, 96 + 64 + 84 + 12 * (int64_t)j);
		if (j > 0)
			ASSERT_TRUE(trig[j]->time >= trig[j - 1]->time);
	}
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 7);	/* the grid, steps 0..6 */
}

TEST(T16_swung_ratchet_spills_past_the_next_clock)
{
	checkSwungSpillOver(false);
}

TEST(T16_swung_ratchet_spills_past_bunched_clocks)
{
	checkSwungSpillOver(true);
}

TEST(T18_ppq_change_in_midi_sync_keeps_the_count)
{
	/* the jack PPQ has no part in MIDI sync: a post of it (the globals of every
	 * project load send it) leaves the MIDI clock count alone, so tempoStep()
	 * stays on the step boundaries. The value is kept for analog sync. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 6 * 4 + 4, fake_msToCycles(0.1));	/* clock 27: tick 432 */
	seqc_setAnalogPpq(4);
	seqc_setAnalogPpq(24);
	fake_clearLog(h);
	fake_runClocks(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(28.0 * per), per, 6 * 3, fake_msToCycles(0.1));
	size_t pos = 0, steps = 0;
	const fake_rec_t *r;
	while ((r = fake_next(h, FAKE_TEMPO_STEP, &pos)) != NULL)
	{
		ASSERT_EQ((long long)r->tick % 96, 0);	/* clocks 30, 36 and 42 */
		steps++;
	}
	ASSERT_EQ(steps, 3);
	/* in analog sync the kept 24 PPQ applies: the next pulse is 16 ticks */
	seqc_setSyncMode(SEQC_SYNC_ANALOG);
	const double at = seqc_position();
	fake_runClocks(h, SEQC_EV_PULSE, h->now + 1000, per, 1, fake_msToCycles(0.1));
	ASSERT_TRUE(seqc_position() >= at + 16.0 && seqc_position() < at + 16.1);
}

TEST_MAIN()
