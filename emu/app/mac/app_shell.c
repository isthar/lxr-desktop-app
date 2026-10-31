/*
 * app_shell.c
 *
 * LICHOMAT Emu without its face (plan R4): the firmware thread and the watchdog
 * thread, and what the main thread does per frame. The struct is private; the
 * harness and the GUI see the opaque API of app_shell.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_shell.h"

#include <limits.h>
#include <mach/mach_time.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_card.h"
#include "app_coreaudio.h"
#include "app_coremidi.h"
#include "app_crash.h"
#include "app_mach.h"
#include "app_settings.h"
#include "app_time.h"

#define APP_SHELL_WARNINGS 64u
#define APP_SHELL_STACK ((size_t)8 << 20)	/* the main thread's size: the firmware's frames are deep */
#define APP_SHELL_SAVE_SEC 0.5	/**< a changed setting is saved this long after the first change: a slider's drag is one save */
#define APP_SHELL_POWER_SEC 2.0	/**< a power cycle waits this long for 'card out applied' */
#define APP_SHELL_STOP_SEC 1.0	/**< what app_shellStop() waits beyond the engine's longest wait: the first callback and the open */
#define APP_SHELL_HANG "hang: no progress for 10 s; the audio is stopped. Restart LICHOMAT Emu."

struct app_shell
{
	app_shellOptions_t o;
	app_engine_t engine;
	app_mach_t mach;
	app_engineConfig_t cfg;
	pthread_t firmware, watchdog;
	int haveFirmware, haveWatchdog;
	_Atomic int stopped, testFault, testStallMs;
	_Atomic double openSec;			/**< the longest output open so far; the firmware thread writes it */
	int hungShown, haveSnap;
	app_snapshot_t snap;
	app_ca_t ca;
	app_ci_t ci;					/**< the input of clock and reset in from audio (F9); the main thread owns it */
	int haveInput, inputAllowed;	/**< the input unit is open; the option is on and the microphone was granted */
	double inputRetryAt;			/**< app_wallSeconds() of the next try while the input should run and is closed */
	char inputErr[256];				/**< the last open failure said */
	app_cm_t cm;
	pthread_t midiOut;
	int haveMidi, haveMidiThread;	/**< the CoreMIDI client exists; the MIDI-out thread runs */
	_Atomic int midiStop;
	int32_t dinUid[2];				/**< [source]: 1 the DIN input, 0 the output */
	char dinName[2][128];
	int dinTold[2];					/**< 'not connected' has been said for the binding as it is now */
	double nextBind;				/**< app_wallSeconds() of the next check (E28) */
	app_paths_t paths;				/**< paths.card is the folder that the engine runs on */
	int havePaths;
	int opActive, op, opProject, opReplace;	/**< a card operation waits for 'card out applied' */
	int32_t cardSeq, opSeq, powerSeq;	/**< the number of the last card command; of the operation's and the power cycle's CARD_OUT */
	char opPath[PATH_MAX];
	double opDeadline, nextZone;	/**< app_wallSeconds() */
	app_settings_t set;				/**< what the app runs with now (spec 6.9); saved to paths.settings */
	int setDirty, setSaveFailed;	/**< a change is waiting to be saved; the failure has been said */
	double setSaveAt;				/**< app_wallSeconds() */
	int powerActive;				/**< a power cycle waits for 'card out applied' */
	double powerDeadline;
	int relaunching;				/**< the relaunch of the options has been called and has not failed: a new process is on its way */
	char outputErr[256];		/**< the last open failure told to the main thread; firmware thread */
	char card[PATH_MAX];
	char warnings[APP_SHELL_WARNINGS][255];
	unsigned warningsTotal;
};

app_shell_t *app_shellNew(void)
{
	return calloc(1u, sizeof(app_shell_t));
}

/* The host's output, on the firmware thread (spec 6.1): the open, then the thread's period is the buffer's duration.
 * A failure goes to the main thread as a notice, once per error text. */
static int app_shellOutputOpen(void *ctx, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info)
{
	app_mach_t *m = ctx;
	app_shell_t *s = m->owner;
	char err[256];
	const double t0 = app_wallSeconds();
	const int failed = app_caOpen(&s->ca, a, req, info, err, sizeof err) != 0;
	if (app_wallSeconds() - t0 > atomic_load(&s->openSec))
		atomic_store(&s->openSec, app_wallSeconds() - t0);	/* app_shellStop() waits at least this long for the engine */
	if (failed)
	{
		if (strcmp(err, s->outputErr) != 0)
		{
			app_ack_t ack;
			memset(&ack, 0, sizeof ack);
			ack.kind = APP_MSG_NOTICE;
			snprintf(ack.text, sizeof ack.text, "audio output: %s", err);
			(void)app_ringPush(&s->engine.ack, &ack);
			snprintf(s->outputErr, sizeof s->outputErr, "%s", err);
		}
		return -1;
	}
	s->outputErr[0] = '\0';
	if (info->deviceRate > 0.0 && info->bufferFrames > 0u)
		atomic_store(&m->period, (double)info->bufferFrames / info->deviceRate);	/* the MIDI-out thread follows it */
	m->realtime = app_threadRealtime(atomic_load(&m->period)) == 0;	/* E29 */
	s->engine.diag.realtime = m->realtime;
	return 0;
}

