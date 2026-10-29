/*
 * test_din.c
 *
 * M10 (stage 1b spec 4.2, 4.3, 4.4, 12.1): MIDI DIN in and out through the
 * firmware's own USART3_IRQHandler(), FIFOs and DIN sync queue (Uart.c,
 * seq_platform.c), without a boot. A byte takes 69,120 cycles (320 us) on the
 * wire. The flag rules of the USART3 model: an input byte during a TX slot is
 * decoded once; an output byte that an RX body sends is not lost; the last byte
 * of a burst is sent once; the first byte after TXEIE goes from 0 to 1 starts at
 * the later of the enable and the wire-free time, also when a pass is due at
 * that time; a byte that arrives while RXNE is set sets ORE and is lost; before
 * RXNEIE (boot step 11) only the byte left in RDR reaches the sync queue.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "main.h"
#include "Soundengine/MIDI/Uart.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include "emu_test.h"

#define BYTE EMU_DIN_BYTE_CYCLES	/* 69,120 */

void USART3_IRQHandler(void);
void codec_initCsCodec(uint32_t Addr1, uint32_t Size1, uint32_t Addr2, uint32_t Size2);

static emu_testDin_t din;

/* the core, USART3 and the firmware's MIDI UART; RXNEIE as boot step 11 sets it */
static void setupNoRx(emu_config_t *cfg)
{
	emu_configDefaults(cfg);
	cfg->sinks.din = emu_testDinSink;
	cfg->sinks.ctx = &din;
	emu_corePowerOn(cfg);
	emu_usartReset(cfg);
	emu_fw.usart3 = USART3_IRQHandler;
	seqp_init();
	initMidiUart();
	memset(&din, 0, sizeof din);
}

static void setup(void)
{
	emu_config_t cfg;
	setupNoRx(&cfg);
	__HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
}

/* the DIN sync entries so far, in stamp order; their source must be DIN */
static seqq_entry_t entry[16];
static uint64_t stamp[16];

static unsigned popAll(void)
{
	unsigned n = 0;
	seqp_src_t src;
	seqq_entry_t e;
	seqc_time_t t;
	while (seqp_syncPop(seqp_now(), &src, &e, &t))
	{
		if (src != SEQP_SRC_DIN)
			return 99u;
		if (n < 16u)
		{
			entry[n] = e;
			stamp[n] = t;
		}
		n++;
	}
	return n;
}

/* the handler, then one output byte if the body was an RX body */
static void rxThenSend(void)
{
	const int rx = (USART3->ISR & USART_ISR_RXNE) != 0;
	USART3_IRQHandler();
	if (rx)
		uart_sendMidiByte(0xFE);
}

TEST(M10_wire_in)
{
	setup();
	emu_inDin(1000, (const uint8_t[]){ 0xF8, 0xFA, 0xF8 }, 3);
	HAL_Delay(2);
	ASSERT_EQ(popAll(), 3);
	const uint8_t types[3] = { SEQC_EV_CLOCK, SEQC_EV_START, SEQC_EV_CLOCK };
	for (unsigned k = 0; k < 3; k++)
	{
		ASSERT_EQ(entry[k].type, types[k]);
		ASSERT_EQ(entry[k].stamp, 1000u + k * BYTE);	/* 1000, 70,120, 139,240 */
		ASSERT_EQ(stamp[k], 1000u + k * BYTE);
	}
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.dinIn, 3);
	ASSERT_EQ(c.dinLost, 0);
}

TEST(M10_wire_out)
{
	setup();
	emu_threadAdvance(5000);
	ASSERT_EQ(emu_now(), 5000);
	uint8_t sent[22];
	for (unsigned i = 0; i < 7; i++)
	{
		sent[3 * i] = 0x90;
		sent[3 * i + 1] = 0x3C;
		sent[3 * i + 2] = 0x64;
	}
	sent[21] = 0xF8;
	for (unsigned i = 0; i < 22; i++)
		uart_sendMidiByte(sent[i]);
	HAL_Delay(10);
	ASSERT_EQ(din.count, 22);		/* the last byte is sent once */
	for (unsigned k = 0; k < 22; k++)
	{
		ASSERT_EQ(din.byte[k], sent[k]);
		ASSERT_EQ(din.t[k], 5000u + k * BYTE);
	}
	ASSERT_EQ(din.t[21], 1456520u);	/* the F8 6.72 ms late, behind 7 Note Ons (spec 4.4) */
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.dinOut, 22);
	ASSERT_FALSE(USART3->CR1 & USART_CR1_TXEIE);	/* the empty slot after the F8 cleared it */
}

TEST(M10_tx_enable_late)
{
	setup();
	emu_threadAdvance(1000);
	uart_sendMidiByte(0x80);
	emu_threadAdvance(1000);
	__HAL_UART_DISABLE_IT(&huart3, UART_IT_TXE);	/* at 2000 */
	emu_threadAdvance(1000);
	uart_sendMidiByte(0x81);						/* at 3000, with the wire busy */
	HAL_Delay(2);
	ASSERT_EQ(din.count, 2);
	ASSERT_EQ(din.byte[0], 0x80);
	ASSERT_EQ(din.t[0], 1000);
	ASSERT_EQ(din.byte[1], 0x81);
	ASSERT_EQ(din.t[1], 1000u + BYTE);	/* the wire-free time, not the enable */
}

