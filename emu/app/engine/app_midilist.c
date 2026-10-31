/*
 * app_midilist.c
 *
 * A CoreMIDI MIDIEventList into MIDI ring entries.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_midilist.h"

unsigned app_midiFromEventList(const MIDIEventList *list, uint8_t port, uint64_t now, app_midiPush_t push, void *ctx)
{
	unsigned entries = 0;
	const MIDIEventPacket *p = &list->packet[0];
	for (UInt32 i = 0; i < list->numPackets; i++)
	{
		entries += app_midiSplit(p->timeStamp ? p->timeStamp : now, port, p->words, p->wordCount, push, ctx);
		p = MIDIEventPacketNext(p);
	}
	return entries;
}
