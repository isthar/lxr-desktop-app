/*
 * mixer.c
 *
 *  Created on: 11.04.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/config.h"
#include "Soundengine/AudioCodecManager.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/BufferTools.h"
#include "Soundengine/DSPAudio/squareRootLut.h"
#include "main.h"
#include "Soundengine/TriggerOut.h"
#include "menu/menu.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/FX/djfilter.h"	//LICHOMAT v0.4: the DJ filter (plan D39)
#include "Soundengine/DSPAudio/FX/atmo.h"	//LICHOMAT v0.4: rumble's follower (plan D40)
#include "Soundengine/vsched.h"	//LICHOMAT v0.5: sample-accurate triggering (plan v0.5)
#include "Soundengine/DSPAudio/voices.h"
#include "Soundengine/DSPAudio/mixerOut.h"

	uint8_t r2_Available;
	uint8_t l1_Available;
	uint8_t l2_Available;
	uint8_t r1_Available;


//-----------------------------------------------------------------------
INDTCMZ uint8_t mixer_audioRouting[6];
//-----------------------------------------------------------------------
#if USE_DECIMATOR
INDTCMZ float mixer_decimation_rate[7];		/**<sets the sample rate decimation. 0..1 = full rate*/
INDTCMZ float mixer_decimation_cnt[6];		/**<s'n'h counter for decimator*/
INDTCMZ int16_t mixer_voice_samples[6];		/**< stores the last outputted sample of the 6 voices*/
#endif
//-----------------------------------------------------------------------
void mixer_init()
{
#if USE_DECIMATOR
	int i;
	for(i=0;i<6;i++)
	{
		mixer_decimation_rate[i] 	= 1;
		mixer_decimation_cnt[i] 	= 0;
		mixer_voice_samples[i] 		= 0;
		mixer_audioRouting[i]		= 0;
	}
	mixer_decimation_rate[6] 		= 1;
#endif

	FxBus_init();
}
//-----------------------------------------------------------------------
IN_ITCM_RAM void mixer_decimateBlock(const uint8_t voiceNr, int16_t* buffer)
{
	uint8_t i;
	float phaseInc = mixer_decimation_rate[voiceNr]*mixer_decimation_rate[6];
	for(i=0;i<OUTPUT_DMA_SIZE;i++)
	{
		mixer_decimation_cnt[voiceNr] += phaseInc;
		if(mixer_decimation_cnt[voiceNr] >= 1.f)
		{
			mixer_decimation_cnt[voiceNr] -= 1.f;
			mixer_voice_samples[voiceNr] = buffer[i];

		}
		buffer[i] = mixer_voice_samples[voiceNr];
	}
	atmo_duckSense(voiceNr, buffer);	//LICHOMAT v0.4: rumble's follower (plan D40)
}
//#pragma GCC push_options
//#pragma GCC optimize ("O0")
//-----------------------------------------------------------------------
/*IN_ITCM_RAM*/ uint8_t mixer_checkOutJackAvailable(uint8_t dest)
{
	//read input pins
	//TODO move pin reading in low priority idle loop
	/*
	 #define switch_l1_Pin GPIO_PIN_6
#define switch_l1_GPIO_Port GPIOD
#define switch_r1_Pin GPIO_PIN_7
#define switch_r1_GPIO_Port GPIOD
#define switch_l2_Pin GPIO_PIN_3
#define switch_l2_GPIO_Port GPIOB
#define switch_r2_Pin GPIO_PIN_4
#define switch_r2_GPIO_Port GPIOB
	 */


	//return MIXER_ROUTING_DAC2_STEREO;

	switch(dest)
	{
		case MIXER_ROUTING_FX_BUS:
			return MIXER_ROUTING_FX_BUS;
			break;

		case MIXER_ROUTING_DAC1_STEREO:
			if(r1_Available && l1_Available) {
				return MIXER_ROUTING_DAC1_STEREO;
			} else
			if (r2_Available && l2_Available) {
				return MIXER_ROUTING_DAC2_STEREO;
			}
			else if(r1_Available) {
				return MIXER_ROUTING_DAC1_R;
			}else if(l1_Available) {
				return MIXER_ROUTING_DAC1_L;
			} else
			if (r2_Available) {
				return MIXER_ROUTING_DAC2_R;
			}else
			if (l2_Available) {
				return MIXER_ROUTING_DAC2_L;
			}
			break;

		case MIXER_ROUTING_DAC2_STEREO:

			if (r2_Available && l2_Available) {
				return MIXER_ROUTING_DAC2_STEREO;
			} else
			if(r1_Available && l1_Available) {
				return MIXER_ROUTING_DAC1_STEREO;
			}
			else
			if (r2_Available) {
				return MIXER_ROUTING_DAC2_R;
			}else
			if (l2_Available) {
				return MIXER_ROUTING_DAC2_L;
			}else if(r1_Available) {
				return MIXER_ROUTING_DAC1_R;
			}else if(l1_Available) {
				return MIXER_ROUTING_DAC1_L;
			}
			break;

		case MIXER_ROUTING_DAC1_L:
			if(l1_Available) {
				return dest;
			} else if (r1_Available) {
				return MIXER_ROUTING_DAC1_R;
			} else if (l2_Available) {
				return MIXER_ROUTING_DAC2_L;
			} else if (r2_Available) {
				return MIXER_ROUTING_DAC2_R;
			}
			break;

		case MIXER_ROUTING_DAC1_R:
			if(r1_Available) {
				return dest;
			} else if (l1_Available) {
				return MIXER_ROUTING_DAC1_L;
			} else if (l2_Available) {
				return MIXER_ROUTING_DAC2_L;
			} else if (r2_Available) {
				return MIXER_ROUTING_DAC2_R;
			}
			break;

		case MIXER_ROUTING_DAC2_L:
			if(l2_Available) {
				return dest;
			} else if (r2_Available) {
				return MIXER_ROUTING_DAC2_R;
			} else if (l1_Available) {
				return MIXER_ROUTING_DAC1_L;
			} else if (r1_Available) {
				return MIXER_ROUTING_DAC1_R;
			}
			break;

		case MIXER_ROUTING_DAC2_R:
			if(r2_Available) {
				return dest;
			} else if (l2_Available) {
				return MIXER_ROUTING_DAC2_L;
			} else if (r1_Available) {
				return MIXER_ROUTING_DAC1_R;
			} else if (l1_Available) {
				return MIXER_ROUTING_DAC1_L;
			}
			break;
	}
	return dest;
}