static void app_shellOutputClose(void *ctx)
{
	app_mach_t *m = ctx;
	app_caClose(&((app_shell_t *)m->owner)->ca);
}

/* the firmware thread: power-on, then one slice after the other until APP_CMD_STOP */
static void *app_shellFirmware(void *arg)
{
	app_shell_t *s = arg;
	app_crashThread();	/* its own alternate stack, for a fault signal on this thread (spec 6.9) */
	app_crashSetFirmware();	/* the note says whether the fault was here or elsewhere */
	s->mach.realtime = app_threadRealtime(atomic_load(&s->mach.period)) == 0;	/* E29: a refusal is not an error */
	s->engine.diag.realtime = s->mach.realtime;
	app_enginePowerOn(&s->engine, &s->cfg);
	while (!app_engineSlice(&s->engine))
	{
		if (atomic_load(&s->testFault))
			raise(SIGSEGV);	/* the crash check, on this thread */
		const int stall = atomic_exchange(&s->testStallMs, 0);
		if (stall > 0)
			nanosleep(&(const struct timespec){ stall / 1000, (long)(stall % 1000) * 1000000L }, NULL);
	}
	if (s->engine.outputRunning)
	{
		emu_waitBegin();	/* stopping a unit can take a while: a wait, not a hang (spec 4.2) */
		app_shellOutputClose(&s->mach);	/* the unit stops before the thread does */
		emu_waitEnd();
	}
	atomic_store(&s->stopped, 1);
	return NULL;
}

/* the MIDI-out thread (spec 6.1, 6.5): DIN messages at once, USB messages at their presentation time. Its wake-up
 * precision is MIDI out's jitter. */
static void *app_shellMidiOut(void *arg)
{
	app_shell_t *s = arg;
	double applied = 0.0;
	const app_midiOutSink_t sink = { &s->cm, app_cmSend };
	while (!atomic_load(&s->stopped) && !atomic_load(&s->midiStop))
	{
		/* the period is the output's buffer duration, which a new device or a new size changes (spec 6.1); a refusal is
		 * not an error here either (E29) */
		(void)app_threadRealtimeFollow(&applied, atomic_load(&s->mach.period));
		const uint64_t now = mach_absolute_time();
		const uint64_t next = app_midiOutStep(&s->engine, now, &sink);
		const uint64_t limit = now + (uint64_t)(0.02 * s->mach.ticksPerSec);	/* the stop flags are looked at every 20 ms */
		/* every push signals the semaphore: a DIN message that comes while a USB entry is awaited goes out at once (E11) */
		app_machWaitSem(s->mach.midiWake, next != 0u && next < limit ? next : limit, s->mach.ticksPerSec);
	}
	return NULL;
}

/* spec 4.2, 6.9: 10 s without progress outside the hook's waits stop the audio */
static void *app_shellWatchdog(void *arg)
{
	app_shell_t *s = arg;
	emu_watchdog_t w;
	memset(&w, 0, sizeof w);
	while (!atomic_load(&s->stopped))
	{
		const struct timespec d = { 0, 100000000L };
		(void)nanosleep(&d, NULL);
		if (emu_watchdogPoll(&w, app_wallSeconds()))
		{
			atomic_store(&s->engine.audio.mute, 1);
			atomic_store(&s->engine.hung, 1);
			break;
		}
	}
	return NULL;
}

static void app_shellSay(app_shell_t *s, const char *text)
{
	if (s->o.message)
		s->o.message(s->o.ctx, text);
}

/* binds one DIN endpoint to what is stored; 'not connected' is said once per binding, not once per check (E28) */
static void app_shellBindNow(app_shell_t *s, int source)
{
	if (!s->haveMidi)
		return;
	const int ok = app_cmBind(&s->cm, source, s->dinUid[source], s->dinName[source]);
	const int stored = s->dinUid[source] != 0 || s->dinName[source][0] != '\0';
	if (ok || !stored)
		s->dinTold[source] = 0;
	else if (!s->dinTold[source])
	{
		char line[300];
		s->dinTold[source] = 1;
		if (s->dinName[source][0])
			snprintf(line, sizeof line, "notice: DIN %s %s not connected", source ? "source" : "destination",
				s->dinName[source]);
		else
			snprintf(line, sizeof line, "notice: DIN %s %d not connected", source ? "source" : "destination",
				(int)s->dinUid[source]);
		app_shellSay(s, line);
	}
}