TEST(M10_rx_during_tx)
{
	/* 4 bytes: slots at 1000, 70,120, 139,240 and 208,360; the empty slot at
	 * 277,480 clears TXEIE. The input at 169,240 (not earlier: the input wire is
	 * busy until 139,240) comes between two slots while the 4th byte waits, so a
	 * stale TXE would let its RX body send that byte early. */
	setup();
	emu_inDin(1000u + BYTE, (const uint8_t[]){ 0xF8 }, 1);		/* exactly a TX slot */
	emu_inDin(169240u, (const uint8_t[]){ 0xFA }, 1);			/* between two slots */
	emu_threadAdvance(1000);
	const uint8_t sent[4] = { 0x90, 0x3C, 0x64, 0xC1 };
	for (unsigned k = 0; k < 4; k++)
		uart_sendMidiByte(sent[k]);
	HAL_Delay(2);
	ASSERT_EQ(din.count, 4);		/* no other sink byte */
	for (unsigned k = 0; k < 4; k++)
	{
		ASSERT_EQ(din.byte[k], sent[k]);
		ASSERT_EQ(din.t[k], 1000u + k * BYTE);
	}
	ASSERT_EQ(popAll(), 2);		/* each input byte decoded once */
	ASSERT_EQ(entry[0].type, SEQC_EV_CLOCK);
	ASSERT_EQ(stamp[0], 70120u);
	ASSERT_EQ(entry[1].type, SEQC_EV_START);
	ASSERT_EQ(stamp[1], 169240u);
}

TEST(M10_tx_from_rx_body)
{
	setup();
	emu_fw.usart3 = rxThenSend;
	emu_inDin(1000, (const uint8_t[]){ 0xF8 }, 1);
	HAL_Delay(2);
	ASSERT_EQ(din.count, 1);		/* the output byte of the RX body is not lost */
	ASSERT_EQ(din.byte[0], 0xFE);
	ASSERT_EQ(din.t[0], 1000);
	ASSERT_EQ(popAll(), 1);
	ASSERT_EQ(stamp[0], 1000);
}

/* the passes of M10_tx_slot_before_pass: their times and the bytes sent by then */
static uint64_t passTime[32];
static unsigned passSent[32], passCount;

static void recordPass(void)
{
	if (passCount < 32u)
	{
		passTime[passCount] = emu_now();
		passSent[passCount] = din.count;
	}
	passCount++;
}

TEST(M10_tx_slot_before_pass)
{
	/* The RX body at a block request's time enables TXEIE with the wire free, so
	 * the TX slot falls due at that same time, after the fires of its step. The
	 * pass that the block request asks for runs after it (spec 4.2, "A pass runs
	 * after every body due at its time"). */
	emu_config_t cfg;
	setupNoRx(&cfg);
	emu_codecReset(&cfg);
	__HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
	emu_fw.usart3 = rxThenSend;
	emu_fw.pass = recordPass;
	codec_initCsCodec(0, 128, 0, 128);			/* at 0 */
	const uint64_t request = emu_framesToCycles(64);	/* 313,180: off the 54,000 grid */
	ASSERT_EQ(request % 54000u != 0, 1);
	emu_inDin(request, (const uint8_t[]){ 0xF8 }, 1);
	emu_runUntil(request + 1u);
	ASSERT_EQ(din.count, 1);
	ASSERT_EQ(din.byte[0], 0xFE);
	ASSERT_EQ(din.t[0], request);
	unsigned at = 0;
	while (at < passCount && passTime[at] != request)
		at++;
	ASSERT_TRUE(at > 0u && at < passCount);	/* the block request's pass */
	ASSERT_EQ(passSent[at], 1);		/* after the TX slot */
	ASSERT_EQ(passSent[at - 1u], 0);
}

TEST(M10_overrun)
{
	emu_config_t cfg;
	setupNoRx(&cfg);					/* RXNEIE clear: before boot step 11 */
	emu_inDin(1000, (const uint8_t[]){ 0xFA, 0xF8, 0xFC }, 3);
	HAL_Delay(2);
	ASSERT_TRUE(USART3->ISR & USART_ISR_ORE);
	ASSERT_TRUE(USART3->ISR & USART_ISR_RXNE);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.dinIn, 1);
	ASSERT_EQ(c.dinLost, 2);
	ASSERT_EQ(popAll(), 0);
	/* boot step 11: the handler runs at once for the byte left in RDR */
	__HAL_UART_CLEAR_OREFLAG(&huart3);
	__HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
	ASSERT_FALSE(USART3->ISR & USART_ISR_RXNE);
	ASSERT_EQ(popAll(), 1);
	ASSERT_EQ(entry[0].type, SEQC_EV_START);
	ASSERT_EQ(stamp[0], 2u * EMU_CYCLES_PER_MS);	/* stamped when the handler runs */
	ASSERT_EQ(din.count, 0);
}

TEST(M10_spp_and_pc)
{
	setup();
	emu_inDin(1000, (const uint8_t[]){ 0xF2, 0x10, 0x00, 0xC0, 0x05 }, 5);
	HAL_Delay(3);
	ASSERT_EQ(popAll(), 2);
	ASSERT_EQ(entry[0].type, SEQQ_SONG_POSITION);
	ASSERT_EQ(entry[0].value, 16);
	ASSERT_EQ(stamp[0], 1000u + 2u * BYTE);		/* the SPP's last byte */
	ASSERT_EQ(entry[1].type, SEQQ_PROGRAM_CHANGE);
	ASSERT_EQ(entry[1].value, 5);				/* channel 0, program 5 */
	ASSERT_EQ(stamp[1], 1000u + 4u * BYTE);		/* the data byte */
}

TEST_MAIN()
