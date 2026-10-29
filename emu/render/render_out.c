/*
 * render_out.c
 *
 * The outputs of lichomat-render (stage 1b spec 5.5); see render_out.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "render_out.h"

#include <errno.h>
#include <string.h>

#include "render_build.h"

static int outFail(char *err, size_t n, const char *path, int e)
{
	snprintf(err, n, "%s: %s", path, strerror(e));
	return 5;
}

static void put16(uint8_t *p, unsigned v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, v & 0xFFFFu);
	put16(p + 2, v >> 16);
}

static unsigned channels(const render_out_t *o)
{
	return o->stereo == RENDER_STEREO_NONE ? 4u : 2u;
}

/* the canonical 44-byte header of 16-bit PCM */
static void wavHeader(uint8_t h[44], unsigned ch, unsigned rate, uint64_t frames)
{
	const uint32_t data = (uint32_t)(frames * ch * 2u);
	memcpy(h, "RIFF", 4);
	put32(h + 4, 36u + data);
	memcpy(h + 8, "WAVEfmt ", 8);
	put32(h + 16, 16u);
	put16(h + 20, 1u);					/* PCM */
	put16(h + 22, ch);
	put32(h + 24, rate);
	put32(h + 28, rate * ch * 2u);		/* byte rate */
	put16(h + 32, ch * 2u);				/* block align */
	put16(h + 34, 16u);
	memcpy(h + 36, "data", 4);
	put32(h + 40, data);
}

/* a file that can be written: opened for writing, then kept (the WAV) or closed
 * again (the summary, written at the end) */
static int checkWritable(const char *path, char *err, size_t n)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return outFail(err, n, path, errno);
	if (fclose(f) != 0)
		return outFail(err, n, path, errno);
	return 0;
}

int render_outOpen(render_out_t *o, const render_opts_t *opts, char *err, size_t n)
{
	memset(o, 0, sizeof *o);
	o->wavPath = opts->out;
	o->stereo = opts->stereo;
	if (opts->summary && checkWritable(opts->summary, err, n) != 0)
		return 5;
	const struct
	{
		const char *path;
		FILE **f;
		const char **keep;
	} logs[] = {
		{ opts->midiLog, &o->midi, &o->midiPath },
		{ opts->clockLog, &o->clock, &o->clockPath },
		{ opts->panelLog, &o->panel, &o->panelPath },
	};
	for (size_t i = 0; i < sizeof logs / sizeof logs[0]; i++)
	{
		if (!logs[i].path)
			continue;
		*logs[i].f = fopen(logs[i].path, "w");
		*logs[i].keep = logs[i].path;
		if (!*logs[i].f)
		{
			const int e = errno;
			for (size_t j = 0; j < i; j++)
				if (*logs[j].f)
				{
					fclose(*logs[j].f);
					*logs[j].f = NULL;
				}
			return outFail(err, n, logs[i].path, e);
		}
	}
	o->wav = fopen(opts->out, "wb");
	if (!o->wav)
	{
		const int e = errno;
		for (size_t j = 0; j < sizeof logs / sizeof logs[0]; j++)
			if (*logs[j].f)
			{
				fclose(*logs[j].f);
				*logs[j].f = NULL;
			}
		return outFail(err, n, opts->out, e);
	}
	uint8_t h[44];
	wavHeader(h, channels(o), opts->wavRate, 0);
	if (fwrite(h, 1, sizeof h, o->wav) != sizeof h)
	{
		const int e = errno;
		fclose(o->wav);
		o->wav = NULL;
		return outFail(err, n, opts->out, e);
	}
	return 0;
}

static void noteError(render_out_t *o, FILE *f, const char *path)
{
	if (!o->writeError && ferror(f))
	{
		o->writeError = errno ? errno : EIO;
		o->errorPath = path;
	}
}

