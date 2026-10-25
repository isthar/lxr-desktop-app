/*
 * usb_manager.c
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 *
 *
 *  Created on: 26.10.2012
 *      Author: Julian Schmidt
 */
#include "Soundengine/USB/usb_manager.h"
#include "Soundengine/USB/usb_midiTx.h"
#include <string.h>
#include "usbd_cdc_if.h"




enum {
	USB_NOT_DETECTED,
	USB_DETECTED,
};
//USBD_HandleTypeDef           USB_OTG_dev;
//-------------------------------------------------------------------------------
#if 0
void usb_init()
{
#if HAL_USB
	  if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
	  {
	    Error_Handler();
	  }
	  if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC) != USBD_OK)
	  {
	    Error_Handler();
	  }
	  if (USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS) != USBD_OK)
	  {
	    Error_Handler();
	  }
	  if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
	  {
	    Error_Handler();
	  }
#else

	  usb_start();
#endif
}
//-------------------------------------------------------------------------------
void usb_stop()
{
	//stop usb port
	USBD_DeInit(&USB_OTG_dev);
}
#endif
//-------------------------------------------------------------------------------
void usb_tick()
{
	usb_flushMidi();
}
//-------------------------------------------------------------------------------
#if 0
void usb_start()
{
	//zero init the usb receive buffer
	memset(usb_MidiMessages,0, sizeof(MidiMsg)*USB_MIDI_INPUT_BUFFER_SIZE);

	//start usb port
	 USBD_Init(&USB_OTG_dev,
	            &USR_desc,
				DEVICE_FS);

	 if (USBD_RegisterClass(&USB_OTG_dev, &MIDI_cb) != USBD_OK)

	 // if (USBD_CDC_RegisterInterface(&USB_OTG_dev, &USBD_Interface_fops_FS) != USBD_OK)
	  {
		//Error_Handler();
	  }
	  if (USBD_Start(&USB_OTG_dev) != USBD_OK)
	  {
		//Error_Handler();
	  }
}
#endif
//------------------------------------------------------------------------------
//LICHOMAT (E2.8): USB MIDI out.
//usb_sendMidi() makes the USB-MIDI event packet of a message (usb_midiTx.c) and
//queues it in usb_txRing. Up to 15 packets go to the IN endpoint in one transfer
//as soon as the endpoint is free: usb_sendMidi() and usb_flushMidi() start one
//when none is in flight, and the end of a transfer (USBD_CDC_DataIn() calls
//usb_midiTxComplete()) starts the next while packets wait. The USB core reads
//usb_txBuffer, never the ring, until the transfer has ended. fw 1.7 sent straight
//from its ring, dropped what it queued while the endpoint was busy, and sent
//128 bytes past the ring's end each time its write pointer wrapped.
//Nothing waits for a host that has not configured the device, nor for one that
//has left a transfer untaken for USBTX_STALL_MS (100 ms, HAL_GetTick()): such a
//host has stopped reading (some read only while a program has the port open),
//and gets no burst of old messages when it reads again.
//
//usb_sendMidi() runs in the main loop and in seq_tick(), which runs in the main
//loop or, while the SD card is busy, in TIM2 at priority 4; usb_midiTxComplete()
//runs in OTG_FS at priority 3. Each masks interrupts (PRIMASK) while it uses the
//ring or the endpoint: at most a copy of 15 packets and the start of a transfer.
//------------------------------------------------------------------------------
extern USBD_HandleTypeDef hUsbDeviceFS;

static usbtx_ring_t usb_txRing;
static usbtx_packet_t usb_txBuffer[USBTX_PACKETS_PER_TRANSFER] __attribute__((aligned(4)));

static uint32_t usb_txLock(void)
{
	const uint32_t primask = __get_PRIMASK();
	__disable_irq();
	return primask;
}
//------------------------------------------------------------------------------
static void usb_txUnlock(uint32_t primask)
{
	__set_PRIMASK(primask);
}
//------------------------------------------------------------------------------
/** a host has configured the device, so the class handle and the IN endpoint
 * exist (fw 1.7's CDC_Transmit_FS() read TxState through a NULL handle before) */
static uint8_t usb_hostReady(void)
{
	return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) && (hUsbDeviceFS.pClassData != NULL);
}
//------------------------------------------------------------------------------
/** start the next transfer if the IN endpoint is free; interrupts are masked */
static void usb_startTransfer(void)
{
	const uint8_t ready = usb_hostReady();
	const uint8_t busy = ready && (((USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData)->TxState != 0);
	const uint32_t n = usbtx_next(&usb_txRing, ready, busy, HAL_GetTick(), usb_txBuffer);

	//cannot fail: the handle exists and no transfer is in flight
	if(n != 0)
		CDC_Transmit_FS((uint8_t*)usb_txBuffer, (uint16_t)(n * sizeof(usbtx_packet_t)));
}
//------------------------------------------------------------------------------
/*
 * start a transfer of the waiting packets if the IN endpoint is free.
 * usb_sendMidi() and the end of each transfer do this as well, so the call
 * from usb_tick() is only a fallback.
 */
void usb_flushMidi()
{
	const uint32_t primask = usb_txLock();
	usb_startTransfer();
	usb_txUnlock(primask);
}
//------------------------------------------------------------------------------
void usb_midiTxComplete(void)
{
	const uint32_t primask = usb_txLock();
	usb_startTransfer();
	usb_txUnlock(primask);
}
//------------------------------------------------------------------------------
uint32_t usb_midiTxDropped(void)
{
	return usb_txRing.dropped;
}
//------------------------------------------------------------------------------
void usb_sendMidi(MidiMsg msg)
{
	const usbtx_packet_t packet = usbtx_packet(msg.status, msg.data1, msg.data2);

	const uint32_t primask = usb_txLock();
	if(usb_hostReady())
		usbtx_push(&usb_txRing, packet);	//a full ring drops the packet and counts it
	usb_startTransfer();
	usb_txUnlock(primask);
}
//-------------------------------------------------------------------------------
uint8_t usb_getMidi(MidiMsg* msg)
{
	if(usb_MidiMessagesRead != usb_MidiMessagesWrite)
	{
		//we have unprocessed messages in the queue
		*msg = usb_MidiMessages[usb_MidiMessagesRead];

		usb_MidiMessagesRead++;
		usb_MidiMessagesRead &= USB_MIDI_INPUT_BUFFER_MASK;

		return 1; //return OK
	}
return 0; //no messages
}

