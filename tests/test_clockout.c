/*
 * test_clockout.c
 *
 * Clock outputs (spec 5 "Outputs", N8):
 * - T5: the MIDI clock out runs on the free-running timeline, also while
 *   stopped, every 16 ticks, straight whatever the shuffle; a start re-phases it
 *   to step 0; the tempo steps run on the same timeline;
 * - T6: while running, a clock out pulse comes every 4 ticks with its pulse in
 *   the step, and each output PPQ prescaler of fw 1.7 divides them evenly.
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

/* busy pattern: ratchets and shifts on every track, so the queue has work */
static void setUpBusy(fake_host_t *h, uint8_t shuffle)
{
	h->shuffle = shuffle;
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		for (uint8_t s = t % 3; s < 16; s += 3)
		{
			fake_setStep(h, 0, t, s, true);
			fake_setTiming(h, 0, t, s, (uint8_t)(s % 6), (int8_t)((s * 5 + t) % 15 - 7));
		}
}

static void checkClockSpacing(uint8_t shuffle)
{
	fake_host_t *h = fake_init();
	setUpBusy(h, shuffle);
	fake_process(h, 0);				/* the stopped timeline's clock at 0 */
	fake_clearLog(h);
	seqc_start();
	fake_process(h, 0);
	const seqc_time_t interval = fake_msToCycles(0.02);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 32), interval);
	/* clocks at ticks 0, 16, 32, ... while running */
	size_t pos = 0;
	const fake_rec_t *r;
	const fake_rec_t *prev = NULL;
	int k = 0;
	while ((r = fake_next(h, FAKE_MIDI_CLOCK, &pos)) != NULL)
	{
		ASSERT_NEAR(r->tick, 16.0 * k, 1e-9);
		if (prev != NULL)
		{
			/* 16 ticks at 120 BPM are 20.833 ms, quantized to the calls */
			const double dt = (double)(r->time - prev->time);
			ASSERT_TRUE(dt > fake_clockCycles(120.0) - interval - 1.0);
			ASSERT_TRUE(dt < fake_clockCycles(120.0) + interval + 1.0);
		}
		prev = r;
		k++;
	}
	ASSERT_EQ(k, 32 * 6 + 1);
	ASSERT_TRUE(fake_count(h, FAKE_TRIGGER) > 100);
	ASSERT_EQ(h->errors, 0);
}

TEST(T5_midi_clock_spacing_shuffle_0) { checkClockSpacing(0); }
TEST(T5_midi_clock_spacing_shuffle_64) { checkClockSpacing(64); }
TEST(T5_midi_clock_spacing_shuffle_127) { checkClockSpacing(127); }

TEST(T5_midi_clock_runs_while_stopped)
{
	fake_host_t *h = fake_init();
	fake_process(h, 0);				/* the timeline starts here */
	fake_runUntil(h, fake_msToCycles(1000.0), fake_msToCycles(0.05));
	/* 120 BPM: a clock every 20.833 ms and a tempo step every 125 ms, from 0 */
	ASSERT_EQ(fake_count(h, FAKE_MIDI_CLOCK), 49);
	ASSERT_EQ(fake_count(h, FAKE_TEMPO_STEP), 9);
	ASSERT_EQ(fake_count(h, FAKE_CLOCK_PULSE), 0);	/* only while running */
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), 0);
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_MIDI_CLOCK, &pos);
	ASSERT_EQ(r->time, 0);
	r = fake_next(h, FAKE_MIDI_CLOCK, &pos);
	ASSERT_NEAR((double)r->time, fake_clockCycles(120.0), fake_msToCycles(0.05) + 1.0);
	ASSERT_EQ(r->tick, 0.0);			/* the transport position is not moved */
}

