/*
 * render_main.c
 *
 * lichomat-render, tier 1 of the emulator (stage 1b spec 5): one render per
 * process on the virtual device.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emu_core.h"
#include "emu_hostfs.h"
#include "render_build.h"
#include "render_card.h"
#include "render_fw.h"
#include "render_opts.h"

/* print "lichomat-render: " + the message + a newline to stderr and exit (spec 5.7) */
static void render_die(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3), noreturn));
static void render_die(int code, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("lichomat-render: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(code);
}

/* The notices of spec 10.1. Each line is one C string, so that each is one run
 * for M21's name scan; the lines that name the maker, the product or the
 * repository are on its allow-list (emu/tests/name_allow.txt). */
static void render_version(void)
{
	static const char *const tagline =
		"emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02";
	printf("lichomat-render %s, the renderer of LICHOMAT Emu\n", EMU_RENDER_VERSION);
	printf("%s\n", tagline);
	printf("Runs LICHOMAT %s with the emulator changes F1-F10. Built from commit %s.\n",
		EMU_RENDER_VERSION, EMU_RENDER_COMMIT);
	static const char *const legal[] = {
		"Copyright (C) 2012-2026 Erica Synths",
		"Copyright (C) 2026 Tomasz Krzywicki",
		"Modified from the LXR firmware 1.7 by Erica Synths; LICHOMAT and its emulator by Tomasz Krzywicki, 2026",
		"Arm Optimized Routines: Copyright (c) 1999-2022, Arm Limited. MIT licence.",
		"FatFs ff.h and integer.h: Copyright (C) 2017, ChaN. FatFs licence.",
		"This program comes with ABSOLUTELY NO WARRANTY. It is free software under the GNU",
		"General Public License version 3 only, with the additional permission and terms",
		"in NOTICE.md. See LICENSE and NOTICE.md.",
		"Source: https://github.com/isthar/lxr-desktop-app at the commit above.",
		"Not affiliated with Erica Synths.",
	};
	for (size_t i = 0; i < sizeof legal / sizeof legal[0]; i++)
		printf("%s\n", legal[i]);
}

/* a model check (spec 3.4, 5.7 code 3); the signals, the trail's context and the
 * card-copy line of the full fault path come with the watchdog (Task R2.6) */
static void render_onFault(void *ctx, const char *what)
{
	(void)ctx;
	char line[512];
	const int len = snprintf(line, sizeof line, "lichomat-render: firmware fault: %s at cycle %llu\n",
		what, (unsigned long long)emu_now());
	if (len > 0)
		(void)!write(2, line, (size_t)len < sizeof line ? (size_t)len : sizeof line - 1);
	emu_trailWrite(2, 32);
	_exit(3);
}

/* a warning about the card (spec 4.8, 11) */
static void render_onWarn(void *ctx, const char *what)
{
	(void)ctx;
	fprintf(stderr, "lichomat-render: warning: %s\n", what);
}

/* --seed-card DIR (spec 6.7): an init card from the firmware's own code */
static void render_seed(const char *dir)
{
	const int empty = render_cardEmpty(dir);
	if (empty < 0)
		render_die(1, "--seed-card: %s is not a folder, or cannot be read", dir);
	if (empty == 0)
		render_die(1, "--seed-card: %s holds entries; it must be missing or hold nothing but dot entries", dir);
	if (emu_hostKind(dir) < 0 && emu_hostMkdir(dir) != 0)
		render_die(2, "card %s: cannot make the folder: %s", dir, strerror(errno));
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = dir;
	cfg.cardIn = 1;
	cfg.fault = render_onFault;
	cfg.warn = render_onWarn;
	emu_powerOn(&cfg);
	emu_runUntil(1u);	/* the boot */
	emu_runUntil(emu_now() + 100u * EMU_CYCLES_PER_MS);
	int res = -1;
	emu_directCall(rfw_seedCard, &res);
	if (res != 0)
		render_die(2, "card %s: the project save failed with FatFs result %d", dir, res);
}

int main(int argc, char **argv)
{
	render_opts_t o;
	char err[512];
	if (render_optsParse(argc, argv, &o, err, sizeof err) != 0)
		render_die(1, "%s", err);
	if (o.help)
	{
		render_optsHelp();
		return 0;
	}
	if (o.version)
	{
		render_version();
		return 0;
	}
	if (o.seedCard)
	{
		render_seed(o.seedCard);
		return 0;
	}
	if (o.keepCard && render_cardEmpty(o.keepCard) != 1)
		render_die(1, "--keep-card: %s must be missing or hold nothing but dot entries", o.keepCard);

	char copy[EMU_HOST_PATH];
	int project;
	const int status = render_cardPrepare(&o, copy, &project, err, sizeof err);
	if (status != 0)
		render_die(status, "%s", err);
	render_cardRemove(copy);
	render_die(1, "rendering comes in R2.3");
}
