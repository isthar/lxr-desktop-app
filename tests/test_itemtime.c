/*
 * test_itemtime.c
 *
 * LICHOMAT v0.5 (plan Task 2, spec S7): seqc_itemTime(), the time at which the item
 * that a callback handles was due. The voice scheduler starts a voice at that time,
 * so it must be the event's own time: in internal sync the tick's time on the
 * timeline, also for shifted, swung, ratcheted, rolled, delayed, humanized and
 * per-track-tempo hits and across a tempo change; in the slave modes the
 * interpolation from the last clock, or the stamp of the clock whose jump passes
 * the item. It is never later than the call and never decreases within a call.
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

#define MAX_SEEN 4096

typedef struct
{
	seqc_time_t item;	/* seqc_itemTime() in trigger() */
	seqc_time_t call;	/* h->now: the time of the core call */
	seqc_tick_t tick;
	uint8_t track, hit;
	uint32_t call_n;	/* the core call it came from */
} seen_t;

static seen_t seen[MAX_SEEN];
static size_t nseen;
static uint32_t calls;			/* core calls so far, counted by run() and clocks() */
static seqc_time_t hostMax;		/* the latest time passed to the core */

static void onTrigger(fake_host_t *h, const seqc_event_t *ev)
{
	if (nseen < MAX_SEEN)
		seen[nseen] = (seen_t){ seqc_itemTime(), h->now, ev->tick, ev->track, ev->hit, calls };
	nseen++;
}

static fake_host_t *setup(void)
{
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_NO_CLOCKS;
	h->onTrigger = onTrigger;
	nseen = 0;
	calls = 0;
	hostMax = 0;
	return h;
}

/** seqc_process() every interval until until, counting the calls */
static void run(fake_host_t *h, seqc_time_t until, seqc_time_t interval)
{
	while (h->now + interval <= until)
	{
		calls++;
		fake_process(h, h->now + interval);
		if (h->now > hostMax)
			hostMax = h->now;
	}
}

/** the checks every item time passes: at most the latest time the core was given,
 * and never earlier than the item before it in the same call */
static void checkOrder(void)
{
	for (size_t i = 0; i < nseen && i < MAX_SEEN; i++)
	{
		ASSERT_TRUE(seen[i].item <= hostMax);
		ASSERT_TRUE(seen[i].item <= seen[i].call || seen[i].call < hostMax);
		if (i > 0 && seen[i].call_n == seen[i - 1].call_n)
			ASSERT_TRUE(seen[i].item >= seen[i - 1].item);
	}
}

TEST(seqc_itemtime_internal_groove)
{
	/* 120 BPM: a tick is 281,250 cycles; the start anchors tick 0 at the first
	 * process call after seqc_start(), 0.1 ms */
	fake_host_t *h = setup();
	const seqc_time_t interval = fake_msToCycles(0.1);
	const double tick = fake_tickCycles(120.0);
	/* track 0: steps 2, 4, ..., 30 with shifts -7..+7 (32 steps) */
	fake_setLength(h, 0, 0, 32);
	fake_setStep(h, 0, 0, 0, true);
	for (uint8_t k = 1; k <= 15; k++)
	{
		fake_setStep(h, 0, 0, (uint8_t)(2 * k), true);
		fake_setTiming(h, 0, 0, (uint8_t)(2 * k), 0, (int8_t)(k - 8));
	}
	/* track 1: ratchets x2, x3, x4, x6, x8 on odd steps */
	for (uint8_t f = 1; f <= 5; f++)
	{
		fake_setStep(h, 0, 1, (uint8_t)(2 * f + 1), true);
		fake_setTiming(h, 0, 1, (uint8_t)(2 * f + 1), f, 0);
	}
	/* track 2 at x3/4, track 3 with delay +3 ms and humanize 4 ms, swing 40 */
	for (uint8_t s = 0; s < 16; s += 4)
	{
		fake_setStep(h, 0, 2, s, true);
		fake_setStep(h, 0, 3, (uint8_t)(s + 1), true);
	}
	fake_setTempo(h, 0, 2, 4);
	fake_setGroove(h, 0, 3, 4, 0, 3);
	h->shuffle = 40;
	/* tracks 4 and 5: the 1/32 roll and the triplet roll (1/24) */
	h->rollRate[4] = SEQC_ROLL(1, 2);
	h->rollRate[5] = SEQC_ROLL(2, 3);

	fake_process(h, 0);
	seqc_start();
	run(h, fake_msToCycles(4000.0), interval);
	ASSERT_TRUE(nseen > 150);	/* 188: 16 + 46 + 6 + 8 + 64 + 48 */
	ASSERT_TRUE(nseen <= MAX_SEEN);
	for (size_t i = 0; i < nseen; i++)
	{
		/* the tick's time on the timeline, rounded down as nextDue is */
		const double exact = (double)interval + (double)seen[i].tick * tick;
		ASSERT_NEAR((double)seen[i].item, exact, 1.0);
		/* the item was not due at the call before */
		ASSERT_TRUE(seen[i].item + interval > seen[i].call);
	}
	checkOrder();
	ASSERT_EQ(h->errors, 0);
}

