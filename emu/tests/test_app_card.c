/*
 * test_app_card.c
 *
 * R4 (stage 1b spec 6.7, 4.8, 11, plan E16, E17, E21): the card folder of the app: the
 * order of a copy, stamped names and backups, the state of a folder, import,
 * export of the card and of one project, the first run's seed, and the paths.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <sys/stat.h>

#include "app_card.h"
#include "app_time.h"
#include "emu_test.h"

static void put(const char *dir, const char *name, const char *text) { emu_testWrite(dir, name, text, strlen(text)); }
static int exists(const char *dir, const char *rel)
{
	char p[2200];
	snprintf(p, sizeof p, "%s/%s", dir, rel);
	return emu_hostKind(p) >= 0;
}
static int holds(const char *dir, const char *rel, const char *text)
{
	char p[2200], b[64] = "";
	snprintf(p, sizeof p, "%s/%s", dir, rel);
	FILE *f = fopen(p, "rb");
	const size_t n = f ? fread(b, 1, sizeof b - 1u, f) : 0u;
	if (f)
		fclose(f);
	b[n] = '\0';
	return !strcmp(b, text);
}
static void countWarn(void *ctx, const char *what) { (void)what; (*(int *)ctx)++; }
static void home(app_paths_t *p)
{
	static char dir[512];
	emu_testTempDir(dir, sizeof dir);
	setenv("LICHOMAT_EMU_HOME", dir, 1);	/* E21 */
	if (app_pathsInit(p) != 0)
		exit(2);
}

#define STAMP 1793451909LL	/* 2026-10-31 13:05:09, local seconds */

TEST(R4_card_order)
{
	app_cardEntry_t l[3] = {
		{ .host = "B.SND", .name = "B.SND", .birthSec = 10, .seq = 0 },
		{ .host = "A.SND", .name = "A.SND", .birthSec = 10, .seq = 1 },
		{ .host = "C.SND", .name = "C.SND", .birthSec = 5, .seq = 2 },
	};
	app_cardOrder(l, 3, 0);	/* the backend's order: birth time, then name (spec 4.8) */
	ASSERT_EQ(strcmp(l[0].name, "C.SND"), 0);
	ASSERT_EQ(strcmp(l[1].name, "A.SND"), 0);
	app_cardOrder(l, 3, 1);	/* a FAT volume: readdir() order, the card's own (spec 6.7) */
	ASSERT_EQ(strcmp(l[0].name, "B.SND"), 0);
	ASSERT_EQ(strcmp(l[2].name, "C.SND"), 0);
}

TEST(R4_card_stamp)
{
	char s[64], dir[512], out[1024];
	ASSERT_EQ(app_stampName(0, "", "", s, sizeof s), 15);
	ASSERT_EQ(strcmp(s, "19700101-000000"), 0);
	app_stampName(11016LL * 86400, "", "", s, sizeof s);
	ASSERT_EQ(strcmp(s, "20000229-000000"), 0);
	app_stampName(STAMP, "crash-", ".txt", s, sizeof s);
	ASSERT_EQ(strcmp(s, "crash-20261031-130509.txt"), 0);
	ASSERT_EQ(app_stampName(0, "", "", s, 10u), 0);	/* does not fit */
	emu_testTempDir(dir, sizeof dir);
	ASSERT_EQ(app_backupName(dir, STAMP, NULL, out, sizeof out), 0);
	ASSERT_TRUE(strstr(out, "/20261031-130509") != NULL);
	mkdir(out, 0755);
	ASSERT_EQ(app_backupName(dir, STAMP, NULL, out, sizeof out), 0);	/* E17: a second one in that second */
	ASSERT_TRUE(strstr(out, "/20261031-130509-2") != NULL);
	ASSERT_EQ(app_backupName(dir, STAMP, "-PROJ03", out, sizeof out), 0);
	ASSERT_TRUE(strstr(out, "/20261031-130509-PROJ03") != NULL);
}

