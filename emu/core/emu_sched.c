/*
 * emu_sched.c
 *
 * The virtual clock and the scheduler of the LICHOMAT emulator (stage 1b spec
 * 3.1, 3.3, 4.2): one 64-bit count of 216 MHz cycles, the interrupt bodies at
 * their times in NVIC priority order, PRIMASK, the main-loop passes, the
 * HAL_GetTick() and HAL_Delay() rules, the tier hook, the inputs, the trail of
 * scheduler events, the watchdog counter and the faults.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stm32f7xx_hal.h"
#include "emu_internal.h"

/* --- state ------------------------------------------------------------------- */

const uint8_t emu_srcPriority[EMU_NUM_SRC] = { 0, 0, 3, 3, 3, 3, 3, 4, 5 };
emu_source_t emu_src[EMU_NUM_SRC];
emu_firmware_t emu_fw;
emu_config_t emu_cfg;
emu_counters_t emu_count;

static uint64_t emu_clock;			/* cycles since power-on */
static uint32_t emu_primask;
static int emu_bodyDepth;
static int emu_runningPrio = EMU_PRIO_THREAD;
static int emu_passing, emu_booting, emu_hasBooted, emu_firstPassDue;
static int emu_passRequested, emu_blockSeen, emu_hookDepth;
static uint64_t emu_nextGridPass;
static uint32_t emu_segmentCalls;	/* HAL_GetTick() calls in thread context in this segment */

#define EMU_MAX_HOOKS 4
static void (*emu_samplers[EMU_MAX_HOOKS])(void);
static unsigned emu_samplerCount;
static void (*emu_passStarts[EMU_MAX_HOOKS])(void);
static unsigned emu_passStartCount;
static void (*emu_inputFns[EMU_IN_NUM])(const emu_input_t *in);

static emu_input_t *emu_heap;		/* the inputs, a binary min-heap on (t, seq) */
static size_t emu_heapCount, emu_heapCap;
static uint64_t emu_heapSeq;

static emu_trailEntry_t emu_trailRing[EMU_TRAIL_LENGTH];
static unsigned emu_trailNext, emu_trailCount;

static _Atomic uint64_t emu_progressCount;
static _Atomic int emu_waiting;

/* --- the clock --------------------------------------------------------------- */

uint64_t emu_framesToCycles(uint64_t frames)
{
	/* 44,140.625 Hz = 353,125 / 8 Hz, so a frame is 216e6 * 8 / 353,125 = 552,960 / 113 cycles */
	return frames / 113u * 552960u + frames % 113u * 552960u / 113u;
}

uint64_t emu_now(void)
{
	return emu_clock;
}

static void emu_setNow(uint64_t t)
{
	emu_clock = t;
	DWT->CYCCNT = (uint32_t)t;	/* always the low 32 bits of the clock */
}

int emu_inBody(void)
{
	return emu_bodyDepth > 0;
}

int emu_inPass(void)
{
	return emu_passing;
}

int emu_booted(void)
{
	return emu_hasBooted;
}

/* --- the trail ------------------------------------------------------------------ */

static void emu_trailPush(uint8_t kind, uint8_t arg)
{
	emu_trailRing[emu_trailNext] = (emu_trailEntry_t){ emu_clock, kind, arg };
	emu_trailNext = (emu_trailNext + 1u) % EMU_TRAIL_LENGTH;
	if (emu_trailCount < EMU_TRAIL_LENGTH)
		emu_trailCount++;
}

unsigned emu_trail(emu_trailEntry_t *out, unsigned n)
{
	if (n > emu_trailCount)
		n = emu_trailCount;
	for (unsigned i = 0; i < n; i++)
		out[i] = emu_trailRing[(emu_trailNext + EMU_TRAIL_LENGTH - n + i) % EMU_TRAIL_LENGTH];
	return n;
}

static size_t emu_putDecimal(char *p, uint64_t v)
{
	char tmp[21];
	size_t n = 0;
	do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
	for (size_t i = 0; i < n; i++)
		p[i] = tmp[n - 1u - i];
	return n;
}

