/*
 * emu_test.h
 *
 * Helpers for the emulator's host tests (emu/tests): a power-on of the core
 * without the firmware, a temporary card folder, and recorders for the sinks.
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emu_internal.h"
#include "seqc_test.h"

/* a new empty folder under $TMPDIR */
static inline void emu_testTempDir(char *out, size_t size)
{
	const char *base = getenv("TMPDIR");
	snprintf(out, size, "%s/lichomat-emu-XXXXXX", base && base[0] ? base : "/tmp");
	if (!mkdtemp(out))
	{
		perror("mkdtemp");
		exit(2);
	}
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

#endif /* EMU_TEST_H_ */
