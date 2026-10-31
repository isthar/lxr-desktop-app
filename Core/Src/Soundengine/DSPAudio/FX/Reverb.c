/*
 * Reverb.c
 *
 *  Created on: 31.08.2020
 *      Author: julian
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */


#include "Soundengine/DSPAudio/FX/Reverb.h"
#include "Soundengine/DSPAudio/FX/FxRamBuffer.h"
#include "Soundengine/DSPAudio/FX/atmo.h"	//LICHOMAT v0.4: ATMO_REVERB_LEAN (plan D41)
#include <math.h>

//LICHOMAT v0.4: the modulated allpasses' position smoothing and depth in samples (plan D36)
#define REVERB_MOD_SMOOTH_HZ	10.f
#define REVERB_MOD_DEPTH		32.f
//LICHOMAT v0.4: freeze: the loop's feedback, the LFO and its least depth (plan D36, A8)
#define REVERB_FREEZE_FB		0.9999f
#define REVERB_FREEZE_LFO_HZ	0.15f
#define REVERB_FREEZE_MOD		0.3f
//LICHOMAT v0.4: the tone knob (tracker Q31): the corners of the output filter at tone 0, 63, 65 and
//127, and the loop damping's coefficient at tone 0, 64 (fw 1.7's 64/127) and 127
#define REVERB_TONE_LP_HZ		1500.f
#define REVERB_TONE_LP_TOP_HZ	18000.f
#define REVERB_TONE_HP_BOT_HZ	20.f
#define REVERB_TONE_HP_HZ		400.f
#define REVERB_TONE_DAMP_DARK	0.25f
#define REVERB_TONE_DAMP_MID	(64.f / 127.f)
#define REVERB_TONE_DAMP_BRIGHT	0.85f
#define REVERB_TONE_NONE		255



static const float earlyTaps[] =
{
		12,
		24,
		38,
		50
};