void emu_trailWrite(int fd, unsigned n)
{
	static const char *const kinds[] = { "body", "pass", "boot", "input", "direct" };
	if (n > emu_trailCount)
		n = emu_trailCount;
	for (unsigned i = 0; i < n; i++)
	{
		const emu_trailEntry_t e = emu_trailRing[(emu_trailNext + EMU_TRAIL_LENGTH - n + i) % EMU_TRAIL_LENGTH];
		char line[64];
		size_t len = emu_putDecimal(line, e.t);
		line[len++] = ' ';
		const char *k = e.kind < 5u ? kinds[e.kind] : "?";
		while (*k)
			line[len++] = *k++;
		line[len++] = ' ';
		len += emu_putDecimal(line + len, e.arg);
		line[len++] = '\n';
		if (write(fd, line, len) < 0)
			return;
	}
}

/* --- messages ------------------------------------------------------------------- */

void emu_fault(const char *what)
{
	if (emu_cfg.fault)
	{
		emu_cfg.fault(emu_cfg.msgCtx, what);
		return;
	}
	fprintf(stderr, "emulator fault at cycle %llu: %s\n", (unsigned long long)emu_clock, what);
	emu_trailWrite(2, 32u);
	abort();
}

void emu_warn(const char *what)
{
	if (emu_cfg.warn)
		emu_cfg.warn(emu_cfg.msgCtx, what);
	else
		fprintf(stderr, "warning: %s\n", what);
}

/* --- the watchdog ---------------------------------------------------------------- */

uint64_t emu_progress(void)
{
	return atomic_load(&emu_progressCount);
}

void emu_waitBegin(void)
{
	atomic_store(&emu_waiting, 1);
}

void emu_waitEnd(void)
{
	atomic_store(&emu_waiting, 0);
	atomic_fetch_add(&emu_progressCount, 1u);
}

int emu_watchdogPoll(emu_watchdog_t *w, double wallSeconds)
{
	const uint64_t p = atomic_load(&emu_progressCount);
	if (!w->primed || p != w->progress || atomic_load(&emu_waiting))
	{
		w->primed = 1;
		w->progress = p;
		w->since = wallSeconds;
		return 0;
	}
	return wallSeconds - w->since >= 10.0;
}

/* --- the hook and the samplers ------------------------------------------------------ */

static void emu_callHook(emu_hookReason_t why)
{
	if (!emu_cfg.hook || emu_hookDepth)
		return;
	emu_hookDepth = 1;
	emu_cfg.hook(emu_cfg.hookCtx, emu_clock, why);
	emu_hookDepth = 0;
}

void emu_addSampler(void (*fn)(void))
{
	if (emu_samplerCount < EMU_MAX_HOOKS)
		emu_samplers[emu_samplerCount++] = fn;
}

void emu_addPassStart(void (*fn)(void))
{
	if (emu_passStartCount < EMU_MAX_HOOKS)
		emu_passStarts[emu_passStartCount++] = fn;
}

static void emu_sample(void)
{
	for (unsigned i = 0; i < emu_samplerCount; i++)
		emu_samplers[i]();
}

void emu_noteBlock(void)
{
	emu_blockSeen = 1;
	if (!emu_passing)
		emu_passRequested = 1;
}

/* --- inputs ----------------------------------------------------------------------- */

static int emu_inputBefore(const emu_input_t *a, const emu_input_t *b)
{
	return a->t < b->t || (a->t == b->t && a->seq < b->seq);
}

uint64_t emu_inputTime(uint64_t t)
{
	if (t < emu_clock)
	{
		emu_count.late++;
		return emu_clock;
	}
	return t;
}

void emu_inputPush(uint64_t t, uint8_t kind, uint8_t a, int32_t b, double level)
{
	if (emu_heapCount == emu_heapCap)
	{
		const size_t cap = emu_heapCap ? emu_heapCap * 2u : 256u;
		emu_input_t *grown = realloc(emu_heap, cap * sizeof *grown);
		if (!grown)
		{
			emu_fault("out of memory for inputs");
			return;
		}
		emu_heap = grown;
		emu_heapCap = cap;
	}
	const emu_input_t in = { emu_inputTime(t), emu_heapSeq++, kind, a, b, level };
	size_t i = emu_heapCount++;
	while (i > 0 && emu_inputBefore(&in, &emu_heap[(i - 1u) / 2u]))
	{
		emu_heap[i] = emu_heap[(i - 1u) / 2u];
		i = (i - 1u) / 2u;
	}
	emu_heap[i] = in;
}

