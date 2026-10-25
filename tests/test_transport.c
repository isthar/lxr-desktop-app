/*
 * test_transport.c
 *
 * T17: the slave transport (spec 5, N4, N6): MIDI Start, Continue and Stop,
 * Song Position Pointer in pattern mode and in song mode (identical consecutive
 * entries, SONG_LOOP, past SONG_END, no reload inside the active pattern's run),
 * the clock jack run gate, and which sync mode accepts which event.
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

static seqc_stats_t stats(void)
{
	seqc_stats_t s;
	seqc_stats(&s);
	return s;
}

/* the logged transport() values from log entry from on; returns how many */
static size_t transports(const fake_host_t *h, size_t from, uint8_t *out, size_t max)
{
	size_t n = 0, pos = from;
	const fake_rec_t *r;
	while ((r = fake_next(h, FAKE_TRANSPORT, &pos)) != NULL)
	{
		if (n < max)
			out[n] = r->value;
		n++;
	}
	return n;
}

/* a MIDI slave at 120 BPM; clocks go through clocks() */
static fake_host_t *midiSlave(void)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	fake_process(h, fake_msToCycles(1.0));
	return h;
}

/* count MIDI clocks at 120 BPM, the first one a clock period after h->now */
static void clocks(fake_host_t *h, uint32_t count)
{
	const double per = fake_clockCycles(120.0);
	fake_runClocks(h, SEQC_EV_CLOCK, h->now + (seqc_time_t)per, per, count, fake_msToCycles(0.1));
}

static void sync(fake_host_t *h, seqc_sync_ev_t ev)
{
	fake_sync(h, ev, h->now + 100);
}

TEST(T17_midi_start_arms_and_the_next_clock_starts)
{
	fake_host_t *h = midiSlave();
	fake_setStep(h, 0, 0, 0, true);
	sync(h, SEQC_EV_START);
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	uint8_t tr[8];
	ASSERT_EQ(transports(h, 0, tr, 8), 1);		/* at the start already: no REWIND */
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_ARM);
	/* START comes before every other callback of the clock that starts */
	h->logMask = FAKE_LOG_ALL;
	const size_t from = h->logCount;
	clocks(h, 1);
	static const fake_kind_t order[] = {
		FAKE_TRANSPORT, FAKE_TEMPO_STEP, FAKE_PATTERN_END, FAKE_STEP_BOUNDARY,
		FAKE_TRIGGER, FAKE_CLOCK_PULSE,
	};
	ASSERT_EQ(h->logCount - from, sizeof order / sizeof order[0]);
	for (size_t i = 0; i < sizeof order / sizeof order[0]; i++)
		ASSERT_EQ(h->log[from + i].kind, order[i]);
	ASSERT_EQ(h->log[from].value, SEQC_TRANSPORT_START);
	h->logMask = FAKE_LOG_NO_CLOCKS;
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ(transports(h, 0, tr, 8), 2);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_START);
	ASSERT_EQ(seqc_absStep(), 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	clocks(h, 6 * 3);
	ASSERT_EQ(seqc_absStep(), 3);
}

TEST(T17_midi_start_while_running_restarts)
{
	fake_host_t *h = midiSlave();
	fake_setStep(h, 0, 0, 0, true);
	fake_setStep(h, 0, 0, 2, true);
	sync(h, SEQC_EV_START);
	clocks(h, 6 * 2 + 1);				/* steps 0..2 */
	ASSERT_EQ(seqc_absStep(), 2);
	const size_t from = h->logCount;
	sync(h, SEQC_EV_START);
	uint8_t tr[8];
	ASSERT_EQ(transports(h, from, tr, 8), 3);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_STOP);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_REWIND);
	ASSERT_EQ(tr[2], SEQC_TRANSPORT_ARM);
	ASSERT_EQ(fake_count(h, FAKE_NOTE_OFF), 2);	/* the pending one went out with the STOP */
	ASSERT_EQ(seqc_absStep(), -1);
	ASSERT_EQ_DBL(seqc_position(), -16.0);
	clocks(h, 1);
	ASSERT_EQ(seqc_absStep(), 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 3);
	ASSERT_EQ(trig[2]->ev.absStep, 0);
}

