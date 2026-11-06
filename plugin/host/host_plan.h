/*
 * host_plan.h
 *
 * What the plugin's two test hosts share (plan E9.7 Task 5): lichomat-plughost (plug_host.cpp,
 * the VST3) and lichomat-auhost (au_host.cpp, the AU) read the same plan (plug_host.cpp's header
 * names its commands), play the same transport, write the same WAV files, pace their blocks the
 * same way and signal the engines the same way, and read the MIDI an instance gave its engine from
 * its event ring the same way (events). A host's own commands go to its hook.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <map>
#include <pthread.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/proc_info.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "plug_clock.h"
#include "plug_params.h"
#include "plug_shm.h"
#include "plug_time.h"

namespace hostplan {

/** the host's name, for its messages */
inline const char* gName = "lichomat-plughost";

struct NoteCmd
{
	uint64_t frame;
	int ch, pitch, vel;
};

struct ParamCmd
{
	uint64_t frame;
	uint32_t id;
	int value;
};

struct TransportCmd
{
	uint64_t frame;
	enum { Play, Stop, Tempo } kind;
	double tempo, ppq;
};

struct Plan
{
	std::string bundle, card, stateIn, stateOut, out, script;
	double rate = 44140.625;
	int block = 128;
	uint64_t frames = 0;
	int instances = 1;
	bool loop = false, rtcheck = false, offline = true, paced = false, bus2 = false, echo = false;
	int channel = 0;
	double loopStart = 0, loopEnd = 0;
	std::vector<NoteCmd> notes;
	std::vector<ParamCmd> params;
	std::vector<TransportCmd> transport;
	std::vector<uint64_t> kills, hangs;
	std::vector<std::pair<uint64_t, int>> renews;
	/* asyncSets: setState from a thread of its own, of FILE or of each of FILE+FILE+... one right after the other */
	std::vector<std::pair<uint64_t, std::string>> setstates, asyncSets;
	struct RawPoint { uint64_t frame; int32_t offset; uint32_t id; int value; };
	std::vector<RawPoint> rawPoints;	/* a point of the block at frame with the sample offset as given, in or out of the block */
	std::string controller;		/* a file for the controller's parameters: infos, values, strings */
	std::string events;			/* the MIDI instance 0 pushed into its engine's ring */
	std::string stateEarly;		/* getState right after setState (FILE.set) and right after setActive (FILE), before any block */
	std::vector<ParamCmd> flushes;	/* changes given in a flush (process() with 0 frames) right before the block from each frame */
	std::vector<uint64_t> emptyFlushes;	/* a flush with no changes right before the block from each frame, after its flush */
	std::vector<uint64_t> noOutputs;	/* the block from each frame with no output bus at all */
	bool nullOut2 = false;		/* Out 2 (inactive) with null sample buffers, as the SDK allows */
	bool nullOut2Left = false;	/* Out 2 with its left channel null, its right one given (filled with 0.5) */
	std::vector<uint64_t> nullOut1;	/* the block from each frame with Out 1's left channel null */
};

[[noreturn]] inline void die (const std::string& what)
{
	fprintf (stderr, "%s: %s\n", gName, what.c_str ());
	exit (1);
}

/** a host's own plan command: true when it took the line (ok false: it could not read it) */
using Extra = std::function<bool (const std::string& cmd, std::istringstream& s, bool& ok)>;

