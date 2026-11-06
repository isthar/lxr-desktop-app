/*
 * plug_host.cpp
 *
 * lichomat-plughost, the plugin's test host (plan E9.2 Task 9), on the VST 3 SDK's hosting
 * classes: it loads a bundle, plays a timeline of transport and notes through one or more
 * instances in blocks, and writes each instance's output as a 16-bit stereo WAV. With script, it
 * also writes the MIDI the plugin must have given its engine, as lichomat-render's usb lines in
 * frames from the engine's epoch: the transport through the same plug_clockBlock(), the notes as
 * the processor converts them. Before frame 0 it calls process() with 0 frames every 10 ms until
 * every instance reports its engine running, so that the epoch is frame 0.
 *
 *   lichomat-plughost PLAN
 *
 * PLAN has one command a line ('#' starts a comment); frames are host frames:
 *   bundle PATH            rate R            block B            frames N         instances K
 *   card DIR               state-in FILE     state-out FILE     out FILE.wav     script FILE
 *   play FRAME TEMPO PPQ   stop FRAME        tempo FRAME TEMPO  loop START END (quarter notes)
 *   note FRAME CH PITCH VELOCITY (0-127; 0 is a Note Off)
 *   kill FRAME             SIGKILL every engine of this process
 *   hang FRAME             SIGSTOP every engine of this process (a firmware that hangs)
 *   renew FRAME I          terminate instance I and put a new one in its place
 *   setstate FRAME FILE    give instance 0 the state FILE while it plays (a preset, a paste, an undo)
 *   rtcheck                count the calls of process() that the interposer (plug_rtcheck) sees
 *   mode realtime|offline  the ProcessSetup's mode (default offline: the plugin waits for its engine)
 *   paced                  each block at its wall-clock time, as an audio device gives it: the
 *                          thread takes the time-constraint policy of the block's period
 *   bus2                   activate Out 2 (plan E9.4): each instance writes it as FILE-out2.wav
 * Instance I > 0 writes FILE-I.wav (and FILE-I-out2.wav). The summary on stdout gives each
 * instance's latency and the Engine parameter it last reported. Before each process() the output
 * buffers are filled with 0.5, as a host may leave anything in them.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "plug_archive.h"
#include "plug_clock.h"
#include "plug_time.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

struct NoteCmd
{
	uint64_t frame;
	int ch, pitch, vel;
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
	bool loop = false, rtcheck = false, offline = true, paced = false, bus2 = false;
	double loopStart = 0, loopEnd = 0;
	std::vector<NoteCmd> notes;
	std::vector<TransportCmd> transport;
	std::vector<uint64_t> kills, hangs;
	std::vector<std::pair<uint64_t, int>> renews;
	std::vector<std::pair<uint64_t, std::string>> setstates;
};

[[noreturn]] void die (const std::string& what)
{
	fprintf (stderr, "lichomat-plughost: %s\n", what.c_str ());
	exit (1);
}

Plan readPlan (const char* path)
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
		if (cmd == "bundle") ok = !!(s >> p.bundle);
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
		else if (cmd == "mode") { std::string m; ok = !!(s >> m) && (m == "realtime" || m == "offline"); p.offline = m == "offline"; }
		else if (cmd == "loop") ok = !!(s >> p.loopStart >> p.loopEnd), p.loop = true;
		else if (cmd == "note") { NoteCmd n; ok = !!(s >> n.frame >> n.ch >> n.pitch >> n.vel); p.notes.push_back (n); }
		else if (cmd == "play") { TransportCmd t {0, TransportCmd::Play, 0, 0}; ok = !!(s >> t.frame >> t.tempo >> t.ppq); p.transport.push_back (t); }
		else if (cmd == "stop") { TransportCmd t {0, TransportCmd::Stop, 0, 0}; ok = !!(s >> t.frame); p.transport.push_back (t); }
		else if (cmd == "tempo") { TransportCmd t {0, TransportCmd::Tempo, 0, 0}; ok = !!(s >> t.frame >> t.tempo); p.transport.push_back (t); }
		else if (cmd == "kill") { uint64_t k; ok = !!(s >> k); p.kills.push_back (k); }
		else if (cmd == "hang") { uint64_t k; ok = !!(s >> k); p.hangs.push_back (k); }
		else if (cmd == "renew") { uint64_t k; int i; ok = !!(s >> k >> i); p.renews.push_back ({k, i}); }
		else if (cmd == "setstate") { uint64_t k; std::string fn; ok = !!(s >> k >> fn); p.setstates.push_back ({k, fn}); }
		else ok = false;
		if (!ok)
			die ("plan: cannot read the line: " + line);
	}
	if (p.bundle.empty () || p.out.empty () || !p.frames || p.block < 1 || p.instances < 1)
		die ("plan: bundle, out, frames, block and instances are needed");
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

struct Instance
{
	IPtr<PlugProvider> provider;
	IPtr<IComponent> component;
	FUnknownPtr<IAudioProcessor> processor;
	HostProcessData data;
	EventList events {512};
	ParameterChanges outParams {8};
	double engine = 0.0;	/* the Engine parameter: 2/3 running */
	std::vector<float> l, r, l2, r2;
	uint32_t latency = 0;
};

