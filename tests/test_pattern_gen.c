/*
 * test_pattern_gen.c
 *
 * LICHOMAT v0.8 (plan v0.8 Tasks 2-3): the pattern generator, Core/Src/Soundengine/Sequencer/PatternGen.c.
 * - pgen_core: the dB table against the formula, the random generator against a 64-bit reference
 *   written from spec §5, the settings, rules of each type (§4, §8);
 * - pgen_vectors: tools/gen/vectors.json (the prototype tools/gen/gen.py) byte for byte (G14,
 *   G15): its dB table, its random draws and its cases, and from Task 3 the vectors' coverage of
 *   the settings.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Soundengine/Sequencer/PatternGen.h"
#include "seqc_test.h"

/** a track as seq_clearTrack() leaves it (gen.py Track()): 127, 100 %, note 60, no p-locks, shift 7,
 * off, 16 steps */
static void cleared(PatternGenTrack *t)
{
	memset(t, 0, sizeof *t);
	for (unsigned s = 0; s < PG_STEPS; s++)
	{
		t->step[s] = (Step){ 127, 100, 60, 0xFF, 0, 0xFF, 0 };
		t->timing[s].shift = 7;
	}
	t->length = 16;
}
static int isOn(const PatternGenTrack *t, unsigned s) { return (t->on[s >> 4] >> (s & 15u)) & 1u; }
static unsigned bits(uint32_t x) { unsigned n = 0; for (; x; x &= x - 1u) n++; return n; }
/** step s's fine nibble as stored, -8..+7 */
static int nibble(const PatternGenTrack *t, unsigned s)
{
	const int n = (t->fine[s >> 1] >> ((s & 1u) * 4u)) & 15;
	return (n & 8) ? n - 16 : n;
}
static PatternGenSettings hat(uint8_t sty, uint8_t den, int8_t lev, int8_t var, uint8_t sed)
{
	return (PatternGenSettings){ PG_TYPE_HAT, sty, den, lev, var, 0, sed, 0, 0 };
}
/** track 5 generated from s out of a cleared track */
static uint8_t gen(PatternGenSettings s, PatternGenTrack *t, const PatternGenSource *src)
{
	cleared(t);
	return patternGen_generate(&s, 5, src, t);
}

TEST(pgen_core_db_table)
{
	for (int db = 0; db >= -30; db--)
	{
		const long v = lround(127.0 * pow(10.0, db / 20.0));
		ASSERT_EQ(patternGen_dbVel(db), v < 1 ? 1 : v);
	}
	ASSERT_EQ(patternGen_dbVel(-15), 23); ASSERT_EQ(patternGen_dbVel(-3), 90);	/* spec §2, §4.2 */
	ASSERT_EQ(patternGen_dbVel(2), 127); ASSERT_EQ(patternGen_dbVel(-33), 4);	/* spec §8: clamped */
}

/* spec §5 in 64-bit arithmetic */
static uint32_t refSeed(unsigned sed, unsigned typ, unsigned sty, unsigned trk)
{
	const unsigned b[4] = { sed, typ, sty, trk };
	unsigned long long x = 2166136261ull;
	for (int i = 0; i < 4; i++)
		x = ((x ^ b[i]) * 16777619ull) % 4294967296ull;
	return x ? (uint32_t)x : 1u;
}
static uint32_t refStep(uint32_t v)
{
	unsigned long long x = v;
	x = (x ^ (x << 13)) & 0xFFFFFFFFull;
	x ^= x >> 17;
	return (uint32_t)((x ^ (x << 5)) & 0xFFFFFFFFull);
}

TEST(pgen_core_random)
{
	for (unsigned sed = 0; sed <= PG_SED_MAX; sed++)
		for (unsigned typ = 0; typ < PG_TYPES; typ++)
			for (unsigned sty = 0; sty <= PG_STY_MAX; sty++)
				for (unsigned trk = 0; trk < PG_TRACKS; trk++)
				{
					const PatternGenSettings s = { (uint8_t)typ, (uint8_t)sty, 0, 0, 0, 0, (uint8_t)sed, 0, 0 };
					ASSERT_TRUE(refSeed(sed, typ, sty, trk) != 0u);	/* "x = 0 -> 1" is a guard only */
					ASSERT_EQ(patternGen_seed(&s, (uint8_t)trk), refSeed(sed, typ, sty, trk));
				}
	uint32_t a = 1u, b = 1u;
	for (int i = 0; i < 100000; i++)
	{
		a = patternGen_step(a);
		b = refStep(b);
		ASSERT_EQ(a, b);
	}
	/* den lev var not src shf are not in the seed (§5: den and lev do not reshuffle) */
	const PatternGenSettings x = { 1, 2, 3, -4, 1, 5, 6, 2, 3 }, y = { 1, 2, 16, -30, -4, 24, 6, 6, -8 };
	ASSERT_EQ(patternGen_seed(&x, 4), patternGen_seed(&y, 4));
}