inline Plan readPlan (const char* path, const Extra& extra = nullptr)
{
	std::ifstream f (path);
	if (!f)
		die (std::string ("cannot read ") + path);
	Plan p;
	std::string line;
	while (std::getline (f, line))
	{
		std::istringstream s (line.substr (0, line.find ('#')));
		std::string cmd;
		if (!(s >> cmd))
			continue;
		bool ok = true;
		if (extra && extra (cmd, s, ok)) { /* the host's own command */ }
		else if (cmd == "bundle") ok = !!(s >> p.bundle);
		else if (cmd == "rate") ok = !!(s >> p.rate);
		else if (cmd == "block") ok = !!(s >> p.block);
		else if (cmd == "frames") ok = !!(s >> p.frames);
		else if (cmd == "instances") ok = !!(s >> p.instances);
		else if (cmd == "card") ok = !!(s >> p.card);
		else if (cmd == "state-in") ok = !!(s >> p.stateIn);
		else if (cmd == "state-out") ok = !!(s >> p.stateOut);
		else if (cmd == "out") ok = !!(s >> p.out);
		else if (cmd == "script") ok = !!(s >> p.script);
		else if (cmd == "rtcheck") p.rtcheck = true;
		else if (cmd == "paced") p.paced = true;
		else if (cmd == "bus2") p.bus2 = true;
		else if (cmd == "echo") p.echo = true;
		else if (cmd == "channel") ok = !!(s >> p.channel) && p.channel >= 0 && p.channel < 16;
		else if (cmd == "param")
		{
			ParamCmd c;
			ok = !!(s >> c.frame >> c.id >> c.value) && plug_paramIndex (c.id) >= 0 && c.value >= 0
			     && c.value <= plug_params[plug_paramIndex (c.id)].max;
			p.params.push_back (c);
		}
		else if (cmd == "mode") { std::string m; ok = !!(s >> m) && (m == "realtime" || m == "offline"); p.offline = m == "offline"; }
		else if (cmd == "loop") ok = !!(s >> p.loopStart >> p.loopEnd), p.loop = true;
		else if (cmd == "note") { NoteCmd n; ok = !!(s >> n.frame >> n.ch >> n.pitch >> n.vel); p.notes.push_back (n); }
		else if (cmd == "play") { TransportCmd t {0, TransportCmd::Play, 0, 0}; ok = !!(s >> t.frame >> t.tempo >> t.ppq); p.transport.push_back (t); }
		else if (cmd == "stop") { TransportCmd t {0, TransportCmd::Stop, 0, 0}; ok = !!(s >> t.frame); p.transport.push_back (t); }
		else if (cmd == "tempo") { TransportCmd t {0, TransportCmd::Tempo, 0, 0}; ok = !!(s >> t.frame >> t.tempo); p.transport.push_back (t); }
		else if (cmd == "kill") { uint64_t k; ok = !!(s >> k); p.kills.push_back (k); }
		else if (cmd == "hang") { uint64_t k; ok = !!(s >> k); p.hangs.push_back (k); }
		else if (cmd == "renew") { uint64_t k; int i; ok = !!(s >> k >> i); p.renews.push_back ({k, i}); }
		else if (cmd == "flush")
		{
			ParamCmd c;
			ok = !!(s >> c.frame >> c.id >> c.value) && plug_paramIndex (c.id) >= 0 && c.value >= 0
			     && c.value <= plug_params[plug_paramIndex (c.id)].max;
			p.flushes.push_back (c);
		}
		else if (cmd == "flush-empty") { uint64_t k; ok = !!(s >> k); p.emptyFlushes.push_back (k); }
		else if (cmd == "param-at")
		{
			Plan::RawPoint c;
			long long o = 0;
			ok = !!(s >> c.frame >> o >> c.id >> c.value) && plug_paramIndex (c.id) >= 0 && c.value >= 0
			     && c.value <= plug_params[plug_paramIndex (c.id)].max && o >= INT32_MIN && o <= INT32_MAX;
			c.offset = (int32_t)o;
			p.rawPoints.push_back (c);
		}
		else if (cmd == "controller") ok = !!(s >> p.controller);
		else if (cmd == "events") ok = !!(s >> p.events);
		else if (cmd == "state-early") ok = !!(s >> p.stateEarly);
		else if (cmd == "no-outputs") { uint64_t k; ok = !!(s >> k); p.noOutputs.push_back (k); }
		else if (cmd == "null-out2") p.nullOut2 = true;
		else if (cmd == "null-out2-left") p.nullOut2Left = true;
		else if (cmd == "null-out1") { uint64_t k; ok = !!(s >> k); p.nullOut1.push_back (k); }
		else if (cmd == "setstate-async") { uint64_t k; std::string fn; ok = !!(s >> k >> fn); p.asyncSets.push_back ({k, fn}); }
		else if (cmd == "setstate") { uint64_t k; std::string fn; ok = !!(s >> k >> fn); p.setstates.push_back ({k, fn}); }
		else ok = false;
		if (!ok)
			die ("plan: cannot read the line: " + line);
	}
	if (p.bundle.empty () || p.out.empty () || !p.frames || p.block < 1 || p.instances < 1)
		die ("plan: bundle, out, frames, block and instances are needed");
	for (auto& r : p.renews)
		if (!p.events.empty () && r.second == 0)
			die ("plan: events and a renew of instance 0 (a new ring, frames from 0 again)");
	return p;
}

