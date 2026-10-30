/*
 * sequencer.c
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


#include "stm32f7xx_hal.h"
#include "Soundengine/globals.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/random.h"
#include "Soundengine/MIDI/Uart.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/seq_core.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include <string.h>
#include <Soundengine/MIDI/CortexParser.h>
#include "Soundengine/USB/usb_manager.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/DSPAudio/automationNode.h"
//#include "SomGenerator.h"
#include "Soundengine/TriggerOut.h"
#include "Soundengine/FxBus.h"

#include "menu/menu.h"
#include "ledHandler.h"
#include "buttonHandler.h"
#include "Preset/PresetManager.h"

void seq_setPendingPatternActive();



uint8_t seq_rollRate = 1;				//start with roll rate = 1/16
uint8_t seq_rollState = 0;					/**< each bit represents a voice. if bit is set, roll is active*/

static uint16_t seq_tempo = 120;			/**< seq speed in bpm*/

uint8_t seq_activeAutomTrack=0;

uint8_t seq_isSyncExternal = 0;


float seq_shuffle = 0;



static int8_t seq_armedArmedAutomationStep = -1;
static int8_t seq_armedArmedAutomationTrack = -1;

static uint8_t seq_mutedTracks=0;			/**< indicate which tracks are muted */
uint8_t seq_songModeEnabled=0;
uint8_t seq_running = 0;					/**<  1 running, 0 if stopped*/

uint8_t seq_activePattern = 0;				/**< the currently playing pattern*/
uint8_t seq_pendingPattern = 0;				/**< next pattern to play*/



uint8_t seq_selectedStep = 0;

uint8_t seq_recordActive = 0;				/**< set to 1 to activate the reording mode*/

uint8_t seq_eraseActive=0;					/**RECORD will be 1 if live erasing the active voice  */

uint8_t seq_quantisation = QUANT_16;


//uint8_t seq_barCounter;						/**< counts the absolute position in bars since the seq was started */

static uint8_t seq_loadPendigFlag = 0;

// Allow it to be configured whether it keeps track of bar position in the song for
// the purpose of pattern changes
uint8_t seq_resetBarOnPatternChange=0;

//LICHOMAT: the MIDI note of each voice, so that its Note Off matches its Note On also
//when several voices share a MIDI channel (D4, spec 12)
static uint8_t midi_voiceChan[NUM_TRACKS];	/**< channel of the voice's last Note On */
static uint8_t midi_voiceNote[NUM_TRACKS];	/**< note of the voice's last Note On */
static uint8_t midi_voicesOn=0;			/**< voices with a MIDI note on, one bit each */
//the Note Offs of the Note Ons that the core does not schedule (the one-shot roll, the
//echo of an incoming note): one pulse later, sent by seq_tick() (spec 12, review F22)
static seqc_time_t seq_voiceOffAt[NUM_TRACKS];	/**< seqp_now() time of the voice's timed Note Off */
static uint8_t seq_voiceOffPending=0;		/**< voices with a timed Note Off, one bit each */
/** SEQC_NOTE_OFF_TICKS in cycles at 1 BPM: divided by the tempo, one pulse */
#define SEQ_NOTE_OFF_CYCLES_AT_1_BPM ((uint32_t)((uint64_t)SEQC_NOTE_OFF_TICKS * 60u * SEQC_CPU_HZ / SEQC_TICKS_PER_BEAT))

// keep track of which midi notes are playing
static uint8_t midi_chan_notes[16];		    /**< what note is playing on each channel */
static uint16_t midi_notes_on=0;		    /**< which channels have a note currently playing */

#if USE_PATTERN_EDIT_BUFFER
	PatternSet seq_patterns;
#else
	PatternSet seq_patternSet;
#endif

PatternBackupBuffer patternBackupBuffer;

//TempPattern seq_tmpPattern;

//uint8_t seq_newPatternAvailable = 0; 	//indicate that a new pattern has loaded in the background and we should switch
uint8_t seq_songData[NUM_KIT_SLOTS][MAX_SONG_LENGTH];
uint8_t *seq_activeSong;
uint8_t seq_songPositionPointer = 0;	//for song playback, current song position
//uint8_t seq_bankPreloadPendingFlag = NO_PRELOAD_PENDING;

//for the automation tracks each track needs 2 modNodes
static AutomationNode seq_automationNodes[NUM_TRACKS][2];

static void seq_sendMidi(MidiMsg msg);
static void seq_sendRealtime(const uint8_t status);
static void seq_sendProgChg(const uint8_t ptn);
//static void seq_eraseStepAndSubSteps(const uint8_t voice, const uint8_t mainStep);
//static void seq_activateTmpPattern();



static uint8_t seq_intIsStepActive(uint8_t voice, uint8_t stepNr, uint8_t patternNr);
static uint8_t seq_intIsMainStepActive(uint8_t voice, uint8_t mainStepNr, uint8_t pattern);
static void seq_resetNote(Step *step);
static void seq_initCore(void);

//------------------------------------------------------------------------------
// LICHOMAT: glue around the timing core seq_core (spec 4.3, N7).
// Only seq_tick() calls the core; the callbacks, which run inside it, read the
// core's state with its queries. Everything else that changes the transport or
// the core's settings (buttons and menu in the main loop, MIDI parsing, interrupt
// handlers) posts a request to the mailbox below, and seq_tick() applies the posts
// first thing. The copies below serve the readers of the core's state outside
// seq_tick().
//------------------------------------------------------------------------------
#define SEQ_MB_STOP     0x01    /**< seqc_stop() */
#define SEQ_MB_REWIND   0x02    /**< seqc_rewind() */
#define SEQ_MB_START    0x04    /**< seqc_start() */

/** one slot per kind of request; a newer post of a kind replaces the older one */
typedef struct
{
	uint8_t transport;              /**< SEQ_MB_* bits, applied as stop, rewind, start */
	uint8_t syncValid;              /**< syncMode holds a post */
	uint8_t syncMode;               /**< seqc_sync_t, as seq_setExtSync() */
	uint8_t ppqValid;               /**< ppq holds a post */
	uint8_t ppq;                    /**< clock in jack PPQ, 4 or 24 */
	uint8_t tempoValid;             /**< tempo holds a post */
	uint16_t tempo;                 /**< master tempo in BPM */
	uint8_t rollMask;               /**< one-shot roll triggers, one bit per voice */
	uint8_t skipMask;               /**< skip-once markers (D6), one bit per track */
	uint8_t skipStep[NUM_TRACKS];   /**< the step of each skip-once marker */
} SeqMailbox;

static SeqMailbox seq_mailbox;
static volatile uint8_t seq_mailboxPosted;          /**< a post waits; seq_tick() reads this without masking */

static volatile uint8_t seq_stepCopy[NUM_TRACKS];   /**< seqc_trackStep(); SEQC_NO_STEP before the first step */
static volatile float seq_phaseCopy[NUM_TRACKS];    /**< LICHOMAT v0.2: seqc_trackPhase() of each track, while recording */
static volatile uint8_t seq_inTick;                 /**< seq_tick() is running */
static volatile uint8_t seq_coreReady;              /**< seq_init() has set up the core */
static uint8_t seq_coreSyncMode = SEQC_SYNC_INTERNAL; /**< the sync mode applied to the core; seq_isSyncExternal changes one seq_tick() earlier */

//------------------------------------------------------------------------------
/** mask interrupts for a mailbox access; any context */
static uint32_t seq_mailboxLock(void)
{
	const uint32_t primask = __get_PRIMASK();
	__disable_irq();
	return primask;
}
//------------------------------------------------------------------------------
static void seq_mailboxUnlock(uint32_t primask)
{
	__set_PRIMASK(primask);
}
//------------------------------------------------------------------------------
/** start, stop or rewind; a stop cancels a start posted before it */
static void seq_postTransport(uint8_t request)
{
	const uint32_t primask = seq_mailboxLock();
	if(request == SEQ_MB_STOP)
		seq_mailbox.transport &= (uint8_t)~SEQ_MB_START;
	seq_mailbox.transport |= request;
	seq_mailboxPosted = 1;
	seq_mailboxUnlock(primask);
}
//------------------------------------------------------------------------------
static void seq_postSyncMode(uint8_t mode)
{
	const uint32_t primask = seq_mailboxLock();
	seq_mailbox.syncMode = mode;
	seq_mailbox.syncValid = 1;
	seq_mailboxPosted = 1;
	seq_mailboxUnlock(primask);
}
//------------------------------------------------------------------------------
static void seq_postTempo(uint16_t bpm)
{
	const uint32_t primask = seq_mailboxLock();
	seq_mailbox.tempo = bpm;
	seq_mailbox.tempoValid = 1;
	seq_mailboxPosted = 1;
	seq_mailboxUnlock(primask);
}
//------------------------------------------------------------------------------
static void seq_postOneShotRoll(uint8_t voice)
{
	const uint32_t primask = seq_mailboxLock();
	seq_mailbox.rollMask |= (uint8_t)(1 << voice);
	seq_mailboxPosted = 1;
	seq_mailboxUnlock(primask);
}
//------------------------------------------------------------------------------
/** D6: the next occurrence of step on track must not play. Inside seq_tick() (the
 * one-shot roll of seq_applyMailbox(), and from E2.4 on MIDI recording) the core
 * is in reach, so the marker goes in at once, before seqc_process() reaches the
 * step; from any other context it is posted. */
static void seq_postSkipOnce(uint8_t track, uint8_t step)
{
	if(seq_inTick)
	{
		seqc_skipOnce(track, step);
		return;
	}
	const uint32_t primask = seq_mailboxLock();
	seq_mailbox.skipMask |= (uint8_t)(1 << track);
	seq_mailbox.skipStep[track] = step;
	seq_mailboxPosted = 1;
	seq_mailboxUnlock(primask);
}
//------------------------------------------------------------------------------
/** The clock in jack's PPQ, posted only when it changes: the globals of every
 * project load send it again, and in analog sync each post restarts the core's
 * clock count and clears its tempo estimate. */
void seq_setClockInPpq(uint8_t ppq)
{
	static uint8_t posted = 0;      //nothing posted yet; the core starts at 4
	const uint32_t primask = seq_mailboxLock();
	if(ppq != posted)
	{
		posted = ppq;
		seq_mailbox.ppq = ppq;
		seq_mailbox.ppqValid = 1;
		seq_mailboxPosted = 1;
	}
	seq_mailboxUnlock(primask);
}
//------------------------------------------------------------------------------
void seq_init()
{
	int i;

	//give active song pointer a proper value
	seq_activeSong = seq_songData[0];

	memset(seq_songData, SONG_END, sizeof(uint8_t) * MAX_SONG_LENGTH * NUM_KIT_SLOTS);
	seq_songData[0][0] = 0;

	for(i=0;i<NUM_TRACKS;i++) {
		autoNode_init(&seq_automationNodes[i][0]);
		autoNode_init(&seq_automationNodes[i][1]);
	}


	for(i=0;i<NUM_PATTERN;i++)
	{
		//seq_patternSet.seq_patternSettings[i].changeBar 	= 0;	//default setting: zero repeats (play once then change)
		//seq_patternSet.seq_patternSettings[i].nextPattern 	= i;	//default setting: repeat same pattern
		seq_clearPattern(i); // will clear all tracks in the pattern
	}

	//the timing core, its time base and the sync input queues
	seq_initCore();
}
//------------------------------------------------------------------------------
/*
static void seq_activateTmpPattern()
{
	memcpy(&seq_patternSet.seq_subStepPattern[seq_activePattern],&seq_tmpPattern.seq_subStepPattern,sizeof(Step)*NUM_TRACKS*NUM_STEPS);
	for(uint8_t i=0; i<NUM_BARS; i++)
	{
		memcpy(&seq_patternSet.seq_mainSteps[i][seq_activePattern],&seq_tmpPattern.seq_mainSteps[i],sizeof(uint16_t)*NUM_TRACKS);
	}
	//memcpy(&seq_patternSet.seq_patternSettings[seq_activePattern],&seq_tmpPattern.seq_patternSettings,sizeof(PatternSetting));
	memcpy(&seq_patternSet.length[seq_activePattern],&seq_tmpPattern.length,sizeof(uint8_t)*NUM_TRACKS);
}
*/
//------------------------------------------------------------------------------