TEST(pgen_core_settings)
{
	/* spec §3's table (gen.py apply_type_defaults()): typ set, sty den lev var not loaded, sed src shf kept */
	static const int8_t want[5][5] = { { 1, 7, -15, 1, 0 }, { 1, 0, -3, 0, -2 }, { 1, 3, -13, 1, 0 },
		{ 1, 2, 0, 0, 0 }, { 1, 4, -15, 0, 0 } };
	for (uint8_t typ = PG_TYPE_HAT; typ < PG_TYPES; typ++)
	{
		PatternGenSettings s = { PG_TYPE_OFF, 8, 16, -30, -4, 24, 42, 3, -5 };
		patternGen_typeDefaults(typ, &s);
		const int8_t *d = want[typ - 1];
		const PatternGenSettings w = { typ, (uint8_t)d[0], (uint8_t)d[1], d[2], d[3], d[4], 42, 3, -5 };
		ASSERT_EQ(memcmp(&s, &w, sizeof s), 0);
	}
	PatternGenSettings o = { PG_TYPE_SUB, 3, 9, -7, 2, 5, 42, 3, -5 }, o2 = o;	/* --- switches it off only */
	patternGen_typeDefaults(PG_TYPE_OFF, &o);
	o2.typ = PG_TYPE_OFF;
	ASSERT_EQ(memcmp(&o, &o2, sizeof o), 0);
	patternGen_typeDefaults(PG_TYPES, &o);	/* no such type: nothing */
	ASSERT_EQ(memcmp(&o, &o2, sizeof o), 0);
	patternGen_typeDefaults(PG_TYPE_HAT, NULL);

	/* gen.py Settings.clamped(): each field clamped into its range, sty into the type's styles (spec §8) */
	static const struct { PatternGenSettings in, out; } clamp[] = {
		{ { 6, 9, 17, -31, 5, 25, 100, 7, 9 }, { 5, 4, 16, -30, 4, 24, 99, 6, 8 } },
		{ { 255, 255, 255, -128, -128, -128, 255, 255, -128 }, { 5, 4, 16, -30, -4, -24, 99, 6, -8 } },
		{ { 0, 9, 0, 1, -5, -25, 0, 0, -9 }, { 0, 8, 0, 0, -4, -24, 0, 0, -8 } },
		{ { 0, 0, 0, 127, 127, 127, 0, 0, 127 }, { 0, 0, 0, 0, 4, 24, 0, 0, 8 } },
		{ { 1, 5, 7, -15, 1, 0, 0, 0, 0 }, { 1, 4, 7, -15, 1, 0, 0, 0, 0 } },	/* hat 0..4 */
		{ { 1, 0, 7, -15, 1, 0, 0, 0, 0 }, { 1, 0, 7, -15, 1, 0, 0, 0, 0 } },
		{ { 2, 0, 0, -3, 0, -2, 0, 0, 0 }, { 2, 1, 0, -3, 0, -2, 0, 0, 0 } },	/* sub 1..8 */
		{ { 2, 8, 0, -3, 0, -2, 0, 0, 0 }, { 2, 8, 0, -3, 0, -2, 0, 0, 0 } },
		{ { 3, 0, 3, -13, 1, 0, 0, 0, 0 }, { 3, 0, 3, -13, 1, 0, 0, 0, 0 } },	/* prc 0..4 */
		{ { 3, 6, 3, -13, 1, 0, 0, 0, 0 }, { 3, 4, 3, -13, 1, 0, 0, 0, 0 } },
		{ { 4, 0, 2, 0, 0, 0, 0, 0, 0 }, { 4, 1, 2, 0, 0, 0, 0, 0, 0 } },	/* rar 1..4 */
		{ { 4, 8, 2, 0, 0, 0, 0, 0, 0 }, { 4, 4, 2, 0, 0, 0, 0, 0, 0 } },
		{ { 5, 0, 4, -15, 0, 0, 0, 0, 0 }, { 5, 1, 4, -15, 0, 0, 0, 0, 0 } },	/* prp 1..4 */
		{ { 5, 5, 4, -15, 0, 0, 0, 0, 0 }, { 5, 4, 4, -15, 0, 0, 0, 0, 0 } },
		{ { 5, 4, 16, -30, -4, -24, 99, 6, -8 }, { 5, 4, 16, -30, -4, -24, 99, 6, -8 } },	/* the edges stay */
		{ { 2, 1, 0, 0, 4, 24, 0, 0, 8 }, { 2, 1, 0, 0, 4, 24, 0, 0, 8 } },
	};
	for (unsigned i = 0; i < sizeof clamp / sizeof clamp[0]; i++)
	{
		const PatternGenSettings c = patternGen_checked(clamp[i].in);
		if (memcmp(&c, &clamp[i].out, sizeof c))
			printf("clamp row %u: typ %d sty %d den %d lev %d var %d not %d sed %d src %d shf %d\n", i, c.typ, c.sty,
				c.den, c.lev, c.var, c.note, c.sed, c.src, c.shf);
		ASSERT_EQ(memcmp(&c, &clamp[i].out, sizeof c), 0);
	}

	/* typ ---, prp from its own track, no track 7, no settings or track: 0 and nothing written */
	PatternGenTrack t, u;
	cleared(&t);
	t.on[1] = 0x00F0u;
	t.step[3].param1Nr = 5;
	memcpy(&u, &t, sizeof t);
	const PatternGenSettings none = { 0, 1, 7, -15, 1, 0, 0, 0, 0 }, h = hat(1, 7, -15, 0, 0);
	const PatternGenSettings own = { PG_TYPE_PRP, 1, 4, -15, 0, 0, 0, 5, 0 };
	const PatternGenSource src = { t.step, { 0x1111u, 0, 0, 0 }, 16 };
	ASSERT_EQ(patternGen_generate(&none, 5, &src, &t), 0); ASSERT_EQ(patternGen_generate(&own, 5, &src, &t), 0);
	ASSERT_EQ(patternGen_generate(&h, 7, NULL, &t), 0); ASSERT_EQ(patternGen_generate(NULL, 5, NULL, &t), 0);
	ASSERT_EQ(patternGen_generate(&h, 5, NULL, NULL), 0);
	ASSERT_EQ(memcmp(&t, &u, sizeof t), 0);
}

TEST(pgen_core_hat_layout)
{
	/* §4.1 style 1: 127 on 3 7 11 15, 7 ghosts at lev -3..+2 dB, 4 bars alike, 64 steps. §3, §8: every
	 * step is written clean, the off ones too (vel 127, 100 %), note 60 + not, no p-lock, flam 0,
	 * shift 7, fine 0 */
	for (uint8_t sed = 0; sed < 20; sed++)
	{
		PatternGenTrack t;
		cleared(&t);
		for (unsigned st = 0; st < PG_STEPS; st++)
		{
			t.step[st] = (Step){ 50, 0x80u | 30u, 50, 7, 3, 8, 4 };
			t.timing[st].flam = 2;
			t.timing[st].shift = 9;
			stepFine_set(t.fine, (uint8_t)st, -4);
		}
		t.on[1] = 0x0F0Fu;
		t.length = 40;
		PatternGenSettings s = hat(1, 7, -15, 0, sed);
		s.note = 3;
		ASSERT_EQ(patternGen_generate(&s, 5, NULL, &t), PG_WROTE | PG_SOFT);
		ASSERT_EQ(t.length, 64); ASSERT_EQ(t.on[0] & 0x4444u, 0x4444u); ASSERT_EQ(bits(t.on[0]), 11u);
		for (unsigned b = 1; b < PG_BARS; b++)
			ASSERT_EQ(t.on[b], t.on[0]);
		for (unsigned st = 0; st < PG_STEPS; st++)
		{
			const Step *x = &t.step[st];
			ASSERT_EQ(x->note, 63); ASSERT_EQ(x->prob, 100);
			ASSERT_EQ(x->param1Nr, 0xFF); ASSERT_EQ(x->param1Val, 0); ASSERT_EQ(x->param2Nr, 0xFF); ASSERT_EQ(x->param2Val, 0);
			ASSERT_EQ(t.timing[st].flam, 0); ASSERT_EQ(t.timing[st].shift, 7); ASSERT_EQ(nibble(&t, st), 0);
			if (!isOn(&t, st))
				ASSERT_EQ(x->volume, 127);
			else
				ASSERT_TRUE(((st & 3u) == 2u) ? x->volume == 127 : (x->volume >= patternGen_dbVel(-18) && x->volume <= patternGen_dbVel(-13)));
		}
	}
}