/* the host's transport, as Live gives it: the position at a block's first frame, wrapped at the
 * loop's end, the tempo of the block's first frame */
struct Timeline
{
	const Plan& p;
	bool playing = false;
	double tempo = 120.0, pos = 0.0;
	size_t next = 0;

	explicit Timeline (const Plan& plan) : p (plan) {}

	/* the context of the block at frame, then the position advanced over frames n */
	plug_transport_t block (uint64_t frame, uint32_t n)
	{
		while (next < p.transport.size () && p.transport[next].frame <= frame)
		{
			const TransportCmd& t = p.transport[next++];
			if (t.kind == TransportCmd::Play) { playing = true; tempo = t.tempo; pos = t.ppq; }
			else if (t.kind == TransportCmd::Stop) playing = false;
			else tempo = t.tempo;
		}
		plug_transport_t t {1, playing ? 1 : 0, tempo, pos, p.loop ? 1 : 0, p.loopStart, p.loopEnd};
		if (playing)
		{
			pos += (double)n * tempo / (60.0 * p.rate);
			if (p.loop && p.loopEnd > p.loopStart && t.ppq < p.loopEnd && pos >= p.loopEnd)
				pos = p.loopStart + std::fmod (pos - p.loopEnd, p.loopEnd - p.loopStart);
		}
		return t;
	}
};

inline std::vector<uint8_t> readFile (const std::string& path)
{
	std::ifstream f (path, std::ios::binary);
	if (!f)
		die ("cannot read " + path);
	return std::vector<uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
}

inline void put16 (FILE* f, unsigned v)
{
	fputc ((int)(v & 255u), f);
	fputc ((int)((v >> 8) & 255u), f);
}

inline void put32 (FILE* f, uint32_t v)
{
	put16 (f, v & 0xFFFFu);
	put16 (f, v >> 16);
}

inline void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double rate)
{
	FILE* f = fopen (path.c_str (), "wb");
	if (!f)
		die ("cannot write " + path);
	const bool native = plug_isNative (rate);
	const uint32_t frames = (uint32_t)l.size (), data = frames * 4u, hz = native ? 44141u : (uint32_t)rate;
	fwrite ("RIFF", 1, 4, f);
	put32 (f, 36u + data);
	fwrite ("WAVEfmt ", 1, 8, f);
	put32 (f, 16);
	put16 (f, 1);
	put16 (f, 2);
	put32 (f, hz);
	put32 (f, hz * 4u);
	put16 (f, 4);
	put16 (f, 16);
	fwrite ("data", 1, 4, f);
	put32 (f, data);
	for (uint32_t i = 0; i < frames; i++)
		for (float x : {l[i], r[i]})
		{
			/* native: floor, lichomat-render's (L1 + L2) >> 1 exactly; else the nearest */
			double v = native ? std::floor ((double)x * 32768.0) : std::floor ((double)x * 32768.0 + 0.5);
			v = v > 32767.0 ? 32767.0 : v < -32768.0 ? -32768.0 : v;
			put16 (f, (uint16_t)(int16_t)v);
		}
	fclose (f);
}

/* paced, the host's thread plays as an audio device's does: a time-constraint thread of the block's
 * period, a quarter of it to compute (app_mach.c:106-117, as the engine's), woken at each block's
 * time with mach_wait_until(); a normal thread's long sleep may end milliseconds late (timer
 * coalescing), and two blocks back to back give the engine no time between them */
