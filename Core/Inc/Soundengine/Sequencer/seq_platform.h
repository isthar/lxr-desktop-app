/*
 * seq_platform.h
 *
 * Firmware platform layer of the LICHOMAT sequencer (spec 4.1, 4.4):
 * - the time base: DWT->CYCCNT at SEQC_CPU_HZ, extended to 64 bits by seqp_now();
 * - the three sync input queues (seq_syncq.h): MIDI DIN, USB MIDI, clock jacks;
 * - the entry points for the interrupt handlers that fill them;
 * - the merged drain for seq_tick().
 *
 * Contexts
 * --------
 * - seqp_stamp(): any context. Each interrupt handler reads it first.
 * - seqp_dinByteIn(): only USART3_IRQHandler (Uart.c).
 * - seqp_usbMessageIn(): only USBD_CDC_DataOut() (usbd_cdc.c), in OTG_FS_IRQHandler.
 * - seqp_jackIn(): only EXTI4_IRQHandler and EXTI9_5_IRQHandler (TriggerOut.c).
 *   The two share the jack queue. Both run at preemption priority 3 (main.c), so
 *   neither interrupts the other, and the queue keeps one producer at a time.
 * - seqp_now(), seqp_syncPop(): only seq_tick(), the consumer.
 * - seqp_syncOverflows(): any context (a sum of counters, for the CPU page).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */

#ifndef SEQ_PLATFORM_H_
#define SEQ_PLATFORM_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32f7xx.h"
#include "Soundengine/Sequencer/seq_core.h"
#include "Soundengine/Sequencer/seq_syncq.h"

/** the sources of sync events, also the queue order on equal stamps */
typedef enum
{
	SEQP_SRC_DIN = 0,	/**< MIDI DIN in (USART3) */
	SEQP_SRC_USB,		/**< USB MIDI in */
	SEQP_SRC_JACK,		/**< clock in and reset in jacks */
	SEQP_NUM_SOURCES
} seqp_src_t;

//------------------------------------------------------------------------------
/** the raw cycle counter, for interrupt stamps; any context */
static inline uint32_t seqp_stamp(void)
{
	return DWT->CYCCNT;
}
//------------------------------------------------------------------------------
/** Enable the cycle counter and start the 64-bit time. Called once by seq_init()
 * before the first seqp_now(). It sets TRCENA, unlocks the DWT (DWT->LAR, which
 * the Cortex-M7 needs) and sets CYCCNTENA. It never writes DWT->CYCCNT: the
 * non-blocking LCD delays (lcd.c) measure with it, and DWT_Delay_Init() (main.c)
 * already zeroed it at boot. The queues are not touched: zeroed memory is an empty
 * queue, and the interrupt handlers may already have queued events. */
void seqp_init(void);
//------------------------------------------------------------------------------
/** The current time in cycles, DWT->CYCCNT extended to 64 bits. seq_tick() context
 * only, and at least once per 19.9 s (2^32 cycles). */
seqc_time_t seqp_now(void);
//------------------------------------------------------------------------------
/** USART3 RX: one received MIDI byte, see seqq_dinByte() */
void seqp_dinByteIn(uint8_t byte, uint32_t stamp);
//------------------------------------------------------------------------------
/** USB MIDI OUT endpoint: one received event packet, see seqq_usbMessage() */
void seqp_usbMessageIn(uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2, uint32_t stamp);
//------------------------------------------------------------------------------
/** EXTI4 or EXTI9_5: a clock in pulse (SEQC_EV_PULSE) or a reset in edge
 * (SEQC_EV_RESET_ON, SEQC_EV_RESET_OFF) */
void seqp_jackIn(seqc_sync_ev_t ev, uint32_t stamp);
//------------------------------------------------------------------------------
/** The queued entry with the earliest stamp of all sources, removed from its queue;
 * see seqq_popEarliest(). *stamp is the entry's stamp as 64-bit time, never later
 * than now. false if no entry is queued. seq_tick() context only. */
bool seqp_syncPop(seqc_time_t now, seqp_src_t *source, seqq_entry_t *entry, seqc_time_t *stamp);
//------------------------------------------------------------------------------
/** sync events rejected because their queue was full, all sources together */
uint32_t seqp_syncOverflows(void);

#endif /* SEQ_PLATFORM_H_ */
