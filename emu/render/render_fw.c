/*
 * render_fw.c
 *
 * Every firmware call of lichomat-render (stage 1b spec 5.3); see render_fw.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "render_fw.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "main.h"
#include "Parameters.h"
#include "render_params.h"
#include "frontPanelParser_ui.h"
#include "menu/menu.h"
#include "menu/CcNr2Text.h"
#include "Preset/PresetManager.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/seq_core.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include "Soundengine/USB/usb_manager.h"

void rfw_seedCard(void *res)
{
	/* sd_formatRoutine() after f_mkfs() and f_mount(): the card is mounted at boot */
	preset_createGlobals();
	preset_init(0);
	snprintf(savefileNames[0][PROJECT_NAMES], sizeof savefileNames[0][PROJECT_NAMES], "Init");
	*(int *)res = (int)preset_saveProject(0);
}

void rfw_morphBusy(void *out)
{
	*(uint8_t *)out = preset_morphBusy();
}

/* --- the menu's edit path (spec 5.3, plan P3) ------------------------------------ */

/* The menu ids of MenuText.h, which only menu.c may include: it defines the name
 * tables. The ids are copied; the tables are declared here with the widths of
 * their definitions, and only their entry counts, xNames[0][0], are read. */
enum
{
	RFW_MENU_FILTER = 1,
	RFW_MENU_WAVEFORM = 2,
	RFW_MENU_AUDIO_OUT = 3,
	RFW_MENU_ROLL_RATES = 4,
	RFW_MENU_SYNC_RATES = 5,
	RFW_MENU_LFO_WAVES = 6,
	RFW_MENU_RETRIGGER = 7,
	RFW_MENU_NEXT_PATTERN = 8,
	RFW_MENU_SEQ_QUANT = 9,
	RFW_MENU_TRANS = 10,
	RFW_MENU_MIDI = 11,
	RFW_MENU_MIDI_ROUTING = 12,
	RFW_MENU_MIDI_FILTERING = 13,
	RFW_MENU_PPQ_IN = 14,
	RFW_MENU_MIX_FM = 15,
	RFW_MENU_PPQ_OUT = 16,
	RFW_MENU_FOLLOW = 17,
	RFW_MENU_FX = 18,
	RFW_MENU_DIST = 19,
	RFW_MENU_RATIO = 20,
	RFW_MENU_DELAY = 21,
	RFW_MENU_REVERB = 22,
	RFW_MENU_PAT_MODE = 23,
	RFW_MENU_STEP_FLAM = 24,
	RFW_MENU_STEP_SHIFT = 25,
	RFW_MENU_AUDIO_OUT_FX = 26,
	RFW_MENU_TRK_TEMPO = 27,
	RFW_MENU_HUM_TIME = 28,
	RFW_MENU_HUM_VOL = 29,
	RFW_MENU_TRK_DELAY = 30,
	RFW_MENU_STEP_PROB = 31,
	RFW_MENU_SCENE_QUANT = 32,
	RFW_MENU_DUCK_SRC = 33,		/* LICHOMAT v0.4 */
	RFW_MENU_DELAY_SYNC = 34
};

extern const char transientNames[][4], outputNames[][7], outputNamesFx[][7], filterTypes[][8],
	syncRateNames[][4], lfoWaveNames[][4], retriggerNames[][4], followNames[][4], quantisationNames[][4],
	midiModes[][4], nextPatternNames[][4], waveformNames[][4], rollRateNames[][4], midiRoutingNames[][6],
	midiFilterNames[][16], ppqOutNames[][4], ppqInNames[][4], mixFmNames[][6], fxNames[][4], distNames[][4],
	ratioNames[][4], delayNames[][4], reverbNames[][4], patModeNames[][4], stepFlamNames[][4],
	stepShiftNames[][4], trackTempoNames[][4], sceneQuantNames[][4], duckSrcNames[][4], delaySyncNames[][4];
extern uint8_t menu_numSamples;		/* menu.c: the samples on the card, after the waveforms */

