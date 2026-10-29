/*
 * test_fwchanges.c
 *
 * M15 (stage 1b spec 7, 12.1): the firmware changes F1, F2, F3, F9, F11 and F12
 * in the emulator, each test a boot without a card (F11's also one with a card).
 * - F1: CC 1 on all 16 channels, with every modulation node at destination 0.
 * - F2: the morph target is kit 0 after a boot without a card, and a morph runs.
 * - F3: the two clear-automation pages show the base build's rows. The base is
 *   95adb57, the commit before F3, built with -D_FORTIFY_SOURCE=0 (see baseRows);
 *   with PRINT_ROWS defined the test prints its rows instead of comparing them.
 * - F9: voice 1 in band 10 reads no sample past its table row. UBSan's bounds
 *   check is on in calcWavetableOscBlock() and calcFmBlock() and fires on any
 *   table[row][1024]; CI runs the same test under ASan.
 * - F11: a power-on with the knobs off 0 degrees, with and without a card: the
 *   boot's first adc_checkPots() edits, and the edit reads the morph target.
 * - F12: a pattern select by the step buttons fills the Euclid parameters from
 *   the active voice, not from track 16 past the generator's arrays.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <sys/wait.h>

#include "stm32f7xx_hal.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "menu/copyClearTools.h"
#include "Preset/PresetManager.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/EuklidGenerator.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Oscillator.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/MIDI/ParameterArray.h"
#include "frontPanelParser_ui.h"
#include "emu_test.h"

#define MS EMU_CYCLES_PER_MS

/* the audio sink: the energy of out 1 (L1 and R1) since the last reset */
static double energy;
static uint64_t frames;

static void audioSink(void *ctx, uint64_t t, const int16_t *out1, const int16_t *out2)
{
	(void)ctx;
	(void)t;
	(void)out2;
	for (unsigned i = 0; i < 2u * EMU_BLOCK_FRAMES; i++)
		energy += (double)out1[i] * out1[i];
	frames += EMU_BLOCK_FRAMES;
}

/* the highest band (table row) of voice 1's oscillator at any block request */
static unsigned maxBand;

static void bandHook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	(void)now;
	if (why == EMU_HOOK_BLOCK && voiceArray[0].osc.tableOffset > maxBand)
		maxBand = voiceArray[0].osc.tableOffset;
}

static void bootNoCard(void)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	cfg.sinks.audio = audioSink;
	cfg.hook = bandHook;
	emu_testBoot(&cfg);
	emu_testRunMs(100);
}

static uint64_t passes(void)
{
	emu_counters_t c;
	emu_getCounters(&c);
	return c.passes;
}

/* the modulation nodes that CC 1 walks (modNode_originalValueChanged()): the six
 * velocity modulators, then the LFO targets of the six voices */
static const ModulationNode *node(unsigned i)
{
	const ModulationNode *const lfoTargets[6] = {
		&voiceArray[0].lfo.modTarget, &voiceArray[1].lfo.modTarget, &voiceArray[2].lfo.modTarget,
		&snareVoice.lfo.modTarget, &cymbalVoice.lfo.modTarget, &hatVoice.lfo.modTarget,
	};
	return i < 6u ? &velocityModulators[i] : lfoTargets[i - 6u];
}

TEST(M15_f1_cc1)
{
	bootNoCard();
	/* F1's case: every node at destination 0, PAR_NONE, which has no pointer */
	ASSERT_TRUE(parameterArray[PAR_NONE].ptr == NULL);
	for (unsigned i = 0; i < 12u; i++)
		ASSERT_EQ(node(i)->destination, PAR_NONE);
	uint8_t cc1[16 * 3];
	for (unsigned ch = 0; ch < 16u; ch++)
	{
		cc1[3 * ch] = (uint8_t)(0xB0u | ch);
		cc1[3 * ch + 1] = 0x01;
		cc1[3 * ch + 2] = 0x7F;
	}
	const uint64_t before = passes();
	emu_inDin(emu_now(), cc1, sizeof cc1);
	emu_testRunMs(200);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.dinIn, 48);
	ASSERT_TRUE(c.passes - before > 600u);
}

