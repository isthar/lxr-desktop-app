/*
 * plug_spawn.c
 *
 * Starting lichomat-engine (plan E9.2 Tasks 6 and 8); see plug_spawn.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_spawn.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

pid_t plug_spawnEngine(const char *engine, const char *card, int wakeRead, int lifeWrite, int shmFd, const char *log,
	char *err, size_t n)
{
	/* The file actions run in order, so a source that is already 3, 4 or 5 would be overwritten
	 * before it is moved: each goes through a copy at 64 or above first. */
	const int src[3] = { wakeRead, lifeWrite, shmFd };
	int tmp[3] = { -1, -1, -1 };
	for (int i = 0; i < 3; i++)
		if ((tmp[i] = fcntl(src[i], F_DUPFD_CLOEXEC, 64)) < 0)
		{
			snprintf(err, n, "fcntl(F_DUPFD_CLOEXEC): %s", strerror(errno));
			for (int k = 0; k < i; k++)
				close(tmp[k]);
			return -1;
		}
	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	posix_spawn_file_actions_init(&fa);
	posix_spawnattr_init(&at);
	posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
	if (log)
	{
		posix_spawn_file_actions_addopen(&fa, 1, log, O_WRONLY | O_CREAT | O_APPEND, 0644);
		posix_spawn_file_actions_adddup2(&fa, 1, 2);
	}
	else
	{
		posix_spawn_file_actions_addinherit_np(&fa, 1);
		posix_spawn_file_actions_addinherit_np(&fa, 2);
	}
	for (int i = 0; i < 3; i++)
		posix_spawn_file_actions_adddup2(&fa, tmp[i], 3 + i);
	sigset_t none, all;
	sigemptyset(&none);
	sigfillset(&all);
	posix_spawnattr_setsigmask(&at, &none);
	posix_spawnattr_setsigdefault(&at, &all);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
	char *const argv[] = { (char *)engine, "--card", (char *)card, NULL };
	pid_t pid = -1;
	const int rc = posix_spawn(&pid, engine, &fa, &at, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&at);
	for (int i = 0; i < 3; i++)
		close(tmp[i]);
	if (rc != 0)
	{
		snprintf(err, n, "posix_spawn %s: %s", engine, strerror(rc));
		return -1;
	}
	return pid;
}
