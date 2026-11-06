/*
 * plugin_controller.cpp
 *
 * The LICHOMAT plugin's edit controller (plan E9.2 Task 8): no window of its own; the read-only
 * parameter of the engine's state, for the host's generic view and for the tests, and (plan E9.4)
 * the automatable parameters of plug_params.h, each shown as the device shows it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plugin_controller.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "plug_params.h"
#include "plugin_ids.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace lichomat {

/* a firmware parameter: the values 0..max in steps of 1, shown as the device's display shows
 * them: value + offset (pan -63..64, the pattern 1..64) or the value's name (a voice's output) */
class FwParameter : public Parameter
{
public:
	explicit FwParameter (const plug_param_t& p) : param (p)
	{
		info.id = p.id;
		UString (info.title, str16BufferSize (String128)).fromAscii (p.name);
		UString (info.shortTitle, str16BufferSize (String128)).fromAscii (p.shortName);
		info.units[0] = 0;
		info.stepCount = p.max;
		info.defaultNormalizedValue = (double)p.def / p.max;
		info.unitId = kRootUnitId;
		info.flags = ParameterInfo::kCanAutomate | (p.kind == PLUG_PK_PROGRAM || p.names ? ParameterInfo::kIsList : 0);
		setNormalized (info.defaultNormalizedValue);
	}

	void toString (ParamValue normalized, String128 string) const SMTG_OVERRIDE
	{
		char text[16];
		const int v = (int)toPlain (normalized);
		if (param.names)
			snprintf (text, sizeof text, "%s", param.names[v]);
		else
			snprintf (text, sizeof text, "%d", v + param.offset);
		UString (string, str16BufferSize (String128)).fromAscii (text);
	}

	bool fromString (const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE
	{
		char text[16];
		UString (const_cast<TChar*> (string), str16BufferSize (String128)).toAscii (text, sizeof text);
		for (int v = 0; param.names && v <= param.max; v++)
			if (!strcmp (text, param.names[v]))
			{
				normalized = (double)v / param.max;
				return true;
			}
		char* end = nullptr;
		const long v = strtol (text, &end, 10) - param.offset;
		if (end == text || v < 0 || v > param.max)
			return false;
		normalized = (double)v / param.max;
		return true;
	}

	ParamValue toPlain (ParamValue normalized) const SMTG_OVERRIDE
	{
		const ParamValue v = std::floor (normalized * param.max + 0.5);
		return !(v >= 0) ? 0 : v > param.max ? param.max : v;	/* also for a value outside 0-1, or not a number */
	}

	ParamValue toNormalized (ParamValue plain) const SMTG_OVERRIDE
	{
		return plain / param.max;
	}

private:
	const plug_param_t& param;
};

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
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		parameters.addParameter (new FwParameter (plug_params[i]));
	return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState (IBStream*)
{
	/* the processor's state is the card and the values the host had; the processor gives them to the
	 * host as output parameter changes once its engine runs (plan E9.4) */
	return kResultOk;
}

} // namespace lichomat
