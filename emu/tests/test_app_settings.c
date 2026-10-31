/*
 * test_app_settings.c
 *
 * R4 (stage 1b spec 6.9, plan E19, E20): the settings file, with the keys it does not know, and the crash note
 * that a fault signal leaves.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app_crash.h"
#include "app_settings.h"
#include "app_time.h"
#include "emu_test.h"

TEST(R4_settings_roundtrip)
{
	char dir[512], path[1024];
	emu_testTempDir(dir, sizeof dir);
	snprintf(path, sizeof path, "%s/settings.txt", dir);
	app_settings_t s, t;
	app_settingsDefaults(&s);
	ASSERT_EQ(app_settingsLoad(&s, path), 1);	/* the first run */
	ASSERT_EQ(s.dAuto, 1);
	ASSERT_EQ(strcmp(s.mix, "sum"), 0);
	snprintf(s.dinSrcName, sizeof s.dinSrcName, "USB MIDI Interface Port 1");
	s.dinSrcUid = -1234567;
	s.usbSrcUid = 0x4C450001;
	s.dAuto = 0;
	s.dMs = 12.5;
	snprintf(s.mix, sizeof s.mix, "out2");
	snprintf(s.crashShown, sizeof s.crashShown, "crash-20261031-130509.txt");
	ASSERT_EQ(app_settingsSave(&s, path), 0);
	app_settingsDefaults(&t);
	ASSERT_EQ(app_settingsLoad(&t, path), 0);
	ASSERT_EQ(strcmp(t.dinSrcName, s.dinSrcName), 0);
	ASSERT_EQ(t.dinSrcUid, -1234567);
	ASSERT_EQ(t.usbSrcUid, 0x4C450001);
	ASSERT_EQ(t.dAuto, 0);
	ASSERT_EQ_DBL(t.dMs, 12.5);
	ASSERT_EQ(strcmp(t.mix, "out2"), 0);
	ASSERT_EQ(strcmp(t.crashShown, s.crashShown), 0);
	ASSERT_EQ(t.window[2], s.window[2]);

	/* strings are written escaped, so that no name can break the file: a line feed, a backslash, a tab, a carriage return,
	 * and the spaces at the ends, which the reader would trim (review of d6fbcd7) */
	snprintf(s.dinSrcName, sizeof s.dinSrcName, "  Port\n2 \\ C:\\x\ty\r  ");
	snprintf(s.dinDstName, sizeof s.dinDstName, " ");
	snprintf(s.outputUid, sizeof s.outputUid, "\\n is a backslash and an n, not a line feed\n");
	snprintf(s.crashShown, sizeof s.crashShown, "x\ny = 1\nwindow.x = 5");	/* a name that holds a whole line of the file */
	s.window[0] = 321;
	ASSERT_EQ(app_settingsSave(&s, path), 0);
	app_settingsDefaults(&t);
	ASSERT_EQ(app_settingsLoad(&t, path), 0);
	ASSERT_EQ(strcmp(t.dinSrcName, s.dinSrcName), 0);
	ASSERT_EQ(strcmp(t.dinDstName, " "), 0);
	ASSERT_EQ(strcmp(t.outputUid, s.outputUid), 0);
	ASSERT_EQ(strcmp(t.crashShown, s.crashShown), 0);
	ASSERT_EQ(t.window[0], 321);	/* the "line" in the name did not become one */
	ASSERT_EQ(t.extra[0], 0);
	FILE *f = fopen(path, "r");	/* the file holds one line per key */
	ASSERT_TRUE(f != NULL);
	unsigned lines = 0;
	for (int c; (c = fgetc(f)) != EOF;)
		lines += c == '\n';
	fclose(f);
	ASSERT_EQ(lines, 1u + 26u);	/* the comment and the 26 keys: the 18 of R4 and the 8 of R5 (F12) */

	/* an over-long line ends at its line feed: its rest is not the next line */
	f = fopen(path, "w");
	ASSERT_TRUE(f != NULL);
	fputs("output.mix = four\n# ", f);
	for (unsigned i = 0; i < 1021u; i++)
		fputc('x', f);
	fputs("window.x = 777\nunknown.long = ", f);
	for (unsigned i = 0; i < 3000u; i++)
		fputc('y', f);
	fputs("\nwindow.y = 55\nunknown.key = kept\nslave.delay.ms = 7", f);	/* the last line has no line feed */
	fclose(f);
	app_settingsDefaults(&t);
	ASSERT_EQ(app_settingsLoad(&t, path), 0);
	ASSERT_EQ(strcmp(t.mix, "four"), 0);
	ASSERT_EQ(t.window[0], 100);	/* the second chunk of the comment line was not read as a setting */
	ASSERT_EQ(t.window[1], 55);		/* the line after the long ones is read */
	ASSERT_EQ_DBL(t.dMs, 7.0);
	ASSERT_TRUE(strstr(t.extra, "unknown.key = kept\n") != NULL);
	ASSERT_TRUE(strstr(t.extra, "unknown.long") == NULL);	/* the long one is dropped whole, not cut */
}

