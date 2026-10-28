/*
 * usbd_cdc.h
 *
 * Stand-in for the USB MIDI class header in the LICHOMAT emulator (stage 1b
 * spec 3.4, 4.3, 4.4): the USB input ring that the endpoint stand-in fills,
 * and the class handle whose TxState usb_manager.c reads. It contains no ST
 * code.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_USBD_CDC_H_
#define EMU_USBD_CDC_H_
#include <stdint.h>
#include "usbd_def.h"
#include "Soundengine/MIDI/MidiMessages.h"

#define USB_MIDI_INPUT_BUFFER_SIZE 64
#define USB_MIDI_INPUT_BUFFER_MASK 0x3F

extern MidiMsg usb_MidiMessages[USB_MIDI_INPUT_BUFFER_SIZE];
extern uint8_t usb_MidiMessagesRead;
extern uint8_t usb_MidiMessagesWrite;

typedef struct
{
	volatile uint32_t TxState;
} USBD_CDC_HandleTypeDef;

#endif /* EMU_USBD_CDC_H_ */
