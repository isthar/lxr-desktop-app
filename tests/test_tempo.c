/*
 * test_tempo.c
 *
 * TT: the per-track tempo of LICHOMAT v0.2 (plan V2, V3). A track's
 * TrackGroove.tempo sets its step length (x1/4 .. x4: 384 .. 24 ticks); a track at
 * x1 stays on the master grid exactly as in v0.1, a track at another tempo steps on
 * a grid of its own. TT1: the grids and the x1 path; TT2: ratchets, shift, swing and
 * rolls in the track's steps, leaving and rejoining the master grid; TT3: the master
 * bar ends the pattern, a pattern change restarts every track; TT4: the early hits
 * of the first step after a restart; TT5: a song position, and the recording phase,
 * skip-once and step serial in the track's own steps; TT6: a track that leaves the
 * master grid at the boundary that predicts a pattern change or a song end; TT7: a
 * tempo edit at the boundary that the restart prediction took for the last one.
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

/* ticks of a track step at the menu's tempo indices 0..10 (x1/4 .. x4) */
static const uint16_t stepTicks[11] = { 384, 288, 192, 144, 128, 96, 72, 64, 48, 32, 24 };

/* start at time 0 at 120 BPM, seqc_process() every 0.1 ms until ms */
static void play(fake_host_t *h, double ms)
{
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, fake_msToCycles(ms), fake_msToCycles(0.1));
}

/* the step hits (no rolls) of track in firing order: how many, up to max stored */
static size_t hits(const fake_host_t *h, int track, const fake_rec_t **out, size_t max)
{
	static const fake_rec_t *all[4096];
	const size_t n = fake_triggers(h, track, all, 4096);
	size_t k = 0;
	for (size_t i = 0; i < n && i < 4096; i++)
		if (!(all[i]->ev.flags & SEQC_EVF_ROLL))
		{
			if (k < max)
				out[k] = all[i];
			k++;
		}
	return k;
}

/* the first step hit of track from pattern, or NULL */
static const fake_rec_t *firstOf(const fake_host_t *h, int track, uint8_t pattern)
{
	const fake_rec_t *r[512];
	const size_t n = hits(h, track, r, 512);
	for (size_t i = 0; i < n && i < 512; i++)
		if (r[i]->ev.pattern == pattern)
			return r[i];
	return NULL;
}

static void allOn(fake_host_t *h, uint8_t pattern, uint8_t track)
{
	for (uint8_t s = 0; s < FAKE_NUM_STEPS; s++)
		fake_setStep(h, pattern, track, s, true);
}

/* stepBoundary() hook: pattern 1 is asked for at the absolute step changeAt */
static uint32_t changeAt;
static void askPattern1(fake_host_t *h, uint32_t absStep)
{
	if (absStep == changeAt)
		fake_setNextPattern(h, 1);
}

TEST(TT1_step_grid_of_every_tempo)
{
	for (uint8_t i = 0; i < 11; i++)
	{
		fake_host_t *h = fake_init();
		allOn(h, 0, 1);
		fake_setTempo(h, 0, 1, i);
		play(h, 3990.0);
		const fake_rec_t *r[8];
		ASSERT_TRUE(hits(h, 1, r, 8) >= 8);
		for (uint32_t k = 0; k < 8; k++)
		{
			ASSERT_EQ(r[k]->ev.tick, (seqc_tick_t)k * stepTicks[i]);
			ASSERT_EQ(r[k]->ev.step, k % 16);
			ASSERT_EQ(r[k]->ev.absStep, k);
			/* step k at k * L * 500 / 384 ms (x3/4: 0, 166.667, 333.333, 500 ...), in the
			 * 0.1 ms call at or after it */
			const double ms = k * stepTicks[i] * 500.0 / 384.0, at = fake_cyclesToMs(r[k]->time);
			ASSERT_TRUE(at > ms - 1e-6 && at < ms + 0.1 + 1e-6);
		}
		ASSERT_EQ(h->errors, 0);
	}
}

