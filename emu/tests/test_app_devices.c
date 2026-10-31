/*
 * test_app_devices.c
 *
 * R4 (stage 1b spec 6.2, plan E22): the device glue against the real system. Each
 * probe runs in a forked child for at most its own number of seconds (5 to 15) and skips (exit 77) when there is
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
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "app_coreaudio.h"
#include "app_card.h"
#include "app_coremidi.h"
#include "app_mach.h"
#include "app_shell.h"
#include "emu_hostfs.h"
#include "emu_test.h"

/* fn in a forked child for at most sec seconds: its exit code, or 77 without an answer (E22). The parent
 * never touches CoreAudio or CoreMIDI, so the child starts clean. */
static int probe(int (*fn)(void), const char *id, int sec)
{
	fflush(stdout);
	const pid_t pid = fork();
	if (pid == 0)
	{
		const int r = fn();
		fflush(stdout);	/* _exit() does not */
		_exit(r);
	}
	for (int i = 0; i < sec * 20; i++)
	{
		int status;
		if (waitpid(pid, &status, WNOHANG) == pid)
			return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
		const struct timespec d = { 0, 50000000L };
		nanosleep(&d, NULL);
	}
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	printf("SKIP %s: no answer within %d s\n", id, sec);
	return 77;
}

#define PROBE(fn, id, sec) do { const int r_ = probe(fn, id, sec); if (r_ == 77) exit(77); ASSERT_EQ(r_, 0); } while (0)

/* callbacks until there are n, at most sec seconds: 1 if they came. A slow device gets its time; a fast one is not waited for */
static int waitCallbacks(app_engine_t *e, uint64_t n, double sec)
{
	const double t0 = app_wallSeconds();
	while (atomic_load(&e->audio.callbacks) < n && app_wallSeconds() - t0 < sec)
		nanosleep(&(const struct timespec){ 0, 5000000L }, NULL);
	return atomic_load(&e->audio.callbacks) >= n;
}

/* the smallest and the largest request among the callbacks that the ring of points holds */
static void requestSpread(app_ring_t *points, unsigned *lo, unsigned *hi)
{
	*lo = UINT_MAX;
	*hi = 0u;
	for (uint32_t i = 0, n = app_ringCount(points); i < n; i++)
	{
		const app_point_t *p = app_ringPeek(points, i);
		*lo = p->request < *lo ? p->request : *lo;
		*hi = p->request > *hi ? p->request : *hi;
	}
}

/* a device whose buffer size is fixed asks 470 or 471 frames in turn (512 at 48 kHz, in 44,140.625 Hz frames); one that
 * varies asks more, and the probe skips it */
#define SPREAD_MAX 2u

TEST(R4_dev_spread)
{
	static app_engine_t e;
	app_host_t h;
	memset(&h, 0, sizeof h);
	h.ticksPerSec = 24e6;
	ASSERT_EQ(app_engineInit(&e, &h), 0);
	unsigned lo, hi;
	requestSpread(&e.audio.points, &lo, &hi);
	ASSERT_EQ(lo, UINT_MAX);	/* no callback: nothing to say */
	app_point_t p = { 0u, 0u, 0u, 1.0, 470u, 470u };
	for (unsigned k = 0; k < 4u; k++)
	{
		p.request = 470u + (k & 1u);
		ASSERT_EQ(app_ringPush(&e.audio.points, &p), 0);
	}
	requestSpread(&e.audio.points, &lo, &hi);
	ASSERT_EQ(lo, 470);
	ASSERT_EQ(hi, 471);
	ASSERT_TRUE(hi - lo <= SPREAD_MAX);
	p.request = 940u;	/* a device that asks twice as much now and then */
	ASSERT_EQ(app_ringPush(&e.audio.points, &p), 0);
	requestSpread(&e.audio.points, &lo, &hi);
	ASSERT_EQ(hi, 940);
	ASSERT_TRUE(hi - lo > SPREAD_MAX);
}

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
	const int came = waitCallbacks(&e, 20u, 3.0);	/* about 0.2 s at 512 frames; as long as it takes at 4,096 */
	app_caClose(&ca);
	unsigned lo, hi;
	requestSpread(&e.audio.points, &lo, &hi);
	if (hi > lo + SPREAD_MAX)
	{
		printf("SKIP R4_dev_coreaudio: %s asks %u to %u frames: its buffer size varies\n", info.name, lo, hi);
		return 77;
	}
	const double want = info.bufferFrames * 44140.625 / info.deviceRate;
	const unsigned got = atomic_load(&e.audio.maxRequest);
	printf("%s: %.0f Hz, %u frames, client %.6f Hz, %s; %llu callbacks, largest request %u (expected %.1f)\n",
		info.name, info.deviceRate, info.bufferFrames, info.clientRate, info.format,
		(unsigned long long)atomic_load(&e.audio.callbacks), got, want);
	if (!(info.clientRate == 44140.625 && (came || atomic_load(&e.audio.callbacks) >= 5u) && fabs(got - want) <= 2.0))
		return 1;
	/* the engine reopens at a stall, a device change and a mix change: open and close must repeat cleanly. The unit has the
	 * channels asked for (the mix's 2 or 4, or the clock's channel, R5 F3), as many as the device has, and at least two */
	static const unsigned ask[] = { 4u, 3u, 8u, 2u };
	for (int k = 0; k < 4; k++)
	{
		const uint64_t before = atomic_load(&e.audio.callbacks);
		const app_outputRequest_t more = { "", 0u, ask[k] };
		if (app_caOpen(&ca, &e.audio, &more, &info, err, sizeof err) != 0)
		{
			printf("cycle %d: %s\n", k, err);
			return 1;
		}
		(void)waitCallbacks(&e, before + 3u, 2.0);	/* at its own pace, not 3 in 100 ms */
		app_caClose(&ca);
		unsigned want = ask[k] < info.deviceChannels ? ask[k] : info.deviceChannels;
		want = want < 2u ? 2u : want;
		if (atomic_load(&e.audio.callbacks) < before + 3u || info.channels != want || info.deviceChannels == 0u)
		{
			printf("cycle %d: %llu callbacks, %u of %u channels (asked %u, expected %u)\n", k,
				(unsigned long long)(atomic_load(&e.audio.callbacks) - before), info.channels, info.deviceChannels, ask[k], want);
			return 1;
		}
	}
	return 0;
}

