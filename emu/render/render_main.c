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
#include "render_script.h"

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
	render_script_t script;
	unsigned nextDirect;		/* the script index from which to look for the next direct event */
	render_direct_t direct[RENDER_MAX_DIRECT];
	unsigned ndirect;
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

/* the log sinks: lines of [t0, tEnd), timed from t0 (P3 step 9) */
static int render_kept(uint64_t t)
{
	return R.phase == PH_PLAY && t >= R.t0 && t < R.tEnd;
}

static void render_din(void *ctx, uint64_t t, uint8_t byte)
{
	(void)ctx;
	if (render_kept(t))
		render_outDin(&R.out, t - R.t0, byte);
}

static void render_usb(void *ctx, uint64_t t, const uint8_t *data, uint16_t length)
{
	(void)ctx;
	if (render_kept(t))
		render_outUsb(&R.out, t - R.t0, data, length);
}

static void render_clock(void *ctx, uint64_t t, int level)
{
	(void)ctx;
	if (render_kept(t))
		render_outClock(&R.out, t - R.t0, level);
}

/* the end of a clock generator's run: the next line of the same kind, or the end */
static uint64_t render_clockEnd(unsigned i)
{
	const render_event_t *e = &R.script.ev[i];
	for (unsigned j = i + 1; j < R.script.n; j++)
		if (R.script.ev[j].kind == e->kind)
			return R.t0 + R.script.ev[j].t;
	return R.tEnd;
}

/* clock k of a generator: t + floor(k * per / mbpm) */
static uint64_t render_clockAt(uint64_t t, uint64_t k, unsigned long long per, uint64_t mbpm)
{
	return t + (uint64_t)((unsigned __int128)k * per / mbpm);
}

#define CLOCK_DIN 540000000000ull	/* 24 PPQ: cycles per minute x 1000 / 24 */
#define CLOCK_JACK 12960000000000ull	/* cycles per minute x 1000 */

/* one timed event as emu_in*() inputs (plan P4's input column) */
static void render_inject(unsigned i)
{
	const render_event_t *e = &R.script.ev[i];
	const uint64_t t = R.t0 + e->t;
	switch (e->kind)
	{
	case RS_DOWN:
	case RS_UP:
		emu_inButton(t, (unsigned)e->a, e->kind == RS_DOWN);
		break;
	case RS_PRESS:
		emu_inButton(t, (unsigned)e->a, 1);
		emu_inButton(t + (uint64_t)e->b * EMU_CYCLES_PER_MS, (unsigned)e->a, 0);
		break;
	case RS_ENCODER:
		emu_inEncoder(t, e->a);
		break;
	case RS_PUSH:
		emu_inPush(t, (unsigned)e->b);
		break;
	case RS_KNOB:
		emu_inKnob(t, (unsigned)e->a, e->b);
		break;
	case RS_FADER:
		emu_inFader(t, (unsigned)e->a, e->level);
		break;
	case RS_DIN:
		emu_inDin(t, e->bytes, e->nbytes);
		break;
	case RS_USB:
		for (unsigned off = 0; off < e->nbytes; off += 64u)
			(void)emu_inUsb(t, e->bytes + off, e->nbytes - off > 64u ? 64u : e->nbytes - off);
		break;
	case RS_DIN_CLOCK:
	{
		if (!e->mbpm)
			break;
		static const uint8_t f8 = 0xF8;
		const uint64_t end = render_clockEnd(i);
		for (uint64_t k = 0, c; (c = render_clockAt(t, k, CLOCK_DIN, e->mbpm)) < end; k++)
			emu_inDin(c, &f8, 1);
		break;
	}
	case RS_USB_CLOCK:
	{
		if (!e->mbpm)
			break;
		const uint64_t end = render_clockEnd(i);
		if (!e->buffer)
		{
			static const uint8_t packet[4] = { 0x0F, 0xF8, 0, 0 };
			for (uint64_t k = 0, c; (c = render_clockAt(t, k, CLOCK_DIN, e->mbpm)) < end; k++)
				(void)emu_inUsb(c, packet, 4);
			break;
		}
		uint64_t k = 0;
		for (uint64_t j = 0, start; (start = t + j * e->buffer) < end; j++)
		{
			uint8_t buf[64];
			unsigned count = 0;
			uint64_t c;
			while ((c = render_clockAt(t, k, CLOCK_DIN, e->mbpm)) < start + e->buffer && c < end && count < 16u)
			{
				buf[4 * count] = 0x0F;
				buf[4 * count + 1] = 0xF8;
				buf[4 * count + 2] = 0;
				buf[4 * count + 3] = 0;
				count++;
				k++;
			}
			if (count)
				(void)emu_inUsb(start, buf, 4u * count);
		}
		break;
	}
	case RS_JACK_CLOCK:
	{
		if (!e->mbpm)
			break;
		const uint64_t end = render_clockEnd(i);
		const uint64_t m = e->mbpm * (uint64_t)e->b;
		for (uint64_t k = 0, c; (c = render_clockAt(t, k, CLOCK_JACK, m)) < end; k++)
			emu_inClockPulse(c);
		break;
	}
	case RS_JACK_PULSE:
		emu_inClockPulse(t);
		break;
	case RS_RESET:
		emu_inReset(t, e->a);
		break;
	case RS_CARD:
		emu_inCard(t, e->a);
		break;
	case RS_JACKS:
		emu_inJacks(t, (uint8_t)e->a);
		break;
	case RS_MARK:
		break;	/* a direct event */
	}
}