TEST(TT1_x1_tracks_do_not_see_a_tempo_track)
{
	static seqc_event_t first[64];
	size_t n0 = 0;
	for (int run = 0; run < 2; run++)
	{
		fake_host_t *h = fake_init();
		for (uint8_t s = 0; s < 16; s += 2)
			fake_setStep(h, 0, 0, s, true);
		fake_setTiming(h, 0, 0, 2, 3, -3);
		h->shuffle = 64;
		allOn(h, 0, 2);
		if (run == 1)
			fake_setTempo(h, 0, 2, 3);		/* x2/3 */
		play(h, 2100.0);
		const fake_rec_t *r[64];
		const size_t n = hits(h, 0, r, 64);
		ASSERT_TRUE(n > 8 && n <= 64);
		if (run == 0)
		{
			n0 = n;
			for (size_t i = 0; i < n; i++)
				first[i] = r[i]->ev;
			continue;
		}
		ASSERT_EQ(n, n0);
		for (size_t i = 0; i < n; i++)
			ASSERT_EQ(memcmp(&r[i]->ev, &first[i], sizeof first[i]), 0);
	}
}

TEST(TT1_defaults_are_the_v01_path)
{
	/* a busy pattern at x1, once with the defaults from groove() and once without the
	 * callback: the same log */
	static fake_rec_t first[4096];
	size_t n0 = 0;
	for (int run = 0; run < 2; run++)
	{
		fake_host_t *h = fake_init();
		if (run == 1)
		{
			h->host.groove = NULL;
			h->host.trackStep = NULL;
			seqc_init(&h->host, SEQC_CPU_HZ);
		}
		h->logMask = FAKE_LOG_NO_CLOCKS;
		h->shuffle = 90;
		for (uint8_t t = 0; t < FAKE_NUM_TRACKS; t++)
		{
			fake_setLength(h, 0, t, (uint8_t)(5 + 3 * t));
			for (uint8_t s = t; s < FAKE_NUM_STEPS; s += 3)
			{
				fake_setStep(h, 0, t, s, true);
				fake_setStepData(h, 0, t, s, (uint8_t)(60 + s), 60, (uint8_t)(s % 2 ? 50 : 100));
				fake_setTiming(h, 0, t, s, (uint8_t)(s % 6), (int8_t)(s % 15 - 7));
			}
		}
		play(h, 4000.0);
		ASSERT_TRUE(h->logCount < 4096);
		if (run == 0)
		{
			n0 = h->logCount;
			memcpy(first, h->log, n0 * sizeof first[0]);
			continue;
		}
		ASSERT_EQ(h->logCount, n0);
		for (size_t i = 0; i < n0; i++)
		{
			ASSERT_EQ(h->log[i].kind, first[i].kind);
			ASSERT_EQ(h->log[i].time, first[i].time);
			ASSERT_EQ(memcmp(&h->log[i].ev, &first[i].ev, sizeof first[i].ev), 0);
		}
	}
}

TEST(TT2_ratchet_and_shift_scale_with_the_step)
{
	fake_host_t *h = fake_init();
	fake_setTempo(h, 0, 1, 4);		/* x3/4: 128 ticks a step, 16-tick shift units */
	fake_setStep(h, 0, 1, 0, true);
	fake_setTiming(h, 0, 1, 0, 2, 0);	/* flam 2 is x3: 0, 42, 85 */
	fake_setStep(h, 0, 1, 1, true);
	fake_setTiming(h, 0, 1, 1, 0, 2);	/* 128 + 32 */
	fake_setStep(h, 0, 1, 3, true);
	fake_setTiming(h, 0, 1, 3, 0, -7);	/* 384 - 112: an early hit, generated at 256 */
	fake_setTempo(h, 0, 2, 10);		/* x4: 24 ticks a step */
	fake_setStep(h, 0, 2, 1, true);
	fake_setTiming(h, 0, 2, 1, 4, 0);	/* x6 over 24 ticks: 4 apart */
	play(h, 450.0);				/* 345 ticks: track 2 loops at 384 */
	static const seqc_tick_t want[5] = { 0, 42, 85, 160, 272 };
	const fake_rec_t *r[8];
	ASSERT_EQ(hits(h, 1, r, 8), 5);
	for (int i = 0; i < 5; i++)
		ASSERT_EQ(r[i]->ev.tick, want[i]);
	ASSERT_EQ(r[4]->ev.step, 3);
	ASSERT_EQ(hits(h, 2, r, 8), 6);
	for (int j = 0; j < 6; j++)
		ASSERT_EQ(r[j]->ev.tick, 24 + 4 * j);
	ASSERT_EQ(h->errors, 0);
}

