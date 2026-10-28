/*
 * stm32f7xx_hal.h
 *
 * Stand-in for the STM32F7 HAL and CMSIS headers in the LICHOMAT emulator
 * (stage 1b spec 3.4, 8.3). Written for the emulator from the behaviour the
 * firmware relies on; it contains no ST code.
 *
 * - The register blocks the firmware uses are plain memory in the virtual
 *   device (emu/core/emu_regs.c): DWT, CoreDebug, FPU, EXTI, GPIOB-E, USART3,
 *   TIM2 and TIM14. Nothing else is declared, so firmware code that touches
 *   another register fails to compile here.
 * - The HAL macros that must reach the scheduler call the virtual device.
 * - The CMSIS intrinsics are C functions with the Cortex-M7's results.
 * - PRIMASK is the scheduler's interrupt mask.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_STM32F7XX_HAL_H_
#define EMU_STM32F7XX_HAL_H_

#include <stddef.h>
#include <stdint.h>

/* --- CMSIS compiler macros ------------------------------------------------- */
#define __IO volatile
#define __I volatile const
#define __O volatile
#define __IM volatile const
#define __OM volatile
#define __IOM volatile
#define __ASM __asm__
#define __INLINE inline
#define __STATIC_INLINE static inline
#define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
/* Apple clang predefines __weak (Objective-C garbage collection) */
#undef __weak
#define __weak __attribute__((weak))
#define UNUSED(X) (void)(X)
#define READ_REG(REG) ((REG))

typedef enum { RESET = 0U, SET = !RESET } FlagStatus, ITStatus;
typedef enum { DISABLE = 0U, ENABLE = !DISABLE } FunctionalState;
typedef enum { HAL_OK = 0x00U, HAL_ERROR = 0x01U, HAL_BUSY = 0x02U, HAL_TIMEOUT = 0x03U } HAL_StatusTypeDef;

/* --- core registers ---------------------------------------------------------- */
typedef struct { volatile uint32_t CTRL, CYCCNT, LAR; } DWT_Type;
typedef struct { volatile uint32_t DEMCR; } CoreDebug_Type;
typedef struct { volatile uint32_t FPDSCR; } FPU_Type;
extern DWT_Type emu_dwt;
extern CoreDebug_Type emu_coreDebug;
extern FPU_Type emu_fpu;
#define DWT (&emu_dwt)
#define CoreDebug (&emu_coreDebug)
#define FPU (&emu_fpu)
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24)
#define DWT_CTRL_CYCCNTENA_Msk (1UL)
#define FPU_FPDSCR_FZ_Msk (1UL << 24)

