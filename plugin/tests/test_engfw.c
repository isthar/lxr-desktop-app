/*
 * test_engfw.c
 *
 * P12: the engine's check of plug_params[] against the firmware's parameters (plan E9.4 Task 6,
 * eng_fw.c). eng_fw.c reads four globals of the firmware; the test defines them, as the engine's
 * other objects need not be linked to run the check.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <string.h>

#include "eng_fw.h"
#include "plug_params.h"
#include "seqc_test.h"

/* the firmware's globals eng_fwValues() reads (Parameters.h, sequencer.h, MidiParser.h); the array is larger
 * than NUM_PARAMS */
uint8_t seq_pendingPattern;
uint8_t activeParameters[1024];
uint8_t midi_MidiChannels[8];
uint8_t midiParser_txRxFilter;

TEST(P12_engine_table)
{
	char err[256];
	ASSERT_EQ(eng_fwCheck(err, sizeof err), 0);
	ASSERT_EQ(eng_fwCheckTable(plug_params, PLUG_PARAM_COUNT, err, sizeof err), 0);
	/* each row on its own: another firmware parameter is refused, and the message names the row */
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		plug_param_t copy[PLUG_PARAM_COUNT];
		memcpy(copy, plug_params, sizeof copy);
		copy[i].par = (uint16_t)(copy[i].par == 0u ? 1u : copy[i].par - 1u);
		err[0] = 0;
		ASSERT_EQ(eng_fwCheckTable(copy, PLUG_PARAM_COUNT, err, sizeof err), -1);
		if (!strstr(err, copy[i].name))
			printf("row %u (%s): \"%s\"\n", i, copy[i].name, err);
		ASSERT_TRUE(strstr(err, copy[i].name) != NULL);
		/* the pattern row is not a firmware parameter of Parameters.h: made one, it is refused too */
		if (plug_params[i].par == PLUG_PAR_PATTERN)
		{
			copy[i].par = 0;
			ASSERT_EQ(eng_fwCheckTable(copy, PLUG_PARAM_COUNT, err, sizeof err), -1);
		}
	}
	/* the one table's own check has the same words */
	err[0] = 0;
	plug_param_t one[PLUG_PARAM_COUNT];
	memcpy(one, plug_params, sizeof one);
	one[1].par ^= 1u;
	ASSERT_EQ(eng_fwCheckTable(one, PLUG_PARAM_COUNT, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "PAR_VOL1") != NULL);
}

TEST_MAIN()
