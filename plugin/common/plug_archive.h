/*
 * plug_archive.h
 *
 * A card folder as one block of bytes, the plugin's state (plan E9.2 Task 7). The firmware reads
 * a directory in the card's order, the files' birth times (emu_hostfs.h:38-42), so the archive
 * keeps that order and a restore recreates it.
 *
 * Format, little-endian: "LCHS", version 1 (u32), then the entries of each directory in the
 * card's order, a directory's own entries right after it:
 *   kind (u8: 1 directory, 2 file), path length (u16), the path relative to the card ('/'
 *   between names), and for a file its size (u64) and its bytes;
 * then kind 0. Names that are not 8.3 and dot entries are left out: the device does not see
 * them (emu_hostList()).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_ARCHIVE_H_
#define PLUG_ARCHIVE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_ARCHIVE_MAGIC "LCHS"
#define PLUG_ARCHIVE_VERSION 1u
#define PLUG_ARCHIVE_MAX ((uint64_t)64 << 20)	/**< the most bytes of files an archive holds */

/** writes n bytes; returns 0, or -1 */
typedef int (*plug_put_t)(void *ctx, const void *p, size_t n);
/** reads exactly n bytes; returns 0, or -1 */
typedef int (*plug_get_t)(void *ctx, void *p, size_t n);

/** Archive the folder dir. A file that changes while it is read (its size or modification time)
 * makes the whole archive start again, up to 3 times. Returns 0, or -1 with the problem in err. */
int plug_archiveWrite(const char *dir, plug_put_t put, void *ctx, char *err, size_t n);

/** Restore an archive into dir, which must be missing or empty; each entry is made in the
 * archive's order and ordered last in its directory (emu_hostOrderLast()). Returns 0, or -1 with
 * the problem in err (dir may then hold part of the archive). */
int plug_archiveRead(const char *dir, plug_get_t get, void *ctx, char *err, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_ARCHIVE_H_ */
