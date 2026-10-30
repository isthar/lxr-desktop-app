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
 * The probability draw (stage 1b F5): seq_cbRandom() returns SEQ_RANDOM(), which
 * a host build sets to a random stream of its own. stubs/seq_random_test.h, which
 * tests/CMakeLists.txt forces into sequencer.c, makes it seqTest_random() here.
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
#include "seq_random_test.h"

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
static int chaseCalls;			/* led_setActive_step() */
static uint8_t chaseStep[64];
static int noteOns;			/* voiceControl_noteOn() */
static uint8_t noteOnVoice[64];
static seqc_time_t noteOnAt[64];
void led_setActive_step(uint8_t stepNr)
{
	if (chaseCalls < 64)
		chaseStep[chaseCalls] = stepNr;
	chaseCalls++;
}
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
static int slideCalls;			/* Drum_setSlide() (LICHOMAT v0.3, plan P4) */
static uint8_t slideVoice[64], slideOn[64];
static int slideAtNoteOn[64];		/* noteOns at the call */
void Drum_setSlide(const uint8_t voiceNr, const uint8_t on)
{
	if (slideCalls < 64)
	{
		slideVoice[slideCalls] = voiceNr;
		slideOn[slideCalls] = on;
		slideAtNoteOn[slideCalls] = noteOns;
	}
	slideCalls++;
}
void voiceControl_noteOff(uint8_t voice) { (void)voice; }
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel)
{
	(void)note; (void)vel;
	if (noteOns < 64)
	{
		noteOnVoice[noteOns] = voice;
		noteOnAt[noteOns] = hostNow;
	}
	noteOns++;
}

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

//------------------------------------------------------------------------------
// LICHOMAT v0.2 (plan V1): the per-track settings follow the pattern operations
//------------------------------------------------------------------------------
TEST(sequencer_groove_follows_copy_clear_and_backup)
{
	reset();
	static const TrackGroove none[NUM_TRACKS];
	const TrackGroove g = { .tempo = -1, .humTime = 4, .humVol = 10, .delay = -3 };
	seq_patternSet.groove[3][2] = g;
	seq_copyTrack(2, 5, 3);
	ASSERT_EQ(memcmp(&seq_patternSet.groove[3][5], &g, sizeof g), 0);
	seq_copyPattern(3, 7);
	ASSERT_EQ(memcmp(seq_patternSet.groove[7], seq_patternSet.groove[3], sizeof none), 0);
	seq_copyPatternToBackupBuffer(3);
	memset(seq_patternSet.groove[3], 0, sizeof none);
	seq_copyBackupBufferToPattern(3);
	ASSERT_EQ(memcmp(&seq_patternSet.groove[3][2], &g, sizeof g), 0);
	seq_clearTrack(2, 3);
	ASSERT_EQ(memcmp(&seq_patternSet.groove[3][2], &none[0], sizeof g), 0);
	ASSERT_EQ(memcmp(&seq_patternSet.groove[3][5], &g, sizeof g), 0);	/* the other track stays */
	seq_clearPattern(7);
	ASSERT_EQ(memcmp(seq_patternSet.groove[7], none, sizeof none), 0);
}

//------------------------------------------------------------------------------
// The probability draw (stage 1b F5): seq_cbRandom() returns SEQ_RANDOM(), which
// stubs/seq_random_test.h makes seqTest_random() for this target. The firmware
// keeps GetRngValue(), the hardware RNG.
//------------------------------------------------------------------------------
static int seqTestRandomCalls;

uint32_t seqTest_random(void)
{
	seqTestRandomCalls++;
	return 0;
}

