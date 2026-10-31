/*
 * test_app_midi.c
 *
 * M18 (stage 1b spec 4.3, 6.4, 6.5, 12.1): the MIDI conversions of the app: the
 * cut of an event packet into ring entries, UMP to USB-MIDI packets and DIN bytes
 * and back, the DIN assembler, the USB transfer of up to 16 packets, and a
 * CoreMIDI event list with its timestamps.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <string.h>

#include "app_midi.h"
#include "app_midilist.h"
#include "seqc_test.h"

/* a ring stand-in, and a recorder of UMP messages */
typedef struct { app_midiEntry_t e[64]; unsigned n; } list_t;
static int listPush(void *ctx, const app_midiEntry_t *e)
{
	list_t *l = ctx;
	if (l->n == 64u)
		return -1;
	l->e[l->n++] = *e;
	return 0;
}
static const app_midiEntry_t *listPeek(void *ctx, unsigned i)
{
	list_t *l = ctx;
	return i < l->n ? &l->e[i] : NULL;
}
typedef struct { uint64_t t[64]; uint32_t w[64][2]; unsigned n; } umps_t;
static void umpEmit(void *ctx, uint64_t t, const uint32_t *w, unsigned n)
{
	umps_t *u = ctx;
	if (u->n < 64u)
	{
		u->t[u->n] = t;
		memcpy(u->w[u->n], w, n * sizeof *w);
		u->n++;
	}
}

TEST(M18_midi_split)
{
	uint32_t words[32];
	for (unsigned i = 0; i < 30u; i++)
		words[i] = 0x10F80000u;
	list_t l = { .n = 0 };
	ASSERT_EQ(app_midiSplit(42u, APP_PORT_USB, words, 30u, listPush, &l), 2);	/* E10: 16 + 14 */
	ASSERT_EQ(l.e[0].nwords, 16);
	ASSERT_EQ(l.e[1].nwords, 14);
	ASSERT_EQ(l.e[1].stamp, 42);
	for (unsigned i = 0; i < 14u; i++)
		words[i] = 0x20903C64u;
	words[14] = 0x30047E7Fu;	/* a complete SysEx of 4 bytes: 7E 7F 06 01, worth 3 packets */
	words[15] = 0x06010000u;
	l.n = 0;
	ASSERT_EQ(app_midiSplit(7u, APP_PORT_DIN, words, 16u, listPush, &l), 2);	/* 14 + 3 > 16: whole into the next */
	ASSERT_EQ(l.e[0].nwords, 14);
	ASSERT_EQ(l.e[1].nwords, 2);
	ASSERT_EQ(l.e[1].words[0], 0x30047E7Fu);
	ASSERT_EQ(l.e[1].port, APP_PORT_DIN);
	uint32_t sx[12];	/* six SysEx packets of 6 bytes: 18 packets' worth, cut at 5 (15) */
	for (unsigned i = 0; i < 12u; i += 2u)
	{
		sx[i] = (i == 0u ? 0x30160000u : i == 10u ? 0x30360000u : 0x30260000u) | 0x0102u;
		sx[i + 1u] = 0x03040506u;
	}
	l.n = 0;
	ASSERT_EQ(app_midiSplit(9u, APP_PORT_USB, sx, 12u, listPush, &l), 2);
	ASSERT_EQ(l.e[0].nwords, 10);
	app_usbPend_t p = { .n = 0 };
	uint8_t data[64];
	unsigned len;
	ASSERT_EQ(app_usbTransfer(listPeek, &l, &p, data, &len), 2);	/* F0 + 36 bytes + F7: 13 packets, one transfer */
	ASSERT_EQ(len, 52);
	ASSERT_EQ(data[48], 0x06);	/* the end: CIN 0x6, the last data byte and F7 */
	ASSERT_EQ(data[50], 0xF7);
}

