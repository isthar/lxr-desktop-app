/*
 * test_usb.c
 *
 * M11 (stage 1b spec 4.3, 4.4, 12.1): the USB endpoint stand-in with the
 * firmware's seqp_usbMessageIn(), usb_sendMidi() and usb_midiTxComplete()
 * (seq_platform.c, usb_manager.c), without a boot. In: the DataOut walk of the
 * 4-byte packets on cable 0 up to a 0 byte, with one stamp per transfer, and at
 * most 16 packets in a 1 ms window that starts at its first packet; a transfer
 * enters whole. Out: transfers of at most 15 packets, each completed 1 ms after
 * it starts, so the 100 ms stall rule never drops the ring.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "usbd_cdc.h"
#include "Soundengine/USB/usb_manager.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include "emu_test.h"

#define MS EMU_CYCLES_PER_MS

extern USBD_HandleTypeDef hUsbDeviceFS;	/* emu_usb.c */

/* the USB sink: each transfer's time, length and bytes */
#define MAX_XFER 256u
static struct { uint64_t t; uint16_t length; uint8_t data[64]; } xfer[MAX_XFER];
static unsigned xferCount;

static void usbSink(void *ctx, uint64_t t, const uint8_t *data, uint16_t length)
{
	(void)ctx;
	if (xferCount < MAX_XFER)
	{
		xfer[xferCount].t = t;
		xfer[xferCount].length = length;
		memcpy(xfer[xferCount].data, data, length <= 64u ? length : 64u);
	}
	xferCount++;
}

static void setup(void)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.sinks.usb = usbSink;
	emu_corePowerOn(&cfg);
	emu_usbReset(&cfg);
	emu_fw.usbMessageIn = seqp_usbMessageIn;
	emu_fw.usbTxComplete = usb_midiTxComplete;
	seqp_init();
	xferCount = 0;
}

static uint32_t txState(void)
{
	return ((USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData)->TxState;
}

/* the USB sync entries so far, in stamp order; their source must be USB */
static seqq_entry_t entry[64];
static uint64_t stamp[64];

static unsigned popAll(void)
{
	unsigned n = 0;
	seqp_src_t src;
	seqq_entry_t e;
	seqc_time_t t;
	while (seqp_syncPop(seqp_now(), &src, &e, &t))
	{
		if (src != SEQP_SRC_USB)
			return 999u;
		if (n < 64u)
		{
			entry[n] = e;
			stamp[n] = t;
		}
		n++;
	}
	return n;
}

/* a transfer of n clock packets (CIN 0xF, F8) at t */
static void clocks(uint64_t t, unsigned n)
{
	uint8_t data[64];
	for (unsigned i = 0; i < n; i++)
	{
		data[4 * i] = 0x0F;
		data[4 * i + 1] = 0xF8;
		data[4 * i + 2] = 0;
		data[4 * i + 3] = 0;
	}
	ASSERT_EQ(emu_inUsb(t, data, 4u * n), 0);
}

static MidiMsg noteOn(uint8_t note)
{
	MidiMsg m;
	memset(&m, 0, sizeof m);
	m.status = 0x90;
	m.data1 = note;
	m.data2 = 0x64;
	m.bits.length = 2;
	return m;
}

TEST(M11_walk)
{
	setup();
	const uint8_t data[24] = {
		0x09, 0x90, 0x3C, 0x64,		/* Note On */
		0x05, 0xF8, 0x00, 0x00,		/* clock with CIN 0x5 (N18.4) */
		0x1F, 0xF8, 0x00, 0x00,		/* cable 1: skipped */
		0x0F, 0xFA, 0x00, 0x00,		/* Start */
		0x00, 0x00, 0x00, 0x00,		/* the end of the walk */
		0x09, 0x90, 0x3E, 0x64,		/* not reached */
	};
	ASSERT_EQ(emu_inUsb(1000, data, sizeof data), 0);
	HAL_Delay(1);
	MidiMsg m;
	ASSERT_EQ(usb_getMidi(&m), 1);
	ASSERT_EQ(m.status, 0x90);
	ASSERT_EQ(m.data1, 0x3C);
	ASSERT_EQ(m.data2, 0x64);
	ASSERT_EQ(m.bits.length, 2);
	ASSERT_EQ(m.bits.source, midiSourceUSB);
	ASSERT_EQ(usb_getMidi(&m), 1);
	ASSERT_EQ(m.status, 0xF8);
	ASSERT_EQ(m.bits.length, 0);
	ASSERT_EQ(usb_getMidi(&m), 1);
	ASSERT_EQ(m.status, 0xFA);
	ASSERT_EQ(m.bits.length, 0);
	ASSERT_EQ(usb_getMidi(&m), 0);
	ASSERT_EQ(popAll(), 2);
	ASSERT_EQ(entry[0].type, SEQC_EV_CLOCK);
	ASSERT_EQ(entry[1].type, SEQC_EV_START);
	ASSERT_EQ(stamp[0], 1000);
	ASSERT_EQ(stamp[1], 1000);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.usbIn, 1);
}

