/*
 * plugin_processor.cpp
 *
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8); see plugin_processor.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plugin_processor.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "plug_archive.h"
#include "plug_time.h"
#include "plugin_ids.h"
#include "plugin_points.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace lichomat {

Processor::Processor ()
{
	setControllerClass (kControllerUID);
	processContextRequirements.needTempo ().needTransportState ().needProjectTimeMusic ().needCycleMusic ();
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		hostValues[i].store (-1);
		stateValues[i].store (-1);
	}
	plug_paramsInit (&params, 0);
}

FUnknown* Processor::create (void*)
{
	return (IAudioProcessor*)new Processor;
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
	const tresult r = AudioEffect::initialize (context);
	if (r != kResultOk)
		return r;
	/* the device's two outputs (plan E9.4): Out 2 inactive until the host activates it, and Out 1 the
	 * sum of both meanwhile */
	addAudioOutput (STR16 ("Out 1"), SpeakerArr::kStereo, kMain, BusInfo::kDefaultActive);
	addAudioOutput (STR16 ("Out 2"), SpeakerArr::kStereo, kAux, 0);
	addEventInput (STR16 ("MIDI In"), 16);
	CardFolder::cleanStale ();
	return kResultOk;
}

tresult PLUGIN_API Processor::terminate ()
{
	link.stop ();
	folder.remove ();
	return AudioEffect::terminate ();
}

tresult PLUGIN_API Processor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs,
                                                   int32 numOuts)
{
	if (numIns == 0 && numOuts == 2 && outputs[0] == SpeakerArr::kStereo && outputs[1] == SpeakerArr::kStereo)
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	return kResultFalse;
}

tresult PLUGIN_API Processor::activateBus (MediaType type, BusDirection dir, int32 index, TBool state)
{
	const tresult r = AudioEffect::activateBus (type, dir, index, state);
	if (r == kResultTrue && type == kAudio && dir == kOutput && index == 1)
		out2Active.store (state != 0);
	return r;
}

