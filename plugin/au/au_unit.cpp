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

/** the render guard's states (LichomatAU::guard) */
enum : uint32_t { kIdle, kRendering, kSettingUp };
/** the longest a setup waits for a render in progress: one offline render waits at most 10 s for its engine
 * (plugin_processor.cpp, offlineWait()) */
constexpr auto kSetupWait = std::chrono::seconds (11);

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
		forget ();
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
		forget ();
		proc->setProcessing (false);
		proc->setActive (false);
		stopNotify.store (true);
		if (notifier.joinable ())
			notifier.join ();
	}

	/* AudioUnitReset (any thread; a host resets an instrument when it stops or jumps): the notes and scheduled
	 * parameter events given before it are dropped, by the render thread at its next render (forget()) */
	OSStatus Reset (AudioUnitScope scope, AudioUnitElement element) override
	{
		forget ();
		return MusicDeviceBase::Reset (scope, element);
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
		forgotten ();
		for (UInt32 k = 0; k < n; k++)
		{
			const AudioUnitParameterEvent& e = events[k];
			if (e.scope != kAudioUnitScope_Global || e.element != 0 || plug_paramIndex (e.parameter) < 0)
				continue;
			if (scheduled >= kMaxScheduled)
			{
				/* dropped, and so not the AU's value either: it stays the one the processor gets */
				dropped[kDroppedScheduled].fetch_add (1, std::memory_order_relaxed);
				continue;
			}
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
					*static_cast<UInt32*> (out) = offline.load (std::memory_order_relaxed) ? 1 : 0;
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

	/* OfflineRender is the one property a host may set while the AU is initialized that touches the processor: the
	 * SDK refuses a stream format, an element count, a maximum of frames per slice and ShouldAllocateBuffer then
	 * (AUBase.cpp: IsStreamFormatWritable(), SetBusCount(), CanSetMaxFrames(), :818), and a ClassInfo goes to the
	 * processor's setState(), which a render may run beside (P13_au_setstate) */
	OSStatus SetProperty (AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, const void* in,
	                      UInt32 size) override
	{
		if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_OfflineRender)
		{
			if (size < sizeof (UInt32))
				return kAudioUnitErr_InvalidPropertyValue;
			const bool now = *static_cast<const UInt32*> (in) != 0;
			if (now == offline.load (std::memory_order_relaxed))
				return noErr;
			if (!IsInitialized ())
			{
				offline.store (now, std::memory_order_relaxed);
				return noErr;
			}
			/* the processor's mode is its setup's: a new setup, and a new engine (as a VST3 host's
			 * setupProcessing() for a freeze or an export), never under a render: the SDK takes no lock
			 * for Render (AUPlugInDispatch.cpp), and setActive(false) unmaps the memory a process()
			 * reads. A render meanwhile is silent (RenderBus()) */
			if (!holdRenders ())
				return kAudioUnitErr_CannotDoInCurrentContext;
			offline.store (now, std::memory_order_relaxed);
			proc->setProcessing (false);
			proc->setActive (false);
			const OSStatus r = setUp ();
			guard.store (kIdle, std::memory_order_release);
			return r;
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

	/* A ClassInfo refused changes nothing: what AUBase's part requires is checked first, then the processor takes the
	 * state (it refuses one it cannot read, with nothing changed), and only then AUBase restores the AU's values */
	OSStatus RestoreState (CFPropertyListRef plist) override
	{
		if (!plist || CFGetTypeID (plist) != CFDictionaryGetTypeID ())
			return kAudioUnitErr_InvalidPropertyValue;
		const auto d = static_cast<CFDictionaryRef> (plist);
		const void* data = CFDictionaryGetValue (d, kStateKey);
		if (!data || CFGetTypeID (data) != CFDataGetTypeID () || !ours (d))
			return kAudioUnitErr_InvalidPropertyValue;
		CFDataRef bytes = static_cast<CFDataRef> (data);
		IPtr<MemoryStream> m = owned (new MemoryStream (const_cast<UInt8*> (CFDataGetBytePtr (bytes)),
		                                                (TSize)CFDataGetLength (bytes)));
		if (proc->setState (m) != kResultOk)
			return kAudioUnitErr_InvalidPropertyValue;
		return MusicDeviceBase::RestoreState (plist);
	}

	/* --- notes ---------------------------------------------------------------------------------- */

	/* Note Ons and Offs from any thread (MusicDevice.h: the render thread before its render, at an offset in it;
	 * another thread at offset 0) into the note queue, which the next render takes. The offset is the host's: the AU
	 * does not tell the render thread by its id, as a host may render on more than one thread */
	OSStatus HandleNoteOn (UInt8 channel, UInt8 note, UInt8 velocity, UInt32 frame) noexcept override
	{
		if (!notes.push ({true, channel, note, velocity, frame}))
			dropped[kDroppedNotes].fetch_add (1, std::memory_order_relaxed);
		return noErr;
	}

	OSStatus HandleNoteOff (UInt8 channel, UInt8 note, UInt8 velocity, UInt32 frame) noexcept override
	{
		if (!notes.push ({false, channel, note, velocity, frame}))
			dropped[kDroppedNotes].fetch_add (1, std::memory_order_relaxed);
		return noErr;
	}

	/* --- rendering ------------------------------------------------------------------------------ */

	/* The render guard: a pull runs only from idle to rendering (an acquire compare-and-swap, no wait), and a setup
	 * while initialized (OfflineRender) only from idle to setting up; a pull that finds a setup in progress outputs
	 * silence and flags it, and so does every later pull of that time stamp */
	OSStatus RenderBus (AudioUnitRenderActionFlags& flags, const AudioTimeStamp& ts, UInt32 bus, UInt32 n) noexcept override
	{
		uint32_t idle = kIdle;
		if (!guard.compare_exchange_strong (idle, kRendering, std::memory_order_acquire, std::memory_order_relaxed))
		{
			if (NeedsToRender (ts))
				silentStamp = true;
			silence (bus, n, flags);
			return noErr;
		}
		if (NeedsToRender (ts))
			render (bus, n);
		if (silentStamp)
			silence (bus, n, flags);
		else
			deliver (bus, n);
		if (bus == 1)
			plug_auPulled (&buses);
		guard.store (kIdle, std::memory_order_release);
		return noErr;
	}

private:
	void nameOutputs ()
	{
		Output (0).SetName (CFSTR ("Out 1"));
		if (Outputs ().GetNumberOfElements () > 1)
			Output (1).SetName (CFSTR ("Out 2"));
	}

	/* the render guard from idle to setting up: waits while a render runs, at most kSetupWait */
	bool holdRenders ()
	{
		const auto until = std::chrono::steady_clock::now () + kSetupWait;
		for (;;)
		{
			uint32_t idle = kIdle;
			if (guard.compare_exchange_weak (idle, kSettingUp, std::memory_order_acquire, std::memory_order_relaxed))
				return true;
			if (std::chrono::steady_clock::now () > until)
				return false;
			std::this_thread::sleep_for (std::chrono::microseconds (100));
		}
	}

	OSStatus setUp ()
	{
		ProcessSetup setup {offline.load (std::memory_order_relaxed) ? kOffline : kRealtime, kSample32, (int32)maxFrames, rate};
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

	/* what AUBase::RestoreState() requires of a ClassInfo before it changes anything (AUBase.cpp:1815-1850): no part
	 * key, the version 0, this AU's subtype and manufacturer */
	bool ours (CFDictionaryRef d)
	{
		const AudioComponentDescription desc = GetComponentDescription ();
		auto number = [d] (CFStringRef key, SInt32& v) {
			const void* n = CFDictionaryGetValue (d, key);
			if (!n || CFGetTypeID (n) != CFNumberGetTypeID ())
				return false;
			CFNumberGetValue (static_cast<CFNumberRef> (n), kCFNumberSInt32Type, &v);
			return true;
		};
		SInt32 version = -1, subtype = 0, manufacturer = 0;
		return !CFDictionaryContainsKey (d, CFSTR (kAUPresetPartKey)) && number (CFSTR (kAUPresetVersionKey), version)
		       && version == 0 && number (CFSTR (kAUPresetSubtypeKey), subtype) && (UInt32)subtype == desc.componentSubType
		       && number (CFSTR (kAUPresetManufacturerKey), manufacturer) && (UInt32)manufacturer == desc.componentManufacturer;
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
			point (plug_params[i].id, at > last ? last : at, (double)(s & 0xFFu) / plug_params[i].max);
		}
		for (UInt32 k = 0; k < scheduled; k++)
		{
			const AudioUnitParameterEvent& e = schedule[k];
			const unsigned i = (unsigned)plug_paramIndex (e.parameter);
			const double max = plug_params[i].max;
			if (e.eventType == kParameterEvent_Immediate)
			{
				const int32 at = (int32)e.eventValues.immediate.bufferOffset;
				point (e.parameter, at > last ? last : at, plug_auPlain (i, e.eventValues.immediate.value) / max);
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
			point (e.parameter, a, plug_auPlain (i, at (a)) / max);
			if (b > a)
				point (e.parameter, b, plug_auPlain (i, b == endAt ? r.endValue : at (b)) / max);
		}
		scheduled = 0;
	}

	/* a point of the host's for the processor; one lost (more than a queue holds) is counted */
	void point (ParamID id, int32 offset, ParamValue value) noexcept
	{
		if (!inParams.add (id, offset, value))
			dropped[kDroppedPoints].fetch_add (1, std::memory_order_relaxed);
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
		silentStamp = false;
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
		forgotten ();
		takeNotes ();
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
		d.processMode = offline.load (std::memory_order_relaxed) ? kOffline : kRealtime;
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

	/* the notes the host gave since the last render, into this cycle's events (as many as one list holds; the rest
	 * wait for the next render) */
	void takeNotes () noexcept
	{
		lichomat::AuNoteQueue::Note m {};
		uint64_t at = 0;
		const uint64_t from = notesFrom.load (std::memory_order_acquire);
		while (events.getEventCount () < lichomat::AuEvents::kMax && notes.pop (m, at))
		{
			if (at < from)
				continue;	/* given before a reset (forget()) */
			Event e {};
			e.sampleOffset = (int32)(m.frame > 0x7FFFFFFFu ? 0x7FFFFFFFu : m.frame);
			if (m.on)
			{
				e.type = Event::kNoteOnEvent;
				e.noteOn = {(int16)m.channel, (int16)m.pitch, 0.0f, (float)m.velocity / 127.0f, 0, -1};
			}
			else
			{
				e.type = Event::kNoteOffEvent;
				e.noteOff = {(int16)m.channel, (int16)m.pitch, (float)m.velocity / 127.0f, -1, 0.0f};
			}
			events.addEvent (e);
		}
	}

	/* the pulled element silent, its buffers as prepared for this pull (the SDK's cache or the host's), and flagged */
	void silence (UInt32 bus, UInt32 n, AudioUnitRenderActionFlags& flags) noexcept
	{
		flags |= kAudioUnitRenderAction_OutputIsSilence;
		auto el = Outputs ().GetElementOrError<ausdk::AUOutputElement> (bus);
		if (!el)
			return;
		auto l = el->GetBufferListOrError ();
		if (!l)
			return;
		for (UInt32 c = 0; c < l->mNumberBuffers; c++)
			if (void* to = l->mBuffers[c].mData)
				memset (to, 0, n * sizeof (float) < l->mBuffers[c].mDataByteSize ? n * sizeof (float) : l->mBuffers[c].mDataByteSize);
	}

	/* A reset, an Initialize or a Cleanup, on the caller's thread: the notes queued so far and the scheduled events
	 * so far are to be dropped. Only atomics: the render thread, the queue's one consumer and the schedule's owner,
	 * drops them (takeNotes(), forgotten()), so a reset beside a render (any thread may call it) is safe */
	void forget () noexcept
	{
		const uint64_t end = notes.end ();
		uint64_t from = notesFrom.load (std::memory_order_relaxed);
		while (from < end && !notesFrom.compare_exchange_weak (from, end, std::memory_order_release, std::memory_order_relaxed))
		{
		}
		resets.fetch_add (1, std::memory_order_release);
	}

	/* the render thread's: the scheduled events given before the last reset dropped */
	void forgotten () noexcept
	{
		const uint32_t r = resets.load (std::memory_order_acquire);
		if (r != resetsSeen)
		{
			scheduled = 0;
			resetsSeen = r;
		}
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

	/* off the render thread: what the AU dropped (more than a render cycle holds), at most once a second, and once
	 * more when the notifier stops */
	void sayDropped (bool now)
	{
		const auto t = std::chrono::steady_clock::now ();
		if (!now && t - saidAt < std::chrono::seconds (1))
			return;
		uint32_t n[kDroppedKinds];
		for (unsigned k = 0; k < kDroppedKinds; k++)
			n[k] = dropped[k].exchange (0, std::memory_order_relaxed);
		if (!n[kDroppedNotes] && !n[kDroppedScheduled] && !n[kDroppedPoints])
			return;
		saidAt = t;
		fprintf (stderr,
		         "LICHOMAT: dropped what the host gave beyond a render cycle's room: %u notes (1,024 queued), %u scheduled "
		         "parameter events (256 a cycle), %u parameter points (64 a parameter)\n",
		         (unsigned)n[kDroppedNotes], (unsigned)n[kDroppedScheduled], (unsigned)n[kDroppedPoints]);
	}

	/* off the render thread: the host's listeners learn of each value the processor reported */
	void notifyLoop ()
	{
		saidAt = std::chrono::steady_clock::now () - std::chrono::seconds (2);
		while (!stopNotify.load ())
		{
			sayDropped (false);
			for (unsigned k = 0; k < kSlots; k++)
				if (pending[k].exchange (false, std::memory_order_relaxed))
				{
					AudioUnitParameter p {GetComponentInstance (), k < PLUG_PARAM_COUNT ? plug_params[k].id : PLUG_AU_ENGINE,
					                      kAudioUnitScope_Global, 0};
					AUParameterListenerNotify (nullptr, nullptr, &p);
				}
			std::this_thread::sleep_for (std::chrono::milliseconds (20));
		}
		sayDropped (true);
	}

	IPtr<lichomat::Processor> proc;
	double rate = 0.0;
	UInt32 maxFrames = 0;
	std::atomic<bool> offline {false};	/**< set while uninitialized, or with the render guard held */
	/** the render guard: kIdle, kRendering (a pull, on the render thread) or kSettingUp (OfflineRender set while
	 * initialized); what the render thread and a setup share below is written only while it is held */
	std::atomic<uint32_t> guard {kIdle};
	bool silentStamp = false;			/**< this time stamp's first pull found a setup in progress (the render thread's) */
	bool out2 = false;					/**< the processor's Out 2 is active (the render thread's, and a setup's) */
	plug_auBuses_t buses {0, 0};		/**< the render thread's, and a setup's */
	/** the AU's own cache, two channels of the maximum frames per element: an element's frames when the host gives
	 * its buffers with each pull (ShouldAllocateBuffer 0), and Out 2's when the host took one element */
	std::vector<float> own;
	bool held[2] {};					/**< the element's frames of this time stamp are in own (the render thread's) */
	UInt32 heldFrames = 0;
	lichomat::AuNoteQueue notes;		/**< the host's notes, from any thread */
	lichomat::AuEvents events;			/**< the notes of the cycle being rendered (the render thread's) */
	lichomat::AuParamChanges inParams, outParams;
	/** the host's last set of each parameter: a generation (24 bits), the offset (24), the value (8) */
	std::atomic<uint64_t> slots[PLUG_PARAM_COUNT];
	std::atomic<uint32_t> setGen {0};
	uint32_t seen[PLUG_PARAM_COUNT] {};	/**< the generation of each slot that the render thread took */
	std::atomic<uint32_t> taken[PLUG_PARAM_COUNT];	/**< seen, published after the process() that took it */
	AudioUnitParameterEvent schedule[kMaxScheduled];
	UInt32 scheduled = 0;
	std::atomic<uint64_t> notesFrom {0};	/**< the queue's notes before this position came before a reset */
	std::atomic<uint32_t> resets {0};		/**< resets so far (forget()) */
	uint32_t resetsSeen = 0;				/**< those the schedule has seen (the render thread's) */
	std::atomic<bool> pending[kSlots];	/**< reported values the listeners are to hear of */
	std::atomic<bool> stopNotify {true};
	/** what the render path dropped since the notifier said so: notes beyond the queue, scheduled events beyond a
	 * cycle's 256, points beyond a parameter's 64 (relaxed counters) */
	enum { kDroppedNotes, kDroppedScheduled, kDroppedPoints, kDroppedKinds };
	std::atomic<uint32_t> dropped[kDroppedKinds] {};
	std::chrono::steady_clock::time_point saidAt {};	/**< the notifier's */
	std::thread notifier;
};

AUSDK_COMPONENT_ENTRY (ausdk::AUMusicDeviceFactory, LichomatAU)
