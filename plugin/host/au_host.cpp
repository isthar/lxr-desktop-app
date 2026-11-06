/*
 * au_host.cpp
 *
 * lichomat-auhost, the AU's test host (plan E9.7 Task 5). It plays lichomat-plughost's plans
 * (host_plan.h; plug_host.cpp's header names the commands; those of the VST3's own interfaces, a
 * flush, null buffers, the controller, states from another thread or before any block, it refuses)
 * through LICHOMAT.component loaded into
 * its own process: it reads the bundle's AudioComponents entry and registers the component's
 * factory for this process alone (AudioComponentRegister(): nothing is installed, nothing is
 * registered with the system), then plays the timeline as an AU host does: the transport through
 * the host callbacks (beat and tempo; the transport state, both forms, with the cycle), the notes
 * through MusicDeviceMIDIEvent() at their offsets, the plan's parameter changes through
 * AudioUnitScheduleParameters(), an echo through AudioUnitSetParameter(), and AudioUnitRender() of
 * Out 1 and, with bus2, of Out 2 after it. Its output is lichomat-plughost's: each instance's WAV
 * files, the script of the MIDI the plugin must have given its engine, and the same summary lines.
 * The values the plugin reports are the AU's parameter values after each block: at the first block
 * every one, then each one that changed; the values the host's listener heard of are counted. With
 * events, the MIDI instance 0 gave its engine, read from its event ring after each block (host_plan.h).
 *
 * Its own plan commands:
 *   pull2 START END   render Out 2 only in the blocks from frame START to frame END
 *   outputs 1         take one output element: Out 1 alone
 *   noalloc           kAudioUnitProperty_ShouldAllocateBuffer 0 on every output element before
 *                     AudioUnitInitialize: the AU renders into the host's buffers only
 *   notes-async FRAME COUNT   from FRAME a thread of its own sends instance 0 COUNT Note Ons, each with its
 *                     Note Off, one every 2 ms, at offset 0 (MusicDevice.h: a thread that is not the
 *                     render thread), while the blocks run: Note On k of pitch 36 + k / 100 and velocity
 *                     1 + k % 100, channel 1
 *   set-end ID VALUE  AudioUnitSetParameter of ID to VALUE (0..its max) after the last block, with no
 *                     render after it, before the state-out
 *   offline FRAME 0|1 set kAudioUnitProperty_OfflineRender of every instance at FRAME, while it plays
 *   ramp FRAME ID FROM TO FRAMES   a scheduled ramp of parameter ID from FROM to TO (its AU values,
 *                     the device's numbers) over FRAMES frames from FRAME, given in FRAME's block (the
 *                     script has no MIDI for it: a case compares the output with points of the VST3)
 *   info              print the AU's parameters (each value's string read back), outputs, latency and
 *                     tail before playing
 *   checks            before playing, what auval checks of an instrument and can be checked here:
 *                     initialized again at another rate (the latency follows), every writable
 *                     parameter's minimum, maximum and default set and scheduled and read back, the
 *                     Engine refusing a set, a render of more frames than the maximum refused, the
 *                     ClassInfo read and given back
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <chrono>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "host_plan.h"
#include "plug_archive.h"
#include "plug_au.h"
#include "plugin_points.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace hostplan;

namespace {

const CFStringRef kStateKey = CFSTR ("lichomat-state");	/* au_unit.cpp's */

/* the host's own plan commands */
struct RampCmd
{
	uint64_t frame;
	uint32_t id;
	float from, to;
	uint32_t frames;
};

struct AuPlan
{
	std::vector<std::pair<uint64_t, uint64_t>> pull2;
	std::vector<RampCmd> ramps;
	std::vector<std::pair<uint64_t, UInt32>> offline;
	std::vector<std::pair<uint32_t, int>> setsAtEnd;
	std::vector<std::pair<uint64_t, int>> notesAsync;
	UInt32 outputs = 2;
	bool info = false, checks = false, noalloc = false;
};

/* the transport of the block being rendered, for the host callbacks */
struct HostTime
{
	plug_transport_t t {};
	double sample = 0.0;
	bool changed = false;
} gTime;

OSStatus beatAndTempo (void* user, Float64* beat, Float64* tempo)
{
	const HostTime* h = static_cast<const HostTime*> (user);
	if (beat)
		*beat = h->t.ppq;
	if (tempo)
		*tempo = h->t.tempo;
	return noErr;
}

OSStatus transportState2 (void* user, Boolean* playing, Boolean* recording, Boolean* changed, Float64* sample,
                          Boolean* cycling, Float64* cycleStart, Float64* cycleEnd)
{
	const HostTime* h = static_cast<const HostTime*> (user);
	if (playing)
		*playing = h->t.playing != 0;
	if (recording)
		*recording = false;
	if (changed)
		*changed = h->changed;
	if (sample)
		*sample = h->sample;
	if (cycling)
		*cycling = h->t.cycle != 0;
	if (cycleStart)
		*cycleStart = h->t.cycleStart;
	if (cycleEnd)
		*cycleEnd = h->t.cycleEnd;
	return noErr;
}

OSStatus transportState (void* user, Boolean* playing, Boolean* changed, Float64* sample, Boolean* cycling,
                         Float64* cycleStart, Float64* cycleEnd)
{
	return transportState2 (user, playing, nullptr, changed, sample, cycling, cycleStart, cycleEnd);
}

