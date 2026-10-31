/*
 * test_buttons.c
 *
 * Host test of the front-panel button logic (Core/Src/buttonHandler.c, task
 * E2.7). The firmware file is compiled as it is, against the stand-in headers in
 * tests/stubs/ and the stand-in functions below, which record what it asks the
 * menu, the LEDs and the sequencer to do. The test presses buttons the way the
 * panel scan (IO/DIO.c) does: the input bit first, then the handler call.
 *
 * fw 1.7 put SEQ_CONFIG (11, a step button number) into the button mode when
 * SHIFT + step 12 opened the global menu. 11 is also SELECT_MODE_LAST_STEP_ALL,
 * so a step button pressed without SHIFT there set the length of every track.
 *
 * SHIFT + a select button held in voice mode arms automation step
 * selectedStep * 8 + button (E4.6). fw 1.7 armed it also past the 64 steps of a
 * track, up to 127: it played the lock of a step of the next track while the
 * button was held, and a knob turn wrote that step.
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
#include "frontPanelParser_ui.h"
#include "IO/DIO.h"
#include "ledHandler.h"
#include "menu/copyClearTools.h"
#include "menu/menu.h"
#include "menu/screensaver.h"
#include "Preset/PresetManager.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/globals.h"
#include "Soundengine/Sequencer/sequencer.h"

//------------------------------------------------------------------------------
// What the test looks at
//------------------------------------------------------------------------------
static uint8_t trackLength[NUM_TRACKS];	/* seq_setTrackLength() / seq_getTrackLength() */
static int setTrackLengthCalls;
static int toggleMainStepCalls;
static int updateSeqLedsCalls;
static int switchSubPageCalls;
static uint8_t lastSubPage;		/* LICHOMAT v0.4: menu_switchSubPage(), read back by menu_getSubPage() */
static uint8_t blinking[256];		/* led_setBlinkLed() per LED number */
static int armCalls;			/* ARM_AUTOMATION_STEP sent with the arm bit */
static uint8_t armedStep;		/* the step of the last one */

//------------------------------------------------------------------------------
// Firmware state that buttonHandler.c reads and writes
//------------------------------------------------------------------------------
uint8_t din_inputData[NUM_INPUTS / 8];
volatile uint32_t systick_ticks;
uint8_t activeParameters[NUM_PARAMS];
uint8_t menu_activePage;
uint8_t menu_activeVoice;
uint8_t menu_activeBar;
uint8_t menu_activePatternBank;
uint8_t menu_muteModeActive;
uint8_t menu_selectedPresetNr[NUM_PRESET_LOCATIONS];
uint32_t sucessTimer;
uint8_t copyClear_Mode;
uint8_t seq_activePattern;
PatternSet seq_patternSet;
FxBus fxBus;
TIM_HandleTypeDef htim2;
DWT_Type host_dwt;

//------------------------------------------------------------------------------
// Stand-ins for the functions it calls
//------------------------------------------------------------------------------
void seq_setTrackLength(uint8_t trackNr, uint8_t length)
{
	setTrackLengthCalls++;
	if (trackNr < NUM_TRACKS)
		trackLength[trackNr] = length;
}
uint8_t seq_getTrackLength(uint8_t trackNr) { return trackNr < NUM_TRACKS ? trackLength[trackNr] : 16; }
void seq_toggleMainStep(uint8_t voice, uint8_t stepNr, uint8_t patternNr)
{
	(void)voice; (void)stepNr; (void)patternNr;
	toggleMainStepCalls++;
}
uint8_t seq_isMainStepActive(uint8_t voice, uint8_t mainStepNr, uint8_t pattern)
{
	(void)voice; (void)mainStepNr; (void)pattern;
	return 0;
}
uint8_t seq_getStepIndex(uint8_t trackNr) { (void)trackNr; return 255; }
void seq_addPatternToSong(uint8_t patternNr) { (void)patternNr; }
void seq_deleteRow(void) {}
void seq_insertRow(void) {}
void seq_setSongModeEnabled(uint8_t onOff) { (void)onOff; }
PatternSet *get_seq_patternSet_p(void) { return &seq_patternSet; }

