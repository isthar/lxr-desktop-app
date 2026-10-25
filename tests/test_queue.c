/*
 * test_queue.c
 *
 * The event queue (N2, amended for task E1.16): firing order, a full queue, and
 * its real-time cost, counted as the queue elements it writes.
 *
 * This file runs against a copy of the core built with -DSEQC_QUEUE_TEST
 * (seq_core_queue_test in tests/CMakeLists.txt). That copy counts the elements
 * the queue writes in seqc_queueMoves and lets the tests insert, pop and choke
 * directly (the test-only block at the end of seq_core.h).
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

#define CAPACITY ((int)SEQC_QUEUE_SIZE)	/* seq_core_queue_test keeps the firmware's queue */

static seqc_stats_t stats(void)
{
	seqc_stats_t s;
	seqc_stats(&s);
	return s;
}

static uint64_t lcgState;
static uint32_t lcg(void)
{
	lcgState = lcgState * 6364136223846793005ull + 1442695040888963407ull;	/* MMIX */
	return (uint32_t)(lcgState >> 33);
}

/* an event with the key fields of N2 and an id in p1Val/p2Val */
static seqc_event_t event(seqc_tick_t tick, uint32_t absStep, bool roll, uint8_t track, uint16_t id)
{
	seqc_event_t ev;
	memset(&ev, 0, sizeof ev);
	ev.tick = tick;
	ev.absStep = absStep;
	ev.track = track;
	ev.flags = roll ? SEQC_EVF_ROLL : 0;
	ev.p1Val = (uint8_t)(id & 0xff);
	ev.p2Val = (uint8_t)(id >> 8);
	return ev;
}

//------------------------------------------------------------------------------
// Firing order
//------------------------------------------------------------------------------
/* every combination of tick 0..1, absStep 0..1, note or roll and track 0..6,
 * queued in a shuffled order, pops in N2's order */
TEST(queue_order_ties)
{
	enum { N = 2 * 2 * 2 * SEQC_NUM_TRACKS };
	for (uint64_t seed = 1; seed <= 20; seed++)
	{
		fake_init();
		lcgState = seed;
		seqc_event_t all[N];
		int k = 0;
		for (int tick = 0; tick < 2; tick++)
			for (int absStep = 1; absStep >= 0; absStep--)	/* absStep descending */
				for (int roll = 0; roll < 2; roll++)	/* note before roll */
					for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
					{
						all[k] = event(tick, (uint32_t)absStep, roll, t, (uint16_t)k);
						k++;
					}
		int order[N];
		for (int i = 0; i < N; i++)
			order[i] = i;
		for (int i = N - 1; i > 0; i--)
		{
			const int j = (int)(lcg() % (uint32_t)(i + 1));
			const int x = order[i];
			order[i] = order[j];
			order[j] = x;
		}
		for (int i = 0; i < N; i++)
			seqc_testQueueInsert(&all[order[i]]);
		ASSERT_EQ(stats().queueDepth, N);
		for (int i = 0; i < N; i++)
		{
			seqc_event_t ev;
			ASSERT_TRUE(seqc_testQueuePop(&ev));
			ASSERT_TRUE(memcmp(&ev, &all[i], sizeof ev) == 0);
		}
		seqc_event_t ev;
		ASSERT_FALSE(seqc_testQueuePop(&ev));
	}
}

/* the reference: the queue as an unsorted list. A pop takes the event that is
 * first by N2's key; of events with the same key, the one queued last, as the
 * sorted array before the ring did (E1.16). */
typedef struct
{
	seqc_event_t	ev;
	uint32_t	seq;		/* insertion number */
} ref_entry_t;

static ref_entry_t ref[CAPACITY];
static int refCount;

/* <0 if a fires before b by N2's key, 0 if the keys are equal */
static int keyCompare(const seqc_event_t *a, const seqc_event_t *b)
{
	if (a->tick != b->tick)
		return a->tick < b->tick ? -1 : 1;
	if (a->absStep != b->absStep)
		return a->absStep > b->absStep ? -1 : 1;
	const int ra = (a->flags & SEQC_EVF_ROLL) != 0, rb = (b->flags & SEQC_EVF_ROLL) != 0;
	if (ra != rb)
		return ra - rb;
	return (int)a->track - (int)b->track;
}