TEST(pgen_core_hat_density_level_and_var)
{
	/* §5: den k's ghosts are among den k+1's, lev moves none, den past the 12 free places is cut;
	 * §8: var +2 adds the next two of the ranking at 2:2, var -2 puts the last two of den's at 1:2 */
	for (uint8_t sed = 0; sed < 10; sed++)
	{
		uint16_t prev = 0;
		for (uint8_t den = 0; den <= PG_DEN_MAX; den++)
		{
			PatternGenTrack a, b;
			gen(hat(1, den, -15, 0, sed), &a, NULL);
			gen(hat(1, den, -27, 0, sed), &b, NULL);
			ASSERT_EQ(a.on[0], b.on[0]); ASSERT_EQ(a.on[0] & prev, prev);
			ASSERT_EQ(bits(a.on[0]), 4u + (den < 12u ? den : 12u));
			prev = a.on[0];
		}
		PatternGenTrack z, p, m, n3;
		gen(hat(1, 5, -15, 0, sed), &z, NULL); gen(hat(1, 5, -15, 2, sed), &p, NULL); gen(hat(1, 5, -15, -2, sed), &m, NULL);
		gen(hat(1, 3, -15, 0, sed), &n3, NULL);
		unsigned c22 = 0, c12 = 0;
		for (unsigned st = 0; st < 16; st++)
		{
			ASSERT_TRUE(!isOn(&z, st) || (isOn(&p, st) && p.step[st].prob == 100));
			c22 += !isOn(&z, st) && isOn(&p, st) && p.step[st].prob == 102;
			c12 += isOn(&m, st) && m.step[st].prob == 101;
			if (isOn(&m, st) && m.step[st].prob == 101)	/* the last two of den's 5: not among den 3's */
				ASSERT_FALSE(isOn(&n3, st));
		}
		ASSERT_EQ(bits(p.on[0]), bits(z.on[0]) + 2u); ASSERT_EQ(c22, 2u); ASSERT_EQ(c12, 2u); ASSERT_EQ(m.on[0], z.on[0]);
		PatternGenTrack full;	/* var +4 past the free places: what is left */
		gen(hat(1, 10, -15, 4, sed), &full, NULL);
		ASSERT_EQ(full.on[0], 0xFFFFu);
		PatternGenTrack all;	/* var -4 on den 2: both at 1:2 */
		gen(hat(1, 2, -15, -4, sed), &all, NULL);
		c12 = 0;
		for (unsigned st = 0; st < 16; st++)
			c12 += isOn(&all, st) && all.step[st].prob == 101;
		ASSERT_EQ(c12, 2u); ASSERT_EQ(bits(all.on[0]), 6u);
	}
}

TEST(pgen_core_hat_rank_ties)
{
	/* §5: a tie of keys goes to the lower place. Track 2 (index 1), found with gen.py: style 3 sed 13
	 * ties places 1 and 13 (weight 3) at ranks 7 and 8; style 0 sed 56 on a track with one accent, on
	 * step 3 (§9: without one it would be style 1), ties 6 (weight 1) and 13 (weight 4) at ranks 14
	 * and 15 */
	static const struct { uint8_t sty, den, sed, lower, higher; } tie[] = { { 3, 7, 13, 1, 13 }, { 0, 14, 56, 6, 13 } };
	for (unsigned i = 0; i < sizeof tie / sizeof tie[0]; i++)
	{
		PatternGenTrack t;
		for (uint8_t den = tie[i].den; den <= tie[i].den + 1u; den++)
		{
			cleared(&t);
			t.on[0] = (tie[i].sty == 0u) ? 1u << 2 : 0u;
			const PatternGenSettings s = hat(tie[i].sty, den, -15, 0, tie[i].sed);
			ASSERT_EQ(patternGen_generate(&s, 1, NULL, &t), PG_WROTE | PG_SOFT);
			for (unsigned o = 0; o < PG_STEPS; o += 16)
			{
				ASSERT_TRUE(isOn(&t, o + tie[i].lower)); ASSERT_EQ(isOn(&t, o + tie[i].higher), den > tie[i].den);
			}
		}
	}
}

TEST(pgen_core_hat_style0_keeps_own_accents)
{
	/* §3, G6: step 2 (64, a p-lock, fine -4) stays and is played in bars 2-4 too (a 16-step track
	 * repeats); 13 at 63 is no accent; den 16 fills every other place with a ghost */
	PatternGenTrack t;
	cleared(&t);
	t.on[0] = (1u << 1) | (1u << 12);
	t.step[1].volume = 64; t.step[1].param1Nr = 9; t.step[12].volume = 63;
	stepFine_set(t.fine, 1, -4);
	const PatternGenSettings s = hat(0, 16, -15, 0, 5);
	ASSERT_EQ(patternGen_generate(&s, 5, NULL, &t), PG_WROTE | PG_SOFT);
	for (unsigned o = 0; o < PG_STEPS; o += 16)
	{
		ASSERT_EQ(t.step[o + 1].volume, 64); ASSERT_EQ(t.step[o + 1].param1Nr, 9); ASSERT_EQ(stepFine_get(t.fine, (uint8_t)(o + 1)), -4);
		ASSERT_TRUE(t.step[o + 12].volume < 64); ASSERT_EQ(t.on[o >> 4], 0xFFFFu);
	}
}