TEST(R4_card_state)
{
	char dir[512], sub[1024], names[256], x[1100];
	emu_testTempDir(dir, sizeof dir);
	snprintf(sub, sizeof sub, "%s/new", dir);
	ASSERT_EQ(app_cardState(sub), 1);	/* missing: seed it */
	mkdir(sub, 0755);
	snprintf(x, sizeof x, "%s/.fseventsd", sub);
	mkdir(x, 0755);
	put(sub, ".DS_Store", "x");
	ASSERT_EQ(app_cardState(sub), 1);	/* a fresh card that macOS mounted */
	ASSERT_EQ(app_cardNonDot(sub, names, sizeof names), 0);
	put(sub, "X.TXT", "x");
	ASSERT_EQ(app_cardState(sub), 0);
	ASSERT_EQ(app_cardNonDot(sub, names, sizeof names), 1);
	ASSERT_EQ(strcmp(names, "X.TXT"), 0);
	snprintf(x, sizeof x, "%s/X.TXT", sub);
	ASSERT_EQ(app_cardState(x), -1);
	char card[512], proj[1024];
	int p = -1;
	emu_testTempDir(card, sizeof card);
	const uint8_t two = 2u, bad = 200u;
	emu_testWrite(card, "PROJ.IDX", &two, 1u);
	snprintf(proj, sizeof proj, "%s/proj02", card);
	mkdir(proj, 0755);
	ASSERT_EQ(app_cardCheck(card, &p), 0);	/* no PRJ.NFO: a notice (spec 11) */
	ASSERT_EQ(p, 2);
	put(proj, "PRJ.NFO", "TESTPROJ");
	ASSERT_EQ(app_cardCheck(card, &p), 1);
	emu_testWrite(card, "PROJ.IDX", &bad, 1u);
	ASSERT_EQ(app_cardCheck(card, &p), 0);	/* 64-255 count as 0 (E4.7) */
	ASSERT_EQ(p, 0);
}

TEST(R4_card_import)
{
	app_paths_t p;
	home(&p);
	mkdir(p.card, 0755);
	put(p.card, "OLD.TXT", "old");
	char src[512], proj[1024], backup[1024], dst[1100];
	emu_testTempDir(src, sizeof src);
	snprintf(proj, sizeof proj, "%s/PROJ00", src);
	mkdir(proj, 0755);
	put(proj, "PRJ.NFO", "TESTPROJ");
	put(proj, "01-B.SND", "b");	/* born before 00-A.SND */
	put(proj, "00-A.SND", "a");
	put(src, ".DS_Store", "x");
	put(src, "Long Name.txt", "x");
	int warnings = 0;
	ASSERT_EQ(app_cardImport(&p, src, STAMP, backup, sizeof backup, countWarn, &warnings), 0);
	ASSERT_TRUE(strstr(backup, "/Card backups/20261031-130509") != NULL);	/* step 2 */
	ASSERT_TRUE(exists(backup, "OLD.TXT"));
	ASSERT_FALSE(exists(p.card, ".DS_Store"));
	ASSERT_EQ(warnings, 1);
	snprintf(dst, sizeof dst, "%s/PROJ00", p.card);
	emu_hostEntry_t *l;
	ASSERT_EQ(emu_hostList(dst, &l, NULL), 3);
	ASSERT_EQ(strcmp(l[1].name, "01-B.SND"), 0);	/* step 3: the source's order */
	free(l);
	ASSERT_EQ(app_cardImport(&p, p.card, STAMP, backup, sizeof backup, NULL, NULL), -1);	/* never its own folder */
}

TEST(R4_card_export)
{
	char card[512], dst[512], err[256], fse[1100];
	emu_testCard(card, sizeof card, 120u);
	emu_testTempDir(dst, sizeof dst);
	snprintf(fse, sizeof fse, "%s/.fseventsd", dst);
	mkdir(fse, 0755);
	ASSERT_EQ(app_cardExport(card, dst, err, sizeof err, NULL, NULL), 0);	/* only dot entries: accepted */
	ASSERT_TRUE(holds(dst, "PROJ00/PRJ.NFO", "TESTPROJ"));
	ASSERT_EQ(app_cardExport(card, dst, err, sizeof err, NULL, NULL), -2);	/* refused, with the names (spec 11) */
	ASSERT_TRUE(strstr(err, "PROJ00") != NULL);
}