TEST(T17_stop_and_continue_resume_in_place)
{
	fake_host_t *h = midiSlave();
	fake_setLength(h, 0, 1, 12);
	fake_setStep(h, 0, 1, 5, true);
	fake_setTiming(h, 0, 1, 5, 5, 0);		/* x8: 480 + 12j */
	sync(h, SEQC_EV_START);
	clocks(h, 6 * 5 + 2);				/* tick 496: hits 480 and 492 played */
	ASSERT_EQ(fake_triggers(h, 1, trig, 16), 2);
	fake_process(h, h->now + fake_msToCycles(10.0));	/* the stop comes well after the clock */
	sync(h, SEQC_EV_STOP);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	const double held = seqc_position();
	ASSERT_TRUE(held > 503.0 && held < 504.0);	/* 7.68 ticks after the clock at 496 */
	clocks(h, 7);					/* the master's clock runs on: count 39 */
	ASSERT_EQ_DBL(seqc_position(), held);
	ASSERT_EQ(seqc_absStep(), 5);
	sync(h, SEQC_EV_CONTINUE);
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	const size_t from = h->logCount;
	clocks(h, 1);					/* off the step grid: it resumes anyway */
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	/* CONTINUE comes first, ahead of the ratchet hit at 504 that this clock plays */
	ASSERT_EQ(h->log[from].kind, FAKE_TRANSPORT);
	ASSERT_EQ(h->log[from].value, SEQC_TRANSPORT_CONTINUE);
	size_t pos = from;
	const fake_rec_t *hit = fake_next(h, FAKE_TRIGGER, &pos);
	ASSERT_TRUE(hit != NULL);
	ASSERT_EQ(hit->ev.tick, 504);
	uint8_t tr[8];
	ASSERT_EQ(transports(h, 0, tr, 8), 5);
	ASSERT_EQ(tr[4], SEQC_TRANSPORT_CONTINUE);
	ASSERT_NEAR(seqc_position(), 496.0 + 16, 0.1);	/* the last clock plus one clock, not held + 16 */
	clocks(h, 6 * 2 - 1);
	const size_t n = transports(h, 0, tr, 8);
	ASSERT_EQ(n, 5);				/* ARM, START, STOP, ARM, CONTINUE */
	ASSERT_EQ(tr[2], SEQC_TRANSPORT_STOP);
	ASSERT_EQ(tr[3], SEQC_TRANSPORT_ARM);
	ASSERT_EQ(tr[4], SEQC_TRANSPORT_CONTINUE);
	/* the rest of the ratchet, then steps 6 and 7 with the indices kept */
	ASSERT_EQ(fake_triggers(h, 1, trig, 16), 8);
	ASSERT_EQ(trig[7]->ev.tick, 480 + 84);
	ASSERT_EQ(seqc_absStep(), 7);
	ASSERT_EQ(seqc_trackStep(1), 7);
	ASSERT_EQ(fake_count(h, FAKE_PATTERN_END), 1);	/* only at step 0 */
}

TEST(T17_continue_at_the_start_is_a_start)
{
	fake_host_t *h = midiSlave();
	sync(h, SEQC_EV_CONTINUE);
	clocks(h, 1);
	uint8_t tr[8];
	ASSERT_EQ(transports(h, 0, tr, 8), 2);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_ARM);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_START);
	ASSERT_EQ(seqc_absStep(), 0);
	ASSERT_EQ(fake_count(h, FAKE_PATTERN_END), 1);
}

TEST(T17_events_that_do_nothing_are_counted)
{
	fake_host_t *h = midiSlave();
	sync(h, SEQC_EV_STOP);				/* stopped already */
	ASSERT_EQ(stats().syncIgnored, 1);
	sync(h, SEQC_EV_START);
	sync(h, SEQC_EV_CONTINUE);			/* armed */
	ASSERT_EQ(stats().syncIgnored, 2);
	clocks(h, 1);
	sync(h, SEQC_EV_CONTINUE);			/* running */
	ASSERT_EQ(stats().syncIgnored, 3);
	sync(h, SEQC_EV_RESET_ON);			/* the jack: analog sync only */
	sync(h, SEQC_EV_RESET_OFF);
	ASSERT_EQ(stats().syncIgnored, 5);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
}

