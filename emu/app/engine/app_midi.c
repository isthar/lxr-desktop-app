/*
 * app_midi.c
 *
 * The MIDI conversions of app_midi.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_midi.h"

#include <string.h>

unsigned app_umpLength(uint32_t w0)
{
	static const uint8_t len[16] = { 1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4 };
	return len[w0 >> 28];
}

unsigned app_midiSplit(uint64_t stamp, uint8_t port, const uint32_t *words, unsigned n, app_midiPush_t push, void *ctx)
{
	app_midiEntry_t e;
	memset(&e, 0, sizeof e);
	e.stamp = stamp;
	e.port = port;
	unsigned entries = 0, packets = 0;
	for (unsigned i = 0; i < n;)
	{
		const unsigned len = app_umpLength(words[i]);
		const unsigned cost = words[i] >> 28 == 0x3u ? 3u : 1u;	/* a SysEx packet: up to 8 bytes with F0 and F7 */
		if (len > n - i)
			break;	/* a cut message */
		if (e.nwords + len > APP_MIDI_WORDS || packets + cost > APP_MIDI_PACKETS)
		{
			(void)push(ctx, &e);	/* a full ring counts the drop */
			entries++;
			e.nwords = 0;
			packets = 0;
		}
		memcpy(&e.words[e.nwords], &words[i], len * sizeof *words);
		e.nwords = (uint8_t)(e.nwords + len);
		packets += cost;
		i += len;
	}
	if (e.nwords)
	{
		(void)push(ctx, &e);
		entries++;
	}
	return entries;
}

