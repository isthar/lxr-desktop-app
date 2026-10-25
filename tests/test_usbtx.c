/*
 * test_usbtx.c
 *
 * Host tests of the USB MIDI output (Core/Src/Soundengine/USB/usb_midiTx.c, task
 * E2.8): the USB-MIDI event packet and its code index number (CIN) for every
 * message the firmware sends, and the transmit ring: order, transfer size, a busy
 * endpoint, a missing host, a host that has stopped reading, a full ring and a
 * long stream that wraps the ring.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <string.h>
#include "Soundengine/USB/usb_midiTx.h"
#include "seqc_test.h"

/** the four bytes of a packet as 0xAABBCCDD, in the order they go to the host */
static uint32_t bytesOf(usbtx_packet_t p)
{
	return ((uint32_t)p.b[0] << 24) | ((uint32_t)p.b[1] << 16) | ((uint32_t)p.b[2] << 8) | p.b[3];
}

TEST(usbtx_channel_messages)
{
	ASSERT_EQ(bytesOf(usbtx_packet(0x93, 0x3C, 0x64)), 0x09933C64);	/* note on, channel 4 */
	ASSERT_EQ(bytesOf(usbtx_packet(0x80, 0x24, 0x00)), 0x08802400);	/* note off */
	ASSERT_EQ(bytesOf(usbtx_packet(0xA5, 0x30, 0x10)), 0x0AA53010);	/* poly pressure */
	ASSERT_EQ(bytesOf(usbtx_packet(0xB0, 0x07, 0x64)), 0x0BB00764);	/* control change */
	ASSERT_EQ(bytesOf(usbtx_packet(0xE1, 0x00, 0x40)), 0x0EE10040);	/* pitch bend */
}

TEST(usbtx_one_data_byte)
{
	/* program change and channel pressure have one data byte; the unused byte is 0,
	 * whatever the message held there (fw 1.7 sent it as it was) */
	ASSERT_EQ(bytesOf(usbtx_packet(0xC9, 0x05, 0x7F)), 0x0CC90500);
	ASSERT_EQ(bytesOf(usbtx_packet(0xD2, 0x40, 0x11)), 0x0DD24000);
}

TEST(usbtx_system_common)
{
	/* fw 1.7 sent these with CIN 0xF, a single byte: the host lost the data bytes */
	ASSERT_EQ(bytesOf(usbtx_packet(0xF1, 0x23, 0x55)), 0x02F12300);	/* MTC quarter frame */
	ASSERT_EQ(bytesOf(usbtx_packet(0xF2, 0x10, 0x02)), 0x03F21002);	/* Song Position Pointer */
	ASSERT_EQ(bytesOf(usbtx_packet(0xF3, 0x05, 0x66)), 0x02F30500);	/* Song Select */
	ASSERT_EQ(bytesOf(usbtx_packet(0xF6, 0x12, 0x34)), 0x05F60000);	/* Tune Request */
}

TEST(usbtx_realtime_single_byte)
{
	const uint8_t realtime[] = { 0xF8, 0xFA, 0xFB, 0xFC, 0xFE, 0xFF };
	for (unsigned i = 0; i < sizeof realtime; i++)
		ASSERT_EQ(bytesOf(usbtx_packet(realtime[i], 0x12, 0x34)), 0x0F000000u | ((uint32_t)realtime[i] << 16));
}

TEST(usbtx_sysex_and_undefined_single_byte)
{
	/* the DIN parser routes SysEx one byte per message: F0, each payload byte (in
	 * status, with MidiMsg.bits.sysxbyte set), F7. fw 1.7 sent payload byte 0x43 with
	 * CIN 0x4 and two stale bytes, and 0x05 with the reserved CIN 0x0. */
	ASSERT_EQ(bytesOf(usbtx_packet(0xF0, 0x11, 0x22)), 0x0FF00000);
	ASSERT_EQ(bytesOf(usbtx_packet(0x43, 0x11, 0x22)), 0x0F430000);
	ASSERT_EQ(bytesOf(usbtx_packet(0x05, 0x11, 0x22)), 0x0F050000);
	ASSERT_EQ(bytesOf(usbtx_packet(0x7F, 0x11, 0x22)), 0x0F7F0000);
	ASSERT_EQ(bytesOf(usbtx_packet(0xF7, 0x11, 0x22)), 0x0FF70000);
	ASSERT_EQ(bytesOf(usbtx_packet(0xF4, 0x11, 0x22)), 0x0FF40000);	/* undefined */
	ASSERT_EQ(bytesOf(usbtx_packet(0xF5, 0x11, 0x22)), 0x0FF50000);	/* undefined */
}

