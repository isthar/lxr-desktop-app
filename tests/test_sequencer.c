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
#include "Soundengine/DSPAudio/DrumVoice.h"
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
#include "Soundengine/vsched.h"
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
/* LICHOMAT v0.6 (review of Task 7): seq_addNoteAt() records inside seq_tick() only; the MIDI
 * parser's place there, seq_parseMidiInput(), calls this stand-in, which plays one note */
static struct { bool on, stamped; uint8_t track, vel, note; seqc_time_t t; } tickNote;
void uart_processMidi(uint8_t maxBytes)
{
	(void)maxBytes;
	if (!tickNote.on)
		return;
	tickNote.on = false;
	if (tickNote.stamped)
		seq_addNoteAt(tickNote.track, tickNote.vel, tickNote.note, tickNote.t);
	else
		seq_addNote(tickNote.track, tickNote.vel, tickNote.note);
}
void uart_sendFrontpanelByte(uint8_t data) { (void)data; }
void uart_sendFrontpanelSysExByte(uint8_t data) { (void)data; }
static int midiOns;			/* LICHOMAT v0.7 (plan v0.7 Task 4): the Note Ons given to uart_sendMidi() */
static uint8_t midiOnStatus[64], midiOnVel[64];
void uart_sendMidi(MidiMsg msg)
{
	if ((msg.status & 0xF0) != 0x90)
		return;
	if (midiOns < 64)
	{
		midiOnStatus[midiOns] = msg.status;
		midiOnVel[midiOns] = msg.data2;
	}
	midiOns++;
}
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
static int fillOnNoteOn = -1;		/* >= 0: the next Note On calls seq_setFill() with it, once */
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel)
{
	(void)note; (void)vel;
	if (noteOns < 64)
	{
		noteOnVoice[noteOns] = voice;
		noteOnAt[noteOns] = hostNow;
	}
	noteOns++;
	if (fillOnNoteOn >= 0)
	{
		const int on = fillOnNoteOn;
		fillOnNoteOn = -1;
		seq_setFill((uint8_t)on);
	}
}
static vsched_rec_t posted[64];		/* vsched_post() (LICHOMAT v0.5, plan Task 10) */
static seqc_time_t postedAt[64];
static int posts, noteOnEvents;
void vsched_post(const vsched_rec_t *r)
{
	if (posts < 64)
	{
		posted[posts] = *r;
		postedAt[posts] = hostNow;
	}
	posts++;
}
void voiceControl_noteOnEvent(uint8_t voice) { (void)voice; noteOnEvents++; }

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
 * song: patterns 0, 1, 2, then SONG_END, and guard bytes after it. The backup
 * buffer holds the active pattern, as the boot's project load leaves it
 * (preset_loadProject(), appMain.c): seq_init() does not touch it, and the
 * pattern switch below copies it back into the active pattern, so a stale or
 * zeroed buffer would give pattern 0 lengths of 0 and silent steps. */
/** LICHOMAT v0.6: a note at time t recorded inside one more seq_tick() at the host's time */
static void addNoteAtInTick(uint8_t track, uint8_t vel, uint8_t note, seqc_time_t t)
{
	tickNote.on = true;
	tickNote.stamped = true;
	tickNote.track = track;
	tickNote.vel = vel;
	tickNote.note = note;
	tickNote.t = t;
	seq_tick();
}

/** LICHOMAT v0.6: an unstamped note (seq_addNote()) recorded inside one more seq_tick() */
static void addNoteInTick(uint8_t track, uint8_t vel, uint8_t note)
{
	tickNote.on = true;
	tickNote.stamped = false;
	tickNote.track = track;
	tickNote.vel = vel;
	tickNote.note = note;
	seq_tick();
}

