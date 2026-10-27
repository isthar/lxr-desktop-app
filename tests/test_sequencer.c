/*
 * test_sequencer.c
 *
 * Host test of the firmware's sequencer glue (Core/Src/Soundengine/Sequencer/
 * sequencer.c, task E4.6), compiled as it is, with the timing core seq_core and
 * against the stand-in headers in tests/stubs/ and the stand-in functions below.
 * seq_tick() runs the core on a time that the test moves on; the sync input
 * queues are empty.
 *
 * The song page (fw 1.7 bug): the song cursor of the menu, menu_songMenuPosition,
 * follows the song pointer at every pattern switch. The pointer is 0xff before
 * entry 0, after every rewind (each stop) and after song mode is switched on, and
 * fw 1.7 copied that too. A step button on the song page then wrote the pattern
 * number at seq_activeSong[255]: the last entry of the song three slots up, or
 * past seq_songData into patternBackupBuffer. Here seq_activeSong points into a
 * buffer with guard bytes after the 64 entries, so the test sees a store past the
 * song also without a sanitizer.
 *
 * The automation step (fw 1.7 bug): seq_armAutomationStep() took any step and
 * track, and seq_recordAutomation() wrote the armed one. SHIFT + a held select
 * button (buttonHandler.c) asked for steps up to 127, and a knob turn wrote a step
 * of the next track, or of the next pattern. An armed step or track out of range
 * now disarms.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include "buttonHandler.h"
#include "ledHandler.h"
#include "menu/menu.h"
#include "Preset/PresetManager.h"
#include "Soundengine/DSPAudio/automationNode.h"
#include "Soundengine/DSPAudio/random.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/globals.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/MIDI/Uart.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/TriggerOut.h"
#include "Soundengine/USB/usb_manager.h"

#define GUARD_AFTER 256		/* past index 255, where fw 1.7 put the pattern */
#define GUARD_BYTE 0xA5
#define MS (SEQC_CPU_HZ / 1000u)

//------------------------------------------------------------------------------
// The song and its guard bytes
//------------------------------------------------------------------------------
static uint8_t songAndGuard[MAX_SONG_LENGTH + GUARD_AFTER];

/** guard bytes after the song that a store changed */
static int changedAfterSong(void)
{
	int n = 0;
	for (int i = MAX_SONG_LENGTH; i < (int)sizeof songAndGuard; i++)
		if (songAndGuard[i] != GUARD_BYTE)
			n++;
	return n;
}

//------------------------------------------------------------------------------
// Firmware state that sequencer.c reads and writes
//------------------------------------------------------------------------------
uint8_t activeParameters[NUM_PARAMS];
FxBus fxBus;
DWT_Type host_dwt;
uint8_t frontParser_activeTrack;
uint8_t frontParser_shownPattern;
uint8_t menu_activeBar;
uint8_t menu_activePage;
uint8_t menu_loadedPresetNr[NUM_CURRENT];
uint8_t menu_playedPattern;
uint8_t menu_selectedPresetNr[NUM_PRESET_LOCATIONS];
uint8_t menu_shownPattern;
uint8_t menu_songMenuPosition;
uint8_t midiParser_txRxFilter;
uint8_t midi_MidiChannels[8];

//------------------------------------------------------------------------------
// Stand-ins for the functions it calls
//------------------------------------------------------------------------------
static seqc_time_t hostNow = 1;		/* seqp_now(), in cycles */

void seqp_init(void) {}
seqc_time_t seqp_now(void) { return hostNow; }
bool seqp_syncPop(seqc_time_t now, seqp_src_t *source, seqq_entry_t *entry, seqc_time_t *stamp)
{
	(void)now; (void)source; (void)entry; (void)stamp;
	return false;
}