TEST(R4_card_project)
{
	app_paths_t p;
	home(&p);
	char path[1200], target[512], err[256];
	mkdir(p.card, 0755);
	snprintf(path, sizeof path, "%s/PROJ01", p.card);
	mkdir(path, 0755);
	put(path, "PRJ.NFO", "NEWPROJ");
	emu_testTempDir(target, sizeof target);	/* the owner's card: PROJ00, proj01, PROJ.IDX */
	snprintf(path, sizeof path, "%s/PROJ00", target);
	mkdir(path, 0755);
	put(path, "PRJ.NFO", "OTHER");
	snprintf(path, sizeof path, "%s/proj01", target);
	mkdir(path, 0755);
	put(path, "PRJ.NFO", "OLDPROJ");
	put(target, "PROJ.IDX", "0");
	ASSERT_EQ(app_cardExportProject(&p, 1, target, 0, STAMP, err, sizeof err, NULL, NULL), 2);	/* the app asks */
	ASSERT_EQ(app_cardExportProject(&p, 1, target, 1, STAMP, err, sizeof err, NULL, NULL), 0);
	snprintf(path, sizeof path, "%s/20261031-130509-PROJ01", p.backups);
	ASSERT_TRUE(holds(path, "PRJ.NFO", "OLDPROJ"));	/* the replaced one, on the Mac */
	ASSERT_TRUE(holds(target, "PROJ01/PRJ.NFO", "NEWPROJ"));
	ASSERT_TRUE(holds(target, "PROJ00/PRJ.NFO", "OTHER"));	/* the other projects stay */
	ASSERT_TRUE(holds(target, "PROJ.IDX", "0"));
}

TEST(R4_card_seed)
{
	char dir[512], card[1100], err[256];
	emu_testTempDir(dir, sizeof dir);
	snprintf(card, sizeof card, "%s/Card", dir);
	ASSERT_EQ(app_cardSeed(EMU_TEST_RENDER, card, err, sizeof err), 0);	/* lichomat-render --seed-card (spec 6.7) */
	int p = -1;
	ASSERT_EQ(app_cardCheck(card, &p), 1);
	ASSERT_EQ(p, 0);
	ASSERT_TRUE(holds(card, "PROJ00/PRJ.NFO", "Init"));
}

/* a copy that fails half way puts the old card back and leaves no partial one; the backup is gone with it */
TEST(R4_card_rollback)
{
	app_paths_t p;
	home(&p);
	mkdir(p.card, 0755);
	put(p.card, "OLD.TXT", "old");
	char src[512], backup[1024], x[1100];
	emu_testTempDir(src, sizeof src);
	put(src, "A.TXT", "a");
	put(src, "B.TXT", "b");
	snprintf(x, sizeof x, "%s/B.TXT", src);
	ASSERT_EQ(chmod(x, 0), 0);	/* it cannot be read: the copy stops there */
	ASSERT_EQ(app_cardImport(&p, src, STAMP, backup, sizeof backup, NULL, NULL), -1);
	chmod(x, 0644);
	ASSERT_TRUE(holds(p.card, "OLD.TXT", "old"));
	ASSERT_FALSE(exists(p.card, "A.TXT"));
	char list[256];
	ASSERT_EQ(app_cardNonDot(p.backups, list, sizeof list), 0);
}

/* the card's own folder is refused, and any folder that holds it or is inside it; a backup is a good source */
TEST(R4_card_own)
{
	app_paths_t p;
	home(&p);
	mkdir(p.card, 0755);
	char sub[1100], backup[1024], again[1024];
	snprintf(sub, sizeof sub, "%s/PROJ00", p.card);
	mkdir(sub, 0755);
	put(sub, "PRJ.NFO", "FIRST");
	ASSERT_EQ(app_cardImport(&p, p.card, STAMP, backup, sizeof backup, NULL, NULL), -1);
	ASSERT_EQ(app_cardImport(&p, sub, STAMP, backup, sizeof backup, NULL, NULL), -1);	/* inside it */
	ASSERT_EQ(app_cardImport(&p, p.home, STAMP, backup, sizeof backup, NULL, NULL), -1);	/* holds it */
	ASSERT_TRUE(holds(p.card, "PROJ00/PRJ.NFO", "FIRST"));	/* nothing was moved */
	char src[512];
	emu_testTempDir(src, sizeof src);
	snprintf(sub, sizeof sub, "%s/PROJ00", src);
	mkdir(sub, 0755);
	put(sub, "PRJ.NFO", "SECOND");
	ASSERT_EQ(app_cardImport(&p, src, STAMP, backup, sizeof backup, NULL, NULL), 0);
	ASSERT_TRUE(holds(backup, "PROJ00/PRJ.NFO", "FIRST"));
	ASSERT_EQ(app_cardImport(&p, backup, STAMP + 1, again, sizeof again, NULL, NULL), 0);	/* restore the first card */
	ASSERT_TRUE(holds(p.card, "PROJ00/PRJ.NFO", "FIRST"));
	ASSERT_TRUE(holds(again, "PROJ00/PRJ.NFO", "SECOND"));
}