/* a card or engine warning: kept for the diagnostics (spec 11) and said */
static void app_shellKeepWarning(app_shell_t *s, const char *text)
{
	char line[300];
	snprintf(s->warnings[s->warningsTotal++ % APP_SHELL_WARNINGS], sizeof s->warnings[0], "%s", text);
	snprintf(line, sizeof line, "warning: %s", text);
	app_shellSay(s, line);
}

static void app_shellCopyWarn(void *ctx, const char *what)
{
	app_shellKeepWarning(ctx, what);
}

static void app_shellSayf(app_shell_t *s, const char *kind, const char *what, const char *a, const char *b)
{
	char line[PATH_MAX * 2 + 300];
	snprintf(line, sizeof line, "%s: %s%s%s", kind, what, a ? a : "", b ? b : "");
	app_shellSay(s, line);
}

/* the MIDI-out thread, then the endpoints: after this their unique IDs are free (E18) */
static void app_shellMidiStop(app_shell_t *s)
{
	if (s->haveMidiThread)
	{
		atomic_store(&s->midiStop, 1);
		(void)semaphore_signal(s->mach.midiWake);
		(void)pthread_join(s->midiOut, NULL);	/* it looks at the flag every 20 ms */
		s->haveMidiThread = 0;
	}
	if (s->haveMidi)
	{
		app_cmDispose(&s->cm);
		s->haveMidi = 0;
	}
}

/* a model check of the firmware (spec 11): the crash note, then the end of the app, as for a fault signal */
static void app_shellFault(void *ctx, const char *what)
{
	(void)ctx;
	fprintf(stderr, "LICHOMAT Emu: %s\n", what);	/* the terminal of the harness, if there is one */
	(void)app_crashWrite(what);
	abort();
}

static void app_shellSave(app_shell_t *s)
{
	s->setDirty = 0;
	if (!s->havePaths)
		return;
	if (app_settingsSave(&s->set, s->paths.settings) != 0)
	{
		if (!s->setSaveFailed)	/* once, not at every try */
			app_shellSay(s, "error: the settings could not be saved");
		s->setSaveFailed = 1;
	}
	else
		s->setSaveFailed = 0;
}

/* a setting has changed: it is saved shortly, and a burst of changes is one save */
static void app_shellChanged(app_shell_t *s)
{
	if (!s->setDirty)
	{
		s->setDirty = 1;
		s->setSaveAt = app_wallSeconds() + APP_SHELL_SAVE_SEC;
	}
}

static const char *const app_shellMixNames[] = { "sum", "out1", "out2", "four" };

/* the settings that a command to the firmware thread changes (spec 6.9). Values the file would refuse are not stored. */
static void app_shellNoteCommand(app_shell_t *s, const app_cmd_t *c)
{
	app_settings_t *t = &s->set;
	switch (c->kind)
	{
	case APP_CMD_DELAY:
		t->dAuto = c->a != 0;
		if (!t->dAuto && c->x >= 0.0)	/* the file holds 0-500 ms; to the microsecond, as a person enters it */
			t->dMs = round(fmin(c->x, 0.5) * 1e6) / 1e3;
		break;
	case APP_CMD_MIX:
		if (c->a >= 0 && c->a < 4)
			snprintf(t->mix, sizeof t->mix, "%s", app_shellMixNames[c->a]);
		break;
	case APP_CMD_GENERATOR:
		if (c->a && c->x >= 1.0 && c->x <= APP_GEN_BPM_MAX && (c->b == 4 || c->b == 24))
		{
			t->genBpm = c->x;
			t->genPpq = c->b;
		}
		break;
	case APP_CMD_CLOCK_OUT:	/* a channel the engine takes (1-64) is on; the channel is kept while it is off */
		t->clockOutAudio = c->a >= 1 && c->a <= (int)APP_CLOCK_CHANNELS;
		if (t->clockOutAudio)
			t->clockOutChannel = c->a;
		break;
	case APP_CMD_OUTPUT:
		snprintf(t->outputUid, sizeof t->outputUid, "%s", c->text);
		t->bufferFrames = c->a > 0 && c->a <= 4096 ? (uint32_t)c->a : 0u;
		break;
	default:
		return;
	}
	app_shellChanged(s);
}

/* closes the input: the callback has returned for good then, so this thread may push the release of a high reset (F11) */
static void app_shellInputClose(app_shell_t *s)
{
	if (!s->haveInput)
		return;
	app_ciClose(&s->ci);
	s->haveInput = 0;
	app_jackInStop(&s->engine.jackIn, mach_absolute_time());
}