void Reverb_init(Reverb *r)
{

	TiltEq_init(&r->eq);
	TiltEq_init(&r->eqFb);
	TiltEq_init(&r->eqFbR);	//LICHOMAT v0.4: the right output (plan D36)
	r->inGain = 1.f;
	r->size = 1.f;
	r->decay = 0.5f;
	r->mod = 0.f;
	r->decim = 1;
	r->lastWetL = r->lastWetR = 0.f;
	r->frozen = 0;	//LICHOMAT v0.4: freeze (plan A8)
	r->damp = 0.5f;
	r->modDepth = 0.f;
	r->tone = REVERB_TONE_NONE;	//LICHOMAT v0.4: tracker Q31

	SineCosine_init(&r->lfo);
	r->reverbBufferSram = REVERB_RAM;	//LICHOMAT v0.4: own buffer (plan D31)

	r->earlyVolume = 1.0f;


	setOnePoleCoef(&r->lpFeedback, 0.5f);
	setOnePoleCoef(&r->lpImpulse, 0.5f);

		//feedbackShaper.setShape(0.7f);
	r->lastOut = r->lastOut2 = 0;
	SineCosine_setFreq(&r->lfo, 1.31f);
	/*
	AllpassDelay apDelay[2];
		AllpassModDelay apModDelay[2];
		*/
#if SPIN_SEMI
		for(int i=0;i<4;i++)
		{
			AllpassDelay_init(&r->apDelay[i][0]);
			AllpassDelay_init(&r->apDelay[i][1]);
			AllpassDelay_setBuffer(&r->apDelay[i][0], &r->reverbBufferSram[i*2*(MAX_ALLPASS_SIZE)]);
			AllpassDelay_setBuffer(&r->apDelay[i][1], &r->reverbBufferSram[(i*2+1)*(MAX_ALLPASS_SIZE)]);
			//apDelay[i][0].setBuffer(&reverbBufferSram[i*2*(MAX_ALLPASS_SIZE)]);
			//apDelay[i][1].setBuffer(&reverbBufferSram[(i*2+1)*(MAX_ALLPASS_SIZE)]);
		}

		// 8 * MAX_ALLPASS_SIZE occupied

		for(int i=0;i<4;i++)
		{
			AllpassDelay_init(&r->inputAllpass[i]);
			//TODO could have way less memory -> max delay is 379 samples
			//inputAllpass[i].setBuffer(&reverbBufferSram[8*MAX_ALLPASS_SIZE + i*(MAX_ALLPASS_SIZE)]);
			AllpassDelay_setBuffer(&r->inputAllpass[i], &r->reverbBufferSram[8*MAX_ALLPASS_SIZE + i*(MAX_ALLPASS_SIZE)]);

		}
#else
		for(int i=0;i<2;i++)
		{
			AllpassDelay_init(&r->apDelay[i]);
			AllpassModDelay_init(&r->apModDelay[i]);

			AllpassDelay_setBuffer(&r->apDelay[i], &r->reverbBufferSram[i*2*(MAX_ALLPASS_SIZE)]);
			AllpassModDelay_setBuffer(&r->apModDelay[i], &r->reverbBufferSram[(i*2+1)*(MAX_ALLPASS_SIZE)]);
		}

		// 4 * MAX_ALLPASS_SIZE occupied

		for(int i=0;i<4;i++)
		{
			AllpassDelay_init(&r->inputAllpass[i]);
			//TODO could have way less memory -> max delay is 379 samples
			//inputAllpass[i].setBuffer(&reverbBufferSram[8*MAX_ALLPASS_SIZE + i*(MAX_ALLPASS_SIZE)]);
			AllpassDelay_setBuffer(&r->inputAllpass[i], &r->reverbBufferSram[4*MAX_ALLPASS_SIZE + i*(MAX_ALLPASS_SIZE)]);

		}
#endif


#if SPIN_SEMI
		// 12 * MAX_ALLPASS_SIZE occupied
		for(int i=0;i<2;i++)
		{
			VerbDelay_init(&r->delay[i]);
			VerbDelay_setBuffer(&r->delay[i], &r->reverbBufferSram[12 * MAX_ALLPASS_SIZE + i*MAX_DELAY_SIZE]);
			//delay[i].setBuffer(&reverbBufferSram[12 * MAX_ALLPASS_SIZE + i*MAX_DELAY_SIZE]);
		}
		for(int i=0;i<2;i++)
		{
			VerbModDelay_init(&r->modDelay[i]);
			//modDelay[i].setBuffer(&reverbBufferSram[14 * MAX_ALLPASS_SIZE + i*MAX_DELAY_SIZE]);
			VerbModDelay_setBuffer(&r->modDelay[i], &r->reverbBufferSram[14 * MAX_ALLPASS_SIZE + i*MAX_DELAY_SIZE]);
		}

		VerbDelay_setBuffer(&r->earlyReflectionDelay, &r->reverbBufferSram[16 * MAX_ALLPASS_SIZE]);
#else
		// 8 * MAX_ALLPASS_SIZE occupied
		for(int i=0;i<4;i++)
		{
			VerbDelay_init(&r->delay[i]);
			VerbDelay_setBuffer(&r->delay[i], &r->reverbBufferSram[8 * MAX_ALLPASS_SIZE + i*MAX_DELAY_SIZE]);
			//delay[i].setBuffer(&reverbBufferSram[12 * MAX_ALLPASS_SIZE + i*MAX_DELAY_SIZE]);
		}



		VerbDelay_init(&r->earlyReflectionDelay);
		VerbDelay_setBuffer(&r->earlyReflectionDelay, &r->reverbBufferSram[12 * MAX_ALLPASS_SIZE]);

		VerbDelay_init(&r->preDelay);
		VerbDelay_setBuffer(&r->preDelay, &r->reverbBufferSram[13 * MAX_ALLPASS_SIZE]);
#endif



		VerbDelay_setLength(&r->earlyReflectionDelay , 2000);
		//44 samples = 1ms, 2030 = 46ms
		VerbDelay_setLength(&r->preDelay, 2031);


		AllpassDelay_setDelay(&r->inputAllpass[0], 142);
		AllpassDelay_setDelay(&r->inputAllpass[1], 107);
		AllpassDelay_setDelay(&r->inputAllpass[2], 379);
		AllpassDelay_setDelay(&r->inputAllpass[3], 277);

		VerbDelay_setLength(&r->delay[0], 2047);
		VerbDelay_setLength(&r->delay[1], 1913); //todo wieso habe ich nur halbe buffer size? größer 4096 geht nicht
	//	delay[1].setLength(3090);

#if SPIN_SEMI
		VerbModDelay_setLength(&r->modDelay[0], 2047);
		VerbModDelay_setLength(&r->modDelay[1], 1999);

		AllpassDelay_setDelay(&r->apDelay[0][0], 672);
		AllpassDelay_setDelay(&r->apDelay[0][1], 757);

		AllpassDelay_setDelay(&r->apDelay[1][0], 1800);
		AllpassDelay_setDelay(&r->apDelay[1][1], 743);

		AllpassDelay_setDelay(&r->apDelay[2][0], 908);
		AllpassDelay_setDelay(&r->apDelay[2][1], 523);

		AllpassDelay_setDelay(&r->apDelay[3][0], 2039);
		AllpassDelay_setDelay(&r->apDelay[3][1], 877);

		VerbModDelay_setModDepth(&r->modDelay[0], 170);
		VerbModDelay_setModDepth(&r->modDelay[1], 170);

/*
		for(int i=0;i<4;i++)
		{
				AllpassDelay_setCoefficient(&r->apDelay[i][0], INITIAL_ALLPASS_COFF);
				AllpassDelay_setCoefficient(&r->apDelay[i][1], INITIAL_ALLPASS_COFF);
		}

		*/

		AllpassDelay_setCoefficient(&r->apDelay[i][0], 0.7);
		AllpassDelay_setCoefficient(&r->apDelay[i][1], 0.7);

		AllpassDelay_setCoefficient(&r->apDelay[i][0], 0.7);
		AllpassDelay_setCoefficient(&r->apDelay[i][1], 0.7);

		AllpassDelay_setCoefficient(&r->apDelay[i][0], 0.5);
		AllpassDelay_setCoefficient(&r->apDelay[i][1], 0.5);

		AllpassDelay_setCoefficient(&r->apDelay[i][0], 0.5);
		AllpassDelay_setCoefficient(&r->apDelay[i][1], 0.5);
	    //AllpassDelay_setCoefficient(&r->apDelay[2][1], -INITIAL_ALLPASS_COFF);
#else
		VerbDelay_setLength(&r->delay[2], 1720);
		VerbDelay_setLength(&r->delay[3], 1163); //todo wieso habe ich nur halbe buffer size? größer 4096 geht nicht

		AllpassDelay_setDelay(&r->apDelay[0], 1800);
		AllpassDelay_setDelay(&r->apDelay[1], 2039);

		AllpassModDelay_setDelay(&r->apModDelay[0], 672);
		AllpassModDelay_setDelay(&r->apModDelay[1], 908);

		for(int i=0;i<2;i++)
		{
			//LICHOMAT v0.4: VerbModDelay_init()'s setOnePoleFreq(0.00013) smooths the read position with
			//a time constant of about 20 minutes, so the unit starts near 0 samples and neither the size
			//nor the LFO reaches it. 10 Hz here (16 ms, 32 ms at half rate), from the unit's length, and a
			//depth: the LFO's amplitude is 0.5, so +-16 samples at modulation 127 (plan D36)
			setOnePoleFreq(&r->apModDelay[i].delay.lp, REVERB_MOD_SMOOTH_HZ);
			setOnePoleValue(&r->apModDelay[i].delay.lp, VerbModDelay_getLength(&r->apModDelay[i].delay));
			AllpassModDelay_setModDepth(&r->apModDelay[i], REVERB_MOD_DEPTH);
		}
#endif
	Reverb_setDecay(r, r->decay);	//LICHOMAT v0.4: a feedback from the start (plan D36)

	    AllpassDelay_setCoefficient(&r->inputAllpass[0], 0.75);
	    AllpassDelay_setCoefficient(&r->inputAllpass[1], 0.75);
	    AllpassDelay_setCoefficient(&r->inputAllpass[2], 0.625);
	    AllpassDelay_setCoefficient(&r->inputAllpass[3], 0.625);

}




