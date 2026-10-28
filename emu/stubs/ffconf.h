/*
 * ffconf.h
 *
 * FatFs configuration for ChaN's ff.h in the LICHOMAT emulator (stage 1b spec
 * 4.8): the option values of the firmware build (FATFS/Target/ffconf.h), written
 * fresh. The emulator compiles no FatFs core; these values set the layout of the
 * FatFs types and the API that ff.h declares.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef _FFCONF
#define _FFCONF 68300	/* the revision of ff.h, R0.12c */

#define _FS_READONLY 0
#define _FS_MINIMIZE 0
#define _USE_STRFUNC 2
#define _USE_FIND 1
#define _USE_MKFS 1
#define _USE_FASTSEEK 1
#define _USE_EXPAND 0
#define _USE_CHMOD 0
#define _USE_LABEL 0
#define _USE_FORWARD 0
#define _CODE_PAGE 850
#define _USE_LFN 0
#define _MAX_LFN 255
#define _LFN_UNICODE 0
#define _STRF_ENCODE 3
#define _FS_RPATH 1
#define _VOLUMES 1
#define _STR_VOLUME_ID 0
#define _VOLUME_STRS "RAM", "NAND", "CF", "SD1", "SD2", "USB1", "USB2", "USB3"
#define _MULTI_PARTITION 0
#define _MIN_SS 512
#define _MAX_SS 512
#define _USE_TRIM 0
#define _FS_NOFSINFO 0
#define _FS_TINY 0
#define _FS_EXFAT 0
#define _FS_NORTC 0
#define _NORTC_MON 6
#define _NORTC_MDAY 4
#define _NORTC_YEAR 2015
#define _FS_LOCK 1
#define _FS_REENTRANT 0
#define _FS_TIMEOUT 1000

#endif /* _FFCONF */