TEST(R4_settings_keep)
{
	char dir[512], path[1024];
	emu_testTempDir(dir, sizeof dir);
	const char *text = "# a comment\noutput.mix = four\nfuture.option = 1\noutput.buffer = banana\nclockin.ppq = 7\ndin.source.uid = 4294967296\nusb.source.uid = -2147483648\nusb.destination.uid = -5\nwindow.width = -300\n";
	emu_testWrite(dir, "settings.txt", text, strlen(text));
	snprintf(path, sizeof path, "%s/settings.txt", dir);
	app_settings_t s;
	app_settingsDefaults(&s);
	ASSERT_EQ(app_settingsLoad(&s, path), 0);
	ASSERT_EQ(strcmp(s.mix, "four"), 0);
	ASSERT_EQ(s.bufferFrames, 0);	/* a bad value keeps its default */
	ASSERT_EQ(s.genPpq, 24);		/* only 4 or 24 */
	ASSERT_EQ(s.dinSrcUid, 0);		/* out of range for an int32: kept, and no conversion of it (UBSan) */
	ASSERT_EQ(s.usbSrcUid, INT32_MIN);	/* the extreme that fits */
	ASSERT_EQ(s.usbDstUid, -5);		/* a negative ID is a valid one */
	ASSERT_EQ(s.window[2], 1100);	/* below its range: kept */
	ASSERT_TRUE(strstr(s.extra, "future.option = 1\n") != NULL);	/* a key of a later version (E19, R5 F12) */
	ASSERT_EQ(app_settingsSave(&s, path), 0);
	app_settingsDefaults(&s);
	ASSERT_EQ(app_settingsLoad(&s, path), 0);
	ASSERT_TRUE(strstr(s.extra, "future.option = 1\n") != NULL);
}

TEST(R5_settings_clock)
{
	char dir[512], path[1024];
	emu_testTempDir(dir, sizeof dir);
	snprintf(path, sizeof path, "%s/settings.txt", dir);
	app_settings_t s, t;
	app_settingsDefaults(&s);
	ASSERT_EQ(s.clockOutAudio + s.clockInAudio, 0);	/* both off by default (spec 6.8) */
	ASSERT_EQ(s.clockOutChannel, 2);
	ASSERT_EQ(s.clockInChannel, 1);
	ASSERT_EQ(s.resetChannel, 0);
	ASSERT_EQ_DBL(s.clockInThreshold, 0.5);
	ASSERT_EQ_DBL(s.resetThreshold, 0.5);
	const char *text = "clockout.audio = 1\nclockout.channel = 4\nclockin.audio = 1\nclockin.device = Interface:x\\s\n"
		"clockin.channel = 65\nclockin.threshold = 0.3\nreset.channel = 2\nreset.threshold = 0.99\n";
	emu_testWrite(dir, "settings.txt", text, strlen(text));
	ASSERT_EQ(app_settingsLoad(&s, path), 0);
	ASSERT_EQ(s.clockOutAudio, 1);
	ASSERT_EQ(s.clockOutChannel, 4);
	ASSERT_EQ(s.clockInAudio, 1);
	ASSERT_EQ(strcmp(s.clockInDevice, "Interface:x "), 0);
	ASSERT_EQ(s.clockInChannel, 1);			/* 65: out of range, the default stays */
	ASSERT_EQ_DBL(s.clockInThreshold, 0.3);
	ASSERT_EQ(s.resetChannel, 2);
	ASSERT_EQ_DBL(s.resetThreshold, 0.5);	/* above 95 % */
	ASSERT_TRUE(strstr(s.extra, "clock") == NULL);
	ASSERT_EQ(app_settingsSave(&s, path), 0);
	app_settingsDefaults(&t);
	ASSERT_EQ(app_settingsLoad(&t, path), 0);
	ASSERT_EQ(t.clockOutChannel, 4);
	ASSERT_EQ(strcmp(t.clockInDevice, s.clockInDevice), 0);
	ASSERT_EQ_DBL(t.clockInThreshold, 0.3);
}

static uint64_t noteNow(void) { return 123456789u; }
static void noteTrail(int fd, unsigned n)
{
	char b[32];
	const int k = snprintf(b, sizeof b, "trail %u\n", n);
	(void)!write(fd, b, (size_t)k);
}