/* --- CMSIS intrinsics with the Cortex-M7's results (M1) ----------------------- */
static inline int32_t emu_saturate(int64_t value, uint32_t bits)
{
	const int64_t hi = ((int64_t)1 << (bits - 1)) - 1, lo = -((int64_t)1 << (bits - 1));
	return (int32_t)(value > hi ? hi : (value < lo ? lo : value));
}
static inline int32_t __SSAT(int32_t value, uint32_t bits) { return emu_saturate(value, bits); }
static inline int32_t __QADD(int32_t a, int32_t b) { return emu_saturate((int64_t)a + b, 32); }
static inline int32_t __QSUB(int32_t a, int32_t b) { return emu_saturate((int64_t)a - b, 32); }
static inline uint32_t __QADD16(uint32_t a, uint32_t b)
{
	const int32_t lo = emu_saturate((int64_t)(int16_t)(a & 0xFFFFu) + (int16_t)(b & 0xFFFFu), 16);
	const int32_t hi = emu_saturate((int64_t)(int16_t)(a >> 16) + (int16_t)(b >> 16), 16);
	return ((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo;
}
static inline uint32_t __QSUB16(uint32_t a, uint32_t b)
{
	const int32_t lo = emu_saturate((int64_t)(int16_t)(a & 0xFFFFu) - (int16_t)(b & 0xFFFFu), 16);
	const int32_t hi = emu_saturate((int64_t)(int16_t)(a >> 16) - (int16_t)(b >> 16), 16);
	return ((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo;
}
static inline uint64_t __SMLALD(uint32_t x, uint32_t y, uint64_t acc)
{
	return acc + (uint64_t)((int64_t)(int16_t)(x & 0xFFFFu) * (int16_t)(y & 0xFFFFu)
		+ (int64_t)(int16_t)(x >> 16) * (int16_t)(y >> 16));
}
/* CLZ of 0 is 32 on the M7; __builtin_clz(0) is undefined */
static inline uint8_t __CLZ(uint32_t value) { return value ? (uint8_t)__builtin_clz(value) : 32u; }
#define __PKHBT(ARG1, ARG2, ARG3) \
	((((uint32_t)(ARG1)) & 0x0000FFFFUL) | ((((uint32_t)(ARG2)) << (ARG3)) & 0xFFFF0000UL))
/* only dead code (calcNoise() in Oscillator.c) reads the APSR */
typedef union { struct { uint32_t _reserved0:27, Q:1, V:1, C:1, Z:1, N:1; } b; uint32_t w; } APSR_Type;
static inline uint32_t __get_APSR(void) { return 0u; }

/* --- PRIMASK: the scheduler's interrupt mask (emu/core/emu_sched.c) ------------ */
uint32_t __get_PRIMASK(void);
void __set_PRIMASK(uint32_t priMask);
void __disable_irq(void);
void __enable_irq(void);

/* --- NVIC ---------------------------------------------------------------------- */
typedef enum { EXTI4_IRQn = 10, EXTI9_5_IRQn = 23 } IRQn_Type;
void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority);
void HAL_NVIC_EnableIRQ(IRQn_Type IRQn);

/* --- GPIO and EXTI ------------------------------------------------------------- */
typedef struct { volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2]; } GPIO_TypeDef;
extern GPIO_TypeDef emu_gpioB, emu_gpioC, emu_gpioD, emu_gpioE;
#define GPIOB (&emu_gpioB)
#define GPIOC (&emu_gpioC)
#define GPIOD (&emu_gpioD)
#define GPIOE (&emu_gpioE)
#define GPIO_PIN_0 ((uint16_t)0x0001)
#define GPIO_PIN_1 ((uint16_t)0x0002)
#define GPIO_PIN_2 ((uint16_t)0x0004)
#define GPIO_PIN_3 ((uint16_t)0x0008)
#define GPIO_PIN_4 ((uint16_t)0x0010)
#define GPIO_PIN_5 ((uint16_t)0x0020)
#define GPIO_PIN_6 ((uint16_t)0x0040)
#define GPIO_PIN_7 ((uint16_t)0x0080)
#define GPIO_PIN_8 ((uint16_t)0x0100)
#define GPIO_PIN_9 ((uint16_t)0x0200)
#define GPIO_PIN_10 ((uint16_t)0x0400)
#define GPIO_PIN_11 ((uint16_t)0x0800)
#define GPIO_PIN_12 ((uint16_t)0x1000)
#define GPIO_PIN_13 ((uint16_t)0x2000)
#define GPIO_PIN_14 ((uint16_t)0x4000)
#define GPIO_PIN_15 ((uint16_t)0x8000)
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { volatile uint32_t IMR, EMR, RTSR, FTSR, SWIER, PR; } EXTI_TypeDef;
extern EXTI_TypeDef emu_exti;
#define EXTI (&emu_exti)
#define __HAL_GPIO_EXTI_GET_IT(pin) (EXTI->PR & (pin))
/* write 1 to clear on the device */
#define __HAL_GPIO_EXTI_CLEAR_IT(pin) (EXTI->PR &= ~(uint32_t)(pin))

/* --- timers -------------------------------------------------------------------- */
typedef struct { volatile uint32_t CR1, DIER, SR, CNT, PSC, ARR; } TIM_TypeDef;
extern TIM_TypeDef emu_tim2, emu_tim14;
#define TIM2 (&emu_tim2)
#define TIM14 (&emu_tim14)
#define TIM_CR1_CEN (1UL << 0)
#define TIM_IT_UPDATE (1UL << 0)
typedef struct { TIM_TypeDef *Instance; } TIM_HandleTypeDef;
void emu_timEnableIt(TIM_TypeDef *tim, uint32_t it);
void emu_timDisableIt(TIM_TypeDef *tim, uint32_t it);
void emu_timSetCounter(TIM_TypeDef *tim, uint32_t count);
#define __HAL_TIM_ENABLE_IT(h, it) emu_timEnableIt((h)->Instance, (it))
#define __HAL_TIM_DISABLE_IT(h, it) emu_timDisableIt((h)->Instance, (it))
#define __HAL_TIM_SET_COUNTER(h, c) emu_timSetCounter((h)->Instance, (c))
#define __HAL_TIM_SetCounter __HAL_TIM_SET_COUNTER
#define __HAL_TIM_CLEAR_IT(h, it) ((h)->Instance->SR &= ~(uint32_t)(it))
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *htim);

/* --- USART3 -------------------------------------------------------------------- */
typedef struct { volatile uint32_t CR1, CR2, CR3, BRR, ISR, ICR, RDR, TDR; } USART_TypeDef;
extern USART_TypeDef emu_usart3;
#define USART3 (&emu_usart3)
#define USART_ISR_ORE (1UL << 3)
#define USART_ISR_RXNE (1UL << 5)
#define USART_ISR_TXE (1UL << 7)
#define USART_ICR_ORECF (1UL << 3)
#define USART_CR1_RXNEIE (1UL << 5)
#define USART_CR1_TXEIE (1UL << 7)
/* the HAL's codes: register index << 8 | bit position */
#define UART_IT_RXNE 0x0525U
#define UART_IT_TXE 0x0727U
typedef struct { USART_TypeDef *Instance; } UART_HandleTypeDef;
void emu_uartEnableIt(USART_TypeDef *uart, uint32_t it);
void emu_uartDisableIt(USART_TypeDef *uart, uint32_t it);
#define __HAL_UART_ENABLE_IT(h, it) emu_uartEnableIt((h)->Instance, (it))
#define __HAL_UART_DISABLE_IT(h, it) emu_uartDisableIt((h)->Instance, (it))
#define __HAL_UART_CLEAR_OREFLAG(h) ((h)->Instance->ISR &= ~(uint32_t)USART_ISR_ORE)

/* --- handles that only need to exist (spec 3.4) --------------------------------- */
typedef struct { void *Instance; } SPI_HandleTypeDef;
typedef struct { void *Instance; } I2S_HandleTypeDef;
typedef struct { void *Instance; } ADC_HandleTypeDef;
typedef struct { void *Instance; } RNG_HandleTypeDef;
#define I2S_AUDIOFREQ_44K 44100U

/* --- the HAL functions of the virtual device (spec 3.4) ---------------------------- */
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t Delay);
uint32_t HAL_RCC_GetHCLKFreq(void);
uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef *hrng);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc, uint32_t *pData, uint32_t Length);

#endif /* EMU_STM32F7XX_HAL_H_ */
