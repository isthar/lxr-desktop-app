/*
 * test_app_devices.c
 *
 * R4 (stage 1b spec 6.2, plan E22): the device glue against the real system. Each
 * probe runs in a forked child for at most 5 s and skips (exit 77) when there is
 * no output device, when the server refuses, or when there is no answer: CI has
 * no audio device and the sandbox may refuse or hang CoreAudio.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <mach/mach_time.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "app_coreaudio.h"
#include "app_coremidi.h"
#include "app_mach.h"
#include "app_shell.h"
#include "emu_hostfs.h"
#include "emu_test.h"

/* fn in a forked child for at most 5 s: its exit code, or 77 without an answer (E22). The parent
 * never touches CoreAudio or CoreMIDI, so the child starts clean. */
static int probe(int (*fn)(void), const char *id)
{
	fflush(stdout);
	const pid_t pid = fork();
	if (pid == 0)
	{
		const int r = fn();
		fflush(stdout);	/* _exit() does not */
		_exit(r);
	}
	for (int i = 0; i < 100; i++)
	{
		int status;
		if (waitpid(pid, &status, WNOHANG) == pid)
			return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
		const struct timespec d = { 0, 50000000L };
		nanosleep(&d, NULL);
	}
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	printf("SKIP %s: no answer within 5 s\n", id);
	return 77;
}

#define PROBE(fn, id) do { const int r_ = probe(fn, id); if (r_ == 77) exit(77); ASSERT_EQ(r_, 0); } while (0)

static int coreaudioChild(void)
{
	static app_engine_t e;	/* its audio rings only */
	app_host_t h;
	memset(&h, 0, sizeof h);
	h.ticksPerSec = 24e6;
	if (app_engineInit(&e, &h) != 0)
		return 1;
	app_ca_t ca;
	memset(&ca, 0, sizeof ca);
	ca.engine = &e;
	const app_outputRequest_t req = { "", 0u, 2u };	/* the default output at its own buffer */
	app_outputInfo_t info;
	char err[256];
	if (app_caOpen(&ca, &e.audio, &req, &info, err, sizeof err) != 0)
	{
		printf("SKIP R4_dev_coreaudio: %s\n", err);
		return 77;
	}
	const struct timespec d = { 1, 0 };
	nanosleep(&d, NULL);
	app_caClose(&ca);
	const double want = info.bufferFrames * 44140.625 / info.deviceRate;
	const unsigned got = atomic_load(&e.audio.maxRequest);
	printf("%s: %.0f Hz, %u frames, client %.6f Hz, %s; %llu callbacks, largest request %u (expected %.1f)\n",
		info.name, info.deviceRate, info.bufferFrames, info.clientRate, info.format,
		(unsigned long long)atomic_load(&e.audio.callbacks), got, want);
	if (!(info.clientRate == 44140.625 && atomic_load(&e.audio.callbacks) >= 5u && fabs(got - want) <= 2.0))
		return 1;
	/* the engine reopens at a stall, a device change and a mix change: open and close must repeat cleanly. Four
	 * channels asked of a stereo device give two. */
	const app_outputRequest_t four = { "", 0u, 4u };
	for (int k = 0; k < 3; k++)
	{
		const uint64_t before = atomic_load(&e.audio.callbacks);
		if (app_caOpen(&ca, &e.audio, &four, &info, err, sizeof err) != 0)
		{
			printf("cycle %d: %s\n", k, err);
			return 1;
		}
		nanosleep(&(const struct timespec){ 0, 100000000L }, NULL);
		app_caClose(&ca);
		if (atomic_load(&e.audio.callbacks) < before + 3u || (info.channels != 2u && info.channels != 4u))
		{
			printf("cycle %d: %llu callbacks, %u channels\n", k,
				(unsigned long long)(atomic_load(&e.audio.callbacks) - before), info.channels);
			return 1;
		}
	}
	return 0;
}

TEST(R4_dev_coreaudio) { PROBE(coreaudioChild, "R4_dev_coreaudio"); }