/* one MT 3 packet: status 0 complete, 1 start, 2 continue, 3 end */
static void app_sxFlush(app_sx_t *sx, uint64_t t, int last, app_umpEmit_t emit, void *ctx)
{
	const uint32_t st = last ? (sx->started ? 3u : 0u) : (sx->started ? 2u : 1u);
	uint8_t b[6] = { 0 };
	memcpy(b, sx->b, sx->n);
	const uint32_t w[2] = {
		0x30000000u | (st << 20) | ((uint32_t)sx->n << 16) | ((uint32_t)b[0] << 8) | b[1],
		((uint32_t)b[2] << 24) | ((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 8) | b[5],
	};
	emit(ctx, t, w, 2u);
	sx->started = (uint8_t)!last;
	sx->n = 0;
}

void app_sxByte(app_sx_t *sx, uint64_t t, uint8_t b, app_umpEmit_t emit, void *ctx)
{
	if (b == 0xF0u)
	{
		if (sx->active)
			app_sxFlush(sx, t, 1, emit, ctx);
		sx->active = 1;
		sx->started = 0;
		sx->n = 0;
	}
	else if (b == 0xF7u)
	{
		if (sx->active)
			app_sxFlush(sx, t, 1, emit, ctx);
		sx->active = 0;
	}
	else if (sx->active)
	{
		if (sx->n == 6u)
			app_sxFlush(sx, t, 0, emit, ctx);
		sx->b[sx->n++] = b & 0x7Fu;
	}
}

static int app_usbPut(uint8_t *out, unsigned *at, unsigned cap, uint8_t cin, uint8_t b0, uint8_t b1, uint8_t b2)
{
	if (*at + 4u > cap)
		return -1;
	out[(*at)++] = cin;	/* cable 0 */
	out[(*at)++] = b0;
	out[(*at)++] = b1;
	out[(*at)++] = b2;
	return 0;
}

/* CIN 0x4 for three bytes that do not end the SysEx; 0x5-0x7 for its end */
static int app_usbSxByte(app_usbPend_t *p, uint8_t b, uint8_t *out, unsigned *at, unsigned cap)
{
	p->b[p->n++] = b;
	if (b == 0xF7u)
	{
		const unsigned n = p->n;
		p->n = 0;
		return app_usbPut(out, at, cap, (uint8_t)(0x4u + n), p->b[0], n > 1u ? p->b[1] : 0u, n > 2u ? p->b[2] : 0u);
	}
	if (p->n == 3u)
	{
		p->n = 0;
		return app_usbPut(out, at, cap, 0x4u, p->b[0], p->b[1], p->b[2]);
	}
	return 0;
}

int app_umpToUsb(const uint32_t *w, unsigned n, app_usbPend_t *pend, uint8_t *out, unsigned cap)
{
	unsigned at = 0;
	for (unsigned i = 0; i < n; i += app_umpLength(w[i]))
	{
		const uint32_t x = w[i];
		const uint8_t st = (uint8_t)(x >> 16), d1 = (uint8_t)((x >> 8) & 0x7Fu), d2 = (uint8_t)(x & 0x7Fu);
		int r = 0;
		switch (x >> 28)
		{
		case 0x2u:	/* MIDI 1.0 channel voice: CIN = the status's high nibble */
			r = app_usbPut(out, &at, cap, (uint8_t)(st >> 4), st, d1, (st & 0xE0u) == 0xC0u ? 0u : d2);
			break;
		case 0x1u:	/* system common and realtime */
			if (st >= 0xF8u)
				r = app_usbPut(out, &at, cap, 0xFu, st, 0u, 0u);
			else if (st == 0xF2u)
				r = app_usbPut(out, &at, cap, 0x3u, st, d1, d2);
			else if (st == 0xF1u || st == 0xF3u)
				r = app_usbPut(out, &at, cap, 0x2u, st, d1, 0u);
			else if (st == 0xF6u)
				r = app_usbPut(out, &at, cap, 0x5u, st, 0u, 0u);
			break;
		case 0x3u:	/* 7-bit SysEx */
		{
			if (i + 1u >= n)
				return (int)at;
			const uint32_t y = w[i + 1u];
			const unsigned status = (x >> 20) & 0xFu, count = (x >> 16) & 0xFu;
			const uint8_t b[6] = { (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(y >> 24), (uint8_t)(y >> 16),
				(uint8_t)(y >> 8), (uint8_t)y };
			if (status == 0u || status == 1u)
				r |= app_usbSxByte(pend, 0xF0u, out, &at, cap);
			for (unsigned k = 0; k < count && k < 6u; k++)
				r |= app_usbSxByte(pend, b[k] & 0x7Fu, out, &at, cap);
			if (status == 0u || status == 3u)
				r |= app_usbSxByte(pend, 0xF7u, out, &at, cap);
			break;
		}
		default:	/* utility messages, and anything that is not MIDI 1.0 */
			break;
		}
		if (r)
			return -1;
	}
	return (int)at;
}

unsigned app_usbTransfer(app_midiPeek_t peek, void *ctx, app_usbPend_t *pend, uint8_t out[64], unsigned *len)
{
	*len = 0;
	const app_midiEntry_t *first = peek(ctx, 0u);
	unsigned taken = 0;
	for (const app_midiEntry_t *e = first; e && e->stamp == first->stamp; e = peek(ctx, ++taken))
	{
		app_usbPend_t p = *pend;	/* committed only if the entry fits */
		const int got = app_umpToUsb(e->words, e->nwords, &p, out + *len, 64u - *len);
		if (got < 0)
			break;	/* the next transfer takes it; a well-formed entry fits an empty one (E10) */
		*len += (unsigned)got;
		*pend = p;
	}
	return taken;
}

static void app_dinEmit(app_dinAsm_t *a, app_umpEmit_t emit, void *ctx)
{
	const uint32_t w = (a->status < 0xF0u ? 0x20000000u : 0x10000000u) | ((uint32_t)a->status << 16)
		| (a->need > 0u ? (uint32_t)a->d[0] << 8 : 0u) | (a->need > 1u ? a->d[1] : 0u);
	emit(ctx, a->t, &w, 1u);
}

void app_dinByte(app_dinAsm_t *a, uint64_t t, uint8_t b, app_umpEmit_t emit, void *ctx)
{
	if (b >= 0xF8u)
	{
		const uint32_t w = 0x10000000u | ((uint32_t)b << 16);	/* a message of its own, at its own time */
		emit(ctx, t, &w, 1u);
		return;
	}
	if (b == 0xF0u)
	{
		if (a->sx.active)
			app_sxByte(&a->sx, a->sxT, 0xF7u, emit, ctx);
		a->status = 0;
		a->sxT = t;
	}
	if (b == 0xF0u || b == 0xF7u || (a->sx.active && !(b & 0x80u)))
	{
		app_sxByte(&a->sx, a->sxT, b, emit, ctx);	/* every packet carries the time of the F0 */
		return;
	}
	if (b & 0x80u)
	{
		if (a->sx.active)
			app_sxByte(&a->sx, a->sxT, 0xF7u, emit, ctx);	/* a status byte ends a SysEx */
		a->status = b;
		a->have = 0;
		a->t = t;
		a->fresh = 1;
		a->need = b < 0xF0u ? ((b & 0xE0u) == 0xC0u ? 1u : 2u) : b == 0xF2u ? 2u : (b == 0xF1u || b == 0xF3u) ? 1u : 0u;
		if (b == 0xF4u || b == 0xF5u)
			a->status = 0;	/* undefined */
		else if (a->need == 0u)
		{
			app_dinEmit(a, emit, ctx);	/* F6 */
			a->status = 0;
		}
		return;
	}
	if (!a->status)
	{
		a->orphans++;
		return;
	}
	if (a->have == 0u && !a->fresh)
		a->t = t;	/* running status: the message starts at its first data byte */
	a->fresh = 0;
	a->d[a->have++] = b;
	if (a->have == a->need)
	{
		app_dinEmit(a, emit, ctx);
		a->have = 0;
		if (a->status >= 0xF0u)
			a->status = 0;	/* system common has no running status */
	}
}

static void app_bytePut(uint8_t *out, unsigned *at, unsigned cap, uint8_t b)
{
	if (*at < cap)
		out[(*at)++] = b;	/* a full buffer drops the rest; the count is what was written */
}

unsigned app_umpToBytes(const uint32_t *w, unsigned n, uint8_t *out, unsigned cap)
{
	unsigned at = 0;
	for (unsigned i = 0; i < n;)
	{
		const unsigned len = app_umpLength(w[i]);
		if (len > n - i)
			break;	/* a cut message */
		const uint32_t x = w[i];
		const uint8_t st = (uint8_t)(x >> 16), d1 = (uint8_t)((x >> 8) & 0x7Fu), d2 = (uint8_t)(x & 0x7Fu);
		switch (x >> 28)
		{
		case 0x2u:
			app_bytePut(out, &at, cap, st);
			app_bytePut(out, &at, cap, d1);
			if ((st & 0xE0u) != 0xC0u)
				app_bytePut(out, &at, cap, d2);
			break;
		case 0x1u:
			if (st == 0xF4u || st == 0xF5u)
				break;	/* undefined */
			app_bytePut(out, &at, cap, st);
			if (st == 0xF1u || st == 0xF2u || st == 0xF3u)
				app_bytePut(out, &at, cap, d1);
			if (st == 0xF2u)
				app_bytePut(out, &at, cap, d2);
			break;
		case 0x3u:
		{
			const uint32_t y = w[i + 1u];
			const unsigned status = (x >> 20) & 0xFu, count = (x >> 16) & 0xFu;
			const uint8_t b[6] = { (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(y >> 24), (uint8_t)(y >> 16),
				(uint8_t)(y >> 8), (uint8_t)y };
			if (status == 0u || status == 1u)
				app_bytePut(out, &at, cap, 0xF0u);
			for (unsigned k = 0; k < count && k < 6u; k++)
				app_bytePut(out, &at, cap, b[k] & 0x7Fu);
			if (status == 0u || status == 3u)
				app_bytePut(out, &at, cap, 0xF7u);
			break;
		}
		default:
			break;
		}
		i += len;
	}
	return at;
}

void app_usbToUmp(uint64_t t, const uint8_t *data, unsigned len, app_sx_t *sx, app_umpEmit_t emit, void *ctx)
{
	for (unsigned i = 0; i + 4u <= len; i += 4u)
	{
		const unsigned cin = data[i] & 0xFu;
		const uint32_t b0 = data[i + 1u], b1 = data[i + 2u], b2 = data[i + 3u];
		uint32_t w;
		/* CIN 0xF is one byte as it is (usb_midiTx.c): a realtime message, or a SysEx byte, F0 and F7 included,
		 * that the DIN -> USB thru sends one per packet. CIN 0x5 is F6, or the last byte of a SysEx */
		const int rt = b0 >= 0xF8u;
		if (cin >= 0x8u && cin <= 0xEu)
			w = 0x20000000u | b0 << 16 | b1 << 8 | b2;
		else if (cin == 0x2u || cin == 0x3u || (cin == 0x5u && (b0 == 0xF6u || rt)) || (cin == 0xFu && rt))
			w = 0x10000000u | b0 << 16 | (cin == 0x2u || cin == 0x3u ? b1 << 8 : 0u) | (cin == 0x3u ? b2 : 0u);
		else if (cin == 0xFu && b0 >= 0x80u && b0 != 0xF0u && b0 != 0xF7u)
			continue;	/* F4, F5 and anything else that is no SysEx byte and no realtime message */
		else
		{
			const unsigned nb = cin == 0x4u || cin == 0x7u ? 3u : cin == 0x6u ? 2u : cin == 0x5u || cin == 0xFu ? 1u : 0u;
			const uint8_t b[3] = { (uint8_t)b0, (uint8_t)b1, (uint8_t)b2 };
			for (unsigned k = 0; k < nb; k++)
				app_sxByte(sx, t, b[k], emit, ctx);
			continue;	/* CIN 0x0 and 0x1 give nb = 0: ignored */
		}
		emit(ctx, t, &w, 1u);
	}
}
