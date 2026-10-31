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

/* the output channels of a device: the sum over its output streams' buffers */
static unsigned app_caDeviceChannels(AudioObjectID dev)
{
	const AudioObjectPropertyAddress a = app_caAddr(kAudioDevicePropertyStreamConfiguration, APP_CA_OUTPUT);
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

static AudioObjectID app_caDefaultDevice(void)
{
	AudioObjectID d = kAudioObjectUnknown;
	if (app_caGet(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, APP_CA_GLOBAL, &d, sizeof d) != noErr)
		return kAudioObjectUnknown;
	return d;
}

/* the UID's device, or the default (and then the app follows the default) */
static AudioObjectID app_caFindDevice(const char *uid, int *followDefault)
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
	return d == kAudioObjectUnknown ? app_caDefaultDevice() : d;
}

int app_caList(app_outputDevice_t *out, int max)
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
			const unsigned channels = app_caDeviceChannels(ids[i]);
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
	ca->device = app_caFindDevice(req->uid, &ca->followDefault);
	if (ca->device == kAudioObjectUnknown)
	{
		snprintf(err, n, "no output device");
		return -1;
	}
	const unsigned deviceChannels = app_caDeviceChannels(ca->device);
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
	/* 4. the client format: float32, interleaved, 44,140.625 Hz; four channels only on a device that has them */
	ca->channels = req->channels >= 4u && deviceChannels >= 4u ? 4u : 2u;
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
	if (ca->channels == 4u)
	{
		/* client channels 0-3 to device channels 0-3, the rest silent */
		SInt32 map[64];
		const unsigned count = deviceChannels < 64u ? deviceChannels : 64u;
		for (unsigned i = 0; i < count; i++)
			map[i] = i < 4u ? (SInt32)i : -1;
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
