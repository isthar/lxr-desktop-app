/*
 * render_opts.c
 *
 * The command line of lichomat-render (stage 1b spec 5.1, 5.7 code 1).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "render_opts.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu_core.h"

static int fail(char *err, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int fail(char *err, size_t n, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, n, fmt, ap);
	va_end(ap);
	return -1;
}

/* an unsigned decimal integer: digits only, no sign; 0 or -1 */
static int parseU64(const char *s, uint64_t max, uint64_t *out)
{
	if (!s[0])
		return -1;
	uint64_t v = 0;
	for (const char *p = s; *p; p++)
	{
		if (*p < '0' || *p > '9')
			return -1;
		const unsigned d = (unsigned)(*p - '0');
		if (v > (UINT64_MAX - d) / 10u)
			return -1;
		v = v * 10u + d;
	}
	if (v > max)
		return -1;
	*out = v;
	return 0;
}

static int parseInt(const char *s, int lo, int hi, int *out)
{
	uint64_t v;
	if (parseU64(s, (uint64_t)hi, &v) != 0 || v < (uint64_t)lo)
		return -1;
	*out = (int)v;
	return 0;
}

/* a decimal "digits [. digits]" with at most maxDigits decimals, as num / 10^digits */
static int parseDecimal(const char *s, unsigned maxDigits, uint64_t *num, unsigned *digits)
{
	uint64_t v = 0;
	unsigned intDigits = 0, decimals = 0;
	const char *p = s;
	for (; *p >= '0' && *p <= '9'; p++, intDigits++)
	{
		if (intDigits >= 12)
			return -1;
		v = v * 10u + (unsigned)(*p - '0');
	}
	if (intDigits == 0)
		return -1;
	if (*p == '.')
	{
		p++;
		for (; *p >= '0' && *p <= '9'; p++, decimals++)
		{
			if (decimals >= maxDigits)
				return -1;
			v = v * 10u + (unsigned)(*p - '0');
		}
		if (decimals == 0)
			return -1;
	}
	if (*p)
		return -1;
	*num = v;
	*digits = decimals;
	return 0;
}

static uint64_t pow10u(unsigned d)
{
	uint64_t p = 1;
	while (d--)
		p *= 10u;
	return p;
}

/* a fader level: a decimal 0-1 */
static int parseLevel(const char *s, size_t len, double *out)
{
	char buf[32];
	if (len == 0 || len >= sizeof buf)
		return -1;
	memcpy(buf, s, len);
	buf[len] = '\0';
	uint64_t num;
	unsigned digits;
	if (parseDecimal(buf, 15, &num, &digits) != 0)
		return -1;
	const double v = strtod(buf, NULL);
	if (!(v >= 0.0 && v <= 1.0))
		return -1;
	*out = v;
	return 0;
}

/* all, none, or a list of L1, R1, L2, R2 joined by ',' */
static int parseJacks(const char *s, uint8_t *out)
{
	if (strcmp(s, "all") == 0)
	{
		*out = EMU_JACKS_ALL;
		return 0;
	}
	if (strcmp(s, "none") == 0)
	{
		*out = 0;
		return 0;
	}
	static const char names[4][3] = { "L1", "R1", "L2", "R2" };
	uint8_t mask = 0;
	const char *p = s;
	for (;;)
	{
		const size_t len = strcspn(p, ",");
		int found = -1;
		for (int i = 0; i < 4; i++)
			if (len == 2 && memcmp(p, names[i], 2) == 0)
				found = i;
		if (found < 0)
			return -1;
		mask |= (uint8_t)(1u << found);
		if (!p[len])
			break;
		p += len + 1;
	}
	*out = mask;
	return 0;
}

static void defaults(render_opts_t *o)
{
	memset(o, 0, sizeof *o);
	o->project = -1;
	o->pattern = -1;
	o->song = -1;
	o->bpm = -1;
	o->secondsNum = 10;
	for (int i = 0; i < 6; i++)
		o->faders[i] = 1.0;
	o->jacks = EMU_JACKS_ALL;
	o->seed = 1;
	o->wavRate = 44141;
	o->stereo = RENDER_STEREO_NONE;
}

