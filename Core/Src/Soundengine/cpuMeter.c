/*
 * cpuMeter.c
 *
 * CPU meter of the LICHOMAT audio engine, see cpuMeter.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "Soundengine/cpuMeter.h"

#define CPUMETER_MAX_DEPTH 2u	/* a kit autoload nests one level */

static uint32_t cpuMeter_start[CPUMETER_MAX_DEPTH];	/* cpuMeter_begin() time per level */
static uint8_t cpuMeter_depth;				/* pieces in progress */
static uint8_t cpuMeter_nested;				/* the outermost piece in progress had a nested one */
static uint64_t cpuMeter_blockCycles;			/* work since the last rendered block */

static uint8_t cpuMeter_windowOpen;			/* cpuMeter_windowStart is set */
static uint32_t cpuMeter_windowStart;			/* end of the first block of this second */
static uint64_t cpuMeter_windowCycles;			/* work of the blocks of this second */
static uint32_t cpuMeter_windowBlocks;
static uint64_t cpuMeter_windowPeak;			/* highest block load of this second, % */

static volatile cpuMeter_reading_t cpuMeter_reading;	/* the latest reading */

//------------------------------------------------------------------------------
static uint16_t cpuMeter_clamp(uint64_t pct)
{
	return (uint16_t)(pct > CPUMETER_MAX_PCT ? CPUMETER_MAX_PCT : pct);
}
//------------------------------------------------------------------------------
void cpuMeter_begin(uint32_t now)
{
	if(cpuMeter_depth < CPUMETER_MAX_DEPTH)
		cpuMeter_start[cpuMeter_depth] = now;
	if(cpuMeter_depth > 0)
		cpuMeter_nested = 1;
	if(cpuMeter_depth < UINT8_MAX)
		cpuMeter_depth++;
}
//------------------------------------------------------------------------------
uint8_t cpuMeter_end(uint32_t now, uint8_t blockRendered)
{
	if(cpuMeter_depth == 0)
		return 0;		/* no matching cpuMeter_begin() */
	cpuMeter_depth--;
	if(cpuMeter_depth >= CPUMETER_MAX_DEPTH)
		return 0;		/* nested deeper than a kit autoload: not measured */
	if(cpuMeter_depth == 0 && cpuMeter_nested)
	{
		cpuMeter_nested = 0;	/* around a kit autoload: its nested pieces were counted */
		return 0;
	}

	cpuMeter_blockCycles += (uint32_t)(now - cpuMeter_start[cpuMeter_depth]);
	if(!blockRendered)
		return 0;

	const uint64_t pct = cpuMeter_blockCycles * 100u / CPUMETER_BLOCK_CYCLES;
	if(!cpuMeter_windowOpen)
	{
		cpuMeter_windowOpen = 1;
		cpuMeter_windowStart = now;
	}
	cpuMeter_windowCycles += cpuMeter_blockCycles;
	cpuMeter_windowBlocks++;
	if(pct > cpuMeter_windowPeak)
		cpuMeter_windowPeak = pct;
	cpuMeter_blockCycles = 0;

	if((uint32_t)(now - cpuMeter_windowStart) < CPUMETER_WINDOW_CYCLES)
		return 0;

	cpuMeter_reading.avgPct = cpuMeter_clamp(cpuMeter_windowCycles * 100u
			/ ((uint64_t)cpuMeter_windowBlocks * CPUMETER_BLOCK_CYCLES));
	cpuMeter_reading.peakPct = cpuMeter_clamp(cpuMeter_windowPeak);
	cpuMeter_windowStart = now;
	cpuMeter_windowCycles = 0;
	cpuMeter_windowBlocks = 0;
	cpuMeter_windowPeak = 0;
	return 1;
}
//------------------------------------------------------------------------------
void cpuMeter_setQueueStats(uint16_t queueMax, uint32_t queueOverflow)
{
	cpuMeter_reading.queueMax = queueMax;
	cpuMeter_reading.queueOverflow = queueOverflow;
}
//------------------------------------------------------------------------------
void cpuMeter_get(cpuMeter_reading_t *out)
{
	out->avgPct = cpuMeter_reading.avgPct;
	out->peakPct = cpuMeter_reading.peakPct;
	out->queueMax = cpuMeter_reading.queueMax;
	out->queueOverflow = cpuMeter_reading.queueOverflow;
}
//------------------------------------------------------------------------------
void cpuMeter_reset(void)
{
	for(uint8_t i = 0; i < CPUMETER_MAX_DEPTH; i++)
		cpuMeter_start[i] = 0;
	cpuMeter_depth = 0;
	cpuMeter_nested = 0;
	cpuMeter_blockCycles = 0;
	cpuMeter_windowOpen = 0;
	cpuMeter_windowStart = 0;
	cpuMeter_windowCycles = 0;
	cpuMeter_windowBlocks = 0;
	cpuMeter_windowPeak = 0;
	cpuMeter_reading.avgPct = 0;
	cpuMeter_reading.peakPct = 0;
	cpuMeter_reading.queueMax = 0;
	cpuMeter_reading.queueOverflow = 0;
}
