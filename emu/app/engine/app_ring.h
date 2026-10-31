/*
 * app_ring.h
 *
 * A lock-free single-producer, single-consumer ring of fixed-size entries
 * (stage 1b spec 6.1). The counters run freely in 64 bits; the capacity is a
 * power of two. Memory is allocated only by app_ringInit(). A full ring drops
 * the new entry and counts it (spec 11). The producer calls Push, Slot and
 * Commit; the consumer Peek and Pop; Reset only while neither side runs.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_RING_H_
#define APP_RING_H_

#include <stdalign.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

typedef struct app_ring
{
	alignas(64) _Atomic uint64_t head;		/**< entries pushed; written by the producer */
	alignas(64) _Atomic uint64_t tail;		/**< entries popped; written by the consumer */
	alignas(64) _Atomic uint64_t dropped;	/**< written by the producer */
	uint8_t *buf;
	size_t size;	/**< bytes per entry */
	uint32_t mask;	/**< capacity - 1 */
} app_ring_t;

int app_ringInit(app_ring_t *r, size_t entrySize, uint32_t capacity);	/**< 0, or -1 */
void app_ringFree(app_ring_t *r);
void app_ringReset(app_ring_t *r);
int app_ringPush(app_ring_t *r, const void *entry);		/**< 0, or -1 when full */
void *app_ringSlot(app_ring_t *r, uint32_t i);			/**< the i-th free slot, or NULL */
void app_ringCommit(app_ring_t *r, uint32_t n);
const void *app_ringPeek(app_ring_t *r, uint32_t i);	/**< the i-th pending entry, or NULL */
void app_ringPop(app_ring_t *r, uint32_t n);
uint32_t app_ringCount(app_ring_t *r);
uint64_t app_ringPopped(app_ring_t *r);
uint64_t app_ringDropped(app_ring_t *r);

#endif /* APP_RING_H_ */