int render_optsParse(int argc, char **argv, render_opts_t *o, char *err, size_t n)
{
	defaults(o);
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--help") == 0)
			o->help = 1;
		else if (strcmp(argv[i], "--version") == 0)
			o->version = 1;
	}
	if (o->help || o->version)
		return 0;	/* whatever else is given (spec 5.1) */

	for (int i = 1; i < argc; i++)
	{
		const char *opt = argv[i];
		/* the options without a value */
		if (strcmp(opt, "--no-start") == 0)
		{
			o->noStart = 1;
			continue;
		}
		if (strcmp(opt, "--as-booted") == 0)
		{
			o->asBooted = 1;
			continue;
		}
#if EMU_RENDER_TEST_HOOKS
		if (strcmp(opt, "--test-hang") == 0)
		{
			o->testHang = 1;
			continue;
		}
		if (strcmp(opt, "--test-slice") == 0)
		{
			o->testSlice = 1;
			continue;
		}
		if (strcmp(opt, "--test-prelude-reload") == 0)
		{
			o->testPreludeReload = 1;
			continue;
		}
#endif
		static const char *const withValue[] = {
			"--card", "--out", "--seed-card", "--project", "--pattern", "--song", "--bpm", "--param",
			"--seconds", "--bars", "--events", "--faders", "--jacks", "--seed", "--wav-rate", "--stereo",
			"--midi-log", "--clock-log", "--panel-log", "--summary", "--keep-card",
#if EMU_RENDER_TEST_HOOKS
			"--test-fault",
#endif
		};
		int known = 0;
		for (size_t k = 0; k < sizeof withValue / sizeof withValue[0]; k++)
			if (strcmp(opt, withValue[k]) == 0)
				known = 1;
		if (!known)
			return fail(err, n, "%s: unknown option (see --help)", opt);
		if (i + 1 >= argc)
			return fail(err, n, "%s: needs a value", opt);
		const char *v = argv[++i];

		if (strcmp(opt, "--card") == 0)
			o->card = v;
		else if (strcmp(opt, "--out") == 0)
			o->out = v;
		else if (strcmp(opt, "--seed-card") == 0)
			o->seedCard = v;
		else if (strcmp(opt, "--project") == 0)
		{
			if (parseInt(v, 0, 63, &o->project) != 0)
				return fail(err, n, "--project: '%s' is not a project 0-63", v);
		}
		else if (strcmp(opt, "--pattern") == 0)
		{
			if (parseInt(v, 0, 63, &o->pattern) != 0)
				return fail(err, n, "--pattern: '%s' is not a pattern 0-63", v);
			o->song = -1;
		}
		else if (strcmp(opt, "--song") == 0)
		{
			if (parseInt(v, 0, 63, &o->song) != 0)
				return fail(err, n, "--song: '%s' is not a song 0-63", v);
			o->pattern = -1;
		}
		else if (strcmp(opt, "--bpm") == 0)
		{
			if (strcmp(v, "ext") == 0)
				o->bpm = 0;
			else if (strcmp(v, "mid") == 0)
				o->bpm = 1;
			else if (parseInt(v, 2, 255, &o->bpm) != 0)
				return fail(err, n, "--bpm: '%s' is not 2-255, 'ext' or 'mid'", v);
		}
		else if (strcmp(opt, "--param") == 0)
		{
			const char *eq = strchr(v, '=');
			uint64_t value;
			if (!eq || eq == v)
				return fail(err, n, "--param: '%s' is not NAME=VALUE", v);
			if (parseU64(eq + 1, 255, &value) != 0)
				return fail(err, n, "--param: '%s': the value is not 0-255", v);
			if (o->nparams >= RENDER_MAX_PARAMS)
				return fail(err, n, "--param: more than %d", RENDER_MAX_PARAMS);
			o->params[o->nparams++] = v;
		}
		else if (strcmp(opt, "--seconds") == 0)
		{
			uint64_t num;
			unsigned digits;
			if (parseDecimal(v, 6, &num, &digits) != 0 || num == 0 || num > 86400u * pow10u(digits))
				return fail(err, n, "--seconds: '%s' is not a decimal above 0 and up to 86400, with at most 6 decimals", v);
			o->secondsNum = num;
			o->secondsDigits = digits;
			o->bars = 0;
		}
		else if (strcmp(opt, "--bars") == 0)
		{
			int bars;
			if (parseInt(v, 1, 9999, &bars) != 0)
				return fail(err, n, "--bars: '%s' is not an integer 1-9999", v);
			o->bars = (unsigned)bars;
		}
		else if (strcmp(opt, "--events") == 0)
			o->events = v;
		else if (strcmp(opt, "--faders") == 0)
		{
			const char *p = v;
			for (int f = 0; f < 6; f++)
			{
				const size_t len = strcspn(p, ",");
				if (parseLevel(p, len, &o->faders[f]) != 0)
					return fail(err, n, "--faders: '%s' is not six levels 0-1 joined by ','", v);
				p += len;
				if (f < 5)
				{
					if (*p != ',')
						return fail(err, n, "--faders: '%s' is not six levels 0-1 joined by ','", v);
					p++;
				}
			}
			if (*p)
				return fail(err, n, "--faders: '%s' is not six levels 0-1 joined by ','", v);
		}
		else if (strcmp(opt, "--jacks") == 0)
		{
			if (parseJacks(v, &o->jacks) != 0)
				return fail(err, n, "--jacks: '%s' is not all, none or a list of L1, R1, L2, R2", v);
		}
		else if (strcmp(opt, "--seed") == 0)
		{
			if (parseU64(v, UINT64_MAX, &o->seed) != 0)
				return fail(err, n, "--seed: '%s' is not an unsigned 64-bit integer", v);
		}
		else if (strcmp(opt, "--wav-rate") == 0)
		{
			if (strcmp(v, "44141") == 0)
				o->wavRate = 44141;
			else if (strcmp(v, "44100") == 0)
				o->wavRate = 44100;
			else
				return fail(err, n, "--wav-rate: '%s' is not 44141 or 44100", v);
		}
		else if (strcmp(opt, "--stereo") == 0)
		{
			if (strcmp(v, "out1") == 0)
				o->stereo = RENDER_STEREO_OUT1;
			else if (strcmp(v, "out2") == 0)
				o->stereo = RENDER_STEREO_OUT2;
			else if (strcmp(v, "sum") == 0)
				o->stereo = RENDER_STEREO_SUM;
			else
				return fail(err, n, "--stereo: '%s' is not out1, out2 or sum", v);
		}
		else if (strcmp(opt, "--midi-log") == 0)
			o->midiLog = v;
		else if (strcmp(opt, "--clock-log") == 0)
			o->clockLog = v;
		else if (strcmp(opt, "--panel-log") == 0)
			o->panelLog = v;
		else if (strcmp(opt, "--summary") == 0)
			o->summary = v;
		else if (strcmp(opt, "--keep-card") == 0)
			o->keepCard = v;
#if EMU_RENDER_TEST_HOOKS
		else if (strcmp(opt, "--test-fault") == 0)
		{
			static const char *const faults[] = { "segv", "abort", "model", "ubsan" };
			o->testFault = RENDER_FAULT_NONE;
			for (int k = 0; k < 4; k++)
				if (strcmp(v, faults[k]) == 0)
					o->testFault = RENDER_FAULT_SEGV + k;
			if (o->testFault == RENDER_FAULT_NONE)
				return fail(err, n, "--test-fault: '%s' is not segv, abort, model or ubsan", v);
		}
#endif
	}

	/* the modes (spec 5.1) */
	if (o->seedCard)
	{
		if (o->card)
			return fail(err, n, "--seed-card: not with --card");
		if (o->out)
			return fail(err, n, "--seed-card: not with --out");
		return 0;
	}
	if (!o->card && !o->out)
		return fail(err, n, "--card: missing; render with --card DIR --out FILE, or see --help");
	if (!o->card)
		return fail(err, n, "--card: missing; a render needs --card and --out");
	if (!o->out)
		return fail(err, n, "--out: missing; a render needs --card and --out");
	return 0;
}

