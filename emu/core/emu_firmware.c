/*
 * emu_firmware.c
 *
 * Power-on of the LICHOMAT emulator with the firmware (stage 1b spec 4.10):
 * the core, every model, and the firmware entry points that the interrupt
 * bodies and the passes call. The bodies follow the call lists of
 * stm32f7xx_it.c, which is ST-licensed and not compiled; M20 hashes those
 * lists and fails when one changes.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "main.h"
#include "appMain.h"
#include "encoder.h"
#include "lcd.h"
#include "menu/menu.h"
#include "menu/screensaver.h"
#include "IO/adcPots.h"
#include "Soundengine/AudioCodecManager.h"
#include "Soundengine/globals.h"
#include "Soundengine/Soundengine.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include "Soundengine/USB/usb_manager.h"
#include "emu_internal.h"

#ifndef EMU_OVERLAY_MAIN_H
#error "Core/Inc/main.h is not the emulator's overlay: emu/CMakeLists.txt passes -ivfsoverlay"
#endif

void USART3_IRQHandler(void);	/* Uart.c */
void EXTI4_IRQHandler(void);	/* TriggerOut.c */
void EXTI9_5_IRQHandler(void);	/* TriggerOut.c */

/* SysTick_Handler(): HAL_IncTick() is the clock itself here */
static void emu_fwSysTick(void)
{
	screensaver_timer++;
	emu_panelEncoderStep();
	encoder_callback();
}

/* TIM8_TRG_COM_TIM14_IRQHandler(), after systick_ticks++ */
static void emu_fwTim14(void)
{
	for (int i = 0; i < 4; i++)
		if (lcd_process_display_interrupt)
			menu_tick();
}

/* TIM2_IRQHandler() without its debug pin */
static void emu_fwTim2(void)
{
	soundengine_cpuBegin();
	seq_tick();
	const uint8_t blockRendered = soundengine_calcNextSampleBlock();
	soundengine_cpuEnd(blockRendered);
}

void emu_powerOn(const emu_config_t *cfg)
{
	static int poweredOn;
	if (poweredOn)
	{
		emu_fault("emu_powerOn: once per process; the firmware's state survives only a new process");
		return;
	}
	poweredOn = 1;
	emu_corePowerOn(cfg);
	emu_fw.boot = appMain_boot;
	emu_fw.pass = appMain_loopOnce;
	emu_fw.sysTick = emu_fwSysTick;
	emu_fw.tim14 = emu_fwTim14;
	emu_fw.tim2 = emu_fwTim2;
	emu_fw.usart3 = USART3_IRQHandler;
	emu_fw.exti4 = EXTI4_IRQHandler;
	emu_fw.exti9_5 = EXTI9_5_IRQHandler;
	emu_fw.usbTxComplete = usb_midiTxComplete;
	emu_fw.usbMessageIn = seqp_usbMessageIn;
	emu_fw.sampleValid = &bCurrentSampleValid;
	emu_fw.dac2Buffer = dma_buffer;
	emu_fw.dac1Buffer = dma_buffer2;
	emu_fw.adcBuffer = adc_buf;
	emu_codecReset(cfg);
	emu_lcdReset(cfg);
	emu_panelReset(cfg);
	emu_usartReset(cfg);
	emu_usbReset(cfg);
	emu_jacksReset(cfg);
	emu_cardReset(cfg);
	emu_randomReset(cfg);
}
