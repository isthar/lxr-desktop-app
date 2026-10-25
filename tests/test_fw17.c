/*
 * test_fw17.c
 *
 * T20: equivalence with fw 1.7. With shuffle 0, every hit of the core lands on
 * the same 1/24-step pulse as in the replica of the fw 1.7 timing
 * (tests/reference/fw17_replica.c, through fw17_adapter.h). The tempo error of
 * fw 1.7 (D3) does not matter here: hits are compared by step and pulse.
 *
 * Two differences are intended and kept out of the comparison:
 * - D2: fw 1.7 shares its choke flags between tracks. The replica runs with
 *   per-track flags, except in the test that shows the defect.
 * - fw 1.7 resets its choke flags at every step, so a hit that spills over a
 *   step boundary can play after a newer step's hit; the core drops it (spec
 *   6.4). The random patterns keep the steps of a track 3 steps apart, so their
 *   hits never overlap.
 * At a start fw 1.7 also plays the spill-over of the last step of each track
 * (index -1 is "previous" to step 0); the core has no step before step 0, so
 * those hits are left out of the replica's list. The last two tests pin both
 * differences.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdlib.h>

#include "fake_host.h"
#include "fw17_adapter.h"
#include "seqc_test.h"

#define MAX_HITS 4096

typedef struct
{
	uint8_t length[SEQC_NUM_TRACKS];
	bool on[SEQC_NUM_TRACKS][SEQC_NUM_STEPS];
	uint8_t flam[SEQC_NUM_TRACKS][SEQC_NUM_STEPS];
	uint8_t shift[SEQC_NUM_TRACKS][SEQC_NUM_STEPS];	/* raw, 0..15 */
} scenario_t;

static fw17_hit_t ref[MAX_HITS];
static fw17_hit_t got[MAX_HITS];
static const fake_rec_t *trig[MAX_HITS];

static void clearScenario(scenario_t *sc)
{
	memset(sc, 0, sizeof *sc);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		sc->length[t] = 16;
		for (uint8_t s = 0; s < SEQC_NUM_STEPS; s++)
			sc->shift[t][s] = SEQC_SHIFT_CENTER;
	}
}

static int compareHits(const void *a, const void *b)
{
	const fw17_hit_t *x = a, *y = b;
	if (x->seqStep != y->seqStep)
		return x->seqStep - y->seqStep;
	if (x->pulse != y->pulse)
		return x->pulse - y->pulse;
	if (x->track != y->track)
		return x->track - y->track;
	return x->step - y->step;
}

/* the replica's hits of steps 0..steps-1, sorted, without the spill-over of the
 * step before step 0 */
static int runReplica(const scenario_t *sc, int steps, int perTrackChoke)
{
	fw17_reset(130, perTrackChoke);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		fw17_setLength(t, sc->length[t]);
		for (uint8_t s = 0; s < sc->length[t]; s++)
			if (sc->on[t][s])
				fw17_setStep(t, s, sc->flam[t][s], sc->shift[t][s]);
	}
	const int all = fw17_run(steps, ref, MAX_HITS);
	int n = 0;
	for (int i = 0; i < all && i < MAX_HITS; i++)
	{
		const fw17_hit_t *r = &ref[i];
		if (r->seqStep == 0 && r->step == sc->length[r->track] - 1)
			continue;
		ref[n++] = *r;
	}
	qsort(ref, (size_t)n, sizeof ref[0], compareHits);
	return n;
}

/* the core's hits of steps 0..steps-1, as step and pulse, sorted */
static int runCore(const scenario_t *sc, int steps)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		fake_setLength(h, 0, t, sc->length[t]);
		for (uint8_t s = 0; s < sc->length[t]; s++)
			if (sc->on[t][s])
			{
				fake_setStep(h, 0, t, s, true);
				h->timing[0][t][s] = (StepTiming){ .flam = sc->flam[t][s], .shift = sc->shift[t][s] };
			}
	}
	seqc_setTempo(130.0f);
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, (seqc_time_t)(fake_stepCycles(130.0) * steps) - 1, fake_msToCycles(0.1));
	const int n = (int)fake_triggers(h, -1, trig, MAX_HITS);
	for (int i = 0; i < n && i < MAX_HITS; i++)
	{
		const seqc_event_t *ev = &trig[i]->ev;
		got[i] = (fw17_hit_t){ ev->track, ev->step, (int)(ev->tick / 96), (int)(ev->tick % 96 / 4) };
		if (ev->tick % 4 != 0)
			got[i].pulse = -1;			/* off the 96 PPQN grid: never equal */
	}
	qsort(got, (size_t)n, sizeof got[0], compareHits);
	return n;
}

/* compare core and replica; on a difference print the first one */
static void checkEquivalent(const scenario_t *sc, int steps)
{
	const int nr = runReplica(sc, steps, 1);
	const int nc = runCore(sc, steps);
	ASSERT_TRUE(nr > 0);
	for (int i = 0; i < nr && i < nc; i++)
	{
		if (compareHits(&ref[i], &got[i]) != 0)
		{
			printf("hit %d: fw 1.7 track %d step %d at step %d pulse %d, core track %d step %d at step %d pulse %d\n",
					i, ref[i].track, ref[i].step, ref[i].seqStep, ref[i].pulse,
					got[i].track, got[i].step, got[i].seqStep, got[i].pulse);
			ASSERT_TRUE(false);
		}
	}
	ASSERT_EQ(nc, nr);
}

