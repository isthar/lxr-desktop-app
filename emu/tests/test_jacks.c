/*
 * test_jacks.c
 *
 * M12 and the output-jack part of M8 (stage 1b spec 4.5, 4.6, 12.1).
 * - The sync jacks: clock in reaches the jack sync queue only in sync mode 1, at
 *   4 and at 24 PPQ; reset in sets GPIOD->IDR bit 5 inverted and acts in order
 *   with the pulses; an edge before the line's enable runs the body once at the
 *   enable; the clock out's edges are sampled after every pass, so a pulse inside
 *   one pass is lost; the firmware's clock out at 120 BPM.
 * - The output jacks: a voice routed to an unplugged output sounds on the output
 *   that mixer_checkOutJackAvailable() picks for that state, and nowhere else;
 *   with all jacks plugged each routing stays. A boot without a card has empty
 *   patterns (seq_init()), so DIN Note Ons trigger voice 1, not the sequencer.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>

#include "stm32f7xx_hal.h"
#include "main.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "emu_test.h"

#define MS EMU_CYCLES_PER_MS

void EXTI4_IRQHandler(void);		/* TriggerOut.c */
void EXTI9_5_IRQHandler(void);		/* TriggerOut.c */
uint8_t mixer_checkOutJackAvailable(uint8_t dest);	/* mixer.c, not in mixer.h */

/* --- the sync jacks without a boot ------------------------------------------------ */

/* the core, the jack model and the firmware's two handlers, sequencer and sync
 * queues; only the clock in line is enabled */
static void setupSync(void)
{
	emu_config_t cfg;
	emu_testCore(&cfg);
	emu_jacksReset(&cfg);
	emu_fw.exti4 = EXTI4_IRQHandler;
	emu_fw.exti9_5 = EXTI9_5_IRQHandler;
	seqp_init();
	seq_init();
	HAL_NVIC_SetPriority(EXTI4_IRQn, 3, 0);
	HAL_NVIC_EnableIRQ(EXTI4_IRQn);
}

/* the jack sync entries so far, in stamp order; their source must be the jacks */
static seqq_entry_t entry[64];
static uint64_t stamp[64];

static unsigned popAll(void)
{
	unsigned n = 0;
	seqp_src_t src;
	seqq_entry_t e;
	seqc_time_t t;
	while (seqp_syncPop(seqp_now(), &src, &e, &t))
	{
		if (src != SEQP_SRC_JACK)
			return 999u;
		if (n < 64u)
		{
			entry[n] = e;
			stamp[n] = t;
		}
		n++;
	}
	return n;
}

/* 16 clock in pulses 10 ms apart from 10 ms after now; returns the first one's time */
static uint64_t sixteenPulses(void)
{
	const uint64_t first = emu_now() + 10u * MS;
	for (unsigned k = 0; k < 16u; k++)
		emu_inClockPulse(first + k * 10u * MS);
	HAL_Delay(170);
	return first;
}

TEST(M12_clock_in)
{
	setupSync();
	static const uint8_t ppq[2] = { 4, 24 };
	for (unsigned p = 0; p < 2u; p++)
	{
		seq_setExtSync(SEQC_SYNC_ANALOG);
		seq_setClockInPpq(ppq[p]);
		const uint64_t first = sixteenPulses();
		ASSERT_EQ(popAll(), 16);
		for (unsigned k = 0; k < 16u; k++)
		{
			ASSERT_EQ(entry[k].type, SEQC_EV_PULSE);
			ASSERT_EQ(stamp[k], first + k * 10u * MS);
			ASSERT_EQ(entry[k].stamp, (uint32_t)(first + k * 10u * MS));
		}
	}
	static const uint8_t other[2] = { SEQC_SYNC_INTERNAL, SEQC_SYNC_MIDI };
	for (unsigned m = 0; m < 2u; m++)
	{
		seq_setExtSync(other[m]);
		sixteenPulses();
		ASSERT_EQ(popAll(), 0);
		ASSERT_FALSE(EXTI->PR & (1u << 4));	/* the handler ran and cleared its bit */
	}
	ASSERT_EQ(seqp_syncOverflows(), 0);
}

