/*
 * MidiVoiceControl.c
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


#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/TriggerOut.h"
#include "Soundengine/MIDI/Uart.h"
#include "Soundengine/MIDI/MidiParser.h"
//#include "LCD_driver.h"
uint8_t voiceStatus[NUM_DRUM_VOICES];
static uint8_t active_voices=0;	// which voices are currently playing a note
//----------------------------------------------------------------
// this fn assumes a valid voice is sent
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel)
{
	active_voices |= (1<<voice);

	if(voice < 3)
		Drum_trigger(voice, vel, note);
	else if(voice < 4)
		Snare_trigger(vel, note);
	else if(voice < 5)
		Cymbal_trigger(vel, note);
	else
		HiHat_trigger(vel,voice-5,note);
	
	//Send trigger out signal
	/*
	if(trigger_isGateModeOn())
	{
		if(vel)
			trigger_triggerVoice(voice, TRIGGER_ON);
	} else {
		trigger_triggerVoice(voice, TRIGGER_PULSE);
	}
	*/

	//no MIDI Note On here: the caller sends exactly one (D4)

	// Send to front panel so it can pulse the LED
	uart_sendFrontpanelByte(NOTE_ON);
	uart_sendFrontpanelByte(voice);
	uart_sendFrontpanelByte(0);
}
//----------------------------------------------------------------
void voiceControl_noteOff(uint8_t voice)
{
	if(voice==0xff)
	{
		active_voices = 0;
		//the note of every voice, then any channel note sent without a voice
		for(uint8_t i=0;i<NUM_TRACKS;i++)
			seq_sendVoiceNoteOff(i);
		seq_midiNoteOff(0xff);
		return;
	}

	active_voices &= (~(1<<voice));

	//send midi note off: the voice's last note, on the channel it was sent on
	seq_sendVoiceNoteOff(voice);
}
//----------------------------------------------------------------
uint8_t voiceControl_isVoicePlaying(uint8_t voice)
{
	return (active_voices & (1<<voice));
}
