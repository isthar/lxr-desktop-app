/*
 * emu_codec.c
 *
 * The codec model of the LICHOMAT emulator (stage 1b spec 4.1): it replaces
 * cs4344_cs5343.c. codec_initCsCodec() starts the block requests; block
 * request k comes 64·k frames after the codec start and does what both I2S
 * branches of HAL_I2S_TxCpltCallback() do: count an underrun, pass the half
 * that starts playing to the audio sink, free the other half for the render,
 * and set TIM2's counter to 1450.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "emu_internal.h"

#define EMU_SAMPLE_VALID 0xFFu		/* SAMPLE_VALID of globals.h */
#define EMU_HALF_SAMPLES 128u		/* 64 stereo frames of int16 */

static uint64_t emu_codecBlock;		/* the number of the next block request */
static uint8_t emu_dmaPointer;

static void emu_blockFire(void)
{
	emu_src[EMU_SRC_BLOCK].pending++;
	emu_codecBlock++;
	emu_src[EMU_SRC_BLOCK].due = emu_count.codecStart + emu_framesToCycles(EMU_BLOCK_FRAMES * emu_codecBlock);
}

static void emu_blockBody(void)
{
	emu_count.blocks++;
	if (emu_fw.sampleValid && *emu_fw.sampleValid != EMU_SAMPLE_VALID)
		emu_count.underruns++;
	emu_dmaPointer++;
	const unsigned playing = emu_dmaPointer & 1u;
	if (emu_cfg.sinks.audio && emu_fw.dac1Buffer && emu_fw.dac2Buffer)
		emu_cfg.sinks.audio(emu_cfg.sinks.ctx, emu_now(),
			&emu_fw.dac1Buffer[playing * EMU_HALF_SAMPLES], &emu_fw.dac2Buffer[playing * EMU_HALF_SAMPLES]);
	if (emu_fw.sampleValid)
		*emu_fw.sampleValid = (uint8_t)(1u - playing);
	emu_tim2Write(1450u);
	emu_noteBlock();
}

/* AudioCodecManager.c's CodecInit() calls this from soundengine_init() */
void codec_initCsCodec(uint32_t Addr1, uint32_t Size1, uint32_t Addr2, uint32_t Size2)
{
	(void)Addr1;
	(void)Size1;
	(void)Addr2;
	(void)Size2;
	if (emu_count.codecStart != EMU_NEVER)
		return;
	emu_count.codecStart = emu_now();
	emu_codecBlock = 1u;
	emu_src[EMU_SRC_BLOCK].due = emu_count.codecStart + emu_framesToCycles(EMU_BLOCK_FRAMES);
}

void emu_codecReset(const emu_config_t *cfg)
{
	(void)cfg;
	emu_codecBlock = 0;
	emu_dmaPointer = 0;
	emu_src[EMU_SRC_BLOCK] = (emu_source_t){ EMU_NEVER, emu_blockFire, emu_blockBody, 0 };
}