static int app_shellInputOpen(app_shell_t *s)
{
	const app_settings_t *t = &s->set;
	app_clockInOptions_t o;
	char err[256];
	memset(&o, 0, sizeof o);
	o.on = 1;
	snprintf(o.uid, sizeof o.uid, "%s", t->clockInDevice);
	o.clockChannel = t->clockInChannel;
	o.resetChannel = t->resetChannel;
	o.clockThreshold = t->clockInThreshold;
	o.resetThreshold = t->resetThreshold;
	if (app_ciOpen(&s->ci, &s->engine.jackIn, &o, s->mach.ticksPerSec, err, sizeof err) == 0)
	{
		s->haveInput = 1;
		s->inputErr[0] = '\0';
		return 0;
	}
	s->inputRetryAt = app_wallSeconds() + 2.0;
	if (strcmp(err, s->inputErr) != 0)	/* said once per text, not at every try */
	{
		char line[300];
		snprintf(s->inputErr, sizeof s->inputErr, "%s", err);
		snprintf(line, sizeof line, "notice: clock in from audio: %s", err);
		app_shellSay(s, line);
	}
	return -1;
}

int app_shellClockIn(app_shell_t *s, const app_clockInOptions_t *o)
{
	app_settings_t *t = &s->set;
	const int cc = o->clockChannel >= 1 && o->clockChannel <= (int)APP_CLOCK_CHANNELS ? o->clockChannel : t->clockInChannel;
	const int rc = o->resetChannel >= 0 && o->resetChannel <= (int)APP_CLOCK_CHANNELS ? o->resetChannel : t->resetChannel;
	char uid[sizeof o->uid];
	snprintf(uid, sizeof uid, "%s", o->uid);
	const int reopen = !s->haveInput || strcmp(uid, t->clockInDevice) != 0 || cc != t->clockInChannel || rc != t->resetChannel;
	t->clockInAudio = s->inputAllowed = o->on != 0;
	snprintf(t->clockInDevice, sizeof t->clockInDevice, "%s", uid);
	t->clockInChannel = cc;
	t->resetChannel = rc;
	if (o->clockThreshold >= 0.05 && o->clockThreshold <= 0.95)	/* NaN fails both */
		t->clockInThreshold = o->clockThreshold;
	if (o->resetThreshold >= 0.05 && o->resetThreshold <= 0.95)
		t->resetThreshold = o->resetThreshold;
	app_shellChanged(s);
	atomic_store(&s->engine.jackIn.clockThreshold, (float)t->clockInThreshold);	/* a running input follows at once */
	atomic_store(&s->engine.jackIn.resetThreshold, (float)t->resetThreshold);
	if (!o->on)
	{
		app_shellInputClose(s);
		s->inputErr[0] = '\0';
		return 0;
	}
	if (!reopen)
		return 0;
	app_shellInputClose(s);
	return app_shellInputOpen(s);
}

int app_shellInputs(app_shell_t *s, app_outputDevice_t *out, int max)
{
	(void)s;
	return app_caInputs(out, max);
}

int app_shellInputInfo(const app_shell_t *s, app_outputInfo_t *out, const char **err)
{
	if (err)
		*err = s->inputErr;
	if (s->haveInput && out)
		*out = s->ci.info;
	return s->haveInput;
}

static void app_shellCardIn(app_shell_t *s)
{
	app_cmd_t c;
	memset(&c, 0, sizeof c);
	c.kind = APP_CMD_CARD_IN;
	c.a = ++s->cardSeq;
	(void)app_engineCommand(&s->engine, &c);
}

/* 'card out applied': the operation of app_shellCardOp(), on the main thread (spec 6.7) */
static void app_shellRunOp(app_shell_t *s)
{
	char backup[PATH_MAX], err[PATH_MAX + 100];
	const int64_t now = app_localSeconds();
	int restart = 0;
	err[0] = '\0';
	s->opActive = 0;
	if (s->op == APP_OP_IMPORT)
	{
		const int rc = app_cardImport(&s->paths, s->opPath, now, backup, sizeof backup, app_shellCopyWarn, s);
		if (rc == -2)	/* the copy failed and the old card could not be moved back */
			app_shellSayf(s, "error", "the import failed and the card could not be put back: it is in ", backup,
				"; move it back by hand");
		else if (rc != 0)
			app_shellSayf(s, "error", "the import from ", s->opPath, " failed; the card is as it was");
		else
		{
			char line[PATH_MAX * 2 + 100];
			snprintf(line, sizeof line, "notice: imported %s%s%s", s->opPath, backup[0] ? "; the card before is in " : "",
				backup);
			app_shellSay(s, line);
			restart = 1;
		}
	}
	else if (s->op == APP_OP_EXPORT)
	{
		const int rc = app_cardExport(s->paths.card, s->opPath, err, sizeof err, app_shellCopyWarn, s);
		if (rc == -3)	/* the card's own folder, or one in it or around it */
			app_shellSayf(s, "error", err, "; nothing was copied", NULL);
		else if (rc == -2)
			app_shellSayf(s, "error", s->opPath, " is not empty, nothing was copied: ", err);
		else if (rc != 0)
			app_shellSayf(s, "error", "the export into ", s->opPath, " failed");
		else
			app_shellSayf(s, "notice", "exported the card into ", s->opPath, NULL);
	}
	else
	{
		const int rc = app_cardExportProject(&s->paths, s->opProject, s->opPath, s->opReplace, now, err, sizeof err,
			app_shellCopyWarn, s);
		if (rc == 2)
			app_shellSayf(s, "ask", err, " (repeat with replace)", NULL);
		else if (rc != 0)
			app_shellSayf(s, "error", err, NULL, NULL);
		else
			app_shellSayf(s, "notice", "exported the project into ", s->opPath, NULL);
	}
	if (!restart || !s->o.relaunch)
	{
		if (restart)
			app_shellSay(s, "notice: no restart from here; power cycle to load the imported card");
		app_shellCardIn(s);	/* the firmware remounts it as after a card swap */
		return;
	}
	app_shellRestart(s);	/* the card is out and nothing writes (E18) */
}