void seq_copyPatternToBackupBuffer(uint8_t patternNr)
{
	memcpy(&patternBackupBuffer.seq_subStepPattern, &seq_patternSet.seq_subStepPattern[patternNr],  sizeof(Step)*NUM_TRACKS*NUM_STEPS);
	memcpy(&patternBackupBuffer.seq_stepTiming, &seq_patternSet.seq_stepTiming[patternNr],  sizeof(uint8_t)*NUM_TRACKS*NUM_STEPS);
	for(uint8_t i=0; i<NUM_BARS; i++)
	{
		memcpy(&patternBackupBuffer.seq_mainSteps[i], &seq_patternSet.seq_mainSteps[i][patternNr],  sizeof(uint16_t)*NUM_TRACKS);
	}
	memcpy(&patternBackupBuffer.length, &seq_patternSet.length[patternNr],  sizeof(uint8_t)*NUM_TRACKS);



	patternBackupBuffer.shuffle = seq_patternSet.shuffle[patternNr];;
	patternBackupBuffer.kitNr = seq_patternSet.kitNr[patternNr];
	memcpy(patternBackupBuffer.groove, seq_patternSet.groove[patternNr], sizeof patternBackupBuffer.groove); //LICHOMAT v0.2: the per-track settings

}
//------------------------------------------------------------------------------
void seq_copyBackupBufferToPattern(uint8_t patternNr)
{
	memcpy(&seq_patternSet.seq_subStepPattern[patternNr], &patternBackupBuffer.seq_subStepPattern,  sizeof(Step)*NUM_TRACKS*NUM_STEPS);
	memcpy(&seq_patternSet.seq_stepTiming[patternNr]    , &patternBackupBuffer.seq_stepTiming,      sizeof(uint8_t)*NUM_TRACKS*NUM_STEPS);
	for(uint8_t i=0; i<NUM_BARS; i++)
	{
		memcpy(&seq_patternSet.seq_mainSteps[i][patternNr], &patternBackupBuffer.seq_mainSteps[i],  sizeof(uint16_t)*NUM_TRACKS);
	}
	memcpy(&seq_patternSet.length[patternNr], &patternBackupBuffer.length,  sizeof(uint8_t)*NUM_TRACKS);

	seq_patternSet.shuffle[patternNr] = patternBackupBuffer.shuffle;
	seq_patternSet.kitNr[patternNr] = patternBackupBuffer.kitNr;
	memcpy(seq_patternSet.groove[patternNr], patternBackupBuffer.groove, sizeof patternBackupBuffer.groove); //LICHOMAT v0.2: the per-track settings
}
/*
//------------------------------------------------------------------------------
void seq_copyEditBufferToRam(uint8_t patternNr)
{
	memcpy(&seq_patternSet.seq_subStepPattern[patternNr],&seq_tmpPattern.seq_subStepPattern, sizeof(Step)*NUM_TRACKS*NUM_STEPS);
	for(uint8_t i=0; i<NUM_BARS; i++)
	{
		memcpy(&seq_patternSet.seq_mainSteps[i][patternNr],&seq_tmpPattern.seq_mainSteps[i],sizeof(uint16_t)*NUM_TRACKS);
	}
	//memcpy(&seq_patternSet.seq_patternSettings[seq_activePattern],&seq_tmpPattern.seq_patternSettings,sizeof(PatternSetting));
	memcpy(&seq_patternSet.length[patternNr],&seq_tmpPattern.length,sizeof(uint8_t)*NUM_TRACKS);
}
*/
//------------------------------------------------------------------------------
void seq_setShuffle(float shuffle)
{
	seq_shuffle = shuffle;
}
//------------------------------------------------------------------------------
void seq_setTrackLength(uint8_t trackNr, uint8_t length)
{
	//set new end marker
	/*
	if(length == 16)
		length=0;
	*/
	// **PATROT this was changed from setting length on seq_activePattern to shown (or edited) pattern

#if USE_PATTERN_EDIT_BUFFER

	patternBackupBuffer.length[trackNr]=length;

#else
	seq_patternSet.length[frontParser_shownPattern][trackNr]=length;
#endif
}

//------------------------------------------------------------------------------
uint8_t seq_getTrackLength(uint8_t trackNr)
{
	// **PATROT this was changed from getting seq_activePattern to shown (or edited) pattern
#if USE_PATTERN_EDIT_BUFFER
	uint8_t r=patternBackupBuffer.length[trackNr];
#else
	uint8_t r=seq_patternSet.length[frontParser_shownPattern][trackNr];
#endif
	/*
	if(r==0)
		return 16;
	*/
	return r;
}
//------------------------------------------------------------------------------
void seq_setBpm(uint16_t bpm)
{
	seq_tempo 	= bpm;
	//the core takes the new master tempo at the next seq_tick()
	seq_postTempo(bpm);
	lfo_recalcSync();
}
//------------------------------------------------------------------------------
uint16_t seq_getBpm()
{
	return seq_tempo;
}
//------------------------------------------------------------------------------
void seq_setNextPattern(const uint8_t patNr)
{
	if(patNr >= MAX_PATTERN) return;
	seq_pendingPattern = patNr;
	seq_loadPendigFlag = 1;

	if(!seq_running)
	{
		//immediately change pattern, and restart it from step 0 (fw 1.7: seq_setStepIndexToStart())
		seq_setPendingPatternActive();
		seq_postTransport(SEQ_MB_REWIND);
	}
}
//------------------------------------------------------------------------------
static void seq_sendMidi(MidiMsg msg)
{
	//send to usb midi
	usb_sendMidi(msg);

	//send to hardware midi out jack
	uart_sendMidi(msg);

}


//------------------------------------------------------------------------------
static void seq_parseAutomationNodes(uint8_t track, const Step* stepData)
{
	//set new destination
	autoNode_setDestination(&seq_automationNodes[track][0], stepData->param1Nr);
	autoNode_setDestination(&seq_automationNodes[track][1], stepData->param2Nr);
	//set new mod value
	autoNode_updateValue(&seq_automationNodes[track][0], stepData->param1Val);
	autoNode_updateValue(&seq_automationNodes[track][1], stepData->param2Val);
}
//------------------------------------------------------------------------------
void seq_triggerVoice(uint8_t voiceNr, uint8_t vol, uint8_t note)
{
	uint8_t midiNote; // which midi note to send

	if(voiceNr > 6) return;


	//the p-locks and the MIDI velocity of the track's current step; none before the
	//first step (fw 1.7 read step -1 there)
	const uint8_t step = seq_stepCopy[voiceNr];
	const Step *stepData = (step < NUM_STEPS) ? &seq_patternSet.seq_subStepPattern[seq_activePattern][voiceNr][step] : NULL;
	if(stepData)
		seq_parseAutomationNodes(voiceNr, stepData);

	//turn the trigger off before sending the next one
	/*
	if(voiceNr>=5)
	{
		//hihat channels choke each other
		trigger_triggerVoice(5, TRIGGER_OFF);
		trigger_triggerVoice(6, TRIGGER_OFF);
	} else {
		trigger_triggerVoice(voiceNr, TRIGGER_OFF);
	}
	*/

	// if a note is on for that channel send note-off first
	voiceControl_noteOff(voiceNr);

	//Trigger internal synth voice
	voiceControl_noteOn(voiceNr, note, vol);

	// the note that is played will be whatever is received unless we have a note override set
	// A note override is any non-zero value for this parameter
	//if(midi_NoteOverride[voiceNr] == 0)
	if(activeParameters[PAR_MIDI_NOTE1 + voiceNr] == 0)
		midiNote = note;
	else
		//midiNote = midi_NoteOverride[voiceNr];
		midiNote = activeParameters[PAR_MIDI_NOTE1 + voiceNr];

	//send the new note to midi/usb out: the only Note On (D4), with the current step's volume
	//as in fw 1.7 (N18.3), or the voice's velocity before the first step
	seq_sendVoiceNoteOn(voiceNr, midiNote, stepData ? (stepData->volume&STEP_VOLUME_MASK) : vol);
	//the core does not schedule this note: its Note Off one pulse later, as a core event's
	seq_sendVoiceNoteOffLater(voiceNr);
}
//------------------------------------------------------------------------------
//has to be called AFTER the last pattern switch is finished, so the playing pattern is properly put into the temp buffer
#if 0
static void seq_CheckIfNextPatternNeedsBankSwitch()
{
	if(seq_songPositionPointer+1 < MAX_SONG_LENGTH)
	{
		//determine next bank to load
		const uint8_t nextPattern = seq_activeSong[seq_songPositionPointer+1];
		if(nextPattern==SONG_END || nextPattern == SONG_LOOP) return;
		const uint8_t nextBank = nextPattern/16;

		/*
		const uint8_t lowestPatternInMemory = menu_activePatternBank * 16;
		const uint8_t highestPatternInMemory = lowestPatternInMemory + 15;

		if( (nextPattern > lowestPatternInMemory) && (nextPattern  < highestPatternInMemory) )
		*/
		menu_activePatternBank = nextBank ;
	}
}
#endif
//------------------------------------------------------------------------------
static uint8_t seq_determineNextPattern(uint8_t autoIncrement)
{

	/*
	  extern uint8_t seq_songData[MAX_SONG_LENGTH];
	  extern uint8_t seq_songPositionPointer;
	 */
	if(seq_songModeEnabled)
	{
		if(autoIncrement)
		{
			seq_songPositionPointer++;
			//TODO: this will loop when all 64 pattern slots are used... maybe a stop would be better?
			if(seq_songPositionPointer == MAX_SONG_LENGTH) seq_songPositionPointer = 0;
			return seq_activeSong[seq_songPositionPointer];
		}
		else
		{
			uint8_t pos = seq_songPositionPointer +1;
			if(pos == MAX_SONG_LENGTH) pos = 0;
			return seq_activeSong[pos];
		}
	}
	else
		return seq_activePattern;

	/*
	const PatternSetting * const p=&seq_patternSet.seq_patternSettings[seq_activePattern];
	if(seq_barCounter % (p->changeBar+1) == 0)
		return p->nextPattern;
	else
		*/

}