uint32_t GetRngValue(void) { return 0x12345678u; }
void autoNode_init(AutomationNode *node) { (void)node; }
void autoNode_setDestination(AutomationNode *node, uint16_t dest) { (void)node; (void)dest; }
void autoNode_updateValue(AutomationNode *node, uint8_t val) { (void)node; (void)val; }
uint8_t buttonHandler_getMode(void) { return SELECT_MODE_VOICE; }
void led_blinkHandler(void) {}
void led_clearActive_step(void) {}
void led_clearAllBlinkLeds(void) {}
void led_clearSequencerLeds(void) {}
uint8_t led_idx2StepLed(uint8_t idx) { return idx; }
void led_initPatternSelectLeds(void) {}
void led_resetBlinkTimer(void) {}
void led_setActive_step(uint8_t stepNr) { (void)stepNr; }
void led_setBlinkLed(const uint8_t ledNr, const uint8_t onOff) { (void)ledNr; (void)onOff; }
void led_setValue(uint8_t val, uint8_t ledNr) { (void)val; (void)ledNr; }
void led_updateSeqLeds(void) {}
void lfo_recalcSync(void) {}
uint8_t menu_getActiveVoice(void) { return 0; }
uint8_t menu_getViewedPattern(void) { return 0; }
void menu_repaint(void) {}
void menu_repaintAll(void) {}
void menu_setShownPattern(uint8_t patternNr) { (void)patternNr; }
void midiParser_checkMtc(void) {}
void midiParser_parseMidiMessage(volatile MidiMsg msg) { (void)msg; }
void preset_loadDrumsetRam(uint8_t presetNr, uint8_t isMorph, uint8_t sendImmediate)
{
	(void)presetNr; (void)isMorph; (void)sendImmediate;
}
void preset_touchTimer(PresetTimer type) { (void)type; }
void trigger_clockTick(uint8_t onOff, uint8_t cnt) { (void)onOff; (void)cnt; }
void trigger_reset(uint8_t value) { (void)value; }
void trigger_setClockPhase(uint8_t pulse) { (void)pulse; }
void uart_processMidi(uint8_t maxBytes) { (void)maxBytes; }
void uart_sendFrontpanelByte(uint8_t data) { (void)data; }
void uart_sendFrontpanelSysExByte(uint8_t data) { (void)data; }
void uart_sendMidi(MidiMsg msg) { (void)msg; }
uint8_t usb_getMidi(MidiMsg *msg) { (void)msg; return 0; }
void usb_sendMidi(MidiMsg msg) { (void)msg; }
void voiceControl_noteOff(uint8_t voice) { (void)voice; }
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel) { (void)voice; (void)note; (void)vel; }

//------------------------------------------------------------------------------
// Driving the sequencer
//------------------------------------------------------------------------------
/** seq_tick() every millisecond for ms milliseconds */
static void run(int ms)
{
	for (int i = 0; i < ms; i++) {
		hostNow += MS;
		seq_tick();
	}
}

/** a fresh sequencer, stopped, in pattern mode on pattern 0, with the test's
 * song: patterns 0, 1, 2, then SONG_END, and guard bytes after it */
static void reset(void)
{
	seq_init();
	seq_setRunning(0);
	seq_setSongModeEnabled(0);
	seq_setNextPattern(0);
	run(1);
	memset(songAndGuard, GUARD_BYTE, sizeof songAndGuard);
	memset(songAndGuard, SONG_END, MAX_SONG_LENGTH);
	songAndGuard[0] = 0;
	songAndGuard[1] = 1;
	songAndGuard[2] = 2;
	seq_activeSong = songAndGuard;
	seq_songPositionPointer = 0;
	menu_songMenuPosition = 0;
}

/** play for ms milliseconds, then the stop button (CortexParser.c,
 * FRONT_SEQ_RUN_STOP 0): rewind and stop */
static void playAndStop(int ms)
{
	seq_setRunning(1);
	run(ms);
	seq_resetPosition();
	seq_setRunning(0);
	run(1);
}

//------------------------------------------------------------------------------
// The song page
//------------------------------------------------------------------------------
TEST(sequencer_song_page_after_a_stop)
{
	reset();
	playAndStop(500);
	ASSERT_EQ(seq_songPositionPointer, 0xff);	/* before entry 0 */

	seq_setNextPattern(1);			/* a pattern selected while stopped */
	run(1);
	seq_addPatternToSong(5);		/* a step button on the song page */
	ASSERT_EQ(changedAfterSong(), 0);	/* fw 1.7: song[255] = 5 */
	ASSERT_EQ(songAndGuard[0], 5);
	ASSERT_EQ(menu_songMenuPosition, 1);
	ASSERT_EQ(songAndGuard[1], 1);
}

