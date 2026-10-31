/*
 * app_coreaudio.c
 *
 * The CoreAudio output of LICHOMAT Emu (stage 1b spec 6.2, 6.3, plan Task 7): the
 * device, the AUHAL unit with its client format and converter, the render
 * callback, and the listeners that tell the firmware thread of a device change or
 * an overload. The callback and the listeners only push into rings.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_coreaudio.h"

#include <Block.h>
#include <limits.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_CA_GLOBAL kAudioObjectPropertyScopeGlobal
#define APP_CA_OUTPUT kAudioObjectPropertyScopeOutput

static AudioObjectPropertyAddress app_caAddr(AudioObjectPropertySelector sel, AudioObjectPropertyScope scope)
{
	return (AudioObjectPropertyAddress){ sel, scope, kAudioObjectPropertyElementMain };
}

static OSStatus app_caGet(AudioObjectID obj, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope, void *data,
	UInt32 size)
{
	const AudioObjectPropertyAddress a = app_caAddr(sel, scope);
	UInt32 n = size;
	return AudioObjectGetPropertyData(obj, &a, 0, NULL, &n, data);
}

/* a device property that the HAL may keep in the output scope or in the global one */
static OSStatus app_caGetEither(AudioObjectID obj, AudioObjectPropertySelector sel, void *data, UInt32 size)
{
	const OSStatus s = app_caGet(obj, sel, APP_CA_OUTPUT, data, size);
	return s == noErr ? s : app_caGet(obj, sel, APP_CA_GLOBAL, data, size);
}

/* "the call: the OSStatus", with the four characters of a code that has them */
static int app_caFail(char *err, size_t n, const char *call, OSStatus st)
{
	if (err && n)
	{
		const unsigned u = (unsigned)st;
		const char c[5] = { (char)(u >> 24), (char)(u >> 16), (char)(u >> 8), (char)u, 0 };
		int text = 1;
		for (int i = 0; i < 4; i++)
			text = text && c[i] >= 0x20 && c[i] < 0x7F;
		if (text)
			snprintf(err, n, "%s: %d ('%s')", call, (int)st, c);
		else
			snprintf(err, n, "%s: %d", call, (int)st);
	}
	return -1;
}

static void app_caCopyString(CFStringRef s, char *out, size_t n)
{
	out[0] = '\0';
	if (s)
	{
		if (!CFStringGetCString(s, out, (CFIndex)n, kCFStringEncodingUTF8))
			out[0] = '\0';
		CFRelease(s);
	}
}

/* the channels of a device in a scope: the sum over its streams' buffers */
static unsigned app_caChannels(AudioObjectID dev, AudioObjectPropertyScope scope)
{
	const AudioObjectPropertyAddress a = app_caAddr(kAudioDevicePropertyStreamConfiguration, scope);
	UInt32 size = 0;
	if (AudioObjectGetPropertyDataSize(dev, &a, 0, NULL, &size) != noErr || size < sizeof(UInt32))
		return 0;
	AudioBufferList *l = calloc(1u, size < sizeof *l ? sizeof *l : size);
	if (!l)
		return 0;
	unsigned channels = 0;
	if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, l) == noErr)
	{
		const UInt32 fit = (size - (UInt32)offsetof(AudioBufferList, mBuffers)) / (UInt32)sizeof(AudioBuffer);
		for (UInt32 i = 0; i < l->mNumberBuffers && i < fit; i++)
			channels += l->mBuffers[i].mNumberChannels;
	}
	free(l);
	return channels;
}

static AudioObjectID app_caDefaultDevice(AudioObjectPropertySelector def)	/* the system's default output or input */
{
	AudioObjectID d = kAudioObjectUnknown;
	if (app_caGet(kAudioObjectSystemObject, def, APP_CA_GLOBAL, &d, sizeof d) != noErr)
		return kAudioObjectUnknown;
	return d;
}

