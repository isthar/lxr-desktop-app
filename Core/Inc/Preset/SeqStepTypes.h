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

#endif /* SEQSTEPTYPES_H_ */
