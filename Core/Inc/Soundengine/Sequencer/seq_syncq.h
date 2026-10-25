/*
 * seq_syncq.h
 *
 * Sync input queues of the LICHOMAT sequencer (spec 4.3, 4.4, 7.2):
 * - one single-producer, single-consumer (SPSC) queue of timestamped sync events
 *   per source: MIDI DIN, USB MIDI and the clock jacks;
 * - the decoding of received MIDI bytes and USB MIDI messages into queue entries:
 *   the realtime messages F8, FA, FB, FC and the Song Position Pointer F2;
 * - the 32-bit stamp unwrap and the merge of the queues in stamp order;
 * - the 64-bit extension of the 32-bit cycle counter.
 *
 * Plain C11 without HAL or CMSIS, so the host tests (tests/test_syncq.c) build it
 * as well. The firmware instances, the DWT time base and the entry points for the
 * interrupt handlers are in seq_platform.c.
 *
 * Producer and consumer
 * ---------------------
 * - Each queue has one producer, the interrupt handler of its source, and one
 *   consumer, seq_tick().
 * - The producer writes the entry, then the write index. The consumer reads the
 *   entry, then advances the read index. Both indices are aligned 32-bit words, so
 *   one store publishes them on the Cortex-M7. The interrupt runs on the same core,
 *   like a signal handler in a thread, so atomic_signal_fence() is the barrier
 *   that keeps the compiler from moving the entry accesses across the index
 *   accesses.
 * - A full queue rejects the new entry and counts it in overflow. One slot always
 *   stays free, so a queue holds SEQQ_SIZE - 1 entries.
 * - A zeroed queue is empty, so the queues need no initialisation.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */

#ifndef SEQ_SYNCQ_H_
#define SEQ_SYNCQ_H_

#include <stdint.h>
#include <stdbool.h>
#include "Soundengine/Sequencer/seq_core.h"

#define SEQQ_SIZE              64u   /**< entries per queue, a power of two */
#define SEQQ_SONG_POSITION   0x80u   /**< entry type of a Song Position Pointer; value = sixteenths */

/** one queue entry, 8 bytes */
typedef struct seqq_entry
{
	uint8_t		type;		/**< a seqc_sync_ev_t, or SEQQ_SONG_POSITION */
	uint8_t		reserved;	/**< 0 */
	uint16_t	value;		/**< SEQQ_SONG_POSITION: sixteenths, 0..16383; otherwise 0 */
	uint32_t	stamp;		/**< DWT->CYCCNT when the interrupt handler received it */
} seqq_entry_t;

_Static_assert(sizeof(seqq_entry_t) == 8, "a sync queue entry is 8 bytes (spec 10)");

/** an SPSC queue; all zero is an empty queue */
typedef struct seqq
{
	volatile uint32_t	write;		/**< next slot the producer fills */
	volatile uint32_t	read;		/**< next slot the consumer takes */
	volatile uint32_t	overflow;	/**< entries rejected because the queue was full */
	seqq_entry_t		buf[SEQQ_SIZE];
} seqq_t;

/** DIN decoder state for the three bytes of a Song Position Pointer; all zero is idle */
typedef struct seqq_din
{
	uint8_t state;		/**< 0 idle, 1 after F2, 2 after the LSB */
	uint8_t lsb;
} seqq_din_t;

//------------------------------------------------------------------------------
// Producer side: only the interrupt handler of the queue's source
//------------------------------------------------------------------------------
/** append an entry; false if the queue is full (counted in q->overflow) */
bool seqq_push(seqq_t *q, uint8_t type, uint16_t value, uint32_t stamp);
//------------------------------------------------------------------------------
/** One byte received on MIDI DIN. F8, FA, FB and FC become SEQC_EV_CLOCK,
 * SEQC_EV_START, SEQC_EV_CONTINUE and SEQC_EV_STOP, also between the data bytes of
 * another message. A complete F2 <lsb> <msb> becomes SEQQ_SONG_POSITION, stamped
 * with its last byte; any other status byte cancels an incomplete one. All other
 * bytes are ignored here: the MIDI parser gets every byte through the RX FIFO. */
void seqq_dinByte(seqq_din_t *d, seqq_t *q, uint8_t byte, uint32_t stamp);
//------------------------------------------------------------------------------
/** One USB MIDI event packet (cable number and code index in cin). On cable 0, F8,
 * FA, FB or FC with CIN 0xF (single byte) becomes the sync event, and so does the
 * same byte with CIN 0x5, which some hosts send for realtime (N18.4). CIN 0x3 with
 * F2 becomes SEQQ_SONG_POSITION. Everything else is ignored here. */
void seqq_usbMessage(seqq_t *q, uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2, uint32_t stamp);

//------------------------------------------------------------------------------
// Consumer side: only seq_tick()
//------------------------------------------------------------------------------
/** copy the oldest entry into out; false if the queue is empty */
bool seqq_peek(const seqq_t *q, seqq_entry_t *out);
//------------------------------------------------------------------------------
/** remove the oldest entry; nothing if the queue is empty */
void seqq_drop(seqq_t *q);
//------------------------------------------------------------------------------
/** The 64-bit time of a 32-bit stamp, given the 64-bit time now of the same counter.
 * A stamp up to 2^31 cycles (9.9 s at 216 MHz) old unwraps exactly. A stamp later
 * than now, possible through preemption, counts as now (spec 4.4). A stamp before
 * the time origin counts as 0. */
uint64_t seqq_unwrap(uint64_t now, uint32_t stamp);
//------------------------------------------------------------------------------
/** Remove and return the entry with the earliest stamp of all count queues. On
 * equal stamps the queue with the lower index comes first. Sets *source to that
 * index, *entry to the entry and *time to its unwrapped stamp. false if all queues
 * are empty. */
bool seqq_popEarliest(seqq_t *const queues[], unsigned count, uint64_t now,
		unsigned *source, seqq_entry_t *entry, uint64_t *time);
//------------------------------------------------------------------------------
/** Extend a free-running 32-bit counter to 64 bits: *time advances by the counts
 * since *last, and *last becomes cycles. Correct while it is called at least once
 * per 2^32 counts (19.9 s at 216 MHz). */
static inline uint64_t seqq_extend(uint64_t *time, uint32_t *last, uint32_t cycles)
{
	*time += (uint32_t)(cycles - *last);
	*last = cycles;
	return *time;
}

#endif /* SEQ_SYNCQ_H_ */
