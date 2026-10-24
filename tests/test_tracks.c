/*
 * test_tracks.c
 *
 * T10: the per-track step counters and the pattern end (spec 6.5, N4, N5).
 * Every track wraps at its own length (polymeter); the master track is the
 * longest by raw length, as fw 1.7 seq_getLongestTrack(); patternEnd() comes at
 * the master wrap, also at step 0 after a start; the indices restart only when
 * the pattern changes; a song end stops and rewinds the transport.
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

#define MAX_SEEN 256

/* the track steps seen at each boundary, recorded by the stepBoundary() hook */
static uint8_t seen[MAX_SEEN][SEQC_NUM_TRACKS];
static uint8_t seenPattern[MAX_SEEN];
static uint32_t seenCount;

static void recordSteps(fake_host_t *h, uint32_t absStep)
{
	if (absStep < MAX_SEEN)
	{
		for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
			seen[absStep][t] = seqc_trackStep(t);
		seenPattern[absStep] = h->activePattern;
	}
	seenCount = absStep + 1;
}

/* start at time 0 and run at 120 BPM until just after the boundary of step last */
static void runTo(fake_host_t *h, uint32_t last)
{
	h->onStepBoundary = recordSteps;
	seenCount = 0;
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * last) + fake_msToCycles(1.0), fake_msToCycles(1.0));
}

/* the absolute steps of the logged patternEnd() calls */
static size_t patternEnds(const fake_host_t *h, int32_t *out, size_t max)
{
	size_t n = 0, pos = 0;
	const fake_rec_t *r;
	while ((r = fake_next(h, FAKE_PATTERN_END, &pos)) != NULL)
	{
		if (n < max)
			out[n] = r->absStep;
		n++;
	}
	return n;
}

TEST(T10_no_track_step_before_the_first_step)
{
	fake_host_t *h = fake_init();
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		ASSERT_EQ(seqc_trackStep(t), SEQC_NO_STEP);
	ASSERT_EQ(seqc_trackStep(7), SEQC_NO_STEP);
	ASSERT_EQ(seqc_masterTrack(), 0);
	fake_process(h, 0);
	seqc_start();
	ASSERT_EQ(seqc_trackStep(0), SEQC_NO_STEP);	/* the start is pending */
	fake_process(h, 0);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		ASSERT_EQ(seqc_trackStep(t), 0);
	ASSERT_EQ(seqc_trackStep(7), SEQC_NO_STEP);
}

TEST(T10_polymeter_wrap_per_track)
{
	fake_host_t *h = fake_init();
	/* raw 0 plays 16 steps and raw 70 plays 64 */
	static const uint8_t raw[SEQC_NUM_TRACKS] = { 16, 12, 7, 64, 0, 5, 70 };
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		fake_setLength(h, 0, t, raw[t]);
	runTo(h, 199);
	ASSERT_EQ(seenCount, 200);
	for (uint32_t n = 0; n < 200; n++)
		for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
			ASSERT_EQ(seen[n][t], n % fake_effectiveLength(raw[t]));
	/* track 6 has the largest raw length: the pattern ends every 64 steps */
	ASSERT_EQ(seqc_masterTrack(), 6);
	int32_t ends[8];
	ASSERT_EQ(patternEnds(h, ends, 8), 4);
	ASSERT_EQ(ends[0], 0);
	ASSERT_EQ(ends[1], 64);
	ASSERT_EQ(ends[2], 128);
	ASSERT_EQ(ends[3], 192);
	ASSERT_EQ(h->errors, 0);
}

TEST(T10_raw_zero_loses_to_any_other_length)
{
	fake_host_t *h = fake_init();
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		fake_setLength(h, 0, t, 0);		/* 16 steps each */
	fake_setLength(h, 0, 4, 8);			/* the master, as in fw 1.7 */
	runTo(h, 40);
	ASSERT_EQ(seqc_masterTrack(), 4);
	int32_t ends[8];
	ASSERT_EQ(patternEnds(h, ends, 8), 6);
	for (int i = 0; i < 6; i++)
		ASSERT_EQ(ends[i], 8 * i);
	/* the other tracks keep running through the pattern ends */
	ASSERT_EQ(seen[40][0], 40 % 16);
	ASSERT_EQ(seen[40][4], 0);
}

TEST(T10_tie_goes_to_the_lowest_track)
{
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 2, 24);
	fake_setLength(h, 0, 5, 24);
	runTo(h, 30);
	ASSERT_EQ(seqc_masterTrack(), 2);
	int32_t ends[4];
	ASSERT_EQ(patternEnds(h, ends, 4), 2);
	ASSERT_EQ(ends[1], 24);
}

