/*
 * PatternGen.c
 *
 * LICHOMAT v0.8 (plan v0.8 Tasks 2-3, G5-G10, G14, G19-G21; spec
 * docs/superpowers/specs/2026-11-11-groove-generator-design.md §3-§5, §8): the pattern generator,
 * a port of the prototype tools/gen/gen.py, the reference: the same random draws in the same
 * order, the same ranking and the same steps. tests/test_pattern_gen.c checks it byte for byte
 * against gen.py's vectors.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "Soundengine/Sequencer/PatternGen.h"

#include <string.h>

/* spec §5: round(127 x 10^(-k/20)), at least 1, for k = 0..30 dB below the accent (gen.py VEL_TABLE) */
static const uint8_t pg_velOfDb[1 - PG_LEV_MIN] = {
	127, 113, 101, 90, 80, 71, 64, 57, 51, 45, 40, 36, 32, 28, 25, 23,
	20, 18, 16, 14, 13, 11, 10, 9, 8, 7, 6, 6, 5, 5, 4 };

/* spec §3: sty den lev var not of hat, sub, prc, rar, prp (gen.py TYPE_DEFAULTS) */
static const int8_t pg_defaults[PG_TYPES - 1][5] = {
	{ 1, 7, -15, 1, 0 }, { 1, 0, -3, 0, -2 }, { 1, 3, -13, 1, 0 }, { 1, 2, 0, 0, 0 }, { 1, 4, -15, 0, 0 } };

/* the styles of each type (spec §4); patternGen_checked() clamps sty into them (spec §8, gen.py STY_RANGE) */
static const uint8_t pg_styMin[PG_TYPES] = { 0, 0, 1, 0, 1, 1 };
static const uint8_t pg_styMax[PG_TYPES] = { PG_STY_MAX, 4, 8, 4, 4, 4 };

typedef struct
{
	uint32_t	x;			/* the random state (spec §5) */
	uint16_t	key[PG_STEPS];		/* draw 1: rand(65536) of each grid place, 16 (prc 32) */
	int8_t		off[32];		/* draw 2: each grid place's level offset in dB */
	uint8_t		order[PG_STEPS];	/* the last ranking, the best place first */
	uint16_t	acc[PG_BARS];		/* the current track's accents, bar by bar as it plays */
	uint64_t	hit;			/* the steps written (gen.py _Out.written) */
	uint8_t		note;			/* 60 + not, clamped to 0..127 */
} pg_ctx_t;