TEST(T5_start_rephases_the_clock)
{
	fake_host_t *h = fake_init();
	fake_setStep(h, 0, 0, 0, true);
	fake_process(h, 0);
	fake_runUntil(h, fake_msToCycles(30.0), fake_msToCycles(0.1));	/* clocks at 0 and 20.8 ms */
	ASSERT_EQ(fake_count(h, FAKE_MIDI_CLOCK), 2);
	fake_clearLog(h);
	seqc_start();
	const seqc_time_t t = fake_msToCycles(30.1);
	fake_process(h, t);
	/* START, then at tick 0: tempo step, pattern end, boundary, trigger, pulse, MIDI clock */
	static const fake_kind_t order[] = {
		FAKE_TRANSPORT, FAKE_TEMPO_STEP, FAKE_PATTERN_END, FAKE_STEP_BOUNDARY,
		FAKE_TRIGGER, FAKE_CLOCK_PULSE, FAKE_MIDI_CLOCK,
	};
	ASSERT_EQ(h->logCount, sizeof order / sizeof order[0]);
	for (size_t i = 0; i < h->logCount; i++)
	{
		ASSERT_EQ(h->log[i].kind, order[i]);
		ASSERT_NEAR(h->log[i].tick, 0.0, 1e-9);
	}
	ASSERT_EQ(h->log[0].value, SEQC_TRANSPORT_START);
	ASSERT_EQ(h->log[6].time, t);
	/* the next clock is 16 ticks after the start, not on the old phase */
	fake_runUntil(h, t + fake_msToCycles(25.0), fake_msToCycles(0.1));
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_MIDI_CLOCK, &pos);
	r = fake_next(h, FAKE_MIDI_CLOCK, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_NEAR(r->tick, 16.0, 1e-9);
	ASSERT_NEAR((double)(r->time - t), fake_clockCycles(120.0), fake_msToCycles(0.1) + 1.0);
}

TEST(T5_tempo_steps_come_with_the_boundaries)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_process(h, 0);				/* the stopped timeline's tempo step at 0 */
	fake_clearLog(h);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 8) - 1, fake_msToCycles(0.1));
	ASSERT_EQ(fake_count(h, FAKE_TEMPO_STEP), 8);
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), 8);
	/* each tempo step right before its boundary */
	for (size_t i = 0; i + 1 < h->logCount; i++)
		if (h->log[i].kind == FAKE_TEMPO_STEP)
		{
			const fake_kind_t next = h->log[i + 1].kind;
			ASSERT_TRUE(next == FAKE_PATTERN_END || next == FAKE_STEP_BOUNDARY);
			ASSERT_NEAR(h->log[i].tick, h->log[i + 1].tick, 1e-9);
		}
	/* stopped, the tempo steps continue on the free-running timeline */
	seqc_stop();
	fake_clearLog(h);
	fake_runUntil(h, h->now + (seqc_time_t)(fake_stepCycles(120.0) * 4), fake_msToCycles(0.1));
	ASSERT_EQ(fake_count(h, FAKE_TEMPO_STEP), 4);
}

TEST(T5_tempo_change_keeps_the_clock_continuous)
{
	fake_host_t *h = fake_init();
	fake_process(h, 0);
	fake_clearLog(h);
	seqc_start();
	fake_process(h, 0);
	const seqc_time_t t1 = fake_msToCycles(100.0);	/* 76.8 ticks: clocks at 0..64 */
	fake_runUntil(h, t1, fake_msToCycles(0.01));
	ASSERT_EQ(fake_count(h, FAKE_MIDI_CLOCK), 5);
	seqc_setTempo(60.0f);
	fake_runUntil(h, t1 + fake_msToCycles(200.0), fake_msToCycles(0.01));
	size_t pos = 0;
	const fake_rec_t *r;
	int k = 0;
	seqc_time_t t80 = 0;
	while ((r = fake_next(h, FAKE_MIDI_CLOCK, &pos)) != NULL)
	{
		ASSERT_NEAR(r->tick, 16.0 * k, 1e-9);
		if (k == 5)
			t80 = r->time;
		k++;
	}
	/* tick 80 is 3.2 ticks after the change; at 60 BPM a tick is 2.604 ms */
	ASSERT_NEAR(fake_cyclesToMs(t80 - t1), 3.2 * 60000.0 / (60.0 * 384.0), 0.011);
	ASSERT_EQ(k, 5 + (int)((76.8 + 200.0 * 60.0 * 384.0 / 60000.0 - 80.0) / 16.0) + 1);
}