static seqc_event_t refPop(void)
{
	int best = 0;
	for (int i = 1; i < refCount; i++)
	{
		const int c = keyCompare(&ref[i].ev, &ref[best].ev);
		if (c < 0 || (c == 0 && ref[i].seq > ref[best].seq))
			best = i;
	}
	const seqc_event_t ev = ref[best].ev;
	ref[best] = ref[--refCount];
	return ev;
}

/* ops random inserts and pops against the reference, then a drain. Ticks drift
 * upwards, so most inserts are appends and some go between pending events; the
 * small ranges give many ties on tick, absStep, note and roll, and track.
 * Returns the number of rejected inserts. */
static long runAgainstReference(uint64_t seed, int ops, uint32_t insertPercent)
{
	fake_init();
	lcgState = seed;
	refCount = 0;
	long rejected = 0;
	uint32_t seq = 0;
	seqc_tick_t base = 0;
	for (int i = 0; i < ops; i++)
	{
		if (lcg() % 100u < insertPercent)
		{
			/* one draw per statement: the order of evaluation of arguments is unspecified */
			base += lcg() % 2u;
			const seqc_tick_t tick = base + lcg() % 4u;
			const uint32_t absStep = lcg() % 4u;
			const bool roll = lcg() % 4u == 0;
			const uint8_t track = (uint8_t)(lcg() % SEQC_NUM_TRACKS);
			const seqc_event_t ev = event(tick, absStep, roll, track, (uint16_t)seq);
			seqc_testQueueInsert(&ev);
			if (refCount < CAPACITY)
				ref[refCount++] = (ref_entry_t){ ev, seq };
			else
				rejected++;
			seq++;
		}
		else if (refCount > 0)
		{
			const seqc_event_t want = refPop();
			seqc_event_t ev;
			if (!seqc_testQueuePop(&ev) || memcmp(&ev, &want, sizeof ev) != 0)
			{
				printf("seed %llu, op %d: the pop differs from the reference\n", (unsigned long long)seed, i);
				return -1;
			}
		}
		const seqc_stats_t s = stats();
		if (s.queueDepth != refCount || s.queueOverflow != (uint32_t)rejected)
		{
			printf("seed %llu, op %d: depth %u (want %d), overflow %u (want %ld)\n", (unsigned long long)seed,
					i, (unsigned)s.queueDepth, refCount, (unsigned)s.queueOverflow, rejected);
			return -1;
		}
	}
	while (refCount > 0)
	{
		const seqc_event_t want = refPop();
		seqc_event_t ev;
		if (!seqc_testQueuePop(&ev) || memcmp(&ev, &want, sizeof ev) != 0)
		{
			printf("seed %llu, drain: the pop differs from the reference\n", (unsigned long long)seed);
			return -1;
		}
	}
	seqc_event_t ev;
	if (seqc_testQueuePop(&ev))
		return -1;
	return rejected;
}

TEST(queue_order_matches_the_reference_sort)
{
	for (uint64_t seed = 1; seed <= 20; seed++)
		ASSERT_EQ(runAgainstReference(seed, 4000, 50), 0);	/* stays below CAPACITY */
}