void menu_switchPage(uint8_t pageNr) { menu_activePage = pageNr; }
void menu_switchSubPage(uint8_t subPageNr, uint8_t toggle) { (void)toggle; lastSubPage = subPageNr; switchSubPageCalls++; }
uint8_t menu_getSubPage(void) { return lastSubPage; }
uint8_t menu_getActivePage(void) { return menu_activePage; }
uint8_t menu_getActiveVoice(void) { return menu_activeVoice; }
void menu_setActiveVoice(uint8_t voiceNr) { menu_activeVoice = voiceNr; }
uint8_t menu_getViewedPattern(void) { return 0; }
void menu_setShownPattern(uint8_t patternNr) { (void)patternNr; }
void menu_setLoadSaveCategory(uint8_t cat) { (void)cat; }
void menu_parseGlobalParam(uint16_t paramNr, uint8_t value) { (void)paramNr; (void)value; }
void menu_repaint(void) {}
void menu_repaintAll(void) {}
void menu_resetActiveParameter(void) {}

void led_setBlinkLed(const uint8_t ledNr, const uint8_t onOff) { blinking[ledNr] = onOff; }
void led_clearAllBlinkLeds(void) { memset(blinking, 0, sizeof blinking); }
void led_updateSeqLeds(void) { updateSeqLedsCalls++; }
uint8_t led_idx2StepLed(uint8_t idx)
{
	static const uint8_t leds[16] = { LED_STEP1, LED_STEP2, LED_STEP3, LED_STEP4, LED_STEP5, LED_STEP6,
		LED_STEP7, LED_STEP8, LED_STEP9, LED_STEP10, LED_STEP11, LED_STEP12, LED_STEP13, LED_STEP14,
		LED_STEP15, LED_STEP16 };
	return leds[idx % 16];
}
void led_clearSelectLeds(void) {}
void led_clearSequencerLeds(void) {}
void led_clearVoiceLeds(void) {}
void led_initPatternSelectLeds(void) {}
void led_setActiveSelectButton(uint8_t butNr) { (void)butNr; }
void led_setActiveVoice(uint8_t voiceNr) { (void)voiceNr; }
void led_setActiveVoiceLeds(uint8_t pattern) { (void)pattern; }
void led_setMode2(uint8_t status) { (void)status; }
static uint8_t ledValue[256];		/* led_setValue() per LED number */
void led_setValue(uint8_t val, uint8_t ledNr) { ledValue[ledNr] = val; }
void led_updateBarLeds(void) {}

void frontPanel_sendData(uint8_t status, uint8_t data1, uint8_t data2)
{
	if (status == ARM_AUTOMATION_STEP && (data2 & 0x40)) {	/* buttonHandler.c ARM_AUTOMATION */
		armCalls++;
		armedStep = data1;
	}
}
void copyClear_armClearMenu(uint8_t isShown) { (void)isShown; }
void copyClear_copyTrack(void) {}
void copyClear_executeClear(void) {}
void copyClear_reset(void) {}
void copyClear_setDst(int8_t dst, uint8_t type) { (void)dst; (void)type; }
void copyClear_setSrc(int8_t src, uint8_t type) { (void)src; (void)type; }
uint8_t copyClear_srcSet(void) { return 0; }
void preset_loadDrumsetRam(uint8_t presetNr, uint8_t isMorph, uint8_t sendImmediate)
{
	(void)presetNr; (void)isMorph; (void)sendImmediate;
}
FRESULT preset_savePatternRAM(uint8_t presetNr) { (void)presetNr; return FR_OK; }
FRESULT preset_savePatternSD(uint8_t presetNr) { (void)presetNr; return FR_OK; }
void preset_touchTimer(PresetTimer type) { (void)type; }
void screensaver_touch(void) {}
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim) { (void)htim; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim) { (void)htim; return HAL_OK; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return 216000000u; }

//------------------------------------------------------------------------------
// Pressing buttons
//------------------------------------------------------------------------------
/** voice mode on the page of voice 1, every track 16 steps, nothing pressed */
static void reset(void)
{
	memset(din_inputData, 0, sizeof din_inputData);
	memset(trackLength, 16, sizeof trackLength);
	memset(blinking, 0, sizeof blinking);
	setTrackLengthCalls = 0;
	toggleMainStepCalls = 0;
	updateSeqLedsCalls = 0;
	switchSubPageCalls = 0;
	lastSubPage = 0;
	armCalls = 0;
	armedStep = 0;
	menu_activePage = VOICE1_PAGE;
	menu_activeVoice = 0;
	menu_activeBar = 0;
	copyClear_Mode = MODE_NONE;
	buttonHandler_setMode(SELECT_MODE_VOICE);
}

