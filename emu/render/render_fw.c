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

#include "main.h"
#include "Parameters.h"
#include "menu/menu.h"
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
	sprintf(savefileNames[0][PROJECT_NAMES], "Init");
	*(int *)res = (int)preset_saveProject(0);
}

void rfw_morphBusy(void *out)
{
	*(uint8_t *)out = preset_morphBusy();
}

void rfw_bpm(void *value)
{
	/* the menu's edit path for PAR_BPM (menu.c menu_encoderChangeParameter()): its
	 * dtype is DTYPE_0B255, so the clamp keeps every value */
	activeParameters[PAR_BPM] = *(const uint8_t *)value;
	menu_parseGlobalParam(PAR_BPM, activeParameters[PAR_BPM]);
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
