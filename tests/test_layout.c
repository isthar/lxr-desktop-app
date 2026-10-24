/*
 * test_layout.c
 *
 * The step types keep the layout of the fw 1.7 pattern files (PatV03): Step is
 * stored raw (7 bytes), StepTiming as one byte with flam in bits 0-3 and shift
 * in bits 4-7. seqc_event_t is 24 bytes (N2). Including seq_core.h here also
 * shows that it builds without the HAL: no HAL directory is on the include path
 * of the host tests.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Preset/SeqStepTypes.h"
#include "Soundengine/Sequencer/seq_core.h"
#include "seqc_test.h"

TEST(layout_step)
{
	ASSERT_EQ(sizeof(Step), 7);
	ASSERT_EQ(offsetof(Step, volume), 0);
	ASSERT_EQ(offsetof(Step, prob), 1);
	ASSERT_EQ(offsetof(Step, note), 2);
	ASSERT_EQ(offsetof(Step, param1Nr), 3);
	ASSERT_EQ(offsetof(Step, param1Val), 4);
	ASSERT_EQ(offsetof(Step, param2Nr), 5);
	ASSERT_EQ(offsetof(Step, param2Val), 6);
}

TEST(layout_step_timing)
{
	ASSERT_EQ(sizeof(StepTiming), 1);
	const StepTiming t = { .flam = 5, .shift = 10 };
	uint8_t b;
	memcpy(&b, &t, 1);
	ASSERT_EQ(b, 0xA5);				/* flam in bits 0-3, shift in bits 4-7 */
}

TEST(layout_event)
{
	ASSERT_EQ(sizeof(seqc_event_t), 24);
}

TEST_MAIN()
