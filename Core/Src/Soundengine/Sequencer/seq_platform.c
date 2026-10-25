/*
 * seq_platform.c
 *
 * Firmware platform layer of the LICHOMAT sequencer, see seq_platform.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */

#include "Soundengine/Sequencer/seq_platform.h"

/* zeroed at startup, which makes them empty queues and an idle SPP decoder */
static seqq_t seqp_queues[SEQP_NUM_SOURCES];
static seqq_din_t seqp_dinDecoder;
static seqq_t *const seqp_queueList[SEQP_NUM_SOURCES] = {
	&seqp_queues[SEQP_SRC_DIN], &seqp_queues[SEQP_SRC_USB], &seqp_queues[SEQP_SRC_JACK] };

/* the 64-bit time; seq_tick() context only */
static uint64_t seqp_time;
static uint32_t seqp_lastCycles;

//------------------------------------------------------------------------------
void seqp_init(void)
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->LAR = 0xC5ACCE55u;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	seqp_lastCycles = DWT->CYCCNT;
	seqp_time = seqp_lastCycles;
}
//------------------------------------------------------------------------------
seqc_time_t seqp_now(void)
{
	return seqq_extend(&seqp_time, &seqp_lastCycles, DWT->CYCCNT);
}
//------------------------------------------------------------------------------
void seqp_dinByteIn(uint8_t byte, uint32_t stamp)
{
	seqq_dinByte(&seqp_dinDecoder, &seqp_queues[SEQP_SRC_DIN], byte, stamp);
}
//------------------------------------------------------------------------------
void seqp_usbMessageIn(uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2, uint32_t stamp)
{
	seqq_usbMessage(&seqp_queues[SEQP_SRC_USB], cin, status, data1, data2, stamp);
}
//------------------------------------------------------------------------------
void seqp_jackIn(seqc_sync_ev_t ev, uint32_t stamp)
{
	seqq_push(&seqp_queues[SEQP_SRC_JACK], (uint8_t)ev, 0, stamp);
}
//------------------------------------------------------------------------------
bool seqp_syncPop(seqc_time_t now, seqp_src_t *source, seqq_entry_t *entry, seqc_time_t *stamp)
{
	unsigned src;

	if (!seqq_popEarliest(seqp_queueList, SEQP_NUM_SOURCES, now, &src, entry, stamp))
		return false;
	*source = (seqp_src_t)src;
	return true;
}
//------------------------------------------------------------------------------
uint32_t seqp_syncOverflows(void)
{
	return seqp_queues[SEQP_SRC_DIN].overflow + seqp_queues[SEQP_SRC_USB].overflow
			+ seqp_queues[SEQP_SRC_JACK].overflow;
}