TEST(usbtx_data_bytes_seven_bit)
{
	ASSERT_EQ(bytesOf(usbtx_packet(0x90, 0xBC, 0xE4)), 0x09903C64);
	ASSERT_EQ(bytesOf(usbtx_packet(0xF2, 0xFF, 0x80)), 0x03F27F00);
}

TEST(usbtx_cin_of_every_status)
{
	/* cable 0, the byte itself in MIDI_0, and a CIN of USB MIDI 1.0 table 4-1 that
	 * fits the byte: never the reserved 0x0 and 0x1, never a SysEx CIN 0x4, 0x6, 0x7.
	 * The packet carries as many data bytes as the table gives that CIN, and 0 in
	 * the others. */
	for (unsigned s = 0; s < 256; s++)
	{
		const usbtx_packet_t p = usbtx_packet((uint8_t)s, 0x55, 0x2A);
		ASSERT_EQ(p.b[0] >> 4, 0);
		ASSERT_EQ(p.b[1], s);
		const unsigned nData =
			((s >= 0x80 && s < 0xC0) || (s >= 0xE0 && s < 0xF0) || s == 0xF2) ? 2u :
			((s >= 0xC0 && s < 0xE0) || s == 0xF1 || s == 0xF3) ? 1u : 0u;
		ASSERT_EQ(p.b[2], nData >= 1 ? 0x55u : 0u);
		ASSERT_EQ(p.b[3], nData == 2 ? 0x2Au : 0u);
		const unsigned cin = p.b[0] & 0x0Fu;
		if (s >= 0x80 && s < 0xF0)
			ASSERT_EQ(cin, s >> 4);
		else if (s == 0xF1 || s == 0xF3)
			ASSERT_EQ(cin, 0x2);
		else if (s == 0xF2)
			ASSERT_EQ(cin, 0x3);
		else if (s == 0xF6)
			ASSERT_EQ(cin, 0x5);
		else
			ASSERT_EQ(cin, 0xF);
	}
}

//------------------------------------------------------------------------------
// The transmit ring. The time passed to usbtx_next() is 0 unless a test is about
// a transfer that the host does not take.
//------------------------------------------------------------------------------
static usbtx_ring_t ring;
static usbtx_packet_t xfer[USBTX_PACKETS_PER_TRANSFER];

/** packet number n of a test stream: a Note On that carries n in its two data bytes */
static usbtx_packet_t numbered(uint32_t n)
{
	return usbtx_packet(0x90, (uint8_t)(n & 0x7Fu), (uint8_t)((n >> 7) & 0x7Fu));
}

static uint32_t numberOf(usbtx_packet_t p)
{
	return (uint32_t)p.b[2] | ((uint32_t)p.b[3] << 7);
}

TEST(usbtx_ring_order)
{
	memset(&ring, 0, sizeof ring);
	ASSERT_EQ(usbtx_next(&ring, true, false, 0, xfer), 0);	/* nothing waits: no transfer */
	for (uint32_t i = 0; i < 3; i++)
		ASSERT_TRUE(usbtx_push(&ring, numbered(i)));
	ASSERT_EQ(usbtx_waiting(&ring), 3);
	ASSERT_EQ(usbtx_next(&ring, true, false, 0, xfer), 3);
	for (uint32_t i = 0; i < 3; i++)
		ASSERT_EQ(numberOf(xfer[i]), i);
	ASSERT_EQ(usbtx_waiting(&ring), 0);
	ASSERT_EQ(usbtx_next(&ring, true, false, 0, xfer), 0);
}

TEST(usbtx_transfer_size)
{
	/* A transfer takes at most 15 packets, 60 bytes: one short packet of the 64-byte
	 * endpoint, so it never needs a zero-length packet. fw 1.7 sent everything from
	 * its read pointer to its write pointer, and 128 bytes more once the write pointer
	 * had wrapped. */
	ASSERT_EQ(USBTX_PACKETS_PER_TRANSFER * sizeof(usbtx_packet_t), 60);
	memset(&ring, 0, sizeof ring);
	for (uint32_t i = 0; i < 40; i++)
		ASSERT_TRUE(usbtx_push(&ring, numbered(i)));
	ASSERT_EQ(usbtx_next(&ring, true, false, 0, xfer), 15);
	ASSERT_EQ(numberOf(xfer[0]), 0);
	ASSERT_EQ(numberOf(xfer[14]), 14);
	ASSERT_EQ(usbtx_next(&ring, true, false, 0, xfer), 15);
	ASSERT_EQ(numberOf(xfer[0]), 15);
	ASSERT_EQ(usbtx_next(&ring, true, false, 0, xfer), 10);
	ASSERT_EQ(numberOf(xfer[9]), 39);
	ASSERT_EQ(usbtx_waiting(&ring), 0);
}

