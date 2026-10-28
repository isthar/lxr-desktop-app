/*
 * emu_usart.c
 *
 * USART3, the MIDI DIN port of the LICHOMAT emulator (stage 1b spec 4.2, 4.3,
 * 4.4). USART3_IRQHandler() (Uart.c) runs unchanged. Its RDR read and TDR write
 * are plain member accesses that no stub sees, so the model does the device's
 * flag handling itself:
 * - RX: a byte that arrives sets RDR and ISR.RXNE, or ISR.ORE if RXNE is still
 *   set (the byte is lost). The body runs if CR1.RXNEIE is set.
 * - TX: when CR1.TXEIE goes from 0 to 1, a TX slot comes at the later of that
 *   time and the time the wire is free. A slot writes 0xFFFF into TDR, sets
 *   ISR.TXE and runs the body. A byte the handler wrote goes to the DIN sink at
 *   the slot's time; the next slot comes 69,120 cycles later while TXEIE stays
 *   set.
 * - After every body: RXNE and TXE are cleared, and TDR holds 0xFFFF again.
 * Input bytes are paced like a wire: 320 us apart, never overlapping.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdlib.h>
#include <string.h>

#include "stm32f7xx_hal.h"
#include "emu_internal.h"

#define EMU_TDR_EMPTY 0xFFFFu

typedef struct { uint64_t t; uint8_t byte; } emu_dinByte_t;

static struct
{
	emu_dinByte_t *rx;		/* the input bytes, in time order */
	size_t rxHead, rxCount, rxCap;
	uint64_t rxFree;		/* the input wire is free from here */
	uint64_t txSlot;		/* the next TX slot (EMU_NEVER: none) */
	uint64_t txFree;		/* the output wire is free from here */
} emu_u3;

static void emu_usartSchedule(void)
{
	const uint64_t rx = emu_u3.rxCount ? emu_u3.rx[emu_u3.rxHead].t : EMU_NEVER;
	emu_src[EMU_SRC_USART3].due = rx < emu_u3.txSlot ? rx : emu_u3.txSlot;
}

/* the IRQ line: (RXNE and RXNEIE) or (TXE and TXEIE) */
static void emu_usartUpdateLine(void)
{
	const uint32_t isr = USART3->ISR, cr1 = USART3->CR1;
	if (((isr & USART_ISR_RXNE) && (cr1 & USART_CR1_RXNEIE)) || ((isr & USART_ISR_TXE) && (cr1 & USART_CR1_TXEIE)))
		emu_src[EMU_SRC_USART3].pending = 1;
}

static void emu_usartFire(void)
{
	const uint64_t now = emu_now();
	while (emu_u3.rxCount && emu_u3.rx[emu_u3.rxHead].t <= now)
	{
		const uint8_t byte = emu_u3.rx[emu_u3.rxHead].byte;
		emu_u3.rxHead = (emu_u3.rxHead + 1u) % emu_u3.rxCap;
		emu_u3.rxCount--;
		if (USART3->ISR & USART_ISR_RXNE)
		{
			USART3->ISR |= USART_ISR_ORE;	/* overrun: the byte is lost */
			emu_count.dinLost++;
		}
		else
		{
			USART3->RDR = byte;
			USART3->ISR |= USART_ISR_RXNE;
			emu_count.dinIn++;
		}
	}
	if (emu_u3.txSlot <= now)
	{
		emu_u3.txSlot = EMU_NEVER;
		if (USART3->CR1 & USART_CR1_TXEIE)
		{
			USART3->TDR = EMU_TDR_EMPTY;
			USART3->ISR |= USART_ISR_TXE;
		}
	}
	emu_usartUpdateLine();
	emu_usartSchedule();
}

static void emu_usartBody(void)
{
	if (emu_fw.usart3)
		emu_fw.usart3();
	if (USART3->TDR != EMU_TDR_EMPTY)
	{
		const uint64_t now = emu_now();
		emu_count.dinOut++;
		if (emu_cfg.sinks.din)
			emu_cfg.sinks.din(emu_cfg.sinks.ctx, now, (uint8_t)(USART3->TDR & 0xFFu));
		emu_u3.txFree = now + EMU_DIN_BYTE_CYCLES;
		if (USART3->CR1 & USART_CR1_TXEIE)
			emu_u3.txSlot = emu_u3.txFree;
	}
	USART3->TDR = EMU_TDR_EMPTY;
	USART3->ISR &= ~(USART_ISR_RXNE | USART_ISR_TXE);
	emu_usartSchedule();
}

void emu_uartEnableIt(USART_TypeDef *uart, uint32_t it)
{
	const uint32_t bit = 1u << (it & 0x1Fu);
	const int was = (uart->CR1 & bit) != 0;
	uart->CR1 |= bit;
	if (uart != USART3 || was)
		return;
	if (bit == USART_CR1_TXEIE)
	{
		const uint64_t now = emu_now();
		emu_u3.txSlot = emu_u3.txFree > now ? emu_u3.txFree : now;
		emu_usartSchedule();
	}
	else if (bit == USART_CR1_RXNEIE)
	{
		/* a byte that waits in RDR runs the body at once */
		emu_usartUpdateLine();
		if (!emu_inBody())
			emu_dispatch();
	}
}

void emu_uartDisableIt(USART_TypeDef *uart, uint32_t it)
{
	uart->CR1 &= ~(1u << (it & 0x1Fu));
}

void emu_inDin(uint64_t t, const uint8_t *bytes, unsigned count)
{
	uint64_t at = emu_inputTime(t);
	for (unsigned i = 0; i < count; i++)
	{
		if (at < emu_u3.rxFree)
			at = emu_u3.rxFree;
		if (emu_u3.rxCount == emu_u3.rxCap)
		{
			const size_t cap = emu_u3.rxCap ? emu_u3.rxCap * 2u : 1024u;
			emu_dinByte_t *grown = malloc(cap * sizeof *grown);
			if (!grown)
			{
				emu_fault("out of memory for DIN input");
				return;
			}
			for (size_t k = 0; k < emu_u3.rxCount; k++)
				grown[k] = emu_u3.rx[(emu_u3.rxHead + k) % emu_u3.rxCap];
			free(emu_u3.rx);
			emu_u3.rx = grown;
			emu_u3.rxHead = 0;
			emu_u3.rxCap = cap;
		}
		emu_u3.rx[(emu_u3.rxHead + emu_u3.rxCount) % emu_u3.rxCap] = (emu_dinByte_t){ at, bytes[i] };
		emu_u3.rxCount++;
		emu_u3.rxFree = at + EMU_DIN_BYTE_CYCLES;
		at = emu_u3.rxFree;
	}
	emu_usartSchedule();
}

void emu_usartReset(const emu_config_t *cfg)
{
	(void)cfg;
	free(emu_u3.rx);
	memset(&emu_u3, 0, sizeof emu_u3);
	emu_u3.txSlot = EMU_NEVER;
	USART3->TDR = EMU_TDR_EMPTY;
	emu_src[EMU_SRC_USART3] = (emu_source_t){ EMU_NEVER, emu_usartFire, emu_usartBody, 0 };
}
