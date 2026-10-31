/*
 * test_card.c
 *
 * M9 (stage 1b spec 4.8, 12.1): the card backend, a host folder behind the
 * FatFs API. The storage scenario's operations and results; create-always
 * truncating in place; the directory order, birth time and not name; dot files
 * and names that are not 8.3 skipped, with one warning each; two files for one
 * slot; the lock of one open file or sub-directory; the card switch; a copy that
 * keeps the order and skips dot entries; and a kit save and load by the
 * firmware. The pure tests run the core with the card model alone and call
 * f_*() as presetManager.c does, through the global SDFile.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <sys/attr.h>
#include <time.h>

#include "stm32f7xx_hal.h"
#include "fatfs.h"
#include "Preset/PresetManager.h"
#include "emu_hostfs.h"
#include "emu_test.h"

/* the warnings about the card */
static unsigned warnings;
static char warning[4][1024];

static void recordWarning(void *ctx, const char *what)
{
	(void)ctx;
	if (warnings < 4u)
		snprintf(warning[warnings], sizeof warning[0], "%s", what);
	warnings++;
}

static char card[512];

/* the core with the card model alone, on a new empty card folder */
static void setup(void)
{
	emu_testTempDir(card, sizeof card);
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	cfg.warn = recordWarning;
	emu_corePowerOn(&cfg);
	emu_cardReset(&cfg);
}

static void hostDir(const char *name)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", card, name);
	if (mkdir(path, 0755) != 0)
	{
		perror(path);
		exit(2);
	}
}

/* the host names of a card folder in the backend's order, joined by spaces */
static void hostNames(const char *sub, char *out, size_t size)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", card, sub);
	emu_hostEntry_t *list;
	const int n = emu_hostList(path, &list, NULL);
	out[0] = '\0';
	for (int i = 0; i < n; i++)
		snprintf(out + strlen(out), size - strlen(out), "%s%s", i ? " " : "", list[i].host);
	free(list);
	if (n < 0)
		snprintf(out, size, "(cannot list)");
}

/* the wall clock in seconds; not time(), which the firmware's float 'time'
 * (graphic.c:25) takes the place of in these binaries */
static time_t wallSeconds(void)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	return now.tv_sec;
}

/* set the birth time of a card file on the host */
static void hostBirth(const char *sub, time_t sec, long nsec)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", card, sub);
	struct attrlist attrs;
	memset(&attrs, 0, sizeof attrs);
	attrs.bitmapcount = ATTR_BIT_MAP_COUNT;
	attrs.commonattr = ATTR_CMN_CRTIME;
	struct timespec ts = { sec, nsec };
	if (setattrlist(path, &attrs, &ts, sizeof ts, 0) != 0)
	{
		perror(path);
		exit(2);
	}
}

/* the names of a card directory through f_opendir() and f_readdir(), joined by
 * spaces, a directory with a trailing '/' */
static FRESULT cardNames(const char *path, char *out, size_t size)
{
	DIR dir;
	FILINFO fno;
	out[0] = '\0';
	FRESULT r = f_opendir(&dir, path);
	if (r != FR_OK)
		return r;
	while ((r = f_readdir(&dir, &fno)) == FR_OK && fno.fname[0])
		snprintf(out + strlen(out), size - strlen(out), "%s%s%s", out[0] ? " " : "", fno.fname,
			(fno.fattrib & AM_DIR) ? "/" : "");
	const FRESULT c = f_closedir(&dir);
	return r != FR_OK ? r : c;
}

static int64_t hostSize(const char *sub)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", card, sub);
	struct stat st;
	return stat(path, &st) == 0 ? (int64_t)st.st_size : -1;
}

/* create-always and write n bytes of data */
static FRESULT writeFile(const char *name, const void *data, UINT n)
{
	FRESULT r = f_open(&SDFile, name, FA_CREATE_ALWAYS | FA_WRITE);
	if (r != FR_OK)
		return r;
	UINT bw = 0;
	r = f_write(&SDFile, data, n, &bw);
	if (r == FR_OK && bw != n)
		r = FR_DISK_ERR;
	const FRESULT c = f_close(&SDFile);
	return r != FR_OK ? r : c;
}