TEST(T17_stop_and_start_while_armed)
{
	fake_host_t *h = midiSlave();
	fake_setStep(h, 0, 0, 0, true);
	sync(h, SEQC_EV_START);
	sync(h, SEQC_EV_START);				/* armed: stop, then arm again */
	uint8_t tr[8];
	ASSERT_EQ(transports(h, 0, tr, 8), 3);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_ARM);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_STOP);
	ASSERT_EQ(tr[2], SEQC_TRANSPORT_ARM);
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	sync(h, SEQC_EV_STOP);				/* armed: the master stopped */
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	ASSERT_EQ(stats().syncIgnored, 0);
	clocks(h, 12);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	ASSERT_EQ(fake_count(h, FAKE_TRIGGER), 0);
}

TEST(T17_transport_events_are_ignored_in_internal_sync)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_process(h, 0);
	sync(h, SEQC_EV_START);
	sync(h, SEQC_EV_CONTINUE);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	seqc_songPosition(8);				/* stopped: the LXR is the master */
	ASSERT_EQ(stats().syncIgnored, 3);
	ASSERT_EQ(seqc_absStep(), -1);
	ASSERT_EQ(fake_count(h, FAKE_SEEK), 0);
	seqc_start();
	fake_process(h, h->now + 10);
	sync(h, SEQC_EV_STOP);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ(stats().syncIgnored, 4);
	seqc_songPosition(8);
	ASSERT_EQ(stats().syncIgnored, 5);
	ASSERT_EQ(seqc_absStep(), 0);
	ASSERT_EQ(fake_count(h, FAKE_SEEK), 0);
}

TEST(T17_spp_in_pattern_mode)
{
	fake_host_t *h = midiSlave();
	h->shuffle = 127;
	fake_setLength(h, 0, 1, 12);
	fake_setLength(h, 0, 2, 7);
	fake_setStep(h, 0, 0, 37 % 16, true);
	fake_setStep(h, 0, 1, 37 % 12, true);
	fake_setStep(h, 0, 2, 37 % 7, true);
	seqc_songPosition(37);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	ASSERT_EQ(seqc_absStep(), 37);
	ASSERT_EQ(seqc_trackStep(0), 5);
	ASSERT_EQ(seqc_trackStep(1), 1);
	ASSERT_EQ(seqc_trackStep(2), 2);
	ASSERT_EQ_DBL(seqc_position(), 37.0 * 96 - 16);
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_SEEK, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->arg, 37);
	ASSERT_EQ(r->result, 37);
	sync(h, SEQC_EV_CONTINUE);
	clocks(h, 1);
	uint8_t tr[8];
	ASSERT_EQ(transports(h, 0, tr, 8), 2);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_CONTINUE);
	ASSERT_EQ(h->calls[FAKE_CLOCK_PULSE], 1);	/* from tick 37 * 96 on, not from 0 */
	clocks(h, 1);
	ASSERT_EQ(h->calls[FAKE_CLOCK_PULSE], 5);
	clocks(h, 5);
	/* the first boundary plays step 37 without a pattern end; 37 is odd: swung */
	ASSERT_EQ(fake_count(h, FAKE_PATTERN_END), 0);
	ASSERT_EQ(fake_triggers(h, -1, trig, 8), 3);
	for (int k = 0; k < 3; k++)
	{
		ASSERT_EQ(trig[k]->ev.absStep, 37);
		ASSERT_EQ(trig[k]->ev.tick, 37 * 96 + 64);
		ASSERT_EQ(trig[k]->ev.track, k);
	}
	ASSERT_EQ(seqc_absStep(), 38);
	ASSERT_EQ(seqc_trackStep(1), 2);
}