void render_outAudio(render_out_t *o, const int16_t *out1, const int16_t *out2)
{
	uint8_t buf[EMU_BLOCK_FRAMES * 4u * 2u];
	uint8_t *p = buf;
	for (unsigned i = 0; i < EMU_BLOCK_FRAMES; i++)
	{
		int16_t s[4];
		unsigned k = 0;
		switch (o->stereo)
		{
		case RENDER_STEREO_OUT1:
			s[k++] = out1[2 * i];
			s[k++] = out1[2 * i + 1];
			break;
		case RENDER_STEREO_OUT2:
			s[k++] = out2[2 * i];
			s[k++] = out2[2 * i + 1];
			break;
		case RENDER_STEREO_SUM:
			s[k++] = (int16_t)(((int32_t)out1[2 * i] + out2[2 * i]) >> 1);
			s[k++] = (int16_t)(((int32_t)out1[2 * i + 1] + out2[2 * i + 1]) >> 1);
			break;
		default:
			s[k++] = out1[2 * i];
			s[k++] = out1[2 * i + 1];
			s[k++] = out2[2 * i];
			s[k++] = out2[2 * i + 1];
			break;
		}
		for (unsigned j = 0; j < k; j++)
		{
			put16(p, (uint16_t)s[j]);
			p += 2;
		}
	}
	fwrite(buf, 1, (size_t)(p - buf), o->wav);
	noteError(o, o->wav, o->wavPath);
	o->frames += EMU_BLOCK_FRAMES;
}

/* "CYCLES MS" with MS = floor(c / 216) / 1000 in integer arithmetic (plan P5) */
static void stamp(FILE *f, uint64_t t)
{
	const unsigned long long us = (unsigned long long)(t / 216u);
	fprintf(f, "%llu %llu.%03llu", (unsigned long long)t, us / 1000u, us % 1000u);
}

void render_outDin(render_out_t *o, uint64_t t, uint8_t byte)
{
	if (!o->midi)
		return;
	stamp(o->midi, t);
	fprintf(o->midi, " DIN %02X\n", byte);
	noteError(o, o->midi, o->midiPath);
}

void render_outUsb(render_out_t *o, uint64_t t, const uint8_t *data, unsigned length)
{
	if (!o->midi)
		return;
	stamp(o->midi, t);
	fputs(" USB", o->midi);
	for (unsigned i = 0; i + 4u <= length; i += 4u)
		fprintf(o->midi, " %02X%02X%02X%02X", data[i], data[i + 1], data[i + 2], data[i + 3]);
	fputc('\n', o->midi);
	noteError(o, o->midi, o->midiPath);
}

void render_outClock(render_out_t *o, uint64_t t, int level)
{
	if (!o->clock)
		return;
	stamp(o->clock, t);
	fprintf(o->clock, " %d\n", level ? 1 : 0);
	noteError(o, o->clock, o->clockPath);
}

static void lcdRow(FILE *f, const uint8_t row[16])
{
	fputc('"', f);
	for (unsigned i = 0; i < 16; i++)
	{
		const uint8_t ch = row[i];
		if (ch < 0x20 || ch > 0x7E || ch == '"' || ch == '\\')
			fprintf(f, "\\x%02X", ch);
		else
			fputc(ch, f);
	}
	fputc('"', f);
}

void render_outMark(render_out_t *o, uint64_t t, const char *name, const emu_lcdSnapshot_t *lcd, uint64_t leds)
{
	if (!o->panel)
		return;
	FILE *f = o->panel;
	stamp(f, t);
	fprintf(f, " %s ", name);
	lcdRow(f, lcd->chars[0]);
	fputc(' ', f);
	lcdRow(f, lcd->chars[1]);
	if (lcd->cursorOn || lcd->blinkOn)
		fprintf(f, " %u,%u", lcd->cursorRow, lcd->cursorCol);
	else
		fputs(" -", f);
	fprintf(f, " %011llX\n", (unsigned long long)(leds & ((1ull << EMU_NUM_LEDS) - 1u)));
	noteError(o, f, o->panelPath);
}

