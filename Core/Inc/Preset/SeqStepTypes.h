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

#endif /* SEQSTEPTYPES_H_ */
