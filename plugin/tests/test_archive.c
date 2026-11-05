/*
 * test_archive.c
 *
 * P8: a card folder as the plugin's state (plan E9.2 Task 7): a round trip keeps the files, the
 * folders and the card's order (birth times, not names); names the device cannot see are left
 * out; a damaged archive is refused.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "emu_hostfs.h"
#include "plug_archive.h"
#include "seqc_test.h"

typedef struct
{
	uint8_t *p;
	size_t n, cap, at;
} mem_t;

static int memPut(void *ctx, const void *p, size_t n)
{
	mem_t *m = ctx;
	if (m->n + n > m->cap)
	{
		m->cap = (m->n + n) * 2u;
		m->p = realloc(m->p, m->cap);
	}
	memcpy(m->p + m->n, p, n);
	m->n += n;
	return 0;
}

static int memGet(void *ctx, void *p, size_t n)
{
	mem_t *m = ctx;
	if (m->at + n > m->n)
		return -1;
	memcpy(p, m->p + m->at, n);
	m->at += n;
	return 0;
}

static char root[512];

/* a fresh folder's path under the test's own temporary folder, in a buffer the next call reuses */
static const char *fresh(const char *name)
{
	static char p[1024];
	if (!root[0])
	{
		snprintf(root, sizeof root, "%s/plug-archive-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
		if (!mkdtemp(root))
			abort();
	}
	snprintf(p, sizeof p, "%s/%s", root, name);
	return p;
}

static void file(const char *dir, const char *name, const char *text)
{
	char p[1024];
	snprintf(p, sizeof p, "%s/%s", dir, name);
	const int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0 || write(fd, text, strlen(text)) != (ssize_t)strlen(text))
		abort();
	close(fd);
	nanosleep(&(const struct timespec){ 0, 2000000L }, NULL);	/* distinct birth times */
}

/* "NAME:size NAME/ ..." of a folder in the card's order, recursively */
static void listing(const char *dir, char *out, size_t n)
{
	emu_hostEntry_t *list;
	const int count = emu_hostList(dir, &list, NULL);
	for (int i = 0; i < count; i++)
	{
		char p[1024];
		snprintf(p, sizeof p, "%s/%s", dir, list[i].host);
		const size_t len = strlen(out);
		snprintf(out + len, n - len, "%s%s ", list[i].name, list[i].isDir ? "/" : "");
		if (list[i].isDir)
		{
			listing(p, out, n);
			strncat(out, "| ", n - strlen(out) - 1u);
		}
		else
		{
			char text[64] = { 0 };
			const int fd = open(p, O_RDONLY);
			(void)!read(fd, text, sizeof text - 1u);
			close(fd);
			const size_t l2 = strlen(out);
			snprintf(out + l2, n - l2, "=%s ", text);
		}
	}
	free(list);
}

TEST(P8_archive_roundtrip)
{
	char src[1024];
	snprintf(src, sizeof src, "%s", fresh("src"));
	char dirs[1024];
	ASSERT_EQ(mkdir(src, 0755), 0);
	/* the card's order: ZZZ.TXT, PROJ00, AAA.TXT; in PROJ00: 01-B.PAT, 00-A.PAT */
	file(src, "ZZZ.TXT", "zzz");
	snprintf(dirs, sizeof dirs, "%s/PROJ00", src);
	ASSERT_EQ(mkdir(dirs, 0755), 0);
	nanosleep(&(const struct timespec){ 0, 2000000L }, NULL);
	file(src, "AAA.TXT", "aaa");
	file(dirs, "01-B.PAT", "pattern one");
	file(dirs, "00-A.PAT", "pattern zero");
	char want[2048] = "";
	listing(src, want, sizeof want);
	ASSERT_EQ(strcmp(want, "ZZZ.TXT =zzz PROJ00/ 01-B.PAT =pattern one 00-A.PAT =pattern zero | AAA.TXT =aaa "), 0);
	mem_t m = { 0 };
	char err[256];
	ASSERT_EQ(plug_archiveWrite(src, memPut, &m, err, sizeof err), 0);
	ASSERT_EQ(memcmp(m.p, "LCHS\x01\x00\x00\x00", 8), 0);
	const char *dst = fresh("dst");
	ASSERT_EQ(plug_archiveRead(dst, memGet, &m, err, sizeof err), 0);
	ASSERT_EQ(m.at, m.n);
	char got[2048] = "";
	listing(dst, got, sizeof got);
	printf("P8_archive_roundtrip: %zu bytes; %s\n", m.n, got);
	ASSERT_EQ(strcmp(got, want), 0);
	free(m.p);
}

TEST(P8_archive_skips)
{
	char src[1024];
	snprintf(src, sizeof src, "%s", fresh("skips"));
	ASSERT_EQ(mkdir(src, 0755), 0);
	file(src, "GOOD.TXT", "g");
	file(src, ".hidden", "h");
	file(src, "a name too long.txt", "x");
	mem_t m = { 0 };
	char err[256];
	ASSERT_EQ(plug_archiveWrite(src, memPut, &m, err, sizeof err), 0);
	const char *dst = fresh("skips-out");
	ASSERT_EQ(plug_archiveRead(dst, memGet, &m, err, sizeof err), 0);
	char got[512] = "";
	listing(dst, got, sizeof got);
	ASSERT_EQ(strcmp(got, "GOOD.TXT =g "), 0);
	free(m.p);
}

TEST(P8_archive_refused)
{
	char src[1024];
	snprintf(src, sizeof src, "%s", fresh("ref"));
	ASSERT_EQ(mkdir(src, 0755), 0);
	file(src, "ONE.TXT", "one");
	mem_t m = { 0 };
	char err[256];
	ASSERT_EQ(plug_archiveWrite(src, memPut, &m, err, sizeof err), 0);
	/* cut short */
	mem_t cut = m;
	cut.n -= 3;
	cut.at = 0;
	ASSERT_EQ(plug_archiveRead(fresh("ref-cut"), memGet, &cut, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "early") != NULL);
	/* another magic, another version */
	m.p[0] = 'X';
	m.at = 0;
	ASSERT_EQ(plug_archiveRead(fresh("ref-magic"), memGet, &m, err, sizeof err), -1);
	m.p[0] = 'L';
	m.p[4] = 2;
	m.at = 0;
	ASSERT_EQ(plug_archiveRead(fresh("ref-version"), memGet, &m, err, sizeof err), -1);
	m.p[4] = 1;
	/* a path that leaves the card: "../X.TXT" in place of "ONE.TXT" (same length) */
	memcpy(m.p + 11, "../X.TX", 7);
	m.at = 0;
	ASSERT_EQ(plug_archiveRead(fresh("ref-path"), memGet, &m, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "not a card's") != NULL);
	/* into a folder that holds something */
	memcpy(m.p + 11, "ONE.TXT", 7);
	m.at = 0;
	ASSERT_EQ(plug_archiveRead(src, memGet, &m, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "not empty") != NULL);
	free(m.p);
}

TEST_MAIN()