TEST(seqc_itemtime_internal_tempo_change)
{
	/* across a tempo change the anchor moves; each item stays in its call's interval */
	fake_host_t *h = setup();
	const seqc_time_t interval = fake_msToCycles(0.1);
	for (uint8_t s = 0; s < 16; s++)
	{
		fake_setStep(h, 0, 0, s, true);
		fake_setTiming(h, 0, 0, s, (uint8_t)(s % 6), (int8_t)((s % 5) - 2));
	}
	fake_process(h, 0);
	seqc_start();
	run(h, fake_msToCycles(1003.3), interval);
	seqc_setTempo(137.0);
	run(h, fake_msToCycles(3000.0), interval);
	ASSERT_TRUE(nseen > 60);
	for (size_t i = 0; i < nseen && i < MAX_SEEN; i++)
	{
		ASSERT_TRUE(seen[i].item <= seen[i].call);
		ASSERT_TRUE(seen[i].item + interval > seen[i].call);
	}
	checkOrder();
}

/* clocks first + k * per, delivered the way seq_tick() does, counting the calls */
static void clocks(fake_host_t *h, const seqc_time_t *at, size_t count, seqc_time_t interval)
{
	size_t i = 0;
	seqc_time_t t = h->now;
	while (i < count)
	{
		t += interval;
		while (i < count && at[i] <= t)
		{
			calls++;
			fake_sync(h, SEQC_EV_CLOCK, at[i++]);
			if (h->now > hostMax)
				hostMax = h->now;
		}
		calls++;
		fake_process(h, t);
		if (h->now > hostMax)
			hostMax = h->now;
	}
}

static seqc_time_t stampAt[512];

TEST(seqc_itemtime_slave)
{
	/* MIDI slave at 125 BPM: the grid hits at their clocks, the ratchet hits between
	 * clocks from the interpolation, a hit that an early clock passes at its stamp */
	fake_host_t *h = setup();
	const seqc_time_t interval = fake_msToCycles(0.1);
	for (uint8_t s = 0; s < 16; s += 4)
		fake_setStep(h, 0, 0, s, true);
	fake_setStep(h, 0, 1, 2, true);
	fake_setTiming(h, 0, 1, 2, 3, 0);		/* x4: 192, 216, 240, 264 */
	fake_setStep(h, 0, 1, 8, true);
	fake_setTiming(h, 0, 1, 8, 5, 0);		/* x8: 768, 780, ... */
	seqc_setSyncMode(SEQC_SYNC_MIDI);
	fake_process(h, fake_msToCycles(1.0));
	seqc_start();
	const double per = fake_clockCycles(125.0);
	const seqc_time_t t0 = fake_msToCycles(10.0);
	/* clock k at position 16 k; clock 49 comes 0.4 of a clock early, so the hit at
	 * 780 = 16 x 49 - 4 is not reached by the interpolation (16 x 48 + 9.6) */
	const size_t n = 6 * 16 + 1;
	for (size_t k = 0; k < n; k++)
		stampAt[k] = t0 + (seqc_time_t)((double)k * per + 0.5) - (k == 49 ? (seqc_time_t)(0.4 * per) : 0u);
	clocks(h, stampAt, n, interval);
	ASSERT_EQ(seqc_state(), SEQC_RUNNING);
	int early = 0, between = 0;
	for (size_t i = 0; i < nseen && i < MAX_SEEN; i++)
	{
		const seqc_tick_t tk = seen[i].tick;
		if (tk == 780)
		{
			ASSERT_EQ(seen[i].item, stampAt[49]);
			early++;
		}
		else if (tk < 16 * 48 && tk % 16 == 0)
			ASSERT_NEAR((double)seen[i].item, (double)stampAt[tk / 16], 2.0);
		else if (tk < 16 * 48)
		{
			/* the interpolation between clocks, on the steady clock's line; after the
			 * early clock the tempo fit takes it in, and the line moves */
			ASSERT_NEAR((double)seen[i].item, (double)t0 + (double)tk / 16.0 * per, 3.0);
			between++;
		}
	}
	ASSERT_EQ(early, 1);
	ASSERT_EQ(between, 2);			/* the x4 ratchet's 216 and 264 */
	checkOrder();

	/* a dropout (SEQC_DROPOUT_MS, 500 ms): after the last clock, the items that still
	 * fire are within the clamp to the next clock, at most one clock after the last
	 * stamp, and the dropout's own run stops at its limit */
	const size_t before = nseen;
	run(h, h->now + fake_msToCycles(700.0), interval);
	seqc_stats_t st;
	seqc_stats(&st);
	ASSERT_EQ(st.dropouts, 1);
	for (size_t i = before; i < nseen && i < MAX_SEEN; i++)
		ASSERT_TRUE(seen[i].item <= stampAt[n - 1] + (seqc_time_t)per + 2u);
	checkOrder();
}

TEST(seqc_itemtime_outside_a_callback)
{
	/* outside a callback it is the core's current time */
	fake_host_t *h = setup();
	fake_process(h, 12345);
	ASSERT_EQ(seqc_itemTime(), 12345);
	fake_process(h, 99999);
	ASSERT_EQ(seqc_itemTime(), 99999);
}

TEST_MAIN()