static void reset(void)
{
	activeParameters[PAR_TRIG_TIMING] = 1;	/* LICHOMAT v0.5: blk, v0.4's path, which the cases above were written for (plan P11) */
	seq_init();
	seq_copyPatternToBackupBuffer(seq_activePattern);
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
	addNoteInTick(1, 100, 64);	/* LICHOMAT v0.6: inside seq_tick(), as the MIDI parser */
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

/** the modes of Drum_setSlide() for voice 0 since slideCalls was 0, in order */
static int slideModes0(uint8_t *out, int max)
{
	int n = 0;
	for (int i = 0; i < slideCalls && i < 64; i++)
		if (slideVoice[i] == 0 && n < max)
			out[n++] = slideOn[i];
	return n;
}

/* LICHOMAT v0.3 (review of P3): the later hits of a slide step let its glide go on; the
 * first hit of the next step without the flag ends it */
TEST(sequencer_slide_keeps_gliding_through_its_ratchets)
{
	reset();
	seq_setMainStep(0, 0, 0, 1);
	seq_setMainStep(0, 0, 1, 1);
	seq_patternSet.seq_subStepPattern[0][0][0].prob = 100 | STEP_SLIDE_FLAG;
	seq_patternSet.seq_stepTiming[0][0][0].flam = 1;	/* x2: hit 1 at 62.5 ms */
	slideCalls = 0;
	seq_setRunning(1);
	run(200);					/* step 1 at 125 ms, step 2 at 250 ms */
	seq_setRunning(0);
	run(1);
	uint8_t m[8];
	ASSERT_EQ(slideModes0(m, 8), 3);
	ASSERT_EQ(m[0], DRUM_SLIDE_ON);
	ASSERT_EQ(m[1], DRUM_SLIDE_KEEP);
	ASSERT_EQ(m[2], DRUM_SLIDE_OFF);
}

TEST(sequencer_slide_keeps_gliding_through_its_roll_hits)
{
	reset();
	seq_setMainStep(0, 0, 1, 1);			/* step 1 slides, step 2 is off */
	seq_patternSet.seq_subStepPattern[0][0][1].prob = 100 | STEP_SLIDE_FLAG;
	seq_setRollRate(10);				/* 1/32: hits at 0 and 62.5 ms of each step */
	seq_setRunning(1);
	run(120);
	seq_setRoll(0, 1);				/* from step 1's boundary on */
	slideCalls = 0;
	run(160);					/* to 280 ms */
	seq_setRoll(0, 0);
	seq_setRunning(0);
	run(1);
	seq_setRollRate(8);				/* 1/16, as before */
	uint8_t m[8];
	/* step 1: its hit 0 (the roll hit of the same tick is dropped), the roll hit at 187.5 ms;
	 * step 2: the roll hit at 250 ms */
	ASSERT_EQ(slideModes0(m, 8), 3);
	ASSERT_EQ(m[0], DRUM_SLIDE_ON);
	ASSERT_EQ(m[1], DRUM_SLIDE_KEEP);
	ASSERT_EQ(m[2], DRUM_SLIDE_OFF);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.3 (plan P6): mute scenes
//------------------------------------------------------------------------------
TEST(sequencer_mute_scene_store_and_recall_at_the_bar)
{
	uint8_t m;
	seq_clearMuteScenes();
	seq_setMuteSceneQuant(0);
	seq_setMute(7, 0);			/* unmute all */
	seq_setMute(1, 1);
	seq_setMute(4, 1);
	seq_storeMuteScene(2);
	ASSERT_EQ(seq_muteScenes[2], 0x92);	/* stored, tracks 1 and 4 */
	seq_setMute(7, 0);
	const uint8_t serial0 = seq_appliedMuteScene(&m);
	ASSERT_EQ(seq_recallMuteSceneAt(2, 1), 1);
	ASSERT_EQ(seq_isTrackMuted(1), 0);
	seq_muteSceneBoundary(5);
	seq_muteSceneBoundary(255);		/* SEQC_NO_STEP */
	ASSERT_EQ(seq_isTrackMuted(1), 0);
	seq_muteSceneBoundary(32);
	ASSERT_EQ(seq_isTrackMuted(1), 1);
	ASSERT_EQ(seq_isTrackMuted(4), 1);
	ASSERT_EQ(seq_isTrackMuted(0), 0);
	ASSERT_EQ((uint8_t)(seq_appliedMuteScene(&m) - serial0), 1);
	ASSERT_EQ(m, 0x12);
	seq_muteSceneBoundary(48);		/* nothing waits */
	ASSERT_EQ((uint8_t)(seq_appliedMuteScene(&m) - serial0), 1);
	seq_setMute(7, 0);
}

// LICHOMAT v0.3 (the P5-P8 review): seq_setFill() posts to the mailbox, so a call made while
// the core runs (here from a Note On inside seq_tick(); on the device the main loop that the
// audio interrupt's seq_tick() preempts) is applied by the next seq_tick(): the FILL step plays
// and the core counts no reentry
TEST(sequencer_fill_set_while_the_core_runs_applies)
{
	reset();
	Step *s0 = &seq_patternSet.seq_subStepPattern[0][0][0];
	Step *s4 = &seq_patternSet.seq_subStepPattern[0][0][32];
	const Step old0 = *s0, old4 = *s4;
	seq_setMainStep(0, 0, 0, 1);
	seq_setMainStep(0, 0, 4, 1);
	s0->volume |= STEP_ACTIVE_MASK;
	s4->volume |= STEP_ACTIVE_MASK;
	s0->prob = 100;
	s4->prob = STEP_COND_FILL;
	seqc_stats_t st0, st1;
	seqc_stats(&st0);
	noteOns = 0;
	fillOnNoteOn = 1;				/* step 0's Note On turns fill on */
	seq_setRunning(1);
	run(600);					/* steps 0 to 4; step 4 at 500 ms */
	seq_setFill(0);
	seq_setRunning(0);
	run(1);
	fillOnNoteOn = -1;
	int ons0 = 0;
	for (int i = 0; i < noteOns && i < 64; i++)
		if (noteOnVoice[i] == 0)
			ons0++;
	seqc_stats(&st1);
	ASSERT_EQ(st1.reentries, st0.reentries);
	ASSERT_EQ(ons0, 2);				/* step 0 and the FILL step 4 */
	seq_setMainStep(0, 0, 0, 0);
	seq_setMainStep(0, 0, 4, 0);
	*s0 = old0;
	*s4 = old4;
}

// LICHOMAT v0.3 (the P5-P8 review): clearing the scenes (a new or loaded project) drops a
// waiting recall; the panel sees it as applied with the mutes unchanged, so its LED stops
TEST(sequencer_mute_scene_clear_drops_a_waiting_recall)
{
	uint8_t m;
	seq_clearMuteScenes();
	seq_setMuteSceneQuant(0);
	seq_setMute(7, 0);
	seq_setMute(2, 1);
	seq_storeMuteScene(0);
	seq_setMute(7, 0);
	seq_setMute(5, 1);
	const uint8_t serial0 = seq_appliedMuteScene(&m);
	ASSERT_EQ(seq_recallMuteSceneAt(0, 1), 1);
	seq_clearMuteScenes();
	ASSERT_EQ((uint8_t)(seq_appliedMuteScene(&m) - serial0), 1);
	ASSERT_EQ(m, 0x20);				/* track 5, as before */
	seq_muteSceneBoundary(0);			/* nothing waits any more */
	ASSERT_EQ(seq_isTrackMuted(2), 0);
	ASSERT_EQ(seq_isTrackMuted(5), 1);
	ASSERT_EQ((uint8_t)(seq_appliedMuteScene(&m) - serial0), 1);
	seq_clearMuteScenes();				/* nothing waits: no count */
	ASSERT_EQ((uint8_t)(seq_appliedMuteScene(&m) - serial0), 1);
	seq_setMute(7, 0);
}

TEST(sequencer_mute_scene_empty_step_stopped_and_latest)
{
	seq_clearMuteScenes();
	seq_setMute(7, 0);
	ASSERT_EQ(seq_recallMuteSceneAt(0, 1), 0);	/* empty: nothing */
	seq_setMute(6, 1);
	seq_storeMuteScene(0);
	seq_setMute(7, 0);
	seq_storeMuteScene(1);				/* nothing muted, but stored */
	ASSERT_EQ(seq_muteScenes[1], 0x80);
	seq_storeMuteScene(8);				/* ignored */
	seq_setMuteSceneQuant(1);
	ASSERT_EQ(seq_recallMuteSceneAt(0, 1), 1);
	seq_muteSceneBoundary(5);			/* any master step */
	ASSERT_EQ(seq_isTrackMuted(6), 1);
	ASSERT_EQ(seq_recallMuteSceneAt(1, 0), 1);	/* stopped: at once */
	ASSERT_EQ(seq_isTrackMuted(6), 0);
	seq_setMuteSceneQuant(0);
	seq_recallMuteSceneAt(1, 1);
	seq_recallMuteSceneAt(0, 1);			/* the latest recall wins */
	seq_muteSceneBoundary(0);
	ASSERT_EQ(seq_isTrackMuted(6), 1);
	seq_setMute(7, 0);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.5 (plan Task 10, S8, S10): trg smp posts the voice side as a record
//------------------------------------------------------------------------------
/** steps 0 (volume 90, p-lock 12 = 34) and 4 (slide) of track 0, played 600 ms */
static void playTwoSteps(uint8_t timing)
{
	reset();
	activeParameters[PAR_TRIG_TIMING] = timing;
	Step *s0 = &seq_patternSet.seq_subStepPattern[0][0][0], *s4 = &seq_patternSet.seq_subStepPattern[0][0][4];
	const Step old0 = *s0, old4 = *s4;
	seq_setMainStep(0, 0, 0, 1);
	seq_setMainStep(0, 0, 4, 1);
	s0->volume = 90 | STEP_ACTIVE_MASK;
	s0->param1Nr = 12;
	s0->param1Val = 34;
	s4->volume |= STEP_ACTIVE_MASK;
	s4->prob = 100 | STEP_SLIDE_FLAG;
	posts = noteOns = noteOnEvents = slideCalls = 0;
	seq_setRunning(1);
	run(600);
	seq_setRunning(0);
	run(1);
	seq_setMainStep(0, 0, 0, 0);
	seq_setMainStep(0, 0, 4, 0);
	*s0 = old0;
	*s4 = old4;
}

TEST(sequencer_smp_posts_the_voice_side)
{
	playTwoSteps(0);
	ASSERT_EQ(posts, 2);
	ASSERT_EQ(noteOnEvents, 2);			/* the LED at fire time */
	ASSERT_EQ(noteOns, 0);				/* no trigger at fire time */
	ASSERT_EQ(slideCalls, 0);			/* the slide mode goes with the record */
	for (int i = 0; i < 2; i++)
	{
		ASSERT_EQ(posted[i].track, 0);
		ASSERT_EQ(posted[i].flags & (VS_TIMED | VS_PLOCKS | VS_SRC_MASK), VS_TIMED | VS_PLOCKS | VS_SRC_SEQ);
		ASSERT_TRUE(posted[i].time <= (uint32_t)postedAt[i]);	/* the event's time, not the tick's */
		ASSERT_TRUE(posted[i].time + MS > (uint32_t)postedAt[i]);
	}
	ASSERT_EQ(posted[0].vel, 90);
	ASSERT_EQ(posted[0].p1Nr, 12);
	ASSERT_EQ(posted[0].p1Val, 34);
	ASSERT_EQ((posted[0].flags & VS_SLIDE_MASK) >> VS_SLIDE_SHIFT, DRUM_SLIDE_OFF);
	ASSERT_EQ((posted[1].flags & VS_SLIDE_MASK) >> VS_SLIDE_SHIFT, DRUM_SLIDE_ON);
	const uint32_t d = posted[1].time - posted[0].time;	/* 4 steps at 120 BPM: 108,000,000 cycles */
	ASSERT_TRUE(d >= 107999999u && d <= 108000001u);
}

TEST(sequencer_blk_triggers_at_once)
{
	playTwoSteps(1);
	ASSERT_EQ(posts, 0);
	ASSERT_EQ(noteOns, 2);
	ASSERT_EQ(noteOnEvents, 0);
}

TEST(sequencer_smp_oneshot_roll_posts_an_untimed_record)
{
	reset();
	activeParameters[PAR_TRIG_TIMING] = 0;
	posts = noteOns = noteOnEvents = 0;
	seq_setRoll(2, 1);				/* stopped: the one-shot roll, through the mailbox */
	run(2);
	seq_setRoll(2, 0);
	ASSERT_EQ(posts, 1);
	ASSERT_EQ(noteOnEvents, 1);
	ASSERT_EQ(noteOns, 0);
	ASSERT_EQ(posted[0].track, 2);
	ASSERT_EQ(posted[0].flags, VS_SRC_ONESHOT);	/* untimed, no p-locks before the first step */
	ASSERT_EQ(posted[0].note, SEQ_DEFAULT_NOTE);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.6 (plan v0.6 Task 4): the fine microtiming and the template follow the
// pattern operations (M13)
//------------------------------------------------------------------------------
TEST(sequencer_micro_follows_copy_clear_rotate_and_backup)
{
	reset();
	static const uint8_t none[STEP_FINE_BYTES];
	const TrackTemplate tp = { .id = 3, .amount = -40 };
	for (uint8_t s = 0; s < NUM_STEPS; s++)
		stepFine_set(seq_patternSet.seq_stepFine[3][2], s, (int8_t)(s % 12 - 6));
	seq_patternSet.tmpl[3][2] = tp;
	seq_copyTrack(2, 5, 3);
	ASSERT_EQ(memcmp(seq_patternSet.seq_stepFine[3][5], seq_patternSet.seq_stepFine[3][2], STEP_FINE_BYTES), 0);
	ASSERT_EQ(memcmp(&seq_patternSet.tmpl[3][5], &tp, sizeof tp), 0);
	seq_copyPattern(3, 7);
	ASSERT_EQ(memcmp(seq_patternSet.seq_stepFine[7], seq_patternSet.seq_stepFine[3], sizeof seq_patternSet.seq_stepFine[3]), 0);
	ASSERT_EQ(memcmp(seq_patternSet.tmpl[7], seq_patternSet.tmpl[3], sizeof seq_patternSet.tmpl[3]), 0);
	seq_copyPatternToBackupBuffer(3);
	memset(seq_patternSet.seq_stepFine[3], 0, sizeof seq_patternSet.seq_stepFine[3]);
	memset(seq_patternSet.tmpl[3], 0, sizeof seq_patternSet.tmpl[3]);
	seq_copyBackupBufferToPattern(3);
	ASSERT_EQ(memcmp(seq_patternSet.seq_stepFine[3], seq_patternSet.seq_stepFine[7], sizeof seq_patternSet.seq_stepFine[3]), 0);
	ASSERT_EQ(memcmp(&seq_patternSet.tmpl[3][2], &tp, sizeof tp), 0);
	seq_clearTrack(2, 3);
	ASSERT_EQ(memcmp(seq_patternSet.seq_stepFine[3][2], none, STEP_FINE_BYTES), 0);
	ASSERT_EQ(seq_patternSet.tmpl[3][2].id, 0);
	ASSERT_EQ(seq_patternSet.tmpl[3][2].amount, 0);
	ASSERT_EQ(memcmp(&seq_patternSet.tmpl[3][5], &tp, sizeof tp), 0);	/* the other track stays */
	seq_clearPattern(7);
	for (uint8_t t = 0; t < NUM_TRACKS; t++)
	{
		ASSERT_EQ(memcmp(seq_patternSet.seq_stepFine[7][t], none, STEP_FINE_BYTES), 0);
		ASSERT_EQ(seq_patternSet.tmpl[7][t].id, 0);
	}
	/* a rotation within the length 5 moves each fine part with its step */
	seq_patternSet.length[4][2] = 5;
	for (uint8_t s = 0; s < NUM_STEPS; s++)
		stepFine_set(seq_patternSet.seq_stepFine[4][2], s, (int8_t)(s % 12 - 6));
	seq_rotateTrack(2, 4, 1);
	static const int8_t right[6] = { -2, -6, -5, -4, -3, -1 };
	for (uint8_t s = 0; s < 6; s++)
		ASSERT_EQ(stepFine_get(seq_patternSet.seq_stepFine[4][2], s), right[s]);
	seq_rotateTrack(2, 4, -1);
	for (uint8_t s = 0; s < NUM_STEPS; s++)
		ASSERT_EQ(stepFine_get(seq_patternSet.seq_stepFine[4][2], s), s % 12 - 6);
}

//------------------------------------------------------------------------------
// LICHOMAT v0.6 (plan v0.6 Task 6): the step page's mic and shf, the track page's tpl and amt
// through their direct calls (M14, M16, M17)
//------------------------------------------------------------------------------
static int microOf(uint8_t p, uint8_t t, uint8_t s)
{
	return stepMicro(seq_patternSet.seq_stepTiming[p][t][s], stepFine_get(seq_patternSet.seq_stepFine[p][t], s));
}

TEST(sequencer_micro_step_page)
{
	reset();
	StepTiming *t = &seq_patternSet.seq_stepTiming[2][1][9];
	*t = (StepTiming){ .flam = 4, .shift = 7 };
	seq_setStepMicro(2, 1, 9, 84 + 19);		/* mic +19: shf +2, fine -5 */
	ASSERT_EQ(t->shift, 9);
	ASSERT_EQ(stepFine_get(seq_patternSet.seq_stepFine[2][1], 9), -5);
	ASSERT_EQ(activeParameters[PAR_STEP_SHIFT], 9);
	ASSERT_EQ(activeParameters[PAR_STEP_MICRO], 84 + 19);
	/* spec 6.1: mic +14, then shf +1 -> +2: mic +26 */
	seq_setStepMicro(2, 1, 9, 84 + 14);
	ASSERT_EQ(t->shift, 8);
	seq_setStepShift(2, 1, 9, 9);
	seq_loadStepMicro(2, 1, 9);
	ASSERT_EQ(activeParameters[PAR_STEP_MICRO], 84 + 26);
	/* U33 step 2: on mic +6, shf +1 -> +2 -> +1 reads +18, then +6 */
	seq_setStepMicro(2, 1, 9, 84 + 6);
	ASSERT_EQ(t->shift, 8);
	seq_setStepShift(2, 1, 9, 9);
	ASSERT_EQ(microOf(2, 1, 9), 18);
	seq_setStepShift(2, 1, 9, 8);
	ASSERT_EQ(microOf(2, 1, 9), 6);
	/* a shf edit past the reach stops mic at +-84 */
	seq_setStepMicro(2, 1, 9, 84 + 77);		/* +6 units, fine +5 */
	seq_setStepShift(2, 1, 9, 14);
	ASSERT_EQ(microOf(2, 1, 9), 84);
	ASSERT_EQ(stepFine_get(seq_patternSet.seq_stepFine[2][1], 9), 0);
	seq_setStepMicro(2, 1, 9, 84 - 78);		/* -6 units, fine -6 */
	seq_setStepShift(2, 1, 9, 0);
	ASSERT_EQ(microOf(2, 1, 9), -84);
	/* a menu value past 168 is 0; the flam stayed throughout */
	seq_setStepMicro(2, 1, 9, 200);
	ASSERT_EQ(microOf(2, 1, 9), 0);
	ASSERT_EQ(t->shift, 7);
	ASSERT_EQ(t->flam, 4);
	/* tpl and amt: the direct calls take values past 127, and out-of-range values are the defaults */
	seq_setGrooveParam(1, 2, PAR_TRACK_TPL, 7);
	seq_setGrooveParam(1, 2, PAR_TRACK_TPL_AMT, 200);
	ASSERT_EQ(seq_patternSet.tmpl[2][1].id, 7);
	ASSERT_EQ(seq_patternSet.tmpl[2][1].amount, 100);
	seq_loadGrooveParams(1, 2);
	ASSERT_EQ(activeParameters[PAR_TRACK_TPL], 7);
	ASSERT_EQ(activeParameters[PAR_TRACK_TPL_AMT], 200);
	seq_setGrooveParam(1, 2, PAR_TRACK_TPL, 12);	/* LICHOMAT v0.7: 8..11 are us1..us4 (plan v0.7 V7-1) */
	seq_setGrooveParam(1, 2, PAR_TRACK_TPL_AMT, 201);
	seq_loadGrooveParams(1, 2);
	ASSERT_EQ(activeParameters[PAR_TRACK_TPL], 0);
	ASSERT_EQ(activeParameters[PAR_TRACK_TPL_AMT], 100);
	/* M16: +19 at 120 BPM and x1 is 24.7 ms, at 142 BPM 20.9; -18 at 120 is -23.4; +19 at x3/4
	 * and 142 BPM is 27.9 */
	seq_setBpm(120);
	ASSERT_EQ(seq_stepMicroTenthsMs(1, 2, 84 + 19), 247);
	ASSERT_EQ(seq_stepMicroTenthsMs(1, 2, 84 - 18), -234);
	seq_setBpm(142);
	ASSERT_EQ(seq_stepMicroTenthsMs(1, 2, 84 + 19), 209);
	seq_patternSet.groove[2][1].tempo = -1;
	ASSERT_EQ(seq_stepMicroTenthsMs(1, 2, 84 + 19), 279);
	seq_patternSet.groove[2][1].tempo = -5;		/* x1/4: +84 at 120 BPM is 437.5 ms */
	seq_setBpm(120);
	ASSERT_EQ(seq_stepMicroTenthsMs(1, 2, 168), 4375);
	seq_patternSet.groove[2][1].tempo = 0;
	seq_setBpm(125);				/* +-1 tick is +-1.25 ms: the half away from zero */
	ASSERT_EQ(seq_stepMicroTenthsMs(1, 2, 85), 13);
	ASSERT_EQ(seq_stepMicroTenthsMs(1, 2, 83), -13);
	seq_setBpm(120);
	run(1);
}

TEST(sequencer_micro_host_callbacks_reach_the_core)
{
	/* review of Tasks 4-5: the glue's fine() and tmpl() (seq_cbFine(), seq_cbTmpl()). Track 0,
	 * steps 0 and 2 in smp: fine +5 on step 0 (shift 7) posts them 192 - 5 = 187 ticks apart;
	 * with drg at 100 % (+6 on step 2) 187 + 6 = 193. 281,250 cycles a tick at 120 BPM */
	static const uint32_t want[2] = { 187u * 281250u, 193u * 281250u };
	for (int k = 0; k < 2; k++)
	{
		reset();
		activeParameters[PAR_TRIG_TIMING] = 0;
		Step *s0 = &seq_patternSet.seq_subStepPattern[0][0][0], *s2 = &seq_patternSet.seq_subStepPattern[0][0][2];
		const Step old0 = *s0, old2 = *s2;
		StepTiming *t0 = &seq_patternSet.seq_stepTiming[0][0][0], *t2 = &seq_patternSet.seq_stepTiming[0][0][2];
		const StepTiming oldT0 = *t0, oldT2 = *t2;
		const uint8_t oldFine = seq_patternSet.seq_stepFine[0][0][0];
		const TrackTemplate oldTmpl = seq_patternSet.tmpl[0][0];
		seq_setMainStep(0, 0, 0, 1);
		seq_setMainStep(0, 0, 2, 1);
		s0->volume |= STEP_ACTIVE_MASK;
		s2->volume |= STEP_ACTIVE_MASK;
		*t0 = (StepTiming){ .flam = 0, .shift = 7 };
		*t2 = (StepTiming){ .flam = 0, .shift = 7 };
		seq_patternSet.seq_stepFine[0][0][0] = 0;
		stepFine_set(seq_patternSet.seq_stepFine[0][0], 0, 5);
		seq_patternSet.tmpl[0][0] = (TrackTemplate){ .id = (uint8_t)(k ? 7 : 0), .amount = 0 };
		posts = 0;
		seq_setRunning(1);
		run(400);
		seq_setRunning(0);
		run(1);
		int on[2], n = 0;
		for (int i = 0; i < posts && i < 64; i++)
			if (posted[i].track == 0 && n < 2)
				on[n++] = i;
		ASSERT_EQ(n, 2);
		const uint32_t d = posted[on[1]].time - posted[on[0]].time;
		ASSERT_TRUE(d >= want[k] - 1u && d <= want[k] + 1u);
		seq_setMainStep(0, 0, 0, 0);
		seq_setMainStep(0, 0, 2, 0);
		*s0 = old0;
		*s2 = old2;
		*t0 = oldT0;
		*t2 = oldT2;
		seq_patternSet.seq_stepFine[0][0][0] = oldFine;
		seq_patternSet.tmpl[0][0] = oldTmpl;
	}
	activeParameters[PAR_TRIG_TIMING] = 1;
}

//------------------------------------------------------------------------------
// LICHOMAT v0.6 (plan v0.6 Task 7, spec 10.1 record_micro): recording keeps the played offset
//------------------------------------------------------------------------------
#define TICK_120 281250u		/* cycles of a tick at 120 BPM */

/** record mode and the start; the time of step 0 (the seq_tick() that applies the start) */
static seqc_time_t recordStart(void)
{
	seq_setRecordingMode(1);
	seq_setRunning(1);
	run(1);
	return hostNow;
}

/** one seq_tick() at time t */
static void tickAt(seqc_time_t t)
{
	hostNow = t;
	seq_tick();
}

/** the Note Ons of voice at or after time from, at most max, into at */
static int voiceOnsFrom(uint8_t voice, seqc_time_t from, seqc_time_t *at, int max)
{
	int n = 0;
	for (int i = 0; i < noteOns && i < 64; i++)
		if (noteOnVoice[i] == voice && noteOnAt[i] >= from && n < max)
			at[n++] = noteOnAt[i];
	return n;
}

static void recordStop(void)
{
	seq_setRecordingMode(0);
	seq_setRunning(0);
	run(1);
	activeParameters[PAR_REC_QUANT] = 0;
}

TEST(record_micro_rqn_off_stores_the_offset)
{
	reset();
	activeParameters[PAR_REC_QUANT] = 0;
	const uint8_t tr = 1;
	seq_patternSet.seq_stepTiming[0][tr][4] = (StepTiming){ .flam = 3, .shift = 7 };	/* x4 */
	Step *s4 = &seq_patternSet.seq_subStepPattern[0][tr][4];
	s4->param1Nr = 5;
	s4->param1Val = 66;
	const seqc_time_t t0 = recordStart();
	/* 19 ticks after step 4's grid tick: step 4, shf +2, fine -5; flam and p-locks kept */
	seqc_time_t t = t0 + (seqc_time_t)(4 * 96 + 19) * TICK_120;
	tickAt(t);
	addNoteAtInTick(tr, 100, 64, t);
	ASSERT_TRUE(seq_isMainStepActive(tr, 4, 0));
	ASSERT_EQ(seq_patternSet.seq_stepTiming[0][tr][4].shift, 9);
	ASSERT_EQ(seq_patternSet.seq_stepTiming[0][tr][4].flam, 3);
	ASSERT_EQ(stepFine_get(seq_patternSet.seq_stepFine[0][tr], 4), -5);
	ASSERT_EQ(s4->param1Nr, 5);
	ASSERT_EQ(s4->param1Val, 66);
	ASSERT_EQ(s4->note, 64);
	ASSERT_EQ(s4->volume & 0x7f, 100);
	/* 30 ticks before step 6's: step 6 at -30, with its skip-once marker */
	t = t0 + (seqc_time_t)(6 * 96 - 30) * TICK_120;
	tickAt(t);
	noteOns = 0;
	addNoteAtInTick(tr, 90, 62, t);
	ASSERT_TRUE(seq_isMainStepActive(tr, 6, 0));
	ASSERT_FALSE(seq_isMainStepActive(tr, 5, 0));
	ASSERT_EQ(microOf(0, tr, 6), -30);
	/* this loop plays neither; the next one step 4's four hits at +19, then step 6 at -30 */
	while (hostNow < t0 + (seqc_time_t)(1536 + 600) * TICK_120)
		run(1);
	seqc_time_t at[8];
	ASSERT_EQ(voiceOnsFrom(tr, t, at, 8), 5);
	ASSERT_TRUE(at[0] >= t0 + (seqc_time_t)(1536 + 384 + 19) * TICK_120);
	ASSERT_TRUE(at[0] < t0 + (seqc_time_t)(1536 + 384 + 19) * TICK_120 + MS);
	ASSERT_TRUE(at[4] >= t0 + (seqc_time_t)(1536 + 546) * TICK_120);
	ASSERT_TRUE(at[4] < t0 + (seqc_time_t)(1536 + 546) * TICK_120 + MS);
	recordStop();
}

TEST(record_micro_rqn_on_keeps_the_timing)
{
	reset();
	activeParameters[PAR_REC_QUANT] = 1;
	const uint8_t tr = 1;
	seq_patternSet.seq_stepTiming[0][tr][4] = (StepTiming){ .flam = 3, .shift = 10 };
	stepFine_set(seq_patternSet.seq_stepFine[0][tr], 4, 2);
	const seqc_time_t t0 = recordStart();
	seqc_time_t t = t0 + (seqc_time_t)(4 * 96 + 19) * TICK_120;
	tickAt(t);
	addNoteAtInTick(tr, 100, 64, t);
	ASSERT_TRUE(seq_isMainStepActive(tr, 4, 0));
	ASSERT_EQ(seq_patternSet.seq_stepTiming[0][tr][4].shift, 10);	/* the timing as it was */
	ASSERT_EQ(stepFine_get(seq_patternSet.seq_stepFine[0][tr], 4), 2);
	t = t0 + (seqc_time_t)(6 * 96 - 30) * TICK_120;
	tickAt(t);
	noteOns = 0;
	addNoteAtInTick(tr, 90, 62, t);
	ASSERT_TRUE(seq_isMainStepActive(tr, 6, 0));
	ASSERT_EQ(microOf(0, tr, 6), 0);
	while (hostNow < t0 + (seqc_time_t)(1536 + 600) * TICK_120)
		run(1);
	seqc_time_t at[8];
	ASSERT_EQ(voiceOnsFrom(tr, t, at, 8), 5);		/* step 6 skipped in this loop, then on the grid */
	ASSERT_TRUE(at[4] >= t0 + (seqc_time_t)(1536 + 576) * TICK_120);
	ASSERT_TRUE(at[4] < t0 + (seqc_time_t)(1536 + 576) * TICK_120 + MS);
	recordStop();
}

TEST(record_micro_stamped_note_after_the_boundary)
{
	/* a MIDI note stamped 2 ms (1.536 ticks) before step 4, parsed 3 ms after it: step 4, -2 */
	reset();
	const seqc_time_t t0 = recordStart();
	tickAt(t0 + (seqc_time_t)384 * TICK_120 + 3 * MS);
	const uint32_t stamp = (uint32_t)(t0 + (seqc_time_t)384 * TICK_120 - 2 * MS);
	addNoteAtInTick(1, 100, 60, seq_noteTime(stamp));
	ASSERT_TRUE(seq_isMainStepActive(1, 4, 0));
	ASSERT_FALSE(seq_isMainStepActive(1, 3, 0));
	ASSERT_EQ(microOf(0, 1, 4), -2);
	recordStop();
	/* seq_noteTime() across the 32-bit wrap of the cycle counter */
	tickAt((1ull << 32) + 1000u);
	ASSERT_TRUE(seq_noteTime((uint32_t)(hostNow - 5000u)) == hostNow - 5000u);
	ASSERT_TRUE(seq_noteTime((uint32_t)(hostNow + 700u)) == hostNow + 700u);
}

TEST(record_micro_template_and_delay_are_left_out)
{
	/* lay at 100 % and delay +10 ms (8 ticks): step 5's reference is 480 + 18 + 8 = 506, so a
	 * note at 510 stores +4, and the next loop plays it at 1536 + 510 */
	reset();
	activeParameters[PAR_REC_QUANT] = 0;
	seq_patternSet.tmpl[0][1] = (TrackTemplate){ .id = 2, .amount = 0 };
	seq_patternSet.groove[0][1].delay = 10;
	const seqc_time_t t0 = recordStart();
	const seqc_time_t t = t0 + (seqc_time_t)510 * TICK_120;
	tickAt(t);
	addNoteAtInTick(1, 100, 60, t);
	ASSERT_EQ(microOf(0, 1, 5), 4);
	while (hostNow < t0 + (seqc_time_t)(1536 + 600) * TICK_120)
		run(1);
	seqc_time_t at[4];
	ASSERT_EQ(voiceOnsFrom(1, t, at, 4), 1);
	ASSERT_TRUE(at[0] >= t + (seqc_time_t)1536 * TICK_120);
	ASSERT_TRUE(at[0] < t + (seqc_time_t)1536 * TICK_120 + MS);
	recordStop();
	seq_patternSet.tmpl[0][1] = (TrackTemplate){ 0, 0 };
	seq_patternSet.groove[0][1].delay = 0;
}

TEST(record_micro_roll_hits_keep_the_timing)
{
	/* M21: roll hits record at their own step, the step's timing as it was */
	reset();
	activeParameters[PAR_REC_QUANT] = 0;
	for (uint8_t s = 0; s < 16; s++)
	{
		seq_patternSet.seq_stepTiming[0][1][s] = (StepTiming){ .flam = 0, .shift = 9 };
		stepFine_set(seq_patternSet.seq_stepFine[0][1], s, 3);
	}
	seq_setRollRate(8);				/* 1/16: one roll hit a step */
	recordStart();
	seq_setRoll(1, 1);
	run(700);
	seq_setRoll(1, 0);
	for (uint8_t s = 1; s <= 4; s++)
	{
		ASSERT_TRUE(seq_isMainStepActive(1, s, 0));
		ASSERT_EQ(microOf(0, 1, s), 27);
	}
	recordStop();
}

TEST(record_micro_oneshot_roll_at_the_apply_time)
{
	/* M21: the one-shot roll records at the core's time when seq_tick() applies it, before
	 * that call's seqc_process(): tick 394, step 4 at +10 */
	reset();
	activeParameters[PAR_REC_QUANT] = 0;
	seq_setRollRate(0);				/* One: through the mailbox */
	const seqc_time_t t0 = recordStart();
	tickAt(t0 + (seqc_time_t)394 * TICK_120);
	seq_setRoll(1, 1);
	tickAt(t0 + (seqc_time_t)394 * TICK_120 + MS);
	seq_setRoll(1, 0);
	ASSERT_TRUE(seq_isMainStepActive(1, 4, 0));
	ASSERT_EQ(microOf(0, 1, 4), 10);
	seq_setRollRate(8);
	recordStop();
}

TEST(record_micro_rqn_2_stores_the_offset)
{
	/* review of Task 7 (M22): any rqn value other than 1 is off, also a stored 2 */
	reset();
	activeParameters[PAR_REC_QUANT] = 2;
	seq_patternSet.seq_stepTiming[0][2][4] = (StepTiming){ .flam = 0, .shift = 7 };
	stepFine_set(seq_patternSet.seq_stepFine[0][2], 4, 0);
	const seqc_time_t t0 = recordStart();
	const seqc_time_t t = t0 + (seqc_time_t)(4 * 96 + 19) * TICK_120;
	tickAt(t);
	addNoteAtInTick(2, 100, 64, t);
	ASSERT_TRUE(seq_isMainStepActive(2, 4, 0));
	ASSERT_EQ(microOf(0, 2, 4), 19);
	recordStop();
}

TEST(record_micro_mu_within_the_reach_of_the_template)
{
	/* review of Task 7 (M19): shuffle 127 (swing 64), drg at 200 % on track 2: step 2's G is
	 * +12 and step 3's reference 288 + 64 = 352, so a note at 277 is step 2's, 73 late; the
	 * stored mu is 84 - 12 = 72, and the next loop plays it where it was played, less the tick
	 * that playback's clamp of P takes */
	reset();
	activeParameters[PAR_REC_QUANT] = 0;
	seq_patternSet.tmpl[0][2] = (TrackTemplate){ .id = 7, .amount = 100 };
	seq_setShuffle(1.0f);
	const seqc_time_t t0 = recordStart();
	const seqc_time_t t = t0 + (seqc_time_t)277 * TICK_120;
	tickAt(t);
	addNoteAtInTick(2, 100, 64, t);
	seq_setShuffle(0.0f);
	ASSERT_TRUE(seq_isMainStepActive(2, 2, 0));
	ASSERT_EQ(microOf(0, 2, 2), 72);
	while (hostNow < t0 + (seqc_time_t)(1536 + 300) * TICK_120)
		run(1);
	seqc_time_t at[4];
	ASSERT_EQ(voiceOnsFrom(2, t, at, 4), 1);
	ASSERT_TRUE(at[0] >= t0 + (seqc_time_t)(1536 + 192 + 72 + 12) * TICK_120);
	ASSERT_TRUE(at[0] < t0 + (seqc_time_t)(1536 + 192 + 72 + 12) * TICK_120 + MS);
	recordStop();
	seq_patternSet.tmpl[0][2] = (TrackTemplate){ 0, 0 };
}

TEST(record_micro_only_inside_seq_tick)
{
	/* review of Task 7: seq_addNoteAt() reads the core directly, so outside seq_tick() it
	 * records nothing; inside it records */
	reset();
	activeParameters[PAR_REC_QUANT] = 0;
	seq_setMainStep(0, 3, 4, 0);
	const seqc_time_t t0 = recordStart();
	const seqc_time_t t = t0 + (seqc_time_t)(4 * 96 + 5) * TICK_120;
	tickAt(t);
	seq_addNoteAt(3, 100, 64, t);
	seq_addNote(3, 100, 64);
	ASSERT_FALSE(seq_isMainStepActive(3, 4, 0));
	addNoteAtInTick(3, 100, 64, t);
	ASSERT_TRUE(seq_isMainStepActive(3, 4, 0));
	recordStop();
}

//------------------------------------------------------------------------------
// LICHOMAT v0.7 (plan v0.7 Task 4): the velocity templates follow the pattern operations, the
// host's vtmpl() and userTemplates reach the core, the own templates' capture, the clear of a
// track's microtiming
//------------------------------------------------------------------------------
TEST(sequencer_dynamics_follow_copy_clear_and_backup)
{
	reset();
	const TrackTemplate vt = { .id = 6, .amount = -25 };
	static const TrackTemplate none[NUM_TRACKS];
	seq_patternSet.vtpl[3][2] = vt;
	seq_copyTrack(2, 5, 3);
	ASSERT_EQ(memcmp(&seq_patternSet.vtpl[3][5], &vt, sizeof vt), 0);
	seq_copyPattern(3, 7);
	ASSERT_EQ(memcmp(seq_patternSet.vtpl[7], seq_patternSet.vtpl[3], sizeof seq_patternSet.vtpl[3]), 0);
	seq_copyPatternToBackupBuffer(3);
	memset(seq_patternSet.vtpl[3], 0, sizeof seq_patternSet.vtpl[3]);
	seq_copyBackupBufferToPattern(3);
	ASSERT_EQ(memcmp(&seq_patternSet.vtpl[3][2], &vt, sizeof vt), 0);
	seq_clearTrack(2, 3);
	ASSERT_EQ(seq_patternSet.vtpl[3][2].id, 0);
	ASSERT_EQ(seq_patternSet.vtpl[3][2].amount, 0);
	ASSERT_EQ(memcmp(&seq_patternSet.vtpl[3][5], &vt, sizeof vt), 0);	/* the other track stays */
	seq_clearPattern(7);
	ASSERT_EQ(memcmp(seq_patternSet.vtpl[7], none, sizeof none), 0);
	seq_patternSet.vtpl[4][2] = vt;						/* a rotation moves steps, not the track's settings */
	seq_rotateTrack(2, 4, 1);
	ASSERT_EQ(memcmp(&seq_patternSet.vtpl[4][2], &vt, sizeof vt), 0);
	seq_clearPattern(3);
	seq_clearPattern(4);
}

TEST(sequencer_dynamics_host_callbacks_reach_the_core)
{
	/* the glue's vtmpl() (seq_cbVtmpl()) and userTemplates (seq_userTemplates). Track 0, steps 0 and
	 * 2 at volume 127 in smp, voice 1 on MIDI channel 6. k 0: vtp of8 at 100 %: the records and the
	 * Note Ons carry 40 and 127 (q 0, 2); k 1: vtp us1 with -6 dB at position 0: 64 and 127; k 2: tpl
	 * us1 with +12 ticks at position 2: the records 192 + 12 = 204 ticks apart (281,250 cycles) */
	static const uint8_t vel[3][2] = { { 40, 127 }, { 64, 127 }, { 127, 127 } };
	static const uint32_t apart[3] = { 192u * 281250u, 192u * 281250u, 204u * 281250u };
	for (int k = 0; k < 3; k++)
	{
		reset();
		activeParameters[PAR_TRIG_TIMING] = 0;
		midiParser_txRxFilter = 0x10;		/* MIDI_FILTER_NOTES << 4: Note Ons out */
		midi_MidiChannels[0] = 5;
		Step *s0 = &seq_patternSet.seq_subStepPattern[0][0][0], *s2 = &seq_patternSet.seq_subStepPattern[0][0][2];
		const Step old0 = *s0, old2 = *s2;
		seq_setMainStep(0, 0, 0, 1);
		seq_setMainStep(0, 0, 2, 1);
		s0->volume = (uint8_t)(STEP_ACTIVE_MASK | 127);
		s2->volume = (uint8_t)(STEP_ACTIVE_MASK | 127);
		memset(seq_userTemplates, 0, sizeof seq_userTemplates);
		seq_userTemplates[0].vel[0] = -6;
		seq_userTemplates[0].timing[2] = 12;
		seq_patternSet.vtpl[0][0] = (TrackTemplate){ .id = (uint8_t)(k == 0 ? 1 : (k == 1 ? 5 : 0)), .amount = 0 };
		seq_patternSet.tmpl[0][0] = (TrackTemplate){ .id = (uint8_t)(k == 2 ? 8 : 0), .amount = 0 };
		posts = 0;
		midiOns = 0;
		seq_setRunning(1);
		run(400);
		seq_setRunning(0);
		run(1);
		int on[2], n = 0;
		for (int i = 0; i < posts && i < 64; i++)
			if (posted[i].track == 0 && n < 2)
				on[n++] = i;
		ASSERT_EQ(n, 2);
		ASSERT_EQ(posted[on[0]].vel, vel[k][0]);
		ASSERT_EQ(posted[on[1]].vel, vel[k][1]);
		const uint32_t d = posted[on[1]].time - posted[on[0]].time;
		ASSERT_TRUE(d >= apart[k] - 1u && d <= apart[k] + 1u);
		int m = 0;
		for (int i = 0; i < midiOns && i < 64; i++)
			if (midiOnStatus[i] == 0x95)
			{
				if (m < 2)
					ASSERT_EQ(midiOnVel[i], vel[k][m]);
				m++;
			}
		ASSERT_TRUE(m >= 2);
		seq_setMainStep(0, 0, 0, 0);
		seq_setMainStep(0, 0, 2, 0);
		*s0 = old0;
		*s2 = old2;
		seq_patternSet.vtpl[0][0] = (TrackTemplate){ 0, 0 };
		seq_patternSet.tmpl[0][0] = (TrackTemplate){ 0, 0 };
		memset(seq_userTemplates, 0, sizeof seq_userTemplates);
	}
	midi_MidiChannels[0] = 0;
	midiParser_txRxFilter = 0;
	activeParameters[PAR_TRIG_TIMING] = 1;
}

/** step s of track t in pattern p on, at volume vel, with microtiming T */
static void ownStep(uint8_t p, uint8_t t, uint8_t s, uint8_t vel, int micro)
{
	seq_setMainStep(p, t, s, 1);
	Step *st = &seq_patternSet.seq_subStepPattern[p][t][s];
	st->volume = (uint8_t)((st->volume & STEP_ACTIVE_MASK) | (vel & 0x7F));
	stepFine_set(seq_patternSet.seq_stepFine[p][t], s, stepMicro_split(micro, &seq_patternSet.seq_stepTiming[p][t][s]));
}

TEST(own_capture_takes_steps_1_to_16)
{
	/* V7-6..V7-10: the timing part is each step's T, the velocity part its dB below the loudest
	 * of steps 1-16 (127 here); steps that are off, step 21, and steps past the length give 0 and
	 * 0 dB; the other slots stay; no step on empties the slot; the loudest at 0 gives 0 dB */
	reset();
	seq_clearPattern(40);
	static const struct { uint8_t s, vel; int T; int8_t db; } on[8] = {
		{ 0, 100, 12, -2 }, { 2, 127, -7, 0 }, { 3, 64, 84, -6 }, { 5, 40, -84, -10 },
		{ 8, 20, 0, -16 }, { 11, 10, 5, -22 }, { 13, 1, 30, -42 }, { 15, 0, -1, -42 },
	};
	for (uint8_t s = 0; s < NUM_STEPS; s++)			/* stale values on every step, then off */
	{
		ownStep(40, 2, s, 5, 40);
		seq_setMainStep(40, 2, s, 0);
	}
	for (int k = 0; k < 8; k++)
		ownStep(40, 2, on[k].s, on[k].vel, on[k].T);
	ownStep(40, 2, 20, 127, 50);				/* step 21: not a position */
	memset(seq_userTemplates, 0x5A, sizeof seq_userTemplates);
	ASSERT_EQ(seq_captureUserTemplate(2, 40, 1), 8);
	int8_t wantT[16] = { 0 }, wantV[16] = { 0 };
	for (int k = 0; k < 8; k++)
	{
		wantT[on[k].s] = (int8_t)on[k].T;
		wantV[on[k].s] = on[k].db;
	}
	ASSERT_EQ(memcmp(seq_userTemplates[1].timing, wantT, 16), 0);
	ASSERT_EQ(memcmp(seq_userTemplates[1].vel, wantV, 16), 0);
	ASSERT_EQ(seq_userTemplates[0].timing[0], 0x5A);		/* the other slots stay */
	ASSERT_EQ(seq_userTemplates[2].vel[15], 0x5A);
	/* length 12: steps 13-16 give 0 */
	seq_patternSet.length[40][2] = 12;
	ASSERT_EQ(seq_captureUserTemplate(2, 40, 2), 6);
	for (int s = 12; s < 16; s++)
	{
		ASSERT_EQ(seq_userTemplates[2].timing[s], 0);
		ASSERT_EQ(seq_userTemplates[2].vel[s], 0);
	}
	ASSERT_EQ(seq_userTemplates[2].timing[3], 84);
	ASSERT_EQ(seq_userTemplates[2].vel[5], -10);
	/* the loudest at 0: every step on is 0 dB, the timing taken */
	seq_patternSet.length[40][2] = 16;
	for (int k = 0; k < 8; k++)
		seq_patternSet.seq_subStepPattern[40][2][on[k].s].volume &= STEP_ACTIVE_MASK;
	ASSERT_EQ(seq_captureUserTemplate(2, 40, 3), 8);
	static const int8_t zero[16];
	ASSERT_EQ(memcmp(seq_userTemplates[3].vel, zero, 16), 0);
	ASSERT_EQ(memcmp(seq_userTemplates[3].timing, wantT, 16), 0);
	/* no step on: the slot is empty (V7-9) */
	for (uint8_t s = 0; s < NUM_STEPS; s++)
		seq_setMainStep(40, 2, s, 0);
	ASSERT_EQ(seq_captureUserTemplate(2, 40, 1), 0);
	static const UserTemplate empty;
	ASSERT_EQ(memcmp(&seq_userTemplates[1], &empty, sizeof empty), 0);
	/* out of range: nothing changes */
	const UserTemplate keep = seq_userTemplates[2];
	ASSERT_EQ(seq_captureUserTemplate(2, 40, 4), 0);
	ASSERT_EQ(seq_captureUserTemplate(7, 40, 2), 0);
	ASSERT_EQ(seq_captureUserTemplate(2, 64, 2), 0);
	ASSERT_EQ(memcmp(&seq_userTemplates[2], &keep, sizeof keep), 0);
	seq_clearPattern(40);
	seq_clearUserTemplates();
	ASSERT_EQ(seq_userTemplates[0].timing[0], 0);
}

TEST(own_capture_clear_track_micro)
{
	/* V7-14: every step of the track in the pattern to T 0 (shift 7, fine 0), its flam kept; a
	 * stored shift of 15 also becomes 7; the track's templates, the other tracks and the other
	 * patterns stay */
	reset();
	for (uint8_t s = 0; s < NUM_STEPS; s++)
	{
		StepTiming *a = &seq_patternSet.seq_stepTiming[41][1][s];
		a->flam = (uint8_t)(s % 6);
		stepFine_set(seq_patternSet.seq_stepFine[41][1], s, stepMicro_split((s * 7) % 169 - 84, a));
		stepFine_set(seq_patternSet.seq_stepFine[41][2], s, stepMicro_split(13, &seq_patternSet.seq_stepTiming[41][2][s]));
		stepFine_set(seq_patternSet.seq_stepFine[42][1], s, stepMicro_split(-20, &seq_patternSet.seq_stepTiming[42][1][s]));
	}
	seq_patternSet.seq_stepTiming[41][1][63].shift = 15;
	seq_patternSet.tmpl[41][1] = (TrackTemplate){ .id = 4, .amount = 20 };
	seq_patternSet.vtpl[41][1] = (TrackTemplate){ .id = 2, .amount = -30 };
	seq_clearTrackMicro(1, 41);
	for (uint8_t s = 0; s < NUM_STEPS; s++)
	{
		ASSERT_EQ(seq_patternSet.seq_stepTiming[41][1][s].shift, 7);
		ASSERT_EQ(seq_patternSet.seq_stepTiming[41][1][s].flam, s % 6);
		ASSERT_EQ(stepFine_get(seq_patternSet.seq_stepFine[41][1], s), 0);
		ASSERT_EQ(stepMicro(seq_patternSet.seq_stepTiming[41][2][s], stepFine_get(seq_patternSet.seq_stepFine[41][2], s)), 13);
		ASSERT_EQ(stepMicro(seq_patternSet.seq_stepTiming[42][1][s], stepFine_get(seq_patternSet.seq_stepFine[42][1], s)), -20);
	}
	ASSERT_EQ(seq_patternSet.tmpl[41][1].id, 4);
	ASSERT_EQ(seq_patternSet.vtpl[41][1].amount, -30);
	seq_clearTrackMicro(7, 41);				/* out of range: nothing */
	seq_clearTrackMicro(1, 64);
	ASSERT_EQ(stepMicro(seq_patternSet.seq_stepTiming[42][1][0], stepFine_get(seq_patternSet.seq_stepFine[42][1], 0)), -20);
	seq_clearPattern(41);
	seq_clearPattern(42);
}

TEST(own_capture_takes_what_plays)
{
	/* V7-10 (Q36): the track's timing template is added to each step's T and the volume goes
	 * through its velocity template: lay and of8 at 100 % on 16 steps of T 0 and volume 127 give
	 * lay's ticks and of8's dB (of8's velocities 40, 23, 127 ... round back to its dB); lay at
	 * 200 % with T 80 on step 2 clamps 80 + 36 to 84 */
	reset();
	seq_clearPattern(43);
	for (uint8_t s = 0; s < 16; s++)
		ownStep(43, 3, s, 127, 0);
	seq_patternSet.tmpl[43][3] = (TrackTemplate){ .id = 2, .amount = 0 };	/* lay, 100 % */
	seq_patternSet.vtpl[43][3] = (TrackTemplate){ .id = 1, .amount = 0 };	/* of8, 100 % */
	ASSERT_EQ(seq_captureUserTemplate(3, 43, 0), 16);
	static const int8_t lay[16] = { 0, 18, 0, 5, 0, 18, 0, 5, 0, 18, 0, 5, 0, 18, 0, 5 };
	static const int8_t of8[16] = { -10, -15, 0, -14, -10, -14, 0, -13, -10, -16, 0, -14, -10, -15, 0, -13 };
	ASSERT_EQ(memcmp(seq_userTemplates[0].timing, lay, 16), 0);
	ASSERT_EQ(memcmp(seq_userTemplates[0].vel, of8, 16), 0);
	seq_patternSet.tmpl[43][3].amount = 100;				/* lay, 200 % */
	ownStep(43, 3, 1, 127, 80);
	ASSERT_EQ(seq_captureUserTemplate(3, 43, 0), 16);
	ASSERT_EQ(seq_userTemplates[0].timing[1], 84);
	ASSERT_EQ(seq_userTemplates[0].timing[3], 10);
	/* capturing into the slot the track plays: the old slot is read first */
	seq_patternSet.tmpl[43][3] = (TrackTemplate){ .id = TEMPLATE_BUILTIN + 0, .amount = 0 };	/* us1 */
	seq_patternSet.vtpl[43][3] = (TrackTemplate){ 0, 0 };
	ownStep(43, 3, 1, 127, 0);
	ASSERT_EQ(seq_captureUserTemplate(3, 43, 0), 16);
	ASSERT_EQ(seq_userTemplates[0].timing[1], 84);			/* T 0 + us1's old 84 */
	ASSERT_EQ(seq_userTemplates[0].timing[3], 10);
	seq_clearPattern(43);
	seq_clearUserTemplates();
}

TEST(sequencer_dynamics_vtmpl_reads_the_track_in_its_pattern)
{
	/* implementer of Task 4 (a mutant with vtpl[track][pattern] passed the plan's track 0 in
	 * pattern 0): track 2 in pattern 0 with of8 at 100 % posts 40 and 127 (q 0, 2) while
	 * vtpl[2][0] is off */
	reset();
	activeParameters[PAR_TRIG_TIMING] = 0;
	Step *s0 = &seq_patternSet.seq_subStepPattern[0][2][0], *s2 = &seq_patternSet.seq_subStepPattern[0][2][2];
	const Step old0 = *s0, old2 = *s2;
	seq_setMainStep(0, 2, 0, 1);
	seq_setMainStep(0, 2, 2, 1);
	s0->volume = (uint8_t)(STEP_ACTIVE_MASK | 127);
	s2->volume = (uint8_t)(STEP_ACTIVE_MASK | 127);
	seq_patternSet.vtpl[0][2] = (TrackTemplate){ .id = 1, .amount = 0 };
	seq_patternSet.vtpl[2][0] = (TrackTemplate){ 0, 0 };
	posts = 0;
	seq_setRunning(1);
	run(400);
	seq_setRunning(0);
	run(1);
	int on[2], n = 0;
	for (int i = 0; i < posts && i < 64; i++)
		if (posted[i].track == 2 && n < 2)
			on[n++] = i;
	ASSERT_EQ(n, 2);
	ASSERT_EQ(posted[on[0]].vel, 40);
	ASSERT_EQ(posted[on[1]].vel, 127);
	const uint32_t d = posted[on[1]].time - posted[on[0]].time;
	ASSERT_TRUE(d >= 192u * 281250u - 1u && d <= 192u * 281250u + 1u);
	seq_setMainStep(0, 2, 0, 0);
	seq_setMainStep(0, 2, 2, 0);
	*s0 = old0;
	*s2 = old2;
	seq_patternSet.vtpl[0][2] = (TrackTemplate){ 0, 0 };
	activeParameters[PAR_TRIG_TIMING] = 1;
}

TEST(own_capture_slots_set_and_clear)
{
	/* implementer of Task 4: seq_setUserTemplate() writes one slot and ignores slot 4 and NULL;
	 * seq_setUserTemplates() writes all four and ignores NULL; seq_clearUserTemplates() empties
	 * them */
	static const UserTemplate none[USER_TEMPLATES];
	static UserTemplate all[USER_TEMPLATES];
	for (int u = 0; u < USER_TEMPLATES; u++)
		for (int q = 0; q < TEMPLATE_STEPS; q++)
		{
			all[u].timing[q] = (int8_t)(u * 16 + q - 30);
			all[u].vel[q] = (int8_t)-(u * 10 + q);
		}
	seq_clearUserTemplates();
	seq_setUserTemplate(2, &all[3]);
	ASSERT_EQ(memcmp(&seq_userTemplates[2], &all[3], sizeof all[3]), 0);
	ASSERT_EQ(memcmp(&seq_userTemplates[0], &none[0], sizeof none[0]), 0);
	ASSERT_EQ(memcmp(&seq_userTemplates[3], &none[0], sizeof none[0]), 0);
	seq_setUserTemplate(USER_TEMPLATES, &all[1]);		/* out of range: nothing */
	seq_setUserTemplate(1, NULL);
	ASSERT_EQ(memcmp(&seq_userTemplates[1], &none[0], sizeof none[0]), 0);
	ASSERT_EQ(memcmp(&seq_userTemplates[2], &all[3], sizeof all[3]), 0);
	seq_setUserTemplates(all);
	ASSERT_EQ(memcmp(seq_userTemplates, all, sizeof all), 0);
	seq_setUserTemplates(NULL);
	ASSERT_EQ(memcmp(seq_userTemplates, all, sizeof all), 0);
	seq_clearUserTemplates();
	ASSERT_EQ(memcmp(seq_userTemplates, none, sizeof none), 0);
}

TEST(own_capture_edges_of_the_length_and_the_range)
{
	/* implementer of Task 4: the step at index len is past the length; a length of 0 plays as
	 * 16 (the core's rule); T -84 with psh at 200 % (-8 at q 2) clamps to -84; out of range with
	 * steps on returns 0 and writes nothing; seq_clearTrackMicro() of track 7 does not reach
	 * pattern 42's track 0, which lies right after pattern 41's track 6 */
	reset();
	seq_clearPattern(44);
	seq_clearUserTemplates();
	ownStep(44, 4, 0, 127, 3);
	ownStep(44, 4, 2, 64, -84);
	ownStep(44, 4, 12, 100, 20);
	ownStep(44, 4, 15, 90, -9);
	seq_patternSet.length[44][4] = 12;
	ASSERT_EQ(seq_captureUserTemplate(4, 44, 0), 2);
	ASSERT_EQ(seq_userTemplates[0].timing[12], 0);
	ASSERT_EQ(seq_userTemplates[0].vel[12], 0);
	ASSERT_EQ(seq_userTemplates[0].timing[2], -84);
	seq_patternSet.length[44][4] = 0;				/* as 16 */
	ASSERT_EQ(seq_captureUserTemplate(4, 44, 1), 4);
	ASSERT_EQ(seq_userTemplates[1].timing[12], 20);
	ASSERT_EQ(seq_userTemplates[1].timing[15], -9);
	ASSERT_EQ(seq_userTemplates[1].vel[15], -3);			/* 90 of 127: -2.99 dB */
	seq_patternSet.length[44][4] = 16;
	seq_patternSet.tmpl[44][4] = (TrackTemplate){ .id = 6, .amount = 100 };	/* psh, 200 % */
	ASSERT_EQ(seq_captureUserTemplate(4, 44, 2), 4);
	ASSERT_EQ(seq_userTemplates[2].timing[2], -84);		/* -84 - 8 */
	ASSERT_EQ(seq_userTemplates[2].timing[0], 3);
	const UserTemplate keep[USER_TEMPLATES] = { seq_userTemplates[0], seq_userTemplates[1],
		seq_userTemplates[2], seq_userTemplates[3] };
	ASSERT_EQ(seq_captureUserTemplate(4, 44, USER_TEMPLATES), 0);
	ASSERT_EQ(seq_captureUserTemplate(NUM_TRACKS, 44, 2), 0);
	ASSERT_EQ(seq_captureUserTemplate(4, NUM_PATTERN, 2), 0);
	ASSERT_EQ(memcmp(seq_userTemplates, keep, sizeof keep), 0);
	for (uint8_t s = 0; s < NUM_STEPS; s++)
		stepFine_set(seq_patternSet.seq_stepFine[42][0], s, stepMicro_split(-20, &seq_patternSet.seq_stepTiming[42][0][s]));
	seq_clearTrackMicro(NUM_TRACKS, 41);
	for (uint8_t s = 0; s < NUM_STEPS; s++)
		ASSERT_EQ(stepMicro(seq_patternSet.seq_stepTiming[42][0][s], stepFine_get(seq_patternSet.seq_stepFine[42][0], s)), -20);
	seq_clearPattern(42);
	seq_clearPattern(44);
	seq_clearUserTemplates();
}

TEST_MAIN()