TEST(T20_every_ratchet_and_shift)
{
	/* each (flam, shift) pair once, three steps apart, on all tracks */
	scenario_t sc;
	clearScenario(&sc);
	int k = 0;
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		sc.length[t] = 64;
		for (uint8_t s = 1; s + 3 < 64; s += 3)
		{
			sc.on[t][s] = true;
			sc.flam[t][s] = (uint8_t)(k % 6);
			sc.shift[t][s] = (uint8_t)((k / 6) % 16);
			k++;
		}
	}
	checkEquivalent(&sc, 130);
}

TEST(T20_random_patterns)
{
	srand(20261024);
	for (int round = 0; round < 25; round++)
	{
		scenario_t sc;
		clearScenario(&sc);
		static const uint8_t lengths[] = { 16, 12, 7, 64, 24, 5, 9, 32 };
		for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		{
			const uint8_t len = lengths[rand() % 8];
			sc.length[t] = len;
			/* steps at least 3 apart, also across the wrap */
			int first = -1, last = -1;
			for (int s = rand() % 3; s < len; s += 3 + rand() % 3)
			{
				if (first >= 0 && first + len - s < 3)
					break;
				sc.on[t][s] = true;
				sc.flam[t][s] = (uint8_t)(rand() % 16);
				sc.shift[t][s] = (uint8_t)(rand() % 16);
				if (first < 0)
					first = s;
				last = s;
			}
			(void)last;
		}
		checkEquivalent(&sc, 100);
		if (seqc_test_failed_)
		{
			printf("random round %d\n", round);
			return;
		}
	}
}

/* the replica scenario 6 of the spec, D2 */
static void setUpD2(scenario_t *sc)
{
	clearScenario(sc);
	sc->on[5][0] = true;	/* hat x4 */
	sc->flam[5][0] = 3;
	sc->on[3][1] = true;	/* snare -2 */
	sc->shift[3][1] = 5;
	sc->on[0][3] = true;	/* kick */
	sc->on[5][2] = true;	/* hat x2 +4 */
	sc->flam[5][2] = 1;
	sc->shift[5][2] = 11;
}

TEST(T20_d2_scenario_matches_per_track_flags)
{
	scenario_t sc;
	setUpD2(&sc);
	checkEquivalent(&sc, 6);
}

TEST(T20_global_choke_flags_of_fw17_drop_hits)
{
	/* the replica with fw 1.7's global flags loses the two hits of D2 */
	scenario_t sc;
	setUpD2(&sc);
	const int fixed = runReplica(&sc, 6, 1);
	const int fw17 = runReplica(&sc, 6, 0);
	ASSERT_EQ(fixed - fw17, 2);
	ASSERT_EQ(runCore(&sc, 6), fixed);
}

/* The two intended differences that the comparison above leaves out, pinned
 * (stage 1 report). */
TEST(T20_spill_over_after_a_newer_step_is_choked)
{
	/* fw 1.7 clears its choke flags at every step. Step 1 is x2 at +5 (pulse 15
	 * of step 1 and pulse 3 of step 2), step 2 at -2 plays early, at pulse 18 of
	 * step 1. In step 2 the flags are clear again, so fw 1.7 still plays the
	 * second hit of step 1 after the newer hit. The core's per-track choke (spec
	 * 6.4) drops it. */
	scenario_t sc;
	clearScenario(&sc);
	sc.on[0][1] = true;
	sc.flam[0][1] = 1;
	sc.shift[0][1] = 12;
	sc.on[0][2] = true;
	sc.shift[0][2] = 5;
	const int nr = runReplica(&sc, 4, 1);
	const int nc = runCore(&sc, 4);
	ASSERT_EQ(nr, 3);
	ASSERT_EQ(nc, 2);
	ASSERT_EQ(compareHits(&ref[0], &got[0]), 0);	/* step 1 at pulse 15 */
	ASSERT_EQ(compareHits(&ref[1], &got[1]), 0);	/* step 2 at pulse 18 of step 1 */
	ASSERT_EQ(ref[2].step, 1);			/* fw 1.7 only: step 1 at pulse 3 of step 2 */
	ASSERT_EQ(ref[2].seqStep, 2);
	ASSERT_EQ(ref[2].pulse, 3);
}

TEST(T20_no_spill_over_of_the_last_step_at_the_start)
{
	/* at a start fw 1.7 takes index -1 as the step before step 0, so the late hit
	 * of the last step (step 15, x2 at +7: its second hit at pulse 9 of the next
	 * step) also plays at pulse 9 of step 0. The core has no step before step 0.
	 * After the first wrap both play it. */
	scenario_t sc;
	clearScenario(&sc);
	sc.on[0][15] = true;
	sc.flam[0][15] = 1;
	sc.shift[0][15] = 14;
	fw17_reset(130, 1);
	fw17_setStep(0, 15, 1, 14);
	const int nr = fw17_run(20, ref, MAX_HITS);
	qsort(ref, (size_t)nr, sizeof ref[0], compareHits);
	const int nc = runCore(&sc, 20);
	ASSERT_EQ(nr, 3);
	ASSERT_EQ(nc, 2);
	ASSERT_EQ(ref[0].step, 15);			/* fw 1.7 only */
	ASSERT_EQ(ref[0].seqStep, 0);
	ASSERT_EQ(ref[0].pulse, 9);
	ASSERT_EQ(compareHits(&ref[1], &got[0]), 0);	/* step 15, pulse 21 */
	ASSERT_EQ(compareHits(&ref[2], &got[1]), 0);	/* pulse 9 of step 16 */
}

TEST_MAIN()
