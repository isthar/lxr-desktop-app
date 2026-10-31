/*
 * app_coremidi.h
 *
 * The MIDI ports of LICHOMAT Emu (stage 1b spec 6.4, 6.5, 6.9): a virtual source
 * and destination 'LICHOMAT Emu' (the USB side), and an input and an output
 * port for the DIN endpoints the user binds. MIDI 1.0 UMP API of macOS 11.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_COREMIDI_H_
#define APP_COREMIDI_H_

#include <CoreMIDI/CoreMIDI.h>
#include <stdatomic.h>

#include "app_bind.h"
#include "app_engine.h"

#define APP_CM_MAX 64	/**< endpoints of one kind that a listing or a binding looks at */

typedef struct app_cm
{
	MIDIClientRef client;
	MIDIEndpointRef usbSrc, usbDst;
	MIDIPortRef dinIn, dinOut;
	MIDIEndpointRef dinSrc;			/**< bound; 0 'not connected'. Main thread */
	_Atomic MIDIEndpointRef dinDst;	/**< bound by the main thread, read by app_cmSend() on the MIDI-out thread */
	int32_t usbSrcUid, usbDstUid;	/**< the IDs they have now, to store */
	app_engine_t *engine;
} app_cm_t;

/** 0, or -1 with "CoreMIDI: the call: the OSStatus" in err (no server, or the server refuses); a failed init leaves
 * nothing open. srcUid and dstUid are the stored unique IDs, 0 for none. */
int app_cmInit(app_cm_t *m, app_engine_t *e, int32_t srcUid, int32_t dstUid, char *err, size_t n);
/** the system's endpoints, sources or destinations, in the system's order; m's own are left out (NULL: none) */
int app_cmList(const app_cm_t *m, int sources, app_endpoint_t *out, int max);
/** binds the endpoint that app_bindPick() chooses, or none; 1 bound, 0 not connected. Main thread */
int app_cmBind(app_cm_t *m, int source, int32_t uid, const char *name);
void app_cmSend(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n);	/**< MIDI-out sink */
void app_cmDispose(app_cm_t *m);

#endif /* APP_COREMIDI_H_ */
