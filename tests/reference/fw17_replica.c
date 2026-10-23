/*
 * fw17_replica.c
 *
 * Host-side replica of the firmware 1.7 sequencer timing: seq_tick() (internal
 * clock branch), seq_calcDeltaT(), seq_processFlam(), seq_getShiftFlamPattern(),
 * getShiftValue() and flamIdxToPattern(), copied as literally as possible from
 * Core/Src/Soundengine/Sequencer/sequencer.c. Hardware and UI calls are replaced
 * by logging; timer rates are taken from main.c (TIM2, TIM14).
 *
 * It is the "before" reference for LICHOMAT stage 1, see
 * docs/superpowers/specs/2026-10-23-timing-core-design.md (defects D1-D3, test T20).
 *
 * Build and run on the host: cc -O2 -o fw17_replica fw17_replica.c -lm && ./fw17_replica
 *
 * Copyright (C) 2012-2026 Erica Synths
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define NUM_TRACKS 7
#define NUM_STEPS 64
#define MIDI_PRESCALER_MASK 0x04

typedef struct { uint8_t flam:4; uint8_t shift:4; } StepTiming;
typedef enum { STEP_PREV, STEP_CURRENT, STEP_NEXT } StepPosition;

// ---------------- simulated hardware time ----------------
static const double F_TIM2 = 108e6 / 2.0 / 1501.0;    // seq_tick() call rate (TIM2 IRQ)
static const double F_SYSTICK = 108e6 / 13501.0 / 2.0; // systick_ticks rate (TIM14 IRQ), ~4 kHz
static const double FS = 44140.0;                      // REAL_FS
static const int BLOCK = 64;                           // OUTPUT_DMA_SIZE
static double t_now;
static uint32_t systick_ticks;

// ---------------- pattern (one pattern is enough) ----------------
static uint8_t mainStep[NUM_TRACKS][NUM_STEPS];
static StepTiming timing[NUM_TRACKS][NUM_STEPS];
static uint8_t prob[NUM_TRACKS][NUM_STEPS];
static uint8_t trackLen[NUM_TRACKS];

// ---------------- sequencer state (names as in sequencer.c) ----------------
static float seq_deltaT;
static uint32_t seq_lastTick;
static uint8_t isStepShuffled;
static float seq_shuffle;
static float seq_lastShuffle;
static uint8_t seq_masterStepCnt;
static uint8_t seq_prescaleCounter;
static uint16_t seq_tempo;
static int8_t seq_stepIndex[NUM_TRACKS];
static uint8_t lastWasNoteOn[NUM_TRACKS];
static struct { uint8_t prev, current, next; } seq_rndValue[NUM_TRACKS];
static uint8_t chokeCurrent, chokeNext;                          // firmware: global
static uint8_t chokeCurrentT[NUM_TRACKS], chokeNextT[NUM_TRACKS]; // fix: per track
static int PER_TRACK_CHOKE;

// ---------------- logs ----------------
typedef struct { double t; int track, step, idx, seqStep; } Trig;
static Trig trigs[8192]; static int ntrigs;
static double stepStart[4096]; static int nsteps;
static double midiClk[65536]; static int nclk;
typedef struct { double t; int seqStep, p; } Pulse;
static Pulse pulses[4096 * 24]; static int npulses;

static void seq_calcDeltaT(uint16_t bpm, uint8_t isShuffled)
{
	seq_deltaT 	= (1000*60)/bpm; 	// integer division, exactly as in the firmware
	seq_deltaT /= 96.f;
	seq_deltaT *= 4;
	if(seq_shuffle != 0)
	{
		uint8_t isStepShuffled = isShuffled;
		const float shuffleFactor = isStepShuffled * seq_shuffle;
		const float originalDeltaT = seq_deltaT;
		seq_deltaT += shuffleFactor * originalDeltaT * 16.f;
		seq_deltaT -= seq_lastShuffle * originalDeltaT * 16.f;
		seq_lastShuffle = shuffleFactor;
	}
}

static int8_t getShiftValue(uint8_t in) { return in <= 14 ? (int8_t)in - 7 : 0; }

static uint32_t flamIdxToPattern(uint8_t flamIdx)
{
	switch(flamIdx) {
	default:
	case 0: return 0b100000000000000000000000;
	case 1: return 0b100000000000100000000000;
	case 2: return 0b100000001000000010000000;
	case 3: return 0b100000100000100000100000;
	case 4: return 0b100010001000100010001000;
	case 5: return 0b100100100100100100100100;
	}
}

static uint32_t seq_getShiftFlamPattern(const uint8_t track, uint8_t step, StepPosition stepPos)
{
	uint8_t currentStepActive = mainStep[track][step];
	if(!currentStepActive) return 0;
	const uint8_t flamCurrentStep = timing[track][step].flam;
	int8_t shiftCurrentStep = getShiftValue(timing[track][step].shift);
	uint32_t stepPattern;
	switch(stepPos) {
	case STEP_PREV: if(seq_rndValue[track].prev > prob[track][step]) currentStepActive = 0; break;
	default:
	case STEP_CURRENT: if(seq_rndValue[track].current > prob[track][step]) currentStepActive = 0; break;
	case STEP_NEXT: if(seq_rndValue[track].next > prob[track][step]) currentStepActive = 0; break;
	}
	if(!currentStepActive) return 0;
	stepPattern = flamIdxToPattern(flamCurrentStep);
	shiftCurrentStep *= 3;
	if(shiftCurrentStep == 0) {
		return stepPos == STEP_CURRENT ? (stepPattern & 0xFFFFFF) : 0;
	} else if(shiftCurrentStep > 0) {
		if(stepPos == STEP_PREV) return (stepPattern << (24-shiftCurrentStep)) & 0xFFFFFF;
		if(stepPos == STEP_NEXT) return 0;
		return (stepPattern >> shiftCurrentStep) & 0xFFFFFF;
	} else {
		shiftCurrentStep *= -1;
		if(stepPos == STEP_PREV) return 0;
		if(stepPos == STEP_NEXT) return (stepPattern >> (24-shiftCurrentStep)) & 0xFFFFFF;
		return (stepPattern << shiftCurrentStep) & 0xFFFFFF;
	}
}

static void seq_triggerVoice(int track, int step, int idx)
{
	trigs[ntrigs++] = (Trig){ t_now, track, step, idx, nsteps - 1 };
}

static void seq_processFlam(uint8_t idx)
{
	if(idx==0) {
		chokeCurrent=0; chokeNext=0;
		memset(chokeCurrentT, 0, sizeof chokeCurrentT); memset(chokeNextT, 0, sizeof chokeNextT);
	}
	for(uint8_t track=0;track<NUM_TRACKS;track++)
	{
		const uint8_t length = trackLen[track];
		const uint8_t currentStep = seq_stepIndex[track];
		const uint8_t nextStep = (seq_stepIndex[track]+1)%length;
		const uint8_t prevStep = seq_stepIndex[track] != 0 ? seq_stepIndex[track] - 1 : length-1;
		uint32_t currentStepPattern = seq_getShiftFlamPattern(track, currentStep, STEP_CURRENT);
		uint32_t nextStepPattern 	= seq_getShiftFlamPattern(track, nextStep, STEP_NEXT);
		uint32_t prevStepPattern 	= seq_getShiftFlamPattern(track, prevStep, STEP_PREV);
		uint8_t prevActive 		= (prevStepPattern 		& (1UL<<(23-idx)) ) != 0;
		uint8_t currentActive 	= (currentStepPattern 	& (1UL<<(23-idx)) ) != 0;
		uint8_t nextActive 		= (nextStepPattern 		& (1UL<<(23-idx)) ) != 0;
		uint8_t *cCur  = PER_TRACK_CHOKE ? &chokeCurrentT[track] : &chokeCurrent;
		uint8_t *cNext = PER_TRACK_CHOKE ? &chokeNextT[track]    : &chokeNext;
		if(nextActive) {
			lastWasNoteOn[track] = 1; *cNext = 1; seq_triggerVoice(track, nextStep, idx);
		} else if((currentActive) && (*cNext == 0)) {
			lastWasNoteOn[track] = 1; *cCur = 1; seq_triggerVoice(track, currentStep, idx);
		} else if((prevActive) && (*cNext == 0) && (*cCur == 0)) {
			lastWasNoteOn[track] = 1; seq_triggerVoice(track, prevStep, idx);
		} else if (lastWasNoteOn[track]) {
			lastWasNoteOn[track] = 0; // voiceControl_noteOff(track)
		}
	}
}

static void seq_nextStep(void)
{
	seq_masterStepCnt++;
	stepStart[nsteps++] = t_now;
	for(int i=0;i<NUM_TRACKS;i++) {
		seq_stepIndex[i]++;
		if((seq_stepIndex[i]) >= trackLen[i] || (seq_stepIndex[i] & 0x3f) == 0) seq_stepIndex[i] = 0;
		if(mainStep[i][seq_stepIndex[i]]) {
			seq_rndValue[i].prev = seq_rndValue[i].current;
			seq_rndValue[i].current = seq_rndValue[i].next;
			seq_rndValue[i].next = rand() % 100 + 1;
		}
	}
}

// internal-clock branch of seq_tick()
static void seq_tick(void)
{
	if(seq_deltaT == -1) { seq_deltaT = 32000; seq_nextStep(); return; }
	if(systick_ticks-seq_lastTick >= seq_deltaT)
	{
		float rest = systick_ticks-seq_lastTick - seq_deltaT;
		seq_lastTick = systick_ticks;
		seq_calcDeltaT(seq_tempo, isStepShuffled);
		isStepShuffled = 0;
		seq_deltaT = seq_deltaT - rest;
		if(seq_prescaleCounter == 0) seq_nextStep();
		pulses[npulses++] = (Pulse){ t_now, nsteps - 1, seq_prescaleCounter };
		seq_processFlam(seq_prescaleCounter);
		if((seq_prescaleCounter%MIDI_PRESCALER_MASK) == 0) midiClk[nclk++] = t_now;
		seq_prescaleCounter++;
		if(seq_prescaleCounter==23) isStepShuffled = seq_masterStepCnt%2 == 1;
		if(seq_prescaleCounter>=24) seq_prescaleCounter=0;
	}
}

// ---------------- harness ----------------
static void reset(uint16_t bpm, float shuffle, int perTrackChoke)
{
	memset(mainStep, 0, sizeof mainStep); memset(timing, 0, sizeof timing);
	for(int t=0;t<NUM_TRACKS;t++) { trackLen[t] = 16; for(int s=0;s<NUM_STEPS;s++) { prob[t][s] = 100; timing[t][s].shift = 7; } }
	seq_tempo = bpm; seq_shuffle = shuffle; seq_lastShuffle = 0; isStepShuffled = 0;
	seq_deltaT = 0; seq_lastTick = 0; seq_prescaleCounter = 0; seq_masterStepCnt = 0;
	memset(seq_stepIndex, -1, sizeof seq_stepIndex); memset(lastWasNoteOn, 0, sizeof lastWasNoteOn);
	memset(seq_rndValue, 0, sizeof seq_rndValue);
	PER_TRACK_CHOKE = perTrackChoke;
	ntrigs = nsteps = nclk = npulses = 0;
}

static void run(int steps)
{
	for(long k=0; nsteps <= steps; k++) {
		t_now = k / F_TIM2;
		systick_ticks = (uint32_t)floor(t_now * F_SYSTICK);
		seq_tick();
	}
}

static double stepLenFromTempo(uint16_t bpm) { return 60.0 / bpm / 4.0; }

static void printTrigs(const char *title, int seqStepFrom, int seqStepTo)
{
	printf("  %s\n", title);
	for(int i=0;i<ntrigs;i++) {
		if(trigs[i].seqStep < seqStepFrom || trigs[i].seqStep > seqStepTo) continue;
		double ref = stepStart[seqStepFrom];
		printf("    track %d step %2d  pulse %2d  t=%7.2f ms (from step %d start)\n",
			trigs[i].track, trigs[i].step, trigs[i].idx, (trigs[i].t - ref)*1e3, seqStepFrom);
	}
}

int main(void)
{
	printf("== 1. Tempo accuracy (internal clock, integer division in seq_calcDeltaT) ==\n");
	uint16_t bpms[] = {120, 125, 128, 130, 133, 135, 137, 140, 145};
	for(unsigned i=0;i<sizeof bpms/sizeof bpms[0];i++) {
		reset(bpms[i], 0, 0); run(1024);
		double avg = (stepStart[1024] - stepStart[0]) / 1024.0;
		printf("  set %3u BPM -> plays %.3f BPM (%+.3f%%)\n", bpms[i], 60.0/(avg*4), (stepLenFromTempo(bpms[i])/avg - 1)*100);
	}

	printf("\n== 2. Audio-block jitter of straight 16ths at 130 BPM (64-sample blocks) ==\n");
	{
		reset(130, 0, 0); mainStep[0][0]=mainStep[0][4]=mainStep[0][8]=mainStep[0][12]=1; run(256);
		double T = (stepStart[256]-stepStart[0])/256.0, tb = BLOCK/FS, mn=1e9, mx=-1e9;
		for(int i=0;i<ntrigs;i++) {
			double onset = ceil(trigs[i].t / tb) * tb;           // audible from next rendered block
			double ideal = stepStart[0] + trigs[i].seqStep * T;
			double e = onset - ideal; if(e<mn) mn=e; if(e>mx) mx=e;
		}
		printf("  trigger onset spread (max-min vs ideal grid): %.2f ms (block = %.2f ms, systick = %.2f ms)\n", (mx-mn)*1e3, tb*1e3, 1e3/F_SYSTICK);
	}

	printf("\n== 3. Shuffle: where do the 24 pulses of a swung step land? (130 BPM, shuffle 64/127) ==\n");
	{
		const float s = 64/127.f;
		reset(130, s, 0); run(64);
		double T = (stepStart[60]-stepStart[0])/60.0; // measured (includes the tempo error)
		double P = T / 24.0;
		// find a swung step: odd stepIndex <=> seqStep index 1,3,5,...
		int k = 5;
		printf("  pulse  t_actual-t_straight [ms]   (step %d, nominal pulse = %.2f ms, swing delay = %.1f pulses = %.2f ms)\n", k, P*1e3, s*16, s*16*P*1e3);
		for(int i=0;i<npulses;i++) if(pulses[i].seqStep == k) {
			double straight = stepStart[k-1] + T + pulses[i].p * P; // grid from previous (unswung) step start
			printf("    %2d   %+7.2f\n", pulses[i].p, (pulses[i].t - straight)*1e3);
		}
		double mn=1e9, mx=0;
		for(int i=40;i<nclk;i++) { double d = midiClk[i]-midiClk[i-1]; if(d<mn) mn=d; if(d>mx) mx=d; }
		printf("  MIDI clock out interval: min %.3f ms, max %.3f ms (straight would be %.3f ms)\n", mn*1e3, mx*1e3, 60.0/130/24*1e3);
	}

	printf("\n== 4. Ratchet x4 on a swung step (track 5, step 1), 130 BPM, shuffle 64/127 ==\n");
	{
		reset(130, 64/127.f, 0); mainStep[5][1]=1; timing[5][1].flam=3; run(20);
		double P = (stepStart[20]-stepStart[0])/20.0/24.0;
		char title[160];
		snprintf(title, sizeof title, "hits (ideal if the whole step were swung: 0.00, %.2f, %.2f, %.2f ms after the swung hit)", 6*P*1e3, 12*P*1e3, 18*P*1e3);
		printTrigs(title, 1, 1);
	}

	printf("\n== 5. Microtiming +3 (= +9 pulses) on a swung step (track 3, step 1), 130 BPM, shuffle 64/127 ==\n");
	{
		reset(130, 64/127.f, 0); mainStep[3][1]=1; timing[3][1].shift=10; run(20);
		double P = stepLenFromTempo(130)/24.0;
		printTrigs("hit", 1, 1);
		printf("    expected if swing applied to the whole step: %.2f ms after the swung step start; unswung: %.2f ms\n", 0.0 + 9*P*1e3, 9*P*1e3 - (64/127.f)*16*P*1e3);
	}

	for(int fix=0; fix<=1; fix++) {
		printf("\n== 6%c. Cross-track choke, %s ==\n", fix?'b':'a', fix?"per-track choke flags (fix)":"firmware (global choke flags)");
		reset(130, 0, fix);
		// hat: x4 ratchet on step 0; snare: step 1 shifted -2 (= -6 pulses, lands on pulse 18 of step 0)
		mainStep[5][0]=1; timing[5][0].flam=3;
		mainStep[3][1]=1; timing[3][1].shift=5;
		// kick on step 3; hat on step 2 with x2 ratchet + shift +4 (second hit spills to pulse 0 of step 3)
		mainStep[0][3]=1;
		mainStep[5][2]=1; timing[5][2].flam=1; timing[5][2].shift=11;
		run(6);
		printTrigs("steps 0-3 (expect hat x4: pulses 0,6,12,18 of step 0; hat x2+shift: pulse 12 of step 2 and pulse 0 of step 3)", 0, 3);
	}
	return 0;
}