tresult PLUGIN_API Processor::canProcessSampleSize (int32 symbolicSampleSize)
{
	return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing (ProcessSetup& setup)
{
	if (setup.maxSamplesPerBlock < 1 || setup.maxSamplesPerBlock > 8192 || !(setup.sampleRate >= 8000.0)
	    || setup.sampleRate > 384000.0)
		return kResultFalse;
	margin = plug_marginFromEnv ();
	latency = plug_latency (setup.sampleRate, (uint32_t)setup.maxSamplesPerBlock, margin);
	readOffset = plug_readOffset (setup.sampleRate, (uint32_t)setup.maxSamplesPerBlock, margin);
	return AudioEffect::setupProcessing (setup);
}

/* the instance's log: BASE/logs/<id>.log of the folder its first engine starts on, kept through a
 * restored state's new folder, so that one file tells the instance's story */
const std::string& Processor::logOf (const CardFolder& f)
{
	if (logPath.empty ())
		logPath = f.log ();
	return logPath;
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
	if (state)
	{
		std::string err;
		if (!folder.valid ())
		{
			if (folder.create (err))
				folder.copyStartCard ();	/* a new instance: the plugin's start card or LICHOMAT Emu's */
			else
				fprintf (stderr, "LICHOMAT: %s\n", err.c_str ());
		}
		plug_clockInit (&clock, processSetup.sampleRate);
		frames = 0;
		runningPid = 0;
		holdFrames = (uint32_t)std::ceil (processSetup.sampleRate * 1024.0 / 48000.0);
		resumeAt = 0;
		firstAwaited = false;
		waitedInVain = -2;
		reported = -1;
		/* the parameters: a value settles within a quarter of a second; the host keeps its values
		 * through a new activation, or takes those of a state set since */
		plug_paramsInit (&params, (uint64_t)(processSetup.sampleRate / 4.0));
		channel = PLUG_VALUE_NONE;
		const uint64_t gen = restoreGen.load (std::memory_order_acquire);
		restoreValues (gen != restoreSeen.load (std::memory_order_relaxed) ? stateValues : hostValues);
		restoreSeen.store (gen, std::memory_order_release);
		if (folder.valid ()
		    && !link.start (processSetup.sampleRate, (uint32_t)processSetup.maxSamplesPerBlock,
		                    processSetup.processMode == kRealtime, folder.card (), logOf (folder), logOf (folder), gen, err))
			fprintf (stderr, "LICHOMAT: the engine cannot start: %s\n", err.c_str ());
		active = true;
	}
	else
	{
		link.stop ();
		active = false;
	}
	return AudioEffect::setActive (state);
}

/* the block's Note Ons and Offs as MIDI, in frame order */
unsigned Processor::notesOf (IEventList* events, int32 n)
{
	unsigned count = 0;
	const int32 total = events ? events->getEventCount () : 0;
	for (int32 i = 0; i < total && count < kMaxMidi; i++)
	{
		Event e;
		if (events->getEvent (i, e) != kResultOk)
			continue;
		uint8_t st, pitch, vel;
		if (e.type == Event::kNoteOnEvent)
		{
			const long v = lrintf (e.noteOn.velocity * 127.0f);
			vel = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
			st = (uint8_t)((vel ? 0x90 : 0x80) | (e.noteOn.channel & 15));
			pitch = (uint8_t)(e.noteOn.pitch & 0x7F);
		}
		else if (e.type == Event::kNoteOffEvent)
		{
			const long v = lrintf (e.noteOff.velocity * 127.0f);
			vel = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
			st = (uint8_t)(0x80 | (e.noteOff.channel & 15));
			pitch = (uint8_t)(e.noteOff.pitch & 0x7F);
		}
		else
			continue;
		const uint32_t off = (uint32_t)(e.sampleOffset < 0 ? 0 : e.sampleOffset >= n ? n - 1 : e.sampleOffset);
		/* insertion in frame order, after the messages of the same frame */
		unsigned k = count++;
		while (k > 0 && notes[k - 1].offset > off)
		{
			notes[k] = notes[k - 1];
			k--;
		}
		notes[k] = plug_midi_t {off, 3, {st, pitch, vel}};
	}
	return count;
}

/* values (-1: none) for the host and the next engine */
void Processor::restoreValues (const std::atomic<int>* from)
{
	int v[PLUG_PARAM_COUNT];
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		v[i] = from[i].load (std::memory_order_relaxed);
	plug_paramsRestore (&params, v);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		hostValues[i].store (params.p[i].want, std::memory_order_relaxed);	/* so a getState before any block holds them */
}

/* The host's parameter changes and the engine's values of a block as MIDI into paramMidi (plan E9.4,
 * plug_params.h): first, at the block's first frame, those the host gave when there was no channel to send them on
 * (no engine, or a flush), then the host's of the block, to the engine when it runs (else kept for it), then the
 * engine's values compared with the host's. A flush or a block that is not played (n 0) only takes the host's
 * changes: they are its values and wait for the next block. Returns the MIDI count; owed, the messages of the host's
 * values the engine did not have (the pending ones and those of a first report), all at the block's first frame. */
unsigned Processor::paramsOf (ProcessData& data, plug_shm_t* s, bool running, uint32_t n, unsigned& owed)
{
	plug_values_t v;
	const bool live = running && n > 0;
	const bool have = live && plug_valuesRead (s, &v);
	if (have)
		channel = v.channel;
	const uint8_t ch = live ? channel : (uint8_t)PLUG_VALUE_NONE;
	unsigned nm = plug_paramsPending (&params, ch, frames, paramMidi, kMaxMidi);
	owed = nm;
	const unsigned np = pointsOf (data.inputParameterChanges, (int32)n, points, kMaxMidi);
	nm = plug_paramsHost (&params, points, np, frames, ch, paramMidi, nm, kMaxMidi);
	if (have)
	{
		const unsigned before = nm;
		nm = plug_paramsReport (&params, v.values, v.frame, frames, channel, paramMidi, nm, kMaxMidi);
		owed += nm - before;
	}
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		if (hostValues[i].load (std::memory_order_relaxed) != params.p[i].want)
			hostValues[i].store (params.p[i].want, std::memory_order_relaxed);
	return nm;
}

/* the values the host is to get, as output parameter changes at the block's first frame */
void Processor::showValues (ProcessData& data)
{
	if (!data.outputParameterChanges)
		return;
	unsigned from = 0;
	uint8_t v = 0;
	for (int i; (i = plug_paramsToShow (&params, &from, &v)) >= 0;)
	{
		int32 index = 0, point = 0;
		IParamValueQueue* q = data.outputParameterChanges->addParameterData (plug_params[i].id, index);
		if (q && q->addPoint (0, (double)v / plug_params[i].max, point) == kResultOk)
			params.p[i].shown = 1;
	}
}

void Processor::reportState (ProcessData& data, uint64_t state)
{
	if ((int)state == reported || !data.outputParameterChanges)
		return;
	int32 index = 0;
	IParamValueQueue* q = data.outputParameterChanges->addParameterData (kParamEngine, index);
	int32 point = 0;
	if (q && q->addPoint (0, (double)state / 3.0, point) == kResultOk)
		reported = (int)state;
}

static double monotonicNow ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* Offline (a bounce, a freeze, an export) the host does not wait for real time, nor need we:
 * process() waits for the engine, which runs at least 15 times real time, until it runs and has
 * written the frames before audioEnd, or it failed; 10 s at most by the clock. A wait in vain is
 * told to the monitor, which ends that engine (a stall), and no other wait is made for it: an
 * engine that hangs costs a render 10 s, not 10 s a block. */
void Processor::offlineWait (plug_shm_t* s, uint64_t audioEnd)
{
	const pid_t engine = link.pid ();
	if (engine == waitedInVain)
		return;
	link.waiting (true);
	const double t0 = monotonicNow ();
	for (;;)
	{
		const uint64_t st = plug_load (&s->state);
		if (st == PLUG_ENGINE_FAILED || (st == PLUG_ENGINE_RUNNING && plug_load (&s->audioEnd) >= audioEnd))
			break;
		const double waited = monotonicNow () - t0;
		if (waited >= 10.0)
		{
			waitedInVain = link.pid ();
			link.stalled (waitedInVain, waited);
			break;
		}
		struct timespec d = {0, 100000L};
		nanosleep (&d, nullptr);
	}
	link.waiting (false);
}

tresult PLUGIN_API Processor::process (ProcessData& data)
{
	plug_shm_t* s = link.shm ();
	const bool offline = processSetup.processMode == kOffline;
	if (offline && s && data.numSamples > 0)
		offlineWait (s, 0);	/* running, so that the first block's events reach it */
	/* the gen of the card of the engine started last, before the memory's state and pid (EngineLink::engineGen()) */
	const uint64_t engineGen = link.engineGen ();
	const uint64_t state = s ? plug_load (&s->state) : PLUG_ENGINE_NONE;
	reportState (data, state);
	const uint64_t gen = restoreGen.load (std::memory_order_acquire);
	if (gen != restoreSeen.load (std::memory_order_relaxed))
	{
		restoreValues (stateValues);	/* a state set while active: its values */
		/* the old engine still runs, and its report would count as the new card's first: none is taken until
		 * the next engine's epoch is set (plug_paramsEngine; plug_paramsReport ignores frames before it) */
		params.epoch = UINT64_MAX;
		restoreSeen.store (gen, std::memory_order_release);
	}
	bool running = s && state == PLUG_ENGINE_RUNNING;
	if (running)
	{
		const uint64_t pid = plug_load (&s->pid);
		/* a new engine, if it plays the card of the state restored last: one started for an earlier state (a host
		 * that sets two in a row; the monitor ends it once it sees the second restart) is not taken, and is as none
		 * until the engine of the last one runs, else its first report would give that state's missing values the
		 * earlier card's */
		if (pid != runningPid && engineGen == gen)
		{
			plug_clockReset (&clock);	/* a new engine: the slave starts afresh */
			plug_paramsEngine (&params, plug_load (&s->epoch));	/* and gets the host's values */
			channel = PLUG_VALUE_NONE;	/* the new card's own global channel, from its first values: not the old one's */
			/* The host's values that the new engine lacks reach it at a block's first frame (its first report, the
			 * pending ones), and the firmware takes a few milliseconds to settle a value: a note then plays with the
			 * card's (a voice the host set silent clicks at the card's volume). So its slave does not start, nor do
			 * the host's notes go to it (they are left out, as while no engine runs), before holdFrames after that
			 * frame; the slave then resumes at the 16th the transport has reached, on the beat grid. Not when that
			 * report gives it nothing, nor for the first engine of an offline activation (a bounce, a freeze, an
			 * export, which would lose its first 16th) */
			firstAwaited = !(offline && runningPid == 0);
			resumeAt = firstAwaited ? frames + holdFrames : 0;	/* at most so long without a report */
			runningPid = pid;
		}
		running = pid == runningPid;
	}
	if (data.numSamples <= 0 || data.numOutputs < 1 || data.outputs[0].numChannels < 2)
	{
		/* a flush (the host's changes between blocks) or a block that is not played: the host's changes are its
		 * values (a getState holds them), sent at the next block's first frame after its transport messages (a
		 * new engine gets them by its first report) */
		unsigned owed = 0;
		(void)paramsOf (data, s, running, 0, owed);
		return kResultOk;
	}
	const uint32_t n = (uint32_t)data.numSamples;
	plug_transport_t t {};
	if (const ProcessContext* c = data.processContext)
	{
		t.valid = (c->state & ProcessContext::kTempoValid) && (c->state & ProcessContext::kProjectTimeMusicValid);
		t.playing = (c->state & ProcessContext::kPlaying) != 0;
		t.tempo = c->tempo;
		t.ppq = c->projectTimeMusic;
		t.cycle = (c->state & ProcessContext::kCycleActive) && (c->state & ProcessContext::kCycleValid);
		t.cycleStart = c->cycleStartMusic;
		t.cycleEnd = c->cycleEndMusic;
	}
	unsigned owed = 0;
	const unsigned np = paramsOf (data, s, running, n, owed);
	if (firstAwaited && params.synced)
	{
		firstAwaited = false;	/* the new engine's first report, in this block */
		resumeAt = owed ? frames + holdFrames : frames;
	}
	const bool held = running && frames < resumeAt;
	const unsigned nc = held ? 0u : plug_clockBlock (&clock, &t, n, clocks, kMaxMidi);
	const unsigned nn = held ? 0u : notesOf (data.inputEvents, (int32)n);
	showValues (data);
	if (running)
		for (unsigned i = 0, j = 0, k = 0; i < nc || j < nn || k < np;)
		{
			/* at a frame the transport's messages first (Start before a note on the downbeat), then the
			 * parameters (a note plays with the value set at its frame), then the notes */
			const uint32_t oc = i < nc ? clocks[i].offset : UINT32_MAX, op = k < np ? paramMidi[k].offset : UINT32_MAX;
			const uint32_t on = j < nn ? notes[j].offset : UINT32_MAX;
			const plug_midi_t& m = (oc <= op && oc <= on) ? clocks[i++] : (op <= on) ? paramMidi[k++] : notes[j++];
			(void)plug_evPush (s, frames + m.offset, m.b, m.len);
		}
	if (s)
	{
		plug_publish (s, frames + n);
		link.wake ();
		if (offline && frames + n > readOffset)
			offlineWait (s, frames + n - readOffset);
	}
	/* Out 1 and Out 2 the device's outputs when the host activated Out 2 and gave its buffers, else
	 * Out 1 their sum (the prototype's output) and Out 2, if given, silent */
	const bool hasOut2 = data.numOutputs > 1 && data.outputs[1].numChannels >= 2 && data.outputs[1].channelBuffers32
	                     && data.outputs[1].channelBuffers32[0] && data.outputs[1].channelBuffers32[1];
	const bool split = hasOut2 && out2Active.load (std::memory_order_relaxed);
	/* a null channel (an unwritten buffer; the SDK allows it for an inactive bus) is not written */
	float** out1 = data.outputs[0].channelBuffers32;
	float* ch[PLUG_AUDIO_CHANNELS] = {out1 ? out1[0] : nullptr, out1 ? out1[1] : nullptr,
	                                   split ? data.outputs[1].channelBuffers32[0] : nullptr,
	                                   split ? data.outputs[1].channelBuffers32[1] : nullptr};
	const unsigned outs = split ? PLUG_AUDIO_CHANNELS : 2u;
	uint32_t copied = 0;
	const uint32_t skip = frames >= readOffset ? 0u : (uint32_t)std::min<uint64_t> (n, readOffset - frames);
	for (unsigned c = 0; c < outs; c++)
		for (uint32_t i = 0; ch[c] && i < skip; i++)
			ch[c][i] = 0.0f;
	if (skip < n)
	{
		if (s)
		{
			float* at[PLUG_AUDIO_CHANNELS];
			for (unsigned c = 0; c < PLUG_AUDIO_CHANNELS; c++)
				at[c] = ch[c] ? ch[c] + skip : nullptr;
			copied = plug_audioRead (s, frames + skip - readOffset, n - skip, at, split ? 0 : 1);
		}
		else
			for (unsigned c = 0; c < outs; c++)
				for (uint32_t i = skip; ch[c] && i < n; i++)
					ch[c][i] = 0.0f;
	}
	data.outputs[0].silenceFlags = copied ? 0 : 3;
	if (hasOut2)
	{
		if (!split)
			for (unsigned c = 0; c < 2; c++)
				for (uint32_t i = 0; i < n; i++)
					data.outputs[1].channelBuffers32[c][i] = 0.0f;
		data.outputs[1].silenceFlags = split && copied ? 0 : 3;
	}
	frames += n;
	return kResultOk;
}

static int streamPut (void* ctx, const void* p, size_t n)
{
	int32 written = 0;
	return static_cast<IBStream*> (ctx)->write (const_cast<void*> (p), (int32)n, &written) == kResultOk
	           && written == (int32)n
	           ? 0
	           : -1;
}

static int streamGet (void* ctx, void* p, size_t n)
{
	int32 got = 0;
	return static_cast<IBStream*> (ctx)->read (p, (int32)n, &got) == kResultOk && got == (int32)n ? 0 : -1;
}

tresult PLUGIN_API Processor::getState (IBStream* state)
{
	std::string err;
	if (!state)
		return kResultFalse;
	if (!folder.valid ())
	{
		if (!folder.create (err))
			return kResultFalse;
		folder.copyStartCard ();
	}
	char e[512];
	if (plug_archiveWrite (folder.card ().c_str (), streamPut, state, e, sizeof e) != 0)
	{
		fprintf (stderr, "LICHOMAT: getState: %s\n", e);
		return kResultFalse;
	}
	/* the parameters' values after the card (plan E9.4): those of the host, else those of the last
	 * state set, which no block has taken yet */
	const bool pending = restoreGen.load (std::memory_order_acquire) != restoreSeen.load (std::memory_order_acquire);
	int v[PLUG_PARAM_COUNT];
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		v[i] = (pending ? stateValues : hostValues)[i].load (std::memory_order_relaxed);
	return plug_paramsWrite (streamPut, state, v) == 0 ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::setState (IBStream* state)
{
	CardFolder fresh;
	std::string err;
	if (!state || !fresh.create (err))
		return kResultFalse;
	char e[512];
	int v[PLUG_PARAM_COUNT];
	if (plug_archiveRead (fresh.card ().c_str (), streamGet, state, e, sizeof e) != 0
	    || (plug_paramsRead (streamGet, state, v) != 0 && snprintf (e, sizeof e, "the parameters' values are damaged") > 0))
	{
		fprintf (stderr, "LICHOMAT: setState: %s\n", e);
		fresh.remove ();
		return kResultFalse;
	}
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		stateValues[i].store (v[i], std::memory_order_relaxed);
	const uint64_t gen = restoreGen.fetch_add (1, std::memory_order_release) + 1u;
	if (active)
		link.restart (fresh.card (), logOf (fresh), logOf (fresh), gen);	/* an engine of this gen (process()) */
	CardFolder old;
	old.take (folder);
	folder.take (fresh);
	old.remove ();
	return kResultOk;
}

} // namespace lichomat