TEST(T17_spp_in_song_mode_with_identical_entries)
{
	fake_host_t *h = midiSlave();
	static const uint8_t song[] = { 1, 1, 2 };
	fake_setSong(h, song, 3);
	fake_setSongMode(h, true);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		fake_setLength(h, 2, t, 8);
	fake_setLength(h, 1, 1, 12);			/* polymeter: the run is not the entry */
	fake_setStep(h, 1, 0, 4, true);
	fake_setStep(h, 2, 0, 0, true);
	seqc_songPosition(20);				/* in the second A: its run began at 0 */
	ASSERT_EQ(h->activePattern, 1);
	ASSERT_EQ(h->songPos, 1);
	ASSERT_EQ(seqc_trackStep(0), 4);
	ASSERT_EQ(seqc_trackStep(1), 20 % 12);		/* the entry offset would give 4 */
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_SEEK, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->result, 20);
	sync(h, SEQC_EV_CONTINUE);
	clocks(h, 1 + 6 * 13);				/* steps 20..33 */
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 2);
	ASSERT_EQ(trig[0]->ev.absStep, 20);
	ASSERT_EQ(trig[0]->ev.pattern, 1);
	ASSERT_EQ(trig[1]->ev.absStep, 32);		/* the song moved on to B */
	ASSERT_EQ(trig[1]->ev.pattern, 2);
	ASSERT_EQ(h->songPos, 2);
	ASSERT_EQ(h->errors, 0);
}

TEST(T17_spp_at_a_pattern_start_calls_no_pattern_end)
{
	/* a DAW locates to a bar start: the first boundary plays the located entry's
	 * step 0, it does not end the pattern */
	fake_host_t *h = midiSlave();
	static const uint8_t song[] = { 1, 2 };
	fake_setSong(h, song, 2);
	fake_setSongMode(h, true);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		fake_setLength(h, 2, t, 8);
	fake_setStep(h, 2, 0, 0, true);
	seqc_songPosition(16);				/* entry 1, step 0 */
	ASSERT_EQ(h->activePattern, 2);
	ASSERT_EQ(h->songPos, 1);
	ASSERT_EQ(seqc_trackStep(0), 0);
	sync(h, SEQC_EV_CONTINUE);
	clocks(h, 1);
	ASSERT_EQ(fake_count(h, FAKE_PATTERN_END), 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 16);
	ASSERT_EQ(trig[0]->ev.pattern, 2);
	clocks(h, 6 * 8);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);		/* the song ended at 24 */
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
}

TEST(T17_spp_0_then_continue_is_a_continue)
{
	/* SPP 0 is a seek, not a rewind: the resume is a CONTINUE, and step 0 comes
	 * without a pattern end */
	fake_host_t *h = midiSlave();
	static const uint8_t song[] = { 1, 2 };
	fake_setSong(h, song, 2);
	fake_setSongMode(h, true);
	fake_setStep(h, 1, 0, 0, true);
	seqc_songPosition(0);
	ASSERT_EQ(h->activePattern, 1);
	ASSERT_EQ(h->songPos, 0);
	ASSERT_EQ(seqc_absStep(), 0);
	sync(h, SEQC_EV_CONTINUE);
	clocks(h, 1);
	uint8_t tr[8];
	ASSERT_EQ(transports(h, 0, tr, 8), 2);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_ARM);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_CONTINUE);
	ASSERT_EQ(h->songPos, 0);
	ASSERT_EQ(fake_count(h, FAKE_PATTERN_END), 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 0);
	ASSERT_EQ(trig[0]->ev.pattern, 1);
	clocks(h, 6 * 16 - 1);				/* steps 1..15 */
	ASSERT_EQ(fake_count(h, FAKE_PATTERN_END), 0);
	clocks(h, 1);					/* step 16 */
	ASSERT_EQ(fake_count(h, FAKE_PATTERN_END), 1);
	ASSERT_EQ(h->songPos, 1);
	ASSERT_EQ(h->errors, 0);
}

