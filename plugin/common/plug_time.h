/*
 * plug_time.h
 *
 * Host frames, the engine's virtual cycles and the plugin's latency (plan E9.2 Task 5).
 *
 * - The engine runs at 44,140.625 Hz (353,125 / 8), 216 MHz cycles. At exactly that host rate
 *   the plugin passes the engine's frames through ("native": no resampling, bit for bit the
 *   renderer's); at any other rate the engine resamples (eng_resample.c).
 * - An event at host frame m reaches the firmware at cycle t0 + plug_framesToCycles(m - epoch).
 *   It sounds 128 engine frames later (the voice queue, emu_onsets.py:43-45); resampled, the
 *   converter adds 48 engine frames (Normal complexity, prime method None: measured while
 *   planning, 47.9-48.1 frames at 44.1-96 kHz).
 * - The plugin reads the ring plug_readOffset() frames behind the frame it plays: the engine
 *   has the events of a block only after that block, and needs one block more of events than
 *   it renders (eng_run.c). The reported latency is the sound's delay plus that offset, so an
 *   event at host frame m sounds at frame m + plug_latency() of the plugin's output.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_TIME_H_
#define PLUG_TIME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_ENGINE_RATE (353125.0 / 8.0)	/**< 44,140.625 Hz */
#define PLUG_VOICE_DELAY 128u				/**< engine frames from an input to its sound */
#define PLUG_CONVERTER_DELAY 48u			/**< engine frames the converter adds */
#define PLUG_MARGIN_DEFAULT 128u			/**< host frames of slack for the engine's scheduling */

/** 1 when rate is exactly 44,140.625 Hz: no resampling */
int plug_isNative(double rate);

/** host frames from an event to its sound: 128, or round(176 x rate / 44,140.625) */
uint32_t plug_soundDelay(double rate);

/** How far behind the played frame the plugin reads the ring: maxBlock + margin + the engine's lag,
 * 1 frame native and ceil(rate / 44,140.625) + 1 resampled. With the events up to host frame H
 * published, the engine has written at least up to H - 1 engine frames (eng_run.c: a hook at block
 * request k needs the events up to 64k + 66 and then gives block k + 1, frames 64k + 64 to 64k + 127). */
uint32_t plug_readOffset(double rate, uint32_t maxBlock, uint32_t margin);

/** the reported latency: plug_soundDelay() + plug_readOffset() */
uint32_t plug_latency(double rate, uint32_t maxBlock, uint32_t margin);

/** host frames after the epoch to cycles after t0, rounded down; exact at the native rate and
 * at an integer rate */
uint64_t plug_framesToCycles(uint64_t frames, double rate);

/** the host frames after the epoch that cover cycles after t0: the smallest m with
 * plug_framesToCycles(m) >= cycles */
uint64_t plug_framesCovering(uint64_t cycles, double rate);

/** the margin: LICHOMAT_PLUGIN_MARGIN (16-4096 host frames) if set, else PLUG_MARGIN_DEFAULT */
uint32_t plug_marginFromEnv(void);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_TIME_H_ */
