/*
 * emu_test.h
 *
 * Helpers for the emulator's host tests (emu/tests): a power-on of the core
 * without the firmware, the boot of the whole firmware, a temporary card folder
 * and a minimal card, a fake tier for the hook, recorders for the sinks, and the
 * LCD as text.
 * The test runner is tests/seqc_test.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_TEST_H_
#define EMU_TEST_H_

#include <ftw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "emu_internal.h"
#include "seqc_test.h"

/* The folders of emu_testTempDir(), removed when the process exits: after the
 * tests, a failed ASSERT_ included, or at an exit(). An abort() leaves them,
 * and ctest's emu_tmp_clean names them. A forked child leaves them to its
 * parent. */
#define EMU_TEST_MAX_DIRS 64
__attribute__((unused)) static char emu_testDirs[EMU_TEST_MAX_DIRS][512];
__attribute__((unused)) static unsigned emu_testDirCount;
__attribute__((unused)) static pid_t emu_testDirPid;

static inline int emu_testRemoveEntry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st;
	(void)flag;
	(void)ftw;
	if (remove(path) != 0)
		perror(path);
	return 0;
}

static inline void emu_testRemoveDirs(void)
{
	if (getpid() != emu_testDirPid)
		return;
	for (unsigned i = 0; i < emu_testDirCount; i++)
		nftw(emu_testDirs[i], emu_testRemoveEntry, 16, FTW_DEPTH | FTW_PHYS);
	emu_testDirCount = 0;
}

/* a new empty folder under $TMPDIR, removed at exit */
static inline void emu_testTempDir(char *out, size_t size)
{
	const char *base = getenv("TMPDIR");
	snprintf(out, size, "%s/lichomat-emu-XXXXXX", base && base[0] ? base : "/tmp");
	if (!mkdtemp(out))
	{
		perror("mkdtemp");
		exit(2);
	}
	if (emu_testDirPid != getpid())
	{
		/* the first folder of this process; a forked child keeps its own list */
		emu_testDirPid = getpid();
		emu_testDirCount = 0;
		atexit(emu_testRemoveDirs);
	}
	if (emu_testDirCount >= EMU_TEST_MAX_DIRS || strlen(out) >= sizeof emu_testDirs[0])
	{
		fprintf(stderr, "emu_testTempDir: more than %u folders, or a path too long: %s\n",
			EMU_TEST_MAX_DIRS, out);
		rmdir(out);
		exit(2);
	}
	snprintf(emu_testDirs[emu_testDirCount++], sizeof emu_testDirs[0], "%s", out);
}

/* the core alone: clock, scheduler, registers; the test adds models and fakes */
static inline void emu_testCore(emu_config_t *cfg)
{
	emu_configDefaults(cfg);
	emu_corePowerOn(cfg);
}

/* the fault messages a test expects instead of an abort */
__attribute__((unused)) static char emu_testFault[256];
__attribute__((unused)) static unsigned emu_testFaults;

static inline void emu_testOnFault(void *ctx, const char *what)
{
	(void)ctx;
	snprintf(emu_testFault, sizeof emu_testFault, "%s", what);
	emu_testFaults++;
}

/* A fake tier for the hook tests (spec 3.3, 6.2): a wall clock and an audio ring.
 * Before the codec starts, the hook sleeps to the wall clock. After it, every
 * block request adds a block to a ring of cap blocks; while the ring is full,
 * the hook waits for the fake device, which takes one block per blockSec of wall
 * time. Each wait is bracketed for the watchdog, which the wait polls at its
 * start and at its end, as the watchdog thread would: so a wait longer than the
 * watchdog's 10 s fires it unless the bracket works (M3_paced_by_ring). */
typedef struct emu_testTier
{
	double wall;			/* seconds */
	unsigned fill, cap;		/* blocks in the ring, its size */
	double blockSec;		/* the fake device takes one block per blockSec */
	uint64_t blockHooks, timeHooks;
	double maxWait;			/* the longest single wait */
	emu_watchdog_t dog;
	int dogFired;			/* the watchdog fired during a wait */
	void (*extra)(uint64_t now, emu_hookReason_t why);	/* a test's own work */
} emu_testTier_t;

