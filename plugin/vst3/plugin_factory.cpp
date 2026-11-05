/*
 * plugin_factory.cpp
 *
 * The LICHOMAT plugin's class factory (plan E9.2 Task 8): one instrument, "LICHOMAT" by
 * "LICHOMAT", in the host's Instrument|Drum category. No URL or address: the strings of the
 * binary name no maker (M21).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_notices.h"
#include "plugin_controller.h"
#include "plugin_ids.h"
#include "plugin_processor.h"
#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF (PLUG_VENDOR, "", "")

DEF_CLASS2 (INLINE_UID_FROM_FUID (lichomat::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass, PLUG_NAME,
            Vst::kDistributable, PlugType::kInstrumentDrum, PLUG_VERSION, kVstVersionString, lichomat::Processor::create)

DEF_CLASS2 (INLINE_UID_FROM_FUID (lichomat::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            PLUG_NAME " Controller", 0, "", PLUG_VERSION, kVstVersionString, lichomat::Controller::create)

END_FACTORY