TEST(R4_dev_coreaudio) { PROBE(coreaudioChild, "R4_dev_coreaudio", 10); }

/* the verdict of R5_dev_input on what its input did in 6 s: 0 pass, 1 fail, 77 skip. Callbacks that arrive and never reach
 * app_jackInRender() are a fault, not a missing permission: the input would be dead while the diagnostics call it running
 * (review of 876c9f6: a device buffer larger than the unit's slice) */
static int inputVerdict(uint64_t entries, uint64_t skipped, uint64_t callbacks, double lead)
{
	if (entries > 0u && callbacks == 0u)
	{
		printf("FAIL R5_dev_input: %llu input callbacks entered, none reached app_jackInRender (%llu skipped)\n",
			(unsigned long long)entries, (unsigned long long)skipped);
		return 1;
	}
	if (callbacks < APP_IN_WARMUP + 8u)
	{
		printf("SKIP R5_dev_input: no input callbacks in 6 s (permission pending?)\n");
		return 77;
	}
	return lead > 0.0 && lead < 0.5 ? 0 : 1;
}

/* R5 (F13): the input's properties need no permission; the open starts input IO, which prompts the terminal for the
 * microphone, so it runs only with LICHOMAT_EMU_INPUT_TEST=1. No input device, no permission and no callbacks are a skip */
static int inputChild(void)
{
	app_outputDevice_t d[32];
	app_outputInfo_t info;
	if (app_caInputs(d, 32) == 0 || app_ciProbe("", &info) != 0)
	{
		printf("SKIP R5_dev_input: no input device\n");
		return 77;
	}
	printf("%s: %.0f Hz, %u ch, %u frames, latency %u + %u, safety offset %u\n", info.name, info.deviceRate, info.channels,
		info.bufferFrames, info.deviceLatency, info.streamLatency, info.safetyOffset);
	if (!(info.deviceRate > 0.0 && info.channels >= 1u))
		return 1;
	const char *go = getenv("LICHOMAT_EMU_INPUT_TEST");
	if (!go || strcmp(go, "1") != 0)
	{
		printf("SKIP R5_dev_input: the open needs the microphone permission; LICHOMAT_EMU_INPUT_TEST=1 runs it\n");
		return 77;
	}
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	const double tps = 1e9 * tb.denom / tb.numer;
	static app_jackIn_t in;
	static app_ci_t ci;
	const app_clockInOptions_t o = { 1, "", 1, 0, 0.5, 0.5 };
	char err[256] = "";
	if (app_ringInit(&in.edges, sizeof(app_panelEvent_t), 256u) != 0 || app_ciOpen(&ci, &in, &o, tps, err, sizeof err) != 0)
	{
		printf("SKIP R5_dev_input: open: %s\n", err);
		return 77;
	}
	const double t0 = app_wallSeconds();
	while (atomic_load(&in.callbacks) < APP_IN_WARMUP + 8u && app_wallSeconds() - t0 < 6.0)
		nanosleep(&(const struct timespec){ 0, 5000000L }, NULL);
	const uint64_t entries = atomic_load(&ci.entries);	/* app_ciClose() clears the input's counters */
	app_ciClose(&ci);
	const double lead = (double)atomic_load(&in.lead) / tps;
	printf("%llu input callbacks of %llu entered (%llu skipped), lead %.2f ms\n", (unsigned long long)atomic_load(&in.callbacks),
		(unsigned long long)entries, (unsigned long long)atomic_load(&in.skipped), lead * 1e3);
	return inputVerdict(entries, atomic_load(&in.skipped), atomic_load(&in.callbacks), lead);
}

TEST(R5_dev_input) { PROBE(inputChild, "R5_dev_input", 12); }

TEST(R5_dev_input_verdict)
{
	ASSERT_EQ(inputVerdict(0u, 0u, 0u, 0.0), 77);		/* nothing arrived: the permission may be pending */
	ASSERT_EQ(inputVerdict(120u, 120u, 0u, 0.0), 1);	/* callbacks arrive and none reaches the detector: a fault */
	ASSERT_EQ(inputVerdict(40u, 0u, 0u, 0.0), 1);
	ASSERT_EQ(inputVerdict(12u, 0u, 12u, 0.0), 77);		/* too slow to judge */
	ASSERT_EQ(inputVerdict(400u, 0u, 400u, 0.012), 0);
	ASSERT_EQ(inputVerdict(400u, 3u, 397u, 0.012), 0);
	ASSERT_EQ(inputVerdict(400u, 0u, 400u, 0.0), 1);	/* no lead */
	ASSERT_EQ(inputVerdict(400u, 0u, 400u, 0.6), 1);	/* a lead of more than half a second */
}