//------------------------------------------------------------------------------
// A full queue
//------------------------------------------------------------------------------
TEST(queue_full_rejects_and_counts)
{
	fake_init();
	for (int i = 0; i < CAPACITY; i++)
	{
		const seqc_event_t ev = event(100 + i, 0, false, 0, (uint16_t)i);
		seqc_testQueueInsert(&ev);
	}
	seqc_stats_t s = stats();
	ASSERT_EQ(s.queueDepth, CAPACITY);
	ASSERT_EQ(s.queueMax, CAPACITY);
	ASSERT_EQ(s.queueOverflow, 0);
	/* rejected, at either end or in between */
	const seqc_event_t first = event(0, 0, false, 0, 9000);
	const seqc_event_t last = event(1000, 0, false, 0, 9001);
	const seqc_event_t middle = event(150, 0, true, 3, 9002);
	seqc_testQueueInsert(&first);
	seqc_testQueueInsert(&last);
	seqc_testQueueInsert(&middle);
	s = stats();
	ASSERT_EQ(s.queueDepth, CAPACITY);
	ASSERT_EQ(s.queueOverflow, 3);
	for (int i = 0; i < CAPACITY; i++)
	{
		seqc_event_t ev;
		ASSERT_TRUE(seqc_testQueuePop(&ev));
		ASSERT_EQ(ev.tick, 100 + i);
	}
	ASSERT_EQ(stats().queueDepth, 0);
}

/* mostly full: inserts are rejected, pops move the start of the queue round the
 * array many times, and inserts between pending events cross the array's end */
TEST(queue_full_wraps_over_many_cycles)
{
	for (uint64_t seed = 1; seed <= 5; seed++)
	{
		const long rejected = runAgainstReference(seed, 20 * CAPACITY, 60);
		ASSERT_TRUE(rejected > CAPACITY);
		ASSERT_EQ(stats().queueMax, CAPACITY);
	}
}

//------------------------------------------------------------------------------
// Real-time cost (the E1 final review)
//------------------------------------------------------------------------------
/* One seqc_process() call: the queue elements it wrote, the events it queued
 * (what is pending now, plus what left the queue: triggered, muted, or dropped
 * by the choke or the same-tick rule) and its step boundaries. */
typedef struct
{
	uint32_t	moves;
	long		inserted;
	size_t		boundaries;
} call_t;

static call_t processCall(fake_host_t *h, seqc_time_t now)
{
	const uint32_t m0 = seqc_queueMoves;
	const seqc_stats_t s0 = stats();
	const size_t trig0 = h->calls[FAKE_TRIGGER];
	const size_t b0 = h->calls[FAKE_STEP_BOUNDARY];
	fake_process(h, now);
	const seqc_stats_t s1 = stats();
	call_t c;
	c.moves = seqc_queueMoves - m0;
	c.inserted = (long)s1.queueDepth - (long)s0.queueDepth + (long)(h->calls[FAKE_TRIGGER] - trig0)
			+ (long)(s1.muteDrops - s0.muteDrops) + (long)(s1.chokeDrops - s0.chokeDrops);
	c.boundaries = h->calls[FAKE_STEP_BOUNDARY] - b0;
	return c;
}

/* The review's dense pattern: 7 tracks, every step x8 (flam 5), the odd steps
 * shifted by odd units and the even ones by even units, with shuffle. 16 steps
 * at 120 BPM, one seqc_process() every 0.1 ms. */
static fake_host_t *startDenseShuffled(int8_t odd, int8_t even, uint8_t shuffle)
{
	fake_host_t *h = fake_init();
	h->logMask = 0;
	h->shuffle = shuffle;
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
		for (uint8_t s = 0; s < 16; s++)
		{
			fake_setStep(h, 0, t, s, true);
			fake_setTiming(h, 0, t, s, 5, (s & 1) ? odd : even);
		}
	fake_process(h, 0);
	seqc_start();
	return h;
}

static fake_host_t *startDense(int8_t shift)
{
	return startDenseShuffled(shift, shift, 0);
}

/* The core generates a boundary's hits track by track. With every track on the
 * same grid, hit j of track t orders before hits j+1..7 of tracks 0..t-1, so a
 * shift towards the tail alone would move t * (7 - j) of the new events:
 * C(7,2) * C(8,2) = 588 in all. Inserting from the nearer end moves at most that
 * (354 without shift). Without shuffle the events still pending from the step
 * before fire before every new one and do not move. */
#define DENSE_NEW_EVENT_MOVES		(21 * 28)
#define DENSE_NEW_EVENT_MOVES_NO_SHIFT	354

/* With shuffle, or shifts that alternate between odd and even steps, a step's
 * hits interleave with those still pending from the step before, and a
 * boundary queues up to 105 events. The bounds are the writes measured with
 * the two-ended insert; a tail-only insert writes 3878, 1414 and 1379. */
