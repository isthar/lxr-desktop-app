/*
 * plugin_controller.h
 *
 * The LICHOMAT plugin's edit controller (plan E9.2 Task 8).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace lichomat {

class Controller : public Steinberg::Vst::EditControllerEx1
{
public:
	static Steinberg::FUnknown* create (void*);
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
};

} // namespace lichomat
