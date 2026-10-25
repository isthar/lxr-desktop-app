/*
 * test_slave.c
 *
 * Slave modes (spec 5, N4, N9, N15, N16, N17, N21):
 * - T16: MIDI clock: a steady clock, USB-like jitter of +-1 ms, a tempo ramp
 *   from 120 to 140 BPM, early, late and bunched clocks without a lost event,
 *   the clamp to the next clock, outliers, a dropout and the resume, the local
 *   start on the master's step grid, the sync mode switches, tempo jumps beyond
 *   the outlier band, and bunched clocks that are no tempo jump (N16); clocks
 *   that share a stamp, in USB transfers of several clocks and one transfer
 *   per audio buffer, give the clock tempo (N21, T16_packed_*);
 * - T18: the clock in jack at 4 and 24 PPQ, a tempo jump at 4 PPQ (N16), the
 *   pending note-off sent at a dropout (N17), and a lost or an extra pulse.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>

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
	ASSERT_EQ_DBL(seqc_position(), -16.0);		/* one clock before step 0 */
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
	/* N21: a small tempo change, inside the outlier band: the long window of
	 * the fit gives way to the short one, the fit of the last 24 intervals.
	 * 12 intervals at 120 and 12 at 118 BPM: by symmetry its slope is their
	 * mean. (A step to 100 BPM restarts the fit instead, see
	 * T16_packed_step_restarts_the_fit.) */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, fake_clockCycles(120.0), 30, fake_msToCycles(0.1));
	const seqc_time_t t1 = t0 + (seqc_time_t)(29.0 * fake_clockCycles(120.0) + 0.5)
			+ (seqc_time_t)fake_clockCycles(118.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t1, fake_clockCycles(118.0), 12, fake_msToCycles(0.1));
	const double mean = (12.0 * fake_clockCycles(120.0) + 12.0 * fake_clockCycles(118.0)) / 24.0;
	ASSERT_NEAR(seqc_currentBpm(), SEQC_CPU_HZ * 60.0 / (24.0 * mean), 5e-3);
	ASSERT_EQ(stats().intervalsExcluded, 0);
	ASSERT_EQ(stats().tempoRestarts, 0);
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
	/* N21: each packet is one point, at its last clock, so the estimate is the
	 * clock tempo. The run ends on a packet cut short: clock 96 alone, with the
	 * stamp of clock 98, two clocks late, the one outlier. */
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, 1);
	ASSERT_EQ(stats().tempoRestarts, 0);
}

TEST(T16_shared_stamp_without_an_estimate_is_one_transfer)
{
	/* USB bunching right after the switch: the first two clocks share a stamp.
	 * They are one transfer, the first point of the fit, at the second clock
	 * (N21): no interval of 0 is measured, and nothing is left out. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const double per = fake_clockCycles(100.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_sync(h, SEQC_EV_CLOCK, t0);
	fake_sync(h, SEQC_EV_CLOCK, t0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(per + 0.5), per, 30, fake_msToCycles(0.1));
	ASSERT_EQ(stats().intervalsExcluded, 0);
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-2);
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

TEST(T16_outlier_clock_is_left_out)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	const size_t count = 60;
	for (size_t i = 0; i < count; i++)
		stamps[i] = t0 + (seqc_time_t)((double)i * per + (i == 40 ? 0.8 * per : 0.0));
	deliver(h, SEQC_EV_CLOCK, stamps, count, fake_msToCycles(0.1));
	/* clock 40 is 0.8 of a clock late, further from the fitted line than the
	 * band: left out. Clock 41 is on time, on the line: used (N21: the fit
	 * leaves out points, not intervals). */
	ASSERT_EQ(stats().intervalsExcluded, 1);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 10);	/* steps 0..9 */
}

TEST(T16_interval_inside_the_band_is_kept)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 30, fake_msToCycles(0.1));
	const seqc_time_t late = t0 + (seqc_time_t)(29.0 * per + 1.4 * per);
	fake_sync(h, SEQC_EV_CLOCK, late);
	fake_process(h, late);			/* its transfer is complete (N21) */
	/* 40% late, inside the band: used. The least-squares fit of all 31 points
	 * (N21) weights interval j of n - 1 by j (n - j), here 30 of
	 * n (n^2 - 1) / 6 = 4960 for the last one. */
	ASSERT_EQ(stats().intervalsExcluded, 0);
	ASSERT_NEAR(seqc_currentBpm(), 120.0 / (1.0 + 0.4 * 30.0 / 4960.0), 1e-3);
}

TEST(T16_outlier_band_is_half_a_clock)
{
	/* SEQC_TEMPO_OUTLIER_PCT: with exact stamps the band is half a clock, so a
	 * clock 0.48 of a clock late is used and one 0.52 late is left out */
	static const double offsets[] = { 0.48, 0.52 };
	for (size_t i = 0; i < 2; i++)
	{
		fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
		const double per = fake_clockCycles(120.0);
		const seqc_time_t t0 = fake_msToCycles(10.0);
		fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 30, fake_msToCycles(0.1));
		const seqc_time_t late = t0 + (seqc_time_t)((30.0 + offsets[i]) * per);
		fake_sync(h, SEQC_EV_CLOCK, late);
		fake_process(h, late);
		ASSERT_EQ(stats().intervalsExcluded, i);
	}
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

TEST(T16_dropout_while_stopped)
{
	/* stopped, only the gap between two stamps can show a dropout: the master
	 * stops its clock, changes the tempo from 120 to 60 BPM and starts it again */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 30, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	const seqc_time_t t1 = t0 + (seqc_time_t)(29.0 * per + 0.5) + fake_msToCycles(2000.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t1, fake_clockCycles(60.0), 30, fake_msToCycles(0.1));
	ASSERT_EQ(stats().dropouts, 1);
	ASSERT_NEAR(seqc_currentBpm(), 60.0, 1e-3);	/* the gap is not used */
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

TEST(T16_restart_after_a_stop_waits_for_the_masters_step)
{
	/* the local stop button (stop, rewind) and play while the master's clock
	 * runs on: the rewind goes to one clock before step 0, and the count is not
	 * reset, so step 0 comes with the master's next 16th, with no burst of the
	 * steps played before */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 6 * 8 + 3, fake_msToCycles(0.1));	/* clock 50 */
	ASSERT_EQ(seqc_absStep(), 8);
	seqc_stop();
	seqc_rewind();
	ASSERT_EQ_DBL(seqc_position(), -16.0);		/* one clock before step 0 */
	fake_clearLog(h);
	seqc_start();
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	/* the count runs on: clocks 51..53 do not begin a step, clock 54 does */
	fake_runClocks(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(51.0 * per + 0.5), per, 3, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	fake_runClocks(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(54.0 * per + 0.5), per, 7, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 1);			/* steps 0 and 1, no burst */
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 2);
	ASSERT_EQ(trig[0]->ev.absStep, 0);
	ASSERT_EQ(trig[0]->time, t0 + (seqc_time_t)(54.0 * per + 0.5));
	ASSERT_EQ(trig[1]->time, t0 + (seqc_time_t)(60.0 * per + 0.5));
}

TEST(T16_position_follows_the_tempo_between_clocks)
{
	/* between clocks seqc_position() is interpolated at the estimate: 2/16 of a
	 * clock interval after clock 30 (tick 480) it is 482. Nothing is due before
	 * tick 484 (the note-off of step 5), so the core's own position stays 480 (an
	 * N10 early exit): the position is not the last handled item */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 31, fake_msToCycles(0.1));
	fake_process(h, t0 + (seqc_time_t)(30.0 * per + 2.0 / 16.0 * per + 0.5));
	ASSERT_NEAR(seqc_position(), 482.0, 0.01);
}

TEST(T16_late_clock_anchors_at_its_stamp)
{
	/* a clock stamped before the core's current time (stamped after seq_tick()
	 * drained the queue, delivered at the next one) is handled at the current
	 * time, but its own stamp anchors the interpolation and feeds the fit */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 31, fake_msToCycles(0.1));	/* clock 30: tick 480 */
	const seqc_time_t stamp = t0 + (seqc_time_t)(31.0 * per + 0.5);
	fake_process(h, stamp + fake_msToCycles(5.0));
	fake_sync(h, SEQC_EV_CLOCK, stamp);		/* clock 31, delivered 5 ms late */
	ASSERT_NEAR(seqc_position(), 496.0 + 0.005 * 768.0, 0.01);	/* from the stamp */
	fake_process(h, stamp + fake_msToCycles(5.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);	/* its point is at the stamp */
}

TEST(T16_short_gap_while_stopped_is_no_dropout)
{
	/* N15: stopped, a gap between two stamps is a dropout from SEQC_DROPOUT_MS
	 * on (here two clocks at 120 BPM are shorter) */
	static const double gaps[] = { 490.0, 510.0 };
	for (size_t i = 0; i < 2; i++)
	{
		fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
		const double per = fake_clockCycles(120.0);
		const seqc_time_t t0 = fake_msToCycles(10.0);
		fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 30, fake_msToCycles(0.1));
		const seqc_time_t t1 = t0 + (seqc_time_t)(29.0 * per + 0.5) + fake_msToCycles(gaps[i]);
		fake_runClocks(h, SEQC_EV_CLOCK, t1, per, 2, fake_msToCycles(0.1));
		ASSERT_EQ(stats().dropouts, i);
	}
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

