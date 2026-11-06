/*
 * plug_resample_wav.c
 *
 * A 16-bit stereo WAV at 44,140.625 Hz (lichomat-render's --stereo sum) through the engine's
 * converter, 64 frames a push, into a 16-bit WAV at RATE (plan E9.2 Task 6). P7 compares the
 * engine's resampled output with this.
 *
 *   plug_resample_wav IN.wav RATE OUT.wav FRAMES
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eng_resample.h"

static FILE *o;
static uint64_t left;

static void put16(unsigned v)
{
	fputc((int)(v & 255u), o);
	fputc((int)((v >> 8) & 255u), o);
}

static void put32(uint32_t v)
{
	put16(v & 0xFFFFu);
	put16(v >> 16);
}

static void emit(void *ctx, const float *lr, uint32_t frames)
{
	(void)ctx;
	for (uint32_t i = 0; i < 2u * frames && left; i += 2)
	{
		for (unsigned c = 0; c < 2; c++)
		{
			double v = floor((double)lr[i + c] * 32768.0 + 0.5);
			v = v > 32767.0 ? 32767.0 : v < -32768.0 ? -32768.0 : v;
			put16((uint16_t)(int16_t)v);
		}
		left--;
	}
}

int main(int argc, char **argv)
{
	if (argc != 5)
	{
		fprintf(stderr, "usage: plug_resample_wav IN.wav RATE OUT.wav FRAMES\n");
		return 1;
	}
	FILE *in = fopen(argv[1], "rb");
	unsigned char h[44];
	if (!in || fread(h, 1, 44, in) != 44 || memcmp(h, "RIFF", 4) || h[22] != 2 || h[34] != 16)
	{
		fprintf(stderr, "plug_resample_wav: %s is not a 16-bit stereo WAV\n", argv[1]);
		return 1;
	}
	const double rate = strtod(argv[2], NULL);
	const uint64_t frames = strtoull(argv[4], NULL, 10);
	eng_resample_t r;
	char err[160];
	if (eng_resampleOpen(&r, rate, 2, err, sizeof err) != 0)
	{
		fprintf(stderr, "plug_resample_wav: %s\n", err);
		return 1;
	}
	o = fopen(argv[3], "wb");
	const uint32_t data = (uint32_t)(frames * 4u);
	fwrite("RIFF", 1, 4, o);
	put32(36u + data);
	fwrite("WAVEfmt ", 1, 8, o);
	put32(16);
	put16(1);
	put16(2);
	put32((uint32_t)rate);
	put32((uint32_t)rate * 4u);
	put16(4);
	put16(16);
	fwrite("data", 1, 4, o);
	put32(data);
	left = frames;
	int16_t s[128];
	size_t got;
	while (left && (got = fread(s, 4, 64, in)) > 0)
	{
		float lr[128] = { 0 };
		for (size_t i = 0; i < 2u * got; i++)
			lr[i] = (float)s[i] / 32768.0f;
		if (eng_resamplePush(&r, lr, emit, NULL) != 0)
			return 1;
	}
	while (left)	/* past the input's end: silence */
	{
		const float lr[128] = { 0 };
		if (eng_resamplePush(&r, lr, emit, NULL) != 0)
			return 1;
	}
	eng_resampleClose(&r);
	fclose(o);
	return 0;
}
