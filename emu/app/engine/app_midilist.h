/*
 * app_midilist.h
 *
 * A CoreMIDI MIDIEventList into MIDI ring entries (stage 1b spec 6.4): one
 * entry or more per packet, each with the packet's timestamp; a timestamp of
 * 0 becomes now (plan E8). Header-only use of CoreMIDI: MIDIEventPacketNext()
 * is inline, so this file links no framework.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_MIDILIST_H_
#define APP_MIDILIST_H_

#include <CoreMIDI/CoreMIDI.h>

#include "app_midi.h"

unsigned app_midiFromEventList(const MIDIEventList *list, uint8_t port, uint64_t now, app_midiPush_t push, void *ctx);

#endif /* APP_MIDILIST_H_ */
