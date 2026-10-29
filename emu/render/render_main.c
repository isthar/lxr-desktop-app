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
#include <limits.h>
#include <pthread.h>
#include <signal.h>
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
#if EMU_RENDER_TEST_HOOKS
#include "emu_internal.h"	/* emu_fault(), for --test-fault model only */
#endif

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
	char **warnings;			/* every warning, for the summary and stderr */
	unsigned nwarnings, capWarnings;
	render_script_t script;
	unsigned nextDirect;		/* the script index from which to look for the next direct event */
	render_direct_t *direct;	/* every direct event that ran, for the summary */
	unsigned ndirect, capDirect;
	uint64_t scriptInputs;		/* the emu_in*() calls of the script's timed events */
	/* --param, in command-line order: the edits, and for the summary the names,
	 * the numbers and the values after the prelude */
	rfw_param_t params[RENDER_MAX_PARAMS];
	const char *paramNames[RENDER_MAX_PARAMS];
	char paramNumText[RENDER_MAX_PARAMS][8];
	unsigned paramNumbers[RENDER_MAX_PARAMS];
	uint8_t paramValues[RENDER_MAX_PARAMS];
} R;

static double wallNow(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* the warnings, last on stderr at every exit (plan P5 prints them after the short
 * summary at exit 0); while seeding they are printed as they come */
static void render_printWarnings(void)
{
	for (unsigned i = 0; i < R.nwarnings; i++)
		fprintf(stderr, "lichomat-render: warning: %s\n", R.warnings[i]);
}

/* print "lichomat-render: " + the message + a newline to stderr, then the warnings,
 * and exit (spec 5.7); the card copy goes first */
static void render_die(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3), noreturn));
static void render_die(int code, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("lichomat-render: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	render_printWarnings();
	render_cardRemove(R.copy);
	exit(code);
}

/* --- faults and the watchdog (spec 5.7, 11, plan P2) ---------------------------- */

/* a line for stderr, built without stdio: the paths below also run in a signal
 * handler and on the watchdog thread */
typedef struct
{
	char b[EMU_HOST_PATH + 256];
	size_t n;
} render_line_t;

static void lineAdd(render_line_t *l, const char *s)
{
	while (*s && l->n < sizeof l->b - 1u)
		l->b[l->n++] = *s++;
}

static void lineNumber(render_line_t *l, uint64_t v)
{
	char d[20];
	unsigned k = 0;
	do
	{
		d[k++] = (char)('0' + v % 10u);
		v /= 10u;
	} while (v);
	while (k && l->n < sizeof l->b - 1u)
		l->b[l->n++] = d[--k];
}

static void lineWrite(render_line_t *l)
{
	l->b[l->n++] = '\n';
	(void)!write(2, l->b, l->n);
	l->n = 0;
}

/* Codes 3 and 4 (plan P2): "lichomat-render: HEAD WHAT at cycle C", the last 32
 * trail lines, the card copy's folder, the warnings; write(2) only, then _exit() without
 * cleanup, so the copy stays for inspection. The watchdog calls it while the
 * firmware thread makes no progress, so it is not in the scheduler, which alone
 * moves the clock and writes the trail: both hold still while they are read. */
static void render_abort(int code, const char *head, const char *what) __attribute__((noreturn));
static void render_abort(int code, const char *head, const char *what)
{
	render_line_t l;
	l.n = 0;
	lineAdd(&l, "lichomat-render: ");
	lineAdd(&l, head);
	lineAdd(&l, what);
	lineAdd(&l, " at cycle ");
	lineNumber(&l, emu_now());
	lineWrite(&l);
	emu_trailWrite(2, 32);
	if (R.copy[0])
	{
		lineAdd(&l, "lichomat-render: the card copy is kept in ");
		lineAdd(&l, R.copy);
		lineWrite(&l);
	}
	for (unsigned i = 0; i < R.nwarnings; i++)
	{
		lineAdd(&l, "lichomat-render: warning: ");
		lineAdd(&l, R.warnings[i]);
		lineWrite(&l);
	}
	_exit(code);
}

/* code 3: a model check, a failed check of the renderer, a fault signal */
static void render_fault(const char *what) __attribute__((noreturn));
static void render_fault(const char *what)
{
	render_abort(3, "firmware fault: ", what);
}

static void render_onSignal(int sig)
{
	const char *name = "a fault signal";
	switch (sig)
	{
	case SIGSEGV:	name = "SIGSEGV"; break;
	case SIGBUS:	name = "SIGBUS"; break;
	case SIGFPE:	name = "SIGFPE"; break;
	case SIGILL:	name = "SIGILL"; break;
	case SIGABRT:	name = "SIGABRT"; break;
	}
	render_fault(name);
}

/* the fault signals, on an alternate stack so that a stack overflow is reported
 * too; a second fault inside the handler takes the default action */
static void render_signals(void)
{
	static char altStack[1u << 17];
	stack_t ss;
	memset(&ss, 0, sizeof ss);
	ss.ss_sp = altStack;
	ss.ss_size = sizeof altStack;
	(void)sigaltstack(&ss, NULL);
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = render_onSignal;
	sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	static const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
	for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
		(void)sigaction(sigs[i], &sa, NULL);
}

/* A sanitizer report is a firmware fault (spec 11): both sanitizers end in
 * abort() on every platform, which the SIGABRT handler reports (plan P2). */
const char *__asan_default_options(void) __attribute__((used, visibility("default")));
const char *__asan_default_options(void)
{
	return "abort_on_error=1";
}

const char *__ubsan_default_options(void) __attribute__((used, visibility("default")));
const char *__ubsan_default_options(void)
{
	return "abort_on_error=1";
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
	printf("Runs LICHOMAT %s with the emulator changes F1-F12 and F6b. Built from commit %s.\n",
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

/* a model check (spec 3.4, 5.7 code 3) */
static void render_onFault(void *ctx, const char *what)
{
	(void)ctx;
	render_fault(what);
}

/* a warning (spec 4.8, 11): kept for the summary and printed last at the exit;
 * printed at once while seeding */
static void render_warn(const char *what)
{
	if (!R.o)
	{
		fprintf(stderr, "lichomat-render: warning: %s\n", what);
		return;
	}
	if (R.nwarnings == R.capWarnings)
	{
		const unsigned cap = R.capWarnings ? 2u * R.capWarnings : 32u;
		char **grown = realloc(R.warnings, cap * sizeof *grown);
		if (!grown)
			render_fault("out of memory for the warnings");
		R.warnings = grown;
		R.capWarnings = cap;
	}
	char *copy = strdup(what);
	if (!copy)
		render_fault("out of memory for the warnings");
	R.warnings[R.nwarnings++] = copy;
}

/* A warning of the device (emu_card.c) names the card copy, a temporary folder
 * that is gone after the render; it names the card instead, as the renderer's own
 * warnings do: every occurrence of the copy's path becomes the card's. */
static void render_onWarn(void *ctx, const char *what)
{
	(void)ctx;
	const size_t n = strlen(R.copy);
	if (!R.o || !n || !strstr(what, R.copy))
	{
		render_warn(what);
		return;
	}
	size_t cardLen = strlen(R.o->card);
	while (cardLen > 1u && R.o->card[cardLen - 1u] == '/')
		cardLen--;
	char text[2u * EMU_HOST_PATH + 256u];
	size_t len = 0;
	for (const char *p = what; *p && len < sizeof text - 1u;)
		if (!strncmp(p, R.copy, n))
		{
			for (size_t k = 0; k < cardLen && len < sizeof text - 1u; k++)
				text[len++] = R.o->card[k];
			p += n;
		}
		else
			text[len++] = *p++;
	text[len] = '\0';
	render_warn(text);
}

/* code 4 (spec 4.2, plan P2): no scheduler progress for 10 s of wall time outside
 * the hook's waits, polled every 100 ms */
static void *render_watchdog(void *arg)
{
	(void)arg;
	emu_watchdog_t w;
	memset(&w, 0, sizeof w);
	for (;;)
	{
		const struct timespec d = { 0, 100000000L };
		(void)nanosleep(&d, NULL);
		if (emu_watchdogPoll(&w, wallNow()))
			render_abort(4, "hang: ", "no progress for 10 s");
	}
	return NULL;
}

/* after power-on */
static void render_startWatchdog(void)
{
	pthread_t thread;
	const int e = pthread_create(&thread, NULL, render_watchdog, NULL);
	if (e != 0)
	{
		char what[128];
		snprintf(what, sizeof what, "the watchdog thread did not start: %s", strerror(e));
		render_warn(what);
		return;
	}
	(void)pthread_detach(thread);
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
	render_startWatchdog();
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

/* --- the script's timed inputs (P3 step 6, P4) ------------------------------------ */

#define CLOCK_DIN 540000000000ull	/* 24 PPQ: cycles per minute x 1000 / 24 */
#define CLOCK_JACK 12960000000000ull	/* cycles per minute x 1000 */

/* clock k of a generator: t + floor(k * per / mbpm) */
static uint64_t render_clockAt(uint64_t t, uint64_t k, unsigned long long per, uint64_t mbpm)
{
	return t + (uint64_t)((unsigned __int128)k * per / mbpm);
}

static int render_isClock(const render_event_t *e)
{
	return e->kind == RS_DIN_CLOCK || e->kind == RS_USB_CLOCK || e->kind == RS_JACK_CLOCK;
}

/* one input call of the script, counted for the summary */
#define RENDER_IN(call) (R.scriptInputs++, (void)(call))

/* one timed event that is not a clock generator, as emu_in*() inputs (plan P4's
 * input column); the direct events do nothing here */
static void render_inject(const render_event_t *e)
{
	const uint64_t t = R.t0 + e->t;
	switch (e->kind)
	{
	case RS_DOWN:
	case RS_UP:
		RENDER_IN(emu_inButton(t, (unsigned)e->a, e->kind == RS_DOWN));
		break;
	case RS_PRESS:
		RENDER_IN(emu_inButton(t, (unsigned)e->a, 1));
		RENDER_IN(emu_inButton(t + (uint64_t)e->b * EMU_CYCLES_PER_MS, (unsigned)e->a, 0));
		break;
	case RS_ENCODER:
		RENDER_IN(emu_inEncoder(t, e->a));
		break;
	case RS_PUSH:
		RENDER_IN(emu_inPush(t, (unsigned)e->b));
		break;
	case RS_KNOB:
		RENDER_IN(emu_inKnob(t, (unsigned)e->a, e->b));
		break;
	case RS_FADER:
		RENDER_IN(emu_inFader(t, (unsigned)e->a, e->level));
		break;
	case RS_DIN:
		RENDER_IN(emu_inDin(t, e->bytes, e->nbytes));
		break;
	case RS_USB:
		for (unsigned off = 0; off < e->nbytes; off += 64u)
			RENDER_IN(emu_inUsb(t, e->bytes + off, e->nbytes - off > 64u ? 64u : e->nbytes - off));
		break;
	case RS_JACK_PULSE:
		RENDER_IN(emu_inClockPulse(t));
		break;
	case RS_RESET:
		RENDER_IN(emu_inReset(t, e->a));
		break;
	case RS_CARD:
		RENDER_IN(emu_inCard(t, e->a));
		break;
	case RS_JACKS:
		RENDER_IN(emu_inJacks(t, (uint8_t)e->a));
		break;
	case RS_DIN_CLOCK:
	case RS_USB_CLOCK:
	case RS_JACK_CLOCK:
		break;	/* render_gen_t */
	case RS_MARK:
	case RS_PARAM:
	case RS_SAVE_KIT:
	case RS_SAVE_PATTERN:
	case RS_SAVE_PROJECT:
		break;	/* direct events */
	}
}

/* A clock generator of the script (plan P4): its clocks from its line's time
 * until the next line of its kind or the end, whichever comes first. */
typedef struct
{
	const render_event_t *e;
	unsigned line;		/* its index in the script */
	uint64_t t, end;
	uint64_t k;			/* the next clock */
	uint64_t next;		/* the time of its next input; EMU_NEVER when done */
} render_gen_t;

static uint64_t render_genClock(const render_gen_t *g, uint64_t k)
{
	if (g->e->kind == RS_JACK_CLOCK)
		return render_clockAt(g->t, k, CLOCK_JACK, g->e->mbpm * (uint64_t)g->e->b);
	return render_clockAt(g->t, k, CLOCK_DIN, g->e->mbpm);
}

/* the time of g's next input: its next clock, or for a usb-clock with a buffer the
 * start of the buffer that holds it (no transfer for an empty buffer) */
static void render_genNext(render_gen_t *g)
{
	const uint64_t c = render_genClock(g, g->k);
	if (c >= g->end)
		g->next = EMU_NEVER;
	else if (g->e->kind == RS_USB_CLOCK && g->e->buffer)
		g->next = g->t + (c - g->t) / g->e->buffer * g->e->buffer;
	else
		g->next = c;
}

static void render_genInject(render_gen_t *g)
{
	static const uint8_t f8 = 0xF8;
	static const uint8_t packet[4] = { 0x0F, 0xF8, 0, 0 };
	switch (g->e->kind)
	{
	case RS_DIN_CLOCK:
		RENDER_IN(emu_inDin(g->next, &f8, 1));
		g->k++;
		break;
	case RS_JACK_CLOCK:
		RENDER_IN(emu_inClockPulse(g->next));
		g->k++;
		break;
	default:	/* RS_USB_CLOCK */
		if (!g->e->buffer)
		{
			RENDER_IN(emu_inUsb(g->next, packet, 4));
			g->k++;
			break;
		}
		/* the clocks of [start, start + buffer), at most 16 (one transfer) */
		uint8_t buf[64];
		unsigned count = 0;
		uint64_t c;
		while (count < 16u && (c = render_genClock(g, g->k)) < g->next + g->e->buffer && c < g->end)
		{
			memcpy(&buf[4u * count++], packet, 4);
			g->k++;
		}
		RENDER_IN(emu_inUsb(g->next, buf, 4u * count));
		break;
	}
	render_genNext(g);
}

/* P3 step 6: START (SHIFT + START for --song), then the script's timed inputs in
 * time order, the clock generators' clocks merged with the other lines, since the
 * DIN and USB inputs queue in the order they are given (spec 5.4: each enters at
 * its own time); at equal times in script order. Then the LED latch. */
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
	const render_script_t *sc = &R.script;
	render_gen_t *gens = NULL;
	unsigned ngens = 0;
	for (unsigned i = 0; i < sc->n; i++)
	{
		const render_event_t *e = &sc->ev[i];
		if (!render_isClock(e) || !e->mbpm)
			continue;
		uint64_t end = R.tEnd;
		for (unsigned j = i + 1; j < sc->n; j++)
			if (sc->ev[j].kind == e->kind)
			{
				if (R.t0 + sc->ev[j].t < end)
					end = R.t0 + sc->ev[j].t;
				break;
			}
		render_gen_t *grown = realloc(gens, (ngens + 1u) * sizeof *gens);
		if (!grown)
			render_fault("out of memory for the script's clocks");
		gens = grown;
		gens[ngens] = (render_gen_t){ e, i, R.t0 + e->t, end, 0, 0 };
		render_genNext(&gens[ngens++]);
	}
	unsigned line = 0;
	for (;;)
	{
		while (line < sc->n && render_isClock(&sc->ev[line]))
			line++;
		uint64_t best = line < sc->n ? R.t0 + sc->ev[line].t : EMU_NEVER;
		unsigned bestLine = line < sc->n ? line : UINT_MAX;
		render_gen_t *g = NULL;
		for (unsigned k = 0; k < ngens; k++)
			if (gens[k].next != EMU_NEVER && (gens[k].next < best || (gens[k].next == best && gens[k].line < bestLine)))
			{
				g = &gens[k];
				best = g->next;
				bestLine = g->line;
			}
		if (g)
			render_genInject(g);
		else if (line < sc->n)
			render_inject(&sc->ev[line++]);
		else
			break;
	}
	free(gens);
	(void)emu_ledSnapshot();
}

static int render_isDirect(const render_event_t *e)
{
	return e->kind == RS_MARK || e->kind == RS_PARAM || e->kind == RS_SAVE_KIT || e->kind == RS_SAVE_PATTERN
		|| e->kind == RS_SAVE_PROJECT;
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
	if (R.ndirect == R.capDirect)
	{
		const unsigned cap = R.capDirect ? 2u * R.capDirect : 256u;
		render_direct_t *grown = realloc(R.direct, cap * sizeof *grown);
		if (!grown)
			render_fault("out of memory for the direct events");
		R.direct = grown;
		R.capDirect = cap;
	}
	R.direct[R.ndirect++] = (render_direct_t){ e->line, render_eventName(e->kind), R.t0 + e->t, now };
	if (e->kind == RS_MARK && now < R.tEnd)
	{
		emu_lcdSnapshot_t lcd;
		emu_lcdSnapshot(&lcd);
		render_outMark(&R.out, now - R.t0, e->name, &lcd, emu_ledSnapshot());
	}
	else if (e->kind == RS_PARAM)
	{
		rfw_param_t edit = { (uint16_t)e->a, (uint8_t)e->b };
		emu_directCall(rfw_edit, &edit);
	}
	else if (e->kind == RS_SAVE_KIT || e->kind == RS_SAVE_PATTERN || e->kind == RS_SAVE_PROJECT)
	{
		uint8_t n = (uint8_t)e->a;
		emu_directCall(e->kind == RS_SAVE_KIT ? rfw_saveKit : e->kind == RS_SAVE_PATTERN ? rfw_savePattern
			: rfw_saveProject, &n);
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
	s.paramNames = R.paramNames;
	s.paramNumbers = R.paramNumbers;
	s.paramValues = R.paramValues;
	s.nparams = o->nparams;
	s.direct = R.direct;
	s.scriptInputs = R.scriptInputs;
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
	}
	render_printWarnings();
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

#define MS(x) ((uint64_t)(x) * EMU_CYCLES_PER_MS)

/* button b down at t and up after ms */
static void render_press(uint64_t t, unsigned b, unsigned ms)
{
	emu_inButton(t, b, 1);
	emu_inButton(t + MS(ms), b, 0);
}

/* step n (1-16) as a button (spec 4.6): steps 16-13 are 12-15, 12-9 are 20-23,
 * 8-5 are 28-31, 4-1 are 36-39 */
static unsigned render_stepButton(unsigned n)
{
	static const unsigned base[4] = { 36, 28, 20, 12 };
	return base[(n - 1u) / 4u] + 3u - (n - 1u) % 4u;
}

/* passes until e, the prelude's last event, then a settle (P3 steps 3 and 4) */
static void render_settleAfter(uint64_t e)
{
	while (emu_now() < e)
		render_pass();
	R.preludeEnd = e;
	render_settle();
}

/* --pattern P (P3 step 4), the buttons a user presses (buttonHandler.c): SHIFT +
 * step 16 opens the pattern page, bar- three times goes to bank 0, bar+ once per
 * bank of 16, then step (P mod 16) + 1 selects the pattern */
static void render_patternWalk(int p)
{
	const uint64_t w = emu_now();
	const unsigned banks = (unsigned)p / 16u;
	emu_inButton(w, 32, 1);
	render_press(w + MS(20), 12, 50);
	emu_inButton(w + MS(100), 32, 0);
	for (unsigned j = 0; j < 3; j++)
		render_press(w + MS(150 + 100 * j), 40, 50);
	for (unsigned j = 0; j < banks; j++)
		render_press(w + MS(450 + 100 * j), 0, 50);
	render_press(w + MS(450 + 100 * banks), render_stepButton((unsigned)p % 16u + 1u), 50);
	render_settleAfter(w + MS(450 + 100 * banks + 50));
	render_selected_t sel;
	emu_directCall(rfw_selected, &sel);
	if (sel.pattern != p)
	{
		char what[96];
		snprintf(what, sizeof what, "the --pattern walk ended on pattern %d, not %d", sel.pattern, p);
		render_fault(what);
	}
}

/* --song S (P3 step 4), the menu's load page (buttonHandler.c, menu.c): MODE3
 * (LOAD) + step 10 opens it for songs, the encoder goes to 0 (the menu clamps) and
 * up to S, and a push loads the song */
static void render_songWalk(int song)
{
	const uint64_t w = emu_now();
	emu_inButton(w, 26, 1);
	render_press(w + MS(20), 22, 50);
	emu_inButton(w + MS(100), 26, 0);
	emu_inEncoder(w + MS(150), -64);
	if (song > 0)
		emu_inEncoder(w + MS(500), song);
	emu_inPush(w + MS(800), 50);
	render_settleAfter(w + MS(850));
	render_selected_t sel;
	emu_directCall(rfw_selected, &sel);
	if (sel.song != song)
	{
		char what[96];
		snprintf(what, sizeof what, "the --song walk ended on song %d, not %d", sel.song, song);
		render_fault(what);
	}
}

/* the sync fallback (spec 5.1, plan P3): without --bpm, a project in EXT or MID
 * sync whose script sends no clock to that input plays at 120 BPM internal */
static void render_syncFallback(void)
{
	render_selected_t sel;
	emu_directCall(rfw_selected, &sel);
	if ((sel.sync != 1 && sel.sync != 2) || render_scriptHasClock(&R.script, sel.sync))
		return;
	uint8_t v = 120;
	emu_directCall(rfw_bpm, &v);
	render_warn(sel.sync == 1 ? "the project syncs to EXT; the script sends no clock to that input: 120 BPM internal"
		: "the project syncs to MID; the script sends no clock to that input: 120 BPM internal");
}

/* the length in blocks (P3 step 4): whole blocks, rounded up from the frames */
static uint64_t render_blocks(const render_opts_t *o)
{
	unsigned bpm = 0;
	if (o->bars)
	{
		/* B: the numeric --bpm, else seq_getBpm() in internal sync, read after the
		 * prelude, so after the sync fallback and the --param edits */
		if (o->bpm >= 2)
			bpm = (unsigned)o->bpm;
		else if (R.selected.sync == 0)
			bpm = (unsigned)R.selected.bpm;
		else
			render_die(1, "--bars: needs an internal tempo; the project syncs to %s",
				R.selected.sync == 1 ? "EXT" : "MID");
	}
	/* the options checked the RIFF limit already, except for --bars at the card's
	 * tempo, which only the prelude gives */
	char err[256];
	if (render_optsLengthCheck(o, bpm, err, sizeof err) != 0)
		render_die(1, "%s", err);
	return render_optsBlocks(o, bpm);
}

#if EMU_RENDER_TEST_HOOKS
/* --test-fault segv: a read through NULL. UBSan's null check is off here, so
 * that the read is a real SIGSEGV in a UBSan build too. */
__attribute__((no_sanitize("undefined"))) static void render_testSegv(void)
{
	volatile int *volatile p = NULL;
	(void)*p;
}

/* --test-fault and --test-hang (P3 step 10): a direct call at the first pass
 * boundary at or after t0 + 100 ms */
static void render_testCall(void *arg)
{
	const render_opts_t *o = arg;
	if (o->testHang)
	{
		volatile unsigned spin = 0;
		for (;;)
			spin++;	/* without the HAL: no scheduler progress */
	}
	switch (o->testFault)
	{
	case RENDER_FAULT_SEGV:
		render_testSegv();
		break;
	case RENDER_FAULT_ABORT:
		abort();
	case RENDER_FAULT_MODEL:
		emu_fault("test fault");
		break;
	case RENDER_FAULT_UBSAN:
	{
		volatile int big = INT_MAX;
		big = big + 1;	/* a signed overflow: a report in a UBSan build */
		break;
	}
	}
}
#endif

static void render_run(const render_opts_t *o) __attribute__((noreturn));
static void render_run(const render_opts_t *o)
{
	char err[EMU_HOST_PATH + 256];
	R.wallStart = wallNow();
	R.o = o;
	/* the --param names before power-on (plan R2.5) */
	for (unsigned i = 0; i < o->nparams; i++)
	{
		const char *name;
		if (rfw_paramParse(o->params[i], &R.params[i], &name, err, sizeof err) != 0)
			render_die(1, "--param: %s", err);
		R.paramNumbers[i] = R.params[i].p;
		if (!name)
		{
			snprintf(R.paramNumText[i], sizeof R.paramNumText[i], "%u", R.params[i].p);
			name = R.paramNumText[i];
		}
		R.paramNames[i] = name;
	}
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
	render_startWatchdog();
	emu_runUntil(1u);	/* the boot */
	R.preludeEnd = emu_now();
	render_settle();

	/* the prelude (P3 step 4): what plays, through the panel */
	if (o->pattern >= 0)
		render_patternWalk(o->pattern);
	else if (o->song >= 0)
		render_songWalk(o->song);
	/* then at one pass boundary, the prelude's last event: --bpm, the sync
	 * fallback, each --param in command-line order, all through the menu's edit
	 * path */
	if (o->bpm >= 0)
	{
		uint8_t v = (uint8_t)o->bpm;
		emu_directCall(rfw_bpm, &v);
	}
	else
		render_syncFallback();
	for (unsigned i = 0; i < o->nparams; i++)
		emu_directCall(rfw_edit, &R.params[i]);
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
	for (unsigned i = 0; i < o->nparams; i++)
	{
		rfw_param_t get = R.params[i];
		emu_directCall(rfw_paramGet, &get);
		R.paramValues[i] = get.v;
	}
	R.blocks = render_blocks(o);

	R.phase = PH_WAIT;
	while (R.phase == PH_WAIT)
		render_pass();

	/* P3 step 7: render_finish() ends the process from the hook */
	R.nextDirect = render_findDirect(0);
#if EMU_RENDER_TEST_HOOKS
	int testPending = o->testFault != RENDER_FAULT_NONE || o->testHang;
	uint64_t sliceFrames = 470u;	/* --test-slice: slices of 470 and 471 frames in turn */
	unsigned slices = 0;
#endif
	for (;;)
	{
		uint64_t until = R.tEnd + EMU_CPU_HZ;
		if (R.nextDirect < R.script.n && R.t0 + R.script.ev[R.nextDirect].t < until)
			until = R.t0 + R.script.ev[R.nextDirect].t;
#if EMU_RENDER_TEST_HOOKS
		if (testPending && R.t0 + C100 < until)
			until = R.t0 + C100;
		if (o->testSlice)
		{
			while (R.t0 + emu_framesToCycles(sliceFrames) <= emu_now())
				sliceFrames += (slices++ & 1u) ? 470u : 471u;
			if (R.t0 + emu_framesToCycles(sliceFrames) < until)
				until = R.t0 + emu_framesToCycles(sliceFrames);
		}
#endif
		emu_runUntil(until);
		if (emu_now() > R.tEnd + EMU_CPU_HZ)
			render_fault("the render did not end at its end time");
		while (R.nextDirect < R.script.n && R.t0 + R.script.ev[R.nextDirect].t <= emu_now())
		{
			render_runDirect(&R.script.ev[R.nextDirect]);
			R.nextDirect = render_findDirect(R.nextDirect + 1);
		}
#if EMU_RENDER_TEST_HOOKS
		if (testPending && emu_now() >= R.t0 + C100)
		{
			testPending = 0;
			emu_directCall(render_testCall, (void *)o);
		}
#endif
	}
}

int main(int argc, char **argv)
{
	render_opts_t o;
	char err[512];
	if (render_optsParse(argc, argv, &o, err, sizeof err) != 0)
		render_die(1, "%s", err);
	render_signals();
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