/* the entries of dir that are not dot entries, as "A, B": whatever order readdir() gives, sorted */
static void listing(const char *dir, char *out, size_t n)
{
	app_cardEntry_t *l = NULL;
	const int c = app_cardList(dir, 0, &l, NULL, NULL);
	char *names[64];
	int k = 0;
	for (int i = 0; i < c && k < 64; i++)
		names[k++] = l[i].host;
	for (int i = 0; i < k; i++)
		for (int j = i + 1; j < k; j++)
			if (strcmp(names[i], names[j]) > 0)
			{
				char *t = names[i];
				names[i] = names[j];
				names[j] = t;
			}
	out[0] = '\0';
	for (int i = 0; i < k; i++)
		snprintf(out + strlen(out), n - strlen(out), "%s%s", i ? ", " : "", names[i]);
	free(l);
}

/* the card and a target for PROJ01: the card's has an unreadable file after a good one; the target is the owner's card, with
 * the project that is to be replaced under a lower-case name */
static void projectFixture(app_paths_t *p, char *target, size_t tn, int withOld)
{
	char path[1200];
	home(p);
	mkdir(p->card, 0755);
	snprintf(path, sizeof path, "%s/PROJ01", p->card);
	mkdir(path, 0755);
	put(path, "PRJ.NFO", "NEWPROJ");
	put(path, "A.SND", "a");
	put(path, "B.SND", "b");
	snprintf(path, sizeof path, "%s/PROJ01/B.SND", p->card);
	if (chmod(path, 0) != 0)	/* it cannot be read: the copy stops there */
		exit(2);
	emu_testTempDir(target, tn);
	snprintf(path, sizeof path, "%s/PROJ00", target);
	mkdir(path, 0755);
	put(path, "PRJ.NFO", "OTHER");
	snprintf(path, sizeof path, "%s/proj01", target);
	if (withOld)
	{
		mkdir(path, 0755);
		put(path, "PRJ.NFO", "OLDPROJ");
		put(path, "X.SND", "x");
	}
	put(target, "PROJ.IDX", "0");
}

static void unreadableBack(const app_paths_t *p)
{
	char path[1200];
	snprintf(path, sizeof path, "%s/PROJ01/B.SND", p->card);
	chmod(path, 0644);
}

/* the new copy fails half way: the owner's project is as it was, no partial PROJ01 and no temporary folder is left */
TEST(R4_card_projfail)
{
	app_paths_t p;
	char target[512], err[512], names[256];
	projectFixture(&p, target, sizeof target, 1);
	ASSERT_EQ(app_cardExportProject(&p, 1, target, 1, STAMP, err, sizeof err, NULL, NULL), -1);
	unreadableBack(&p);
	ASSERT_TRUE(holds(target, "proj01/PRJ.NFO", "OLDPROJ"));
	ASSERT_TRUE(holds(target, "proj01/X.SND", "x"));
	ASSERT_TRUE(holds(target, "PROJ00/PRJ.NFO", "OTHER"));
	listing(target, names, sizeof names);
	ASSERT_EQ(strcmp(names, "PROJ.IDX, PROJ00, proj01"), 0);
	ASSERT_TRUE(strstr(err, "unchanged") != NULL);
	ASSERT_EQ(app_cardNonDot(p.backups, names, sizeof names), 0);	/* nothing was taken from the target yet */
}

