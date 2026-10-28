/*
 * emu_hostfs.h
 *
 * The host side of the card backend (stage 1b spec 4.8): POSIX file and
 * directory calls on the host folder that holds the virtual card. It is a file
 * of its own because FatFs's DIR clashes with <dirent.h>; emu_card.c is the
 * FatFs side.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_HOSTFS_H_
#define EMU_HOSTFS_H_

#include <stdint.h>

#define EMU_HOST_PATH 1024

/** one entry of a card directory */
typedef struct emu_hostEntry
{
	char host[256];		/**< the name on the host */
	char name[13];		/**< the 8.3 name as FatFs returns it: upper case, "NAME.EXT" */
	int isDir;
	uint64_t size;
	int64_t birthSec;	/**< birth time, the card's directory order */
	long birthNsec;
} emu_hostEntry_t;

/** The 8.3 name of a host name, upper case, into name[13]; 0 if the host name is
 * not a valid 8.3 name. */
int emu_host83(const char *host, char name[13]);

/** List a host directory in the card's order: birth time, then name. Dot
 * entries are skipped silently; names that are not 8.3 are skipped and passed
 * to skipped() (may be NULL). Returns the number of entries in *out (malloc'd,
 * free() it), or -1 if the directory cannot be read. */
int emu_hostList(const char *dir, emu_hostEntry_t **out, void (*skipped)(const char *path));

/** 1 directory, 0 file, -1 missing */
int emu_hostKind(const char *path);

/** create a directory; 0 or -1 */
int emu_hostMkdir(const char *path);

/** remove a file, or a directory that holds nothing but dot entries; 0 or -1 */
int emu_hostRemove(const char *path, int isDir);

/** open a file: write 0 read only, 1 read and write; create 1 creates a missing
 * file; truncate 1 empties it. Returns a file descriptor or -1. */
int emu_hostOpen(const char *path, int write, int create, int truncate);

/** the size of an open file, or -1 */
int64_t emu_hostSize(int fd);

/** Make path come after every other entry of its directory in the card's order:
 * its birth time becomes the latest birth time there plus 1 us, if that is later
 * than its own. */
void emu_hostOrderLast(const char *dir, const char *path);

/** Copy the card folder src into the empty folder dst file by file in the
 * card's order, skipping dot entries and names that are not 8.3, with the
 * copies' birth times in that order. skipTmp 1 leaves /TMP out. 0 or -1. */
int emu_hostCopyCard(const char *src, const char *dst, int skipTmp);

#endif /* EMU_HOSTFS_H_ */