uint32_t patternGen_step(uint32_t x)
{
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

uint32_t patternGen_seed(const PatternGenSettings *s, uint8_t track)
{
	const uint8_t b[4] = { s->sed, s->typ, s->sty, track };
	uint32_t x = 2166136261u;
	for (unsigned i = 0; i < 4u; i++)
		x = (x ^ b[i]) * 16777619u;
	return x ? x : 1u;
}

static uint32_t pg_rand(pg_ctx_t *c, uint32_t n)
{
	c->x = patternGen_step(c->x);
	return c->x % n;
}

uint8_t patternGen_dbVel(int db)
{
	if (db > 0) db = 0;
	if (db < PG_LEV_MIN) db = PG_LEV_MIN;
	return pg_velOfDb[-db];
}

static int pg_clamp(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

PatternGenSettings patternGen_checked(PatternGenSettings s)
{
	s.typ = (uint8_t)pg_clamp(s.typ, PG_TYPE_OFF, PG_TYPES - 1);
	s.sty = (uint8_t)pg_clamp(s.sty, pg_styMin[s.typ], pg_styMax[s.typ]);	/* within 0..8 */
	s.den = (uint8_t)pg_clamp(s.den, 0, PG_DEN_MAX);
	s.lev = (int8_t)pg_clamp(s.lev, PG_LEV_MIN, 0);
	s.var = (int8_t)pg_clamp(s.var, -PG_VAR_MAX, PG_VAR_MAX);
	s.note = (int8_t)pg_clamp(s.note, -PG_NOTE_MAX, PG_NOTE_MAX);
	s.sed = (uint8_t)pg_clamp(s.sed, 0, PG_SED_MAX);
	s.src = (uint8_t)pg_clamp(s.src, 0, PG_TRACKS - 1);
	s.shf = (int8_t)pg_clamp(s.shf, -PG_SHF_MAX, PG_SHF_MAX);
	return s;
}

void patternGen_typeDefaults(uint8_t typ, PatternGenSettings *s)
{
	if (!s || (typ >= PG_TYPES))
		return;
	s->typ = typ;
	if (typ == PG_TYPE_OFF)
		return;
	const int8_t *d = pg_defaults[typ - 1u];
	s->sty = (uint8_t)d[0]; s->den = (uint8_t)d[1]; s->lev = d[2]; s->var = d[3]; s->note = d[4];
}

/* spec §5 (gen.py rank()): the places p < n with w[p] > 0 and bit p of allowed into c->order, the
 * largest key[p] x w[p] first, the lower place on a tie; returns how many */
static unsigned pg_rank(pg_ctx_t *c, const uint16_t *key, const uint8_t *w, uint64_t allowed, unsigned n)
{
	uint64_t cand = 0;
	for (unsigned p = 0; p < n; p++)
		if (w[p] && ((allowed >> p) & 1u))
			cand |= (uint64_t)1 << p;
	unsigned k = 0;
	for (; cand; k++)
	{
		unsigned best = 0;
		uint32_t bestKey = 0;
		int found = 0;
		for (unsigned p = 0; p < n; p++)
		{
			const uint32_t kp = (uint32_t)key[p] * w[p];
			if (((cand >> p) & 1u) && (!found || (kp > bestKey)))
			{
				best = p;
				bestKey = kp;
				found = 1;
			}
		}
		c->order[k] = (uint8_t)best;
		cand &= ~((uint64_t)1 << best);
	}
	return k;
}

static int pg_isOn(const uint16_t *on, unsigned s) { return (on[s >> 4] >> (s & 15u)) & 1u; }
static uint8_t pg_noteOf(int n) { return (uint8_t)pg_clamp(n, 0, 127); }
/** step s's fine nibble as stored (a raw copy keeps a value stepFine_get() reads as 0) */
static uint8_t pg_fine(const uint8_t *fine, unsigned s) { return (uint8_t)((fine[s >> 1] >> ((s & 1u) * 4u)) & 0x0Fu); }

/* gen.py's _Out and cur.played(): the accents (on, vel >= 64) of the current track as it plays (a track
 * shorter than 64 steps repeated to 64, spec §8) bar by bar into c->acc; then every step clean and off
 * (spec §3, §8: vel 127, prob 100, note 60 + not, no p-lock, flam 0, shift 7, fine 0) and the length
 * 64; with keep the accents stay as they play (gen.py _Out.keep: the step, its timing and fine) */
static void pg_frame(pg_ctx_t *c, PatternGenTrack *io, int keep)
{
	const unsigned len = (io->length == 0u) ? 16u : (io->length > PG_STEPS) ? (unsigned)PG_STEPS : io->length;
	memset(c->acc, 0, sizeof c->acc);
	for (unsigned i = 0; i < PG_STEPS; i++)
	{
		const unsigned j = i % len;
		if (pg_isOn(io->on, j) && ((io->step[j].volume & 0x7Fu) >= PG_ACCENT_MIN))
			c->acc[i >> 4] |= (uint16_t)(1u << (i & 15u));
	}
	for (unsigned i = PG_STEPS; i-- > 0u;)	/* downwards: step i % len <= i is still the current one */
	{
		const unsigned j = i % len;
		if (keep && ((c->acc[i >> 4] >> (i & 15u)) & 1u))
		{
			io->step[i] = io->step[j];
			io->timing[i] = io->timing[j];
			stepFine_set(io->fine, (uint8_t)i, (int8_t)pg_fine(io->fine, j));
		}
		else
		{
			io->step[i] = (Step){ 127, PG_PROB_ALWAYS, c->note, PG_NO_PLOCK, 0, PG_NO_PLOCK, 0 };
			io->timing[i] = (StepTiming){ 0, PG_SHIFT_NONE };
			stepFine_set(io->fine, (uint8_t)i, 0);
		}
	}
	for (unsigned b = 0; b < PG_BARS; b++)
		io->on[b] = keep ? c->acc[b] : 0u;
	io->length = PG_STEPS;
}

/* gen.py _Out.hit(): step i on with vel, prob, note and flam (the rest is the frame's) */
static void pg_hit(pg_ctx_t *c, PatternGenTrack *io, unsigned i, uint8_t vel, uint8_t prob, uint8_t note, uint8_t flam)
{
	io->step[i].volume = vel;
	io->step[i].prob = prob;
	io->step[i].note = note;
	io->timing[i].flam = (uint8_t)(flam & 0x0Fu);
	io->on[i >> 4] |= (uint16_t)(1u << (i & 15u));
	c->hit |= (uint64_t)1 << i;
}

/* gen.py _Out.done(): PG_SOFT when a written step is on below 127 */
static uint8_t pg_soft(const pg_ctx_t *c, const PatternGenTrack *io)
{
	for (unsigned i = 0; i < PG_STEPS; i++)
		if (((c->hit >> i) & 1u) && pg_isOn(io->on, i) && ((io->step[i].volume & 0x7Fu) < 127u))
			return PG_SOFT;
	return 0;
}

/* spec §4.1, §8 (gen.py _hat()): bar by bar the style's accents at 127 (style 0: the current track's,
 * which pg_frame() kept), then den ghosts on the best ranked free places at lev + the place's offset;
 * var +n: the next n places of the ranking at 2:2, var -n: the last n of the den at 1:2 */
static void pg_hat(pg_ctx_t *c, const PatternGenSettings *s, PatternGenTrack *io)
{
	/* the ghosts' weights of styles 0-4 (gen.py HAT_WEIGHTS) on a quarter (steps 1 5 9 13), an
	 * offbeat (3 7 11 15) and a 2nd or 4th 16th (2 4 ... 16) */
	static const uint8_t wq[5] = { 2, 2, 0, 8, 1 }, wo[5] = { 1, 0, 0, 0, 1 }, we[5] = { 4, 4, 4, 3, 1 };
	uint8_t w[16];
	for (unsigned p = 0; p < 16u; p++)
		w[p] = ((p & 3u) == 0u) ? wq[s->sty] : ((p & 3u) == 2u) ? wo[s->sty] : we[s->sty];
	for (unsigned b = 0; b < PG_BARS; b++)
	{
		/* gen.py HAT_ACCENTS: the offbeats, style 2 the quarters too */
		const uint16_t acc = (s->sty == 0u) ? c->acc[b] : (s->sty == 2u) ? 0x5555u : 0x4444u;
		if (s->sty != 0u)
			for (unsigned p = 0; p < 16u; p++)
				if ((acc >> p) & 1u)
					pg_hit(c, io, 16u * b + p, 127, PG_PROB_ALWAYS, c->note, 0);
		const unsigned nfree = pg_rank(c, c->key, w, (uint16_t)~acc, 16);
		const unsigned n = (s->den < nfree) ? s->den : nfree;
		const unsigned more = (s->var > 0) ? (unsigned)s->var : 0u;
		const unsigned last = (n + more < nfree) ? n + more : nfree;
		const unsigned fewer = (s->var < 0) ? (((unsigned)-s->var < n) ? (unsigned)-s->var : n) : 0u;
		for (unsigned r = 0; r < last; r++)
		{
			const unsigned p = c->order[r];
			const uint8_t prob = (r >= n) ? PG_COND_2_2 : (r >= n - fewer) ? PG_COND_1_2 : PG_PROB_ALWAYS;
			pg_hit(c, io, 16u * b + p, patternGen_dbVel(s->lev + c->off[p]), prob, c->note, 0);
		}
	}
}

uint8_t patternGen_generate(const PatternGenSettings *in, uint8_t track, const PatternGenSource *src, PatternGenTrack *io)
{
	if (!in || !io || (track >= PG_TRACKS))
		return 0;
	const PatternGenSettings s = patternGen_checked(*in);
	if ((s.typ == PG_TYPE_OFF) || ((s.typ == PG_TYPE_PRP) && (s.src == track)))
		return 0;	/* gen.py: the current track as it is */
	(void)src;	/* Task 3: sub prc rar prp */
	pg_ctx_t c;
	memset(&c, 0, sizeof c);
	/* spec §5: the state, then the draws in their fixed order, which depend on typ and sty only */
	c.x = patternGen_seed(&s, track);
	const unsigned grid = (s.typ == PG_TYPE_PRC) ? 32u : 16u;
	for (unsigned p = 0; p < grid; p++)	/* draw 1: the places' keys */
		c.key[p] = (uint16_t)pg_rand(&c, 65536u);
	for (unsigned p = 0; p < grid; p++)	/* draw 2: the level offsets, prc -2..+2 dB, the others -3..+2 */
		c.off[p] = (int8_t)((s.typ == PG_TYPE_PRC) ? (int)pg_rand(&c, 5u) - 2 : (int)pg_rand(&c, 6u) - 3);
	c.note = pg_noteOf(PG_NOTE_BASE + s.note);
	switch (s.typ)
	{
	case PG_TYPE_HAT:
		pg_frame(&c, io, s.sty == 0u);
		pg_hat(&c, &s, io);
		break;
	default:
		return 0;	/* Task 3: sub prc rar prp */
	}
	return (uint8_t)(PG_WROTE | pg_soft(&c, io));
}