TEST(M12_reset)
{
	setupSync();
	seq_setExtSync(SEQC_SYNC_ANALOG);
	ASSERT_TRUE(GPIOD->IDR & GPIO_PIN_5);			/* reset in off: the pin is high */
	/* two edges before the line is enabled: the pending bit only */
	emu_inReset(1u * MS, 1);
	HAL_Delay(1);
	ASSERT_FALSE(GPIOD->IDR & GPIO_PIN_5);
	emu_inReset(2u * MS, 0);
	HAL_Delay(2);
	ASSERT_TRUE(GPIOD->IDR & GPIO_PIN_5);
	ASSERT_TRUE(EXTI->PR & (1u << 5));
	ASSERT_EQ(popAll(), 0);
	const uint64_t enableAt = emu_now();
	HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3, 0);
	HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);				/* the body runs once, now */
	ASSERT_EQ(popAll(), 1);
	ASSERT_EQ(entry[0].type, SEQC_EV_RESET_OFF);	/* the pin's level at the body */
	ASSERT_EQ(stamp[0], enableAt);
	ASSERT_FALSE(EXTI->PR & (1u << 5));
	/* the gate in order with the pulses: pulses at 10, 20, 30 and 40 ms after t,
	 * the reset on at 15 ms and off at 25 ms */
	const uint64_t t = emu_now();
	for (unsigned k = 1; k <= 4u; k++)
		emu_inClockPulse(t + k * 10u * MS);
	emu_inReset(t + 15u * MS, 1);
	emu_inReset(t + 25u * MS, 0);
	HAL_Delay(20);
	ASSERT_FALSE(GPIOD->IDR & GPIO_PIN_5);			/* on: the pin is low */
	HAL_Delay(25);
	ASSERT_TRUE(GPIOD->IDR & GPIO_PIN_5);
	ASSERT_EQ(popAll(), 6);
	const uint8_t types[6] = { SEQC_EV_PULSE, SEQC_EV_RESET_ON, SEQC_EV_PULSE, SEQC_EV_RESET_OFF,
		SEQC_EV_PULSE, SEQC_EV_PULSE };
	const unsigned at[6] = { 10, 15, 20, 25, 30, 40 };
	for (unsigned k = 0; k < 6u; k++)
	{
		ASSERT_EQ(entry[k].type, types[k]);
		ASSERT_EQ(stamp[k], t + at[k] * MS);
	}
	/* outside sync mode 1 the reset sets the pin but reaches no queue */
	seq_setExtSync(SEQC_SYNC_INTERNAL);
	emu_inReset(emu_now() + MS, 1);
	HAL_Delay(2);
	ASSERT_FALSE(GPIOD->IDR & GPIO_PIN_5);
	ASSERT_EQ(popAll(), 0);
}

/* --- the clock out ------------------------------------------------------------------ */

#define MAX_EDGES 256u
static uint64_t edgeTime[MAX_EDGES];
static int edgeLevel[MAX_EDGES];
static unsigned edgeCount;

static void clockSink(void *ctx, uint64_t t, int level)
{
	(void)ctx;
	if (edgeCount < MAX_EDGES)
	{
		edgeTime[edgeCount] = t;
		edgeLevel[edgeCount] = level;
	}
	edgeCount++;
}

/* a fake pass: PC13 high in pass 10 and low in pass 30; high and low in pass 50 */
static unsigned passIndex;
static uint64_t passTime[64];

static void clockPass(void)
{
	if (passIndex < 64u)
		passTime[passIndex] = emu_now();
	if (passIndex == 10u)
		GPIOC->ODR |= GPIO_PIN_13;
	if (passIndex == 30u)
		GPIOC->ODR &= ~(uint32_t)GPIO_PIN_13;
	if (passIndex == 50u)
	{
		GPIOC->ODR |= GPIO_PIN_13;
		GPIOC->ODR &= ~(uint32_t)GPIO_PIN_13;
	}
	passIndex++;
}

