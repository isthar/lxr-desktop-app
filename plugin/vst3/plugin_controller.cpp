/*
 * plugin_controller.cpp
 *
 * The LICHOMAT plugin's edit controller (plan E9.2 Task 8): no window of its own; one read-only
 * parameter, the engine's state, for the host's generic view and for the tests.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plugin_controller.h"

#include "plugin_ids.h"
#include "public.sdk/source/vst/vstparameters.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace lichomat {

FUnknown* Controller::create (void*)
{
	return (IEditController*)new Controller;
}

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
	const tresult r = EditControllerEx1::initialize (context);
	if (r != kResultOk)
		return r;
	auto* p = new StringListParameter (STR16 ("Engine"), kParamEngine, nullptr, ParameterInfo::kIsReadOnly | ParameterInfo::kIsList);
	p->appendString (STR16 ("stopped"));
	p->appendString (STR16 ("starting"));
	p->appendString (STR16 ("running"));
	p->appendString (STR16 ("failed"));
	parameters.addParameter (p);
	return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState (IBStream*)
{
	return kResultOk;	/* the processor's state is the card; no parameter follows from it */
}

} // namespace lichomat