typedef struct
{
	int8_t		odd;		/**< the shift of the odd steps */
	int8_t		even;		/**< the shift of the even steps */
	uint8_t		shuffle;
	uint32_t	maxMoves;	/**< the most queue elements one boundary call writes */
} dense_case_t;

static const dense_case_t denseCases[] = {
	{ +7, +7,   0, SEQC_NUM_TRACKS * 8 + DENSE_NEW_EVENT_MOVES },	/* a step's hits still pending at the next boundary */
	{  0,  0,   0, SEQC_NUM_TRACKS * 8 + DENSE_NEW_EVENT_MOVES_NO_SHIFT },	/* tail-only: 644 */
	{ +7, -7, 127, 1194 },	/* tail-only: 3878 */
	{ -7, +7, 127,  543 },	/* tail-only: 1414 */
	{ +7, +7, 127, 1077 },	/* tail-only: 1379 */
};

static void denseBoundaryMovesAreBounded(const dense_case_t *dc)
{
	fake_host_t *h = startDenseShuffled(dc->odd, dc->even, dc->shuffle);
	const bool lockstep = dc->odd == dc->even && dc->shuffle == 0;
	const seqc_time_t until = (seqc_time_t)(16.0 * fake_stepCycles(120.0));
	size_t boundaries = 0;
	for (seqc_time_t t = 0; t < until; t += fake_msToCycles(0.1))
	{
		const call_t c = processCall(h, t);
		if (c.boundaries == 0)
			continue;
		ASSERT_EQ(c.boundaries, 1);
		if (lockstep)
			ASSERT_EQ(c.inserted, SEQC_NUM_TRACKS * 8);
		ASSERT_TRUE(c.moves <= dc->maxMoves);
		boundaries++;
	}
	ASSERT_EQ(boundaries, 16);
	ASSERT_TRUE(stats().queueMax >= SEQC_NUM_TRACKS * 8);
	ASSERT_EQ(stats().queueOverflow, 0);
	ASSERT_EQ(h->errors, 0);
}

TEST(queue_dense_boundary_moves_are_bounded)
{
	for (size_t i = 0; i < sizeof denseCases / sizeof denseCases[0]; i++)
		denseBoundaryMovesAreBounded(&denseCases[i]);
}

/* A worst case found by a hill climb over legal settings (the E1.16 review):
 * every step on, per-step shift and flam, roll rates, shuffle 0. Its boundary
 * calls write up to 3861 queue elements (5977 with a tail-only insert) and
 * queue up to 111 events; other calls write up to 289. This is the pattern to
 * give the CPU meter (H12). The bounds are measured, not derived. */
static const int8_t adversarialShift[SEQC_NUM_TRACKS][16] = {
	{ -3, -3, -4, -3, +1, -6, +5, +6, +6, -7, +1, +5, +3, +4, -2, -6 },
	{ +3, +1, +5, +7, -4, -2, -5, +7, +6, -6, +4, +2, +5, -3, +2, +1 },
	{ +0, +7, -5, +7, -7, +7, -4, +5, +7, -7, -7, +3, +7, -7, +7, +4 },
	{ +1, -1, -2, -5, +2, +2, -4, +7, +2, -7, +7, +2, +7, -5, +6, +3 },
	{ +6, +3, -4, +5, -6, +4, -4, +7, +1, -7, -2, +2, +2, +1, +6, +2 },
	{ +7, -5, -7, +4, -2, -7, -3, +7, +1, -7, +0, +2, +4, +5, +6, -5 },
	{ -3, -1, -7, -5, -1, -4, -5, +6, +1, -7, +7, -7, +7, -1, +3, -1 },
};
static const uint8_t adversarialFlam[SEQC_NUM_TRACKS][16] = {
	{ 4, 4, 2, 5, 1, 2, 4, 5, 5, 5, 3, 1, 2, 0, 2, 4 },
	{ 3, 1, 4, 3, 1, 4, 4, 5, 5, 5, 0, 2, 0, 4, 2, 1 },
	{ 5, 1, 2, 0, 4, 1, 5, 5, 5, 5, 1, 2, 2, 0, 4, 0 },
	{ 3, 5, 0, 0, 5, 3, 0, 5, 5, 5, 3, 3, 4, 4, 4, 2 },
	{ 4, 1, 1, 5, 4, 3, 0, 5, 5, 5, 1, 2, 3, 4, 2, 2 },
	{ 3, 0, 5, 2, 0, 5, 3, 5, 5, 5, 3, 4, 5, 2, 2, 3 },
	{ 1, 0, 4, 1, 0, 3, 2, 3, 5, 5, 5, 2, 3, 4, 4, 4 },
};
static const uint8_t adversarialRoll[SEQC_NUM_TRACKS] = { 1, 8, 8, 4, 4, 4, 4 };