TEST(M9_scenario)
{
	setup();
	FILINFO fno;
	UINT n = 0;
	ASSERT_EQ(f_mount(&SDFatFS, SDPath, 1), FR_OK);				/* 1 */
	ASSERT_EQ(f_stat("/proj00", &fno), FR_NO_FILE);				/* 2 */
	ASSERT_EQ(f_mkdir("/proj00"), FR_OK);						/* 3 */
	ASSERT_EQ(f_mkdir("/proj00"), FR_EXIST);					/* 4 */
	ASSERT_EQ(f_chdir("/proj00"), FR_OK);						/* 5 */
	ASSERT_EQ(f_open(&SDFile, "PRJ.NFO", FA_CREATE_ALWAYS | FA_WRITE), FR_OK);	/* 6 */
	ASSERT_EQ(f_write(&SDFile, "TESTPROJ", 8, &n), FR_OK);		/* 7 */
	ASSERT_EQ(n, 8);
	ASSERT_EQ(f_close(&SDFile), FR_OK);							/* 8 */
	uint8_t glo[30];
	for (unsigned i = 0; i < sizeof glo; i++)
		glo[i] = (uint8_t)i;
	ASSERT_EQ(f_open(&SDFile, "glo.cfg", FA_CREATE_ALWAYS | FA_WRITE), FR_OK);	/* 9 */
	ASSERT_EQ(f_write(&SDFile, glo, sizeof glo, &n), FR_OK);	/* 10 */
	ASSERT_EQ(n, 30);
	ASSERT_EQ(f_close(&SDFile), FR_OK);							/* 11 */
	ASSERT_EQ(f_open(&SDFile, "GLO.CFG", FA_READ), FR_OK);		/* 12 */
	uint8_t in[30];
	memset(in, 0xEE, sizeof in);
	ASSERT_EQ(f_read(&SDFile, in, sizeof in, &n), FR_OK);		/* 13 */
	ASSERT_EQ(n, 30);
	ASSERT_EQ(f_tell(&SDFile), 30);
	ASSERT_EQ(memcmp(in, glo, sizeof glo), 0);
	ASSERT_EQ(f_lseek(&SDFile, f_tell(&SDFile) - 6), FR_OK);	/* 14: the V1 pattern seek */
	memset(in, 0xEE, sizeof in);
	ASSERT_EQ(f_read(&SDFile, in, 6, &n), FR_OK);				/* 15 */
	ASSERT_EQ(n, 6);
	ASSERT_EQ(memcmp(in, glo + 24, 6), 0);
	ASSERT_EQ(f_close(&SDFile), FR_OK);							/* 16 */
	ASSERT_EQ(f_open(&SDFile, "NOSUCH.SND", FA_READ), FR_NO_FILE);		/* 17 */
	ASSERT_EQ(f_open(&SDFile, "/NODIR/A.SND", FA_READ), FR_NO_PATH);	/* 18 */
	ASSERT_EQ(f_open(&SDFile, "A+B.SND", FA_READ), FR_INVALID_NAME);	/* 19 */
	ASSERT_EQ(f_unlink("glo.cfg"), FR_OK);						/* 20 */
	ASSERT_EQ(f_mkfs("", 0, 0, NULL, 0), FR_DENIED);			/* 21 */
	/* on the host, names upper case on create */
	char names[256];
	hostNames("", names, sizeof names);
	ASSERT_EQ(strcmp(names, "PROJ00"), 0);
	hostNames("PROJ00", names, sizeof names);
	ASSERT_EQ(strcmp(names, "PRJ.NFO"), 0);
	ASSERT_EQ(hostSize("PROJ00/PRJ.NFO"), 8);
	ASSERT_EQ(warnings, 0);
}

TEST(M9_truncate)
{
	setup();
	ASSERT_EQ(f_mount(&SDFatFS, SDPath, 1), FR_OK);
	uint8_t data[100];
	memset(data, 0x5A, sizeof data);
	ASSERT_EQ(writeFile("A.PAT", data, 100), FR_OK);
	ASSERT_EQ(writeFile("B.PAT", data, 1), FR_OK);
	struct stat before, after;
	char path[1024];
	snprintf(path, sizeof path, "%s/A.PAT", card);
	ASSERT_EQ(stat(path, &before), 0);
	ASSERT_EQ(writeFile("A.PAT", data, 10), FR_OK);
	ASSERT_EQ(stat(path, &after), 0);
	ASSERT_EQ(after.st_size, 10);
	/* in place: the same file, so it keeps its place in the order */
	ASSERT_EQ(after.st_ino, before.st_ino);
	ASSERT_EQ(after.st_birthtimespec.tv_sec, before.st_birthtimespec.tv_sec);
	ASSERT_EQ(after.st_birthtimespec.tv_nsec, before.st_birthtimespec.tv_nsec);
	char names[256];
	hostNames("", names, sizeof names);
	ASSERT_EQ(strcmp(names, "A.PAT B.PAT"), 0);
}

