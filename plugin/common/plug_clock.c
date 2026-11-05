/*
 * plug_clock.c
 *
 * The host's transport as MIDI clock (plan E9.2 Task 4); see plug_clock.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_clock.h"

#include <math.h>

#define PLUG_JUMP (1.0 / 48.0)	/**< half a clock, in quarter notes: a larger difference is a jump */
#define PLUG_AT_ZERO 1e-6		/**< a start closer than this to position 0 is a start at 0 */

typedef struct
{
	plug_midi_t *out;
	unsigned n, max;
} sink_t;

static void put(sink_t *s, uint32_t offset, uint8_t a, uint8_t b, uint8_t c, uint8_t len)
{
	if (s->n < s->max)
		s->out[s->n++] = (plug_midi_t){ offset, len, { a, b, c } };
}

void plug_clockInit(plug_clock_t *c, double rate)
{
	c->rate = rate;
	plug_clockReset(c);
}

void plug_clockReset(plug_clock_t *c)
{
	c->running = 0;
	c->startPending = 0;
	c->next = 0;
	c->expect = 0.0;
}

/* the 16th that a start or a jump to p resumes at: the first at or after p, also past 16,383 */
static int64_t sixteenth(double p)
{
	if (p <= 0.0)
		return 0;
	return (int64_t)ceil(p * 4.0 - PLUG_AT_ZERO);
}

/* Song Position s and Continue at offset; the clocks resume at 16th s. Past 16,383 only the
 * message's 14 bits wrap: the clocks count from the real 16th */
static void resume(plug_clock_t *c, sink_t *s, uint32_t offset, double p)
{
	const int64_t six = sixteenth(p);
	const unsigned spp = (unsigned)(six & PLUG_SPP_MAX);
	put(s, offset, 0xF2, (uint8_t)(spp & 0x7Fu), (uint8_t)(spp >> 7), 3);
	put(s, offset, 0xFB, 0, 0, 1);
	c->next = six * (PLUG_CLOCK_PPQ / 4);
	c->running = 1;
	c->startPending = 0;
}

/* the slave starts at p */
static void start(plug_clock_t *c, sink_t *s, uint32_t offset, double p)
{
	if (p < PLUG_AT_ZERO)
	{
		c->next = 0;	/* Start with the clock of position 0, also after a count-in */
		c->startPending = 1;
		c->running = 0;
	}
	else
		resume(c, s, offset, p);
}

/* the clocks whose frames fall in [f0, f1), position q0 at frame fo */
static void clocks(plug_clock_t *c, sink_t *s, double fpq, uint32_t fo, double q0, uint32_t f0, uint32_t f1)
{
	for (;;)
	{
		const double x = (double)fo + ((double)c->next / PLUG_CLOCK_PPQ - q0) * fpq;
		double off = floor(x + 0.5);
		if (off < (double)f0)
			off = (double)f0;	/* due before the block: now */
		if (off >= (double)f1)
			return;
		if (c->startPending && c->next == 0)
		{
			put(s, (uint32_t)off, 0xFA, 0, 0, 1);
			c->startPending = 0;
			c->running = 1;
		}
		put(s, (uint32_t)off, 0xF8, 0, 0, 1);
		c->next++;
	}
}

unsigned plug_clockBlock(plug_clock_t *c, const plug_transport_t *t, uint32_t frames, plug_midi_t *out, unsigned max)
{
	sink_t s = { out, 0, max };
	if (!t->valid || !t->playing || !(t->tempo > 0.0))
	{
		if (c->running)
			put(&s, 0, 0xFC, 0, 0, 1);
		c->running = 0;
		c->startPending = 0;
		return s.n;
	}
	const double fpq = c->rate * 60.0 / t->tempo;	/* frames per quarter note */
	const double p0 = t->ppq;
	if (!c->running && !c->startPending)
		start(c, &s, 0, p0);
	else if (fabs(p0 - c->expect) > PLUG_JUMP)
	{
		if (c->running)
		{
			put(&s, 0, 0xFC, 0, 0, 1);
			resume(c, &s, 0, p0);
		}
		else
			start(c, &s, 0, p0);	/* a jump during a count-in */
	}
	const double end = p0 + (double)frames / fpq;
	const double w = t->cycle && t->cycleEnd > t->cycleStart && p0 < t->cycleEnd ? floor((t->cycleEnd - p0) * fpq + 0.5) : -1.0;
	if (w >= 0.0 && w < (double)frames)
	{
		/* the loop's end inside the block: a jump at its frame. One at or after the block's end is
		 * the next block's jump, as when the host splits its blocks there */
		const uint32_t wf = (uint32_t)w;
		clocks(c, &s, fpq, 0, p0, 0, wf);
		if (c->running)
		{
			put(&s, wf, 0xFC, 0, 0, 1);
			resume(c, &s, wf, t->cycleStart);
		}
		else
			start(c, &s, wf, t->cycleStart);
		clocks(c, &s, fpq, wf, t->cycleStart, wf, frames);
		c->expect = t->cycleStart + (double)(frames - wf) / fpq;
	}
	else
	{
		clocks(c, &s, fpq, 0, p0, 0, frames);
		c->expect = end;
	}
	return s.n;
}
