/*
 * app_shell.h
 *
 * LICHOMAT Emu without its face: start-up, the threads, the per-frame work of
 * the main thread. lichomat-emu-dev (E1) and the GUI (Task 12) both drive it,
 * through this header only.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_SHELL_H_
#define APP_SHELL_H_

#include <stddef.h>
#include <stdint.h>

#include "app_bind.h"
#include "app_settings.h"
#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_shell app_shell_t;

typedef struct app_shellOptions
{
	const char *cardDir;	/**< the card folder; NULL: the app folder's Card, seeded at the first run (spec 6.7). Card operations
							 * work on this folder, whichever it is */
	const char *home;		/**< the app folder; NULL: $LICHOMAT_EMU_HOME, else Application Support/LICHOMAT Emu (E21) */
	const char *render;		/**< lichomat-render, which seeds the first card; NULL: no seeding */
	/** starts the app again after an import (spec 6.7 step 4); called on the main thread with the shell stopped.
	 * 0 if a new process is on its way (it may not return), else -1. NULL: the card is put back in without a restart */
	int (*relaunch)(void *ctx);
	int noAudio, noMidi;
	const char *deviceUid;	/**< the output device's UID; NULL or "" the system's default */
	uint32_t bufferFrames;	/**< the output buffer in device frames; 0 the device's own */
	int32_t usbSrcUid, usbDstUid;	/**< the stored unique IDs of the virtual endpoints, 0 for none (spec 6.9) */
	int32_t dinSrcUid, dinDstUid;	/**< the stored DIN bindings: the unique ID first, then the name */
	const char *dinSrcName, *dinDstName;	/**< NULL or "" for none; copied by app_shellStart() */
	void (*message)(void *ctx, const char *text);	/**< warnings, notices, errors; main thread */
	void *ctx;
} app_shellOptions_t;

app_shell_t *app_shellNew(void);	/**< calloc'd; one per process */
int app_shellStart(app_shell_t *s, const app_shellOptions_t *o, char *err, size_t n);
int app_shellFrame(app_shell_t *s);	/**< main thread: snapshot, messages; 1 if a new snapshot came */
const app_snapshot_t *app_shellSnapshot(const app_shell_t *s);	/**< the newest; NULL before the first */
int app_shellPanel(app_shell_t *s, const app_panelEvent_t *ev);
int app_shellCommand(app_shell_t *s, const app_cmd_t *c);
int app_shellDevices(app_shell_t *s, app_outputDevice_t *out, int max);	/**< the output devices, for the GUI and `devices`; main thread */
enum { APP_OP_IMPORT, APP_OP_EXPORT, APP_OP_PROJECT };
/** Import from the folder path, export the card into it, or export project into it (replace 1: a PROJnn there goes
 * into Card backups). It posts 'card out'; app_shellFrame() does the work when 'card out applied' comes, puts the card
 * back (an import restarts the app instead) and says every result as a message: 'notice: ...', 'error: ...', or
 * 'ask: ... (repeat with replace)' where a project is in the way. No answer in 5 s: 'error: the card did not come out;
 * nothing was copied'. 0 posted, -1 not (one operation at a time). Main thread */
int app_shellCardOp(app_shell_t *s, int op, const char *path, int project, int replace);
/** the system's MIDI endpoints (not ours), sources or destinations, for the DIN bindings; main thread. 0 without MIDI */
int app_shellEndpoints(app_shell_t *s, int sources, app_endpoint_t *out, int max);
/** stores the DIN binding (source 1: the DIN input, 0: the output) and binds it; uid 0 and "" unbinds. Main thread */
void app_shellBind(app_shell_t *s, int source, int32_t uid, const char *name);
int app_shellHung(const app_shell_t *s);
int app_shellWarnings(const app_shell_t *s);				/**< the warnings kept, at most the last 64 (spec 11) */
const char *app_shellWarning(const app_shell_t *s, int i);	/**< oldest first */
void app_shellTestFault(app_shell_t *s);	/**< harness only: SIGSEGV on the firmware thread (the crash note, spec 6.9) */
/** tests only: the main thread handles an acknowledgement of kind app_ackKind_t and number seq as if the engine had sent it
 * (a late 'card out applied' of an operation that has timed out, say). Main thread */
void app_shellTestAck(app_shell_t *s, int kind, int32_t seq);
/** The settings of spec 6.9 (the app folder's settings.txt) are loaded by app_shellStart(), kept as the app runs and
 * saved within a second of a change. The window frame x, y, width, height is the GUI's to read at start and to set as
 * it moves; main thread */
void app_shellWindow(const app_shell_t *s, int out[4]);
void app_shellSetWindow(app_shell_t *s, const int frame[4]);
/** what the app runs with now: the settings file, the options and the changes since (spec 6.9). The GUI shows its Audio and
 * MIDI windows from it; main thread */
const app_settings_t *app_shellSettings(const app_shell_t *s);
const char *app_shellHome(const app_shell_t *s);	/**< the app folder, where the crash notes are; "" if there is none */
/** Power cycle (spec 6.9): 'card out', and on its acknowledgement (or after 2 s) app_shellRestart(). The harness's
 * `power` and the GUI's menu. Main thread */
void app_shellPowerCycle(app_shell_t *s);
/** Saves the settings, stops the MIDI-out thread and disposes of the MIDI endpoints, so that their unique IDs are
 * free for the new process (spec 6.9, E18), then calls the relaunch of the options. It needs no firmware thread, so it
 * is also the hang's Restart; it puts no card out. A failure is said as a message. Main thread; it may not return */
void app_shellRestart(app_shell_t *s);
/** Waits up to sec seconds for the process pid to end (kqueue, EVFILT_PROC): 0 if it has ended or is gone, -1 if it
 * is still running, pid is invalid or the wait failed. The GUI's main() and the harness call it before
 * app_shellStart() when they are given --after-pid N (E18); never on the main run loop */
int app_waitPid(int pid, double sec);
/** APP_CMD_STOP; joins the firmware thread for up to 2 s; then stops and joins the MIDI-out thread and disposes of the
 * MIDI endpoints, so that their unique IDs are free again (spec 6.9) */
void app_shellStop(app_shell_t *s);

#ifdef __cplusplus
}
#endif

#endif /* APP_SHELL_H_ */