TEST(T17_spp_with_song_loop)
{
	fake_host_t *h = midiSlave();
	static const uint8_t song[] = { 1, 2, FAKE_SONG_LOOP };
	fake_setSong(h, song, 3);
	fake_setSongMode(h, true);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		fake_setLength(h, 2, t, 8);
	seqc_songPosition(50);				/* 24 steps per loop: entry 0, step 2 */
	ASSERT_EQ(h->activePattern, 1);
	ASSERT_EQ(h->songPos, 0);
	ASSERT_EQ(seqc_trackStep(0), 2);
	ASSERT_EQ(seqc_absStep(), 50);
}

TEST(T17_spp_past_the_song_end_stops_at_the_next_pattern_end)
{
	fake_host_t *h = midiSlave();
	static const uint8_t song[] = { 1, 2 };
	fake_setSong(h, song, 2);
	fake_setSongMode(h, true);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		fake_setLength(h, 2, t, 8);
	fake_setStep(h, 2, 0, 7, true);
	seqc_songPosition(100);				/* the last step of the last entry */
	ASSERT_EQ(h->activePattern, 2);
	ASSERT_EQ(seqc_trackStep(0), 7);
	sync(h, SEQC_EV_CONTINUE);
	clocks(h, 1 + 6 + 6);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 100);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);		/* the song ended at 101 */
	ASSERT_EQ(seqc_absStep(), -1);
}

TEST(T17_spp_within_the_active_pattern_does_not_reload_it)
{
	/* N6, song mode: a locate into the run of the pattern that plays already
	 * changes no pattern, as the song walk at a pattern end does not: no reload,
	 * so no unsaved edit is lost and the kit is not loaded again. A locate into
	 * another pattern's entry switches with the usual side effects. */
	fake_host_t *h = midiSlave();
	static const uint8_t song[] = { 0, 0, 2 };
	fake_setSong(h, song, 3);
	fake_setSongMode(h, true);
	const uint32_t changes = h->patternChanges;
	seqc_songPosition(20);				/* the second entry of pattern 0 */
	ASSERT_EQ(h->activePattern, 0);
	ASSERT_EQ(h->songPos, 1);
	ASSERT_EQ(h->patternChanges, changes);
	ASSERT_EQ(seqc_trackStep(0), 4);		/* its run began at 0 */
	seqc_songPosition(35);				/* pattern 2 */
	ASSERT_EQ(h->activePattern, 2);
	ASSERT_EQ(h->songPos, 2);
	ASSERT_EQ(h->patternChanges, changes + 1);
	seqc_songPosition(3);				/* back to pattern 0 */
	ASSERT_EQ(h->activePattern, 0);
	ASSERT_EQ(h->songPos, 0);
	ASSERT_EQ(h->patternChanges, changes + 2);
}

TEST(T17_spp_then_midi_start_plays_step_0)
{
	/* a DAW located, then started from the top: SPP, then Start */
	fake_host_t *h = midiSlave();
	fake_setStep(h, 0, 0, 0, true);
	seqc_songPosition(8);
	sync(h, SEQC_EV_START);
	clocks(h, 1);
	ASSERT_EQ(seqc_absStep(), 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 0);
	ASSERT_EQ(fake_count(h, FAKE_PATTERN_END), 1);
	clocks(h, 6);
	ASSERT_EQ(seqc_absStep(), 1);
	ASSERT_EQ(h->errors, 0);
}

TEST(T17_spp_then_rewind_plays_step_0)
{
	/* the local stop button after a locate, then a Continue */
	fake_host_t *h = midiSlave();
	fake_setStep(h, 0, 0, 0, true);
	seqc_songPosition(8);
	seqc_rewind();
	sync(h, SEQC_EV_CONTINUE);
	clocks(h, 1);
	uint8_t tr[8];
	ASSERT_EQ(transports(h, 0, tr, 8), 3);
	ASSERT_EQ(tr[2], SEQC_TRANSPORT_START);
	ASSERT_EQ(seqc_absStep(), 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 0);
	ASSERT_EQ(fake_count(h, FAKE_PATTERN_END), 1);
	clocks(h, 6);
	ASSERT_EQ(seqc_absStep(), 1);
	ASSERT_EQ(h->errors, 0);
}

