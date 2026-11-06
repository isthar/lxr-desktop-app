/*
 * plugin_processor.h
 *
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8): two stereo outputs, Out 1 and Out 2
 * (plan E9.4), one event input. Each block it turns the host's transport into MIDI clock
 * (plug_clock.h), the block's notes and the host's parameter changes into MIDI (plug_params.h),
 * writes them into the engine's ring at their frames, publishes the block, wakes the engine, and
 * plays the ring plug_readOffset() frames behind: Out 1 and Out 2 are the device's outputs when
 * the host has activated Out 2, else Out 1 is their sum; the reported latency is plug_latency().
 * The firmware's values that changed by themselves go to the host as output parameter changes.
 * In real time it never waits; offline (kOffline: Live's bounce, freeze and export) it waits for
 * the engine. The state is the card folder as an LCHS archive (plug_archive.h) and the
 * parameters' values after it (plug_params.h).
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

#include "plug_clock.h"
#include "plug_params.h"
#include "plugin_card.h"
#include "plugin_link.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

namespace lichomat {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
	Processor ();
	static Steinberg::FUnknown* create (void*);

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
	                                                  Steinberg::Vst::SpeakerArrangement* outputs,
	                                                  Steinberg::int32 numOuts) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API activateBus (Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir,
	                                           Steinberg::int32 index, Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
	Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE { return latency; }
	Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE { return Steinberg::Vst::kInfiniteTail; }
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	static constexpr unsigned kMaxMidi = 1024;	/**< MIDI messages of one block: clocks, notes, parameters */

private:
	unsigned notesOf (Steinberg::Vst::IEventList* events, Steinberg::int32 frames);
	unsigned paramsOf (Steinberg::Vst::ProcessData& data, plug_shm_t* s, bool running, uint32_t frames, unsigned& owed);
	void showValues (Steinberg::Vst::ProcessData& data);
	void restoreValues (const std::atomic<int>* from);
	void reportState (Steinberg::Vst::ProcessData& data, uint64_t state);
	const std::string& logOf (const CardFolder& f);
	void offlineWait (plug_shm_t* s, uint64_t audioEnd);

	EngineLink link;
	CardFolder folder;
	std::string logPath;	/**< BASE/logs/<id>.log, set when the first engine starts */
	plug_clock_t clock {};
	Steinberg::uint32 latency = 0;
	uint32_t readOffset = 0;
	uint32_t margin = 0;
	uint64_t frames = 0;		/**< host frames since the activation */
	uint64_t runningPid = 0;	/**< the engine taken last (one of the state restored last): the clock and the parameters' */
	uint32_t holdFrames = 0;	/**< 1,024 frames at 48 kHz (21.3 ms) at the host's rate: a value given to the firmware
								 * has settled by then */
	uint64_t resumeAt = 0;		/**< the host frame before which a new engine's slave and the host's notes wait */
	bool firstAwaited = false;	/**< the new engine's first report, still to come, sets resumeAt */
	pid_t waitedInVain = -2;	/**< the engine an offline wait gave up on (-1: none ran): no more waits for it */
	int reported = -1;			/**< the engine state last sent to the controller */
	bool active = false;
	std::atomic<bool> out2Active {false};	/**< the host activated Out 2 */
	plug_params_t params {};	/**< the audio thread's: the parameters' step with the firmware */
	uint8_t channel = PLUG_VALUE_NONE;	/**< the firmware's global MIDI channel, from its last values */
	/** the host's values (params' want), for getState() and a new activation; the audio thread writes them */
	std::atomic<int> hostValues[PLUG_PARAM_COUNT];
	/** the values of the last state set, for the audio thread, which takes them when restoreGen moves */
	std::atomic<int> stateValues[PLUG_PARAM_COUNT];
	std::atomic<uint64_t> restoreGen {0};
	std::atomic<uint64_t> restoreSeen {0};	/**< the audio thread's (and setActive()'s, while it does not run) */
	plug_midi_t clocks[kMaxMidi];
	plug_midi_t notes[kMaxMidi];
	plug_midi_t paramMidi[kMaxMidi];
	plug_paramPoint_t points[kMaxMidi];
};

} // namespace lichomat
