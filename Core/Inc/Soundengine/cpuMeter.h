/*
 * cpuMeter.h
 *
 * CPU meter of the LICHOMAT audio engine (spec 9.3). It adds up the cycles of the
 * audio work, seq_tick() and the render of a sample block, per rendered block and
 * compares them with the time one block lasts:
 *   64 samples / 44140 Hz x 216 MHz = 313185 cycles.
 * Once a second it publishes a reading: the mean block load and the highest block
 * load of that second, in percent. The CPU page of the global menu shows it.
 *
 * Plain C11 without the HAL, so the host tests build it too (tests/test_cpumeter.c).
 * The firmware calls it through soundengine_cpuBegin() and soundengine_cpuEnd() in
 * Soundengine.c, which pass DWT->CYCCNT. Those run in the seq_tick() context only
 * (TIM2_IRQHandler, or soundengine_process() with the TIM2 interrupt masked), so
 * the counters need no locking. The menu reads the published reading from the
 * TIM14 interrupt; a torn read only mixes two readings on the display.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef CPUMETER_H_
#define CPUMETER_H_

#include <stdint.h>

#define CPUMETER_BLOCK_CYCLES	313185u		/**< one sample block: 64 / 44140 Hz x 216 MHz, rounded */
#define CPUMETER_WINDOW_CYCLES	216000000u	/**< one reading per second of DWT->CYCCNT at 216 MHz */
#define CPUMETER_MAX_PCT	999u		/**< readings are clamped to the 3 digits of the CPU page */

/** one reading, see cpuMeter_get() */
typedef struct cpuMeter_reading
{
	uint16_t avgPct;		/**< mean load of the blocks of the last full second, % of a block */
	uint16_t peakPct;		/**< highest load of a single block in that second */
	uint16_t queueMax;		/**< seqc_stats_t.queueMax at the time of the reading */
	uint32_t queueOverflow;		/**< seqc_stats_t.queueOverflow at the time of the reading */
} cpuMeter_reading_t;

/** Start of a measured piece of audio work, now = DWT->CYCCNT. The pieces may
 * nest: a kit autoload inside seq_tick() runs soundengine_process() again. */
void cpuMeter_begin(uint32_t now);
//------------------------------------------------------------------------------
/** End of the piece of the matching cpuMeter_begin(); blockRendered: it rendered a
 * sample block. Work that rendered no block counts for the next block. A piece
 * that had nested pieces is not counted: those were counted, and the rest of it
 * is the kit load, not audio work. Returns 1 if this call published a reading. */
uint8_t cpuMeter_end(uint32_t now, uint8_t blockRendered);
//------------------------------------------------------------------------------
/** set the event queue counters of the latest reading */
void cpuMeter_setQueueStats(uint16_t queueMax, uint32_t queueOverflow);
//------------------------------------------------------------------------------
/** copy the latest reading; all zero before the first one */
void cpuMeter_get(cpuMeter_reading_t *out);
//------------------------------------------------------------------------------
/** forget everything (host tests) */
void cpuMeter_reset(void);

#endif /* CPUMETER_H_ */
