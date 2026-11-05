/*
 * eng_main.c
 *
 * lichomat-engine, the LICHOMAT plugin's engine process (plan E9.2 Task 6):
 *
 *   lichomat-engine --card DIR              play for the plugin that started it: fd 3 is the read
 *                                           end of its wake pipe, fd 4 the write end of its life
 *                                           pipe, held open and never written, fd 5 the shared
 *                                           memory (plug_shm.h)
 *   lichomat-engine --seed-card DIR         an init card into DIR, missing or empty
 *   lichomat-engine --version
 *
 * With --card, a missing or empty card folder is seeded first, by a process of its own: the
 * firmware powers on once per process (emu_firmware.c:66-74). A thread of its own ends the engine
 * when the plugin's process goes away, whatever the firmware's thread does. Exit codes: 0 the
 * plugin ended it or went away, 1 usage, 2 the card or the shared memory, 3 a firmware fault or a
 * fault signal, 4 the converter.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <errno.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "emu_notices.h"
#include "eng_run.h"
#include "plug_notices.h"

extern char **environ;

#define ENG_WAKE_FD 3
#define ENG_LIFE_FD 4
#define ENG_SHM_FD 5

static plug_shm_t *eng_shm;

/* a fault signal: a line, the state, the exit; write(2) only, on the alternate stack */
static void eng_onSignal(int sig)
{
	static const char msg[] = "lichomat-engine: fault signal\n";
	(void)!write(2, msg, sizeof msg - 1u);
	if (eng_shm)
		__atomic_store_n(&eng_shm->state, (uint64_t)PLUG_ENGINE_FAILED, __ATOMIC_RELEASE);
	(void)sig;
	_exit(3);
}

static void eng_signals(void)
{
	static char altStack[1u << 17];
	stack_t ss;
	memset(&ss, 0, sizeof ss);
	ss.ss_sp = altStack;
	ss.ss_size = sizeof altStack;
	(void)sigaltstack(&ss, NULL);
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = eng_onSignal;
	sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	static const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
	for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
		(void)sigaction(sigs[i], &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
}

/* a sanitizer report is a fault, as in the renderer (render_main.c:209-221) */
const char *__asan_default_options(void) __attribute__((used, visibility("default")));
const char *__asan_default_options(void)
{
	return "abort_on_error=1";
}

const char *__ubsan_default_options(void) __attribute__((used, visibility("default")));
const char *__ubsan_default_options(void)
{
	return "abort_on_error=1";
}

static void eng_version(void)
{
	printf("lichomat-engine %s, the engine of the LICHOMAT plugin\n", PLUG_VERSION);
	printf("%s\n", PLUG_TAGLINE);
	for (size_t i = 0; i < EMU_LEGAL_COUNT; i++)
		printf("%s\n", emu_legal[i]);
}

/* seed the card in a process of our own: --seed-card, with only stdout and stderr */
static int eng_seedChild(const char *card)
{
	char self[4096];
	uint32_t size = sizeof self;
	if (_NSGetExecutablePath(self, &size) != 0)
	{
		fprintf(stderr, "lichomat-engine: the executable's path is too long\n");
		return 2;
	}
	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addinherit_np(&fa, 1);
	posix_spawn_file_actions_addinherit_np(&fa, 2);
	posix_spawnattr_init(&at);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT);
	char *const argv[] = { self, "--seed-card", (char *)card, NULL };
	pid_t pid;
	const int rc = posix_spawn(&pid, self, &fa, &at, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&at);
	if (rc != 0)
	{
		fprintf(stderr, "lichomat-engine: cannot start the seeding: %s\n", strerror(rc));
		return 2;
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			return 2;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 2;
}

/* the process that started the engine: the plugin's host, or plug_drive */
static pid_t eng_parent;

/* The plugin's process gone (a host that crashed or was killed): the engine ends, whatever its main
 * thread does, a firmware that hangs, the prelude or the seeding included. Every 100 ms; in
 * eng_waitFor() the wake pipe's end and the quit flag end it sooner. write(2) and _exit() only. */
static void *eng_watch(void *arg)
{
	(void)arg;
	for (;;)
	{
		nanosleep(&(const struct timespec){ 0, 100000000L }, NULL);
		const pid_t p = getppid();
		if (p != eng_parent || p == 1)
		{
			static const char msg[] = "lichomat-engine: the plugin's process is gone\n";
			(void)!write(2, msg, sizeof msg - 1u);
			_exit(0);
		}
	}
	return NULL;
}

static void eng_watchParent(void)
{
	eng_parent = getppid();
	pthread_attr_t a;
	pthread_t th;
	pthread_attr_init(&a);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&a, 64u * 1024u);
	if (pthread_create(&th, &a, eng_watch, NULL) != 0)
		fprintf(stderr, "lichomat-engine: no thread to watch the plugin's process; the engine ends when it waits for events\n");
	pthread_attr_destroy(&a);
}

static int eng_usage(void)
{
	fprintf(stderr, "usage: lichomat-engine --card DIR | --seed-card DIR | --version\n");
	return 1;
}

int main(int argc, char **argv)
{
	eng_signals();
	if (argc == 2 && !strcmp(argv[1], "--version"))
	{
		eng_version();
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "--seed-card"))
		return eng_seed(argv[2]);
	if (argc != 3 || strcmp(argv[1], "--card"))
		return eng_usage();
	const char *card = argv[2];
	eng_watchParent();
	char err[256];
	eng_shm = plug_shmMap(ENG_SHM_FD, err, sizeof err);
	if (!eng_shm || plug_shmCheck(eng_shm, err, sizeof err) != 0)
	{
		fprintf(stderr, "lichomat-engine: %s\n", err);
		return 2;
	}
	const int empty = eng_cardEmpty(card);
	if (empty < 0 || (empty == 1 && eng_seedChild(card) != 0))
	{
		fprintf(stderr, "lichomat-engine: card %s: %s\n", card, empty < 0 ? "not a folder that can be read" : "seeding failed");
		plug_store(&eng_shm->state, PLUG_ENGINE_FAILED);
		return 2;
	}
	(void)ENG_LIFE_FD;	/* held open by the process, never used */
	eng_run(eng_shm, ENG_WAKE_FD, card);
}