TEST(T16_switch_to_midi_arms_a_start_that_has_not_begun)
{
	/* seqc_start() in internal sync, then the switch to MIDI sync before the
	 * seqc_process() that would play step 0: the start becomes a slave's local
	 * start, armed until the master's next step, instead of running on the
	 * interpolation without a clock */
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_setStep(h, 0, 0, 0, true);
	fake_process(h, fake_msToCycles(1.0));
	seqc_start();
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	fake_runUntil(h, fake_msToCycles(100.0), fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), 0);
	ASSERT_EQ(fake_count(h, FAKE_TRIGGER), 0);
	size_t pos = 0, n = 0;
	const fake_rec_t *r;
	uint8_t tr[4];
	while ((r = fake_next(h, FAKE_TRANSPORT, &pos)) != NULL)
		if (n < 4)
			tr[n++] = r->value;
	/* the reported START is taken back before the ARM */
	ASSERT_EQ(n, 3);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_START);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_STOP);
	ASSERT_EQ(tr[2], SEQC_TRANSPORT_ARM);
	/* the count restarted at the switch: the first clock begins a step */
	const double per = fake_clockCycles(120.0);
	fake_runClocks(h, SEQC_EV_CLOCK, h->now + (seqc_time_t)per, per, 7, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ(seqc_absStep(), 1);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	/* and the step clock reports START again */
	pos = 0;
	n = 0;
	while ((r = fake_next(h, FAKE_TRANSPORT, &pos)) != NULL)
		if (n < 4)
			tr[n++] = r->value;
	ASSERT_EQ(n, 4);
	ASSERT_EQ(tr[3], SEQC_TRANSPORT_START);
}

TEST(T16_tempo_post_while_running_keeps_the_position)
{
	/* with one point of the fit the position runs at the last known tempo; a
	 * tempo post changes that rate, and the position it reached stays. 3.9
	 * ticks after step 0 nothing was due (the next pulse is at 4), so the
	 * position is the interpolation, not yet handled. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	fake_runClocks(h, SEQC_EV_CLOCK, fake_msToCycles(10.0), fake_clockCycles(120.0), 1, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 0);
	fake_process(h, fake_msToCycles(15.1));
	const double before = seqc_position();
	ASSERT_TRUE(before > 3.5 && before < 4.0);
	seqc_setTempo(60.0f);
	ASSERT_TRUE(seqc_position() >= before);
	fake_process(h, h->now + fake_msToCycles(1.0));
	ASSERT_TRUE(seqc_position() >= before);
}

TEST(T16_switch_keeps_the_position_between_items)
{
	/* the switch keeps the live position, not the last handled item: at tick
	 * 102, off the 4-tick grid of the clock out pulses */
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, (seqc_time_t)(fake_tickCycles(120.0) * 102.0), fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_position(), 102.0, 0.1);
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	ASSERT_NEAR(seqc_position(), 102.0, 0.1);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
}

TEST(T16_switch_between_slave_modes_keeps_an_armed_transport)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	seqc_setSyncMode(SEQC_SYNC_ANALOG);
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + 1000, fake_stepCycles(120.0), 1, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);		/* at 4 PPQ every pulse begins a step */
	ASSERT_EQ(seqc_absStep(), 0);
}

TEST(T16_same_sync_mode_again_keeps_the_estimate_and_the_count)
{
	/* fw 1.7 calls seq_setExtSync() on every front panel BPM message: a post of
	 * the mode in use changes nothing, so a local start still lands on the
	 * master's 16th grid */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const double per = fake_clockCycles(100.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_CLOCK, t0, per, 4, fake_msToCycles(0.1));	/* count 4 */
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-3);
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-3);
	seqc_start();
	fake_runClocks(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(4.0 * per + 0.5), per, 2, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_ARMED);		/* clocks 4 and 5 */
	fake_runClocks(h, SEQC_EV_CLOCK, t0 + (seqc_time_t)(6.0 * per + 0.5), per, 1, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);		/* clock 6 begins a step */
	ASSERT_EQ(stats().intervalsExcluded, 0);
}

TEST(T16_invalid_sync_mode_is_ignored)
{
	/* the glue passes the posted value unchecked: a mode other than 0..2 keeps
	 * the mode in use */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_setSyncMode((seqc_sync_t)3);
	fake_sync(h, SEQC_EV_CLOCK, fake_msToCycles(2.0));
	ASSERT_EQ(stats().syncIgnored, 0);		/* still MIDI sync */
	seqc_start();
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
}

TEST(T16_switch_to_internal_starts_an_armed_transport)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	seqc_setSyncMode(SEQC_SYNC_INTERNAL);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ_DBL(seqc_position(), 0.0);
	const uint32_t clocks = h->calls[FAKE_MIDI_CLOCK];
	const uint32_t steps = h->calls[FAKE_TEMPO_STEP];
	fake_process(h, h->now + 10);
	ASSERT_EQ(seqc_absStep(), 0);
	/* the start re-phases the MIDI clock out and the tempo steps (N8): both at step 0 */
	ASSERT_EQ(h->calls[FAKE_MIDI_CLOCK], clocks + 1);
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], steps + 1);
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

/* MIDI-slaved to step 40 at 120 BPM, running or stopped there */
static fake_host_t *slavedToStep40(bool stop)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	fake_runClocks(h, SEQC_EV_CLOCK, fake_msToCycles(10.0), fake_clockCycles(120.0), 6 * 40 + 1,
			fake_msToCycles(0.1));	/* clock 240: tick 3840 */
	if (stop)
		seqc_stop();
	return h;
}

TEST(T16_switch_to_internal_while_running)
{
	/* the position continues at the master tempo (120 BPM), and the MIDI clock
	 * out and the tempo steps start from it: no burst to catch up */
	fake_host_t *h = slavedToStep40(false);
	ASSERT_EQ(seqc_absStep(), 40);
	seqc_setSyncMode(SEQC_SYNC_INTERNAL);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	const double at = seqc_position();
	ASSERT_NEAR(at, 3840.0, 0.2);
	const uint32_t clocks = h->calls[FAKE_MIDI_CLOCK];
	const uint32_t steps = h->calls[FAKE_TEMPO_STEP];
	fake_runUntil(h, h->now + fake_msToCycles(50.0), fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_NEAR(seqc_position(), at + 0.05 * 768.0, 0.2);
	ASSERT_TRUE(h->calls[FAKE_MIDI_CLOCK] - clocks >= 2 && h->calls[FAKE_MIDI_CLOCK] - clocks <= 3);
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], steps);
	ASSERT_EQ(seqc_absStep(), 40);
}

TEST(T16_switch_to_internal_while_stopped)
{
	/* stopped away from the start: the position holds, and the free-running
	 * timeline continues from it without a burst of MIDI clocks */
	fake_host_t *h = slavedToStep40(true);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	const double at = seqc_position();
	seqc_setSyncMode(SEQC_SYNC_INTERNAL);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	ASSERT_EQ_DBL(seqc_position(), at);
	const uint32_t clocks = h->calls[FAKE_MIDI_CLOCK];
	const uint32_t steps = h->calls[FAKE_TEMPO_STEP];
	fake_runUntil(h, h->now + fake_msToCycles(50.0), fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	ASSERT_EQ_DBL(seqc_position(), at);
	ASSERT_TRUE(h->calls[FAKE_MIDI_CLOCK] - clocks >= 2 && h->calls[FAKE_MIDI_CLOCK] - clocks <= 3);
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], steps);
}

TEST(T18_jack_4ppq_one_pulse_per_step)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	ASSERT_EQ_DBL(seqc_position(), -96.0);		/* one pulse is a step */
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

TEST(T18_ppq_post_while_running_keeps_the_position)
{
	/* at 4 PPQ 50.5 ticks after a pulse, a post of 24 PPQ makes the next pulse
	 * 16 ticks: the position reached stays, it does not fall back to the
	 * new clamp */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_start();
	const double per = fake_stepCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_PULSE, t0, per, 3, fake_msToCycles(0.1));	/* pulse 2: tick 192 */
	fake_process(h, t0 + (seqc_time_t)(2.0 * per + 48.5 / 96.0 * per + 0.5));
	fake_process(h, t0 + (seqc_time_t)(2.0 * per + 50.5 / 96.0 * per + 0.5));	/* nothing due */
	const double before = seqc_position();
	ASSERT_NEAR(before, 192.0 + 50.5, 0.01);
	seqc_setAnalogPpq(24);
	ASSERT_TRUE(seqc_position() >= before);
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
	ASSERT_EQ_DBL(seqc_position(), -16.0);
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
		ASSERT_NEAR(fmod(r->tick, 96.0), 0.0, 1e-9);	/* clocks 30, 36 and 42 */
		steps++;
	}
	ASSERT_EQ(steps, 3);
	/* in analog sync the kept 24 PPQ applies: the next pulse is 16 ticks */
	seqc_setSyncMode(SEQC_SYNC_ANALOG);
	const double at = seqc_position();
	fake_runClocks(h, SEQC_EV_PULSE, h->now + 1000, per, 1, fake_msToCycles(0.1));
	ASSERT_TRUE(seqc_position() >= at + 16.0 && seqc_position() < at + 16.1);
}

/* append count clock stamps, period cycles apart, to stamps[] from index n on:
 * the first one a period after stamps[n - 1], or at 10 ms if n is 0. Returns
 * the new number of stamps. */
static size_t appendClocks(size_t n, double period, size_t count)
{
	double t = n > 0 ? (double)stamps[n - 1] : (double)fake_msToCycles(10.0) - period;
	for (size_t i = 0; i < count && n < MAX_CLOCKS; i++)
	{
		t += period;
		stamps[n++] = (seqc_time_t)(t + 0.5);
	}
	return n;
}

/* N16: a MIDI slave settles at from BPM (a full window), then gets clocks at
 * to BPM, outside the band. The first SEQC_TEMPO_RESTART - 1 of them are left
 * out, the next one restarts the estimate, and every clock moves the position. */