TEST(T6_pulses_every_4_ticks_while_running)
{
	fake_host_t *h = fake_init();
	setUpBusy(h, 127);				/* straight whatever the swing */
	fake_process(h, 0);
	fake_runUntil(h, fake_msToCycles(50.0), fake_msToCycles(0.1));
	ASSERT_EQ(fake_count(h, FAKE_CLOCK_PULSE), 0);	/* stopped */
	seqc_start();
	const seqc_time_t t0 = fake_msToCycles(50.0);
	fake_process(h, t0);
	fake_runUntil(h, t0 + (seqc_time_t)(fake_stepCycles(120.0) * 8) - 1, fake_msToCycles(0.01));
	size_t pos = 0;
	const fake_rec_t *r;
	int k = 0;
	while ((r = fake_next(h, FAKE_CLOCK_PULSE, &pos)) != NULL)
	{
		ASSERT_NEAR(r->tick, 4.0 * k, 1e-9);
		ASSERT_EQ(r->value, k % 24);
		const double late = (double)(r->time - t0) - 4.0 * k * fake_tickCycles(120.0);
		ASSERT_TRUE(late > -1.0 && late < fake_msToCycles(0.01) + 1.0);
		k++;
	}
	ASSERT_EQ(k, 8 * 24);
	seqc_stop();
	fake_clearLog(h);
	fake_runUntil(h, h->now + fake_msToCycles(100.0), fake_msToCycles(0.1));
	ASSERT_EQ(fake_count(h, FAKE_CLOCK_PULSE), 0);
}

TEST(T6_restart_begins_the_pulses_at_step_0)
{
	fake_host_t *h = fake_init();
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 10.5), fake_msToCycles(0.1));
	seqc_stop();
	seqc_rewind();					/* the stop button */
	fake_clearLog(h);
	fake_runUntil(h, h->now + fake_msToCycles(50.0), fake_msToCycles(0.1));
	ASSERT_EQ(fake_count(h, FAKE_CLOCK_PULSE), 0);	/* stopped */
	/* stopped, the MIDI clock runs on the free-running timeline */
	size_t pos = 0;
	const fake_rec_t *r;
	const fake_rec_t *prev = NULL;
	while ((r = fake_next(h, FAKE_MIDI_CLOCK, &pos)) != NULL)
	{
		if (prev != NULL)
			ASSERT_NEAR((double)(r->time - prev->time), fake_clockCycles(120.0),
				fake_msToCycles(0.1) + 1.0);
		prev = r;
	}
	ASSERT_TRUE(fake_count(h, FAKE_MIDI_CLOCK) >= 2);
	fake_clearLog(h);
	seqc_start();
	const seqc_time_t t0 = h->now + fake_msToCycles(0.1);
	fake_process(h, t0);
	fake_runUntil(h, t0 + (seqc_time_t)(fake_stepCycles(120.0) * 2) - 1, fake_msToCycles(0.01));
	/* the pulses start again at step 0, not at the old position */
	pos = 0;
	int k = 0;
	while ((r = fake_next(h, FAKE_CLOCK_PULSE, &pos)) != NULL)
	{
		ASSERT_NEAR(r->tick, 4.0 * k, 1e-9);
		ASSERT_EQ(r->value, k % 24);
		k++;
	}
	ASSERT_EQ(k, 2 * 24);
	/* and the first MIDI clock after the restart is at step 0 */
	pos = 0;
	r = fake_next(h, FAKE_MIDI_CLOCK, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_NEAR(r->tick, 0.0, 1e-9);
	ASSERT_EQ(r->time, t0);
	ASSERT_EQ(h->errors, 0);
}

/* fw 1.7 trigger_clockTick() (TriggerOut.c): the counter runs mod 96 from
 * trigger_reset(1) at the start, and the jack is high on the pulses where it is a
 * multiple of the prescaler of the output PPQ setting */
static const struct { int ppq, prescaler; } outputs[] = {
	{ 2, 48 }, { 4, 24 }, { 8, 12 }, { 16, 6 }, { 24, 4 }, { 48, 2 },
};

TEST(T6_output_ppq_prescalers_divide_evenly)
{
	fake_host_t *h = fake_init();
	setUpBusy(h, 127);
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 16) - 1, fake_msToCycles(0.02));
	for (size_t o = 0; o < sizeof outputs / sizeof outputs[0]; o++)
	{
		int counter = 0;				/* trigger_reset(1) */
		int edges = 0;
		double lastEdge = -1.0;
		size_t pos = 0;
		const fake_rec_t *r;
		while ((r = fake_next(h, FAKE_CLOCK_PULSE, &pos)) != NULL)
		{
			if (counter % outputs[o].prescaler == 0)
			{
				if (lastEdge >= 0.0)
					ASSERT_NEAR(r->tick - lastEdge, 4.0 * outputs[o].prescaler, 1e-9);
				lastEdge = r->tick;
				edges++;
			}
			counter = (counter + 1) % 96;
		}
		/* 16 steps are 4 quarter notes */
		ASSERT_EQ(edges, 4 * outputs[o].ppq);
	}
}

TEST_MAIN()