std::string cfText (CFStringRef s)
{
	char b[256] = "";
	if (s)
		CFStringGetCString (s, b, sizeof b, kCFStringEncodingUTF8);
	return b;
}

OSType fourcc (const std::string& s)
{
	if (s.size () != 4)
		die ("a four-character code is \"" + s + "\"");
	return (OSType)((uint8_t)s[0] << 24 | (uint8_t)s[1] << 16 | (uint8_t)s[2] << 8 | (uint8_t)s[3]);
}

/* the bundle's one AudioComponents entry, registered for this process */
AudioComponent load (const std::string& path, AudioComponentDescription& desc)
{
	CFURLRef url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*)path.c_str (), (CFIndex)path.size (), true);
	CFBundleRef bundle = url ? CFBundleCreate (nullptr, url) : nullptr;
	if (url)
		CFRelease (url);
	if (!bundle)
		die ("cannot open the bundle " + path);
	CFArrayRef comps = (CFArrayRef)CFBundleGetValueForInfoDictionaryKey (bundle, CFSTR ("AudioComponents"));
	if (!comps || CFGetTypeID (comps) != CFArrayGetTypeID () || CFArrayGetCount (comps) != 1)
		die ("the bundle has not one AudioComponents entry");
	CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex (comps, 0);
	auto text = [d] (CFStringRef key) {
		CFTypeRef v = CFDictionaryGetValue (d, key);
		return v && CFGetTypeID (v) == CFStringGetTypeID () ? cfText ((CFStringRef)v) : std::string ();
	};
	SInt32 version = 0;
	CFTypeRef v = CFDictionaryGetValue (d, CFSTR ("version"));
	if (!v || CFGetTypeID (v) != CFNumberGetTypeID () || !CFNumberGetValue ((CFNumberRef)v, kCFNumberSInt32Type, &version))
		die ("the AudioComponents entry has no version");
	desc = AudioComponentDescription {fourcc (text (CFSTR ("type"))), fourcc (text (CFSTR ("subtype"))),
	                                  fourcc (text (CFSTR ("manufacturer"))), 0, 0};
	const std::string name = text (CFSTR ("name")), factory = text (CFSTR ("factoryFunction"));
	CFErrorRef err = nullptr;
	if (!CFBundleLoadExecutableAndReturnError (bundle, &err))
		die ("cannot load the bundle's executable");
	CFStringRef fn = CFStringCreateWithCString (nullptr, factory.c_str (), kCFStringEncodingUTF8);
	void* f = CFBundleGetFunctionPointerForName (bundle, fn);
	CFRelease (fn);
	if (!f)
		die ("the bundle exports no " + factory);
	CFStringRef cname = CFStringCreateWithCString (nullptr, name.c_str (), kCFStringEncodingUTF8);
	AudioComponent c = AudioComponentRegister (&desc, cname, (UInt32)version, (AudioComponentFactoryFunction)f);
	CFRelease (cname);
	if (!c)
		die ("AudioComponentRegister refused the component");
	printf ("%s: component %s %s %s \"%s\", version %d.%d.%d, factory %s\n", gName, text (CFSTR ("type")).c_str (),
	        text (CFSTR ("subtype")).c_str (), text (CFSTR ("manufacturer")).c_str (), name.c_str (), version >> 16,
	        (version >> 8) & 255, version & 255, factory.c_str ());
	return c;
}

/* the values the host's listener heard of, by parameter id */
struct Heard
{
	std::mutex m;
	std::map<uint32_t, int> count;
};

struct Instance
{
	AudioComponentInstance au = nullptr;
	double engine = 0.0;	/* the Engine parameter: 2 running */
	std::vector<float> l, r, l2, r2;
	std::vector<float> out[4];	/* a block of L1 R1 L2 R2 */
	uint32_t latency = 0;
	plug_params_t mirror {};	/* the plugin's step with its engine, for the script (plug_params.h) */
	ParameterChanges points {64};	/* the block's points of the host, as the processor takes them */
	std::vector<std::pair<uint32_t, float>> echo;	/* reported values for the next block */
	float shown[PLUG_PARAM_COUNT] {};	/* the AU's values the host knows */
	bool first = true;
	int waits = 0;
	AUEventListenerRef listener = nullptr;
	std::shared_ptr<Heard> heard = std::make_shared<Heard> ();
};

void set (Instance& in, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement el, const void* v, UInt32 size,
          const char* what)
{
	const OSStatus r = AudioUnitSetProperty (in.au, id, scope, el, v, size);
	if (r != noErr)
		die (std::string (what) + " refused: " + std::to_string ((int)r));
}

/* the ClassInfo of a state: the plugin's bytes with the keys every AU's state has */
CFDictionaryRef classInfo (const AudioComponentDescription& desc, const std::vector<uint8_t>& state)
{
	CFMutableDictionaryRef d = CFDictionaryCreateMutable (nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
	                                                      &kCFTypeDictionaryValueCallBacks);
	const SInt32 nums[4] = {0, (SInt32)desc.componentType, (SInt32)desc.componentSubType, (SInt32)desc.componentManufacturer};
	const CFStringRef keys[4] = {CFSTR ("version"), CFSTR ("type"), CFSTR ("subtype"), CFSTR ("manufacturer")};
	for (int k = 0; k < 4; k++)
	{
		CFNumberRef n = CFNumberCreate (nullptr, kCFNumberSInt32Type, &nums[k]);
		CFDictionarySetValue (d, keys[k], n);
		CFRelease (n);
	}
	CFDictionarySetValue (d, CFSTR ("name"), CFSTR ("Untitled"));
	CFDataRef data = CFDataCreate (nullptr, state.data (), (CFIndex)state.size ());
	CFDictionarySetValue (d, kStateKey, data);
	CFRelease (data);
	return d;
}

