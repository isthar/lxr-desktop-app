/*
 * usb_midiTx.h
 *
 * USB MIDI output of the LICHOMAT firmware (task E2.8), the part without the HAL:
 * - usbtx_packet(): the USB-MIDI event packet (USB MIDI 1.0, section 4) of one
 *   MIDI message, on cable 0, with the code index number (CIN) of table 4-1;
 * - usbtx_ring_t: the packets that wait for the USB IN endpoint, and usbtx_next(),
 *   which hands the glue the next transfer once the endpoint is free, and keeps
 *   nothing for a host that is not there or has stopped reading.
 *
 * Plain C11 without HAL or CMSIS, so the host tests (tests/test_usbtx.c) build it
 * as well. The glue in usb_manager.c owns the ring, masks interrupts (PRIMASK)
 * around every call, passes HAL_GetTick() as the time, and starts the transfers.
 *
 * Packets
 * -------
 * - 8n..En, the channel messages: CIN = the high nibble of the status (8 Note Off,
 *   9 Note On, A poly pressure, B Control Change, C Program Change, D channel
 *   pressure, E pitch bend). C and D have one data byte.
 * - F1 MTC quarter frame and F3 Song Select: CIN 0x2 (two-byte system common).
 * - F2 Song Position Pointer: CIN 0x3 (three-byte system common).
 * - F6 Tune Request: CIN 0x5 (single-byte system common).
 * - F8..FF realtime, F0 and F7, the undefined F4 and F5, and 00..7F: CIN 0xF, the
 *   byte on its own. The MIDI parser routes SysEx one byte per message (F0, each
 *   payload byte in MidiMsg.status with bits.sysxbyte set, F7), so SysEx goes out
 *   byte by byte, which USB MIDI 1.0 allows for any MIDI byte.
 * - Unused bytes are 0, and data bytes are sent with bit 7 cleared.
 * fw 1.7 used the high nibble of the status for everything: F1, F2, F3 and F6 went
 * out as single bytes without their data, and SysEx payload bytes with CIN 0x0..0x7.
 *
 * Ring
 * ----
 * - write and read count the packets since the start; write - read is the number
 *   waiting, also across the 32-bit wrap. A zeroed ring is empty.
 * - A full ring rejects the new packet and counts it in dropped (spec 11, as for the
 *   sync queues). While a host reads, a transfer normally ends within a millisecond,
 *   so the ring fills only when more than 128 packets come at once, or within
 *   USBTX_STALL_MS after the host has stopped reading.
 * - While no host has configured the device, nothing waits for one: usbtx_next()
 *   empties the ring, and that is not counted.
 * - A host that has configured the device but stopped reading (some drivers read
 *   only while a program has the port open) leaves the transfer in flight. Once it
 *   has waited USBTX_STALL_MS, usbtx_next() treats the host as gone: it empties the
 *   ring, uncounted, at every call until the host takes that transfer. A host that
 *   reads again then gets the transfer that was in flight, as with fw 1.7, and no
 *   burst of old messages after it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */

#ifndef USB_MIDITX_H_
#define USB_MIDITX_H_

#include <stdint.h>
#include <stdbool.h>

#define USBTX_RING_SIZE             128u  /**< packets that can wait, a power of two */
#define USBTX_PACKETS_PER_TRANSFER   15u  /**< 60 bytes: one short packet of the 64-byte bulk
                                               endpoint, so a transfer needs no zero-length packet */
#define USBTX_STALL_MS              100u  /**< a transfer in flight for this long: the host has
                                               stopped reading */

/** one USB-MIDI event packet: cable number and CIN, then the three MIDI bytes */
typedef struct usbtx_packet
{
	uint8_t b[4];
} usbtx_packet_t;

_Static_assert(sizeof(usbtx_packet_t) == 4, "a USB-MIDI event packet is 4 bytes");

/** the packets waiting for the IN endpoint; all zero is an empty ring */
typedef struct usbtx_ring
{
	uint32_t		write;		/**< packets queued since the start */
	uint32_t		read;		/**< packets handed to a transfer since the start */
	uint32_t		dropped;	/**< packets rejected because the ring was full */
	uint32_t		sentAt;		/**< the time (ms) at which the last transfer was handed out */
	usbtx_packet_t	buf[USBTX_RING_SIZE];
} usbtx_ring_t;

//------------------------------------------------------------------------------
/** The USB-MIDI event packet of the MIDI message status, data1, data2 (see above).
 * Every status byte has a packet; data bytes the message does not have are 0. */
usbtx_packet_t usbtx_packet(uint8_t status, uint8_t data1, uint8_t data2);
//------------------------------------------------------------------------------
/** Queue a packet. false if the ring is full: the packet is dropped and counted. */
bool usbtx_push(usbtx_ring_t *r, usbtx_packet_t p);
//------------------------------------------------------------------------------
/** The next transfer, at the time now (a millisecond count that may wrap): copies up
 * to USBTX_PACKETS_PER_TRANSFER waiting packets, oldest first, into out and returns
 * how many; they no longer wait, and now is kept as the start of that transfer.
 * Returns 0 and keeps every packet while busy (the transfer handed out last is in
 * flight). Without a host (hostReady false), or while busy USBTX_STALL_MS or more
 * after the start of that transfer, the waiting packets are discarded and 0 is
 * returned. */
uint32_t usbtx_next(usbtx_ring_t *r, bool hostReady, bool busy, uint32_t now, usbtx_packet_t *out);
//------------------------------------------------------------------------------
/** the number of packets waiting */
static inline uint32_t usbtx_waiting(const usbtx_ring_t *r)
{
	return r->write - r->read;
}

#endif /* USB_MIDITX_H_ */
