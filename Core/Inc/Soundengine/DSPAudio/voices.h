/*
 * voices.h
 *
 * LICHOMAT v0.5 (plan v0.5 Task 6): the six voices by number, 0-2 Drum 1-3, 3 the snare, 4 the
 * cymbal, 5 the hi-hat (tracks 5 and 6), for the mixer's per-voice helper and the host test of
 * split renders; each calls what fw 1.7's mixer called for that voice. Header-only: the
 * mixer, in ITCM, inlines them.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef VOICES_H_
#define VOICES_H_
#include <stdint.h>
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/ResonantFilter.h"
#include "Soundengine/DSPAudio/lfo.h"

#define VOICES_COUNT 6

/** the filter coefficient of the voice's next control step */
static inline void voices_filter(const uint8_t v)
{
	switch(v)
	{
	case 0: case 1: case 2:	SVF_recalcFreq(&voiceArray[v].filter); break;
	case 3:			SVF_recalcFreq(&snareVoice.filter); break;
	case 4:			SVF_recalcFreq(&cymbalVoice.filter); break;
	default:		SVF_recalcFreq(&hatVoice.filter); break;
	}
}

/** the voice's next control step: EGs, snap, slide, oscillator increments */
static inline void voices_async(const uint8_t v)
{
	switch(v)
	{
	case 0: case 1: case 2:	calcDrumVoiceAsync(v); break;
	case 3:			Snare_calcAsync(); break;
	case 4:			Cymbal_calcAsync(); break;
	default:		HiHat_calcAsync(); break;
	}
}

/** a control step at a split */
static inline void voices_control(const uint8_t v)
{
	voices_filter(v);
	voices_async(v);
}

/** n samples (even, at least 2) of the voice from its current state into buf */
static inline void voices_render(const uint8_t v, int16_t *buf, const uint8_t n)
{
	switch(v)
	{
	case 0: case 1: case 2:	calcDrumVoiceSyncBlock(v, buf, n); break;
	case 3:			Snare_calcSyncBlock(buf, n); break;
	case 4:			Cymbal_calcSyncBlock(buf, n); break;
	default:		HiHat_calcSyncBlock(buf, n); break;
	}
}

/** the pan latch of the voice's last trigger (USE_TRIGGERED_PAN) */
static inline void voices_pan(const uint8_t v, float *panL, float *panR)
{
	switch(v)
	{
	case 0: case 1: case 2:	*panL = voiceArray[v].triggeredPanL; *panR = voiceArray[v].triggeredPanR; break;
	case 3:			*panL = snareVoice.triggeredPanL; *panR = snareVoice.triggeredPanR; break;
	case 4:			*panL = cymbalVoice.triggeredPanL; *panR = cymbalVoice.triggeredPanR; break;
	default:		*panL = hatVoice.triggeredPanL; *panR = hatVoice.triggeredPanR; break;
	}
}

/** S15: the LFOs that voice v retriggers, dispatched again after its trigger */
static inline void voices_redispatchLfos(const uint8_t v)
{
	const uint8_t r = (uint8_t)(v + 1u);
	if(voiceArray[0].lfo.retrigger == r)	lfo_dispatchNextValue(&voiceArray[0].lfo);
	if(voiceArray[1].lfo.retrigger == r)	lfo_dispatchNextValue(&voiceArray[1].lfo);
	if(voiceArray[2].lfo.retrigger == r)	lfo_dispatchNextValue(&voiceArray[2].lfo);
	if(snareVoice.lfo.retrigger == r)	lfo_dispatchNextValue(&snareVoice.lfo);
	if(cymbalVoice.lfo.retrigger == r)	lfo_dispatchNextValue(&cymbalVoice.lfo);
	if(hatVoice.lfo.retrigger == r)		lfo_dispatchNextValue(&hatVoice.lfo);
}

#endif /* VOICES_H_ */
