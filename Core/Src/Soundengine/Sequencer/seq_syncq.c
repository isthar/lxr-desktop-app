/*
 * seq_syncq.c
 *
 * Sync input queues of the LICHOMAT sequencer, see seq_syncq.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */

#include "Soundengine/Sequencer/seq_syncq.h"
#include <stdatomic.h>

#define SEQQ_MASK		(SEQQ_SIZE - 1u)

/* MIDI status bytes, as in MidiMessages.h (which needs the HAL) */
#define SEQQ_MIDI_SPP		0xF2u
#define SEQQ_MIDI_CLOCK		0xF8u
#define SEQQ_MIDI_START		0xFAu
#define SEQQ_MIDI_CONTINUE	0xFBu
#define SEQQ_MIDI_STOP		0xFCu

/* USB MIDI code index numbers, as in usbd_cdc.h */
#define SEQQ_CIN_THREE_BYTE	0x03u	/**< three-byte system common message, e.g. SPP */
#define SEQQ_CIN_COMMON_1	0x05u	/**< single-byte system common or SysEx end; some hosts send realtime with it */
#define SEQQ_CIN_SINGLE_BYTE	0x0Fu	/**< single byte, e.g. realtime */

_Static_assert((SEQQ_SIZE & SEQQ_MASK) == 0, "SEQQ_SIZE must be a power of two");

//------------------------------------------------------------------------------
bool seqq_push(seqq_t *q, uint8_t type, uint16_t value, uint32_t stamp)
{
	const uint32_t w = q->write;
	const uint32_t next = (w + 1u) & SEQQ_MASK;

	if (next == q->read)
	{
		q->overflow++;
		return false;
	}
	q->buf[w] = (seqq_entry_t){ .type = type, .reserved = 0, .value = value, .stamp = stamp };
	atomic_signal_fence(memory_order_release);	/* the entry before the index */
	q->write = next;
	return true;
}
//------------------------------------------------------------------------------
/** a realtime status byte: the transport bytes and the clock become sync events */
static void seqq_realtime(seqq_t *q, uint8_t status, uint32_t stamp)
{
	switch (status)
	{
	case SEQQ_MIDI_CLOCK:
		seqq_push(q, SEQC_EV_CLOCK, 0, stamp);
		break;
	case SEQQ_MIDI_START:
		seqq_push(q, SEQC_EV_START, 0, stamp);
		break;
	case SEQQ_MIDI_CONTINUE:
		seqq_push(q, SEQC_EV_CONTINUE, 0, stamp);
		break;
	case SEQQ_MIDI_STOP:
		seqq_push(q, SEQC_EV_STOP, 0, stamp);
		break;
	default:			/* F9, FD, FE (active sensing), FF (reset) */
		break;
	}
}
//------------------------------------------------------------------------------
void seqq_dinByte(seqq_din_t *d, seqq_t *q, uint8_t byte, uint32_t stamp)
{
	if (byte >= SEQQ_MIDI_CLOCK)
	{
		/* realtime bytes may come anywhere, also inside an SPP */
		seqq_realtime(q, byte, stamp);
		return;
	}
	if (byte & 0x80u)
	{
		/* any other status byte starts a new message */
		d->state = byte == SEQQ_MIDI_SPP ? 1u : 0u;
		return;
	}
	if (d->state == 1u)
	{
		d->lsb = byte;
		d->state = 2u;
	}
	else if (d->state == 2u)
	{
		seqq_push(q, SEQQ_SONG_POSITION, (uint16_t)(d->lsb | ((unsigned)byte << 7)), stamp);
		d->state = 0u;
	}
}
//------------------------------------------------------------------------------
void seqq_usbMessage(seqq_t *q, uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2, uint32_t stamp)
{
	if ((cin & 0xF0u) != 0)
		return;		/* cable 0 only, as USBD_CDC_DataOut() */

	const uint8_t code = cin & 0x0Fu;
	if (code == SEQQ_CIN_SINGLE_BYTE || (code == SEQQ_CIN_COMMON_1 && status >= SEQQ_MIDI_CLOCK))
		seqq_realtime(q, status, stamp);	/* N18.4: realtime with CIN 0x5 as well */
	else if (code == SEQQ_CIN_THREE_BYTE && status == SEQQ_MIDI_SPP)
		seqq_push(q, SEQQ_SONG_POSITION, (uint16_t)((data1 & 0x7Fu) | ((unsigned)(data2 & 0x7Fu) << 7)), stamp);
}
//------------------------------------------------------------------------------
bool seqq_peek(const seqq_t *q, seqq_entry_t *out)
{
	const uint32_t r = q->read;

	if (r == q->write)
		return false;
	atomic_signal_fence(memory_order_acquire);	/* the index before the entry */
	*out = q->buf[r];
	return true;
}
//------------------------------------------------------------------------------
void seqq_drop(seqq_t *q)
{
	const uint32_t r = q->read;

	if (r == q->write)
		return;
	atomic_signal_fence(memory_order_release);	/* the entry was read before the slot is freed */
	q->read = (r + 1u) & SEQQ_MASK;
}
//------------------------------------------------------------------------------
uint64_t seqq_unwrap(uint64_t now, uint32_t stamp)
{
	const uint32_t age = (uint32_t)now - stamp;	/* cycles from the stamp to now, modulo 2^32 */

	if (age > 0x7FFFFFFFu)
		return now;				/* stamped after now */
	return age > now ? 0 : now - age;
}
//------------------------------------------------------------------------------
bool seqq_popEarliest(seqq_t *const queues[], unsigned count, uint64_t now,
		unsigned *source, seqq_entry_t *entry, uint64_t *time)
{
	unsigned best = count;
	uint64_t bestTime = 0;

	for (unsigned i = 0; i < count; i++)
	{
		seqq_entry_t e;
		if (!seqq_peek(queues[i], &e))
			continue;
		const uint64_t t = seqq_unwrap(now, e.stamp);
		if (best == count || t < bestTime)
		{
			best = i;
			bestTime = t;
			*entry = e;
		}
	}
	if (best == count)
		return false;
	seqq_drop(queues[best]);
	*source = best;
	*time = bestTime;
	return true;
}
