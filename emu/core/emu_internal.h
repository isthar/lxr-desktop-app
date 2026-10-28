/*
 * emu_internal.h
 *
 * The inside of the virtual device: the scheduler's sources and inputs, the
 * hooks between the models, and the firmware entry points. The models in
 * emu/core use it, and the host tests in emu/tests, which install fake
 * firmware entries. The tiers use emu_core.h only.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_INTERNAL_H_
#define EMU_INTERNAL_H_

#include <stdint.h>
#include "emu_core.h"

/* --- the interrupt sources, in the order of the table in spec 4.2 ----------- */

typedef enum
{
	EMU_SRC_SYSTICK,	/**< priority 0, every 216,000 cycles from power-on */
	EMU_SRC_TIM14,		/**< priority 0, every 54,004 cycles once started */
	EMU_SRC_USART3,		/**< priority 3, RXNE and TXE */
	EMU_SRC_OTG_OUT,	/**< priority 3, one incoming USB transfer */
	EMU_SRC_OTG_IN,		/**< priority 3, 1 ms after CDC_Transmit_FS() */
	EMU_SRC_EXTI4,		/**< priority 3, clock in */
	EMU_SRC_EXTI9_5,	/**< priority 3, reset in */
	EMU_SRC_TIM2,		/**< priority 4, TIM2 update */
	EMU_SRC_BLOCK,		/**< priority 5, the codec's block request */
	EMU_NUM_SRC
} emu_src_t;

#define EMU_PRIO_THREAD 256	/**< the running priority of thread context */

extern const uint8_t emu_srcPriority[EMU_NUM_SRC];

/** One interrupt source. At time due the scheduler calls fire(), which updates
 * the model, makes the body pending (pending) and sets the next due time
 * (EMU_NEVER: none). The scheduler runs pending bodies by priority, then in this
 * table's order, while PRIMASK is clear. */
typedef struct emu_source
{
	uint64_t due;
	void (*fire)(void);
	void (*body)(void);
	uint32_t pending;
} emu_source_t;

extern emu_source_t emu_src[EMU_NUM_SRC];

/** run the pending bodies that the running priority allows */
void emu_dispatch(void);

/* --- the firmware entry points --------------------------------------------------- */

/** What the interrupt bodies and the passes call in the firmware. emu_powerOn()
 * installs the real entries (emu_firmware.c); a host test of one model installs
 * fakes after emu_corePowerOn(). A NULL entry is skipped. */
typedef struct emu_firmware
{
	void (*boot)(void);			/**< appMain_boot() */
	void (*pass)(void);			/**< appMain_loopOnce() */
	void (*sysTick)(void);		/**< SysTick body: screensaver_timer, encoder change, encoder_callback() */
	void (*tim14)(void);		/**< TIM14 body after systick_ticks++: menu_tick() 4 times */
	void (*tim2)(void);			/**< TIM2 body: cpu meter, seq_tick(), the render */
	void (*usart3)(void);		/**< USART3_IRQHandler() */
	void (*exti4)(void);		/**< EXTI4_IRQHandler() */
	void (*exti9_5)(void);		/**< EXTI9_5_IRQHandler() */
	void (*usbTxComplete)(void);	/**< usb_midiTxComplete() */
	void (*usbMessageIn)(uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2, uint32_t stamp);	/**< seqp_usbMessageIn() */
	volatile uint8_t *sampleValid;	/**< &bCurrentSampleValid */
	int16_t *dac2Buffer;		/**< dma_buffer, out 2 */
	int16_t *dac1Buffer;		/**< dma_buffer2, out 1 */
	uint16_t *adcBuffer;		/**< adc_buf */
} emu_firmware_t;

extern emu_firmware_t emu_fw;

/* --- power-on --------------------------------------------------------------------- */

/** the configuration of the running device */
extern emu_config_t emu_cfg;
/** the counters of emu_getCounters() */
extern emu_counters_t emu_count;

/** power-on of the clock, the scheduler and the registers (emu_regs.c);
 * emu_powerOn() adds the models and the firmware */
void emu_corePowerOn(const emu_config_t *cfg);

void emu_regsReset(const emu_config_t *cfg);	/* emu_regs.c */
void emu_codecReset(const emu_config_t *cfg);	/* emu_codec.c */
void emu_lcdReset(const emu_config_t *cfg);		/* emu_lcd.c */
void emu_panelReset(const emu_config_t *cfg);	/* emu_panel.c */
void emu_usartReset(const emu_config_t *cfg);	/* emu_usart.c */
void emu_usbReset(const emu_config_t *cfg);		/* emu_usb.c */
void emu_jacksReset(const emu_config_t *cfg);	/* emu_jacks.c */
void emu_cardReset(const emu_config_t *cfg);	/* emu_card.c */
void emu_randomReset(const emu_config_t *cfg);	/* emu_random.c */

/* --- time --------------------------------------------------------------------------- */

/** Advance the clock in thread context (HAL_Delay(), a HAL_GetTick() spin),
 * running the bodies that fall due and calling the tier hook. Inside a body it
 * does nothing. */
void emu_threadAdvance(uint64_t cycles);

/** 1 while an interrupt body runs */
int emu_inBody(void);

/** 1 while a main-loop pass runs */
int emu_inPass(void);

/** the codec model reports a block request: the hook runs after it, and outside
 * a pass a pass follows it */
void emu_noteBlock(void);

/** call fn after every body, pass, boot and direct call (output sampling) */
void emu_addSampler(void (*fn)(void));

/** call fn at the start of every pass (the knob ramp) */
void emu_addPassStart(void (*fn)(void));

/* --- inputs --------------------------------------------------------------------------- */

typedef enum
{
	EMU_IN_BUTTON,	/**< a: button, b: down */
	EMU_IN_ENCODER,	/**< b: detents */
	EMU_IN_PUSH,	/**< b: 1 pressed, 0 released */
	EMU_IN_KNOB,	/**< a: knob 0-3, b: degrees */
	EMU_IN_FADER,	/**< a: fader 0-5, level */
	EMU_IN_JACKS,	/**< a: plugged jacks */
	EMU_IN_CARD,	/**< b: inserted */
	EMU_IN_CLOCK,	/**< a clock in pulse */
	EMU_IN_RESET,	/**< b: on */
	EMU_IN_NUM
} emu_inputKind_t;

typedef struct emu_input
{
	uint64_t t;
	uint64_t seq;
	uint8_t kind;
	uint8_t a;
	int32_t b;
	double level;
} emu_input_t;

/** queue an input for time t (emu_now() if earlier, counted as late) */
void emu_inputPush(uint64_t t, uint8_t kind, uint8_t a, int32_t b, double level);

/** the model that takes inputs of a kind at their time */
void emu_inputHandler(uint8_t kind, void (*fn)(const emu_input_t *in));

/** an input's time: t, or emu_now() if t has passed (counted as late) */
uint64_t emu_inputTime(uint64_t t);

/* --- messages ------------------------------------------------------------------------- */

/** a model check failed: emu_cfg.fault, or print the trail and abort() */
void emu_fault(const char *what);

/** a warning about the card: emu_cfg.warn, or stderr */
void emu_warn(const char *what);

/* --- the models' entry points for each other ------------------------------------------ */

/** TIM2: the counter that the codec model sets at a block request */
void emu_tim2Write(uint32_t count);

/** EXTI: an edge on line 4 or 5 sets its pending bit; the body runs once the
 * line is enabled */
void emu_extiRaise(unsigned line);

/** the panel: release the next queued Gray change of the encoder (SysTick) */
void emu_panelEncoderStep(void);

#endif /* EMU_INTERNAL_H_ */
