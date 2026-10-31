/*
 * vsched.c
 *
 * LICHOMAT v0.5: the voice scheduler of sample-accurate triggering (tracker E6.10),
 * see vsched.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "Soundengine/vsched.h"

#include <string.h>

/* the window (S5): written by the DMA interrupt, read by the render */
typedef struct
{
	uint32_t w0, w1;
} vsched_slot_t;

static volatile vsched_slot_t vs_slot[2];
static volatile uint32_t vs_serial;	/**< completions so far; slot[serial & 1] is the latest */
static uint32_t vs_est;			/**< the latest estimate, interrupt only */
static uint8_t vs_seeded;

#ifdef VSCHED_TEST
void (*vsched_testReadHook)(int step);
#define VS_READ_HOOK(step) do { if (vsched_testReadHook) vsched_testReadHook(step); } while (0)
#else
#define VS_READ_HOOK(step) ((void)0)
#endif

/* the rings and the plan (seq_tick() context) */
static vsched_rec_t vs_rec[VSCHED_VOICES][VSCHED_RING];	/**< oldest first (P4) */
static uint8_t vs_count[VSCHED_VOICES];
static uint8_t vs_due[VSCHED_VOICES];		/**< bit i: vs_rec[v][i] is due in this block */
static uint8_t vs_late[VSCHED_VOICES];		/**< bit i: and late */
static uint8_t vs_k[VSCHED_VOICES];		/**< the collapsed offset of the due records (S2, P3) */
static uint8_t vs_phase[VSCHED_VOICES];		/**< S13: the pending phase split, 0 = none */
static uint32_t vs_planSerial;			/**< the window's serial, for the trigger log */
static vsched_stats_t vs_stats;
static vsched_trace_t vs_trace;

//------------------------------------------------------------------------------
void vsched_blockDone(uint32_t t)
{
	const uint32_t pred = vs_est + VSCHED_BLOCK_CYCLES;
	const int32_t e = (int32_t)(t - pred);
	uint32_t next;
	if (!vs_seeded || e > (int32_t)(VSCHED_BLOCK_CYCLES / 2u) || e < -(int32_t)(VSCHED_BLOCK_CYCLES / 2u))
		next = t;					/* the first block, or a stall: re-seed */
	else
		next = pred + (uint32_t)(e < 0 ? e : e / 8);	/* late-only filter (S5) */
	const uint32_t n = vs_serial + 1u;
	vs_slot[n & 1u].w0 = vs_est;			/* the previous estimate */
	vs_slot[n & 1u].w1 = next;
	vs_est = next;
	vs_seeded = 1;
	vs_serial = n;					/* publish: one 32-bit store */
}
//------------------------------------------------------------------------------
/** the latest completion's window (P2): one completion writes the other slot; the serial
 * read again shows a second one, and the read is made once more (no spin). Not 0.5-2
 * blocks long (the first completion, a re-seed): no window */