std::vector<uint8_t> cardState (const std::string& card)
{
	std::vector<uint8_t> out;
	char err[512];
	auto put = [] (void* ctx, const void* p, size_t n) {
		auto* v = static_cast<std::vector<uint8_t>*> (ctx);
		v->insert (v->end (), static_cast<const uint8_t*> (p), static_cast<const uint8_t*> (p) + n);
		return 0;
	};
	if (plug_archiveWrite (card.c_str (), put, &out, err, sizeof err) != 0)
		die (err);
	return out;
}

float param (const Instance& in, AudioUnitParameterID id)
{
	AudioUnitParameterValue v = 0.0f;
	if (AudioUnitGetParameter (in.au, id, kAudioUnitScope_Global, 0, &v) != noErr)
		die ("AudioUnitGetParameter " + std::to_string (id));
	return v;
}

/* the AU's parameters, outputs, latency and tail */
void printInfo (const Instance& in, double rate)
{
	UInt32 size = 0;
	Boolean w = false;
	if (AudioUnitGetPropertyInfo (in.au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, &w) != noErr)
		die ("no parameter list");
	std::vector<AudioUnitParameterID> ids (size / sizeof (AudioUnitParameterID));
	AudioUnitGetProperty (in.au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data (), &size);
	for (AudioUnitParameterID id : ids)
	{
		AudioUnitParameterInfo p {};
		size = sizeof p;
		if (AudioUnitGetProperty (in.au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &p, &size) != noErr)
			die ("no ParameterInfo of " + std::to_string (id));
		std::string strings = "-";
		CFArrayRef a = nullptr;
		size = sizeof a;
		if (AudioUnitGetProperty (in.au, kAudioUnitProperty_ParameterValueStrings, kAudioUnitScope_Global, id, &a, &size) == noErr && a)
		{
			strings.clear ();
			for (CFIndex k = 0; k < CFArrayGetCount (a); k++)
				strings += (k ? "|" : "") + cfText ((CFStringRef)CFArrayGetValueAtIndex (a, k));
			CFRelease (a);
		}
		std::string shows;
		for (float v : {p.minValue, p.defaultValue, p.maxValue})
		{
			AudioUnitParameterStringFromValue s {id, &v, nullptr};
			size = sizeof s;
			if (AudioUnitGetProperty (in.au, kAudioUnitProperty_ParameterStringFromValue, kAudioUnitScope_Global, 0, &s, &size) != noErr
			    || !s.outString)
				die ("no ParameterStringFromValue of " + std::to_string (id));
			shows += (shows.empty () ? "" : "|") + cfText (s.outString);
			AudioUnitParameterValueFromString back {id, s.outString, 0.0f};
			size = sizeof back;
			if ((p.flags & kAudioUnitParameterFlag_IsWritable)
			    && (AudioUnitGetProperty (in.au, kAudioUnitProperty_ParameterValueFromString, kAudioUnitScope_Global, 0, &back,
			                              &size) != noErr
			        || back.outValue != v))
				die ("ParameterValueFromString of " + std::to_string (id) + " does not give " + std::to_string (v) + " back");
			CFRelease (s.outString);
		}
		const std::string name = (p.flags & kAudioUnitParameterFlag_HasCFNameString) ? cfText (p.cfNameString) : p.name;
		if ((p.flags & kAudioUnitParameterFlag_CFNameRelease) && p.cfNameString)
			CFRelease (p.cfNameString);
		printf ("%s: info: param %u \"%s\" %g..%g default %g unit %u %s%s strings %s shows %s\n", gName, (unsigned)id,
		        name.c_str (), p.minValue, p.maxValue, p.defaultValue, (unsigned)p.unit,
		        (p.flags & kAudioUnitParameterFlag_IsWritable) ? "rw" : "r",
		        (p.flags & kAudioUnitParameterFlag_ValuesHaveStrings) ? "s" : "", strings.c_str (), shows.c_str ());
	}
	UInt32 outs = 0;
	size = sizeof outs;
	AudioUnitGetProperty (in.au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Output, 0, &outs, &size);
	std::string names;
	for (UInt32 b = 0; b < outs; b++)
	{
		CFStringRef n = nullptr;
		size = sizeof n;
		if (AudioUnitGetProperty (in.au, kAudioUnitProperty_ElementName, kAudioUnitScope_Output, b, &n, &size) == noErr && n)
		{
			names += (b ? ", " : "") + cfText (n);
			CFRelease (n);
		}
	}
	AUChannelInfo ch[4] {};
	size = sizeof ch;
	AudioUnitGetProperty (in.au, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, ch, &size);
	Float64 lat = 0.0, tail = 0.0;
	size = sizeof lat;
	AudioUnitGetProperty (in.au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &lat, &size);
	size = sizeof tail;
	AudioUnitGetProperty (in.au, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &tail, &size);
	printf ("%s: info: outputs %u: %s; channels %d in, %d out; kAudioUnitProperty_Latency %.0f frames; TailTime %g s\n",
	        gName, (unsigned)outs, names.c_str (), ch[0].inChannels, ch[0].outChannels, lat * rate, tail);
}

