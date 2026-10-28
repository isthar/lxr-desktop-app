/*
 * emu_card.c
 *
 * The card backend of the LICHOMAT emulator (stage 1b spec 4.8): the FatFs
 * API (ChaN's ff.h, R0.12c) on a host folder, above ff.c. It keeps FatFs's
 * behaviour where the firmware can see it:
 * - 8.3 names: a component with + . [ ] ? and the like is FR_INVALID_NAME;
 *   names are upper case on create and case-insensitive on lookup;
 * - FR_NO_FILE for a missing last component, FR_NO_PATH for an earlier one;
 * - create-always truncates in place; a mount resets the current directory;
 * - the directory order is the creation order (emu_hostfs.c), and f_findfirst()
 *   returns the first match in it, with * and ? matching;
 * - dot entries are skipped; names that are not 8.3 are skipped with one
 *   warning each; two files for one slot give a warning that names both;
 * - one open file or sub-directory at a time (_FS_LOCK 1), the root not
 *   counted: a second gives FR_TOO_MANY_OPEN_FILES;
 * - while the card switch is out every call returns FR_NOT_READY;
 * - it never reports FR_NO_FILESYSTEM, and f_mkfs() returns FR_DENIED.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fatfs.h"
#include "emu_hostfs.h"
#include "emu_internal.h"

#define EMU_CARD_DEPTH 8
#define EMU_CARD_OBJECTS 8
#define EMU_CARD_WARNED 64

char SDPath[4] = "0:/";
FATFS SDFatFS;
FIL SDFile;

typedef struct emu_cardPath
{
	char names[EMU_CARD_DEPTH][13];	/* 8.3 components from the root */
	int depth;
} emu_cardPath_t;

typedef struct emu_cardObject
{
	int used, isDir, read, write;
	uint32_t cookie;
	char host[EMU_HOST_PATH];
	int fd;						/* a file */
	uint64_t size;
	emu_hostEntry_t *entries;	/* a directory, listed at its open */
	int count, pos;
	char pattern[16];			/* f_findfirst()'s pattern */
} emu_cardObject_t;

static struct
{
	char root[EMU_HOST_PATH];	/* the card folder on the host; "" for none */
	int cardIn;					/* the switch */
	int registered;				/* f_mount() gave a file system object */
	int mounted;
	emu_cardPath_t cwd;
	emu_cardObject_t obj[EMU_CARD_OBJECTS];
	uint32_t cookies;
	char warned[EMU_CARD_WARNED][EMU_HOST_PATH];
	int warnedCount;
} emu_card;

/* --- warnings, once each ------------------------------------------------------- */

static void emu_cardWarnOnce(const char *key, const char *text)
{
	for (int i = 0; i < emu_card.warnedCount; i++)
		if (!strcmp(emu_card.warned[i], key))
			return;
	if (emu_card.warnedCount < EMU_CARD_WARNED)
		snprintf(emu_card.warned[emu_card.warnedCount++], EMU_HOST_PATH, "%s", key);
	emu_warn(text);
}

static void emu_cardSkipped(const char *path)
{
	char text[EMU_HOST_PATH + 64];
	snprintf(text, sizeof text, "card: %s is not an 8.3 name; the device would not see it", path);
	emu_cardWarnOnce(path, text);
}

/* two files for one slot: the same NN- prefix and extension */
static void emu_cardCheckSlots(const char *dir, const emu_hostEntry_t *e, int n)
{
	for (int i = 0; i < n; i++)
	{
		const char *a = e[i].name;
		if (e[i].isDir || a[0] < '0' || a[0] > '9' || a[1] < '0' || a[1] > '9' || a[2] != '-')
			continue;
		const char *extA = strchr(a, '.');
		for (int j = i + 1; j < n; j++)
		{
			const char *b = e[j].name;
			const char *extB = strchr(b, '.');
			if (e[j].isDir || strncmp(a, b, 3) || !extA || !extB || strcmp(extA, extB))
				continue;
			char key[EMU_HOST_PATH], text[EMU_HOST_PATH + 128];
			snprintf(key, sizeof key, "%s/%s|%s", dir, a, b);
			snprintf(text, sizeof text, "card: %s holds %s and %s for one slot; the older, %s, loads", dir, a, b, a);
			emu_cardWarnOnce(key, text);
		}
	}
}

