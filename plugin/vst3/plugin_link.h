/*
 * plugin_link.h
 *
 * The plugin's side of its engine process (plan E9.2 Task 8): the shared memory, the wake pipe,
 * and a monitor thread that starts lichomat-engine, sees it die (the end of its life pipe) or
 * stall, starts it again after 0.5 s, and gives up (failed) after 5 deaths in a minute or 3
 * stalls in 10 minutes. A stall is an engine killed because it hung (2 s without taking the
 * events or giving audio while the plugin is ahead by 0.5 s or waits for it offline), did not
 * start in 30 s, or did not answer one of the processor's offline waits in 10 s. While no engine
 * runs the audio thread reads silence: the memory stays mapped from start() to stop().
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include "plug_shm.h"

namespace lichomat {

class EngineLink
{
public:
	~EngineLink () { stop (); }

	/* the host's main thread */

	/** map the memory and start the monitor, which starts the engine on card; realtime: the host
	 * plays in real time; false with err */
	bool start (double rate, uint32_t maxBlock, bool realtime, const std::string& card, const std::string& engineLog,
	            const std::string& pluginLog, std::string& err);
	/** the engine starts again on another card, also after a failure; returns once a new engine is
	 * started on it, or could not be (at most 3 s) */
	void restart (const std::string& card, const std::string& engineLog, const std::string& pluginLog);
	/** end the engine (quit, then SIGKILL after 2 s), the monitor, the memory */
	void stop ();

	/* the audio thread: no allocation, no lock, one non-blocking write */

	plug_shm_t* shm () const { return mem; }
	void wake () const
	{
		if (wakeFd[1] >= 0)
			(void)!write (wakeFd[1], "w", 1);	/* O_NONBLOCK: a full pipe is a wake-up already pending */
	}

	/** the running engine's pid, -1 when none */
	pid_t pid () const { return enginePid.load (); }

	/* the processor offline (it may wait) */

	/** the processor waits for the engine, or no longer (the monitor's hang rule) */
	void waiting (bool on) { offlineWaiting.store (on); }
	/** the processor waited seconds (10 s or a little more) in vain for engine pid: the monitor ends
	 * it, a stall, and logs the wait */
	void stalled (pid_t pid, double seconds)
	{
		stalledFor.store (seconds);
		stalledPid.store (pid);
	}

	/** lichomat-engine next to the plugin's binary: Contents/Helpers/ of the bundle */
	static std::string enginePath ();

private:
	void monitor ();
	void note (const char* fmt, ...) __attribute__ ((format (printf, 2, 3)));

	plug_shm_t* mem = nullptr;
	int memFd = -1;
	int wakeFd[2] = {-1, -1};
	std::thread thread;
	std::atomic<bool> stopping {false};
	std::atomic<uint64_t> restartAsked {0};	/**< restart() calls; the monitor answers each in restartDone */
	std::atomic<uint64_t> restartDone {0};	/**< the last call answered: an engine started on its card, or none */
	std::atomic<pid_t> enginePid {-1};
	std::atomic<bool> offlineWaiting {false};
	std::atomic<pid_t> stalledPid {-1};
	std::atomic<double> stalledFor {0.0};
	std::mutex paths;	/* the monitor thread and restart(); never the audio thread */
	std::string card, engineLog, pluginLog, engine;
};

} // namespace lichomat