TEST(TT2_swing_on_the_tracks_own_odd_steps)
{
	fake_host_t *h = fake_init();
	h->shuffle = 127;			/* 2/3 of a step: 64 ticks at x1, 32 at x2, 85 at x3/4 */
	allOn(h, 0, 0);
	allOn(h, 0, 1);
	fake_setTempo(h, 0, 1, 8);
	allOn(h, 0, 2);
	fake_setTempo(h, 0, 2, 4);
	play(h, 700.0);
	const fake_rec_t *r[16];
	ASSERT_TRUE(hits(h, 0, r, 16) >= 4);
	ASSERT_EQ(r[1]->ev.tick, 96 + 64);
	static const seqc_tick_t x2[6] = { 0, 48 + 32, 96, 144 + 32, 192, 240 + 32 };
	ASSERT_TRUE(hits(h, 1, r, 16) >= 6);
	for (int i = 0; i < 6; i++)
		ASSERT_EQ(r[i]->ev.tick, x2[i]);
	ASSERT_TRUE(hits(h, 2, r, 16) >= 3);
	ASSERT_EQ(r[1]->ev.tick, 128 + 85);
	ASSERT_EQ(r[2]->ev.tick, 256);
}

TEST(TT2_rolls_follow_the_tracks_steps)
{
	fake_host_t *h = fake_init();
	fake_setTempo(h, 0, 1, 8);		/* x2 */
	h->rollRate[1] = 2;			/* every second step of the track */
	play(h, 400.0);
	size_t pos = 0, n = 0;
	seqc_tick_t ticks[8];
	const fake_rec_t *e;
	while ((e = fake_next(h, FAKE_TRIGGER, &pos)) != NULL)
		if (e->ev.track == 1 && n < 8)
		{
			ASSERT_TRUE(e->ev.flags & SEQC_EVF_ROLL);
			ticks[n++] = e->ev.tick;
		}
	ASSERT_TRUE(n >= 3);
	ASSERT_EQ(ticks[0], 0);
	ASSERT_EQ(ticks[1], 96);
	ASSERT_EQ(ticks[2], 192);
}

static void toX34At2(fake_host_t *h, uint32_t absStep)
{
	if (absStep == 2)
		fake_setTempo(h, 0, 1, 4);
}
static void backToX1At448(fake_host_t *h, uint8_t track)
{
	if (track == 1 && seqc_position() == 448.0)
		fake_setTempo(h, 0, 1, 5);
}

TEST(TT2_tempo_change_leaves_and_rejoins_the_master_grid)
{
	fake_host_t *h = fake_init();
	allOn(h, 0, 1);
	h->onStepBoundary = toX34At2;
	h->onTrackStep = backToX1At448;
	play(h, 900.0);				/* 691 ticks */
	/* x1 at 0, 96; x3/4 from the master boundary at 192: 320, 448; x1 again from 448,
	 * that step ending at the next master boundary, 480: 576, 672 */
	static const seqc_tick_t want[8] = { 0, 96, 192, 320, 448, 480, 576, 672 };
	const fake_rec_t *r[16];
	ASSERT_EQ(hits(h, 1, r, 16), 8);
	for (uint32_t k = 0; k < 8; k++)
	{
		ASSERT_EQ(r[k]->ev.tick, want[k]);
		ASSERT_EQ(r[k]->ev.step, k);
		ASSERT_EQ(r[k]->ev.absStep, k);
	}
	ASSERT_EQ(fake_count(h, FAKE_TRACK_STEP), 2);	/* 320 and 448 */
	ASSERT_TRUE(seqc_trackOnMasterGrid(1));
	ASSERT_EQ(h->errors, 0);
}

TEST(TT3_the_master_bar_ends_the_pattern)
{
	/* the longest track at x1/2 (32 steps, a loop of 64 master steps) ends the pattern
	 * every 32 master steps: the pattern end counts master steps */
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 3, 32);
	fake_setTempo(h, 0, 3, 2);
	play(h, 9000.0);			/* boundary 72 at 9000 ms */
	int32_t ends[8];
	size_t n = 0, pos = 0;
	const fake_rec_t *e;
	while ((e = fake_next(h, FAKE_PATTERN_END, &pos)) != NULL)
		if (n < 8)
			ends[n++] = e->absStep;
	ASSERT_EQ(n, 3);
	ASSERT_EQ(ends[0], 0);
	ASSERT_EQ(ends[1], 32);
	ASSERT_EQ(ends[2], 64);
	ASSERT_EQ(seqc_masterTrack(), 3);
	ASSERT_EQ(seqc_barStep(), 72 % 32);
	ASSERT_EQ(seqc_trackStep(3), 36 % 32);	/* own step 36 at 6912 = 36 * 192 */
	ASSERT_FALSE(seqc_trackOnMasterGrid(3));
}

