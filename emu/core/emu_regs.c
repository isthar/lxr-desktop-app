/*
 * emu_regs.c
 *
 * The register and HAL model of the LICHOMAT emulator (stage 1b spec 3.4,
 * 4.2): the register blocks as plain memory, the handles the firmware needs,
 * SysTick, TIM14 and TIM2, the NVIC lines of EXTI4 and EXTI9_5, and the HAL
 * functions that have no model of their own.
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
#include "Soundengine/globals.h"
#include "emu_internal.h"

/* --- register blocks and handles ------------------------------------------------ */

DWT_Type emu_dwt;
CoreDebug_Type emu_coreDebug;
FPU_Type emu_fpu;
EXTI_TypeDef emu_exti;
GPIO_TypeDef emu_gpioB, emu_gpioC, emu_gpioD, emu_gpioE;
TIM_TypeDef emu_tim2, emu_tim14;
USART_TypeDef emu_usart3;

ADC_HandleTypeDef hadc1;
RNG_HandleTypeDef hrng;
SPI_HandleTypeDef hspi1;
TIM_HandleTypeDef htim2 = { TIM2 };
TIM_HandleTypeDef htim14 = { TIM14 };
UART_HandleTypeDef huart3 = { USART3 };

/* as stm32f7xx_it.c defines it; the TIM14 body counts it */
volatile uint32_t systick_ticks;

/* --- SysTick: every 216,000 cycles from power-on ----------------------------------- */

static void emu_sysTickFire(void)
{
	emu_src[EMU_SRC_SYSTICK].pending++;
	emu_src[EMU_SRC_SYSTICK].due += EMU_CYCLES_PER_MS;
}

static void emu_sysTickBody(void)
{
	if (emu_fw.sysTick)
		emu_fw.sysTick();
}

/* --- TIM14: prescaler 13,500 and period 1 at 108 MHz: every 54,004 cycles ---------- */

#define EMU_TIM14_PERIOD 54004u

static void emu_tim14Fire(void)
{
	emu_src[EMU_SRC_TIM14].pending++;
	emu_src[EMU_SRC_TIM14].due += EMU_TIM14_PERIOD;
}

static void emu_tim14Body(void)
{
	systick_ticks++;
	if (emu_fw.tim14)
		emu_fw.tim14();
}

/* --- TIM2: a 32-bit up-counter at 54 MHz (4 cycles per count), ARR 1500 ------------- */

#define EMU_TIM2_ARR 1500u
#define EMU_TIM2_CYCLES 4u

static struct
{
	int running;		/* CR1.CEN */
	int interrupt;		/* DIER.UIE */
	uint32_t base;		/* the count at baseTime */
	uint64_t baseTime;	/* counts happen at baseTime + 4k */
} emu_t2;

/* the count at the current time */
static uint32_t emu_tim2Count(void)
{
	if (!emu_t2.running)
		return emu_t2.base;
	const uint64_t n = (emu_now() - emu_t2.baseTime) / EMU_TIM2_CYCLES;
	if (emu_t2.base <= EMU_TIM2_ARR)
		return (uint32_t)((emu_t2.base + n) % (EMU_TIM2_ARR + 1u));
	const uint64_t toWrap = (uint64_t)UINT32_MAX + 1u - emu_t2.base;
	if (n < toWrap)
		return emu_t2.base + (uint32_t)n;
	return (uint32_t)((n - toWrap) % (EMU_TIM2_ARR + 1u));
}

/* the next update event; above ARR the counter wraps at 2^32 without one */
static void emu_tim2Schedule(void)
{
	TIM2->CR1 = emu_t2.running ? TIM_CR1_CEN : 0u;
	TIM2->DIER = emu_t2.interrupt ? TIM_IT_UPDATE : 0u;
	TIM2->CNT = emu_t2.base;
	if (!emu_t2.running || !emu_t2.interrupt)
	{
		emu_src[EMU_SRC_TIM2].due = EMU_NEVER;
		return;
	}
	const uint64_t n = (emu_now() - emu_t2.baseTime) / EMU_TIM2_CYCLES;
	const uint32_t c = emu_tim2Count();
	const uint64_t toUpdate = c <= EMU_TIM2_ARR ? (uint64_t)(EMU_TIM2_ARR + 1u - c)
		: (uint64_t)UINT32_MAX + 1u - c + EMU_TIM2_ARR + 1u;
	emu_src[EMU_SRC_TIM2].due = emu_t2.baseTime + (n + toUpdate) * EMU_TIM2_CYCLES;
}