int memPut (void* ctx, const void* p, size_t n)
{
	int32 w = 0;
	return static_cast<IBStream*> (ctx)->write (const_cast<void*> (p), (int32)n, &w) == kResultOk && w == (int32)n ? 0 : -1;
}

void readEngineParam (Instance& in)
{
	for (int32 i = 0; i < in.outParams.getParameterCount (); i++)
	{
		IParamValueQueue* q = in.outParams.getParameterData (i);
		ParamValue v;
		int32 off;
		if (q && q->getParameterId () == 0 && q->getPointCount () > 0 && q->getPoint (q->getPointCount () - 1, off, v) == kResultOk)
			in.engine = v;
	}
}

std::vector<uint8_t> readFile (const std::string& path)
{
	std::ifstream f (path, std::ios::binary);
	if (!f)
		die ("cannot read " + path);
	return std::vector<uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
}

/* a component at the plan's rate and block, with its state, active and processing */
void open (Instance& in, const VST3::Hosting::PluginFactory& factory, const VST3::Hosting::ClassInfo& ci, const Plan& p)
{
	in.provider = owned (new PlugProvider (factory, ci, true));
	if (!in.provider->initialize ())
		die ("the plugin did not initialize");
	in.component = in.provider->getComponentPtr ();
	in.processor = FUnknownPtr<IAudioProcessor> (in.component);
	ProcessSetup setup {p.offline ? kOffline : kRealtime, kSample32, p.block, p.rate};
	if (in.processor->setupProcessing (setup) != kResultOk)
		die ("setupProcessing refused");
	in.component->activateBus (kAudio, kOutput, 0, true);
	if (p.bus2)
		in.component->activateBus (kAudio, kOutput, 1, true);
	in.component->activateBus (kEvent, kInput, 0, true);
	std::vector<uint8_t> state;
	if (!p.stateIn.empty ())
		state = readFile (p.stateIn);
	else if (!p.card.empty ())
	{
		IPtr<MemoryStream> m = owned (new MemoryStream);
		char err[512];
		if (plug_archiveWrite (p.card.c_str (), memPut, m.get (), err, sizeof err) != 0)
			die (err);
		state.assign (m->getData (), m->getData () + m->getSize ());
	}
	if (!state.empty ())
	{
		IPtr<MemoryStream> m = owned (new MemoryStream (state.data (), (TSize)state.size ()));
		if (in.component->setState (m) != kResultOk)
			die ("setState refused the card");
	}
	if (in.component->setActive (true) != kResultOk)
		die ("setActive refused");
	in.latency = in.processor->getLatencySamples ();
	in.processor->setProcessing (true);
	in.data.prepare (*in.component, p.block, kSample32);
	in.data.inputEvents = &in.events;
	in.data.outputParameterChanges = &in.outParams;
	in.engine = 0.0;
}

