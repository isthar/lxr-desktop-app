/*
 * mixerOut.h
 *
 * LICHOMAT v0.5 (plan v0.5 Task 7): fw 1.7's mixer_addDataToOutput() for samples [from, from + n)
 * of a voice's block, so that a split voice is mixed in two parts, each with its pan latch
 * (spec 4.2, S14). dest is the routing after mixer_checkOutJackAvailable(). From 0 for 64 it
 * adds what fw 1.7 added.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef MIXEROUT_H_
#define MIXEROUT_H_
#include <stdint.h>
#include "stm32f7xx_hal.h"
#include "Soundengine/DSPAudio/mixer.h"

/** outL/outR out 2 (dma_buffer), outL2/outR2 out 1 (dma_buffer2), fxBuffer the FX bus; interleaved stereo */
static inline void mixer_addSegment(const uint8_t dest, const float panL, const float panR, const int16_t *data,
		const uint8_t from, const uint8_t n, int16_t *outL, int16_t *outR, int16_t *outL2, int16_t *outR2,
		int16_t *fxBuffer)
{
	uint8_t i;
	data += from;
	outL += 2 * from;
	outR += 2 * from;
	outL2 += 2 * from;
	outR2 += 2 * from;
	fxBuffer += 2 * from;
	switch(dest)
	{
	case MIXER_ROUTING_FX_BUS:
		for(i=0;i<n;i++)
		{
			const int32_t y = *((int32_t*)fxBuffer);
			const int32_t x = __PKHBT( (int16_t)(data[i] * panL), (int16_t)(data[i] * panR), 16);
			*((int32_t*)fxBuffer) = __QADD16(x, y);
			fxBuffer += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_STEREO:
		for(i=0;i<n;i++)
		{
			const int32_t y = *((int32_t*)outL2);
			const int32_t x = __PKHBT( (int16_t)(data[i] * panL), (int16_t)(data[i] * panR), 16);
			*((int32_t*)outL2) = __QADD16(x, y);
			outL2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_STEREO:
		for(i=0;i<n;i++)
		{
			const int32_t y = *((int32_t*)outL);
			const int32_t x = __PKHBT( (int16_t)(data[i] * panL), (int16_t)(data[i] * panR), 16);
			*((int32_t*)outL) = __QADD16(x, y);
			outL += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_L:
		for(i=0;i<n;i++) { *outL2 = __QADD16(*outL2,data[i]); outL2 += 2; }
		break;
	case MIXER_ROUTING_DAC1_R:
		for(i=0;i<n;i++) { *outR2 = __QADD16(*outR2,data[i]); outR2 += 2; }
		break;
	case MIXER_ROUTING_DAC2_L:
		for(i=0;i<n;i++) { *outL = __QADD16(*outL,data[i]); outL += 2; }
		break;
	case MIXER_ROUTING_DAC2_R:
		for(i=0;i<n;i++) { *outR = __QADD16(*outR,data[i]); outR += 2; }
		break;
	}
}

#endif /* MIXEROUT_H_ */
