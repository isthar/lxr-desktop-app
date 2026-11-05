/*
 * plugin_link.cpp
 *
 * The plugin's side of its engine process (plan E9.2 Task 8); see plugin_link.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plugin_link.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <vector>

#include "plug_spawn.h"

namespace lichomat {

static double wallNow ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

std::string EngineLink::enginePath ()
{
	Dl_info info;
	if (!dladdr ((const void*)&EngineLink::enginePath, &info) || !info.dli_fname)
		return {};
	std::string p = info.dli_fname;	/* .../LICHOMAT.vst3/Contents/MacOS/LICHOMAT */
	const size_t slash = p.rfind ('/');
	if (slash == std::string::npos)
		return {};
	return p.substr (0, slash) + "/../Helpers/lichomat-engine";
}

void EngineLink::note (const char* fmt, ...)
{
	std::string path;
	{
		std::lock_guard<std::mutex> g (paths);
		path = pluginLog;
	}
	FILE* f = path.empty () ? nullptr : fopen (path.c_str (), "a");
	if (!f)
		return;
	struct timespec ts;
	clock_gettime (CLOCK_REALTIME, &ts);
	struct tm tm;
	localtime_r (&ts.tv_sec, &tm);
	char when[32];
	strftime (when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
	fprintf (f, "%s.%03ld ", when, ts.tv_nsec / 1000000L);
	va_list ap;
	va_start (ap, fmt);
	vfprintf (f, fmt, ap);
	va_end (ap);
	fputc ('\n', f);
	fclose (f);
}

bool EngineLink::start (double rate, uint32_t maxBlock, bool realtime, const std::string& cardDir,
                        const std::string& eLog, const std::string& pLog, std::string& err)
{
	stop ();
	char e[256];
	mem = plug_shmCreate (rate, maxBlock, &memFd, e, sizeof e);
	if (!mem)
	{
		err = e;
		return false;
	}
	mem->realtime = realtime ? 1u : 0u;
	if (pipe (wakeFd) != 0 || fcntl (wakeFd[0], F_SETFD, FD_CLOEXEC) != 0 || fcntl (wakeFd[1], F_SETFD, FD_CLOEXEC) != 0
	    || fcntl (wakeFd[1], F_SETFL, O_NONBLOCK) != 0)
	{
		err = std::string ("pipe: ") + strerror (errno);
		stop ();
		return false;
	}
	{
		std::lock_guard<std::mutex> g (paths);
		card = cardDir;
		engineLog = eLog;
		pluginLog = pLog;
		engine = enginePath ();
	}
	stopping = false;
	restartDone = restartAsked.load ();
	thread = std::thread ([this] { monitor (); });
	return true;
}

void EngineLink::restart (const std::string& cardDir, const std::string& eLog, const std::string& pLog)
{
	{
		std::lock_guard<std::mutex> g (paths);
		card = cardDir;
		engineLog = eLog;
		pluginLog = pLog;
	}
	if (!thread.joinable ())
		return;
	const uint64_t n = ++restartAsked;
	const double t0 = wallNow ();
	while (restartDone.load () < n && wallNow () - t0 < 3.0)
		std::this_thread::sleep_for (std::chrono::milliseconds (5));
}

void EngineLink::stop ()
{
	if (thread.joinable ())
	{
		stopping = true;
		if (mem)
			plug_store (&mem->quit, 1);
		wake ();
		thread.join ();
	}
	for (int& fd : wakeFd)
	{
		if (fd >= 0)
			close (fd);
		fd = -1;
	}
	if (mem)
		plug_shmClose (mem);
	mem = nullptr;
	if (memFd >= 0)
		close (memFd);
	memFd = -1;
}

/* the monitor thread: one engine after the other until stop() */
void EngineLink::monitor ()
{
	std::vector<double> deaths;
	/* No engine, for good: failed until stop(), or until restart() asks for another card. The
	 * thread stays, so that restart() is answered at once and starts an engine on its card. */
	auto failed = [&] (uint64_t asked) {
		plug_store (&mem->state, PLUG_ENGINE_FAILED);
		restartDone = asked;
		while (!stopping && restartAsked.load () == asked)
			std::this_thread::sleep_for (std::chrono::milliseconds (50));
		deaths.clear ();
	};
	while (!stopping)
	{
		const uint64_t asked = restartAsked.load ();	/* before the paths, which restart() sets first */
		int life[2];
		if (pipe (life) != 0)
		{
			note ("pipe: %s", strerror (errno));
			failed (asked);
			continue;
		}
		fcntl (life[0], F_SETFD, FD_CLOEXEC);
		fcntl (life[1], F_SETFD, FD_CLOEXEC);
		std::string c, log, exe;
		{
			std::lock_guard<std::mutex> g (paths);
			c = card;
			log = engineLog;
			exe = engine;
		}
		plug_store (&mem->quit, 0);
		plug_store (&mem->state, PLUG_ENGINE_NONE);
		char err[256];
		/* no log (BASE/logs could not be made): the engine's output goes nowhere rather than stop it */
		const pid_t pid = plug_spawnEngine (exe.c_str (), c.c_str (), wakeFd[0], life[1], memFd,
		                                    log.empty () ? "/dev/null" : log.c_str (), err, sizeof err);
		close (life[1]);
		if (pid < 0)
		{
			note ("cannot start the engine: %s", err);
			close (life[0]);
			failed (asked);
			continue;
		}
		enginePid = pid;
		restartDone = asked;
		note ("engine %d started on %s", (int)pid, c.c_str ());
		/* watch it */
		const double started = wallNow ();
		double lastMove = started;
		uint64_t lastFrontier = plug_load (&mem->frontier);
		const char* why = "it died";
		for (;;)
		{
			struct pollfd p = {life[0], POLLIN, 0};
			const int r = poll (&p, 1, 100);
			if (r > 0)
			{
				char b;
				if (read (life[0], &b, 1) <= 0)
				{
					if (stopping)
						why = "the plugin stopped it";	/* it took the quit before this loop saw stopping */
					break;	/* the end of the pipe: the engine is gone */
				}
			}
			if (stopping)
			{
				why = "the plugin stopped it";
				plug_store (&mem->quit, 1);
				wake ();
				struct pollfd q = {life[0], POLLIN, 0};
				if (poll (&q, 1, 2000) <= 0)
					kill (pid, SIGKILL);
				break;
			}
			if (restartAsked.load () != asked)
			{
				why = "a new card";
				kill (pid, SIGKILL);
				continue;	/* until the end of the pipe */
			}
			const double now = wallNow ();
			const uint64_t st = plug_load (&mem->state), frontier = plug_load (&mem->frontier);
			if (frontier != lastFrontier)
			{
				lastFrontier = frontier;
				lastMove = now;
			}
			const bool behind = plug_load (&mem->hostFrames) > frontier + (uint64_t)(mem->rate / 2.0);
			if ((st == PLUG_ENGINE_RUNNING && behind && now - lastMove > 2.0)
			    || (st != PLUG_ENGINE_RUNNING && now - started > 30.0))
			{
				why = st == PLUG_ENGINE_RUNNING ? "it hung (2 s without taking events)" : "it did not start in 30 s";
				kill (pid, SIGKILL);
			}
		}
		close (life[0]);
		int status = 0;
		char how[32] = "status unknown";	/* ECHILD when the host reaps its children itself */
		if (waitpid (pid, &status, 0) == pid)
			snprintf (how, sizeof how, WIFSIGNALED (status) ? "signal %d" : "exit %d",
			          WIFSIGNALED (status) ? WTERMSIG (status) : WEXITSTATUS (status));
		enginePid = -1;
		const bool reported = plug_load (&mem->state) == PLUG_ENGINE_FAILED;
		note ("engine %d ended (%s): %s; underruns %llu, late %llu, dropped %llu, ring full %llu", (int)pid, how, why,
		      (unsigned long long)mem->underruns, (unsigned long long)mem->late, (unsigned long long)mem->dropped,
		      (unsigned long long)mem->evDropped);
		if (stopping)
			break;
		plug_store (&mem->state, PLUG_ENGINE_NONE);	/* the audio thread reads silence */
		if (restartAsked.load () != asked)
			continue;	/* a new card: no death */
		const double now = wallNow ();
		deaths.push_back (now);
		while (!deaths.empty () && now - deaths.front () > 60.0)
			deaths.erase (deaths.begin ());
		if (deaths.size () >= 5 || reported)
		{
			note ("the engine is not started again: %s", reported ? "it reported a failure" : "5 deaths in a minute");
			failed (asked);
			continue;
		}
		for (int i = 0; i < 10 && !stopping && restartAsked.load () == asked; i++)
			std::this_thread::sleep_for (std::chrono::milliseconds (50));
	}
	restartDone = restartAsked.load ();	/* a restart() still waiting returns */
}

} // namespace lichomat
