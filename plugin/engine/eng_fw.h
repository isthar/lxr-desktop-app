/*
 * eng_fw.h
 *
 * The engine's reads of the firmware's memory (plan E9.4 Task 6): the values of the plugin's
 * parameters, the global MIDI channel and the receive filter. The only file of the engine that
 * includes firmware headers. They are reads, not calls: the tier hook may make them (emu_core.h),
 * as lichomat-render reads its counters from the hook (render_fw.h, rfw_counters()). The
 * firmware runs on the hook's thread and stands still while it runs, so each value is whole; one
 * read in the middle of a kit load may hold part of the new kit, and the next block's the rest.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef ENG_FW_H_
#define ENG_FW_H_

#include <stddef.h>
#include <stdint.h>

#include "plug_params.h"

/** the firmware's values of plug_params[] (activeParameters[par], the next pattern for the
 * Pattern), its global MIDI channel (0-15, or PLUG_VALUE_NONE) and the receive filter's bits */
void eng_fwValues(uint8_t values[PLUG_PARAM_COUNT], uint8_t *channel, uint8_t *rxFilter);

/** 0 when every parameter of plug_params[] is the firmware's parameter it names (Parameters.h);
 * else -1 with the first that is not in err */
int eng_fwCheck(char *err, size_t n);

/** the same check on a table of n rows (plug_params[]'s order; a test's copy with a row changed) */
int eng_fwCheckTable(const plug_param_t *table, unsigned n, char *err, size_t errlen);

#endif /* ENG_FW_H_ */
