/*
 * usbd_def.h
 *
 * Stand-in for the USB device library's definitions in the LICHOMAT emulator
 * (stage 1b spec 3.4, 4.4): the device handle that usb_manager.c reads. It
 * contains no ST code.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_USBD_DEF_H_
#define EMU_USBD_DEF_H_
#include <stdint.h>

#define USBD_STATE_CONFIGURED 0x03U

enum { USBD_OK = 0U, USBD_BUSY, USBD_FAIL };

typedef struct
{
	volatile uint8_t dev_state;
	void *pClassData;
} USBD_HandleTypeDef;

#endif /* EMU_USBD_DEF_H_ */
