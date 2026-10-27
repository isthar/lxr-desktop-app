/*
 * stm32f7xx_hal.h
 *
 * Host stand-in for the STM32 HAL, for host tests that compile firmware files
 * (test_buttons.c compiles Core/Src/buttonHandler.c, test_midicc.c
 * Core/Src/Soundengine/MIDI/MidiParser.c, test_sequencer.c
 * Core/Src/Soundengine/Sequencer/sequencer.c, test_presets.c
 * Core/Src/Preset/presetManager.c, test_delay.c
 * Core/Src/Soundengine/DSPAudio/FX/Delay.c, test_dsp.c DSPAudio/ResonantFilter.c,
 * Decay.c and Oscillator.c). It has only what the headers of those files need:
 * the handle types that main.h, IO/DIO.h and DSPAudio/random.h name, the DWT and
 * clock calls of the inline functions in delay.h, the CMSIS calls of those in
 * DSPAudio/BufferTools.h, of Delay.c and of Oscillator.c, the TIM2 calls of
 * buttonHandler.c, the interrupt mask of sequencer.c, and the tick and the SD
 * card information type of presetManager.c and FATFS/Target/bsp_driver_sd.h.
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
typedef struct { int unused_; } HAL_SD_CardInfoTypeDef;
typedef struct { volatile uint32_t CTRL; volatile uint32_t CYCCNT; } DWT_Type;

extern DWT_Type host_dwt;
#define DWT (&host_dwt)

/* The CMSIS intrinsics of the delay (DSPAudio/FX/Delay.c, test_delay.c), of the
 * oscillators (DSPAudio/Oscillator.c, test_dsp.c) and of DSPAudio/BufferTools.h,
 * with the results of the Arm instructions: QADD16 and QSUB16 add and subtract
 * the two halfwords with signed saturation, SSAT saturates a signed value to sat
 * bits (the CMSIS macro takes its argument as int32_t, as this prototype does),
 * PKHBT is the C macro of cmsis_gcc.h, and CLZ counts the leading zero bits (32
 * for 0). The APSR read gives no flags set (calcNoise() reads its V flag). */
static inline int32_t host_ssat(int32_t val, uint32_t sat)
{
	const int32_t max = (int32_t)((1u << (sat - 1)) - 1u);
	const int32_t min = -max - 1;
	return val > max ? max : (val < min ? min : val);
}
static inline uint32_t __QADD16(uint32_t op1, uint32_t op2)
{
	const int32_t lo = (int16_t)(op1 & 0xFFFFu) + (int16_t)(op2 & 0xFFFFu);
	const int32_t hi = (int16_t)(op1 >> 16) + (int16_t)(op2 >> 16);
	return ((uint32_t)(uint16_t)host_ssat(hi, 16) << 16) | (uint16_t)host_ssat(lo, 16);
}
static inline uint32_t __QSUB16(uint32_t op1, uint32_t op2)
{
	const int32_t lo = (int16_t)(op1 & 0xFFFFu) - (int16_t)(op2 & 0xFFFFu);
	const int32_t hi = (int16_t)(op1 >> 16) - (int16_t)(op2 >> 16);
	return ((uint32_t)(uint16_t)host_ssat(hi, 16) << 16) | (uint16_t)host_ssat(lo, 16);
}
static inline int32_t __SSAT(int32_t val, uint32_t sat) { return host_ssat(val, sat); }
#define __PKHBT(ARG1, ARG2, ARG3) ((((uint32_t)(ARG1)) & 0x0000FFFFUL) | \
                                   ((((uint32_t)(ARG2)) << (ARG3)) & 0xFFFF0000UL))
static inline uint32_t __CLZ(uint32_t value) { return value ? (uint32_t)__builtin_clz(value) : 32u; }
typedef union {
	struct { uint32_t _reserved0:27; uint32_t Q:1; uint32_t V:1; uint32_t C:1; uint32_t Z:1; uint32_t N:1; } b;
	uint32_t w;
} APSR_Type;
static inline uint32_t __get_APSR(void) { return 0; }

/* The interrupt mask of the mailbox in sequencer.c. The host test runs in one
 * thread with no interrupts, so the mask is only a value kept here. */
__attribute__((unused)) static uint32_t host_primask;
static inline uint32_t __get_PRIMASK(void) { return host_primask; }
static inline void __set_PRIMASK(uint32_t primask) { host_primask = primask; }
static inline void __disable_irq(void) { host_primask = 1; }

uint32_t HAL_RCC_GetHCLKFreq(void);
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim);

#endif /* HOST_STM32F7XX_HAL_H_ */
