/*
 * test_queue.c
 *
 * The event queue (N2, amended for tasks E1.16 and E1.18): firing order, a full
 * queue, and its real-time cost, counted as the queue elements it writes.
 *
 * This file runs against a copy of the core built with -DSEQC_QUEUE_TEST
 * (seq_core_queue_test in tests/CMakeLists.txt). That copy counts the elements
 * the queue writes in seqc_queueMoves and lets the tests insert, pop and choke
 * directly (the test-only block at the end of seq_core.h).
 *
 * Task E1.18 queues the events of a step boundary together, in one merge. The
 * tests here check that it gives the order and the rejects of one insert per
 * event, against the reference model and against fire logs recorded from the
 * E1.16 queue, and that one seqc_process() call writes at most about 400
 * elements.
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


/* As runAgainstReference(), with the inserts in batches of 1 to maxBatch events,
 * queued together as the events of one step boundary are, and the pops in runs
 * of 1 to maxBatch. Ticks reach a few ticks before the latest pending ones, so a
 * batch interleaves with them. A batch longer than the free space is cut as
 * single inserts would cut it: its last events are rejected. Returns the number
 * of rejected inserts. */
static long runBatchesAgainstReference(uint64_t seed, int ops, uint32_t insertPercent, int maxBatch)
{
	static seqc_event_t batch[2 * CAPACITY];
	fake_init();
	lcgState = seed;
	refCount = 0;
	long rejected = 0;
	uint32_t seq = 0;
	seqc_tick_t base = 100;
	for (int i = 0; i < ops; i++)
	{
		const int n = 1 + (int)(lcg() % (uint32_t)maxBatch);
		if (lcg() % 100u < insertPercent)
		{
			for (int k = 0; k < n; k++)
			{
				base += lcg() % 2u;
				const seqc_tick_t tick = base + (seqc_tick_t)(lcg() % 12u) - 6;
				const uint32_t absStep = lcg() % 4u;
				const bool roll = lcg() % 4u == 0;
				const uint8_t track = (uint8_t)(lcg() % SEQC_NUM_TRACKS);
				batch[k] = event(tick, absStep, roll, track, (uint16_t)seq);
				if (refCount < CAPACITY)
					ref[refCount++] = (ref_entry_t){ batch[k], seq };
				else
					rejected++;
				seq++;
			}
			seqc_testQueueInsertBatch(batch, (uint16_t)n);
		}
		else
		{
			for (int k = 0; k < n && refCount > 0; k++)
			{
				const seqc_event_t want = refPop();
				seqc_event_t ev;
				if (!seqc_testQueuePop(&ev) || memcmp(&ev, &want, sizeof ev) != 0)
				{
					printf("seed %llu, op %d: the pop differs from the reference\n", (unsigned long long)seed, i);
					return -1;
				}
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

/* batches of the size of a boundary's events (up to 112, see seq_core.c),
 * with ties and interleaving, in the order of single inserts; the queue is
 * mostly below its capacity */
TEST(queue_batches_match_the_reference_sort)
{
	for (uint64_t seed = 1; seed <= 40; seed++)
		ASSERT_TRUE(runBatchesAgainstReference(seed, 400, 30, 112) >= 0);
}

/* A batch that belongs before most pending events merges from the head: the
 * start of the queue moves back by the batch, and of the pending events only
 * those in front of the batch's last event move. 50 pending events at ticks
 * 100..149 (ids 0..49), then a batch with the keys of the first three (ids
 * 200..202): of equal keys the staged event fires first, as with one insert per
 * event, where the one queued last fires first (N2). */
TEST(queue_batch_before_pending_merges_from_the_head)
{
	enum { M = 50, K = 3 };
	fake_init();
	refCount = 0;
	uint32_t seq = 0;
	for (int i = 0; i < M; i++)
	{
		const seqc_event_t ev = event(100 + i, 0, false, 0, (uint16_t)i);
		seqc_testQueueInsert(&ev);
		ref[refCount++] = (ref_entry_t){ ev, seq++ };
	}
	seqc_event_t batch[K];
	for (int k = 0; k < K; k++)
	{
		batch[k] = event(100 + k, 0, false, 0, (uint16_t)(200 + k));
		ref[refCount++] = (ref_entry_t){ batch[k], seq++ };
	}
	const unsigned head0 = seqc_testQueueHead();
	const uint32_t m0 = seqc_queueMoves;
	seqc_testQueueInsertBatch(batch, K);
	ASSERT_EQ(seqc_testQueueHead(), (head0 + CAPACITY - K) % CAPACITY);
	ASSERT_EQ(seqc_queueMoves - m0, 2 * K + (K - 1));	/* staged, merged, and ids 0 and 1 */
	ASSERT_EQ(stats().queueDepth, M + K);
	static const int firstIds[] = { 200, 0, 201, 1, 202, 2, 3 };
	for (int i = 0; i < M + K; i++)
	{
		const seqc_event_t want = refPop();
		seqc_event_t ev;
		ASSERT_TRUE(seqc_testQueuePop(&ev));
		ASSERT_TRUE(memcmp(&ev, &want, sizeof ev) == 0);
		if (i < (int)(sizeof firstIds / sizeof firstIds[0]))
			ASSERT_EQ(ev.p1Val | ev.p2Val << 8, firstIds[i]);
	}
	ASSERT_EQ(stats().queueDepth, 0);
}

/* batches into a nearly full queue, also longer than the free space and than a
 * boundary's events: the rejects of single inserts */
TEST(queue_batches_full_reject_as_single_inserts)
{
	for (uint64_t seed = 1; seed <= 20; seed++)
	{
		const long rejected = runBatchesAgainstReference(seed, 400, 60, 2 * CAPACITY);
		ASSERT_TRUE(rejected > CAPACITY);
		ASSERT_EQ(stats().queueMax, CAPACITY);
	}
}

/* The fire log of random runs of the core (queue_equiv.c) is the one recorded
 * from the E1.16 queue, which queued every event on its own: tick, track, step,
 * hit, flags and the rest of each event, and the final counters. test_overflow.c
 * has the same test with a 16-entry queue, which also checks the rejects. */
static const uint64_t equivHashes[] = {
	0xc88d09ec0ce0fecfull,	0xbdf5f754b3336e85ull,	0xfc49d78014a89d21ull,
	0xbd8f086c93050ff5ull,	0xf4c34b6647a2b52cull,	0x6544d8dfeb091873ull,
	0xe8e767f7ccabb99eull,	0xe7923f3eee302450ull,	0x289cc3469a5621feull,
	0x093d3e104f2a7b68ull,	0x8881a84af9bfc4c5ull,	0xfeb61d65b12264f6ull,
	0x0cceb370bfbb9a19ull,	0xfbc30cad8a59682dull,	0x8c839aa36983add7ull,
	0xb4db143156fb78daull,	0xe784ec5d43c1cc92ull,	0x1ac113a22ffa9651ull,
	0xb4e9b6b2b3bd7044ull,	0x9e5c6487047640b3ull,	0x1c7062ca8e3a16e6ull,
	0xf3ba113fc39efc20ull,	0x7fe3ca60e9d6480cull,	0x1b39377a33c32feaull,
	0x26077b0b2d2fd2afull,	0x07da10be753e4ad8ull,	0x7a1d38198d154f24ull,
	0x88bed0f2c558d8bfull,	0x4bfad26283d54ceeull,	0xca71a59002c872f8ull,
	0x0a8b8875b09e5ac7ull,	0x1e89a072e183cb06ull,	0x51e77d1e2a744130ull,
	0x64a4540b81f8e0f1ull,	0x6ff92e55dfe5d0b7ull,	0xcc54fc6c6ca3b7c4ull,
	0x429e6155d053c769ull,	0x0f9c9510a7eea257ull,	0xd5c997cd8e65d530ull,
	0x4d889b9990bcf84full,	0xab55b15955735ff8ull,	0x2ea843906db534daull,
	0x34c81af849b8cacfull,	0xf337f4af5a2df734ull,	0x68af72ae5dbf49ccull,
	0x764a5f0e579ea92cull,	0xe551edcb28bb1991ull,	0xf0d1ec49ad2497a9ull,
	0x804eefb8c5f5739bull,	0xa0a3e86dbb9d71c3ull,	0xc0696e277338e470ull,
	0xa1590d1bcb632ad2ull,	0x5db30a7933d1077eull,	0x843d566ef1f44740ull,
	0xe6bdba17131f68deull,	0x6adcad187402b621ull,	0x4f05c35195a374f7ull,
	0xe52910a169ce3e6cull,	0xbfb3f10b1d64a3a0ull,	0x16136287a312bdafull,
	0xd8893efc4245ae7full,	0x5a34dc15c6a65b72ull,	0x8d01a039d53f4b83ull,
	0x60546a023af826a8ull,
};

TEST(queue_fire_log_matches_the_per_event_inserts)
{
	const size_t n = sizeof equivHashes / sizeof equivHashes[0];
	uint32_t skips = 0, chokes = 0;
	for (size_t i = 0; i < n; i++)
	{
		seqc_stats_t s;
		const uint64_t got = queue_equivRun(i + 1, &s);
		if (got != equivHashes[i])
			printf("seed %zu: hash 0x%016llx, recorded 0x%016llx\n", i + 1, (unsigned long long)got,
					(unsigned long long)equivHashes[i]);
		ASSERT_TRUE(got == equivHashes[i]);
		ASSERT_EQ(s.queueOverflow, 0);
		skips += s.skipDrops;
		chokes += s.chokeDrops;
	}
	ASSERT_TRUE(skips > 0);
	ASSERT_TRUE(chokes > 0);
}

//------------------------------------------------------------------------------
// Real-time cost (the E1 final review, task E1.18)
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

/* The most queue elements one call writes, for calls with a step boundary and
 * calls without one (they fire events, and their choke compacts the queue) */
typedef struct
{
	uint32_t	boundary;
	uint32_t	other;
	size_t		boundaries;
} cost_t;

/* seqc_process() every intervalMs for steps steps at bpm (seqc_setTempo() is
 * the caller's) */
static cost_t runCost(fake_host_t *h, double bpm, double intervalMs, int steps)
{
	cost_t c = { 0, 0, 0 };
	const seqc_time_t until = h->now + (seqc_time_t)(steps * fake_stepCycles(bpm));
	for (seqc_time_t t = h->now; t < until; t += fake_msToCycles(intervalMs))
	{
		const call_t k = processCall(h, t);
		uint32_t *max = k.boundaries > 0 ? &c.boundary : &c.other;
		if (k.moves > *max)
			*max = k.moves;
		c.boundaries += k.boundaries;
	}
	return c;
}

/* The goal of task E1.18 for every call, with a boundary or not. The bounds
 * below are the measured writes, all within it. */
#define CALL_MOVES_GOAL	400

/* The review's dense pattern: 7 tracks, every step x8 (flam 5), the odd steps
 * shifted by `odd` units, the even ones by `even` units, and shuffle `shuffle`.
 * The callers run 16 steps at 120 BPM, one seqc_process() every 0.1 ms. */
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

/* A boundary writes each new event twice, staged and merged, and moves the
 * pending events on the shorter side of the merge once. With every track on
 * the same grid, its 56 events come after every pending one: 112 writes (the
 * E1.16 queue: 644 with shift +7, 410 without). With shuffle, or shifts that
 * alternate between odd and even steps, a step's hits interleave with those
 * still pending from the step before, and a boundary queues up to 105 events:
 * 210 writes (E1.16: 1194). The calls without a boundary compact the choked
 * events once, from the nearer end (E1.16: 168, 217 and 448, where every fire's
 * choke compacted most of the queue). */
typedef struct
{
	int8_t		odd;		/**< the shift of the odd steps */
	int8_t		even;		/**< the shift of the even steps */
	uint8_t		shuffle;
	uint32_t	maxBoundary;	/**< the most queue elements one call with a boundary writes */
	uint32_t	maxOther;	/**< the same for the calls without a boundary */
} dense_case_t;

static const dense_case_t denseCases[] = {
	{ +7, +7,   0, 2 * SEQC_NUM_TRACKS * 8, 0 },	/* a step's hits still pending at the next boundary */
	{  0,  0,   0, 2 * SEQC_NUM_TRACKS * 8, 0 },
	{ +7, -7, 127, 210,  0 },
	{ -7, +7, 127, 140,  0 },
	{ +7, +7, 127, 147, 28 },
};

static void denseMovesAreBounded(const dense_case_t *dc)
{
	fake_host_t *h = startDenseShuffled(dc->odd, dc->even, dc->shuffle);
	const bool lockstep = dc->odd == dc->even && dc->shuffle == 0;
	const seqc_time_t until = (seqc_time_t)(16.0 * fake_stepCycles(120.0));
	size_t boundaries = 0;
	uint32_t maxBoundary = 0, maxOther = 0;
	for (seqc_time_t t = 0; t < until; t += fake_msToCycles(0.1))
	{
		const call_t c = processCall(h, t);
		if (c.boundaries == 0)
		{
			if (c.moves > maxOther)
				maxOther = c.moves;
			continue;
		}
		ASSERT_EQ(c.boundaries, 1);
		if (lockstep)
			ASSERT_EQ(c.inserted, SEQC_NUM_TRACKS * 8);
		if (c.moves > maxBoundary)
			maxBoundary = c.moves;
		boundaries++;
	}
	ASSERT_TRUE(dc->maxBoundary <= CALL_MOVES_GOAL && dc->maxOther <= CALL_MOVES_GOAL);
	ASSERT_TRUE(maxBoundary <= dc->maxBoundary);
	ASSERT_TRUE(maxOther <= dc->maxOther);
	ASSERT_EQ(boundaries, 16);
	ASSERT_TRUE(stats().queueMax >= SEQC_NUM_TRACKS * 8);
	ASSERT_EQ(stats().queueOverflow, 0);
	ASSERT_EQ(h->errors, 0);
}

TEST(queue_dense_moves_are_bounded)
{
	for (size_t i = 0; i < sizeof denseCases / sizeof denseCases[0]; i++)
		denseMovesAreBounded(&denseCases[i]);
}

/* A worst case of the E1.16 queue, found by a hill climb over legal settings
 * (the E1.16 review, "climbed2"): every step on, per-step shift and flam, roll
 * rates, shuffle 0. Its boundary calls queue up to 111 events. At 120, 130 and
 * 300 BPM, with a call every 0.1, 1 or 3 ms, they write up to 330 queue
 * elements (the E1.16 queue: 3861, 608 of them choke compaction), and the other
 * calls up to 43 (E1.16: 289). This is the pattern to give the CPU meter (H12). */
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

#define ADVERSARIAL_BOUNDARY_MOVES	330
#define ADVERSARIAL_OTHER_MOVES		43

/* the tempos and call intervals the cost tests run at */
static const double costBpms[] = { 120.0, 130.0, 300.0 };
static const double costIntervalsMs[] = { 0.1, 1.0, 3.0 };

TEST(queue_adversarial_pattern_cost_is_bounded)
{
	uint32_t maxBoundary = 0, maxOther = 0;
	for (size_t b = 0; b < 3; b++)
		for (size_t k = 0; k < 3; k++)
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
			seqc_setTempo((float)costBpms[b]);
			seqc_start();
			const cost_t c = runCost(h, costBpms[b], costIntervalsMs[k], 32);
			ASSERT_TRUE(c.boundaries >= 31);
			if (c.boundary > maxBoundary)
				maxBoundary = c.boundary;
			if (c.other > maxOther)
				maxOther = c.other;
			ASSERT_EQ(stats().queueOverflow, 0);
			ASSERT_EQ(h->errors, 0);
		}
	ASSERT_TRUE(maxBoundary <= ADVERSARIAL_BOUNDARY_MOVES);
	ASSERT_TRUE(maxOther <= ADVERSARIAL_OTHER_MOVES);
}

/* The review's random patterns: every step x8 with a random shift of -7..+7
 * units, shuffle 127, over many seeds, at the tempos and call intervals above.
 * Boundary calls write up to 240 queue elements and the other calls up to 76
 * (the E1.16 queue: 1691 and 229 for these seeds; the E1.16 review saw up to
 * 2211 in boundary calls with its own seeds). */
#define RANDOM_SEEDS			200
#define RANDOM_BOUNDARY_MOVES		240
#define RANDOM_OTHER_MOVES		76

_Static_assert(ADVERSARIAL_BOUNDARY_MOVES <= CALL_MOVES_GOAL && ADVERSARIAL_OTHER_MOVES <= CALL_MOVES_GOAL,
		"the hill-climbed pattern's bounds are within the goal");
_Static_assert(RANDOM_BOUNDARY_MOVES <= CALL_MOVES_GOAL && RANDOM_OTHER_MOVES <= CALL_MOVES_GOAL,
		"the random patterns' bounds are within the goal");

TEST(queue_random_shifts_cost_is_bounded)
{
	uint32_t maxBoundary = 0, maxOther = 0;
	for (uint64_t seed = 1; seed <= RANDOM_SEEDS; seed++)
	{
		fake_host_t *h = fake_init();
		h->logMask = 0;
		h->shuffle = 127;
		lcgState = seed;
		for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
			for (uint8_t s = 0; s < 16; s++)
			{
				fake_setStep(h, 0, t, s, true);
				fake_setTiming(h, 0, t, s, 5, (int8_t)((int)(lcg() % 15u) - 7));
			}
		const double bpm = costBpms[seed % 3];
		fake_process(h, 0);
		seqc_setTempo((float)bpm);
		seqc_start();
		const cost_t c = runCost(h, bpm, costIntervalsMs[(seed / 3) % 3], 32);
		ASSERT_TRUE(c.boundaries >= 31);
		if (c.boundary > maxBoundary)
			maxBoundary = c.boundary;
		if (c.other > maxOther)
			maxOther = c.other;
		ASSERT_EQ(stats().queueOverflow, 0);
		ASSERT_EQ(h->errors, 0);
	}
	ASSERT_TRUE(maxBoundary <= RANDOM_BOUNDARY_MOVES);
	ASSERT_TRUE(maxOther <= RANDOM_OTHER_MOVES);
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
	const uint32_t m0 = seqc_queueMoves;
	ASSERT_EQ(seqc_testQueueChoke(0, 5), 0);	/* track 0 has absSteps 5 and 6 */
	ASSERT_EQ(seqc_testQueueChoke(1, 5), 0);
	ASSERT_EQ(seqc_testQueueChoke(5, 99), 0);	/* no events of track 5 */
	ASSERT_EQ(seqc_queueMoves - m0, 0);
	ASSERT_EQ(stats().queueDepth, 10);
}

/* A choke compacts the queue from the end nearer to the removed events, so it
 * writes at most the shorter side. Ten events i = 0..9 with the start of the
 * queue moved on so that they wrap; i = 1 and i = 8 are of tracks 0 and 1 at
 * absStep 5, the others of track 2. */
TEST(queue_choke_compacts_from_the_nearer_end)
{
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
		const uint8_t track = i == 1 ? 0 : i == 8 ? 1 : 2;
		const seqc_event_t ev = event(1000 + i, 5, false, track, (uint16_t)i);
		seqc_testQueueInsert(&ev);
	}
	uint32_t m0 = seqc_queueMoves;
	ASSERT_EQ(seqc_testQueueChoke(0, 6), 1);	/* i = 1: i = 0 moves, not i = 2..9 */
	ASSERT_EQ(seqc_queueMoves - m0, 1);
	m0 = seqc_queueMoves;
	ASSERT_EQ(seqc_testQueueChoke(1, 6), 1);	/* i = 8: i = 9 moves */
	ASSERT_EQ(seqc_queueMoves - m0, 1);
	m0 = seqc_queueMoves;
	ASSERT_EQ(seqc_testQueueChoke(2, 6), 8);	/* all the rest: nothing is kept */
	ASSERT_EQ(seqc_queueMoves - m0, 0);
	ASSERT_EQ(stats().queueDepth, 0);

	/* the same ten again; the kept events stay in order */
	for (int i = 0; i < 10; i++)
	{
		const uint8_t track = i == 1 ? 0 : i == 8 ? 1 : 2;
		const seqc_event_t ev = event(1000 + i, 5, false, track, (uint16_t)i);
		seqc_testQueueInsert(&ev);
	}
	ASSERT_EQ(seqc_testQueueChoke(0, 6), 1);
	ASSERT_EQ(seqc_testQueueChoke(1, 6), 1);
	const int kept[] = { 0, 2, 3, 4, 5, 6, 7, 9 };
	for (size_t k = 0; k < sizeof kept / sizeof kept[0]; k++)
	{
		seqc_event_t ev;
		ASSERT_TRUE(seqc_testQueuePop(&ev));
		ASSERT_EQ(ev.p1Val, kept[k]);
	}
	ASSERT_EQ(stats().queueDepth, 0);
}

/* Events that a choke removed count in chokeDrops also when the queue is cleared
 * in the same call (a song end, a slave Start, a jack reset), as with the E1.16
 * queue, which counted them at the fire. Track 0 (length 17, the master) plays
 * step 15 x8 and step 16 x1, both shifted +7, and track 1 step 0 x8 +7, with
 * shuffle 127. Step 16's hit at tick 1620 chokes the last 5 hits of step 15
 * (1624..1672), which interleave with track 1's hits at 1632, 1644, ...: the
 * first is dropped at the head, the other 4 are still queued when the queue is
 * cleared. The E1.16 queue counts 5, with the same triggers. */
static fake_host_t *chokeBeforeClear(void)
{
	fake_host_t *h = fake_init();
	h->logMask = 0;
	h->shuffle = 127;
	fake_setLength(h, 0, 0, 17);
	fake_setStep(h, 0, 0, 15, true);
	fake_setTiming(h, 0, 0, 15, 5, +7);
	fake_setStep(h, 0, 0, 16, true);
	fake_setTiming(h, 0, 0, 16, 0, +7);
	fake_setStep(h, 0, 1, 0, true);
	fake_setTiming(h, 0, 1, 0, 5, +7);
	return h;
}

TEST(queue_choke_counts_at_a_song_end_in_the_same_call)
{
	fake_host_t *h = chokeBeforeClear();
	fake_setSongMode(h, true);			/* song { 0 }: it ends at tick 17 * 96 */
	fake_process(h, 0);
	seqc_start();
	const double cyclesPerTick = fake_stepCycles(120.0) / 96.0;
	fake_runUntil(h, (seqc_time_t)(1610 * cyclesPerTick), fake_msToCycles(0.1));
	const uint32_t drops0 = stats().chokeDrops;
	const uint32_t trig0 = h->calls[FAKE_TRIGGER];
	fake_process(h, (seqc_time_t)(1640 * cyclesPerTick));	/* the choke, then the song end */
	ASSERT_EQ(seqc_state(), SEQC_STOPPED);
	ASSERT_EQ(h->calls[FAKE_TRIGGER] - trig0, 3);
	ASSERT_EQ(stats().chokeDrops - drops0, 5);
	ASSERT_EQ(stats().queueDepth, 0);
	ASSERT_EQ(h->errors, 0);
}

TEST(queue_choke_counts_at_a_slave_start_or_reset_in_the_same_call)
{
	/* a MIDI Start, and a jack reset at 24 PPQ, half a clock after the clock at
	 * tick 1616: the slave position reaches the choke first */
	for (int analog = 0; analog < 2; analog++)
	{
		fake_host_t *h = chokeBeforeClear();
		seqc_setSyncMode(analog ? SEQC_SYNC_ANALOG : SEQC_SYNC_MIDI);
		seqc_setAnalogPpq(24);
		fake_process(h, fake_msToCycles(1.0));
		fake_sync(h, SEQC_EV_START, h->now + 100);
		const double per = fake_clockCycles(120.0);
		fake_runClocks(h, analog ? SEQC_EV_PULSE : SEQC_EV_CLOCK, h->now + (seqc_time_t)per, per, 102,
				fake_msToCycles(0.1));
		const uint32_t drops0 = stats().chokeDrops;
		const uint32_t trig0 = h->calls[FAKE_TRIGGER];
		fake_sync(h, analog ? SEQC_EV_RESET_ON : SEQC_EV_START, h->now + (seqc_time_t)(per / 2));
		ASSERT_EQ(seqc_state(), analog ? SEQC_STOPPED : SEQC_ARMED);
		ASSERT_EQ(h->calls[FAKE_TRIGGER] - trig0, 2);
		ASSERT_EQ(stats().chokeDrops - drops0, 5);
		ASSERT_EQ(stats().queueDepth, 0);
		ASSERT_EQ(h->errors, 0);
	}
}

/* skip-once (D6) removes queued hits by their place in the ring, after the start
 * of the queue has moved on and wrapped. Track 0 plays x8 on every step (hits 12
 * ticks apart), track 1 x8 on steps 6..15; track 4 step 4 has flam 1 and shift -2
 * (an early hit 24 ticks before the step, a late one 24 after). At phase 0.52 of
 * step 3 of each pattern cycle the queue holds track 0's hits at +60, +72 and +84
 * and track 4's early hit, which orders before track 0's +72 (absStep
 * descending). Skip-once removes it from the nearer end, so only track 0's +60
 * moves. The boundary merges move the start of the queue back 47 slots per cycle:
 * at these removals it is in slots 29, 238, 191, ..., 24, 233, ..., 45, 254. It
 * wraps, and at 254, the 18th, the kept events straddle the array's end. */
#define SKIP_CYCLES	18

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
	int straddles = 0;
	for (int k = 0; k < SKIP_CYCLES; k++)
	{
		runToTick(h, (16 * k + 3) * 96 + 50);
		const uint32_t m0 = seqc_queueMoves;
		const unsigned d0 = stats().queueDepth;
		const unsigned head = seqc_testQueueHead();
		ASSERT_TRUE(head != 0);				/* the start of the queue has moved */
		straddles += head + d0 > CAPACITY;		/* the kept events cross the array's end */
		fake_addNote(h, 4, 100, 64);			/* skip-once of track 4, step 4 */
		ASSERT_EQ(stats().queueDepth, d0 - 1);
		ASSERT_EQ(seqc_queueMoves - m0, 1);
	}
	ASSERT_TRUE(straddles > 0);
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

/* skip-once from inside the generation of a boundary, after the early hits of
 * the occurrence it skips are staged: they are removed with the queued ones and
 * do not play (N2). Track 0 step 1 is x8 with shift -7, so boundary 0 stages its
 * early hits; then its stepOn() read of track 1 for step 1 calls seqc_skipOnce()
 * for track 0 step 1, as the glue's recording can from a callback. */
static bool (*plainStepOn)(void *ctx, uint8_t pattern, uint8_t track, uint8_t step);
static bool skippedInStepOn;
static int32_t skippedAtStep;

static bool stepOnThatSkips(void *ctx, uint8_t pattern, uint8_t track, uint8_t step)
{
	if (!skippedInStepOn && track == 1 && step == 1)
	{
		skippedInStepOn = true;
		skippedAtStep = seqc_absStep();
		seqc_skipOnce(0, 1);
	}
	return plainStepOn(ctx, pattern, track, step);
}

TEST(queue_skip_once_from_the_generation_removes_the_staged_hits)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER);
	fake_setStep(h, 0, 0, 1, true);
	fake_setTiming(h, 0, 0, 1, 5, -7);
	fake_setStep(h, 0, 0, 2, true);			/* plays */
	plainStepOn = h->host.stepOn;
	h->host.stepOn = stepOnThatSkips;
	skippedInStepOn = false;
	seqc_init(&h->host, SEQC_CPU_HZ);		/* with the wrapped stepOn() */
	fake_process(h, 0);
	seqc_start();
	fake_runUntil(h, (seqc_time_t)(4 * fake_stepCycles(120.0)), fake_msToCycles(0.1));
	ASSERT_TRUE(skippedInStepOn);
	ASSERT_EQ(skippedAtStep, 0);			/* at boundary 0 */
	const fake_rec_t *trig[16];
	ASSERT_EQ(fake_triggers(h, 0, trig, 16), 1);
	ASSERT_EQ(trig[0]->ev.step, 2);
	ASSERT_EQ(stats().skipDrops, 1);
	ASSERT_EQ(h->errors, 0);
}

TEST_MAIN()
