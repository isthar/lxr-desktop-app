/*
 * render_card.c
 *
 * The card of lichomat-render (stage 1b spec 5.2, 5.7 code 2, 11); see
 * render_card.h. It works on emu_hostfs.h and POSIX alone, without firmware
 * headers.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "render_card.h"

#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail(char *err, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int fail(char *err, size_t n, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, n, fmt, ap);
	va_end(ap);
	return 2;
}

int render_cardEmpty(const char *dir)
{
	const int kind = emu_hostKind(dir);
	if (kind < 0)
		return 1;
	if (kind == 0)
		return -1;
	DIR *d = opendir(dir);
	if (!d)
		return -1;
	int empty = 1;
	const struct dirent *e;
	while ((e = readdir(d)) != NULL)
		if (e->d_name[0] != '.')
			empty = 0;
	closedir(d);
	return empty;
}

/* the entry of a listing with the 8.3 name (as FatFs matches it, ignoring case) */
static const emu_hostEntry_t *find(const emu_hostEntry_t *list, int count, const char *name, int isDir)
{
	for (int i = 0; i < count; i++)
		if (!strcmp(list[i].name, name) && list[i].isDir == isDir)
			return &list[i];
	return NULL;
}

/* the project byte of /PROJ.IDX as preset_loadLastUsedProjectNrFromSD() reads it:
 * 0 without the file or a byte, and a byte of 64-255 counts as 0 (E4.7) */
static int indexProject(const char *copy, const emu_hostEntry_t *idx)
{
	if (!idx)
		return 0;
	char path[EMU_HOST_PATH];
	snprintf(path, sizeof path, "%s/%s", copy, idx->host);
	const int fd = emu_hostOpen(path, 0, 0, 0);
	if (fd < 0)
		return 0;
	uint8_t byte = 0;
	const ssize_t got = read(fd, &byte, 1);
	close(fd);
	return got == 1 && byte < 64u ? byte : 0;
}

/* 1 if the copy has PROJnn/PRJ.NFO, 0 if not, -1 if a folder cannot be listed */
static int hasInfo(const char *copy, const emu_hostEntry_t *root, int count, int project)
{
	char name[13];
	snprintf(name, sizeof name, "PROJ%02d", project);
	const emu_hostEntry_t *dir = find(root, count, name, 1);
	if (!dir)
		return 0;
	char path[EMU_HOST_PATH];
	snprintf(path, sizeof path, "%s/%s", copy, dir->host);
	emu_hostEntry_t *list;
	const int n = emu_hostList(path, &list, NULL);
	if (n < 0)
		return -1;
	const int found = find(list, n, "PRJ.NFO", 0) != NULL;
	free(list);
	return found;
}

/* the warning of a name the copy skips (spec 11), with the device's wording (emu_card.c) */
static void (*cardWarn)(const char *what);

static void skippedName(const char *path)
{
	char text[EMU_HOST_PATH + 64];
	snprintf(text, sizeof text, "card: %s is not an 8.3 name; the device would not see it", path);
	if (cardWarn)
		cardWarn(text);
}

int render_cardPrepare(const render_opts_t *o, char copy[EMU_HOST_PATH], int *project, char *err, size_t n,
	void (*warn)(const char *what))
{
	cardWarn = warn;
	const char *card = o->card;
	copy[0] = '\0';
	const int kind = emu_hostKind(card);
	if (kind < 0)
		return fail(err, n, "card %s: no such folder", card);
	if (kind == 0)
		return fail(err, n, "card %s: not a folder", card);
	DIR *d = opendir(card);
	if (!d)
		return fail(err, n, "card %s: cannot read: %s", card, strerror(errno));
	closedir(d);

	const char *tmp = getenv("TMPDIR");
	if (!tmp || !tmp[0])
		tmp = "/tmp";
	const size_t len = strlen(tmp);
	snprintf(copy, EMU_HOST_PATH, "%.*s/lichomat-render-XXXXXX", (int)(len > 1 && tmp[len - 1] == '/' ? len - 1 : len), tmp);
	if (!mkdtemp(copy))
	{
		const int e = errno;
		copy[0] = '\0';
		return fail(err, n, "card %s: cannot make its copy in %s: %s", card, tmp, strerror(e));
	}
	int status = 0;
	emu_hostEntry_t *root = NULL;
	int count = -1;
	if (emu_hostCopyCardSkipped(card, copy, !o->asBooted, skippedName) != 0
		|| (count = emu_hostList(copy, &root, NULL)) < 0)
		status = fail(err, n, "card %s: cannot read", card);
	if (status == 0)
	{
		const emu_hostEntry_t *idx = find(root, count, "PROJ.IDX", 0);
		const int p = o->project >= 0 ? o->project : indexProject(copy, idx);
		const int info = hasInfo(copy, root, count, p);
		if (info < 0)
			status = fail(err, n, "card %s: cannot read PROJ%02d", card, p);
		else if (info == 0)
			status = fail(err, n, "card %s: project %d has no PROJ%02d/PRJ.NFO", card, p, p);
		else if (o->project >= 0)
		{
			/* how the device remembers the project to load (spec 5.2) */
			char path[EMU_HOST_PATH];
			snprintf(path, sizeof path, "%s/%s", copy, idx ? idx->host : "PROJ.IDX");
			const int fd = emu_hostOpen(path, 1, 1, 1);
			const uint8_t byte = (uint8_t)p;
			if (fd < 0 || write(fd, &byte, 1) != 1)
				status = fail(err, n, "card %s: cannot write PROJ.IDX into its copy %s", card, copy);
			if (fd >= 0)
				close(fd);
		}
		*project = p;
	}
	free(root);
	if (status != 0)
	{
		render_cardRemove(copy);
		copy[0] = '\0';
	}
	return status;
}

int render_cardKeep(const char *copy, const char *keep)
{
	if (emu_hostKind(keep) < 0 && emu_hostMkdir(keep) != 0)
		return -1;
	return emu_hostCopyCard(copy, keep, 0);
}

static int removeEntry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st;
	(void)flag;
	(void)ftw;
	return remove(path) == 0 ? 0 : -1;
}

void render_cardRemove(const char *dir)
{
	if (dir[0])
		nftw(dir, removeEntry, 16, FTW_DEPTH | FTW_PHYS);
}