TEST(M18_midi_usb_cin)
{
	const uint32_t w[] = { 0x20903C64u, 0x20C00500u, 0x10F20800u, 0x10F10300u, 0x10F60000u, 0x10FA0000u,
		0x30047E7Fu, 0x06010000u };
	const uint8_t want[] = { 0x09, 0x90, 0x3C, 0x64, 0x0C, 0xC0, 0x05, 0x00, 0x03, 0xF2, 0x08, 0x00,
		0x02, 0xF1, 0x03, 0x00, 0x05, 0xF6, 0x00, 0x00, 0x0F, 0xFA, 0x00, 0x00,
		0x04, 0xF0, 0x7E, 0x7F, 0x07, 0x06, 0x01, 0xF7 };
	uint8_t out[64];
	app_usbPend_t p = { .n = 0 };
	ASSERT_EQ(app_umpToUsb(w, 8u, &p, out, sizeof out), (int)sizeof want);
	ASSERT_EQ(memcmp(out, want, sizeof want), 0);
	ASSERT_EQ(p.n, 0);
	ASSERT_EQ(app_umpToUsb(w, 8u, &p, out, 28u), -1);	/* does not fit */
}

TEST(M18_midi_usb_transfer)
{
	list_t l = { .n = 0 };
	const uint32_t clock = 0x10F80000u;
	for (unsigned i = 0; i < 20u; i++)
		app_midiSplit(1000u, APP_PORT_USB, &clock, 1u, listPush, &l);
	app_midiSplit(2000u, APP_PORT_USB, &clock, 1u, listPush, &l);
	app_usbPend_t p = { .n = 0 };
	uint8_t data[64];
	unsigned len;
	ASSERT_EQ(app_usbTransfer(listPeek, &l, &p, data, &len), 16);	/* at most 16 packets (spec 4.3) */
	ASSERT_EQ(len, 64);
	ASSERT_EQ(data[60], 0x0F);
	ASSERT_EQ(data[61], 0xF8);
	memmove(l.e, l.e + 16, 5u * sizeof l.e[0]);
	l.n = 5;
	ASSERT_EQ(app_usbTransfer(listPeek, &l, &p, data, &len), 4);	/* the rest of stamp 1000; 2000 is another */
	ASSERT_EQ(len, 16);
}

TEST(M18_midi_bytes)
{
	const uint32_t w[] = { 0x20903C64u, 0x20C00500u, 0x10F20800u, 0x10F80000u, 0x30047E7Fu, 0x06010000u };
	const uint8_t want[] = { 0x90, 0x3C, 0x64, 0xC0, 0x05, 0xF2, 0x08, 0x00, 0xF8,
		0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
	uint8_t out[32];
	ASSERT_EQ(app_umpToBytes(w, 6u, out, sizeof out), sizeof want);
	ASSERT_EQ(memcmp(out, want, sizeof want), 0);
}

/* spec 6.5: complete messages at the time of their first byte; realtime at its own */
TEST(M18_midi_din_asm)
{
	static const struct { uint64_t t; uint8_t b; } in[] = {
		{ 0, 0x90 }, { 69120, 0x3C }, { 138240, 0xF8 }, { 207360, 0x64 },
		{ 276480, 0x3E }, { 345600, 0x40 },		/* running status: from its first data byte */
		{ 414720, 0xC0 }, { 483840, 0x05 },		/* one data byte */
		{ 552960, 0xF0 }, { 622080, 0x7E }, { 691200, 0x01 }, { 760320, 0x02 }, { 829440, 0x03 },
		{ 898560, 0x04 }, { 967680, 0x05 }, { 1036800, 0x06 }, { 1105920, 0xF7 },
		{ 1175040, 0x40 },						/* a data byte without a status */
	};
	app_dinAsm_t a;
	memset(&a, 0, sizeof a);
	umps_t u = { .n = 0 };
	for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++)
		app_dinByte(&a, in[i].t, in[i].b, umpEmit, &u);
	ASSERT_EQ(u.n, 6);
	ASSERT_EQ(u.w[0][0], 0x10F80000u);
	ASSERT_EQ(u.t[0], 138240);
	ASSERT_EQ(u.w[1][0], 0x20903C64u);
	ASSERT_EQ(u.t[1], 0);
	ASSERT_EQ(u.w[2][0], 0x20903E40u);
	ASSERT_EQ(u.t[2], 276480);
	ASSERT_EQ(u.w[3][0], 0x20C00500u);
	ASSERT_EQ(u.w[4][0], 0x30167E01u);	/* SysEx start, 6 bytes */
	ASSERT_EQ(u.w[4][1], 0x02030405u);
	ASSERT_EQ(u.w[5][0], 0x30310600u);	/* end, 1 byte */
	ASSERT_EQ(u.t[5], 552960);			/* every packet at the time of its F0 */
	ASSERT_EQ(a.orphans, 1);
}

