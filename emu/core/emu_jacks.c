/*
 * emu_jacks.c
 *
 * The sync jacks of the LICHOMAT emulator (stage 1b spec 4.5).
 * - Clock in, PD4, falling edge: EXTI PR bit 4, then EXTI4_IRQHandler() once
 *   the line is enabled.
 * - Reset in, PD5, both edges, inverted: GPIOD->IDR bit 5 is the pin (low means
 *   the reset is on), EXTI PR bit 5, then EXTI9_5_IRQHandler().
 * - Clock out, PC13: sampled after every body and every pass; each change is
 *   an edge at that time.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "emu_internal.h"

static int emu_clockOutLevel;

static void emu_jacksClockInput(const emu_input_t *in)
{
	(void)in;
	emu_extiRaise(4u);
}

static void emu_jacksResetInput(const emu_input_t *in)
{
	if (in->b)
		GPIOD->IDR &= ~(uint32_t)GPIO_PIN_5;	/* on: the pin is low */
	else
		GPIOD->IDR |= GPIO_PIN_5;
	emu_extiRaise(5u);
}

static void emu_jacksSample(void)
{
	const int level = (GPIOC->ODR & GPIO_PIN_13) ? 1 : 0;
	if (level == emu_clockOutLevel)
		return;
	emu_clockOutLevel = level;
	emu_count.clockEdges++;
	if (emu_cfg.sinks.clockOut)
		emu_cfg.sinks.clockOut(emu_cfg.sinks.ctx, emu_now(), level);
}

void emu_inClockPulse(uint64_t t)
{
	emu_inputPush(t, EMU_IN_CLOCK, 0, 0, 0.0);
}

void emu_inReset(uint64_t t, int on)
{
	emu_inputPush(t, EMU_IN_RESET, 0, on ? 1 : 0, 0.0);
}

void emu_jacksReset(const emu_config_t *cfg)
{
	(void)cfg;
	emu_clockOutLevel = 0;
	GPIOD->IDR |= GPIO_PIN_5;	/* reset in off */
	emu_inputHandler(EMU_IN_CLOCK, emu_jacksClockInput);
	emu_inputHandler(EMU_IN_RESET, emu_jacksResetInput);
	emu_addSampler(emu_jacksSample);
}
