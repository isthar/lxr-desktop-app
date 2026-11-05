/*
 * plugin_processor.h
 *
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8): one stereo output, one event input.
 * Each block it turns the host's transport into MIDI clock (plug_clock.h) and the block's notes
 * into MIDI, writes both into the engine's ring at their frames, publishes the block, wakes the
 * engine, and plays the ring plug_readOffset() frames behind; the reported latency is
 * plug_latency(). In real time it never waits; offline (kOffline: Live's bounce, freeze and
 * export) it waits for the engine. The state is the card folder as an LCHS archive
 * (plug_archive.h).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include "plug_clock.h"
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
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
	Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE { return latency; }
	Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE { return Steinberg::Vst::kInfiniteTail; }
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	static constexpr unsigned kMaxMidi = 1024;	/**< MIDI messages of one block: clocks and notes */

private:
	unsigned notesOf (Steinberg::Vst::IEventList* events, Steinberg::int32 frames);
	void reportState (Steinberg::Vst::ProcessData& data, uint64_t state);
	const std::string& logOf (const CardFolder& f);

	EngineLink link;
	CardFolder folder;
	std::string logPath;	/**< BASE/logs/<id>.log, set when the first engine starts */
	plug_clock_t clock {};
	Steinberg::uint32 latency = 0;
	uint32_t readOffset = 0;
	uint32_t margin = 0;
	uint64_t frames = 0;		/**< host frames since the activation */
	uint64_t runningPid = 0;	/**< the engine that the clock generator last started */
	int reported = -1;			/**< the engine state last sent to the controller */
	bool active = false;
	plug_midi_t clocks[kMaxMidi];
	plug_midi_t notes[kMaxMidi];
};

} // namespace lichomat
