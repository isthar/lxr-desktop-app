/*
 * PatternGen.c
 *
 * LICHOMAT v0.8 (plan v0.8 Tasks 2-3, G5-G10, G14, G19-G21; spec
 * docs/superpowers/specs/2026-11-11-groove-generator-design.md §3-§5, §8, §9): the pattern generator,
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
	uint16_t	key[PG_STEPS];		/* draw 1: rand(65536) of each grid place, 16 (prc 32); draw 3: prc's
						 * var keys at 32-47, rar style 3-4's keys of the 64 steps */
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
/** a raw length as it plays: 0 as 16, above 64 as 64 */
static unsigned pg_len(uint8_t length) { return (length == 0u) ? 16u : (length > PG_STEPS) ? (unsigned)PG_STEPS : length; }
static uint8_t pg_noteOf(int n) { return (uint8_t)pg_clamp(n, 0, 127); }
/** step s's fine nibble as stored (a raw copy keeps a value stepFine_get() reads as 0) */
static uint8_t pg_fine(const uint8_t *fine, unsigned s) { return (uint8_t)((fine[s >> 1] >> ((s & 1u) * 4u)) & 0x0Fu); }

/* gen.py's _Out and cur.played(): the accents (on, vel >= 64) of the current track as it plays (a track
 * shorter than 64 steps repeated to 64, spec §8) bar by bar into c->acc; then every step clean and off
 * (spec §3, §8: vel 127, prob 100, note 60 + not, no p-lock, flam 0, shift 7, fine 0) and the length
 * 64; with keep the accents stay as they play (gen.py _Out.keep: the step, its timing and fine) */