/* --- R5, review of 876c9f6: the input follows the device's buffer. No device or IO is needed in any of these ------------ */

/* the unit's slice and the callback's buffer hold the device's largest buffer, not the buffer of the day: AUHAL's own
 * slice followed the device (512, then 1024 and 2048 when the Audio window raised it) and a snapshot at the open went stale */
TEST(R5_ci_slice)
{
	const AudioValueRange built = { 14.0, 4096.0 }, big = { 64.0, 16384.0 }, none = { 0.0, 0.0 }, silly = { 1.0, 4e9 };
	ASSERT_EQ(app_ciSliceFrames(&built, 512u), 4096);
	ASSERT_EQ(app_ciSliceFrames(&built, 2048u), 4096);
	ASSERT_EQ(app_ciSliceFrames(&big, 512u), 16384);
	ASSERT_EQ(app_ciSliceFrames(&none, 512u), 4096);	/* no range known: what the first version assumed */
	ASSERT_EQ(app_ciSliceFrames(NULL, 512u), 4096);
	ASSERT_EQ(app_ciSliceFrames(&none, 8192u), 8192);	/* a buffer beyond it is held all the same */
	ASSERT_EQ(app_ciSliceFrames(&built, 6000u), 6000);
	ASSERT_EQ(app_ciSliceFrames(&silly, 512u), APP_CI_SLICE_MAX);
	ASSERT_EQ(app_ciSliceFrames(&built, 1u << 24), APP_CI_SLICE_MAX);
}

/* a change of the device's buffer reopens the input (app_shellFrame() polls the flag), or it measures its lead at the old one */
TEST(R5_ci_watch)
{
	app_ci_t ci;
	memset(&ci, 0, sizeof ci);
	ci.device = 77u;
	unsigned n = app_ciWatch(&ci);
	ASSERT_EQ(n, 4);
	unsigned alive = 0, rate = 0, config = 0, buffer = 0;
	for (unsigned i = 0; i < n; i++)
	{
		const AudioObjectPropertyAddress *a = &ci.listen[i].addr;
		ASSERT_EQ(ci.listen[i].obj, 77);
		alive += a->mSelector == kAudioDevicePropertyDeviceIsAlive;
		rate += a->mSelector == kAudioDevicePropertyNominalSampleRate;
		config += a->mSelector == kAudioDevicePropertyStreamConfiguration;
		if (a->mSelector == kAudioDevicePropertyBufferFrameSize)
		{
			buffer++;
			ASSERT_EQ(a->mScope, kAudioObjectPropertyScopeWildcard);	/* the HAL keeps it in either scope */
		}
	}
	ASSERT_TRUE(alive == 1u && rate == 1u && config == 1u && buffer == 1u);
	ci.followDefault = 1;
	n = app_ciWatch(&ci);
	ASSERT_EQ(n, 5);
	ASSERT_TRUE(n <= APP_CI_LISTEN);
	ASSERT_EQ(ci.listen[4].obj, kAudioObjectSystemObject);
	ASSERT_EQ(ci.listen[4].addr.mSelector, kAudioHardwarePropertyDefaultInputDevice);
}

/* a callback that cannot be served is counted, not dropped in silence: it is what a dead input looks like from outside */
TEST(R5_ci_oversize)
{
	static app_jackIn_t in;
	static app_ci_t ci;
	const app_jackInConfig_t cfg = { 24e6, 48000.0, 40u, 2u, 0u, 1u, 0.5f, 0.5f };
	app_jackInStart(&in, &cfg);
	ci.in = &in;
	ci.channels = 2u;
	ci.maxFrames = 256u;
	AudioTimeStamp ts;
	memset(&ts, 0, sizeof ts);
	ts.mFlags = kAudioTimeStampHostTimeValid;
	ts.mHostTime = 1000u;
	AudioUnitRenderActionFlags flags = 0;
	ASSERT_EQ(app_ciInput(&ci, &flags, &ts, 1u, 512u, NULL), noErr);	/* more frames than the buffer holds */
	ASSERT_EQ(atomic_load(&ci.entries), 1);
	ASSERT_EQ(atomic_load(&in.skipped), 1);
	ASSERT_EQ(atomic_load(&in.callbacks), 0);
	ts.mFlags = 0;	/* no host time to put the edges at */
	ASSERT_EQ(app_ciInput(&ci, &flags, &ts, 1u, 128u, NULL), noErr);
	ASSERT_EQ(atomic_load(&ci.entries), 2);
	ASSERT_EQ(atomic_load(&in.skipped), 2);
	ASSERT_EQ(app_ciInput(&ci, &flags, &ts, 1u, 0u, NULL), noErr);	/* nothing to serve: entered, not a loss */
	ASSERT_EQ(atomic_load(&ci.entries), 3);
	ASSERT_EQ(atomic_load(&in.skipped), 2);
	ASSERT_EQ(atomic_load(&in.callbacks), 0);
}

/* one channel cannot be the clock and the reset: said, before any device is touched */
TEST(R5_ci_samechannel)
{
	static app_jackIn_t in;
	static app_ci_t ci;
	char err[256] = "";
	const app_clockInOptions_t o = { 1, "", 3, 3, 0.5, 0.5 };
	ASSERT_EQ(app_ciOpen(&ci, &in, &o, 24e6, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "both on input channel 3") != NULL);
	ASSERT_EQ(ci.started, 0);
	ASSERT_EQ(ci.unit == NULL, 1);
}

