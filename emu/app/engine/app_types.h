/*
 * app_types.h
 *
 * The plain types that cross from the firmware thread to the GUI and back
 * (stage 1b spec 6.1): no atomics, so C++ and Objective-C++ include it too.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_TYPES_H_
#define APP_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "emu_core.h"

typedef enum { APP_MIX_SUM, APP_MIX_OUT1, APP_MIX_OUT2, APP_MIX_FOUR } app_mix_t;

#define APP_CLOCK_CHANNELS 64u	/**< the clock-out jack on output channel 1 to this; inputs as many (spec 6.8) */

typedef struct app_outputRequest { char uid[128]; uint32_t bufferFrames; unsigned channels; } app_outputRequest_t;
typedef struct app_outputInfo
{
	double deviceRate, clientRate;
	uint32_t deviceLatency, streamLatency, safetyOffset, bufferFrames, channels, deviceChannels;	/**< device frames; the open unit's outputs and the device's */
	char name[128], format[160];	/**< the device; the formats and the converter (spec 6.9) */
} app_outputInfo_t;

/** clock and reset in from audio (spec 6.8, plan R5): inputs 1-64, reset 0 none; thresholds 0.05-0.95 of full scale */
typedef struct app_clockInOptions
{
	int on;
	char uid[128];	/**< the input device's UID, "" the system's default input */
	int clockChannel, resetChannel;
	double clockThreshold, resetThreshold;
} app_clockInOptions_t;

/* the rings' entries (spec 6.1) */
typedef enum { APP_P_BUTTON, APP_P_ENCODER, APP_P_PUSH, APP_P_KNOB, APP_P_FADER, APP_P_JACKS, APP_P_CARD,
	APP_P_CLOCK, APP_P_RESET } app_panelKind_t;
/** BUTTON index 0-40, value 1 down; ENCODER value detents; PUSH value ms; KNOB index 1-4, value
 * degrees; FADER index 1-6, level; JACKS value EMU_JACK_* bits; CARD value 1 in; CLOCK and RESET
 * (value 1 on) carry the click's host time (E13) */
typedef struct app_panelEvent { uint64_t host; double level; int32_t value; uint8_t kind, index; } app_panelEvent_t;
typedef enum { APP_CMD_CARD_OUT, APP_CMD_CARD_IN, APP_CMD_DELAY, APP_CMD_MIX, APP_CMD_GENERATOR, APP_CMD_OUTPUT,
	APP_CMD_STOP, APP_CMD_CLOCK_OUT } app_cmdKind_t;
/** DELAY a = 1 automatic, else x = D in s; MIX a = app_mix_t; GENERATOR a = on, x = BPM, b = PPQ (4 or 24);
 * OUTPUT text = device UID ("" the default), a = buffer frames (0 the device's); CARD_OUT and CARD_IN a = a number that the
 * acknowledgement echoes, so that the sender can tell its own from an earlier one; CLOCK_OUT a = the output channel 1-64
 * of the clock-out jack, 0 off */
typedef struct app_cmd { uint8_t kind; int32_t a, b; double x; char text[128]; } app_cmd_t;
typedef enum { APP_ACK_CARD_OUT, APP_ACK_CARD_IN, APP_MSG_WARNING, APP_MSG_NOTICE } app_ackKind_t;
/** seq: the number of the CARD_OUT or CARD_IN command that this acknowledges (0 for the other kinds) */
typedef struct app_ack { uint8_t kind; int32_t seq; char text[255]; } app_ack_t;
enum { APP_NOTE_DEVICE, APP_NOTE_OVERLOAD };	/**< the notification ring's entries, one byte */

/** the firmware's counters of spec 5.5 */
typedef struct app_fwCounters
{
	uint32_t queueOverflow, dropouts, syncIgnored, intervalsExcluded, tempoRestarts, syncOverflows, usbTxDropped;
} app_fwCounters_t;

/** the diagnostics (spec 6.9), in every snapshot (E15) */
typedef struct app_diagnostics
{
	double blockRate, load;			/**< the last finished 2-s window */
	double L, D, Deff;				/**< s; Deff is 0 in internal sync */
	double leadMin, leadMax;		/**< s: drained events since the output opened */
	double inLatMin, inLatMax;		/**< s: the last finished 2-s window */
	double inputLead;				/**< s: the input's lead that L holds; 0 while no input runs (plan R5 F7) */
	int dAuto, extSync, outputRunning;
	int realtime;					/**< 1 if the firmware thread has the time-constraint policy (E29) */
	uint64_t outUnderruns, callbacks, overloads, mapJumps, midiStale, midiInDropped, midiOutDropped,
		midiUnsent, panelDropped, snapDropped, ackDropped, dinOrphans, warnings, jackInDropped, jackInSkipped;
	app_outputInfo_t output;
	emu_counters_t core;
	app_fwCounters_t fw;
} app_diagnostics_t;

typedef struct app_snapshot
{
	emu_lcdSnapshot_t lcd;
	uint64_t leds, vt;		/**< the latched LEDs (spec 4.7); the virtual time */
	uint8_t clockOut;		/**< the clock-out jack high at any time since the last snapshot */
	app_diagnostics_t diag;
} app_snapshot_t;

/** an output device for the GUI's list (Task 7) */
typedef struct app_outputDevice { uint32_t id; char uid[128], name[128]; unsigned channels; } app_outputDevice_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_TYPES_H_ */
