/*
 * app_jackaudio.h
 *
 * The clock jacks as audio (stage 1b spec 6.8, plan R5): the clock-out jack's
 * edges to frames and levels, the input's edge detector with hysteresis, an
 * input frame's host time, and the input callback's body, which pushes
 * panel-style events into a ring. Plain C; the input callback's part never
 * blocks.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_JACKAUDIO_H_
#define APP_JACKAUDIO_H_

#include <stdatomic.h>
#include <stdint.h>

#include "app_ring.h"
#include "app_types.h"

#define APP_JACK_THRESHOLD 0.5f	/**< the default: half of full scale (spec 6.8) */
#define APP_JACK_HYST 0.05f		/**< a high input falls below the threshold minus this (F5) */
#define APP_CLK_EDGES 32u		/**< clock-out edges waiting for their block */
#define APP_IN_WARMUP 32u		/**< input callbacks before the input's lead is fixed (F7) */

typedef struct app_clkOut
{
	uint64_t frame[APP_CLK_EDGES];
	uint8_t level[APP_CLK_EDGES];
	unsigned head, n;	/**< the oldest waiting edge; how many wait */
	int level0;			/**< the level before the oldest waiting edge */
	uint64_t dropped;	/**< edges that found the queue full */
} app_clkOut_t;

/** the largest frame f with codecStart + floor(f x 552,960 / 113) <= t; 0 before codecStart (F1) */
uint64_t app_clkFrame(uint64_t codecStart, uint64_t t);
/** an edge at a frame; successive frames never go back */
void app_clkEdge(app_clkOut_t *c, uint64_t frame, int level);
/** the level (0 or 1) at frames first .. first + n - 1; uses up the edges before first + n. An edge before first sets the
 * level from the first frame; of several edges at one frame the last counts */
void app_clkRender(app_clkOut_t *c, uint64_t first, unsigned n, int16_t *level);

typedef struct app_edgeDet { float threshold; int high; } app_edgeDet_t;
/** 1 if v rose above the threshold, -1 if it fell below threshold - APP_JACK_HYST, else 0; NaN changes nothing (F5) */
int app_edgeStep(app_edgeDet_t *d, float v);
/** mHostTime + (offset - latency) frames, in host ticks; 0 if before 0 (spec 6.8, F6) */
uint64_t app_inHostTime(uint64_t hostTime, unsigned offset, uint32_t latency, double ticksPerFrame);

typedef struct app_jackInConfig
{
	double ticksPerSec, deviceRate;
	uint32_t latency;			/**< input device + stream latency, device frames */
	unsigned channels;			/**< interleaved channels of the frames */
	unsigned clockCh, resetCh;	/**< 0-based; >= channels: none */
	float clockThreshold, resetThreshold;
} app_jackInConfig_t;

typedef struct app_jackIn
{
	app_ring_t edges;	/**< app_panelEvent_t: input callback -> firmware thread (F8) */
	_Atomic float clockThreshold, resetThreshold;	/**< the main thread may move them while the input runs */
	_Atomic uint64_t callbacks, lead;	/**< lead: host ticks (F7), fixed after APP_IN_WARMUP callbacks; 0 while none */
	app_jackInConfig_t cfg;	/**< from here on the callback's, set by app_jackInStart() while none runs */
	app_edgeDet_t clock, reset;
	int64_t warmMax;
} app_jackIn_t;

void app_jackInStart(app_jackIn_t *in, const app_jackInConfig_t *cfg);	/**< before the unit starts: all low, warm-up anew */
/** Input callback: the edges of x[0 .. frames x channels) in frame order, as APP_P_CLOCK (a rise) and APP_P_RESET (value 1:
 * the jack high, the reset on) at their host times; the warm-up's lead */
void app_jackInRender(app_jackIn_t *in, uint64_t hostTime, uint64_t entry, double rateScalar, const float *x, unsigned frames);
/** after the unit has stopped for good, on that thread: a high reset is let go at host time now (F11); no lead */
void app_jackInStop(app_jackIn_t *in, uint64_t now);

#endif /* APP_JACKAUDIO_H_ */