TEST(R4_crash_note)
{
	char dir[512], name[64], path[1100], text[512];
	emu_testTempDir(dir, sizeof dir);
	app_timeZoneRefresh();
	const pid_t pid = fork();
	if (pid == 0)
	{
		app_crashInit(dir, noteNow, noteTrail);
		app_crashThread();
		app_crashSignals();
		raise(SIGSEGV);	/* a fault signal on this thread */
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);	/* the app then ends (spec 6.9) */
	ASSERT_EQ(app_crashLatest(dir, "", name, sizeof name), 1);
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "r");
	ASSERT_TRUE(f != NULL);
	const size_t n = fread(text, 1, sizeof text - 1u, f);
	fclose(f);
	text[n] = '\0';
	ASSERT_TRUE(strstr(text, "LICHOMAT Emu crash: SIGSEGV\nvirtual time: 123456789 cycles\n") != NULL);
	ASSERT_TRUE(strstr(text, "trail 64\n") != NULL);
	ASSERT_EQ(app_crashLatest(dir, name, path, sizeof path), 0);	/* shown once */
}

/* a child that sets the crash note up as the shell does, then runs what: the parent reads how it ended and the note.
 * alarm(): a handler that does not end the child must fail the test, not hang it (SIGALRM, never the signal asked for) */
static void *crashRaiseOther(void *arg)
{
	app_crashThread();
	raise((int)(intptr_t)arg);
	return NULL;
}

static int crashChild(const char *dir, int sig, int onOtherThread, char *text, size_t n)
{
	char name[64], path[1100];
	const pid_t pid = fork();
	if (pid == 0)
	{
		alarm(5);
		app_crashInit(dir, noteNow, noteTrail);
		app_crashThread();
		app_crashSignals();
		app_crashSetFirmware();	/* this thread is the firmware's */
		if (onOtherThread)
		{
			pthread_t t;
			pthread_create(&t, NULL, crashRaiseOther, (void *)(intptr_t)sig);
			pthread_join(t, NULL);
		}
		else
			raise(sig);
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	text[0] = '\0';
	if (!WIFSIGNALED(status) || WTERMSIG(status) != sig)
		return -1;
	if (app_crashLatest(dir, "", name, sizeof name) != 1)
		return -2;
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "r");
	if (f == NULL)
		return -3;
	const size_t k = fread(text, 1, n - 1u, f);
	fclose(f);
	text[k] = '\0';
	return 0;
}

/* macOS ignores SA_RESETHAND for SIGILL and SIGTRAP: the handler has to reset the disposition itself, or its raise()
 * enters it again, forever (review of d6fbcd7) */
TEST(R4_crash_trap)
{
	static const struct { int sig; const char *name; } cases[] = { { SIGTRAP, "SIGTRAP" }, { SIGILL, "SIGILL" } };
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
	{
		char dir[512], text[512], want[64];
		emu_testTempDir(dir, sizeof dir);
		ASSERT_EQ(crashChild(dir, cases[i].sig, 0, text, sizeof text), 0);	/* ended with the signal, left a note */
		snprintf(want, sizeof want, "LICHOMAT Emu crash: %s\nvirtual time: ", cases[i].name);
		ASSERT_TRUE(strstr(text, want) != NULL);
	}
}

/* the note says which thread faulted; a fault elsewhere is not the firmware's (review of d6fbcd7) */
TEST(R4_crash_thread)
{
	char dir[512], text[512];
	emu_testTempDir(dir, sizeof dir);
	ASSERT_EQ(crashChild(dir, SIGSEGV, 0, text, sizeof text), 0);
	ASSERT_TRUE(strstr(text, "\nthread: firmware\n") != NULL);
	emu_testTempDir(dir, sizeof dir);
	ASSERT_EQ(crashChild(dir, SIGSEGV, 1, text, sizeof text), 0);
	ASSERT_TRUE(strstr(text, "\nthread: other\n") != NULL);
	ASSERT_TRUE(strstr(text, "thread: firmware") == NULL);
}

/* a model check writes its note, then aborts: a second note (a second fault, the abort's own SIGABRT) is refused */
TEST(R4_crash_once)
{
	char dir[512];
	emu_testTempDir(dir, sizeof dir);
	const pid_t pid = fork();
	if (pid == 0)
	{
		alarm(5);
		app_crashInit(dir, noteNow, noteTrail);
		app_crashSignals();
		const int first = app_crashWrite("model check");
		const int second = app_crashWrite("model check again");
		_exit(first == 0 && second == -1 ? 0 : 1);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

TEST_MAIN()