TEST(T17_spp_while_running_is_ignored)
{
	fake_host_t *h = midiSlave();
	sync(h, SEQC_EV_START);
	clocks(h, 7);
	seqc_songPosition(16);
	ASSERT_EQ(stats().syncIgnored, 1);
	ASSERT_EQ(seqc_absStep(), 1);
}

TEST(T17_spp_while_armed_seeks)
{
	/* a Continue can be applied before the SPP parsed in the same seq_tick() */
	fake_host_t *h = midiSlave();
	fake_setStep(h, 0, 0, 8, true);
	sync(h, SEQC_EV_CONTINUE);
	seqc_songPosition(8);
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	clocks(h, 1);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 8);
}

TEST(T17_spp_clears_the_queue_and_restarts_the_count)
{
	fake_host_t *h = midiSlave();
	fake_setStep(h, 0, 1, 1, true);
	fake_setTiming(h, 0, 1, 1, 5, 7);		/* x8 from 180 to 264 */
	sync(h, SEQC_EV_START);
	clocks(h, 1 + 6 + 6);				/* tick 192: hits 180 and 192 */
	ASSERT_EQ(fake_triggers(h, 1, trig, 16), 2);
	sync(h, SEQC_EV_STOP);
	clocks(h, 2);					/* count 15 since the Start */
	const uint32_t tempoSteps = h->calls[FAKE_TEMPO_STEP];
	seqc_songPosition(4);
	sync(h, SEQC_EV_CONTINUE);
	clocks(h, 1);					/* the count restarted: this clock begins a step */
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], tempoSteps + 1);
	clocks(h, 6 * 4);
	ASSERT_EQ(fake_triggers(h, 1, trig, 16), 2);	/* the rest of the ratchet is gone */
	ASSERT_EQ(seqc_absStep(), 8);
}

TEST(T17_midi_start_in_analog_sync_starts_with_the_next_pulse)
{
	/* N13.10: at 24 PPQ the NEXT_CLOCK arm starts playback on the next pulse
	 * (fw 1.7 waited for its own 6-pulse grid), and the count reset puts the
	 * step grid in line with the Start: tempoStep() comes on that pulse and on
	 * every 6th after it */
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	seqc_setSyncMode(SEQC_SYNC_ANALOG);
	seqc_setAnalogPpq(24);
	fake_process(h, fake_msToCycles(1.0));
	const double per = fake_clockCycles(120.0);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 3, fake_msToCycles(0.1));
	const uint32_t t0 = h->calls[FAKE_TEMPO_STEP];
	sync(h, SEQC_EV_START);
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 1, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ(seqc_absStep(), 0);
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], t0 + 1);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 5, fake_msToCycles(0.1));
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], t0 + 1);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 1, fake_msToCycles(0.1));
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], t0 + 2);
}

TEST(T17_midi_start_resets_the_count_for_a_later_local_start)
{
	/* N4, N13.10: the count runs from the Start, so a local start after a
	 * Start and a Stop lands on the master's step grid */
	fake_host_t *h = midiSlave();
	clocks(h, 3);					/* count 3 before the Start */
	sync(h, SEQC_EV_START);
	clocks(h, 13);					/* count 13 since the Start */
	sync(h, SEQC_EV_STOP);
	seqc_rewind();
	seqc_start();
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	clocks(h, 5);					/* counts 13..17: off the grid */
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	clocks(h, 1);					/* count 18: begins a step */
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
}

TEST(T17_jack_run_gate)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_setStep(h, 0, 0, 0, true);
	seqc_setSyncMode(SEQC_SYNC_ANALOG);		/* 4 PPQ: one pulse per step */
	fake_process(h, fake_msToCycles(1.0));
	const double per = fake_stepCycles(120.0);
	sync(h, SEQC_EV_RESET_OFF);			/* released: like a local start */
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 3, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 2);
	sync(h, SEQC_EV_RESET_OFF);			/* running: does nothing */
	ASSERT_EQ(stats().syncIgnored, 1);
	const size_t from = h->logCount;
	sync(h, SEQC_EV_RESET_ON);			/* active: stop and rewind */
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	ASSERT_EQ(seqc_absStep(), -1);
	uint8_t tr[8];
	ASSERT_EQ(transports(h, from, tr, 8), 2);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_STOP);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_REWIND);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 2, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), -1);			/* held while the gate is active */
	sync(h, SEQC_EV_RESET_OFF);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 1, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 0);			/* the next pulse plays step 0 */
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 2);
	sync(h, SEQC_EV_START);				/* MIDI Start is accepted in analog sync */
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
}

