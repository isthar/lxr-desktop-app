/*
 * app_timemap.c
 *
 * The time map, L and the entry rule of app_timemap.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_timemap.h"

#include <math.h>
#include <string.h>

#include "emu_core.h"

void app_tmInit(app_timemap_t *m, double ticksPerSec)
{
	memset(m, 0, sizeof *m);
	m->ticksPerSec = ticksPerSec;
	m->nominal = (double)EMU_CPU_HZ / ticksPerSec;
	m->slope = m->nominal;
}

void app_tmWall(app_timemap_t *m, double c, uint64_t h)
{
	m->valid = 1;
	m->audio = 0;
	m->slope = m->nominal;
	m->rate = 0.0;
	m->c0 = c;
	m->h0 = h;
	m->nhist = 0;
	m->jumpHost = h;
}

double app_tmVirtual(const app_timemap_t *m, uint64_t h)
{
	return m->c0 + (double)(int64_t)(h - m->h0) * m->slope;
}

uint64_t app_tmHost(const app_timemap_t *m, double c)
{
	const double h = (double)m->h0 + (c - m->c0) / m->slope;
	return h <= 0.0 ? 0u : (uint64_t)llround(h);
}

double app_frameCycles(uint64_t codecStart, double frame)
{
	return (double)codecStart + frame * APP_FRAME_CYCLES;
}

int app_tmPoint(app_timemap_t *m, double c, uint64_t h)
{
	/* a jump only on a line of audio points: the first one after a wall anchor is not a jump */
	const int off = m->valid && m->audio && fabs(c - app_tmVirtual(m, h)) > APP_TM_JUMP;
	if (!m->valid || off || !m->audio)
	{
		/* a new line: the first point, a jump, or the end of the wall clock's pacing */
		m->nhist = 0;
		m->slope = m->nominal;
		m->audio = 1;
		if (off)
		{
			m->jumps++;
			m->jumpHost = h;
		}
	}
	m->valid = 1;
	m->c0 = c;	/* the line goes through the latest point */
	m->h0 = h;
	const double window = APP_TM_WINDOW * m->ticksPerSec;
	if (m->nhist == 0u || (double)(h - m->hist[m->nhist - 1u].h) >= window / 32.0)
	{
		if (m->nhist == APP_TM_HIST)
		{
			memmove(m->hist, m->hist + 1, (APP_TM_HIST - 1u) * sizeof m->hist[0]);
			m->nhist--;
		}
		m->hist[m->nhist].c = c;
		m->hist[m->nhist].h = h;
		m->nhist++;
	}
	/* hist[0] stays the newest point at least a window old, if there is one: the start of the rate's span (E32) */
	unsigned old = 0;
	while (old + 1u < m->nhist && (double)(h - m->hist[old + 1u].h) >= window)
		old++;
	if (old)
	{
		memmove(m->hist, m->hist + old, (m->nhist - old) * sizeof m->hist[0]);
		m->nhist -= old;
	}
	const double age = (double)(h - m->hist[0].h);
	m->rate = age >= window ? (c - m->hist[0].c) / age : 0.0;
	const unsigned from = m->nhist > 1u && age > window ? 1u : 0u;	/* the slope's window is at most 2 s */
	const double span = (double)(h - m->hist[from].h);
	if (span >= window / 8.0)
	{
		/* the drift of the device's clock against the host's; a wild value keeps the nominal slope */
		const double s = (c - m->hist[from].c) / span;
		m->slope = fabs(s / m->nominal - 1.0) <= 0.01 ? s : m->nominal;
	}
	return off;
}

double app_latencyL(const app_latencyIn_t *in)
{
	const double offset = in->hostLead + (double)(in->deviceLatency + in->streamLatency) / in->deviceRate;
	const double blocks = 5.0 + (in->clockOutAudio ? 1.0 : 0.0);
	return offset + (2.0 * (double)in->maxRequest + blocks * EMU_BLOCK_FRAMES) / APP_FS;
}

uint64_t app_entryTime(double mapped, uint64_t d, uint64_t now, uint64_t prev)
{
	const double t = mapped + (double)d;
	const uint64_t e = t > (double)now ? (uint64_t)t : now;
	return e < prev ? prev : e;
}