TEST(pgen_core_hat_style0_reads_as_played)
{
	/* §8: the current track is read as it plays. 12 steps repeat to 64: the accent on step 1 (100,
	 * p-lock, flam, fine +3 and a raw nibble -7 on its neighbour) plays on 1 13 25 37 49 61, a
	 * different place in each bar; the ghost on 6 (30) is no accent; steps 17-32 past the length do
	 * not play. den 0: nothing written, so no PG_SOFT */
	PatternGenTrack t;
	cleared(&t);
	t.length = 12;
	t.on[0] = 1u | (1u << 5);
	t.on[1] = 0xFFFFu;
	t.step[0] = (Step){ 100, 0x80u | 102u, 61, 0xFF, 0, 12, 34 };
	t.timing[0].flam = 1;
	t.timing[0].shift = 8;
	t.fine[0] = 0x93u;	/* step 0: +3; step 1 (off): the raw nibble 9, -7 */
	t.step[5].volume = 30;
	const Step acc = t.step[0];
	const PatternGenSettings s = hat(0, 0, -15, 0, 0);
	ASSERT_EQ(patternGen_generate(&s, 5, NULL, &t), PG_WROTE);
	ASSERT_EQ(t.length, 64);
	ASSERT_EQ(t.on[0], (1u << 0) | (1u << 12)); ASSERT_EQ(t.on[1], 1u << 8); ASSERT_EQ(t.on[2], 1u << 4);
	ASSERT_EQ(t.on[3], (1u << 0) | (1u << 12));
	for (unsigned st = 0; st < PG_STEPS; st++)
	{
		if (!isOn(&t, st))
		{
			ASSERT_EQ(t.step[st].volume, 127); ASSERT_EQ(t.step[st].param2Nr, 0xFF); ASSERT_EQ(nibble(&t, st), 0);
			ASSERT_EQ(t.timing[st].shift, 7);
			continue;
		}
		ASSERT_EQ(st % 12u, 0u);
		ASSERT_EQ(memcmp(&t.step[st], &acc, sizeof acc), 0);
		ASSERT_EQ(t.timing[st].flam, 1); ASSERT_EQ(t.timing[st].shift, 8); ASSERT_EQ(nibble(&t, st), 3);
	}

	/* 64 steps, bar by bar (§8): an accent on place b of bar b; den 15 fills each bar around its own */
	cleared(&t);
	t.length = 64;
	for (unsigned b = 0; b < PG_BARS; b++)
	{
		t.on[b] = (uint16_t)(1u << b);
		t.step[16u * b + b] = (Step){ 80, 100, 60, 40, (uint8_t)b, 0xFF, 0 };
		stepFine_set(t.fine, (uint8_t)(16u * b + b), -5);
	}
	const PatternGenSettings f = hat(0, 15, -15, 0, 3);
	ASSERT_EQ(patternGen_generate(&f, 5, NULL, &t), PG_WROTE | PG_SOFT);
	for (unsigned b = 0; b < PG_BARS; b++)
	{
		ASSERT_EQ(t.on[b], 0xFFFFu);
		for (unsigned p = 0; p < 16u; p++)
		{
			const Step *x = &t.step[16u * b + p];
			if (p == b)
			{
				ASSERT_EQ(x->volume, 80); ASSERT_EQ(x->param1Nr, 40); ASSERT_EQ(x->param1Val, b); ASSERT_EQ(nibble(&t, 16u * b + p), -5);
			}
			else
			{
				ASSERT_TRUE(x->volume < 64); ASSERT_EQ(x->param1Nr, 0xFF); ASSERT_EQ(nibble(&t, 16u * b + p), 0);
			}
		}
	}
}

TEST(pgen_core_style0_keeps_raw_fine)
{
	/* §3: style 0 keeps the user's accents byte for byte, the fine nibble as stored too, where
	 * stepFine_get() would read something else: the raw 0x9 (-7, out of -6..+5, read as 0) on step 3,
	 * a low nibble, and 0xE (-2) on step 8, a high one, of a 16-step track, in all 4 bars; hat and prc */
	static const uint8_t typ[2] = { PG_TYPE_HAT, PG_TYPE_PRC };
	for (unsigned i = 0; i < 2u; i++)
	{
		PatternGenTrack t;
		cleared(&t);
		t.on[0] = (1u << 2) | (1u << 7);
		t.fine[1] = 0x09u;
		t.fine[3] = 0xE0u;
		const PatternGenSettings s = { typ[i], 0, 4, -15, 0, 0, 0, 0, 0 };
		ASSERT_EQ(patternGen_generate(&s, 5, NULL, &t) & PG_WROTE, PG_WROTE);
		for (unsigned b = 0; b < PG_BARS; b++)
		{
			ASSERT_TRUE(isOn(&t, 16u * b + 2u)); ASSERT_TRUE(isOn(&t, 16u * b + 7u));
			ASSERT_EQ(t.fine[8u * b + 1u], 0x09u); ASSERT_EQ(t.fine[8u * b + 3u], 0xE0u);
		}
	}
}

TEST(pgen_core_types)
{
	/* the spec's own words, one rule a type; the vectors check the rest */
	PatternGenTrack t, k, u;
	static const uint16_t bar[8] = { 0, 0x8888u, 0xCCCCu, 0xEEEEu, 0x4444u, 0x4C4Cu, 0x40C0u, 0x0CCCu };
	for (uint8_t sty = 1; sty <= 7; sty++)	/* sub §4.2 with the default kick; style 7's second bar */
	{
		gen((PatternGenSettings){ PG_TYPE_SUB, sty, 0, -3, 0, -2, 0, 5, 0 }, &t, NULL);
		ASSERT_EQ(t.on[0], bar[sty]); ASSERT_EQ(t.on[1], sty == 7 ? 0xECCCu : bar[sty]); ASSERT_EQ(t.on[3], t.on[1]);
	}
	static const struct { uint8_t sty, den, st, prob; } one[] = {	/* rar §4.4: 4:4 109, 2:2 102, 1:4 106, 1:2 101 */
		{ 1, 1, 48, 109 }, { 1, 2, 48, 102 }, { 2, 1, 0, 106 }, { 2, 3, 0, 101 }, { 2, 4, 0, 100 } };
	for (unsigned i = 0; i < sizeof one / sizeof one[0]; i++)
	{
		ASSERT_EQ(gen((PatternGenSettings){ PG_TYPE_RAR, one[i].sty, one[i].den, 0, 0, 0, 0, 5, 0 }, &t, NULL), PG_WROTE);
		ASSERT_EQ(bits(t.on[0]) + bits(t.on[1]) + bits(t.on[2]) + bits(t.on[3]), 1u);
		ASSERT_TRUE(isOn(&t, one[i].st)); ASSERT_EQ(t.step[one[i].st].prob, one[i].prob);
	}
	gen((PatternGenSettings){ PG_TYPE_RAR, 1, 4, 0, 0, 0, 0, 5, 0 }, &t, NULL);	/* G19: the stutter, x4 */
	ASSERT_EQ(t.on[3], 0x0F01u); ASSERT_EQ(t.timing[57].flam, 3); ASSERT_EQ(t.step[57].prob, 102);
	/* prp §4.5: from its own track nothing; style 1: the source (1 at 2:2, 6 at 40) 5 later, note + 3 */
	cleared(&k);
	k.on[0] = (1u << 0) | (1u << 5);
	k.step[0].prob = 102;
	k.step[5].volume = 40;
	const PatternGenSource src = { k.step, { k.on[0], 0, 0, 0 }, 0 };
	const PatternGenSettings own = { PG_TYPE_PRP, 1, 3, -15, 0, 3, 0, 5, 5 };
	cleared(&t);
	memcpy(&u, &t, sizeof t);
	ASSERT_EQ(patternGen_generate(&own, 5, &src, &t), 0); ASSERT_EQ(memcmp(&t, &u, sizeof t), 0);
	ASSERT_EQ(gen((PatternGenSettings){ PG_TYPE_PRP, 1, 3, -15, 0, 3, 0, 0, 5 }, &t, &src), PG_WROTE | PG_SOFT);
	for (unsigned b = 0; b < PG_BARS; b++)
		ASSERT_EQ(t.on[b], (1u << 5) | (1u << 10));
	ASSERT_EQ(t.step[5].prob, 102); ASSERT_EQ(t.step[10].volume, 40); ASSERT_EQ(t.step[10].note, 63);
}