TEST(M18_midi_usb_out)
{
	const uint8_t transfer[] = { 0x09, 0x90, 0x3C, 0x64, 0x0F, 0xF8, 0x00, 0x00, 0x0C, 0xC0, 0x05, 0x00 };
	app_sx_t sx;
	memset(&sx, 0, sizeof sx);
	umps_t u = { .n = 0 };
	app_usbToUmp(5000u, transfer, sizeof transfer, &sx, umpEmit, &u);
	ASSERT_EQ(u.n, 3);
	ASSERT_EQ(u.w[0][0], 0x20903C64u);
	ASSERT_EQ(u.w[1][0], 0x10F80000u);
	ASSERT_EQ(u.w[2][0], 0x20C00500u);
	ASSERT_EQ(u.t[2], 5000);

	/* the DIN -> USB thru sends a SysEx one byte per CIN 0xF packet (MidiParser.c, usb_midiTx.c:54): one MT 3
	 * packet comes out, not four MT 1 words. A realtime byte inside it is a message of its own; F4 and F5 are dropped */
	const uint8_t sysex[] = { 0x0F, 0xF0, 0, 0, 0x0F, 0x7E, 0, 0, 0x0F, 0x01, 0, 0, 0x0F, 0xF7, 0, 0 };
	memset(&sx, 0, sizeof sx);
	u.n = 0;
	app_usbToUmp(6000u, sysex, sizeof sysex, &sx, umpEmit, &u);
	ASSERT_EQ(u.n, 1);
	ASSERT_EQ(u.w[0][0], 0x30027E01u);	/* complete, 2 bytes */
	ASSERT_EQ(u.w[0][1], 0);
	ASSERT_EQ(u.t[0], 6000);
	const uint8_t mixed[] = { 0x0F, 0xF0, 0, 0, 0x0F, 0x7E, 0, 0, 0x0F, 0xF8, 0, 0, 0x0F, 0xF4, 0, 0, 0x0F, 0xF5, 0, 0,
		0x0F, 0x01, 0, 0, 0x05, 0xFA, 0, 0, 0x05, 0xF7, 0, 0 };
	memset(&sx, 0, sizeof sx);
	u.n = 0;
	app_usbToUmp(7000u, mixed, sizeof mixed, &sx, umpEmit, &u);
	ASSERT_EQ(u.n, 3);
	ASSERT_EQ(u.w[0][0], 0x10F80000u);	/* the clock, at once */
	ASSERT_EQ(u.w[1][0], 0x10FA0000u);	/* CIN 0x5 carries a realtime byte as well: a Start, not a SysEx byte */
	ASSERT_EQ(u.w[2][0], 0x30027E01u);	/* F4, F5 dropped; 7E 01 stay */
}

/* spec 12.1 M18: distinct stamps stay distinct; six clocks stamped 0 enter as one transfer */
TEST(M18_eventlist)
{
	static uint32_t buf[256];	/* a MIDIEventList is 4-byte aligned */
	MIDIEventList *list = (MIDIEventList *)buf;
	const UInt32 clock = 0x10F80000u;
	MIDIEventPacket *p = MIDIEventListInit(list, kMIDIProtocol_1_0);
	for (unsigned k = 0; k < 6u; k++)
		p = MIDIEventListAdd(list, sizeof buf, p, 1000u + 100u * k, 1u, &clock);
	ASSERT_TRUE(p != NULL);
	list_t l = { .n = 0 };
	ASSERT_EQ(app_midiFromEventList(list, APP_PORT_USB, 777u, listPush, &l), 6);
	for (unsigned k = 0; k < 6u; k++)
		ASSERT_EQ(l.e[k].stamp, 1000u + 100u * k);
	p = MIDIEventListInit(list, kMIDIProtocol_1_0);
	for (unsigned k = 0; k < 6u; k++)
		p = MIDIEventListAdd(list, sizeof buf, p, 0u, 1u, &clock);
	l.n = 0;
	app_midiFromEventList(list, APP_PORT_USB, 777u, listPush, &l);
	ASSERT_EQ(l.e[0].stamp, 777);	/* E8 */
	app_usbPend_t pend = { .n = 0 };
	uint8_t data[64];
	unsigned len;
	ASSERT_EQ(app_usbTransfer(listPeek, &l, &pend, data, &len), l.n);
	ASSERT_EQ(len, 24);
}

TEST_MAIN()