//-----------------------------------------------------------------------
#if 0
static inline void mixer_moveDataToOutput(uint8_t dest, const float panL, const float panR, int16_t* data,int16_t* outL,int16_t* outR,int16_t* outL2, int16_t* outR2)
{
	//check if a cable is in the selected out
	dest = mixer_checkOutJackAvailable(dest);

	uint8_t i;
	switch(dest)
	{

	case MIXER_ROUTING_DAC1_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL2 = data[i] * panL;
			outL2 += 2;

			*outR2 = data[i] * panR;
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL = data[i] * panL;
			outL += 2;

			*outR = data[i] * panR;
			outR += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL2 = data[i];
			outL2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outR2 = data[i];
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL = data[i];
			outL += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outR = data[i];
			outR += 2;
		}
		break;
	}
}
#endif
//-----------------------------------------------------------------------
#if 0
static float fastTanh(float var)
{
   if(var < -1.95f)     return -1.0f;
   else if(var > 1.95f) return  1.0f;
   else					return  4.15f*var/(4.29f+var*var);
}




double ecc83(float x, float v)
{
	/*
	double input = x -0.05*v*x +0.01*v;//(h100f(x -0.05*v*x +0.01*v))*(1.0+0.55*v) +0.55*v;
	input += 1.0; // [0:2]
	input *= 64.0;
	uint8_t itg = input;
	float frac = input-itg;
	double val;
	if(itg<127)
		val = (1.0-frac) * ecc83Table[itg] + frac * ecc83Table[itg+1];
	else val = ecc83Table[127];
	val = val *(1.0+0.55*v) +0.55*v;
	//if(val>0.999999)val =0.999999;
	return val;
	*/
}