//LICHOMAT v0.4: one tick of the reverb's core: the per-sample body of fw 1.7's Reverb_process(),
//unchanged, plus the right output of the mirrored taps (plan D36)
static inline void Reverb_tick(Reverb *r, float monoInput, float *wetL, float *wetR)
{
		monoInput = VerbDelay_process(&r->preDelay, monoInput);

		float tmp;
		float in, in2, out, out2;
#if USE_OUT_2
		float out2;
#endif

#if HIGH_RES_SINE_COS
		 SineCosine_calc(&r->lfo);
		 VerbModDelay_setModValue(&r->modDelay[1], SineCosine_getSine(&r->lfo));
		 VerbModDelay_setModValue(&r->modDelay[0], SineCosine_getCosine(&r->lfo));
#endif

		 //in = 0.5 * calcOnePole(&r->lpImpulse, AllpassDelay_process(&r->inputAllpass[0],  AllpassDelay_process(&r->inputAllpass[1], AllpassDelay_process(&r->inputAllpass[2], AllpassDelay_process(&r->inputAllpass[3], (monoInput))))));

		 	 //############## early reverb ####################

		 float early = 0;
		 //float vol = 1;//0.25f;
#if !ATMO_REVERB_LEAN	//LICHOMAT v0.4: lean has no early reflections (plan D41)
		 VerbDelay_process(&r->earlyReflectionDelay, monoInput);
		 /*
		 for(int j=0;j<EARLY_TAPS;j++)
		 {
		   early += VerbDelay_ReadTap(&r->earlyReflectionDelay, earlyTaps[j]) * vol ;
		   vol *= 0.5f;
		 }
		 */
		 early += VerbDelay_ReadTap(&r->earlyReflectionDelay, earlyTaps[0]) ;
		 early += VerbDelay_ReadTap(&r->earlyReflectionDelay, earlyTaps[1]) * 0.5f ;
		 early += VerbDelay_ReadTap(&r->earlyReflectionDelay, earlyTaps[2]) * 0.125f ;
		 early += VerbDelay_ReadTap(&r->earlyReflectionDelay, earlyTaps[3]) * 0.25f ;
#endif

		 monoInput = 0.5 * TiltEq_process(&r->eq, monoInput);//calcOnePole(&r->lpImpulse, monoInput);
		 in  = AllpassDelay_process(&r->inputAllpass[0], AllpassDelay_process(&r->inputAllpass[1], (monoInput)));
		 in2 = AllpassDelay_process(&r->inputAllpass[2], AllpassDelay_process(&r->inputAllpass[3], (monoInput)));



		 //in = 0.5*lpImpulse.process(inputAllpass[0].process(inputAllpass[1].process(inputAllpass[2].process(inputAllpass[3].process((*input1)<<16)))));







		 //############## Late reverb ####################

#if SPIN_SEMI

		    tmp = AllpassDelay_process(&r->apDelay[0][0], (in + r->lastOut));
#if !USE_LOWER_DENSITY
		    tmp = AllpassDelay_process(&r->apDelay[0][1], tmp);
#endif
		    tmp = VerbDelay_process(&r->delay[0], tmp);

		    out = tmp;


		    //out = __QSUB16((int16_t)(VerbDelay_ReadTap(&r->delay[0], 75)*0.83f),out);
		    //out = __QADD16((int16_t)(VerbDelay_ReadTap(&r->delay[0], 33)*0.2f),out);
		    out = (VerbDelay_ReadTap(&r->delay[0], 75)*0.83f) - out;
		    out = ((VerbDelay_ReadTap(&r->delay[0], 33)*0.2f) + out);
#if USE_OUT_2
		    out2 = tmp * 0.1;
#endif


		    tmp = AllpassDelay_process(&r->apDelay[1][0], tmp*r->feedback);
#if !USE_LOWER_DENSITY
		    tmp = AllpassDelay_process(&r->apDelay[1][1], tmp);
#endif
		    tmp = VerbModDelay_process(&r->modDelay[0], tmp);

		     //out = __QADD16((int16_t)(tmp*0.2f),out);
		    out = ((tmp*0.67) + out);



#if USE_OUT_2
		     out2 = ((tmp*0.2) + out2);
#endif


		    tmp = AllpassDelay_process(&r->apDelay[2][0], (in + tmp*r->feedback));
#if !USE_LOWER_DENSITY
		    tmp = AllpassDelay_process(&r->apDelay[2][1], tmp);
#endif
		    tmp = VerbDelay_process(&r->delay[1], tmp);
		    out = ((tmp*0.7f)+out);;

#if USE_OUT_2
		    out2 = (tmp * 0.5) + out2;
		    out2 = (VerbDelay_ReadTap(&r->delay[1], 33)*0.7) +out2;
#endif



		    tmp = AllpassDelay_process(&r->apDelay[3][0], tmp*r->feedback);
#if !USE_LOWER_DENSITY
		    tmp = AllpassDelay_process(&r->apDelay[3][1], tmp);
#endif
		    tmp = VerbModDelay_process(&r->modDelay[1], tmp);

		    r->lastOut = calcOnePole(&r->lpFeedback, tmp*r->feedback);

		    out = ((tmp*0.1f) + out);

#if USE_OUT_2
		    out2 = ((tmp) + out2);;
		    out2 = ((VerbModDelay_interpolatedReadTap(&r->modDelay[1],17)*0.2) + out2);
#endif


		   // output[0][i] = interpolate(input[0][i],output[0][i],dryWet) + early * earlyMix;
		   // output[1][i] = interpolate(input[1][i],output[1][i],dryWet) + early * earlyMix;
		  // return  dryWet*(__QADD16(out*0.5f,out2*0.5f) ) + (1-dryWet)*input;;
		    //return  (((revItg)(dryWet*(add(out , out2>>1) )))>>16) + (1-dryWet)*input;;
#if USE_OUT_2
		    out = (out + out2/2);
#endif

#if 0
		  //  r->lastOut = calcOnePole(&r->lpFeedback, tmp*r->feedback);
#else
		  //  *input1 =  dryWetMixer(out,*input1,dryWet);


		   // return  (((revItg)(dryWet*(add(out , out2) )))>>16) + (1-dryWet)*input;;

	//	   return  dryWet*out + (1-dryWet)*input;
#endif
#else




		    //loop 1
		    // in1 + loop 2 out ->apMod[0]->del[0]-> * fb -> ap[0] -> del[1] * fb --> loop 2
		    tmp = AllpassModDelay_process(&r->apModDelay[0], (in + r->lastOut2));
		    tmp = VerbDelay_process(&r->delay[0], tmp);

		    tmp *= r->feedback;

		    tmp = AllpassDelay_process(&r->apDelay[0], (tmp));	//LICHOMAT v0.4: loop 1's own allpass; fw 1.7 used apDelay[1], loop 2's (plan AF1)
		    tmp = VerbDelay_process(&r->delay[1], tmp);

		     r->lastOut = calcOnePole(&r->lpFeedback, tmp*r->feedback);





		    //r->lastOut = tmp * r->feedback;

		    //loop 2
		    // in2 + loop 1 out ->apMod[1]->del[2]-> * fb -> ap[1] -> del[3] * fb --> loop 1
		    tmp = AllpassModDelay_process(&r->apModDelay[1], (in2 + r->lastOut));
			tmp = VerbDelay_process(&r->delay[2], tmp);

			tmp *= r->feedback;

			tmp = AllpassDelay_process(&r->apDelay[1], (tmp));
			tmp = VerbDelay_process(&r->delay[3], tmp);

			r->lastOut2 = tmp * r->feedback;

		    /*/********* left output, all wet *********
		     accumulator = 0.6 X node48_54[266]
		     accumulator += 0.6 x node48_54[2974]
		     accumulator -= 0.6 X node55_59[1913]
		     accumulator += 0.6 X node59_63[1996]
		     accumulator -= 0.6 X node24_30[1990]
		     accumulator -= 0.6 x node31_33[187]
		     YL = accumulator - 0.6 X node33_39[1066]

		     ********* right output, all wet *********
		     ********* accumulator = 0.6 X node24_30[353]
		     ********* accumulator += 0.6 X node24_30[3627]
		     ********* accumulator -= 0.6 X node31_33[1228]
		     ********* accumulator += 0.6 X node33_39[2673]
		     ********* accumulator -= 0.6 X node48_54[2111]
		     ********* accumulator -= 0.6 X node55_59[335]
		     ********* YR = accumulator - 0.6 X node59_63[121] */
			/*
			delay[0], 2047);
			delay[1], 1913);
			delay[2], 1720);
			delay[3], 1163);

			apDelay[0], 1800);
			apDelay[1], 2039);

			apModDelay[0], 672);
			apModDelay[1], 908);
			*/



		    //read output taps
			out2  = VerbDelay_ReadTapSample(&r->delay[2], 266) * 0.6;
			//out2 += VerbDelay_ReadTapSample(&r->delay[2], 2046) * 0.6;
			out2 -= VerbDelay_ReadTapSample(&r->apDelay[1].delay, 1913) * 0.6;
			out2 += VerbDelay_ReadTapSample(&r->delay[3], 996) * 0.6;
			//out2 -= VerbDelay_ReadTapSample(&r->delay[0], 1990) * 0.6;
			out2 -= VerbDelay_ReadTapSample(&r->apDelay[0].delay, 187) * 0.6;
			out2 -= VerbDelay_ReadTapSample(&r->delay[1], 1066) * 0.6;

			out= TiltEq_process(&r->eqFb,fmaf(early, r->earlyVolume, out2));

#if ATMO_REVERB_LEAN
			*wetR = out;	//LICHOMAT v0.4: lean, the wet R is the wet L (plan D41)
#else
			float outR;
			outR  = VerbDelay_ReadTapSample(&r->delay[0], 266) * 0.6f;	//LICHOMAT v0.4: the mirrored taps (plan D36)
			outR -= VerbDelay_ReadTapSample(&r->apDelay[0].delay, 1913) * 0.6f;
			outR += VerbDelay_ReadTapSample(&r->delay[1], 996) * 0.6f;
			outR -= VerbDelay_ReadTapSample(&r->apDelay[1].delay, 187) * 0.6f;
			outR -= VerbDelay_ReadTapSample(&r->delay[3], 1066) * 0.6f;
			*wetR = TiltEq_process(&r->eqFbR, fmaf(early, r->earlyVolume, outR));
#endif

#endif
			*wetL = out;
}

