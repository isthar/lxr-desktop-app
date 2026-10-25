/*
 * test_syncq.c
 *
 * Host tests of the sync input queues (Core/Src/Soundengine/Sequencer/seq_syncq.c,
 * task E2.1): the SPSC queue, the stamp unwrap, the merge of the queues in stamp
 * order, the 64-bit counter extension, and the decoding of MIDI DIN bytes and USB
 * MIDI packets into sync events and song positions.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <string.h>
#include "Soundengine/Sequencer/seq_syncq.h"
#include "seqc_test.h"

static seqq_t qa, qb, qc;

static void reset(void)
{
	memset(&qa, 0, sizeof qa);
	memset(&qb, 0, sizeof qb);
	memset(&qc, 0, sizeof qc);
}

/** take every entry of q, oldest first; returns how many there were */
static int takeAll(seqq_t *q, seqq_entry_t *out, int max)
{
	int n = 0;
	seqq_entry_t e;
	while (n < max && seqq_peek(q, &e))
	{
		out[n++] = e;
		seqq_drop(q);
	}
	return n;
}

TEST(syncq_fifo)
{
	reset();
	seqq_entry_t e;
	ASSERT_FALSE(seqq_peek(&qa, &e));
	ASSERT_TRUE(seqq_push(&qa, SEQC_EV_START, 0, 100));
	ASSERT_TRUE(seqq_push(&qa, SEQC_EV_CLOCK, 0, 200));
	ASSERT_TRUE(seqq_peek(&qa, &e));
	ASSERT_EQ(e.type, SEQC_EV_START);
	ASSERT_EQ(e.stamp, 100);
	ASSERT_TRUE(seqq_peek(&qa, &e));	/* peek does not remove */
	ASSERT_EQ(e.type, SEQC_EV_START);
	seqq_drop(&qa);
	ASSERT_TRUE(seqq_peek(&qa, &e));
	ASSERT_EQ(e.type, SEQC_EV_CLOCK);
	ASSERT_EQ(e.stamp, 200);
	ASSERT_EQ(e.value, 0);
	ASSERT_EQ(e.reserved, 0);
	seqq_drop(&qa);
	ASSERT_FALSE(seqq_peek(&qa, &e));
	seqq_drop(&qa);				/* dropping from an empty queue does nothing */
	ASSERT_EQ(qa.read, qa.write);
	ASSERT_EQ(qa.overflow, 0);
}

TEST(syncq_full)
{
	reset();
	seqq_entry_t e;
	for (uint32_t i = 0; i < SEQQ_SIZE - 1u; i++)
		ASSERT_TRUE(seqq_push(&qa, SEQC_EV_CLOCK, 0, i));
	ASSERT_FALSE(seqq_push(&qa, SEQC_EV_CLOCK, 0, 999));	/* one slot stays free */
	ASSERT_EQ(qa.overflow, 1);
	ASSERT_TRUE(seqq_peek(&qa, &e));
	ASSERT_EQ(e.stamp, 0);
	seqq_drop(&qa);
	ASSERT_TRUE(seqq_push(&qa, SEQC_EV_CLOCK, 0, 1000));
	/* the order survives the wrap of the indices */
	for (uint32_t i = 1; i < SEQQ_SIZE - 1u; i++)
	{
		ASSERT_TRUE(seqq_peek(&qa, &e));
		ASSERT_EQ(e.stamp, i);
		seqq_drop(&qa);
	}
	ASSERT_TRUE(seqq_peek(&qa, &e));
	ASSERT_EQ(e.stamp, 1000);
	seqq_drop(&qa);
	ASSERT_FALSE(seqq_peek(&qa, &e));
	ASSERT_EQ(qa.overflow, 1);
}

TEST(syncq_unwrap)
{
	ASSERT_EQ(seqq_unwrap(1000, 900), 900);
	ASSERT_EQ(seqq_unwrap(1000, 1000), 1000);
	ASSERT_EQ(seqq_unwrap(1000, 1100), 1000);			/* stamped after now: now */
	ASSERT_EQ(seqq_unwrap(0x100000010ull, 0xFFFFFFF0u), 0xFFFFFFF0ull);	/* across a counter wrap */
	ASSERT_EQ(seqq_unwrap(0x300000005ull, 0x00000001u), 0x300000001ull);
	ASSERT_EQ(seqq_unwrap(0x180000000ull, 0x00000001u), 0x100000001ull);	/* 2^31 - 1 cycles old */
	ASSERT_EQ(seqq_unwrap(50, 0xFFFFFF00u), 0);			/* before the time origin: 0 */
}

