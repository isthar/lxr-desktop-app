/*
 * au_unit.cpp
 *
 * LICHOMAT as an Audio Unit v2 instrument (plan E9.7 Task 4): a MusicDeviceBase of Apple's
 * AudioUnitSDK around the plugin's own VST3 processor (plugin_processor.h), linked into the
 * component, with its engine process per instance, its two outputs, parameters, latency and state.
 * The AU turns its host's calls into the processor's: Note Ons and Offs into events at their frames,
 * parameter sets and scheduled parameter events into points, the host's beat, tempo and transport
 * into a process context; it renders both output elements with one process() per time stamp and
 * gives the host the values the processor reports. On the render thread nothing waits, allocates or
 * locks but the processor's offline wait (plan E9.2 P-14); a notifier thread tells the host's
 * listeners of the reported values.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <AudioToolbox/AudioUnitUtilities.h>
#include <AudioUnitSDK/MusicDeviceBase.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "au_lists.h"
#include "plug_au.h"
#include "plug_params.h"
#include "plugin_ids.h"
#include "plugin_processor.h"
#include "public.sdk/source/common/memorystream.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

/** the ClassInfo key of the plugin's state: the VST3 processor's bytes, LCHS and LCHV (plan E9.7 A-11) */
const CFStringRef kStateKey = CFSTR ("lichomat-state");
constexpr unsigned kSlots = PLUG_PARAM_COUNT + 1;	/**< the 36 parameters and the Engine, last */
constexpr UInt32 kMaxScheduled = 256;				/**< scheduled parameter events of one render cycle */

const char* const kEngineNames[4] = {"stopped", "starting", "running", "failed"};

CFStringRef cfString (const char* s)
{
	return CFStringCreateWithCString (nullptr, s, kCFStringEncodingUTF8);
}

} // namespace

class LichomatAU : public ausdk::MusicDeviceBase
{
public:
	explicit LichomatAU (AudioComponentInstance ci) : MusicDeviceBase (ci, 0, 2)
	{
		proc = owned (new lichomat::Processor);
		proc->initialize (nullptr);
		SpeakerArrangement stereo[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo};
		proc->setBusArrangements (nullptr, 0, stereo, 2);
		proc->activateBus (kAudio, kOutput, 0, true);
		proc->activateBus (kEvent, kInput, 0, true);
		/* every parameter exists before the host sees one: setting a value never allocates */
		Globals ()->SetParameter (PLUG_AU_ENGINE, 0.0f);
		for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
			Globals ()->SetParameter (plug_params[i].id, plug_auValue (i, plug_params[i].def));
		for (auto& s : slots)
			s.store (0);
		for (auto& p : pending)
			p.store (false);
	}

	~LichomatAU () override
	{
		proc->terminate ();	/* the dispatcher has called Cleanup() (DoCleanup) before */
	}

	void PostConstructor () override
	{
		MusicDeviceBase::PostConstructor ();
		nameOutputs ();
	}

	/* --- the processor's life ----------------------------------------------------------------- */