static void checkTempoJump(double from, double to)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const size_t settled = appendClocks(0, fake_clockCycles(from), SEQC_TEMPO_WINDOW + 1);
	const size_t n = appendClocks(settled, fake_clockCycles(to), SEQC_TEMPO_RESTART + 24);
	deliver(h, SEQC_EV_CLOCK, stamps, settled + SEQC_TEMPO_RESTART - 1, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), from, 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART - 1);
	ASSERT_EQ(stats().tempoRestarts, 0);
	deliver(h, SEQC_EV_CLOCK, stamps + settled + SEQC_TEMPO_RESTART - 1, 1, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), to, 1e-3);	/* from the SEQC_TEMPO_RESTART outliers alone */
	ASSERT_EQ(stats().tempoRestarts, 1);
	deliver(h, SEQC_EV_CLOCK, stamps + settled + SEQC_TEMPO_RESTART, 24, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), to, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART - 1);
	/* the position followed every clock: no step lost or doubled */
	const size_t steps = (n - 1) / SEQC_MIDI_CLOCKS_PER_STEP + 1;
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), steps);
	for (size_t k = 0; k < steps; k++)
		ASSERT_EQ(trig[k]->ev.absStep, k);
}

TEST(T16_tempo_jump_up_is_followed)
{
	/* 90 to 200 BPM: the intervals get 55% shorter, so every clock after the
	 * jump is an outlier. With the tempo fit (N21), smaller steps of about 15%
	 * or more also take the clocks out of the fit's band within a few clocks
	 * and restart it (T16_packed_step_restarts_the_fit). */
	checkTempoJump(90.0, 200.0);
}

TEST(T16_tempo_jump_down_is_followed)
{
	/* 200 to 90 BPM: the intervals get 122% longer */
	checkTempoJump(200.0, 90.0);
}

TEST(T16_alternating_outliers_do_not_restart)
{
	/* at 120 BPM, 8 intervals alternately 60% longer and 60% shorter: every
	 * other clock is 0.6 of a clock late, and the one after it back on the
	 * line. The 4 late ones are outliers on their own, glitches: no jump.
	 * Then SEQC_TEMPO_RESTART longer intervals in a row, 1.6 to 1.9 times the
	 * interval, are one: the fit restarts from those clocks alone, as after a
	 * dropout (N21), at the mean of the intervals between them, 1.8 times. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, SEQC_TEMPO_WINDOW + 1);
	for (int k = 0; k < 8; k++)
		n = appendClocks(n, per * (k % 2 == 0 ? 1.6 : 0.4), 1);
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, 4);
	ASSERT_EQ(stats().tempoRestarts, 0);
	const size_t before = n;
	for (unsigned k = 0; k < SEQC_TEMPO_RESTART; k++)
		n = appendClocks(n, per * (1.6 + 0.1 * k), 1);
	deliver(h, SEQC_EV_CLOCK, stamps + before, n - before, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0 / 1.8, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_EQ(stats().intervalsExcluded, 4 + SEQC_TEMPO_RESTART - 1);
}

TEST(T16_other_side_outlier_starts_a_new_row)
{
	/* at 120 BPM: one clock 0.6 of a clock early, an outlier before the line,
	 * then SEQC_TEMPO_RESTART clocks at 40 BPM, each further after the line.
	 * The first late one ends the row of the early one and starts a new row of
	 * one, so only the last of them restarts the fit, from the late ones alone
	 * (N16, N21), at 40 BPM. (A late outlier cannot be followed by an early one
	 * straight away: the next clock would have to come before it.) */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, SEQC_TEMPO_WINDOW + 1);
	n = appendClocks(n, per * 0.4, 1);
	n = appendClocks(n, fake_clockCycles(40.0), SEQC_TEMPO_RESTART);
	deliver(h, SEQC_EV_CLOCK, stamps, n - 1, fake_msToCycles(0.1));
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	deliver(h, SEQC_EV_CLOCK, stamps + n - 1, 1, fake_msToCycles(0.1));
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_NEAR(seqc_currentBpm(), 40.0, 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART);
}

TEST(T16_inlier_resets_the_outlier_run)
{
	/* at 120 BPM: 3 clocks at 270 BPM (intervals 56% shorter), each further
	 * before the line, then one back on the line, then 3 more at 270: no
	 * restart, the estimate stays. The count started again at the clock back on
	 * the line, so the next outlier is the 4th in a row. (N21: the fit tests how
	 * far a clock is from its line; one interval at 120 after the first 3 would
	 * leave the clock early, in the row.) */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0), fast = fake_clockCycles(270.0);
	size_t n = appendClocks(0, per, SEQC_TEMPO_WINDOW + 1);
	n = appendClocks(n, fast, 3);
	n = appendClocks(n, 4.0 * per - 3.0 * fast, 1);	/* back on the line */
	n = appendClocks(n, fast, 3);
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, 6);
	ASSERT_EQ(stats().tempoRestarts, 0);
	const size_t before = n;
	n = appendClocks(n, fast, 1);
	deliver(h, SEQC_EV_CLOCK, stamps + before, 1, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 270.0, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 1);
	seqc_resetStats();
	ASSERT_EQ(stats().tempoRestarts, 0);
}

TEST(T16_long_window_is_96_clocks)
{
	/* N21: the estimate is the slope of the last SEQC_TEMPO_LONG_WINDOW clocks.
	 * Clock 30 is 0.3 of a clock late, inside the band: it tilts the line while
	 * it is in the long window, at clock 126 (96 clocks later) still, and not
	 * once it has left it at clock 127 (the short window lost it long before,
	 * without a switch) */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, 140);
	stamps[30] += (seqc_time_t)(0.3 * per);
	deliver(h, SEQC_EV_CLOCK, stamps, 127, fake_msToCycles(0.1));	/* clocks 0..126 */
	ASSERT_TRUE(fabs(seqc_currentBpm() - 120.0) > 5e-3);
	deliver(h, SEQC_EV_CLOCK, stamps + 127, 1, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-4);
	deliver(h, SEQC_EV_CLOCK, stamps + 128, n - 128, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-4);
	ASSERT_EQ(stats().intervalsExcluded, 0);
}

/* 300 clocks at 120 BPM, from clock 200 at 126, every one from clock 6 on
 * up to 0.3 ms of jitter. With stale, a clock stamped 0.2 ms before clock 5
 * comes after it, behind a Continue (ignored while running), so it does not join
 * clock 5's transfer: an untested point of the fit, stamped before the newest
 * one. bpm[i] is the estimate after clock i. */
static void runStaleClock(bool stale, double *bpm)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	size_t n = appendClocks(0, fake_clockCycles(120.0), 200);
	n = appendClocks(n, fake_clockCycles(126.0), 100);
	lcg = 12345;
	for (size_t i = 6; i < n; i++)
		stamps[i] += (seqc_time_t)(int64_t)(jitterMs() * 0.3 * (double)fake_msToCycles(1.0));
	deliver(h, SEQC_EV_CLOCK, stamps, 6, fake_msToCycles(0.1));
	const seqc_time_t t = stamps[5] + fake_msToCycles(1.0);
	fake_sync(h, SEQC_EV_CONTINUE, t);
	fake_process(h, t);
	if (stale)
		fake_sync(h, SEQC_EV_CLOCK, stamps[5] - fake_msToCycles(0.2));
	fake_process(h, t + fake_msToCycles(0.1));
	for (size_t i = 6; i < n; i++)
	{
		deliver(h, SEQC_EV_CLOCK, stamps + i, 1, fake_msToCycles(0.1));
		bpm[i] = seqc_currentBpm();
	}
}

TEST(T16_stale_clock_in_the_first_points_leaves_the_window_cleanly)
{
	/* The stale clock goes into the ring after clock 5, which is stamped later.
	 * It must not leave the long window early, nor take wrong sums with it, when
	 * clock 5 becomes the base: once it has left the window (from clock 101 on),
	 * the estimate is that of the same clocks without it, also across the tempo
	 * change. */
	static double with[300], without[300];
	runStaleClock(true, with);
	runStaleClock(false, without);
	for (size_t i = 101; i < 300; i++)
		ASSERT_NEAR(with[i], without[i], 1e-9);
	ASSERT_NEAR(with[299], 126.0, 0.05);
}

/* deliver stamps[from..to) one transfer at a time: the clocks with one stamp,
 * then seqc_process() at that stamp */
static void deliverPacked(fake_host_t *h, size_t from, size_t to)
{
	size_t i = from;
	while (i < to)
	{
		const seqc_time_t t = stamps[i];
		while (i < to && stamps[i] == t)
			fake_sync(h, SEQC_EV_CLOCK, stamps[i++]);
		fake_process(h, t);
	}
}

TEST(T16_second_jump_right_after_a_restart)
{
	/* in transfers of 4 clocks: 200 to 90 BPM restarts the fit from its 4
	 * outlier transfers, which span 12 clocks, half a short window, so the next
	 * transfer is tested at once (N21): 40 BPM, again later. The restart
	 * emptied the row, so this outlier starts a new row of one (without that,
	 * it would be the 5th in a row of 4) and the second jump takes
	 * SEQC_TEMPO_RESTART transfers, like the first. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	size_t n = appendClocks(0, fake_clockCycles(200.0), 4 * SEQC_TEMPO_WINDOW);
	n = appendClocks(n, fake_clockCycles(90.0), 4 * SEQC_TEMPO_RESTART);
	const size_t first = n;
	n = appendClocks(n, fake_clockCycles(40.0), 4 * SEQC_TEMPO_RESTART);
	for (size_t k = 0; k < n; k++)
		stamps[k] = stamps[k | 3u];		/* transfers of 4, at their last clock */
	deliverPacked(h, 0, first);
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_NEAR(seqc_currentBpm(), 90.0, 1e-3);
	deliverPacked(h, first, first + 4);
	ASSERT_NEAR(seqc_currentBpm(), 90.0, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART);	/* a new row of one */
	deliverPacked(h, first + 4, n);
	ASSERT_EQ(stats().tempoRestarts, 2);
	ASSERT_NEAR(seqc_currentBpm(), 40.0, 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, 2 * (SEQC_TEMPO_RESTART - 1));
}

