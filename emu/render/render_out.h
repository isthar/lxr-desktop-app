/*
 * render_out.h
 *
 * The outputs of lichomat-render (stage 1b spec 5.5): the WAV file and the
 * summary. Every output is opened before power-on, so that a path that cannot
 * be written stops the render with exit code 5 before it starts (spec 5.7).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef RENDER_OUT_H_
#define RENDER_OUT_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "emu_core.h"
#include "render_fw.h"
#include "render_opts.h"

#define RENDER_MAX_WARNINGS 32
#define RENDER_MAX_DIRECT 256

typedef struct render_out
{
	FILE *wav, *midi, *clock, *panel;
	const char *wavPath, *midiPath, *clockPath, *panelPath;
	int stereo;					/**< RENDER_STEREO_* */
	uint64_t frames;			/**< frames written */
	int writeError;				/**< 0, or the errno of the first failed write */
	const char *errorPath;		/**< the file of that write */
} render_out_t;

/** a direct event of the script, as the summary lists it */
typedef struct render_direct
{
	unsigned line;
	const char *event;
	uint64_t due, at;
} render_direct_t;

/** what the summary prints (spec 5.5), cycles absolute from power-on */
typedef struct render_summary
{
	const render_opts_t *o;
	int project;				/**< the project the boot loads */
	const char *jacks;			/**< the --jacks value: all, none or a list */
	uint64_t blocks, bootEnd, codecStart, preludeEnd, morphBusyLast, t0, end;
	uint64_t underrunsBefore, underrunsAfter;
	render_selected_t selected;
	render_fwCounters_t fw;
	emu_counters_t counters;
	const render_direct_t *direct;
	unsigned ndirect;
	double wallSeconds, virtualSeconds, speed;
	const char *const *warnings;
	unsigned nwarnings;
} render_summary_t;

/** Open every output that the options name and write the WAV header with zero
 * sizes. Returns 0, or 5 with "FILE: strerror" in err and nothing left open. */
int render_outOpen(render_out_t *o, const render_opts_t *opts, char *err, size_t n);

/** one block of 64 frames, out 1 and out 2 interleaved stereo, in the layout of
 * --stereo */
void render_outAudio(render_out_t *o, const int16_t *out1, const int16_t *out2);

/** The logs (spec 5.5, plan P5); t is cycles from t0. Each does nothing when its
 * log is not given. */
void render_outDin(render_out_t *o, uint64_t t, uint8_t byte);
void render_outUsb(render_out_t *o, uint64_t t, const uint8_t *data, unsigned length);
void render_outClock(render_out_t *o, uint64_t t, int level);
/** a panel-log line: the LCD and emu_ledSnapshot()'s mask */
void render_outMark(render_out_t *o, uint64_t t, const char *name, const emu_lcdSnapshot_t *lcd, uint64_t leds);

/** Write the WAV header's sizes and close every output. Returns 0, or 5 with
 * "FILE: strerror" in err after a failed write or close. */
int render_outClose(render_out_t *o, unsigned wavRate, char *err, size_t n);

/** Write the summary as one JSON object. Returns 0, or 5 with "FILE:
 * strerror" in err. */
int render_summaryWrite(const char *path, const render_summary_t *s, char *err, size_t n);

#endif /* RENDER_OUT_H_ */