TEST(syncq_merge)
{
	reset();
	seqq_t *const qs[3] = { &qa, &qb, &qc };
	ASSERT_TRUE(seqq_push(&qb, SEQC_EV_CLOCK, 0, 300));
	ASSERT_TRUE(seqq_push(&qa, SEQC_EV_CLOCK, 0, 100));
	ASSERT_TRUE(seqq_push(&qc, SEQC_EV_PULSE, 0, 200));
	ASSERT_TRUE(seqq_push(&qa, SEQC_EV_STOP, 0, 400));
	ASSERT_TRUE(seqq_push(&qb, SEQC_EV_START, 0, 400));	/* the same stamp as the DIN stop */
	const uint32_t stamps[5] = { 100, 200, 300, 400, 400 };
	const unsigned sources[5] = { 0, 2, 1, 0, 1 };		/* equal stamps: lower index first */
	const uint8_t types[5] = { SEQC_EV_CLOCK, SEQC_EV_PULSE, SEQC_EV_CLOCK, SEQC_EV_STOP, SEQC_EV_START };
	for (int i = 0; i < 5; i++)
	{
		unsigned src = 9;
		seqq_entry_t e;
		uint64_t t = 0;
		ASSERT_TRUE(seqq_popEarliest(qs, 3, 1000, &src, &e, &t));
		ASSERT_EQ(src, sources[i]);
		ASSERT_EQ(t, stamps[i]);
		ASSERT_EQ(e.stamp, stamps[i]);
		ASSERT_EQ(e.type, types[i]);
	}
	unsigned src;
	seqq_entry_t e;
	uint64_t t;
	ASSERT_FALSE(seqq_popEarliest(qs, 3, 1000, &src, &e, &t));
}

TEST(syncq_merge_wrap)
{
	reset();
	seqq_t *const qs[3] = { &qa, &qb, &qc };
	ASSERT_TRUE(seqq_push(&qb, SEQC_EV_CLOCK, 0, 0x00000050u));	/* after the counter wrapped */
	ASSERT_TRUE(seqq_push(&qa, SEQC_EV_CLOCK, 0, 0xFFFFFF00u));	/* before the wrap: earlier */
	unsigned src;
	seqq_entry_t e;
	uint64_t t;
	ASSERT_TRUE(seqq_popEarliest(qs, 3, 0x100000100ull, &src, &e, &t));
	ASSERT_EQ(src, 0);
	ASSERT_EQ(t, 0xFFFFFF00ull);
	ASSERT_TRUE(seqq_popEarliest(qs, 3, 0x100000100ull, &src, &e, &t));
	ASSERT_EQ(src, 1);
	ASSERT_EQ(t, 0x100000050ull);
}

TEST(syncq_extend)
{
	uint64_t time = 0xFFFFFF00u;
	uint32_t last = 0xFFFFFF00u;
	ASSERT_EQ(seqq_extend(&time, &last, 0xFFFFFFF0u), 0xFFFFFFF0ull);
	ASSERT_EQ(seqq_extend(&time, &last, 0x00000010u), 0x100000010ull);	/* the counter wrapped */
	ASSERT_EQ(last, 0x00000010u);
	ASSERT_EQ(seqq_extend(&time, &last, 0x00000010u), 0x100000010ull);	/* no time passed */
	ASSERT_EQ(seqq_extend(&time, &last, 0x80000000u), 0x180000000ull);
}