void app_shellRestart(app_shell_t *s)
{
	if (s->relaunching)
		return;	/* a new process is on its way; a second one would start a third */
	if (!s->o.relaunch)
	{
		app_shellSay(s, "notice: no restart from here");
		return;
	}
	s->relaunching = 1;
	s->opActive = s->powerActive = 0;	/* a card operation or power cycle that waited for 'card out applied' ends here: nothing is copied */
	app_shellSay(s, "notice: restarting...");
	app_shellSave(s);	/* the settings, with the endpoints' IDs, are what the new process starts from */
	app_shellInputClose(s);
	app_shellMidiStop(s);	/* their unique IDs are free before the new process asks for them (E18, A10) */
	if (s->o.relaunch(s->o.ctx) != 0)
	{
		s->relaunching = 0;	/* nothing is on its way: the user may try again */
		app_shellSay(s, "error: the app could not start itself again; start it by hand");
	}
}

int app_shellRelaunching(const app_shell_t *s)
{
	return s->relaunching;
}

void app_shellPowerCycle(app_shell_t *s)
{
	app_cmd_t c;
	if (s->relaunching)
		return;	/* the new process is on its way (app_shellRestart) */
	if (s->opActive || s->powerActive)
	{
		app_shellSay(s, "error: a card operation or a power cycle is running");
		return;
	}
	if (!s->o.relaunch)
	{
		app_shellSay(s, "notice: no power cycle from here");
		return;
	}
	memset(&c, 0, sizeof c);
	c.kind = APP_CMD_CARD_OUT;
	c.a = s->cardSeq + 1;
	if (app_engineCommand(&s->engine, &c) != 0)
	{
		app_shellSay(s, "error: the command ring is full; no power cycle");
		return;
	}
	s->powerSeq = s->cardSeq = c.a;
	s->powerActive = 1;
	s->powerDeadline = app_wallSeconds() + APP_SHELL_POWER_SEC;
}

int app_shellCardOp(app_shell_t *s, int op, const char *path, int project, int replace)
{
	app_cmd_t c;
	if (!s->havePaths || s->relaunching || s->opActive || s->powerActive || !path || !path[0]
		|| snprintf(s->opPath, sizeof s->opPath, "%s", path) >= (int)sizeof s->opPath)
		return -1;
	memset(&c, 0, sizeof c);
	c.kind = APP_CMD_CARD_OUT;
	c.a = s->cardSeq + 1;
	if (app_engineCommand(&s->engine, &c) != 0)
		return -1;
	s->opSeq = s->cardSeq = c.a;
	s->op = op;
	s->opProject = project;
	s->opReplace = replace;
	s->opActive = 1;
	s->opDeadline = app_wallSeconds() + 5.0;
	return 0;
}

static int app_shellFail(char *err, size_t n, const char *what, int code)
{
	if (err && n)
		snprintf(err, n, "app: %s%s%s", what, code ? ": " : "", code ? strerror(code) : "");
	return -1;
}

