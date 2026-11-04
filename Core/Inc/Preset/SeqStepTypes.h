/*
 * SeqStepTypes.h
 *
 * The step data types of the pattern format, moved out of SeqStep.h unchanged so
 * that code without the HAL can use them: the sequencer timing core (seq_core)
 * and its host tests. SeqStep.h includes this file.
 *
 * The layout is part of the pattern file format (PatV03) and must not change:
 * - Step is stored raw, 7 bytes;
 * - StepTiming is stored and copied as one byte, flam in bits 0-3 and shift in
 *   bits 4-7.
 *
 * Created: 26.06.2012 16:46:31
 *  Author: Julian
 *
 * Copyright (C) 2012-2026 Erica Synths
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

#ifndef SEQSTEPTYPES_H_
#define SEQSTEPTYPES_H_

#include <stdint.h>

/** a copy of the step struct from the sequencer.h file from the cortex src
used to load step data from sequencer and write it to the pattern preset*/
typedef struct StepStruct
{
	//uint8_t  	active;		// bit 0: on/off
	uint8_t 	volume;		// 0-127 volume -> 0x7f => lower 7 bit, upper bit => active <- active c an probably be removed
	uint8_t  	prob;		//step probability 7 bit [0:100]
	uint8_t		note;		// 7 bit

	//parameter automation
	uint8_t 	param1Nr;
	uint8_t 	param1Val; //probably 7

	uint8_t 	param2Nr;
	uint8_t 	param2Val; //probably 7
}Step;

typedef struct StepTiming
{
	uint8_t flam:4; // [0:5] => off, x2, x3, x4, x6, x8
	uint8_t shift:4; // [-7:7]
}StepTiming;

_Static_assert(sizeof(Step) == 7, "Step is stored raw in pattern files (7 bytes)");
_Static_assert(sizeof(StepTiming) == 1, "StepTiming is stored and copied as one byte");

/* LICHOMAT v0.2: the per-track settings of a pattern, stored in the extension block
 * at the end of a pattern file (presetManager.c). Zero bytes are the defaults: x1,
 * no humanize, no delay. */
typedef struct TrackGroove
{
	int8_t		tempo;		// tempo index - 5: -5..+5 = x1/4 x1/3 x1/2 x2/3 x3/4 x1 x4/3 x3/2 x2 x3 x4
	uint8_t		humTime;	// humanize time: random +-0..8 ms
	uint8_t		humVol;		// humanize volume: random +-0..25 %
	int8_t		delay;		// track delay: -15..+30 ms
}TrackGroove;
_Static_assert(sizeof(TrackGroove) == 4, "TrackGroove is stored raw in the extension block (4 bytes)");

#define GROOVE_TEMPO_X1		5	// the menu's tempo index of x1; TrackGroove.tempo is index - 5
#define GROOVE_TEMPO_COUNT	11	// tempo indices 0..10
#define GROOVE_HUM_TIME_MAX	8	// ms
#define GROOVE_HUM_VOL_MAX	25	// percent
#define GROOVE_DELAY_MIN	(-15)	// ms
#define GROOVE_DELAY_MAX	30	// ms

/** g with every field out of range set to its default (a file or a host from elsewhere) */
static inline TrackGroove trackGroove_checked(TrackGroove g)
{
	if(g.tempo < -GROOVE_TEMPO_X1 || g.tempo > GROOVE_TEMPO_COUNT - 1 - GROOVE_TEMPO_X1) g.tempo = 0;
	if(g.humTime > GROOVE_HUM_TIME_MAX) g.humTime = 0;
	if(g.humVol > GROOVE_HUM_VOL_MAX) g.humVol = 0;
	if(g.delay < GROOVE_DELAY_MIN || g.delay > GROOVE_DELAY_MAX) g.delay = 0;
	return g;
}

/* LICHOMAT v0.2: Step.prob is 0..100 probability in percent, 101..115 a trig condition,
 * in this order: 1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4 FILL !FILL FIRST !FIRST PRE !PRE;
 * 116..255 play like 100. fw 1.7 plays every value above 100 like 100. */
