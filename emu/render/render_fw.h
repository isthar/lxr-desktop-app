/*
 * render_fw.h
 *
 * Every firmware call of lichomat-render (stage 1b spec 5.3): the only file of
 * the renderer that includes firmware headers. Each function runs through
 * emu_directCall() at a pass boundary, except the lookups of rfw_paramParse()
 * and the counter getters of rfw_counters(), as their comments say.
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

#include <stddef.h>
#include <stdint.h>

/** --seed-card (spec 6.7): the steps of sd_formatRoutine() (appMain.c) after its
 * confirmation and f_mkfs(): default globals, kit 0 from preset_init(0),
 * project 0 named "Init", saved by preset_saveProject(0). res is an int that
 * receives the FRESULT. */
void rfw_seedCard(void *res);

/** preset_morphBusy() into *(uint8_t *)out */
void rfw_morphBusy(void *out);

/** an edit of the menu's edit path: parameter p (below NUM_PARAMS) to the stored
 * value v, before the clamp */
typedef struct rfw_param
{
	uint16_t p;
	uint8_t v;
} rfw_param_t;

/** NAME=VALUE (--param, the param event; stage 1b spec 5.3, plan P3) into *out.
 * NAME is a PAR_ name of Parameters.h without PAR_, in any case, or a decimal
 * parameter number below NUM_PARAMS; VALUE is 0-255. *name receives the
 * parameter's table name in lower case (for a number, the last of its aliases,
 * so 250 is bpm, not beginning_of_globals), or NULL for a number without one.
 * Returns 0, or -1 with the problem in err: not NAME=VALUE, a value out of
 * range, "parameter NAME: no such parameter", or "parameter NAME:
 * menu_encoderChangeParameter() has a special case for it (spec 5.3)" for the
 * LFO voices and the modulation targets. A lookup in const tables, not a
 * firmware call: it runs at any time, also before power-on. */
int rfw_paramParse(const char *spec, rfw_param_t *out, const char **name, char *err, size_t n);

/** the menu's edit path (spec 5.3, plan P3, menu.c menu_encoderChangeParameter()):
 * clamp v by parameter_dtypes[p] as the menu does, set activeParameters[p], then
 * the menu's send: frontPanel_sendData() with MIDI_CC or CC_2 for a sound
 * parameter, menu_parseGlobalParam() for any other. arg is an rfw_param_t. */
void rfw_edit(void *arg);

/** activeParameters[p] into v; arg is an rfw_param_t */
void rfw_paramGet(void *arg);

/** --bpm (plan P3): the edit PAR_BPM = *(uint8_t *)value through rfw_edit(): 2-255
 * a tempo, 0 EXT, 1 MID (CortexParser.c FRONT_SET_BPM) */
void rfw_bpm(void *value);

/** The save events (spec 5.3, 5.4): the RAM half of the save page's saves, and
 * its project save. arg points to the slot or project, a uint8_t 0-63.
 * - save-kit: preset_saveDrumsetRam(N, 0), the active kit into kit slot N;
 * - save-pattern: preset_savePatternRAM(N), the active pattern into slot N,
 *   which then becomes the active pattern when it is another slot;
 * - save-project: menu.c's SAVE_TYPE_PROJECT case: TIM2 stopped while the
 *   sequencer is stopped, preset_saveProject(N), TIM2 started again, and
 *   menu_showSdError() on a failure. */
void rfw_saveKit(void *arg);
void rfw_savePattern(void *arg);
void rfw_saveProject(void *arg);

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
