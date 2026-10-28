/*
 * fatfs.h
 *
 * Stand-in for the FatFs glue header in the LICHOMAT emulator (stage 1b spec
 * 4.8): the card backend (emu/core/emu_card.c) supplies these at the FatFs API,
 * and BSP_SD_IsDetected() reads the virtual card switch. It contains no ST code.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_FATFS_H_
#define EMU_FATFS_H_
#include <stdint.h>
#include "ff.h"

extern char SDPath[4];
extern FATFS SDFatFS;
extern FIL SDFile;

void MX_FATFS_Init(void);
void MX_FATFS_ReInit(void);

/* the card detect switch (bsp_driver_sd.h in the firmware build) */
#define SD_PRESENT ((uint8_t)0x01)
#define SD_NOT_PRESENT ((uint8_t)0x00)
uint8_t BSP_SD_IsDetected(void);

#endif /* EMU_FATFS_H_ */
