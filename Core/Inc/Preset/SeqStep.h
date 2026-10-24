/*
 * SeqStep.h
 *
 * Created: 26.06.2012 16:46:31
 *  Author: Julian
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */ 


#ifndef SEQSTEP_H_
#define SEQSTEP_H_
#include "main.h"

#include "SeqStepTypes.h"     /* Step, StepTiming */

typedef enum StepPositionEnum
{
	STEP_PREV,
	STEP_CURRENT,
	STEP_NEXT
}StepPosition;

/*
typedef union
{
	uint32_t completeData;
	struct
	{
		uint8_t unused;
		uint8_t prevStep;
		uint8_t currentStep;
		uint8_t nextStep;
	}StepData;
}StepPattern;
*/





#endif /* SEQSTEP_H_ */
