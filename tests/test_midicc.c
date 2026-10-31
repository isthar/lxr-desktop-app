/*
 * test_midicc.c
 *
 * Host test of where the CC handler (midiParser_ccHandler() in
 * Core/Src/Soundengine/MIDI/MidiParser.c, task E4.5) keeps the last value of
 * each parameter: midiParser_originalCcValues, the table that automationNode.c
 * reads to put a parameter back. A MIDI_CC is kept at [data1], a MIDI_CC2 at
 * [data1 + 128].
 *
 * fw 1.7 kept a MIDI_CC at [(uint16_t)(data1 - 1) + 1], which is 65536 for
 * CC 0: 64 KB past the 255-byte table, in the pattern memory on the device.
 * Every kit load sends parameter 127 as CC 0, and so does an incoming Bank
 * Select MSB. A MIDI_CC2 from an NRPN can have data1 up to 255 (the low byte of
 * the NRPN number), so its [data1 + 128] ran up to 129 bytes past the table.
 *
 * A Note On while the step page is shown sets the note of the active step,
 * activeParameters[PAR_ACTIVE_STEP], of the shown pattern and track (task E4.8).
 * fw 1.7 took the step as it is, and NRPN 121 from MIDI sets it to 0-127: a step
 * of 64-127 wrote the note into the next track, or past the step array.
 *
 * MidiParser.c is compiled as it is, against the stand-in headers in tests/stubs/
 * and the stand-in functions below. One name is changed for this target
 * (tests/CMakeLists.txt): midiParser_originalCcValues becomes
 * (*midiParser_ccTable), a pointer to a 255-byte array, which the test points
 * into a larger buffer. The bytes around the table are guard bytes: a store out
 * of the table lands there, also without a sanitizer, and the test sees it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include "menu/menu.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/FX/atmo.h"
#include "Soundengine/globals.h"
#include "Soundengine/MIDI/CortexParser.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/MIDI/Uart.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/USB/usb_manager.h"

#define TABLE_SIZE 0xff
#define GUARD_BEFORE 256
#define GUARD_AFTER (65536 + 256)	/* past index 65536, where fw 1.7 put CC 0 */
#define GUARD_BYTE 0xA5

_Static_assert(sizeof midiParser_originalCcValues == TABLE_SIZE, "the table of MidiParser.c");

//------------------------------------------------------------------------------
// The table and its guard bytes
//------------------------------------------------------------------------------
static uint8_t memoryAround[GUARD_BEFORE + TABLE_SIZE + GUARD_AFTER];
static uint8_t tableBefore[TABLE_SIZE];

static int modNodeCalls;
static uint16_t modNodeLastIdx;

/** guard bytes around the table, the table as it was, and no mod node call yet */
static void reset(void)
{
	midiParser_ccTable = (uint8_t (*)[TABLE_SIZE])(memoryAround + GUARD_BEFORE);
	memset(memoryAround, GUARD_BYTE, sizeof memoryAround);
	for (int i = 0; i < TABLE_SIZE; i++)
		midiParser_originalCcValues[i] = tableBefore[i] = (uint8_t)(i & 0x7f);
	modNodeCalls = 0;
	modNodeLastIdx = 0;
}

/** guard bytes that a store changed */
static long changedOutside(void)
{
	long n = 0;
	for (size_t i = 0; i < sizeof memoryAround; i++)
		if ((i < GUARD_BEFORE || i >= GUARD_BEFORE + TABLE_SIZE) && memoryAround[i] != GUARD_BYTE)
			n++;
	return n;
}

/** table entries that differ from reset(), except index except */
static int changedInTableExcept(int except)
{
	int n = 0;
	for (int i = 0; i < TABLE_SIZE; i++)
		if (i != except && midiParser_originalCcValues[i] != tableBefore[i])
			n++;
	return n;
}

static void cc(uint8_t status, uint8_t data1, uint8_t data2)
{
	MidiMsg msg = { .status = status, .data1 = data1, .data2 = data2 };
	midiParser_ccHandler(msg, 1, 0, 0.f);
}