static void emu_tim2Fire(void)
{
	emu_src[EMU_SRC_TIM2].pending++;
	emu_t2.base = 0;
	emu_t2.baseTime = emu_src[EMU_SRC_TIM2].due;
	emu_tim2Schedule();
}

static void emu_tim2Body(void)
{
	if (emu_fw.tim2)
		emu_fw.tim2();
}

void emu_tim2Write(uint32_t count)
{
	emu_t2.base = count;
	emu_t2.baseTime = emu_now();
	emu_tim2Schedule();
}

void emu_timSetCounter(TIM_TypeDef *tim, uint32_t count)
{
	if (tim == TIM2)
		emu_tim2Write(count);
	else
		tim->CNT = count;
}

void emu_timEnableIt(TIM_TypeDef *tim, uint32_t it)
{
	if (tim == TIM2)
	{
		if (it & TIM_IT_UPDATE)
			emu_t2.interrupt = 1;
		emu_tim2Schedule();
	}
	else
		tim->DIER |= it;
}

void emu_timDisableIt(TIM_TypeDef *tim, uint32_t it)
{
	if (tim == TIM2)
	{
		if (it & TIM_IT_UPDATE)
			emu_t2.interrupt = 0;
		emu_tim2Schedule();
	}
	else
		tim->DIER &= ~it;
}

/* sets DIER.UIE and CR1.CEN */
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM2)
	{
		if (!emu_t2.running)
			emu_t2.baseTime = emu_now();
		emu_t2.running = 1;
		emu_t2.interrupt = 1;
		emu_tim2Schedule();
	}
	else if (htim->Instance == TIM14)
	{
		if (!(TIM14->CR1 & TIM_CR1_CEN))
			emu_src[EMU_SRC_TIM14].due = emu_now() + EMU_TIM14_PERIOD;
		TIM14->CR1 |= TIM_CR1_CEN;
		TIM14->DIER |= TIM_IT_UPDATE;
	}
	return HAL_OK;
}

/* clears DIER.UIE and CR1.CEN */
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM2)
	{
		emu_t2.base = emu_tim2Count();
		emu_t2.running = 0;
		emu_t2.interrupt = 0;
		emu_tim2Schedule();
	}
	else if (htim->Instance == TIM14)
	{
		TIM14->CR1 &= ~TIM_CR1_CEN;
		TIM14->DIER &= ~TIM_IT_UPDATE;
		emu_src[EMU_SRC_TIM14].due = EMU_NEVER;
	}
	return HAL_OK;
}

/* clears CR1.CEN only: DIER.UIE stays set (menu.c's project save) */
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM2)
	{
		emu_t2.base = emu_tim2Count();
		emu_t2.running = 0;
		emu_tim2Schedule();
	}
	else if (htim->Instance == TIM14)
	{
		TIM14->CR1 &= ~TIM_CR1_CEN;
		emu_src[EMU_SRC_TIM14].due = EMU_NEVER;
	}
	return HAL_OK;
}

/* --- NVIC: the EXTI lines are enabled at the end of the boot ------------------------ */

static int emu_extiEnabled[2];	/* line 4, lines 9-5 */

static emu_src_t emu_extiSource(unsigned line)
{
	return line == 4u ? EMU_SRC_EXTI4 : EMU_SRC_EXTI9_5;
}

void emu_extiRaise(unsigned line)
{
	EXTI->PR |= 1u << line;
	if (emu_extiEnabled[line == 4u ? 0 : 1])
		emu_src[emu_extiSource(line)].pending = 1;
}

static void emu_exti4Body(void)
{
	if (emu_fw.exti4)
		emu_fw.exti4();
}

static void emu_exti9_5Body(void)
{
	if (emu_fw.exti9_5)
		emu_fw.exti9_5();
}