/* a unit that had started when the open failed (or any started unit that is closed) lets a high reset go: its callback has
 * returned for good, so this thread may push the release (F11) */
static int ciReleaseChild(void)
{
	const AudioComponentDescription desc = { kAudioUnitType_Output, kAudioUnitSubType_HALOutput,
		kAudioUnitManufacturer_Apple, 0, 0 };
	AudioComponent comp = AudioComponentFindNext(NULL, &desc);
	static app_jackIn_t in;
	static app_ci_t ci;
	const app_jackInConfig_t cfg = { 24e6, 48000.0, 40u, 2u, 0u, 1u, 0.5f, 0.5f };
	if (!comp || app_ringInit(&in.edges, sizeof(app_panelEvent_t), 16u) != 0)
	{
		printf("SKIP R5_ci_release: no HAL unit\n");
		return 77;
	}
	app_jackInStart(&in, &cfg);
	memset(&ci, 0, sizeof ci);
	ci.in = &in;
	if (AudioComponentInstanceNew(comp, &ci.unit) != noErr)
	{
		printf("SKIP R5_ci_release: no HAL unit\n");
		return 77;
	}
	ci.started = 1;		/* the unit never runs here (no IO, no permission); close treats it as one that did */
	in.reset.high = 1;	/* and the jack was high when it stopped */
	app_ciClose(&ci);
	const app_panelEvent_t *e = app_ringPeek(&in.edges, 0u);
	const int ok = e != NULL && app_ringCount(&in.edges) == 1u && e->kind == APP_P_RESET && e->value == 0 && !in.reset.high
		&& atomic_load(&in.lead) == 0u;
	if (!ok)
		printf("the release of the high reset is missing: %u events in the ring\n", (unsigned)app_ringCount(&in.edges));
	return ok ? 0 : 1;
}

TEST(R5_ci_release) { PROBE(ciReleaseChild, "R5_ci_release", 5); }

/* spec 6.2: the render callback never leaves a buffer as it found it. A buffer that is not the interleaved one the format
 * promised gets silence, not whatever it held (it would play as noise). No device is needed. */
TEST(R4_dev_render_short)
{
	static app_engine_t e;
	app_host_t h;
	memset(&h, 0, sizeof h);
	h.ticksPerSec = 24e6;
	ASSERT_EQ(app_engineInit(&e, &h), 0);
	app_ca_t ca;
	memset(&ca, 0, sizeof ca);
	ca.channels = 2u;
	ca.audio = &e.audio;
	static uint8_t one[100], two[64], full[512 * 2 * sizeof(float)];
	struct { UInt32 n; AudioBuffer b[2]; } io;
	AudioUnitRenderActionFlags flags = 0;
	AudioTimeStamp ts;
	memset(&ts, 0, sizeof ts);
	memset(one, 0x55, sizeof one);
	memset(two, 0x55, sizeof two);
	io.n = 1u;
	io.b[0] = (AudioBuffer){ 2u, sizeof one, one };	/* an interleaved buffer that is too short for 512 frames */
	ASSERT_EQ(app_caRender(&ca, &flags, &ts, 0, 512u, (AudioBufferList *)&io), noErr);
	for (size_t i = 0; i < sizeof one; i++)
		ASSERT_EQ(one[i], 0);
	memset(one, 0x55, sizeof one);
	io.n = 2u;	/* two buffers: the non-interleaved layout, which the format did not promise */
	io.b[1] = (AudioBuffer){ 1u, sizeof two, two };
	ASSERT_EQ(app_caRender(&ca, &flags, &ts, 0, 512u, (AudioBufferList *)&io), noErr);
	for (size_t i = 0; i < sizeof one; i++)
		ASSERT_EQ(one[i], 0);
	for (size_t i = 0; i < sizeof two; i++)
		ASSERT_EQ(two[i], 0);
	io.n = 0u;	/* no buffer: nothing to write */
	ASSERT_EQ(app_caRender(&ca, &flags, &ts, 0, 512u, (AudioBufferList *)&io), noErr);
	memset(full, 0x55, sizeof full);
	io.n = 1u;
	io.b[0] = (AudioBuffer){ 2u, sizeof full, full };	/* the promised buffer: rendered, silence from an empty ring */
	ASSERT_EQ(app_caRender(&ca, &flags, &ts, 0, 512u, (AudioBufferList *)&io), noErr);
	for (size_t i = 0; i < sizeof full; i++)
		ASSERT_EQ(full[i], 0);
	ASSERT_EQ(atomic_load(&e.audio.callbacks), 1);
}

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

TEST(R4_dev_coremidi) { PROBE(coremidiChild, "R4_dev_coremidi", 5); }

/* spec 6.5, 6.9: the whole shell with MIDI and without audio. Play on the init card (120 BPM) gives 48 clocks a second
 * on the virtual source at their presentation times, and Note Ons on a DIN destination that the test binds; the
 * endpoints are gone when app_shellStop() returns. */
