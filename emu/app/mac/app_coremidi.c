/*
 * app_coremidi.c
 *
 * The MIDI ports of LICHOMAT Emu (stage 1b spec 6.4, 6.5, 6.9, plan Task 8). The two
 * read blocks run on CoreMIDI's thread and only push into the engine's rings
 * (spec 6.1); app_cmSend() runs on the MIDI-out thread.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_coremidi.h"

#include <mach/mach_time.h>
#include <stdio.h>
#include <string.h>

#include "app_midilist.h"

static int app_cmFail(app_cm_t *m, char *err, size_t n, const char *call, OSStatus st)
{
	if (err && n)
		snprintf(err, n, "CoreMIDI: %s: %d", call, (int)st);
	app_cmDispose(m);
	return -1;
}

/* the endpoint's unique ID, and the ID that the system gave it if the wanted one is taken (spec 6.9) */
static SInt32 app_cmSetUid(MIDIEndpointRef ep, int32_t want)
{
	SInt32 uid = 0;
	if (want != 0)
		(void)MIDIObjectSetIntegerProperty(ep, kMIDIPropertyUniqueID, want);	/* kMIDIIDNotUnique keeps the new one */
	(void)MIDIObjectGetIntegerProperty(ep, kMIDIPropertyUniqueID, &uid);
	return uid;
}

int app_cmInit(app_cm_t *m, app_engine_t *e, int32_t srcUid, int32_t dstUid, char *err, size_t n)
{
	memset(m, 0, sizeof *m);
	m->engine = e;
	OSStatus st = MIDIClientCreateWithBlock(CFSTR("LICHOMAT Emu"), &m->client, NULL);	/* no notifications (E28) */
	if (st != noErr)
		return app_cmFail(m, err, n, "MIDIClientCreate", st);
	st = MIDISourceCreateWithProtocol(m->client, CFSTR("LICHOMAT Emu"), kMIDIProtocol_1_0, &m->usbSrc);
	if (st != noErr)
		return app_cmFail(m, err, n, "MIDISourceCreate", st);
	const MIDIReceiveBlock usbBlock = ^(const MIDIEventList *list, void *ref) {
		(void)ref;
		app_midiFromEventList(list, APP_PORT_USB, mach_absolute_time(), app_engineMidiPush, e);
	};
	const MIDIReceiveBlock dinBlock = ^(const MIDIEventList *list, void *ref) {
		(void)ref;
		app_midiFromEventList(list, APP_PORT_DIN, mach_absolute_time(), app_engineMidiPush, e);
	};
	st = MIDIDestinationCreateWithProtocol(m->client, CFSTR("LICHOMAT Emu"), kMIDIProtocol_1_0, &m->usbDst, usbBlock);
	if (st != noErr)
		return app_cmFail(m, err, n, "MIDIDestinationCreate", st);
	(void)MIDIObjectSetIntegerProperty(m->usbDst, kMIDIPropertyAdvanceScheduleTimeMuSec, 0);	/* spec 6.4 */
	m->usbSrcUid = app_cmSetUid(m->usbSrc, srcUid);
	m->usbDstUid = app_cmSetUid(m->usbDst, dstUid);
	st = MIDIInputPortCreateWithProtocol(m->client, CFSTR("DIN in"), kMIDIProtocol_1_0, &m->dinIn, dinBlock);
	if (st != noErr)
		return app_cmFail(m, err, n, "MIDIInputPortCreate", st);
	st = MIDIOutputPortCreate(m->client, CFSTR("DIN out"), &m->dinOut);
	if (st != noErr)
		return app_cmFail(m, err, n, "MIDIOutputPortCreate", st);
	return 0;
}

/* the endpoint's display name as UTF-8, cut at a character boundary to fit */
static void app_cmName(MIDIEndpointRef ep, char *out, size_t n)
{
	CFStringRef s = NULL;
	out[0] = '\0';
	if (MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &s) != noErr || s == NULL)
		return;
	CFIndex used = 0;
	(void)CFStringGetBytes(s, CFRangeMake(0, CFStringGetLength(s)), kCFStringEncodingUTF8, 0, false, (UInt8 *)out,
		(CFIndex)n - 1, &used);
	out[used] = '\0';
	CFRelease(s);
}

/* one pass for the list and the references that its indices stand for */
static int app_cmEnum(const app_cm_t *m, int sources, MIDIEndpointRef *refs, app_endpoint_t *out, int max)
{
	const ItemCount total = sources ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
	int n = 0;
	for (ItemCount i = 0; i < total && n < max; i++)
	{
		const MIDIEndpointRef ep = sources ? MIDIGetSource(i) : MIDIGetDestination(i);
		if (ep == 0 || (m && (ep == m->usbSrc || ep == m->usbDst)))
			continue;
		SInt32 uid = 0;
		(void)MIDIObjectGetIntegerProperty(ep, kMIDIPropertyUniqueID, &uid);
		out[n].uid = uid;
		app_cmName(ep, out[n].name, sizeof out[n].name);
		if (refs)
			refs[n] = ep;
		n++;
	}
	return n;
}

int app_cmList(const app_cm_t *m, int sources, app_endpoint_t *out, int max)
{
	return app_cmEnum(m, sources, NULL, out, max);
}

int app_cmBind(app_cm_t *m, int source, int32_t uid, const char *name)
{
	MIDIEndpointRef refs[APP_CM_MAX];
	app_endpoint_t list[APP_CM_MAX];
	const int n = app_cmEnum(m, source, refs, list, APP_CM_MAX);
	const int k = app_bindPick(list, n, uid, name);
	MIDIEndpointRef ep = k >= 0 ? refs[k] : 0;
	if (source)
	{
		if (ep != m->dinSrc)
		{
			if (m->dinSrc)
				(void)MIDIPortDisconnectSource(m->dinIn, m->dinSrc);	/* it may be gone already */
			if (ep && MIDIPortConnectSource(m->dinIn, ep, NULL) != noErr)
				ep = 0;	/* tried again at the next check */
			m->dinSrc = ep;
		}
	}
	else
		atomic_store(&m->dinDst, ep);
	return ep != 0;
}

void app_cmSend(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n)
{
	app_cm_t *m = ctx;
	uint32_t buf[64];
	MIDIEventList *list = (MIDIEventList *)buf;
	MIDIEventPacket *p = MIDIEventListInit(list, kMIDIProtocol_1_0);
	if (!MIDIEventListAdd(list, sizeof buf, p, when, n, w))
		return;
	if (port == APP_PORT_USB)
		(void)MIDIReceivedEventList(m->usbSrc, list);	/* at its presentation time, with it as the stamp (spec 6.5) */
	else
	{
		const MIDIEndpointRef dst = atomic_load(&m->dinDst);
		if (dst)
			(void)MIDISendEventList(m->dinOut, dst, list);	/* early, stamped: CoreMIDI schedules it (E11) */
	}
}

void app_cmDispose(app_cm_t *m)
{
	if (m->usbSrc)
		(void)MIDIEndpointDispose(m->usbSrc);
	if (m->usbDst)
		(void)MIDIEndpointDispose(m->usbDst);
	if (m->dinIn)
		(void)MIDIPortDispose(m->dinIn);
	if (m->dinOut)
		(void)MIDIPortDispose(m->dinOut);
	if (m->client)
		(void)MIDIClientDispose(m->client);
	m->usbSrc = m->usbDst = 0;
	m->dinIn = m->dinOut = 0;
	m->dinSrc = 0;
	atomic_store(&m->dinDst, 0);
	m->client = 0;
}
