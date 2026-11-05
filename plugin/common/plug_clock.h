/*
 * plug_clock.h
 *
 * The host's transport as MIDI clock (plan E9.2 Task 4): for each block, the MIDI Timing Clocks
 * (24 a quarter note), Start, Continue, Stop and Song Position Pointers that put the firmware's
 * sequencer, a MIDI slave, where the host's transport is. A pure function of the block's
 * transport and the generator's state: no allocation, no system call.
 *
 * - Clock k is at position k / 24 quarter notes. Its frame in a block that starts at position p0
 *   at tempo T is the nearest to (k / 24 - p0) x 60 x rate / T. Clocks go out in order, each
 *   once: one that falls before the block (the host's rounding) goes out at frame 0.
 * - Playing from position 0 (or from a count-in before it): Start, with the clock of position 0.
 * - Playing from p > 0: Song Position s and Continue at the first frame, then the clocks from 16th
 *   s on (the slave's next clock plays step s, seq_core.h:752-765). s is the first 16th at or after
 *   p, or the one before it when p is at most half a clock after it, s = ceil(4p - 1/12): a host
 *   reports a loop's start a fraction of a frame late, and that 16th's clock goes out at once.
 *   Past 16th 16,383 (bar 1,024) the message carries s modulo 16,384, its 14 bits, which keeps a
 *   pattern whose length is a power of two in line; the clocks still count from the real 16th.
 * - A jump while playing, between blocks (the block's position is more than half a clock from
 *   the one expected) or at the loop's end inside a block: Stop, Song Position, Continue at the
 *   jump's frame, then the clocks from the new 16th on. The slave takes a Song Position only
 *   while stopped (seq_core.h:752-765).
 * - Stopped, or no tempo or position from the host: Stop once, then nothing.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_CLOCK_H_
#define PLUG_CLOCK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_CLOCK_PPQ 24
#define PLUG_SPP_MAX 16383	/**< the largest Song Position, in sixteenths: the message's 14 bits */

/** the host's transport at a block's first frame */
typedef struct plug_transport
{
	int valid;			/**< the host gave the tempo and the musical position */
	int playing;
	double tempo;		/**< quarter notes per minute */
	double ppq;			/**< the musical position, quarter notes */
	int cycle;			/**< the loop is on and its bounds are given */
	double cycleStart, cycleEnd;	/**< quarter notes */
} plug_transport_t;

/** one MIDI message at a frame of the block */
typedef struct plug_midi
{
	uint32_t offset;
	uint8_t len;
	uint8_t b[3];
} plug_midi_t;

typedef struct plug_clock
{
	double rate;		/**< host frames per second */
	int running;		/**< Start or Continue went out */
	int startPending;	/**< Start goes out with the clock of position 0 */
	int64_t next;		/**< the index of the next clock */
	double expect;		/**< the position expected at the next block's first frame */
} plug_clock_t;

void plug_clockInit(plug_clock_t *c, double rate);

/** forget what went out: the next playing block starts the slave afresh (a new engine) */
void plug_clockReset(plug_clock_t *c);

/** The MIDI of one block of frames frames, in frame order, into out. Returns the count, at most
 * max (the rest is left out). */
unsigned plug_clockBlock(plug_clock_t *c, const plug_transport_t *t, uint32_t frames, plug_midi_t *out, unsigned max);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_CLOCK_H_ */