/** NRPN coarse and fine as a controller sends them, then a fresh table */
static void selectNrpn(uint16_t nrpn)
{
	reset();
	cc(MIDI_CC, NRPN_COARSE, (uint8_t)(nrpn >> 7));
	cc(MIDI_CC, NRPN_FINE, (uint8_t)(nrpn & 0x7f));
	reset();
}

/** a 7-bit value that differs from the entry at index after reset() */
static uint8_t valueFor(int index) { return (uint8_t)((index & 0x7f) ^ 0x55); }

//------------------------------------------------------------------------------
// The tests
//------------------------------------------------------------------------------
TEST(midicc_cc0_is_kept_in_the_table)
{
	/* Bank Select MSB from MIDI in, and parameter 127 of every kit load */
	reset();
	cc(MIDI_CC, CC_BANK_CHANGE, 0x2A);
	ASSERT_EQ(changedOutside(), 0);
	ASSERT_EQ(midiParser_originalCcValues[0], 0x2A);
	ASSERT_EQ(changedInTableExcept(0), 0);
	/* the rest of the handler is as in fw 1.7 */
	ASSERT_EQ(modNodeCalls, 1);
	ASSERT_EQ(modNodeLastIdx, 0xFFFF);
}

TEST(midicc_cc_is_kept_at_its_number)
{
	for (int d = 1; d < 128; d++) {
		if (d == NRPN_DATA_ENTRY_COARSE)
			selectNrpn(5);
		else
			reset();
		cc(MIDI_CC, (uint8_t)d, valueFor(d));
		ASSERT_EQ(changedOutside(), 0);
		ASSERT_EQ(midiParser_originalCcValues[d], valueFor(d));
		ASSERT_EQ(modNodeCalls, 1);
		if (d == NRPN_DATA_ENTRY_COARSE) {
			/* the data entry goes on as a MIDI_CC2 for NRPN 5 */
			ASSERT_EQ(midiParser_originalCcValues[5 + 128], valueFor(d));
			ASSERT_EQ(modNodeLastIdx, 5 + 128);
			midiParser_originalCcValues[5 + 128] = tableBefore[5 + 128];
		} else {
			ASSERT_EQ(modNodeLastIdx, d - 1);
		}
		ASSERT_EQ(changedInTableExcept(d), 0);
	}
}

TEST(midicc_cc2_is_kept_at_its_number_plus_128)
{
	for (int d = 0; d < TABLE_SIZE - 128; d++) {
		reset();
		cc(MIDI_CC2, (uint8_t)d, valueFor(d));
		ASSERT_EQ(changedOutside(), 0);
		ASSERT_EQ(midiParser_originalCcValues[d + 128], valueFor(d));
		ASSERT_EQ(changedInTableExcept(d + 128), 0);
		ASSERT_EQ(modNodeCalls, 1);
		ASSERT_EQ(modNodeLastIdx, d + 128);
	}
}

TEST(midicc_cc2_past_the_table_is_not_kept)
{
	/* data1 127..255 has no entry: [data1 + 128] is 255..383 */
	for (int d = TABLE_SIZE - 128; d < 256; d++) {
		reset();
		cc(MIDI_CC2, (uint8_t)d, 0x11);
		ASSERT_EQ(changedOutside(), 0);
		ASSERT_EQ(changedInTableExcept(-1), 0);
		ASSERT_EQ(modNodeCalls, 1);
		ASSERT_EQ(modNodeLastIdx, d + 128);
	}
}

TEST(midicc_nrpn_255_stays_in_the_table)
{
	/* NRPN 255 from MIDI in: its data entry is a MIDI_CC2 with data1 255 */
	selectNrpn(255);
	cc(MIDI_CC, NRPN_DATA_ENTRY_COARSE, 0x33);
	ASSERT_EQ(changedOutside(), 0);
	ASSERT_EQ(changedInTableExcept(NRPN_DATA_ENTRY_COARSE), 0);
	ASSERT_EQ(midiParser_originalCcValues[NRPN_DATA_ENTRY_COARSE], 0x33);
}