#define ADVERSARIAL_BOUNDARY_MOVES	3861
#define ADVERSARIAL_OTHER_MOVES		289

TEST(queue_adversarial_pattern_cost_is_bounded)
{
	fake_host_t *h = fake_init();
	h->logMask = 0;
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		h->rollRate[t] = adversarialRoll[t];
		for (uint8_t s = 0; s < 16; s++)
		{
			fake_setStep(h, 0, t, s, true);
			fake_setTiming(h, 0, t, s, adversarialFlam[t][s], adversarialShift[t][s]);
		}
	}
	fake_process(h, 0);
	seqc_start();
	const seqc_time_t until = (seqc_time_t)(16.0 * fake_stepCycles(120.0));
	size_t boundaries = 0;
	for (seqc_time_t t = 0; t < until; t += fake_msToCycles(0.1))
	{
		const call_t c = processCall(h, t);
		if (c.boundaries == 0)
		{
			ASSERT_TRUE(c.moves <= ADVERSARIAL_OTHER_MOVES);
			continue;
		}
		ASSERT_EQ(c.boundaries, 1);
		ASSERT_TRUE(c.moves <= ADVERSARIAL_BOUNDARY_MOVES);
		boundaries++;
	}
	ASSERT_EQ(boundaries, 16);
	ASSERT_EQ(stats().queueOverflow, 0);
	ASSERT_EQ(h->errors, 0);
}

/* fires that remove nothing write nothing */
TEST(queue_choke_without_removal_moves_nothing)
{
	/* in the core: every call without a boundary fires hits, and the choke finds
	 * nothing to remove */
	fake_host_t *h = startDense(7);
	const seqc_time_t until = (seqc_time_t)(16.0 * fake_stepCycles(120.0));
	size_t fired = 0;
	for (seqc_time_t t = 0; t < until; t += fake_msToCycles(0.1))
	{
		const size_t trig0 = h->calls[FAKE_TRIGGER];
		const call_t c = processCall(h, t);
		fired += h->calls[FAKE_TRIGGER] - trig0;
		if (c.boundaries == 0)
			ASSERT_EQ(c.moves, 0);
	}
	ASSERT_TRUE(fired > 15 * SEQC_NUM_TRACKS * 8);
	ASSERT_EQ(stats().chokeDrops, 0);

	/* directly, with the start of the queue moved on so that it wraps */
	fake_init();
	for (int i = 0; i < CAPACITY - 4; i++)
	{
		const seqc_event_t ev = event(i, 0, false, 6, 0);
		seqc_testQueueInsert(&ev);
		seqc_event_t out;
		ASSERT_TRUE(seqc_testQueuePop(&out));
	}
	for (int i = 0; i < 10; i++)
	{
		const seqc_event_t ev = event(1000 + i, (uint32_t)(5 + i % 2), false, (uint8_t)(i % 3), (uint16_t)i);
		seqc_testQueueInsert(&ev);
	}
	uint32_t m0 = seqc_queueMoves;
	ASSERT_EQ(seqc_testQueueChoke(0, 5), 0);	/* track 0 has absSteps 5 and 6 */
	ASSERT_EQ(seqc_testQueueChoke(1, 5), 0);
	ASSERT_EQ(seqc_testQueueChoke(5, 99), 0);	/* no events of track 5 */
	ASSERT_EQ(seqc_queueMoves - m0, 0);
	ASSERT_EQ(stats().queueDepth, 10);

	/* a removal writes only the kept events after the first removed one: of
	 * track 1 (i = 1, 4, 7), only i = 4 is at absStep 5, so i = 5..9 move */
	m0 = seqc_queueMoves;
	ASSERT_EQ(seqc_testQueueChoke(1, 6), 1);
	ASSERT_EQ(seqc_queueMoves - m0, 5);
	const int kept[] = { 0, 1, 2, 3, 5, 6, 7, 8, 9 };
	for (size_t k = 0; k < sizeof kept / sizeof kept[0]; k++)
	{
		seqc_event_t ev;
		ASSERT_TRUE(seqc_testQueuePop(&ev));
		ASSERT_EQ(ev.p1Val, kept[k]);
	}
	ASSERT_EQ(stats().queueDepth, 0);
}

