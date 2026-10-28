/*
 * emu_panel.c
 *
 * The front panel of the LICHOMAT emulator (stage 1b spec 4.6, 4.7): the
 * register edge of the unchanged DIO.c, encoder.c and adcPots.c, and the LEDs.
 * Each input path of the firmware compares with its previous poll, so the
 * panel queues events and releases them at the poll points:
 * - buttons 0-39: the 5 bytes of HAL_SPI_Receive() at each dio_process(), at
 *   most one transition per button per scan; button 40 on GPIOB->IDR bit 7;
 * - the encoder: GPIOE->IDR bits 13 (A) and 14 (B), one Gray change per
 *   SysTick; the push on bit 15, active low;
 * - knobs 1-4: two wipers each in adc_buf[0..7], at most 1 degree per pass;
 * - faders 1-6: adc_buf[8..13], at once;
 * - the output jacks: GPIOD->IDR bits 6 and 7, GPIOB->IDR bits 4 and 6.
 * The LEDs are the 5 bytes of HAL_SPI_Transmit() and GPIOB->ODR bit 8.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdio.h>
#include <string.h>

#include "stm32f7xx_hal.h"
#include "emu_internal.h"

#define EMU_NUM_BUTTONS 41u
#define EMU_BUTTON_QUEUE 64u
#define EMU_ENCODER_RUNS 64u
#define EMU_PUSH_MIN_MS 40u

#define EMU_ENC_A (1u << 13)
#define EMU_ENC_B (1u << 14)
#define EMU_ENC_PUSH (1u << 15)
#define EMU_BUTTON40 (1u << 7)
#define EMU_LED40 (1u << 8)

static struct
{
	uint8_t bytes[5];							/* the button bytes, 1 = pressed */
	uint8_t queue[EMU_NUM_BUTTONS][EMU_BUTTON_QUEUE];	/* transitions not yet scanned */
	uint8_t head[EMU_NUM_BUTTONS], count[EMU_NUM_BUTTONS];
	struct { int8_t dir; uint32_t changes; } runs[EMU_ENCODER_RUNS];	/* queued Gray changes */
	unsigned runHead, runCount;
	int angle[4], target[4];					/* knob angles in degrees */
	uint8_t leds[5];							/* the latched LED bytes */
	uint64_t seen;								/* LEDs on since the last snapshot */
} emu_panel;

static uint16_t emu_panelAdcStore[14];

static uint16_t *emu_panelAdc(void)
{
	return emu_fw.adcBuffer ? emu_fw.adcBuffer : emu_panelAdcStore;
}

/* --- buttons -------------------------------------------------------------------- */

static void emu_panelButtonInput(const emu_input_t *in)
{
	const unsigned b = in->a;
	if (b >= EMU_NUM_BUTTONS)
		return;
	if (emu_panel.count[b] == EMU_BUTTON_QUEUE)
	{
		char what[64];
		snprintf(what, sizeof what, "panel: more than %u queued transitions of button %u", EMU_BUTTON_QUEUE, b);
		emu_fault(what);
		return;
	}
	emu_panel.queue[b][(emu_panel.head[b] + emu_panel.count[b]) % EMU_BUTTON_QUEUE] = in->b ? 1u : 0u;
	emu_panel.count[b]++;
}

/* one scan of dio_process(): at most one transition per button */
static void emu_panelScan(void)
{
	for (unsigned b = 0; b < EMU_NUM_BUTTONS; b++)
	{
		if (!emu_panel.count[b])
			continue;
		const uint8_t down = emu_panel.queue[b][emu_panel.head[b]];
		emu_panel.head[b] = (uint8_t)((emu_panel.head[b] + 1u) % EMU_BUTTON_QUEUE);
		emu_panel.count[b]--;
		if (b == 40u)
		{
			if (down)
				GPIOB->IDR |= EMU_BUTTON40;
			else
				GPIOB->IDR &= ~EMU_BUTTON40;
		}
		else if (down)
			emu_panel.bytes[b / 8u] |= (uint8_t)(1u << (b % 8u));
		else
			emu_panel.bytes[b / 8u] &= (uint8_t)~(1u << (b % 8u));
	}
}

HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
	(void)hspi;
	(void)Timeout;
	emu_panelScan();
	memcpy(pData, emu_panel.bytes, Size < 5u ? Size : 5u);
	return HAL_OK;
}

void emu_inButton(uint64_t t, unsigned button, int down)
{
	emu_inputPush(t, EMU_IN_BUTTON, (uint8_t)button, down ? 1 : 0, 0.0);
}