void seq_setPendingPatternActive()
{
	if((seq_activePattern != seq_pendingPattern) || seq_loadPendigFlag)
	{
		// if this setting is active and the user has manually changed patterns,
		// reset the bar counter. uncommenting the below will cause it to only reset
		// when a manual pattern change is invoked. to me the whole auto pattern change modulo stuff
		// above is a bit broken.

		/*
		if( seq_resetBarOnPatternChange)
			seq_barCounter=0;
			*/

		// TODO we need to also reset barCounter to 0 when the end of a repetition set of a pattern plays
		// EVEN IF the pattern is set to play itself again, this will facilitate having the bits that play
		// certain steps only on certain intervals of bar counter

		seq_loadPendigFlag = 0;

		/*//background loading removed
		//first check if 2 new pattern is available
		if(seq_newPatternAvailable)
		{
			seq_newPatternAvailable = 0;
			seq_activateTmpPattern();
		}
		*/
		//copy current backup to current slot (discarding unsaved changes)
		seq_copyBackupBufferToPattern(seq_activePattern);
		//copy pending pattern to backup buffer
		seq_copyPatternToBackupBuffer(seq_pendingPattern);

		seq_activePattern = seq_pendingPattern;

		activeParameters[PAR_SHUFFLE] = seq_patternSet.shuffle[seq_activePattern];
		seq_setShuffle(activeParameters[PAR_SHUFFLE]/127.f);

		//A new pattern is activated here -> Load corresponding drumkit
		if(activeParameters[PAR_PATTERN_MODE] == SKIP_NEXT_KIT_AUTOLOAD)
		{
			activeParameters[PAR_PATTERN_MODE] = 1;
		}
		else if(activeParameters[PAR_PATTERN_MODE] == 1)
		{
			//turn FX off to save cycles
			fxBus.fxType = FX_OFF;
			preset_loadDrumsetRam(seq_patternSet.kitNr[seq_activePattern], 0, 1);
			menu_selectedPresetNr[SAVE_TYPE_KIT] = seq_patternSet.kitNr[seq_activePattern];
			menu_loadedPresetNr[CURRENT_KIT] = seq_patternSet.kitNr[seq_activePattern];
			preset_touchTimer(PRESET_KIT_TIMER);
		}

		//use shuffle value from pattern
		//activeParameters[PAR_SHUFFLE] = seq_patternSet.shuffle[seq_activePattern];
		//seq_setShuffle(activeParameters[PAR_SHUFFLE]/127.f);

		//send the ack message to tell the front that a new pattern starts playing
		/*
		uart_sendFrontpanelByte(FRONT_SEQ_CC);
		uart_sendFrontpanelByte(FRONT_SEQ_CHANGE_PAT);
		uart_sendFrontpanelByte(seq_activePattern);
		*/

		//if(frontParser_midiMsg.data2 > 7) return;
		//ack message that the sequencer changed to the requested pattern

		//stop blinking pattern led
		led_setBlinkLed((uint8_t)(led_idx2StepLed(seq_activePattern)),0);
		//clear last pattern led



		menu_playedPattern = seq_activePattern;

		/*
		//step display follows pattern changes
		//if(parameter_values[PAR_FOLLOW] & FOLLOW_PATTERN ) {
		//Playing pattern is always the shown pattern. Makes more sense!
		if(1) {

			//update for song screen to follow patterns
			menu_songMenuPosition = seq_songPositionPointer;

			if( menu_activePage < PATTERN_LOAD_PAGE || menu_activePage > SONG_MODE_PAGE)
			{

				menu_setShownPattern(seq_activePattern);
				led_clearSequencerLeds();
				//query current sequencer step states and light up the corresponding leds
				const uint8_t trackNr = menu_getActiveVoice(); //max 6 => 0x6 = 0b110
				//uint8_t patternNr = menu_getViewedPattern(); //max 7 => 0x07 = 0b111
				//uint8_t value = (uint8_t)((trackNr<<4) | (patternNr&0x7));


				//frontPanel_sendData(LED_CC,LED_QUERY_SEQ_TRACK,value); muss gemacht werden
				//frontParser_updateTrackLeds(trackNr, patternNr);
				led_updateSeqLeds();
				activeParameters[PAR_TRACK_LENGTH] = seq_getTrackLength(trackNr);
				//parameter_values[PAR_TRACK_ROTATION] = seq_getTrackRotation(trackNr);

				//frontPanel_sendData(SEQ_CC,SEQ_REQUEST_PATTERN_PARAMS,frontParser_midiMsg.data2); muss auch gem,acht werdne!
				//parameter_values[PAR_PATTERN_BEAT] = seq_patternSet.seq_patternSettings[frontParser_shownPattern].changeBar;
				//parameter_values[PAR_STEP_SLIDE] = seq_patternSet.seq_patternSettings[frontParser_shownPattern].nextPattern;
				menu_repaint();


			} else {
				//store the pending pattern update for shift button release handler
				menu_shownPattern = seq_activePattern;


			}
		}




		//if( (buttonHandler_getMode() == SELECT_MODE_PERF) || (buttonHandler_getMode() == SELECT_MODE_PAT_GEN) )
		if( (buttonHandler_getMode() == SELECT_MODE_PATTERN))
		{
			//only show pattern changes when in select pattern  mode

			led_clearSequencerLeds();
			//led_clearSelectLeds();
			led_clearAllBlinkLeds();
			// re init the LEDs showing active/viewed pattern
			led_initPatternSelectLeds();
			//led_setValue(1,LED_PART_SELECT1+frontParser_midiMsg.data2);
			menu_repaint();

		}
		else if(( (buttonHandler_getMode() == SELECT_MODE_SONG)))
		{
			//update song mode screen
			menu_repaintAll();
		}
		*/


		// send a pattern change message to midi/usb out
		seq_sendProgChg(seq_activePattern);


		// all notes off here since we are switching patterns
		voiceControl_noteOff(0xFF);

		//song mode - check for preload pattern bank
		/*
		if(seq_songModeEnabled)
		{
			seq_CheckIfNextPatternNeedsBankSwitch();
		}
		*/

	}
	else if(activeParameters[PAR_PATTERN_MODE] == SKIP_NEXT_KIT_AUTOLOAD)
	{
		activeParameters[PAR_PATTERN_MODE] = 1;
	}
	//moved song list display here to prevent skipping list on same consecutive patterns
	if(1) {

				//update for song screen to follow patterns. Before entry 0 (0xff after a
				//rewind or song mode on) the cursor goes to entry 0, where the first pattern
				//end moves the pointer; the song page writes at the cursor (fw 1.7 wrote
				//song[255], E4.6)
				menu_songMenuPosition = (seq_songPositionPointer < MAX_SONG_LENGTH) ? seq_songPositionPointer : 0;

				if( menu_activePage < PATTERN_LOAD_PAGE || menu_activePage > SONG_MODE_PAGE)
				{

					menu_setShownPattern(seq_activePattern);
					led_clearSequencerLeds();
					//query current sequencer step states and light up the corresponding leds
					const uint8_t trackNr = menu_getActiveVoice(); //max 6 => 0x6 = 0b110
					//uint8_t patternNr = menu_getViewedPattern(); //max 7 => 0x07 = 0b111
					//uint8_t value = (uint8_t)((trackNr<<4) | (patternNr&0x7));


					//frontPanel_sendData(LED_CC,LED_QUERY_SEQ_TRACK,value); muss gemacht werden
					//frontParser_updateTrackLeds(trackNr, patternNr);
					led_updateSeqLeds();
					activeParameters[PAR_TRACK_LENGTH] = seq_getTrackLength(trackNr);
					//parameter_values[PAR_TRACK_ROTATION] = seq_getTrackRotation(trackNr);

					//frontPanel_sendData(SEQ_CC,SEQ_REQUEST_PATTERN_PARAMS,frontParser_midiMsg.data2); muss auch gem,acht werdne!
					//parameter_values[PAR_PATTERN_BEAT] = seq_patternSet.seq_patternSettings[frontParser_shownPattern].changeBar;
					//parameter_values[PAR_STEP_SLIDE] = seq_patternSet.seq_patternSettings[frontParser_shownPattern].nextPattern;
					menu_repaint();


				} else {
					//store the pending pattern update for shift button release handler
					menu_shownPattern = seq_activePattern;


				}
			}




			//if( (buttonHandler_getMode() == SELECT_MODE_PERF) || (buttonHandler_getMode() == SELECT_MODE_PAT_GEN) )
			if( (buttonHandler_getMode() == SELECT_MODE_PATTERN))
			{
				//only show pattern changes when in select pattern  mode

				led_clearSequencerLeds();
				//led_clearSelectLeds();
				led_clearAllBlinkLeds();
				// re init the LEDs showing active/viewed pattern
				led_initPatternSelectLeds();
				//led_setValue(1,LED_PART_SELECT1+frontParser_midiMsg.data2);
				menu_repaint();

			}
			else if(( (buttonHandler_getMode() == SELECT_MODE_SONG)))
			{
				//update song mode screen
				menu_repaintAll();
			}

}
//------------------------------------------------------------------------------
uint8_t seq_getExtSync()
{
	return seq_isSyncExternal;
}
//------------------------------------------------------------------------------
void seq_setExtSync(uint8_t isExt)
{
	//the interrupt handlers and the MIDI parser gate with the new mode at once;
	//the core switches at the next seq_tick()
	seq_isSyncExternal = isExt;
	seq_postSyncMode(isExt);
}
//------------------------------------------------------------------------------
void seq_armAutomationStep(uint8_t stepNr, uint8_t track,uint8_t isArmed)
{
	//a step or track out of range disarms: seq_recordAutomation() writes the armed
	//step, and fw 1.7 armed steps up to 127 from SHIFT + a select button (E4.6)
	if(isArmed && (stepNr < NUM_STEPS) && (track < NUM_TRACKS)) {
		seq_armedArmedAutomationStep 	= stepNr;
		seq_armedArmedAutomationTrack 	= track;
	} else {
		seq_armedArmedAutomationStep 	= -1;
		seq_armedArmedAutomationTrack 	= -1;
	}
}

//------------------------------------------------------------------------------
//returns how many % [0:100) of the track's current step have elapsed, for the MIDI
//record quantisation: from 50% on, a note goes to the next step (spec 6.7).
//It is seqc_trackPhase() x 100 (LICHOMAT v0.2: of the track's own step), which
//reaches 50 halfway between the swung grid times of this step and the next one.
//0 while stopped, before the first step and for trackNr >= NUM_TRACKS.
//Valid only while recording (seq_recordActive): seq_tick() refreshes the copy only
//then, so with recording off, and until the first seq_tick() after REC is switched
//on, it holds the phase of the last tick that ran while recording.
float seq_getElapsedStepTimePercent(uint8_t trackNr)
{
	return (trackNr < NUM_TRACKS ? seq_phaseCopy[trackNr] : 0.f) * 100.f;
}
//------------------------------------------------------------------------------
void seq_setQuantisation(uint8_t value)
{
	seq_quantisation = value;
}
//------------------------------------------------------------------------------
void seq_toggleStep(uint8_t voice, uint8_t stepNr, uint8_t patternNr)
{
	if((seq_patternSet.seq_subStepPattern[patternNr][voice][stepNr].volume&STEP_ACTIVE_MASK)==0)
	{
		seq_patternSet.seq_subStepPattern[patternNr][voice][stepNr].volume |= STEP_ACTIVE_MASK;
	} else {
		seq_patternSet.seq_subStepPattern[patternNr][voice][stepNr].volume &= ~STEP_ACTIVE_MASK;
	}
}
//------------------------------------------------------------------------------
void seq_toggleMainStep(uint8_t voice, uint8_t stepNr, uint8_t patternNr)
{
	const uint8_t bar 	= stepNr / 16;
	const uint8_t shift = stepNr - bar*16;
	seq_patternSet.seq_mainSteps[bar][patternNr][voice] ^= (1<<shift);
}
//------------------------------------------------------------------------------
void seq_setMainStep(uint8_t patternNr, uint8_t voice, uint8_t stepNr, uint8_t onOff)
{
	const uint8_t bar 	= stepNr / 16;
	const uint8_t shift = stepNr - bar*16;

	if(onOff)
	{
		seq_patternSet.seq_mainSteps[bar][patternNr][voice] |= (1<<shift);
	}
	else
	{
		seq_patternSet.seq_mainSteps[bar][patternNr][voice] &= ~(1<<shift);
	}
}
//------------------------------------------------------------------------------
// this appears unused
//void seq_setStep(uint8_t voice, uint8_t stepNr, uint8_t onOff)
//{
//	if(onOff)
//	{
//		seq_patternSet.seq_subStepPattern[seq_activePattern][voice][stepNr].volume |= STEP_ACTIVE_MASK;
//	}
//	else
//	{
//		seq_patternSet.seq_subStepPattern[seq_activePattern][voice][stepNr].volume &= ~STEP_ACTIVE_MASK;
//	}
//}
//------------------------------------------------------------------------------
uint8_t seq_isRunning() {
	return seq_running;
}
//------------------------------------------------------------------------------
void seq_resetPosition()
{
	//back to the start at the next seq_tick(); the core reports it with the REWIND
	//transport, and seq_cbTransport() does the fw 1.7 side effects
	seq_postTransport(SEQ_MB_REWIND);
}
//------------------------------------------------------------------------------
void seq_setRunning(uint8_t isRunning)
{
	//N7: seq_isRunning() follows the request at once, armed counts as running.
	//The core starts or stops at the next seq_tick(); the side effects (MIDI Start
	//and Stop, clock out reset, save timers) are in seq_cbTransport().
	seq_running = isRunning ? 1 : 0;
	seq_postTransport(isRunning ? SEQ_MB_START : SEQ_MB_STOP);
}
//------------------------------------------------------------------------------
void seq_setSongModeEnabled(uint8_t onOff)
{
	seq_songModeEnabled = onOff;
	if(onOff)
	{
		seq_songPositionPointer = -1; //because nextStep() will first increase it
	}
}
//------------------------------------------------------------------------------
void seq_addPatternToSong(uint8_t patternNr)
{
	seq_activeSong[menu_songMenuPosition] = patternNr;
	if(menu_songMenuPosition < (MAX_SONG_LENGTH-1))
	{
		menu_songMenuPosition++;
	}
}
//------------------------------------------------------------------------------
void seq_deleteRow()
{
	 //seq_songData[NUM_KIT_SLOTS][MAX_SONG_LENGTH];
	for(int i=menu_songMenuPosition; i < MAX_SONG_LENGTH-1 ;i++)
	{
		seq_activeSong[i] = seq_activeSong[i+1];
	}

}
//------------------------------------------------------------------------------
void seq_insertRow()
{
	//for(int i=menu_songMenuPosition; i < MAX_SONG_LENGTH-1 ;i++)
	for(int i=MAX_SONG_LENGTH-1; i > menu_songMenuPosition ;i--)
	{
		seq_activeSong[i] = seq_activeSong[i-1];
	}
}
//------------------------------------------------------------------------------
static uint8_t seq_intIsStepActive(uint8_t voice, uint8_t stepNr, uint8_t patternNr)
{
	//return ((seq_patternSet.seq_subStepPattern[patternNr][voice][stepNr].volume & STEP_ACTIVE_MASK) > 0);

	//TODO since we don't have substeps anymore, we can probably ditch the whole active flag/mask thing
	return 1;
}
// above will be inlined, below is for ext linkage
uint8_t seq_isStepActive(uint8_t voice, uint8_t stepNr, uint8_t patternNr)
{
	return seq_intIsStepActive(voice,stepNr,patternNr);
}

//------------------------------------------------------------------------------
static uint8_t seq_intIsMainStepActive(uint8_t voice, uint8_t mainStepNr, uint8_t pattern)
{
	const uint8_t bar = mainStepNr / 16;
	const uint8_t shift = mainStepNr - bar*16;
	return (seq_patternSet.seq_mainSteps[bar][pattern][voice] & (1<<shift)) > 0;
}
// above is inlined below for ext linkage
uint8_t seq_isMainStepActive(uint8_t voice, uint8_t mainStepNr, uint8_t pattern)
{
	return seq_intIsMainStepActive(voice, mainStepNr, pattern);
}