TEST(midicc_modulation_and_automation_keep_nothing)
{
	/* a modulator (isFloatModulator) and an automation step (updateOriginalValue 0) */
	const uint8_t numbers[] = { CC_BANK_CHANGE, 1, 64, 127 };
	for (unsigned i = 0; i < sizeof numbers; i++) {
		MidiMsg msg = { .status = MIDI_CC, .data1 = numbers[i], .data2 = 0x7f };
		reset();
		midiParser_ccHandler(msg, 0, 0, 0.f);
		midiParser_ccHandler(msg, 1, 1, 0.5f);
		msg.status = MIDI_CC2;
		midiParser_ccHandler(msg, 0, 0, 0.f);
		midiParser_ccHandler(msg, 1, 1, 0.5f);
		ASSERT_EQ(changedOutside(), 0);
		ASSERT_EQ(changedInTableExcept(-1), 0);
	}
}

//------------------------------------------------------------------------------
// The step page: a Note On sets the note of the active step (E4.8)
//------------------------------------------------------------------------------
static PatternSet patternsBefore;
static uint8_t activeVoice;
static uint8_t viewedPattern;

/** the step page of pattern and track, with a pattern set that is all 0x5A */
static void showStepPage(uint8_t pattern, uint8_t track, uint8_t step)
{
	reset();
	memset(&seq_patternSet, 0x5A, sizeof seq_patternSet);
	memcpy(&patternsBefore, &seq_patternSet, sizeof seq_patternSet);
	menu_activePage = SEQ_PAGE;
	viewedPattern = pattern;
	activeVoice = track;
	frontParser_activeTrack = track;
	activeParameters[PAR_ACTIVE_STEP] = step;
	activeParameters[PAR_STEP_NOTE] = 0x5A;
}

/** a Note On from MIDI in on channel 1, which every voice and the global channel have here */
static void noteOn(uint8_t note)
{
	MidiMsg msg = { .status = NOTE_ON, .data1 = note, .data2 = 100 };
	midiParser_parseMidiMessage(msg);
}

/** bytes of the pattern set that differ from showStepPage() */
static long changedPatterns(void)
{
	const uint8_t *now = (const uint8_t *)&seq_patternSet, *before = (const uint8_t *)&patternsBefore;
	long n = 0;
	for (size_t i = 0; i < sizeof seq_patternSet; i++)
		if (now[i] != before[i])
			n++;
	return n;
}

static const uint8_t patterns[] = { 0, 17, NUM_PATTERN - 1 };

TEST(midicc_note_on_sets_the_note_of_the_active_step)
{
	for (unsigned p = 0; p < sizeof patterns; p++)
		for (uint8_t track = 0; track < NUM_TRACKS; track++)
			for (int step = 0; step < NUM_STEPS; step++) {
				showStepPage(patterns[p], track, (uint8_t)step);
				noteOn(36);
				ASSERT_EQ(seq_patternSet.seq_subStepPattern[patterns[p]][track][step].note, 36);
				ASSERT_EQ(changedPatterns(), 1);
				ASSERT_EQ(activeParameters[PAR_STEP_NOTE], 36);
			}
}

TEST(midicc_note_on_past_the_64_steps_changes_no_pattern)
{
	/* NRPN 121 sets the active step to 0-127; the panel sets it to 0-63 */
	for (unsigned p = 0; p < sizeof patterns; p++)
		for (uint8_t track = 0; track < NUM_TRACKS; track++)
			for (int step = NUM_STEPS; step <= 128; step = step == 128 ? 255 : step + 1) {
				showStepPage(patterns[p], track, (uint8_t)step);
				noteOn(36);
				ASSERT_EQ(changedPatterns(), 0);
				ASSERT_EQ(activeParameters[PAR_STEP_NOTE], 0x5A);
			}
}

TEST_MAIN()

//------------------------------------------------------------------------------
// Firmware state that MidiParser.c reads and writes
//------------------------------------------------------------------------------
uint8_t activeParameters[NUM_PARAMS];
uint8_t frontParser_activeTrack;
uint8_t menu_activePage;
volatile uint32_t systick_ticks;
uint8_t mixer_audioRouting[6];
float mixer_decimation_rate[7];
DrumVoice voiceArray[NUM_DRUM_VOICES];
SnareVoice snareVoice;
CymbalVoice cymbalVoice;
HiHatVoice hatVoice;
ModulationNode velocityModulators[6];
FxBus fxBus;
PatternSet seq_patternSet;
DWT_Type host_dwt;