TEST(T16_bunched_clocks_are_not_a_tempo_jump)
{
	/* USB: a late packet brings 6 clocks with one stamp, that of the last. They
	 * are one transfer, one point of the fit at the 6th clock (N21), and it lies
	 * on the line: no outlier and nothing left out. The estimate stays, and the
	 * position follows every clock. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, SEQC_TEMPO_WINDOW + 1);
	const seqc_time_t packet = stamps[n - 1] + (seqc_time_t)(6.0 * per + 0.5);
	for (int k = 0; k < 6; k++)
		stamps[n++] = packet;
	n = appendClocks(n, per, 12);
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_EQ(stats().intervalsExcluded, 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), (n - 1) / SEQC_MIDI_CLOCKS_PER_STEP + 1);
}

TEST(T16_bunched_pairs_are_not_a_tempo_jump)
{
	/* USB: 2 * SEQC_TEMPO_RESTART late packets in a row, each with 2 clocks
	 * and one stamp, that of the second. Each packet is one point of the fit,
	 * at its second clock, and lies on the line (N21): no outlier, and the
	 * estimate does not restart at half the tempo. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const unsigned pairs = 2 * SEQC_TEMPO_RESTART;
	size_t n = appendClocks(0, per, SEQC_TEMPO_WINDOW + 1);
	for (unsigned k = 0; k < pairs; k++)
	{
		n = appendClocks(n, 2.0 * per, 1);
		stamps[n] = stamps[n - 1];		/* the second clock of the packet */
		n++;
	}
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_EQ(stats().intervalsExcluded, 0);
	const size_t before = n;
	n = appendClocks(n, per, 12);
	deliver(h, SEQC_EV_CLOCK, stamps + before, n - before, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), (n - 1) / SEQC_MIDI_CLOCKS_PER_STEP + 1);
}

/* a late bunch of 6 clocks gap cycles apart (not one stamp), 6 clocks after the
 * last one at 120 BPM, then 24 clocks at 120. A clock less than
 * SEQC_TEMPO_JOIN_MS after the newest clock of the last transfer joins it
 * (unless another sync event came): the bunch is one point of the fit, at its
 * last clock, 5 gaps late, near the line. Nothing is left out and the fit does
 * not restart. */
static void checkBunchedGaps(double gap)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, SEQC_TEMPO_WINDOW + 1);
	stamps[n] = stamps[n - 1] + (seqc_time_t)(6.0 * per + 0.5);
	n++;
	n = appendClocks(n, gap, 5);
	n = appendClocks(n, per, 24);
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 0.5);	/* the bunch's point is 5 gaps late */
	ASSERT_EQ(stats().intervalsExcluded, 0);
	const size_t steps = (n - 1) / SEQC_MIDI_CLOCKS_PER_STEP + 1;
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), steps);
	for (size_t k = 0; k < steps; k++)
	{
		ASSERT_EQ(trig[k]->ev.absStep, k);
		ASSERT_TRUE(trig[k]->time >= stamps[SEQC_MIDI_CLOCKS_PER_STEP * k]);	/* never before its clock */
	}
}

TEST(T16_din_bunched_clocks_are_not_a_tempo_jump)
{
	/* DIN: a forwarded bunch of back-to-back 0xF8 bytes, 0.32 ms apart */
	checkBunchedGaps((double)fake_msToCycles(0.32));
}

TEST(T16_usb_bunched_clocks_10us_apart_are_not_a_tempo_jump)
{
	/* USB: clocks of one burst in separate transfers, 10 us apart */
	checkBunchedGaps((double)fake_msToCycles(0.01));
}

/* a DIN interface forwards a late host buffer as back-to-back 0xF8 bytes, gap
 * cycles apart, from the true time of the last clock of the bunch on: at
 * 120 BPM, every 2 beats a bunch of 2, 3 or 6 clocks. Each clock arrives in
 * its own seq_tick(). A clock less than SEQC_TEMPO_JOIN_MS after the newest
 * clock of the last transfer joins it, so each bunch is one point of the fit,
 * at its last clock, at most 5 gaps late. No restart, nothing left out, within
 * 0.5% from two beats on, and every step played. */
static void checkDinBunches(double gap)
{
	static const size_t sizes[] = { 2, 3, 6 };
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0), t0 = (double)fake_msToCycles(10.0);
	const size_t count = 24 * 16 + 1;
	for (size_t k = 0; k < count; k++)
		stamps[k] = (seqc_time_t)(t0 + (double)k * per + 0.5);
	for (size_t b = 0; b < 7; b++)
	{
		const size_t size = sizes[b % 3];
		const size_t last = 48 * (b + 1) + 11;		/* off the step grid */
		for (size_t j = 0; j < size; j++)
			stamps[last - size + 1 + j] = stamps[last] + (seqc_time_t)((double)j * gap + 0.5);
	}
	double worst = 0.0;
	size_t i = 0;
	seqc_time_t t = h->now;
	while (i < count)
	{
		t += fake_msToCycles(0.1);
		while (i < count && stamps[i] <= t)
			fake_sync(h, SEQC_EV_CLOCK, stamps[i++]);
		fake_process(h, t);
		const double e = fabs((double)seqc_currentBpm() - 120.0) / 120.0 * 100.0;
		if (i > 2 * 24 && e > worst)
			worst = e;
	}
	ASSERT_TRUE(worst < 0.5);
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_EQ(stats().intervalsExcluded, 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 128), (count - 1) / SEQC_MIDI_CLOCKS_PER_STEP + 1);
}

TEST(T16_packed_din_bunch_is_one_transfer)
{
	/* 0.32 ms apart, the bytes of a DIN port; 1.0 and 1.5 ms apart, so that a
	 * bunch of 6 spans 5 and 7.5 ms, more than SEQC_TEMPO_JOIN_MS: the join is
	 * measured from the newest clock of the transfer, not from its first */
	checkDinBunches((double)fake_msToCycles(0.32));
	checkDinBunches((double)fake_msToCycles(1.0));
	checkDinBunches((double)fake_msToCycles(1.5));
}

TEST(T16_packed_din_bunch_head_stays_out_after_another_event)
{
	/* at 120 BPM, clock 40 is 0.6 of a clock late, evaluated alone and left
	 * out. A MIDI Stop follows, then clock 41 1 ms after clock 40. The Stop
	 * ends the transfer of clock 40, so clock 41 does not join it: it is a
	 * transfer of its own, inside the band, and clock 40 stays counted. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, 40);
	const seqc_time_t head = stamps[n - 1] + (seqc_time_t)(1.6 * per + 0.5);
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	fake_sync(h, SEQC_EV_CLOCK, head);
	fake_process(h, head);
	ASSERT_EQ(stats().intervalsExcluded, 1);
	fake_sync(h, SEQC_EV_STOP, head + fake_msToCycles(0.5));
	fake_sync(h, SEQC_EV_CLOCK, head + fake_msToCycles(1.0));
	fake_process(h, head + fake_msToCycles(1.0));
	ASSERT_EQ(stats().intervalsExcluded, 1);
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 0.5);
}

TEST(T16_packed_din_bunch_head_redoes_a_restart)
{
	/* 120 to 60 BPM: SEQC_TEMPO_RESTART clocks at 60 restart the fit, the last
	 * of them the head of a bunch of 2, 0.32 ms apart. The second clock of the
	 * bunch joins the head: the restart is undone and done again with the
	 * whole bunch as the 4th outlier, so it is counted once. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	size_t n = appendClocks(0, fake_clockCycles(120.0), SEQC_TEMPO_WINDOW + 1);
	n = appendClocks(n, fake_clockCycles(60.0), SEQC_TEMPO_RESTART);
	stamps[n] = stamps[n - 1] + fake_msToCycles(0.32);
	n++;
	const size_t before = n;
	n = appendClocks(n, fake_clockCycles(60.0), 2 * SEQC_TEMPO_WINDOW);
	deliver(h, SEQC_EV_CLOCK, stamps, before, fake_msToCycles(0.1));
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART - 1);
	deliver(h, SEQC_EV_CLOCK, stamps + before, n - before, fake_msToCycles(0.1));
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART - 1);
	ASSERT_NEAR(seqc_currentBpm(), 60.0, 0.5);
}

/* at 120 BPM, clock 40 is 0.6 of a clock late, evaluated alone and left out;
 * then one more clock gap cycles after it. Returns the host. */
static fake_host_t *lateHeadThen(double gapMs)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, 40);
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	const seqc_time_t head = stamps[n - 1] + (seqc_time_t)(1.6 * per + 0.5);
	fake_sync(h, SEQC_EV_CLOCK, head);
	fake_process(h, head);
	if (gapMs > 0.0)
	{
		fake_sync(h, SEQC_EV_CLOCK, head + fake_msToCycles(gapMs));
		fake_process(h, head + fake_msToCycles(gapMs));
	}
	return h;
}

TEST(T16_packed_join_is_under_2_ms)
{
	/* SEQC_TEMPO_JOIN_MS: a clock 1.9 ms after the left-out head joins it, and
	 * the head is uncounted; one 2.5 ms after it is a transfer of its own,
	 * inside the band, and the head stays counted */
	lateHeadThen(0.0);
	ASSERT_EQ(stats().intervalsExcluded, 1);
	lateHeadThen(1.9);
	ASSERT_EQ(stats().intervalsExcluded, 0);
	lateHeadThen(2.5);
	ASSERT_EQ(stats().intervalsExcluded, 1);
	ASSERT_EQ(stats().tempoRestarts, 0);
}

