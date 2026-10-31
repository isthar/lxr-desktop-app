/*
 * FxBus.c
 *
 *  Created on: 26.08.2020
 *      Author: julian
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */


#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/BufferTools.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/FX/atmo.h"	//LICHOMAT v0.4: the reverb stage (plan D32)
#include "Soundengine/DSPAudio/FX/fxsync.h"	//LICHOMAT v0.4: the synced delay (plan D38)
#include "Soundengine/Sequencer/seq_core.h"	//LICHOMAT v0.4: seqc_currentBpm() (plan D38)
#include <string.h>	//LICHOMAT v0.4: memset() of the reverb's bus (plan D32)
INDTCMZ FxBus fxBus;
uint8_t fxBus_delaySync;	//LICHOMAT v0.4: PAR_FX_DELAY_SYNC, 0 = off (plan D38)

void FxBus_init()
{
	fxBus.dryWet = 0;
	fxBus.audioRouting = 0;
	fxBus.fxType = FX_OFF;

	fxBus.delay.delayType = DELAY_NORMAL;

	Distortion_init(&fxBus.distortion);
	Ringmod_init(&fxBus.ringmod);
	Compressor_init(&fxBus.compressor);
	Delay_init(&fxBus.delay);
	atmo_init();	//LICHOMAT v0.4: the reverb stage (plan D32)



#if USE_REVERB
	Reverb_init(&fxBus.reverb);
#endif
}

//LICHOMAT v0.4: fw 1.7's slot effect, moved unchanged out of FxBus_process() (plan D32)
static __attribute__((noinline)) IN_ITCM_RAM void FxBus_processSlot(int16_t *fxBuffer, int16_t *out, int16_t *out2, uint32_t size, uint8_t channels)
{
	switch(fxBus.fxType)
	{
	default:
	case FX_OFF:
	{
		if(channels == 2)
		{
			int32_t* stereoIn = (int32_t*)fxBuffer;
			int32_t* stereoOut = (int32_t*)out;

			for(uint8_t i=0;i<size;i++)
			{
				//add stereo samples using SIMD
				*stereoOut = __QADD16(*stereoOut, *stereoIn);
				stereoOut++;
				stereoIn++;
			}
		}
		else
		{
			int32_t* in = (int32_t*)fxBuffer;
			for(uint8_t i=0;i<size;i++)
			{
				*out =  __QADD16(*out, *in);
				out +=2;
				in++;

			}
		}
	}
	break;

	case FX_DISTORTION:
		Distortion_process(&fxBus.distortion, fxBuffer, out, out2, size, channels, fxBus.dryWet);
		break;

	case FX_RING:
		Ringmod_process(&fxBus.ringmod, fxBuffer, out, out2, size, channels, fxBus.dryWet);
		break;

	case FX_COMPRESSOR:
		Compressor_process(&fxBus.compressor, fxBuffer, out, out2, size, channels, fxBus.dryWet);
		break;

	case FX_DELAY:
		{	//LICHOMAT v0.4: the synced time, once per block (plan D38)
			const float maxS = (fxBus.delay.delayType == DELAY_NORMAL) ? (float)(FX_BUFFER_SIZE - 64) : (float)((FX_BUFFER_SIZE >> 1) - 64);
			Delay_setSync(&fxBus.delay, fxsync_samples(fxBus_delaySync, seqc_currentBpm(), REAL_FS, maxS));
		}
		if(fxBus.delay.delayType == DELAY_NORMAL)
		{
			Delay_process(&fxBus.delay, fxBuffer, out, out2, size, channels, fxBus.dryWet);
		}
		else
		{
			Delay_processPingPong(&fxBus.delay, fxBuffer, out, out2, size, channels, fxBus.dryWet);
		}
		break;
#if USE_REVERB
	case FX_REVERB:
		Reverb_process(&fxBus.reverb, fxBuffer, out, out2, size, channels, fxBus.dryWet);
		break;
#endif

	}

}

