/*
 * stm32f7xx_hal.h
 *
 * Host stand-in for the STM32 HAL, for host tests that compile firmware files
 * (test_buttons.c compiles Core/Src/buttonHandler.c, test_midicc.c
 * Core/Src/Soundengine/MIDI/MidiParser.c, test_sequencer.c
 * Core/Src/Soundengine/Sequencer/sequencer.c). It has only what the headers of
 * those files need: the handle types that main.h, IO/DIO.h and DSPAudio/random.h
 * name, the DWT and clock calls of the inline functions in delay.h, the CMSIS
 * calls of those in DSPAudio/BufferTools.h, the TIM2 calls of buttonHandler.c and
 * the interrupt mask of sequencer.c.
 *
 * It also takes the DTCM placement out of the firmware's declarations: INDTCM and
 * INDTCMZ (main.h, Soundengine/config.h) are __attribute__ ((section(".dtcm"))),
 * which macOS rejects and which means nothing on the host. main.h includes this
 * header before it defines them, and the system headers that the firmware headers
 * include come first, so the macro below only meets the firmware's attribute.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef HOST_STM32F7XX_HAL_H_
#define HOST_STM32F7XX_HAL_H_

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* __attribute__ ((section(".dtcm"))) becomes __attribute__ ((unused)) */
#define section(name) unused

/* The effect headers (DSPAudio/FX/) declare buffers of __fp16, the half-precision
 * storage type of ARM compilers. They only declare them, so where the host
 * compiler has no __fp16 (GCC on x86-64), a 16-bit integer stands in for it. */
#if !defined(__ARM_FP16_FORMAT_IEEE) && !defined(__ARM_FP16_FORMAT_ALTERNATIVE) && !defined(__clang__)
#define __fp16 uint16_t
#endif

#define __STATIC_INLINE static inline

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef struct { int unused_; } I2S_HandleTypeDef;
typedef struct { int unused_; } RNG_HandleTypeDef;
typedef struct { int unused_; } SPI_HandleTypeDef;
typedef struct { int unused_; } TIM_HandleTypeDef;
typedef struct { int unused_; } UART_HandleTypeDef;
typedef struct { volatile uint32_t CTRL; volatile uint32_t CYCCNT; } DWT_Type;

extern DWT_Type host_dwt;
#define DWT (&host_dwt)

/* CMSIS SIMD intrinsics that inline functions of DSPAudio/BufferTools.h call.
 * Declared only: no host test calls those functions, and a call would not link. */
uint32_t __QADD16(uint32_t op1, uint32_t op2);
uint32_t __QSUB16(uint32_t op1, uint32_t op2);

/* The interrupt mask of the mailbox in sequencer.c. The host test runs in one
 * thread with no interrupts, so the mask is only a value kept here. */
__attribute__((unused)) static uint32_t host_primask;
static inline uint32_t __get_PRIMASK(void) { return host_primask; }
static inline void __set_PRIMASK(uint32_t primask) { host_primask = primask; }
static inline void __disable_irq(void) { host_primask = 1; }

uint32_t HAL_RCC_GetHCLKFreq(void);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim);

#endif /* HOST_STM32F7XX_HAL_H_ */