TEST(usbtx_busy_endpoint_keeps_packets)
{
	/* fw 1.7 moved its read pointer on also when the endpoint was busy: lost */
	memset(&ring, 0, sizeof ring);
	ASSERT_TRUE(usbtx_push(&ring, numbered(1)));
	ASSERT_TRUE(usbtx_push(&ring, numbered(2)));
	ASSERT_EQ(usbtx_next(&ring, true, true, 0, xfer), 0);
	ASSERT_EQ(usbtx_waiting(&ring), 2);
	ASSERT_EQ(usbtx_next(&ring, true, false, 0, xfer), 2);
	ASSERT_EQ(numberOf(xfer[0]), 1);
	ASSERT_EQ(numberOf(xfer[1]), 2);
}

TEST(usbtx_no_host_keeps_nothing)
{
	memset(&ring, 0, sizeof ring);
	for (uint32_t i = 0; i < 5; i++)
		ASSERT_TRUE(usbtx_push(&ring, numbered(i)));
	ASSERT_EQ(usbtx_next(&ring, false, false, 0, xfer), 0);
	ASSERT_EQ(usbtx_waiting(&ring), 0);	/* nothing is kept for a host that is not there */
	ASSERT_EQ(ring.dropped, 0);		/* and that is not an overflow */
	ASSERT_TRUE(usbtx_push(&ring, numbered(7)));
	ASSERT_EQ(usbtx_next(&ring, true, false, 0, xfer), 1);
	ASSERT_EQ(numberOf(xfer[0]), 7);
}

TEST(usbtx_stalled_host_keeps_nothing)
{
	/* A host that has stopped reading leaves the transfer in flight (busy). For
	 * USBTX_STALL_MS the packets wait for it; after that they are discarded, as
	 * without a host, at every call until it takes the transfer, so a host that reads
	 * again gets no burst of old messages. The transfer starts 64 ms before the
	 * millisecond count wraps. */
	const uint32_t start = 0xFFFFFFC0u;
	memset(&ring, 0, sizeof ring);
	ASSERT_TRUE(usbtx_push(&ring, numbered(0)));
	ASSERT_EQ(usbtx_next(&ring, true, false, start, xfer), 1);
	for (uint32_t i = 1; i <= 3; i++)
		ASSERT_TRUE(usbtx_push(&ring, numbered(i)));
	ASSERT_EQ(usbtx_next(&ring, true, true, start + 1u, xfer), 0);
	ASSERT_EQ(usbtx_waiting(&ring), 3);		/* before the wrap: the packets wait */
	ASSERT_EQ(usbtx_next(&ring, true, true, start + USBTX_STALL_MS - 1u, xfer), 0);
	ASSERT_EQ(usbtx_waiting(&ring), 3);		/* a slow host: the packets wait */
	ASSERT_EQ(usbtx_next(&ring, true, true, start + USBTX_STALL_MS, xfer), 0);
	ASSERT_EQ(usbtx_waiting(&ring), 0);		/* it has stopped reading: discarded */
	ASSERT_TRUE(usbtx_push(&ring, numbered(9)));
	ASSERT_EQ(usbtx_next(&ring, true, true, start + USBTX_STALL_MS + 1u, xfer), 0);
	ASSERT_EQ(usbtx_waiting(&ring), 0);		/* and at every call after that */
	ASSERT_TRUE(usbtx_push(&ring, numbered(4)));
	ASSERT_EQ(usbtx_next(&ring, true, true, start + 5000u, xfer), 0);
	ASSERT_EQ(usbtx_waiting(&ring), 0);		/* nothing waits while it does not read */
	ASSERT_EQ(ring.dropped, 0);			/* and that is not an overflow */
	ASSERT_TRUE(usbtx_push(&ring, numbered(5)));
	ASSERT_EQ(usbtx_next(&ring, true, false, start + 6000u, xfer), 1);	/* it took the transfer */
	ASSERT_EQ(numberOf(xfer[0]), 5);
	ASSERT_TRUE(usbtx_push(&ring, numbered(6)));
	ASSERT_EQ(usbtx_next(&ring, true, true, start + 6000u + USBTX_STALL_MS - 1u, xfer), 0);
	ASSERT_EQ(usbtx_waiting(&ring), 1);		/* the new transfer counts from its own start */
}