/* menu.c getMaxEntriesForMenu() (static there; M20 hashes the original) */
static uint8_t rfw_menuEntries(uint8_t menuId)
{
	switch (menuId)
	{
	case RFW_MENU_TRANS:		return transientNames[0][0];
	case RFW_MENU_AUDIO_OUT:	return outputNames[0][0];
	case RFW_MENU_AUDIO_OUT_FX:	return outputNamesFx[0][0];
	case RFW_MENU_FILTER:		return filterTypes[0][0];
	case RFW_MENU_SYNC_RATES:	return syncRateNames[0][0];
	case RFW_MENU_LFO_WAVES:	return lfoWaveNames[0][0];
	case RFW_MENU_RETRIGGER:	return retriggerNames[0][0];
	case RFW_MENU_FOLLOW:		return followNames[0][0];
	case RFW_MENU_SEQ_QUANT:	return quantisationNames[0][0];
	case RFW_MENU_MIDI:			return midiModes[0][0];
	case RFW_MENU_NEXT_PATTERN:	return nextPatternNames[0][0];
	case RFW_MENU_WAVEFORM:		return (uint8_t)(waveformNames[0][0] + menu_numSamples);
	case RFW_MENU_ROLL_RATES:	return rollRateNames[0][0];
	case RFW_MENU_MIDI_ROUTING:	return midiRoutingNames[0][0];
	case RFW_MENU_MIDI_FILTERING:	return midiFilterNames[0][0];
	case RFW_MENU_PPQ_OUT:		return ppqOutNames[0][0];
	case RFW_MENU_PPQ_IN:		return ppqInNames[0][0];
	case RFW_MENU_MIX_FM:		return mixFmNames[0][0];
	case RFW_MENU_FX:			return fxNames[0][0];
	case RFW_MENU_DIST:			return distNames[0][0];
	case RFW_MENU_RATIO:		return ratioNames[0][0];
	case RFW_MENU_DELAY:		return delayNames[0][0];
	case RFW_MENU_REVERB:		return reverbNames[0][0];
	case RFW_MENU_PAT_MODE:		return patModeNames[0][0];
	case RFW_MENU_STEP_FLAM:	return stepFlamNames[0][0];
	case RFW_MENU_STEP_SHIFT:	return stepShiftNames[0][0];
	case RFW_MENU_TRK_TEMPO:	return trackTempoNames[0][0];
	case RFW_MENU_HUM_TIME:		return GROOVE_HUM_TIME_MAX + 1;
	case RFW_MENU_HUM_VOL:		return GROOVE_HUM_VOL_MAX + 1;
	case RFW_MENU_TRK_DELAY:	return GROOVE_DELAY_MAX - GROOVE_DELAY_MIN + 1;
	case RFW_MENU_STEP_PROB:	return STEP_PROB_CODES;
	case RFW_MENU_SCENE_QUANT:	return sceneQuantNames[0][0];
	case RFW_MENU_DUCK_SRC:		return duckSrcNames[0][0];
	case RFW_MENU_DELAY_SYNC:	return delaySyncNames[0][0];
	default:					return 0;
	}
}

/* the menu's clamp, menu.c menu_encoderChangeParameter() (M20 hashes the original),
 * for a value set at once rather than stepped */
static uint8_t rfw_clamp(uint16_t p, unsigned v)
{
	switch (parameter_dtypes[p] & 0xFF)
	{
	case DTYPE_0B255:	return (uint8_t)v;
	case DTYPE_1B16:	return (uint8_t)(v < 1 ? 1 : v > 16 ? 16 : v);
	case DTYPE_1B64:	return (uint8_t)(v < 1 ? 1 : v > 64 ? 64 : v);
	case DTYPE_0B15:	return (uint8_t)(v > 15 ? 15 : v);
	case DTYPE_ON_OFF:
	case DTYPE_0b1:		return (uint8_t)(v > 1 ? 1 : v);
	case DTYPE_MENU:
	{
		const unsigned n = rfw_menuEntries((uint8_t)(parameter_dtypes[p] >> 8));
		return (uint8_t)(v >= n ? n - 1u : v);	/* n == 0 gives 255, as the menu's (uint8_t)(0 - 1) */
	}
	case DTYPE_AUTOM_TARGET:
	{
		const unsigned n = getNumModTargets();
		return (uint8_t)(v >= n ? n - 1u : v);
	}
	case DTYPE_PM63:
	case DTYPE_0B127:	return (uint8_t)(v > 127 ? 127 : v);
	default:		return (uint8_t)(v > 100 ? 100 : v);	/* default falls into DTYPE_0B100 */
	}
}

static int rfw_paramRefused(uint16_t p)
{
	const unsigned d = parameter_dtypes[p] & 0xFF;
	return d == DTYPE_TARGET_SELECTION_VELO || d == DTYPE_VOICE_LFO || d == DTYPE_TARGET_SELECTION_LFO;
}

/* a decimal number of digits only, up to max; -1 if not */
static long rfw_number(const char *s, size_t len, long max)
{
	long v = 0;
	if (len == 0)
		return -1;
	for (size_t i = 0; i < len; i++)
	{
		if (s[i] < '0' || s[i] > '9')
			return -1;
		v = v * 10 + (s[i] - '0');
		if (v > max)
			return -1;
	}
	return v;
}