static void pg_frame(pg_ctx_t *c, PatternGenTrack *io, int keep)
{
	const unsigned len = pg_len(io->length);
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

/* weight 1 on each of a bar's 16 places */
static const uint8_t pg_one[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

/* spec §4.2, §8, G20 (gen.py kick_places()): the places of bar 1 of track src as it plays (a track
 * shorter than 64 steps repeated); no track, an empty bar or src on the track itself: 1 5 9 13 */
static uint16_t pg_kick(const PatternGenSettings *s, uint8_t track, const PatternGenSource *src)
{
	uint16_t k = 0;
	if (src && (s->src != track))
	{
		const unsigned len = pg_len(src->length);
		for (unsigned p = 0; p < 16u; p++)
			k |= (uint16_t)(pg_isOn(src->on, p % len) << p);
	}
	return k ? k : 0x1111u;
}

/* spec §4.2: the bar of styles 1-7 (bit p is step p + 1) and style 7's second bar (gen.py SUB_BARS);
 * style 8's quarter figures ...x ..xx .xxx ..x. .... and their weights (gen.py SUB_FIGURES) */
static const uint16_t pg_subBar[8] = { 0, 0x8888u, 0xCCCCu, 0xEEEEu, 0x4444u, 0x4C4Cu, 0x40C0u, 0x0CCCu };
#define PG_SUB_BAR_B	0xECCCu
static const uint8_t pg_subFig[5] = { 0x8, 0xC, 0xE, 0x4, 0x0 }, pg_subFigW[5] = { 3, 4, 1, 2, 1 };
#define PG_SUB_FIG_TOTAL	(3u + 4u + 1u + 2u + 1u)	/* the sum of pg_subFigW (gen.py SUB_FIGURE_TOTAL) */

/* spec §4.2, §8 (gen.py _sub()): the style's bars off the kick (style 8: one bar of figures, draw 3,
 * den keeps its best ranked hits); the last hit after each kick before the next at 127, the others
 * at lev; var's second note on quarters 2 and 4 */
static void pg_sub(pg_ctx_t *c, const PatternGenSettings *s, uint16_t kick, PatternGenTrack *io)
{
	uint16_t bar[2];
	if (s->sty == 8u)
	{
		uint16_t h = 0;
		for (unsigned q = 0; q < 4u; q++)
		{
			unsigned r = pg_rand(c, PG_SUB_FIG_TOTAL), f = 0;
			while (r >= pg_subFigW[f])
				r -= pg_subFigW[f++];
			h |= (uint16_t)(pg_subFig[f] << (4u * q));
		}
		h &= (uint16_t)~kick;
		if (s->den)
		{
			const unsigned m = pg_rank(c, c->key, pg_one, h, 16);
			h = 0;
			for (unsigned i = 0; (i < s->den) && (i < m); i++)
				h |= (uint16_t)(1u << c->order[i]);
		}
		bar[0] = bar[1] = h;
	}
	else
	{
		bar[0] = (uint16_t)(pg_subBar[s->sty] & ~kick);
		bar[1] = (uint16_t)(((s->sty == 7u) ? PG_SUB_BAR_B : pg_subBar[s->sty]) & ~kick);
	}
	uint64_t hits = 0, kicks = 0, loud = 0;
	for (unsigned b = 0; b < PG_BARS; b++)
	{
		hits |= (uint64_t)bar[b & 1u] << (16u * b);
		kicks |= (uint64_t)kick << (16u * b);
	}
	for (unsigned k = 0; k < PG_STEPS; k++)	/* back from each kick to the one before it */
	{
		if (!((kicks >> k) & 1u))
			continue;
		for (unsigned i = (k + PG_STEPS - 1u) % PG_STEPS; !((kicks >> i) & 1u); i = (i + PG_STEPS - 1u) % PG_STEPS)
			if ((hits >> i) & 1u)
			{
				loud |= (uint64_t)1 << i;
				break;
			}
	}
	for (unsigned i = 0; i < PG_STEPS; i++)
		if ((hits >> i) & 1u)
			pg_hit(c, io, i, ((loud >> i) & 1u) ? 127 : patternGen_dbVel(s->lev), PG_PROB_ALWAYS,
				((s->var != 0) && (i & 4u)) ? pg_noteOf(c->note + s->var) : c->note, 0);
}

/* spec §4.3, §8, §9 (gen.py _prc()): bar 1's den accents by rank off the kick, bar 2 with bar 1's
 * top-ranked accent moved to the best ranked free place of keys 16-31 off the kick (§9: no draw for
 * it, so den never changes which accent moves), bars 3-4 as 1-2 (style 0: each bar's own accents as
 * the current track plays, in all 4 bars, which pg_frame() kept, §9); ghosts on the step before an
 * accent in its bar (none before step 1, §9), off accents and kick, ranked by keys 0-15, at lev + the
 * offset of the place in bar 1 or 2; var +n: n at 2:2 in bar 4, its own keys (draw 3) and steps 13-16
 * x4; var -n: n of bar 4's accents at 1:2, not on style 0 */
static void pg_prc(pg_ctx_t *c, const PatternGenSettings *s, uint16_t kick, PatternGenTrack *io)
{
	/* gen.py PRC_WEIGHTS by the 16th of the quarter; style 3: 8 on steps 4 7 12 15, else 1 */
	static const uint8_t wq[5][4] = { { 1, 1, 1, 1 }, { 0, 4, 2, 4 }, { 0, 2, 6, 2 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 } };
	uint8_t w[16];
	for (unsigned p = 0; p < 16u; p++)
		w[p] = ((s->sty == 3u) && ((0x4848u >> p) & 1u)) ? 8 : wq[s->sty][p & 3u];
	for (unsigned p = 0; p < 16u; p++)	/* draw 3: var's keys, nothing else (§9) */
		c->key[32u + p] = (uint16_t)pg_rand(c, 65536u);
	uint16_t acc[PG_BARS], ghost = 0;
	if (s->sty == 0u)
		memcpy(acc, c->acc, sizeof acc);
	else
	{
		uint16_t a1 = 0, a2;
		const unsigned n = pg_rank(c, c->key, w, (uint16_t)~kick, 16);
		for (unsigned i = 0; (i < s->den) && (i < n); i++)
			a1 |= (uint16_t)(1u << c->order[i]);
		a2 = a1;
		if (a1)	/* LICHOMAT v0.8 §9: the top-ranked accent, c->order[0] until the next ranking */
		{
			const unsigned top = c->order[0];
			if (pg_rank(c, &c->key[16], w, (uint16_t)~(a1 | kick), 16))
				a2 = (uint16_t)((a1 & ~(1u << top)) | (1u << c->order[0]));
		}
		acc[0] = acc[2] = a1;
		acc[1] = acc[3] = a2;
		for (unsigned b = 0; b < PG_BARS; b++)
			for (unsigned p = 0; p < 16u; p++)
				if ((acc[b] >> p) & 1u)
					pg_hit(c, io, 16u * b + p, 127, PG_PROB_ALWAYS, c->note, 0);
	}
	const unsigned ghosts = (((unsigned)s->den + 1u) / 2u < 3u) ? ((unsigned)s->den + 1u) / 2u : 3u;
	for (unsigned b = 0; b < PG_BARS; b++)
	{
		const uint16_t places = (uint16_t)(acc[b] >> 1);	/* LICHOMAT v0.8 §9: none before step 1 */
		const unsigned m = pg_rank(c, c->key, pg_one, (uint16_t)(places & ~(acc[b] | kick)), 16);
		ghost = 0;
		for (unsigned i = 0; (i < ghosts) && (i < m); i++)
		{
			const unsigned q = c->order[i];
			pg_hit(c, io, 16u * b + q, patternGen_dbVel(s->lev + c->off[16u * (b & 1u) + q]), PG_PROB_ALWAYS, c->note, 0);
			ghost |= (uint16_t)(1u << q);
		}
	}
	if (s->var > 0)	/* ghost: bar 4's */
	{
		uint8_t late[16];
		for (unsigned p = 0; p < 16u; p++)
			late[p] = (uint8_t)(w[p] * ((p >= 12u) ? 4u : 1u));
		const unsigned m = pg_rank(c, &c->key[32], late, (uint16_t)~(acc[3] | ghost | kick), 16);
		for (unsigned i = 0; (i < (unsigned)s->var) && (i < m); i++)
			pg_hit(c, io, 48u + c->order[i], 127, PG_COND_2_2, c->note, 0);
	}
	else if ((s->var < 0) && (s->sty != 0u))
	{
		const unsigned m = pg_rank(c, &c->key[32], pg_one, acc[3], 16);
		for (unsigned i = 0; (i < (unsigned)-s->var) && (i < m); i++)
			io->step[48u + c->order[i]].prob = PG_COND_1_2;
	}
}

/* spec §4.4, §8, §9, G19 (gen.py _rar()): everything at 127, den 0 nothing; style 1 bar 4's step 1
 * and den 3+ the stutter on its steps 9-12 (flam x4); style 2 step 1; style 3 min(den, 8) of the 64
 * steps off the kick by their keys (draw 3), bars 1 and 4 weigh 2, at 25 %; style 4 steps 1 (1:2) and
 * 49 (4:4) and min(den - 2, 8) of the scatter (§9) */
static void pg_rar(pg_ctx_t *c, const PatternGenSettings *s, uint16_t kick, PatternGenTrack *io)
{
	const unsigned den = s->den;
	if ((s->sty == 3u) || (s->sty == 4u))
		for (unsigned i = 0; i < PG_STEPS; i++)
			c->key[i] = (uint16_t)pg_rand(c, 65536u);
	if (den == 0u)
		return;
	if (s->sty == 1u)
	{
		pg_hit(c, io, 48, 127, (den == 1u) ? PG_COND_4_4 : PG_COND_2_2, c->note, 0);
		for (unsigned i = 56; (den >= 3u) && (i < 60u); i++)
			pg_hit(c, io, i, 127, (den == 3u) ? PG_COND_4_4 : PG_COND_2_2, c->note, PG_FLAM_X4);
	}
	else if (s->sty == 2u)
		pg_hit(c, io, 0, 127, (den == 1u) ? PG_COND_1_4 : (den <= 3u) ? PG_COND_1_2 : PG_PROB_ALWAYS, c->note, 0);
	else
	{
		uint64_t fixed = 0, allowed = 0;
		unsigned n = (den < 8u) ? den : 8u;
		if (s->sty == 4u)
		{
			pg_hit(c, io, 0, 127, PG_COND_1_2, c->note, 0);
			pg_hit(c, io, 48, 127, PG_COND_4_4, c->note, 0);
			fixed = ((uint64_t)1 << 0) | ((uint64_t)1 << 48);
			n = (den <= 2u) ? 0u : (den - 2u < 8u) ? den - 2u : 8u;
		}
		uint8_t w[PG_STEPS];
		for (unsigned i = 0; i < PG_STEPS; i++)
		{
			w[i] = ((i < 16u) || (i >= 48u)) ? 2 : 1;
			if (!((kick >> (i & 15u)) & 1u) && !((fixed >> i) & 1u))
				allowed |= (uint64_t)1 << i;
		}
		const unsigned m = pg_rank(c, c->key, w, allowed, PG_STEPS);
		for (unsigned i = 0; (i < n) && (i < m); i++)
			pg_hit(c, io, c->order[i], 127, PG_PROB_RARE, c->note, 0);
	}
}

/* spec §4.5, §8, G21 (gen.py _prp()): track src as it plays (a track shorter than 64 steps repeated);
 * an accent is a step on at 64 or above. Style 1: its steps shf later with their velocity, prob (no
 * slide flag) and note + not; style 2: den at 127 a bar where it is off, by rank; style 3: a ghost
 * (lev) on the step before each accent where it is off; style 4: each accent shf later as a ghost
 * (lev); styles 3-4 take the accent's prob without the slide flag */
static void pg_prp(pg_ctx_t *c, const PatternGenSettings *s, const PatternGenSource *src, PatternGenTrack *io)
{
	/* gen.py PRP_FILL_WEIGHTS by the 16th of the quarter: quarter 1, 2nd and 4th 4, offbeat 3 */
	static const uint8_t wq[4] = { 1, 4, 3, 4 };
	const unsigned len = src ? pg_len(src->length) : 16u;
	const unsigned shf = (unsigned)(s->shf + PG_STEPS);	/* a positive modulo of a shf of -8..+8 */
	uint64_t on = 0, accent = 0;
	for (unsigned i = 0; src && (i < PG_STEPS); i++)
		if (pg_isOn(src->on, i % len))
		{
			on |= (uint64_t)1 << i;
			if ((src->step[i % len].volume & 0x7Fu) >= PG_ACCENT_MIN)
				accent |= (uint64_t)1 << i;
		}
	if (s->sty == 2u)
	{
		uint8_t w[16];
		for (unsigned p = 0; p < 16u; p++)
			w[p] = wq[p & 3u];
		for (unsigned b = 0; b < PG_BARS; b++)
		{
			const unsigned m = pg_rank(c, c->key, w, ~(on >> (16u * b)), 16);
			for (unsigned i = 0; (i < s->den) && (i < m); i++)
				pg_hit(c, io, 16u * b + c->order[i], 127, PG_PROB_ALWAYS, c->note, 0);
		}
		return;
	}
	for (unsigned i = 0; i < PG_STEPS; i++)
	{
		if (!((on >> i) & 1u))	/* every style below writes for a step that is on */
			continue;
		const Step *x = &src->step[i % len];
		const unsigned before = (i + PG_STEPS - 1u) % PG_STEPS, later = (i + shf) % PG_STEPS;
		if (s->sty == 1u)
			pg_hit(c, io, later, (uint8_t)(x->volume & 0x7Fu), (uint8_t)(x->prob & STEP_PROB_MASK),
				pg_noteOf((int)x->note + s->note), 0);
		else if ((s->sty == 3u) && ((accent >> i) & 1u) && !((on >> before) & 1u))
			pg_hit(c, io, before, patternGen_dbVel(s->lev), (uint8_t)(x->prob & STEP_PROB_MASK), c->note, 0);
		else if ((s->sty == 4u) && ((accent >> i) & 1u))
			pg_hit(c, io, later, patternGen_dbVel(s->lev), (uint8_t)(x->prob & STEP_PROB_MASK), c->note, 0);
	}
}

uint8_t patternGen_generate(const PatternGenSettings *in, uint8_t track, const PatternGenSource *src, PatternGenTrack *io)
{
	if (!in || !io || (track >= PG_TRACKS))
		return 0;
	PatternGenSettings s = patternGen_checked(*in);
	if ((s.typ == PG_TYPE_OFF) || ((s.typ == PG_TYPE_PRP) && (s.src == track)))
		return 0;	/* gen.py: the current track as it is */
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
	const uint16_t kick = pg_kick(&s, track, src);
	const int own = ((s.typ == PG_TYPE_HAT) || (s.typ == PG_TYPE_PRC)) && (s.sty == 0u);
	pg_frame(&c, io, own);
	if (own && !(c.acc[0] | c.acc[1] | c.acc[2] | c.acc[3]))
		s.sty = 1;	/* LICHOMAT v0.8 §9: style 0 without an accent is style 1, seeded with style 0 */
	switch (s.typ)	/* each draws its draw 3 first */
	{
	case PG_TYPE_HAT:	pg_hat(&c, &s, io); break;
	case PG_TYPE_SUB:	pg_sub(&c, &s, kick, io); break;
	case PG_TYPE_PRC:	pg_prc(&c, &s, kick, io); break;
	case PG_TYPE_RAR:	pg_rar(&c, &s, kick, io); break;
	default:		pg_prp(&c, &s, src, io); break;
	}
	return (uint8_t)(PG_WROTE | pg_soft(&c, io));
}