//LICHOMAT v0.4: the wet signal of a stereo block, L and R (plan D36). At half rate each core tick
//takes the mean of two frames, and the outputs interpolate linearly from the previous tick (plan D42).
void Reverb_processWet(Reverb *r, const int16_t *in, float *wetL, float *wetR, uint16_t frames)
{
#if !HIGH_RES_SINE_COS
	 SineCosine_calc(&r->lfo);
	 VerbModDelay_setModValue(&r->apModDelay[1].delay, SineCosine_getSine(&r->lfo) * r->modDepth);
	 VerbModDelay_setModValue(&r->apModDelay[0].delay, SineCosine_getCosine(&r->lfo) * r->modDepth);
#endif
	const float g = r->inGain * (0.5f / 32768.f);
	if(r->decim == 2)
	{
		float prevL = r->lastWetL;
		float prevR = r->lastWetR;
		for(uint16_t i = 0; i + 1 < frames; i += 2)
		{
			const float x = ((float)in[2*i] + (float)in[2*i+1] + (float)in[2*i+2] + (float)in[2*i+3]) * (0.5f * g);
			float l, rr;
			Reverb_tick(r, x, &l, &rr);
			wetL[i] = 0.5f * (prevL + l);
			wetR[i] = 0.5f * (prevR + rr);
			wetL[i+1] = l;
			wetR[i+1] = rr;
			prevL = l;
			prevR = rr;
		}
		r->lastWetL = prevL;
		r->lastWetR = prevR;
	}
	else
	{
		for(uint16_t i = 0; i < frames; i++)
			Reverb_tick(r, ((float)in[2*i] + (float)in[2*i+1]) * g, &wetL[i], &wetR[i]);
	}
}