static uint8_t morphTo;

static void morph(void *arg)
{
	(void)arg;
	preset_morph(morphTo);
}

TEST(M15_f2_morph)
{
	bootNoCard();
	ASSERT_TRUE(morphParameters == mainParameters[0]);
	uint64_t before = passes();
	morphTo = 127;
	emu_directCall(morph, NULL);
	emu_testRunMs(200);
	ASSERT_TRUE(passes() - before > 600u);
	before = passes();
	morphTo = 0;
	emu_directCall(morph, NULL);
	emu_testRunMs(200);
	ASSERT_TRUE(passes() - before > 600u);
}

/* --- F3 --------------------------------------------------------------------------- */

static uint8_t clearTarget;

static void clearPage(void *arg)
{
	(void)arg;
	copyClear_armClearMenu(1);
	copyClear_setClearTarget(clearTarget);
	menu_repaintAll();
}

#ifndef PRINT_ROWS
/* The base build's rows: 95adb57 with -D_FORTIFY_SOURCE=0 and without sanitizers
 * (-DLICHOMAT_EMU_SANITIZE=), because UBSan's alignment check stops it in
 * mixer.c before F7 (eb08240) aligns the DMA buffers. */
static const char baseRows[2][2][17] = {
	{ "clear [autom. 1]", "                " },
	{ "clear [autom. 2]", "                " },
};
#else
static void printRow(const uint8_t *row)
{
	putchar('"');
	for (unsigned i = 0; i < 16u; i++)
	{
		if (row[i] >= 0x20u && row[i] < 0x7Fu && row[i] != '"' && row[i] != '\\')
			putchar(row[i]);
		else
			printf("\\x%02x", row[i]);
	}
	printf("\"\n");
}
#endif

TEST(M15_f3_clear_pages)
{
	bootNoCard();
	static const uint8_t targets[2] = { CLEAR_AUTOMATION1, CLEAR_AUTOMATION2 };
	for (unsigned p = 0; p < 2u; p++)
	{
		clearTarget = targets[p];
		emu_directCall(clearPage, NULL);
		emu_testRunMs(20);
		emu_lcdSnapshot_t s;
		emu_lcdSnapshot(&s);
#ifdef PRINT_ROWS
		printRow(s.chars[0]);
		printRow(s.chars[1]);
#else
		ASSERT_EQ(memcmp(s.chars[0], baseRows[p][0], 16), 0);
		ASSERT_EQ(memcmp(s.chars[1], baseRows[p][1], 16), 0);
#endif
	}
}

/* --- F9 --------------------------------------------------------------------------- */

/* the frontPanel_sendData() of a menu edit, for any parameter number */
static void param(uint16_t nr, uint8_t value)
{
	if (nr < 128u)
		frontPanel_sendData(MIDI_CC, (uint8_t)nr, value);
	else
		frontPanel_sendData(CC_2, (uint8_t)(nr - 128u), value);
}

static uint8_t wave, fmMode;

/* U8's recipe with less pitch modulation: coarse 127 (12,543.85 Hz), pitch EG
 * decay 64 and slope 64, and pitch modulation 32, not 127. The envelope's peak
 * factor is 1 + 32 x (32/127)^2 = 3.03 (MidiParser.c:94-98, DrumVoice.c:188-190),
 * so the attack sweeps band 10 from 38 kHz down to 14,080 Hz. With 127 the factor
 * is 33 and the frequency 414 kHz, and freq2PhaseIncr1024() (Oscillator.c:47-50)
 * converts a float of 2^32 or more to uint32_t from REAL_FS (44,140 Hz) up, which
 * UBSan's float-cast check reports; F6's f2u32_sat() does not cover that function.
 * Then the oscillator waveform, and FM (PAR_MIX_MOD_1 0) or the mix of both
 * oscillators (1). */