static _Atomic unsigned gSeen;	/* what the probe's own input port got from our source */
static _Atomic uint32_t gWord;
static _Atomic uint64_t gStamp;

/* spec 6.4, 6.5, 6.9: stored unique IDs; packets stamped +10, +20, +30 ms reach the read proc at their stamps; what
 * app_cmSend() hands to the virtual source reaches a port connected to it, with its stamp */
static int coremidiChild(void)
{
	static app_engine_t e;
	app_host_t h;
	memset(&h, 0, sizeof h);
	h.ticksPerSec = 24e6;
	if (app_engineInit(&e, &h) != 0)
		return 1;
	app_cm_t m;
	char err[256];
	const int32_t uid = (int32_t)(0x4C450000u | ((uint32_t)getpid() & 0xFFFFu));
	if (app_cmInit(&m, &e, uid, uid + 1, err, sizeof err) != 0)
	{
		printf("SKIP R4_dev_coremidi: %s\n", err);
		return 77;
	}
	int ok = m.usbSrcUid == uid && m.usbDstUid == uid + 1;
	if (!ok)
		printf("unique IDs: source %d, destination %d (asked %d, %d)\n", m.usbSrcUid, m.usbDstUid, uid, uid + 1);
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	const uint64_t step = (uint64_t)(0.010 * 1e9 * tb.denom / tb.numer), t0 = mach_absolute_time();
	MIDIPortRef out;
	ok &= MIDIOutputPortCreate(m.client, CFSTR("probe"), &out) == noErr;
	uint32_t buf[64];
	MIDIEventList *l = (MIDIEventList *)buf;
	MIDIEventPacket *p = MIDIEventListInit(l, kMIDIProtocol_1_0);
	const UInt32 clock = 0x10F80000u;
	for (unsigned k = 1; k <= 3u; k++)
		p = MIDIEventListAdd(l, sizeof buf, p, t0 + k * step, 1u, &clock);
	ok &= MIDISendEventList(out, m.usbDst, l) == noErr;
	unsigned got = 0;
	while (got < 3u && mach_absolute_time() < t0 + 100u * step)
	{
		const app_midiEntry_t *x = app_ringPeek(&e.midiIn[APP_PORT_USB], 0u);
		if (!x)
			continue;
		const uint64_t now = mach_absolute_time(), want = t0 + (got + 1u) * step;
		printf("packet %u: stamp %+.3f ms, arrived %+.3f ms after it\n", got + 1u,
			(double)(x->stamp - t0) / step * 10.0, ((double)now - (double)want) / step * 10.0);
		ok &= x->stamp == want && now + step / 10u >= want;	/* not before its stamp - 1 ms */
		app_ringPop(&e.midiIn[APP_PORT_USB], 1u);
		got++;
	}
	/* the out path: the virtual source to a port of this client */
	MIDIPortRef in;
	const uint32_t start = 0x10FA0000u;
	ok &= MIDIInputPortCreateWithProtocol(m.client, CFSTR("probe in"), kMIDIProtocol_1_0, &in,
		^(const MIDIEventList *list, void *ref) {
			(void)ref;
			atomic_store(&gWord, list->packet[0].words[0]);
			atomic_store(&gStamp, list->packet[0].timeStamp);
			atomic_fetch_add(&gSeen, 1u);
		}) == noErr;
	ok &= MIDIPortConnectSource(in, m.usbSrc, NULL) == noErr;
	const uint64_t when = mach_absolute_time();
	app_cmSend(&m, APP_PORT_USB, when, &start, 1u);
	while (atomic_load(&gSeen) == 0u && mach_absolute_time() < when + 100u * step)
		;
	printf("out: %u packets, word %08x, stamp %+.3f ms\n", atomic_load(&gSeen), atomic_load(&gWord),
		(double)(int64_t)(atomic_load(&gStamp) - when) / step * 10.0);
	ok &= atomic_load(&gSeen) == 1u && atomic_load(&gWord) == start && atomic_load(&gStamp) == when;
	app_cmDispose(&m);
	return ok && got == 3u ? 0 : 1;
}

