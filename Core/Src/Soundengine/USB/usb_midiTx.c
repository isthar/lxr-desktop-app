/*
 * usb_midiTx.c
 *
 * USB MIDI output without the HAL: event packets and the transmit ring
 * (usb_midiTx.h, task E2.8).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */

#include "Soundengine/USB/usb_midiTx.h"

/* code index numbers, USB MIDI 1.0 table 4-1 */
#define USBTX_CIN_TWO_BYTE_COMMON    0x2u  /**< two-byte system common: F1, F3 */
#define USBTX_CIN_THREE_BYTE_COMMON  0x3u  /**< three-byte system common: F2 */
#define USBTX_CIN_ONE_BYTE_COMMON    0x5u  /**< single-byte system common: F6 */
#define USBTX_CIN_SINGLE_BYTE        0xFu  /**< one MIDI byte as it is */

//------------------------------------------------------------------------------
usbtx_packet_t usbtx_packet(uint8_t status, uint8_t data1, uint8_t data2)
{
	usbtx_packet_t p = { { USBTX_CIN_SINGLE_BYTE, status, 0, 0 } };

	data1 &= 0x7Fu;
	data2 &= 0x7Fu;
	if (status >= 0x80u && status < 0xF0u)
	{
		/* channel message: the CIN is the message type; C and D have one data byte */
		p.b[0] = (uint8_t)(status >> 4);
		p.b[2] = data1;
		if (status < 0xC0u || status >= 0xE0u)
			p.b[3] = data2;
		return p;
	}
	switch (status)
	{
	case 0xF1u:	/* MTC quarter frame */
	case 0xF3u:	/* Song Select */
		p.b[0] = USBTX_CIN_TWO_BYTE_COMMON;
		p.b[2] = data1;
		break;
	case 0xF2u:	/* Song Position Pointer */
		p.b[0] = USBTX_CIN_THREE_BYTE_COMMON;
		p.b[2] = data1;
		p.b[3] = data2;
		break;
	case 0xF6u:	/* Tune Request */
		p.b[0] = USBTX_CIN_ONE_BYTE_COMMON;
		break;
	default:	/* realtime, F0, F4, F5, F7, a SysEx payload byte: the byte on its own */
		break;
	}
	return p;
}
//------------------------------------------------------------------------------
bool usbtx_push(usbtx_ring_t *r, usbtx_packet_t p)
{
	if (r->write - r->read >= USBTX_RING_SIZE)
	{
		r->dropped++;
		return false;
	}
	r->buf[r->write & (USBTX_RING_SIZE - 1u)] = p;
	r->write++;
	return true;
}
//------------------------------------------------------------------------------
uint32_t usbtx_next(usbtx_ring_t *r, bool hostReady, bool busy, uint32_t now, usbtx_packet_t *out)
{
	/* no host, or one that has not taken the transfer in flight for USBTX_STALL_MS
	 * and so has stopped reading: nothing waits for it */
	if (!hostReady || (busy && now - r->sentAt >= USBTX_STALL_MS))
	{
		r->read = r->write;
		return 0;
	}
	if (busy)
		return 0;

	uint32_t n = r->write - r->read;
	if (n > USBTX_PACKETS_PER_TRANSFER)
		n = USBTX_PACKETS_PER_TRANSFER;
	for (uint32_t i = 0; i < n; i++)
		out[i] = r->buf[(r->read + i) & (USBTX_RING_SIZE - 1u)];
	r->read += n;
	if (n != 0)
		r->sentAt = now;
	return n;
}