#define PITCH_MOD_AMOUNT 32u

static void band10Voice(void *arg)
{
	(void)arg;
	for (uint8_t track = 1; track < 7u; track++)
		seq_setMute(track, 1);
	param(PAR_OSC_WAVE_DRUM1, wave);
	param(PAR_COARSE1, 127);
	param(PAR_MODAMNT1, PITCH_MOD_AMOUNT);
	param(PAR_MOD_EG1, 64);
	param(PAR_PITCH_SLOPE1, 64);
	param(PAR_MIX_MOD_1, fmMode ? 0 : 1);
}

/* voice 1 16 times, 100 ms apart, on its channel; the note is its note filter (48
 * in the default globals), which plays the default note */
static void trigger16(void)
{
	const uint8_t note = activeParameters[PAR_MIDI_NOTE1] ? activeParameters[PAR_MIDI_NOTE1] : SEQ_DEFAULT_NOTE;
	const uint8_t on[3] = { (uint8_t)(0x90u | midi_MidiChannels[0]), note, 127 };
	const uint64_t t = emu_now();
	for (unsigned k = 0; k < 16u; k++)
		emu_inDin(t + k * 100u * MS, on, 3);
	emu_runUntil(t + 1600u * MS);
}

TEST(M15_f9_band10)
{
	bootNoCard();
	/* calcWavetableOscBlock() for saw, triangle and rect, then calcFmBlock() */
	static const struct { uint8_t wave, fm; } runs[] = {
		{ SAW, 0 }, { TRI, 0 }, { REC, 0 }, { SAW, 1 }, { TRI, 1 }, { REC, 1 },
	};
	for (unsigned r = 0; r < sizeof runs / sizeof runs[0]; r++)
	{
		wave = runs[r].wave;
		fmMode = runs[r].fm;
		emu_directCall(band10Voice, NULL);
		ASSERT_EQ(voiceArray[0].osc.waveform, wave);
		ASSERT_EQ(voiceArray[0].mixOscs, !fmMode);
		energy = 0.0;
		frames = 0;
		maxBand = 0;
		trigger16();
		ASSERT_EQ(maxBand, 10);		/* above 14,080 Hz (Oscillator.c:34-42) */
		ASSERT_TRUE(frames > 0u);
		const double rms = sqrt(energy / (2.0 * (double)frames)) / 32768.0;
		ASSERT_TRUE(20.0 * log10(rms) > -60.0);
	}
}

/* --- F11 -------------------------------------------------------------------------- */

/* The boot's first adc_checkPots() (appMain.c:178) compares with positions that
 * start at 0 (spec 4.6), so a knob at 40 degrees is an edit of +1 (M8). The edit
 * runs preset_getMorphValue() (menu.c:4304), which reads morphParameters[index]
 * (presetManager.c:2263). fw 1.7 set the morph target only later in the boot
 * (appMain.c:356 with a card, F2's line without), and UBSan stopped at the NULL
 * pointer. One boot per process, so a child boots without a card and the parent
 * with one; all four knobs at 40 degrees. */
static void bootKnobs(const char *cardDir)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = cardDir;
	cfg.cardIn = cardDir != NULL;
	for (unsigned k = 0; k < 4u; k++)
		cfg.knobs[k] = 40;
	emu_testBoot(&cfg);
	emu_testRunMs(100);
}