int rfw_paramParse(const char *spec, rfw_param_t *out, const char **name, char *err, size_t n)
{
	const char *eq = strchr(spec, '=');
	if (!eq || eq == spec)
	{
		snprintf(err, n, "'%s' is not NAME=VALUE", spec);
		return -1;
	}
	const long v = rfw_number(eq + 1, strlen(eq + 1), 255);
	if (v < 0)
	{
		snprintf(err, n, "'%s': the value is not 0-255", spec);
		return -1;
	}
	const size_t len = (size_t)(eq - spec);
	long p = rfw_number(spec, len, NUM_PARAMS - 1);
	const unsigned count = sizeof render_params / sizeof render_params[0];
	*name = NULL;
	if (p >= 0)
	{
		for (unsigned i = 0; i < count; i++)
			if (render_params[i].par == p)
				*name = render_params[i].name;	/* the last alias */
	}
	else if (spec[0] < '0' || spec[0] > '9')
		for (unsigned i = 0; i < count; i++)
			if (strlen(render_params[i].name) == len && strncasecmp(spec, render_params[i].name, len) == 0)
			{
				p = render_params[i].par;
				*name = render_params[i].name;
				break;
			}
	if (p < 0 || p >= NUM_PARAMS)
	{
		snprintf(err, n, "parameter %.*s: no such parameter", (int)len, spec);
		return -1;
	}
	if (rfw_paramRefused((uint16_t)p))
	{
		snprintf(err, n, "parameter %.*s: menu_encoderChangeParameter() has a special case for it (spec 5.3)",
			(int)len, spec);
		return -1;
	}
	out->p = (uint16_t)p;
	out->v = (uint8_t)v;
	return 0;
}

void rfw_edit(void *arg)
{
	const rfw_param_t *e = arg;
	const uint16_t p = e->p;
	/* the clamp, then menu.c's send at the end of menu_encoderChangeParameter() */
	activeParameters[p] = rfw_clamp(p, e->v);
	if (p < 128)
		frontPanel_sendData(MIDI_CC, (uint8_t)p, activeParameters[p]);
	else if (p < END_OF_SOUND_PARAMETERS)
		frontPanel_sendData(CC_2, (uint8_t)(p - 128), activeParameters[p]);
	else
		menu_parseGlobalParam(p, activeParameters[p]);
}

void rfw_paramGet(void *arg)
{
	rfw_param_t *e = arg;
	e->v = activeParameters[e->p];
}

void rfw_bpm(void *value)
{
	rfw_param_t e = { PAR_BPM, *(const uint8_t *)value };
	rfw_edit(&e);
}

/* --- the save events (spec 5.3, 5.4) --------------------------------------------- */

void rfw_saveKit(void *arg)
{
	preset_saveDrumsetRam(*(const uint8_t *)arg, 0);
}

void rfw_savePattern(void *arg)
{
	(void)preset_savePatternRAM(*(const uint8_t *)arg);
}

/* menu.c, the save page's case SAVE_TYPE_PROJECT (M20 hashes the original) */
void rfw_saveProject(void *arg)
{
	const uint8_t n = *(const uint8_t *)arg;
	FRESULT res;
	if (!seq_isRunning())
		HAL_TIM_Base_Stop(&htim2);
	res = preset_saveProject(n);
	if (!seq_isRunning())
		HAL_TIM_Base_Start_IT(&htim2);
	if (res != FR_OK)
		menu_showSdError(res);
}

void rfw_selected(void *out)
{
	render_selected_t *s = out;
	s->pattern = seq_activePattern;
	s->song = -1;
	for (int i = 0; i < NUM_KIT_SLOTS; i++)
		if (seq_activeSong == seq_songData[i])
		{
			s->song = i;
			break;
		}
	s->bpm = seq_getBpm();
	s->sync = seq_getExtSync();
}

void rfw_counters(render_fwCounters_t *out)
{
	seqc_stats_t st;
	seqc_stats(&st);
	out->queueDepth = st.queueDepth;
	out->queueMax = st.queueMax;
	out->queueOverflow = st.queueOverflow;
	out->chokeDrops = st.chokeDrops;
	out->muteDrops = st.muteDrops;
	out->skipDrops = st.skipDrops;
	out->syncIgnored = st.syncIgnored;
	out->intervalsExcluded = st.intervalsExcluded;
	out->dropouts = st.dropouts;
	out->reentries = st.reentries;
	out->tempoRestarts = st.tempoRestarts;
	out->syncOverflows = seqp_syncOverflows();
	out->usbTxDropped = usb_midiTxDropped();
}
