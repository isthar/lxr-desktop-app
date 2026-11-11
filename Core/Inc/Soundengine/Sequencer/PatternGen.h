/*
 * PatternGen.h
 *
 * LICHOMAT v0.8 (plan v0.8 G2-G21; spec docs/superpowers/specs/2026-11-11-groove-generator-design.md
 * §3-§5, §8-§11): the pattern generator of the track page. It writes plain steps into one 64-step track:
 * the settings, the source track and the current track in, the new track out. Pure C on the
 * types of SeqStepTypes.h; tests/test_pattern_gen.c builds it as it is and compares it byte for
 * byte with the prototype tools/gen/gen.py (tools/gen/vectors.json), which is the reference (G14).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PATTERNGEN_H_
#define PATTERNGEN_H_

#include <stdint.h>
#include "Preset/SeqStepTypes.h"

enum { PG_STEPS = 64, PG_BARS = 4, PG_TRACKS = 7 };	// a generated track: 4 bars of 16ths
enum { PG_TYPE_OFF, PG_TYPE_HAT, PG_TYPE_SUB, PG_TYPE_PRC, PG_TYPE_RAR, PG_TYPE_PRP, PG_TYPES };
/* the settings' ranges (spec §3): lev in dB, not in semitones, shf in steps */
enum { PG_STY_MAX = 8, PG_DEN_MAX = 16, PG_LEV_MIN = -30, PG_VAR_MAX = 4, PG_NOTE_MAX = 24, PG_SED_MAX = 99, PG_SHF_MAX = 8 };
/* an accent is a step at 64 or above (spec §3); 60 the voice's own pitch (SEQ_DEFAULT_NOTE); 0xFF no
 * p-lock (NO_AUTOMATION); prob 100 or a trig condition of SeqStepTypes.h, in its order from
 * STEP_COND_1_2 (1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4); rar's scatter at 25 %; StepTiming.flam x4 (off
 * x2 x3 x4 x6 x8) and shift 7, none */
enum { PG_ACCENT_MIN = 64, PG_NOTE_BASE = 60, PG_NO_PLOCK = 0xFF, PG_PROB_ALWAYS = 100, PG_COND_1_2 = STEP_COND_1_2,
	PG_COND_2_2 = STEP_COND_1_2 + 1, PG_COND_1_4 = STEP_COND_1_2 + 5, PG_COND_4_4 = STEP_COND_1_2 + 8, PG_PROB_RARE = 25,
	PG_FLAM_X4 = 3, PG_SHIFT_NONE = 7 };

/** one track's generator settings (RAM only, spec §2); zero bytes are the defaults */
typedef struct PatternGenSettings
{
	uint8_t	typ;	// PG_TYPE_*
	uint8_t	sty;	// 0..8
	uint8_t	den;	// 0..16
	int8_t	lev;	// -30..0 dB
	int8_t	var;	// -4..+4
	int8_t	note;	// -24..+24, the menu's "not"
	uint8_t	sed;	// 0..99
	uint8_t	src;	// the source track 0..6, the menu's 1..7
	int8_t	shf;	// -8..+8 steps
}PatternGenSettings;

/** one track as a pattern holds it (PatternSet's arrays of one pattern and track) */
typedef struct PatternGenTrack
{
	Step		step[PG_STEPS];
	StepTiming	timing[PG_STEPS];
	uint8_t		fine[STEP_FINE_BYTES];
	uint16_t	on[PG_BARS];	// seq_mainSteps: bit p of on[b] is step 16b + p
	uint8_t		length;		// raw: 0 plays as 16, above 64 as 64
}PatternGenTrack;

/** the source track, read only: its steps in place */
typedef struct PatternGenSource
{
	const Step	*step;		// PG_STEPS steps
	uint16_t	on[PG_BARS];
	uint8_t		length;		// raw: 0 plays as 16, above 64 as 64
}PatternGenSource;

#define PG_WROTE	0x01u	// the track was rewritten
#define PG_SOFT		0x02u	// a step the generator wrote is below velocity 127: the voice needs vol

/** track (0..6) from s into io (the current track in, the new one out); src is track
 * patternGen_checked(*s).src, or NULL for an empty track (gen.py's None). src->step may share memory
 * with io->step only when that track is the destination itself: the one case where src is never
 * read (the kick is then 1 5 9 13, and prp writes nothing). Every track is read as it plays: shorter
 * than 64 steps, it repeats to 64 (spec §8). Style 0 of hat and prc on a track without an accent as
 * it plays is style 1, seeded with style 0 (spec §9). LICHOMAT v0.8 spec §11: own (bit i step i, as
 * stored), in the steps of io the last run wrote, none of which is an accent of style 0 or counts
 * for §9's check, so on a shorter track bit j masks step j wherever it plays; out the steps this run
 * wrote (gen.py Track.written), 0 when it returns 0; NULL is no mask and no answer. Returns PG_WROTE
 * | PG_SOFT, or 0 when io is left as it was: typ --- or past prp, prp from its own track, no track
 * 0..6 */
uint8_t patternGen_generate(const PatternGenSettings *s, uint8_t track, const PatternGenSource *src, PatternGenTrack *io,
	uint64_t *own);
/** s as gen.py's Settings.clamped(): every field clamped into its range (spec §3), sty into the
 * styles of typ (spec §8: hat and prc 0..4, sub 1..8, rar and prp 1..4); a typ past prp is ---, so a
 * stray byte never wipes a track; patternGen_generate() applies it itself */
PatternGenSettings patternGen_checked(PatternGenSettings s);
/** gen.py's apply_type_defaults(): typ into s and the type's sty den lev var not (spec §3); typ ---
 * sets typ only, a typ past prp changes nothing. LICHOMAT v0.8 spec §11: hat or prc on cur (the track
 * now; NULL none) with an accent of the user's, a step on at 64 or above as it plays that own (as in
 * patternGen_generate()) leaves out, load style 0 instead of 1 */
void patternGen_typeDefaults(uint8_t typ, PatternGenSettings *s, const PatternGenSource *cur, uint64_t own);
/** round(127 x 10^(dB/20)), at least 1; dB clamped to -30..0 (spec §5, §8) */
uint8_t patternGen_dbVel(int db);
/** the random generator of spec §5, for the host tests: the start state from the bytes sed typ sty
 * track as they are (patternGen_generate() seeds with the checked settings), one step */
uint32_t patternGen_seed(const PatternGenSettings *s, uint8_t track);
uint32_t patternGen_step(uint32_t x);

#endif /* PATTERNGEN_H_ */