/* skip-once (D6) removes queued hits by their place in the ring, after the start
 * of the queue has moved on and wrapped. Track 0 plays x8 on every step (hits 12
 * ticks apart), track 1 x8 on steps 6..15; track 4 step 4 has flam 1 and shift -2
 * (an early hit 24 ticks before the step, a late one 24 after). At phase 0.52 of
 * step 3 of each pattern cycle the queue holds track 0's hits at +60, +72 and +84
 * and track 4's early hit, which orders before track 0's +72 (absStep
 * descending). Skip-once removes it and moves only track 0's last two. At these
 * removals the start of the queue is in slots 29, 217, 149, 81, 13, 201, 133, 65,
 * 253, ...: it wraps, and at 253 the kept events straddle the array's end. */
#define SKIP_CYCLES	12

static void runToTick(fake_host_t *h, double tick)
{
	const double cyclesPerTick = fake_stepCycles(120.0) / 96.0;
	const seqc_time_t until = (seqc_time_t)(tick * cyclesPerTick);
	fake_runUntil(h, until, fake_msToCycles(0.1));
}

TEST(queue_skip_once_with_a_moved_head)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER);
	h->recordActive = true;
	for (uint8_t s = 0; s < 16; s++)
	{
		fake_setStep(h, 0, 0, s, true);
		fake_setTiming(h, 0, 0, s, 5, 0);
	}
	for (uint8_t s = 6; s < 16; s++)
	{
		fake_setStep(h, 0, 1, s, true);
		fake_setTiming(h, 0, 1, s, 5, 0);
	}
	fake_setStep(h, 0, 4, 4, true);
	fake_setTiming(h, 0, 4, 4, 1, -2);
	fake_process(h, 0);
	seqc_start();
	for (int k = 0; k < SKIP_CYCLES; k++)
	{
		runToTick(h, (16 * k + 3) * 96 + 50);
		const uint32_t m0 = seqc_queueMoves;
		const unsigned d0 = stats().queueDepth;
		fake_addNote(h, 4, 100, 64);			/* skip-once of track 4, step 4 */
		ASSERT_EQ(stats().queueDepth, d0 - 1);
		ASSERT_EQ(seqc_queueMoves - m0, 2);
	}
	runToTick(h, (16 * SKIP_CYCLES - 1) * 96 - 1);
	const fake_rec_t *trig[1];
	ASSERT_EQ(fake_triggers(h, 4, trig, 1), 0);	/* track 4 plays none of its hits */
	ASSERT_EQ(fake_triggers(h, 0, trig, 1), (16 * SKIP_CYCLES - 1) * 8);
	ASSERT_EQ(fake_triggers(h, 1, trig, 1), (10 * SKIP_CYCLES - 1) * 8);
	ASSERT_EQ(stats().skipDrops, SKIP_CYCLES);
	ASSERT_EQ(stats().queueOverflow, 0);
	ASSERT_EQ(h->errors, 0);
	ASSERT_EQ(h->logLost, 0);
}

TEST_MAIN()
