/*
 * panel.h
 *
 * The virtual panel of LICHOMAT Emu (stage 1b spec 6.6): a C interface to the Dear ImGui code in panel.cpp. The state is
 * the GUI's own: what the mouse and the keys have done to the controls, the windows that are open and the lines the
 * shell has said. The firmware's state comes in the shell's snapshot and goes out as panel events and commands.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PANEL_H_
#define PANEL_H_

#include <stdint.h>

#include "app_bind.h"
#include "app_shell.h"
#include "app_types.h"
#include "panel_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_LINES 64u		/**< the lines of the message list that are kept */
#define PANEL_LINE 512u
#define PANEL_DEVICES 32
#define PANEL_ENDPOINTS 64
#define PANEL_TEXT 8192u	/**< a crash note, as far as it is shown */

/** what the panel asks the Cocoa side to do, which needs a folder from the user, or the microphone permission (R5, F10)
 * (panel_request) */
enum { PANEL_REQ_NONE, PANEL_REQ_IMPORT, PANEL_REQ_EXPORT, PANEL_REQ_PROJECT, PANEL_REQ_REPLACE, PANEL_REQ_CLOCKIN };

typedef struct panel
{
	/* the controls (spec 6.6): a button is down if the mouse holds it, a secondary click has latched it or its key is down */
	uint8_t latched[PANEL_BUTTONS], mouse[PANEL_BUTTONS], down[PANEL_BUTTONS];
	float knobRest[PANEL_KNOBS];	/**< degrees not yet sent, toward 0 */
	int knobAngle[PANEL_KNOBS];		/**< 0-359: only for the drawing */
	float encRest, encDragged;		/**< detents not yet sent; points moved since the click */
	int encAngle;					/**< detents, for the drawing */
	float fader[PANEL_FADERS], faderSent[PANEL_FADERS];	/**< what the controls show; what the firmware has been sent */
	uint8_t jacks, jacksSent;		/**< EMU_JACK_* bits */
	uint8_t faderHold[PANEL_FADERS];	/**< a double click holds the fader full until the mouse is let go */
	uint8_t cardIn, cardSent, resetOn, resetSent, genOn;
	float genBpm;
	int genPpq;
	double clockOutAt;				/**< ImGui time of the last pulse of the clock-out LED */
	uint64_t leds;					/**< the newest snapshot's LEDs, kept for a frame without one */

	/* the windows and popups */
	int showDiag, showAudio, showMidi, showAbout, showMessages;
	int popupCrash, popupHang, popupAsk, popupError, popupProject, popupImport;
	int modalOpen;					/**< a popup was open in the last frame: the keys belong to it */
	int started;
	float dMs;						/**< D in the MIDI window, from the settings */
	int projectNumber;

	/* what the shell has said */
	char line[PANEL_LINES][PANEL_LINE];
	unsigned lines;					/**< all lines said so far; the last PANEL_LINES are kept */
	double lineAt;					/**< ImGui time of the newest line */
	char crashName[64], crashHead[PANEL_LINE], crashText[PANEL_TEXT];
	char hangText[PANEL_LINE], askText[PANEL_LINE], errorText[PANEL_LINE];

	/* lists for the Audio and MIDI windows, refreshed while they are open */
	app_outputDevice_t dev[PANEL_DEVICES];
	app_endpoint_t src[PANEL_ENDPOINTS], dst[PANEL_ENDPOINTS];
	int nDev, nSrc, nDst;
	double devAt, epAt;

	/* clock and reset in from audio (spec 6.8, plan R5): the options as the window shows them, which the Cocoa side asks
	 * the microphone for before the shell gets them (F10) */
	app_clockInOptions_t clockIn;
	int micDenied;					/**< macOS has said no to the microphone: the option stays off, with a message */
	int audioReset;					/**< the reset comes from the input now: the panel's switch is disabled (F11) */
	app_outputDevice_t inDev[PANEL_DEVICES];
	int nInDev;
	double inDevAt;

	/* for the Cocoa side: a card operation that needs a folder (PANEL_REQ_*), and the project it is for */
	int request, requestProject;
} panel_t;

/** the power-on state of the controls (spec 6.6): faders full, all jacks plugged, the card in, the knobs at 0 degrees */
void panel_init(panel_t *p);
/** a line from the shell's message callback ('notice: ...', 'error: ...', 'crash note: NAME\n...', 'hang: ...', 'ask: ...'); may hold newlines */
void panel_message(panel_t *p, const char *text);
/** One frame of ImGui, between NewFrame() and Render(): from the shell's newest snapshot, panel events and commands out.
 * Nothing but the menu bar and the popups before the first snapshot. */
void panel_frame(panel_t *p, app_shell_t *s);
/** copies the six clockin.* and reset.* settings into p->clockIn (the shell never opens the input by itself, F10) */
void panel_loadClockIn(panel_t *p, const app_settings_t *set);
/** the request of the last frame, once (PANEL_REQ_NONE if there is none); *project is the project of PANEL_REQ_PROJECT */
int panel_request(panel_t *p, int *project);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_H_ */
