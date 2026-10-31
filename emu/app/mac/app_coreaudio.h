/*
 * app_coreaudio.h
 *
 * The output of LICHOMAT Emu (stage 1b spec 6.2): AUHAL at 44,140.625 Hz, float32,
 * Normal converter complexity at maximum quality; listeners on one serial queue
 * push into the engine's notification ring. Open and close run on the firmware thread.
 * The input of the clock jacks as audio (plan R5, spec 6.8) is a second AUHAL unit, opened and closed by the main thread.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_COREAUDIO_H_
#define APP_COREAUDIO_H_

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>

#include "app_engine.h"

#define APP_CA_LISTEN 6u	/**< the property listeners of an open: five on the device, one on the system object */

typedef struct app_caListen { AudioObjectID obj; AudioObjectPropertyAddress addr; } app_caListen_t;

typedef struct app_ca
{
	AudioUnit unit;
	AudioObjectID device;
	int followDefault;			/**< no UID asked, or none found: follow the system's default output */
	unsigned channels;
	app_audio_t *audio;
	app_engine_t *engine;		/**< for app_engineNote(); set by the caller before app_caOpen() */
	dispatch_queue_t queue;		/**< the listeners' serial queue: one producer (spec 6.1) */
	AudioObjectPropertyListenerBlock listener;	/**< Block_copy()'d: Add and Remove need the same pointer */
	app_caListen_t listen[APP_CA_LISTEN];
	unsigned nListen;
	int initialized, started;	/**< what app_caClose() has to undo */
} app_ca_t;

int app_caList(app_outputDevice_t *out, int max);	/**< the output devices (app_types.h) */
/** 0 running, else -1 with "the call: the OSStatus" (or "no output device") in err; a failed open leaves nothing open */
int app_caOpen(app_ca_t *ca, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info, char *err, size_t n);
void app_caClose(app_ca_t *ca);	/**< stops the unit and the listeners; the callback has returned for good when it returns */
/** AUHAL input of o (plan R5 F9): the clock jacks as audio. One unit at the device's own rate and all its inputs up to 64,
 * float32 interleaved; the input callback runs app_jackInRender() on in */
typedef struct app_ci
{
	AudioUnit unit;
	AudioObjectID device;
	int followDefault, initialized, started;
	unsigned channels;		/**< the client's interleaved channels: the device's inputs, at most 64 */
	UInt32 maxFrames;
	float *buf;
	AudioBufferList abl;	/**< the callback's: one interleaved buffer over buf */
	app_jackIn_t *in;
	dispatch_queue_t queue;
	AudioObjectPropertyListenerBlock listener;
	app_caListen_t listen[4];
	unsigned nListen;
	_Atomic int changed;	/**< a listener saw the device change; the main thread reopens (F9) */
	app_outputInfo_t info;	/**< the input's name, rate, latencies, buffer, channels */
} app_ci_t;

int app_caInputs(app_outputDevice_t *out, int max);	/**< the devices with input channels */
/** AUHAL input of o (F9); app_jackInStart(in) is called before the start. 0, or -1 with the reason in err and nothing left
 * open. Main thread; only with the microphone permission (F10) */
int app_ciOpen(app_ci_t *ci, app_jackIn_t *in, const app_clockInOptions_t *o, double ticksPerSec, char *err, size_t n);
void app_ciClose(app_ci_t *ci);	/**< the callback has returned for good when it returns */
int app_ciProbe(const char *uid, app_outputInfo_t *info);	/**< the input's properties without IO (no permission needed); -1: no device */
/** the input unit's callback (ref is the app_ci_t), here for a test to call */
OSStatus app_ciInput(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus, UInt32 frames,
	AudioBufferList *io);

/** the unit's render callback (ref is the app_ca_t), here for a test to call */
OSStatus app_caRender(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus, UInt32 frames,
	AudioBufferList *io);

#endif /* APP_COREAUDIO_H_ */
