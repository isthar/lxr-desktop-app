/*
 * emu_usb.c
 *
 * The USB endpoint stand-in of the LICHOMAT emulator (stage 1b spec 4.3, 4.4).
 * usb_manager.c runs unchanged on it.
 * - In: one incoming transfer is one OTG_FS out body. It walks the 4-byte
 *   USB-MIDI 1.0 packets on cable 0 up to the first packet whose first byte is
 *   0, hands each message with a CIN of 0x2-0xF to seqp_usbMessageIn() with one
 *   stamp per transfer, and appends it to usb_MidiMessages[64] unless the ring
 *   is full. At most 16 packets enter in any 1 ms; a transfer is never split.
 * - Out: CDC_Transmit_FS() passes the transfer to the USB sink, holds TxState
 *   at 1 for 1 ms, then the OTG_FS in body clears it and calls
 *   usb_midiTxComplete().
 * hUsbDeviceFS is always configured, with pClassData set.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdlib.h>
#include <string.h>

#include "stm32f7xx_hal.h"
#include "usbd_cdc_if.h"
#include "emu_internal.h"

#define EMU_USB_PACKETS_PER_MS 16u

MidiMsg usb_MidiMessages[USB_MIDI_INPUT_BUFFER_SIZE];
uint8_t usb_MidiMessagesRead;
uint8_t usb_MidiMessagesWrite;
USBD_HandleTypeDef hUsbDeviceFS;

static USBD_CDC_HandleTypeDef emu_cdc;

typedef struct { uint64_t t; uint8_t length; uint8_t data[64]; } emu_usbTransfer_t;

static struct
{
	emu_usbTransfer_t *in;		/* incoming transfers, in time order */
	size_t head, count, cap;
	uint64_t last;				/* the time of the last transfer queued */
	uint64_t window;			/* start of the current 1 ms window */
	unsigned windowPackets;		/* packets entered in it */
	emu_usbTransfer_t current;	/* the transfer the OTG_FS out body walks */
	uint64_t inDone;			/* the OTG_FS in body's time (EMU_NEVER: none) */
} emu_usb;

/* the data bytes of a USB-MIDI 1.0 packet by its code index number */
static const uint8_t emu_cinLength[16] = { 0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1 };

static void emu_usbSchedule(void)
{
	uint64_t due = emu_usb.inDone;
	/* While an out body waits behind PRIMASK, the next transfer waits for it: its
	 * body calls emu_usbFire() again at its end. A due time kept in the past would
	 * stall the scheduler. */
	if (emu_usb.count && !emu_src[EMU_SRC_OTG_OUT].pending)
	{
		const emu_usbTransfer_t *head = &emu_usb.in[emu_usb.head];
		uint64_t t = head->t;
		const unsigned packets = head->length / 4u;
		if (t < emu_usb.window + EMU_CYCLES_PER_MS && emu_usb.windowPackets + packets > EMU_USB_PACKETS_PER_MS)
			t = emu_usb.window + EMU_CYCLES_PER_MS;	/* it waits until the window ends */
		if (t < due)
			due = t;
	}
	emu_src[EMU_SRC_OTG_OUT].due = due;
	emu_src[EMU_SRC_OTG_IN].due = EMU_NEVER;
}

/* OTG_OUT's fire handles both directions: one due time for the scheduler */
static void emu_usbFire(void)
{
	const uint64_t now = emu_now();
	if (emu_usb.inDone <= now)
	{
		emu_usb.inDone = EMU_NEVER;
		emu_src[EMU_SRC_OTG_IN].pending++;
	}
	if (emu_usb.count && !emu_src[EMU_SRC_OTG_OUT].pending)
	{
		const emu_usbTransfer_t *head = &emu_usb.in[emu_usb.head];
		const unsigned packets = head->length / 4u;
		if (head->t <= now)
		{
			if (now >= emu_usb.window + EMU_CYCLES_PER_MS)
			{
				emu_usb.window = now;
				emu_usb.windowPackets = 0;
			}
			if (emu_usb.windowPackets + packets <= EMU_USB_PACKETS_PER_MS)
			{
				emu_usb.windowPackets += packets;
				emu_usb.current = *head;
				emu_usb.head = (emu_usb.head + 1u) % emu_usb.cap;
				emu_usb.count--;
				emu_src[EMU_SRC_OTG_OUT].pending = 1;
			}
		}
	}
	emu_usbSchedule();
}