inline void paceAsDevice (const Plan& p)
{
	mach_timebase_info_data_t tb;
	mach_timebase_info (&tb);
	thread_time_constraint_policy_data_t pol;
	pol.period = (uint32_t)((double)p.block / p.rate * 1e9 * (double)tb.denom / (double)tb.numer);
	pol.computation = pol.period / 4u;
	pol.constraint = pol.period;
	pol.preemptible = 1;
	if (thread_policy_set (pthread_mach_thread_np (pthread_self ()), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&pol,
	                       THREAD_TIME_CONSTRAINT_POLICY_COUNT) != KERN_SUCCESS)
		fprintf (stderr, "%s: the time-constraint policy was refused; pacing as a normal thread\n", gName);
}

/* mach_absolute_time() at wall time t0 + seconds */
inline uint64_t machAt (uint64_t t0, double seconds)
{
	mach_timebase_info_data_t tb;
	mach_timebase_info (&tb);
	return t0 + (uint64_t)(seconds * 1e9 * (double)tb.denom / (double)tb.numer);
}

/* The descriptors of POSIX shared memory objects in this process: the plugin keeps the one it shares with its
 * engine open, its name unlinked (plug_shmCreate()) */
inline std::set<int> shmDescriptors ()
{
	std::set<int> out;
	const int bytes = proc_pidinfo (getpid (), PROC_PIDLISTFDS, 0, nullptr, 0);
	if (bytes <= 0)
		return out;
	std::vector<proc_fdinfo> fds ((size_t)bytes / sizeof (proc_fdinfo) + 16u);
	const int got = proc_pidinfo (getpid (), PROC_PIDLISTFDS, 0, fds.data (), (int)(fds.size () * sizeof (proc_fdinfo)));
	for (int i = 0; got > 0 && i < got / (int)sizeof (proc_fdinfo); i++)
		if (fds[(size_t)i].proc_fdtype == PROX_FDTYPE_PSHM)
			out.insert (fds[(size_t)i].proc_fd);
	return out;
}

/* the events an instance pushed into its engine's ring: a mapping of its shared memory of our own (read only) */
struct RingTap
{
	const plug_shm_t* s = nullptr;
	uint64_t next = 0;	/* the next event to read */
	std::map<uint64_t, std::vector<std::string>> lines;

	/* the object among the descriptors that were not there before: the instance's */
	void find (const std::set<int>& before)
	{
		for (int fd : shmDescriptors ())
		{
			struct stat st;
			if (before.count (fd) || fstat (fd, &st) != 0 || st.st_size < (off_t)sizeof (plug_shm_t))
				continue;
			void* m = mmap (nullptr, sizeof (plug_shm_t), PROT_READ, MAP_SHARED, fd, 0);
			if (m == MAP_FAILED)
				continue;
			const plug_shm_t* x = (const plug_shm_t*)m;
			if (__atomic_load_n (&x->magic, __ATOMIC_ACQUIRE) == PLUG_SHM_MAGIC && x->version == PLUG_SHM_VERSION
			    && x->size == sizeof (plug_shm_t))
			{
				s = x;
				next = plug_load (&x->evWrite);
				return;
			}
			munmap (m, sizeof (plug_shm_t));
		}
		die ("events: no shared memory of the plugin among this process's descriptors");
	}

	/* the events written since the last read: the plugin writes them in process() on this thread, and the engine
	 * only takes them (evRead), so the slots stay as written until the next process() */
	void read ()
	{
		const uint64_t w = plug_load (&s->evWrite);
		for (; next < w; next++)
		{
			const plug_event_t& e = s->ev[next & (PLUG_EVENT_SLOTS - 1u)];
			char b[16];
			snprintf (b, sizeof b, e.len == 3 ? "%02X %02X %02X" : e.len == 2 ? "%02X %02X" : "%02X", e.b[0], e.b[1], e.b[2]);
			lines[e.frame].push_back (b);
		}
	}
};

/* sig to every child of this process: the engines */
inline void signalEngines (int sig)
{
	pid_t kids[64];
	memset (kids, 0, sizeof kids);
	(void)proc_listchildpids (getpid (), kids, sizeof kids);
	for (pid_t k : kids)
		if (k > 0)
			kill (k, sig);
}

} // namespace hostplan