static int emu_cardList(const char *dir, emu_hostEntry_t **out)
{
	const int n = emu_hostList(dir, out, emu_cardSkipped);
	if (n > 1)
		emu_cardCheckSlots(dir, *out, n);
	return n;
}

/* --- paths ------------------------------------------------------------------------- */

static int emu_cardUpper(int c)
{
	return c >= 'a' && c <= 'z' ? c - 32 : c;
}

/* one path component as FatFs's create_name() takes it */
static FRESULT emu_cardName(const char *s, size_t len, char out[13])
{
	size_t body = 0, ext = 0, n = 0;
	int dot = 0;
	for (size_t i = 0; i < len; i++)
	{
		const unsigned char c = (unsigned char)s[i];
		if (c == '.')
		{
			if (dot || body == 0)
				return FR_INVALID_NAME;
			dot = 1;
			out[n++] = '.';
			continue;
		}
		if (c < 0x21u || c == 0x7Fu || strchr("\"*+,:;<=>?[]|", c))
			return FR_INVALID_NAME;
		if (dot ? ++ext > 3u : ++body > 8u)
			return FR_INVALID_NAME;
		out[n++] = (char)emu_cardUpper(c);
	}
	if (body == 0 || (dot && ext == 0))
		return FR_INVALID_NAME;
	out[n] = '\0';
	return FR_OK;
}

/* a FatFs path to components from the root */
static FRESULT emu_cardParse(const TCHAR *path, emu_cardPath_t *p)
{
	if (path[0] == '0' && path[1] == ':')
		path += 2;
	if (*path == '/' || *path == '\\')
	{
		p->depth = 0;
		while (*path == '/' || *path == '\\')
			path++;
	}
	else
		*p = emu_card.cwd;
	while (*path)
	{
		const char *end = path;
		while (*end && *end != '/' && *end != '\\')
			end++;
		const size_t len = (size_t)(end - path);
		if (len == 1 && path[0] == '.')
			;
		else if (len == 2 && path[0] == '.' && path[1] == '.')
		{
			if (p->depth > 0)
				p->depth--;
		}
		else
		{
			if (p->depth == EMU_CARD_DEPTH)
				return FR_INVALID_NAME;
			const FRESULT r = emu_cardName(path, len, p->names[p->depth]);
			if (r != FR_OK)
				return r;
			p->depth++;
		}
		path = end;
		while (*path == '/' || *path == '\\')
			path++;
	}
	return FR_OK;
}

/* Find the host path of p. The components but the last must be directories
 * (else FR_NO_PATH); a missing last component is FR_NO_FILE, with *host the path
 * it would have (its 8.3 name upper case). *entry gets the last component's
 * entry when it exists. */
static FRESULT emu_cardResolve(const emu_cardPath_t *p, char host[EMU_HOST_PATH], emu_hostEntry_t *entry)
{
	snprintf(host, EMU_HOST_PATH, "%s", emu_card.root);
	for (int i = 0; i < p->depth; i++)
	{
		emu_hostEntry_t *list;
		const int n = emu_cardList(host, &list);
		if (n < 0)
			return FR_DISK_ERR;
		int found = -1;
		for (int k = 0; k < n; k++)
			if (!strcmp(list[k].name, p->names[i]))
			{
				found = k;
				break;
			}
		const size_t used = strlen(host);
		if (found < 0)
		{
			snprintf(host + used, EMU_HOST_PATH - used, "/%s", p->names[i]);
			free(list);
			return i == p->depth - 1 ? FR_NO_FILE : FR_NO_PATH;
		}
		snprintf(host + used, EMU_HOST_PATH - used, "/%s", list[found].host);
		if (i == p->depth - 1)
		{
			if (entry)
				*entry = list[found];
		}
		else if (!list[found].isDir)
		{
			free(list);
			return FR_NO_PATH;
		}
		free(list);
	}
	return FR_OK;
}

