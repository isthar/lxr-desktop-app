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
#include <cstring>
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
		for (auto& g : taken)
			g.store (0);
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
		own.assign (4 * (size_t)maxFrames, 0.0f);	/* here, not on the render thread */
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

	/* The processor's state holds the values a render gave it (getState() reads them), so the host's sets that no
	 * render has taken yet (a knob turned, then the project saved) go into its values block here. A slot counts as
	 * taken when the render thread published its generation (after the process() that took it) before getState():
	 * taken[] is read first, then the slot, so a slot taken while the state is read is put in as well, with the same
	 * value; a set after the slot's read is not in this state */
	OSStatus SaveState (CFPropertyListRef* out) override
	{
		CFPropertyListRef base = nullptr;
		const OSStatus r = MusicDeviceBase::SaveState (&base);
		if (r != noErr)
			return r;
		int set[PLUG_PARAM_COUNT];
		bool any = false;
		for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		{
			const uint32_t t = taken[i].load (std::memory_order_acquire);
			const uint64_t s = slots[i].load (std::memory_order_acquire);
			set[i] = (uint32_t)(s >> 40) != t ? (int)(s & 0xFFu) : -1;
			any = any || set[i] >= 0;
		}
		IPtr<MemoryStream> m = owned (new MemoryStream);
		if (proc->getState (m) != kResultOk)
		{
			CFRelease (base);
			return kAudioUnitErr_CannotDoInCurrentContext;
		}
		const UInt8* bytes = reinterpret_cast<const UInt8*> (m->getData ());
		std::vector<uint8_t> state (bytes, bytes + m->getSize ());
		if (any)
			withValues (state, set);
		CFMutableDictionaryRef d = CFDictionaryCreateMutableCopy (nullptr, 0, static_cast<CFDictionaryRef> (base));
		CFRelease (base);
		CFDataRef data = CFDataCreate (nullptr, state.data (), (CFIndex)state.size ());
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
		deliver (bus, n);
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

	/* the values set[] (-1: none) into a state's values block, which the processor's getState() writes last (LCHV,
	 * plug_params.h: 12 bytes and 5 a value) */
	static void withValues (std::vector<uint8_t>& state, const int* set)
	{
		for (size_t count = 0; count <= PLUG_PARAM_COUNT; count++)
		{
			if (state.size () < 12 + 5 * count)
				return;
			const size_t at = state.size () - 12 - 5 * count;
			const uint8_t* h = state.data () + at;
			if (memcmp (h, PLUG_STATE_VALUES_MAGIC, 4) != 0 || h[8] != count || h[9] || h[10] || h[11])
				continue;
			struct Reader
			{
				const uint8_t* p;
				size_t left;
			} rd {h, 12 + 5 * count};
			int v[PLUG_PARAM_COUNT];
			if (plug_paramsRead ([] (void* ctx, void* to, size_t n) {
				    auto* x = static_cast<Reader*> (ctx);
				    if (n > x->left)
					    return -1;
				    memcpy (to, x->p, n);
				    x->p += n;
				    x->left -= n;
				    return 0;
			    }, &rd, v) != 0)
				return;
			for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
				if (set[i] >= 0)
					v[i] = set[i];
			state.resize (at);
			plug_paramsWrite ([] (void* ctx, const void* p, size_t n) {
				auto* s = static_cast<std::vector<uint8_t>*> (ctx);
				s->insert (s->end (), static_cast<const uint8_t*> (p), static_cast<const uint8_t*> (p) + n);
				return 0;
			}, &state, v);
			return;
		}
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
			channelsOf (b, bus, n, outs, ch[b]);
		heldFrames = n;
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
		for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
			if (taken[i].load (std::memory_order_relaxed) != seen[i])
				taken[i].store (seen[i], std::memory_order_release);	/* in the processor's values now (SaveState) */
		takeReports ();
		events.clear ();
		inParams.clear ();
		outParams.clear ();
	}

	float* ownChannel (UInt32 b, UInt32 c) noexcept { return own.data () + (2 * b + c) * (size_t)maxFrames; }

	/* Where output element b's frames of a render go, on the first pull of a time stamp (bus's). The SDK prepared the
	 * pulled element's buffers (its cache, or the host's with one element); another element renders into its cache,
	 * which its later pull copies (AUBase.h:658-683). An element the host told not to allocate one
	 * (ShouldAllocateBuffer 0), whose buffers come with each pull, renders into the AU's own cache, which every pull of
	 * it copies (deliver()); so does Out 2 when the host took one element, and it is not given. Only the SDK's calls
	 * that return an error, none that throws (RenderBus() is noexcept) */
	void channelsOf (UInt32 b, UInt32 bus, UInt32 n, UInt32 outs, float** ch) noexcept
	{
		held[b] = false;
		ch[0] = ownChannel (b, 0);
		ch[1] = ownChannel (b, 1);
		if (b >= outs)
			return;
		auto el = Outputs ().GetElementOrError<ausdk::AUOutputElement> (b);
		if (!el)
			return;
		if (!el->WillAllocateBuffer ())
		{
			held[b] = true;
			return;
		}
		auto l = b == bus ? el->GetBufferListOrError () : el->PrepareBufferOrError (n);
		if (l && l->mNumberBuffers >= 2)
		{
			ch[0] = static_cast<float*> (l->mBuffers[0].mData);
			ch[1] = static_cast<float*> (l->mBuffers[1].mData);
		}
	}

	/* a pull of an element whose frames wait in the AU's own cache: into the buffers the host gave for it (a null
	 * channel left as it is) */
	void deliver (UInt32 bus, UInt32 n) noexcept
	{
		if (bus > 1 || !held[bus])
			return;
		auto el = Outputs ().GetElementOrError<ausdk::AUOutputElement> (bus);
		if (!el)
			return;
		auto l = el->GetBufferListOrError ();
		if (!l || l->mNumberBuffers < 2)
			return;
		const UInt32 k = n < heldFrames ? n : heldFrames;
		for (UInt32 c = 0; c < 2; c++)
			if (void* to = l->mBuffers[c].mData)
				memcpy (to, ownChannel (bus, c), k * sizeof (float));
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
	/** the AU's own cache, two channels of the maximum frames per element: an element's frames when the host gives
	 * its buffers with each pull (ShouldAllocateBuffer 0), and Out 2's when the host took one element */
	std::vector<float> own;
	bool held[2] {};					/**< the element's frames of this time stamp are in own (the render thread's) */
	UInt32 heldFrames = 0;
	lichomat::AuEvents events;
	lichomat::AuParamChanges inParams, outParams;
	/** the host's last set of each parameter: a generation (24 bits), the offset (24), the value (8) */
	std::atomic<uint64_t> slots[PLUG_PARAM_COUNT];
	std::atomic<uint32_t> setGen {0};
	uint32_t seen[PLUG_PARAM_COUNT] {};	/**< the generation of each slot that the render thread took */
	std::atomic<uint32_t> taken[PLUG_PARAM_COUNT];	/**< seen, published after the process() that took it */
	AudioUnitParameterEvent schedule[kMaxScheduled];
	UInt32 scheduled = 0;
	std::atomic<bool> pending[kSlots];	/**< reported values the listeners are to hear of */
	std::atomic<bool> stopNotify {true};
	std::thread notifier;
};

AUSDK_COMPONENT_ENTRY (ausdk::AUMusicDeviceFactory, LichomatAU)
