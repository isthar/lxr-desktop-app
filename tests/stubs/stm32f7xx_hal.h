/*
 * stm32f7xx_hal.h
 *
 * Host stand-in for the STM32 HAL, for host tests that compile firmware user
 * interface code (test_buttons.c compiles Core/Src/buttonHandler.c). It has only
 * what the headers of buttonHandler.c need: the handle types that main.h and
 * IO/DIO.h name, the DWT and clock calls of the inline functions in delay.h, and
 * the TIM2 calls of buttonHandler.c.
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

#define __STATIC_INLINE static inline

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef struct { int unused_; } I2S_HandleTypeDef;
typedef struct { int unused_; } SPI_HandleTypeDef;
typedef struct { int unused_; } TIM_HandleTypeDef;
typedef struct { int unused_; } UART_HandleTypeDef;
typedef struct { volatile uint32_t CTRL; volatile uint32_t CYCCNT; } DWT_Type;

extern DWT_Type host_dwt;
#define DWT (&host_dwt)

uint32_t HAL_RCC_GetHCLKFreq(void);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim);

#endif /* HOST_STM32F7XX_HAL_H_ */
