/*
 * fatfs.h
 *
 * Host stand-in for the FatFs application header (FATFS/App/fatfs.h) for
 * test_presets, which compiles Core/Src/Preset/presetManager.c. It takes the
 * real FatFs declarations (Middlewares/Third_Party/FatFs/src/ff.h, with the
 * firmware's FATFS/Target/ffconf.h) and leaves out the SD card driver. The file
 * functions themselves are the in-memory card of test_presets.c.
 *
 * It comes before tests/stubs/ on the include path of test_presets, so it hides
 * tests/stubs/fatfs.h, which has only the result type.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef HOST_PRESETS_FATFS_H_
#define HOST_PRESETS_FATFS_H_

#include "ff.h"

extern FIL SDFile;

#endif /* HOST_PRESETS_FATFS_H_ */
