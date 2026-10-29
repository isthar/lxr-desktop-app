/*
 * render_fw.h
 *
 * Every firmware call of lichomat-render (stage 1b spec 5.3): the only file of
 * the renderer that includes firmware headers. Each function runs through
 * emu_directCall() at a pass boundary.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef RENDER_FW_H_
#define RENDER_FW_H_

#include <stdint.h>

/** --seed-card (spec 6.7): the steps of sd_formatRoutine() (appMain.c) after its
 * confirmation and f_mkfs(): default globals, kit 0 from preset_init(0),
 * project 0 named "Init", saved by preset_saveProject(0). res is an int that
 * receives the FRESULT. */
void rfw_seedCard(void *res);

/** preset_morphBusy() into *(uint8_t *)out */
void rfw_morphBusy(void *out);

/** --bpm (stage 1b plan P3): the menu's edit of PAR_BPM to *(uint8_t *)value,
 * 2-255 a tempo, 0 EXT, 1 MID (CortexParser.c FRONT_SET_BPM). Task R2.5's edit
 * path for every parameter takes its place. */
void rfw_bpm(void *value);

/** what plays (the summary's selected, spec 5.5) */
typedef struct render_selected
{
	int pattern;	/**< seq_activePattern */
	int song;		/**< S with seq_activeSong == seq_songData[S], or -1 */
	int bpm;		/**< seq_getBpm() */
	int sync;		/**< seq_getExtSync(): 0 internal, 1 EXT, 2 MID */
} render_selected_t;

/** rfw_selected(render_selected_t *), through emu_directCall() */
void rfw_selected(void *out);

/** the firmware's counters for the summary: the fields of seqc_stats_t by their
 * C names, seqp_syncOverflows() and usb_midiTxDropped() */
typedef struct render_fwCounters
{
	uint32_t queueDepth, queueMax, queueOverflow, chokeDrops, muteDrops, skipDrops, syncIgnored,
		intervalsExcluded, dropouts, reentries, tempoRestarts;
	uint32_t syncOverflows, usbTxDropped;
} render_fwCounters_t;

/** Copy the three counter getters' values. They only copy counters, so the end
 * of the render reads them from the tier hook (stage 1b plan R2.3, spec 5.3
 * step 7): the one firmware read that is not a direct call. */
void rfw_counters(render_fwCounters_t *out);

#endif /* RENDER_FW_H_ */