/* a component at the plan's rate and block, with its state, initialized */
void open (Instance& in, AudioComponent comp, const AudioComponentDescription& desc, const Plan& p, const AuPlan& a)
{
	if (AudioComponentInstanceNew (comp, &in.au) != noErr)
		die ("AudioComponentInstanceNew failed");
	if (a.outputs != 2)
		set (in, kAudioUnitProperty_ElementCount, kAudioUnitScope_Output, 0, &a.outputs, sizeof a.outputs, "ElementCount");
	AudioStreamBasicDescription f {};
	f.mSampleRate = p.rate;
	f.mFormatID = kAudioFormatLinearPCM;
	f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
	f.mBytesPerPacket = f.mBytesPerFrame = 4;
	f.mFramesPerPacket = 1;
	f.mChannelsPerFrame = 2;
	f.mBitsPerChannel = 32;
	for (UInt32 b = 0; b < a.outputs; b++)
		set (in, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, b, &f, sizeof f, "StreamFormat");
	if (a.noalloc)
	{
		const UInt32 no = 0;
		for (UInt32 b = 0; b < a.outputs; b++)
			set (in, kAudioUnitProperty_ShouldAllocateBuffer, kAudioUnitScope_Output, b, &no, sizeof no, "ShouldAllocateBuffer");
	}
	const UInt32 maxFrames = (UInt32)p.block;
	set (in, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof maxFrames,
	     "MaximumFramesPerSlice");
	HostCallbackInfo cb {&gTime, beatAndTempo, nullptr, transportState, transportState2};
	set (in, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &cb, sizeof cb, "HostCallbacks");
	const UInt32 offline = p.offline ? 1 : 0;
	set (in, kAudioUnitProperty_OfflineRender, kAudioUnitScope_Global, 0, &offline, sizeof offline, "OfflineRender");
	std::vector<uint8_t> state;
	if (!p.stateIn.empty ())
		state = readFile (p.stateIn);
	else if (!p.card.empty ())
		state = cardState (p.card);
	if (!state.empty ())
	{
		CFDictionaryRef ci = classInfo (desc, state);
		const OSStatus r = AudioUnitSetProperty (in.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &ci, sizeof ci);
		CFRelease (ci);
		if (r != noErr)
			die ("setState refused the card");
	}
	if (AudioUnitInitialize (in.au) != noErr)
		die ("AudioUnitInitialize failed");
	Float64 lat = 0.0;
	UInt32 size = sizeof lat;
	AudioUnitGetProperty (in.au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &lat, &size);
	in.latency = (uint32_t)std::lround (lat * p.rate);
	for (int b = 0; b < 4; b++)
		in.out[b].assign ((size_t)p.block + 1, 0.0f);	/* + 1: the checks' render of one frame too many */
	/* the listener, as a host's view would have it */
	std::shared_ptr<Heard> heard = in.heard;
	AUEventListenerCreateWithDispatchQueue (&in.listener, 0.0f, 0.0f, dispatch_get_global_queue (QOS_CLASS_DEFAULT, 0),
	                                       ^(void*, const AudioUnitEvent* e, UInt64, AudioUnitParameterValue) {
		                                       std::lock_guard<std::mutex> g (heard->m);
		                                       heard->count[e->mArgument.mParameter.mParameterID]++;
	                                       });
	for (unsigned i = 0; i <= PLUG_PARAM_COUNT; i++)
	{
		AudioUnitEvent e {};
		e.mEventType = kAudioUnitEvent_ParameterValueChange;
		e.mArgument.mParameter = {in.au, i < PLUG_PARAM_COUNT ? plug_params[i].id : PLUG_AU_ENGINE, kAudioUnitScope_Global, 0};
		AUEventListenerAddEventType (in.listener, nullptr, &e);
	}
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		in.shown[i] = param (in, plug_params[i].id);
	in.engine = 0.0;
	in.first = true;
	plug_paramsInit (&in.mirror, 0);
	in.echo.clear ();
}

void close (Instance& in)
{
	if (in.listener)
		AUListenerDispose (in.listener);
	in.listener = nullptr;
	AudioUnitUninitialize (in.au);
	AudioComponentInstanceDispose (in.au);
	in.au = nullptr;
}

/* Out 1 (bus 0) or Out 2 (bus 1) of a block of n frames at sample time t into in.out */
OSStatus renderBus (Instance& in, UInt32 bus, UInt32 n, Float64 t)
{
	struct
	{
		AudioBufferList list;
		AudioBuffer second;
	} b {};
	b.list.mNumberBuffers = 2;
	for (UInt32 c = 0; c < 2; c++)
	{
		AudioBuffer& x = c == 0 ? b.list.mBuffers[0] : b.second;
		x.mNumberChannels = 1;
		x.mDataByteSize = n * 4u;
		x.mData = in.out[2 * bus + c].data ();
	}
	AudioTimeStamp ts {};
	ts.mSampleTime = t;
	ts.mFlags = kAudioTimeStampSampleTimeValid;
	AudioUnitRenderActionFlags flags = 0;
	return AudioUnitRender (in.au, &flags, &ts, bus, n, &b.list);
}