TEST(M9_order)
{
	setup();
	ASSERT_EQ(f_mount(&SDFatFS, SDPath, 1), FR_OK);
	static const char *const created[3] = { "03-ZZZZZ.SND", "01-AAAAA.SND", "02-MMMMM.SND" };
	for (int i = 0; i < 3; i++)
		ASSERT_EQ(writeFile(created[i], "x", 1), FR_OK);
	/* creation order, not name order (spec 4.8) */
	DIR dir;
	FILINFO fno;
	ASSERT_EQ(f_findfirst(&dir, &fno, "", "0?-*.SND"), FR_OK);
	for (int i = 0; i < 3; i++)
	{
		ASSERT_EQ(strcmp(fno.fname, created[i]), 0);
		ASSERT_EQ(f_findnext(&dir, &fno), FR_OK);
	}
	ASSERT_EQ(fno.fname[0], '\0');
	ASSERT_EQ(f_closedir(&dir), FR_OK);
	char names[256];
	ASSERT_EQ(cardNames("", names, sizeof names), FR_OK);
	ASSERT_EQ(strcmp(names, "03-ZZZZZ.SND 01-AAAAA.SND 02-MMMMM.SND"), 0);
	ASSERT_EQ(f_stat("01-AAAAA.SND", &fno), FR_OK);
	ASSERT_EQ(fno.fsize, 1);
	/* a new entry comes last, as on FAT, even after a host file born in the
	 * future */
	emu_testWrite(card, "04-FUTUR.SND", "x", 1);
	hostBirth("04-FUTUR.SND", wallSeconds() + 3600, 0);
	ASSERT_EQ(writeFile("00-NEWER.SND", "x", 1), FR_OK);
	ASSERT_EQ(f_mkdir("sub"), FR_OK);
	ASSERT_EQ(cardNames("", names, sizeof names), FR_OK);
	ASSERT_EQ(strcmp(names, "03-ZZZZZ.SND 01-AAAAA.SND 02-MMMMM.SND 04-FUTUR.SND 00-NEWER.SND SUB/"), 0);
	/* the pattern's ? and *: a non-match and a case-insensitive match */
	ASSERT_EQ(f_findfirst(&dir, &fno, "", "0?-M*.snd"), FR_OK);
	ASSERT_EQ(strcmp(fno.fname, "02-MMMMM.SND"), 0);
	ASSERT_EQ(f_closedir(&dir), FR_OK);
	ASSERT_EQ(f_findfirst(&dir, &fno, "", "1?-*.SND"), FR_OK);
	ASSERT_EQ(fno.fname[0], '\0');
	ASSERT_EQ(f_closedir(&dir), FR_OK);
}

/* LongerName.snd and A+B.SND stand for names that are not 8.3. The plan's
 * LongName.snd has 8 characters before the dot, so it is an 8.3 name in mixed
 * case: FatFs without LFN sees it as LONGNAME.SND, and the backend lists it so
 * (below). */
TEST(M9_skipped)
{
	setup();
	emu_testWrite(card, ".DS_Store", "x", 1);
	emu_testWrite(card, "._X.SND", "x", 1);
	hostDir(".Spotlight-V100");
	emu_testWrite(card, "LongerName.snd", "x", 1);
	emu_testWrite(card, "A+B.SND", "x", 1);
	emu_testWrite(card, "a.snd", "x", 1);
	ASSERT_EQ(f_mount(&SDFatFS, SDPath, 1), FR_OK);
	for (int pass = 0; pass < 2; pass++)
	{
		char names[256];
		ASSERT_EQ(cardNames("/", names, sizeof names), FR_OK);
		ASSERT_EQ(strcmp(names, "A.SND"), 0);
	}
	ASSERT_EQ(warnings, 2);	/* once each, not once per listing */
	ASSERT_TRUE((strstr(warning[0], "LongerName.snd") && strstr(warning[1], "A+B.SND"))
		|| (strstr(warning[1], "LongerName.snd") && strstr(warning[0], "A+B.SND")));
	/* an 8.3 name in mixed case is listed upper case */
	emu_testWrite(card, "LongName.snd", "x", 1);
	FILINFO fno;
	ASSERT_EQ(f_stat("/longname.snd", &fno), FR_OK);
	ASSERT_EQ(strcmp(fno.fname, "LONGNAME.SND"), 0);
	ASSERT_EQ(warnings, 2);
}