/* the same without a project in the way: the target gets nothing */
TEST(R4_card_projnew)
{
	app_paths_t p;
	char target[512], err[512], names[256];
	projectFixture(&p, target, sizeof target, 0);
	ASSERT_EQ(app_cardExportProject(&p, 1, target, 0, STAMP, err, sizeof err, NULL, NULL), -1);
	unreadableBack(&p);
	listing(target, names, sizeof names);
	ASSERT_EQ(strcmp(names, "PROJ.IDX, PROJ00"), 0);
}

/* a good replace leaves the new project under its own name, the old one in Card backups, and no temporary folder */
TEST(R4_card_projclean)
{
	app_paths_t p;
	char target[512], err[512], names[256], path[1200];
	projectFixture(&p, target, sizeof target, 1);
	unreadableBack(&p);
	ASSERT_EQ(app_cardExportProject(&p, 1, target, 1, STAMP, err, sizeof err, NULL, NULL), 0);
	listing(target, names, sizeof names);
	ASSERT_EQ(strcmp(names, "PROJ.IDX, PROJ00, PROJ01"), 0);
	ASSERT_TRUE(holds(target, "PROJ01/PRJ.NFO", "NEWPROJ"));
	ASSERT_TRUE(holds(target, "PROJ01/B.SND", "b"));
	snprintf(path, sizeof path, "%s/20261031-130509-PROJ01", p.backups);
	ASSERT_TRUE(holds(path, "X.SND", "x"));
}

/* a stale temporary folder of an earlier run is no reason to fail: the next free name is taken */
TEST(R4_card_projstale)
{
	app_paths_t p;
	char target[512], err[512], names[256], path[1200];
	projectFixture(&p, target, sizeof target, 1);
	unreadableBack(&p);
	snprintf(path, sizeof path, "%s/EMUN00.TMP", target);
	mkdir(path, 0755);
	snprintf(path, sizeof path, "%s/EMUO00.TMP", target);
	mkdir(path, 0755);
	ASSERT_EQ(app_cardExportProject(&p, 1, target, 1, STAMP, err, sizeof err, NULL, NULL), 0);
	listing(target, names, sizeof names);
	ASSERT_EQ(strcmp(names, "EMUN00.TMP, EMUO00.TMP, PROJ.IDX, PROJ00, PROJ01"), 0);	/* theirs are theirs */
	ASSERT_TRUE(holds(target, "PROJ01/PRJ.NFO", "NEWPROJ"));
}

/* the old project cannot be copied into Card backups: nothing is taken from the target */
TEST(R4_card_projnobackup)
{
	app_paths_t p;
	char target[512], err[512], names[256];
	projectFixture(&p, target, sizeof target, 1);
	unreadableBack(&p);
	ASSERT_EQ(chmod(p.backups, 0555), 0);
	const int rc = app_cardExportProject(&p, 1, target, 1, STAMP, err, sizeof err, NULL, NULL);
	chmod(p.backups, 0755);
	ASSERT_EQ(rc, -1);
	ASSERT_TRUE(holds(target, "proj01/PRJ.NFO", "OLDPROJ"));
	listing(target, names, sizeof names);
	ASSERT_EQ(strcmp(names, "PROJ.IDX, PROJ00, proj01"), 0);
	ASSERT_TRUE(strstr(err, "Card backups") != NULL);
}

static char gChmodDir[1100];
static void chmodWarn(void *ctx, const char *what) { (void)ctx; (void)what; chmod(gChmodDir, 0555); }

/* the copy fails and the folder it made cannot be cleared (here a folder of it turns read-only while the copy is on its way
 * to the next one), so the old card cannot be put back: the result says so and names the backup, where the old card is */
TEST(R4_card_noreturn)
{
	app_paths_t p;
	home(&p);
	mkdir(p.card, 0755);
	put(p.card, "OLD.TXT", "old");
	char src[512], backup[1024], a[1100], b[1100];
	emu_testTempDir(src, sizeof src);
	snprintf(a, sizeof a, "%s/A", src);
	snprintf(b, sizeof b, "%s/B", src);
	mkdir(a, 0755);
	put(a, "F1.TXT", "f");	/* copied; its folder then turns read-only */
	mkdir(b, 0755);
	put(b, "B1.TXT", "b");
	put(b, "Long Name.txt", "x");	/* a warning, while B is listed */
	snprintf(a, sizeof a, "%s/B/B1.TXT", src);
	ASSERT_EQ(chmod(a, 0), 0);	/* the copy of B stops here */
	snprintf(gChmodDir, sizeof gChmodDir, "%s/A", p.card);
	const int rc = app_cardImport(&p, src, STAMP, backup, sizeof backup, chmodWarn, NULL);
	chmod(gChmodDir, 0755);
	chmod(a, 0644);
	ASSERT_EQ(rc, -2);
	ASSERT_TRUE(strstr(backup, "/Card backups/20261031-130509") != NULL);
	ASSERT_TRUE(holds(backup, "OLD.TXT", "old"));
}