TEST(pgen_core_soft_below_127)
{
	/* §3: vol goes on for a written step below 127. prp style 1 copies the source's velocity (bits
	 * 0-6): 126 is soft; 127 and 255 (the legacy bit 7, played as 127) are not */
	static const struct { uint8_t vel, copied, ret; } c[] = {
		{ 126, 126, PG_WROTE | PG_SOFT }, { 127, 127, PG_WROTE }, { 255, 127, PG_WROTE } };
	for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++)
	{
		PatternGenTrack k, t;
		cleared(&k);
		k.on[0] = 1u << 2;
		k.step[2].volume = c[i].vel;
		const PatternGenSource src = { k.step, { k.on[0], 0, 0, 0 }, 16 };
		ASSERT_EQ(gen((PatternGenSettings){ PG_TYPE_PRP, 1, 3, -15, 0, 0, 0, 0, 0 }, &t, &src), c[i].ret);
		ASSERT_EQ(t.on[0], 1u << 2); ASSERT_EQ(t.step[2].volume, c[i].copied);
	}
}

/** k cleared with steps on at the bits of bar 1, as a 16-step source */
static PatternGenSource source(PatternGenTrack *k, uint16_t bar1)
{
	cleared(k);
	k->on[0] = bar1;
	return (PatternGenSource){ k->step, { bar1, 0, 0, 0 }, 16 };
}

TEST(pgen_core_kick_as_played)
{
	/* §4.2, §8: the kick is bar 1 of track src as it plays, read through sub style 2 (..xx a quarter,
	 * never on a kick). 6 steps with step 1 on (and step 10, past the length) play on 1 7 13; a kick on
	 * 3 (track 1, generating track 2); src on the track itself, an empty track or none: 1 5 9 13 */
	PatternGenTrack t, k;
	PatternGenSource src = source(&k, (1u << 0) | (1u << 9));
	src.length = 6;
	PatternGenSettings s = { PG_TYPE_SUB, 2, 0, -3, 0, -2, 0, 0, 0 };
	cleared(&t);
	ASSERT_EQ(patternGen_generate(&s, 1, &src, &t), PG_WROTE | PG_SOFT);
	for (unsigned b = 0; b < PG_BARS; b++)
		ASSERT_EQ(t.on[b], 0xCC8Cu);
	src = source(&k, 1u << 2);
	cleared(&t);
	(void)patternGen_generate(&s, 1, &src, &t);
	ASSERT_EQ(t.on[0], 0xCCC8u); ASSERT_EQ(t.on[3], 0xCCC8u);
	for (unsigned i = 0; i < 3u; i++)	/* step 4 before the kick on 5 at 127, step 3 at lev */
	{
		s.src = (i == 0u) ? 1 : 0;
		src = source(&k, (i == 0u) ? (1u << 2) : 0u);
		cleared(&t);
		(void)patternGen_generate(&s, 1, (i == 2u) ? NULL : &src, &t);
		ASSERT_EQ(t.on[0], 0xCCCCu); ASSERT_EQ(t.step[3].volume, 127); ASSERT_EQ(t.step[2].volume, 90);
	}
}

TEST(pgen_core_prc_weights)
{
	/* §4.3's weights decide, found with gen.py on track 3, den 1: style 2 sed 5 takes the offbeat 3
	 * (6) and its ghost on 2; style 3 sed 0 with the kick on 2 6 10 14 takes step 4 (8), not step 1
	 * (1), and its ghost on 3; var +1 puts its 2:2 in bar 4 on steps 13-16, which weigh x4 there (at
	 * x1 each of these seeds would take a step before 13; spec §9's draws: var's keys come first) */
	PatternGenTrack t, k;
	const PatternGenSource kick = source(&k, 0x2222u);
	PatternGenSettings s = { PG_TYPE_PRC, 2, 1, -13, 0, 0, 5, 0, 0 };
	cleared(&t);
	ASSERT_EQ(patternGen_generate(&s, 2, NULL, &t), PG_WROTE | PG_SOFT);
	ASSERT_EQ(t.on[0], 0x0006u); ASSERT_EQ(t.step[2].volume, 127);
	s = (PatternGenSettings){ PG_TYPE_PRC, 3, 1, -13, 0, 0, 0, 0, 0 };
	cleared(&t);
	ASSERT_EQ(patternGen_generate(&s, 2, &kick, &t), PG_WROTE | PG_SOFT);
	ASSERT_EQ(t.on[0], 0x000Cu); ASSERT_EQ(t.step[3].volume, 127);
	static const struct { uint8_t sty, sed; uint16_t late; } v[] = {
		{ 1, 2, 1u << 15 }, { 2, 1, 1u << 14 }, { 3, 0, 1u << 14 }, { 4, 0, 1u << 15 } };
	for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
	{
		s = (PatternGenSettings){ PG_TYPE_PRC, v[i].sty, 1, -13, 1, 0, v[i].sed, 0, 0 };
		cleared(&t);
		(void)patternGen_generate(&s, 2, NULL, &t);
		uint16_t late = 0;
		for (unsigned p = 0; p < 16u; p++)
			late |= (uint16_t)((isOn(&t, 48u + p) && (t.step[48u + p].prob == 102)) ? 1u << p : 0u);
		ASSERT_EQ(late, v[i].late);
	}
}

/** the steps of bar b that are on at 127 and 100 %: prc's accents */
static uint16_t accents(const PatternGenTrack *t, unsigned b)
{
	uint16_t a = 0;
	for (unsigned p = 0; p < 16u; p++)
		if (isOn(t, 16u * b + p) && (t->step[16u * b + p].volume == 127) && (t->step[16u * b + p].prob == 100))
			a |= (uint16_t)(1u << p);
	return a;
}