int app_shellStart(app_shell_t *s, const app_shellOptions_t *o, char *err, size_t n)
{
	app_host_t host;
	s->o = *o;
	if (o->cardDir)
	{
		if (snprintf(s->card, sizeof s->card, "%s", o->cardDir) >= (int)sizeof s->card)
			return app_shellFail(err, n, "the card folder's path is too long", 0);
		s->o.cardDir = s->card;
	}
	app_timeZoneRefresh();
	s->havePaths = (o->home ? app_pathsAt(&s->paths, o->home) : app_pathsInit(&s->paths)) == 0;
	app_settingsDefaults(&s->set);
	if (s->havePaths)
	{
		if (app_settingsLoad(&s->set, s->paths.settings) < 0)
			app_shellSay(s, "notice: settings.txt could not be read; the defaults are used");
		app_crashInit(s->paths.home, emu_now, emu_trailWrite);	/* the trail writer is the core's, async-signal-safe */
		app_crashThread();
		app_crashSignals();
	}
	if (o->cardDir)
	{
		if (s->havePaths)
			snprintf(s->paths.card, sizeof s->paths.card, "%s", s->o.cardDir);	/* the card operations work on it */
	}
	else
	{
		char why[300];
		if (!s->havePaths)
			return app_shellFail(err, n, "the app folder cannot be made", 0);
		const int state = app_cardState(s->paths.card);
		if (state < 0)
		{
			app_shellSay(s, "error: the card folder cannot be read");
			return app_shellFail(err, n, "the card folder cannot be read", 0);
		}
		if (state == 1)	/* the first run: an init card from the firmware's own code (spec 6.7) */
		{
			if (!o->render)
				return app_shellFail(err, n, "no lichomat-render to make the first card", 0);
			if (app_cardSeed(o->render, s->paths.card, why, sizeof why) != 0)
				return app_shellFail(err, n, why, 0);
		}
		snprintf(s->card, sizeof s->card, "%s", s->paths.card);
		s->o.cardDir = s->card;
	}
	int project = 0;
	if (app_cardCheck(s->o.cardDir, &project) == 0)
	{
		char line[120];
		snprintf(line, sizeof line, "notice: project %d has no PRJ.NFO: the device boots with an empty project", project);
		app_shellSay(s, line);	/* spec 11 */
	}
	s->nextZone = app_wallSeconds() + 60.0;
	if (app_machInit(&s->mach, &s->engine) != 0)
		return app_shellFail(err, n, "the Mach host did not start", 0);
	s->mach.owner = s;
	app_machHost(&s->mach, &host);
	if (!o->noAudio)
	{
		s->mach.output = &s->ca;
		s->ca.engine = &s->engine;
		host.outputOpen = app_shellOutputOpen;	/* the host is copied by app_engineInit() */
		host.outputClose = app_shellOutputClose;
	}
	if (app_engineInit(&s->engine, &host) != 0)	/* it zeroes the engine */
		return app_shellFail(err, n, "out of memory for the rings", 0);
	s->engine.audio.wake = app_machWake;
	s->engine.audio.wakeCtx = &s->mach;
	/* the options given on the command line win over the settings; what runs is then what the settings hold */
	if (o->deviceUid && o->deviceUid[0])
		snprintf(s->set.outputUid, sizeof s->set.outputUid, "%s", o->deviceUid);
	if (o->bufferFrames)
		s->set.bufferFrames = o->bufferFrames;
	if (o->usbSrcUid)
		s->set.usbSrcUid = o->usbSrcUid;
	if (o->usbDstUid)
		s->set.usbDstUid = o->usbDstUid;
	if (o->dinSrcUid || (o->dinSrcName && o->dinSrcName[0]))
	{
		s->set.dinSrcUid = o->dinSrcUid;
		snprintf(s->set.dinSrcName, sizeof s->set.dinSrcName, "%s", o->dinSrcName ? o->dinSrcName : "");
	}
	if (o->dinDstUid || (o->dinDstName && o->dinDstName[0]))
	{
		s->set.dinDstUid = o->dinDstUid;
		snprintf(s->set.dinDstName, sizeof s->set.dinDstName, "%s", o->dinDstName ? o->dinDstName : "");
	}
	app_mix_t mix = APP_MIX_SUM;
	for (int k = 0; k < 4; k++)
		if (!strcmp(s->set.mix, app_shellMixNames[k]))
			mix = (app_mix_t)k;
	s->cfg = (app_engineConfig_t){ .cardDir = s->o.cardDir, .seed = app_seedNow(), .dAuto = s->set.dAuto,
		.dSec = s->set.dMs / 1e3, .mix = mix, .clockOutChannel = s->set.clockOutAudio ? s->set.clockOutChannel : 0,
		.output = { "", s->set.bufferFrames, 2u }, .fault = app_shellFault, .faultCtx = s };
	snprintf(s->cfg.output.uid, sizeof s->cfg.output.uid, "%s", s->set.outputUid);
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, APP_SHELL_STACK);
	int e = pthread_create(&s->firmware, &attr, app_shellFirmware, s);
	if (e != 0)
	{
		pthread_attr_destroy(&attr);
		return app_shellFail(err, n, "the firmware thread did not start", e);
	}
	s->haveFirmware = 1;
	e = pthread_create(&s->watchdog, NULL, app_shellWatchdog, s);
	pthread_attr_destroy(&attr);
	if (e != 0)
	{
		app_shellStop(s);
		return app_shellFail(err, n, "the watchdog thread did not start", e);
	}
	s->haveWatchdog = 1;
	s->dinUid[1] = s->set.dinSrcUid;
	s->dinUid[0] = s->set.dinDstUid;
	snprintf(s->dinName[1], sizeof s->dinName[1], "%s", s->set.dinSrcName);
	snprintf(s->dinName[0], sizeof s->dinName[0], "%s", s->set.dinDstName);
	if (!o->noMidi)
	{
		char merr[256];
		if (app_cmInit(&s->cm, &s->engine, s->set.usbSrcUid, s->set.usbDstUid, merr, sizeof merr) != 0)
		{
			char line[300];
			snprintf(line, sizeof line, "notice: MIDI is off: %s", merr);	/* the app runs on without it */
			app_shellSay(s, line);
		}
		else
		{
			s->haveMidi = 1;
			s->set.usbSrcUid = s->cm.usbSrcUid;	/* the IDs they have now are asked for again at the next start (spec 6.9) */
			s->set.usbDstUid = s->cm.usbDstUid;
			e = pthread_create(&s->midiOut, NULL, app_shellMidiOut, s);
			if (e != 0)
			{
				app_shellStop(s);
				return app_shellFail(err, n, "the MIDI-out thread did not start", e);
			}
			s->haveMidiThread = 1;
			app_shellBindNow(s, 1);
			app_shellBindNow(s, 0);
			s->nextBind = app_wallSeconds() + 1.0;
		}
	}
	char note[64];
	if (s->havePaths && app_crashLatest(s->paths.home, s->set.crashShown, note, sizeof note))
	{
		/* a note from an earlier run (spec 6.9): its name and its first 3 lines, said once */
		char path[PATH_MAX + 80], text[400];
		size_t k = (size_t)snprintf(text, sizeof text, "crash note: %s", note);
		snprintf(path, sizeof path, "%s/%s", s->paths.home, note);
		FILE *f = fopen(path, "r");
		char row[200];
		for (int i = 0; f && i < 3 && fgets(row, sizeof row, f) && k + 1u < sizeof text; i++)
		{
			row[strcspn(row, "\r\n")] = '\0';
			k += (size_t)snprintf(text + k, sizeof text - k, "\n%s", row);
		}
		if (f)
			fclose(f);
		app_shellSay(s, text);
		snprintf(s->set.crashShown, sizeof s->set.crashShown, "%s", note);
	}
	app_shellSave(s);	/* the first run's file, the endpoints' IDs, the note that has been shown */
	return 0;
}