/* --- the encoder ------------------------------------------------------------------ */

/* forward is 00 -> 01 -> 11 -> 10 -> 00, as A<<1 | B */
static const uint8_t emu_grayNext[4] = { 1u, 3u, 0u, 2u };
static const uint8_t emu_grayPrev[4] = { 2u, 0u, 3u, 1u };

static void emu_panelEncoderInput(const emu_input_t *in)
{
	if (in->b == 0)
		return;
	const int8_t dir = in->b > 0 ? 1 : -1;
	const uint32_t changes = 4u * (uint32_t)(in->b > 0 ? in->b : -in->b);
	if (emu_panel.runCount)
	{
		const unsigned last = (emu_panel.runHead + emu_panel.runCount - 1u) % EMU_ENCODER_RUNS;
		if (emu_panel.runs[last].dir == dir)
		{
			emu_panel.runs[last].changes += changes;
			return;
		}
	}
	if (emu_panel.runCount == EMU_ENCODER_RUNS)
	{
		emu_fault("panel: too many queued encoder turns");
		return;
	}
	const unsigned slot = (emu_panel.runHead + emu_panel.runCount) % EMU_ENCODER_RUNS;
	emu_panel.runs[slot].dir = dir;
	emu_panel.runs[slot].changes = changes;
	emu_panel.runCount++;
}

void emu_panelEncoderStep(void)
{
	if (!emu_panel.runCount)
		return;
	const uint32_t idr = GPIOE->IDR;
	const uint8_t state = (uint8_t)(((idr & EMU_ENC_A) ? 2u : 0u) | ((idr & EMU_ENC_B) ? 1u : 0u));
	const uint8_t next = emu_panel.runs[emu_panel.runHead].dir > 0 ? emu_grayNext[state] : emu_grayPrev[state];
	GPIOE->IDR = (idr & ~(EMU_ENC_A | EMU_ENC_B)) | ((next & 2u) ? EMU_ENC_A : 0u) | ((next & 1u) ? EMU_ENC_B : 0u);
	if (--emu_panel.runs[emu_panel.runHead].changes == 0)
	{
		emu_panel.runHead = (emu_panel.runHead + 1u) % EMU_ENCODER_RUNS;
		emu_panel.runCount--;
	}
}

void emu_inEncoder(uint64_t t, int detents)
{
	emu_inputPush(t, EMU_IN_ENCODER, 0, detents, 0.0);
}

static void emu_panelPushInput(const emu_input_t *in)
{
	if (in->b)
		GPIOE->IDR &= ~EMU_ENC_PUSH;	/* active low */
	else
		GPIOE->IDR |= EMU_ENC_PUSH;
}

void emu_inPush(uint64_t t, unsigned ms)
{
	if (ms < EMU_PUSH_MIN_MS)
		ms = EMU_PUSH_MIN_MS;
	const uint64_t start = emu_inputTime(t);
	emu_inputPush(start, EMU_IN_PUSH, 0, 1, 0.0);
	emu_inputPush(start + (uint64_t)ms * EMU_CYCLES_PER_MS, EMU_IN_PUSH, 0, 0, 0.0);
}

/* --- knobs ------------------------------------------------------------------------- */

/* a wiper: a triangle 0-4095 that peaks at peak degrees (spec 4.6) */
static uint16_t emu_wiper(int degrees, int peak)
{
	const int x = (((degrees - peak + 540) % 360) + 360) % 360;
	const int tri = 180 - (x > 180 ? x - 180 : 180 - x);
	return (uint16_t)((tri * 4095 + 90) / 180);
}

static void emu_panelKnobWrite(unsigned k)
{
	uint16_t *adc = emu_panelAdc();
	adc[2u * k] = (uint16_t)(emu_wiper(emu_panel.angle[k], 90) << 4);
	adc[2u * k + 1u] = (uint16_t)(emu_wiper(emu_panel.angle[k], 180) << 4);
}

static void emu_panelKnobInput(const emu_input_t *in)
{
	if (in->a < 4u)
		emu_panel.target[in->a] += in->b;
}

/* at the start of every pass: each knob moves at most 1 degree */
static void emu_panelKnobRamp(void)
{
	for (unsigned k = 0; k < 4u; k++)
	{
		if (emu_panel.angle[k] == emu_panel.target[k])
			continue;
		emu_panel.angle[k] += emu_panel.target[k] > emu_panel.angle[k] ? 1 : -1;
		emu_panelKnobWrite(k);
	}
}

