/*
 * queue_equiv.c
 *
 * Random runs of the core for the queue equivalence tests, see queue_equiv.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "queue_equiv.h"
#include "fake_host.h"

static uint64_t lcgState;
static uint32_t lcg(void)
{
	lcgState = lcgState * 6364136223846793005ull + 1442695040888963407ull;	/* MMIX */
	return (uint32_t)(lcgState >> 33);
}

static uint64_t hash;
static void hashValue(uint64_t v, int bytes)
{
	for (int i = 0; i < bytes; i++)
	{
		hash ^= (v >> (8 * i)) & 0xffu;
		hash *= 1099511628211ull;			/* FNV-1a */
	}
}

/* a recorded note on a random track: skip-once when the phase is 0.5 or more */
static void recordNote(fake_host_t *h)
{
	const uint8_t track = (uint8_t)(lcg() % SEQC_NUM_TRACKS);
	const uint8_t note = (uint8_t)(lcg() % 128u);
	fake_addNote(h, track, 100, note);
}

static void onTrigger(fake_host_t *h, const seqc_event_t *ev)
{
	hashValue((uint64_t)ev->tick, 8);
	hashValue(ev->absStep, 4);
	hashValue(ev->track, 1);
	hashValue(ev->pattern, 1);
	hashValue(ev->step, 1);
	hashValue(ev->hit, 1);
	hashValue(ev->vel, 1);
	hashValue(ev->note, 1);
	hashValue(ev->flags, 1);
	hashValue(ev->p1Nr, 1);
	hashValue(ev->p1Val, 1);
	hashValue(ev->p2Nr, 1);
	hashValue(ev->p2Val, 1);
	if (lcg() % 64u == 0)
		recordNote(h);					/* skip-once inside a fire */
}

static void onStepBoundary(fake_host_t *h, uint32_t absStep)
{
	(void)absStep;
	if (lcg() % 8u == 0)
		recordNote(h);					/* before the boundary's events */
}

static void randomPattern(fake_host_t *h, uint8_t p)
{
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		const uint32_t len = lcg() % 4u;
		h->length[p][t] = len == 0 ? 0 : len == 1 ? 16 : (uint8_t)(1u + lcg() % 64u);
		const uint32_t density = 20u + lcg() % 81u;	/* percent of the steps on */
		for (uint8_t s = 0; s < SEQC_NUM_STEPS; s++)
		{
			fake_setStep(h, p, t, s, lcg() % 100u < density);
			const uint8_t vel = (uint8_t)(lcg() % 128u);
			const uint8_t note = (uint8_t)(lcg() % 128u);
			const uint8_t prob = lcg() % 2u ? 100 : (uint8_t)(lcg() % 101u);
			fake_setStepData(h, p, t, s, vel, note, prob);
			const uint8_t p1 = (uint8_t)lcg(), p2 = (uint8_t)lcg();
			fake_setPlocks(h, p, t, s, p1, (uint8_t)(p1 ^ 0x5a), p2, (uint8_t)(p2 ^ 0xa5));
			/* flam 0..5, sometimes 6 or 7 (one hit); shift -7..+7, sometimes 15 (0) */
			const uint8_t flam = (uint8_t)(lcg() % 8u);
			h->timing[p][t][s].flam = flam;
			h->timing[p][t][s].shift = (uint8_t)(lcg() % 16u);
		}
	}
}

uint64_t queue_equivRun(uint64_t seed, seqc_stats_t *stats)
{
	static const double bpms[] = { 90.0, 120.0, 174.0, 300.0 };
	static const double intervalsMs[] = { 0.1, 0.7, 3.0, 25.0 };
	static const uint8_t rolls[] = { 0, 0, 0, 1, 2, 4, 8, 16 };

	fake_host_t *h = fake_init();
	h->logMask = 0;
	h->recordActive = true;
	h->onTrigger = onTrigger;
	h->onStepBoundary = onStepBoundary;
	lcgState = seed;
	hash = 14695981039346656037ull;
	fake_seed(h, seed);
	randomPattern(h, 0);
	randomPattern(h, 1);
	const uint32_t shuffle = lcg() % 3u;
	h->shuffle = shuffle == 0 ? 0 : shuffle == 1 ? 127 : (uint8_t)(lcg() % 128u);
	for (uint8_t t = 0; t < SEQC_NUM_TRACKS; t++)
	{
		h->rollRate[t] = rolls[lcg() % 8u];
		h->muted[t] = lcg() % 10u == 0;
	}
	const double bpm = bpms[lcg() % 4u];
	const seqc_time_t interval = fake_msToCycles(intervalsMs[lcg() % 4u]);

	fake_process(h, 0);
	seqc_setTempo((float)bpm);
	seqc_start();
	const seqc_time_t step = (seqc_time_t)fake_stepCycles(bpm);
	const seqc_time_t until = 48 * step;
	seqc_time_t nextAction = lcg() % (2 * step);
	for (seqc_time_t t = 0; t < until; t += interval)
	{
		fake_process(h, t);
		if (t < nextAction)
			continue;
		/* on average one action per step, whatever the interval */
		nextAction = t + lcg() % (2 * step);
		if (lcg() % 8u == 0)
			fake_setNextPattern(h, (uint8_t)(lcg() % 2u));
		else
			recordNote(h);
	}
	seqc_stats(stats);
	hashValue(h->calls[FAKE_TRIGGER], 4);
	hashValue(stats->queueOverflow, 4);
	hashValue(stats->chokeDrops, 4);
	hashValue(stats->muteDrops, 4);
	hashValue(stats->skipDrops, 4);
	hashValue(stats->queueMax, 2);
	hashValue(stats->queueDepth, 2);
	return hash;
}
