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

uint8_t patternGen_generate(const PatternGenSettings *in, uint8_t track, const PatternGenSource *src, PatternGenTrack *io)
{
	if (!in || !io || (track >= PG_TRACKS))
		return 0;
	const PatternGenSettings s = patternGen_checked(*in);
	if ((s.typ == PG_TYPE_OFF) || ((s.typ == PG_TYPE_PRP) && (s.src == track)))
		return 0;	/* gen.py: the current track as it is */
	(void)src;
	return 0;	/* hat next; Task 3: sub prc rar prp */
}
