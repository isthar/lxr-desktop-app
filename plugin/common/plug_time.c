/*
 * plug_time.c
 *
 * Host frames, cycles and the latency (plan E9.2 Task 5); see plug_time.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_time.h"

#include <math.h>
#include <stdlib.h>

#define PLUG_CPU_HZ 216000000ull

int plug_isNative(double rate)
{
	return rate == PLUG_ENGINE_RATE;
}

uint32_t plug_soundDelay(double rate)
{
	if (plug_isNative(rate))
		return PLUG_VOICE_DELAY;
	return (uint32_t)floor((double)(PLUG_VOICE_DELAY + PLUG_CONVERTER_DELAY) * rate / PLUG_ENGINE_RATE + 0.5);
}

uint32_t plug_readOffset(double rate, uint32_t maxBlock, uint32_t margin)
{
	const uint32_t lag = plug_isNative(rate) ? 1u : (uint32_t)ceil(rate / PLUG_ENGINE_RATE) + 1u;
	return maxBlock + margin + lag;
}

uint32_t plug_latency(double rate, uint32_t maxBlock, uint32_t margin)
{
	return plug_soundDelay(rate) + plug_readOffset(rate, maxBlock, margin);
}

static int integerRate(double rate)
{
	return rate == floor(rate) && rate >= 1.0 && rate <= 1e7;
}

uint64_t plug_framesToCycles(uint64_t frames, double rate)
{
	if (plug_isNative(rate))	/* emu_framesToCycles(): a frame is 552,960 / 113 cycles */
		return frames / 113u * 552960u + frames % 113u * 552960u / 113u;
	if (integerRate(rate))
		return (uint64_t)((unsigned __int128)frames * PLUG_CPU_HZ / (uint64_t)rate);
	return (uint64_t)floor((double)frames * ((double)PLUG_CPU_HZ / rate));
}

uint64_t plug_framesCovering(uint64_t cycles, double rate)
{
	uint64_t m;
	if (plug_isNative(rate))
		m = (uint64_t)(((unsigned __int128)cycles * 113u + 552959u) / 552960u);
	else if (integerRate(rate))
		m = (uint64_t)(((unsigned __int128)cycles * (uint64_t)rate + PLUG_CPU_HZ - 1u) / PLUG_CPU_HZ);
	else
		m = (uint64_t)ceil((double)cycles * rate / (double)PLUG_CPU_HZ);
	while (plug_framesToCycles(m, rate) < cycles)
		m++;
	while (m > 0 && plug_framesToCycles(m - 1u, rate) >= cycles)
		m--;
	return m;
}

uint32_t plug_marginFromEnv(void)
{
	const char *v = getenv("LICHOMAT_PLUGIN_MARGIN");
	if (!v || !*v)
		return PLUG_MARGIN_DEFAULT;
	char *end;
	const long m = strtol(v, &end, 10);
	return (*end == '\0' && m >= 16 && m <= 4096) ? (uint32_t)m : PLUG_MARGIN_DEFAULT;
}