#define STEP_COND_1_2		101u
#define STEP_COND_FILL		110u
#define STEP_COND_NOT_FILL	111u
#define STEP_COND_FIRST		112u
#define STEP_COND_NOT_FIRST	113u
#define STEP_COND_PRE		114u
#define STEP_COND_NOT_PRE	115u
#define STEP_PROB_CODES		116u	// the values the step page offers: 0..115

// LICHOMAT v0.3: bit 7 of Step.prob is the step's slide flag (plan D23); bits 0..6 hold the
// probability 0..100 or the trig condition 101..115 (116..127 play like 100)
#define STEP_SLIDE_FLAG		0x80u
#define STEP_PROB_MASK		0x7Fu

/* LICHOMAT v0.6: a step's microtiming T (spec v0.6 M1-M3), in ticks of 1/96 of the track's
 * step, -84..+84. The coarse part is StepTiming.shift (units of 12 ticks), the fine part a
 * signed nibble -6..+5 in PatternSet.seq_stepFine, 32 bytes a track: byte i holds step 2i in
 * bits 0-3 and step 2i+1 in bits 4-7. The split is canonical: coarse = floor((T + 6) / 12),
 * the nearest unit with ties upwards, which fw 1.7 and v0.2-v0.5 play; fine = T - 12 coarse.
 * A nibble of -8, -7, +6 or +7 counts as 0. Zero bytes are the default. */
#define STEP_MICRO_MAX		84	// ticks: the shift's reach, 7/8 of a step
#define STEP_SHIFT_UNIT		12	// ticks of a shift unit at x1
#define STEP_FINE_MIN		(-6)
#define STEP_FINE_MAX		5
#define STEP_FINE_BYTES		32	// a track's nibbles: 64 steps / 2

/** the fine part of a stored nibble (the low 4 bits of n): -6..+5, any other value 0 */
static inline int8_t stepFine_decode(uint8_t n)
{
	const int f = (n & 0x08u) ? (int)(n & 0x0Fu) - 16 : (int)(n & 0x0Fu);
	return (int8_t)((f < STEP_FINE_MIN || f > STEP_FINE_MAX) ? 0 : f);
}

/** step s's fine part in a track's STEP_FINE_BYTES */
static inline int8_t stepFine_get(const uint8_t *track, uint8_t s)
{
	return stepFine_decode((uint8_t)(track[(s >> 1) & 31u] >> ((s & 1u) * 4u)));
}

/** set step s's fine part (its low 4 bits) in a track's STEP_FINE_BYTES */
static inline void stepFine_set(uint8_t *track, uint8_t s, int8_t fine)
{
	const unsigned sh = (s & 1u) * 4u;
	uint8_t *b = &track[(s >> 1) & 31u];
	*b = (uint8_t)((*b & ~(0x0Fu << sh)) | (((unsigned)fine & 0x0Fu) << sh));
}

/** T of a step: 12 x (shift - 7) + fine; a shift of 15 counts as 0, a fine part out of range as 0 */
static inline int stepMicro(StepTiming t, int8_t fine)
{
	const int c = (t.shift > 14) ? 0 : (int)t.shift - 7;
	if((fine < STEP_FINE_MIN) || (fine > STEP_FINE_MAX)) fine = 0;
	return STEP_SHIFT_UNIT * c + fine;
}

/** the canonical split of T, clamped to +-84 first: the coarse part into t->shift (the flam
 * stays), the fine part returned */
static inline int8_t stepMicro_split(int micro, StepTiming *t)
{
	if(micro > STEP_MICRO_MAX) micro = STEP_MICRO_MAX;
	if(micro < -STEP_MICRO_MAX) micro = -STEP_MICRO_MAX;
	const int c = (micro + 6 + 8 * STEP_SHIFT_UNIT) / STEP_SHIFT_UNIT - 8;	// floor((T + 6) / 12), the numerator kept positive
	t->shift = (uint8_t)(c + 7);
	return (int8_t)(micro - STEP_SHIFT_UNIT * c);
}