/* one acknowledgement or message of the engine, on the main thread */
static void app_shellAck(app_shell_t *s, const app_ack_t *a)
{
	char line[300];
	if (a->kind == APP_MSG_WARNING)
		app_shellKeepWarning(s, a->text);
	else if (a->kind == APP_ACK_CARD_OUT)
	{
		/* only the acknowledgement of this operation's own CARD_OUT: one that comes late from an operation that has
		 * timed out says nothing about the card now */
		if (s->opActive && a->seq == s->opSeq)
			app_shellRunOp(s);
		else if (s->powerActive && a->seq == s->powerSeq)
		{
			s->powerActive = 0;
			app_shellRestart(s);	/* the card is out and nothing writes (E18) */
		}
	}
	else if (a->kind == APP_MSG_NOTICE)
	{
		snprintf(line, sizeof line, "notice: %s", a->text);
		app_shellSay(s, line);
	}
}

void app_shellTestAck(app_shell_t *s, int kind, int32_t seq)
{
	app_ack_t a;
	memset(&a, 0, sizeof a);
	a.kind = (uint8_t)kind;
	a.seq = seq;
	app_shellAck(s, &a);
}

int app_shellFrame(app_shell_t *s)
{
	app_ack_t a;
	while (app_engineAck(&s->engine, &a))
		app_shellAck(s, &a);
	const double wall = app_wallSeconds();
	if (s->opActive && wall > s->opDeadline)
	{
		s->opActive = 0;
		app_shellSay(s, "error: the card did not come out; nothing was copied");
		app_shellCardIn(s);
	}
	if (s->powerActive && wall > s->powerDeadline)
	{
		s->powerActive = 0;
		app_shellRestart(s);	/* no answer in 2 s: the power goes off all the same (spec 6.9) */
	}
	if (s->haveInput && atomic_exchange(&s->ci.changed, 0))
		app_shellInputClose(s);	/* the device went, or its rate or inputs changed: a new try below (F9) */
	if (s->set.clockInAudio && s->inputAllowed && !s->haveInput && wall >= s->inputRetryAt)
		(void)app_shellInputOpen(s);
	if (s->setDirty && wall >= s->setSaveAt)
		app_shellSave(s);
	if (wall >= s->nextZone)
	{
		s->nextZone = wall + 60.0;
		app_timeZoneRefresh();	/* the offset that backup names use changes with the clocks (E20) */
	}
	if (s->haveMidi && app_wallSeconds() >= s->nextBind)
	{
		s->nextBind = app_wallSeconds() + 1.0;
		app_shellBindNow(s, 1);
		app_shellBindNow(s, 0);
	}
	const int fresh = app_engineSnapshot(&s->engine, &s->snap);
	if (fresh)
		s->haveSnap = 1;
	if (!s->hungShown && atomic_load(&s->engine.hung))
	{
		s->hungShown = 1;
		app_shellSay(s, APP_SHELL_HANG);
	}
	return fresh;
}