TEST(T16_packed_join_uncounts_the_head_once)
{
	/* the head of a burst is left out; its next two clocks, 0.32 and 0.64 ms
	 * later, reach the core in one drain. The first joins and uncounts the
	 * head, the second only joins: an earlier outlier stays counted. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, 60);
	stamps[40] += (seqc_time_t)(0.8 * per);		/* an outlier of its own */
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	ASSERT_EQ(stats().intervalsExcluded, 1);
	const seqc_time_t head = stamps[n - 1] + (seqc_time_t)(3.0 * per + 0.5);	/* clock 60, 2 late */
	fake_sync(h, SEQC_EV_CLOCK, head);
	fake_process(h, head);
	ASSERT_EQ(stats().intervalsExcluded, 2);
	fake_sync(h, SEQC_EV_CLOCK, head + fake_msToCycles(0.32));
	fake_sync(h, SEQC_EV_CLOCK, head + fake_msToCycles(0.64));
	fake_process(h, head + fake_msToCycles(0.64));
	ASSERT_EQ(stats().intervalsExcluded, 1);
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 0.5);
}

TEST(T16_packed_switch_ends_the_join)
{
	/* a switch to analog sync and back between a left-out head and a clock
	 * under SEQC_TEMPO_JOIN_MS after it: the switches cleared the estimate, so
	 * the clock is the first point of a new fit, not a join that brings the old
	 * fit back */
	fake_host_t *h = lateHeadThen(0.0);
	ASSERT_EQ(stats().intervalsExcluded, 1);
	seqc_setSyncMode(SEQC_SYNC_ANALOG);
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	const seqc_time_t t = h->now + fake_msToCycles(1.0);
	fake_sync(h, SEQC_EV_CLOCK, t);
	fake_process(h, t);
	ASSERT_EQ(stats().intervalsExcluded, 1);
	ASSERT_NEAR(seqc_currentBpm(), SEQC_DEFAULT_BPM, 1e-3);	/* no estimate: the master tempo */
}

TEST(T18_jack_pulses_never_join)
{
	/* the join is for MIDI clock only (N21): a jack pulse 1 ms after pulse 29,
	 * delivered before seqc_process(), is an extra pulse of its own and is left
	 * out, and pulse 29, 0.3 of a pulse late but inside the band, stays a point
	 * of the fit. The fit of 30 points weights the last one by 6 / (30 * 31). */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_setAnalogPpq(24);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, 30);
	stamps[n - 1] += (seqc_time_t)(0.3 * per);
	deliver(h, SEQC_EV_PULSE, stamps, n - 1, fake_msToCycles(0.1));
	fake_sync(h, SEQC_EV_PULSE, stamps[n - 1]);
	fake_sync(h, SEQC_EV_PULSE, stamps[n - 1] + fake_msToCycles(1.0));
	fake_process(h, stamps[n - 1] + fake_msToCycles(1.0));
	ASSERT_EQ(stats().intervalsExcluded, 1);
	ASSERT_NEAR(seqc_currentBpm(), 120.0 / (1.0 + 0.3 * 6.0 / (30.0 * 31.0)), 1e-2);
}

TEST(T16_packed_din_bunch_completing_a_jump_is_one_point)
{
	/* 120 to 60 BPM, and the 4th to 9th clocks at 60 come as a DIN burst, 0.32 ms
	 * apart from the true time of the 9th on. The head is the 4th outlier and
	 * restarts the fit; the rest of the burst joins it, so the restart is done
	 * again with the burst as one point, at its last clock: within 0.1% from
	 * two beats after the jump, one restart */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	size_t n = appendClocks(0, fake_clockCycles(120.0), SEQC_TEMPO_WINDOW + 1);
	const size_t jump = n;
	n = appendClocks(n, fake_clockCycles(60.0), 3 * 24);
	const size_t last = jump + 8;
	for (size_t j = 0; j < 6; j++)
		stamps[last - 5 + j] = stamps[last] + (seqc_time_t)((double)j * fake_msToCycles(0.32) + 0.5);
	double worst = 0.0;
	size_t i = 0;
	seqc_time_t t = h->now;
	while (i < n)
	{
		t += fake_msToCycles(0.1);
		while (i < n && stamps[i] <= t)
			fake_sync(h, SEQC_EV_CLOCK, stamps[i++]);
		fake_process(h, t);
		const double e = fabs((double)seqc_currentBpm() - 60.0) / 60.0 * 100.0;
		if (i > jump + 2 * 24 && e > worst)
			worst = e;
	}
	ASSERT_TRUE(worst < 0.1);
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART - 1);
}

/* at 120 BPM, a bunch of size clocks 0.32 ms apart whose last clock is the
 * 9th of a run: after the switch to MIDI sync, or (resume) after a dropout. The
 * fit tests no point while it spans less than half a short window, so the late
 * head of the bunch goes into it. The next clock, less than SEQC_TEMPO_JOIN_MS
 * later, joins it all the same: the fit goes back to before the head, and the
 * bunch is one point, at its last clock. Within 0.5% from two beats on (the
 * head alone made a step amplitude of size - 1 clocks: up to 3.5% for 4
 * beats), no restart, and nothing left out. With outlier, clock 20 before the
 * dropout is 0.8 of a clock late and left out: the join takes back only a head
 * that was left out, so it still counts. */
static void checkEarlyDinBunch(size_t size, bool resume, bool outlier)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = resume ? appendClocks(appendClocks(0, per, SEQC_TEMPO_WINDOW), (double)fake_msToCycles(1000.0), 1)
			: appendClocks(0, per, 1);
	if (outlier)
		stamps[20] += (seqc_time_t)(0.8 * per);
	const size_t first = n - 1, last = first + 8;
	n = appendClocks(n, per, 24 * 8);
	for (size_t j = 0; j < size; j++)
		stamps[last - size + 1 + j] = stamps[last] + (seqc_time_t)((double)j * fake_msToCycles(0.32) + 0.5);
	double worst = 0.0;
	size_t i = 0;
	seqc_time_t t = h->now;
	while (i < n)
	{
		t += fake_msToCycles(0.1);
		while (i < n && stamps[i] <= t)
			fake_sync(h, SEQC_EV_CLOCK, stamps[i++]);
		fake_process(h, t);
		const double e = fabs((double)seqc_currentBpm() - 120.0) / 120.0 * 100.0;
		if (i > first + 2 * 24 && e > worst)
			worst = e;
	}
	ASSERT_TRUE(worst < 0.5);
	ASSERT_EQ(stats().dropouts, resume ? 1 : 0);
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_EQ(stats().intervalsExcluded, outlier ? 1 : 0);
	if (!resume)
		ASSERT_EQ(fake_triggers(h, 0, trig, 128), (n - 1) / SEQC_MIDI_CLOCKS_PER_STEP + 1);
}

TEST(T16_packed_din_bunch_in_the_first_clocks)
{
	checkEarlyDinBunch(3, false, false);
	checkEarlyDinBunch(6, false, false);
	checkEarlyDinBunch(6, true, false);
	checkEarlyDinBunch(6, true, true);
}

TEST(T16_clock_stamped_before_the_newest_point)
{
	/* DIN and USB clocks at once: after clock 40 went into the fit, and a MIDI
	 * Continue (ignored while running) ended its transfer, one more clock comes
	 * stamped 0.2 ms before it. It is a point of its own; its interval from
	 * clock 40 is negative, not 2^64 cycles less 0.2 ms. The step amplitude it
	 * leaves is about one clock, as an extra clock would, and decays, so a jump
	 * from 120 to 60 BPM at clock 150 still restarts the fit within
	 * SEQC_TEMPO_RESTART clocks. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0), slow = fake_clockCycles(60.0);
	double t = (double)fake_msToCycles(10.0);
	for (size_t k = 0; k < 150 + SEQC_TEMPO_RESTART + 1; k++)
	{
		const seqc_time_t ts = (seqc_time_t)(t + 0.5);
		t += k < 150 ? per : slow;
		fake_sync(h, SEQC_EV_CLOCK, ts);
		fake_process(h, ts + fake_msToCycles(0.05));
		if (k == 40)
		{
			fake_sync(h, SEQC_EV_CONTINUE, ts + fake_msToCycles(0.05));
			fake_sync(h, SEQC_EV_CLOCK, ts - fake_msToCycles(0.2));
			fake_process(h, ts + fake_msToCycles(0.1));
		}
	}
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_NEAR(seqc_currentBpm(), 60.0, 0.3);
}

TEST(T16_clock_stamped_before_the_only_point_becomes_the_base)
{
	/* the fit stores the stamps of the long window after its base, the oldest
	 * point: a clock stamped before the only point, in the untested first
	 * points, drops that point and is the new base. The estimate builds up
	 * from it as usual. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const double per = fake_clockCycles(100.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_sync(h, SEQC_EV_CLOCK, t0);
	fake_process(h, t0);
	fake_sync(h, SEQC_EV_CONTINUE, t0 + 100);	/* armed: ends the join */
	fake_sync(h, SEQC_EV_CLOCK, t0 - fake_msToCycles(1.0));
	fake_process(h, t0 + 200);
	size_t n = 0;
	for (size_t k = 0; k < 30; k++)
		stamps[n++] = t0 + (seqc_time_t)((double)(k + 1) * per + 0.5);
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 0.5);
}

/* A host stall: at 120 BPM, after history clocks, the next 3 come 0.6, 1.2 and
 * 1.8 clocks late, outliers in a row, and the 4th, 2.4 late, heads a catch-up
 * burst of 3, 0.32 ms apart, whose last clock is back inside the band. The head
 * is the 4th outlier and restarts the fit, but the burst's next clock joins it:
 * the restart is undone (the ring keeps the points of the fit before it, also
 * when the window is full) and the burst evaluated as one point, which ends
 * the row. No restart, and the tempo stays. */