/* P3 step 6: START (SHIFT + START for --song), the script's timed events, the LED latch */
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
	for (unsigned i = 0; i < R.script.n; i++)
		render_inject(i);
	(void)emu_ledSnapshot();
}

static int render_isDirect(const render_event_t *e)
{
	return e->kind == RS_MARK;
}

/* the first direct event at or after index i, or R.script.n */
static unsigned render_findDirect(unsigned i)
{
	while (i < R.script.n && !render_isDirect(&R.script.ev[i]))
		i++;
	return i;
}

/* P3 step 7: a direct event at a pass boundary */
static void render_runDirect(const render_event_t *e)
{
	const uint64_t now = emu_now();
	if (R.ndirect < RENDER_MAX_DIRECT)
		R.direct[R.ndirect++] = (render_direct_t){ e->line, render_eventName(e->kind), R.t0 + e->t, now };
	if (e->kind == RS_MARK && now < R.tEnd)
	{
		emu_lcdSnapshot_t lcd;
		emu_lcdSnapshot(&lcd);
		render_outMark(&R.out, now - R.t0, e->name, &lcd, emu_ledSnapshot());
	}
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
	s.direct = R.direct;
	s.ndirect = R.ndirect;
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
	if (o->events && render_scriptLoad(o->events, &R.script, err, sizeof err) != 0)
		render_die(1, "%s", err);
	const int status = render_cardPrepare(o, R.copy, &R.project, err, sizeof err, render_warn);
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
	cfg.sinks.din = render_din;
	cfg.sinks.usb = render_usb;
	cfg.sinks.clockOut = render_clock;
	cfg.hook = render_hook;
	cfg.fault = render_onFault;
	cfg.warn = render_onWarn;
	emu_powerOn(&cfg);
	emu_runUntil(1u);	/* the boot */
	R.preludeEnd = emu_now();
	render_settle();

	/* the prelude's direct calls run at this boundary, which is the prelude's last
	 * event: --bpm; the sync fallback and --param come in Task R2.5 */
	if (o->bpm >= 0)
	{
		uint8_t v = (uint8_t)o->bpm;
		emu_directCall(rfw_bpm, &v);
	}
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
	R.nextDirect = render_findDirect(0);
	for (;;)
	{
		uint64_t until = R.tEnd + EMU_CPU_HZ;
		if (R.nextDirect < R.script.n && R.t0 + R.script.ev[R.nextDirect].t < until)
			until = R.t0 + R.script.ev[R.nextDirect].t;
		emu_runUntil(until);
		if (emu_now() > R.tEnd + EMU_CPU_HZ)
			render_fault("the render did not end at its end time");
		while (R.nextDirect < R.script.n && R.t0 + R.script.ev[R.nextDirect].t <= emu_now())
		{
			render_runDirect(&R.script.ev[R.nextDirect]);
			R.nextDirect = render_findDirect(R.nextDirect + 1);
		}
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