/* the panel scan updates din_inputData first; BUT_PAGE_DOWN has no input bit */
static void down(uint8_t button)
{
	if (button < NUM_INPUTS)
		din_inputData[button / 8] |= (uint8_t)(1u << (button & 7));
	buttonHandler_buttonPressed(button);
}

static void up(uint8_t button)
{
	if (button < NUM_INPUTS)
		din_inputData[button / 8] &= (uint8_t)~(1u << (button & 7));
	buttonHandler_buttonReleased(button);
}

static void press(uint8_t button)
{
	down(button);
	up(button);
}

/** press `button` while `modifier` (SHIFT, LOAD or SAVE) is held */
static void pressWith(uint8_t modifier, uint8_t button)
{
	down(modifier);
	press(button);
	up(modifier);
}

//------------------------------------------------------------------------------
// The global menu (SHIFT + step 12)
//------------------------------------------------------------------------------
TEST(buttons_global_menu_step_keeps_track_lengths)
{
	reset();
	pressWith(BUT_SHIFT, BUT_SEQ12);
	ASSERT_EQ(menu_activePage, MENU_MIDI_PAGE);
	ASSERT_TRUE(blinking[LED_STEP12]);

	press(BUT_SEQ5);			/* fw 1.7: every track became 5 steps long */
	ASSERT_EQ(setTrackLengthCalls, 0);
	for (int t = 0; t < NUM_TRACKS; t++)
		ASSERT_EQ(trackLength[t], 16);
	ASSERT_EQ(toggleMainStepCalls, 0);	/* and no step is set either */
	ASSERT_EQ(menu_activePage, MENU_MIDI_PAGE);
	ASSERT_TRUE(blinking[LED_STEP12]);
	ASSERT_EQ(buttonHandler_getMode(), SELECT_MODE_MENU);
}

TEST(buttons_global_menu_page_buttons_keep_the_menu_led)
{
	reset();
	pressWith(BUT_SHIFT, BUT_SEQ12);
	press(BUT_PAGE_UP);			/* fw 1.7 redrew the LEDs of the Last Step page */
	ASSERT_EQ(menu_activeBar, 1);
	ASSERT_EQ(updateSeqLedsCalls, 1);
	ASSERT_TRUE(blinking[LED_STEP12]);
	press(BUT_PAGE_DOWN);
	ASSERT_EQ(menu_activeBar, 0);
	ASSERT_EQ(updateSeqLedsCalls, 2);
	ASSERT_TRUE(blinking[LED_STEP12]);
	ASSERT_EQ(menu_activePage, MENU_MIDI_PAGE);
	ASSERT_EQ(setTrackLengthCalls, 0);
}

TEST(buttons_global_menu_other_buttons_stay_in_the_menu)
{
	reset();
	pressWith(BUT_SHIFT, BUT_SEQ12);
	press(BUT_SELECT1);			/* select buttons do nothing here */
	ASSERT_EQ(menu_activePage, MENU_MIDI_PAGE);
	ASSERT_EQ(switchSubPageCalls, 0);
	press(BUT_VOICE_3);			/* a voice button selects the voice, the menu stays */
	ASSERT_EQ(menu_activeVoice, 2);
	ASSERT_EQ(menu_activePage, MENU_MIDI_PAGE);
	pressWith(BUT_SHIFT, BUT_SEQ12);	/* the next page of the menu (menu.c) */
	ASSERT_EQ(menu_activePage, MENU_MIDI_PAGE);
	ASSERT_EQ(buttonHandler_getMode(), SELECT_MODE_MENU);
	press(BUT_SEQ16);
	ASSERT_EQ(setTrackLengthCalls, 0);
	ASSERT_EQ(toggleMainStepCalls, 0);
}

TEST(buttons_voice_mode_after_the_global_menu)
{
	reset();
	press(BUT_SEQ5);			/* voice mode: a step button sets a step */
	ASSERT_EQ(toggleMainStepCalls, 1);
	pressWith(BUT_SHIFT, BUT_SEQ12);
	press(BUT_MODE1);			/* back to the voice pages */
	ASSERT_EQ(menu_activePage, VOICE1_PAGE);
	ASSERT_EQ(buttonHandler_getMode(), SELECT_MODE_VOICE);
	press(BUT_SEQ5);
	ASSERT_EQ(toggleMainStepCalls, 2);
	ASSERT_EQ(setTrackLengthCalls, 0);
}

