/*
 * emu_core.h
 *
 * The virtual device of the LICHOMAT emulator (stage 1b spec 3.3). The whole
 * firmware runs on it: boot, main-loop passes and interrupt bodies, on one host
 * thread, against a model of the registers and the HAL, with one virtual clock
 * of 216 MHz cycles. Both tiers (lichomat-render, LICHOMAT Emu) use only this
 * header.
 *
 * Every function runs on the firmware thread, except emu_progress() and
 * emu_watchdogPoll(), which another thread may call.
 *
 * Time
 * ----
 * Times are virtual cycles since power-on (EMU_CPU_HZ per second). Time passes
 * only in the scheduler: between passes, in HAL_Delay() and in a HAL_GetTick()
 * spin of the firmware (spec 4.2). Inside an interrupt body it stands still.
 *
 * Inputs
 * ------
 * An input carries the time at which it reaches the device. An input for a time
 * before emu_now() enters at emu_now() and is counted in emu_counters_t.late.
 * DIN bytes are paced like a wire (320 us each), USB transfers at most 16
 * USB-MIDI packets per 1 ms; the panel releases its events at the firmware's
 * poll points (spec 4.6).
 *
 * The tier hook
 * -------------
 * emu_config_t.hook is called on the firmware thread while virtual time passes:
 * after every block request, and at least once per 1 ms of virtual time while
 * HAL_Delay() or a HAL_GetTick() spin advance the clock (spec 3.3). It may inject
 * inputs and read the sinks' output. It never calls the firmware, emu_runUntil()
 * or emu_directCall(). A hook that waits for the audio device or the wall clock
 * brackets the wait with emu_waitBegin() and emu_waitEnd(), so that the watchdog
 * does not count it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_CORE_H_
#define EMU_CORE_H_

#include <stdint.h>

#define EMU_CPU_HZ          216000000u   /**< virtual cycles per second */
#define EMU_CYCLES_PER_MS   216000u      /**< one SysTick period */
#define EMU_NEVER           UINT64_MAX   /**< a time that never comes */
#define EMU_BLOCK_FRAMES    64u          /**< frames per audio block */
#define EMU_PASS_GRID       54000u       /**< passes run at least on this grid (250 us) */
#define EMU_DIN_BYTE_CYCLES 69120u       /**< one byte on the MIDI wire (320 us) */
#define EMU_NUM_LEDS        41u          /**< LEDs 0-39 on the shift registers, LED 40 on PB8 */

/** frames at 44,140.625 Hz to cycles, exact: floor(frames * 552,960 / 113) */
uint64_t emu_framesToCycles(uint64_t frames);

/** the output jacks, as bits of emu_config_t.jacks and emu_inJacks() */
enum
{
	EMU_JACK_L1 = 1u << 0,
	EMU_JACK_R1 = 1u << 1,
	EMU_JACK_L2 = 1u << 2,
	EMU_JACK_R2 = 1u << 3,
	EMU_JACKS_ALL = 0x0Fu
};

/** Where the device's outputs go. A sink never blocks; NULL drops the output. */
typedef struct emu_sinks
{
	void *ctx;
	/** one block at the block request that starts playing it: 64 interleaved
	 * int16 stereo frames of out 1 (dma_buffer2: L1, R1) and of out 2 (dma_buffer:
	 * L2, R2) */
	void (*audio)(void *ctx, uint64_t t, const int16_t *out1, const int16_t *out2);
	/** a byte on the DIN out wire, at its start on the wire */
	void (*din)(void *ctx, uint64_t t, uint8_t byte);
	/** one USB transfer of USB-MIDI 1.0 packets on the IN endpoint, at its start */
	void (*usb)(void *ctx, uint64_t t, const uint8_t *data, uint16_t length);
	/** a change of the clock out jack (PC13) */
	void (*clockOut)(void *ctx, uint64_t t, int level);
} emu_sinks_t;

/** why the tier hook is called */
typedef enum
{
	EMU_HOOK_BLOCK,		/**< after a block request (its block is in the audio sink) */
	EMU_HOOK_TIME		/**< virtual time passed in HAL_Delay() or a HAL_GetTick() spin */
} emu_hookReason_t;

/** the tier hook, see the top of this file */
typedef void (*emu_hook_t)(void *ctx, uint64_t now, emu_hookReason_t why);

/** the power-on configuration */
typedef struct emu_config
{
	const char *cardDir;	/**< host folder that holds the virtual card; NULL: none */
	int cardIn;				/**< the card switch: 1 inserted */
	uint8_t jacks;			/**< plugged output jacks, EMU_JACK_* bits */
	double faders[6];		/**< fader levels 0-1, faders 1-6 */
	int knobs[4];			/**< knob angles at power-on in degrees; 0 (spec 4.10) unless a test sets them */
	uint64_t seed;			/**< seed of both random streams (spec 4.9) */
	emu_sinks_t sinks;
	emu_hook_t hook;
	void *hookCtx;
	/** a model check failed (spec 3.4, 11); NULL: print and abort() */
	void (*fault)(void *ctx, const char *what);
	/** a warning about the card (spec 4.8); NULL: print to stderr */
	void (*warn)(void *ctx, const char *what);
	void *msgCtx;			/**< ctx of fault and warn */
} emu_config_t;

/** card in, all jacks plugged, faders full, seed 1, no sinks, no hook */
void emu_configDefaults(emu_config_t *cfg);

/** Power-on: the registers in the state that the CubeMX init leaves them in
 * (spec 4.10). The configuration is copied. Once per process: the firmware's
 * static state survives only a new process. */
void emu_powerOn(const emu_config_t *cfg);

/** Run the clock to virtual time t. The first call boots (appMain_boot()). The
 * call returns at the first pass boundary at or after t; a pass boundary is the
 * end of the boot or of a pass. */