//void Reverb_process(Reverb *r, int16_t* input1, int16_t* input2)
void Reverb_process(Reverb *r, int16_t *fxBuffer,int16_t* outL, int16_t* outR, uint16_t size,  uint8_t channels, float dryWet)
{
	//LICHOMAT v0.4: the core is Reverb_processWet()'s; fw 1.7's dry/wet mix of the (mono) wet L stays
	float wetL[OUTPUT_DMA_SIZE], wetR[OUTPUT_DMA_SIZE];
	int32_t x;
	int32_t* stereoOut = (int32_t*)outL;

	if(size > OUTPUT_DMA_SIZE) size = OUTPUT_DMA_SIZE;
	Reverb_processWet(r, fxBuffer, wetL, wetR, size);
	for(uint8_t i=0; i<size;i++)
	{
		const uint8_t idx1 = i*2;
		const uint8_t idx2 = i*2+1;
			int16_t wet =  __SSAT(wetL[i]*32768, 16);
			//int16_t dw = fxBuffer[i] + __SSAT((wet - fxBuffer[i]) * dryWet,16);

			x = __PKHBT(fxBuffer[idx1] + __SSAT((wet - fxBuffer[idx1]) * dryWet,16), fxBuffer[idx2] + __SSAT((wet - fxBuffer[idx2]) * dryWet,16), 16);
			//LERP dryWet
			*stereoOut = __QADD16(*stereoOut, x);
			stereoOut++;
	}
}
IN_ITCM_RAM void Reverb_setDelayTime(Reverb *r, float t)
{
#if SPIN_SEMI
	if(r->delayTime != t)
	{
		r->delayTime = t;

		for(int i=0;i<2;i++)
		{
			VerbDelay_setRoomSize(&r->delay[i], t);
		}
		for(int i=0;i<2;i++)
		{
			VerbModDelay_setRoomSize(&r->modDelay[i], t);
		}

		VerbDelay_setRoomSize(&r->earlyReflectionDelay, t);
	}
#else
	/*
	  	//44 samples = 1ms, 2030 = 46ms
		VerbDelay_setLength(&r->preDelay, 2031);
	 */
	float sz = 0.02166f + t*0.97784342688330871492f;
	VerbDelay_setRoomSize(&r->preDelay, sz);
#endif
}