TEST(T10_same_pattern_keeps_the_indices_running)
{
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 1, 12);
	runTo(h, 20);
	size_t pos = 0;
	const fake_rec_t *r;
	int ends = 0;
	while ((r = fake_next(h, FAKE_PATTERN_END, &pos)) != NULL)
	{
		ASSERT_EQ(r->value, SEQC_PAT_SAME);
		ends++;
	}
	ASSERT_EQ(ends, 2);				/* steps 0 and 16 */
	ASSERT_EQ(seen[16][0], 0);
	ASSERT_EQ(seen[16][1], 4);			/* 16 mod 12: no reset */
	ASSERT_EQ(seen[20][1], 8);
	ASSERT_EQ(h->patternChanges, 0);
}

TEST(T10_pattern_change_restarts_every_track)
{
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 1, 12);
	fake_setLength(h, 1, 1, 5);
	runTo(h, 3);
	fake_setNextPattern(h, 1);			/* running: switches at the pattern end */
	ASSERT_EQ(h->activePattern, 0);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 23) + fake_msToCycles(1.0), fake_msToCycles(1.0));
	ASSERT_EQ(seenPattern[15], 0);
	ASSERT_EQ(seen[15][1], 3);
	ASSERT_EQ(seenPattern[16], 1);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		ASSERT_EQ(seen[16][t], 0);
	ASSERT_EQ(seen[21][1], 0);			/* pattern 1: track 1 has 5 steps */
	ASSERT_EQ(seen[23][1], 2);
	ASSERT_EQ(h->patternChanges, 1);
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_PATTERN_END, &pos);	/* step 0 */
	r = fake_next(h, FAKE_PATTERN_END, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->absStep, 16);
	ASSERT_EQ(r->value, SEQC_PAT_CHANGED);
	ASSERT_EQ(r->pattern, 1);
}

TEST(T10_reload_of_the_same_pattern_restarts)
{
	fake_host_t *h = fake_init();
	fake_setLength(h, 0, 1, 12);
	runTo(h, 3);
	fake_setNextPattern(h, 0);			/* the same pattern, with the reload flag */
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(120.0) * 16) + fake_msToCycles(1.0), fake_msToCycles(1.0));
	ASSERT_EQ(seen[16][1], 0);
	ASSERT_EQ(h->patternChanges, 1);
}

TEST(T10_first_boundary_starts_the_song)
{
	fake_host_t *h = fake_init();
	static const uint8_t song[] = { 3, 5 };
	fake_setSong(h, song, 2);
	fake_setSongMode(h, true);
	runTo(h, 17);
	int32_t ends[4];
	ASSERT_EQ(patternEnds(h, ends, 4), 2);
	ASSERT_EQ(ends[0], 0);				/* the pointer goes from -1 to entry 0 */
	ASSERT_EQ(seenPattern[0], 3);
	ASSERT_EQ(seenPattern[16], 5);
	ASSERT_EQ(h->songPos, 1);
}

TEST(T10_song_end_stops_and_rewinds)
{
	fake_host_t *h = fake_init();
	static const uint8_t song[] = { 1, 2 };
	fake_setSong(h, song, 2);
	fake_setSongMode(h, true);
	runTo(h, 40);
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	ASSERT_EQ(seqc_absStep(), -1);
	ASSERT_EQ(seqc_trackStep(0), SEQC_NO_STEP);
	ASSERT_EQ(fake_count(h, FAKE_STEP_BOUNDARY), 32);	/* no stepBoundary() at the song end */
	size_t pos = 0;
	const fake_rec_t *r;
	uint8_t tr[4];
	size_t n = 0;
	while ((r = fake_next(h, FAKE_TRANSPORT, &pos)) != NULL)
		if (n < 4)
			tr[n++] = r->value;
	ASSERT_EQ(n, 3);
	ASSERT_EQ(tr[0], SEQC_TRANSPORT_START);
	ASSERT_EQ(tr[1], SEQC_TRANSPORT_STOP);
	ASSERT_EQ(tr[2], SEQC_TRANSPORT_REWIND);
	ASSERT_EQ(h->songPos, FAKE_NO_SONG_POS);

	/* the next start plays the song from entry 0, once */
	fake_clearLog(h);
	seenCount = 0;
	seqc_start();
	const seqc_time_t t = h->now;
	fake_process(h, t);
	fake_runUntil(h, t + (seqc_time_t)(fake_stepCycles(120.0) * 16) + fake_msToCycles(1.0), fake_msToCycles(1.0));
	ASSERT_EQ(seenPattern[0], 1);
	ASSERT_EQ(seenPattern[15], 1);
	ASSERT_EQ(seenPattern[16], 2);
	ASSERT_EQ(h->errors, 0);
}

TEST_MAIN()