static int closeOne(render_out_t *o, FILE **f, const char *path)
{
	if (!*f)
		return 0;
	noteError(o, *f, path);
	if (fclose(*f) != 0 && !o->writeError)
	{
		o->writeError = errno;
		o->errorPath = path;
	}
	*f = NULL;
	return o->writeError;
}

int render_outClose(render_out_t *o, unsigned wavRate, char *err, size_t n)
{
	if (o->wav)
	{
		uint8_t h[44];
		wavHeader(h, channels(o), wavRate, o->frames);
		if (fseek(o->wav, 0, SEEK_SET) != 0 || fwrite(h, 1, sizeof h, o->wav) != sizeof h)
			noteError(o, o->wav, o->wavPath);
	}
	closeOne(o, &o->wav, o->wavPath);
	closeOne(o, &o->midi, o->midiPath);
	closeOne(o, &o->clock, o->clockPath);
	closeOne(o, &o->panel, o->panelPath);
	if (o->writeError)
		return outFail(err, n, o->errorPath, o->writeError);
	return 0;
}

/* --- the summary ------------------------------------------------------------------ */

static void jsonString(FILE *f, const char *s)
{
	if (!s)
	{
		fputs("null", f);
		return;
	}
	fputc('"', f);
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
	{
		if (*p == '"' || *p == '\\')
			fprintf(f, "\\%c", *p);
		else if (*p < 0x20 || *p == 0x7F)
			fprintf(f, "\\u%04x", *p);
		else
			fputc(*p, f);
	}
	fputc('"', f);
}

static void jsonU(FILE *f, const char *key, uint64_t v, int comma)
{
	fprintf(f, "\"%s\": %llu%s", key, (unsigned long long)v, comma ? ", " : "");
}

static void jsonI(FILE *f, const char *key, long long v, int comma)
{
	fprintf(f, "\"%s\": %lld%s", key, v, comma ? ", " : "");
}