static emu_input_t emu_inputPop(void)
{
	const emu_input_t top = emu_heap[0];
	const emu_input_t last = emu_heap[--emu_heapCount];
	size_t i = 0;
	for (;;)
	{
		size_t c = 2u * i + 1u;
		if (c >= emu_heapCount)
			break;
		if (c + 1u < emu_heapCount && emu_inputBefore(&emu_heap[c + 1u], &emu_heap[c]))
			c++;
		if (!emu_inputBefore(&emu_heap[c], &last))
			break;
		emu_heap[i] = emu_heap[c];
		i = c;
	}
	if (emu_heapCount)
		emu_heap[i] = last;
	return top;
}

void emu_inputHandler(uint8_t kind, void (*fn)(const emu_input_t *in))
{
	if (kind < EMU_IN_NUM)
		emu_inputFns[kind] = fn;
}

/* --- PRIMASK and the dispatch ------------------------------------------------------- */

uint32_t __get_PRIMASK(void)
{
	return emu_primask;
}

void __set_PRIMASK(uint32_t priMask)
{
	emu_primask = priMask & 1u;
	if (!emu_primask)
		emu_dispatch();
}

void __disable_irq(void)
{
	emu_primask = 1u;
}

void __enable_irq(void)
{
	emu_primask = 0u;
	emu_dispatch();
}

void emu_dispatch(void)
{
	while (!emu_primask)
	{
		int best = -1;
		for (int s = 0; s < EMU_NUM_SRC; s++)
			if (emu_src[s].pending && emu_srcPriority[s] < emu_runningPrio
				&& (best < 0 || emu_srcPriority[s] < emu_srcPriority[best]))
				best = s;
		if (best < 0)
			return;
		emu_src[best].pending--;
		const int saved = emu_runningPrio;
		emu_runningPrio = emu_srcPriority[best];
		emu_bodyDepth++;
		emu_trailPush(EMU_TRAIL_BODY, (uint8_t)best);
		atomic_fetch_add(&emu_progressCount, 1u);
		if (emu_src[best].body)
			emu_src[best].body();
		emu_sample();
		emu_bodyDepth--;
		emu_runningPrio = saved;
	}
}

/* --- the scheduler ---------------------------------------------------------------- */

/* the earliest time at which an input or a source falls due */
static uint64_t emu_nextDue(void)
{
	uint64_t t = emu_heapCount ? emu_heap[0].t : EMU_NEVER;
	for (int s = 0; s < EMU_NUM_SRC; s++)
		if (emu_src[s].due < t)
			t = emu_src[s].due;
	return t;
}

/* Run the next time point at or before limit: its inputs, the fire of every
 * source due then, and the bodies. Returns 0 if nothing falls due by limit. */
static int emu_step(uint64_t limit)
{
	const uint64_t t = emu_nextDue();
	if (t == EMU_NEVER || t > limit)
		return 0;
	emu_setNow(t);
	atomic_fetch_add(&emu_progressCount, 1u);
	while (emu_heapCount && emu_heap[0].t <= t)
	{
		const emu_input_t in = emu_inputPop();
		emu_trailPush(EMU_TRAIL_INPUT, in.kind);
		if (in.kind < EMU_IN_NUM && emu_inputFns[in.kind])
			emu_inputFns[in.kind](&in);
	}
	for (int s = 0; s < EMU_NUM_SRC; s++)
		if (emu_src[s].due <= t)
		{
			if (emu_src[s].fire)
				emu_src[s].fire();
			else
				emu_src[s].due = EMU_NEVER;	/* a source without a model never fires */
		}
	emu_dispatch();
	if (emu_blockSeen)
	{
		emu_blockSeen = 0;
		emu_callHook(EMU_HOOK_BLOCK);
	}
	return 1;
}

void emu_threadAdvance(uint64_t cycles)
{
	if (emu_bodyDepth > 0 || cycles == 0)
		return;	/* inside an interrupt body time stands still */
	const uint64_t target = emu_clock + cycles;
	while (emu_clock < target)
	{
		const uint64_t boundary = (emu_clock / EMU_CYCLES_PER_MS + 1u) * EMU_CYCLES_PER_MS;
		const uint64_t end = boundary < target ? boundary : target;
		while (emu_step(end))
			;
		emu_setNow(end);
		if (end % EMU_CYCLES_PER_MS == 0)
			emu_callHook(EMU_HOOK_TIME);
	}
}