TEST(M12_clock_out_core)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.sinks.clockOut = clockSink;
	emu_corePowerOn(&cfg);
	emu_jacksReset(&cfg);
	emu_fw.pass = clockPass;
	edgeCount = 0;
	passIndex = 0;
	emu_runUntil(60u * EMU_PASS_GRID);
	ASSERT_TRUE(passIndex > 50u);
	ASSERT_EQ(passTime[10], 10u * EMU_PASS_GRID);	/* the passes on the grid */
	ASSERT_EQ(edgeCount, 2);
	ASSERT_EQ(edgeLevel[0], 1);
	ASSERT_EQ(edgeTime[0], passTime[10]);
	ASSERT_EQ(edgeLevel[1], 0);
	ASSERT_EQ(edgeTime[1], passTime[30]);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.clockEdges, 2);					/* the pulse in pass 50 is lost */
}

/* --- with the firmware ---------------------------------------------------------------- */

/* the audio sink: the energy of L1, R1, L2 and R2 since the last reset */
static double energy[4];
static uint64_t frames;

static void audioSink(void *ctx, uint64_t t, const int16_t *out1, const int16_t *out2)
{
	(void)ctx;
	(void)t;
	for (unsigned i = 0; i < EMU_BLOCK_FRAMES; i++)
	{
		energy[0] += (double)out1[2 * i] * out1[2 * i];
		energy[1] += (double)out1[2 * i + 1] * out1[2 * i + 1];
		energy[2] += (double)out2[2 * i] * out2[2 * i];
		energy[3] += (double)out2[2 * i + 1] * out2[2 * i + 1];
	}
	frames += EMU_BLOCK_FRAMES;
}

/* the RMS of channel ch in dBFS (-999 for silence) */
static double rmsDb(unsigned ch)
{
	if (!frames || energy[ch] <= 0.0)
		return -999.0;
	return 20.0 * log10(sqrt(energy[ch] / (double)frames) / 32768.0);
}

static void bootNoCard(void)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	cfg.sinks.audio = audioSink;
	cfg.sinks.clockOut = clockSink;
	emu_testBoot(&cfg);
	emu_testRunMs(100);
}

TEST(M12_fw_clock_out)
{
	bootNoCard();
	ASSERT_EQ(seq_getBpm(), 120);
	ASSERT_FALSE(GPIOC->ODR & GPIO_PIN_13);
	edgeCount = 0;
	const uint64_t press = emu_now();
	emu_inButton(press, 33u, 1);					/* START/STOP */
	emu_inButton(press + 50u * MS, 33u, 0);
	emu_runUntil(press + 2000u * MS);
	ASSERT_TRUE(seq_isRunning());
	ASSERT_TRUE(edgeCount < MAX_EDGES);
	unsigned rising = 0;
	for (unsigned k = 0; k < edgeCount; k++)
	{
		ASSERT_EQ(edgeLevel[k], (k % 2u) ? 0 : 1);	/* rising and falling in turn */
		if (edgeLevel[k] != 1)
			continue;
		rising++;
		if (k + 1u < edgeCount)
		{
			/* one 96-PPQN pulse, 5.208 ms at 120 BPM, on the 250 us pass grid */
			const double high = (double)(edgeTime[k + 1u] - edgeTime[k]) / MS;
			ASSERT_NEAR(high, 60000.0 / 120.0 / 96.0, 0.25);
		}
		if (k >= 2u)
		{
			/* 4 PPQ out (PRE_4_PPQ_OUT): one pulse per 125 ms */
			const double period = (double)(edgeTime[k] - edgeTime[k - 2u]) / MS;
			ASSERT_NEAR(period, 125.0, 0.25);
		}
	}
	ASSERT_TRUE(rising >= 15u && rising <= 17u);	/* 8 per second for 2 s */
	ASSERT_TRUE(edgeTime[0] - press < 10u * MS);	/* the first pulse at the start */
}

