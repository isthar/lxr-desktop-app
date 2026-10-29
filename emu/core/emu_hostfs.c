/*
 * emu_hostfs.c
 *
 * The host side of the card backend (stage 1b spec 4.8), see emu_hostfs.h.
 * The card's directory order is the entries' birth time, then their name: in
 * folders that only the firmware wrote this is FAT's creation order. APFS
 * readdir() returns hash order, so it is never used for order.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "emu_hostfs.h"

/* FatFs's characters that a short name cannot hold */
static int emu_hostBadChar(unsigned char c)
{
	return c < 0x21u || c >= 0x7Fu || strchr("\"*+,./:;<=>?[\\]|", c) != NULL;
}

int emu_host83(const char *host, char name[13])
{
	const char *dot = strchr(host, '.');
	const size_t body = dot ? (size_t)(dot - host) : strlen(host);
	const size_t ext = dot ? strlen(dot + 1) : 0;
	if (body == 0 || body > 8 || (dot && (ext == 0 || ext > 3)))
		return 0;
	size_t n = 0;
	for (size_t i = 0; i < body; i++)
	{
		const unsigned char c = (unsigned char)host[i];
		if (emu_hostBadChar(c))
			return 0;
		name[n++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
	}
	if (dot)
	{
		name[n++] = '.';
		for (size_t i = 0; i < ext; i++)
		{
			const unsigned char c = (unsigned char)dot[1 + i];
			if (emu_hostBadChar(c))
				return 0;
			name[n++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
		}
	}
	name[n] = '\0';
	return 1;
}

static int emu_hostOrder(const void *pa, const void *pb)
{
	const emu_hostEntry_t *a = pa, *b = pb;
	if (a->birthSec != b->birthSec)
		return a->birthSec < b->birthSec ? -1 : 1;
	if (a->birthNsec != b->birthNsec)
		return a->birthNsec < b->birthNsec ? -1 : 1;
	return strcmp(a->name, b->name);
}

int emu_hostList(const char *dir, emu_hostEntry_t **out, void (*skipped)(const char *path))
{
	*out = NULL;
	DIR *d = opendir(dir);
	if (!d)
		return -1;
	int count = 0, cap = 0;
	emu_hostEntry_t *list = NULL;
	struct dirent *e;
	while ((e = readdir(d)) != NULL)
	{
		if (e->d_name[0] == '.')
			continue;	/* ., .., .DS_Store, ._ files, .fseventsd, .Spotlight-V100 */
		char path[EMU_HOST_PATH];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		emu_hostEntry_t entry;
		memset(&entry, 0, sizeof entry);
		struct stat st;
		if (strlen(e->d_name) >= sizeof entry.host || !emu_host83(e->d_name, entry.name) || stat(path, &st) != 0)
		{
			if (skipped)
				skipped(path);
			continue;
		}
		snprintf(entry.host, sizeof entry.host, "%s", e->d_name);
		entry.isDir = S_ISDIR(st.st_mode) ? 1 : 0;
		entry.size = entry.isDir ? 0u : (uint64_t)st.st_size;
		entry.birthSec = (int64_t)st.st_birthtimespec.tv_sec;
		entry.birthNsec = st.st_birthtimespec.tv_nsec;
		if (count == cap)
		{
			cap = cap ? cap * 2 : 32;
			emu_hostEntry_t *grown = realloc(list, (size_t)cap * sizeof *grown);
			if (!grown)
			{
				free(list);
				closedir(d);
				return -1;
			}
			list = grown;
		}
		list[count++] = entry;
	}
	closedir(d);
	if (count > 1)
		qsort(list, (size_t)count, sizeof *list, emu_hostOrder);
	*out = list;
	return count;
}

int emu_hostKind(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return -1;
	return S_ISDIR(st.st_mode) ? 1 : 0;
}

int emu_hostMkdir(const char *path)
{
	return mkdir(path, 0755) == 0 ? 0 : -1;
}

int emu_hostRemove(const char *path, int isDir)
{
	if (!isDir)
		return unlink(path) == 0 ? 0 : -1;
	DIR *d = opendir(path);
	if (!d)
		return -1;
	struct dirent *e;
	int others = 0;
	while ((e = readdir(d)) != NULL)
		if (e->d_name[0] != '.')
			others = 1;
	closedir(d);
	if (others)
		return -1;
	/* only dot entries are left: macOS's .DS_Store and the like */
	d = opendir(path);
	if (!d)
		return -1;
	while ((e = readdir(d)) != NULL)
	{
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		char sub[EMU_HOST_PATH];
		snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
		unlink(sub);
	}
	closedir(d);
	return rmdir(path) == 0 ? 0 : -1;
}

int emu_hostOpen(const char *path, int write, int create, int truncate)
{
	int flags = write ? O_RDWR : O_RDONLY;
	if (create)
		flags |= O_CREAT;
	if (truncate)
		flags |= O_TRUNC;
	return open(path, flags, 0644);
}

int64_t emu_hostSize(int fd)
{
	struct stat st;
	return fstat(fd, &st) == 0 ? (int64_t)st.st_size : -1;
}

static int emu_hostSetBirth(const char *path, int64_t sec, long nsec)
{
	struct attrlist attrs;
	memset(&attrs, 0, sizeof attrs);
	attrs.bitmapcount = ATTR_BIT_MAP_COUNT;
	attrs.commonattr = ATTR_CMN_CRTIME;
	struct timespec ts = { (time_t)sec, nsec };
	return setattrlist(path, &attrs, &ts, sizeof ts, FSOPT_NOFOLLOW) == 0 ? 0 : -1;
}

void emu_hostOrderLast(const char *dir, const char *path)
{
	emu_hostEntry_t *list;
	const int n = emu_hostList(dir, &list, NULL);
	if (n < 0)
		return;
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	int64_t sec = 0;
	long nsec = 0;
	int self = -1;
	for (int i = 0; i < n; i++)
	{
		if (!strcmp(list[i].host, base))
		{
			self = i;
			continue;
		}
		if (list[i].birthSec > sec || (list[i].birthSec == sec && list[i].birthNsec > nsec))
		{
			sec = list[i].birthSec;
			nsec = list[i].birthNsec;
		}
	}
	nsec += 1000;
	if (nsec >= 1000000000L)
	{
		sec++;
		nsec -= 1000000000L;
	}
	if (self >= 0 && (list[self].birthSec < sec || (list[self].birthSec == sec && list[self].birthNsec < nsec)))
		emu_hostSetBirth(path, sec, nsec);
	free(list);
}

static int emu_hostCopyFile(const char *src, const char *dst)
{
	const int in = open(src, O_RDONLY);
	if (in < 0)
		return -1;
	const int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out < 0)
	{
		close(in);
		return -1;
	}
	char buf[65536];
	ssize_t n;
	int status = 0;
	while ((n = read(in, buf, sizeof buf)) > 0)
		if (write(out, buf, (size_t)n) != n)
		{
			status = -1;
			break;
		}
	if (n < 0)
		status = -1;
	close(in);
	close(out);
	return status;
}

static int emu_hostCopyDir(const char *src, const char *dst, int skipTmp, int64_t *sec, long *nsec,
	void (*skipped)(const char *path))
{
	emu_hostEntry_t *list;
	const int n = emu_hostList(src, &list, skipped);
	if (n < 0)
		return -1;
	int status = 0;
	for (int i = 0; i < n && status == 0; i++)
	{
		if (skipTmp && list[i].isDir && !strcmp(list[i].name, "TMP"))
			continue;
		char from[EMU_HOST_PATH], to[EMU_HOST_PATH];
		snprintf(from, sizeof from, "%s/%s", src, list[i].host);
		snprintf(to, sizeof to, "%s/%s", dst, list[i].host);
		status = list[i].isDir ? emu_hostMkdir(to) : emu_hostCopyFile(from, to);
		if (status == 0)
		{
			*nsec += 1000;
			if (*nsec >= 1000000000L)
			{
				(*sec)++;
				*nsec -= 1000000000L;
			}
			status = emu_hostSetBirth(to, *sec, *nsec);
		}
		if (status == 0 && list[i].isDir)
			status = emu_hostCopyDir(from, to, 0, sec, nsec, skipped);
	}
	free(list);
	return status;
}

int emu_hostCopyCardSkipped(const char *src, const char *dst, int skipTmp, void (*skipped)(const char *path))
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	int64_t sec = (int64_t)now.tv_sec;
	long nsec = now.tv_nsec;
	return emu_hostCopyDir(src, dst, skipTmp, &sec, &nsec, skipped);
}

int emu_hostCopyCard(const char *src, const char *dst, int skipTmp)
{
	return emu_hostCopyCardSkipped(src, dst, skipTmp, NULL);
}