/* --- the volume ---------------------------------------------------------------------- */

static void emu_cardCloseAll(void)
{
	for (int i = 0; i < EMU_CARD_OBJECTS; i++)
	{
		emu_cardObject_t *o = &emu_card.obj[i];
		if (o->used && !o->isDir && o->fd >= 0)
			close(o->fd);
		free(o->entries);
		memset(o, 0, sizeof *o);
	}
}

static FRESULT emu_cardMountNow(void)
{
	if (!emu_card.cardIn || !emu_card.root[0] || emu_hostKind(emu_card.root) != 1)
		return FR_NOT_READY;
	emu_card.mounted = 1;
	emu_card.cwd.depth = 0;
	return FR_OK;
}

/* every call starts here: the volume is mounted on first access */
static FRESULT emu_cardReady(void)
{
	if (!emu_card.registered)
		return FR_NOT_ENABLED;
	if (!emu_card.cardIn)
		return FR_NOT_READY;
	return emu_card.mounted ? FR_OK : emu_cardMountNow();
}

FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt)
{
	(void)path;
	emu_cardCloseAll();		/* FatFs clears the locks of the old file system object */
	emu_card.mounted = 0;
	if (!fs)
	{
		emu_card.registered = 0;
		return FR_OK;
	}
	emu_card.registered = 1;
	return opt == 1 ? emu_cardMountNow() : FR_OK;
}

FRESULT f_mkfs(const TCHAR *path, BYTE opt, DWORD au, void *work, UINT len)
{
	(void)path;
	(void)opt;
	(void)au;
	(void)work;
	(void)len;
	return FR_DENIED;	/* the emulator never presents an unformatted card */
}

void MX_FATFS_Init(void)
{
}

void MX_FATFS_ReInit(void)
{
}

uint8_t BSP_SD_IsDetected(void)
{
	return emu_card.cardIn ? SD_PRESENT : SD_NOT_PRESENT;
}

static void emu_cardInput(const emu_input_t *in)
{
	emu_card.cardIn = in->b ? 1 : 0;
	if (!emu_card.cardIn)
		emu_card.mounted = 0;	/* a card swap: the next access mounts again */
}

void emu_inCard(uint64_t t, int inserted)
{
	emu_inputPush(t, EMU_IN_CARD, 0, inserted ? 1 : 0, 0.0);
}

/* --- open objects ---------------------------------------------------------------------- */

/* the open object that holds the lock: a file or a sub-directory */
static emu_cardObject_t *emu_cardLocked(void)
{
	for (int i = 0; i < EMU_CARD_OBJECTS; i++)
		if (emu_card.obj[i].used && strcmp(emu_card.obj[i].host, emu_card.root))
			return &emu_card.obj[i];
	return NULL;
}

static emu_cardObject_t *emu_cardNew(_FDID *id)
{
	for (int i = 0; i < EMU_CARD_OBJECTS; i++)
		if (!emu_card.obj[i].used)
		{
			emu_cardObject_t *o = &emu_card.obj[i];
			memset(o, 0, sizeof *o);
			o->used = 1;
			o->fd = -1;
			o->cookie = ++emu_card.cookies;
			id->fs = &SDFatFS;
			id->id = (WORD)(i + 1);
			id->sclust = o->cookie;
			return o;
		}
	return NULL;
}

static emu_cardObject_t *emu_cardFind(const _FDID *id, int isDir)
{
	if (id->id < 1u || id->id > EMU_CARD_OBJECTS)
		return NULL;
	emu_cardObject_t *o = &emu_card.obj[id->id - 1u];
	return o->used && o->isDir == isDir && o->cookie == id->sclust ? o : NULL;
}

static void emu_cardFree(emu_cardObject_t *o, _FDID *id)
{
	if (!o->isDir && o->fd >= 0)
		close(o->fd);
	free(o->entries);
	memset(o, 0, sizeof *o);
	id->id = 0;
}