void close (Instance& in)
{
	in.processor->setProcessing (false);
	in.component->setActive (false);
	in.data.unprepare ();
	in.processor = nullptr;
	in.component = nullptr;
	in.provider = nullptr;
}

/* process() with 0 frames every 10 ms until the instance reports its engine running */
void waitRunning (Instance& in, ProcessContext& ctx)
{
	const auto t0 = std::chrono::steady_clock::now ();
	while (in.engine < 0.6)
	{
		in.outParams.clearQueue ();
		in.events.clear ();
		in.data.numSamples = 0;
		in.data.processContext = &ctx;
		in.processor->process (in.data);
		readEngineParam (in);
		if (std::chrono::steady_clock::now () - t0 > std::chrono::seconds (60))
			die ("the engine did not run within 60 s");
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
}

void put16 (FILE* f, unsigned v)
{
	fputc ((int)(v & 255u), f);
	fputc ((int)((v >> 8) & 255u), f);
}

void put32 (FILE* f, uint32_t v)
{
	put16 (f, v & 0xFFFFu);
	put16 (f, v >> 16);
}

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double rate)
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
void paceAsDevice (const Plan& p)
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
		fprintf (stderr, "lichomat-plughost: the time-constraint policy was refused; pacing as a normal thread\n");
}

/* mach_absolute_time() at wall time t0 + seconds */
uint64_t machAt (uint64_t t0, double seconds)
{
	mach_timebase_info_data_t tb;
	mach_timebase_info (&tb);
	return t0 + (uint64_t)(seconds * 1e9 * (double)tb.denom / (double)tb.numer);
}

/* sig to every child of this process: the engines */
void signalEngines (int sig)
{
	pid_t kids[64];
	memset (kids, 0, sizeof kids);
	(void)proc_listchildpids (getpid (), kids, sizeof kids);
	for (pid_t k : kids)
		if (k > 0)
			kill (k, sig);
}

} // namespace

