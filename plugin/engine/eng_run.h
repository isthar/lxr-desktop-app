/*
 * eng_run.h
 *
 * The engine of the LICHOMAT plugin (plan E9.2 Task 6): the firmware on the virtual device,
 * fed from the plugin's event ring and writing the plugin's audio ring.
 *
 * - The prelude is lichomat-render's for --no-start --bpm mid --as-booted (render_main.c:
 *   1005-1047): the boot, the settle, the edit PAR_BPM = 1 (MIDI clock sync), the same direct
 *   calls in the same order, then t0 at the first block request 100 ms after it. So at
 *   44,140.625 Hz the engine's output is the renderer's, bit for bit, for the same inputs.
 * - At t0 the engine takes the plugin's hostFrames as its epoch: host frame epoch + m is cycle
 *   t0 + plug_framesToCycles(m).
 * - From then on the tier hook, which the device calls at least every block (1.45 ms) and every
 *   1 ms of a delay (emu_core.h:27-35), waits until the plugin has published the events up to
 *   66 engine frames past now, and hands every published event to the device at its cycle: the
 *   messages of one frame as one USB transfer. No input comes late. The wait is unpaced: the
 *   engine runs as soon as the plugin has published.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef ENG_RUN_H_
#define ENG_RUN_H_

#include "plug_shm.h"

/** Boot the firmware on the card folder and play until the plugin ends it. Exits: 0 when the
 * plugin sets quit, closes the wake pipe (wake, a read end; -1 for none) or goes away (also seen
 * by eng_main.c's watching thread, at any time); 3 on a firmware fault; 4 when the resampler
 * fails. */
void eng_run(plug_shm_t *s, int wake, const char *card) __attribute__((noreturn));

/** --seed-card (lichomat-render's render_seed(), render_main.c:328-350): an init card from the
 * firmware's own code into dir, missing or empty. Returns 0, or 2 with a message on stderr. */
int eng_seed(const char *dir);

/** 1 if dir is missing or holds nothing but dot entries, 0 if it holds entries, -1 if it is not
 * a folder that can be read */
int eng_cardEmpty(const char *dir);

#endif /* ENG_RUN_H_ */
