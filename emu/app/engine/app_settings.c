/*
 * app_settings.c
 *
 * settings.txt: read with a range for every key, written to a temporary file and renamed. Keys that are not
 * known are kept as read (plan E19), so that R5's options can be added without losing them.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_settings.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum { T_STR, T_U32, T_I32, T_INT, T_DBL } app_type_t;
typedef struct { const char *key; app_type_t type; size_t off, size; double lo, hi; } app_field_t;
#define F(k, t, m, lo, hi) { k, t, offsetof(app_settings_t, m), sizeof(((app_settings_t *)0)->m), lo, hi }
static const app_field_t app_fields[] = {
	F("output.device", T_STR, outputUid, 0, 0),
	F("output.buffer", T_U32, bufferFrames, 0, 4096),
	F("output.mix", T_STR, mix, 0, 0),
	F("din.source.uid", T_I32, dinSrcUid, INT32_MIN, INT32_MAX),
	F("din.source.name", T_STR, dinSrcName, 0, 0),
	F("din.destination.uid", T_I32, dinDstUid, INT32_MIN, INT32_MAX),
	F("din.destination.name", T_STR, dinDstName, 0, 0),
	F("usb.source.uid", T_I32, usbSrcUid, INT32_MIN, INT32_MAX),
	F("usb.destination.uid", T_I32, usbDstUid, INT32_MIN, INT32_MAX),
	F("slave.delay.auto", T_INT, dAuto, 0, 1),
	F("slave.delay.ms", T_DBL, dMs, 0, 500),
	F("window.x", T_INT, window[0], -100000, 100000),
	F("window.y", T_INT, window[1], -100000, 100000),
	F("window.width", T_INT, window[2], 200, 100000),
	F("window.height", T_INT, window[3], 200, 100000),
	F("clockin.bpm", T_DBL, genBpm, 1, 300),
	F("clockin.ppq", T_INT, genPpq, 4, 24),
	F("crash.shown", T_STR, crashShown, 0, 0),
};
#define NFIELDS (sizeof app_fields / sizeof app_fields[0])

void app_settingsDefaults(app_settings_t *s)
{
	memset(s, 0, sizeof *s);
	snprintf(s->mix, sizeof s->mix, "sum");
	s->dAuto = 1;
	s->window[0] = 100;
	s->window[1] = 100;
	s->window[2] = 1100;
	s->window[3] = 760;
	s->genBpm = 120.0;
	s->genPpq = 24;
}

static char *app_trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		*--e = '\0';
	return s;
}

/* A string is written with \\ for a backslash, \n, \r and \t for those characters and \s for a space at either end
 * (the reader trims those), so that no name breaks the file. Any other backslash is read as itself. */
static void app_putEscaped(FILE *f, const char *s)
{
	const size_t n = strlen(s);
	for (size_t i = 0; i < n; i++)
	{
		const char c = s[i];
		if (c == '\\')
			fputs("\\\\", f);
		else if (c == '\n')
			fputs("\\n", f);
		else if (c == '\r')
			fputs("\\r", f);
		else if (c == '\t')
			fputs("\\t", f);
		else if (c == ' ' && (i == 0u || i + 1u == n))
			fputs("\\s", f);
		else
			fputc(c, f);
	}
}

static void app_unescape(char *s)
{
	char *w = s;
	for (const char *r = s; *r; r++)
	{
		if (*r == '\\' && r[1])
		{
			const char c = r[1] == 'n' ? '\n' : r[1] == 'r' ? '\r' : r[1] == 't' ? '\t' : r[1] == 's' ? ' '
				: r[1] == '\\' ? '\\' : '\0';
			if (c)
			{
				*w++ = c;
				r++;
				continue;
			}
		}
		*w++ = *r;
	}
	*w = '\0';
}