void mixer_saturateOutputs(float gain,  int16_t* outL,int16_t* outR,int16_t* outL2, int16_t* outR2)
{
    gain = 1+gain*2;
    //float v = gain
	for(uint32_t i=0;i<OUTPUT_DMA_SIZE;i++)
	{
		/*
		*outR = __SSAT((int32_t)(ecc83(*outR/32768.0f, gain) * 32768.0f), 16); ;
		outR += 2;

		*outL =  __SSAT((int32_t)(ecc83(*outL/32768.0f, gain) * 32768.0f), 16); ;
		outL += 2;
		*/

		*outR2 =  __SSAT((int32_t)(ecc83(*outR2/32768.0f*gain, 1.) * 32768.0f), 16); ;
		outR2 += 2;

		*outL2 =  __SSAT((int32_t)(ecc83(*outL2/32768.0f*gain, 1.) * 32768.0f), 16); ;
		outL2 += 2;
	}
}
#endif
//-----------------------------------------------------------------------
IN_ITCM_RAM static inline void mixer_addDataToOutputFilteredPan(uint8_t dest, const float pan,  int16_t* data,int16_t* outL,int16_t* outR,int16_t* outL2, int16_t* outR2, int16_t *fxBuffer, OnePoleFilter* filter)
{


	//check if a cable is in the selected out
	dest = mixer_checkOutJackAvailable(dest);
	//assert_param(dest <= MIXER_ROUTING_DAC2_R);

	//TODO may be possible to optimize here using both halfwords of add for stereo mixing
	uint8_t i;
	switch(dest)
	{

	case MIXER_ROUTING_FX_BUS:
#if 0
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			fxBuffer[i] = __QADD16(fxBuffer[i],data[i]) & 0xFFFF;
		}
#else
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{

			//----
			float mixPan 	= calcOnePole(filter, pan);
			float panL = sqrtf((127-mixPan)/127.f);
			float panR = sqrtf(mixPan/127.f);
			//----
			int32_t y;
			int32_t x;
			y = *((int32_t*)fxBuffer);
			x = __PKHBT( (int16_t)(data[i] * panL), (int16_t)(data[i] * panR), 16);

			*((int32_t*)fxBuffer) = __QADD16(x, y);
			fxBuffer += 2;
		}
#endif
		break;

	case MIXER_ROUTING_DAC1_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			//----
			float mixPan 	= calcOnePole(filter, pan);
			float panL = sqrtf((127-mixPan)/127.f);
			float panR = sqrtf(mixPan/127.f);
			//----
#if 1
			int32_t y;
			int32_t x;
			y = *((int32_t*)outL2);
			//x = ((int16_t)(data[i] * panL)& 0xFFFF) | (((int16_t)(data[i] * panR)& 0xFFFF)<<16);
			x = __PKHBT( (int16_t)(data[i] * panL), (int16_t)(data[i] * panR), 16);
			*((int32_t*)outL2) = __QADD16(x, y);
			outL2 += 2;
#else


			*outL2 = __QADD16(*outL2,(int16_t)(data[i] * panL)) & 0xFFFF;
			outL2 += 2;

			*outR2 = __QADD16(*outR2,(int16_t)(data[i] * panR)) & 0xFFFF;
			outR2 += 2;
#endif
		}
		break;
	case MIXER_ROUTING_DAC2_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			//----
			float mixPan 	= calcOnePole(filter, pan);
			float panL = sqrtf((127-mixPan)/127.f);
			float panR = sqrtf(mixPan/127.f);
			//----