IN_ITCM_RAM void Reverb_setFeedback(Reverb *r, float fb)
{
	r->feedback = fb;//feedbackShaper.shape(fb);
}

IN_ITCM_RAM void Reverb_setComposition(Reverb *r, float c)
{
	TiltEq_setParameter(&r->eq, -1+2*c);
	TiltEq_setParameter(&r->eqFb, -1+2*c);
	TiltEq_setParameter(&r->eqFbR, -1+2*c);	//LICHOMAT v0.4: the right output (plan D36)
	//compositionParameter = c;
	r->damp = c;	//LICHOMAT v0.4: frozen, the loop keeps no damping until unfrozen (plan A8)
	setOnePoleCoef(&r->lpFeedback, r->frozen ? 1.f : c);
//	setOnePoleCoef(&r->lpImpulse, c);
}

//LICHOMAT v0.4: the knobs of plan D36 and D42
static float Reverb_clamp01(float v)
{
	return (v > 0.f) ? ((v < 1.f) ? v : 1.f) : 0.f;
}

//RT60 = 0.3 s * 2^(6 d); the loop multiplies by fb four times per round trip of its eight units,
//tau = 12,262 s samples of the core, so fb = 10^(-3 tau / (4 RT60)); in expf() only, which the
//emulator vendors (its check M19)
static void Reverb_calcFeedback(Reverb *r)
{
	if(r->frozen)
	{	/* plan D36: -0.3 dB per minute, the loop always loses a little */
		r->feedback = REVERB_FREEZE_FB;
		return;
	}
	const float rt60 = 0.3f * expf(6.f * 0.69314718f * r->decay);
	const float tau = 12262.f * r->size * (float)r->decim / REAL_FS;
	r->feedback = expf(-0.75f * 2.30258509f * tau / rt60);
}

void Reverb_setDecim(Reverb *r, uint8_t decim)
{
	r->decim = (decim == 2) ? 2 : 1;
	r->lastWetL = r->lastWetR = 0.f;
	Reverb_calcFeedback(r);
	if(r->tone != REVERB_TONE_NONE)	/* the tone's corners are in Hz of the core's rate */
		Reverb_setTone(r, r->tone / 127.f);
}

void Reverb_setSize(Reverb *r, float s01)
{
	const float s = 0.4f + 0.6f * Reverb_clamp01(s01);
	r->size = s;
	for(int i=0;i<4;i++)
		VerbDelay_setRoomSize(&r->delay[i], s);
	for(int i=0;i<2;i++)
	{
		AllpassDelay_setRoomSize(&r->apDelay[i], s);
		AllpassModDelay_setRoomSize(&r->apModDelay[i], s);
	}
	Reverb_calcFeedback(r);
}

void Reverb_setDecay(Reverb *r, float d01)
{
	r->decay = Reverb_clamp01(d01);
	Reverb_calcFeedback(r);
}

//the LFO runs at 0.2 + 1.3 m Hz; Reverb_processWet() steps it once per block of OUTPUT_DMA_SIZE frames
void Reverb_setMod(Reverb *r, float m01)
{
	r->mod = Reverb_clamp01(m01);
	if(r->frozen)
	{	/* plan D36: frozen, the LFO at 0.15 Hz with a depth of at least 0.3 */
		r->modDepth = (r->mod > REVERB_FREEZE_MOD) ? r->mod : REVERB_FREEZE_MOD;
		SineCosine_setFreq(&r->lfo, REVERB_FREEZE_LFO_HZ * (float)OUTPUT_DMA_SIZE);
	}
	else
	{
		r->modDepth = r->mod;
		SineCosine_setFreq(&r->lfo, (0.2f + 1.3f * r->mod) * (float)OUTPUT_DMA_SIZE);
	}
}

//plan D36, A8: frozen, the input gain is 0, the feedback 0.9999, the loop damping off (the
//coefficient of tone 127) and the LFO slow; unfrozen, the decay, tone and mod settings return.
//The size, decay, tone and mod setters keep the freeze while it lasts.
void Reverb_setFreeze(Reverb *r, uint8_t on)
{
	r->frozen = (on != 0);
	r->inGain = r->frozen ? 0.f : 1.f;
	Reverb_calcFeedback(r);
	setOnePoleCoef(&r->lpFeedback, r->frozen ? 1.f : r->damp);
	Reverb_setMod(r, r->mod);
}

//tracker Q31: a one-pole low-pass (or, as x - low-pass, high-pass) at hz on a tilt EQ, matched to
//the core's rate fs: a = 1 - e^(-2 pi hz / fs), whose -3 dB point is hz within 2 % up to 1.5 kHz
//at both rates (fw 1.7's TiltEq_setFreq() uses 3 REAL_FS at any rate)
static void Reverb_setOnePoleEq(TiltEq *eq, float hz, float fs, uint8_t highpass)
{
	const float a = 1.f - expf(-6.2831853f * hz / fs);
	eq->a0 = a;
	eq->b1 = 1.f - a;
	eq->lgain = highpass ? 0.f : 1.f;
	eq->hgain = highpass ? 1.f : 0.f;
}