IN_ITCM_RAM void FxBus_process(int16_t *fxBuffer, int16_t* outL,int16_t* outR,int16_t* outL2, int16_t* outR2, uint32_t size)
{
	int16_t* out;
	int16_t* out2 = NULL;
	// < MIXER_ROUTING_DAC1_L -> stereo 1 or 2
	const uint8_t channels = fxBus.audioRouting < MIXER_ROUTING_DAC1_L ? 2 : 1;
	switch(fxBus.audioRouting)
	{
	default:
	case MIXER_ROUTING_DAC1_STEREO:
		out = outL2;
		out2 = outR2;
		break;
	case MIXER_ROUTING_DAC2_STEREO:
		out = outL;
		out2 = outR;
		break;
	case MIXER_ROUTING_DAC1_L:
		out = outL2;
		break;
	case MIXER_ROUTING_DAC1_R:
		out = outR2;
		break;
	case MIXER_ROUTING_DAC2_L:
		out = outL;
		break;
	case MIXER_ROUTING_DAC2_R:
		out = outR;
		break;

	}

	//LICHOMAT v0.4: the reverb after the slot; with the level at 0 this is v0.3's path (plan D32)
	if(!atmo_active())
	{
		FxBus_processSlot(fxBuffer, out, out2, size, channels);
		return;
	}
	int16_t bus[OUTPUT_DMA_SIZE * 2];
	memset(bus, 0, sizeof bus);
	//LICHOMAT v0.4: the slot with the destination's channels, as v0.3 runs it (plan D32: the slot is
	//unchanged; v0.4 review: the compressor's stereo branch follows (L+R)/2, its mono branch L); a mono
	//destination's signal is the reverb's input on both sides
	FxBus_processSlot(fxBuffer, bus, (channels == 2) ? bus + 1 : NULL, size, channels);
	if(channels != 2)
		for(uint32_t i = 0; i < size; i++)
			bus[2 * i + 1] = bus[2 * i];
	atmo_process(bus, size);
	if(channels == 2)
	{
		int32_t *o = (int32_t*)out;
		const int32_t *b = (const int32_t*)bus;
		for(uint32_t i = 0; i < size; i++)
			o[i] = __QADD16(o[i], b[i]);
	}
	else
	{
		for(uint32_t i = 0; i < size; i++)
			out[2 * i] = __SSAT((int32_t)out[2 * i] + bus[2 * i], 16);
	}
}

void FxBus_prepareFxChange()
{
	//memset(FX_BUFFER, 0, FX_BUFFER_SIZE*sizeof(__fp16) );
//	Ringmod_resetPhase(&fxBus.ringmod);
}

IN_ITCM_RAM void FxBus_updateParameter()
{
	switch(fxBus.fxType)
	{
	case FX_OFF:

		break;

	case FX_DISTORTION:
		Distortion_setDrive(&fxBus.distortion, fxBus.parameter[0] );
		Distortion_setParam1(&fxBus.distortion, fxBus.parameter[1] );
		Distortion_setParam2(&fxBus.distortion, fxBus.parameter[2] );
		Distortion_setParam3(&fxBus.distortion, fxBus.parameter[3] );
		break;

	case FX_RING:
		Ringmod_setFreq(&fxBus.ringmod, fxBus.parameter[0] );
		//Ringmod_setWave(&fxBus.ringmod, fxBus.parameter[1] );
		break;

	case FX_COMPRESSOR:

		Compressor_setAttack(&fxBus.compressor , fxBus.parameter[0]);
		Compressor_setRelease(&fxBus.compressor , fxBus.parameter[1]);
		Compressor_setTreshold(&fxBus.compressor , fxBus.parameter[2]);
		Compressor_setGain(&fxBus.compressor , fxBus.parameter[3]);
		break;

	case FX_DELAY:
		//LICHOMAT v0.4: as one update with interrupts masked, as the audio interrupt sets the synced time (plan D38)
		Delay_setRangeAndTime(&fxBus.delay, fxBus.parameter[1], fxBus.parameter[0]);
		Delay_setFilter(&fxBus.delay, fxBus.parameter[2]);
		Delay_setFeedback(&fxBus.delay, fxBus.parameter[3]);

		break;
#if USE_REVERB
	case FX_REVERB:
		Reverb_setDelayTime(&fxBus.reverb, fxBus.parameter[0]);
		Reverb_setFeedback(&fxBus.reverb, fxBus.parameter[1]);
		Reverb_setComposition(&fxBus.reverb, fxBus.parameter[2]);

		Reverb_setVariation(&fxBus.reverb, fxBus.parameter[3]);
		break;
#endif
	default:
		break;
	}
}

IN_ITCM_RAM void FxBus_setParameter(uint8_t idx, float value)
{
	fxBus.parameter[idx] = value;

	FxBus_updateParameter();


}


