/*
 * plug_spawn.h
 *
 * Starting lichomat-engine (plan E9.2 Tasks 6 and 8), for the plugin and for plug_drive.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_SPAWN_H_
#define PLUG_SPAWN_H_

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start engine --card card with wakeRead as fd 3, lifeWrite as fd 4 and shmFd as fd 5, stdin
 * from /dev/null, stdout and stderr appended to log (NULL: the caller's), no other descriptor,
 * every signal at its default and none blocked: the caller may be a host's thread with its own
 * mask. Returns the pid, or -1 with the problem in err. */
pid_t plug_spawnEngine(const char *engine, const char *card, int wakeRead, int lifeWrite, int shmFd, const char *log,
	char *err, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_SPAWN_H_ */