TEST(M9_duplicate)
{
	setup();
	emu_testWrite(card, "05-OLDER.SND", "o", 1);
	emu_testWrite(card, "05-NEWER.SND", "n", 1);
	/* the second decides: by name, and by the nanoseconds alone, 05-NEWER.SND
	 * would come first */
	const time_t t = wallSeconds() - 100;
	hostBirth("05-OLDER.SND", t, 900000000L);
	hostBirth("05-NEWER.SND", t + 1, 100000000L);
	ASSERT_EQ(f_mount(&SDFatFS, SDPath, 1), FR_OK);
	DIR dir;
	FILINFO fno;
	/* the older one, though 05-NEWER.SND comes first by name */
	ASSERT_EQ(f_findfirst(&dir, &fno, "", "05-*.SND"), FR_OK);
	ASSERT_EQ(strcmp(fno.fname, "05-OLDER.SND"), 0);
	ASSERT_EQ(f_closedir(&dir), FR_OK);
	ASSERT_EQ(f_findfirst(&dir, &fno, "", "05-*.SND"), FR_OK);
	ASSERT_EQ(f_closedir(&dir), FR_OK);
	ASSERT_EQ(warnings, 1);
	ASSERT_TRUE(strstr(warning[0], "05-OLDER.SND") != NULL);
	ASSERT_TRUE(strstr(warning[0], "05-NEWER.SND") != NULL);
}

TEST(M9_lock)
{
	setup();
	hostDir("PROJ00");
	emu_testWrite(card, "PROJ00/A.SND", "a", 1);
	emu_testWrite(card, "PROJ00/B.SND", "b", 1);
	ASSERT_EQ(f_mount(&SDFatFS, SDPath, 1), FR_OK);
	ASSERT_EQ(f_chdir("/proj00"), FR_OK);
	ASSERT_EQ(f_open(&SDFile, "A.SND", FA_READ), FR_OK);
	DIR sub, root;
	ASSERT_EQ(f_opendir(&sub, "/proj00"), FR_TOO_MANY_OPEN_FILES);
	FIL second;
	ASSERT_EQ(f_open(&second, "B.SND", FA_READ), FR_TOO_MANY_OPEN_FILES);
	ASSERT_EQ(f_opendir(&root, "/"), FR_OK);	/* the root is not counted */
	ASSERT_EQ(f_closedir(&root), FR_OK);
	ASSERT_EQ(f_close(&SDFile), FR_OK);
	ASSERT_EQ(f_opendir(&sub, "/proj00"), FR_OK);
	ASSERT_EQ(f_open(&SDFile, "A.SND", FA_READ), FR_TOO_MANY_OPEN_FILES);	/* a directory holds it too */
	ASSERT_EQ(f_closedir(&sub), FR_OK);
	ASSERT_EQ(f_opendir(&root, "/"), FR_OK);	/* the root again, then a file */
	ASSERT_EQ(f_open(&SDFile, "A.SND", FA_READ), FR_OK);
	ASSERT_EQ(f_close(&SDFile), FR_OK);
	ASSERT_EQ(f_closedir(&root), FR_OK);
}

TEST(M9_card_switch)
{
	setup();
	hostDir("PROJ00");
	emu_testWrite(card, "PROJ00/PRJ.NFO", "TESTPROJ", 8);
	FILINFO fno;
	ASSERT_EQ(f_mount(&SDFatFS, SDPath, 1), FR_OK);
	ASSERT_EQ(f_chdir("/proj00"), FR_OK);
	ASSERT_EQ(f_stat("PRJ.NFO", &fno), FR_OK);
	ASSERT_EQ(BSP_SD_IsDetected(), SD_PRESENT);
	emu_inCard(emu_now(), 0);
	HAL_Delay(1);
	ASSERT_EQ(BSP_SD_IsDetected(), SD_NOT_PRESENT);
	ASSERT_EQ(f_open(&SDFile, "PRJ.NFO", FA_READ), FR_NOT_READY);
	ASSERT_EQ(f_stat("PRJ.NFO", &fno), FR_NOT_READY);
	ASSERT_EQ(f_mkdir("/proj01"), FR_NOT_READY);
	emu_inCard(emu_now(), 1);
	HAL_Delay(1);
	ASSERT_EQ(BSP_SD_IsDetected(), SD_PRESENT);
	ASSERT_EQ(f_mount(&SDFatFS, SDPath, 1), FR_OK);
	/* the mount reset the current folder to the root */
	ASSERT_EQ(f_stat("PRJ.NFO", &fno), FR_NO_FILE);
	ASSERT_EQ(f_stat("/proj00/PRJ.NFO", &fno), FR_OK);
	ASSERT_EQ(hostSize("PROJ01"), -1);	/* the mkdir made nothing */
}

