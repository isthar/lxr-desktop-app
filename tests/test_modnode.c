/*
 * test_modnode.c
 *
 * Host test of the modulation nodes (Core/Src/Soundengine/DSPAudio/
 * modulationNode.c, stage 1b F1): the six velocity modulators and the LFO
 * targets of the six voices. Every node starts at destination 0, PAR_NONE, the
 * mod wheel's slot, and that parameter has no pointer. A CC 1 (the mod wheel)
 * makes the CC handler call modNode_originalValueChanged(0) (MidiParser.c), and
 * fw 1.7 then read the original value of every node at destination 0 through
 * that NULL pointer: from address 0, which is ITCM on the device, and a crash
 * on a host. Such a node now keeps its original value.
 *
 * modulationNode.c is compiled as it is, against the stand-in headers in
 * tests/stubs/ and the stand-ins below. parameterArray is the test's own and
 * starts zeroed, as the firmware's does: entry 0 stays { NULL, TYPE_UINT8 }
 * there too, because parameterArray_init() never sets it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include "menu/menu.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/ParameterArray.h"

#define NODES 12

//------------------------------------------------------------------------------
// Firmware state that modulationNode.c reads and writes
//------------------------------------------------------------------------------
uint8_t activeParameters[NUM_PARAMS];
Parameter parameterArray[END_OF_SOUND_PARAMETERS];
DrumVoice voiceArray[NUM_DRUM_VOICES];
SnareVoice snareVoice;
CymbalVoice cymbalVoice;
HiHatVoice hatVoice;

//------------------------------------------------------------------------------
// Stand-ins for the functions it calls
//------------------------------------------------------------------------------
void paramArray_setParameter(uint16_t idx, ptrValue newValue)
{
	(void)idx;
	(void)newValue;
}

void midiParser_ccHandler(MidiMsg msg, uint8_t updateOriginalValue, uint8_t isFloatModulator, float v)
{
	(void)msg; (void)updateOriginalValue; (void)isFloatModulator; (void)v;
}

//------------------------------------------------------------------------------
// The nodes
//------------------------------------------------------------------------------
/** node i: the six velocity modulators, then the LFO targets of the six voices */
static ModulationNode *node(int i)
{
	ModulationNode *const lfoTargets[6] = {
		&voiceArray[0].lfo.modTarget, &voiceArray[1].lfo.modTarget, &voiceArray[2].lfo.modTarget,
		&snareVoice.lfo.modTarget, &cymbalVoice.lfo.modTarget, &hatVoice.lfo.modTarget,
	};
	return i < 6 ? &velocityModulators[i] : lfoTargets[i - 6];
}

/** every node at destination 0, as modNode_init() leaves it at boot, with the
 *  original value 100 + i */
static void resetNodes(void)
{
	memset(parameterArray, 0, sizeof parameterArray);
	for (int i = 0; i < NODES; i++) {
		modNode_init(node(i));
		node(i)->originalValue.itg = 100u + (uint32_t)i;
	}
}

TEST(modnode_cc1_keeps_the_nodes_at_destination_0)
{
	resetNodes();
	ASSERT_TRUE(parameterArray[PAR_NONE].ptr == NULL);
	modNode_originalValueChanged(PAR_NONE);	/* CC 1: paramNr = data1 - 1 = 0 */
	for (int i = 0; i < NODES; i++) {
		ASSERT_EQ(node(i)->destination, PAR_NONE);
		ASSERT_EQ(node(i)->originalValue.itg, 100u + (uint32_t)i);
	}
}

TEST(modnode_a_node_with_a_parameter_takes_its_new_value)
{
	static uint8_t value = 7;
	resetNodes();
	parameterArray[5].ptr = &value;
	parameterArray[5].type = TYPE_UINT8;
	modNode_setDestination(node(3), 5);
	ASSERT_EQ(node(3)->originalValue.itg, 7u);
	value = 42;
	modNode_originalValueChanged(5);
	ASSERT_EQ(node(3)->originalValue.itg, 42u);
	ASSERT_EQ(node(4)->originalValue.itg, 104u);	/* at destination 0: unchanged */
}

TEST_MAIN()