static void app_setField(app_settings_t *s, const app_field_t *f, const char *v)
{
	char *p = (char *)s + f->off, *end;
	if (f->type == T_STR)
	{
		char u[1024];
		snprintf(u, sizeof u, "%s", v);
		app_unescape(u);
		if (strlen(u) < f->size)
			memcpy(p, u, strlen(u) + 1u);
		return;
	}
	errno = 0;
	if (f->type == T_DBL)
	{
		const double x = strtod(v, &end);
		if (!errno && end != v && !*end && x >= f->lo && x <= f->hi)
			memcpy(p, &x, sizeof x);
		return;	/* a bad value keeps the default */
	}
	const int64_t x = strtoll(v, &end, 10);	/* integers never go through a double */
	if (errno || end == v || *end || x < (int64_t)f->lo || x > (int64_t)f->hi)
		return;
	if (f->type == T_U32)	/* each conversion only in its own branch, on a value in range */
	{
		const uint32_t u = (uint32_t)x;
		memcpy(p, &u, sizeof u);
	}
	else if (f->type == T_I32)
	{
		const int32_t i = (int32_t)x;
		memcpy(p, &i, sizeof i);
	}
	else
	{
		const int n = (int)x;
		memcpy(p, &n, sizeof n);
	}
}

int app_settingsLoad(app_settings_t *s, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return errno == ENOENT ? 1 : -1;
	size_t used = strlen(s->extra);
	char line[1024];
	while (fgets(line, sizeof line, f))
	{
		const size_t len = strlen(line);
		if (len + 1u == sizeof line && line[len - 1u] != '\n')
		{
			/* a line of 1023 characters or more: the rest of it, up to its line feed, is not a line of its own, and a
			 * value that was cut would be a wrong one: the line is dropped whole */
			int c = fgetc(f);
			if (c != EOF && c != '\n')
			{
				while ((c = fgetc(f)) != EOF && c != '\n')
					;
				continue;
			}
		}
		char *t = app_trim(line), *eq = strchr(t, '=');
		if (!*t || *t == '#' || !eq)
			continue;
		*eq = '\0';
		const char *key = app_trim(t), *val = app_trim(eq + 1);
		size_t k = 0;
		while (k < NFIELDS && strcmp(app_fields[k].key, key))
			k++;
		if (k < NFIELDS)
		{
			app_setField(s, &app_fields[k], val);
			continue;
		}
		const int w = snprintf(s->extra + used, sizeof s->extra - used, "%s = %s\n", key, val);
		if (w > 0 && (size_t)w < sizeof s->extra - used)
			used += (size_t)w;
		else
			s->extra[used] = '\0';	/* no room: the line is dropped whole */
	}
	fclose(f);
	if (strcmp(s->mix, "sum") && strcmp(s->mix, "out1") && strcmp(s->mix, "out2") && strcmp(s->mix, "four"))
		snprintf(s->mix, sizeof s->mix, "sum");
	if (s->genPpq != 4 && s->genPpq != 24)
		s->genPpq = 24;
	return 0;
}

int app_settingsSave(const app_settings_t *s, const char *path)
{
	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	if (!f)
		return -1;
	fputs("# LICHOMAT Emu settings (stage 1b spec 6.9), written by the app\n", f);
	for (size_t k = 0; k < NFIELDS; k++)
	{
		const char *p = (const char *)s + app_fields[k].off;
		uint32_t u;
		int32_t i;
		int n;
		double x;
		fprintf(f, "%s = ", app_fields[k].key);
		switch (app_fields[k].type)
		{
		case T_STR: app_putEscaped(f, p); break;
		case T_U32: memcpy(&u, p, sizeof u); fprintf(f, "%u", u); break;
		case T_I32: memcpy(&i, p, sizeof i); fprintf(f, "%d", i); break;
		case T_INT: memcpy(&n, p, sizeof n); fprintf(f, "%d", n); break;
		case T_DBL: memcpy(&x, p, sizeof x); fprintf(f, "%.17g", x); break;
		}
		fputc('\n', f);
	}
	fputs(s->extra, f);
	const int bad = ferror(f) || fflush(f) != 0 || fsync(fileno(f)) != 0;
	if (fclose(f) != 0 || bad)
	{
		unlink(tmp);
		return -1;
	}
	return rename(tmp, path) == 0 ? 0 : -1;
}
