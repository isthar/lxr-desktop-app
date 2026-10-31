/*
 * test_app_ring.c
 *
 * M18 (stage 1b spec 12.1): the SPSC ring of the app (plan R4): fill, drop and
 * reset; bulk writes through slots far past the capacity; one producer thread
 * and one consumer, in order, over a million entries.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <pthread.h>
#include <stdint.h>

#include "app_ring.h"
#include "seqc_test.h"

TEST(M18_ring_fill_drop)
{
	app_ring_t r;
	ASSERT_EQ(app_ringInit(&r, sizeof(uint32_t), 6u), -1);	/* not a power of two */
	ASSERT_EQ(app_ringInit(&r, sizeof(uint32_t), 8u), 0);
	for (uint32_t v = 0; v < 8u; v++)
		ASSERT_EQ(app_ringPush(&r, &v), 0);
	const uint32_t extra = 99u;
	ASSERT_EQ(app_ringPush(&r, &extra), -1);	/* full: dropped and counted (spec 11) */
	ASSERT_EQ(app_ringDropped(&r), 1);
	ASSERT_EQ(app_ringCount(&r), 8);
	ASSERT_EQ(*(const uint32_t *)app_ringPeek(&r, 7u), 7);
	ASSERT_TRUE(app_ringPeek(&r, 8u) == NULL);
	app_ringPop(&r, 3u);
	ASSERT_EQ(*(const uint32_t *)app_ringPeek(&r, 0u), 3);
	ASSERT_EQ(app_ringPopped(&r), 3);
	app_ringReset(&r);
	ASSERT_EQ(app_ringCount(&r), 0);
	ASSERT_EQ(app_ringPopped(&r), 0);
	app_ringFree(&r);
}

/* bulk writes through slots, far past the capacity */
TEST(M18_ring_wrap)
{
	app_ring_t r;
	ASSERT_EQ(app_ringInit(&r, sizeof(uint64_t), 16u), 0);
	uint64_t next = 0, want = 0;
	for (unsigned round = 0; round < 1000u; round++)
	{
		unsigned n = 0;
		uint64_t *slot;
		while (n < 5u && (slot = app_ringSlot(&r, n)) != NULL)
			*slot = next + n++;
		app_ringCommit(&r, n);
		next += n;
		for (unsigned i = 0; i < 3u && app_ringCount(&r); i++, want++)
		{
			ASSERT_EQ(*(const uint64_t *)app_ringPeek(&r, 0u), want);
			app_ringPop(&r, 1u);
		}
	}
	ASSERT_EQ(app_ringPopped(&r), want);
	ASSERT_EQ(app_ringDropped(&r), 0);
	app_ringFree(&r);
}

#define N_THREADED 1000000u
static app_ring_t shared;

static void *producer(void *arg)
{
	(void)arg;
	for (uint64_t i = 0; i < N_THREADED;)
	{
		uint64_t *slot = app_ringSlot(&shared, 0u);
		if (!slot)
			continue;	/* full: the consumer catches up */
		*slot = i++;
		app_ringCommit(&shared, 1u);
	}
	return NULL;
}

TEST(M18_ring_threads)
{
	ASSERT_EQ(app_ringInit(&shared, sizeof(uint64_t), 256u), 0);
	pthread_t t;
	ASSERT_EQ(pthread_create(&t, NULL, producer, NULL), 0);
	uint64_t want = 0;
	int ordered = 1;
	while (want < N_THREADED)
	{
		const uint64_t *v = app_ringPeek(&shared, 0u);
		if (!v)
			continue;
		ordered &= *v == want;
		want++;
		app_ringPop(&shared, 1u);
	}
	pthread_join(t, NULL);
	ASSERT_TRUE(ordered);
	app_ringFree(&shared);
}

TEST_MAIN()
