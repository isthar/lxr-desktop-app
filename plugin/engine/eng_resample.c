/*
 * eng_resample.c
 *
 * The engine's sample-rate conversion (plan E9.2 Task 6); see eng_resample.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "eng_resample.h"

#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <string.h>

#define ENG_NO_DATA 0x6E6F6474	/* 'nodt': the block is used up; the converter keeps its state */

static OSStatus eng_input(AudioConverterRef c, UInt32 *packets, AudioBufferList *io, AudioStreamPacketDescription **d,
	void *ctx)
{
	(void)c;
	(void)d;
	eng_resample_t *r = ctx;
	const unsigned left = r->have - r->used;
	if (left == 0)
	{
		*packets = 0;
		return ENG_NO_DATA;
	}
	if (*packets > left)
		*packets = left;
	io->mBuffers[0].mData = &r->in[2u * r->used];
	io->mBuffers[0].mDataByteSize = *packets * 2u * (UInt32)sizeof(float);
	io->mBuffers[0].mNumberChannels = 2;
	r->used += *packets;
	return noErr;
}

int eng_resampleOpen(eng_resample_t *r, double rate, char *err, size_t n)
{
	memset(r, 0, sizeof *r);
	AudioStreamBasicDescription in;
	memset(&in, 0, sizeof in);
	in.mSampleRate = 353125.0 / 8.0;
	in.mFormatID = kAudioFormatLinearPCM;
	in.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
	in.mBitsPerChannel = 32;
	in.mChannelsPerFrame = 2;
	in.mFramesPerPacket = 1;
	in.mBytesPerFrame = in.mBytesPerPacket = 2u * (UInt32)sizeof(float);
	AudioStreamBasicDescription out = in;
	out.mSampleRate = rate;
	AudioConverterRef c;
	OSStatus st = AudioConverterNew(&in, &out, &c);
	if (st != noErr)
	{
		snprintf(err, n, "AudioConverterNew to %.3f Hz: %d", rate, (int)st);
		return -1;
	}
	const struct { AudioConverterPropertyID id; UInt32 v; const char *name; } props[] = {
		{ kAudioConverterSampleRateConverterComplexity, kAudioConverterSampleRateConverterComplexity_Normal, "complexity" },
		{ kAudioConverterSampleRateConverterQuality, kAudioConverterQuality_Max, "quality" },
		{ kAudioConverterPrimeMethod, kConverterPrimeMethod_None, "prime method" },
	};
	for (size_t i = 0; i < sizeof props / sizeof props[0]; i++)
	{
		st = AudioConverterSetProperty(c, props[i].id, sizeof props[i].v, &props[i].v);
		if (st != noErr)
		{
			snprintf(err, n, "AudioConverterSetProperty(%s): %d", props[i].name, (int)st);
			AudioConverterDispose(c);
			return -1;
		}
	}
	r->conv = c;
	return 0;
}

int eng_resamplePush(eng_resample_t *r, const float *lr, void (*fn)(void *ctx, const float *lr, uint32_t frames),
	void *ctx)
{
	memcpy(r->in, lr, sizeof r->in);
	r->have = ENG_RESAMPLE_IN;
	r->used = 0;
	for (;;)
	{
		UInt32 packets = ENG_RESAMPLE_OUT;
		AudioBufferList abl;
		abl.mNumberBuffers = 1;
		abl.mBuffers[0].mNumberChannels = 2;
		abl.mBuffers[0].mDataByteSize = (UInt32)sizeof r->out;
		abl.mBuffers[0].mData = r->out;
		const OSStatus st = AudioConverterFillComplexBuffer((AudioConverterRef)r->conv, eng_input, r, &packets, &abl, NULL);
		if (packets)
			fn(ctx, r->out, packets);
		if (st == ENG_NO_DATA)
			return 0;
		if (st != noErr)
			return (int)st;
		if (packets == 0)
			return 0;
	}
}

void eng_resampleClose(eng_resample_t *r)
{
	if (r->conv)
		AudioConverterDispose((AudioConverterRef)r->conv);
	r->conv = NULL;
}