#if 1
			int32_t x,y;
			y = *((int32_t*)outL);
			//simdBuffer x;
			//x = ((int16_t)(data[i] * panL)& 0xFFFF) | (((int16_t)(data[i] * panR)& 0xFFFF)<<16);
			//*((int32_t*)outL) = __SADD16(x, y);
			//x.word[0] = data[i] * panL;
			//x.word[1] = data[i] * panR;
			x = __PKHBT( (int16_t)(data[i] * panL), (int16_t)(data[i] * panR), 16);
			//x = __PKHBT(data[i] * panL, data[i] * panR, 16);
			*((int32_t*)outL) = __QADD16(x, y);
			outL += 2;
#else
			*outL = __QADD16(*outL,(int16_t)(data[i] * panL)) & 0xFFFF;
			outL += 2;

			*outR = __QADD16(*outR,(int16_t)(data[i] * panR)) & 0xFFFF;
			outR += 2;
#endif
		}
		break;
	case MIXER_ROUTING_DAC1_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{

			*outL2 = __QADD16(*outL2,data[i]);
			outL2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outR2 = __QADD16(*outR2,data[i]) ;
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL = __QADD16(*outL,data[i]);
			outL += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outR = __QADD16(*outR,data[i]);
			outR += 2;
		}
		break;

	}
}
//#pragma GCC pop_options
//-----------------------------------------------------------------------
//LICHOMAT v0.5: bit v set: voice v is silent until its next trigger (voices_silent()), and the voice
//scheduler drops its phase. In flash, called once per block: inlined, GCC's jump threading makes
//the six checks 916 B of ITCM
__attribute__((noinline)) static uint8_t mixer_silentVoices(void)
{
	uint8_t silent = 0;
	for(uint8_t v = 0; v < VOICES_COUNT; v++)
		if(voices_silent(v))
			silent |= (uint8_t)(1u << v);
	return silent;
}
//-----------------------------------------------------------------------
//LICHOMAT v0.5: voice v's block with the voice scheduler's splits (plan v0.5 Task 7, spec 4.2),
//with the persistent note phase phi (S13 amended): a note started at k has its control steps at
//k of every block, 64 samples each, until the voice's next record.
//- a phase split at phi: [0, phi) ends the step that began at phi in the block before, then the
//  note's next step; skipped when a record at k <= phi cuts that step (the old note's tail);
//- a record split at k: up to k with the values the voice has, then its records and the LFOs it
//  retriggers (S15), a control step, [k, 64), and the phase k;
//- both when phi < k: [0, phi), a step, [phi, k), the split, [k, 64);
//- otherwise one call of 64, as fw 1.7's six sections made it.
//The decimator runs over the whole block; [0, k) is mixed with the pan latch from before the
//split, [k, 64) with the new one (S14).
IN_ITCM_RAM static void mixer_voice(const uint8_t v, int16_t* sampleData, int16_t* output, int16_t* output2, int16_t* fxBusData)
{
	float panL0 = 0.f, panR0 = 0.f, panL, panR;
	const uint8_t k = vsched_split(v);
	const uint8_t phi = vsched_phaseStep(v);
	uint8_t pos = 0;
	if(phi)
	{
		voices_render(v, sampleData, phi);
		voices_control(v);
		pos = phi;
	}
	if(k)
	{
		voices_render(v, sampleData + pos, (uint8_t)(k - pos));
		voices_pan(v, &panL0, &panR0);
		vsched_applySplit(v);
		voices_redispatchLfos(v);
		voices_control(v);
		pos = k;
	}
	voices_render(v, sampleData + pos, (uint8_t)(OUTPUT_DMA_SIZE - pos));
	//decimate voice
	mixer_decimateBlock(v, sampleData);
	//copy to selected dma buffer
	voices_pan(v, &panL, &panR);
	const uint8_t dest = mixer_checkOutJackAvailable(mixer_audioRouting[v]);
	if(k)
	{
		mixer_addSegment(dest, panL0, panR0, sampleData, 0, k, &output[0], &output[1], &output2[0], &output2[1], fxBusData);
		mixer_addSegment(dest, panL, panR, sampleData, k, (uint8_t)(OUTPUT_DMA_SIZE - k), &output[0], &output[1], &output2[0], &output2[1], fxBusData);
	}
	else
	{
		mixer_addSegment(dest, panL, panR, sampleData, 0, OUTPUT_DMA_SIZE, &output[0], &output[1], &output2[0], &output2[1], fxBusData);
	}
}
//-----------------------------------------------------------------------
IN_ITCM_RAM void mixer_calcNextSampleBlock(int16_t* output,int16_t* output2)
{

	r2_Available = (switch_r2_GPIO_Port->IDR & switch_r2_Pin)>0;
	l1_Available = (switch_l1_GPIO_Port->IDR & switch_l1_Pin)>0;
	l2_Available = (switch_l2_GPIO_Port->IDR & switch_l2_Pin)>0;
	r1_Available = (switch_r1_GPIO_Port->IDR & switch_r1_Pin)>0;

	//LICHOMAT v0.5: the voice scheduler's plan of this block (a silent voice back on the block
	//grid first), and its records at offset 0 before the control path, where fw 1.7's triggers
	//came (plan v0.5 Task 7, S12)
	vsched_beginBlock(mixer_silentVoices());
	vsched_applyPhase0();

	modNode_resetTargets();
	//re assign velocity modulation
	modNode_reassignVeloMod();

	//calc and dispatch LFO
	lfo_dispatchNextValue(&voiceArray[0].lfo);
	lfo_dispatchNextValue(&voiceArray[1].lfo);
	lfo_dispatchNextValue(&voiceArray[2].lfo);
	lfo_dispatchNextValue(&snareVoice.lfo);
	lfo_dispatchNextValue(&cymbalVoice.lfo);
	lfo_dispatchNextValue(&hatVoice.lfo);

	//update filter frequencies, then calc async, in fw 1.7's order; LICHOMAT v0.5: a voice with a
	//phase (S13) keeps its note's step until its phase
	for(uint8_t v = 0; v < VOICES_COUNT; v++)
		if(!vsched_phase(v))
			voices_filter(v);
	for(uint8_t v = 0; v < VOICES_COUNT; v++)
		if(!vsched_phase(v))
			voices_async(v);

	//an array to store intermediate voice samples befor output distribution; LICHOMAT v0.5:
	//4-byte aligned, as the voices' pair loops read it at an even offset
	__attribute__((aligned(4))) int16_t sampleData[OUTPUT_DMA_SIZE];
	int16_t fxBusData[OUTPUT_DMA_SIZE*2];

	const uint8_t pos = 0;

	//Clear is necessary, otherwise we keep trash data on non routed outputs
	bufferTool_clearBuffer(output,OUTPUT_DMA_SIZE*2);
	bufferTool_clearBuffer(output2,OUTPUT_DMA_SIZE*2);

	bufferTool_clearBuffer(fxBusData,OUTPUT_DMA_SIZE*2);

	//LICHOMAT v0.5: the six voices in fw 1.7's order: Drum 1-3, snare, cymbal, hi-hat
	for(uint8_t v = 0; v < VOICES_COUNT; v++)
		mixer_voice(v, sampleData, &output[pos], &output2[pos], fxBusData);

	FxBus_process(fxBusData,  &output[pos],&output[pos+1],&output2[pos],&output2[pos+1], OUTPUT_DMA_SIZE);
	djFilter_process(&output[pos], &output2[pos], OUTPUT_DMA_SIZE);	//LICHOMAT v0.4: the DJ filter on both stereo outputs (plan D39)
}