static void emu_cardInfo(const emu_hostEntry_t *e, FILINFO *fno)
{
	fno->fsize = (FSIZE_t)e->size;
	fno->fdate = 0;
	fno->ftime = 0;
	fno->fattrib = e->isDir ? AM_DIR : AM_ARC;
	snprintf(fno->fname, sizeof fno->fname, "%s", e->name);
}

/* --- files ------------------------------------------------------------------------------- */

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode)
{
	FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardPath_t p;
	r = emu_cardParse(path, &p);
	if (r == FR_OK && p.depth == 0)
		r = FR_INVALID_NAME;
	if (r != FR_OK)
		return r;
	char host[EMU_HOST_PATH];
	emu_hostEntry_t entry;
	r = emu_cardResolve(&p, host, &entry);
	const int exists = r == FR_OK;
	if (r != FR_OK && r != FR_NO_FILE)
		return r;
	const int write = (mode & FA_WRITE) != 0, read = (mode & FA_READ) != 0;
	const int create = (mode & (FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW)) != 0;
	if (exists && entry.isDir)
		return create ? FR_DENIED : FR_NO_FILE;
	if (!exists && !create)
		return FR_NO_FILE;
	if (exists && (mode & FA_CREATE_NEW))
		return FR_EXIST;
	emu_cardObject_t *held = emu_cardLocked();
	if (held)
		return !strcmp(held->host, host) && (write || held->write) ? FR_LOCKED : FR_TOO_MANY_OPEN_FILES;
	const int fd = emu_hostOpen(host, write, !exists, exists && (mode & FA_CREATE_ALWAYS));
	if (fd < 0)
		return FR_DISK_ERR;
	if (!exists)
	{
		char dir[EMU_HOST_PATH];
		snprintf(dir, sizeof dir, "%s", host);
		*strrchr(dir, '/') = '\0';
		emu_hostOrderLast(dir, host);	/* a new entry comes last in its directory */
	}
	emu_cardObject_t *o = emu_cardNew(&fp->obj);
	if (!o)
	{
		close(fd);
		return FR_TOO_MANY_OPEN_FILES;
	}
	snprintf(o->host, sizeof o->host, "%s", host);
	o->fd = fd;
	o->read = read;
	o->write = write;
	o->size = (uint64_t)emu_hostSize(fd);
	fp->flag = mode;
	fp->err = 0;
	fp->obj.objsize = (FSIZE_t)o->size;
	fp->fptr = (mode & FA_OPEN_APPEND) == FA_OPEN_APPEND ? (FSIZE_t)o->size : 0;
	return FR_OK;
}

FRESULT f_close(FIL *fp)
{
	const FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardObject_t *o = emu_cardFind(&fp->obj, 0);
	if (!o)
		return FR_INVALID_OBJECT;
	emu_cardFree(o, &fp->obj);
	return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
	*br = 0;
	const FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardObject_t *o = emu_cardFind(&fp->obj, 0);
	if (!o)
		return FR_INVALID_OBJECT;
	if (!o->read)
		return FR_DENIED;
	const ssize_t n = pread(o->fd, buff, btr, (off_t)fp->fptr);
	if (n < 0)
		return FR_DISK_ERR;
	*br = (UINT)n;
	fp->fptr += (FSIZE_t)n;
	return FR_OK;
}

FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
	*bw = 0;
	const FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardObject_t *o = emu_cardFind(&fp->obj, 0);
	if (!o)
		return FR_INVALID_OBJECT;
	if (!o->write)
		return FR_DENIED;
	const ssize_t n = pwrite(o->fd, buff, btw, (off_t)fp->fptr);
	if (n < 0)
		return FR_DISK_ERR;
	*bw = (UINT)n;
	fp->fptr += (FSIZE_t)n;
	if (fp->fptr > o->size)
		o->size = fp->fptr;
	fp->obj.objsize = (FSIZE_t)o->size;
	return FR_OK;
}

