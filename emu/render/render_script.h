/*
 * render_script.h
 *
 * The event scripts of lichomat-render (stage 1b spec 5.4): the parser. Times
 * are cycles from t0. The clock generators are expanded when t0 and the end are
 * known (render_main.c).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef RENDER_SCRIPT_H_
#define RENDER_SCRIPT_H_

#include <stddef.h>
#include <stdint.h>

#define RENDER_MAX_BYTES 256	/**< bytes of a din or usb line */
#define RENDER_MARK_NAME 64

typedef enum
{
	RS_DOWN, RS_UP, RS_PRESS, RS_ENCODER, RS_PUSH, RS_KNOB, RS_FADER,
	RS_DIN, RS_USB, RS_DIN_CLOCK, RS_USB_CLOCK, RS_JACK_CLOCK, RS_JACK_PULSE,
	RS_RESET, RS_CARD, RS_JACKS,
	/* the direct events: each runs at the first pass boundary at or after its time */
	RS_MARK,
	RS_PARAM	/**< the menu's edit path, as --param (spec 5.3) */
} render_eventKind_t;

typedef struct render_event
{
	uint64_t t;			/**< cycles from t0 */
	unsigned line;
	render_eventKind_t kind;
	int a, b;			/**< button, ms, detents, knob, degrees, fader, on/off, PPQ, jack mask;
						 * param: the parameter number and the value */
	double level;		/**< fader */
	uint64_t mbpm;		/**< clocks: BPM x 1000; 0 is off */
	uint64_t buffer;	/**< usb-clock: the buffer in cycles; 0 is none */
	uint8_t *bytes;		/**< din: the bytes; usb: the USB-MIDI packets */
	unsigned nbytes;
	char name[RENDER_MARK_NAME];	/**< mark */
} render_event_t;

typedef struct render_script
{
	render_event_t *ev;
	unsigned n, cap;
} render_script_t;

/** Parse the script at path into s. Returns 0, or -1 with "FILE:LINE: problem"
 * (or "FILE: strerror") in err. */
int render_scriptLoad(const char *path, render_script_t *s, char *err, size_t n);

/** 1 if the script sends a clock to the input of sync mode sync (1 EXT: a
 * jack-clock or jack-pulse; 2 MID: a din-clock, a usb-clock, or an F8 byte in
 * a din or usb line), else 0 (the fallback of stage 1b plan P3) */
int render_scriptHasClock(const render_script_t *s, int sync);

/** the name of an event kind, as the script writes it */
const char *render_eventName(render_eventKind_t kind);

#endif /* RENDER_SCRIPT_H_ */