TEST(sequencer_song_page_after_song_mode_on)
{
	reset();
	seq_setSongModeEnabled(1);
	ASSERT_EQ(seq_songPositionPointer, 0xff);
	seq_setNextPattern(2);
	run(1);
	ASSERT_EQ(menu_songMenuPosition, 0);	/* fw 1.7: 255 */
	seq_addPatternToSong(7);
	ASSERT_EQ(changedAfterSong(), 0);
	ASSERT_EQ(songAndGuard[0], 7);
	ASSERT_EQ(menu_songMenuPosition, 1);
}

TEST(sequencer_song_page_follows_the_song)
{
	/* the cursor follows the song pointer, as in fw 1.7: entry 0 at the start,
	 * entries 1 and 2 at the pattern ends 2 s and 4 s later (16 steps at 120 BPM).
	 * The END in entry 3 would stop at 6 s. */
	reset();
	seq_setSongModeEnabled(1);
	seq_setRunning(1);
	int moves = 0;
	uint8_t lastPointer = seq_songPositionPointer;
	for (int ms = 0; ms < 5000; ms++) {
		run(1);
		if (seq_songPositionPointer != lastPointer) {
			lastPointer = seq_songPositionPointer;
			moves++;
			ASSERT_TRUE(seq_songPositionPointer < MAX_SONG_LENGTH);
			ASSERT_EQ(menu_songMenuPosition, seq_songPositionPointer);
		}
	}
	ASSERT_EQ(moves, 3);
	ASSERT_EQ(menu_songMenuPosition, 2);
	ASSERT_EQ(changedAfterSong(), 0);
	seq_resetPosition();
	seq_setRunning(0);
	run(1);
}

//------------------------------------------------------------------------------
// The automation step that a held button arms (CortexParser.c
// FRONT_ARM_AUTOMATION_STEP), and a knob turn writes (seq_recordAutomation())
//------------------------------------------------------------------------------
TEST(sequencer_automation_step_in_range)
{
	reset();
	seq_armAutomationStep(5, 1, 1);
	ASSERT_TRUE(seq_isAutomationStepArmed());
	seq_recordAutomation(1, 10, 11);
	ASSERT_EQ(seq_patternSet.seq_subStepPattern[seq_activePattern][1][5].param1Nr, 10);
	ASSERT_EQ(seq_patternSet.seq_subStepPattern[seq_activePattern][1][5].param1Val, 11);
	seq_armAutomationStep(0, 0, 0);
	ASSERT_FALSE(seq_isAutomationStepArmed());
	seq_recordAutomation(1, 12, 13);
	ASSERT_EQ(seq_patternSet.seq_subStepPattern[seq_activePattern][1][5].param1Nr, 10);
}

TEST(sequencer_automation_step_past_63_is_not_armed)
{
	/* fw 1.7 armed step 79 of track 1, and the knob turn wrote step 16 of track 2 */
	reset();
	const Step before = seq_patternSet.seq_subStepPattern[seq_activePattern][1][15];
	seq_armAutomationStep(79, 0, 1);
	ASSERT_FALSE(seq_isAutomationStepArmed());
	seq_recordAutomation(0, 10, 11);
	ASSERT_EQ(memcmp(&before, &seq_patternSet.seq_subStepPattern[seq_activePattern][1][15], sizeof before), 0);

	/* nor a track past the last one: the next pattern's track 1 */
	const Step nextPattern = seq_patternSet.seq_subStepPattern[seq_activePattern + 1][0][5];
	seq_armAutomationStep(5, NUM_TRACKS, 1);
	ASSERT_FALSE(seq_isAutomationStepArmed());
	seq_recordAutomation(0, 10, 11);
	ASSERT_EQ(memcmp(&nextPattern, &seq_patternSet.seq_subStepPattern[seq_activePattern + 1][0][5], sizeof nextPattern), 0);
}

TEST_MAIN()