static void checkStall(size_t history)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, history);
	const seqc_time_t last = stamps[n - 1];
	for (int k = 1; k <= 3; k++)
		stamps[n++] = last + (seqc_time_t)((double)k * per + 0.6 * k * per + 0.5);
	const seqc_time_t head = last + (seqc_time_t)(4.0 * per + 2.4 * per + 0.5);
	for (int j = 0; j < 3; j++)
		stamps[n++] = head + (seqc_time_t)(j * fake_msToCycles(0.32));
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_EQ(stats().intervalsExcluded, 3);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1.0);
	/* the master goes on from the burst's last clock: within 1% from two beats
	 * on (the burst lands 0.43 of a clock late), and exact at the end */
	const size_t before = n;
	n = appendClocks(n, per, 2 * SEQC_TEMPO_LONG_WINDOW);
	double worst = 0.0;
	for (size_t i = before; i < n; i++)
	{
		deliver(h, SEQC_EV_CLOCK, stamps + i, 1, fake_msToCycles(0.1));
		const double e = fabs((double)seqc_currentBpm() - 120.0) / 120.0 * 100.0;
		if (i >= before + 2 * 24 && e > worst)
			worst = e;
	}
	ASSERT_TRUE(worst < 1.0);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 0);
}

TEST(T16_undone_restart_keeps_the_window)
{
	/* the ring keeps the points of the fit before a restart, so undoing it
	 * restores the whole window: at 120 BPM, after 200 clocks of which clock 105
	 * is 0.3 of a clock late (inside the band), 3 clocks come late in a row and
	 * the 4th heads a burst of 3 that ends on time. The head restarts the fit,
	 * the burst undoes it, and the burst's point takes clock 105 out of the
	 * long window as it should: the estimate is exact at once. (Had the restart
	 * written over the oldest points of the window, they would stay in the
	 * sums: 120.02 BPM.) */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	size_t n = appendClocks(0, per, 200);
	stamps[105] += (seqc_time_t)(0.3 * per);
	deliver(h, SEQC_EV_CLOCK, stamps, n, fake_msToCycles(0.1));
	ASSERT_TRUE(fabs(seqc_currentBpm() - 120.0) > 0.01);	/* clock 105 tilts the line */
	const size_t before = n;
	const seqc_time_t last = stamps[n - 1];
	for (int k = 1; k <= 3; k++)
		stamps[n++] = last + (seqc_time_t)((double)k * per + 0.6 * k * per + 0.5);
	const seqc_time_t end = last + (seqc_time_t)(6.0 * per + 0.5);
	for (int j = 0; j < 3; j++)
		stamps[n++] = end - (seqc_time_t)((2 - j) * fake_msToCycles(0.32));
	deliver(h, SEQC_EV_CLOCK, stamps + before, n - before, fake_msToCycles(0.1));
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_EQ(stats().intervalsExcluded, 3);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
}

TEST(T16_stall_with_a_catch_up_burst_is_no_tempo_jump)
{
	checkStall(31);
	checkStall(SEQC_TEMPO_LONG_WINDOW + 104);	/* the ring full and wrapped */
}

TEST(T18_tempo_jump_4ppq_restarts_after_2)
{
	/* N16 at 4 PPQ: 100 to 225 BPM, pulses 56% closer. The estimate restarts
	 * after SEQC_TEMPO_RESTART_4PPQ outliers in a row. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_start();
	const size_t settled = appendClocks(0, fake_stepCycles(100.0), SEQC_TEMPO_WINDOW_4PPQ + 1);
	const size_t n = appendClocks(settled, fake_stepCycles(225.0), SEQC_TEMPO_RESTART_4PPQ + 4);
	deliver(h, SEQC_EV_PULSE, stamps, settled + SEQC_TEMPO_RESTART_4PPQ - 1, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 0);
	deliver(h, SEQC_EV_PULSE, stamps + settled + SEQC_TEMPO_RESTART_4PPQ - 1, 1, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 225.0, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART_4PPQ - 1);
	deliver(h, SEQC_EV_PULSE, stamps + settled + SEQC_TEMPO_RESTART_4PPQ, 4, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 225.0, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), n);	/* one step per pulse */
	for (size_t k = 0; k < n; k++)
		ASSERT_EQ(trig[k]->ev.absStep, k);
}

TEST(T18_tempo_jump_24ppq_restarts_after_4)
{
	/* N16: the jack at 24 PPQ is a 24 PPQ source, so it takes
	 * SEQC_TEMPO_RESTART outliers in a row, not SEQC_TEMPO_RESTART_4PPQ */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_setAnalogPpq(24);
	seqc_start();
	const size_t settled = appendClocks(0, fake_clockCycles(90.0), SEQC_TEMPO_WINDOW + 1);
	const size_t n = appendClocks(settled, fake_clockCycles(200.0), SEQC_TEMPO_RESTART);
	deliver(h, SEQC_EV_PULSE, stamps, settled + SEQC_TEMPO_RESTART_4PPQ, fake_msToCycles(0.1));
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_NEAR(seqc_currentBpm(), 90.0, 1e-3);
	deliver(h, SEQC_EV_PULSE, stamps + settled + SEQC_TEMPO_RESTART_4PPQ,
			n - settled - SEQC_TEMPO_RESTART_4PPQ, fake_msToCycles(0.1));
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_NEAR(seqc_currentBpm(), 200.0, 1e-3);
}

TEST(T18_dropout_note_off_is_sent)
{
	/* N17, with the jack at 4 PPQ at 120 BPM. After the last pulse (step 4) the
	 * interpolation reaches the clamp and plays step 5 at tick 480; its note-off,
	 * due at 484, waits for a pulse that does not come. At the dropout, 500 ms
	 * after the last pulse, it goes out at once. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_start();
	const double per = fake_stepCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_runClocks(h, SEQC_EV_PULSE, t0, per, 5, fake_msToCycles(0.1));
	const seqc_time_t limit = t0 + (seqc_time_t)(4.0 * per + 0.5) + fake_msToCycles(SEQC_DROPOUT_MS);
	fake_runUntil(h, limit, fake_msToCycles(1.0));
	ASSERT_EQ(stats().dropouts, 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 6);
	ASSERT_EQ(fake_count(h, FAKE_NOTE_OFF), 5);	/* the one of step 5 is pending */
	fake_runUntil(h, limit + fake_msToCycles(1.0), fake_msToCycles(1.0));
	ASSERT_EQ(stats().dropouts, 1);
	ASSERT_EQ(fake_count(h, FAKE_NOTE_OFF), 6);
	size_t pos = 0;
	const fake_rec_t *r, *off = NULL;
	while ((r = fake_next(h, FAKE_NOTE_OFF, &pos)) != NULL)
		off = r;
	ASSERT_EQ(off->track, 0);
	ASSERT_TRUE(off->time > limit && off->time <= limit + fake_msToCycles(1.0));
	ASSERT_NEAR(off->tick, 480.0, 1e-9);		/* the position holds at the clamp */
	/* none later: not while the position holds, and not when the pulses resume */
	fake_runUntil(h, limit + fake_msToCycles(1000.0), fake_msToCycles(1.0));
	ASSERT_EQ(fake_count(h, FAKE_NOTE_OFF), 6);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + fake_msToCycles(1.0), per, 2, fake_msToCycles(0.1));
	fake_runUntil(h, h->now + fake_msToCycles(10.0), fake_msToCycles(0.1));	/* past 580 */
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 7);	/* step 6 at the second pulse */
	ASSERT_EQ(fake_count(h, FAKE_NOTE_OFF), 7);	/* one note-off per trigger */
}

TEST(T18_dropout_sends_the_note_off_of_the_hit_it_plays)
{
	/* N17 when no seqc_process() comes between the clamp and the dropout time:
	 * the dropout first plays step 5 at the clamp, then sends the pending
	 * note-offs, its own included */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_start();
	const double per = fake_stepCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	for (int k = 0; k < 5; k++)
		fake_sync(h, SEQC_EV_PULSE, t0 + (seqc_time_t)(k * per + 0.5));
	const seqc_time_t limit = t0 + (seqc_time_t)(4.0 * per + 0.5) + fake_msToCycles(SEQC_DROPOUT_MS);
	fake_process(h, limit + fake_msToCycles(1.0));
	ASSERT_EQ(stats().dropouts, 1);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), 6);
	ASSERT_EQ(fake_count(h, FAKE_NOTE_OFF), 6);
}

/* N21: count clocks at bpm, the true time of clock k at 10 ms + k periods,
 * stamped the way a USB host delivers them, into stamps[]:
 * - packet > 0: transfers of packet clocks, each stamped at its last clock;
 * - buffer > 0: one transfer per audio buffer of buffer cycles (its phase 0.31
 *   of a buffer), stamped at the end of the buffer the clock falls in. */
static void usbStamps(double bpm, size_t count, unsigned packet, double buffer)
{
	const double per = fake_clockCycles(bpm), t0 = (double)fake_msToCycles(10.0);
	for (size_t k = 0; k < count; k++)
	{
		double t = t0 + (double)k * per;
		if (packet > 0)
		{
			size_t last = k / packet * packet + packet - 1;
			if (last >= count)
				last = count - 1;
			t = t0 + (double)last * per;
		}
		else if (buffer > 0.0)
			t = 0.31 * buffer + ceil((t - 0.31 * buffer) / buffer) * buffer;
		stamps[k] = (seqc_time_t)(t + 0.5);
	}
}

/* deliver the clocks of stamps[0..count) one transfer at a time, the way
 * seq_tick() does: the clocks with one stamp, then seqc_process() at that
 * stamp. Returns the largest |error| of seqc_currentBpm() against bpm, in %,
 * after the transfers that end after clock first. */