/* what auval checks of an instrument and this host can check (the plan's checks) */
void runChecks (Instance& in, const Plan& p, const AuPlan& a)
{
	int passed = 0;
	auto expect = [&passed] (bool ok, const std::string& what) {
		if (!ok)
			die ("checks: " + what);
		passed++;
	};
	/* initialized again at another rate and back: the latency follows the rate */
	for (double rate : {p.rate == 48000.0 ? 96000.0 : 48000.0, p.rate})
	{
		expect (AudioUnitUninitialize (in.au) == noErr, "AudioUnitUninitialize");
		AudioStreamBasicDescription f {};
		UInt32 size = sizeof f;
		AudioUnitGetProperty (in.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, &size);
		f.mSampleRate = rate;
		for (UInt32 b = 0; b < a.outputs; b++)
			set (in, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, b, &f, sizeof f, "StreamFormat");
		expect (AudioUnitInitialize (in.au) == noErr, "AudioUnitInitialize at " + std::to_string (rate));
		Float64 lat = 0.0;
		size = sizeof lat;
		AudioUnitGetProperty (in.au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &lat, &size);
		const uint32_t want = plug_latency (rate, (uint32_t)p.block, plug_marginFromEnv ());
		expect (std::lround (lat * rate) == (long)want, "the latency at " + std::to_string (rate));
	}
	/* every writable parameter: its minimum, maximum and default, set and scheduled, read back */
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		const AudioUnitParameterID id = plug_params[i].id;
		AudioUnitParameterInfo info {};
		UInt32 size = sizeof info;
		AudioUnitGetProperty (in.au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &size);
		if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) && info.cfNameString)
			CFRelease (info.cfNameString);
		for (float v : {info.minValue, info.maxValue, info.defaultValue})
		{
			expect (AudioUnitSetParameter (in.au, id, kAudioUnitScope_Global, 0, v, 0) == noErr && param (in, id) == v,
			        "set " + std::to_string (id));
			AudioUnitParameterEvent e {};
			e.scope = kAudioUnitScope_Global;
			e.parameter = id;
			e.eventType = kParameterEvent_Immediate;
			e.eventValues.immediate.value = v;
			expect (AudioUnitScheduleParameters (in.au, &e, 1) == noErr && param (in, id) == v, "scheduled " + std::to_string (id));
		}
		in.shown[i] = info.defaultValue;
	}
	expect (AudioUnitSetParameter (in.au, PLUG_AU_ENGINE, kAudioUnitScope_Global, 0, 1.0f, 0) != noErr, "the Engine took a set");
	/* more frames than the maximum: refused */
	expect (renderBus (in, 0, (UInt32)p.block + 1, -1000000.0) == kAudioUnitErr_TooManyFramesToProcess, "a render too long");
	/* the ClassInfo read and given back */
	CFPropertyListRef ci = nullptr;
	UInt32 size = sizeof ci;
	expect (AudioUnitGetProperty (in.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &ci, &size) == noErr && ci,
	        "the ClassInfo read");
	expect (AudioUnitSetProperty (in.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &ci, sizeof ci) == noErr,
	        "the ClassInfo given back");
	CFRelease (ci);
	printf ("%s: checks: %d passed\n", gName, passed);
}

/* 0-frame renders every 10 ms (each its own time stamp, before frame 0) until the engine runs */
void waitRunning (Instance& in)
{
	const auto t0 = std::chrono::steady_clock::now ();
	while (in.engine < 1.5)
	{
		renderBus (in, 0, 0, -1.0 - in.waits++);
		in.engine = param (in, PLUG_AU_ENGINE);
		if (std::chrono::steady_clock::now () - t0 > std::chrono::seconds (60))
			die ("the engine did not run within 60 s");
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
}

/* the values the AU shows after the block at frame: printed (every one after the first block, then
 * each that changed), into the mirror as the plugin's report, and kept for an echo */
void readReports (Instance& in, size_t i, uint64_t frame, bool echo)
{
	std::string line;
	in.engine = param (in, PLUG_AU_ENGINE);
	for (unsigned k = 0; k < PLUG_PARAM_COUNT; k++)
	{
		const float v = param (in, plug_params[k].id);
		if (!in.first && v == in.shown[k])
			continue;
		in.shown[k] = v;
		const uint8_t plain = plug_auPlain (k, v);
		line += " " + std::to_string (plug_params[k].id) + "=" + std::to_string (plain);
		in.mirror.p[k].want = in.mirror.p[k].sent = plain;
		in.mirror.p[k].shown = 1;
		if (echo)
			in.echo.push_back ({plug_params[k].id, v});
	}
	in.first = false;
	if (!line.empty ())
		printf ("%s: instance %zu: frame %llu:%s\n", gName, i, (unsigned long long)frame, line.c_str ());
}

std::vector<uint8_t> stateOf (Instance& in)
{
	CFPropertyListRef ci = nullptr;
	UInt32 size = sizeof ci;
	if (AudioUnitGetProperty (in.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &ci, &size) != noErr || !ci)
		die ("getState failed");
	CFDictionaryRef d = (CFDictionaryRef)ci;
	std::vector<CFStringRef> keys ((size_t)CFDictionaryGetCount (d));
	CFDictionaryGetKeysAndValues (d, (const void**)keys.data (), nullptr);
	std::vector<std::string> names;
	for (CFStringRef k : keys)
		names.push_back (cfText (k));
	std::sort (names.begin (), names.end ());
	std::string all;
	for (auto& n : names)
		all += " " + n;
	printf ("%s: classinfo keys:%s\n", gName, all.c_str ());
	CFDataRef data = (CFDataRef)CFDictionaryGetValue (d, kStateKey);
	if (!data || CFGetTypeID (data) != CFDataGetTypeID ())
		die ("the ClassInfo has no " + cfText (kStateKey));
	std::vector<uint8_t> out (CFDataGetBytePtr (data), CFDataGetBytePtr (data) + CFDataGetLength (data));
	CFRelease (ci);
	return out;
}

} // namespace

