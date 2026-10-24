/*
 * seq_core.c
 *
 * Timing core of the LICHOMAT sequencer, see seq_core.h.
 *
 * This is the skeleton of task E1.1: every function of the interface exists,
 * so the host test harness builds and links, but nothing plays yet. The
 * tasks E1.3 to E1.12 replace it with the timing core.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */

#include "Soundengine/Sequencer/seq_core.h"

#include <string.h>

static seqc_state_t seqc_skeletonState;

void seqc_init(const seqc_host_t *host, uint32_t cyclesPerSecond)
{
	(void)host;
	(void)cyclesPerSecond;
	seqc_skeletonState = SEQC_STOPPED;
}
void seqc_setSyncMode(seqc_sync_t mode) { (void)mode; }
void seqc_setTempo(float bpm) { (void)bpm; }
void seqc_setAnalogPpq(uint8_t ppq) { (void)ppq; }
void seqc_start(void) { seqc_skeletonState = SEQC_RUNNING; }
void seqc_stop(void) { seqc_skeletonState = SEQC_STOPPED; }
void seqc_rewind(void) {}
void seqc_syncEvent(seqc_sync_ev_t ev, seqc_time_t stamp) { (void)ev; (void)stamp; }
void seqc_songPosition(uint16_t sixteenths) { (void)sixteenths; }
void seqc_process(seqc_time_t now) { (void)now; }
void seqc_skipOnce(uint8_t track, uint8_t step) { (void)track; (void)step; }
uint8_t seqc_trackStep(uint8_t track) { (void)track; return SEQC_NO_STEP; }
int32_t seqc_absStep(void) { return -1; }
uint8_t seqc_masterTrack(void) { return 0; }
double seqc_position(void) { return 0.0; }
float seqc_stepPhase(void) { return 0.0f; }
float seqc_currentBpm(void) { return SEQC_DEFAULT_BPM; }
seqc_state_t seqc_state(void) { return seqc_skeletonState; }
bool seqc_isRunning(void) { return seqc_skeletonState != SEQC_STOPPED; }
void seqc_stats(seqc_stats_t *out) { memset(out, 0, sizeof *out); }
void seqc_resetStats(void) {}
