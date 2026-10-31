/*
 * test_mixseg.c: mixer_addSegment() (mixerOut.h), the mixer's add of a voice's block in
 * segments (plan v0.5 Task 7, spec 9.1 mixer_segments). On every routing (out 1 and 2
 * stereo and mono, the FX bus) a block split at k adds [0, k) with the pan latch from
 * before the split and [k, 64) with the new one, sample for sample as fw 1.7: a stereo
 * pair or the FX bus takes (int16)(x * panL), (int16)(x * panR) with QADD16, a mono output
 * x with QADD16; k 0 is one whole block. Nothing outside the routing's output changes.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include <string.h>
#include "Soundengine/DSPAudio/mixerOut.h"

#define N 64

typedef struct
{
	__attribute__((aligned(4))) int16_t out[2 * N];		/* out 2 (dma_buffer): outL, outR */
	__attribute__((aligned(4))) int16_t out2[2 * N];	/* out 1 (dma_buffer2): outL2, outR2 */
	__attribute__((aligned(4))) int16_t fx[2 * N];		/* the FX bus */
} outs_t;

static uint32_t rnd = 7;
static int16_t r16(void)
{
	rnd = rnd * 1664525u + 1013904223u;
	return (int16_t)(rnd >> 16);
}

static int16_t sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }

/** fw 1.7's add of sample x to frame i of dest */
static void refAdd(outs_t *o, uint8_t dest, float l, float r, int16_t x, int i)
{
	int16_t *pair = dest == MIXER_ROUTING_FX_BUS ? o->fx : dest == MIXER_ROUTING_DAC1_STEREO ? o->out2 : o->out;
	switch (dest)
	{
	case MIXER_ROUTING_FX_BUS: case MIXER_ROUTING_DAC1_STEREO: case MIXER_ROUTING_DAC2_STEREO:
		pair[2 * i] = sat16(pair[2 * i] + (int16_t)(x * l));
		pair[2 * i + 1] = sat16(pair[2 * i + 1] + (int16_t)(x * r));
		break;
	case MIXER_ROUTING_DAC1_L: o->out2[2 * i] = sat16(o->out2[2 * i] + x); break;
	case MIXER_ROUTING_DAC1_R: o->out2[2 * i + 1] = sat16(o->out2[2 * i + 1] + x); break;
	case MIXER_ROUTING_DAC2_L: o->out[2 * i] = sat16(o->out[2 * i] + x); break;
	default: o->out[2 * i + 1] = sat16(o->out[2 * i + 1] + x); break;
	}
}

TEST(mixer_segments)
{
	static const float pans[][2] = { { 1.f, 0.f }, { 0.f, 1.f }, { 0.70710677f, 0.70710677f }, { 0.3f, 0.9539392f } };
	int16_t data[N];
	for (uint8_t dest = MIXER_ROUTING_DAC1_STEREO; dest <= MIXER_ROUTING_FX_BUS; dest++)
		for (unsigned p = 0; p < 4; p++)
			for (uint8_t k = 0; k < N; k += 2)
			{
				const unsigned q = (p + 1) % 4;
				outs_t a, b;
				for (int i = 0; i < 2 * N; i++)
				{
					a.out[i] = (int16_t)(r16() / 2);
					a.out2[i] = (int16_t)(r16() / 2);
					a.fx[i] = (int16_t)(r16() / 2);
				}
				for (int i = 0; i < N; i++)		/* full scale too: the saturation */
					data[i] = (i % 9 == 0) ? (int16_t)(i % 2 ? 32767 : -32768) : r16();
				b = a;
				if (k)
					mixer_addSegment(dest, pans[p][0], pans[p][1], data, 0, k, &a.out[0], &a.out[1], &a.out2[0], &a.out2[1], a.fx);
				mixer_addSegment(dest, pans[q][0], pans[q][1], data, k, (uint8_t)(N - k), &a.out[0], &a.out[1], &a.out2[0], &a.out2[1], a.fx);
				for (int i = 0; i < N; i++)
					refAdd(&b, dest, pans[i < k ? p : q][0], pans[i < k ? p : q][1], data[i], i);
				ASSERT_EQ(memcmp(&a, &b, sizeof a), 0);
			}
}

TEST_MAIN()