TEST(R4_dev_coremidi) { PROBE(coremidiChild, "R4_dev_coremidi"); }

/* spec 6.5, 6.9: the whole shell with MIDI and without audio. Play on the init card (120 BPM) gives 48 clocks a second
 * on the virtual source at their presentation times, and Note Ons on a DIN destination that the test binds; the
 * endpoints are gone when app_shellStop() returns. */
#define MO_MAX 1024u
static char gCard[1024];
static char gWhy[256];
static _Atomic int gNoMidi;
static _Atomic unsigned gClocks, gDinNotes, gDinOther;
static uint64_t gClockStamp[MO_MAX], gClockArrive[MO_MAX];	/* the read block's; read after the port is disposed */

static void midioutMessage(void *ctx, const char *text)
{
	(void)ctx;
	if (strstr(text, "MIDI is off") != NULL)
	{
		snprintf(gWhy, sizeof gWhy, "%s", text);
		atomic_store(&gNoMidi, 1);
	}
}

static int cmpDouble(const void *a, const void *b)
{
	const double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

static int midioutChild(void)
{
	app_shell_t *s = app_shellNew();
	const int32_t uid = (int32_t)(0x4C460000u | ((uint32_t)getpid() & 0xFFFFu));
	app_shellOptions_t o;
	char err[256] = "";
	memset(&o, 0, sizeof o);
	o.cardDir = gCard;
	o.noAudio = 1;
	o.message = midioutMessage;
	o.usbSrcUid = uid;
	o.usbDstUid = uid + 1;
	if (!s || app_shellStart(s, &o, err, sizeof err) != 0)
	{
		printf("start: %s\n", err);
		return 1;
	}
	if (atomic_load(&gNoMidi))
	{
		app_shellStop(s);
		printf("SKIP R4_dev_midiout: %s\n", gWhy);
		return 77;
	}
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	const double ms = 1e6 * tb.denom / tb.numer;	/* ticks per millisecond */
	MIDIClientRef client = 0;
	MIDIPortRef in = 0;
	MIDIEndpointRef dst = 0;
	MIDIObjectRef obj = 0;
	MIDIObjectType type;
	int ok = MIDIClientCreate(CFSTR("probe"), NULL, NULL, &client) == noErr;
	ok &= MIDIObjectFindByUniqueID(uid, &obj, &type) == noErr && type == kMIDIObjectType_Source;
	ok &= MIDIInputPortCreateWithProtocol(client, CFSTR("in"), kMIDIProtocol_1_0, &in, ^(const MIDIEventList *list, void *ref) {
		(void)ref;
		const uint64_t now = mach_absolute_time();
		const MIDIEventPacket *p = &list->packet[0];
		for (UInt32 i = 0; i < list->numPackets; i++, p = MIDIEventPacketNext(p))
			for (UInt32 k = 0; k < p->wordCount; k++)
				if (p->words[k] == 0x10F80000u)
				{
					const unsigned n = atomic_load(&gClocks);
					if (n < MO_MAX)
					{
						gClockStamp[n] = p->timeStamp;
						gClockArrive[n] = now;
						atomic_store(&gClocks, n + 1u);
					}
				}
	}) == noErr;
	ok &= MIDIPortConnectSource(in, (MIDIEndpointRef)obj, NULL) == noErr;
	ok &= MIDIDestinationCreateWithProtocol(client, CFSTR("R4 probe DIN"), kMIDIProtocol_1_0, &dst,
		^(const MIDIEventList *list, void *ref) {
			(void)ref;
			const MIDIEventPacket *p = &list->packet[0];
			for (UInt32 i = 0; i < list->numPackets; i++, p = MIDIEventPacketNext(p))
				for (UInt32 k = 0; k < p->wordCount; k++)
					atomic_fetch_add(p->words[k] >> 28 == 2u && ((p->words[k] >> 20) & 0xFu) == 9u ? &gDinNotes : &gDinOther, 1u);
		}) == noErr;
	SInt32 dstUid = 0;
	ok &= MIDIObjectGetIntegerProperty(dst, kMIDIPropertyUniqueID, &dstUid) == noErr;
	app_endpoint_t list[APP_CM_MAX];
	int found = 0, ours = 0;
	const int count = app_shellEndpoints(s, 0, list, APP_CM_MAX);	/* the destinations: the probe's is there, ours is left out */
	for (int i = 0; i < count; i++)
	{
		found += list[i].uid == dstUid && !strcmp(list[i].name, "R4 probe DIN");
		ours += list[i].uid == uid + 1;
	}
	ok &= found == 1 && ours == 0;
	app_shellBind(s, 0, dstUid, "R4 probe DIN");
	const double t0 = app_wallSeconds();
	app_panelEvent_t ev;
	memset(&ev, 0, sizeof ev);
	ev.kind = APP_P_BUTTON;
	ev.index = 33;
	int pressed = 0, released = 0;
	for (double t = 0.0; t < 4.5; t = app_wallSeconds() - t0)
	{
		if (!pressed && t >= 1.5)
		{
			ev.value = 1;
			ok &= app_shellPanel(s, &ev) == 0;
			pressed = 1;
		}
		else if (pressed && !released && t >= 1.55)
		{
			ev.value = 0;
			ok &= app_shellPanel(s, &ev) == 0;
			released = 1;
		}
		(void)app_shellFrame(s);
		nanosleep(&(const struct timespec){ 0, 5000000L }, NULL);
	}
	ok &= MIDIPortDispose(in) == noErr;	/* no read block runs after this */
	app_shellStop(s);
	const unsigned n = atomic_load(&gClocks);
	double delta[MO_MAX], early = 1e9, late = -1e9, med = 0.0;
	unsigned off = 0;
	for (unsigned i = 0; i < n; i++)
	{
		const double after = ((double)gClockArrive[i] - (double)gClockStamp[i]) / ms;
		early = fmin(early, after);
		late = fmax(late, after);
		if (i > 0u)
		{
			delta[i - 1u] = (double)(gClockStamp[i] - gClockStamp[i - 1u]) / ms;
			off += fabs(delta[i - 1u] - 1000.0 / 48.0) > 2.5;
		}
	}
	if (n >= 2u)
	{
		qsort(delta, n - 1u, sizeof delta[0], cmpDouble);
		med = delta[(n - 1u) / 2u];
	}
	/* the firmware's clock moves in its 1.45 ms blocks: spacings of 19.75 to 21.75 ms around the median, and a short one
	 * (3.5 ms) in some runs where play restarts its phase. The first clocks of the run arrive up to 17 ms after their
	 * stamps because the firmware thread pushes them late, so only an arrival before the stamp fails */
	printf("clocks: %u, median spacing %.4f ms (%.4f), %u more than 2.5 ms off; arrival %.3f to %.3f ms after the stamp\n",
		n, med, 1000.0 / 48.0, off, early, late);
	printf("DIN: %u Note Ons, %u other words\n", atomic_load(&gDinNotes), atomic_load(&gDinOther));
	ok &= n >= 100u && fabs(med - 1000.0 / 48.0) < 0.1 && off <= 3u && early > -1.0;	/* not before its stamp - 1 ms */
	ok &= atomic_load(&gDinNotes) >= 4u;
	ok &= MIDIObjectFindByUniqueID(uid, &obj, &type) != noErr;	/* disposed by app_shellStop() */
	ok &= MIDIObjectFindByUniqueID(uid + 1, &obj, &type) != noErr;
	MIDIEndpointDispose(dst);
	MIDIClientDispose(client);
	return ok ? 0 : 1;
}

TEST(R4_dev_midiout)
{
	emu_testTempDir(gCard, sizeof gCard);
	ASSERT_EQ(emu_hostCopyCard(EMU_TEST_CARDS "/init", gCard, 0), 0);
	PROBE(midioutChild, "R4_dev_midiout");
}

TEST_MAIN()