TEST(buttons_load_or_save_with_step_12_does_nothing)
{
	reset();
	pressWith(BUT_MODE3, BUT_SEQ12);	/* LOAD + step 12 */
	pressWith(BUT_MODE4, BUT_SEQ12);	/* SAVE + step 12 */
	ASSERT_EQ(menu_activePage, VOICE1_PAGE);
	ASSERT_EQ(buttonHandler_getMode(), SELECT_MODE_VOICE);
	ASSERT_EQ(setTrackLengthCalls, 0);
	ASSERT_EQ(toggleMainStepCalls, 0);
}

//------------------------------------------------------------------------------
// The Last Step page (SHIFT + step 14) keeps its two modes
//------------------------------------------------------------------------------
TEST(buttons_last_step_track_sets_the_active_track)
{
	reset();
	menu_activeVoice = 2;
	pressWith(BUT_SHIFT, BUT_SEQ14);
	ASSERT_EQ(menu_activePage, MENU_LAST_STEP_PAGE);
	ASSERT_EQ(buttonHandler_getMode(), SELECT_MODE_LAST_STEP_TRACK);
	press(BUT_SEQ5);
	ASSERT_EQ(setTrackLengthCalls, 1);
	for (int t = 0; t < NUM_TRACKS; t++)
		ASSERT_EQ(trackLength[t], t == 2 ? 5 : 16);
}

TEST(buttons_last_step_all_sets_every_track)
{
	reset();
	pressWith(BUT_SHIFT, BUT_SEQ14);
	pressWith(BUT_SHIFT, BUT_SEQ14);	/* the second press: all tracks */
	ASSERT_EQ(buttonHandler_getMode(), SELECT_MODE_LAST_STEP_ALL);
	press(BUT_SEQ5);
	ASSERT_EQ(setTrackLengthCalls, NUM_TRACKS);
	for (int t = 0; t < NUM_TRACKS; t++)
		ASSERT_EQ(trackLength[t], 5);
}

TEST(buttons_last_step_from_the_global_menu_starts_with_one_track)
{
	reset();
	pressWith(BUT_SHIFT, BUT_SEQ12);
	pressWith(BUT_SHIFT, BUT_SEQ14);	/* as in fw 1.7: the active track first */
	ASSERT_EQ(menu_activePage, MENU_LAST_STEP_PAGE);
	ASSERT_EQ(buttonHandler_getMode(), SELECT_MODE_LAST_STEP_TRACK);
	press(BUT_SEQ3);
	ASSERT_EQ(setTrackLengthCalls, 1);
	ASSERT_EQ(trackLength[0], 3);
	ASSERT_EQ(trackLength[1], 16);
}

//------------------------------------------------------------------------------
// SHIFT + a held select button arms an automation step (E4.6)
//------------------------------------------------------------------------------
/** no parameter locks in the first two tracks of pattern 0, as after a clear */
static void clearLocks(void)
{
	for (int t = 0; t < 2; t++)
		for (int s = 0; s < NUM_STEPS; s++) {
			seq_patternSet.seq_subStepPattern[0][t][s].param1Nr = NO_AUTOMATION;
			seq_patternSet.seq_subStepPattern[0][t][s].param2Nr = NO_AUTOMATION;
		}
}

/** hold SHIFT and select button `select` for longer than BUTTON_TIMEOUT */
static void holdShiftAndSelect(uint8_t select)
{
	down(BUT_SHIFT);
	down(select);
	systick_ticks += BUTTON_TIMEOUT + 1;
	buttonHandler_tick();
}

static void releaseShiftAndSelect(uint8_t select)
{
	up(select);
	up(BUT_SHIFT);
}

TEST(buttons_automation_step_in_range)
{
	/* step 1 selected: select 3 arms step 3 of the voice, as in fw 1.7 (step 1 * 8
	 * + 2, a leftover of 8 sub-steps per step), and plays its lock while held */
	reset();
	clearLocks();
	press(BUT_SEQ1);
	ASSERT_EQ(buttonHandler_selectedStep, 0);
	seq_patternSet.seq_subStepPattern[0][0][2].param1Nr = 10;	/* parameter 9 */
	seq_patternSet.seq_subStepPattern[0][0][2].param1Val = 99;
	activeParameters[9] = 3;
	holdShiftAndSelect(BUT_SELECT3);
	ASSERT_EQ(armCalls, 1);
	ASSERT_EQ(armedStep, 2);
	ASSERT_EQ(activeParameters[9], 99);
	releaseShiftAndSelect(BUT_SELECT3);
	ASSERT_EQ(activeParameters[9], 3);
}