FRESULT f_lseek(FIL *fp, FSIZE_t ofs)
{
	const FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardObject_t *o = emu_cardFind(&fp->obj, 0);
	if (!o)
		return FR_INVALID_OBJECT;
	if (ofs > o->size)
	{
		if (!o->write)
			ofs = (FSIZE_t)o->size;	/* a read-only file clips the pointer at its end */
		else
		{
			if (ftruncate(o->fd, (off_t)ofs) != 0)
				return FR_DISK_ERR;
			o->size = ofs;
			fp->obj.objsize = ofs;
		}
	}
	fp->fptr = ofs;
	return FR_OK;
}

/* --- directories ------------------------------------------------------------------------ */

FRESULT f_opendir(DIR *dp, const TCHAR *path)
{
	FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardPath_t p;
	r = emu_cardParse(path, &p);
	if (r != FR_OK)
		return r;
	char host[EMU_HOST_PATH];
	emu_hostEntry_t entry;
	r = emu_cardResolve(&p, host, &entry);
	if (r == FR_NO_FILE || (r == FR_OK && p.depth > 0 && !entry.isDir))
		return FR_NO_PATH;
	if (r != FR_OK)
		return r;
	if (p.depth > 0)
	{
		emu_cardObject_t *held = emu_cardLocked();
		if (held && (strcmp(held->host, host) || !held->isDir))
			return FR_TOO_MANY_OPEN_FILES;
	}
	emu_hostEntry_t *list;
	const int n = emu_cardList(host, &list);
	if (n < 0)
		return FR_DISK_ERR;
	emu_cardObject_t *o = emu_cardNew(&dp->obj);
	if (!o)
	{
		free(list);
		return FR_TOO_MANY_OPEN_FILES;
	}
	o->isDir = 1;
	snprintf(o->host, sizeof o->host, "%s", host);
	o->entries = list;
	o->count = n;
	return FR_OK;
}

FRESULT f_closedir(DIR *dp)
{
	const FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardObject_t *o = emu_cardFind(&dp->obj, 1);
	if (!o)
		return FR_INVALID_OBJECT;
	emu_cardFree(o, &dp->obj);
	return FR_OK;
}

FRESULT f_readdir(DIR *dp, FILINFO *fno)
{
	const FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardObject_t *o = emu_cardFind(&dp->obj, 1);
	if (!o)
		return FR_INVALID_OBJECT;
	if (!fno)
	{
		o->pos = 0;		/* f_rewinddir() */
		return FR_OK;
	}
	if (o->pos >= o->count)
	{
		fno->fname[0] = '\0';
		return FR_OK;
	}
	emu_cardInfo(&o->entries[o->pos++], fno);
	return FR_OK;
}

/* FatFs's pattern matching: case-insensitive, ? one character, * any run */
static int emu_cardMatch(const char *pat, const char *name)
{
	while (*pat)
	{
		if (*pat == '*')
		{
			while (*pat == '*')
				pat++;
			if (!*pat)
				return 1;
			for (;; name++)
			{
				if (emu_cardMatch(pat, name))
					return 1;
				if (!*name)
					return 0;
			}
		}
		if (!*name)
			return 0;
		if (*pat != '?' && emu_cardUpper((unsigned char)*pat) != emu_cardUpper((unsigned char)*name))
			return 0;
		pat++;
		name++;
	}
	return *name == '\0';
}

FRESULT f_findnext(DIR *dp, FILINFO *fno)
{
	emu_cardObject_t *o = emu_cardFind(&dp->obj, 1);
	for (;;)
	{
		const FRESULT r = f_readdir(dp, fno);
		if (r != FR_OK || !fno || !fno->fname[0])
			return r;
		if (o && emu_cardMatch(o->pattern, fno->fname))
			return FR_OK;
	}
}

FRESULT f_findfirst(DIR *dp, FILINFO *fno, const TCHAR *path, const TCHAR *pattern)
{
	const FRESULT r = f_opendir(dp, path);
	if (r != FR_OK)
		return r;
	emu_cardObject_t *o = emu_cardFind(&dp->obj, 1);
	snprintf(o->pattern, sizeof o->pattern, "%s", pattern);
	return f_findnext(dp, fno);
}