int render_summaryWrite(const char *path, const render_summary_t *s, char *err, size_t n)
{
	FILE *f = fopen(path, "w");
	if (!f)
		return outFail(err, n, path, errno);
	const render_opts_t *o = s->o;
	fputs("{\"version\": 1, \"commit\": ", f);
	jsonString(f, EMU_RENDER_COMMIT);
	fputs(", \"toolchain\": {\"clang\": ", f);
	jsonString(f, EMU_RENDER_CLANG);
	fputs(", \"sdk\": ", f);
	jsonString(f, EMU_RENDER_SDK);
	fputs("},\n \"inputs\": {", f);
	jsonI(f, "project", s->project, 1);
	jsonI(f, "pattern", o->pattern, 1);
	jsonI(f, "song", o->song, 1);
	fputs("\"bpm\": ", f);
	if (o->bpm == 0)
		fputs("\"ext\"", f);
	else if (o->bpm == 1)
		fputs("\"mid\"", f);
	else
		fprintf(f, "%d", o->bpm);
	fputs(", \"params\": [", f);
	for (unsigned i = 0; i < o->nparams; i++)
	{
		if (i)
			fputs(", ", f);
		jsonString(f, o->params[i]);
	}
	fputs("], ", f);
	jsonU(f, "seed", o->seed, 1);
	fputs("\"faders\": [", f);
	for (unsigned i = 0; i < 6; i++)
		fprintf(f, "%s%.17g", i ? ", " : "", o->faders[i]);
	fputs("], \"jacks\": ", f);
	jsonString(f, s->jacks);
	fprintf(f, ", \"asBooted\": %s, \"noStart\": %s, ", o->asBooted ? "true" : "false", o->noStart ? "true" : "false");
	jsonU(f, "blocks", s->blocks, 1);
	fputs("\"events\": ", f);
	jsonString(f, o->events);
	fputs("},\n ", f);
	jsonU(f, "bootEnd", s->bootEnd, 1);
	jsonU(f, "codecStart", s->codecStart, 1);
	jsonU(f, "preludeEnd", s->preludeEnd, 1);
	jsonU(f, "morphBusyLast", s->morphBusyLast, 1);
	jsonU(f, "t0", s->t0, 1);
	jsonU(f, "end", s->end, 1);
	jsonU(f, "underrunsBeforeT0", s->underrunsBefore, 1);
	jsonU(f, "underrunsAfterT0", s->underrunsAfter, 1);
	fputs("\n \"selected\": {", f);
	jsonI(f, "pattern", s->selected.pattern, 1);
	jsonI(f, "song", s->selected.song, 1);
	jsonI(f, "bpm", s->selected.bpm, 1);
	jsonI(f, "sync", s->selected.sync, 0);
	fputs("}, \"params\": {}, \"paramNumbers\": {},\n \"direct\": [", f);
	for (unsigned i = 0; i < s->ndirect; i++)
	{
		const render_direct_t *d = &s->direct[i];
		fputs(i ? ", {" : "{", f);
		jsonU(f, "line", d->line, 1);
		fputs("\"event\": ", f);
		jsonString(f, d->event);
		fputs(", ", f);
		jsonU(f, "due", d->due, 1);
		jsonU(f, "at", d->at, 0);
		fputc('}', f);
	}
	fputs("],\n ", f);
	jsonU(f, "syncOverflows", s->fw.syncOverflows, 1);
	jsonU(f, "usbTxDropped", s->fw.usbTxDropped, 1);
	const render_fwCounters_t *q = &s->fw;
	fputs("\n \"seqc\": {", f);
	jsonU(f, "queueDepth", q->queueDepth, 1);
	jsonU(f, "queueMax", q->queueMax, 1);
	jsonU(f, "queueOverflow", q->queueOverflow, 1);
	jsonU(f, "chokeDrops", q->chokeDrops, 1);
	jsonU(f, "muteDrops", q->muteDrops, 1);
	jsonU(f, "skipDrops", q->skipDrops, 1);
	jsonU(f, "syncIgnored", q->syncIgnored, 1);
	jsonU(f, "intervalsExcluded", q->intervalsExcluded, 1);
	jsonU(f, "dropouts", q->dropouts, 1);
	jsonU(f, "reentries", q->reentries, 1);
	jsonU(f, "tempoRestarts", q->tempoRestarts, 0);
	const emu_counters_t *c = &s->counters;
	fputs("},\n \"counters\": {", f);
	jsonU(f, "bootEnd", c->bootEnd, 1);
	jsonU(f, "codecStart", c->codecStart, 1);
	jsonU(f, "blocks", c->blocks, 1);
	jsonU(f, "underruns", c->underruns, 1);
	jsonU(f, "passes", c->passes, 1);
	jsonU(f, "dinIn", c->dinIn, 1);
	jsonU(f, "dinLost", c->dinLost, 1);
	jsonU(f, "dinOut", c->dinOut, 1);
	jsonU(f, "usbIn", c->usbIn, 1);
	jsonU(f, "usbInDropped", c->usbInDropped, 1);
	jsonU(f, "usbOut", c->usbOut, 1);
	jsonU(f, "clockEdges", c->clockEdges, 1);
	jsonU(f, "late", c->late, 1);
	jsonU(f, "tickCalls", c->tickCalls, 0);
	fprintf(f, "},\n \"wallSeconds\": %.6f, \"virtualSeconds\": %.6f, \"speed\": %.6f,\n \"warnings\": [",
		s->wallSeconds, s->virtualSeconds, s->speed);
	for (unsigned i = 0; i < s->nwarnings; i++)
	{
		if (i)
			fputs(", ", f);
		jsonString(f, s->warnings[i]);
	}
	fputs("]}\n", f);
	const int bad = ferror(f);
	const int e = errno;
	if (fclose(f) != 0 || bad)
		return outFail(err, n, path, bad ? (e ? e : EIO) : errno);
	return 0;
}