TEST(pgen_core_prc_bar2_moves_the_top_accent)
{
	/* §9: bar 2 moves bar 1's accent with the highest ranking key, which is den 1's only accent,
	 * whatever den, to a free place off the kick; bars 3-4 are bars 1-2. Kicks 1 5 9 13, 2 6 10 14
	 * and 1 4 7 11 13 */
	static const uint16_t kicks[3] = { 0x1111u, 0x2222u, 0x1449u };
	PatternGenTrack t, k;
	for (unsigned ki = 0; ki < 3u; ki++)
	{
		const PatternGenSource kick = source(&k, kicks[ki]);
		for (uint8_t sty = 1; sty <= 4; sty++)
			for (uint8_t sed = 0; sed < 25; sed++)
			{
				uint16_t top = 0;
				for (uint8_t den = 1; den <= PG_DEN_MAX; den++)
				{
					const PatternGenSettings s = { PG_TYPE_PRC, sty, den, -13, 0, 0, sed, 0, 0 };
					cleared(&t);
					(void)patternGen_generate(&s, 2, &kick, &t);
					const uint16_t a1 = accents(&t, 0), a2 = accents(&t, 1);
					ASSERT_EQ(a1 & kicks[ki], 0u); ASSERT_EQ(a2 & kicks[ki], 0u);
					ASSERT_EQ(accents(&t, 2), a1); ASSERT_EQ(accents(&t, 3), a2);
					if (den == 1u)
						top = a1;
					ASSERT_EQ(a1 & top, top);
					if (bits(a1 ^ a2) == 0u)	/* no free place left: nothing moves */
						continue;
					ASSERT_EQ(bits(a1 ^ a2), 2u);
					if ((a1 & ~a2) != top)
						printf("kick 0x%04x sty %d sed %d den %d: 0x%04x moves, not 0x%04x\n", kicks[ki], sty, sed, den,
							a1 & ~a2, top);
					ASSERT_EQ(a1 & ~a2, top);
				}
			}
	}
}

TEST(pgen_core_prc_style0_bar_by_bar)
{
	/* §9: style 0 keeps each bar's own accents as the track plays, in all 4 bars (bars 3-4 are no
	 * copies of bars 1-2), and puts den 6's 3 ghosts before them, off the kick 1 5 9 13: accents on
	 * 3 11 | 7 | 15 | 3 10, ghosts on 2 10 | 6 | 14 | 2 (10's pickup 9 is a kick step) */
	static const uint16_t acc[PG_BARS] = { (1u << 2) | (1u << 10), 1u << 6, 1u << 14, (1u << 2) | (1u << 9) };
	static const uint16_t ghost[PG_BARS] = { (1u << 1) | (1u << 9), 1u << 5, 1u << 13, 1u << 1 };
	PatternGenTrack t;
	cleared(&t);
	t.length = 64;
	for (unsigned b = 0; b < PG_BARS; b++)
	{
		t.on[b] = acc[b];
		for (unsigned p = 0; p < 16u; p++)
			t.step[16u * b + p] = (Step){ (uint8_t)(100u + b), 100, 60, 30, (uint8_t)(16u * b + p), 0xFF, 0 };
	}
	const PatternGenSettings s = { PG_TYPE_PRC, 0, 6, -13, 0, 0, 4, 0, 0 };
	ASSERT_EQ(patternGen_generate(&s, 2, NULL, &t), PG_WROTE | PG_SOFT);
	for (unsigned b = 0; b < PG_BARS; b++)
	{
		ASSERT_EQ(t.on[b], acc[b] | ghost[b]);
		for (unsigned p = 0; p < 16u; p++)
			if ((acc[b] >> p) & 1u)
			{
				ASSERT_EQ(t.step[16u * b + p].volume, 100u + b); ASSERT_EQ(t.step[16u * b + p].param1Val, 16u * b + p);
			}
			else if ((ghost[b] >> p) & 1u)
			{
				ASSERT_TRUE(t.step[16u * b + p].volume < 64); ASSERT_EQ(t.step[16u * b + p].param1Nr, 0xFF);
			}
	}
}

TEST(pgen_core_prc_no_pickup_before_step_1)
{
	/* §9: an accent on step 1 of a bar gets no ghost (before, step 16 of the same bar got it). With the
	 * kick on 2 6 10 14, styles 3-4 put accents on step 1, and no ghost lands on a step 16; style 0
	 * with the user's accents on 1 and 7 (kick 1 5 9 13): the only ghost is on 6 */
	PatternGenTrack t, k;
	const PatternGenSource kick = source(&k, 0x2222u);
	unsigned bites = 0;
	for (uint8_t sty = 3; sty <= 4; sty++)
		for (uint8_t den = 1; den <= 8; den++)
			for (uint8_t sed = 0; sed < 30; sed++)
			{
				const PatternGenSettings s = { PG_TYPE_PRC, sty, den, -13, 0, 0, sed, 0, 0 };
				cleared(&t);
				(void)patternGen_generate(&s, 2, &kick, &t);
				for (unsigned b = 0; b < PG_BARS; b++)
				{
					bites += (accents(&t, b) & 0x8001u) == 1u;
					ASSERT_FALSE(isOn(&t, 16u * b + 15u) && (t.step[16u * b + 15u].volume < 127));
				}
			}
	ASSERT_TRUE(bites > 0u);
	for (uint8_t den = 1; den <= 6; den++)
	{
		const PatternGenSettings s = { PG_TYPE_PRC, 0, den, -13, 0, 0, 0, 0, 0 };
		cleared(&t);
		t.on[0] = (1u << 0) | (1u << 6);
		ASSERT_EQ(patternGen_generate(&s, 2, NULL, &t), PG_WROTE | PG_SOFT);
		for (unsigned b = 0; b < PG_BARS; b++)
			ASSERT_EQ(t.on[b], (1u << 0) | (1u << 5) | (1u << 6));
	}
}

/** v 0: a cleared track; 1: ghosts at 63 on steps 1-16; 2: a 16-step track whose accent on step
 * 21 does not play. None has an accent as it plays */
static void noAccents(PatternGenTrack *t, unsigned v)
{
	cleared(t);
	if (v == 1u)
	{
		t->on[0] = 0xFFFFu;
		for (unsigned st = 0; st < 16u; st++)
			t->step[st].volume = 63;
	}
	else if (v == 2u)
		t->on[1] = 1u << 4;
}

