/*
 * pattern_gen_fuzz.c
 *
 * LICHOMAT v0.8 (E10.17): the C side of the differential fuzz of the pattern generator,
 * tools/gen/fuzz_c.py, which runs this with random cases and compares every answer with the
 * prototype tools/gen/gen.py. Binary records on stdin, one answer each on stdout, little endian:
 *
 *   in   settings 9 (typ sty den lev var not sed src shf, src 0-6), track 1, has 1 (0 no source,
 *        1 the source below, 2 the destination itself as the source), source: length 1, on 8,
 *        steps 448; current track: length 1, on 8, steps 448, timing 64, fine 32; own 8 (spec §11)
 *   out  return 1, then the track: length 1, on 8, steps 448, timing 64, fine 32; own 8
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdio.h>
#include <string.h>

#include "Soundengine/Sequencer/PatternGen.h"

enum { STEP_BYTES = PG_STEPS * 7, SRC = 1 + 8 + STEP_BYTES, TRACK = SRC + PG_STEPS + 32, IN = 9 + 2 + SRC + TRACK + 8,
	OUT = 1 + TRACK + 8 };

static const unsigned char *getOn(const unsigned char *p, uint16_t on[PG_BARS])
{
	for (unsigned b = 0; b < PG_BARS; b++, p += 2)
		on[b] = (uint16_t)(p[0] | (p[1] << 8));
	return p;
}

int main(void)
{
	_Static_assert(sizeof(Step) == 7 && sizeof(StepTiming) == 1, "the record's layout");
	static unsigned char in[IN], out[OUT];
	static Step srcSteps[PG_STEPS];
	while (fread(in, 1, IN, stdin) == IN)
	{
		const unsigned char *p = in;
		const PatternGenSettings s = { p[0], p[1], p[2], (int8_t)p[3], (int8_t)p[4], (int8_t)p[5], p[6], p[7], (int8_t)p[8] };
		p += 9;
		const uint8_t track = *p++;
		const unsigned has = *p++;
		PatternGenSource src;
		src.length = *p++;
		p = getOn(p, src.on);
		memcpy(srcSteps, p, STEP_BYTES);
		p += STEP_BYTES;
		src.step = srcSteps;
		PatternGenTrack t;
		t.length = *p++;
		p = getOn(p, t.on);
		memcpy(t.step, p, STEP_BYTES);
		p += STEP_BYTES;
		memcpy(t.timing, p, PG_STEPS);
		p += PG_STEPS;
		memcpy(t.fine, p, sizeof t.fine);
		p += sizeof t.fine;
		uint64_t own = 0;
		for (unsigned i = 0; i < 8u; i++)
			own |= (uint64_t)p[i] << (8u * i);
		/* has 2: the source is the destination's own steps, in place */
		const PatternGenSource self = { t.step, { t.on[0], t.on[1], t.on[2], t.on[3] }, t.length };
		const uint8_t r = patternGen_generate(&s, track, (has == 0u) ? NULL : (has == 2u) ? &self : &src, &t, &own);
		unsigned char *q = out;
		*q++ = r;
		*q++ = t.length;
		for (unsigned b = 0; b < PG_BARS; b++)
		{
			*q++ = (unsigned char)(t.on[b] & 0xFFu);
			*q++ = (unsigned char)(t.on[b] >> 8);
		}
		memcpy(q, t.step, STEP_BYTES);
		q += STEP_BYTES;
		memcpy(q, t.timing, PG_STEPS);
		q += PG_STEPS;
		memcpy(q, t.fine, sizeof t.fine);
		q += sizeof t.fine;
		for (unsigned i = 0; i < 8u; i++)
			*q++ = (unsigned char)(own >> (8u * i));
		if (fwrite(out, 1, OUT, stdout) != OUT)
			return 1;
	}
	return 0;
}