TEST(TT3_pattern_change_restarts_every_track)
{
	fake_host_t *h = fake_init();
	for (uint8_t p = 0; p < 2; p++)
	{
		allOn(h, p, 0);
		allOn(h, p, 1);
		fake_setTempo(h, p, 1, 3);		/* x2/3: 144 ticks */
		fake_setLength(h, p, 1, 5);
	}
	changeAt = 10;
	h->onStepBoundary = askPattern1;	/* the change at abs 16, tick 1536 */
	play(h, 2550.0);			/* 1958 ticks */
	const fake_rec_t *r[32];
	ASSERT_EQ(hits(h, 1, r, 32), 14);	/* 0 .. 1440, then 1536, 1680, 1824; not 1584 */
	for (uint32_t k = 0; k <= 10; k++)
	{
		ASSERT_EQ(r[k]->ev.tick, (seqc_tick_t)k * 144);
		ASSERT_EQ(r[k]->ev.pattern, 0);
		ASSERT_EQ(r[k]->ev.step, k % 5);
	}
	for (uint32_t k = 0; k < 3; k++)
	{
		ASSERT_EQ(r[11 + k]->ev.tick, 1536 + (seqc_tick_t)k * 144);
		ASSERT_EQ(r[11 + k]->ev.pattern, 1);
		ASSERT_EQ(r[11 + k]->ev.step, k);
		ASSERT_EQ(r[11 + k]->ev.absStep, 11 + k);
	}
	const fake_rec_t *x1 = firstOf(h, 0, 1);
	ASSERT_TRUE(x1 != NULL);
	ASSERT_EQ(x1->ev.tick, 1536);
	ASSERT_EQ(x1->ev.step, 0);
	ASSERT_EQ(h->errors, 0);
}

TEST(TT4_restart_brings_the_first_steps_early_hits)
{
	/* every step of tracks 1-4 is shifted early; the change to pattern 1 comes at 1536,
	 * predicted at the master boundary at 1440:
	 * - track 1 at x2/3 (18-tick units, -3: 1482): its own boundary at 1440 falls into
	 *   the predicting master step, and its step lasts 144 >= 96 ticks: the prediction
	 *   generates it as occurrence abs + 2, and that boundary skips it;
	 * - track 2 at x4 (3, -3: 1527): its last boundary before the change, 1512, does;
	 * - track 3 at x3/4 (16, -3: 1488): no boundary of its own in [1440, 1536): the
	 *   prediction removes its pattern-0 hit at 1488 and generates pattern 1's;
	 * - track 4 at x1/4 (48, -7: 1200): before 1440, not played */
	fake_host_t *h = fake_init();
	static const uint8_t tempo[5] = { 0, 3, 10, 4, 0 };
	for (uint8_t p = 0; p < 2; p++)
		for (uint8_t t = 1; t <= 4; t++)
		{
			allOn(h, p, t);
			fake_setTempo(h, p, t, tempo[t]);
			for (uint8_t s = 0; s < FAKE_NUM_STEPS; s++)
				fake_setTiming(h, p, t, s, 0, t == 4 ? -7 : -3);
		}
	changeAt = 10;
	h->onStepBoundary = askPattern1;
	play(h, 2300.0);
	static const seqc_tick_t want[3] = { 1482, 1527, 1488 };
	for (uint8_t t = 1; t <= 3; t++)
	{
		const fake_rec_t *r = firstOf(h, t, 1);
		ASSERT_TRUE(r != NULL);
		ASSERT_EQ(r->ev.step, 0);
		ASSERT_EQ(r->ev.tick, want[t - 1]);
	}
	const fake_rec_t *r[256];
	const size_t n3 = hits(h, 3, r, 256);
	for (size_t i = 0; i < n3 && i < 256; i++)
		if (r[i]->ev.pattern == 0)
			ASSERT_TRUE(r[i]->ev.tick < 1488);
	const fake_rec_t *r4 = firstOf(h, 4, 1);
	ASSERT_TRUE(r4 != NULL);
	ASSERT_EQ(r4->ev.step, 1);
	ASSERT_EQ(r4->ev.tick, 1536 + 384 - 336);
	/* the prediction is over at 1536: track 1's step 1 of pattern 1 is early again */
	const size_t n1 = hits(h, 1, r, 256);
	bool step1 = false;
	for (size_t i = 0; i < n1 && i < 256; i++)
		step1 = step1 || (r[i]->ev.pattern == 1 && r[i]->ev.step == 1 && r[i]->ev.tick == 1680 - 54);
	ASSERT_TRUE(step1);
	ASSERT_EQ(h->errors, 0);
}