TEST(M15_f11_knobs_at_boot)
{
	const pid_t child = fork();
	ASSERT_TRUE(child >= 0);
	if (child == 0)
	{
		bootKnobs(NULL);
		_exit(emu_booted() && morphParameters == mainParameters[0] ? 0 : 1);
	}
	char dir[512];
	emu_testCard(dir, sizeof dir, 120);
	bootKnobs(dir);
	ASSERT_TRUE(emu_booted());
	ASSERT_TRUE(morphParameters == mainParameters[0]);
	int status = 0;
	ASSERT_EQ(waitpid(child, &status, 0), child);
	ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* --- F12 -------------------------------------------------------------------------- */

/* a press of button b at *t for 50 ms; the next one 100 ms later */
static void press(uint64_t *t, unsigned b)
{
	emu_inButton(*t, b, 1);
	emu_inButton(*t + 50u * MS, b, 0);
	*t += 100u * MS;
}

/* button b with SHIFT held, as the renderer's reload does */
static void shiftPress(uint64_t *t, unsigned b)
{
	emu_inButton(*t, 32, 1);
	emu_inButton(*t + 20u * MS, b, 1);
	emu_inButton(*t + 70u * MS, b, 0);
	emu_inButton(*t + 100u * MS, 32, 0);
	*t += 150u * MS;
}

/* every track's own Euclid values (length 9 + t, steps 1 + t, rotation 3 + t),
 * and a stale mark in the three parameters that the request fills */
static void setEuklid(void *arg)
{
	(void)arg;
	for (uint8_t t = 0; t < 7u; t++)
	{
		euklid_setLength(t, (uint8_t)(9u + t));
		euklid_setSteps(t, (uint8_t)(1u + t), seq_activePattern);
		euklid_setRotation(t, (uint8_t)(3u + t), seq_activePattern);
	}
	activeParameters[PAR_EUKLID_LENGTH] = 0xEE;
	activeParameters[PAR_EUKLID_STEPS] = 0xEE;
	activeParameters[PAR_EUKLID_ROTATION] = 0xEE;
}

TEST(M15_f12_euklid)
{
	bootNoCard();
	uint64_t t = emu_now() + MS;
	press(&t, 3);					/* voice 3: the active voice is 2 */
	emu_runUntil(t);
	ASSERT_EQ(menu_getActiveVoice(), 2);
	emu_directCall(setEuklid, NULL);
	ASSERT_EQ(euklid_getLength(2), 11);
	ASSERT_EQ(euklid_getSteps(2), 3);
	ASSERT_EQ(euklid_getRotation(2), 5);
	/* pattern 20 by the step buttons (the walk of --pattern): SHIFT + step 16
	 * opens the pattern page, bar- three times sets bank 0, bar+ bank 1, then
	 * step 5. The select sends SEQ_REQUEST_EUKLID_PARAMS (buttonHandler.c,
	 * SELECT_MODE_PATTERN), which fw 1.7 sent with menu_activePage (16) as the
	 * track, and UBSan stopped at euklid_getLength()'s index 16 */
	t = emu_now() + MS;
	shiftPress(&t, 12);
	for (unsigned k = 0; k < 3u; k++)
		press(&t, 40);
	press(&t, 0);
	press(&t, 31);
	emu_runUntil(t + 100u * MS);
	char row[17];
	emu_testLcdRow(1, row);
	ASSERT_EQ(strcmp(row, "Current:  21    "), 0);
	ASSERT_EQ(seq_activePattern, 20);
	ASSERT_EQ(menu_getActiveVoice(), 2);
	/* the active voice's values; the unfixed code without UBSan (Release, the host's layout) read
	 * 5, 0, 0 here: euklid_rotation[2] and two bytes after the arrays */
	ASSERT_EQ(activeParameters[PAR_EUKLID_LENGTH], 11);
	ASSERT_EQ(activeParameters[PAR_EUKLID_STEPS], 3);
	ASSERT_EQ(activeParameters[PAR_EUKLID_ROTATION], 5);
	/* the Euclid page (SHIFT + mode 2, SELECT_MODE_PAT_GEN) requests them again
	 * for the active voice (buttonHandler.c:493), so it shows them with and
	 * without the fix */
	t = emu_now() + MS;
	shiftPress(&t, 25);
	emu_runUntil(t + 100u * MS);
	emu_testLcdRow(0, row);
	ASSERT_EQ(strcmp(row, "LEN stp rot mrp "), 0);
	emu_testLcdRow(1, row);
	ASSERT_EQ(strcmp(row, " 11   3   5   0 "), 0);
}

TEST_MAIN()
