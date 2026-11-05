/*
 * plugin_ids.h
 *
 * The LICHOMAT plugin's class ids and parameters (plan E9.2 Task 8).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace lichomat {

/* random, made once for this plugin; a host keys a set's devices by them: never change them */
static const Steinberg::FUID kProcessorUID (0x7A3C1E54, 0x9B2D4F61, 0x8E0A5C37, 0x41D6B9F2);
static const Steinberg::FUID kControllerUID (0x2F8B6D13, 0xC4E1A970, 0x5D3F82B6, 0x9A7C0E45);

/** the engine's state, read only: 0 stopped, 1 starting, 2 running, 3 failed (the steps of a list
 * of 4, plug_shm.h's PLUG_ENGINE_* in that order) */
enum : Steinberg::Vst::ParamID
{
	kParamEngine = 0
};

} // namespace lichomat
