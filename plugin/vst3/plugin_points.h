/*
 * plugin_points.h
 *
 * A block's parameter changes from the host as plug_params.h's points (plan E9.4 Task 8): the
 * processor's, and the test host's for the MIDI it expects the plugin to give its engine, so
 * that both take them in the same order.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include "plug_params.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

namespace lichomat {

/** The points of the plugin's parameters in changes, a block of n frames (0: a flush between
 * blocks), in the order of its queues and of each queue's points; an offset outside the block is
 * its nearest frame. Returns the count, at most max. */
inline unsigned pointsOf (Steinberg::Vst::IParameterChanges* changes, Steinberg::int32 n, plug_paramPoint_t* out,
                          unsigned max)
{
	unsigned count = 0;
	const Steinberg::int32 queues = changes ? changes->getParameterCount () : 0;
	for (Steinberg::int32 i = 0; i < queues; i++)
	{
		Steinberg::Vst::IParamValueQueue* q = changes->getParameterData (i);
		const int k = q ? plug_paramIndex (q->getParameterId ()) : -1;
		if (k < 0)
			continue;
		for (Steinberg::int32 j = 0; j < q->getPointCount () && count < max; j++)
		{
			Steinberg::int32 offset = 0;
			Steinberg::Vst::ParamValue value = 0;
			if (q->getPoint (j, offset, value) != Steinberg::kResultOk)
				continue;
			const Steinberg::int32 o = offset < 0 || n <= 0 ? 0 : offset >= n ? n - 1 : offset;
			out[count++] = plug_paramPoint_t {(unsigned)k, (uint32_t)o, plug_paramValue ((unsigned)k, value)};
		}
	}
	return count;
}

} // namespace lichomat