void emu_inKnob(uint64_t t, unsigned knob, int degrees)
{
	if (knob >= 1u && knob <= 4u)
		emu_inputPush(t, EMU_IN_KNOB, (uint8_t)(knob - 1u), degrees, 0.0);
}

/* --- faders and output jacks ---------------------------------------------------------- */

static void emu_panelFaderWrite(unsigned f, double level)
{
	if (!(level > 0.0))
		level = 0.0;
	if (level > 1.0)
		level = 1.0;
	emu_panelAdc()[8u + f] = (uint16_t)((uint32_t)(level * 4095.0) << 4);
}

static void emu_panelFaderInput(const emu_input_t *in)
{
	if (in->a < 6u)
		emu_panelFaderWrite(in->a, in->level);
}

void emu_inFader(uint64_t t, unsigned fader, double level)
{
	if (fader >= 1u && fader <= 6u)
		emu_inputPush(t, EMU_IN_FADER, (uint8_t)(fader - 1u), 0, level);
}

static void emu_panelJacksWrite(uint8_t plugged)
{
	GPIOD->IDR = (GPIOD->IDR & ~(GPIO_PIN_6 | GPIO_PIN_7))
		| ((plugged & EMU_JACK_L1) ? GPIO_PIN_6 : 0u) | ((plugged & EMU_JACK_R1) ? GPIO_PIN_7 : 0u);
	GPIOB->IDR = (GPIOB->IDR & ~(GPIO_PIN_4 | GPIO_PIN_6))
		| ((plugged & EMU_JACK_L2) ? GPIO_PIN_4 : 0u) | ((plugged & EMU_JACK_R2) ? GPIO_PIN_6 : 0u);
}

static void emu_panelJacksInput(const emu_input_t *in)
{
	emu_panelJacksWrite(in->a);
}

void emu_inJacks(uint64_t t, uint8_t plugged)
{
	emu_inputPush(t, EMU_IN_JACKS, plugged, 0, 0.0);
}

/* --- LEDs ---------------------------------------------------------------------------- */

static uint64_t emu_ledMask(void)
{
	uint64_t mask = 0;
	for (unsigned i = 0; i < 5u; i++)
		mask |= (uint64_t)emu_panel.leds[i] << (8u * i);
	if (GPIOB->ODR & EMU_LED40)
		mask |= (uint64_t)1 << 40;
	return mask;
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
	(void)hspi;
	(void)Timeout;
	memcpy(emu_panel.leds, pData, Size < 5u ? Size : 5u);
	emu_panel.seen |= emu_ledMask();
	return HAL_OK;
}

/* after every body and pass: LED 40 is a plain GPIO write */
static void emu_panelSample(void)
{
	emu_panel.seen |= emu_ledMask();
}

uint64_t emu_ledSnapshot(void)
{
	const uint64_t seen = emu_panel.seen | emu_ledMask();
	emu_panel.seen = emu_ledMask();
	return seen;
}

/* --- power-on --------------------------------------------------------------------------- */

void emu_panelReset(const emu_config_t *cfg)
{
	memset(&emu_panel, 0, sizeof emu_panel);
	memset(emu_panelAdcStore, 0, sizeof emu_panelAdcStore);
	GPIOE->IDR |= EMU_ENC_PUSH;		/* the encoder push reads released */
	for (unsigned k = 0; k < 4u; k++)
		emu_panel.angle[k] = emu_panel.target[k] = cfg->knobs[k];
	for (unsigned k = 0; k < 4u; k++)
		emu_panelKnobWrite(k);		/* every knob at its power-on angle, 0 degrees by default */
	for (unsigned f = 0; f < 6u; f++)
		emu_panelFaderWrite(f, cfg->faders[f]);
	emu_panelJacksWrite(cfg->jacks);
	emu_inputHandler(EMU_IN_BUTTON, emu_panelButtonInput);
	emu_inputHandler(EMU_IN_ENCODER, emu_panelEncoderInput);
	emu_inputHandler(EMU_IN_PUSH, emu_panelPushInput);
	emu_inputHandler(EMU_IN_KNOB, emu_panelKnobInput);
	emu_inputHandler(EMU_IN_FADER, emu_panelFaderInput);
	emu_inputHandler(EMU_IN_JACKS, emu_panelJacksInput);
	emu_addPassStart(emu_panelKnobRamp);
	emu_addSampler(emu_panelSample);
}