TEST(sequencer_probability_draw_takes_seq_random)
{
	reset();
	seq_setMainStep(0, 0, 0, 1);		/* pattern 0, track 1, step 1 */
	seqTestRandomCalls = 0;
	playAndStop(500);
	seq_setMainStep(0, 0, 0, 0);
	ASSERT_TRUE(seqTestRandomCalls > 0);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.2 (plan V6): a track at x3/4 through the glue. The start is applied
// by the seq_tick() 1 ms after seq_setRunning(1); a step of the track is 166.667 ms.
//------------------------------------------------------------------------------
/** the ms after start + 1 of voice's Note Ons */
static int voiceOns(uint8_t voice, seqc_time_t start, double *out, int max)
{
	int n = 0;
	for (int i = 0; i < noteOns && i < 64; i++)
		if (noteOnVoice[i] == voice && n < max)
			out[n++] = (double)(noteOnAt[i] - start) / MS - 1.0;
	return n;
}

TEST(sequencer_track_tempo_steps_and_chase_light)
{
	reset();
	seq_patternSet.groove[0][1].tempo = -1;		/* x3/4 */
	for (uint8_t s = 0; s < 3; s++)
		seq_setMainStep(0, 1, s, 1);
	frontParser_activeTrack = 1;
	noteOns = chaseCalls = 0;
	const seqc_time_t start = hostNow;
	seq_setRunning(1);
	run(250);
	ASSERT_EQ(seq_getStepIndex(1), 1);		/* own step 1: 166.7 .. 333.3 ms */
	ASSERT_EQ(seq_getStepIndex(0), 1);		/* master step 1: 125 .. 250 ms */
	run(150);
	double at[8];
	ASSERT_EQ(voiceOns(1, start, at, 8), 3);
	static const double want[3] = { 0.0, 166.667, 333.333 };
	for (int k = 0; k < 3; k++)
		ASSERT_TRUE(at[k] >= want[k] - 0.01 && at[k] < want[k] + 1.0);
	/* the chase light follows the track's own steps: 0 at the master boundary 0, then
	 * 1 and 2 at its own boundaries, none at the master boundaries 1 to 3 */
	ASSERT_EQ(chaseCalls, 3);
	for (int k = 0; k < 3; k++)
		ASSERT_EQ(chaseStep[k], k);
	seq_setRunning(0);
	run(1);
}

TEST(sequencer_recording_quantizes_to_the_tracks_own_steps)
{
	reset();
	seq_patternSet.groove[0][1].tempo = -1;		/* x3/4 */
	seq_setMainStep(0, 1, 0, 1);
	seq_setMainStep(0, 1, 1, 1);
	seq_setRecordingMode(1);
	noteOns = 0;
	const seqc_time_t start = hostNow;
	seq_setRunning(1);
	run(278);		/* own step 1 began at 167.7 ms: its phase is 0.66 */
	seq_addNote(1, 100, 64);
	ASSERT_TRUE(seq_isMainStepActive(1, 2, 0));	/* own step 2, not master step 2 */
	run(122);		/* step 2 at 334.3 ms is skipped once */
	double at[8];
	ASSERT_EQ(voiceOns(1, start, at, 8), 2);
	seq_setRecordingMode(0);
	seq_setRunning(0);
	run(1);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.2 (plan V9): rotation within the track length
//------------------------------------------------------------------------------
TEST(sequencer_rotate_moves_steps_within_the_length)
{
	reset();
	const uint8_t p = 4, t = 2;
	seq_patternSet.length[p][t] = 5;
	for (uint8_t s = 0; s < NUM_STEPS; s++)
	{
		seq_patternSet.seq_subStepPattern[p][t][s] = (Step){ .volume = (uint8_t)(0x80 | s), .prob = (uint8_t)(101 + s % 15),
			.note = (uint8_t)(30 + s), .param1Nr = s, .param1Val = (uint8_t)(2 * s), .param2Nr = (uint8_t)(s + 1),
			.param2Val = (uint8_t)(3 * s) };
		seq_patternSet.seq_stepTiming[p][t][s] = (StepTiming){ .flam = (uint8_t)(s % 6), .shift = (uint8_t)(s % 15) };
		seq_setMainStep(p, t, s, (uint8_t)(s % 3 == 0));
	}
	static Step steps[NUM_STEPS];
	static StepTiming timing[NUM_STEPS];
	uint8_t on[NUM_STEPS];
	memcpy(steps, seq_patternSet.seq_subStepPattern[p][t], sizeof steps);
	memcpy(timing, seq_patternSet.seq_stepTiming[p][t], sizeof timing);
	for (uint8_t s = 0; s < NUM_STEPS; s++)
		on[s] = seq_isMainStepActive(t, s, p);
	seq_rotateTrack(t, p, 1);		/* later: step s takes step s - 1, step 0 takes step 4 */
	for (uint8_t s = 0; s < NUM_STEPS; s++)
	{
		const uint8_t from = s < 5 ? (uint8_t)((s + 4) % 5) : s;
		ASSERT_EQ(memcmp(&seq_patternSet.seq_subStepPattern[p][t][s], &steps[from], sizeof(Step)), 0);
		ASSERT_EQ(memcmp(&seq_patternSet.seq_stepTiming[p][t][s], &timing[from], 1), 0);
		ASSERT_EQ(seq_isMainStepActive(t, s, p), on[from]);
	}
	seq_rotateTrack(t, p, -1);		/* back */
	ASSERT_EQ(memcmp(seq_patternSet.seq_subStepPattern[p][t], steps, sizeof steps), 0);
	ASSERT_EQ(memcmp(seq_patternSet.seq_stepTiming[p][t], timing, sizeof timing), 0);
	/* raw length 0 is 16 steps; length 1 has nothing to rotate; the next track is untouched */
	seq_patternSet.length[p][t] = 0;
	seq_rotateTrack(t, p, -1);
	ASSERT_EQ(memcmp(&seq_patternSet.seq_subStepPattern[p][t][15], &steps[0], sizeof(Step)), 0);
	ASSERT_EQ(memcmp(&seq_patternSet.seq_subStepPattern[p][t][0], &steps[1], sizeof(Step)), 0);
	ASSERT_EQ(memcmp(&seq_patternSet.seq_subStepPattern[p][t][16], &steps[16], sizeof(Step)), 0);
	seq_patternSet.length[p][3] = 1;
	const Step s3 = seq_patternSet.seq_subStepPattern[p][3][0];
	seq_rotateTrack(3, p, 1);
	ASSERT_EQ(memcmp(&seq_patternSet.seq_subStepPattern[p][3][0], &s3, sizeof s3), 0);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.3 (plan P4): the slide flag and the probability share Step.prob
TEST(sequencer_step_slide_and_probability_keep_each_other)
{
	Step *s = &seq_patternSet.seq_subStepPattern[3][1][5];
	s->prob = 50;
	seq_setStepSlide(3, 1, 5, 1);
	ASSERT_EQ(s->prob, 0xB2);
	seq_setStepProbKeepSlide(3, 1, 5, 110);
	ASSERT_EQ(s->prob, 0xEE);
	seq_setStepSlide(3, 1, 5, 0);
	ASSERT_EQ(s->prob, 110);
	seq_setStepProbKeepSlide(3, 1, 5, 0xC0);	/* only 7 bits of the edit value count */
	ASSERT_EQ(s->prob, 0x40);
	seq_setStepSlide(64, 1, 5, 1);			/* out of range: nothing */
	seq_setStepSlide(3, 7, 5, 1);
	ASSERT_EQ(s->prob, 0x40);
}

// LICHOMAT v0.3 (plan P4): a slide step of track 0 sets Drum voice 0's slide before
// its Note On; a step of track 3 (not a Drum voice) calls nothing
TEST(sequencer_slide_step_arms_the_drum_voice_before_its_note)
{
	reset();
	Step *s0 = &seq_patternSet.seq_subStepPattern[0][0][0];
	Step *s3 = &seq_patternSet.seq_subStepPattern[0][3][0];
	const Step old0 = *s0, old3 = *s3;
	seq_setMainStep(0, 0, 0, 1);
	seq_setMainStep(0, 3, 0, 1);
	s0->volume |= STEP_ACTIVE_MASK;
	s3->volume |= STEP_ACTIVE_MASK;
	s0->prob = 100 | STEP_SLIDE_FLAG;
	s3->prob = 100;
	noteOns = slideCalls = 0;
	seq_setRunning(1);
	run(100);					/* step 0 only: step 1 is at 125 ms */
	seq_setRunning(0);
	run(1);
	int on0 = -1, on3 = -1;
	for (int i = 0; i < noteOns && i < 64; i++)
	{
		if (noteOnVoice[i] == 0 && on0 < 0) on0 = i;
		if (noteOnVoice[i] == 3 && on3 < 0) on3 = i;
	}
	ASSERT_TRUE(on0 >= 0);
	ASSERT_TRUE(on3 >= 0);
	int calls0 = 0;
	for (int i = 0; i < slideCalls && i < 64; i++)
	{
		ASSERT_TRUE(slideVoice[i] < 3);
		if (slideVoice[i] == 0)
		{
			calls0++;
			ASSERT_EQ(slideOn[i], 1);
			ASSERT_EQ(slideAtNoteOn[i], on0);	/* right before voice 0's Note On */
		}
	}
	ASSERT_EQ(calls0, 1);
	seq_setMainStep(0, 0, 0, 0);
	seq_setMainStep(0, 3, 0, 0);
	*s0 = old0;
	*s3 = old3;
}

TEST_MAIN()
