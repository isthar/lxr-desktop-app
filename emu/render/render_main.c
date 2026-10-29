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
#include <time.h>
#include <unistd.h>

#include "emu_core.h"
#include "emu_hostfs.h"
#include "render_build.h"
#include "render_card.h"
#include "render_fw.h"
#include "render_opts.h"
#include "render_out.h"

#define C100 21600000ull	/* 100 ms (spec 5.3) */

/* the state of one render (stage 1b spec 5.3) */
static struct
{
	const render_opts_t *o;
	render_out_t out;
	char copy[EMU_HOST_PATH];	/* the card copy; "" when there is none */
	int project;
	double wallStart;
	uint64_t preludeEnd, busyLast, t0, tEnd, k0, blocks;
	uint64_t lastUnderruns, underrunsBefore;
	enum { PH_PRELUDE, PH_WAIT, PH_PLAY } phase;
	int t0Hook;		/* the sink found t0; the hook of this request starts the script */
	render_selected_t selected;
	char *warnings[RENDER_MAX_WARNINGS];
	unsigned nwarnings;
} R;

static double wallNow(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* print "lichomat-render: " + the message + a newline to stderr and exit (spec 5.7);
 * the card copy goes first */
static void render_die(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3), noreturn));
static void render_die(int code, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("lichomat-render: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	render_cardRemove(R.copy);
	exit(code);
}

/* code 3 (spec 5.7, plan P2): the message, the trail, the card copy kept; write(2)
 * only, then _exit() without cleanup */
static void render_fault(const char *what) __attribute__((noreturn));
static void render_fault(const char *what)
{
	char line[EMU_HOST_PATH + 512];
	int len = snprintf(line, sizeof line, "lichomat-render: firmware fault: %s at cycle %llu\n",
		what, (unsigned long long)emu_now());
	if (len > 0)
		(void)!write(2, line, (size_t)len < sizeof line ? (size_t)len : sizeof line - 1);
	emu_trailWrite(2, 32);
	if (R.copy[0])
	{
		len = snprintf(line, sizeof line, "lichomat-render: the card copy is kept in %s\n", R.copy);
		if (len > 0)
			(void)!write(2, line, (size_t)len < sizeof line ? (size_t)len : sizeof line - 1);
	}
	_exit(3);
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

/* a model check (spec 3.4, 5.7 code 3); the signals come with the watchdog (Task R2.6) */
static void render_onFault(void *ctx, const char *what)
{
	(void)ctx;
	render_fault(what);
}

/* a warning (spec 4.8, 11): kept for the summary, printed at once while seeding
 * and after the short summary of a render */
static void render_warn(const char *what)
{
	if (!R.o)
	{
		fprintf(stderr, "lichomat-render: warning: %s\n", what);
		return;
	}
	if (R.nwarnings < RENDER_MAX_WARNINGS)
	{
		char *copy = strdup(what);
		if (copy)
			R.warnings[R.nwarnings++] = copy;
	}
}

static void render_onWarn(void *ctx, const char *what)
{
	(void)ctx;
	render_warn(what);
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

/* --- the timeline (spec 5.3, plan P3) ------------------------------------------- */

static int render_t0Due(uint64_t t)
{
	return t - R.preludeEnd >= C100 && (R.busyLast == 0 || t - R.busyLast > C100);
}

/* the audio sink: t0 at the first block request that meets the rule, then the
 * blocks of [t0, tEnd) into the WAV (P3 steps 5 and 9) */
static void render_audio(void *ctx, uint64_t t, const int16_t *out1, const int16_t *out2)
{
	(void)ctx;
	if (R.phase == PH_WAIT && render_t0Due(t))
	{
		emu_counters_t c;
		emu_getCounters(&c);
		R.t0 = t;
		R.k0 = c.blocks;	/* counted before the sink (emu_codec.c) */
		if (c.codecStart + emu_framesToCycles(EMU_BLOCK_FRAMES * R.k0) != t)
			render_fault("t0 is not the time of block request k0");
		R.tEnd = c.codecStart + emu_framesToCycles(EMU_BLOCK_FRAMES * (R.k0 + R.blocks));
		R.underrunsBefore = R.lastUnderruns;
		R.phase = PH_PLAY;
		R.t0Hook = 1;
	}
	if (R.phase == PH_PLAY && t >= R.t0 && t < R.tEnd)
		render_outAudio(&R.out, out1, out2);
}

/* P3 step 6: START (SHIFT + START for --song), the LED latch */
static void render_startScript(uint64_t now)
{
	if (!R.o->noStart)
	{
		emu_inButton(now, 33, 1);
		emu_inButton(now + 50u * EMU_CYCLES_PER_MS, 33, 0);
		if (R.o->song >= 0)
		{
			emu_inButton(now, 32, 1);
			emu_inButton(now + 60u * EMU_CYCLES_PER_MS, 32, 0);
		}
	}
	(void)emu_ledSnapshot();
}

static const char *jacksText(uint8_t jacks, char buf[16])
{
	if ((jacks & EMU_JACKS_ALL) == EMU_JACKS_ALL)
		return "all";
	if (!(jacks & EMU_JACKS_ALL))
		return "none";
	static const char *const names[] = { "L1", "R1", "L2", "R2" };
	buf[0] = '\0';
	for (unsigned i = 0; i < 4; i++)
		if (jacks & (1u << i))
		{
			if (buf[0])
				strcat(buf, ",");
			strcat(buf, names[i]);
		}
	return buf;
}

/* P3 step 8, from the hook: the outputs, the summary, --keep-card, the copy's
 * removal and exit() */
static void render_finish(uint64_t now) __attribute__((noreturn));
static void render_finish(uint64_t now)
{
	(void)now;
	const render_opts_t *o = R.o;
	char err[EMU_HOST_PATH + 256];
	int code = render_outClose(&R.out, o->wavRate, err, sizeof err);
	if (code)
		fprintf(stderr, "lichomat-render: %s\n", err);
	render_summary_t s;
	memset(&s, 0, sizeof s);
	s.o = o;
	s.project = R.project;
	char jbuf[16];
	s.jacks = jacksText(o->jacks, jbuf);
	s.blocks = R.blocks;
	emu_getCounters(&s.counters);
	rfw_counters(&s.fw);	/* pure getters, the one firmware read from the hook (plan P3 step 8) */
	s.bootEnd = s.counters.bootEnd;
	s.codecStart = s.counters.codecStart;
	s.preludeEnd = R.preludeEnd;
	s.morphBusyLast = R.busyLast;
	s.t0 = R.t0;
	s.end = R.tEnd;
	s.underrunsBefore = R.underrunsBefore;
	s.underrunsAfter = R.lastUnderruns - R.underrunsBefore;
	s.selected = R.selected;
	s.warnings = (const char *const *)R.warnings;
	s.nwarnings = R.nwarnings;
	s.wallSeconds = wallNow() - R.wallStart;
	s.virtualSeconds = (double)R.tEnd / (double)EMU_CPU_HZ;
	s.speed = s.wallSeconds > 0 ? s.virtualSeconds / s.wallSeconds : 0;
	if (o->summary && render_summaryWrite(o->summary, &s, err, sizeof err) != 0)
	{
		fprintf(stderr, "lichomat-render: %s\n", err);
		code = 5;
	}
	if (o->keepCard && render_cardKeep(R.copy, o->keepCard) != 0)
	{
		fprintf(stderr, "lichomat-render: --keep-card: cannot copy the card into %s: %s\n", o->keepCard,
			strerror(errno));
		code = 5;
	}
	render_cardRemove(R.copy);
	R.copy[0] = '\0';
	if (code == 0)
	{
		fprintf(stderr, "lichomat-render: t0 at %.3f s, %llu blocks, %llu underruns after t0, %.1fx real time\n",
			(double)R.t0 / (double)EMU_CPU_HZ, (unsigned long long)R.blocks,
			(unsigned long long)s.underrunsAfter, s.speed);
		for (unsigned i = 0; i < R.nwarnings; i++)
			fprintf(stderr, "lichomat-render: warning: %s\n", R.warnings[i]);
	}
	fflush(NULL);
	exit(code);
}

static void render_hook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
#if EMU_RENDER_TEST_HOOKS
	if (R.o->testSlice)
	{
		emu_waitBegin();	/* M14: a tier that waits at every call */
		emu_waitEnd();
	}
#endif
	if (why != EMU_HOOK_BLOCK)
		return;
	if (R.t0Hook)
	{
		R.t0Hook = 0;
		render_startScript(now);
	}
	else if (R.phase == PH_PLAY && now >= R.tEnd)
		render_finish(now);
	emu_counters_t c;
	emu_getCounters(&c);
	R.lastUnderruns = c.underruns;
}

/* a pass of the prelude (P3 step 2) */
static void render_pass(void)
{
	uint8_t busy = 0;
	emu_runUntil(emu_now() + 1u);	/* returns after the next pass */
	emu_directCall(rfw_morphBusy, &busy);
	if (busy)
		R.busyLast = emu_now();
}

/* P3 step 3 */
static void render_settle(void)
{
	while (!(emu_now() - R.preludeEnd >= C100 && (R.busyLast == 0 || emu_now() - R.busyLast > C100)))
		render_pass();
}

/* the length in blocks (P3 step 4): whole blocks, rounded up from the frames */
static uint64_t render_blocks(const render_opts_t *o)
{
	unsigned __int128 num, den;
	if (o->bars)
	{
		unsigned bpm;
		if (o->bpm >= 2)
			bpm = (unsigned)o->bpm;
		else if (R.selected.sync == 0 && o->bpm < 0)
			bpm = (unsigned)R.selected.bpm;
		else
			render_die(1, "--bars: needs an internal tempo; the project syncs to %s",
				(o->bpm == 0 || (o->bpm < 0 && R.selected.sync == 1)) ? "EXT" : "MID");
		num = (unsigned __int128)o->bars * 240u * 353125u;
		den = (unsigned __int128)8u * bpm;
	}
	else
	{
		num = (unsigned __int128)o->secondsNum * 353125u;
		den = 8u;
		for (unsigned i = 0; i < o->secondsDigits; i++)
			den *= 10u;
	}
	const unsigned __int128 frames = (num + den - 1u) / den;
	return (uint64_t)((frames + EMU_BLOCK_FRAMES - 1u) / EMU_BLOCK_FRAMES);
}

static void render_run(const render_opts_t *o) __attribute__((noreturn));
static void render_run(const render_opts_t *o)
{
	char err[EMU_HOST_PATH + 256];
	R.wallStart = wallNow();
	R.o = o;
	const int status = render_cardPrepare(o, R.copy, &R.project, err, sizeof err);
	if (status != 0)
		render_die(status, "%s", err);
	if (render_outOpen(&R.out, o, err, sizeof err) != 0)
		render_die(5, "%s", err);

	/* P3 step 1 */
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = R.copy;
	cfg.cardIn = 1;
	cfg.jacks = o->jacks;
	for (unsigned i = 0; i < 6; i++)
		cfg.faders[i] = o->faders[i];
	cfg.seed = o->seed;
	cfg.sinks.audio = render_audio;
	cfg.hook = render_hook;
	cfg.fault = render_onFault;
	cfg.warn = render_onWarn;
	emu_powerOn(&cfg);
	emu_runUntil(1u);	/* the boot */
	R.preludeEnd = emu_now();
	render_settle();

	/* the prelude's direct calls (--bpm, the sync fallback, --param: Task R2.5) run
	 * at this boundary, which is the prelude's last event */
	R.preludeEnd = emu_now();
#if EMU_RENDER_TEST_HOOKS
	if (o->testPreludeReload)
	{
		/* a kit reload (buttonHandler.c case SEQ_RELOAD), not a prelude event */
		const uint64_t w = emu_now();
		emu_inButton(w, 32, 1);
		emu_inButton(w + 20u * EMU_CYCLES_PER_MS, 15, 1);
		emu_inButton(w + 70u * EMU_CYCLES_PER_MS, 15, 0);
		emu_inButton(w + 100u * EMU_CYCLES_PER_MS, 32, 0);
	}
#endif
	emu_directCall(rfw_selected, &R.selected);
	R.blocks = render_blocks(o);

	R.phase = PH_WAIT;
	while (R.phase == PH_WAIT)
		render_pass();

	/* P3 step 7: render_finish() ends the process from the hook */
	for (;;)
	{
		emu_runUntil(R.tEnd + EMU_CPU_HZ);
		if (emu_now() > R.tEnd + EMU_CPU_HZ)
			render_fault("the render did not end at its end time");
	}
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

	render_run(&o);
}