TEST(M11_window_16)
{
	setup();
	for (unsigned i = 0; i < 3; i++)
		clocks(1000, 8);
	HAL_Delay(2);
	ASSERT_EQ(popAll(), 24);
	for (unsigned k = 0; k < 24; k++)
		ASSERT_EQ(stamp[k], k < 16u ? 1000u : 1000u + MS);	/* the 3rd waits, whole */

	/* 12 and 8: the window is not full, but the 8 would pass 16, so it waits
	 * whole too, and no 1 ms holds more than 16 */
	setup();
	clocks(1000, 12);
	clocks(1000, 8);
	HAL_Delay(2);
	ASSERT_EQ(popAll(), 20);
	for (unsigned k = 0; k < 20; k++)
		ASSERT_EQ(stamp[k], k < 12u ? 1000u : 1000u + MS);
}

TEST(M11_window_start)
{
	/* the window starts at its first packet, not at power-on */
	setup();
	clocks(108000, 16);		/* 0.5 ms */
	clocks(194400, 1);		/* 0.9 ms: the 17th packet within 1 ms of 108,000 */
	HAL_Delay(2);
	ASSERT_EQ(popAll(), 17);
	for (unsigned k = 0; k < 16; k++)
		ASSERT_EQ(stamp[k], 108000);
	ASSERT_EQ(stamp[16], 108000u + MS);	/* 324,000 (the draft: 216,000) */
}

TEST(M11_out)
{
	setup();
	ASSERT_EQ(hUsbDeviceFS.dev_state, USBD_STATE_CONFIGURED);	/* as power-on leaves it */
	emu_threadAdvance(1000);
	for (unsigned i = 0; i < 20; i++)
		usb_sendMidi(noteOn((uint8_t)(0x30 + i)));
	/* The first call finds the endpoint free and sends its packet at once
	 * (usb_manager.c's usb_sendMidi() starts a transfer on every call); the other
	 * 19 wait in the ring: 15 at the first completion, 4 at the next. */
	ASSERT_EQ(xferCount, 1);
	ASSERT_EQ(txState(), 1);
	emu_threadAdvance(MS - 1u);			/* to 216,999 */
	ASSERT_EQ(xferCount, 1);
	ASSERT_EQ(txState(), 1);
	HAL_Delay(5);
	ASSERT_EQ(xferCount, 3);
	ASSERT_EQ(txState(), 0);
	const uint16_t lengths[3] = { 4, 60, 16 };
	unsigned note = 0x30;
	for (unsigned k = 0; k < 3; k++)
	{
		ASSERT_EQ(xfer[k].t, 1000u + k * MS);
		ASSERT_EQ(xfer[k].length, lengths[k]);
		for (unsigned i = 0; i < lengths[k]; i += 4u, note++)
		{
			ASSERT_EQ(xfer[k].data[i], 0x09);
			ASSERT_EQ(xfer[k].data[i + 1u], 0x90);
			ASSERT_EQ(xfer[k].data[i + 2u], note);
			ASSERT_EQ(xfer[k].data[i + 3u], 0x64);
		}
	}
	ASSERT_EQ(usb_midiTxDropped(), 0);
}

TEST(M11_no_stall_drop)
{
	setup();
	emu_threadAdvance(1000);
	for (unsigned k = 0; k < 200; k++)
	{
		usb_sendMidi(noteOn(0x3C));
		HAL_Delay(10);
	}
	ASSERT_EQ(xferCount, 200);
	for (unsigned k = 0; k < 200; k++)
	{
		ASSERT_EQ(xfer[k].t, 1000u + k * 10u * MS);
		ASSERT_EQ(xfer[k].length, 4);
	}
	ASSERT_EQ(usb_midiTxDropped(), 0);
	ASSERT_EQ(txState(), 0);
}

TEST_MAIN()
