/*
 * app_ring.c
 *
 * The single-producer, single-consumer ring of app_ring.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_ring.h"

#include <stdlib.h>
#include <string.h>

int app_ringInit(app_ring_t *r, size_t entrySize, uint32_t capacity)
{
	memset(r, 0, sizeof *r);
	if (capacity == 0u || (capacity & (capacity - 1u)) != 0u || entrySize == 0u)
		return -1;
	r->buf = calloc(capacity, entrySize);
	if (!r->buf)
		return -1;
	r->size = entrySize;
	r->mask = capacity - 1u;
	atomic_init(&r->head, 0u);
	atomic_init(&r->tail, 0u);
	atomic_init(&r->dropped, 0u);
	return 0;
}

void app_ringFree(app_ring_t *r)
{
	free(r->buf);
	r->buf = NULL;
}

void app_ringReset(app_ring_t *r)
{
	atomic_store(&r->head, 0u);
	atomic_store(&r->tail, 0u);
}

void *app_ringSlot(app_ring_t *r, uint32_t i)
{
	const uint64_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
	const uint64_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
	if (head - tail + i > (uint64_t)r->mask)
		return NULL;
	return r->buf + (size_t)((head + i) & r->mask) * r->size;
}

void app_ringCommit(app_ring_t *r, uint32_t n)
{
	const uint64_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
	atomic_store_explicit(&r->head, head + n, memory_order_release);
}

int app_ringPush(app_ring_t *r, const void *entry)
{
	void *slot = app_ringSlot(r, 0u);
	if (!slot)
	{
		atomic_fetch_add_explicit(&r->dropped, 1u, memory_order_relaxed);
		return -1;
	}
	memcpy(slot, entry, r->size);
	app_ringCommit(r, 1u);
	return 0;
}

const void *app_ringPeek(app_ring_t *r, uint32_t i)
{
	const uint64_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
	const uint64_t head = atomic_load_explicit(&r->head, memory_order_acquire);
	if ((uint64_t)i >= head - tail)
		return NULL;
	return r->buf + (size_t)((tail + i) & r->mask) * r->size;
}

void app_ringPop(app_ring_t *r, uint32_t n)
{
	const uint64_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
	atomic_store_explicit(&r->tail, tail + n, memory_order_release);
}

uint32_t app_ringCount(app_ring_t *r)
{
	const uint64_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
	return (uint32_t)(atomic_load_explicit(&r->head, memory_order_acquire) - tail);
}

uint64_t app_ringPopped(app_ring_t *r)
{
	return atomic_load_explicit(&r->tail, memory_order_acquire);
}

uint64_t app_ringDropped(app_ring_t *r)
{
	return atomic_load_explicit(&r->dropped, memory_order_relaxed);
}