#define MO_MAX 1024u
static char gCard[1024], gHome[1024];
static char gWhy[256];
static _Atomic int gNoMidi;
static _Atomic unsigned gClocks, gDinNotes, gDinOther, gStartAt = UINT_MAX;	/* gStartAt: the clocks seen before the FA */
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
	o.home = gHome;
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
				if (p->words[k] == 0x10FA0000u)
				{
					unsigned none = UINT_MAX;	/* the first Start only */
					atomic_compare_exchange_strong(&gStartAt, &none, atomic_load(&gClocks));
				}
				else if (p->words[k] == 0x10F80000u)
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
	const unsigned n = atomic_load(&gClocks), start = atomic_load(&gStartAt);
	double delta[MO_MAX], early = 1e9, late = -1e9, med = 0.0, steadyLate = -1e9, aMin = 1e9, aMax = -1e9;
	unsigned off = 0, spacingBad = 0;
	for (unsigned i = 0; i < n; i++)
	{
		const double after = ((double)gClockArrive[i] - (double)gClockStamp[i]) / ms;
		early = fmin(early, after);
		late = fmax(late, after);
		if (i >= 10u)
			steadyLate = fmax(steadyLate, after);
		if (i > 0u)
		{
			delta[i - 1u] = (double)(gClockStamp[i] - gClockStamp[i - 1u]) / ms;
			off += fabs(delta[i - 1u] - 1000.0 / 48.0) > 2.5;
			/* A4: the F8 intervals as they arrive are 20.83 +- 1.5 ms, from the 10th clock on. The first clocks come out of
			 * the firmware's post-splash work, which is not wall-paced: while other render processes run it takes longer than
			 * the 25 ms render lead, and the first F8 reaches the MIDI-out ring 16 to 131 ms after its stamp (the first USB
			 * program change 53 to 241 ms), so their spacing is not the thread's (it sends what it sees at once). A4 is a
			 * steady-state check. The interval from the last F8 before Start to the first one after it is the firmware's: it
			 * restarts the phase of its clocks 1 ms after the FA (M18_out_start_phase) */
			const double gap = ((double)gClockArrive[i] - (double)gClockArrive[i - 1u]) / ms;
			if (i != start && i >= 10u)
			{
				aMin = fmin(aMin, gap);
				aMax = fmax(aMax, gap);
				spacingBad += fabs(gap - 1000.0 / 48.0) > 1.5;
			}
		}
	}
	if (n >= 2u)
	{
		qsort(delta, n - 1u, sizeof delta[0], cmpDouble);
		med = delta[(n - 1u) / 2u];
	}
	/* The firmware's clock moves in its 1.45 ms blocks: spacings of 19.75 to 21.75 ms around the median, and one short or
	 * long one where Start restarts its phase. The wall clock's render lead (25 ms) puts every message in the ring before its
	 * stamp, so even a firmware thread that is held up for a few ms delivers on time. */
	printf("clocks: %u, median spacing %.4f ms (%.4f), %u more than 2.5 ms off; arrival %.3f to %.3f ms after the stamp "
		"(%.3f from the 10th on)\n", n, med, 1000.0 / 48.0, off, early, late, steadyLate);
	printf("arrival spacing %.3f to %.3f ms, %u outside 20.83 +- 1.5 ms (Start at clock %u)\n", aMin, aMax, spacingBad, start);
	printf("DIN: %u Note Ons, %u other words\n", atomic_load(&gDinNotes), atomic_load(&gDinOther));
	ok &= n >= 100u && start < n && fabs(med - 1000.0 / 48.0) < 0.1 && off <= 3u && early > -1.0;	/* not before its stamp - 1 ms */
	ok &= spacingBad == 0u;
	ok &= atomic_load(&gDinNotes) >= 4u;
	ok &= MIDIObjectFindByUniqueID(uid, &obj, &type) != noErr;	/* disposed by app_shellStop() */
	ok &= MIDIObjectFindByUniqueID(uid + 1, &obj, &type) != noErr;
	/* spec 6.9: the unique IDs that the endpoints have are stored, to be asked for again at the next start */
	char path[1100], text[4096] = "", want[64];
	snprintf(path, sizeof path, "%s/settings.txt", gHome);
	FILE *f = fopen(path, "r");
	if (f)
	{
		text[fread(text, 1, sizeof text - 1u, f)] = '\0';
		fclose(f);
	}
	snprintf(want, sizeof want, "usb.source.uid = %d\n", (int)uid);
	ok &= strstr(text, want) != NULL;
	snprintf(want, sizeof want, "usb.destination.uid = %d\n", (int)uid + 1);
	ok &= strstr(text, want) != NULL;
	MIDIEndpointDispose(dst);
	MIDIClientDispose(client);
	return ok ? 0 : 1;
}

TEST(R4_dev_midiout)
{
	emu_testTempDir(gCard, sizeof gCard);
	emu_testTempDir(gHome, sizeof gHome);
	ASSERT_EQ(emu_hostCopyCard(EMU_TEST_CARDS "/init", gCard, 0), 0);
	PROBE(midioutChild, "R4_dev_midiout", 15);
}

/* spec 6.7, 6.9: a card operation waits for ITS 'card out applied'. A late acknowledgement of an operation that has timed out
 * must not start the next one before the card is out: the shell compares the number that the acknowledgement echoes. Neither
 * audio nor MIDI is needed. */
static char gOpDst[1024];
static _Atomic int gOpDone, gRelaunches;

static void cardopMessage(void *ctx, const char *text)
{
	(void)ctx;
	if (strstr(text, "exported the card into") != NULL)
		atomic_store(&gOpDone, 1);
}

static int cardopRelaunch(void *ctx)
{
	(void)ctx;
	atomic_fetch_add(&gRelaunches, 1);
	return 0;
}