static int vs_window(uint32_t *w0, uint32_t *len, uint32_t *serial)
{
	for (int attempt = 0; attempt < 2; attempt++)
	{
		const uint32_t s = vs_serial;
		VS_READ_HOOK(0);
		const uint32_t a = vs_slot[s & 1u].w0;
		VS_READ_HOOK(1);
		const uint32_t b = vs_slot[s & 1u].w1;
		VS_READ_HOOK(2);
		if (vs_serial - s < 2u)
		{
			*w0 = a;
			*len = b - a;
			*serial = s;
			return s != 0u && *len >= VSCHED_BLOCK_CYCLES / 2u && *len <= 2u * VSCHED_BLOCK_CYCLES;
		}
	}
	return 0;
}
//------------------------------------------------------------------------------
void vsched_post(const vsched_rec_t *r)
{
	if (!seq_isInTick())
	{
		vs_stats.offTick++;				/* S17, P9 */
		return;
	}
	const uint8_t v = vsched_voiceOf(r->track);
	if (vs_count[v] == VSCHED_RING)
	{
		memmove(&vs_rec[v][0], &vs_rec[v][1], (VSCHED_RING - 1u) * sizeof vs_rec[v][0]);
		vs_count[v]--;
		vs_stats.overflow++;
	}
	vs_rec[v][vs_count[v]++] = *r;
}
//------------------------------------------------------------------------------
void vsched_beginBlock(void)
{
	uint32_t w0 = 0, len = 0, serial = 0;
	const int haveWindow = vs_window(&w0, &len, &serial);
	vs_planSerial = serial;
	uint8_t splitting = 0;
	for (uint8_t v = 0; v < VSCHED_VOICES; v++)
	{
		vs_due[v] = vs_late[v] = vs_k[v] = 0;
		for (uint8_t i = 0; i < vs_count[v]; i++)
		{
			const vsched_rec_t *r = &vs_rec[v][i];
			uint8_t k = 0;
			/* untimed (S10): offset 0; no window: offset 0, counted late */
			const vsched_fate_t f = !(r->flags & VS_TIMED) ? VS_DUE
				: !haveWindow ? VS_LATE : vsched_offset(r->time, w0, len, &k);
			if (f == VS_WAIT)
				continue;
			vs_due[v] |= (uint8_t)(1u << i);
			if (f == VS_LATE)
				vs_late[v] |= (uint8_t)(1u << i);
			if (k > vs_k[v])
				vs_k[v] = k;			/* S2: all at the last offset (P3) */
		}
		if (vs_k[v] || vs_phase[v])
			splitting++;
	}
	if (splitting > VSCHED_SPLIT_CAP)
	{
		memset(vs_k, 0, sizeof vs_k);		/* S20: the block falls back to block timing */
		memset(vs_phase, 0, sizeof vs_phase);
		vs_stats.capFallbacks++;
	}
}
//------------------------------------------------------------------------------
/** apply voice v's due records in order (p-locks, slide mode, trigger), then drop them */
static void vs_apply(uint8_t v, uint8_t k)
{
	uint8_t kept = 0;
	for (uint8_t i = 0; i < vs_count[v]; i++)
	{
		const vsched_rec_t r = vs_rec[v][i];
		if (!(vs_due[v] & (1u << i)))
		{
			vs_rec[v][kept++] = r;
			continue;
		}
		const uint8_t late = (uint8_t)((vs_late[v] >> i) & 1u);
		if (late)
			vs_stats.late++;
		if (r.flags & VS_PLOCKS)
			seq_applyPlocks(r.track, r.p1Nr, r.p1Val, r.p2Nr, r.p2Val);
		if (r.track < 3u)
			Drum_setSlide(r.track, (uint8_t)((r.flags & VS_SLIDE_MASK) >> VS_SLIDE_SHIFT));
		voiceControl_trigger(r.track, r.note, r.vel);
		if (vs_trace)
			vs_trace(&r, vs_planSerial, k, late);
	}
	vs_count[v] = kept;
	vs_due[v] = vs_late[v] = 0;
}
//------------------------------------------------------------------------------
void vsched_applyPhase0(void)
{
	for (uint8_t v = 0; v < VSCHED_VOICES; v++)
		if (vs_due[v] && vs_k[v] == 0)
		{
			vs_apply(v, 0);
			vs_phase[v] = 0;	/* a new note at offset 0: its control step is the block's */
		}
}
//------------------------------------------------------------------------------
uint8_t vsched_split(uint8_t v)
{
	return v < VSCHED_VOICES ? vs_k[v] : 0;
}
//------------------------------------------------------------------------------
uint8_t vsched_phase(uint8_t v)
{
	return v < VSCHED_VOICES ? vs_phase[v] : 0;
}
//------------------------------------------------------------------------------
void vsched_applySplit(uint8_t v)
{
	if (v >= VSCHED_VOICES || vs_k[v] == 0)
		return;
	vs_apply(v, vs_k[v]);
	vs_phase[v] = vs_k[v];
	vs_k[v] = 0;
	vs_stats.splits++;
}
//------------------------------------------------------------------------------
void vsched_endPhase(uint8_t v)
{
	if (v < VSCHED_VOICES && vs_phase[v])
	{
		vs_phase[v] = 0;
		vs_stats.phaseSplits++;
	}
}
//------------------------------------------------------------------------------
void vsched_stats(vsched_stats_t *out)
{
	*out = vs_stats;
}
//------------------------------------------------------------------------------
int vsched_window(uint32_t *w0, uint32_t *len)
{
	uint32_t serial;
	return vs_window(w0, len, &serial);
}
//------------------------------------------------------------------------------
void vsched_setTrace(vsched_trace_t fn)
{
	vs_trace = fn;
}
//------------------------------------------------------------------------------
#ifdef VSCHED_TEST
void vsched_testReset(void)
{
	memset((void *)vs_slot, 0, sizeof vs_slot);
	vs_serial = 0;
	vs_est = 0;
	vs_seeded = 0;
	memset(vs_rec, 0, sizeof vs_rec);
	memset(vs_count, 0, sizeof vs_count);
	memset(vs_due, 0, sizeof vs_due);
	memset(vs_late, 0, sizeof vs_late);
	memset(vs_k, 0, sizeof vs_k);
	memset(vs_phase, 0, sizeof vs_phase);
	vs_planSerial = 0;
	memset(&vs_stats, 0, sizeof vs_stats);
	vs_trace = 0;
	vsched_testReadHook = 0;
}
#endif