static double deliverTransfers(fake_host_t *h, double bpm, size_t count, size_t first)
{
	double worst = 0.0;
	size_t i = 0;
	while (i < count)
	{
		const seqc_time_t t = stamps[i];
		while (i < count && stamps[i] == t)
			fake_sync(h, SEQC_EV_CLOCK, stamps[i++]);
		fake_process(h, t);
		const double e = fabs((double)seqc_currentBpm() - bpm) / bpm * 100.0;
		if (i > first && e > worst)
			worst = e;
	}
	return worst;
}

TEST(T16_packed_clocks_give_the_clock_tempo)
{
	/* N21: transfers of 1 to 6 clocks, each stamped once, at its last clock.
	 * Each transfer is one point of the fit, so the estimate is the clock
	 * tempo, not the transfer rate: exact from the second transfer on, nothing
	 * left out, and every step played. */
	for (unsigned n = 1; n <= 6; n++)
	{
		fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
		seqc_start();
		const size_t count = 24 * 8 + 1;
		usbStamps(97.0, count, n, 0.0);
		ASSERT_TRUE(deliverTransfers(h, 97.0, count, n) < 1e-4);
		ASSERT_EQ(stats().tempoRestarts, 0);
		ASSERT_EQ(stats().intervalsExcluded, 0);
		ASSERT_EQ(fake_triggers(h, 0, trig, 64), (count - 1) / SEQC_MIDI_CLOCKS_PER_STEP + 1);
	}
}

TEST(T16_packed_audio_buffers_give_the_clock_tempo)
{
	/* N21: one transfer per audio buffer of 256, 512 or 1024 samples at
	 * 44.1 kHz (5.8, 11.6 and 23.2 ms). The clocks of a buffer share the stamp
	 * of its end, so the stamps move in steps of up to one buffer: at 101 and
	 * 116 BPM a step of a 1024-sample buffer is about a clock, and only the
	 * band of the fit, which grows with the steps, keeps them in. Within 0.5%
	 * from 2 beats on, with no restart. */
	static const double samples[] = { 256.0, 512.0, 1024.0 };
	static const double bpms[] = { 90.0, 101.0, 116.0, 120.0, 128.0, 140.0 };
	for (size_t b = 0; b < sizeof samples / sizeof samples[0]; b++)
		for (size_t t = 0; t < sizeof bpms / sizeof bpms[0]; t++)
		{
			fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
			seqc_start();
			const size_t count = 24 * 16 + 1;
			usbStamps(bpms[t], count, 0, samples[b] / 44100.0 * SEQC_CPU_HZ);
			ASSERT_TRUE(deliverTransfers(h, bpms[t], count, 2 * 24) < 0.5);
			ASSERT_EQ(stats().tempoRestarts, 0);
			ASSERT_EQ(fake_triggers(h, 0, trig, 128), (count - 1) / SEQC_MIDI_CLOCKS_PER_STEP + 1);
		}
}

TEST(T16_packed_transfer_is_evaluated_when_complete)
{
	/* N21: the clocks of one transfer reach the core one by one, with one
	 * stamp. The transfer is evaluated once it is complete: at the next
	 * seqc_process(), or at a clock with a later stamp. Until then the estimate
	 * is the one before it. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const double per = fake_clockCycles(97.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	for (int k = 0; k < 3; k++)
		fake_sync(h, SEQC_EV_CLOCK, t0);	/* the first point */
	fake_process(h, t0);
	ASSERT_NEAR(seqc_currentBpm(), SEQC_DEFAULT_BPM, 1e-3);
	const seqc_time_t t1 = t0 + (seqc_time_t)(3.0 * per + 0.5);
	for (int k = 0; k < 3; k++)
		fake_sync(h, SEQC_EV_CLOCK, t1);
	ASSERT_NEAR(seqc_currentBpm(), SEQC_DEFAULT_BPM, 1e-3);	/* still collected */
	fake_process(h, t1);
	ASSERT_NEAR(seqc_currentBpm(), 97.0, 1e-3);		/* 3 clocks in 3 periods */
	/* 3 clocks at 101 BPM, completed by the next clock, without seqc_process() */
	const seqc_time_t t2 = t1 + (seqc_time_t)(3.0 * fake_clockCycles(101.0) + 0.5);
	for (int k = 0; k < 3; k++)
		fake_sync(h, SEQC_EV_CLOCK, t2);
	ASSERT_NEAR(seqc_currentBpm(), 97.0, 1e-3);
	fake_sync(h, SEQC_EV_CLOCK, t2 + (seqc_time_t)(fake_clockCycles(101.0) + 0.5));
	ASSERT_TRUE(seqc_currentBpm() > 98.0 && seqc_currentBpm() < 101.0);
	ASSERT_EQ(stats().intervalsExcluded, 0);
	ASSERT_EQ(stats().tempoRestarts, 0);
}

TEST(T16_packed_switch_drops_the_transfer)
{
	/* N21: a transfer still being collected when the sync mode changes belongs
	 * to the old mode: the switch clears the estimate and drops it, so the jack
	 * measures its own pulses alone */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	fake_sync(h, SEQC_EV_CLOCK, t0);
	fake_sync(h, SEQC_EV_CLOCK, t0);		/* a transfer of 2, not evaluated yet */
	seqc_setSyncMode(SEQC_SYNC_ANALOG);
	const double per = fake_stepCycles(100.0);
	fake_runClocks(h, SEQC_EV_PULSE, t0 + fake_msToCycles(50.0), per, 2, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-3);	/* from the two pulses */
	ASSERT_EQ(stats().intervalsExcluded, 0);
}

TEST(T16_packed_jump_restarts_from_the_outliers)
{
	/* N16 with transfers of 3 clocks: 90 to 200 BPM, the change in the middle
	 * of a clock interval, so the interval into the first transfer at the new
	 * tempo has some of each. That transfer is the first outlier. After
	 * SEQC_TEMPO_RESTART of them the fit restarts from them alone, as after a
	 * dropout (N21), without the interval into the first: exactly 200 at once. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double slow = fake_clockCycles(90.0), fast = fake_clockCycles(200.0);
	const size_t change = 24 * 4;		/* the interval from clock 96 to 97 */
	const size_t count = change + 3 * (SEQC_TEMPO_RESTART + 8);
	double t = (double)fake_msToCycles(10.0);
	for (size_t k = 0; k < count; k++)
	{
		stamps[k] = (seqc_time_t)(t + 0.5);	/* the true time, for now */
		t += k < change ? slow : k == change ? 0.5 * (slow + fast) : fast;
	}
	for (size_t k = 0; k < count; k++)
		stamps[k] = stamps[k / 3 * 3 + 2];	/* transfers of 3, at their last clock */
	const size_t before = change / 3 * 3 + 3 * SEQC_TEMPO_RESTART - 3;	/* up to the 3rd outlier */
	deliverTransfers(h, 90.0, before, 0);
	ASSERT_NEAR(seqc_currentBpm(), 90.0, 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART - 1);
	ASSERT_EQ(stats().tempoRestarts, 0);
	for (size_t k = 0; k < 3; k++)
		fake_sync(h, SEQC_EV_CLOCK, stamps[before + k]);
	fake_process(h, stamps[before]);	/* the 4th outlier */
	ASSERT_NEAR(seqc_currentBpm(), 200.0, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 1);
	/* the rest of the run: deliverTransfers() starts at index 0, so move them */
	const size_t rest = count - before - 3;
	for (size_t k = 0; k < rest; k++)
		stamps[k] = stamps[before + 3 + k];
	ASSERT_TRUE(deliverTransfers(h, 200.0, rest, 0) < 1e-4);
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART - 1);
	ASSERT_EQ(fake_triggers(h, 0, trig, 128), (count - 1) / SEQC_MIDI_CLOCKS_PER_STEP + 1);
}

TEST(T16_packed_step_restarts_the_fit)
{
	/* N21: 120 to 100 BPM, in transfers of 1 and of 3 clocks. The intervals
	 * are 20% longer, inside the interval band of N16, but each clock is later
	 * than the fitted line by 20% more, so a few clocks after the step the
	 * points leave the band of the fit (half a clock), and SEQC_TEMPO_RESTART
	 * in a row restart it: exactly 100 within 12 clocks. The mean of 24
	 * intervals was still between the two tempos there. */
	for (unsigned n = 1; n <= 3; n += 2)
	{
		fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
		size_t count = 0;
		double t = (double)fake_msToCycles(10.0);
		for (size_t k = 0; k < 30 + 12; k++)
		{
			stamps[count++] = (seqc_time_t)(t + 0.5);
			t += k < 29 ? fake_clockCycles(120.0) : fake_clockCycles(100.0);
		}
		for (size_t k = 0; k < count; k++)
			stamps[k] = stamps[k / n * n + n - 1 < count ? k / n * n + n - 1 : count - 1];
		deliverTransfers(h, 100.0, count, 0);
		ASSERT_NEAR(seqc_currentBpm(), 100.0, 5e-3);
		ASSERT_EQ(stats().tempoRestarts, 1);
		ASSERT_EQ(stats().intervalsExcluded, SEQC_TEMPO_RESTART - 1);
	}
}

TEST(T16_packed_step_amplitude_decays)
{
	/* N21: the step amplitude of the stamps is a decaying peak. In transfers of
	 * 2 clocks at 120 BPM, the transfer of clocks 30 and 31 is 0.4 of a clock
	 * late, inside the band, and the next is back on the line: a step of 0.4
	 * clocks, which widens the switch between the windows. 8 beats later it
	 * has decayed, and a 2% tempo step is followed through the short window:
	 * within 0.5% 24 clocks after the step. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	const double per = fake_clockCycles(120.0), per2 = fake_clockCycles(122.4);
	const size_t step = 32 + 8 * 24, count = step + 24;
	double t = (double)fake_msToCycles(10.0);
	for (size_t k = 0; k < count; k++)
	{
		stamps[k] = (seqc_time_t)(t + (k == 31 ? 0.4 * per : 0.0) + 0.5);
		t += k + 1 < step ? per : per2;
	}
	for (size_t k = 0; k < count; k++)
		stamps[k] = stamps[k | 1u];		/* transfers of 2, at their last clock */
	deliverTransfers(h, 122.4, count, 0);
	ASSERT_NEAR(seqc_currentBpm(), 122.4, 0.005 * 122.4);
	ASSERT_EQ(stats().intervalsExcluded, 0);
	ASSERT_EQ(stats().tempoRestarts, 0);
}