	OSStatus Initialize () override
	{
		const UInt32 outs = Outputs ().GetNumberOfElements ();
		rate = Output (0).GetStreamFormat ().mSampleRate;
		for (UInt32 b = 1; b < outs; b++)
			if (Output (b).GetStreamFormat ().mSampleRate != rate)
				return kAudioUnitErr_FormatNotSupported;
		maxFrames = GetMaxFramesPerSlice ();
		spare.assign (2 * (size_t)maxFrames, 0.0f);
		OSStatus r = setUp ();
		if (r != noErr)
			return r;
		stopNotify.store (false);
		notifier = std::thread ([this] { notifyLoop (); });
		PropertyChanged (kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
		return noErr;
	}

	void Cleanup () override
	{
		proc->setProcessing (false);
		proc->setActive (false);
		stopNotify.store (true);
		if (notifier.joinable ())
			notifier.join ();
	}

	/* --- formats and outputs ------------------------------------------------------------------ */

	bool StreamFormatWritable (AudioUnitScope scope, AudioUnitElement) override
	{
		return scope == kAudioUnitScope_Output && !IsInitialized ();
	}

	bool ValidFormat (AudioUnitScope scope, AudioUnitElement element, const AudioStreamBasicDescription& f) override
	{
		return MusicDeviceBase::ValidFormat (scope, element, f) && f.mChannelsPerFrame == 2;
	}

	/* one sample rate for both outputs: a host that sets one sets both */
	OSStatus ChangeStreamFormat (AudioUnitScope scope, AudioUnitElement element, const AudioStreamBasicDescription& was,
	                             const AudioStreamBasicDescription& now) override
	{
		const OSStatus r = MusicDeviceBase::ChangeStreamFormat (scope, element, was, now);
		if (r != noErr || was.mSampleRate == now.mSampleRate)
			return r;
		for (UInt32 b = 0; b < Outputs ().GetNumberOfElements (); b++)
		{
			AudioStreamBasicDescription f = Output (b).GetStreamFormat ();
			if (f.mSampleRate != now.mSampleRate)
			{
				f.mSampleRate = now.mSampleRate;
				Output (b).SetStreamFormat (f);
				PropertyChanged (kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, b);
			}
		}
		return noErr;
	}

	UInt32 SupportedNumChannels (const AUChannelInfo** out) override
	{
		static const AUChannelInfo info[1] = {{0, 2}};
		if (out)
			*out = info;
		return 1;
	}

	/* a host may take Out 1 alone (one output element: Out 1 is then always the sum) or both */
	bool BusCountWritable (AudioUnitScope scope) override { return scope == kAudioUnitScope_Output; }

	OSStatus SetBusCount (AudioUnitScope scope, UInt32 count) override
	{
		if (scope != kAudioUnitScope_Output || count < 1 || count > 2)
			return kAudioUnitErr_InvalidPropertyValue;
		const OSStatus r = MusicDeviceBase::SetBusCount (scope, count);
		if (r == noErr)
			nameOutputs ();
		return r;
	}

	/* --- parameters --------------------------------------------------------------------------- */

	bool CanScheduleParameters () const noexcept override { return false; }	/* ScheduleParameter() is ours */

	OSStatus GetParameterInfo (AudioUnitScope scope, AudioUnitParameterID id, AudioUnitParameterInfo& info) override
	{
		if (scope != kAudioUnitScope_Global)
			return kAudioUnitErr_InvalidScope;
		info = AudioUnitParameterInfo {};
		info.flags = kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_ValuesHaveStrings;
		if (id == PLUG_AU_ENGINE)
		{
			FillInParameterName (info, cfString ("Engine"), true);
			info.unit = kAudioUnitParameterUnit_Indexed;
			info.minValue = 0.0f;
			info.maxValue = 3.0f;
			info.defaultValue = 0.0f;
			return noErr;
		}
		const int i = plug_paramIndex (id);
		if (i < 0)
			return kAudioUnitErr_InvalidParameter;
		const plug_param_t& p = plug_params[i];
		FillInParameterName (info, cfString (p.name), true);
		info.flags |= kAudioUnitParameterFlag_IsWritable;
		info.unit = p.names ? kAudioUnitParameterUnit_Indexed : kAudioUnitParameterUnit_Generic;
		info.minValue = plug_auValue ((unsigned)i, 0);
		info.maxValue = plug_auValue ((unsigned)i, p.max);
		info.defaultValue = plug_auValue ((unsigned)i, p.def);
		return noErr;
	}

	OSStatus GetParameterValueStrings (AudioUnitScope scope, AudioUnitParameterID id, CFArrayRef* out) override
	{
		const int i = plug_paramIndex (id);
		const char* const* names = id == PLUG_AU_ENGINE ? kEngineNames : i >= 0 ? plug_params[i].names : nullptr;
		if (scope != kAudioUnitScope_Global || !names)
			return kAudioUnitErr_InvalidProperty;
		if (!out)
			return noErr;
		const int n = id == PLUG_AU_ENGINE ? 4 : plug_params[i].max + 1;
		CFMutableArrayRef a = CFArrayCreateMutable (nullptr, n, &kCFTypeArrayCallBacks);
		for (int v = 0; v < n; v++)
		{
			CFStringRef s = cfString (names[v]);
			CFArrayAppendValue (a, s);
			CFRelease (s);
		}
		*out = a;
		return noErr;
	}

	/* A host's set, from any thread: the value goes to the AU's own (GetParameter()) and, as a point
	 * at its offset in the next render cycle, to the processor (the last set of a parameter in a cycle
	 * wins: AU v2's SetParameter() is a value, not an event). */
	OSStatus SetParameter (AudioUnitParameterID id, AudioUnitScope scope, AudioUnitElement element,
	                       AudioUnitParameterValue value, UInt32 offset) noexcept override
	{
		const int i = plug_paramIndex (id);
		if (scope != kAudioUnitScope_Global || element != 0 || i < 0)
			return kAudioUnitErr_InvalidParameter;	/* the Engine is read only */
		const OSStatus r = MusicDeviceBase::SetParameter (id, scope, element, value, offset);
		if (r != noErr)
			return r;
		const uint64_t gen = (uint64_t)(setGen.fetch_add (1, std::memory_order_relaxed) + 1) & 0xFFFFFFu;
		const uint64_t at = offset > 0xFFFFFFu ? 0xFFFFFFu : offset;
		slots[i].store (gen << 40 | at << 8 | plug_auPlain ((unsigned)i, value), std::memory_order_release);
		return noErr;
	}

	/* scheduled events, on the render thread before the cycle they belong to: each one a point (a ramp
	 * its two ends), all of them in the cycle's order */
	OSStatus ScheduleParameter (const AudioUnitParameterEvent* events, UInt32 n) noexcept override
	{
		for (UInt32 k = 0; k < n; k++)
		{
			const AudioUnitParameterEvent& e = events[k];
			if (e.scope != kAudioUnitScope_Global || e.element != 0 || plug_paramIndex (e.parameter) < 0)
				continue;
			if (scheduled < kMaxScheduled)
				schedule[scheduled++] = e;
			const float last = e.eventType == kParameterEvent_Immediate ? e.eventValues.immediate.value
			                                                             : e.eventValues.ramp.endValue;
			Globals ()->SetParameterRT (e.parameter, last);
		}
		return noErr;
	}

	/* --- properties: offline rendering, the parameters' strings --------------------------------- */

	OSStatus GetPropertyInfo (AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, UInt32& size,
	                          bool& writable) override
	{
		if (scope == kAudioUnitScope_Global)
			switch (id)
			{
				case kAudioUnitProperty_OfflineRender:
					size = sizeof (UInt32);
					writable = true;
					return noErr;
				case kAudioUnitProperty_ParameterStringFromValue:
					size = sizeof (AudioUnitParameterStringFromValue);
					writable = false;
					return noErr;
				case kAudioUnitProperty_ParameterValueFromString:
					size = sizeof (AudioUnitParameterValueFromString);
					writable = false;
					return noErr;
				default: break;
			}
		return MusicDeviceBase::GetPropertyInfo (id, scope, element, size, writable);
	}

	OSStatus GetProperty (AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, void* out) override
	{
		if (scope == kAudioUnitScope_Global)
			switch (id)
			{
				case kAudioUnitProperty_OfflineRender:
					*static_cast<UInt32*> (out) = offline ? 1 : 0;
					return noErr;
				case kAudioUnitProperty_ParameterStringFromValue:
				{
					auto* p = static_cast<AudioUnitParameterStringFromValue*> (out);
					AudioUnitParameterValue v = 0;
					if (p->inValue)
						v = *p->inValue;
					else if (GetParameter (p->inParamID, kAudioUnitScope_Global, 0, v) != noErr)
						return kAudioUnitErr_InvalidParameter;
					char text[32];
					if (!show (p->inParamID, v, text, sizeof text))
						return kAudioUnitErr_InvalidParameter;
					p->outString = cfString (text);
					return noErr;
				}
				case kAudioUnitProperty_ParameterValueFromString:
				{
					auto* p = static_cast<AudioUnitParameterValueFromString*> (out);
					char text[32];
					if (!p->inString || !CFStringGetCString (p->inString, text, sizeof text, kCFStringEncodingUTF8))
						return kAudioUnitErr_InvalidPropertyValue;
					return parse (p->inParamID, text, p->outValue) ? OSStatus (noErr) : OSStatus (kAudioUnitErr_InvalidPropertyValue);
				}
				default: break;
			}
		return MusicDeviceBase::GetProperty (id, scope, element, out);
	}

	OSStatus SetProperty (AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, const void* in,
	                      UInt32 size) override
	{
		if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_OfflineRender)
		{
			if (size < sizeof (UInt32))
				return kAudioUnitErr_InvalidPropertyValue;
			const bool now = *static_cast<const UInt32*> (in) != 0;
			if (now == offline)
				return noErr;
			offline = now;
			if (!IsInitialized ())
				return noErr;
			/* the processor's mode is its setup's: a new setup, and a new engine (as a VST3 host's
			 * setupProcessing() for a freeze or an export) */
			proc->setProcessing (false);
			proc->setActive (false);
			return setUp ();
		}
		return MusicDeviceBase::SetProperty (id, scope, element, in, size);
	}

