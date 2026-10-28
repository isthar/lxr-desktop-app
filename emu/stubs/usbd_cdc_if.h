/*
 * usbd_cdc_if.h
 *
 * Stand-in for the USB MIDI interface header in the LICHOMAT emulator (stage 1b
 * spec 4.4): the transfer start that the endpoint stand-in provides. It
 * contains no ST code.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_USBD_CDC_IF_H_
#define EMU_USBD_CDC_IF_H_
#include <stdint.h>
#include "usbd_cdc.h"

uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len);

#endif /* EMU_USBD_CDC_IF_H_ */
