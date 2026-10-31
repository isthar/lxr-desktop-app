/*
 * app_coreaudio.h
 *
 * The output of LICHOMAT Emu (stage 1b spec 6.2): AUHAL at 44,140.625 Hz, float32,
 * Normal converter complexity at maximum quality; listeners on one serial queue
 * push into the engine's notification ring. Open and close run on the firmware thread.
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

#endif /* APP_COREAUDIO_H_ */
