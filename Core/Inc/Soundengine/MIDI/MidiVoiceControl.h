/*
 * MidiVoiceControl.h
 *
 *  Created on: 03.04.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */


#ifndef MIDIVOICECONTROL_H_
#define MIDIVOICECONTROL_H_

#include "stm32f7xx_hal.h"
#include "Soundengine/config.h"


//-------------- DEFINES ----------------------------
//Note numbers to trigger drum channels
// these are not used anymore. the midi_note setting on each voice controls
// what note that voice responds to now
//#define NOTE_VOICE1		36 // C2
//#define NOTE_VOICE2		37
//#define NOTE_VOICE3		38
//#define NOTE_VOICE4		39
//#define NOTE_VOICE5		40
//#define NOTE_VOICE6		41
//#define NOTE_VOICE7		42 // F#2
//-------------- ENUMS ------------------------------

//--------------- VARS ------------------------------
extern uint8_t voiceStatus[NUM_DRUM_VOICES];
//---------------------------------------------------
//------------- Functions ---------------------------
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel);
//LICHOMAT v0.5: voiceControl_noteOn() in its two halves (plan v0.5, S8): the active bit and the panel's LED byte at fire time; the voice trigger at the trigger's sample
void voiceControl_noteOnEvent(uint8_t voice);
void voiceControl_trigger(uint8_t track, uint8_t note, uint8_t vel);
void voiceControl_noteOff(uint8_t voice);//0xff == all voices
uint8_t voiceControl_isVoicePlaying(uint8_t voice);

#endif /* MIDIVOICECONTROL_H_ */
