/*
 * queue_equiv.h
 *
 * Random runs of the core for the queue equivalence tests (task E1.18): the
 * fire log of a seeded random pattern, hashed, to compare with the hashes
 * recorded from the E1.16 queue, which queued every event on its own.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef QUEUE_EQUIV_H_
#define QUEUE_EQUIV_H_

#include <stdint.h>
#include "Soundengine/Sequencer/seq_core.h"

/** Run the random pattern of seed for 48 steps and return a hash of every
 * trigger (all fields of the event, in firing order) and of the final counters
 * (queueOverflow, chokeDrops, muteDrops, skipDrops, queueMax, queueDepth),
 * which it also stores in *stats.
 * The settings come from the seed: two random patterns of 64 steps (on/off,
 * ratchet, shift, probability, velocity, note, p-locks), track lengths, roll
 * rates, mutes, shuffle, the tempo, the seqc_process() interval, pattern
 * switches, and recorded notes (skip-once) between calls and from inside the
 * trigger() and stepBoundary() callbacks. */
uint64_t queue_equivRun(uint64_t seed, seqc_stats_t *stats);

#endif /* QUEUE_EQUIV_H_ */