/* jack pulses at 24 PPQ and 120 BPM */
static void pulses(fake_host_t *h, uint32_t count)
{
	const double per = fake_clockCycles(120.0);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, count, fake_msToCycles(0.1));
}

TEST(T17_jack_run_gate_at_24_ppq)
{
	/* as fw 1.7: the active gate resets the count, and the release waits for a
	 * pulse that begins a step */
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_setStep(h, 0, 0, 0, true);
	seqc_setSyncMode(SEQC_SYNC_ANALOG);
	seqc_setAnalogPpq(24);
	fake_process(h, fake_msToCycles(1.0));
	pulses(h, 3);
	sync(h, SEQC_EV_RESET_ON);
	pulses(h, 2);					/* count 2 since the reset */
	sync(h, SEQC_EV_RESET_OFF);
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	sync(h, SEQC_EV_RESET_OFF);			/* armed: does nothing */
	ASSERT_EQ(stats().syncIgnored, 1);
	pulses(h, 4);
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	ASSERT_EQ(fake_count(h, FAKE_TRIGGER), 0);
	pulses(h, 1);					/* count 6: it begins a step */
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ(seqc_absStep(), 0);
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	ASSERT_EQ(stats().syncIgnored, 1);
}

TEST(T17_jack_reset_after_a_midi_stop_rewinds)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	seqc_setSyncMode(SEQC_SYNC_ANALOG);		/* 4 PPQ */
	fake_process(h, fake_msToCycles(1.0));
	const double per = fake_stepCycles(120.0);
	sync(h, SEQC_EV_START);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 3, fake_msToCycles(0.1));
	sync(h, SEQC_EV_STOP);
	ASSERT_EQ(seqc_absStep(), 2);			/* kept for a Continue */
	const size_t from = h->logCount;
	sync(h, SEQC_EV_RESET_ON);
	ASSERT_EQ(seqc_absStep(), -1);
	uint8_t tr[8];
	ASSERT_EQ(transports(h, from, tr, 8), 1);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_REWIND);
	ASSERT_EQ(stats().syncIgnored, 0);
}

TEST(T17_midi_transport_and_spp_in_analog_sync_at_4_ppq)
{
	/* N4: MIDI Start, Continue and Stop in sync mode 1; SPP in both slave modes */
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	fake_setStep(h, 0, 0, 8, true);
	seqc_setSyncMode(SEQC_SYNC_ANALOG);		/* 4 PPQ: one pulse per step */
	fake_process(h, fake_msToCycles(1.0));
	const double per = fake_stepCycles(120.0);
	sync(h, SEQC_EV_START);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 3, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_absStep(), 2);
	sync(h, SEQC_EV_STOP);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	sync(h, SEQC_EV_CONTINUE);
	ASSERT_EQ(seqc_state(), SEQC_ARMED);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 1, fake_msToCycles(0.1));
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	ASSERT_EQ(seqc_absStep(), 3);
	sync(h, SEQC_EV_STOP);
	seqc_songPosition(8);
	ASSERT_EQ(seqc_absStep(), 8);
	ASSERT_EQ_DBL(seqc_position(), 8.0 * 96 - 96);	/* minus one clock: 96 ticks at 4 PPQ */
	sync(h, SEQC_EV_CONTINUE);
	fake_runClocks(h, SEQC_EV_PULSE, h->now + (seqc_time_t)per, per, 1, fake_msToCycles(0.1));
	ASSERT_EQ(fake_triggers(h, 0, trig, 8), 1);
	ASSERT_EQ(trig[0]->ev.absStep, 8);
	ASSERT_TRUE(seqc_position() < 8.0 * 96 + 2);	/* the pulse lands on the step */
	ASSERT_EQ(stats().syncIgnored, 0);
}

TEST_MAIN()