	Float64 GetLatency () noexcept override
	{
		return rate > 0.0 ? (Float64)proc->getLatencySamples () / rate : 0.0;
	}

	/* --- the state: the AU's ClassInfo with the processor's bytes ------------------------------ */

	OSStatus SaveState (CFPropertyListRef* out) override
	{
		CFPropertyListRef base = nullptr;
		const OSStatus r = MusicDeviceBase::SaveState (&base);
		if (r != noErr)
			return r;
		IPtr<MemoryStream> m = owned (new MemoryStream);
		if (proc->getState (m) != kResultOk)
		{
			CFRelease (base);
			return kAudioUnitErr_CannotDoInCurrentContext;
		}
		CFMutableDictionaryRef d = CFDictionaryCreateMutableCopy (nullptr, 0, static_cast<CFDictionaryRef> (base));
		CFRelease (base);
		CFDataRef data = CFDataCreate (nullptr, reinterpret_cast<const UInt8*> (m->getData ()), (CFIndex)m->getSize ());
		CFDictionarySetValue (d, kStateKey, data);
		CFRelease (data);
		*out = d;
		return noErr;
	}

	OSStatus RestoreState (CFPropertyListRef plist) override
	{
		if (!plist || CFGetTypeID (plist) != CFDictionaryGetTypeID ())
			return kAudioUnitErr_InvalidPropertyValue;
		const void* data = CFDictionaryGetValue (static_cast<CFDictionaryRef> (plist), kStateKey);
		if (!data || CFGetTypeID (data) != CFDataGetTypeID ())
			return kAudioUnitErr_InvalidPropertyValue;
		const OSStatus r = MusicDeviceBase::RestoreState (plist);
		if (r != noErr)
			return r;
		CFDataRef bytes = static_cast<CFDataRef> (data);
		IPtr<MemoryStream> m = owned (new MemoryStream (const_cast<UInt8*> (CFDataGetBytePtr (bytes)),
		                                                (TSize)CFDataGetLength (bytes)));
		return proc->setState (m) == kResultOk ? OSStatus (noErr) : OSStatus (kAudioUnitErr_InvalidPropertyValue);
	}

