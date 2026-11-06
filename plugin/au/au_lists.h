/*
 * au_lists.h
 *
 * The AU's event list and parameter changes for the VST3 processor (plan E9.7 Task 4): the
 * interfaces process() reads (IEventList, IParameterChanges, IParamValueQueue) over fixed arrays,
 * so that filling and clearing them on the render thread never allocates. A queue's points are
 * kept in the order of their offsets (equal offsets in the order they came), as VST3 asks. And the
 * queue that takes the host's notes from any thread to the render thread (AuNoteQueue).
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
#include <cstdint>

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

/** The host's Note Ons and Offs, from any thread (MusicDevice.h: the render thread before its render, at an offset;
 * another thread at offset 0), to the render thread, which takes them at the start of its render: a bounded queue of
 * many producers and one consumer (D. Vyukov's) over kSize fixed cells, each with a sequence number. A push takes the
 * tail's position with a compare-and-swap (relaxed), writes its cell, then publishes it with a release store of the
 * cell's sequence; the consumer reads a cell after an acquire load of its sequence and frees it for the next lap with a
 * release store. No allocation, lock, wait or system call on either side; a push into a full queue fails. Positions are
 * 64 bits: they do not wrap */
class AuNoteQueue
{
public:
	static constexpr uint32_t kSize = 1024;	/**< a power of two */

	struct Note
	{
		bool on;
		uint8_t channel, pitch, velocity;
		uint32_t frame;
	};

	AuNoteQueue ()
	{
		for (uint32_t i = 0; i < kSize; i++)
			cells[i].seq.store (i, std::memory_order_relaxed);
	}

	/** any thread's: false when the queue is full */
	bool push (const Note& n) noexcept
	{
		uint64_t pos = tail.load (std::memory_order_relaxed);
		for (;;)
		{
			Cell& c = cells[pos & (kSize - 1)];
			const uint64_t seq = c.seq.load (std::memory_order_acquire);
			if (seq == pos)
			{
				if (tail.compare_exchange_weak (pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed))
				{
					c.note = n;
					c.seq.store (pos + 1, std::memory_order_release);
					return true;
				}
			}
			else if (seq < pos)
				return false;	/* the cell still holds the note of the lap before: full */
			else
				pos = tail.load (std::memory_order_relaxed);	/* another producer took pos */
		}
	}

	/** the consumer's: the next published note and its position (false when there is none yet) */
	bool pop (Note& n, uint64_t& at) noexcept
	{
		Cell& c = cells[head & (kSize - 1)];
		if (c.seq.load (std::memory_order_acquire) != head + 1)
			return false;
		n = c.note;
		at = head;
		c.seq.store (head + kSize, std::memory_order_release);
		head++;
		return true;
	}

	/** the position the next push takes: everything before it was pushed */
	uint64_t end () const noexcept { return tail.load (std::memory_order_acquire); }

private:
	struct Cell
	{
		std::atomic<uint64_t> seq;
		Note note;
	};
	Cell cells[kSize];
	std::atomic<uint64_t> tail {0};
	uint64_t head = 0;	/**< the consumer's alone */
};

/** one parameter's points of a cycle, at most kMax: when full, a point at or after the last replaces it (the value the
 * parameter ends the cycle with is the last one given), and an earlier one is dropped; addPoint() is kResultFalse for
 * either */
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
		{
			if (offset >= offsets[count - 1])
			{
				offsets[count - 1] = offset;
				values[count - 1] = value;
				index = count - 1;
			}
			return Steinberg::kResultFalse;
		}
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

	/** a point of parameter id: false when a point was lost (AuParamQueue::addPoint()) */
	bool add (Steinberg::Vst::ParamID id, Steinberg::int32 offset, Steinberg::Vst::ParamValue value)
	{
		Steinberg::int32 index = 0, point = 0;
		Steinberg::Vst::IParamValueQueue* q = addParameterData (id, index);
		return q && q->addPoint (offset, value, point) == Steinberg::kResultOk;
	}

	void clear () { count = 0; }

private:
	AuParamQueue queues[kMax];
	Steinberg::int32 count = 0;
};

} // namespace lichomat