/* the UID's device, or the default (and then the app follows the default) */
static AudioObjectID app_caFindDevice(const char *uid, AudioObjectPropertySelector def, int *followDefault)
{
	AudioObjectID d = kAudioObjectUnknown;
	if (uid && uid[0])
	{
		CFStringRef s = CFStringCreateWithCString(kCFAllocatorDefault, uid, kCFStringEncodingUTF8);
		if (s)
		{
			const AudioObjectPropertyAddress a = app_caAddr(kAudioHardwarePropertyTranslateUIDToDevice, APP_CA_GLOBAL);
			UInt32 size = sizeof d;
			if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, sizeof s, &s, &size, &d) != noErr)
				d = kAudioObjectUnknown;
			CFRelease(s);
		}
	}
	*followDefault = d == kAudioObjectUnknown;
	return d == kAudioObjectUnknown ? app_caDefaultDevice(def) : d;
}

static int app_caListScope(app_outputDevice_t *out, int max, AudioObjectPropertyScope scope)
{
	const AudioObjectPropertyAddress a = app_caAddr(kAudioHardwarePropertyDevices, APP_CA_GLOBAL);
	UInt32 size = 0;
	if (max <= 0 || AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &size) != noErr || size == 0u)
		return 0;
	AudioObjectID *ids = malloc(size);
	if (!ids)
		return 0;
	int n = 0;
	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, ids) == noErr)
	{
		for (UInt32 i = 0; i < size / sizeof *ids && n < max; i++)
		{
			const unsigned channels = app_caChannels(ids[i], scope);
			if (channels == 0u)
				continue;
			app_outputDevice_t *d = &out[n++];
			memset(d, 0, sizeof *d);
			d->id = ids[i];
			d->channels = channels;
			CFStringRef s = NULL;
			if (app_caGet(ids[i], kAudioDevicePropertyDeviceUID, APP_CA_GLOBAL, &s, sizeof s) == noErr)
				app_caCopyString(s, d->uid, sizeof d->uid);
			s = NULL;
			if (app_caGet(ids[i], kAudioObjectPropertyName, APP_CA_GLOBAL, &s, sizeof s) == noErr)
				app_caCopyString(s, d->name, sizeof d->name);
		}
	}
	free(ids);
	return n;
}

int app_caList(app_outputDevice_t *out, int max)
{
	return app_caListScope(out, max, APP_CA_OUTPUT);
}

int app_caInputs(app_outputDevice_t *out, int max)
{
	return app_caListScope(out, max, kAudioObjectPropertyScopeInput);
}

/* spec 6.3: the callback's host time is mHostTime (the safety offset and the converter are in it), the entry is when
 * the callback began; both in mach ticks */
OSStatus app_caRender(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
	UInt32 frames, AudioBufferList *io)
{
	(void)flags;
	(void)bus;
	const uint64_t entry = mach_absolute_time();
	app_ca_t *ca = ref;
	if (io->mNumberBuffers != 1u || io->mBuffers[0].mDataByteSize < (size_t)frames * ca->channels * sizeof(float)
		|| io->mBuffers[0].mData == NULL)
	{
		/* not the interleaved buffer that the format promised: silence in every buffer, not what they held */
		for (UInt32 i = 0; i < io->mNumberBuffers; i++)
			if (io->mBuffers[i].mData)
				memset(io->mBuffers[i].mData, 0, io->mBuffers[i].mDataByteSize);
		return noErr;
	}
	const app_audioTime_t t = {
		(ts->mFlags & kAudioTimeStampHostTimeValid) ? ts->mHostTime : entry,
		entry,
		(ts->mFlags & kAudioTimeStampRateScalarValid) ? ts->mRateScalar : 1.0,
	};
	app_audioRender(ca->audio, &t, frames, (float *)io->mBuffers[0].mData, ca->channels);
	return noErr;
}

/* the device's properties whose change the firmware thread hears of (spec 6.2): a reopen, except for an overload */
static unsigned app_caWatch(app_ca_t *ca)
{
	const AudioObjectPropertyScope any = kAudioObjectPropertyScopeWildcard;
	const struct { AudioObjectPropertySelector sel; AudioObjectPropertyScope scope; } device[] = {
		{ kAudioDevicePropertyDeviceIsAlive, APP_CA_GLOBAL },
		{ kAudioDeviceProcessorOverload, any },
		{ kAudioDevicePropertyNominalSampleRate, APP_CA_GLOBAL },
		{ kAudioDevicePropertyBufferFrameSize, any },
		{ kAudioDevicePropertyStreamConfiguration, APP_CA_OUTPUT },
	};
	unsigned n = 0;
	for (unsigned i = 0; i < sizeof device / sizeof device[0]; i++)
		ca->listen[n++] = (app_caListen_t){ ca->device, app_caAddr(device[i].sel, device[i].scope) };
	if (ca->followDefault)
		ca->listen[n++] = (app_caListen_t){ kAudioObjectSystemObject,
			app_caAddr(kAudioHardwarePropertyDefaultOutputDevice, APP_CA_GLOBAL) };
	return n;
}