	/* --- notes ---------------------------------------------------------------------------------- */

	OSStatus HandleNoteOn (UInt8 channel, UInt8 note, UInt8 velocity, UInt32 frame) noexcept override
	{
		Event e {};
		e.type = Event::kNoteOnEvent;
		e.sampleOffset = (int32)frame;
		e.noteOn = {(int16)channel, (int16)note, 0.0f, (float)velocity / 127.0f, 0, -1};
		events.addEvent (e);
		return noErr;
	}

	OSStatus HandleNoteOff (UInt8 channel, UInt8 note, UInt8 velocity, UInt32 frame) noexcept override
	{
		Event e {};
		e.type = Event::kNoteOffEvent;
		e.sampleOffset = (int32)frame;
		e.noteOff = {(int16)channel, (int16)note, (float)velocity / 127.0f, -1, 0.0f};
		events.addEvent (e);
		return noErr;
	}

	/* --- rendering ------------------------------------------------------------------------------ */

	OSStatus RenderBus (AudioUnitRenderActionFlags&, const AudioTimeStamp& ts, UInt32 bus, UInt32 n) noexcept override
	{
		if (NeedsToRender (ts))
			render (bus, n);
		if (bus == 1)
			plug_auPulled (&buses);
		return noErr;
	}

private:
	void nameOutputs ()
	{
		Output (0).SetName (CFSTR ("Out 1"));
		if (Outputs ().GetNumberOfElements () > 1)
			Output (1).SetName (CFSTR ("Out 2"));
	}