//------------------------------------------------------------------------------
void seq_setMute(uint8_t trackNr, uint8_t isMuted)
{
	if(trackNr==7)
	{
		//unmute all
		seq_mutedTracks = 0;
	} else {
		//mute/unmute tracks
		if(isMuted) {
			//mute track
			seq_mutedTracks |= (1<<trackNr);
			// turn off the midi note that may be playing on that track
			// (fw 1.7 passed the MIDI channel here, which turned off another voice)
			voiceControl_noteOff(trackNr);
		} else {
			//unmute track
			seq_mutedTracks &= ~(1<<trackNr);
		}
	}
};
//------------------------------------------------------------------------------
uint8_t seq_isTrackMuted(uint8_t trackNr)
{
	if(seq_mutedTracks & (1<<trackNr) )
	{
		return 1;
	}
	return 0;
}
//------------------------------------------------------------------------------
// LICHOMAT v0.3: mute scenes (plan D17-D20). The panel stores and recalls; the sequencer applies
// a recall at its master boundary and counts the applied scenes for the panel's LEDs
uint8_t seq_muteScenes[MUTE_SCENES];
static uint8_t seq_sceneWanted;			/**< bit 7: a recall waits; bits 0..6: its mutes */
static uint8_t seq_sceneQuant;			/**< 0: at the bar, 1: at the step (PAR_SCENE_QUANT) */
static volatile uint8_t seq_sceneSerial;	/**< counts the applied scenes */
static volatile uint8_t seq_sceneMutes;		/**< the mutes of the latest */

void seq_clearMuteScenes(void)
{
	memset(seq_muteScenes, 0, sizeof seq_muteScenes);
	seq_sceneWanted = 0;
}

void seq_storeMuteScene(uint8_t scene)
{
	if(scene >= MUTE_SCENES) return;
	seq_muteScenes[scene] = (uint8_t)(MUTE_SCENE_STORED | (seq_mutedTracks & MUTE_SCENE_TRACKS));
}

/** the mutes in one store, the notes of newly muted tracks off (as seq_setMute()) */
static void seq_applyMutes(uint8_t mutes)
{
	const uint8_t newly = (uint8_t)(mutes & ~seq_mutedTracks);
	seq_mutedTracks = mutes;
	for(uint8_t t = 0; t < NUM_TRACKS; t++)
		if(newly & (1u << t))
			voiceControl_noteOff(t);
	seq_sceneMutes = mutes;
	seq_sceneSerial = (uint8_t)(seq_sceneSerial + 1u);
}

uint8_t seq_recallMuteSceneAt(uint8_t scene, uint8_t running)
{
	if((scene >= MUTE_SCENES) || !(seq_muteScenes[scene] & MUTE_SCENE_STORED)) return 0;
	const uint8_t mutes = (uint8_t)(seq_muteScenes[scene] & MUTE_SCENE_TRACKS);
	if(running)
		seq_sceneWanted = (uint8_t)(MUTE_SCENE_STORED | mutes);
	else
	{
		seq_sceneWanted = 0;
		seq_applyMutes(mutes);
	}
	return 1;
}

uint8_t seq_recallMuteScene(uint8_t scene)
{
	return seq_recallMuteSceneAt(scene, seq_isRunning());
}

/** at each master boundary (seq_cbStepBoundary()) with seqc_barStep() */
void seq_muteSceneBoundary(uint8_t barStep)
{
	if(!(seq_sceneWanted & MUTE_SCENE_STORED)) return;
	if(!seq_sceneQuant && (barStep % 16u) != 0) return;
	const uint8_t mutes = (uint8_t)(seq_sceneWanted & MUTE_SCENE_TRACKS);
	seq_sceneWanted = 0;
	seq_applyMutes(mutes);
}

/** the count of applied scenes and, in mutes, the latest's mutes; the panel compares counts */
uint8_t seq_appliedMuteScene(uint8_t *mutes)
{
	const uint8_t serial = seq_sceneSerial;
	*mutes = seq_sceneMutes;
	return serial;
}

void seq_setMuteSceneQuant(uint8_t step)
{
	seq_sceneQuant = step ? 1 : 0;
}
//------------------------------------------------------------------------------
// given a pattern and a track:
// this sends the main step info (which main steps are on/off) in addition
// to the length of the track
/*
void seq_sendMainStepInfoToFront(uint16_t stepNr)
{
	//the absolute number of patterns
	const uint8_t currentPattern	= stepNr / 7;
	const uint8_t currentTrack  	= stepNr - currentPattern*7;

	uint16_t dataToSend = seq_patternSet.seq_mainSteps[currentPattern][currentTrack];

	uart_sendFrontpanelSysExByte(  dataToSend	  & 0x7f); //1st 7 bit
	uart_sendFrontpanelSysExByte( (dataToSend>>7) & 0x7f); //2nd 7 bit
	uart_sendFrontpanelSysExByte( (dataToSend>>14)& 0x7f); //last 2 bit

	// send the track length
	uart_sendFrontpanelSysExByte( seq_patternSet.seq_patternLengthRotate[currentPattern][currentTrack].length);

}
*/
//--------------------------------------------------------------------
/** this one is more complicated than the 14 bit upper/lower nibble when transmitting the requested step number via SysEx.
 * because we have to transmit seven 8-bit values that make up the StepStruct we have to pack the data clever into the 7-bit
 * SysEx packets.
 *
 * first we send the lower 7-bit of all 7 values
 * then we transmit an additional 7-bit value containing all the MSBs from the previous seven values.
 *
 * 1st - (value1 & 0x7f)
 * 2nd - (value2 & 0x7f)
 * 3rd - (value3 & 0x7f)
 * 4th - (value4 & 0x7f)
 * 5th - (value5 & 0x7f)
 * 6th - (value6 & 0x7f)
 * 7th - (value7 & 0x7f)
 *
 * 8th - (0 MSB7 MSB6 MSB5 MSB4 MSB3 MSB2 MSB1)
 */
void seq_sendStepInfoToFront(uint16_t stepNr)
{

	//decode the step number
	//the step number is between 0 and NUMBER_STEPS*NUM_TRACKS*NUM_PATTERN = 64*7*8 = 7168 steps
	// stepNr / 64 = absolute number of patterns (each track has 8 patterns * 7 tracks)
	// numPatterns / 8 = the current track

	//the absolute number of patterns
	const uint8_t absPat 			= stepNr/NUM_STEPS;
	const uint8_t currentTrack 		= absPat / 8;
	const uint8_t currentPattern 	= absPat - currentTrack*8;
	const uint8_t currentStep		= stepNr - absPat*NUM_STEPS;

	//encode the data and send it back
	Step *dataToSend = &seq_patternSet.seq_subStepPattern[currentPattern][currentTrack][currentStep];

	uart_sendFrontpanelSysExByte(dataToSend->volume	& 0x7f);
	uart_sendFrontpanelSysExByte(dataToSend->prob	& 0x7f);
	uart_sendFrontpanelSysExByte(dataToSend->note	& 0x7f);

	uart_sendFrontpanelSysExByte(dataToSend->param1Nr	& 0x7f);
	uart_sendFrontpanelSysExByte(dataToSend->param1Val	& 0x7f);

	uart_sendFrontpanelSysExByte(dataToSend->param2Nr	& 0x7f);
	uart_sendFrontpanelSysExByte(dataToSend->param2Val	& 0x7f);

	//now the MSBs from all 7 values
	uart_sendFrontpanelSysExByte( 	(((dataToSend->volume 	& 0x80)>>7) |
									((dataToSend->prob	 	& 0x80)>>6) |
									((dataToSend->note	 	& 0x80)>>5) |
									((dataToSend->param1Nr	& 0x80)>>4) |
									((dataToSend->param1Val	& 0x80)>>3) |
									((dataToSend->param2Nr	& 0x80)>>2) |
									((dataToSend->param2Val	& 0x80)>>1))&0x7f
									);
}
//-------------------------------------------------------------------------------
void seq_setRoll(uint8_t voice, uint8_t onOff)
{
	if(voice >= 7) return;

	if(onOff) {
		seq_rollState |= (1<<voice);
		//if seq is stopped, always use oneshot mode
		if((seq_rollRate == 0xff) || (seq_isRunning() == 0) ) {
			//trigger one shot and record it, in seq_tick() (seq_applyMailbox())
			seq_postOneShotRoll(voice);
		}
	} else {
		seq_rollState &= ~(1<<voice);
	}
};
//--------------------------------------------------------------------------------
//LICHOMAT v0.3: the performance page's roll rates in the order of rollRateNames (MenuText.h),
//as seq_core's SEQC_ROLL(span in track steps, hits); 0xFF is the one-shot roll (plan D21, D22)
static const uint8_t seq_rollRates[] = {
	0xFF,			// One
	SEQC_ROLL(16, 1),	// 1/1
	SEQC_ROLL(8, 1),	// 1/2
	SEQC_ROLL(16, 3),	// 1/3: half-note triplets
	SEQC_ROLL(4, 1),	// 1/4
	SEQC_ROLL(8, 3),	// 1/6: quarter triplets
	SEQC_ROLL(2, 1),	// 1/8
	SEQC_ROLL(4, 3),	// 1/12: eighth triplets
	SEQC_ROLL(1, 1),	// 1/16
	SEQC_ROLL(2, 3),	// 1/24: sixteenth triplets
	SEQC_ROLL(1, 2),	// 1/32
};
void seq_setRollRate(uint8_t rate)
{
	seq_rollRate = (rate < sizeof seq_rollRates) ? seq_rollRates[rate] : SEQC_ROLL(1, 1);
}
//------------------------------------------------------------------------------
/** LICHOMAT v0.3: fill mode for the trig conditions FILL and !FILL, while step 8 is held in the
 * performance mode (plan D16); it applies to the steps generated from now, one step ahead */
void seq_setFill(uint8_t on)
{
	seqc_setFill(on != 0);
}
//------------------------------------------------------------------------
/** quantize a step to the seq_quantisation value*/
/*
#define QUANT(x) (NUM_STEPS/x)
static int8_t seq_quantize(int8_t step)
{
	uint8_t quantisationMultiplier=1;
	switch(seq_quantisation)
	{
	case QUANT_8:
		quantisationMultiplier = QUANT(8);
		break;

	case QUANT_16:
		quantisationMultiplier = QUANT(16);
		break;

	case QUANT_32:
		quantisationMultiplier = QUANT(32);
		break;

	case QUANT_64:
		quantisationMultiplier = QUANT(64);
		break;

	case NO_QUANTISATION:
	default:
		return step;
		break;
	}

	//now calc the quantisation
	float frac = step/(float)quantisationMultiplier;
	int8_t itg = (int8_t)frac;
	frac = frac - itg;

	if(frac>=0.5f)
	{
		return ((itg + 1)*quantisationMultiplier)&0x7f;
	}
	return itg*quantisationMultiplier;
}
*/

//------------------------------------------------------------------------
uint8_t seq_isAutomationStepArmed()
{
	if(seq_armedArmedAutomationStep	!= -1) return 1;
	return 0;
}
//------------------------------------------------------------------------
void seq_recordAutomation(uint8_t voice, uint8_t dest, uint8_t value)
{
	//one read of the copy: a seq_tick() between two reads could make it 255
	const uint8_t step = (voice < NUM_TRACKS) ? seq_stepCopy[voice] : SEQC_NO_STEP;
	if(seq_recordActive && (step < NUM_STEPS))
	{

		//only record to active steps
		/*

		if( seq_intIsMainStepActive(voice,quantizedStep/8,seq_activePattern) &&
				seq_intIsStepActive(voice,quantizedStep,seq_activePattern))
			*/
		if( seq_intIsMainStepActive(voice,step,seq_activePattern) )
		{
      if(seq_activeAutomTrack == 0) {
        seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step].param1Nr = dest;
        seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step].param1Val = value;
      } else {
        seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step].param2Nr = dest;
        seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step].param2Val = value;
      }
      
      // seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step].paramNr[seq_activeAutomTrack] = dest;
      // seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step].paramVal[seq_activeAutomTrack] = value;
      
		}
	}

	if( (seq_armedArmedAutomationStep	!= -1) && (seq_armedArmedAutomationTrack != -1) )
	{
		//step button is held down
		//-> set step automation parameters
		if(seq_activeAutomTrack == 0) {
			seq_patternSet.seq_subStepPattern[seq_activePattern]
			                                  [seq_armedArmedAutomationTrack]
			                                   [seq_armedArmedAutomationStep].param1Nr = dest;
			seq_patternSet.seq_subStepPattern[seq_activePattern]
			                                  [seq_armedArmedAutomationTrack]
			                                   [seq_armedArmedAutomationStep].param1Val = value;
		} else {
			seq_patternSet.seq_subStepPattern[seq_activePattern]
			                                  [seq_armedArmedAutomationTrack]
			                                   [seq_armedArmedAutomationStep].param2Nr = dest;
			seq_patternSet.seq_subStepPattern[seq_activePattern]
			                                  [seq_armedArmedAutomationTrack]
			                                   [seq_armedArmedAutomationStep].param2Val = value;
		}
	}
}