/* --- the HAL's time functions (spec 4.2) --------------------------------------------- */

uint32_t HAL_GetTick(void)
{
	if (emu_bodyDepth == 0)
	{
		emu_count.tickCalls = ++emu_segmentCalls;
		if (emu_segmentCalls > 512u)
			emu_threadAdvance(216u);	/* a spin: 1 us per call after 512 free calls */
	}
	return (uint32_t)(emu_clock / EMU_CYCLES_PER_MS);
}

void HAL_Delay(uint32_t Delay)
{
	emu_threadAdvance((uint64_t)Delay * EMU_CYCLES_PER_MS);
}

/* --- boot, passes, direct calls ---------------------------------------------------- */

static void emu_segmentBegin(void)
{
	emu_segmentCalls = 0;
	emu_count.tickCalls = 0;
}

static void emu_boot(void)
{
	emu_booting = 1;
	emu_segmentBegin();
	emu_trailPush(EMU_TRAIL_BOOT, 0);
	if (emu_fw.boot)
		emu_fw.boot();
	emu_sample();
	emu_booting = 0;
	emu_hasBooted = 1;
	emu_count.bootEnd = emu_clock;
	emu_firstPassDue = 1;
	emu_passRequested = 0;
}

static void emu_pass(void)
{
	emu_passing = 1;
	emu_segmentBegin();
	emu_trailPush(EMU_TRAIL_PASS, 0);
	atomic_fetch_add(&emu_progressCount, 1u);
	for (unsigned i = 0; i < emu_passStartCount; i++)
		emu_passStarts[i]();
	if (emu_fw.pass)
		emu_fw.pass();
	emu_sample();
	emu_passing = 0;
	emu_count.passes++;
	emu_nextGridPass = (emu_clock / EMU_PASS_GRID + 1u) * EMU_PASS_GRID;
	emu_passRequested = 0;
}

void emu_runUntil(uint64_t t)
{
	if (!emu_hasBooted)
	{
		emu_boot();
		if (emu_clock >= t)
			return;
	}
	while (emu_clock < t)
	{
		if (emu_firstPassDue)
		{
			emu_firstPassDue = 0;
			emu_pass();
			continue;
		}
		/* to the next pass: the grid, or a block request outside a pass */
		while (!emu_passRequested && emu_step(emu_nextGridPass))
			;
		if (emu_passRequested)
			while (emu_step(emu_clock))
				;	/* every body due at the pass's time runs first */
		else
			emu_setNow(emu_nextGridPass);
		emu_pass();
	}
}

void emu_directCall(void (*fn)(void *arg), void *arg)
{
	emu_segmentBegin();
	emu_trailPush(EMU_TRAIL_DIRECT, 0);
	fn(arg);
	emu_sample();
}

/* --- power-on ------------------------------------------------------------------- */

void emu_configDefaults(emu_config_t *cfg)
{
	memset(cfg, 0, sizeof *cfg);
	cfg->cardIn = 1;
	cfg->jacks = EMU_JACKS_ALL;
	for (int i = 0; i < 6; i++)
		cfg->faders[i] = 1.0;
	cfg->seed = 1u;
}

void emu_getCounters(emu_counters_t *c)
{
	*c = emu_count;
}

void emu_corePowerOn(const emu_config_t *cfg)
{
	emu_cfg = *cfg;
	memset(&emu_fw, 0, sizeof emu_fw);
	memset(emu_src, 0, sizeof emu_src);
	for (int s = 0; s < EMU_NUM_SRC; s++)
		emu_src[s].due = EMU_NEVER;
	memset(&emu_count, 0, sizeof emu_count);
	emu_count.codecStart = EMU_NEVER;
	emu_heapCount = 0;
	emu_heapSeq = 0;
	emu_samplerCount = 0;
	emu_passStartCount = 0;
	memset(emu_inputFns, 0, sizeof emu_inputFns);
	emu_trailNext = emu_trailCount = 0;
	emu_primask = 0;
	emu_bodyDepth = 0;
	emu_runningPrio = EMU_PRIO_THREAD;
	emu_passing = emu_booting = emu_hasBooted = emu_firstPassDue = 0;
	emu_passRequested = emu_blockSeen = emu_hookDepth = 0;
	emu_nextGridPass = 0;
	emu_segmentCalls = 0;
	emu_setNow(0);
	emu_regsReset(cfg);
}
