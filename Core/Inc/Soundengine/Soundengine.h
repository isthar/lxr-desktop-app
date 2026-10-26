/*
 * Soundengine.h
 *
 *  Created on: Mar 24, 2020
 *      Author: julian
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

#ifndef INC_SOUNDENGINE_SOUNDENGINE_H_
#define INC_SOUNDENGINE_SOUNDENGINE_H_

#include "Soundengine/globals.h"
#include "Soundengine/config.h"

#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/Sequencer/EuklidGenerator.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/MIDI/MidiParser.h"




void soundengine_init();
void soundengine_process();
uint8_t soundengine_calcNextSampleBlock();
/** LICHOMAT CPU meter (cpuMeter.h): call around the audio work, in TIM2_IRQHandler()
 * and in soundengine_process(); blockRendered is what soundengine_calcNextSampleBlock()
 * returned */
void soundengine_cpuBegin(void);
void soundengine_cpuEnd(uint8_t blockRendered);
#endif /* INC_SOUNDENGINE_SOUNDENGINE_H_ */
