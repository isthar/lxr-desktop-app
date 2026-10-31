/*
 * app_midi.h
 *
 * MIDI conversions of LICHOMAT Emu (stage 1b spec 4.3, 6.4, 6.5), pure C:
 * CoreMIDI's MIDI 1.0 UMP words (message types 1, 2 and 3) to and from
 * USB-MIDI 1.0 packets on cable 0, UMP to DIN bytes, the DIN out bytes to
 * complete messages, and the ring entries of the MIDI rings.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_MIDI_H_
#define APP_MIDI_H_

#include <stdint.h>

enum { APP_PORT_USB, APP_PORT_DIN, APP_PORT_JACK };

#define APP_MIDI_WORDS 16u		/**< UMP words per ring entry (E10) */
#define APP_MIDI_PACKETS 16u	/**< USB-MIDI packets' worth per ring entry: 1 per MT 1 or 2, 3 per MT 3 (E10) */

/** one entry of a MIDI-in ring (stamp: host time) or of the MIDI-out ring
 * (stamp: presentation host time) */
typedef struct app_midiEntry
{
	uint64_t stamp;
	uint8_t port;
	uint8_t nwords;
	uint32_t words[APP_MIDI_WORDS];
} app_midiEntry_t;

typedef int (*app_midiPush_t)(void *ctx, const app_midiEntry_t *e);
typedef const app_midiEntry_t *(*app_midiPeek_t)(void *ctx, unsigned i);
typedef void (*app_umpEmit_t)(void *ctx, uint64_t t, const uint32_t *w, unsigned n);

/** the words of the UMP message that starts with w0 */
unsigned app_umpLength(uint32_t w0);
/** one event packet into entries of at most APP_MIDI_PACKETS packets' worth, cut at message boundaries; returns the entries */
unsigned app_midiSplit(uint64_t stamp, uint8_t port, const uint32_t *words, unsigned n, app_midiPush_t push, void *ctx);

/** SysEx bytes between F0 and F7 into MT 3 packets of up to 6 bytes */
typedef struct app_sx { uint8_t b[6], n, started, active; } app_sx_t;
void app_sxByte(app_sx_t *sx, uint64_t t, uint8_t b, app_umpEmit_t emit, void *ctx);

/** SysEx bytes on the USB side not yet packed into a CIN 0x4 packet */
typedef struct app_usbPend { uint8_t b[3], n; } app_usbPend_t;
/** UMP to USB-MIDI 1.0 packets with the CIN of each message, SysEx included; the
 * bytes written, or -1 if they do not fit in cap */
int app_umpToUsb(const uint32_t *w, unsigned n, app_usbPend_t *pend, uint8_t *out, unsigned cap);
/** one USB transfer from the ring's head (spec 6.4): consecutive entries with the
 * head's stamp, up to 16 packets; returns the entries it took */
unsigned app_usbTransfer(app_midiPeek_t peek, void *ctx, app_usbPend_t *pend, uint8_t out[64], unsigned *len);

/** UMP to MIDI 1.0 bytes for the DIN wire; returns the bytes */
unsigned app_umpToBytes(const uint32_t *w, unsigned n, uint8_t *out, unsigned cap);

/** a USB out transfer's packets to UMP messages, all at its time t */
void app_usbToUmp(uint64_t t, const uint8_t *data, unsigned len, app_sx_t *sx, app_umpEmit_t emit, void *ctx);

/** the DIN out sink's bytes to complete MIDI 1.0 messages (spec 6.5) */
typedef struct app_dinAsm
{
	app_sx_t sx;
	uint64_t t, sxT;	/**< the time of the message's first byte; of the SysEx's F0 */
	uint64_t orphans;	/**< data bytes without a status */
	uint8_t status, need, have, fresh, d[2];
} app_dinAsm_t;
void app_dinByte(app_dinAsm_t *a, uint64_t t, uint8_t b, app_umpEmit_t emit, void *ctx);

#endif /* APP_MIDI_H_ */