__attribute__((unused)) static emu_testTier_t emu_tier;

static inline void emu_testTierWait(double until)
{
	if (until <= emu_tier.wall)
		return;
	emu_waitBegin();
	if (emu_watchdogPoll(&emu_tier.dog, emu_tier.wall))
		emu_tier.dogFired = 1;
	if (until - emu_tier.wall > emu_tier.maxWait)
		emu_tier.maxWait = until - emu_tier.wall;
	emu_tier.wall = until;
	if (emu_watchdogPoll(&emu_tier.dog, emu_tier.wall))
		emu_tier.dogFired = 1;
	emu_waitEnd();
}

static inline void emu_testTierHook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	emu_counters_t c;
	emu_getCounters(&c);
	if (why == EMU_HOOK_BLOCK)
	{
		emu_tier.blockHooks++;
		if (++emu_tier.fill > emu_tier.cap)
		{
			emu_testTierWait(emu_tier.wall + emu_tier.blockSec);
			emu_tier.fill--;
		}
	}
	else
	{
		emu_tier.timeHooks++;
		if (c.codecStart == EMU_NEVER)
			emu_testTierWait((double)now / EMU_CPU_HZ);
	}
	if (emu_tier.extra)
		emu_tier.extra(now, why);
}

static inline void emu_testTierReset(unsigned cap, double blockSec)
{
	memset(&emu_tier, 0, sizeof emu_tier);
	emu_tier.cap = cap;
	emu_tier.blockSec = blockSec;
}

/* a recorder of the DIN out sink */
#define EMU_TEST_MAX_BYTES 4096
typedef struct emu_testDin
{
	unsigned count;
	uint64_t t[EMU_TEST_MAX_BYTES];
	uint8_t byte[EMU_TEST_MAX_BYTES];
} emu_testDin_t;

static inline void emu_testDinSink(void *ctx, uint64_t t, uint8_t byte)
{
	emu_testDin_t *d = ctx;
	if (d->count < EMU_TEST_MAX_BYTES)
	{
		d->t[d->count] = t;
		d->byte[d->count] = byte;
	}
	d->count++;
}

/* the LCD rows as text */
static inline void emu_testLcdRow(int row, char out[17])
{
	emu_lcdSnapshot_t s;
	emu_lcdSnapshot(&s);
	memcpy(out, s.chars[row], 16);
	out[16] = '\0';
}

/* the whole firmware: power-on and boot; once per process */
static inline void emu_testBoot(emu_config_t *cfg)
{
	emu_powerOn(cfg);
	emu_runUntil(1u);	/* returns at the end of the boot */
}

/* run for ms of virtual time from now, to the next pass boundary */
static inline void emu_testRunMs(double ms)
{
	emu_runUntil(emu_now() + (uint64_t)(ms * (double)EMU_CYCLES_PER_MS));
}

/* 1 if row 0 or row 1 of the LCD contains text */
static inline int emu_testLcdHas(const char *text)
{
	char r0[17], r1[17];
	emu_testLcdRow(0, r0);
	emu_testLcdRow(1, r1);
	return strstr(r0, text) != NULL || strstr(r1, text) != NULL;
}

static inline void emu_testWrite(const char *dir, const char *name, const void *data, size_t n)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "wb");
	if (!f || fwrite(data, 1, n, f) != n || fclose(f) != 0)
	{
		perror(path);
		exit(2);
	}
}

/* A minimal card (fixtures come in R3): /PROJ00/PRJ.NFO, and GLO.CFG with a tempo
 * when bpm is not 0; byte 29, the kit autoload, is 0. */
static inline void emu_testCard(char *dir, size_t size, uint8_t bpm)
{
	emu_testTempDir(dir, size);
	char proj[1024];
	snprintf(proj, sizeof proj, "%s/PROJ00", dir);
	mkdir(proj, 0755);
	emu_testWrite(proj, "PRJ.NFO", "TESTPROJ", 8);
	if (bpm)
	{
		uint8_t glo[30] = { bpm };
		emu_testWrite(proj, "GLO.CFG", glo, sizeof glo);
	}
}

#endif /* EMU_TEST_H_ */