	OSStatus setUp ()
	{
		ProcessSetup setup {offline ? kOffline : kRealtime, kSample32, (int32)maxFrames, rate};
		if (proc->setupProcessing (setup) != kResultOk)
			return kAudioUnitErr_FormatNotSupported;
		out2 = false;
		proc->activateBus (kAudio, kOutput, 1, false);
		buses = plug_auBuses_t {0, 0};
		if (proc->setActive (true) != kResultOk)
			return kAudioUnitErr_FailedInitialization;
		proc->setProcessing (true);
		return noErr;
	}

	/* a value as the device shows it: its name, or the number */
	static bool show (AudioUnitParameterID id, AudioUnitParameterValue v, char* text, size_t size)
	{
		if (id == PLUG_AU_ENGINE)
		{
			const long k = lroundf (v);
			snprintf (text, size, "%s", kEngineNames[k < 0 ? 0 : k > 3 ? 3 : k]);
			return true;
		}
		const int i = plug_paramIndex (id);
		if (i < 0)
			return false;
		const uint8_t plain = plug_auPlain ((unsigned)i, v);
		if (plug_params[i].names)
			snprintf (text, size, "%s", plug_params[i].names[plain]);
		else
			snprintf (text, size, "%d", (int)lroundf (plug_auValue ((unsigned)i, plain)));
		return true;
	}

	static bool parse (AudioUnitParameterID id, const char* text, AudioUnitParameterValue& v)
	{
		const int i = plug_paramIndex (id);
		if (i < 0)
			return false;
		const plug_param_t& p = plug_params[i];
		for (int k = 0; p.names && k <= p.max; k++)
			if (!strcmp (text, p.names[k]))
			{
				v = plug_auValue ((unsigned)i, (uint8_t)k);
				return true;
			}
		char* end = nullptr;
		const long n = strtol (text, &end, 10);
		if (end == text || n - p.offset < 0 || n - p.offset > p.max)
			return false;
		v = (AudioUnitParameterValue)n;
		return true;
	}