//tracker Q31: the tone 0..1 (0..127). fw 1.7's Reverb_setComposition(tone/127) is nearly silent at
//both ends: tone 1 a low-pass at about 930 Hz on the input and the output with a loop damping of
//0.008 (a 55 Hz one-pole in the loop), tone 127 a high-pass at 20 kHz. Here:
//- tone 64 is Reverb_setComposition(64/127), A7's coefficients bit for bit (flat);
//- the input EQ keeps tone 64's setting, and the tone is one pole on each output (6 dB per octave,
//  heard at once on the tail already in the tank): 0..63 a low-pass from 1.5 kHz to 18 kHz,
//  65..127 a high-pass from 20 Hz to 400 Hz, both geometric in the knob (equal steps are equal
//  intervals, as the DJ filter's, plan D39);
//- the loop damping is geometric too, from 0.25 (tone 0) through 64/127 to 0.85 (tone 127): the
//  dark side's tail darkens as it decays, the bright side's keeps its air;
//- entering tone 64, or crossing from the low-pass side to the high-pass side, clears the output
//  filters' state: tone 64's high-pass is at 0.01 Hz, so the state of a low-pass (the signal) or of
//  the 400 Hz high-pass (its bass) would stay on the output as an offset for about 16 s.
void Reverb_setTone(Reverb *r, float t01)
{
	const float tf = Reverb_clamp01(t01) * 127.f + 0.5f;
	const uint8_t t = (uint8_t)tf;
	const uint8_t old = r->tone;
	if((old != t) && ((t == 64) || ((old < 64) != (t < 64))))
		r->eqFb.lp_out = r->eqFbR.lp_out = 0.f;
	if(t == 64)
	{
		Reverb_setComposition(r, 64.f / 127.f);
		r->tone = 64;
		return;
	}
	TiltEq_setParameter(&r->eq, -1 + 2 * (64.f / 127.f));	/* the input EQ as at tone 64 */
	const float fs = REAL_FS / (float)r->decim;
	float hz, damp;
	if(t < 64)
	{	/* x = 1 at tone 0, 1/63 at tone 63; ln(18000/1500) = 2.4849, ln(0.50394/0.25) = 0.70110 */
		const float x = (63.f - (float)t) / 63.f;
		hz = REVERB_TONE_LP_TOP_HZ * expf(-2.4849066f * x);
		damp = REVERB_TONE_DAMP_MID * expf(-0.70110f * (64.f - (float)t) / 64.f);
	}
	else
	{	/* x = 1 at tone 127, 0 at tone 65; ln(400/20) = 2.9957, ln(0.85/0.50394) = 0.52281 */
		const float x = ((float)t - 65.f) / 62.f;
		hz = REVERB_TONE_HP_BOT_HZ * expf(2.9957323f * x);
		damp = REVERB_TONE_DAMP_MID * expf(0.52281f * ((float)t - 64.f) / 63.f);
	}
	Reverb_setOnePoleEq(&r->eqFb, hz, fs, t > 64);
	Reverb_setOnePoleEq(&r->eqFbR, hz, fs, t > 64);
	r->damp = damp;
	setOnePoleCoef(&r->lpFeedback, r->frozen ? 1.f : damp);
	r->tone = t;
}

//the loop's state after REVERB_RAM was cleared (atmo.c, on waking)
void Reverb_clearState(Reverb *r)
{
	r->lastOut = r->lastOut2 = 0.f;
	r->lastWetL = r->lastWetR = 0.f;
	setOnePoleValue(&r->lpFeedback, 0.0);
}

IN_ITCM_RAM void Reverb_setVariation(Reverb *r, float v)
{
#if SPIN_SEMI
	//variationParameter = v;
	for(int i=0;i<4;i++)
	{
		const float scale = (i+7)/10.f;
		AllpassDelay_setCoefficient(&r->apDelay[i][0], v*scale);
		AllpassDelay_setCoefficient(&r->apDelay[i][1], v*scale*0.8);
		//AllpassDelay_setCoefficient(&r->inputAllpass[i], v*0.9*scale);
	}
#else
	r->earlyVolume = v;
#endif
}


#if 0

static const float earlyTaps[] =
{
		12,
		24,
		38,
		50
};

AllpassLoopVerb::AllpassLoopVerb()
{
	reverbBufferSram = (revItg*)sramBuffer;



	feedbackShaper.setShape(0.7f);
	lastOut = 0;
	lfo.setFreq(1.31f);  //1.18hz

	for(int i=0;i<4;i++)
	{
		apDelay[i][0].setBuffer(&reverbBufferSram[i*2*(MAX_ALLPASS_SIZE)]);
		apDelay[i][1].setBuffer(&reverbBufferSram[(i*2+1)*(MAX_ALLPASS_SIZE)]);
	}
	// 8 * MAX_ALLPASS_SIZE occupied

	for(int i=0;i<4;i++)
	{
		//TODO could have way less memory -> max delay is 379 samples
		inputAllpass[i].setBuffer(&reverbBufferSram[8*MAX_ALLPASS_SIZE + i*(MAX_ALLPASS_SIZE)]);
	}
	// 12 * MAX_ALLPASS_SIZE occupied
	for(int i=0;i<2;i++)
	{
		delay[i].setBuffer(&reverbBufferSram[12 * MAX_ALLPASS_SIZE + i*MAX_DELAY_SIZE]);
	}
	for(int i=0;i<2;i++)
	{
		modDelay[i].setBuffer(&reverbBufferSram[14 * MAX_ALLPASS_SIZE + i*MAX_DELAY_SIZE]);
	}

	earlyReflectionDelay.setBuffer(&reverbBufferSram[16 * MAX_ALLPASS_SIZE]);
	earlyReflectionDelay.setLength(4000);

	inputAllpass[0].setDelay(142);
	inputAllpass[1].setDelay(107);
	inputAllpass[2].setDelay(379);
	inputAllpass[3].setDelay(277);

	delay[0].setLength(1283);
	delay[1].setLength(1109); //todo wieso habe ich nur halbe buffer size? größer 4096 geht nicht
//	delay[1].setLength(3090);

	modDelay[0].setLength(2393);
	modDelay[1].setLength(5281);

	apDelay[0][0].setDelay(853);
	apDelay[0][1].setDelay(757);

	apDelay[1][0].setDelay(947);
	apDelay[1][1].setDelay(743);

	apDelay[2][0].setDelay(953);
	apDelay[2][1].setDelay(523);

	apDelay[3][0].setDelay(197);
	apDelay[3][1].setDelay(877);

	modDelay[0].setModDepth(170);
    modDelay[1].setModDepth(170);

	for(int i=0;i<4;i++)
	{
			apDelay[i][0].setCoefficient(INITIAL_ALLPASS_COFF);
			apDelay[i][1].setCoefficient(INITIAL_ALLPASS_COFF);
	}
    apDelay[2][1].setCoefficient(-INITIAL_ALLPASS_COFF);

    inputAllpass[2].setCoefficient(-0.5);

}