/* an export into the app's own card, into a folder in it or into one that holds it is refused, before anything is moved */
TEST(R4_card_exportown)
{
	app_paths_t p;
	home(&p);
	char sub[1100], err[512], names[256], path[1200];
	mkdir(p.card, 0755);
	snprintf(sub, sizeof sub, "%s/PROJ01", p.card);
	mkdir(sub, 0755);
	put(sub, "PRJ.NFO", "FIRST");
	ASSERT_EQ(app_cardExport(p.card, p.card, err, sizeof err, NULL, NULL), -3);
	ASSERT_TRUE(strstr(err, "card") != NULL);
	ASSERT_EQ(app_cardExport(p.card, sub, err, sizeof err, NULL, NULL), -3);	/* inside it */
	snprintf(path, sizeof path, "%s/NEW", p.card);
	ASSERT_EQ(app_cardExport(p.card, path, err, sizeof err, NULL, NULL), -3);	/* inside it, not there yet */
	ASSERT_EQ(emu_hostKind(path), -1);
	ASSERT_EQ(app_cardExport(p.card, p.home, err, sizeof err, NULL, NULL), -3);	/* holds it */
	snprintf(path, sizeof path, "%s/../%s", p.card, strrchr(p.card, '/') + 1);
	ASSERT_EQ(app_cardExport(p.card, path, err, sizeof err, NULL, NULL), -3);	/* by another way round */
	ASSERT_EQ(app_cardExportProject(&p, 1, p.card, 1, STAMP, err, sizeof err, NULL, NULL), -1);	/* its own PROJ01 */
	ASSERT_TRUE(strstr(err, "card") != NULL);
	ASSERT_EQ(app_cardExportProject(&p, 1, p.home, 1, STAMP, err, sizeof err, NULL, NULL), -1);
	ASSERT_TRUE(holds(p.card, "PROJ01/PRJ.NFO", "FIRST"));
	listing(p.card, names, sizeof names);
	ASSERT_EQ(strcmp(names, "PROJ01"), 0);
	ASSERT_EQ(app_cardNonDot(p.backups, names, sizeof names), 0);
}

/* E21: LICHOMAT_EMU_HOME, else the Application Support folder under $HOME, made as far as it goes */
TEST(R4_card_paths)
{
	app_paths_t p;
	char dir[512];
	emu_testTempDir(dir, sizeof dir);
	unsetenv("LICHOMAT_EMU_HOME");
	setenv("HOME", dir, 1);
	ASSERT_EQ(app_pathsInit(&p), 0);
	ASSERT_TRUE(strstr(p.home, "/Library/Application Support/LICHOMAT Emu") != NULL);
	ASSERT_TRUE(strstr(p.card, "/LICHOMAT Emu/Card") != NULL);
	ASSERT_TRUE(strstr(p.backups, "/LICHOMAT Emu/Card backups") != NULL);
	ASSERT_TRUE(strstr(p.settings, "/LICHOMAT Emu/settings.txt") != NULL);
	ASSERT_EQ(emu_hostKind(p.backups), 1);
	ASSERT_EQ(emu_hostKind(p.card), -1);	/* the first run makes it */
	ASSERT_EQ(app_pathsInit(&p), 0);	/* a second time: nothing to make */
	unsetenv("HOME");
	ASSERT_EQ(app_pathsInit(&p), -1);
	setenv("LICHOMAT_EMU_HOME", dir, 1);
	ASSERT_EQ(app_pathsInit(&p), 0);
	ASSERT_EQ(strcmp(p.home, dir), 0);
}

TEST_MAIN()
