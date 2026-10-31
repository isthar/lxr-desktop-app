/*
 * app_card.c
 *
 * The app's card folder (stage 1b spec 6.7, 4.8, 11, plan E16, E17, E21): paths,
 * the first run's seeding by lichomat-render, ordered copies that set the birth
 * times in copy order, backups, import, export card and export project.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_card.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "app_time.h"

extern char **environ;

/* --- paths ------------------------------------------------------------------------------------ */

static int app_mkdirs(const char *path)
{
	char p[EMU_HOST_PATH];
	if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p)
		return -1;
	for (char *c = p + 1; *c; c++)
		if (*c == '/')
		{
			*c = '\0';
			if (mkdir(p, 0755) != 0 && errno != EEXIST)
				return -1;
			*c = '/';
		}
	return mkdir(p, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

int app_pathsAt(app_paths_t *p, const char *home)
{
	memset(p, 0, sizeof *p);
	if (snprintf(p->home, sizeof p->home, "%s", home) >= (int)sizeof p->home
		|| snprintf(p->card, sizeof p->card, "%s/Card", home) >= (int)sizeof p->card
		|| snprintf(p->backups, sizeof p->backups, "%s/Card backups", home) >= (int)sizeof p->backups
		|| snprintf(p->settings, sizeof p->settings, "%s/settings.txt", home) >= (int)sizeof p->settings)
		return -1;
	return app_mkdirs(p->home) == 0 && app_mkdirs(p->backups) == 0 ? 0 : -1;
}

int app_pathsInit(app_paths_t *p)
{
	const char *env = getenv("LICHOMAT_EMU_HOME");
	if (env && env[0])
		return app_pathsAt(p, env);
	const char *home = getenv("HOME");
	char dir[EMU_HOST_PATH];
	if (!home || !home[0]
		|| snprintf(dir, sizeof dir, "%s/Library/Application Support/LICHOMAT Emu", home) >= (int)sizeof dir)
		return -1;
	return app_pathsAt(p, dir);
}

/* --- the state of a folder -------------------------------------------------------------------- */

int app_cardNonDot(const char *dir, char *names, size_t n)
{
	if (names && n)
		names[0] = '\0';
	DIR *d = opendir(dir);
	if (!d)
		return errno == ENOENT ? 0 : -1;
	int count = 0;
	size_t len = 0;
	for (const struct dirent *de; (de = readdir(d)) != NULL;)
	{
		if (de->d_name[0] == '.')
			continue;
		if (names && n && len + 1u < n)
		{
			const int w = snprintf(names + len, n - len, "%s%s", count ? ", " : "", de->d_name);
			len = w < 0 ? len : (len + (size_t)w < n ? len + (size_t)w : n - 1u);
		}
		count++;
	}
	closedir(d);
	return count;
}

int app_cardState(const char *dir)
{
	const int kind = emu_hostKind(dir);
	if (kind < 0)
		return 1;
	if (kind == 0)
		return -1;
	const int others = app_cardNonDot(dir, NULL, 0);
	return others < 0 ? -1 : others == 0;
}

int app_cardSeed(const char *render, const char *card, char *err, size_t n)
{
	char *const argv[] = { "lichomat-render", "--seed-card", (char *)card, NULL };
	pid_t pid;
	const int rc = posix_spawn(&pid, render, NULL, NULL, argv, environ);
	if (rc != 0)
	{
		snprintf(err, n, "%s could not be started: %s", render, strerror(rc));
		return -1;
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
		{
			snprintf(err, n, "lichomat-render --seed-card: %s", strerror(errno));
			return -1;
		}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	snprintf(err, n, "lichomat-render --seed-card exited with %d",
		WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
	return -1;
}

int app_cardCheck(const char *card, int *project)
{
	emu_hostEntry_t *root;
	*project = 0;
	const int n = emu_hostList(card, &root, NULL);
	if (n < 0)
		return -1;
	int p = 0, has = 0;
	for (int i = 0; i < n; i++)
		if (!root[i].isDir && !strcmp(root[i].name, "PROJ.IDX"))
		{
			char path[EMU_HOST_PATH];
			uint8_t b = 0;
			snprintf(path, sizeof path, "%s/%s", card, root[i].host);
			const int fd = emu_hostOpen(path, 0, 0, 0);
			if (fd >= 0)
			{
				if (read(fd, &b, 1) == 1 && b < 64u)
					p = b;	/* 64-255 count as 0 (E4.7) */
				close(fd);
			}
		}
	char name[13];
	snprintf(name, sizeof name, "PROJ%02d", p);
	for (int i = 0; i < n && !has; i++)
		if (root[i].isDir && !strcmp(root[i].name, name))
		{
			char path[EMU_HOST_PATH];
			emu_hostEntry_t *l;
			snprintf(path, sizeof path, "%s/%s", card, root[i].host);
			const int m = emu_hostList(path, &l, NULL);
			for (int k = 0; k < m; k++)
				has |= !l[k].isDir && !strcmp(l[k].name, "PRJ.NFO");
			if (m >= 0)
				free(l);
		}
	free(root);
	*project = p;
	return has;
}

/* --- the order of a copy ---------------------------------------------------------------------- */

int app_cardIsFat(const char *path)
{
	struct statfs fs;
	return statfs(path, &fs) == 0 && (!strcmp(fs.f_fstypename, "msdos") || !strcmp(fs.f_fstypename, "exfat"));
}

static int app_byBirth(const void *pa, const void *pb)
{
	const app_cardEntry_t *a = pa, *b = pb;
	if (a->birthSec != b->birthSec)
		return a->birthSec < b->birthSec ? -1 : 1;
	if (a->birthNsec != b->birthNsec)
		return a->birthNsec < b->birthNsec ? -1 : 1;
	return strcmp(a->name, b->name);
}

static int app_bySeq(const void *pa, const void *pb)
{
	const app_cardEntry_t *a = pa, *b = pb;
	return (a->seq > b->seq) - (a->seq < b->seq);
}

void app_cardOrder(app_cardEntry_t *list, int n, int fat)
{
	if (n > 1)
		qsort(list, (size_t)n, sizeof *list, fat ? app_bySeq : app_byBirth);
}

int app_cardList(const char *dir, int fat, app_cardEntry_t **out, app_warn_t warn, void *ctx)
{
	*out = NULL;
	DIR *d = opendir(dir);
	if (!d)
		return -1;
	int n = 0, cap = 0;
	app_cardEntry_t *list = NULL;
	for (const struct dirent *de; (de = readdir(d)) != NULL;)
	{
		if (de->d_name[0] == '.')
			continue;	/* ., .., .DS_Store, ._ files, .fseventsd, .Spotlight-V100 */
		app_cardEntry_t e;
		memset(&e, 0, sizeof e);
		char path[EMU_HOST_PATH];
		snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
		struct stat st;
		if (strlen(de->d_name) >= sizeof e.host || !emu_host83(de->d_name, e.name) || stat(path, &st) != 0)
		{
			if (warn)
			{
				char w[EMU_HOST_PATH + 64];
				snprintf(w, sizeof w, "card: %s is not an 8.3 name; the device would not see it", path);
				warn(ctx, w);
			}
			continue;
		}
		snprintf(e.host, sizeof e.host, "%s", de->d_name);
		e.isDir = S_ISDIR(st.st_mode);
		e.birthSec = (int64_t)st.st_birthtimespec.tv_sec;
		e.birthNsec = st.st_birthtimespec.tv_nsec;
		e.seq = (unsigned)n;
		if (n == cap)
		{
			cap = cap ? 2 * cap : 32;
			app_cardEntry_t *grown = realloc(list, (size_t)cap * sizeof *grown);
			if (!grown)
			{
				free(list);
				closedir(d);
				return -1;
			}
			list = grown;
		}
		list[n++] = e;
	}
	closedir(d);
	app_cardOrder(list, n, fat);
	*out = list;
	return n;
}

static int app_setBirth(const char *path, int64_t sec, long nsec)
{
	struct attrlist a;
	memset(&a, 0, sizeof a);
	a.bitmapcount = ATTR_BIT_MAP_COUNT;
	a.commonattr = ATTR_CMN_CRTIME;
	struct timespec ts = { (time_t)sec, nsec };
	return setattrlist(path, &a, &ts, sizeof ts, FSOPT_NOFOLLOW) == 0 ? 0 : -1;
}

static int app_copyFile(const char *src, const char *dst)
{
	const int in = open(src, O_RDONLY);
	const int out = in < 0 ? -1 : open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	int status = in < 0 || out < 0 ? -1 : 0;
	char buf[65536];
	ssize_t n = 0;
	while (status == 0 && (n = read(in, buf, sizeof buf)) > 0)
		if (write(out, buf, (size_t)n) != n)
			status = -1;
	if (n < 0)
		status = -1;
	if (in >= 0)
		close(in);
	if (out >= 0 && close(out) != 0)
		status = -1;
	return status;
}

typedef struct { int srcFat, dstFat; int64_t sec; long nsec; app_warn_t warn; void *ctx; } app_copy_t;

static int app_copyDir(app_copy_t *c, const char *src, const char *dst)
{
	app_cardEntry_t *list;
	const int n = app_cardList(src, c->srcFat, &list, c->warn, c->ctx);
	if (n < 0)
		return -1;
	int status = 0;
	for (int i = 0; i < n && status == 0; i++)
	{
		char from[EMU_HOST_PATH], to[EMU_HOST_PATH];
		snprintf(from, sizeof from, "%s/%s", src, list[i].host);
		snprintf(to, sizeof to, "%s/%s", dst, list[i].host);
		status = list[i].isDir ? mkdir(to, 0755) : app_copyFile(from, to);
		if (status == 0)
		{
			/* births 1 us apart in copy order; on FAT the write order is the order (E16) */
			if ((c->nsec += 1000) >= 1000000000L)
			{
				c->sec++;
				c->nsec -= 1000000000L;
			}
			if (app_setBirth(to, c->sec, c->nsec) != 0 && !c->dstFat)
				status = -1;
		}
		if (status == 0 && list[i].isDir)
			status = app_copyDir(c, from, to);
	}
	free(list);
	return status;
}

int app_cardCopy(const char *src, const char *dst, app_warn_t warn, void *ctx)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	app_copy_t c = { app_cardIsFat(src), app_cardIsFat(dst), (int64_t)now.tv_sec, now.tv_nsec, warn, ctx };
	return app_copyDir(&c, src, dst);
}

/* --- backups, import, export ------------------------------------------------------------------ */

int app_backupName(const char *backups, int64_t localSec, const char *suffix, char *out, size_t n)
{
	char base[48];
	if (!app_stampName(localSec, "", suffix ? suffix : "", base, sizeof base))
		return -1;
	for (unsigned k = 1; k < 1000u; k++)
	{
		if (k == 1u)
			snprintf(out, n, "%s/%s", backups, base);
		else
			snprintf(out, n, "%s/%s-%u", backups, base, k);
		if (emu_hostKind(out) < 0)
			return 0;
	}
	return -1;
}

static int app_rmEntry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st;
	(void)flag;
	(void)ftw;
	return remove(path);
}

static int app_rmTree(const char *path)
{
	return nftw(path, app_rmEntry, 16, FTW_DEPTH | FTW_PHYS);
}

/* path is dir or inside it, both as realpath() gives them */
static int app_within(const char *path, const char *dir)
{
	const size_t n = strlen(dir);
	return !strncmp(path, dir, n) && (path[n] == '/' || path[n] == '\0');
}

int app_cardImport(const app_paths_t *p, const char *src, int64_t localSec, char *backup, size_t bn, app_warn_t warn, void *ctx)
{
	char rs[PATH_MAX], rc[PATH_MAX];
	backup[0] = '\0';
	if (emu_hostKind(src) != 1 || !realpath(src, rs))
		return -1;
	const int had = emu_hostKind(p->card) == 1;
	if (had && realpath(p->card, rc) && (app_within(rs, rc) || app_within(rc, rs)))
		return -1;	/* never from the card's own folder: it is moved away first */
	if (had && (app_backupName(p->backups, localSec, NULL, backup, bn) != 0 || rename(p->card, backup) != 0))
	{
		backup[0] = '\0';
		return -1;	/* step 2 */
	}
	int status = mkdir(p->card, 0755);
	if (status == 0 && (status = app_cardCopy(src, p->card, warn, ctx)) != 0)	/* step 3; step 4, the power cycle, is the shell's */
		(void)app_rmTree(p->card);	/* what this copy made */
	if (status != 0)
	{
		if (had)
			(void)rename(backup, p->card);	/* the old card is back */
		backup[0] = '\0';
		return -1;
	}
	return 0;
}

int app_cardExport(const char *card, const char *dst, char *err, size_t n, app_warn_t warn, void *ctx)
{
	const int others = app_cardNonDot(dst, err, n);
	if (others != 0)
		return others < 0 ? -1 : -2;
	if (emu_hostKind(dst) < 0 && mkdir(dst, 0755) != 0)
		return -1;
	return app_cardCopy(card, dst, warn, ctx);
}

/* the folder of dir whose 8.3 name is name, as FatFs matches it */
static int app_cardFind(const char *dir, const char *name, char *out, size_t n)
{
	DIR *d = opendir(dir);
	int found = 0;
	char n83[13];
	for (const struct dirent *de; d && !found && (de = readdir(d)) != NULL;)
		if (de->d_name[0] != '.' && emu_host83(de->d_name, n83) && !strcmp(n83, name))
		{
			snprintf(out, n, "%s/%s", dir, de->d_name);
			found = emu_hostKind(out) == 1;
		}
	if (d)
		closedir(d);
	return found;
}

static void app_countWarn(void *ctx, const char *what)
{
	(void)what;
	(*(int *)ctx)++;
}

int app_cardExportProject(const app_paths_t *p, int project, const char *dst, int replace, int64_t localSec,
	char *err, size_t n, app_warn_t warn, void *ctx)
{
	char name[13], sfx[16], from[EMU_HOST_PATH], to[EMU_HOST_PATH], backup[EMU_HOST_PATH];
	if (project < 0 || project > 63)
	{
		snprintf(err, n, "project %d: the card has projects 0 to 63", project);
		return -1;
	}
	snprintf(name, sizeof name, "PROJ%02d", project);
	if (!app_cardFind(p->card, name, from, sizeof from))
	{
		snprintf(err, n, "the card has no %s", name);
		return -1;
	}
	if (app_cardFind(dst, name, to, sizeof to))
	{
		if (!replace)
		{
			snprintf(err, n, "%s already holds %s", dst, name);
			return 2;
		}
		int skipped = 0;	/* a name the copy would drop: nothing is replaced */
		snprintf(sfx, sizeof sfx, "-%s", name);
		if (app_backupName(p->backups, localSec, sfx, backup, sizeof backup) != 0 || mkdir(backup, 0755) != 0
			|| app_cardCopy(to, backup, app_countWarn, &skipped) != 0 || skipped || app_rmTree(to) != 0)
		{
			snprintf(err, n, "%s could not be moved into %s%s", to, backup,
				skipped ? ": it holds names that are not 8.3" : "");
			return -1;
		}
	}
	snprintf(to, sizeof to, "%s/%s", dst, name);
	if (mkdir(to, 0755) != 0 || app_cardCopy(from, to, warn, ctx) != 0)
	{
		snprintf(err, n, "%s could not be written", to);
		return -1;
	}
	return 0;
}