void render_optsHelp(void)
{
	static const char *const lines[] = {
		"lichomat-render --card DIR --out OUT.wav [options]",
		"lichomat-render --seed-card DIR",
		"lichomat-render --version | --help",
		"",
		"  --project N             load project N (0-63); default: the one in /PROJ.IDX, else 0",
		"  --pattern P             play pattern P (0-63) in pattern mode",
		"  --song S                play song S (0-63) in song mode",
		"  --bpm B                 internal sync at B BPM (2-255); 'ext' or 'mid' for those sync modes",
		"  --param NAME=VALUE      set a parameter through the menu's edit path; repeatable",
		"  --seconds T             length after script time 0; default 10",
		"  --bars N                length in 4/4 bars of the internal tempo, instead of --seconds",
		"  --events FILE           event script",
		"  --no-start              the script starts the transport itself; nothing is pressed at t0",
		"  --faders F1,F2,F3,F4,F5,F6   fader levels 0-1; default all 1",
		"  --jacks all|none|LIST   plugged output jacks, LIST from L1,R1,L2,R2; default all",
		"  --seed N                random seed; default 1",
		"  --as-booted             keep /TMP, as the device does at power-on; default: /TMP is left out",
		"  --wav-rate 44141|44100  rate written into the WAV header; default 44141",
		"  --stereo out1|out2|sum  write a stereo WAV of one output, or of (out1 + out2) / 2",
		"  --midi-log FILE         MIDI-out log",
		"  --clock-log FILE        clock-out log",
		"  --panel-log FILE        LCD and LED snapshots at the script's marks",
		"  --summary FILE          counters and times, in JSON",
		"  --keep-card DIR         keep the card copy's final state in DIR",
		"  --seed-card DIR         write an init card into DIR, a missing or empty folder, and exit",
		"  --version               the version, the commit and the notices",
		"  --help                  this text",
#if EMU_RENDER_TEST_HOOKS
		"",
		"test build only:",
		"  --test-fault segv|abort|model|ubsan   a fault 100 ms after t0",
		"  --test-hang             spin forever 100 ms after t0",
		"  --test-slice            run in slices of 470 and 471 frames, the hook waiting at every call",
		"  --test-prelude-reload   reload the kit after the prelude's direct calls",
#endif
	};
	for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++)
		printf("%s\n", lines[i]);
}