static int cardopChild(void)
{
	app_shell_t *s = app_shellNew();
	app_shellOptions_t o;
	char err[256] = "";
	memset(&o, 0, sizeof o);
	o.cardDir = gCard;
	o.home = gHome;
	o.noAudio = 1;
	o.noMidi = 1;
	o.message = cardopMessage;
	o.relaunch = cardopRelaunch;
	if (!s || app_shellStart(s, &o, err, sizeof err) != 0)
	{
		printf("start: %s\n", err);
		return 1;
	}
	int ok = app_shellCardOp(s, APP_OP_EXPORT, gOpDst, 0, 0) == 0;
	app_shellTestAck(s, APP_ACK_CARD_OUT, 424242);	/* the late one of an earlier operation: not this one's */
	char names[256];
	ok &= !atomic_load(&gOpDone) && app_cardNonDot(gOpDst, names, sizeof names) == 0;	/* nothing is copied yet */
	if (!ok)
		printf("a stale acknowledgement started the export\n");
	const double t0 = app_wallSeconds();
	while (!atomic_load(&gOpDone) && app_wallSeconds() - t0 < 6.0)
	{
		(void)app_shellFrame(s);
		nanosleep(&(const struct timespec){ 0, 2000000L }, NULL);
	}
	char proj[1100];
	snprintf(proj, sizeof proj, "%s/PROJ00", gOpDst);
	ok &= atomic_load(&gOpDone) && emu_hostKind(proj) == 1;	/* its own acknowledgement: now it is copied */
	/* the power cycle waits for its own as well, and does not fall back on its 2 s deadline when it comes */
	app_shellPowerCycle(s);
	app_shellTestAck(s, APP_ACK_CARD_OUT, 424243);
	ok &= atomic_load(&gRelaunches) == 0;
	const double t1 = app_wallSeconds();
	while (atomic_load(&gRelaunches) == 0 && app_wallSeconds() - t1 < 4.0)
	{
		(void)app_shellFrame(s);
		nanosleep(&(const struct timespec){ 0, 2000000L }, NULL);
	}
	const double took = app_wallSeconds() - t1;
	printf("export done: %d; power cycle: %d relaunch(es) after %.3f s\n", atomic_load(&gOpDone), atomic_load(&gRelaunches), took);
	ok &= atomic_load(&gRelaunches) == 1 && took < 1.5;
	app_shellStop(s);
	return ok ? 0 : 1;
}

TEST(R4_dev_cardop)
{
	emu_testTempDir(gCard, sizeof gCard);
	emu_testTempDir(gHome, sizeof gHome);
	emu_testTempDir(gOpDst, sizeof gOpDst);
	ASSERT_EQ(emu_hostCopyCard(EMU_TEST_CARDS "/init", gCard, 0), 0);
	PROBE(cardopChild, "R4_dev_cardop", 15);
}

/* spec 6.9, E18: once a new process is on its way, a second click (the hang popup's Restart, the power cycle, a card
 * operation) must not start a third. The first relaunch that fails clears the guard: there is nothing on its way then. */
static int relaunchCounted(void *ctx)
{
	atomic_fetch_add(&gRelaunches, 1);
	return *(int *)ctx;	/* the result that this call gives */
}

static void relaunchMessage(void *ctx, const char *text)
{
	(void)ctx;
	if (strstr(text, "restarting") != NULL)
		atomic_fetch_add(&gOpDone, 1);
}

static int relaunchChild(void)
{
	app_shell_t *s = app_shellNew();
	app_shellOptions_t o;
	char err[256] = "";
	int result = -1;	/* the first relaunch fails */
	memset(&o, 0, sizeof o);
	o.cardDir = gCard;
	o.home = gHome;
	o.noAudio = 1;
	o.noMidi = 1;
	o.message = relaunchMessage;
	o.relaunch = relaunchCounted;
	o.ctx = &result;
	if (!s || app_shellStart(s, &o, err, sizeof err) != 0)
	{
		printf("start: %s\n", err);
		return 1;
	}
	int ok = !app_shellRelaunching(s);
	app_shellRestart(s);
	ok &= atomic_load(&gRelaunches) == 1 && !app_shellRelaunching(s);	/* it failed: nothing is on its way */
	result = 0;
	app_shellRestart(s);
	ok &= atomic_load(&gRelaunches) == 2 && app_shellRelaunching(s);
	app_shellRestart(s);	/* the hang popup's second click */
	app_shellPowerCycle(s);	/* the menu */
	ok &= app_shellCardOp(s, APP_OP_EXPORT, gOpDst, 0, 0) == -1;
	ok &= atomic_load(&gRelaunches) == 2;
	const double t0 = app_wallSeconds();
	while (app_wallSeconds() - t0 < 2.5)	/* past the power cycle's 2 s deadline, the one that would restart again */
	{
		(void)app_shellFrame(s);
		nanosleep(&(const struct timespec){ 0, 5000000L }, NULL);
	}
	ok &= atomic_load(&gRelaunches) == 2 && app_shellRelaunching(s);
	ok &= atomic_load(&gOpDone) == 2;	/* 'restarting' was said for the two relaunches, not for the clicks that came after */
	printf("relaunches: %d, restarting said %d time(s)\n", atomic_load(&gRelaunches), atomic_load(&gOpDone));
	app_shellStop(s);
	return ok ? 0 : 1;
}

