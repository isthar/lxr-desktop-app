/*
 * fatfs.h
 *
 * Host stand-in for the FatFs application header: Preset/PresetManager.h only
 * needs the result type of its file functions.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef HOST_FATFS_H_
#define HOST_FATFS_H_

typedef enum { FR_OK = 0, FR_DISK_ERR = 1 } FRESULT;

#endif /* HOST_FATFS_H_ */