PatternSet* get_seq_patternSet_p(void)
{
  return &seq_patternSet;
}

//------------------------------------------------------------------------
void seq_addNote(uint8_t trackNr,uint8_t vel, uint8_t note)
{
	//only record notes when seq is running and recording
	if(!(seq_running && seq_recordActive) || (trackNr >= NUM_TRACKS))
		return;

	//nothing is recorded before the first step (fw 1.7 wrote step 255)
	const uint8_t current = seq_stepCopy[trackNr];
	if(current >= NUM_STEPS)
		return;

	//the effective track length: raw 0 means 16 (fw 1.7 divided by the raw length)
	uint8_t len = seq_patternSet.length[seq_activePattern][trackNr];
	if(len == 0)
		len = 16;
	else if(len > NUM_STEPS)
		len = NUM_STEPS;

	//from half the step on, the note belongs to the next step, and the sequencer
	//must not play that step again when it gets there (D6)
	uint8_t step = current;
	if(seq_getElapsedStepTimePercent(trackNr) >= 50.f)
	{
		step = (uint8_t)((current + 1) % len);
		seq_postSkipOnce(trackNr, step);
	}

	seq_recordNoteAtStep(seq_activePattern, trackNr, step, vel, note);
}
//------------------------------------------------------------------------
void seq_recordNoteAtStep(uint8_t targetPattern, uint8_t trackNr, uint8_t step, uint8_t vel, uint8_t note)
{
	Step *stepPtr;
	if((targetPattern < NUM_PATTERN) && (trackNr < NUM_TRACKS) && (step < NUM_STEPS))
	{
		//set the current step in the requested track active
		stepPtr=&seq_patternSet.seq_subStepPattern[targetPattern][trackNr][step];
		stepPtr->note 		= note;				// note (was SEQ_DEFAULT_NOTE)
		stepPtr->volume		= vel;				// new velocity
		stepPtr->prob		= 100;				// 100% probability
		stepPtr->volume 	|= STEP_ACTIVE_MASK;

		//activate corresponding main step
		seq_setMainStep(targetPattern, trackNr, step,1);

		if( (frontParser_shownPattern == targetPattern) && ( frontParser_activeTrack == trackNr) && (menu_activeBar == (step/16)) )
		{

			if(menu_activePage != PERF_PAGE) //do not show active steps on perf. page
			{
				//limit to 16 steps
				uint8_t stepNr = (uint8_t)(step%16);
				uint8_t led = 0;
				switch(stepNr)
				{
					case 0:
						led = LED_STEP1;
						break;
					case 1:
						led = LED_STEP2;
						break;
					case 2:
						led = LED_STEP3;
						break;
					case 3:
						led = LED_STEP4;
						break;
					case 4:
						led = LED_STEP5;
						break;
					case 5:
						led = LED_STEP6;
						break;
					case 6:
						led = LED_STEP7;
						break;
					case 7:
						led = LED_STEP8;
						break;
					case 8:
						led = LED_STEP9;
						break;
					case 9:
						led = LED_STEP10;
						break;
					case 10:
						led = LED_STEP11;
						break;
					case 11:
						led = LED_STEP12;
						break;
					case 12:
						led = LED_STEP13;
						break;
					case 13:
						led = LED_STEP14;
						break;
					case 14:
						led = LED_STEP15;
						break;
					case 15:
						led = LED_STEP16;
						break;


				}
				led_setValue(1,led);
			}
		}
	}
}

//------------------------------------------------------------------------
// **RECORD erase a main step and all it's sub steps on the active pattern
// for the specified voice
/*
static void seq_eraseStepAndSubSteps(const uint8_t voice, const uint8_t mainStep)
{
	uint8_t i;
	// turn off the main step
	seq_setMainStep(seq_activePattern, voice, mainStep,0);

	// turn off all substeps
	for(i=(uint8_t)(mainStep*8);i<(uint8_t)((mainStep+1)*8);i++) {
		seq_resetNote(&seq_patternSet.seq_subStepPattern[seq_activePattern][voice][i]);
	}

	// first substep needs to be made active
	seq_patternSet.seq_subStepPattern[seq_activePattern][voice][(uint8_t)(mainStep*8)].volume |= STEP_ACTIVE_MASK;

	//if( (frontParser_shownPattern == seq_activePattern) && ( frontParser_activeTrack == voice) )
	//{
		// todo if the pattern is shown on the front, update the leds
		// 		figure out. Right now it clears the LED's after shift is released... which is fine for now.
	//}

}
*/

//------------------------------------------------------------------------
void seq_setRecordingMode(uint8_t active)
{
	seq_recordActive = active;
}

void seq_setErasingMode(uint8_t active)
{
	seq_eraseActive = active;
}

//------------------------------------------------------------------------------
// reset a step to it's default state
static void seq_resetNote(Step *step)
{
	step->note 		= SEQ_DEFAULT_NOTE;
	step->param1Nr 	= NO_AUTOMATION;
	step->param1Val = 0;
	step->param2Nr	= NO_AUTOMATION;
	step->param2Val	= 0;
	step->prob		= 100;
	step->volume	= 127; // clears active bit as well
}
//------------------------------------------------------------------------------
void seq_clearTrack(uint8_t trackNr, uint8_t pattern)
{
	int k;
	for(k=0;k<NUM_STEPS;k++)
	{
		seq_resetNote(&seq_patternSet.seq_subStepPattern[pattern][trackNr][k]);

		seq_patternSet.seq_stepTiming[pattern][trackNr][k].flam = 0;
		seq_patternSet.seq_stepTiming[pattern][trackNr][k].shift = 7;   // 7 = no shift

		//every 1st step in a substep pattern active
		if( (k%8) == 0)
			seq_patternSet.seq_subStepPattern[pattern][trackNr][k].volume |= STEP_ACTIVE_MASK ;
	}

	// all main steps off for this track
	for(uint8_t i=0;i<NUM_BARS;i++)	seq_patternSet.seq_mainSteps[i][pattern][trackNr] = 0;

	//**PATROT all pattern rotations off and all patterns set to length 16
	seq_patternSet.length[pattern][trackNr]=16;
	memset(&seq_patternSet.groove[pattern][trackNr], 0, sizeof(TrackGroove)); //LICHOMAT v0.2: the defaults

}
//------------------------------------------------------------------------------
void seq_clearPattern(uint8_t pattern)
{
	int i;
	for(i=0;i<NUM_TRACKS;i++)
		seq_clearTrack(i, pattern);
}

//------------------------------------------------------------------------------
void seq_clearAutomation(uint8_t trackNr, uint8_t pattern, uint8_t automTrack)
{
	int k;

	if(automTrack==0)
	{
		for(k=0;k<NUM_STEPS;k++)
		{
			seq_patternSet.seq_subStepPattern[pattern][trackNr][k].param1Nr 	= NO_AUTOMATION;
			seq_patternSet.seq_subStepPattern[pattern][trackNr][k].param1Val 	= 0;
		}
	} else {
		for(k=0;k<NUM_STEPS;k++)
		{
			seq_patternSet.seq_subStepPattern[pattern][trackNr][k].param2Nr		= NO_AUTOMATION;
			seq_patternSet.seq_subStepPattern[pattern][trackNr][k].param2Val	= 0;
		}
	}
}
//------------------------------------------------------------------------------
void seq_copyTrack(uint8_t srcNr, uint8_t dstNr, uint8_t pattern)
{
	// todo this fn and the next are very similar. Add a new fn that takes src, dest track and src, dest pat
	// and then have these two fn's call it instead.
	int k;
	Step *src, *dst;
	for(k=0;k<NUM_STEPS;k++)
	{
		dst=&seq_patternSet.seq_subStepPattern[pattern][dstNr][k];
		src=&seq_patternSet.seq_subStepPattern[pattern][srcNr][k];
		dst->note		= src->note;
		dst->param1Nr 	= src->param1Nr;
		dst->param1Val 	= src->param1Val;
		dst->param2Nr	= src->param2Nr;
		dst->param2Val	= src->param2Val;
		dst->prob		= src->prob;
		dst->volume		= src->volume;

		seq_patternSet.seq_stepTiming[pattern][dstNr][k] = seq_patternSet.seq_stepTiming[pattern][srcNr][k];
	}

	// copy which main steps are on/off
	for(uint8_t i=0;i<NUM_BARS;i++)	seq_patternSet.seq_mainSteps[i][pattern][dstNr] = seq_patternSet.seq_mainSteps[i][pattern][srcNr];

	// copy length and rotation offset from source track
	seq_patternSet.length[pattern][dstNr] = seq_patternSet.length[pattern][srcNr];
	seq_patternSet.groove[pattern][dstNr] = seq_patternSet.groove[pattern][srcNr]; //LICHOMAT v0.2: the per-track settings


}
//------------------------------------------------------------------------------
void seq_copyPattern(uint8_t src, uint8_t dst)
{
	int k,j;
	Step *psrc, *pdst;
	for(j=0;j<NUM_TRACKS;j++)
	{
		for(k=0;k<NUM_STEPS;k++)
		{
			pdst=&seq_patternSet.seq_subStepPattern[dst][j][k];
			psrc=&seq_patternSet.seq_subStepPattern[src][j][k];
			pdst->note			= psrc->note;
			pdst->param1Nr 		= psrc->param1Nr;
			pdst->param1Val 	= psrc->param1Val;
			pdst->param2Nr		= psrc->param2Nr;
			pdst->param2Val		= psrc->param2Val;
			pdst->prob			= psrc->prob;
			pdst->volume		= psrc->volume;

			seq_patternSet.seq_stepTiming[dst][j][k] = seq_patternSet.seq_stepTiming[src][j][k];
		}

		for( uint8_t i=0;i<NUM_BARS;i++)
		{
			seq_patternSet.seq_mainSteps[i][dst][j] = seq_patternSet.seq_mainSteps[i][src][j];
		}

		// copy length and rotation offset from source pattern for the track
		seq_patternSet.length[dst][j] = seq_patternSet.length[src][j];
		seq_patternSet.groove[dst][j] = seq_patternSet.groove[src][j]; //LICHOMAT v0.2: the per-track settings

		seq_patternSet.shuffle[dst] = seq_patternSet.shuffle[src];
		seq_patternSet.kitNr[dst] = seq_patternSet.kitNr[src];
	}
}
//------------------------------------------------------------------------------
void seq_setActiveAutomationTrack(uint8_t trackNr)
{
	seq_activeAutomTrack = trackNr;
}

uint8_t seq_getActiveAutomationTrack(void)
{
  return seq_activeAutomTrack;
}
//------------------------------------------------------------------------------

void seq_midiNoteOff(uint8_t chan)
{
	uint8_t i;
	MidiMsg msg;

	// we are not filtering according to tx filter because they might have turned that
	// setting on while a note was sustaining

	msg.bits.length=2;
	msg.data2=0;

	if(chan==0xff) { // all notes off
		for(i=0; i<16; i++)
			if((1<<i) & midi_notes_on) {
				msg.status=	NOTE_OFF | i;
				msg.data1=midi_chan_notes[i];
				seq_sendMidi(msg);
			}
		// reset all
		midi_notes_on=0;
		return;
	}
	// The proper way to do a note off is with 0x80. 0x90 with velocity 0 is also used, however I think there is still
	// synth gear out there that doesn't recognize that properly.
	if((1<<chan) & midi_notes_on) {
		msg.status=	NOTE_OFF | chan;
		msg.data1=midi_chan_notes[chan];
		seq_sendMidi(msg);
		// turn off our knowledge of that note playing
		midi_notes_on &= (~(1<<chan));
	}
}

static void seq_sendRealtime(const uint8_t status)
{
	static MidiMsg msg = {0};
	// FILT filter out realtime msgs if appropriate
	if((midiParser_txRxFilter & (MIDI_FILTER_SYSTEM<<4))==0)
		return;
	msg.status=status;
	seq_sendMidi(msg);

}

/* Send a note on message. This will filter out these messages if appropriate
 */
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc)
{
	static MidiMsg msg = { .bits = { .length = 2 } };  // status + 2 data bytes
	// FILT filter out note msgs if appropriate
	if((midiParser_txRxFilter & (MIDI_FILTER_NOTES<<4))==0)
		return;

	msg.status=NOTE_ON | channel;
	msg.data1=note;
	msg.data2=veloc;
	seq_sendMidi(msg);


	// keep track of which notes are on so we can turn them off later
	midi_chan_notes[channel]=note;
	midi_notes_on |= (1 << channel);

}
//------------------------------------------------------------------------------
/* LICHOMAT: send the MIDI Note On of a voice on its channel and remember the channel
 * and the note. A note of the voice that is still on gets its Note Off first, so a
 * voice has at most one MIDI note on. Filtered like seq_sendMidiNoteOn(). */
