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
	addAudioOutput (STR16 ("Out"), SpeakerArr::kStereo);
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
	if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	return kResultFalse;
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
		reported = -1;
		if (folder.valid ()
		    && !link.start (processSetup.sampleRate, (uint32_t)processSetup.maxSamplesPerBlock,
		                    processSetup.processMode == kRealtime, folder.card (), logOf (folder), logOf (folder), err))
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

/* Offline (a bounce, a freeze, an export) the host does not wait for real time, nor need we:
 * process() waits for the engine, which runs at least 15 times real time. 10 s at most each. */
static void offlineWait (plug_shm_t* s, uint64_t audioEnd)
{
	for (int i = 0; i < 100000; i++)
	{
		const uint64_t st = plug_load (&s->state);
		if (st == PLUG_ENGINE_FAILED || (st == PLUG_ENGINE_RUNNING && plug_load (&s->audioEnd) >= audioEnd))
			return;
		struct timespec d = {0, 100000L};
		nanosleep (&d, nullptr);
	}
}

tresult PLUGIN_API Processor::process (ProcessData& data)
{
	plug_shm_t* s = link.shm ();
	const bool offline = processSetup.processMode == kOffline;
	if (offline && s && data.numSamples > 0)
		offlineWait (s, 0);	/* running, so that the first block's events reach it */
	const uint64_t state = s ? plug_load (&s->state) : PLUG_ENGINE_NONE;
	reportState (data, state);
	if (data.numSamples <= 0 || data.numOutputs < 1 || data.outputs[0].numChannels < 2)
		return kResultOk;
	const uint32_t n = (uint32_t)data.numSamples;
	const bool running = s && state == PLUG_ENGINE_RUNNING;
	if (running)
	{
		const uint64_t pid = plug_load (&s->pid);
		if (pid != runningPid)
		{
			plug_clockReset (&clock);	/* a new engine: the slave starts afresh */
			runningPid = pid;
		}
	}
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
	const unsigned nc = plug_clockBlock (&clock, &t, n, clocks, kMaxMidi);
	const unsigned nn = notesOf (data.inputEvents, (int32)n);
	if (running)
		for (unsigned i = 0, j = 0; i < nc || j < nn;)
		{
			/* the transport's messages first at a frame: Start before a note on the downbeat */
			const plug_midi_t& m = (j == nn || (i < nc && clocks[i].offset <= notes[j].offset)) ? clocks[i++] : notes[j++];
			(void)plug_evPush (s, frames + m.offset, m.b, m.len);
		}
	if (s)
	{
		plug_publish (s, frames + n);
		link.wake ();
		if (offline && frames + n > readOffset)
			offlineWait (s, frames + n - readOffset);
	}
	float* l = data.outputs[0].channelBuffers32[0];
	float* r = data.outputs[0].channelBuffers32[1];
	uint32_t copied = 0;
	const uint32_t skip = frames >= readOffset ? 0u : (uint32_t)std::min<uint64_t> (n, readOffset - frames);
	for (uint32_t i = 0; i < skip; i++)
		l[i] = r[i] = 0.0f;
	if (skip < n)
	{
		if (s)
			copied = plug_audioRead (s, frames + skip - readOffset, n - skip, l + skip, r + skip);
		else
			for (uint32_t i = skip; i < n; i++)
				l[i] = r[i] = 0.0f;
	}
	data.outputs[0].silenceFlags = copied ? 0 : 3;
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
	return kResultOk;
}

tresult PLUGIN_API Processor::setState (IBStream* state)
{
	CardFolder fresh;
	std::string err;
	if (!state || !fresh.create (err))
		return kResultFalse;
	char e[512];
	if (plug_archiveRead (fresh.card ().c_str (), streamGet, state, e, sizeof e) != 0)
	{
		fprintf (stderr, "LICHOMAT: setState: %s\n", e);
		fresh.remove ();
		return kResultFalse;
	}
	if (active)
		link.restart (fresh.card (), logOf (fresh), logOf (fresh));
	CardFolder old;
	old.take (folder);
	folder.take (fresh);
	old.remove ();
	return kResultOk;
}

} // namespace lichomat