/* a MIDI slave at 120 BPM, as tests/test_transport.c */
static fake_host_t *midiSlave(void)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	fake_process(h, fake_msToCycles(1.0));
	return h;
}

TEST(TT5_song_position_places_own_tracks)
{
	fake_host_t *h = midiSlave();
	allOn(h, 0, 1);
	fake_setTempo(h, 0, 1, 4);		/* x3/4: its step 4 runs 512..640, across tick 576 */
	seqc_songPosition(6);
	ASSERT_EQ(seqc_trackStep(1), 4);
	ASSERT_FALSE(seqc_trackOnMasterGrid(1));
	ASSERT_TRUE(seqc_trackOnMasterGrid(0));
	fake_sync(h, SEQC_EV_CONTINUE, h->now + 100);
	const double per = fake_clockCycles(120.0);
	fake_runClocks(h, SEQC_EV_CLOCK, h->now + (seqc_time_t)per, per, 12, fake_msToCycles(0.1));
	const fake_rec_t *r[8];
	ASSERT_EQ(hits(h, 1, r, 8), 1);		/* step 4 is not played; step 5 at 640 is */
	ASSERT_EQ(r[0]->ev.tick, 640);
	ASSERT_EQ(r[0]->ev.step, 5);
}

TEST(TT5_own_steps_for_phase_skip_and_copies)
{
	fake_host_t *h = fake_init();
	fake_setTempo(h, 0, 2, 8);		/* x2: 48 ticks */
	allOn(h, 0, 2);
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	const double tick = fake_tickCycles(120.0);
	const uint32_t serial = seqc_stepSerial();
	fake_process(h, (seqc_time_t)(72 * tick));
	ASSERT_EQ(seqc_trackStep(2), 1);
	ASSERT_EQ(seqc_trackStep(0), 0);
	ASSERT_NEAR(seqc_trackPhase(2), 0.5, 1e-6);	/* 24 of its 48 ticks */
	ASSERT_NEAR(seqc_trackPhase(0), 0.75, 1e-6);	/* the master step: seqc_stepPhase() */
	ASSERT_TRUE(seqc_stepSerial() != serial);
	seqc_skipOnce(2, 2);				/* its own next step, 2 at 96 */
	fake_process(h, (seqc_time_t)(200 * tick));
	const fake_rec_t *r[8];
	ASSERT_EQ(hits(h, 2, r, 8), 4);			/* 0, 48, 144, 192 */
	ASSERT_EQ(r[2]->ev.tick, 144);
	ASSERT_EQ(fake_count(h, FAKE_TRACK_STEP), 4);	/* 48, 96, 144, 192 */
	ASSERT_EQ(seqc_barStep(), 2);
	ASSERT_EQ(h->errors, 0);
}

/* the step hit of track at tick, or NULL */
static const fake_rec_t *hitAt(const fake_host_t *h, int track, seqc_tick_t tick)
{
	const fake_rec_t *r[512];
	const size_t n = hits(h, track, r, 512);
	for (size_t i = 0; i < n && i < 512; i++)
		if (r[i]->ev.tick == tick)
			return r[i];
	return NULL;
}

/* every step of track 1 in patterns 0 and 1 on and shifted by shift units */
static void shiftedTrack1(fake_host_t *h, int8_t shift)
{
	for (uint8_t p = 0; p < 2; p++)
	{
		allOn(h, p, 1);
		for (uint8_t s = 0; s < FAKE_NUM_STEPS; s++)
			fake_setTiming(h, p, 1, s, 0, shift);
	}
}

/* stepBoundary() hook: track 1 of pattern 0 goes to x2 at the master boundary at 1440,
 * where the master track's wrap at 1536 is predicted */
static void toX2At15(fake_host_t *h, uint32_t absStep)
{
	askPattern1(h, absStep);
	if (absStep == 15)
		fake_setTempo(h, 0, 1, 8);
}