/* LICHOMAT v0.6: a track's groove template in a pattern (spec v0.6 M4, M5), stored raw in
 * block 2 of the pattern file. Zero bytes are the default: off, 100 %. */
typedef struct TrackTemplate
{
	uint8_t		id;		// 0 off, 1..7 swg lay lyb rlx 2bt psh drg (the table in seq_core.c)
	int8_t		amount;		// percent - 100: -100..+100 = 0..200 %
}TrackTemplate;
_Static_assert(sizeof(TrackTemplate) == 2, "TrackTemplate is stored raw in block 2 (2 bytes)");

#define TEMPLATE_COUNT		8	// ids 0..7
#define TEMPLATE_STEPS		16	// positions: a bar of 16ths
#define TEMPLATE_AMOUNT_MIN	(-100)
#define TEMPLATE_AMOUNT_MAX	100
#define USER_TEMPLATES		4	// LICHOMAT v0.7: the project's own templates us1..us4 (plan v0.7 V7-6)

/** t with every field out of range set to its default (a file or a host from elsewhere) */
static inline TrackTemplate trackTemplate_checked(TrackTemplate t)
{
	if(t.id >= TEMPLATE_COUNT) t.id = 0;
	if((t.amount < TEMPLATE_AMOUNT_MIN) || (t.amount > TEMPLATE_AMOUNT_MAX)) t.amount = 0;
	return t;
}

/* LICHOMAT v0.7: a track's velocity template in a pattern (plan v0.7 V7-2), a TrackTemplate in
 * PatternSet.vtpl, stored raw in block 3: id 0 off, 1..4 of8 ful dep qtr (the table in
 * seq_core.c), 5..8 the own templates us1..us4; amount the depth in percent - 100 (0..200 %).
 * Zero bytes are the default: off, 100 %. */
#define VEL_TEMPLATE_BUILTIN	5	// ids 0..4: off and the table
#define VEL_TEMPLATE_COUNT	(VEL_TEMPLATE_BUILTIN + USER_TEMPLATES)	// ids 0..8
#define VEL_TEMPLATE_DB_MIN	(-42)	// dB: an own template's floor, 1/127 of the loudest step

/** t with every field out of range set to its default (a file or a host from elsewhere) */
static inline TrackTemplate velTemplate_checked(TrackTemplate t)
{
	if(t.id >= VEL_TEMPLATE_COUNT) t.id = 0;
	if((t.amount < TEMPLATE_AMOUNT_MIN) || (t.amount > TEMPLATE_AMOUNT_MAX)) t.amount = 0;
	return t;
}

/* LICHOMAT v0.7: one of the project's own templates us1..us4 (plan v0.7 V7-6, V7-7), in RAM
 * (seq_userTemplates) and in the project's USERTPL.LCH: at each of the 16 positions an offset
 * in ticks, -84..+84 (the timing part, tpl 8..11), and one in dB, -42..0 (the velocity part,
 * vtp 5..8). Zero bytes are the default: an empty template, which plays as off. */
typedef struct UserTemplate
{
	int8_t		timing[TEMPLATE_STEPS];
	int8_t		vel[TEMPLATE_STEPS];
}UserTemplate;
_Static_assert(sizeof(UserTemplate) == 32, "UserTemplate is stored raw in USERTPL.LCH (32 bytes)");

/** an own template's timing value, 0 when out of range (a file or a host from elsewhere) */
static inline int8_t userTemplate_timing(int8_t v)
{
	return (int8_t)(((v < -STEP_MICRO_MAX) || (v > STEP_MICRO_MAX)) ? 0 : v);
}

/** an own template's velocity value, 0 when out of range */
static inline int8_t userTemplate_vel(int8_t v)
{
	return (int8_t)(((v < VEL_TEMPLATE_DB_MIN) || (v > 0)) ? 0 : v);
}

#endif /* SEQSTEPTYPES_H_ */