int main (int argc, char** argv)
{
	gName = "lichomat-auhost";
	if (argc != 2)
		die ("usage: lichomat-auhost PLAN");
	AuPlan a;
	const Plan p = readPlan (argv[1], [&a] (const std::string& cmd, std::istringstream& s, bool& ok) {
		if (cmd == "pull2")
		{
			uint64_t from = 0, to = 0;
			ok = !!(s >> from >> to) && from < to;
			a.pull2.push_back ({from, to});
		}
		else if (cmd == "offline")
		{
			uint64_t at = 0;
			UInt32 on = 0;
			ok = !!(s >> at >> on) && on <= 1;
			a.offline.push_back ({at, on});
		}
		else if (cmd == "ramp")
		{
			RampCmd r {};
			ok = !!(s >> r.frame >> r.id >> r.from >> r.to >> r.frames) && plug_paramIndex (r.id) >= 0 && r.frames > 0;
			a.ramps.push_back (r);
		}
		else if (cmd == "outputs")
			ok = !!(s >> a.outputs) && (a.outputs == 1 || a.outputs == 2);
		else if (cmd == "info")
			a.info = true;
		else if (cmd == "noalloc")
			a.noalloc = true;
		else if (cmd == "notes-async")
		{
			uint64_t at = 0;
			int count = 0;
			ok = !!(s >> at >> count) && count > 0 && count <= 1000;
			a.notesAsync.push_back ({at, count});
		}
		else if (cmd == "set-end")
		{
			uint32_t id = 0;
			int v = -1;
			ok = !!(s >> id >> v) && plug_paramIndex (id) >= 0 && v >= 0 && v <= plug_params[plug_paramIndex (id)].max;
			a.setsAtEnd.push_back ({id, v});
		}
		else if (cmd == "checks")
			a.checks = true;
		else
			return false;
		return true;
	});
	/* lichomat-plughost's commands of the VST3's own interfaces (plan E9.4's reviews): AU v2 has no flush, no null
	 * buffers of a bus, no edit controller; the event ring and the states from another thread or before any block are
	 * the VST3 host's checks of the processor. Not played here */
	if (!p.flushes.empty () || !p.emptyFlushes.empty () || !p.rawPoints.empty () || !p.controller.empty ()
	    || !p.stateEarly.empty () || !p.noOutputs.empty () || p.nullOut2 || p.nullOut2Left || !p.nullOut1.empty ()
	    || !p.asyncSets.empty ())
		die ("plan: a command of lichomat-plughost alone (flush, flush-empty, param-at, controller, state-early, no-outputs, "
		     "null-out2, null-out2-left, null-out1, setstate-async)");
	AudioComponentDescription desc {};
	const AudioComponent comp = load (p.bundle, desc);
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
	RingTap tap;
	for (int i = 0; i < p.instances; i++)
	{
		const std::set<int> before = i == 0 && !p.events.empty () ? shmDescriptors () : std::set<int> ();
		inst.push_back (std::make_unique<Instance> ());
		open (*inst.back (), comp, desc, p, a);
		if (i == 0 && !p.events.empty ())
			tap.find (before);
	}
	if (a.info)
		printInfo (*inst[0], p.rate);
	if (a.checks)
		runChecks (*inst[0], p, a);
	for (auto& in : inst)
		waitRunning (*in);
	/* the timeline */
	Timeline tl (p);
	plug_clock_t expect;
	plug_clockInit (&expect, p.rate);
	std::map<uint64_t, std::vector<std::string>> script;
	size_t nextNote = 0, nextKill = 0, nextHang = 0, nextRenew = 0, nextSet = 0, nextParam = 0, nextOffline = 0, nextRamp = 0;
	size_t nextAsyncNotes = 0;
	std::vector<std::thread> threads;
	unsigned long long processCalls = 0;
	static AudioUnitParameterEvent events[1024];
	bool wasPlaying = false;
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
			open (in, comp, desc, p, a);
			nextRenew++;
		}
		while (nextSet < p.setstates.size () && p.setstates[nextSet].first <= f)
		{
			CFDictionaryRef ci = classInfo (desc, readFile (p.setstates[nextSet].second));
			const auto w0 = std::chrono::steady_clock::now ();
			const OSStatus r = AudioUnitSetProperty (inst[0]->au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &ci,
			                                         sizeof ci);
			CFRelease (ci);
			printf ("%s: setstate at %llu: %s in %.3f s\n", gName, (unsigned long long)f, r == noErr ? "ok" : "refused",
			        std::chrono::duration<double> (std::chrono::steady_clock::now () - w0).count ());
			nextSet++;
		}
		while (nextAsyncNotes < a.notesAsync.size () && a.notesAsync[nextAsyncNotes].first <= f)
		{
			/* a host's other thread (a MIDI input, a UI keyboard): at offset 0, while this thread renders */
			const int count = a.notesAsync[nextAsyncNotes].second;
			const uint64_t at = f;
			AudioUnit u = inst[0]->au;
			threads.emplace_back ([u, count, at] {
				int refused = 0;
				for (int k = 0; k < count; k++)
				{
					const UInt32 pitch = 36u + (UInt32)k / 100u, vel = 1u + (UInt32)k % 100u;
					refused += MusicDeviceMIDIEvent (u, 0x90, pitch, vel, 0) != noErr;
					refused += MusicDeviceMIDIEvent (u, 0x80, pitch, 0, 0) != noErr;
					std::this_thread::sleep_for (std::chrono::milliseconds (2));
				}
				printf ("%s: notes-async at %llu: %d Note Ons and Offs sent, %d refused\n", gName, (unsigned long long)at, count,
				        refused);
			});
			nextAsyncNotes++;
		}
		while (nextOffline < a.offline.size () && a.offline[nextOffline].first <= f)
		{
			for (auto& in : inst)
				set (*in, kAudioUnitProperty_OfflineRender, kAudioUnitScope_Global, 0, &a.offline[nextOffline].second,
				     sizeof (UInt32), "OfflineRender");
			printf ("%s: offline at %llu: %u\n", gName, (unsigned long long)f, (unsigned)a.offline[nextOffline].second);
			nextOffline++;
		}
		gTime.t = tl.block (f, n);
		gTime.sample = (double)f;
		gTime.changed = (gTime.t.playing != 0) != wasPlaying;
		wasPlaying = gTime.t.playing != 0;
		/* the expected MIDI: the same generator, the same note conversion (plugin_processor.cpp) */
		plug_midi_t m[1024];
		const unsigned nm = plug_clockBlock (&expect, &gTime.t, n, m, 1024);
		auto put = [&script, f] (const plug_midi_t& x) {
			char b[16];
			snprintf (b, sizeof b, x.len == 3 ? "%02X %02X %02X" : x.len == 2 ? "%02X %02X" : "%02X", x.b[0], x.b[1], x.b[2]);
			script[f + x.offset].push_back (b);
		};
		for (unsigned i = 0; i < nm; i++)
			put (m[i]);
		/* the parameters: the echo of the last block's reports (set), then the plan's changes in this
		 * block (scheduled); the MIDI the plugin must give its engine, from instance 0's mirror */
		for (auto& in : inst)
			in->points.clearQueue ();
		for (auto& in : inst)
			for (auto& [id, v] : in->echo)
			{
				const int k = plug_paramIndex (id);
				int32 index = 0, point = 0;
				if (IParamValueQueue* q = in->points.addParameterData (id, index))
					q->addPoint (0, (double)plug_auPlain ((unsigned)k, v) / plug_params[k].max, point);
			}
		unsigned ne = 0;
		while (nextParam < p.params.size () && p.params[nextParam].frame < f + n && ne < 1024)
		{
			const ParamCmd& c = p.params[nextParam++];
			const int k = plug_paramIndex (c.id);
			AudioUnitParameterEvent& e = events[ne++];
			e = AudioUnitParameterEvent {};
			e.scope = kAudioUnitScope_Global;
			e.element = 0;
			e.parameter = c.id;
			e.eventType = kParameterEvent_Immediate;
			e.eventValues.immediate.bufferOffset = (UInt32)(c.frame - f);
			e.eventValues.immediate.value = plug_auValue ((unsigned)k, (uint8_t)c.value);
			for (auto& in : inst)
			{
				int32 index = 0, point = 0;
				if (IParamValueQueue* q = in->points.addParameterData (c.id, index))
					q->addPoint ((int32)(c.frame - f), (double)c.value / plug_params[k].max, point);
				in->shown[k] = e.eventValues.immediate.value;	/* the host's own value, not a report */
			}
		}
		while (nextRamp < a.ramps.size () && a.ramps[nextRamp].frame < f + n && ne < 1024)
		{
			const RampCmd& r = a.ramps[nextRamp++];
			AudioUnitParameterEvent& e = events[ne++];
			e = AudioUnitParameterEvent {};
			e.scope = kAudioUnitScope_Global;
			e.parameter = r.id;
			e.eventType = kParameterEvent_Ramped;
			e.eventValues.ramp.startBufferOffset = (SInt32)(r.frame - f);
			e.eventValues.ramp.durationInFrames = r.frames;
			e.eventValues.ramp.startValue = r.from;
			e.eventValues.ramp.endValue = r.to;
			for (auto& in : inst)
				in->shown[plug_paramIndex (r.id)] = r.to;	/* the host's own value, not a report */
		}
		{
			static plug_paramPoint_t pts[1024];
			static plug_midi_t pm[1024];
			const unsigned np = lichomat::pointsOf (&inst[0]->points, (int32)n, pts, 1024);
			const unsigned nq = plug_paramsHost (&inst[0]->mirror, pts, np, f, (uint8_t)p.channel, pm, 0, 1024);
			for (unsigned i = 0; i < nq; i++)
				put (pm[i]);
		}
		/* the notes, as the MIDI the host sends and as the script's lines */
		static uint8_t notes[1024][3];
		static UInt32 noteAt[1024];
		unsigned nn = 0;
		while (nextNote < p.notes.size () && p.notes[nextNote].frame < f + n && nn < 1024)
		{
			const NoteCmd& c = p.notes[nextNote++];
			notes[nn][0] = (uint8_t)((c.vel > 0 ? 0x90 : 0x80) | (c.ch & 15));
			notes[nn][1] = (uint8_t)(c.pitch & 0x7F);
			notes[nn][2] = (uint8_t)c.vel;
			noteAt[nn++] = (UInt32)(c.frame - f);
			char b[16];
			snprintf (b, sizeof b, "%02X %02X %02X", (unsigned)((c.vel > 0 ? 0x90 : 0x80) | (c.ch & 15)), (unsigned)(c.pitch & 0x7F),
			          (unsigned)c.vel);
			script[c.frame].push_back (b);
		}
		bool pull2 = p.bus2;
		for (auto& [from, to] : a.pull2)
			pull2 = pull2 || (f >= from && f < to);
		for (size_t idx = 0; idx < inst.size (); idx++)
		{
			Instance& in = *inst[idx];
			/* a host's buffers hold anything: the plugin must write every frame (P10_plugin_native) */
			for (auto& b : in.out)
				std::fill (b.begin (), b.begin () + n, 0.5f);
			if (rtInside)
				rtInside (1);
			for (auto& [id, v] : in.echo)
				AudioUnitSetParameter (in.au, id, kAudioUnitScope_Global, 0, v, 0);
			if (ne)
				AudioUnitScheduleParameters (in.au, events, ne);
			for (unsigned k = 0; k < nn; k++)
				MusicDeviceMIDIEvent (in.au, notes[k][0], notes[k][1], notes[k][2], noteAt[k]);
			OSStatus r = renderBus (in, 0, n, (Float64)f);
			if (r == noErr && pull2)
				r = renderBus (in, 1, n, (Float64)f);
			if (rtInside)
				rtInside (0);
			if (r != noErr)
				die ("AudioUnitRender failed: " + std::to_string ((int)r));
			processCalls++;
			if (tap.s && idx == 0)
				tap.read ();
			in.echo.clear ();
			readReports (in, idx, f, p.echo);
			in.l.insert (in.l.end (), in.out[0].begin (), in.out[0].begin () + n);
			in.r.insert (in.r.end (), in.out[1].begin (), in.out[1].begin () + n);
			if (p.bus2 || !a.pull2.empty ())
			{
				in.l2.insert (in.l2.end (), in.out[2].begin (), in.out[2].begin () + n);
				in.r2.insert (in.r2.end (), in.out[3].begin (), in.out[3].begin () + n);
				if (!pull2)
					std::fill (in.l2.end () - n, in.l2.end (), 0.0f), std::fill (in.r2.end () - n, in.r2.end (), 0.0f);
			}
		}
	}
	for (auto& th : threads)
		th.join ();
	for (auto& [id, v] : a.setsAtEnd)
		for (auto& in : inst)
			if (AudioUnitSetParameter (in->au, id, kAudioUnitScope_Global, 0, plug_auValue ((unsigned)plug_paramIndex (id), (uint8_t)v), 0)
			    != noErr)
				die ("set-end " + std::to_string (id) + " refused");
	if (!p.stateOut.empty ())
	{
		const std::vector<uint8_t> st = stateOf (*inst[0]);
		std::ofstream o (p.stateOut, std::ios::binary);
		o.write ((const char*)st.data (), (std::streamsize)st.size ());
	}
	std::this_thread::sleep_for (std::chrono::milliseconds (100));	/* the plugin's notifier, every 20 ms */
	for (size_t i = 0; i < inst.size (); i++)
	{
		const std::string base = p.out.substr (0, p.out.size () - 4) + (i == 0 ? "" : "-" + std::to_string (i));
		writeWav (base + ".wav", inst[i]->l, inst[i]->r, p.rate);
		if (p.bus2 || !a.pull2.empty ())
			writeWav (base + "-out2.wav", inst[i]->l2, inst[i]->r2, p.rate);
		static const char* const states[4] = {"stopped", "starting", "running", "failed"};
		printf ("%s: instance %zu: latency %u, %zu frames, engine %s\n", gName, i, inst[i]->latency, inst[i]->l.size (),
		        states[std::lround (inst[i]->engine) & 3]);
		{
			std::lock_guard<std::mutex> g (inst[i]->heard->m);
			std::string heard;
			for (auto& [id, k] : inst[i]->heard->count)
				heard += " " + std::to_string (id) + ":" + std::to_string (k);
			printf ("%s: instance %zu: heard%s\n", gName, i, heard.c_str ());
		}
		close (*inst[i]);
	}
	if (!p.events.empty ())
	{
		FILE* s = fopen (p.events.c_str (), "w");
		if (!s)
			die ("cannot write " + p.events);
		for (auto& [frame, msgs] : tap.lines)
		{
			fprintf (s, "%lluf usb", (unsigned long long)frame);
			for (auto& b : msgs)
				fprintf (s, " %s", b.c_str ());
			fputc ('\n', s);
		}
		fclose (s);
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
		printf ("%s: rtcheck: %llu process calls, %llu forbidden calls, %llu writes\n", gName, processCalls,
		        rtViolations () - probe, rtWrites ());
	return 0;
}
