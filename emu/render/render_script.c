/*
 * render_script.c
 *
 * The event scripts of lichomat-render (stage 1b spec 5.4, plan P4); see
 * render_script.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "render_script.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "emu_core.h"
#include "render_fw.h"

#define MAX_TOKENS 300
#define CLOCK_DIN 540000000000ull	/* cycles per minute x 1000 / 24 PPQ */
#define CLOCK_JACK 12960000000000ull	/* cycles per minute x 1000 */

static const char *const kindNames[] = {
	"down", "up", "press", "encoder", "push", "knob", "fader", "din", "usb", "din-clock", "usb-clock",
	"jack-clock", "jack-pulse", "reset", "card", "jacks", "mark", "param", "save-kit", "save-pattern",
	"save-project",
};

const char *render_eventName(render_eventKind_t kind)
{
	return (unsigned)kind < sizeof kindNames / sizeof kindNames[0] ? kindNames[kind] : "?";
}

typedef struct
{
	const char *path;
	unsigned line;
	char *err;
	size_t n;
} ctx_t;

static int bad(const ctx_t *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int bad(const ctx_t *c, const char *fmt, ...)
{
	char what[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(what, sizeof what, fmt, ap);
	va_end(ap);
	snprintf(c->err, c->n, "%s:%u: %s", c->path, c->line, what);
	return -1;
}

/* digits [ "." digits ], at most 6 decimals: *num / 10^*digits; the rest in *end */
static int decimal(const char *s, uint64_t *num, unsigned *digits, const char **end)
{
	uint64_t v = 0;
	unsigned d = 0, n = 0;
	const char *p = s;
	for (; *p >= '0' && *p <= '9'; p++, n++)
	{
		if (v > (UINT64_MAX - 9u) / 10u / 1000000u)
			return -1;
		v = v * 10u + (uint64_t)(*p - '0');
	}
	if (n == 0)
		return -1;
	if (*p == '.')
	{
		p++;
		for (; *p >= '0' && *p <= '9'; p++, d++)
		{
			if (d == 6)
				return -1;
			v = v * 10u + (uint64_t)(*p - '0');
		}
		if (d == 0)
			return -1;
	}
	*num = v;
	*digits = d;
	*end = p;
	return 0;
}

static uint64_t pow10u(unsigned d)
{
	uint64_t v = 1;
	while (d--)
		v *= 10u;
	return v;
}

/* [ "+" ] decimal ( "ms" | "s" ) | [ "+" ] integer "f": cycles, rounded down */
static int parseTime(const char *s, uint64_t *cycles, int *relative)
{
	*relative = *s == '+';
	if (*relative)
		s++;
	uint64_t num;
	unsigned d;
	const char *u;
	if (decimal(s, &num, &d, &u) != 0)
		return -1;
	unsigned __int128 c;
	if (!strcmp(u, "ms"))
		c = (unsigned __int128)num * EMU_CYCLES_PER_MS / pow10u(d);
	else if (!strcmp(u, "s"))
		c = (unsigned __int128)num * EMU_CPU_HZ / pow10u(d);
	else if (!strcmp(u, "f") && d == 0)
		c = emu_framesToCycles(num);
	else
		return -1;
	if (c > (unsigned __int128)(UINT64_MAX / 4u))
		return -1;
	*cycles = (uint64_t)c;
	return 0;
}

/* a whole number with an optional sign; -1 if not one */
static int integer(const char *s, long *out, int signOk)
{
	char *end;
	if (!signOk && (*s == '+' || *s == '-'))
		return -1;
	if (!*s)
		return -1;
	errno = 0;
	const long v = strtol(s, &end, 10);
	if (*end || errno || end == s || (s[0] == '+' && !(s[1] >= '0' && s[1] <= '9'))
		|| (s[0] == '-' && !(s[1] >= '0' && s[1] <= '9')))
		return -1;
	*out = v;
	return 0;
}

/* a button: 0-40 or a name (plan P4, spec 4.6) */
static int button(const char *s)
{
	long v;
	if (integer(s, &v, 0) == 0)
		return v >= 0 && v <= 40 ? (int)v : -1;
	static const struct
	{
		const char *name;
		int b;
	} names[] = {
		{ "bar+", 0 }, { "shift", 32 }, { "start", 33 }, { "rec", 34 }, { "copy", 35 }, { "bar-", 40 },
	};
	for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
		if (!strcasecmp(s, names[i].name))
			return names[i].b;
	long n;
	if (!strncasecmp(s, "voice", 5) && integer(s + 5, &n, 0) == 0 && n >= 1 && n <= 7)
		return (int)n;
	if (!strncasecmp(s, "select", 6) && integer(s + 6, &n, 0) == 0 && n >= 1 && n <= 8)
		return n <= 4 ? (int)(16 + n - 1) : (int)(8 + n - 5);
	if (!strncasecmp(s, "mode", 4) && integer(s + 4, &n, 0) == 0 && n >= 1 && n <= 4)
		return (int)(24 + n - 1);
	if (!strncasecmp(s, "step", 4) && integer(s + 4, &n, 0) == 0 && n >= 1 && n <= 16)
	{
		/* step 16-13 are 12-15, 12-9 are 20-23, 8-5 are 28-31, 4-1 are 36-39 */
		static const int base[4] = { 36, 28, 20, 12 };
		const long g = (n - 1) / 4, k = (n - 1) % 4;
		return (int)(base[g] + 3 - k);
	}
	return -1;
}

static int hexByte(const char *s, uint8_t *out)
{
	unsigned v = 0;
	if (strlen(s) != 2)
		return -1;
	for (int i = 0; i < 2; i++)
	{
		const char ch = s[i];
		unsigned d;
		if (ch >= '0' && ch <= '9')
			d = (unsigned)(ch - '0');
		else if (ch >= 'a' && ch <= 'f')
			d = (unsigned)(ch - 'a' + 10);
		else if (ch >= 'A' && ch <= 'F')
			d = (unsigned)(ch - 'A' + 10);
		else
			return -1;
		v = v * 16u + d;
	}
	*out = (uint8_t)v;
	return 0;
}

/* the data bytes after a status, and the USB-MIDI CIN (plan P4); -1 for F0, F7 and
 * the undefined F4, F5 */
static int midiShape(uint8_t status, unsigned *data, uint8_t *cin)
{
	if (status >= 0x80 && status < 0xF0)
	{
		const unsigned hi = status >> 4;
		*data = (hi == 0xC || hi == 0xD) ? 1u : 2u;
		*cin = (uint8_t)hi;
		return 0;
	}
	switch (status)
	{
	case 0xF1:
	case 0xF3:	*data = 1; *cin = 2; return 0;
	case 0xF2:	*data = 2; *cin = 3; return 0;
	case 0xF6:	*data = 0; *cin = 5; return 0;
	default:
		if (status >= 0xF8)
		{
			*data = 0;
			*cin = 0xF;
			return 0;
		}
		return -1;
	}
}

/* a BPM with up to 3 decimals, 1-1000, as BPM x 1000 */
static int bpm(const char *s, uint64_t *mbpm)
{
	uint64_t num;
	unsigned d;
	const char *end;
	if (decimal(s, &num, &d, &end) != 0 || *end || d > 3)
		return -1;
	const uint64_t m = num * pow10u(3 - d);
	if (m < 1000u || m > 1000000u)
		return -1;
	*mbpm = m;
	return 0;
}

static int jacksMask(const char *s, int *mask)
{
	if (!strcasecmp(s, "all"))
	{
		*mask = EMU_JACKS_ALL;
		return 0;
	}
	if (!strcasecmp(s, "none"))
	{
		*mask = 0;
		return 0;
	}
	static const char *const names[] = { "L1", "R1", "L2", "R2" };
	int m = 0;
	const char *p = s;
	while (*p)
	{
		const char *comma = strchr(p, ',');
		const size_t len = comma ? (size_t)(comma - p) : strlen(p);
		int found = -1;
		for (int i = 0; i < 4; i++)
			if (len == 2 && !strncasecmp(p, names[i], 2))
				found = i;
		if (found < 0)
			return -1;
		m |= 1 << found;
		p += len;
		if (*p == ',')
		{
			p++;
			if (!*p)
				return -1;
		}
	}
	*mask = m;
	return 0;
}

/* one event's arguments into e */
static int parseEvent(const ctx_t *c, render_event_t *e, const char *ev, char **arg, int nargs)
{
	long v;
	size_t k;
	for (k = 0; k < sizeof kindNames / sizeof kindNames[0]; k++)
		if (!strcmp(ev, kindNames[k]))
			break;
	if (k == sizeof kindNames / sizeof kindNames[0])
		return bad(c, "unknown event %s", ev);
	e->kind = (render_eventKind_t)k;
	switch (e->kind)
	{
	case RS_DOWN:
	case RS_UP:
		if (nargs != 1)
			return bad(c, "%s takes one button", ev);
		if ((e->a = button(arg[0])) < 0)
			return bad(c, "unknown button %s", arg[0]);
		return 0;
	case RS_PRESS:
		if (nargs < 1 || nargs > 2)
			return bad(c, "press takes a button and an optional time in ms");
		if ((e->a = button(arg[0])) < 0)
			return bad(c, "unknown button %s", arg[0]);
		e->b = 50;
		if (nargs == 2)
		{
			if (integer(arg[1], &v, 0) != 0 || v < 1 || v > 3600000)
				return bad(c, "press: %s is not a time of at least 1 ms", arg[1]);
			e->b = (int)v;
		}
		return 0;
	case RS_ENCODER:
		if (nargs != 1 || integer(arg[0], &v, 1) != 0 || v == 0 || v > 1000 || v < -1000)
			return bad(c, "encoder takes detents, not 0 and at most 1000 in size");
		e->a = (int)v;
		return 0;
	case RS_PUSH:
		e->b = 50;
		if (nargs > 1)
			return bad(c, "push takes an optional time in ms");
		if (nargs == 1)
		{
			if (integer(arg[0], &v, 0) != 0 || v < 40 || v > 3600000)
				return bad(c, "push: %s is not a time of at least 40 ms", arg[0]);
			e->b = (int)v;
		}
		return 0;
	case RS_KNOB:
		if (nargs != 2)
			return bad(c, "knob takes a knob 1-4 and degrees");
		if (integer(arg[0], &v, 0) != 0 || v < 1 || v > 4)
			return bad(c, "knob %s is not 1-4", arg[0]);
		e->a = (int)v;
		if (integer(arg[1], &v, 1) != 0 || v < -100000 || v > 100000)
			return bad(c, "knob: %s is not degrees", arg[1]);
		e->b = (int)v;
		return 0;
	case RS_FADER:
	{
		if (nargs != 2)
			return bad(c, "fader takes a fader 1-6 and a level 0-1");
		if (integer(arg[0], &v, 0) != 0 || v < 1 || v > 6)
			return bad(c, "fader %s is not 1-6", arg[0]);
		e->a = (int)v;
		uint64_t num;
		unsigned d;
		const char *end;
		if (decimal(arg[1], &num, &d, &end) != 0 || *end || num > pow10u(d))
			return bad(c, "fader: %s is not a level 0-1", arg[1]);
		e->level = (double)num / (double)pow10u(d);
		return 0;
	}
	case RS_DIN:
		if (nargs < 1 || nargs > RENDER_MAX_BYTES)
			return bad(c, "din takes 1-%d hex bytes", RENDER_MAX_BYTES);
		e->bytes = malloc((size_t)nargs);
		if (!e->bytes)
			return bad(c, "out of memory");
		for (int i = 0; i < nargs; i++)
			if (hexByte(arg[i], &e->bytes[i]) != 0)
				return bad(c, "din: %s is not a hex byte of two digits", arg[i]);
		e->nbytes = (unsigned)nargs;
		return 0;
	case RS_USB:
	{
		if (nargs < 1 || nargs > RENDER_MAX_BYTES)
			return bad(c, "usb takes 1-%d hex bytes", RENDER_MAX_BYTES);
		e->bytes = calloc((size_t)nargs, 4);
		if (!e->bytes)
			return bad(c, "out of memory");
		unsigned packets = 0;
		for (int i = 0; i < nargs;)
		{
			uint8_t status, cin;
			unsigned data;
			if (hexByte(arg[i], &status) != 0)
				return bad(c, "usb: %s is not a hex byte of two digits", arg[i]);
			if (status < 0x80)
				return bad(c, "usb: the data byte %s has no status byte", arg[i]);
			if (midiShape(status, &data, &cin) != 0)
				return bad(c, "usb: %s is not a message the script can send (no SysEx)", arg[i]);
			uint8_t *p = &e->bytes[4 * packets++];
			p[0] = cin;
			p[1] = status;
			for (unsigned j = 1; j <= data; j++)
			{
				uint8_t b;
				if (i + (int)j >= nargs)
					return bad(c, "usb: the message %s is short", arg[i]);
				if (hexByte(arg[i + (int)j], &b) != 0)
					return bad(c, "usb: %s is not a hex byte of two digits", arg[i + (int)j]);
				if (b >= 0x80)
					return bad(c, "usb: the message %s is short", arg[i]);
				p[1 + j] = b;
			}
			i += 1 + (int)data;
		}
		e->nbytes = 4u * packets;
		return 0;
	}
	case RS_DIN_CLOCK:
	case RS_USB_CLOCK:
		if (nargs >= 1 && !strcmp(arg[0], "off"))
		{
			if (nargs != 1)
				return bad(c, "%s off takes nothing more", ev);
			e->mbpm = 0;
			return 0;
		}
		if (nargs < 1 || nargs > (e->kind == RS_USB_CLOCK ? 2 : 1))
			return bad(c, e->kind == RS_USB_CLOCK ? "usb-clock takes a BPM and an optional buffer in ms, or off"
				: "din-clock takes a BPM or off");
		if (bpm(arg[0], &e->mbpm) != 0)
			return bad(c, "%s: %s is not a BPM of 1-1000", ev, arg[0]);
		if (nargs == 2)
		{
			uint64_t num;
			unsigned d;
			const char *end;
			if (decimal(arg[1], &num, &d, &end) != 0 || *end || num == 0)
				return bad(c, "usb-clock: %s is not a buffer in ms", arg[1]);
			e->buffer = (uint64_t)((unsigned __int128)num * EMU_CYCLES_PER_MS / pow10u(d));
			/* at most 16 clocks (one transfer) in a buffer: buffer <= 16 clock periods */
			if ((unsigned __int128)e->buffer * e->mbpm > (unsigned __int128)16u * CLOCK_DIN)
				return bad(c, "usb-clock: a buffer of %s ms holds more than 16 clocks", arg[1]);
			if (e->buffer == 0)
				return bad(c, "usb-clock: %s is not a buffer in ms", arg[1]);
		}
		return 0;
	case RS_JACK_CLOCK:
		if (nargs == 1 && !strcmp(arg[0], "off"))
		{
			e->mbpm = 0;
			return 0;
		}
		if (nargs != 2)
			return bad(c, "jack-clock takes a BPM and a PPQ (4 or 24), or off");
		if (bpm(arg[0], &e->mbpm) != 0)
			return bad(c, "jack-clock: %s is not a BPM of 1-1000", arg[0]);
		if (integer(arg[1], &v, 0) != 0 || (v != 4 && v != 24))
			return bad(c, "jack-clock: the PPQ %s is not 4 or 24", arg[1]);
		e->b = (int)v;
		return 0;
	case RS_JACK_PULSE:
		if (nargs != 0)
			return bad(c, "jack-pulse takes nothing");
		return 0;
	case RS_RESET:
	case RS_CARD:
	{
		const char *on = e->kind == RS_RESET ? "on" : "in", *off = e->kind == RS_RESET ? "off" : "out";
		if (nargs != 1 || (strcmp(arg[0], on) && strcmp(arg[0], off)))
			return bad(c, "%s takes %s or %s", ev, on, off);
		e->a = !strcmp(arg[0], on);
		return 0;
	}
	case RS_JACKS:
		if (nargs != 1 || jacksMask(arg[0], &e->a) != 0)
			return bad(c, "jacks takes all, none or a list of L1, R1, L2, R2");
		return 0;
	case RS_MARK:
		if (nargs != 1)
			return bad(c, "mark takes one name");
		if (strlen(arg[0]) >= RENDER_MARK_NAME || strspn(arg[0],
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") != strlen(arg[0]))
			return bad(c, "mark: the name %s is not [A-Za-z0-9_.-], up to %d characters", arg[0],
				RENDER_MARK_NAME - 1);
		strcpy(e->name, arg[0]);
		return 0;
	case RS_PARAM:
	{
		/* the name is checked now, the value clamped when it runs (render_fw.c) */
		if (nargs != 1)
			return bad(c, "param takes one NAME=VALUE");
		rfw_param_t edit;
		const char *name;
		char what[256];
		if (rfw_paramParse(arg[0], &edit, &name, what, sizeof what) != 0)
			return bad(c, "param: %s", what);
		e->a = edit.p;
		e->b = edit.v;
		return 0;
	}
	case RS_SAVE_KIT:
	case RS_SAVE_PATTERN:
	case RS_SAVE_PROJECT:
		if (nargs != 1 || integer(arg[0], &v, 0) != 0 || v > 63)
			return bad(c, "%s takes a %s 0-63", ev, e->kind == RS_SAVE_PROJECT ? "project" : "slot");
		e->a = (int)v;
		return 0;
	}
	return bad(c, "unknown event %s", ev);
}

int render_scriptLoad(const char *path, render_script_t *s, char *err, size_t n)
{
	memset(s, 0, sizeof *s);
	FILE *f = fopen(path, "r");
	if (!f)
	{
		snprintf(err, n, "%s: %s", path, strerror(errno));
		return -1;
	}
	ctx_t c = { path, 0, err, n };
	char buf[8192];
	uint64_t prev = 0;
	int status = 0;
	while (status == 0 && fgets(buf, sizeof buf, f))
	{
		c.line++;
		const size_t len = strlen(buf);
		if (len == sizeof buf - 1 && buf[len - 1] != '\n' && !feof(f))
		{
			status = bad(&c, "the line is longer than %zu characters", sizeof buf - 2);
			break;
		}
		char *hash = strchr(buf, '#');
		if (hash)
			*hash = '\0';
		char *tok[MAX_TOKENS];
		int nt = 0;
		for (char *p = strtok(buf, " \t\r\n"); p; p = strtok(NULL, " \t\r\n"))
		{
			if (nt == MAX_TOKENS)
			{
				status = bad(&c, "more than %d fields", MAX_TOKENS);
				break;
			}
			tok[nt++] = p;
		}
		if (status != 0 || nt == 0)
			continue;
		uint64_t t;
		int rel;
		if (parseTime(tok[0], &t, &rel) != 0)
		{
			status = bad(&c, "%s is not a time (ms, s or f, at most 6 decimals)", tok[0]);
			break;
		}
		if (rel)
			t += prev;
		if (t < prev)
		{
			status = bad(&c, "the time %s is before the previous line's", tok[0]);
			break;
		}
		if (nt < 2)
		{
			status = bad(&c, "a time without an event");
			break;
		}
		if (s->n == s->cap)
		{
			const unsigned cap = s->cap ? s->cap * 2u : 64u;
			render_event_t *grown = realloc(s->ev, cap * sizeof *grown);
			if (!grown)
			{
				status = bad(&c, "out of memory");
				break;
			}
			s->ev = grown;
			s->cap = cap;
		}
		render_event_t *e = &s->ev[s->n];
		memset(e, 0, sizeof *e);
		e->t = t;
		e->line = c.line;
		status = parseEvent(&c, e, tok[1], tok + 2, nt - 2);
		s->n++;
		prev = t;
	}
	if (status == 0 && ferror(f))
	{
		snprintf(err, n, "%s: %s", path, strerror(errno));
		status = -1;
	}
	fclose(f);
	return status;
}

int render_scriptHasClock(const render_script_t *s, int sync)
{
	for (unsigned i = 0; i < s->n; i++)
	{
		const render_event_t *e = &s->ev[i];
		if (sync == 1 && (e->kind == RS_JACK_CLOCK || e->kind == RS_JACK_PULSE))
			return 1;
		if (sync == 2)
		{
			if (e->kind == RS_DIN_CLOCK || e->kind == RS_USB_CLOCK)
				return 1;
			if (e->kind == RS_DIN)
				for (unsigned j = 0; j < e->nbytes; j++)
					if (e->bytes[j] == 0xF8)
						return 1;
			if (e->kind == RS_USB)
				for (unsigned j = 0; j < e->nbytes; j += 4)
					if (e->bytes[j + 1] == 0xF8)
						return 1;
		}
	}
	return 0;
}