int main (int argc, char** argv)
{
	if (argc != 2)
		die ("usage: lichomat-plughost PLAN");
	const Plan p = readPlan (argv[1]);
	std::string err;
	auto module = VST3::Hosting::Module::create (p.bundle, err);
	if (!module)
		die (err);
	IPtr<HostApplication> app = owned (new HostApplication);
	PluginContextFactory::instance ().setPluginContext (app);
	const auto& factory = module->getFactory ();
	VST3::Hosting::ClassInfo cls;
	bool found = false;
	for (auto& ci : factory.classInfos ())
		if (ci.category () == kVstAudioEffectClass)
		{
			cls = ci;
			found = true;
		}
	if (!found)
		die ("no audio module class in " + p.bundle);
	using RtFn = void (*) (int);
	using RtCount = unsigned long long (*) (void);
	RtFn rtInside = p.rtcheck ? (RtFn)dlsym (RTLD_DEFAULT, "plug_rtInside") : nullptr;
	RtCount rtViolations = p.rtcheck ? (RtCount)dlsym (RTLD_DEFAULT, "plug_rtViolations") : nullptr;
	RtCount rtWrites = p.rtcheck ? (RtCount)dlsym (RTLD_DEFAULT, "plug_rtWrites") : nullptr;
	if (p.rtcheck && (!rtInside || !rtViolations || !rtWrites))
		die ("rtcheck: run with DYLD_INSERT_LIBRARIES=libplug_rtcheck.dylib");
	unsigned long long probe = 0;
	if (rtInside)
	{
		/* the check's own check: an allocation and a free inside count 2 */
		rtInside (1);
		void* volatile q = malloc (16);
		free (q);
		rtInside (0);
		probe = rtViolations ();
		if (probe != 2)
			die ("rtcheck: the probe counted " + std::to_string (probe) + " calls, not 2");
	}
	std::vector<std::unique_ptr<Instance>> inst;
	for (int i = 0; i < p.instances; i++)
	{
		inst.push_back (std::make_unique<Instance> ());
		open (*inst.back (), factory, cls, p);
	}
	ProcessContext ctx {};
	ctx.sampleRate = p.rate;
	for (auto& in : inst)
		waitRunning (*in, ctx);
	/* the timeline */
	Timeline tl (p);
	plug_clock_t expect;
	plug_clockInit (&expect, p.rate);
	std::map<uint64_t, std::vector<std::string>> script;
	size_t nextNote = 0, nextKill = 0, nextHang = 0, nextRenew = 0, nextSet = 0;
	unsigned long long processCalls = 0;
	if (p.paced)
		paceAsDevice (p);
	const uint64_t wall0 = mach_absolute_time ();
	for (uint64_t f = 0; f < p.frames; f += (uint64_t)p.block)
	{
		if (p.paced)
			mach_wait_until (machAt (wall0, (double)f / p.rate));
		const uint32_t n = (uint32_t)std::min<uint64_t> ((uint64_t)p.block, p.frames - f);
		while (nextKill < p.kills.size () && p.kills[nextKill] <= f)
		{
			signalEngines (SIGKILL);
			nextKill++;
		}
		while (nextHang < p.hangs.size () && p.hangs[nextHang] <= f)
		{
			signalEngines (SIGSTOP);
			nextHang++;
		}
		while (nextRenew < p.renews.size () && p.renews[nextRenew].first <= f)
		{
			Instance& in = *inst[(size_t)p.renews[nextRenew].second];
			close (in);
			open (in, factory, cls, p);
			nextRenew++;
		}
		while (nextSet < p.setstates.size () && p.setstates[nextSet].first <= f)
		{
			std::vector<uint8_t> st = readFile (p.setstates[nextSet].second);
			IPtr<MemoryStream> m = owned (new MemoryStream (st.data (), (TSize)st.size ()));
			const auto w0 = std::chrono::steady_clock::now ();
			const tresult r = inst[0]->component->setState (m);
			printf ("lichomat-plughost: setstate at %llu: %s in %.3f s\n", (unsigned long long)f, r == kResultOk ? "ok" : "refused",
			        std::chrono::duration<double> (std::chrono::steady_clock::now () - w0).count ());
			nextSet++;
		}
		const plug_transport_t t = tl.block (f, n);
		ctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
		if (t.playing)
			ctx.state |= ProcessContext::kPlaying;
		if (t.cycle)
			ctx.state |= ProcessContext::kCycleActive | ProcessContext::kCycleValid;
		ctx.tempo = t.tempo;
		ctx.projectTimeMusic = t.ppq;
		ctx.cycleStartMusic = t.cycleStart;
		ctx.cycleEndMusic = t.cycleEnd;
		ctx.projectTimeSamples = (TSamples)f;
		/* the expected MIDI: the same generator, the same note conversion (plugin_processor.cpp) */
		plug_midi_t m[1024];
		const unsigned nm = plug_clockBlock (&expect, &t, n, m, 1024);
		for (unsigned i = 0; i < nm; i++)
		{
			char b[16];
			snprintf (b, sizeof b, m[i].len == 3 ? "%02X %02X %02X" : "%02X", m[i].b[0], m[i].b[1], m[i].b[2]);
			script[f + m[i].offset].push_back (b);
		}
		for (auto& in : inst)
			in->events.clear ();
		while (nextNote < p.notes.size () && p.notes[nextNote].frame < f + n)
		{
			const NoteCmd& c = p.notes[nextNote++];
			Event e {};
			e.sampleOffset = (int32)(c.frame - f);
			if (c.vel > 0)
			{
				e.type = Event::kNoteOnEvent;
				e.noteOn = {(int16)c.ch, (int16)c.pitch, 0.0f, (float)c.vel / 127.0f, 0, -1};
			}
			else
			{
				e.type = Event::kNoteOffEvent;
				e.noteOff = {(int16)c.ch, (int16)c.pitch, 0.0f, -1, 0.0f};
			}
			for (auto& in : inst)
				in->events.addEvent (e);
			const long v = lrintf ((float)c.vel / 127.0f * 127.0f);
			char b[16];
			snprintf (b, sizeof b, "%02X %02X %02X", (unsigned)((v ? 0x90 : 0x80) | (c.ch & 15)), (unsigned)(c.pitch & 0x7F),
			          (unsigned)v);
			script[c.frame].push_back (b);
		}
		for (auto& in : inst)
		{
			in->outParams.clearQueue ();
			in->data.numSamples = (int32)n;
			in->data.processContext = &ctx;
			/* a host's buffers hold anything: the plugin must write every frame (P10_plugin_native) */
			for (int32 b = 0; b < in->data.numOutputs; b++)
				for (int32 c = 0; c < in->data.outputs[b].numChannels; c++)
					for (uint32_t i = 0; i < n; i++)
						in->data.outputs[b].channelBuffers32[c][i] = 0.5f;
			if (rtInside)
				rtInside (1);
			in->processor->process (in->data);
			if (rtInside)
				rtInside (0);
			processCalls++;
			readEngineParam (*in);
			const float* l = in->data.outputs[0].channelBuffers32[0];
			const float* r = in->data.outputs[0].channelBuffers32[1];
			in->l.insert (in->l.end (), l, l + n);
			in->r.insert (in->r.end (), r, r + n);
			if (p.bus2)
			{
				const float* l2 = in->data.outputs[1].channelBuffers32[0];
				const float* r2 = in->data.outputs[1].channelBuffers32[1];
				in->l2.insert (in->l2.end (), l2, l2 + n);
				in->r2.insert (in->r2.end (), r2, r2 + n);
			}
		}
	}
	if (!p.stateOut.empty ())
	{
		IPtr<MemoryStream> m = owned (new MemoryStream);
		if (inst[0]->component->getState (m) != kResultOk)
			die ("getState failed");
		std::ofstream o (p.stateOut, std::ios::binary);
		o.write (m->getData (), (std::streamsize)m->getSize ());
	}
	for (size_t i = 0; i < inst.size (); i++)
	{
		const std::string base = p.out.substr (0, p.out.size () - 4) + (i == 0 ? "" : "-" + std::to_string (i));
		writeWav (base + ".wav", inst[i]->l, inst[i]->r, p.rate);
		if (p.bus2)
			writeWav (base + "-out2.wav", inst[i]->l2, inst[i]->r2, p.rate);
		static const char* const states[4] = {"stopped", "starting", "running", "failed"};
		printf ("lichomat-plughost: instance %zu: latency %u, %zu frames, engine %s\n", i, inst[i]->latency,
		        inst[i]->l.size (), states[std::lround (inst[i]->engine * 3.0) & 3]);
		close (*inst[i]);
	}
	if (!p.script.empty ())
	{
		FILE* s = fopen (p.script.c_str (), "w");
		for (auto& [frame, msgs] : script)
		{
			fprintf (s, "%lluf usb", (unsigned long long)frame);
			for (auto& b : msgs)
				fprintf (s, " %s", b.c_str ());
			fputc ('\n', s);
		}
		fclose (s);
	}
	if (rtViolations)
		printf ("lichomat-plughost: rtcheck: %llu process calls, %llu forbidden calls, %llu writes\n", processCalls,
		        rtViolations () - probe, rtWrites ());
	PluginContextFactory::instance ().setPluginContext (nullptr);
	return 0;
}
