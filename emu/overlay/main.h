/*
 * main.h
 *
 * Stand-in for Core/Inc/main.h in the LICHOMAT emulator (stage 1b spec 3.4).
 * The firmware's main.h carries ST's licence notice, and 13 headers in Core/Inc
 * include it from their own directory, so a stub directory cannot shadow it.
 * The emulator's builds map Core/Inc/main.h to this file with clang's
 * -ivfsoverlay (emu/CMakeLists.txt). Written fresh, with the same names.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef __MAIN_H
#define __MAIN_H
#define EMU_OVERLAY_MAIN_H 1	/* emu_firmware.c checks that the overlay is in effect */

#include "stm32f7xx_hal.h"
#include "delay.h"

extern float debugWatch;
extern I2S_HandleTypeDef hi2s2;
extern I2S_HandleTypeDef hi2s3;
extern UART_HandleTypeDef huart3;
extern TIM_HandleTypeDef htim2;

#define USE_SD_CARD 1

void Error_Handler(void);

/* the pins of the LXR-02's board */
#define Clock_Out_Pin GPIO_PIN_13
#define Clock_Out_GPIO_Port GPIOC
#define DIO_LD_Pin GPIO_PIN_2
#define DIO_LD_GPIO_Port GPIOB
#define LCD_D4_Pin GPIO_PIN_7
#define LCD_D4_GPIO_Port GPIOE
#define LCD_D5_Pin GPIO_PIN_8
#define LCD_D5_GPIO_Port GPIOE
#define LCD_D6_Pin GPIO_PIN_9
#define LCD_D6_GPIO_Port GPIOE
#define LCD_D7_Pin GPIO_PIN_10
#define LCD_D7_GPIO_Port GPIOE
#define LCD_E_Pin GPIO_PIN_11
#define LCD_E_GPIO_Port GPIOE
#define LCD_RS_Pin GPIO_PIN_12
#define LCD_RS_GPIO_Port GPIOE
#define Enc_A_Pin GPIO_PIN_13
#define Enc_A_GPIO_Port GPIOE
#define Enc_B_Pin GPIO_PIN_14
#define Enc_B_GPIO_Port GPIOE
#define Enc_Switch_Pin GPIO_PIN_15
#define Enc_Switch_GPIO_Port GPIOE
#define SDIO_CS_Pin GPIO_PIN_0
#define SDIO_CS_GPIO_Port GPIOD
#define SD_CD_Pin GPIO_PIN_1
#define SD_CD_GPIO_Port GPIOD
#define Clock_In_EXTI4_Pin GPIO_PIN_4
#define Clock_In_EXTI4_GPIO_Port GPIOD
#define Clock_In_EXTI4_EXTI_IRQn EXTI4_IRQn
#define Reset_In_EXTI5_Pin GPIO_PIN_5
#define Reset_In_EXTI5_GPIO_Port GPIOD
#define Reset_In_EXTI5_EXTI_IRQn EXTI9_5_IRQn
#define switch_l1_Pin GPIO_PIN_6
#define switch_l1_GPIO_Port GPIOD
#define switch_r1_Pin GPIO_PIN_7
#define switch_r1_GPIO_Port GPIOD
#define switch_l2_Pin GPIO_PIN_4
#define switch_l2_GPIO_Port GPIOB
#define switch_r2_Pin GPIO_PIN_6
#define switch_r2_GPIO_Port GPIOB
#define SW_43_Pin GPIO_PIN_7
#define SW_43_GPIO_Port GPIOB
#define LED_43_Pin GPIO_PIN_8
#define LED_43_GPIO_Port GPIOB

/* tightly coupled memory: -Dsection(x)=unused turns these into plain variables */
#define INDTCM __attribute__((section(".dtcm")))
#define INDTCMZ __attribute__((section(".dtcmz")))

#endif /* __MAIN_H */