	/* the host's beat, tempo and transport as the processor's context (plug_clock.h reads it) */
	void hostContext (ProcessContext& c)
	{
		c = ProcessContext {};
		c.sampleRate = rate;
		Float64 beat = 0.0, tempo = 0.0;
		if (CallHostBeatAndTempo (&beat, &tempo) == noErr)
		{
			c.state |= ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
			c.tempo = tempo;
			c.projectTimeMusic = beat;
		}
		Boolean playing = false, recording = false, changed = false, cycling = false;
		Float64 sample = 0.0, cycleStart = 0.0, cycleEnd = 0.0;
		const HostCallbackInfo& h = GetHostCallbackInfo ();
		const OSStatus t = h.transportStateProc2
		                       ? h.transportStateProc2 (h.hostUserData, &playing, &recording, &changed, &sample, &cycling,
		                                                &cycleStart, &cycleEnd)
		                       : CallHostTransportState (&playing, &changed, &sample, &cycling, &cycleStart, &cycleEnd);
		if (t != noErr)
			return;	/* no transport: the sequencer does not follow (README) */
		if (playing)
			c.state |= ProcessContext::kPlaying;
		if (cycling)
			c.state |= ProcessContext::kCycleActive | ProcessContext::kCycleValid;
		c.cycleStartMusic = cycleStart;
		c.cycleEndMusic = cycleEnd;
		c.projectTimeSamples = (TSamples)sample;
	}

	/* the host's parameter sets and scheduled events of this cycle as the processor's points */
	void hostPoints (UInt32 n)
	{
		const int32 last = n > 0 ? (int32)n - 1 : 0;
		for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		{
			const uint64_t s = slots[i].load (std::memory_order_acquire);
			const uint32_t gen = (uint32_t)(s >> 40);
			if (gen == seen[i])
				continue;
			seen[i] = gen;
			const int32 at = (int32)((s >> 8) & 0xFFFFFFu);
			inParams.add (plug_params[i].id, at > last ? last : at, (double)(s & 0xFFu) / plug_params[i].max);
		}
		for (UInt32 k = 0; k < scheduled; k++)
		{
			const AudioUnitParameterEvent& e = schedule[k];
			const unsigned i = (unsigned)plug_paramIndex (e.parameter);
			const double max = plug_params[i].max;
			if (e.eventType == kParameterEvent_Immediate)
			{
				const int32 at = (int32)e.eventValues.immediate.bufferOffset;
				inParams.add (e.parameter, at > last ? last : at, plug_auPlain (i, e.eventValues.immediate.value) / max);
				continue;
			}
			/* a ramp: its value at the cycle's first frame it covers and at its last */
			const auto& r = e.eventValues.ramp;
			const double d = r.durationInFrames > 0 ? (double)r.durationInFrames : 1.0;
			const int32 a = r.startBufferOffset < 0 ? 0 : r.startBufferOffset > last ? last : r.startBufferOffset;
			const int64_t endAt = (int64_t)r.startBufferOffset + (int64_t)r.durationInFrames - 1;
			const int32 b = endAt < a ? a : endAt > last ? last : (int32)endAt;
			const auto at = [&] (int32 f) {
				const double x = ((double)f - r.startBufferOffset) / d;
				return r.startValue + (r.endValue - r.startValue) * (x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x);
			};
			inParams.add (e.parameter, a, plug_auPlain (i, at (a)) / max);
			if (b > a)
				inParams.add (e.parameter, b, plug_auPlain (i, b == endAt ? r.endValue : at (b)) / max);
		}
		scheduled = 0;
	}

	/* the values the processor reported: into the AU's own, and the host's listeners are told */
	void takeReports ()
	{
		for (int32 q = 0; q < outParams.getParameterCount (); q++)
		{
			IParamValueQueue* pq = outParams.getParameterData (q);
			int32 off = 0;
			ParamValue v = 0.0;
			const int32 np = pq ? pq->getPointCount () : 0;
			if (np < 1 || pq->getPoint (np - 1, off, v) != kResultOk)
				continue;
			const ParamID id = pq->getParameterId ();
			const int i = plug_paramIndex (id);
			if (id == lichomat::kParamEngine)
			{
				Globals ()->SetParameterRT (PLUG_AU_ENGINE, (float)lround (v * 3.0));
				pending[PLUG_PARAM_COUNT].store (true, std::memory_order_relaxed);
			}
			else if (i >= 0)
			{
				Globals ()->SetParameterRT (id, plug_auValue ((unsigned)i, plug_paramValue ((unsigned)i, v)));
				pending[i].store (true, std::memory_order_relaxed);
			}
		}
	}