TEST(R4_dev_relaunch)
{
	emu_testTempDir(gCard, sizeof gCard);
	emu_testTempDir(gHome, sizeof gHome);
	emu_testTempDir(gOpDst, sizeof gOpDst);
	ASSERT_EQ(emu_hostCopyCard(EMU_TEST_CARDS "/init", gCard, 0), 0);
	PROBE(relaunchChild, "R4_dev_relaunch", 15);
}

/* spec 4.2, 6.2: the stop command is taken between slices, and the firmware thread may be in a wait of the engine, the first
 * callback of an output (3 s), when it is given. app_shellStop() waits for that, not for 2 s. The thread is held up 3.5 s
 * here; neither audio nor MIDI is needed. */
static int stopwaitChild(void)
{
	app_shell_t *s = app_shellNew();
	app_shellOptions_t o;
	char err[256] = "";
	memset(&o, 0, sizeof o);
	o.cardDir = gCard;
	o.home = gHome;
	o.noAudio = 1;
	o.noMidi = 1;
	if (!s || app_shellStart(s, &o, err, sizeof err) != 0)
	{
		printf("start: %s\n", err);
		return 1;
	}
	const double t0 = app_wallSeconds();
	for (const app_snapshot_t *sn = NULL; (!sn || sn->vt < (uint64_t)(1.2 * EMU_CPU_HZ)) && app_wallSeconds() - t0 < 6.0;
		sn = app_shellSnapshot(s))	/* 0.2 s after the boot, which is the first slice */
	{
		(void)app_shellFrame(s);
		nanosleep(&(const struct timespec){ 0, 5000000L }, NULL);
	}
	app_shellTestStall(s, 3500);
	nanosleep(&(const struct timespec){ 0, 50000000L }, NULL);	/* the thread is in its sleep */
	const double t1 = app_wallSeconds();
	const int rc = app_shellStop(s);
	printf("stop returned %d after %.3f s\n", rc, app_wallSeconds() - t1);
	return rc == 0 ? 0 : 1;
}

TEST(R4_dev_stopwait)
{
	emu_testTempDir(gCard, sizeof gCard);
	emu_testTempDir(gHome, sizeof gHome);
	ASSERT_EQ(emu_hostCopyCard(EMU_TEST_CARDS "/init", gCard, 0), 0);
	PROBE(stopwaitChild, "R4_dev_stopwait", 15);
}

/* spec 6.1: the MIDI-out thread's period is the output's buffer duration, and the buffer changes (a new device, a new size).
 * The thread follows it: the policy is set again when the period has changed and not otherwise. */
static struct { int r1, r2, r3; double p1, p2; } gFollow;

static double policyPeriod(void)	/* the calling thread's time-constraint period, in seconds */
{
	thread_time_constraint_policy_data_t p;
	mach_msg_type_number_t count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
	boolean_t dflt = FALSE;
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	if (thread_policy_get(mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&p, &count, &dflt) != KERN_SUCCESS)
		return -1.0;
	return (double)p.period * tb.numer / tb.denom / 1e9;
}

static void *followThread(void *arg)
{
	(void)arg;
	double applied = 0.0;
	gFollow.r1 = app_threadRealtimeFollow(&applied, 0.010);
	gFollow.p1 = policyPeriod();
	gFollow.r2 = app_threadRealtimeFollow(&applied, 0.010);
	gFollow.r3 = app_threadRealtimeFollow(&applied, 0.020);
	gFollow.p2 = policyPeriod();
	return NULL;
}

TEST(R4_dev_period)
{
	pthread_t t;
	ASSERT_EQ(pthread_create(&t, NULL, followThread, NULL), 0);
	pthread_join(t, NULL);
	if (gFollow.r1 < 0)
	{
		printf("SKIP R4_dev_period: the system refuses the time-constraint policy\n");
		exit(77);
	}
	ASSERT_EQ(gFollow.r1, 1);
	ASSERT_NEAR(gFollow.p1, 0.010, 0.0001);
	ASSERT_EQ(gFollow.r2, 0);	/* the same period: not set again */
	ASSERT_EQ(gFollow.r3, 1);
	ASSERT_NEAR(gFollow.p2, 0.020, 0.0002);	/* the new period took */
}

/* R5 (F9-F12): the shell and the two options, with no input IO. A clock channel of 64 fails before a unit is made on any
 * device that has fewer inputs (and a machine without one has none), so no microphone prompt can come from this test.
 * The C side never opens the input by itself (F10), a failed open is said once and retried, off clears it */
static char gClkSays[4][300];
static _Atomic unsigned gClkSaid;

static void clockinMessage(void *ctx, const char *text)
{
	(void)ctx;
	const unsigned n = atomic_load(&gClkSaid);
	if (strstr(text, "clock in from audio") != NULL && n < 4u)
	{
		snprintf(gClkSays[n], sizeof gClkSays[n], "%s", text);
		atomic_store(&gClkSaid, n + 1u);
	}
}

static void frames(app_shell_t *s, double sec)
{
	const double t0 = app_wallSeconds();
	while (app_wallSeconds() - t0 < sec)
	{
		(void)app_shellFrame(s);
		nanosleep(&(const struct timespec){ 0, 5000000L }, NULL);
	}
}

/* frames until the shell has tried to open the input want times, at most sec seconds */
static unsigned framesUntilTries(app_shell_t *s, unsigned want, double sec)
{
	const double t0 = app_wallSeconds();
	while (app_shellTestInputTries(s) < want && app_wallSeconds() - t0 < sec)
	{
		(void)app_shellFrame(s);
		nanosleep(&(const struct timespec){ 0, 5000000L }, NULL);
	}
	return app_shellTestInputTries(s);
}