/* --- names ------------------------------------------------------------------------------ */

FRESULT f_stat(const TCHAR *path, FILINFO *fno)
{
	FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardPath_t p;
	r = emu_cardParse(path, &p);
	if (r == FR_OK && p.depth == 0)
		r = FR_INVALID_NAME;	/* the root itself */
	if (r != FR_OK)
		return r;
	char host[EMU_HOST_PATH];
	emu_hostEntry_t entry;
	r = emu_cardResolve(&p, host, &entry);
	if (r == FR_OK && fno)
		emu_cardInfo(&entry, fno);
	return r;
}

FRESULT f_mkdir(const TCHAR *path)
{
	FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardPath_t p;
	r = emu_cardParse(path, &p);
	if (r == FR_OK && p.depth == 0)
		r = FR_INVALID_NAME;
	if (r != FR_OK)
		return r;
	char host[EMU_HOST_PATH];
	r = emu_cardResolve(&p, host, NULL);
	if (r == FR_OK)
		return FR_EXIST;
	if (r != FR_NO_FILE)
		return r;
	if (emu_hostMkdir(host) != 0)
		return FR_DENIED;
	char dir[EMU_HOST_PATH];
	snprintf(dir, sizeof dir, "%s", host);
	*strrchr(dir, '/') = '\0';
	emu_hostOrderLast(dir, host);
	return FR_OK;
}

FRESULT f_unlink(const TCHAR *path)
{
	FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardPath_t p;
	r = emu_cardParse(path, &p);
	if (r == FR_OK && p.depth == 0)
		r = FR_INVALID_NAME;
	if (r != FR_OK)
		return r;
	char host[EMU_HOST_PATH];
	emu_hostEntry_t entry;
	r = emu_cardResolve(&p, host, &entry);
	if (r != FR_OK)
		return r;
	for (int i = 0; i < EMU_CARD_OBJECTS; i++)
		if (emu_card.obj[i].used && !strcmp(emu_card.obj[i].host, host))
			return FR_LOCKED;	/* the object is open */
	if (entry.isDir)
	{
		if (emu_card.cwd.depth == p.depth && !memcmp(emu_card.cwd.names, p.names, sizeof p.names[0] * (size_t)p.depth))
			return FR_DENIED;	/* the current directory */
		emu_hostEntry_t *list;
		const int n = emu_cardList(host, &list);
		free(list);
		if (n != 0)
			return FR_DENIED;	/* not empty */
	}
	return emu_hostRemove(host, entry.isDir) == 0 ? FR_OK : FR_DENIED;
}

FRESULT f_chdir(const TCHAR *path)
{
	FRESULT r = emu_cardReady();
	if (r != FR_OK)
		return r;
	emu_cardPath_t p;
	r = emu_cardParse(path, &p);
	if (r != FR_OK)
		return r;
	char host[EMU_HOST_PATH];
	emu_hostEntry_t entry;
	r = emu_cardResolve(&p, host, &entry);
	if (r == FR_NO_FILE || (r == FR_OK && p.depth > 0 && !entry.isDir))
		return FR_NO_PATH;
	if (r != FR_OK)
		return r;
	emu_card.cwd = p;
	return FR_OK;
}

/* --- power-on ------------------------------------------------------------------------------ */

void emu_cardReset(const emu_config_t *cfg)
{
	emu_cardCloseAll();
	memset(&emu_card, 0, sizeof emu_card);
	if (cfg->cardDir)
		snprintf(emu_card.root, sizeof emu_card.root, "%s", cfg->cardDir);
	emu_card.cardIn = cfg->cardIn && cfg->cardDir ? 1 : 0;
	memset(&SDFatFS, 0, sizeof SDFatFS);
	memset(&SDFile, 0, sizeof SDFile);
	emu_inputHandler(EMU_IN_CARD, emu_cardInput);
}