void seq_sendVoiceNoteOn(const uint8_t voice, const uint8_t note, const uint8_t veloc)
{
	MidiMsg msg = {0};

	if(voice >= NUM_TRACKS)
		return;
	seq_sendVoiceNoteOff(voice);

	// FILT filter out note msgs if appropriate
	if((midiParser_txRxFilter & (MIDI_FILTER_NOTES<<4))==0)
		return;

	const uint8_t channel = midi_MidiChannels[voice];
	msg.status = NOTE_ON | channel;
	msg.data1 = note;
	msg.data2 = veloc;
	msg.bits.length = 2;
	seq_sendMidi(msg);

	midi_voiceChan[voice] = channel;
	midi_voiceNote[voice] = note;
	midi_voicesOn |= (uint8_t)(1 << voice);
}
//------------------------------------------------------------------------------
/* LICHOMAT: the Note Off of the voice's last Note On, on the channel and with the note
 * it was sent with, if it is still on. Not filtered, like seq_midiNoteOff(): the
 * filter may have been switched on while the note sounds. It cancels the voice's
 * timed Note Off, and so does the next Note On, which sends this first. */
void seq_sendVoiceNoteOff(const uint8_t voice)
{
	MidiMsg msg = {0};

	if(voice >= NUM_TRACKS)
		return;
	seq_voiceOffPending &= (uint8_t)~(1 << voice);
	if(!(midi_voicesOn & (1 << voice)))
		return;
	midi_voicesOn &= (uint8_t)~(1 << voice);

	msg.status = NOTE_OFF | midi_voiceChan[voice];
	msg.data1 = midi_voiceNote[voice];
	msg.data2 = 0;
	msg.bits.length = 2;
	seq_sendMidi(msg);
}
//------------------------------------------------------------------------------
/* LICHOMAT: the Note Off of the voice's MIDI note one pulse later (SEQC_NOTE_OFF_TICKS
 * at the current tempo), for a Note On that the core does not schedule: the one-shot
 * roll and the echo of an incoming note (spec 12: no hanging notes, also while
 * stopped). Only in the seq_tick() context, where both are sent and where
 * seq_tick() sends the Note Off; a call from elsewhere does nothing. */
void seq_sendVoiceNoteOffLater(const uint8_t voice)
{
	if(!seq_inTick || (voice >= NUM_TRACKS) || !(midi_voicesOn & (1 << voice)))
		return;
	const uint32_t bpm = seq_tempo ? seq_tempo : 1u;
	seq_voiceOffAt[voice] = seqp_now() + SEQ_NOTE_OFF_CYCLES_AT_1_BPM / bpm;
	seq_voiceOffPending |= (uint8_t)(1 << voice);
}
//------------------------------------------------------------------------------
/** the timed Note Offs that are due at now (seq_sendVoiceNoteOffLater()) */
static void seq_sendDueNoteOffs(seqc_time_t now)
{
	for(uint8_t i=0;i<NUM_TRACKS;i++)
	{
		if((seq_voiceOffPending & (1 << i)) && (now >= seq_voiceOffAt[i]))
			voiceControl_noteOff(i);     //as the core's noteOff(); it clears the bit
	}
}


uint8_t seq_getStepIndex(uint8_t trackNr)
{
	//255 before the first step, as fw 1.7 (buttonHandler.c tests for it)
	return (trackNr < NUM_TRACKS) ? seq_stepCopy[trackNr] : SEQC_NO_STEP;
}

/* This will send a prog change on the global channel and will filter
 * out the message if appropriate
 */
static void seq_sendProgChg(const uint8_t ptn)
{
	static MidiMsg msg = { .bits = { .length = 1 } };  // status + 1 data byte

	// FILT filter out PC msgs if appropriate
	if((midiParser_txRxFilter & (MIDI_FILTER_PROG_C<<4))==0)
		return;

	msg.status = PROG_CHANGE | midi_MidiChannels[7];
	msg.data1=ptn;
	msg.bits.length=1;
	seq_sendMidi(msg);
}

//------------------------------------------------------------------------------
// LICHOMAT: host callbacks of seq_core (seq_core.h, N5). The core calls them from
// inside its own functions, so they run in the seq_tick() context.
//------------------------------------------------------------------------------
static bool seq_cbStepOn(void *ctx, uint8_t pattern, uint8_t track, uint8_t step)
{
	(void)ctx;
	return seq_intIsMainStepActive(track, step, pattern) != 0;
}
//------------------------------------------------------------------------------
static const Step *seq_cbStep(void *ctx, uint8_t pattern, uint8_t track, uint8_t step)
{
	(void)ctx;
	return &seq_patternSet.seq_subStepPattern[pattern][track][step];
}
//------------------------------------------------------------------------------
static StepTiming seq_cbTiming(void *ctx, uint8_t pattern, uint8_t track, uint8_t step)
{
	(void)ctx;
	return seq_patternSet.seq_stepTiming[pattern][track][step];
}
//------------------------------------------------------------------------------
static uint8_t seq_cbTrackLength(void *ctx, uint8_t pattern, uint8_t track)
{
	(void)ctx;
	return seq_patternSet.length[pattern][track];
}
//------------------------------------------------------------------------------
/** PAR_SHUFFLE 0..127: seq_setShuffle() stores it divided by 127 */
static uint8_t seq_cbShuffle(void *ctx)
{
	(void)ctx;
	const float shuffle = seq_shuffle * 127.f + 0.5f;
	if(shuffle <= 0.f)
		return 0;
	return shuffle >= 127.f ? 127 : (uint8_t)shuffle;
}
//------------------------------------------------------------------------------
static bool seq_cbMuted(void *ctx, uint8_t track)
{
	(void)ctx;
	return (seq_mutedTracks & (1 << track)) != 0;
}
//------------------------------------------------------------------------------
/** the periodic roll rate in steps; 0 when the track's roll is off or in one-shot mode */
static uint8_t seq_cbRollRate(void *ctx, uint8_t track)
{
	(void)ctx;
	if((seq_rollRate == 0xff) || !(seq_rollState & (1 << track)))
		return 0;
	return seq_rollRate;
}
//------------------------------------------------------------------------------
static uint8_t seq_cbActivePattern(void *ctx)
{
	(void)ctx;
	return seq_activePattern;
}
//------------------------------------------------------------------------------
/** The pattern-end block of fw 1.7 seq_nextStep(): pick the next pattern (song mode
 * or a pending pattern) and switch to it with seq_setPendingPatternActive(). That
 * also runs when the same pattern continues: UI refresh, SKIP_NEXT_KIT_AUTOLOAD. */
static seqc_pat_t seq_cbPatternEnd(void *ctx)
{
	(void)ctx;
	//the song advances only when no other pattern is pending
	if(seq_activePattern == seq_pendingPattern)
	{
		uint8_t next = seq_determineNextPattern(1);
		if((next >= MAX_PATTERN) && (next != SONG_LOOP))
		{
			//SONG_END: the core stops and rewinds, and the REWIND transport puts the song
			//pointer before entry 0, so the next start plays the song from entry 0 once.
			//fw 1.7 set pending = song[0] and the pointer to 0 here, which played entry 0
			//twice after the usual stop press.
			seq_pendingPattern = seq_activePattern;
			return SEQC_PAT_STOP;
		}
		if(next == SONG_LOOP)
		{
			//reset to song start; a marker in entry 0 plays pattern 0 (fw 1.7 did not check it)
			next = seq_activeSong[0] < MAX_PATTERN ? seq_activeSong[0] : 0;
			seq_songPositionPointer = 0;
		}
		seq_pendingPattern = next;
	}
	const uint8_t changed = (seq_activePattern != seq_pendingPattern) || seq_loadPendigFlag;
	seq_setPendingPatternActive();
	return changed ? SEQC_PAT_CHANGED : SEQC_PAT_SAME;
}
//------------------------------------------------------------------------------
/** what seq_cbPatternEnd() will do at the next pattern end, without side effects */
static bool seq_cbPeekNextPattern(void *ctx, uint8_t *pattern)
{
	(void)ctx;
	uint8_t next = seq_pendingPattern;
	if(seq_activePattern == seq_pendingPattern)
	{
		next = seq_determineNextPattern(0);
		if(next == SONG_LOOP)
			next = seq_activeSong[0] < MAX_PATTERN ? seq_activeSong[0] : 0;
	}
	if(next >= MAX_PATTERN)
	{
		*pattern = SEQC_NO_PATTERN;     //the song ends at the next pattern end
		return false;
	}
	*pattern = next;
	return (next != seq_activePattern) || seq_loadPendigFlag;
}
//------------------------------------------------------------------------------
/** the effective length of the longest track of a pattern (fw 1.7 seq_getLongestTrack()) */
static uint8_t seq_masterLength(uint8_t pattern)
{
	uint8_t master = 0;
	for(uint8_t i=1;i<NUM_TRACKS;i++)
	{
		if(seq_patternSet.length[pattern][i] > seq_patternSet.length[pattern][master])
			master = i;
	}
	const uint8_t len = seq_patternSet.length[pattern][master];
	if(len == 0)
		return 16;
	return len > NUM_STEPS ? NUM_STEPS : len;
}
//------------------------------------------------------------------------------
/** N6: a Song Position Pointer while stopped or armed. In song mode, walk the song to
 * absStep and activate that entry; returns the steps since its run of identical
 * entries began. In pattern mode the pattern has looped since step 0. */
static uint32_t seq_cbSeekPosition(void *ctx, uint32_t absStep)
{
	(void)ctx;
	if(!seq_songModeEnabled || (seq_activeSong[0] >= MAX_PATTERN))
		return absStep;

	//E4.1 F33: a looping song repeats every period, one pass through it. A position two
	//periods or more into it is walked from one period before its own, so that a run that
	//crosses the wrap still finds its start: at most about three passes through the song
	//(192 entries) in seq_tick() instead of up to 16384. A loop of one pattern is a single
	//run, so its steps are those since step 0.
	uint32_t period = 0;        //steps of one pass through a looping song, 0 if it ends
	uint8_t uniform = 1;        //every entry of the loop is the same pattern
	for(uint8_t i=0;i<MAX_SONG_LENGTH;i++)
	{
		const uint8_t entry = seq_activeSong[i];
		if(entry == SONG_LOOP)
			break;
		if(entry >= MAX_PATTERN)
		{
			period = 0;         //SONG_END
			break;
		}
		period += seq_masterLength(entry);
		uniform = uniform && (entry == seq_activeSong[0]);
	}
	const uint32_t skipped = (period && (absStep >= 2 * period)) ? (absStep / period - 1) * period : 0;
	const uint32_t walkStep = absStep - skipped;

	uint32_t entryStart = 0;    //absolute step where entry idx begins
	uint32_t runStart = 0;      //absolute step where the run of identical entries began
	uint32_t lastEnd = 0;       //end of the last entry walked through completely
	uint32_t lastRunStart = 0;
	uint32_t steps = 0;
	uint8_t idx = 0;
	uint8_t lastIdx = 0;
	uint8_t prev = SONG_END;    //pattern of the entry before idx
	uint8_t found = 0;

	//every pass adds an entry of at least one step, or wraps at SONG_LOOP to entry 0,
	//which is a pattern, so the walk ends
	for(;;)
	{
		const uint8_t entry = seq_activeSong[idx];
		if(entry == SONG_LOOP)
		{
			idx = 0;
			continue;
		}
		if(entry >= MAX_PATTERN)
			break;      //SONG_END
		if(entry != prev)
			runStart = entryStart;
		const uint32_t len = seq_masterLength(entry);
		if(walkStep < entryStart + len)
		{
			steps = walkStep - runStart + (uniform ? skipped : 0);
			found = 1;
			break;
		}
		prev = entry;
		lastIdx = idx;
		lastRunStart = runStart;
		entryStart += len;
		lastEnd = entryStart;
		idx = (uint8_t)((idx + 1) % MAX_SONG_LENGTH);
	}
	if(!found)
	{
		//past SONG_END: the last step of the last entry
		idx = lastIdx;
		steps = lastEnd - 1 - lastRunStart;
	}

	//activate the entry with the normal pattern-change side effects. Inside the run of
	//the active pattern nothing changes and nothing reloads, as at a pattern end of the
	//song walk: a locate in the DAW loses no unsaved edit and loads no kit again. The
	//call still refreshes the song screen and the LEDs.
	seq_songPositionPointer = idx;
	seq_pendingPattern = seq_activeSong[idx];
	seq_setPendingPatternActive();
	return steps;
}
//------------------------------------------------------------------------------
/** An event fires (spec 6.4): note off the voice's last note, apply the event's own
 * p-locks (D5), trigger the voice, send the MIDI Note On with the event's velocity
 * and the note override. A roll hit is recorded at its own step (N11). */
