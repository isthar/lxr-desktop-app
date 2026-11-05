/*
 * plug_archive.c
 *
 * A card folder as one block of bytes (plan E9.2 Task 7); see plug_archive.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_archive.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "emu_hostfs.h"

enum { KIND_END = 0, KIND_DIR = 1, KIND_FILE = 2 };

typedef struct
{
	uint8_t *p;
	size_t n, cap;
	int failed;		/* out of memory or past PLUG_ARCHIVE_MAX */
	int changed;	/* a file changed while it was read */
} buf_t;

static void bput(buf_t *b, const void *p, size_t n)
{
	if (b->failed)
		return;
	if (b->n + n > PLUG_ARCHIVE_MAX + 65536u)
	{
		b->failed = 1;
		return;
	}
	if (b->n + n > b->cap)
	{
		size_t cap = b->cap ? b->cap : 65536u;
		while (cap < b->n + n)
			cap *= 2u;
		uint8_t *q = realloc(b->p, cap);
		if (!q)
		{
			b->failed = 1;
			return;
		}
		b->p = q;
		b->cap = cap;
	}
	memcpy(b->p + b->n, p, n);
	b->n += n;
}

static void bputLe(buf_t *b, uint64_t v, unsigned bytes)
{
	uint8_t x[8];
	for (unsigned i = 0; i < bytes; i++)
		x[i] = (uint8_t)(v >> (8u * i));
	bput(b, x, bytes);
}

static int fail(char *err, size_t n, const char *fmt, const char *a, const char *b)
{
	snprintf(err, n, fmt, a, b);
	return -1;
}

/* the entries of root/rel, then each directory's own, depth first */
static int addDir(buf_t *b, const char *root, const char *rel, char *err, size_t n)
{
	char path[EMU_HOST_PATH];
	snprintf(path, sizeof path, "%s%s%s", root, *rel ? "/" : "", rel);
	emu_hostEntry_t *list;
	const int count = emu_hostList(path, &list, NULL);
	if (count < 0)
		return fail(err, n, "card archive: cannot list %s%s", path, "");
	int rc = 0;
	for (int i = 0; i < count && rc == 0; i++)
	{
		char child[EMU_HOST_PATH], full[EMU_HOST_PATH];
		snprintf(child, sizeof child, "%s%s%s", rel, *rel ? "/" : "", list[i].host);
		snprintf(full, sizeof full, "%s/%s", root, child);
		const size_t len = strlen(child);
		bput(b, (const uint8_t[]){ (uint8_t)(list[i].isDir ? KIND_DIR : KIND_FILE) }, 1);
		bputLe(b, len, 2);
		bput(b, child, len);
		if (list[i].isDir)
		{
			rc = addDir(b, root, child, err, n);
			continue;
		}
		struct stat before, after;
		const int fd = open(full, O_RDONLY);
		if (fd < 0 || fstat(fd, &before) != 0)
		{
			if (fd >= 0)
				close(fd);
			rc = fail(err, n, "card archive: cannot read %s: %s", full, strerror(errno));
			break;
		}
		bputLe(b, (uint64_t)before.st_size, 8);
		uint8_t chunk[65536];
		uint64_t left = (uint64_t)before.st_size;
		while (left && !b->failed)
		{
			const ssize_t got = read(fd, chunk, left < sizeof chunk ? (size_t)left : sizeof chunk);
			if (got <= 0)
			{
				b->changed = 1;	/* shorter than its size: it is being written */
				break;
			}
			bput(b, chunk, (size_t)got);
			left -= (uint64_t)got;
		}
		if (stat(full, &after) != 0 || after.st_size != before.st_size
			|| after.st_mtimespec.tv_sec != before.st_mtimespec.tv_sec
			|| after.st_mtimespec.tv_nsec != before.st_mtimespec.tv_nsec || left)
			b->changed = 1;
		close(fd);
	}
	free(list);
	return rc;
}

int plug_archiveWrite(const char *dir, plug_put_t put, void *ctx, char *err, size_t n)
{
	for (int attempt = 0; attempt < 3; attempt++)
	{
		buf_t b = { 0 };
		bput(&b, PLUG_ARCHIVE_MAGIC, 4);
		bputLe(&b, PLUG_ARCHIVE_VERSION, 4);
		int rc = addDir(&b, dir, "", err, n);
		bput(&b, (const uint8_t[]){ KIND_END }, 1);
		if (rc == 0 && b.failed)
			rc = fail(err, n, "card archive: %s is larger than 64 MB, or out of memory%s", dir, "");
		if (rc == 0 && !b.changed)
			rc = put(ctx, b.p, b.n) == 0 ? 0 : fail(err, n, "card archive: the stream refused %s%s", "the bytes", "");
		free(b.p);
		if (rc != 0 || !b.changed)
			return rc;
	}
	return fail(err, n, "card archive: the files of %s kept changing while they were read%s", dir, "");
}

