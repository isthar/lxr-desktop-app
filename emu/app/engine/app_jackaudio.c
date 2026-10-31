/*
 * app_jackaudio.c
 *
 * The clock jacks as audio (stage 1b spec 6.8, plan R5); see app_jackaudio.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_jackaudio.h"

#include <math.h>
#include <string.h>

uint64_t app_clkFrame(uint64_t codecStart, uint64_t t)
{
	if (t < codecStart)
		return 0u;
	const unsigned __int128 d = (unsigned __int128)(t - codecStart);	/* floor(f A / B) <= d <=> f A < (d + 1) B */
	return (uint64_t)(((d + 1u) * 113u - 1u) / 552960u);
}

void app_clkEdge(app_clkOut_t *c, uint64_t frame, int level)
{
	if (c->n == APP_CLK_EDGES)
	{
		c->dropped++;
		return;
	}
	const unsigned i = (c->head + c->n) % APP_CLK_EDGES;
	c->frame[i] = frame;
	c->level[i] = (uint8_t)(level != 0);
	c->n++;
}

void app_clkRender(app_clkOut_t *c, uint64_t first, unsigned n, int16_t *level)
{
	for (unsigned i = 0; i < n; i++)
	{
		while (c->n && c->frame[c->head] <= first + i)
		{
			c->level0 = c->level[c->head];
			c->head = (c->head + 1u) % APP_CLK_EDGES;
			c->n--;
		}
		level[i] = (int16_t)c->level0;
	}
}

int app_edgeStep(app_edgeDet_t *d, float v)
{
	if (!d->high && v > d->threshold)
	{
		d->high = 1;
		return 1;
	}
	if (d->high && v < d->threshold - APP_JACK_HYST)
	{
		d->high = 0;
		return -1;
	}
	return 0;	/* NaN fails both comparisons */
}

uint64_t app_inHostTime(uint64_t hostTime, unsigned offset, uint32_t latency, double ticksPerFrame)
{
	const double h = (double)hostTime + ((double)offset - (double)latency) * ticksPerFrame;
	return h <= 0.0 ? 0u : (uint64_t)llround(h);
}

void app_jackInStart(app_jackIn_t *in, const app_jackInConfig_t *cfg)
{
	in->cfg = *cfg;
	in->clock = (app_edgeDet_t){ cfg->clockThreshold, 0 };
	in->reset = (app_edgeDet_t){ cfg->resetThreshold, 0 };
	in->warmMax = 0;
	atomic_store(&in->clockThreshold, cfg->clockThreshold);
	atomic_store(&in->resetThreshold, cfg->resetThreshold);
	atomic_store(&in->callbacks, 0u);
	atomic_store(&in->lead, 0u);
}

static void app_jackPush(app_jackIn_t *in, uint8_t kind, int32_t value, uint64_t host)
{
	app_panelEvent_t ev;
	memset(&ev, 0, sizeof ev);
	ev.kind = kind;
	ev.value = value;
	ev.host = host;
	(void)app_ringPush(&in->edges, &ev);	/* a full ring counts the drop */
}

void app_jackInRender(app_jackIn_t *in, uint64_t hostTime, uint64_t entry, double rateScalar, const float *x, unsigned frames)
{
	const app_jackInConfig_t *g = &in->cfg;
	const double perFrame = g->ticksPerSec / g->deviceRate * rateScalar;	/* host ticks per input frame (spec 6.3) */
	in->clock.threshold = atomic_load_explicit(&in->clockThreshold, memory_order_relaxed);
	in->reset.threshold = atomic_load_explicit(&in->resetThreshold, memory_order_relaxed);
	const int clock = g->clockCh < g->channels, reset = g->resetCh < g->channels;
	for (unsigned i = 0; i < frames; i++)
	{
		const float *f = x + (size_t)i * g->channels;
		const int r = reset ? app_edgeStep(&in->reset, f[g->resetCh]) : 0;
		const int c = clock ? app_edgeStep(&in->clock, f[g->clockCh]) : 0;
		if (r == 0 && c <= 0)
			continue;
		const uint64_t h = app_inHostTime(hostTime, i, g->latency, perFrame);
		if (r != 0)	/* first: a pulse that comes with the release starts the step (F6) */
			app_jackPush(in, APP_P_RESET, r > 0, h);	/* high is the reset on; the core drives PD5 low (spec 4.5) */
		if (c > 0)	/* the clock counts on the rise (spec 6.8) */
			app_jackPush(in, APP_P_CLOCK, 1, h);
	}
	const uint64_t k = atomic_load_explicit(&in->callbacks, memory_order_relaxed);
	if (k < APP_IN_WARMUP)
	{
		/* the buffer's first frame: from its time at the jack to this callback (F7) */
		const int64_t lead = (int64_t)(entry - hostTime) + (int64_t)llround((double)g->latency * perFrame);
		in->warmMax = lead > in->warmMax ? lead : in->warmMax;
		if (k + 1u == APP_IN_WARMUP)
			atomic_store_explicit(&in->lead, (uint64_t)in->warmMax, memory_order_relaxed);
	}
	atomic_store_explicit(&in->callbacks, k + 1u, memory_order_relaxed);
}

void app_jackInStop(app_jackIn_t *in, uint64_t now)
{
	if (in->reset.high)
		app_jackPush(in, APP_P_RESET, 0, now);
	in->clock.high = in->reset.high = 0;
	atomic_store(&in->lead, 0u);
}