TEST(TT6_leaving_the_grid_where_a_change_is_predicted)
{
	/* track 1 leaves the master grid at 1440 for x2 (48 ticks, 6-tick units, -3: -18):
	 * its own step at 1488, step 0 of pattern 0, plays at 1470 with or without the change
	 * to pattern 1 at 1536 that the same master boundary predicts; with the change,
	 * pattern 1's first step (x1, -36) follows at 1500 */
	for (int run = 0; run < 2; run++)
	{
		fake_host_t *h = fake_init();
		shiftedTrack1(h, -3);
		changeAt = run == 0 ? 10 : UINT32_MAX;
		h->onStepBoundary = toX2At15;
		play(h, 2100.0);
		const fake_rec_t *r = hitAt(h, 1, 1470);
		ASSERT_TRUE(r != NULL);
		ASSERT_EQ(r->ev.pattern, 0);
		ASSERT_EQ(r->ev.step, 0);
		ASSERT_EQ(r->ev.absStep, 16);
		if (run == 0)
		{
			const fake_rec_t *f = firstOf(h, 1, 1);
			ASSERT_TRUE(f != NULL);
			ASSERT_EQ(f->ev.step, 0);
			ASSERT_EQ(f->ev.tick, 1500);
		}
		ASSERT_EQ(h->errors, 0);
	}
}

TEST(TT6_leaving_the_grid_before_a_song_end)
{
	/* the song is pattern 0 once and ends at 1536, which the master boundary at 1440
	 * predicts; track 1 leaves the master grid there for x2: its own step at 1488 still
	 * belongs to the song and plays at 1470 */
	fake_host_t *h = fake_init();
	static const uint8_t song[] = { 0 };
	fake_setSong(h, song, 1);
	fake_setSongMode(h, true);
	shiftedTrack1(h, -3);
	changeAt = UINT32_MAX;
	h->onStepBoundary = toX2At15;
	play(h, 2100.0);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);		/* the song ended at 1536 */
	const fake_rec_t *r = hitAt(h, 1, 1470);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->ev.step, 0);
	ASSERT_EQ(r->ev.absStep, 16);
	ASSERT_EQ(h->errors, 0);
}

/* track 1 of pattern 0 goes to x3/4 at the master boundary at 1344, so its own boundary
 * at 1472 lies strictly inside the predicting master step [1440, 1536) */
static void toX34At14(fake_host_t *h, uint32_t absStep)
{
	askPattern1(h, absStep);
	if (absStep == 14)
		fake_setTempo(h, 0, 1, 4);
}
/* trackStep() hook: and to x4 at that boundary */
static void toX4At1472(fake_host_t *h, uint8_t track)
{
	if (track == 1 && seqc_position() == 1472.0)
		fake_setTempo(h, 0, 1, 10);
}

TEST(TT7_tempo_edit_after_the_restart_prediction)
{
	/* at 1440 track 1 (x3/4) has one boundary of its own before the change at 1536,
	 * 1472, and a step of 128 ticks there: the prediction takes the first step of
	 * pattern 1 for occurrence abs + 2. The tempo read at 1472 is x4 (24 ticks, 3-tick
	 * units, -3: -9), which puts own steps at 1472, 1496 and 1520 before the change:
	 * pattern 0 plays 1487 and 1511, and pattern 1's first step (x4) is occurrence 18
	 * at 1527, generated at 1520 */
	fake_host_t *h = fake_init();
	shiftedTrack1(h, -3);
	fake_setTempo(h, 1, 1, 10);
	changeAt = 10;
	h->onStepBoundary = toX34At14;
	h->onTrackStep = toX4At1472;
	play(h, 2300.0);
	static const seqc_tick_t old[2] = { 1487, 1511 };
	for (uint32_t k = 0; k < 2; k++)
	{
		const fake_rec_t *r = hitAt(h, 1, old[k]);
		ASSERT_TRUE(r != NULL);
		ASSERT_EQ(r->ev.pattern, 0);
		ASSERT_EQ(r->ev.absStep, 16 + k);
	}
	const fake_rec_t *f = firstOf(h, 1, 1);
	ASSERT_TRUE(f != NULL);
	ASSERT_EQ(f->ev.step, 0);
	ASSERT_EQ(f->ev.tick, 1527);
	ASSERT_EQ(f->ev.absStep, 18);
	ASSERT_EQ(h->errors, 0);
}

TEST_MAIN()