/* the output jacks of mixer.h's routings: bits 0-3 for L1, R1, L2, R2 */
static unsigned routeChannels(uint8_t dest)
{
	switch (dest)
	{
	case MIXER_ROUTING_DAC1_STEREO: return 0x3u;
	case MIXER_ROUTING_DAC2_STEREO: return 0xCu;
	case MIXER_ROUTING_DAC1_L: return 0x1u;
	case MIXER_ROUTING_DAC1_R: return 0x2u;
	case MIXER_ROUTING_DAC2_L: return 0x4u;
	case MIXER_ROUTING_DAC2_R: return 0x8u;
	default: return 0u;
	}
}

static void muteOthers(void *arg)
{
	(void)arg;
	for (uint8_t track = 1; track < 7u; track++)
		seq_setMute(track, 1);
}

static uint8_t route, picked;

static void setRoute(void *arg)
{
	(void)arg;
	mixer_audioRouting[0] = route;
}

static void pick(void *arg)
{
	(void)arg;
	picked = mixer_checkOutJackAvailable(route);
}

/* Voice 1 on its MIDI channel every 100 ms for 1 s, measured from now. The note
 * is the voice's note filter, 48 in the default globals (MidiParser.c:1512-1518,
 * presetManager.c:1048), or any note when the filter is 0. */
static void soundVoice1(void)
{
	memset(energy, 0, sizeof energy);
	frames = 0;
	const uint64_t t = emu_now();
	const uint8_t note = activeParameters[PAR_MIDI_NOTE1] ? activeParameters[PAR_MIDI_NOTE1] : SEQ_DEFAULT_NOTE;
	const uint8_t on[3] = { (uint8_t)(0x90u | midi_MidiChannels[0]), note, 127 };
	for (unsigned k = 0; k < 10u; k++)
		emu_inDin(t + k * 100u * MS, on, 3);
	emu_runUntil(t + 1000u * MS);
}

TEST(M8_fw_output_jacks)
{
	bootNoCard();
	emu_directCall(muteOthers, NULL);
	/* each case routes voice 1 to an unplugged output, so that the pick moves it;
	 * the all-plugged cases keep each routing */
	static const struct
	{
		uint8_t plugged, route, want;
	} cases[] = {
		{ EMU_JACKS_ALL & ~EMU_JACK_L1, MIXER_ROUTING_DAC1_L, MIXER_ROUTING_DAC1_R },
		{ EMU_JACKS_ALL & ~EMU_JACK_R1, MIXER_ROUTING_DAC1_R, MIXER_ROUTING_DAC1_L },
		{ EMU_JACKS_ALL & ~EMU_JACK_L2, MIXER_ROUTING_DAC2_L, MIXER_ROUTING_DAC2_R },
		{ EMU_JACKS_ALL & ~EMU_JACK_R2, MIXER_ROUTING_DAC2_R, MIXER_ROUTING_DAC2_L },
		{ EMU_JACK_L2 | EMU_JACK_R2, MIXER_ROUTING_DAC1_L, MIXER_ROUTING_DAC2_L },
		{ EMU_JACKS_ALL, MIXER_ROUTING_DAC1_L, MIXER_ROUTING_DAC1_L },
		{ EMU_JACKS_ALL, MIXER_ROUTING_DAC1_R, MIXER_ROUTING_DAC1_R },
		{ EMU_JACKS_ALL, MIXER_ROUTING_DAC2_L, MIXER_ROUTING_DAC2_L },
		{ EMU_JACKS_ALL, MIXER_ROUTING_DAC2_R, MIXER_ROUTING_DAC2_R },
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
	{
		route = cases[i].route;
		emu_directCall(setRoute, NULL);
		emu_inJacks(emu_now(), cases[i].plugged);
		emu_testRunMs(50);
		emu_directCall(pick, NULL);
		ASSERT_EQ(picked, cases[i].want);
		soundVoice1();
		const unsigned on = routeChannels(picked);
		ASSERT_TRUE(frames >= 44000u);
		for (unsigned ch = 0; ch < 4u; ch++)
		{
			if (on & (1u << ch))
				ASSERT_TRUE(rmsDb(ch) > -60.0);
			else
				ASSERT_TRUE(rmsDb(ch) < -90.0);
		}
	}
}

TEST_MAIN()