//------------------------------------------------------------------------------
// Stand-ins for the functions it calls
//------------------------------------------------------------------------------
void modNode_originalValueChanged(uint16_t idx)
{
	modNodeCalls++;
	modNodeLastIdx = idx;
}

void Compressor_setRatio(Compressor *c, uint8_t r) { (void)c; (void)r; }
void Cymbal_setPan(const uint8_t pan) { (void)pan; }
void DecayEg_setDecay(DecayEg *eg, uint8_t data2) { (void)eg; (void)data2; }
void DecayEg_setSlope(DecayEg *eg, uint8_t data2) { (void)eg; (void)data2; }
void FxBus_prepareFxChange(void) {}
void FxBus_setParameter(uint8_t idx, float value) { (void)idx; (void)value; }
void FxBus_updateParameter(void) {}
void HiHat_setPan(const uint8_t pan) { (void)pan; }
void Ringmod_setWave(Ringmod *rm, uint8_t w) { (void)rm; (void)w; }
void atmo_setParam(uint16_t paramNr, float v) { (void)paramNr; (void)v; }	/* LICHOMAT v0.4 (plan A3) */
void SVF_directSetFilterValue(ResonantFilter *filter, float val) { (void)filter; (void)val; }
void SVF_setDrive(ResonantFilter *filter, float drive) { (void)filter; (void)drive; }
void SVF_setReso(ResonantFilter *filter, float feedback) { (void)filter; (void)feedback; }
void Snare_setPan(const uint8_t pan) { (void)pan; }
void lfo_setFreq(Lfo *lfo, float f) { (void)lfo; (void)f; }
void lfo_setSync(Lfo *lfo, uint8_t sync) { (void)lfo; (void)sync; }
uint8_t menu_getActiveVoice(void) { return activeVoice; }
uint8_t menu_getSubPage(void) { return 0; }
uint8_t menu_getViewedPattern(void) { return viewedPattern; }
void menu_repaint(void) {}
void menu_switchSubPage(uint8_t subPageNr, uint8_t toggle) { (void)subPageNr; (void)toggle; }
void osc_recalcFreq(OscInfo *osc) { (void)osc; }
void seq_addNote(uint8_t trackNr, uint8_t vel, uint8_t note) { (void)trackNr; (void)vel; (void)note; }
uint8_t seq_getExtSync(void) { return 0; }
uint8_t seq_isRunning(void) { return 0; }
uint8_t seq_isTrackMuted(uint8_t trackNr) { (void)trackNr; return 0; }
void seq_recordAutomation(uint8_t voice, uint8_t dest, uint8_t value) { (void)voice; (void)dest; (void)value; }
void seq_sendVoiceNoteOffLater(const uint8_t voice) { (void)voice; }
void seq_sendVoiceNoteOn(const uint8_t voice, const uint8_t note, const uint8_t veloc) { (void)voice; (void)note; (void)veloc; }
void seq_setMute(uint8_t trackNr, uint8_t isMuted) { (void)trackNr; (void)isMuted; }
void seq_setRunning(uint8_t isRunning) { (void)isRunning; }
void setDistortionShape(Distortion *dist, float shape) { (void)dist; (void)shape; }
void setPan(const uint8_t voiceNr, const uint8_t pan) { (void)voiceNr; (void)pan; }
double slopeEg2_calcDecay(uint8_t data2) { (void)data2; return 0.0; }
void slopeEg2_setAttack(SlopeEg2 *eg, uint8_t data2, uint8_t isSync) { (void)eg; (void)data2; (void)isSync; }
void slopeEg2_setDecay(SlopeEg2 *eg, uint8_t data2, uint8_t isSync) { (void)eg; (void)data2; (void)isSync; }
void slopeEg2_setSlope(SlopeEg2 *eg, uint8_t data2) { (void)eg; (void)data2; }
void transient_setWaveform(TransientGenerator *transient, const uint8_t waveform) { (void)transient; (void)waveform; }
void uart_sendFrontpanelByte(uint8_t data) { (void)data; }
void uart_sendMidi(MidiMsg msg) { (void)msg; }
void usb_sendMidi(MidiMsg msg) { (void)msg; }
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel) { (void)voice; (void)note; (void)vel; }