void app_caClose(app_ca_t *ca)
{
	if (ca->started)
		AudioOutputUnitStop(ca->unit);	/* the callback has returned when this does */
	ca->started = 0;
	for (unsigned i = 0; i < ca->nListen; i++)
		(void)AudioObjectRemovePropertyListenerBlock(ca->listen[i].obj, &ca->listen[i].addr, ca->queue, ca->listener);
	ca->nListen = 0;
	if (ca->queue)
	{
		dispatch_sync(ca->queue, ^{ });	/* a notification that was queued before the removal has run */
		dispatch_release(ca->queue);
		ca->queue = NULL;
	}
	if (ca->listener)
	{
		Block_release(ca->listener);
		ca->listener = NULL;
	}
	if (ca->initialized)
		AudioUnitUninitialize(ca->unit);
	ca->initialized = 0;
	if (ca->unit)
		AudioComponentInstanceDispose(ca->unit);
	ca->unit = NULL;
	ca->device = kAudioObjectUnknown;
}

static int app_caOpenUnit(app_ca_t *ca, const app_outputRequest_t *req, app_outputInfo_t *info, char *err, size_t n)
{
	OSStatus st;
	/* 1. the device */
	ca->device = app_caFindDevice(req->uid, kAudioHardwarePropertyDefaultOutputDevice, &ca->followDefault);
	if (ca->device == kAudioObjectUnknown)
	{
		snprintf(err, n, "no output device");
		return -1;
	}
	const unsigned deviceChannels = app_caChannels(ca->device, APP_CA_OUTPUT);
	if (deviceChannels == 0u)
	{
		snprintf(err, n, "no output device");
		return -1;
	}
	/* 2. the buffer: clamped to the device's range; 0 keeps the device's own */
	if (req->bufferFrames)
	{
		AudioValueRange range = { 0.0, 0.0 };
		UInt32 frames = req->bufferFrames;
		if (app_caGetEither(ca->device, kAudioDevicePropertyBufferFrameSizeRange, &range, sizeof range) == noErr
			&& range.mMaximum >= range.mMinimum)
		{
			frames = (double)frames < range.mMinimum ? (UInt32)range.mMinimum : frames;
			frames = (double)frames > range.mMaximum ? (UInt32)range.mMaximum : frames;
		}
		AudioObjectPropertyAddress a = app_caAddr(kAudioDevicePropertyBufferFrameSize, APP_CA_OUTPUT);
		st = AudioObjectSetPropertyData(ca->device, &a, 0, NULL, sizeof frames, &frames);
		if (st != noErr)
		{
			a.mScope = APP_CA_GLOBAL;
			st = AudioObjectSetPropertyData(ca->device, &a, 0, NULL, sizeof frames, &frames);
		}
		if (st != noErr)
			return app_caFail(err, n, "AudioObjectSetPropertyData(BufferFrameSize)", st);
	}
	/* 3. the unit */
	const AudioComponentDescription desc = { kAudioUnitType_Output, kAudioUnitSubType_HALOutput,
		kAudioUnitManufacturer_Apple, 0, 0 };
	AudioComponent comp = AudioComponentFindNext(NULL, &desc);
	if (!comp)
	{
		snprintf(err, n, "AudioComponentFindNext: no output unit");
		return -1;
	}
	st = AudioComponentInstanceNew(comp, &ca->unit);
	if (st != noErr)
	{
		ca->unit = NULL;
		return app_caFail(err, n, "AudioComponentInstanceNew", st);
	}
	st = AudioUnitSetProperty(ca->unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &ca->device,
		sizeof ca->device);
	if (st != noErr)
		return app_caFail(err, n, "AudioUnitSetProperty(CurrentDevice)", st);
	/* 4. the client format: float32, interleaved, 44,140.625 Hz; the mix's 2 or 4 channels, or the clock's channel (F3),
	 * as many as the device has */
	const unsigned cap = deviceChannels < APP_CLOCK_CHANNELS ? deviceChannels : APP_CLOCK_CHANNELS;
	ca->channels = req->channels < cap ? req->channels : cap;
	ca->channels = ca->channels < 2u ? 2u : ca->channels;
	AudioStreamBasicDescription f;
	memset(&f, 0, sizeof f);
	f.mSampleRate = 44140.625;
	f.mFormatID = kAudioFormatLinearPCM;
	f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
	f.mBitsPerChannel = 32u;
	f.mChannelsPerFrame = ca->channels;
	f.mFramesPerPacket = 1u;
	f.mBytesPerFrame = f.mBytesPerPacket = ca->channels * (UInt32)sizeof(float);
	st = AudioUnitSetProperty(ca->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
	if (st != noErr)
		return app_caFail(err, n, "AudioUnitSetProperty(StreamFormat)", st);
	if (ca->channels > 2u)
	{
		/* client channels 0 to channels - 1 to the device's channels of the same number, the rest silent */
		SInt32 map[64];
		const unsigned count = deviceChannels < 64u ? deviceChannels : 64u;
		for (unsigned i = 0; i < count; i++)
			map[i] = i < ca->channels ? (SInt32)i : -1;
		st = AudioUnitSetProperty(ca->unit, kAudioOutputUnitProperty_ChannelMap, kAudioUnitScope_Output, 0, map,
			(UInt32)(count * sizeof map[0]));
		if (st != noErr)	/* the scope is the device side of element 0; some units want the client side */
			st = AudioUnitSetProperty(ca->unit, kAudioOutputUnitProperty_ChannelMap, kAudioUnitScope_Input, 0, map,
				(UInt32)(count * sizeof map[0]));
		if (st != noErr)
			return app_caFail(err, n, "AudioUnitSetProperty(ChannelMap)", st);
	}
	/* 5. the converter: Normal complexity at maximum quality (spec 6.2) */
	UInt32 value = kAudioUnitSampleRateConverterComplexity_Normal;
	st = AudioUnitSetProperty(ca->unit, kAudioUnitProperty_SampleRateConverterComplexity, kAudioUnitScope_Global, 0, &value,
		sizeof value);
	if (st != noErr)
		return app_caFail(err, n, "AudioUnitSetProperty(SampleRateConverterComplexity)", st);
	value = kRenderQuality_Max;
	st = AudioUnitSetProperty(ca->unit, kAudioUnitProperty_RenderQuality, kAudioUnitScope_Global, 0, &value, sizeof value);
	if (st != noErr)
		return app_caFail(err, n, "AudioUnitSetProperty(RenderQuality)", st);
	/* 6. the callback, and go */
	const AURenderCallbackStruct cb = { app_caRender, ca };
	st = AudioUnitSetProperty(ca->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
	if (st != noErr)
		return app_caFail(err, n, "AudioUnitSetProperty(SetRenderCallback)", st);
	st = AudioUnitInitialize(ca->unit);
	if (st != noErr)
		return app_caFail(err, n, "AudioUnitInitialize", st);
	ca->initialized = 1;
	st = AudioOutputUnitStart(ca->unit);
	if (st != noErr)
		return app_caFail(err, n, "AudioOutputUnitStart", st);
	ca->started = 1;
	/* 7. the info */
	memset(info, 0, sizeof *info);
	AudioStreamBasicDescription back;
	UInt32 size = sizeof back;
	info->clientRate = AudioUnitGetProperty(ca->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &back, &size)
		== noErr ? back.mSampleRate : 0.0;
	Float64 rate = 0.0;
	(void)app_caGet(ca->device, kAudioDevicePropertyNominalSampleRate, APP_CA_GLOBAL, &rate, sizeof rate);
	info->deviceRate = rate;
	UInt32 u = 0;
	if (app_caGet(ca->device, kAudioDevicePropertyLatency, APP_CA_OUTPUT, &u, sizeof u) == noErr)
		info->deviceLatency = u;
	u = 0;
	if (app_caGet(ca->device, kAudioDevicePropertySafetyOffset, APP_CA_OUTPUT, &u, sizeof u) == noErr)
		info->safetyOffset = u;
	const AudioObjectPropertyAddress sa = app_caAddr(kAudioDevicePropertyStreams, APP_CA_OUTPUT);
	AudioStreamID stream = kAudioObjectUnknown;
	size = sizeof stream;	/* the first stream only */
	if (AudioObjectGetPropertyData(ca->device, &sa, 0, NULL, &size, &stream) == noErr && size >= sizeof stream
		&& stream != kAudioObjectUnknown)
	{
		u = 0;
		if (app_caGet(stream, kAudioStreamPropertyLatency, APP_CA_GLOBAL, &u, sizeof u) == noErr)
			info->streamLatency = u;
	}
	u = 0;
	if (app_caGetEither(ca->device, kAudioDevicePropertyBufferFrameSize, &u, sizeof u) == noErr)
		info->bufferFrames = u;
	info->channels = ca->channels;
	info->deviceChannels = deviceChannels;
	CFStringRef name = NULL;
	if (app_caGet(ca->device, kAudioObjectPropertyName, APP_CA_GLOBAL, &name, sizeof name) == noErr)
		app_caCopyString(name, info->name, sizeof info->name);
	snprintf(info->format, sizeof info->format, "client float32 %u ch %.3f Hz; device %.0f Hz; converter Normal, quality Max",
		ca->channels, info->clientRate, info->deviceRate);
	/* 8. the listeners; a change of the device's state after this point reaches the firmware thread */
	ca->queue = dispatch_queue_create("lichomat-emu.audio", DISPATCH_QUEUE_SERIAL);
	if (!ca->queue)
	{
		snprintf(err, n, "dispatch_queue_create failed");
		return -1;
	}
	app_ca_t *const self = ca;
	ca->listener = Block_copy(^(UInt32 count, const AudioObjectPropertyAddress *addr) {
		for (UInt32 i = 0; i < count; i++)
			(void)app_engineNote(self->engine, addr[i].mSelector == kAudioDeviceProcessorOverload ? APP_NOTE_OVERLOAD
				: APP_NOTE_DEVICE);
		if (self->audio->wake)
			self->audio->wake(self->audio->wakeCtx);
	});
	if (!ca->listener)
	{
		snprintf(err, n, "Block_copy failed");
		return -1;
	}
	const unsigned want = app_caWatch(ca);
	for (unsigned i = 0; i < want; i++)
	{
		st = AudioObjectAddPropertyListenerBlock(ca->listen[i].obj, &ca->listen[i].addr, ca->queue, ca->listener);
		if (st != noErr)
			return app_caFail(err, n, "AudioObjectAddPropertyListenerBlock", st);
		ca->nListen = i + 1u;
	}
	return 0;
}

int app_caOpen(app_ca_t *ca, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info, char *err, size_t n)
{
	app_engine_t *const engine = ca->engine;
	memset(ca, 0, sizeof *ca);
	ca->engine = engine;
	ca->audio = a;
	if (!engine)
	{
		snprintf(err, n, "no engine to notify");
		return -1;
	}
	err[0] = '\0';
	if (app_caOpenUnit(ca, req, info, err, n) != 0)
	{
		app_caClose(ca);
		return -1;
	}
	return 0;
}

/* --- the input of the clock jacks as audio (plan R5, F9) ------------------------------------------------------------ */

#define APP_CA_INPUT kAudioObjectPropertyScopeInput
#define APP_CI_TRY(what, call) do { const OSStatus st_ = (call); if (st_ != noErr) return app_caFail(err, n, what, st_); } while (0)

static int app_ciDescribe(const char *uid, AudioObjectID *dev, int *followDefault, app_outputInfo_t *info)	/* no IO */
{
	memset(info, 0, sizeof *info);
	*dev = app_caFindDevice(uid, kAudioHardwarePropertyDefaultInputDevice, followDefault);
	if (*dev == kAudioObjectUnknown || (info->channels = info->deviceChannels = app_caChannels(*dev, APP_CA_INPUT)) == 0u)
		return -1;
	Float64 rate = 0.0;
	UInt32 u[4] = { 0, 0, 0, 0 };
	AudioStreamID stream = kAudioObjectUnknown;
	CFStringRef name = NULL;
	(void)app_caGet(*dev, kAudioDevicePropertyNominalSampleRate, APP_CA_GLOBAL, &rate, sizeof rate);
	(void)app_caGet(*dev, kAudioDevicePropertyLatency, APP_CA_INPUT, &u[0], sizeof u[0]);
	(void)app_caGet(*dev, kAudioDevicePropertySafetyOffset, APP_CA_INPUT, &u[1], sizeof u[1]);
	if (app_caGet(*dev, kAudioDevicePropertyBufferFrameSize, APP_CA_INPUT, &u[2], sizeof u[2]) != noErr)
		(void)app_caGet(*dev, kAudioDevicePropertyBufferFrameSize, APP_CA_GLOBAL, &u[2], sizeof u[2]);
	/* the first input stream's latency (spec 6.8): room for one ID gives the first */
	if (app_caGet(*dev, kAudioDevicePropertyStreams, APP_CA_INPUT, &stream, sizeof stream) == noErr && stream != kAudioObjectUnknown)
		(void)app_caGet(stream, kAudioStreamPropertyLatency, APP_CA_GLOBAL, &u[3], sizeof u[3]);
	if (app_caGet(*dev, kAudioObjectPropertyName, APP_CA_GLOBAL, &name, sizeof name) == noErr)
		app_caCopyString(name, info->name, sizeof info->name);
	info->deviceRate = info->clientRate = rate;
	info->deviceLatency = u[0];
	info->safetyOffset = u[1];
	info->bufferFrames = u[2];
	info->streamLatency = u[3];
	return rate > 0.0 ? 0 : -1;
}

int app_ciProbe(const char *uid, app_outputInfo_t *info)
{
	AudioObjectID dev;
	int follow;
	return app_ciDescribe(uid, &dev, &follow, info);
}

UInt32 app_ciSliceFrames(const AudioValueRange *range, UInt32 current)
{
	UInt32 n = 4096u;	/* the range is not known: what the first version assumed */
	if (range && range->mMaximum >= 1.0 && range->mMaximum >= range->mMinimum)
		n = range->mMaximum >= (double)APP_CI_SLICE_MAX ? APP_CI_SLICE_MAX : (UInt32)range->mMaximum;
	if (current > n)
		n = current > APP_CI_SLICE_MAX ? APP_CI_SLICE_MAX : current;
	return n;
}

OSStatus app_ciInput(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus, UInt32 frames,
	AudioBufferList *io)
{
	(void)io;	/* NULL for an input callback: the frames are pulled */
	const uint64_t entry = mach_absolute_time();
	app_ci_t *ci = ref;
	atomic_fetch_add_explicit(&ci->entries, 1u, memory_order_relaxed);
	if (frames == 0u)
		return noErr;	/* nothing to serve */
	if (frames > ci->maxFrames || !(ts->mFlags & kAudioTimeStampHostTimeValid))
	{
		/* no room, or no time to put the edges at: counted, or a dead input looks like a running one in the diagnostics */
		atomic_fetch_add_explicit(&ci->in->skipped, 1u, memory_order_relaxed);
		return noErr;
	}
	ci->abl.mNumberBuffers = 1u;
	ci->abl.mBuffers[0].mNumberChannels = ci->channels;
	ci->abl.mBuffers[0].mDataByteSize = frames * ci->channels * (UInt32)sizeof(float);
	ci->abl.mBuffers[0].mData = ci->buf;
	const OSStatus st = AudioUnitRender(ci->unit, flags, ts, bus, frames, &ci->abl);
	if (st == noErr)	/* spec 6.8: mHostTime, the frame's offset, the latency */
		app_jackInRender(ci->in, ts->mHostTime, entry, (ts->mFlags & kAudioTimeStampRateScalarValid) ? ts->mRateScalar : 1.0,
			ci->buf, frames);
	else
		atomic_fetch_add_explicit(&ci->in->skipped, 1u, memory_order_relaxed);
	return st;
}

unsigned app_ciWatch(app_ci_t *ci)
{
	const struct { AudioObjectPropertySelector sel; AudioObjectPropertyScope scope; } device[] = {
		{ kAudioDevicePropertyDeviceIsAlive, APP_CA_GLOBAL },
		{ kAudioDevicePropertyNominalSampleRate, APP_CA_GLOBAL },
		{ kAudioDevicePropertyStreamConfiguration, APP_CA_INPUT },
		/* a device that the output shares gets a new buffer when the Audio window says so: the lead that was measured at the
		 * old one is wrong, and the slice may be too (the HAL keeps the size in either scope) */
		{ kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeWildcard },
	};
	unsigned n = 0;
	for (unsigned i = 0; i < sizeof device / sizeof device[0]; i++)
		ci->listen[n++] = (app_caListen_t){ ci->device, app_caAddr(device[i].sel, device[i].scope) };
	if (ci->followDefault)
		ci->listen[n++] = (app_caListen_t){ kAudioObjectSystemObject,
			app_caAddr(kAudioHardwarePropertyDefaultInputDevice, APP_CA_GLOBAL) };
	return n;
}

void app_ciClose(app_ci_t *ci)
{
	const int ran = ci->started;
	if (ran)
		AudioOutputUnitStop(ci->unit);	/* the callback has returned when this does */
	for (unsigned i = 0; i < ci->nListen; i++)
		(void)AudioObjectRemovePropertyListenerBlock(ci->listen[i].obj, &ci->listen[i].addr, ci->queue, ci->listener);
	if (ci->queue)
	{
		dispatch_sync(ci->queue, ^{ });	/* a notification that was queued before the removal has run */
		dispatch_release(ci->queue);
	}
	if (ci->listener)
		Block_release(ci->listener);
	if (ci->initialized)
		AudioUnitUninitialize(ci->unit);
	if (ci->unit)
		AudioComponentInstanceDispose(ci->unit);
	free(ci->buf);
	app_jackIn_t *const in = ci->in;
	memset(ci, 0, sizeof *ci);
	ci->in = in;
	ci->device = kAudioObjectUnknown;
	/* the callback has returned for good, so this thread may push the release of a high reset (F11). Whoever closes does this:
	 * the shell, and the open that failed after the unit had started */
	if (ran)
		app_jackInStop(in, mach_absolute_time());
}

static int app_ciOpenUnit(app_ci_t *ci, const app_clockInOptions_t *o, double ticksPerSec, char *err, size_t n)
{
	if (app_ciDescribe(o->uid, &ci->device, &ci->followDefault, &ci->info) != 0)	/* the UID's, else the default input */
	{
		snprintf(err, n, "no input device");
		return -1;
	}
	ci->channels = ci->info.channels < APP_CLOCK_CHANNELS ? ci->info.channels : APP_CLOCK_CHANNELS;
	if (o->clockChannel < 1 || (unsigned)o->clockChannel > ci->channels || o->resetChannel < 0
		|| (unsigned)o->resetChannel > ci->channels)
	{
		snprintf(err, n, "%s has %u input channels", ci->info.name, ci->channels);
		return -1;
	}
	const AudioComponentDescription desc = { kAudioUnitType_Output, kAudioUnitSubType_HALOutput,
		kAudioUnitManufacturer_Apple, 0, 0 };
	AudioComponent comp = AudioComponentFindNext(NULL, &desc);
	if (!comp || AudioComponentInstanceNew(comp, &ci->unit) != noErr)
	{
		ci->unit = NULL;
		snprintf(err, n, "no HAL unit for the input");
		return -1;
	}
	/* AUHAL: input (element 1) on, output (element 0) off, then the device; in this order */
	const UInt32 on = 1u, off = 0u;
	APP_CI_TRY("AudioUnitSetProperty(EnableIO, input)", AudioUnitSetProperty(ci->unit,
		kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &on, sizeof on));
	APP_CI_TRY("AudioUnitSetProperty(EnableIO, output)", AudioUnitSetProperty(ci->unit,
		kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &off, sizeof off));
	APP_CI_TRY("AudioUnitSetProperty(CurrentDevice)", AudioUnitSetProperty(ci->unit,
		kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &ci->device, sizeof ci->device));
	/* the client side (element 1's output scope): float32 interleaved at the device's rate, no converter */
	const UInt32 bpf = ci->channels * (UInt32)sizeof(float);
	const AudioStreamBasicDescription f = { ci->info.deviceRate, kAudioFormatLinearPCM,
		kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked, bpf, 1u, bpf, ci->channels, 32u, 0u };
	APP_CI_TRY("AudioUnitSetProperty(StreamFormat, input)", AudioUnitSetProperty(ci->unit,
		kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &f, sizeof f));
	const AURenderCallbackStruct cb = { app_ciInput, ci };
	APP_CI_TRY("AudioUnitSetProperty(SetInputCallback)", AudioUnitSetProperty(ci->unit,
		kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cb, sizeof cb));
	/* the most frames one callback can bring: the device's largest buffer, set as the unit's slice before it initialises and
	 * made room for in buf. AUHAL's own slice follows the device's buffer, which a device that the output shares has raised
	 * (512, 1024, 2048) while this runs; a snapshot of it at the open left every later callback too big, and the input dead */
	AudioValueRange range = { 0.0, 0.0 };
	if (app_caGet(ci->device, kAudioDevicePropertyBufferFrameSizeRange, APP_CA_INPUT, &range, sizeof range) != noErr)
		(void)app_caGet(ci->device, kAudioDevicePropertyBufferFrameSizeRange, APP_CA_GLOBAL, &range, sizeof range);
	ci->maxFrames = app_ciSliceFrames(&range, ci->info.bufferFrames);
	UInt32 slice = ci->maxFrames;
	(void)AudioUnitSetProperty(ci->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &slice,
		sizeof slice);	/* a unit that refuses keeps its own, which the read below finds */
	APP_CI_TRY("AudioUnitInitialize(input)", AudioUnitInitialize(ci->unit));
	ci->initialized = 1;
	UInt32 size = sizeof slice;
	if (AudioUnitGetProperty(ci->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &slice, &size)
			== noErr && slice > ci->maxFrames)
		ci->maxFrames = slice > APP_CI_SLICE_MAX ? APP_CI_SLICE_MAX : slice;	/* it brings no more than this, buf holds it */
	if ((ci->buf = calloc((size_t)ci->maxFrames * ci->channels, sizeof(float))) == NULL)
	{
		snprintf(err, n, "out of memory for the input buffer");
		return -1;
	}
	const app_jackInConfig_t jc = { ticksPerSec, ci->info.deviceRate, ci->info.deviceLatency + ci->info.streamLatency,
		ci->channels, (unsigned)o->clockChannel - 1u, o->resetChannel > 0 ? (unsigned)o->resetChannel - 1u : UINT_MAX,
		(float)o->clockThreshold, (float)o->resetThreshold };
	app_jackInStart(ci->in, &jc);	/* before the first callback (F5-F7) */
	snprintf(ci->info.format, sizeof ci->info.format, "input float32 %u ch %.0f Hz; latency %u + %u frames", ci->channels,
		ci->info.deviceRate, ci->info.deviceLatency, ci->info.streamLatency);
	/* the listeners: the device gone, its rate, inputs or buffer changed, the default input moved (F9) */
	if ((ci->queue = dispatch_queue_create("lichomat-emu.input", DISPATCH_QUEUE_SERIAL)) == NULL)
	{
		snprintf(err, n, "dispatch_queue_create failed");
		return -1;
	}
	app_ci_t *const self = ci;
	ci->listener = Block_copy(^(UInt32 count, const AudioObjectPropertyAddress *addr) {
		(void)count;
		(void)addr;
		atomic_store(&self->changed, 1);
	});
	if (!ci->listener)
	{
		snprintf(err, n, "Block_copy failed");
		return -1;
	}
	const unsigned want = app_ciWatch(ci);
	for (unsigned i = 0; i < want; i++)
	{
		APP_CI_TRY("AudioObjectAddPropertyListenerBlock(input)",
			AudioObjectAddPropertyListenerBlock(ci->listen[i].obj, &ci->listen[i].addr, ci->queue, ci->listener));
		ci->nListen = i + 1u;
	}
	/* last: from the start on a callback can come at any moment, and a failure up to here has only what app_ciClose() undoes.
	 * started is set first, so that a start that fails halfway is stopped, and a reset that it left high is let go */
	ci->started = 1;
	APP_CI_TRY("AudioOutputUnitStart(input)", AudioOutputUnitStart(ci->unit));
	return 0;
}

int app_ciOpen(app_ci_t *ci, app_jackIn_t *in, const app_clockInOptions_t *o, double ticksPerSec, char *err, size_t n)
{
	memset(ci, 0, sizeof *ci);
	ci->in = in;
	err[0] = '\0';
	if (o->resetChannel > 0 && o->resetChannel == o->clockChannel)	/* one channel cannot be both: said before a device is touched */
	{
		snprintf(err, n, "clock and reset are both on input channel %d", o->clockChannel);
		return -1;
	}
	if (app_ciOpenUnit(ci, o, ticksPerSec, err, n) == 0)
		return 0;
	app_ciClose(ci);	/* of a unit that had started it lets a high reset go (F11) */
	return -1;
}