void emu_runUntil(uint64_t t);

/** the current virtual time in cycles */
uint64_t emu_now(void);

/** 1 once appMain_boot() has returned */
int emu_booted(void);

/** Call the firmware directly, between two emu_runUntil() calls, at a pass
 * boundary (spec 5.3). The call is a segment of its own for the HAL_GetTick()
 * rule; time may pass in it. */
void emu_directCall(void (*fn)(void *arg), void *arg);

/* --- inputs (spec 4.3, 4.5, 4.6) -------------------------------------------- */

/** bytes into the DIN in jack, from time t, 320 us apart */
void emu_inDin(uint64_t t, const uint8_t *bytes, unsigned count);
/** One USB transfer of USB-MIDI 1.0 packets (4 bytes each), at most 16 packets.
 * Returns 0, or -1 if length is 0, above 64 or not a multiple of 4. */
int emu_inUsb(uint64_t t, const uint8_t *data, unsigned length);
/** button 0-40 (spec 4.6) goes down (1) or up (0) */
void emu_inButton(uint64_t t, unsigned button, int down);
/** turn the encoder by detents (+ forward); one Gray change per SysTick */
void emu_inEncoder(uint64_t t, int detents);
/** push the encoder for ms (at least 40) */
void emu_inPush(uint64_t t, unsigned ms);
/** turn knob 1-4 by degrees (+ clockwise); it ramps at most 1 degree per pass */
void emu_inKnob(uint64_t t, unsigned knob, int degrees);
/** set fader 1-6 to a level 0-1 */
void emu_inFader(uint64_t t, unsigned fader, double level);
/** the plugged output jacks, EMU_JACK_* bits */
void emu_inJacks(uint64_t t, uint8_t plugged);
/** the card switch */
void emu_inCard(uint64_t t, int inserted);
/** one pulse into the clock in jack (a falling edge of PD4) */
void emu_inClockPulse(uint64_t t);
/** the reset in jack: on (1) or off (0) */
void emu_inReset(uint64_t t, int on);

/* --- snapshots (spec 4.7) ----------------------------------------------------- */

typedef struct emu_lcdSnapshot
{
	uint8_t chars[2][16];	/**< character codes of the visible 16x2 cells */
	uint8_t cgram[64];		/**< the 8 user characters, 8 rows each */
	uint8_t displayOn;
	uint8_t cursorOn;
	uint8_t blinkOn;
	uint8_t cursorRow;		/**< 0 or 1 */
	uint8_t cursorCol;		/**< 0-15; 16 or more is off screen */
} emu_lcdSnapshot_t;

/** the LCD as the model holds it */
void emu_lcdSnapshot(emu_lcdSnapshot_t *s);

/** LED n is bit n: on at any time since the previous call (spec 4.7) */
uint64_t emu_ledSnapshot(void);

/* --- counters ------------------------------------------------------------------ */

typedef struct emu_counters
{
	uint64_t bootEnd;		/**< virtual time at which appMain_boot() returned */
	uint64_t codecStart;	/**< virtual time of the codec start (EMU_NEVER: none) */
	uint64_t blocks;		/**< block requests */
	uint64_t underruns;		/**< block requests that found no rendered block */
	uint64_t passes;		/**< main-loop passes */
	uint64_t dinIn;			/**< DIN bytes that reached RDR */
	uint64_t dinLost;		/**< DIN bytes lost to an overrun (ORE) */
	uint64_t dinOut;		/**< DIN bytes sent */
	uint64_t usbIn;			/**< USB transfers in */
	uint64_t usbInDropped;	/**< USB messages dropped by the full input ring */
	uint64_t usbOut;		/**< USB transfers out */
	uint64_t clockEdges;	/**< changes of the clock out jack */
	uint64_t late;			/**< inputs that came after their time */
	uint64_t tickCalls;		/**< HAL_GetTick() calls in thread context in the last segment */
} emu_counters_t;

void emu_getCounters(emu_counters_t *c);

/* --- watchdog and faults (spec 4.2, 11) ----------------------------------------- */

/** the firmware thread's scheduler entries; any thread */
uint64_t emu_progress(void);
/** the tier hook brackets a wait for the audio device or the wall clock */
void emu_waitBegin(void);
void emu_waitEnd(void);

typedef struct emu_watchdog
{
	uint64_t progress;	/**< progress at the last change */
	double since;		/**< wall time of the last change or wait */
	int primed;
} emu_watchdog_t;

/** Poll from the watchdog thread with the wall time in seconds. Returns 1 when
 * the firmware thread has made no progress for 10 s outside the hook's waits. */
int emu_watchdogPoll(emu_watchdog_t *w, double wallSeconds);

/** one scheduler event of the trail (spec 5.7, 6.9) */
typedef struct emu_trailEntry
{
	uint64_t t;
	uint8_t kind;	/**< emu_trailKind_t */
	uint8_t arg;
} emu_trailEntry_t;

typedef enum
{
	EMU_TRAIL_BODY,		/**< arg: emu_src_t of the body */
	EMU_TRAIL_PASS,		/**< a main-loop pass starts */
	EMU_TRAIL_BOOT,		/**< appMain_boot() starts */
	EMU_TRAIL_INPUT,	/**< arg: the input kind */
	EMU_TRAIL_DIRECT	/**< a direct call starts */
} emu_trailKind_t;

#define EMU_TRAIL_LENGTH 64u

/** copies the last n trail entries, oldest first; returns how many */
unsigned emu_trail(emu_trailEntry_t *out, unsigned n);
/** writes the last n trail entries as text to fd; async-signal-safe */
void emu_trailWrite(int fd, unsigned n);

#endif /* EMU_CORE_H_ */