/* the MIDI part of USBD_CDC_DataOut(), drift-checked by M20 */
static void emu_usbOutBody(void)
{
	const uint32_t stamp = DWT->CYCCNT;		/* seqp_stamp(): once per transfer */
	emu_count.usbIn++;
	for (unsigned i = 0; i + 4u <= emu_usb.current.length; i += 4u)
	{
		const uint8_t *p = &emu_usb.current.data[i];
		if (p[0] == 0u)
			break;
		if (p[0] & 0xF0u)
			continue;	/* not cable 0 */
		const uint8_t length = emu_cinLength[p[0] & 0x0Fu];
		if (length == 0u)
			continue;
		if (emu_fw.usbMessageIn)
			emu_fw.usbMessageIn(p[0], p[1], p[2], p[3], stamp);
		if (((usb_MidiMessagesWrite + 1u) & USB_MIDI_INPUT_BUFFER_MASK) == usb_MidiMessagesRead)
		{
			emu_count.usbInDropped++;	/* a full ring drops the new message */
			continue;
		}
		MidiMsg *m = &usb_MidiMessages[usb_MidiMessagesWrite];
		m->status = p[1];
		m->data1 = p[2];
		m->data2 = p[3];
		m->bits.length = (unsigned)(length - 1u) & 3u;
		m->bits.sysxbyte = 0;
		m->bits.source = midiSourceUSB;
		usb_MidiMessagesWrite = (uint8_t)((usb_MidiMessagesWrite + 1u) & USB_MIDI_INPUT_BUFFER_MASK);
	}
	emu_usbFire();	/* the next transfer may already be due */
}

static void emu_usbInBody(void)
{
	emu_cdc.TxState = 0;
	if (emu_fw.usbTxComplete)
		emu_fw.usbTxComplete();
}

uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len)
{
	if (emu_cdc.TxState != 0)
		return USBD_BUSY;
	emu_cdc.TxState = 1;
	emu_count.usbOut++;
	if (emu_cfg.sinks.usb)
		emu_cfg.sinks.usb(emu_cfg.sinks.ctx, emu_now(), Buf, Len);
	emu_usb.inDone = emu_now() + EMU_CYCLES_PER_MS;
	emu_usbSchedule();
	return USBD_OK;
}

int emu_inUsb(uint64_t t, const uint8_t *data, unsigned length)
{
	if (length == 0u || length > 64u || length % 4u != 0u)
		return -1;
	t = emu_inputTime(t);
	if (t < emu_usb.last)
		t = emu_usb.last;	/* transfers keep their order */
	if (emu_usb.count == emu_usb.cap)
	{
		const size_t cap = emu_usb.cap ? emu_usb.cap * 2u : 256u;
		emu_usbTransfer_t *grown = malloc(cap * sizeof *grown);
		if (!grown)
		{
			emu_fault("out of memory for USB input");
			return -1;
		}
		for (size_t k = 0; k < emu_usb.count; k++)
			grown[k] = emu_usb.in[(emu_usb.head + k) % emu_usb.cap];
		free(emu_usb.in);
		emu_usb.in = grown;
		emu_usb.head = 0;
		emu_usb.cap = cap;
	}
	emu_usbTransfer_t *slot = &emu_usb.in[(emu_usb.head + emu_usb.count) % emu_usb.cap];
	slot->t = t;
	slot->length = (uint8_t)length;
	memcpy(slot->data, data, length);
	emu_usb.count++;
	emu_usb.last = t;
	emu_usbSchedule();
	return 0;
}

void emu_usbReset(const emu_config_t *cfg)
{
	(void)cfg;
	free(emu_usb.in);
	memset(&emu_usb, 0, sizeof emu_usb);
	emu_usb.inDone = EMU_NEVER;
	memset(&emu_cdc, 0, sizeof emu_cdc);
	memset(usb_MidiMessages, 0, sizeof usb_MidiMessages);
	usb_MidiMessagesRead = usb_MidiMessagesWrite = 0;
	hUsbDeviceFS.dev_state = USBD_STATE_CONFIGURED;
	hUsbDeviceFS.pClassData = &emu_cdc;
	emu_src[EMU_SRC_OTG_OUT] = (emu_source_t){ EMU_NEVER, emu_usbFire, emu_usbOutBody, 0 };
	emu_src[EMU_SRC_OTG_IN] = (emu_source_t){ EMU_NEVER, NULL, emu_usbInBody, 0 };
}