static int getLe(plug_get_t get, void *ctx, uint64_t *v, unsigned bytes)
{
	uint8_t x[8];
	if (get(ctx, x, bytes) != 0)
		return -1;
	*v = 0;
	for (unsigned i = 0; i < bytes; i++)
		*v |= (uint64_t)x[i] << (8u * i);
	return 0;
}

/* a relative path of 8.3 names: no empty, dot or other component */
static int goodPath(const char *p)
{
	char part[256];
	for (;;)
	{
		const char *slash = strchr(p, '/');
		const size_t len = slash ? (size_t)(slash - p) : strlen(p);
		if (len == 0 || len >= sizeof part)
			return 0;
		memcpy(part, p, len);
		part[len] = '\0';
		char name[13];
		if (part[0] == '.' || !emu_host83(part, name))
			return 0;
		if (!slash)
			return 1;
		p = slash + 1;
	}
}

int plug_archiveRead(const char *dir, plug_get_t get, void *ctx, char *err, size_t n)
{
	char magic[4];
	uint64_t version;
	if (get(ctx, magic, 4) != 0 || memcmp(magic, PLUG_ARCHIVE_MAGIC, 4) != 0 || getLe(get, ctx, &version, 4) != 0
		|| version != PLUG_ARCHIVE_VERSION)
		return fail(err, n, "card archive: not an LCHS archive of version 1%s%s", "", "");
	if (mkdir(dir, 0755) != 0 && errno != EEXIST)
		return fail(err, n, "card archive: cannot make %s: %s", dir, strerror(errno));
	DIR *d = opendir(dir);
	if (!d)
		return fail(err, n, "card archive: cannot read %s: %s", dir, strerror(errno));
	for (const struct dirent *e; (e = readdir(d)) != NULL;)
		if (e->d_name[0] != '.')
		{
			closedir(d);
			return fail(err, n, "card archive: %s is not empty%s", dir, "");
		}
	closedir(d);
	uint64_t total = 0;
	for (;;)
	{
		uint8_t kind;
		uint64_t len;
		if (get(ctx, &kind, 1) != 0)
			return fail(err, n, "card archive: it ends early%s%s", "", "");
		if (kind == KIND_END)
			return 0;
		char rel[EMU_HOST_PATH], full[EMU_HOST_PATH], parent[EMU_HOST_PATH];
		if ((kind != KIND_DIR && kind != KIND_FILE) || getLe(get, ctx, &len, 2) != 0 || len == 0 || len >= 512u
			|| get(ctx, rel, (size_t)len) != 0)
			return fail(err, n, "card archive: a damaged entry%s%s", "", "");
		rel[len] = '\0';
		if (!goodPath(rel))
			return fail(err, n, "card archive: the path %s is not a card's%s", rel, "");
		snprintf(full, sizeof full, "%s/%s", dir, rel);
		snprintf(parent, sizeof parent, "%s", full);
		*strrchr(parent, '/') = '\0';
		if (kind == KIND_DIR)
		{
			if (mkdir(full, 0755) != 0)
				return fail(err, n, "card archive: cannot make %s: %s", full, strerror(errno));
		}
		else
		{
			uint64_t size;
			if (getLe(get, ctx, &size, 8) != 0 || size > PLUG_ARCHIVE_MAX - total)
				return fail(err, n, "card archive: %s is too large%s", rel, "");
			total += size;
			const int fd = open(full, O_WRONLY | O_CREAT | O_EXCL, 0644);
			if (fd < 0)
				return fail(err, n, "card archive: cannot make %s: %s", full, strerror(errno));
			uint8_t chunk[65536];
			while (size)
			{
				const size_t k = size < sizeof chunk ? (size_t)size : sizeof chunk;
				if (get(ctx, chunk, k) != 0 || write(fd, chunk, k) != (ssize_t)k)
				{
					close(fd);
					return fail(err, n, "card archive: %s ends early, or cannot be written%s", rel, "");
				}
				size -= k;
			}
			if (close(fd) != 0)
				return fail(err, n, "card archive: cannot write %s: %s", full, strerror(errno));
		}
		emu_hostOrderLast(parent, full);
	}
}
