/*
 * eng_fw.c
 *
 * The engine's reads of the firmware's memory (plan E9.4 Task 6); see eng_fw.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "eng_fw.h"

#include <stdio.h>

#include "main.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/Sequencer/sequencer.h"

/* plug_params[]'s parameters by their firmware names, in its order */
static const struct
{
	uint16_t par;
	const char *name;
} eng_pars[PLUG_PARAM_COUNT] = {
	{ PLUG_PAR_PATTERN, "the next pattern" },
	{ PAR_VOL1, "PAR_VOL1" }, { PAR_VOL2, "PAR_VOL2" }, { PAR_VOL3, "PAR_VOL3" },
	{ PAR_VOL4, "PAR_VOL4" }, { PAR_VOL5, "PAR_VOL5" }, { PAR_VOL6, "PAR_VOL6" },
	{ PAR_PAN1, "PAR_PAN1" }, { PAR_PAN2, "PAR_PAN2" }, { PAR_PAN3, "PAR_PAN3" },
	{ PAR_PAN4, "PAR_PAN4" }, { PAR_PAN5, "PAR_PAN5" }, { PAR_PAN6, "PAR_PAN6" },
	{ PAR_FILTER_FREQ_1, "PAR_FILTER_FREQ_1" }, { PAR_FILTER_FREQ_2, "PAR_FILTER_FREQ_2" },
	{ PAR_FILTER_FREQ_3, "PAR_FILTER_FREQ_3" }, { PAR_FILTER_FREQ_4, "PAR_FILTER_FREQ_4" },
	{ PAR_FILTER_FREQ_5, "PAR_FILTER_FREQ_5" }, { PAR_FILTER_FREQ_6, "PAR_FILTER_FREQ_6" },
	{ PAR_VELOD1, "PAR_VELOD1" }, { PAR_VELOD2, "PAR_VELOD2" }, { PAR_VELOD3, "PAR_VELOD3" },
	{ PAR_VELOD4, "PAR_VELOD4" }, { PAR_VELOD5, "PAR_VELOD5" }, { PAR_VELOD6_CLOSED, "PAR_VELOD6_CLOSED" },
	{ PAR_VELOD6_OPEN, "PAR_VELOD6_OPEN" },
	{ PAR_DJ_FILTER, "PAR_DJ_FILTER" }, { PAR_REV_LEVEL, "PAR_REV_LEVEL" }, { PAR_REV_DECAY, "PAR_REV_DECAY" },
	{ PAR_FX_DRY_WET, "PAR_FX_DRY_WET" },
	{ PAR_AUDIO_OUT1, "PAR_AUDIO_OUT1" }, { PAR_AUDIO_OUT2, "PAR_AUDIO_OUT2" }, { PAR_AUDIO_OUT3, "PAR_AUDIO_OUT3" },
	{ PAR_AUDIO_OUT4, "PAR_AUDIO_OUT4" }, { PAR_AUDIO_OUT5, "PAR_AUDIO_OUT5" }, { PAR_AUDIO_OUT6, "PAR_AUDIO_OUT6" },
};

void eng_fwValues(uint8_t values[PLUG_PARAM_COUNT], uint8_t *channel, uint8_t *rxFilter)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		const uint16_t par = plug_params[i].par;
		values[i] = par == PLUG_PAR_PATTERN ? seq_pendingPattern : par < NUM_PARAMS ? activeParameters[par] : PLUG_VALUE_NONE;
	}
	*channel = midi_MidiChannels[7] < 16u ? midi_MidiChannels[7] : (uint8_t)PLUG_VALUE_NONE;
	*rxFilter = (uint8_t)(midiParser_txRxFilter & 0x0Fu);
}

int eng_fwCheckTable(const plug_param_t *table, unsigned n, char *err, size_t errlen)
{
	for (unsigned i = 0; i < n && i < PLUG_PARAM_COUNT; i++)
		if (table[i].par != eng_pars[i].par)
		{
			snprintf(err, errlen, "the parameter %s (id %u) is firmware parameter %u, %s is %u", table[i].name,
				(unsigned)table[i].id, (unsigned)table[i].par, eng_pars[i].name, (unsigned)eng_pars[i].par);
			return -1;
		}
	return 0;
}

int eng_fwCheck(char *err, size_t n)
{
	return eng_fwCheckTable(plug_params, PLUG_PARAM_COUNT, err, n);
}