TEST(T16_packed_glitch_leaves_the_band_as_it_was)
{
	/* a clock 0.8 of a clock late, back on the line at the next: a glitch, not
	 * a step of the stamps (N21). The band stays at SEQC_TEMPO_OUTLIER_PCT, so
	 * the same glitch 12 clocks later is left out as well: one point each. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_start();
	const double per = fake_clockCycles(120.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	const size_t count = 80;
	for (size_t i = 0; i < count; i++)
		stamps[i] = t0 + (seqc_time_t)((double)i * per + (i == 40 || i == 52 ? 0.8 * per : 0.0));
	deliver(h, SEQC_EV_CLOCK, stamps, count, fake_msToCycles(0.1));
	ASSERT_EQ(stats().intervalsExcluded, 2);
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
}

TEST(T16_packed_fit_is_exact_after_hours)
{
	/* N21: the sums count from the oldest point of the window, so transfers 22
	 * hours after boot (stamps near 2^44 cycles) give the same estimate as at
	 * boot, and an hour of jittered transfers of 3 clocks leaves no rounding
	 * behind */
	float bpm[2];
	for (int late = 0; late < 2; late++)
	{
		fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
		const double per = fake_clockCycles(123.4);
		const double t0 = late ? 17592186044416.0 : (double)fake_msToCycles(10.0);	/* 2^44 */
		lcg = 777;
		const uint32_t clocks = 3u * 24u * 2400u;	/* about an hour */
		for (uint32_t k = 2; k < clocks; k += 3)
		{
			const seqc_time_t t = (seqc_time_t)(t0 + (double)k * per + jitterMs() * fake_msToCycles(0.3));
			for (int c = 0; c < 3; c++)
				fake_sync(h, SEQC_EV_CLOCK, t);
			fake_process(h, t);
		}
		/* then a long window of exact transfers: the estimate is exact again */
		for (uint32_t k = clocks + 2; k < clocks + 2u * SEQC_TEMPO_LONG_WINDOW; k += 3)
		{
			const seqc_time_t t = (seqc_time_t)(t0 + (double)k * per + 0.5);
			for (int c = 0; c < 3; c++)
				fake_sync(h, SEQC_EV_CLOCK, t);
			fake_process(h, t);
		}
		bpm[late] = seqc_currentBpm();
		ASSERT_NEAR(bpm[late], 123.4, 1e-4);
		ASSERT_EQ(stats().tempoRestarts, 0);
		ASSERT_EQ(stats().dropouts, 0);
	}
	ASSERT_NEAR(bpm[0], bpm[1], 1e-5);
}

TEST(T16_packed_fit_window_fits_in_32_bit_stamps)
{
	/* N21: the ring keeps 32 bits of each stamp, so the long window spans at
	 * most 2^32 cycles: at a time base of 1 GHz, 4.3 s. A clock at 30 BPM
	 * (83 ms), in transfers of 2, fills 96 clocks in 8 s; the window keeps the
	 * newest 4.3 s of them, and the fit stays exact. */
	fake_host_t *h = fake_init();
	const double hz = 1e9;
	seqc_init(&h->host, (uint32_t)hz);
	h->logMask = FAKE_LOG_NO_CLOCKS;
	for (uint8_t s = 0; s < 16; s++)
		fake_setStep(h, 0, 0, s, true);
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	fake_process(h, 1000000);
	seqc_start();
	const double per = hz * 60.0 / (30.0 * 24.0);
	const uint32_t count = 300;
	for (uint32_t k = 1; k < count; k += 2)
	{
		const seqc_time_t t = (seqc_time_t)(1e7 + (double)k * per + 0.5);
		fake_sync(h, SEQC_EV_CLOCK, t);
		fake_sync(h, SEQC_EV_CLOCK, t);
		fake_process(h, t + 1000);
		if (k > 24)
			ASSERT_NEAR(seqc_currentBpm(), 30.0, 1e-3);
	}
	ASSERT_EQ(stats().dropouts, 0);
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_EQ(stats().intervalsExcluded, 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), (count - 1) / SEQC_MIDI_CLOCKS_PER_STEP + 1);
}

TEST(T16_packed_set_tempo_keeps_a_measured_estimate)
{
	/* seqc_setTempo() in MIDI sync, with transfers of 3 clocks: it sets the
	 * first estimate while the fit has no slope; a measured estimate stays;
	 * after a dropout, which clears the fit, it is the first estimate again */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_MIDI);
	seqc_setTempo(140.0f);
	ASSERT_NEAR(seqc_currentBpm(), 140.0, 1e-3);
	const size_t count = 30;
	usbStamps(100.0, count, 3, 0.0);
	deliverTransfers(h, 100.0, count, 0);
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-3);
	seqc_setTempo(150.0f);
	ASSERT_NEAR(seqc_currentBpm(), 100.0, 1e-3);
	fake_runClocks(h, SEQC_EV_CLOCK, h->now + fake_msToCycles(800.0), fake_clockCycles(100.0), 1,
			fake_msToCycles(0.1));
	ASSERT_EQ(stats().dropouts, 1);
	seqc_setTempo(150.0f);
	ASSERT_NEAR(seqc_currentBpm(), 150.0, 1e-3);
}

/* the jack at ppq, 120 BPM: pulses 0..count-1 of the master, with pulse at
 * (and pulse again, if not 0) lost, or (extra) followed by a second pulse 5% of
 * a pulse after it. Returns the largest |error| of the estimate in % from the
 * pulse after at on. */
static double jackCountError(uint8_t ppq, uint32_t count, uint32_t at, uint32_t again, bool extra)
{
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_setAnalogPpq(ppq);
	seqc_start();
	const double per = ppq == 24 ? fake_clockCycles(120.0) : fake_stepCycles(120.0);
	double worst = 0.0;
	for (uint32_t c = 0; c < count; c++)
	{
		const seqc_time_t t = fake_msToCycles(10.0) + (seqc_time_t)((double)c * per + 0.5);
		const bool event = c == at || (again != 0 && c == again);
		if (event && !extra)
			continue;
		fake_sync(h, SEQC_EV_PULSE, t);
		fake_process(h, t);
		if (event)
		{
			fake_sync(h, SEQC_EV_PULSE, t + (seqc_time_t)(0.05 * per));
			fake_process(h, t + (seqc_time_t)(0.05 * per));
		}
		const double e = fabs((double)seqc_currentBpm() - 120.0) / 120.0 * 100.0;
		if (c > at && e > worst)
			worst = e;
	}
	return worst;
}

TEST(T18_lost_pulse_keeps_the_tempo)
{
	/* jack stamps are exact, so an interval off the slope is a lost or an extra
	 * pulse: it is left out, and the fit goes on from the pulse after it as if
	 * the interval had had the slope (N9, N21). A lost pulse leaves the
	 * estimate alone, and is no step of the stamps that would let a second one
	 * in. */
	ASSERT_TRUE(jackCountError(4, 24, 12, 0, false) < 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, 1);
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_TRUE(jackCountError(24, 24 * 8, 24 * 4, 24 * 4 + 12, false) < 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, 2);
	ASSERT_EQ(stats().tempoRestarts, 0);
}

TEST(T18_double_trigger_is_left_out)
{
	/* a second pulse 5% after a real one: its short interval is left out, and
	 * the next, 5% short, is used: the estimate moves by about 1.5% for the 4
	 * pulses of the window at 4 PPQ, then is exact again */
	ASSERT_TRUE(jackCountError(4, 24, 12, 0, true) < 2.0);
	ASSERT_EQ(stats().tempoRestarts, 0);
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
}

TEST(T18_alternating_outliers_do_not_restart)
{
	/* the jack at 4 PPQ, 120 BPM: 4 intervals alternately 60% longer and 60%
	 * shorter are outliers on both sides, each left out: no jump (N16). Then
	 * SEQC_TEMPO_RESTART_4PPQ longer ones in a row are one: the fit restarts
	 * from those pulses alone, at the interval between them. */
	fake_host_t *h = slaveWithGrid(SEQC_SYNC_ANALOG);
	seqc_start();
	const double per = fake_stepCycles(120.0);
	size_t n = appendClocks(0, per, SEQC_TEMPO_WINDOW_4PPQ + 1);
	for (int k = 0; k < 4; k++)
		n = appendClocks(n, per * (k % 2 == 0 ? 1.6 : 0.4), 1);
	deliver(h, SEQC_EV_PULSE, stamps, n, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0, 1e-3);
	ASSERT_EQ(stats().intervalsExcluded, 4);
	ASSERT_EQ(stats().tempoRestarts, 0);
	const size_t before = n;
	n = appendClocks(n, 1.7 * per, 1);
	n = appendClocks(n, 1.8 * per, 1);
	deliver(h, SEQC_EV_PULSE, stamps + before, n - before, fake_msToCycles(0.1));
	ASSERT_NEAR(seqc_currentBpm(), 120.0 / 1.8, 1e-3);
	ASSERT_EQ(stats().tempoRestarts, 1);
	ASSERT_EQ(stats().intervalsExcluded, 5);
	ASSERT_EQ(fake_triggers(h, 0, trig, 64), n);	/* one step per pulse */
}

TEST_MAIN()