	void render (UInt32 bus, UInt32 n)
	{
		const UInt32 outs = Outputs ().GetNumberOfElements ();
		const bool split = plug_auRender (&buses) && outs > 1;
		if (split != out2)
		{
			proc->activateBus (kAudio, kOutput, 1, split);	/* an atomic store in the processor */
			out2 = split;
		}
		float* ch[2][2];
		for (UInt32 b = 0; b < 2; b++)
		{
			if (b < outs)
			{
				AudioBufferList& l = b == bus ? Output (b).GetBufferList () : Output (b).PrepareBuffer (n);
				ch[b][0] = static_cast<float*> (l.mBuffers[0].mData);
				ch[b][1] = static_cast<float*> (l.mBuffers[1].mData);
			}
			else
			{
				ch[b][0] = spare.data ();
				ch[b][1] = spare.data () + maxFrames;
			}
		}
		hostPoints (n);
		ProcessContext ctx;
		hostContext (ctx);
		AudioBusBuffers out[2] {};
		for (int b = 0; b < 2; b++)
		{
			out[b].numChannels = 2;
			out[b].channelBuffers32 = ch[b];
		}
		ProcessData d;
		d.processMode = offline ? kOffline : kRealtime;
		d.symbolicSampleSize = kSample32;
		d.numSamples = (int32)n;
		d.numInputs = 0;
		d.numOutputs = 2;
		d.outputs = out;
		d.inputParameterChanges = &inParams;
		d.outputParameterChanges = &outParams;
		d.inputEvents = &events;
		d.processContext = &ctx;
		proc->process (d);
		takeReports ();
		events.clear ();
		inParams.clear ();
		outParams.clear ();
	}

	/* off the render thread: the host's listeners learn of each value the processor reported */
	void notifyLoop ()
	{
		while (!stopNotify.load ())
		{
			for (unsigned k = 0; k < kSlots; k++)
				if (pending[k].exchange (false, std::memory_order_relaxed))
				{
					AudioUnitParameter p {GetComponentInstance (), k < PLUG_PARAM_COUNT ? plug_params[k].id : PLUG_AU_ENGINE,
					                      kAudioUnitScope_Global, 0};
					AUParameterListenerNotify (nullptr, nullptr, &p);
				}
			std::this_thread::sleep_for (std::chrono::milliseconds (20));
		}
	}

	IPtr<lichomat::Processor> proc;
	double rate = 0.0;
	UInt32 maxFrames = 0;
	bool offline = false;
	bool out2 = false;					/**< the processor's Out 2 is active (the render thread's) */
	plug_auBuses_t buses {0, 0};		/**< the render thread's */
	std::vector<float> spare;			/**< Out 2's buffers when the host took one output element */
	lichomat::AuEvents events;
	lichomat::AuParamChanges inParams, outParams;
	/** the host's last set of each parameter: a generation (24 bits), the offset (24), the value (8) */
	std::atomic<uint64_t> slots[PLUG_PARAM_COUNT];
	std::atomic<uint32_t> setGen {0};
	uint32_t seen[PLUG_PARAM_COUNT] {};	/**< the generation of each slot that the render thread took */
	AudioUnitParameterEvent schedule[kMaxScheduled];
	UInt32 scheduled = 0;
	std::atomic<bool> pending[kSlots];	/**< reported values the listeners are to hear of */
	std::atomic<bool> stopNotify {true};
	std::thread notifier;
};

AUSDK_COMPONENT_ENTRY (ausdk::AUMusicDeviceFactory, LichomatAU)