TEST(pgen_core_style0_without_accents)
{
	/* §9: style 0 on a track without an accent as it plays is style 1: hat's offbeats at 127 and den
	 * ghosts; prc's den accents off the kick, bar 2 with one moved, var -1's 1:2 on one of bar 4's.
	 * The seed keeps style 0, so style 1's own tracks differ for some seed */
	PatternGenTrack t, one;
	unsigned differ = 0;
	for (unsigned v = 0; v < 3u; v++)
		for (uint8_t sed = 0; sed < 10; sed++)
		{
			const PatternGenSettings h = hat(0, 7, -15, 0, sed);
			noAccents(&t, v);
			ASSERT_EQ(patternGen_generate(&h, 5, NULL, &t), PG_WROTE | PG_SOFT);
			for (unsigned b = 0; b < PG_BARS; b++)
			{
				ASSERT_EQ(t.on[b], t.on[0]); ASSERT_EQ(t.on[b] & 0x4444u, 0x4444u); ASSERT_EQ(bits(t.on[b]), 11u);
				for (unsigned p = 2; p < 16u; p += 4)
					ASSERT_EQ(t.step[16u * b + p].volume, 127);
			}
			gen(hat(1, 7, -15, 0, sed), &one, NULL);
			differ += memcmp(&t, &one, sizeof t) != 0;

			const PatternGenSettings p = { PG_TYPE_PRC, 0, 3, -13, -1, 0, sed, 0, 0 };
			noAccents(&t, v);
			ASSERT_EQ(patternGen_generate(&p, 2, NULL, &t) & PG_WROTE, PG_WROTE);
			const uint16_t a1 = accents(&t, 0);
			ASSERT_EQ(bits(a1), 3u); ASSERT_EQ(a1 & 0x1111u, 0u);
			ASSERT_EQ(bits(a1 ^ accents(&t, 1)), 2u); ASSERT_EQ(accents(&t, 2), a1);
			unsigned c12 = 0;
			for (unsigned st = 0; st < PG_STEPS; st++)
				c12 += isOn(&t, st) && (t.step[st].prob == 101) && (t.step[st].volume == 127) && (st >= 48u);
			ASSERT_EQ(c12, 1u);
		}
	ASSERT_TRUE(differ > 0u);
}

TEST(pgen_core_style0_accent_in_bar3_only)
{
	/* §9: the fallback to style 1 looks at the whole track as it plays. 64 steps whose only accent is
	 * step 37 (bar 3's step 5; ghosts at 30 on 2 19 51): style 0 stays, the accent is kept byte for
	 * byte, no offbeat at 127 comes in; hat: den 7 ghosts a bar; prc: the ghost on 36 before it and
	 * nothing else */
	static const uint8_t typ[2] = { PG_TYPE_HAT, PG_TYPE_PRC };
	PatternGenTrack t, u;
	cleared(&u);
	u.length = 64;
	u.on[2] = 1u << 4;
	u.step[36] = (Step){ 100, 102, 55, 12, 7, 0xFF, 0 };
	u.timing[36].shift = 9;
	stepFine_set(u.fine, 36, -3);
	u.on[0] = 1u << 1; u.on[1] = 1u << 2; u.on[3] = 1u << 2;
	u.step[1].volume = u.step[18].volume = u.step[50].volume = 30;
	for (unsigned i = 0; i < 2u; i++)
		for (uint8_t sed = 0; sed < 10; sed++)
		{
			memcpy(&t, &u, sizeof t);
			const PatternGenSettings s = { typ[i], 0, (i == 0u) ? 7 : 3, -15, 0, 0, sed, 0, 0 };
			ASSERT_EQ(patternGen_generate(&s, 5, NULL, &t), PG_WROTE | PG_SOFT);
			ASSERT_EQ(memcmp(&t.step[36], &u.step[36], sizeof(Step)), 0);
			ASSERT_EQ(t.timing[36].shift, 9); ASSERT_EQ(nibble(&t, 36), -3);
			for (unsigned st = 0; st < PG_STEPS; st++)
				ASSERT_TRUE((st == 36u) || !isOn(&t, st) || (t.step[st].volume < 64));
			for (unsigned b = 0; b < PG_BARS; b++)
				ASSERT_EQ((i == 0u) ? bits(t.on[b]) : t.on[b], (i == 0u) ? ((b == 2u) ? 8u : 7u) : ((b == 2u) ? 0x0018u : 0u));
		}
}

TEST(pgen_core_rar_mixed_keeps_its_two_steps)
{
	/* §4.4 style 4 with the kick on 2 6 10 14: steps 1 (1:2) and 49 (4:4) stay whatever the scatter's
	 * keys (in gen.py 54 of the 100 seeds rank one of them among the 8 best); den 10 - 2 = 8 at 25 %,
	 * off the kick; §9: at most 8, so den 11-16 write den 10's track */
	PatternGenTrack t, u, k;
	const PatternGenSource kick = source(&k, 0x2222u);
	for (uint8_t sed = 0; sed <= PG_SED_MAX; sed++)
	{
		const PatternGenSettings s = { PG_TYPE_RAR, 4, 10, 0, 0, 0, sed, 0, 0 };
		cleared(&t);
		ASSERT_EQ(patternGen_generate(&s, 4, &kick, &t), PG_WROTE);
		ASSERT_TRUE(isOn(&t, 0)); ASSERT_EQ(t.step[0].prob, 101); ASSERT_TRUE(isOn(&t, 48)); ASSERT_EQ(t.step[48].prob, 109);
		unsigned on = 0, rare = 0;
		for (unsigned st = 0; st < PG_STEPS; st++)
		{
			if (!isOn(&t, st))
				continue;
			ASSERT_TRUE((st & 3u) != 1u);
			on++;
			rare += t.step[st].prob == 25;
		}
		ASSERT_EQ(on, 10u); ASSERT_EQ(rare, 8u);
		for (uint8_t den = 11; den <= PG_DEN_MAX; den++)
		{
			const PatternGenSettings more = { PG_TYPE_RAR, 4, den, 0, 0, 0, sed, 0, 0 };
			cleared(&u);
			ASSERT_EQ(patternGen_generate(&more, 4, &kick, &u), PG_WROTE);
			ASSERT_EQ(memcmp(&u, &t, sizeof u), 0);
		}
	}
}

TEST(pgen_core_prp_accent_at_64)
{
	/* G21: an accent is a source step at 64 or above. 64 on step 5, 63 on step 9: style 3's pickup on
	 * step 4 and style 4's echo (shf +2) on step 7, at lev; the 63 gets neither */
	PatternGenTrack t, k;
	const PatternGenSource src = source(&k, (1u << 4) | (1u << 8));
	k.step[4].volume = 64;
	k.step[8].volume = 63;
	static const struct { uint8_t sty; uint16_t bar; } c[] = { { 3, 1u << 3 }, { 4, 1u << 6 } };
	for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++)
	{
		ASSERT_EQ(gen((PatternGenSettings){ PG_TYPE_PRP, c[i].sty, 4, -15, 0, 0, 0, 0, 2 }, &t, &src), PG_WROTE | PG_SOFT);
		for (unsigned b = 0; b < PG_BARS; b++)
			ASSERT_EQ(t.on[b], c[i].bar);
		ASSERT_EQ(t.step[(c[i].sty == 3u) ? 3 : 6].volume, 23);
	}
}

/* tools/gen/vectors.json (tests/pattern_gen_vectors.py): the dB table, the random generator's start
 * states (bytes sed typ sty track) and first draws, the distinct tracks in the firmware's bytes, the
 * cases (src, cur, out: an index into PGV_TRACK, -1 a cleared track) */