/* the priorities of spec 4.2's table; anything else is a firmware change the
 * model does not know */
void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority)
{
	char what[96];
	if (IRQn != EXTI4_IRQn && IRQn != EXTI9_5_IRQn)
	{
		snprintf(what, sizeof what, "HAL_NVIC_SetPriority: IRQ %d has no model", (int)IRQn);
		emu_fault(what);
		return;
	}
	const uint8_t model = emu_srcPriority[IRQn == EXTI4_IRQn ? EMU_SRC_EXTI4 : EMU_SRC_EXTI9_5];
	if (PreemptPriority != model || SubPriority != 0u)
	{
		snprintf(what, sizeof what, "HAL_NVIC_SetPriority: IRQ %d priority %u.%u, the model has %u.0",
			(int)IRQn, (unsigned)PreemptPriority, (unsigned)SubPriority, (unsigned)model);
		emu_fault(what);
	}
}

/* enables the EXTI4 or EXTI9_5 body; an edge before the enable runs it once now */
void HAL_NVIC_EnableIRQ(IRQn_Type IRQn)
{
	unsigned line;
	if (IRQn == EXTI4_IRQn)
		line = 4u;
	else if (IRQn == EXTI9_5_IRQn)
		line = 5u;
	else
	{
		char what[64];
		snprintf(what, sizeof what, "HAL_NVIC_EnableIRQ: IRQ %d has no model", (int)IRQn);
		emu_fault(what);
		return;
	}
	emu_extiEnabled[line == 4u ? 0 : 1] = 1;
	if (EXTI->PR & (1u << line))
		emu_src[emu_extiSource(line)].pending = 1;
	if (!emu_inBody())
		emu_dispatch();
}

/* --- HAL functions without a model of their own ---------------------------------- */

uint32_t HAL_RCC_GetHCLKFreq(void)
{
	return EMU_CPU_HZ;
}

/* the panel model writes adc_buf itself */
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc, uint32_t *pData, uint32_t Length)
{
	(void)hadc;
	(void)pData;
	(void)Length;
	return HAL_OK;
}

/* --- power-on --------------------------------------------------------------------- */

void emu_regsReset(const emu_config_t *cfg)
{
	(void)cfg;
	memset(&emu_dwt, 0, sizeof emu_dwt);
	memset(&emu_coreDebug, 0, sizeof emu_coreDebug);
	memset(&emu_fpu, 0, sizeof emu_fpu);
	memset(&emu_exti, 0, sizeof emu_exti);
	memset(&emu_gpioB, 0, sizeof emu_gpioB);
	memset(&emu_gpioC, 0, sizeof emu_gpioC);
	memset(&emu_gpioD, 0, sizeof emu_gpioD);
	memset(&emu_gpioE, 0, sizeof emu_gpioE);
	memset(&emu_tim2, 0, sizeof emu_tim2);
	memset(&emu_tim14, 0, sizeof emu_tim14);
	memset(&emu_usart3, 0, sizeof emu_usart3);
	emu_tim2.ARR = EMU_TIM2_ARR;
	emu_tim14.ARR = 1u;
	emu_tim14.PSC = 13500u;
	emu_tim2.PSC = 1u;
	systick_ticks = 0;
	memset(&emu_t2, 0, sizeof emu_t2);
	emu_extiEnabled[0] = emu_extiEnabled[1] = 0;

	emu_src[EMU_SRC_SYSTICK] = (emu_source_t){ EMU_CYCLES_PER_MS, emu_sysTickFire, emu_sysTickBody, 0 };
	emu_src[EMU_SRC_TIM14] = (emu_source_t){ EMU_NEVER, emu_tim14Fire, emu_tim14Body, 0 };
	emu_src[EMU_SRC_TIM2] = (emu_source_t){ EMU_NEVER, emu_tim2Fire, emu_tim2Body, 0 };
	emu_src[EMU_SRC_EXTI4] = (emu_source_t){ EMU_NEVER, NULL, emu_exti4Body, 0 };
	emu_src[EMU_SRC_EXTI9_5] = (emu_source_t){ EMU_NEVER, NULL, emu_exti9_5Body, 0 };
}