TEST(buttons_automation_step_past_63_is_not_armed)
{
	/* step 10 selected: fw 1.7 armed step 9 * 8 + 7 = 79, past the 64 steps of the
	 * track, which is step 16 of the next track in memory: it played that step's
	 * lock while held and a knob turn wrote that step */
	reset();
	clearLocks();
	press(BUT_SEQ10);
	ASSERT_EQ(buttonHandler_selectedStep, 9);
	seq_patternSet.seq_subStepPattern[0][1][15].param1Nr = 10;
	seq_patternSet.seq_subStepPattern[0][1][15].param1Val = 99;
	activeParameters[9] = 3;
	holdShiftAndSelect(BUT_SELECT8);
	ASSERT_EQ(armCalls, 0);
	ASSERT_EQ(activeParameters[9], 3);
	releaseShiftAndSelect(BUT_SELECT8);
	ASSERT_EQ(activeParameters[9], 3);
	ASSERT_EQ(armCalls, 0);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.2 (plan V9): SHIFT + step 1 and 2 rotate the selected track
//------------------------------------------------------------------------------
static int rotateCalls;
static uint8_t rotateTrack, rotatePattern;
static int8_t rotateDir;
void seq_rotateTrack(uint8_t trackNr, uint8_t pattern, int8_t dir)
{
	rotateCalls++;
	rotateTrack = trackNr;
	rotatePattern = pattern;
	rotateDir = dir;
}

TEST(buttons_shift_step1_and_step2_rotate_the_active_track)
{
	reset();
	rotateCalls = 0;
	menu_activeVoice = 3;
	pressWith(BUT_SHIFT, BUT_SEQ1);
	ASSERT_EQ(rotateCalls, 1);
	ASSERT_EQ(rotateTrack, 3);
	ASSERT_EQ(rotatePattern, 0);
	ASSERT_EQ(rotateDir, -1);
	pressWith(BUT_SHIFT, BUT_SEQ2);
	ASSERT_EQ(rotateCalls, 2);
	ASSERT_EQ(rotateDir, 1);
	ASSERT_EQ(toggleMainStepCalls, 0);	/* no step changed */
	ASSERT_TRUE(updateSeqLedsCalls >= 2);	/* the step LEDs show the result */
	press(BUT_SEQ1);			/* without SHIFT: a step, no rotation */
	ASSERT_EQ(rotateCalls, 2);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.3 (plan P5, P6): fill and mute scenes in the performance mode
//------------------------------------------------------------------------------
static int fillCalls, recallCalls, storeCalls;
static uint8_t fillOn, recallScene, storeScene;
void seq_setFill(uint8_t on) { fillCalls++; fillOn = on; }
uint8_t seq_recallMuteScene(uint8_t scene) { recallCalls++; recallScene = scene; return 1; }
void seq_storeMuteScene(uint8_t scene) { storeCalls++; storeScene = scene; }
static uint8_t appliedSerial;		/* seq_appliedMuteScene(): the count of applied scenes */
uint8_t seq_appliedMuteScene(uint8_t *mutes) { *mutes = 0; return appliedSerial; }

static void enterPerf(void)
{
	reset();
	press(BUT_MODE2);
	fillCalls = recallCalls = storeCalls = 0;
}

TEST(buttons_perf_step8_holds_fill)
{
	enterPerf();
	ASSERT_EQ(buttonHandler_getMode(), SELECT_MODE_PERF);	/* the state struct is static */
	down(BUT_SEQ8);
	ASSERT_EQ(fillCalls, 1);
	ASSERT_EQ(fillOn, 1);
	up(BUT_SEQ8);
	ASSERT_EQ(fillCalls, 2);
	ASSERT_EQ(fillOn, 0);
	down(BUT_SEQ8); down(BUT_SHIFT); up(BUT_SEQ8);	/* SHIFT while held: the release ends it */
	ASSERT_EQ(fillOn, 0);
	up(BUT_SHIFT);
	down(BUT_SEQ8); down(BUT_MODE3); up(BUT_SEQ8);	/* LOAD too */
	ASSERT_EQ(fillOn, 0);
	up(BUT_MODE3);
	ASSERT_EQ(fillCalls, 6);
}

TEST(buttons_voice_mode_step8_is_no_fill)
{
	enterPerf();
	press(BUT_MODE1);
	press(BUT_SEQ8);
	ASSERT_EQ(fillCalls, 0);
}

TEST(buttons_perf_steps_9_to_16_recall_and_copy_stores)
{
	enterPerf();
	press(BUT_SEQ9);
	ASSERT_EQ(recallCalls, 1);
	ASSERT_EQ(recallScene, 0);
	press(BUT_SEQ16);
	ASSERT_EQ(recallCalls, 2);
	ASSERT_EQ(recallScene, 7);
	pressWith(BUT_COPY, BUT_SEQ11);
	ASSERT_EQ(storeCalls, 1);
	ASSERT_EQ(storeScene, 2);
	ASSERT_EQ(recallCalls, 2);
	ASSERT_EQ(copyClear_Mode, MODE_NONE);
	press(BUT_SEQ8);				/* fill, not a scene */
	ASSERT_EQ(recallCalls, 2);
	ASSERT_EQ(storeCalls, 1);
}

// the P5-P8 review: the release of step 8 in another mode ends fill, but leaves that mode's
// step 8 LED alone
TEST(buttons_fill_release_in_voice_mode_keeps_the_step_led)
{
	enterPerf();
	down(BUT_SEQ8);
	ASSERT_EQ(fillOn, 1);
	press(BUT_MODE1);
	ledValue[LED_STEP8] = 0x55;
	up(BUT_SEQ8);
	ASSERT_EQ(fillOn, 0);
	ASSERT_EQ(ledValue[LED_STEP8], 0x55);
}

// the P5-P8 review: a scene that applies in another mode (here the FX mode, which blinks step
// 15's LED itself) stops no blink; back in the performance mode a waiting recall blinks again
TEST(buttons_scene_apply_leaves_other_modes_blinking)
{
	enterPerf();
	press(BUT_SEQ15);
	ASSERT_EQ(blinking[LED_STEP15], 1);
	pressWith(BUT_SHIFT, BUT_SEQ15);		/* the FX mode */
	ASSERT_EQ(blinking[LED_STEP15], 1);
	appliedSerial++;
	buttonHandler_tick();
	ASSERT_EQ(blinking[LED_STEP15], 1);
	press(BUT_MODE2);
	ASSERT_EQ(blinking[LED_STEP15], 0);		/* applied: no blink */
}

TEST(buttons_scene_blink_back_after_a_mode_change)
{
	enterPerf();
	press(BUT_SEQ10);
	ASSERT_EQ(blinking[LED_STEP10], 1);
	press(BUT_MODE1);
	press(BUT_MODE2);
	ASSERT_EQ(blinking[LED_STEP10], 1);		/* still waiting */
	appliedSerial++;
	buttonHandler_tick();
	ASSERT_EQ(blinking[LED_STEP10], 0);
}

TEST(buttons_voice_mode_steps_do_not_recall_scenes)
{
	enterPerf();
	press(BUT_MODE1);
	press(BUT_SEQ9);
	ASSERT_EQ(recallCalls, 0);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.4 (plan A4, D37): SHIFT + step 15 on the FX page steps the slot's
// sub-page -> reverb (6) -> rumble (7) -> the slot's
//------------------------------------------------------------------------------
TEST(buttons_fx_page_cycles_reverb)
{
	reset();
	fxBus.fxType = 4;
	pressWith(BUT_SHIFT, BUT_SEQ15);		/* fw 1.7: the slot's sub-page */
	ASSERT_EQ(menu_activePage, FX_MODE_PAGE);
	ASSERT_EQ(lastSubPage, 4);
	pressWith(BUT_SHIFT, BUT_SEQ15);
	ASSERT_EQ(lastSubPage, 6);
	pressWith(BUT_SHIFT, BUT_SEQ15);
	ASSERT_EQ(lastSubPage, 7);
	pressWith(BUT_SHIFT, BUT_SEQ15);
	ASSERT_EQ(lastSubPage, 4);
	ASSERT_EQ(menu_activePage, FX_MODE_PAGE);
	ASSERT_EQ(buttonHandler_getMode(), SELECT_MODE_FX);
	ASSERT_TRUE(blinking[LED_STEP15]);
	pressWith(BUT_SHIFT, BUT_SEQ15);
	ASSERT_EQ(lastSubPage, 6);
	press(BUT_SELECT1);				/* a select button still returns to voice mode */
	ASSERT_EQ(buttonHandler_getMode(), SELECT_MODE_VOICE);
	ASSERT_TRUE(menu_activePage != FX_MODE_PAGE);
	fxBus.fxType = 0;
}

TEST_MAIN()