static void seq_cbTrigger(void *ctx, const seqc_event_t *ev)
{
	(void)ctx;
	const uint8_t voiceNr = ev->track;
	const Step plocks = { .param1Nr = ev->p1Nr, .param1Val = ev->p1Val, .param2Nr = ev->p2Nr, .param2Val = ev->p2Val };

	// if a note is on for that voice send note-off first
	voiceControl_noteOff(voiceNr);

	seq_parseAutomationNodes(voiceNr, &plocks);

	//LICHOMAT v0.3: hit 0 of a slide step of Drum 1-3 glides from the voice's last note
	if(voiceNr < 3)
		Drum_setSlide(voiceNr, (ev->flags & SEQC_EVF_SLIDE) ? 1 : 0);

	//Trigger internal synth voice
	voiceControl_noteOn(voiceNr, ev->note, ev->vel);

	// the note that is played will be whatever is received unless we have a note override set
	// A note override is any non-zero value for this parameter
	const uint8_t midiNote = activeParameters[PAR_MIDI_NOTE1 + voiceNr] == 0 ? ev->note : activeParameters[PAR_MIDI_NOTE1 + voiceNr];
	seq_sendVoiceNoteOn(voiceNr, midiNote, ev->vel);

	//record roll notes at their own step
	if((ev->flags & SEQC_EVF_ROLL) && seq_recordActive)
		seq_recordNoteAtStep(ev->pattern, voiceNr, ev->step, ev->vel, ev->note);
}
//------------------------------------------------------------------------------
/** MIDI Note Off one pulse after the trigger (fw 1.7 seq_processFlam()) */
static void seq_cbNoteOff(void *ctx, uint8_t track)
{
	(void)ctx;
	voiceControl_noteOff(track);
}
//------------------------------------------------------------------------------
/** LICHOMAT v0.2: the part of a step boundary that follows the active track: erase,
 * follow-bar, chase light. At the master boundary for a track on the master grid, at its
 * own boundary (trackStep()) for a track at another tempo */
static void seq_activeTrackStep(void)
{
	// **RECORD if we are in erase mode (shift clear while record and playing)
	// and this is the active track on the front, we erase the note value
	const uint8_t eraseTrack = frontParser_activeTrack;
	if(seq_eraseActive && (eraseTrack < NUM_TRACKS) && !(seq_mutedTracks & (1<<eraseTrack)))
	{
		const uint8_t eraseStep = seqc_trackStep(eraseTrack);
		if((eraseStep < NUM_STEPS) && seq_intIsMainStepActive(eraseTrack,eraseStep,seq_activePattern))
		{
			//sub steps are ditched, so only remove main step
			seq_setMainStep(seq_activePattern, eraseTrack, eraseStep,0);
			//if steps are shown, (active bar) turn off LED
			if( (eraseStep >= (menu_activeBar*16)) && (eraseStep < (menu_activeBar+1)*16))
			{
				led_setValue(0,led_idx2StepLed(eraseStep%16));
			}
		}
	}

	//step display follows bars
	if(activeParameters[PAR_FOLLOW] & FOLLOW_BAR ) {

		//only update if bar changed
		if(menu_activeBar != seqc_trackStep(menu_getActiveVoice())/16)
		{
			if( menu_activePage < PATTERN_LOAD_PAGE || menu_activePage > SONG_MODE_PAGE)
			{
				menu_setShownPattern(seq_activePattern);
				led_clearSequencerLeds();
				//query current sequencer step states and light up the corresponding leds
				const uint8_t trackNr = menu_getActiveVoice(); //max 6 => 0x6 = 0b110

				//update menu_activeBar and Bar LEDs
				menu_activeBar = seqc_trackStep(trackNr)/16;

				led_updateSeqLeds();
			}
		}
	}

	//chase light on the step LEDs
	const uint8_t shownPattern = menu_getViewedPattern();
	const uint8_t playedPattern = menu_playedPattern;

	const uint8_t stepIdx = seqc_trackStep(frontParser_activeTrack);

	const uint8_t isOnActivePage = (stepIdx >= menu_activeBar*16) && (stepIdx < (menu_activeBar*16+16));

	if( (shownPattern == playedPattern) && (isOnActivePage)) {
		//only update chaselight LED when in step edit mode
		if( (menu_activePage < MENU_MIDI_PAGE) || menu_activePage == PERF_PAGE ||menu_activePage == SEQ_PAGE || menu_activePage == EUKLID_PAGE) {
			led_setActive_step(stepIdx%16);
		}
	} else {
		led_clearActive_step();
	}
}
//------------------------------------------------------------------------------
/** Step boundary, the rest of fw 1.7 seq_nextStep(): beat LED, erase on the active
 * track, follow-bar, chase light and the MTC check. The erase runs before the core
 * generates the step's late part, so an erased step does not play, unless its early
 * part already played hits before n * 96; its late hits then follow it (N3). */
static void seq_cbStepBoundary(void *ctx, uint32_t absStep)
{
	(void)ctx;
	(void)absStep;

	//---------- now check if the master track is at a full beat position to flash the start/stop button --------
	const uint8_t masterStepPos = seqc_barStep();   //LICHOMAT v0.2: the master bar
	seq_muteSceneBoundary(masterStepPos);   //LICHOMAT v0.3: a recalled mute scene (plan D18)
	if((masterStepPos&3) == 0)
	{
		//a quarter beat occured (multiple of 4 steps in the 16 step pattern)
		//turn on the start/stop led (beat indicator)
		led_setValue(1,LED_START_STOP);
	}
	else if ((masterStepPos&3) == 1)
	{
		//turn it of again on the next step
		led_setValue(0,LED_START_STOP);
	}

	if(seqc_trackOnMasterGrid(frontParser_activeTrack))
		seq_activeTrackStep();

	// check mtc, which might stop the sequencer if we haven't seen one in a while
	midiParser_checkMtc();
}
//------------------------------------------------------------------------------
/** LICHOMAT v0.2: a track at another tempo began its next step */
static void seq_cbTrackStep(void *ctx, uint8_t track)
{
	(void)ctx;
	if(track == frontParser_activeTrack)
		seq_activeTrackStep();
}
//------------------------------------------------------------------------------
/** every step of the tempo, also while stopped (N8) */
static void seq_cbTempoStep(void *ctx)
{
	(void)ctx;
	led_blinkHandler(); //blink leds in sync to tempo/seq
}
//------------------------------------------------------------------------------
/** internal sync only: one MIDI clock */
static void seq_cbMidiClockOut(void *ctx)
{
	(void)ctx;
	seq_sendRealtime(MIDI_CLOCK);
}
//------------------------------------------------------------------------------
/** while running, every 4 ticks: one pulse of the clock out jack. The output PPQ
 * prescaler counts the pulses itself (N8). Its count is set from the position
 * first, so that a Continue from inside a step or after a song position stays on
 * the step grid: trigger_reset(1) only puts it at 0, which is right for a start
 * at step 0. The count runs over four steps, so 2 PPQ stays on every second step
 * counted from the start of the song. */
static void seq_cbClockOutPulse(void *ctx, uint8_t pulseInStep)
{
	(void)ctx;
	trigger_setClockPhase((uint8_t)(((uint32_t)seqc_absStep() % 4u) * SEQC_PULSES_PER_STEP + pulseInStep));
	trigger_clockTick(0, 0);
}
//------------------------------------------------------------------------------
/** N18.2: MIDI Start, Stop and Continue go out only when the LXR is the master. As
 * a slave it sends none; MIDI thru, when on, forwards what arrives, so downstream
 * gear gets no duplicates. seq_coreSyncMode is the mode the core runs in. */
static void seq_sendTransport(const uint8_t status)
{
	if(seq_coreSyncMode == SEQC_SYNC_INTERNAL)
		seq_sendRealtime(status);
}
//------------------------------------------------------------------------------
/** A transport change: the side effects of fw 1.7 seq_setRunning() and
 * seq_resetPosition(), the run state of seq_isRunning(), and the start/stop
 * button and LED, which now follow every change (fw 1.7 only told them about MTC) */
static void seq_cbTransport(void *ctx, seqc_transport_t change)
{
	(void)ctx;
	switch(change)
	{
	case SEQC_TRANSPORT_STOP:
		seq_running = 0;
		preset_touchTimer(PRESET_PAT_TIMER);
		seq_sendTransport(MIDI_STOP);
		trigger_reset(0);
		// if mtc was doing it's thing, tell it to stop it.
		midiParser_checkMtc();
		break;

	case SEQC_TRANSPORT_REWIND:
		led_resetBlinkTimer();
		seq_songPositionPointer = 0xff;     //before entry 0: the first pattern end moves it to 0
		// send notes off on all channels that have notes playing and reset our bitmap to reflect that
		voiceControl_noteOff(0xFF);
		break;

	case SEQC_TRANSPORT_ARM:
		seq_running = 1;
		//abort pattern save timer, since seq is running again...
		preset_touchTimer(PRESET_STOP_PAT_TIMER);
		break;

	case SEQC_TRANSPORT_START:
	case SEQC_TRANSPORT_CONTINUE:
		seq_running = 1;
		//abort pattern save timer, since seq is running again...
		preset_touchTimer(PRESET_STOP_PAT_TIMER);
		seq_sendTransport(change == SEQC_TRANSPORT_START ? MIDI_START : MIDI_CONTINUE);
		trigger_reset(1);
		break;
	}

	uart_sendFrontpanelByte(FRONT_SEQ_CC);
	uart_sendFrontpanelByte(FRONT_SEQ_RUN_STOP);
	uart_sendFrontpanelByte(seq_running);
}
//------------------------------------------------------------------------------
/* LICHOMAT: the source of the probability draw. A host build defines
 * SEQ_RANDOM() before this point, to a random stream of its own (stage 1b F5,
 * emulator spec 4.9); the firmware takes the hardware RNG. */
#ifndef SEQ_RANDOM
#define SEQ_RANDOM() GetRngValue()
#endif
/** the probability draw: the core uses 1 + random() % 100 */
static uint32_t seq_cbRandom(void *ctx)
{
	(void)ctx;
	return SEQ_RANDOM();
}
//------------------------------------------------------------------------------
/** LICHOMAT v0.2: the track's settings in the pattern (tempo, humanize, delay) */
static TrackGroove seq_cbGroove(void *ctx, uint8_t pattern, uint8_t track)
{
	(void)ctx;
	return seq_patternSet.groove[pattern][track];
}
//------------------------------------------------------------------------------
static const seqc_host_t seq_host =
{
	.ctx = NULL,
	.stepOn = seq_cbStepOn,
	.step = seq_cbStep,
	.timing = seq_cbTiming,
	.trackLength = seq_cbTrackLength,
	.shuffle = seq_cbShuffle,
	.muted = seq_cbMuted,
	.rollRate = seq_cbRollRate,
	.activePattern = seq_cbActivePattern,
	.patternEnd = seq_cbPatternEnd,
	.peekNextPattern = seq_cbPeekNextPattern,
	.seekPosition = seq_cbSeekPosition,
	.trigger = seq_cbTrigger,
	.noteOff = seq_cbNoteOff,
	.stepBoundary = seq_cbStepBoundary,
	.tempoStep = seq_cbTempoStep,
	.midiClockOut = seq_cbMidiClockOut,
	.clockOutPulse = seq_cbClockOutPulse,
	.transport = seq_cbTransport,
	.random = seq_cbRandom,
	.groove = seq_cbGroove,
	.trackStep = seq_cbTrackStep,
};
//------------------------------------------------------------------------------
static void seq_initCore(void)
{
	for(uint8_t i=0;i<NUM_TRACKS;i++)
	{
		seq_stepCopy[i] = SEQC_NO_STEP;
		seq_phaseCopy[i] = 0.f;
	}

	seqp_init();
	seqc_init(&seq_host, SEQC_CPU_HZ);
	seq_coreReady = 1;
}
//------------------------------------------------------------------------------
/** apply the posts of the other contexts, in the order sync mode, clock in PPQ,
 * tempo, stop, rewind, start, one-shot rolls, skip-once markers */
