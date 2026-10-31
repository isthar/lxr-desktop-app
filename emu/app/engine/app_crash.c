/*
 * app_crash.c
 *
 * The crash note (stage 1b spec 6.9, 11, plan E20): the signal handler, the alternate signal stacks, the note and the
 * search for the newest one. Only write(), open() and close() run in the handler; the time is CLOCK_REALTIME plus the
 * UTC offset that the main thread stored.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_crash.h"

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_time.h"

static char app_crashDir[1024];
static uint64_t (*app_crashNow)(void);
static void (*app_crashTrail)(int fd, unsigned n);
/* one note per process; a signal handler may run on two threads at once, so it is taken with one exchange. Lock-free
 * atomics are async-signal-safe */
static atomic_int app_crashDone;
static atomic_uintptr_t app_crashFirmware;	/* the firmware thread's pthread_t, 0 until it is set */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_POINTER_LOCK_FREE == 2, "the crash note needs lock-free atomics");

static size_t app_add(char *b, size_t k, size_t n, const char *s)
{
	while (*s && k + 1u < n)
		b[k++] = *s++;
	return k;
}

static size_t app_addU64(char *b, size_t k, size_t n, uint64_t v)
{
	char d[20];
	unsigned m = 0;
	do
		d[m++] = (char)('0' + v % 10u);
	while ((v /= 10u) != 0u);
	while (m && k + 1u < n)
		b[k++] = d[--m];
	return k;
}

void app_crashInit(const char *dir, uint64_t (*now)(void), void (*trail)(int fd, unsigned n))
{
	snprintf(app_crashDir, sizeof app_crashDir, "%s", dir);
	app_crashNow = now;
	app_crashTrail = trail;
}

int app_crashWrite(const char *what)
{
	if (!app_crashDir[0] || atomic_exchange(&app_crashDone, 1) != 0)
		return -1;	/* a model check writes, then aborts: one note */
	char name[40], path[1100], text[512];
	if (!app_stampName(app_localSeconds(), "crash-", ".txt", name, sizeof name))
		return -1;
	size_t k = app_add(path, 0u, sizeof path, app_crashDir);
	k = app_add(path, k, sizeof path, "/");
	path[app_add(path, k, sizeof path, name)] = '\0';
	const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	k = app_add(text, 0u, sizeof text, "LICHOMAT Emu crash: ");
	k = app_add(text, k, sizeof text, what);
	k = app_add(text, k, sizeof text, "\nvirtual time: ");
	k = app_addU64(text, k, sizeof text, app_crashNow ? app_crashNow() : 0u);
	const uintptr_t fw = atomic_load(&app_crashFirmware);	/* pthread_self() is async-signal-safe */
	k = app_add(text, k, sizeof text, fw != 0u && fw == (uintptr_t)pthread_self() ? " cycles\nthread: firmware"
		: " cycles\nthread: other");
	k = app_add(text, k, sizeof text, "\nthe last 64 scheduler events and inputs, oldest first:\n");
	(void)!write(fd, text, k);
	if (app_crashTrail)
		app_crashTrail(fd, 64u);
	close(fd);
	return 0;
}

static void app_crashOnSignal(int sig)
{
	const char *name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGFPE ? "SIGFPE"
		: sig == SIGILL ? "SIGILL" : sig == SIGABRT ? "SIGABRT" : "SIGTRAP";
	(void)app_crashWrite(name);
	/* macOS ignores SA_RESETHAND for SIGILL and SIGTRAP, so the handler would catch its own raise() for ever: the default
	 * action is put back by hand, and it ends the app when the handler returns */
	struct sigaction dfl;
	memset(&dfl, 0, sizeof dfl);
	dfl.sa_handler = SIG_DFL;
	sigemptyset(&dfl.sa_mask);
	(void)sigaction(sig, &dfl, NULL);
	raise(sig);
}

void app_crashThread(void)
{
	stack_t ss;
	memset(&ss, 0, sizeof ss);
	ss.ss_size = 4u * SIGSTKSZ;
	ss.ss_sp = malloc(ss.ss_size);	/* the thread's, for its life */
	if (ss.ss_sp)
		(void)sigaltstack(&ss, NULL);
}

void app_crashSetFirmware(void)
{
	atomic_store(&app_crashFirmware, (uintptr_t)pthread_self());
}

void app_crashSignals(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = app_crashOnSignal;
	sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	static const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP };
	for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
		(void)sigaction(sigs[i], &sa, NULL);
}

int app_crashLatest(const char *dir, const char *shown, char *name, size_t n)
{
	DIR *d = opendir(dir);
	char best[64] = "";
	for (struct dirent *e; d && (e = readdir(d)) != NULL;)
		if (strlen(e->d_name) == 25u && !strncmp(e->d_name, "crash-", 6) && !strcmp(e->d_name + 21, ".txt")
			&& strcmp(e->d_name, best) > 0)
			snprintf(best, sizeof best, "%s", e->d_name);
	if (d)
		closedir(d);
	if (!best[0] || strcmp(best, shown) <= 0)
		return 0;
	snprintf(name, n, "%s", best);
	return 1;
}