typedef struct { uint8_t bytes[4]; uint32_t state; uint32_t next[8]; } pgv_prng_t;
typedef struct { uint8_t length; uint16_t on[PG_BARS]; uint8_t step[PG_STEPS * sizeof(Step)]; uint8_t timing[PG_STEPS];
	uint8_t fine[STEP_FINE_BYTES]; } pgv_track_t;
typedef struct { const char *name; PatternGenSettings set; uint8_t track, vol; int16_t src, cur, out; } pgv_t;
#include "pattern_gen_vectors.h"
#define PGV_TYPES_DONE	PG_TYPES	/* every type (Task 3) */

TEST(pgen_vectors_vel_table)
{
	for (unsigned d = 0; d < PGV_VEL_COUNT; d++)
		ASSERT_EQ(patternGen_dbVel(-(int)d), PGV_VEL[d]);
}

TEST(pgen_vectors_prng)
{
	ASSERT_TRUE(PGV_PRNG_COUNT > 0u);
	for (unsigned i = 0; i < PGV_PRNG_COUNT; i++)
	{
		const pgv_prng_t *p = &PGV_PRNG[i];
		const PatternGenSettings s = { p->bytes[1], p->bytes[2], 0, 0, 0, 0, p->bytes[0], 0, 0 };
		uint32_t x = patternGen_seed(&s, p->bytes[3]);
		ASSERT_EQ(x, p->state);
		for (unsigned k = 0; k < sizeof p->next / sizeof p->next[0]; k++)
		{
			x = patternGen_step(x);
			ASSERT_EQ(x, p->next[k]);
		}
	}
}

static void pgvLoad(int16_t i, PatternGenTrack *t)
{
	if (i < 0)
	{
		cleared(t);
		return;
	}
	const pgv_track_t *v = &PGV_TRACK[i];
	_Static_assert(sizeof v->step == sizeof t->step && sizeof v->timing == sizeof t->timing, "the vectors' layout");
	memset(t, 0, sizeof *t);
	memcpy(t->step, v->step, sizeof t->step);
	memcpy(t->timing, v->timing, sizeof t->timing);
	memcpy(t->fine, v->fine, sizeof t->fine);
	memcpy(t->on, v->on, sizeof t->on);
	t->length = v->length;
}

/** 1 and the first difference printed, or 0 */
static int pgvDiffers(unsigned v, uint8_t got, const PatternGenTrack *g, const PatternGenTrack *w)
{
	const pgv_t *x = &PGV[v];
	const uint8_t want = ((x->set.typ == PG_TYPE_OFF) || ((x->set.typ == PG_TYPE_PRP) && (x->set.src == x->track)))
		? 0u : (uint8_t)(PG_WROTE | (x->vol ? PG_SOFT : 0u));
	if (got != want)
	{
		printf("vector %u (%s): returns %d, not %d\n", v, x->name, got, want);
		return 1;
	}
	for (unsigned s = 0; s < PG_STEPS; s++)
	{
		uint8_t gt, wt;
		memcpy(&gt, &g->timing[s], 1);
		memcpy(&wt, &w->timing[s], 1);
		if ((isOn(g, s) == isOn(w, s)) && !memcmp(&g->step[s], &w->step[s], sizeof(Step)) && (gt == wt)
			&& (nibble(g, s) == nibble(w, s)))
			continue;
		const Step *a = &g->step[s], *b = &w->step[s];
		printf("vector %u (%s) step %u, on vol prob note p1 v1 p2 v2 timing fine: %d %d %d %d %d %d %d %d 0x%02x %d,"
			" want %d %d %d %d %d %d %d %d 0x%02x %d\n", v, x->name, s,
			isOn(g, s), a->volume, a->prob, a->note, a->param1Nr, a->param1Val, a->param2Nr, a->param2Val, gt, nibble(g, s),
			isOn(w, s), b->volume, b->prob, b->note, b->param1Nr, b->param1Val, b->param2Nr, b->param2Val, wt, nibble(w, s));
		return 1;
	}
	if (g->length == w->length)
		return 0;
	printf("vector %u (%s): length %d, not %d\n", v, x->name, g->length, w->length);
	return 1;
}

TEST(pgen_vectors_byte_for_byte)
{
	unsigned bad = 0, run = 0;
	ASSERT_TRUE(PGV_COUNT >= 150u);
	for (unsigned v = 0; v < PGV_COUNT; v++)
	{
		const pgv_t *x = &PGV[v];
		PatternGenTrack src, io, want;
		if (x->set.typ >= PGV_TYPES_DONE)
			continue;
		pgvLoad(x->src, &src); pgvLoad(x->cur, &io); pgvLoad(x->out, &want);
		const PatternGenSource s = { src.step, { src.on[0], src.on[1], src.on[2], src.on[3] }, src.length };
		const uint8_t got = patternGen_generate(&x->set, x->track, &s, &io);
		bad += (unsigned)pgvDiffers(v, got, &io, &want);
		run++;
	}
	printf("%u vectors, %u differ\n", run, bad);
	ASSERT_TRUE(run > 0u);
	ASSERT_EQ(bad, 0u);
}

TEST(pgen_vectors_cover_the_settings)
{
	/* the vectors reach every style row of each type and the ends of the ranges */
	unsigned sty[PG_TYPES] = { 0 }, ends = 0;
	for (unsigned v = 0; v < PGV_COUNT; v++)
	{
		const PatternGenSettings *s = &PGV[v].set;
		ASSERT_TRUE((s->typ < PG_TYPES) && (s->sty <= PG_STY_MAX));
		sty[s->typ] |= 1u << s->sty;
		ends |= (unsigned)(s->den == 0) | (unsigned)(s->den == 16) << 1 | (unsigned)(s->lev == -30) << 2
			| (unsigned)(s->lev == 0) << 3 | (unsigned)(s->var == -4) << 4 | (unsigned)(s->var == 4) << 5
			| (unsigned)(s->shf == -8) << 6 | (unsigned)(s->shf == 8) << 7 | (unsigned)(s->src == PGV[v].track) << 8
			| (unsigned)(s->note != 0) << 9;
	}
	ASSERT_EQ(sty[PG_TYPE_HAT] & 0x1Fu, 0x1Fu); ASSERT_EQ(sty[PG_TYPE_SUB] & 0x1FEu, 0x1FEu);
	ASSERT_EQ(sty[PG_TYPE_PRC] & 0x1Fu, 0x1Fu); ASSERT_EQ(sty[PG_TYPE_RAR] & 0x1Eu, 0x1Eu);
	ASSERT_EQ(sty[PG_TYPE_PRP] & 0x1Eu, 0x1Eu); ASSERT_EQ(ends, 0x3FFu);
}

TEST_MAIN()