TEST(usbtx_full_ring_drops_new_packets)
{
	memset(&ring, 0, sizeof ring);
	for (uint32_t i = 0; i < USBTX_RING_SIZE; i++)
		ASSERT_TRUE(usbtx_push(&ring, numbered(i)));
	ASSERT_FALSE(usbtx_push(&ring, numbered(1000)));
	ASSERT_FALSE(usbtx_push(&ring, numbered(1001)));
	ASSERT_EQ(ring.dropped, 2);
	ASSERT_EQ(usbtx_waiting(&ring), USBTX_RING_SIZE);
	ASSERT_EQ(usbtx_next(&ring, true, false, 0, xfer), USBTX_PACKETS_PER_TRANSFER);
	ASSERT_EQ(numberOf(xfer[0]), 0);		/* the waiting packets stay, the new ones went */
	ASSERT_TRUE(usbtx_push(&ring, numbered(2000)));	/* a transfer made room */
	uint32_t expect = USBTX_PACKETS_PER_TRANSFER, n;
	while ((n = usbtx_next(&ring, true, false, 0, xfer)) != 0)
		for (uint32_t i = 0; i < n; i++, expect++)
			ASSERT_EQ(numberOf(xfer[i]), expect < USBTX_RING_SIZE ? expect : 2000);
	ASSERT_EQ(expect, USBTX_RING_SIZE + 1);
	ASSERT_EQ(ring.dropped, 2);
}

TEST(usbtx_counters_wrap)
{
	memset(&ring, 0, sizeof ring);
	ring.write = ring.read = 0xFFFFFFF0u;	/* 16 packets before the 32-bit wrap */
	uint32_t pushed = 0, taken = 0, n;
	for (int round = 0; round < 10; round++)
	{
		for (int i = 0; i < 7; i++)
			ASSERT_TRUE(usbtx_push(&ring, numbered(pushed++)));
		while ((n = usbtx_next(&ring, true, false, 0, xfer)) != 0)
			for (uint32_t i = 0; i < n; i++)
				ASSERT_EQ(numberOf(xfer[i]), taken++);
	}
	ASSERT_EQ(taken, 70);
	ASSERT_EQ(ring.write, 70u - 16u);	/* it did wrap */
	ASSERT_EQ(ring.dropped, 0);
}

#define STREAM 10000u
static uint32_t accepted[STREAM], received[STREAM];

TEST(usbtx_long_stream_wraps_ring)
{
	/* 10000 packets in bursts of 0 to 40, with the endpoint busy at random: a push
	 * fails exactly when 128 packets wait, and the host receives every accepted packet
	 * once and in order, at most 15 per transfer */
	memset(&ring, 0, sizeof ring);
	uint32_t seed = 12345, sent = 0, nAccepted = 0, nReceived = 0, n;
	while (sent < STREAM)
	{
		seed = seed * 1103515245u + 12345u;
		uint32_t burst = (seed >> 16) % 41u;
		for (; burst > 0 && sent < STREAM; burst--, sent++)
		{
			const bool full = usbtx_waiting(&ring) == USBTX_RING_SIZE;
			ASSERT_EQ(usbtx_push(&ring, numbered(sent)), !full);
			if (!full)
				accepted[nAccepted++] = sent;
		}
		seed = seed * 1103515245u + 12345u;
		const bool busy = ((seed >> 16) & 3u) == 0;	/* busy one time in four */
		const uint32_t before = usbtx_waiting(&ring);
		n = usbtx_next(&ring, true, busy, 0, xfer);
		ASSERT_TRUE(n <= USBTX_PACKETS_PER_TRANSFER);
		ASSERT_EQ(n, busy ? 0 : (before < USBTX_PACKETS_PER_TRANSFER ? before : USBTX_PACKETS_PER_TRANSFER));
		for (uint32_t i = 0; i < n; i++)
			received[nReceived++] = numberOf(xfer[i]);
	}
	while ((n = usbtx_next(&ring, true, false, 0, xfer)) != 0)
		for (uint32_t i = 0; i < n; i++)
			received[nReceived++] = numberOf(xfer[i]);
	ASSERT_EQ(nReceived, nAccepted);
	ASSERT_EQ(ring.dropped, STREAM - nAccepted);
	ASSERT_TRUE(ring.dropped > 0);		/* the bursts did fill the ring */
	ASSERT_TRUE(ring.write > 20u * USBTX_RING_SIZE);	/* and the stream wrapped it many times */
	for (uint32_t i = 0; i < nReceived; i++)
		ASSERT_EQ(received[i], accepted[i]);
}

TEST_MAIN()
