/*
 * au_lists.h
 *
 * The AU's event list and parameter changes for the VST3 processor (plan E9.7 Task 4): the
 * interfaces process() reads (IEventList, IParameterChanges, IParamValueQueue) over fixed arrays,
 * so that filling and clearing them on the render thread never allocates. A queue's points are
 * kept in the order of their offsets (equal offsets in the order they came), as VST3 asks.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

namespace lichomat {

/** a cycle's events: Note Ons and Offs, at most kMax (more are dropped) */
class AuEvents : public Steinberg::U::ImplementsNonDestroyable<Steinberg::U::Directly<Steinberg::Vst::IEventList>>
{
public:
	static constexpr Steinberg::int32 kMax = 1024;

	Steinberg::int32 PLUGIN_API getEventCount () override { return count; }

	Steinberg::tresult PLUGIN_API getEvent (Steinberg::int32 index, Steinberg::Vst::Event& e) override
	{
		if (index < 0 || index >= count)
			return Steinberg::kInvalidArgument;
		e = events[index];
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API addEvent (Steinberg::Vst::Event& e) override
	{
		if (count >= kMax)
			return Steinberg::kResultFalse;
		events[count++] = e;
		return Steinberg::kResultOk;
	}

	void clear () { count = 0; }

private:
	Steinberg::Vst::Event events[kMax] {};
	Steinberg::int32 count = 0;
};

/** one parameter's points of a cycle, at most kMax (more are dropped) */
class AuParamQueue
: public Steinberg::U::ImplementsNonDestroyable<Steinberg::U::Directly<Steinberg::Vst::IParamValueQueue>>
{
public:
	static constexpr Steinberg::int32 kMax = 64;

	Steinberg::Vst::ParamID PLUGIN_API getParameterId () override { return id; }
	Steinberg::int32 PLUGIN_API getPointCount () override { return count; }

	Steinberg::tresult PLUGIN_API getPoint (Steinberg::int32 index, Steinberg::int32& offset,
	                                        Steinberg::Vst::ParamValue& value) override
	{
		if (index < 0 || index >= count)
			return Steinberg::kInvalidArgument;
		offset = offsets[index];
		value = values[index];
		return Steinberg::kResultOk;
	}

	/* in the order of the offsets, after the points of the same offset */
	Steinberg::tresult PLUGIN_API addPoint (Steinberg::int32 offset, Steinberg::Vst::ParamValue value,
	                                        Steinberg::int32& index) override
	{
		if (count >= kMax)
			return Steinberg::kResultFalse;
		Steinberg::int32 k = count++;
		while (k > 0 && offsets[k - 1] > offset)
		{
			offsets[k] = offsets[k - 1];
			values[k] = values[k - 1];
			k--;
		}
		offsets[k] = offset;
		values[k] = value;
		index = k;
		return Steinberg::kResultOk;
	}

	Steinberg::Vst::ParamID id = 0;
	Steinberg::int32 count = 0;

private:
	Steinberg::int32 offsets[kMax] {};
	Steinberg::Vst::ParamValue values[kMax] {};
};

/** a cycle's parameter changes: a queue per parameter, in the order of their first point */
class AuParamChanges
: public Steinberg::U::ImplementsNonDestroyable<Steinberg::U::Directly<Steinberg::Vst::IParameterChanges>>
{
public:
	static constexpr Steinberg::int32 kMax = 64;	/**< more than the plugin's 37 parameters */

	Steinberg::int32 PLUGIN_API getParameterCount () override { return count; }

	Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData (Steinberg::int32 index) override
	{
		return index >= 0 && index < count ? &queues[index] : nullptr;
	}

	Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData (const Steinberg::Vst::ParamID& id,
	                                                               Steinberg::int32& index) override
	{
		for (Steinberg::int32 k = 0; k < count; k++)
			if (queues[k].id == id)
			{
				index = k;
				return &queues[k];
			}
		if (count >= kMax)
			return nullptr;
		index = count;
		queues[count].id = id;
		queues[count].count = 0;
		return &queues[count++];
	}

	/** a point of parameter id */
	void add (Steinberg::Vst::ParamID id, Steinberg::int32 offset, Steinberg::Vst::ParamValue value)
	{
		Steinberg::int32 index = 0, point = 0;
		if (Steinberg::Vst::IParamValueQueue* q = addParameterData (id, index))
			q->addPoint (offset, value, point);
	}

	void clear () { count = 0; }

private:
	AuParamQueue queues[kMax];
	Steinberg::int32 count = 0;
};

} // namespace lichomat