#define CK(c) do { if (!(c)) { printf("check failed, line %d: %s\n", __LINE__, #c); ok = 0; } } while (0)

static int clockinChild(void)
{
	app_outputDevice_t in[32];
	const int n = app_caInputs(in, 32);
	for (int i = 0; i < n; i++)
		if (in[i].channels >= APP_CLOCK_CHANNELS)
		{
			printf("SKIP R5_shell_clockin: %s has %u inputs, so the open of channel 64 would start input IO\n", in[i].name,
				in[i].channels);
			return 77;
		}
	app_shell_t *s = app_shellNew();
	app_shellOptions_t o;
	char err[256] = "";
	memset(&o, 0, sizeof o);
	o.cardDir = gCard;
	o.home = gHome;
	o.noAudio = 1;
	o.noMidi = 1;
	o.message = clockinMessage;
	if (!s || app_shellStart(s, &o, err, sizeof err) != 0)
	{
		printf("start: %s\n", err);
		return 1;
	}
	const app_settings_t *set = app_shellSettings(s);
	int ok = 1;
	CK(set->clockOutAudio == 1 && set->clockOutChannel == 3 && set->clockInAudio == 1 && set->clockInChannel == 64);
	frames(s, 0.3);
	CK(atomic_load(&gClkSaid) == 0u);	/* the stored option does not open the input: the GUI asks for the permission first */
	/* the clock-out command: the flag and the channel, as the engine will take them */
	app_cmd_t c;
	memset(&c, 0, sizeof c);
	c.kind = APP_CMD_CLOCK_OUT;
	c.a = 0;
	CK(app_shellCommand(s, &c) == 0 && set->clockOutAudio == 0 && set->clockOutChannel == 3);
	c.a = 5;
	CK(app_shellCommand(s, &c) == 0 && set->clockOutAudio == 1 && set->clockOutChannel == 5);
	c.a = 65;	/* the engine takes it as off */
	CK(app_shellCommand(s, &c) == 0 && set->clockOutAudio == 0 && set->clockOutChannel == 5);
	/* the options of the input, off: stored, in range only */
	app_clockInOptions_t x;
	memset(&x, 0, sizeof x);
	snprintf(x.uid, sizeof x.uid, "dev:x");
	x.clockChannel = 7;
	x.resetChannel = 8;
	x.clockThreshold = 0.3;
	x.resetThreshold = 0.7;
	CK(app_shellClockIn(s, &x) == 0);
	CK(set->clockInAudio == 0 && !strcmp(set->clockInDevice, "dev:x") && set->clockInChannel == 7 && set->resetChannel == 8
		&& set->clockInThreshold == 0.3 && set->resetThreshold == 0.7);
	x.clockChannel = 65;
	x.resetChannel = 70;
	x.clockThreshold = 0.01;
	x.resetThreshold = 0.99;
	CK(app_shellClockIn(s, &x) == 0);
	CK(set->clockInChannel == 7 && set->resetChannel == 8 && set->clockInThreshold == 0.3 && set->resetThreshold == 0.7);
	/* on, with a channel no device has: no unit, no IO; said once, however often it is tried */
	x.clockChannel = 64;
	x.resetChannel = 0;
	x.clockThreshold = x.resetThreshold = 0.5;
	x.on = 1;
	app_outputInfo_t info;
	const char *why = "";
	CK(app_shellClockIn(s, &x) == -1 && set->clockInAudio == 1 && set->clockInChannel == 64 && set->resetChannel == 0);
	CK(!app_shellInputInfo(s, &info, &why) && why[0] != '\0');
	CK(atomic_load(&gClkSaid) == 1u && !strncmp(gClkSays[0], "notice: clock in from audio: ", 29));
	CK(app_shellTestInputTries(s) == 1u);
	/* the retry every 2 s is counted: the same text is not said again, so the message cannot tell that it ran */
	CK(framesUntilTries(s, 2u, 4.0) == 2u);
	frames(s, 0.5);	/* and the next one is 2 s after that, not at every frame */
	CK(app_shellTestInputTries(s) == 2u);
	CK(atomic_load(&gClkSaid) == 1u && !app_shellInputInfo(s, &info, &why) && why[0] != '\0');
	printf("said: %s\n", gClkSays[0]);
	x.on = 0;
	CK(app_shellClockIn(s, &x) == 0 && set->clockInAudio == 0 && !app_shellInputInfo(s, &info, &why) && why[0] == '\0');
	frames(s, 2.4);
	CK(atomic_load(&gClkSaid) == 1u);
	CK(app_shellTestInputTries(s) == 2u);	/* off: no retry */
	app_shellStop(s);
	return ok ? 0 : 1;
}

#undef CK

TEST(R5_shell_clockin)
{
	emu_testTempDir(gCard, sizeof gCard);
	emu_testTempDir(gHome, sizeof gHome);
	ASSERT_EQ(emu_hostCopyCard(EMU_TEST_CARDS "/init", gCard, 0), 0);
	const char *text = "clockout.audio = 1\nclockout.channel = 3\nclockin.audio = 1\nclockin.channel = 64\n";
	emu_testWrite(gHome, "settings.txt", text, strlen(text));
	PROBE(clockinChild, "R5_shell_clockin", 15);
}

TEST_MAIN()
