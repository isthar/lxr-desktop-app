/*
 * app_engine.h
 *
 * The firmware thread of LICHOMAT Emu (stage 1b spec 6.1-6.5): power-on, slices
 * of one block, the tier hook, the sinks, and every ring between the firmware
 * thread and the outside. Time, waits and the output device come only through
 * app_host_t (plan E3). A function without a thread named runs on the firmware
 * thread.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_ENGINE_H_
#define APP_ENGINE_H_

#include <stdatomic.h>
#include <stdint.h>

#include "app_midi.h"
#include "app_ring.h"
#include "app_timemap.h"
#include "app_types.h"
#include "emu_core.h"

#define APP_AUDIO_FRAMES 16384u	/**< the audio ring, in 4-channel int16 frames (E5) */
#define APP_RING_SLACK 256u		/**< 4 blocks above the largest request (spec 6.2) */
#define APP_WARMUP 32u			/**< callbacks of an open before L is fixed (E7) */
#define APP_SNAP_BLOCKS 8u		/**< a snapshot every 8 blocks (E15) */
#define APP_STALE_SEC 0.1		/**< spec 4.3, E9 */
#define APP_STALL_SEC 0.5		/**< no callback for this long while the ring is full: a stall */
#define APP_FIRST_SEC 3.0		/**< the first callback of an open may take this long; none is a failed open */
#define APP_RETRY_SEC 2.0		/**< reopen tries while no output runs */
#define APP_D_MAX_SEC 1.0		/**< D is 0 to this */
#define APP_GEN_BPM_MAX 300.0	/**< the generator's tempo is 1 to this: the firmware's SEQC_SLAVE_BPM_MAX (app_fw.c checks it) */

typedef struct app_audioTime { uint64_t hostTime, entry; double rateScalar; } app_audioTime_t;
/** a time-map point: the ring position of the callback's first frame, its times, request and frames */
typedef struct app_point { uint64_t frame, hostTime, entry; double rateScalar; uint32_t request, got; } app_point_t;

typedef struct app_audio
{
	app_ring_t frames;				/**< int16_t[4] per frame, L1 R1 L2 R2: firmware -> callback */
	app_ring_t points;				/**< app_point_t: callback -> firmware */
	_Atomic int mix, mute;			/**< app_mix_t; 1: silence (a hang, spec 6.9) */
	_Atomic uint32_t maxRequest;	/**< the largest request so far (E6) */
	_Atomic uint64_t underruns, callbacks;	/**< written by the callback */
	void (*wake)(void *ctx);		/**< signals the firmware thread; never blocks */
	void *wakeCtx;
} app_audio_t;

/** Callback thread (spec 6.2): frames out of the ring with the mix, as int16 / 32,768,
 * silence for the rest and an underrun counted once frames were ever pushed; one
 * point; maxRequest; wake. Never blocks, allocates or locks. */
void app_audioRender(app_audio_t *a, const app_audioTime_t *ts, unsigned frames, float *out, unsigned channels);

typedef struct app_host
{
	void *ctx;
	double ticksPerSec;
	uint64_t (*now)(void *ctx);
	void (*waitAudio)(void *ctx, uint64_t until);	/**< returns at a wake() or at until */
	void (*sleepUntil)(void *ctx, uint64_t until);
	int (*outputOpen)(void *ctx, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info);	/**< 0 running */
	void (*outputClose)(void *ctx);
	void (*midiOutWake)(void *ctx);
} app_host_t;

typedef struct app_engineConfig
{
	const char *cardDir;
	uint64_t seed;
	int dAuto;
	double dSec;
	app_mix_t mix;
	app_outputRequest_t output;
	void (*fault)(void *ctx, const char *what);	/**< a model check (spec 11); NULL: print and abort() */
	void *faultCtx;
} app_engineConfig_t;

typedef struct app_engine
{
	app_host_t host;
	app_audio_t audio;
	app_ring_t panel, cmd, snap, ack, notes, midiOut;
	app_ring_t midiIn[2];	/**< [APP_PORT_USB], [APP_PORT_DIN]: one read proc each */
	_Atomic int hung;		/**< written by the watchdog thread */
	/* the firmware thread's */
	app_engineConfig_t cfg;
	app_outputRequest_t req;
	app_timemap_t map;
	app_outputInfo_t out;
	int outputRunning, dAuto, cardAck, clockLevel, clockSeen, genOn;
	int cbSeen;				/**< this open has had a callback: the stall test is armed */
	uint8_t extSync;
	uint64_t retryAt, lastCallback, frameBase, dCycles, staleFrom, prev[3];
	uint64_t genT0, genK, genNext, genMbpm, genPpq;
	uint64_t blocks, slices, lastSnap, winHost, winBlocks, waitTicks, winWait;
	uint32_t warm, lReq;
	double lLead, L, winInMin, winInMax;
	app_usbPend_t usbPend;
	app_sx_t usbOutSx;
	app_dinAsm_t din;
	app_diagnostics_t diag;
} app_engine_t;

int app_engineInit(app_engine_t *e, const app_host_t *host);	/**< allocates every ring; before the threads */
void app_enginePowerOn(app_engine_t *e, const app_engineConfig_t *cfg);
int app_engineSlice(app_engine_t *e);	/**< one block of virtual time; 1 after APP_CMD_STOP */
int app_enginePanel(app_engine_t *e, const app_panelEvent_t *ev);	/**< GUI thread */
int app_engineCommand(app_engine_t *e, const app_cmd_t *c);		/**< GUI thread */
int app_engineNote(app_engine_t *e, unsigned kind);				/**< CoreAudio's listener queue */
int app_engineMidiPush(void *ctx, const app_midiEntry_t *m);		/**< read procs: into midiIn[m->port] */
int app_engineSnapshot(app_engine_t *e, app_snapshot_t *out);		/**< GUI thread: the newest, older dropped; 1 if any */
int app_engineAck(app_engine_t *e, app_ack_t *out);				/**< GUI thread */
typedef struct app_midiOutSink
{
	void *ctx;
	void (*send)(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n);
} app_midiOutSink_t;
/** MIDI-out thread (spec 6.5, E11): sends what is due at host time now; returns the
 * presentation time of the USB entry it waits for, or 0 when the ring is empty */
uint64_t app_midiOutStep(app_engine_t *e, uint64_t now, const app_midiOutSink_t *sink);

#endif /* APP_ENGINE_H_ */