const app_snapshot_t *app_shellSnapshot(const app_shell_t *s)
{
	return s->haveSnap ? &s->snap : NULL;
}

int app_shellPanel(app_shell_t *s, const app_panelEvent_t *ev)
{
	return app_enginePanel(&s->engine, ev) == 0 ? 0 : -1;
}

int app_shellCommand(app_shell_t *s, const app_cmd_t *c)
{
	if (app_engineCommand(&s->engine, c) != 0)
		return -1;
	app_shellNoteCommand(s, c);
	return 0;
}

int app_shellDevices(app_shell_t *s, app_outputDevice_t *out, int max)
{
	(void)s;
	return app_caList(out, max);
}

int app_shellEndpoints(app_shell_t *s, int sources, app_endpoint_t *out, int max)
{
	return s->haveMidi ? app_cmList(&s->cm, sources, out, max) : 0;
}

void app_shellBind(app_shell_t *s, int source, int32_t uid, const char *name)
{
	source = source != 0;
	s->dinUid[source] = uid;
	snprintf(s->dinName[source], sizeof s->dinName[source], "%s", name ? name : "");
	s->dinTold[source] = 0;
	app_shellBindNow(s, source);
	if (source)
	{
		s->set.dinSrcUid = s->dinUid[1];
		snprintf(s->set.dinSrcName, sizeof s->set.dinSrcName, "%s", s->dinName[1]);
	}
	else
	{
		s->set.dinDstUid = s->dinUid[0];
		snprintf(s->set.dinDstName, sizeof s->set.dinDstName, "%s", s->dinName[0]);
	}
	app_shellChanged(s);
}

void app_shellWindow(const app_shell_t *s, int out[4])
{
	memcpy(out, s->set.window, sizeof s->set.window);
}

void app_shellSetWindow(app_shell_t *s, const int frame[4])
{
	if (memcmp(frame, s->set.window, sizeof s->set.window) == 0)
		return;
	for (int i = 0; i < 4; i++)	/* the file would refuse these at the next start */
		if (frame[i] < (i < 2 ? -100000 : 200) || frame[i] > 100000)
			return;
	memcpy(s->set.window, frame, sizeof s->set.window);
	app_shellChanged(s);
}

const app_settings_t *app_shellSettings(const app_shell_t *s)
{
	return &s->set;
}

const char *app_shellHome(const app_shell_t *s)
{
	return s->havePaths ? s->paths.home : "";
}

int app_shellHung(const app_shell_t *s)
{
	return atomic_load(&s->engine.hung);
}

int app_shellWarnings(const app_shell_t *s)
{
	return (int)(s->warningsTotal < APP_SHELL_WARNINGS ? s->warningsTotal : APP_SHELL_WARNINGS);
}

const char *app_shellWarning(const app_shell_t *s, int i)
{
	const int n = app_shellWarnings(s);
	if (i < 0 || i >= n)
		return "";
	return s->warnings[(s->warningsTotal - (unsigned)n + (unsigned)i) % APP_SHELL_WARNINGS];
}

void app_shellTestFault(app_shell_t *s)
{
	atomic_store(&s->testFault, 1);
}

void app_shellTestStall(app_shell_t *s, int ms)
{
	atomic_store(&s->testStallMs, ms);
}

int app_shellStop(app_shell_t *s)
{
	int rc = 0;
	app_shellInputClose(s);
	if (s->haveFirmware)
	{
		app_cmd_t c;
		memset(&c, 0, sizeof c);
		c.kind = APP_CMD_STOP;
		for (int i = 0; i < 200 && app_engineCommand(&s->engine, &c) != 0; i++)
		{
			const struct timespec d = { 0, 5000000L };	/* a full command ring: the thread drains it */
			(void)nanosleep(&d, NULL);
		}
		/* the thread takes the command between slices, and a slice can be in a wait of the engine: the first callback of
		 * an output, 3 s, and an open, as long as the longest so far; then the close and the command itself */
		const double deadline = app_wallSeconds() + APP_FIRST_SEC + atomic_load(&s->openSec) + APP_SHELL_STOP_SEC;
		while (!atomic_load(&s->stopped) && app_wallSeconds() < deadline)
		{
			const struct timespec d = { 0, 5000000L };
			(void)nanosleep(&d, NULL);
		}
		if (atomic_load(&s->stopped))	/* else a stuck thread: the process ends without it */
		{
			(void)pthread_join(s->firmware, NULL);
			s->haveFirmware = 0;
			if (s->haveWatchdog)
			{
				(void)pthread_join(s->watchdog, NULL);	/* it looks at stopped every 100 ms */
				s->haveWatchdog = 0;
			}
		}
		else
			rc = -1;
	}
	app_shellMidiStop(s);
	if (s->setDirty)
		app_shellSave(s);	/* a change of the last half second */
	return rc;
}