void AllpassLoopVerb::setDelayTime(float t)
{
	if(delayTime != t)
	{
		delayTime = t;

		for(int i=0;i<2;i++)
		{
			delay[i].setRoomSize(t);
		}
		for(int i=0;i<2;i++)
		{
			modDelay[i].setRoomSize(t);
		}

		earlyReflectionDelay.setRoomSize(t);
	}
}

void AllpassLoopVerb::setComposition(float c)
{
	compositionParameter = c;
	lpFeedback.setFreq(c);
	lpImpulse.setFreq(c);
}

void AllpassLoopVerb::setVariation(float v)
{
	variationParameter = v;
	for(int i=0;i<4;i++)
		{
			apDelay[i][0].setCoefficient(v);
			apDelay[i][1].setCoefficient(v);
			inputAllpass[i].setCoefficient(v);
		}
}

void AllpassLoopVerb::setFeedback(float fb)
{
	feedback = feedbackShaper.shape(fb);
}


void AllpassLoopVerb::process(int16_t* input1, int16_t* input2)
{
	revItg tmp;
	revItg in, out, out2;

	 lfo.calc();
	 modDelay[1].setModValue(lfo.getSine());
	 modDelay[0].setModValue(lfo.getCosine());



	 in = 0.5*lpImpulse.process(inputAllpass[0].process(inputAllpass[1].process(inputAllpass[2].process(inputAllpass[3].process((*input1)<<16)))));


	 	 //############## early reverb ####################

	 float early = 0;
	 float vol = 0.25f;
	 earlyReflectionDelay.process(*input1);
	 for(int j=0;j<EARLY_TAPS;j++)
	 {
	   early += earlyReflectionDelay.ReadTap(earlyTaps[j]) * vol ;
	   vol *= 0.5f;
	 }


	 //############## Late reverb ####################

	    tmp = apDelay[0][0].process(in + lastOut);
	    tmp = apDelay[0][1].process(tmp);

	    tmp = delay[0].process(tmp);

	    out = tmp;

	    out = sub((int16_t)(delay[0].ReadTap(75)*0.83f),out);
	    out = add((int16_t)(delay[0].ReadTap(33)*0.2f),out);

	    out2 = tmp * 0.1;



	    tmp = apDelay[1][0].process(tmp*feedback);
	    tmp = apDelay[1][1].process(tmp);
	    tmp = modDelay[0].process(tmp);

	     out = add((int16_t)(tmp*0.2f),out);


	     out2 = add((int16_t)(tmp*0.67),out2);

	    tmp = apDelay[2][0].process(in + tmp*feedback);
	    tmp = apDelay[2][1].process(tmp);
	    tmp = delay[1].process(tmp);

	    out2 = add((int16_t)(tmp * 0.5),out2);;
	    out2 = sub((int16_t)(delay[2].ReadTap(33)*0.7),out2);;

	    out = add((int16_t)(tmp*0.7f),out);;


	    tmp = apDelay[3][0].process(tmp*feedback);
	    tmp = apDelay[3][1].process(tmp);
	    tmp = modDelay[1].process(tmp);

	    lastOut = lpFeedback.process(tmp*feedback);

	    out = add((int16_t)(tmp*0.1f),out);


	    out2 = add((int16_t)(tmp),out2);;
	    out2 = add((int16_t)(modDelay[1].interpolatedReadTap(17)*0.2),out2);



	   // output[0][i] = interpolate(input[0][i],output[0][i],dryWet) + early * earlyMix;
	   // output[1][i] = interpolate(input[1][i],output[1][i],dryWet) + early * earlyMix;
	  // return  dryWet*(__QADD16(out*0.5f,out2*0.5f) ) + (1-dryWet)*input;;
	    //return  (((revItg)(dryWet*(add(out , out2>>1) )))>>16) + (1-dryWet)*input;;

	    out = (add(out , out2>>1)>>16);

	    *input1 =  dryWetMixer(out,*input1,dryWet);


	   // return  (((revItg)(dryWet*(add(out , out2) )))>>16) + (1-dryWet)*input;;

//	   return  dryWet*out + (1-dryWet)*input;

}

#endif