static void seq_applyMailbox(void)
{
	SeqMailbox mb;
	const uint32_t primask = seq_mailboxLock();
	mb = seq_mailbox;
	memset(&seq_mailbox, 0, sizeof seq_mailbox);
	seq_mailboxPosted = 0;
	seq_mailboxUnlock(primask);

	if(mb.syncValid)
	{
		//the copy first: a transport change that the switch causes (an armed start in
		//internal sync) goes out as the new mode wants (N18.2)
		if(mb.syncMode <= SEQC_SYNC_MIDI)
			seq_coreSyncMode = mb.syncMode;
		seqc_setSyncMode((seqc_sync_t)mb.syncMode);
	}
	if(mb.ppqValid)
		seqc_setAnalogPpq(mb.ppq);
	if(mb.tempoValid)
		seqc_setTempo((float)mb.tempo);

	if(mb.transport & SEQ_MB_STOP)
		seqc_stop();
	if(mb.transport & SEQ_MB_REWIND)
	{
		seqc_rewind();
		//fw 1.7 seq_resetPosition() sent all notes off on every stop press, also when stopped already
		voiceControl_noteOff(0xFF);
	}
	if(mb.transport & SEQ_MB_START)
		seqc_start();

	for(uint8_t i=0;i<NUM_TRACKS;i++)
	{
		if(mb.rollMask & (1 << i))
		{
			//one-shot roll (seq_setRoll()): trigger, and record while running. A note
			//recorded on the next step sets its skip-once marker in the core at once
			//(seq_postSkipOnce() inside seq_tick()), before seqc_process() reaches it
			seq_triggerVoice(i,ROLL_VOLUME,SEQ_DEFAULT_NOTE);
			seq_addNote(i,ROLL_VOLUME,SEQ_DEFAULT_NOTE);
		}
	}
	for(uint8_t i=0;i<NUM_TRACKS;i++)
	{
		if(mb.skipMask & (1 << i))
			seqc_skipOnce(i, mb.skipStep[i]);
	}
}
//------------------------------------------------------------------------------
/** feed the queued sync events to the core, oldest first, until none is left;
 * returns 1 if there was one, and in *now the time of the last pop, for seqc_process().
 * The time is read again before every pop. The handling of an entry can call out of
 * the sequencer for long: a kit load takes tens of ms, and the pattern switch of an
 * accepted Program Change, the seek of a Song Position Pointer in song mode and a
 * slave clock that reaches a pattern end (patternEnd(), the kit autoload) all
 * load one. An entry stamped during that is later than the time read before it, and
 * popped against that time it would reach the core early, at that time instead of its
 * stamp, which skews the slave tempo and the position between clocks. */
static uint8_t seq_drainSyncQueues(seqc_time_t *now)
{
	seqp_src_t source;
	seqq_entry_t entry;
	seqc_time_t stamp;
	uint8_t drained = 0;

	for(;;)
	{
		*now = seqp_now();
		if(!seqp_syncPop(*now, &source, &entry, &stamp))
			break;
		drained = 1;
		if(entry.type == SEQQ_PROGRAM_CHANGE)
		{
			//a Program Change switches the pattern here, in its place among Start,
			//Continue and Stop (fw 1.7 parsed them in arrival order), with the
			//filter and the channel test of the fw 1.7 parser
			if((midiParser_txRxFilter & MIDI_FILTER_PROG_C) && ((entry.value >> 8) == midi_MidiChannels[7]))
			{
				seq_setNextPattern((uint8_t)entry.value);
				//a switch while stopped loaded the pattern (and the kit) and rewinds
				//through the mailbox: the rewind before the next event
				if(seq_mailboxPosted)
					seq_applyMailbox();
			}
			continue;
		}
		//the RX filter for MIDI system messages; the core checks the sync mode
		if((source != SEQP_SRC_JACK) && !(midiParser_txRxFilter & MIDI_FILTER_SYSTEM))
			continue;
		if(entry.type == SEQQ_SONG_POSITION)
		{
			seqc_songPosition(entry.value);
			continue;
		}
		seqc_syncEvent((seqc_sync_ev_t)entry.type, stamp);
		//fw 1.7 seq_resetPosition() sent all notes off on every active reset gate edge, also
		//when stopped at the start already, where the core reports no rewind
		if((source == SEQP_SRC_JACK) && (entry.type == SEQC_EV_RESET_ON) && (seq_coreSyncMode == SEQC_SYNC_ANALOG))
			voiceControl_noteOff(0xFF);
	}
	return drained;
}
//------------------------------------------------------------------------------
/** Copy the core state that other contexts read (N7), only what can have changed:
 * - the step indices change at a step boundary or through a post or a sync event (a
 *   rewind, a song position); LICHOMAT v0.2: each of them changes seqc_stepSerial(),
 *   also a boundary of a track at another tempo;
 * - the phases, the costly ones, are read only by recording (seq_addNote());
 * - in the slave modes seq_getBpm() shows the incoming tempo, rounded, and the
 *   LFOs follow it (N9); it changes only with a clock. */
static void seq_refreshCopies(uint8_t changed)
{
	static uint8_t copied = 0;          //nothing copied yet: the first call copies
	static uint32_t lastSerial;
	const uint32_t serial = seqc_stepSerial();
	if(changed || !copied || (serial != lastSerial))
	{
		copied = 1;
		lastSerial = serial;
		for(uint8_t i=0;i<NUM_TRACKS;i++)
			seq_stepCopy[i] = seqc_trackStep(i);
	}
	if(seq_recordActive)
		for(uint8_t i=0;i<NUM_TRACKS;i++)
			seq_phaseCopy[i] = seqc_trackPhase(i);   //LICHOMAT v0.2: each track's own step

	if(changed && seq_isSyncExternal)
	{
		const uint16_t bpm = (uint16_t)(seqc_currentBpm() + 0.5f);
		if(bpm != seq_tempo)
		{
			seq_tempo = bpm;
			lfo_recalcSync();
		}
	}
}
//------------------------------------------------------------------------------
#define SEQ_DIN_BYTES_PER_TICK      32  /**< MIDI DIN bytes parsed per seq_tick() (spec 7.2) */
#define SEQ_USB_MESSAGES_PER_TICK   8   /**< USB MIDI messages parsed per seq_tick() */

/** MIDI input (D8): DIN and USB are parsed only here, in the seq_tick() context,
 * with a budget per call. The message handlers reach the core through the mailbox,
 * except seq_postSkipOnce(), which sets the skip-once marker of a recorded note in
 * the core at once. Program Changes act in seq_drainSyncQueues(). */
static void seq_parseMidiInput(void)
{
	MidiMsg msg;

	uart_processMidi(SEQ_DIN_BYTES_PER_TICK);
	for(uint8_t i=0;(i<SEQ_USB_MESSAGES_PER_TICK) && usb_getMidi(&msg);i++)
		midiParser_parseMidiMessage(msg);
}
//------------------------------------------------------------------------------
/** call periodically to check if the next step has to be processed (spec 4.3, N7).
 * Called from soundengine_process() with the TIM2 interrupt disabled, and from the
 * TIM2 interrupt; the two never overlap. Most calls find nothing to do: they read
 * the post flag and the queue indices, the core returns at once (N10), and the
 * copies are refreshed only where something changed. */
void seq_tick()
{
	//a kit autoload inside a core callback runs soundengine_process() and so seq_tick()
	//again: the nested call must not touch the mailbox, the queues or the core
	if(seq_inTick || !seq_coreReady)
		return;
	seq_inTick = 1;

	uint8_t changed = 0;
	if(seq_mailboxPosted)
	{
		seq_applyMailbox();
		changed = 1;
	}

	seqc_time_t now;
	if(seq_drainSyncQueues(&now))
		changed = 1;
	seqc_process(now);
	//the Note Offs one pulse after a one-shot roll or a note echo
	if(seq_voiceOffPending)
		seq_sendDueNoteOffs(now);

	seq_refreshCopies(changed);

	//last, so that recording sees the state of this call. A note recorded here sets
	//its skip-once marker in the core at once (seq_postSkipOnce() inside seq_tick())
	seq_parseMidiInput();

	seq_inTick = 0;
}
//------------------------------------------------------------------------------
/** LICHOMAT v0.2: the track page's four parameters (Euklid page, second screen) from the
 * track's settings in pattern */
void seq_loadGrooveParams(uint8_t trackNr, uint8_t pattern)
{
	if((trackNr >= NUM_TRACKS) || (pattern >= NUM_PATTERN)) return;
	const TrackGroove g = trackGroove_checked(seq_patternSet.groove[pattern][trackNr]);
	activeParameters[PAR_TRACK_TEMPO] = (uint8_t)(g.tempo + GROOVE_TEMPO_X1);
	activeParameters[PAR_HUMAN_TIME] = g.humTime;
	activeParameters[PAR_HUMAN_VOL] = g.humVol;
	activeParameters[PAR_TRACK_DELAY] = (uint8_t)(g.delay - GROOVE_DELAY_MIN);
}
//------------------------------------------------------------------------------
/** LICHOMAT v0.2: a track page parameter edited, as its menu value */
void seq_setGrooveParam(uint8_t trackNr, uint8_t pattern, uint16_t paramNr, uint8_t value)
{
	if((trackNr >= NUM_TRACKS) || (pattern >= NUM_PATTERN)) return;
	TrackGroove *g = &seq_patternSet.groove[pattern][trackNr];
	switch(paramNr)
	{
	case PAR_TRACK_TEMPO:	g->tempo = (int8_t)(value < GROOVE_TEMPO_COUNT ? value - GROOVE_TEMPO_X1 : 0); break;
	case PAR_HUMAN_TIME:	g->humTime = value <= GROOVE_HUM_TIME_MAX ? value : 0; break;
	case PAR_HUMAN_VOL:	g->humVol = value <= GROOVE_HUM_VOL_MAX ? value : 0; break;
	case PAR_TRACK_DELAY:	g->delay = (int8_t)(value <= GROOVE_DELAY_MAX - GROOVE_DELAY_MIN ? value + GROOVE_DELAY_MIN : 0); break;
	default: break;
	}
}
//------------------------------------------------------------------------------
/** LICHOMAT v0.3: the slide flag of a step (bit 7 of Step.prob, plan D23), probability kept */
void seq_setStepSlide(uint8_t pattern, uint8_t track, uint8_t step, uint8_t on)
{
	if((pattern >= NUM_PATTERN) || (track >= NUM_TRACKS)
			|| (step >= sizeof seq_patternSet.seq_subStepPattern[0][0] / sizeof(Step))) return;
	Step *s = &seq_patternSet.seq_subStepPattern[pattern][track][step];
	s->prob = (uint8_t)(on ? (s->prob | STEP_SLIDE_FLAG) : (s->prob & STEP_PROB_MASK));
}
//------------------------------------------------------------------------------
/** LICHOMAT v0.3: the step page's probability or condition (7 bits), slide flag kept */
void seq_setStepProbKeepSlide(uint8_t pattern, uint8_t track, uint8_t step, uint8_t prob)
{
	if((pattern >= NUM_PATTERN) || (track >= NUM_TRACKS)
			|| (step >= sizeof seq_patternSet.seq_subStepPattern[0][0] / sizeof(Step))) return;
	Step *s = &seq_patternSet.seq_subStepPattern[pattern][track][step];
	s->prob = (uint8_t)((s->prob & STEP_SLIDE_FLAG) | (prob & STEP_PROB_MASK));
}
//------------------------------------------------------------------------------
/** LICHOMAT v0.2: move every step of the track in pattern one step later (dir +1) or
 * earlier (-1) within the track length: the step data (volume, probability or condition,
 * note, p-locks), the step timing (flam, shift) and the main-step bit move together, the
 * step at one end wraps to the other, and the steps past the length stay */
void seq_rotateTrack(uint8_t trackNr, uint8_t pattern, int8_t dir)
{
	if((trackNr >= NUM_TRACKS) || (pattern >= NUM_PATTERN) || ((dir != 1) && (dir != -1))) return;
	uint8_t len = seq_patternSet.length[pattern][trackNr];
	if(len == 0) len = 16;
	else if(len > NUM_STEPS) len = NUM_STEPS;
	if(len < 2) return;
	Step *s = seq_patternSet.seq_subStepPattern[pattern][trackNr];
	StepTiming *tm = seq_patternSet.seq_stepTiming[pattern][trackNr];
	const uint8_t wrap = (dir > 0) ? (uint8_t)(len - 1) : 0;
	const Step keep = s[wrap];
	const StepTiming keepTiming = tm[wrap];
	const uint8_t keepOn = seq_intIsMainStepActive(trackNr, wrap, pattern);
	if(dir > 0)
	{
		for(uint8_t i = (uint8_t)(len - 1); i > 0; i--)
		{
			s[i] = s[i-1];
			tm[i] = tm[i-1];
			seq_setMainStep(pattern, trackNr, i, seq_intIsMainStepActive(trackNr, (uint8_t)(i-1), pattern));
		}
		s[0] = keep;
		tm[0] = keepTiming;
		seq_setMainStep(pattern, trackNr, 0, keepOn);
	}
	else
	{
		for(uint8_t i = 0; i + 1 < len; i++)
		{
			s[i] = s[i+1];
			tm[i] = tm[i+1];
			seq_setMainStep(pattern, trackNr, i, seq_intIsMainStepActive(trackNr, (uint8_t)(i+1), pattern));
		}
		s[len-1] = keep;
		tm[len-1] = keepTiming;
		seq_setMainStep(pattern, trackNr, (uint8_t)(len - 1), keepOn);
	}
}