TEST(syncq_din)
{
	reset();
	seqq_din_t d = { 0, 0 };
	/* a note on with a clock between its data bytes, then SPP 272 = 0x10 + 0x02 * 128
	 * with a clock between its data bytes, then the transport and two ignored bytes */
	const uint8_t bytes[] = { 0x90, 0x3C, 0xF8, 0x64, 0xF2, 0x10, 0xF8, 0x02, 0xFB, 0xFE, 0xFA, 0xFC, 0xF9 };
	for (unsigned i = 0; i < sizeof bytes; i++)
		seqq_dinByte(&d, &qa, bytes[i], 1000u + i);
	seqq_entry_t e[16];
	ASSERT_EQ(takeAll(&qa, e, 16), 6);
	ASSERT_EQ(e[0].type, SEQC_EV_CLOCK);
	ASSERT_EQ(e[0].stamp, 1002);
	ASSERT_EQ(e[1].type, SEQC_EV_CLOCK);
	ASSERT_EQ(e[1].stamp, 1006);
	ASSERT_EQ(e[2].type, SEQQ_SONG_POSITION);
	ASSERT_EQ(e[2].value, 272);
	ASSERT_EQ(e[2].stamp, 1007);
	ASSERT_EQ(e[3].type, SEQC_EV_CONTINUE);
	ASSERT_EQ(e[3].stamp, 1008);
	ASSERT_EQ(e[4].type, SEQC_EV_START);
	ASSERT_EQ(e[4].stamp, 1010);
	ASSERT_EQ(e[5].type, SEQC_EV_STOP);
	ASSERT_EQ(e[5].stamp, 1011);
}

TEST(syncq_din_spp_cut)
{
	reset();
	seqq_din_t d = { 0, 0 };
	/* an SPP cut short by a note on is dropped; data bytes outside an SPP are ignored */
	const uint8_t bytes[] = { 0xF2, 0x10, 0x90, 0x02, 0x40, 0xF2, 0x7F, 0x7F, 0x05 };
	for (unsigned i = 0; i < sizeof bytes; i++)
		seqq_dinByte(&d, &qa, bytes[i], 2000u + i);
	seqq_entry_t e[8];
	ASSERT_EQ(takeAll(&qa, e, 8), 1);
	ASSERT_EQ(e[0].type, SEQQ_SONG_POSITION);
	ASSERT_EQ(e[0].value, 16383);
	ASSERT_EQ(e[0].stamp, 2007);
}

TEST(syncq_usb)
{
	reset();
	seqq_usbMessage(&qb, 0x0F, 0xF8, 0x00, 0x00, 10);	/* clock */
	seqq_usbMessage(&qb, 0x09, 0x90, 0x3C, 0x64, 11);	/* note on: not a sync event */
	seqq_usbMessage(&qb, 0x03, 0xF2, 0x10, 0x02, 12);	/* SPP 272 */
	seqq_usbMessage(&qb, 0x0F, 0xFB, 0x00, 0x00, 13);	/* continue */
	seqq_usbMessage(&qb, 0x1F, 0xF8, 0x00, 0x00, 14);	/* cable 1: ignored */
	seqq_usbMessage(&qb, 0x0F, 0xFE, 0x00, 0x00, 15);	/* active sensing: ignored */
	seqq_usbMessage(&qb, 0x05, 0xF8, 0x00, 0x00, 16);	/* realtime with CIN 0x5, as some hosts send it (N18.4) */
	seqq_usbMessage(&qb, 0x05, 0xF7, 0x00, 0x00, 17);	/* CIN 0x5 with the end of a SysEx: ignored */
	seqq_usbMessage(&qb, 0x0F, 0xFC, 0x00, 0x00, 18);	/* stop */
	seqq_usbMessage(&qb, 0x05, 0xFA, 0x00, 0x00, 19);	/* start with CIN 0x5 */
	seqq_entry_t e[8];
	ASSERT_EQ(takeAll(&qb, e, 8), 6);
	ASSERT_EQ(e[0].type, SEQC_EV_CLOCK);
	ASSERT_EQ(e[0].stamp, 10);
	ASSERT_EQ(e[1].type, SEQQ_SONG_POSITION);
	ASSERT_EQ(e[1].value, 272);
	ASSERT_EQ(e[1].stamp, 12);
	ASSERT_EQ(e[2].type, SEQC_EV_CONTINUE);
	ASSERT_EQ(e[2].stamp, 13);
	ASSERT_EQ(e[3].type, SEQC_EV_CLOCK);
	ASSERT_EQ(e[3].stamp, 16);
	ASSERT_EQ(e[4].type, SEQC_EV_STOP);
	ASSERT_EQ(e[4].stamp, 18);
	ASSERT_EQ(e[5].type, SEQC_EV_START);
	ASSERT_EQ(e[5].stamp, 19);
}

TEST_MAIN()