TEST(M9_copy)
{
	setup();
	hostDir("PROJ00");
	emu_testWrite(card, "PROJ00/03-Z.SND", "z", 1);
	emu_testWrite(card, "PROJ00/01-A.SND", "a", 1);
	char path[1024], dir[1024];
	snprintf(path, sizeof path, "%s/PROJ00/01-A.SND", card);
	snprintf(dir, sizeof dir, "%s/PROJ00", card);
	emu_hostOrderLast(dir, path);	/* born after 03-Z.SND, whatever the clock's grain */
	emu_testWrite(card, "PROJ00/.DS_Store", "x", 1);
	hostDir("TMP");
	emu_testWrite(card, "TMP/TMP.SND", "t", 1);
	char names[256];
	hostNames("PROJ00", names, sizeof names);
	ASSERT_EQ(strcmp(names, "03-Z.SND 01-A.SND"), 0);
	for (int skipTmp = 1; skipTmp >= 0; skipTmp--)
	{
		char dst[512];
		emu_testTempDir(dst, sizeof dst);
		ASSERT_EQ(emu_hostCopyCard(card, dst, skipTmp), 0);
		/* the copy lists in the source's order; read it through card's helpers */
		char saved[512];
		snprintf(saved, sizeof saved, "%s", card);
		snprintf(card, sizeof card, "%s", dst);
		hostNames("PROJ00", names, sizeof names);
		ASSERT_EQ(strcmp(names, "03-Z.SND 01-A.SND"), 0);
		ASSERT_EQ(hostSize("PROJ00/.DS_Store"), -1);
		ASSERT_EQ(hostSize("TMP/TMP.SND"), skipTmp ? -1 : 1);
		hostNames("", names, sizeof names);
		ASSERT_EQ(strcmp(names, skipTmp ? "PROJ00" : "PROJ00 TMP"), 0);
		snprintf(card, sizeof card, "%s", saved);
	}
}

static FRESULT saveResult, loadResult;

static void saveAndLoad(void *arg)
{
	(void)arg;
	saveResult = preset_saveDrumsetSD(7, 0);
	loadResult = preset_loadDrumsetSD(7, 0);
}

TEST(M9_fw_save_load)
{
	emu_testCard(card, sizeof card, 120);
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	cfg.warn = recordWarning;
	emu_testBoot(&cfg);
	emu_testRunMs(100);
	char path[1024];
	snprintf(path, sizeof path, "%s/TMP", card);
	emu_hostEntry_t *before;
	const int nBefore = emu_hostList(path, &before, NULL);
	ASSERT_TRUE(nBefore >= 0);	/* the boot made /TMP, the current folder */
	emu_directCall(saveAndLoad, NULL);
	ASSERT_EQ(saveResult, FR_OK);
	ASSERT_EQ(loadResult, FR_OK);
	emu_hostEntry_t *after;
	const int nAfter = emu_hostList(path, &after, NULL);
	ASSERT_EQ(nAfter, nBefore + 1);
	/* the one new entry: a kit of 255 bytes for slot 7 */
	int found = 0;
	for (int i = 0; i < nAfter; i++)
	{
		int old = 0;
		for (int k = 0; k < nBefore; k++)
			old |= !strcmp(after[i].host, before[k].host);
		if (old)
			continue;
		found++;
		const size_t len = strlen(after[i].name);
		ASSERT_TRUE(len > 4 && !strcmp(after[i].name + len - 4, ".SND"));
		ASSERT_TRUE(!strncmp(after[i].name, "07-", 3) || !strcmp(after[i].name, "P007.SND"));
		ASSERT_EQ(after[i].size, 268);	/* LICHOMAT v0.4: 255 + the 13-byte LCHM block (plan D34) */
		ASSERT_EQ(strcmp(after[i].host, after[i].name), 0);	/* upper case on create */
	}
	ASSERT_EQ(found, 1);
	free(before);
	free(after);
	ASSERT_EQ(warnings, 0);
}

TEST_MAIN()
