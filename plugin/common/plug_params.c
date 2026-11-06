/*
 * plug_params.c
 *
 * The plugin's automatable parameters (plan E9.4 Task 5); see plug_params.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_params.h"

#include <math.h>
#include <string.h>

/* The firmware's MIDI implementation (MidiMessages.h, MidiParser.c): a CC n sets parameter n - 1,
 * NRPN n parameter n + 128; Parameters.h numbers them, and eng_fw.c checks this table against it
 * when the engine starts. def: the init kit's values (lichomat-render --seed-card). */
#define CC(id, name, sn, cc, max, off, def) { id, name, sn, PLUG_PK_CC, cc, (cc) - 1, max, off, def, NULL }
#define NRPN(id, name, sn, nr, max, def) { id, name, sn, PLUG_PK_NRPN, nr, (nr) + 128, max, 0, def, NULL }

/* a voice's output (MenuText.h, outputNames): both of out 1 or of out 2, one side of either, or the FX bus */
static const char *const plug_outputs[] = { "St1", "St2", "L1", "R1", "L2", "R2", "FX" };
#define OUT(id, name, sn, nr, def) { id, name, sn, PLUG_PK_NRPN, nr, (nr) + 128, 6, 0, def, plug_outputs }

const plug_param_t plug_params[PLUG_PARAM_COUNT] = {
	{ 1, "Pattern", "Pattern", PLUG_PK_PROGRAM, 0, PLUG_PAR_PATTERN, 63, 1, 0, NULL },
	CC(10, "Drum 1 Volume", "D1 Vol", 89, 127, 0, 127),
	CC(11, "Drum 2 Volume", "D2 Vol", 90, 127, 0, 127),
	CC(12, "Drum 3 Volume", "D3 Vol", 91, 127, 0, 127),
	CC(13, "Snare Volume", "SN Vol", 92, 127, 0, 127),
	CC(14, "Cymbal Volume", "CY Vol", 93, 127, 0, 127),
	CC(15, "Hi-hat Volume", "HH Vol", 94, 127, 0, 127),
	CC(20, "Drum 1 Pan", "D1 Pan", 95, 127, -63, 63),
	CC(21, "Drum 2 Pan", "D2 Pan", 96, 127, -63, 63),
	CC(22, "Drum 3 Pan", "D3 Pan", 97, 127, -63, 63),
	CC(23, "Snare Pan", "SN Pan", 100, 127, -63, 63),
	CC(24, "Cymbal Pan", "CY Pan", 101, 127, -63, 63),
	CC(25, "Hi-hat Pan", "HH Pan", 102, 127, -63, 63),
	CC(30, "Drum 1 Filter", "D1 Filt", 38, 127, 0, 127),
	CC(31, "Drum 2 Filter", "D2 Filt", 39, 127, 0, 127),
	CC(32, "Drum 3 Filter", "D3 Filt", 40, 127, 0, 127),
	CC(33, "Snare Filter", "SN Filt", 41, 127, 0, 127),
	CC(34, "Cymbal Filter", "CY Filt", 42, 127, 0, 68),
	CC(35, "Hi-hat Filter", "HH Filt", 43, 127, 0, 104),
	CC(40, "Drum 1 Decay", "D1 Dec", 51, 127, 0, 21),
	CC(41, "Drum 2 Decay", "D2 Dec", 53, 127, 0, 13),
	CC(42, "Drum 3 Decay", "D3 Dec", 55, 127, 0, 3),
	CC(43, "Snare Decay", "SN Dec", 57, 127, 0, 10),
	CC(44, "Cymbal Decay", "CY Dec", 59, 127, 0, 107),
	CC(45, "Hi-hat Closed Decay", "HH Dec", 61, 127, 0, 5),
	CC(46, "Hi-hat Open Decay", "HH Open", 62, 127, 0, 17),
	NRPN(50, "DJ Filter", "DJ Filt", 96, 127, 64),
	NRPN(51, "Reverb Level", "Rev Lvl", 98, 127, 0),
	NRPN(52, "Reverb Decay", "Rev Dec", 116, 127, 64),
	NRPN(53, "FX Amount", "FX Amt", 108, 100, 0),
	OUT(60, "Drum 1 Output", "D1 Out", 87, 0),
	OUT(61, "Drum 2 Output", "D2 Out", 88, 0),
	OUT(62, "Drum 3 Output", "D3 Out", 89, 0),
	OUT(63, "Snare Output", "SN Out", 90, 0),
	OUT(64, "Cymbal Output", "CY Out", 91, 0),
	OUT(65, "Hi-hat Output", "HH Out", 92, 0),
};

int plug_paramIndex(uint32_t id)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		if (plug_params[i].id == id)
			return (int)i;
	return -1;
}

uint8_t plug_paramValue(unsigned i, double normalized)
{
	if (!(normalized > 0.0))
		return 0;	/* also not a number */
	const unsigned max = plug_params[i].max;
	const double v = floor(normalized * max + 0.5);
	return (uint8_t)(v >= max ? max : v);
}

unsigned plug_paramMidi(unsigned i, uint8_t v, uint8_t ch, uint32_t offset, plug_midi_t *out)
{
	const plug_param_t *p = &plug_params[i];
	const uint8_t cc = (uint8_t)(0xB0u | (ch & 15u));
	if (p->kind == PLUG_PK_PROGRAM)
	{
		out[0] = (plug_midi_t){ offset, 2, { (uint8_t)(0xC0u | (ch & 15u)), (uint8_t)(v & 0x7Fu), 0 } };
		return 1;
	}
	if (p->kind == PLUG_PK_NRPN)
	{
		out[0] = (plug_midi_t){ offset, 3, { cc, 99, (uint8_t)(p->number >> 7) } };
		out[1] = (plug_midi_t){ offset, 3, { cc, 98, (uint8_t)(p->number & 0x7Fu) } };
		out[2] = (plug_midi_t){ offset, 3, { cc, 6, (uint8_t)(v & 0x7Fu) } };
		return 3;
	}
	out[0] = (plug_midi_t){ offset, 3, { cc, (uint8_t)p->number, (uint8_t)(v & 0x7Fu) } };
	return 1;
}

void plug_paramsInit(plug_params_t *ps, uint64_t settle)
{
	memset(ps, 0, sizeof *ps);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		ps->p[i].want = -1;
		ps->p[i].sent = -1;
		ps->p[i].first = 1;
	}
	ps->settle = settle;
}

void plug_paramsEngine(plug_params_t *ps, uint64_t epoch)
{
	ps->epoch = epoch;
	ps->synced = 0;
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		ps->p[i].first = 1;
}

void plug_paramsRestore(plug_params_t *ps, const int *values)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		plug_paramSync_t *p = &ps->p[i];
		const int v = values[i];
		p->want = v >= 0 && v <= plug_params[i].max ? v : -1;
		p->shown = 0;
		p->sent = -1;
		p->sentFrame = 0;
		p->first = 1;
		p->pending = 0;	/* a new engine gets the restored values by its first report */
	}
	ps->synced = 0;
}

/* m into out[0..*n) after the messages of its offset or an earlier one; 0 when out is full */
static int insertMidi(plug_midi_t *out, unsigned *n, unsigned max, const plug_midi_t *m)
{
	if (*n >= max)
		return 0;
	unsigned k = (*n)++;
	while (k > 0 && out[k - 1].offset > m->offset)
	{
		out[k] = out[k - 1];
		k--;
	}
	out[k] = *m;
	return 1;
}

/* parameter i to v at offset into out, all its messages or none */
static int send(plug_params_t *ps, unsigned i, uint8_t v, uint32_t offset, uint64_t frame0, uint8_t ch,
	plug_midi_t *out, unsigned *n, unsigned max)
{
	plug_midi_t m[3];
	const unsigned k = plug_paramMidi(i, v, ch, offset, m);
	if (*n + k > max)
		return 0;
	for (unsigned j = 0; j < k; j++)
		(void)insertMidi(out, n, max, &m[j]);
	ps->p[i].sent = v;
	ps->p[i].sentFrame = frame0 + offset;
	ps->p[i].pending = 0;
	return 1;
}

unsigned plug_paramsHost(plug_params_t *ps, const plug_paramPoint_t *pts, unsigned npts, uint64_t frame0, uint8_t ch,
	plug_midi_t *out, unsigned n, unsigned max)
{
	for (unsigned j = 0; j < npts; j++)
	{
		const unsigned i = pts[j].index;
		if (i >= PLUG_PARAM_COUNT)
			continue;
		plug_paramSync_t *p = &ps->p[i];
		const uint8_t v = pts[j].value > plug_params[i].max ? plug_params[i].max : pts[j].value;
		p->want = v;
		p->shown = 1;
		if (v == p->sent)
		{
			p->pending = 0;	/* the engine has it */
			continue;
		}
		p->pending = 1;	/* until it is sent: no channel, or no room */
		if (ch == PLUG_VALUE_NONE)
			continue;
		(void)send(ps, i, v, pts[j].offset, frame0, ch, out, &n, max);
	}
	return n;
}

unsigned plug_paramsReport(plug_params_t *ps, const uint8_t *values, uint64_t frame, uint64_t frame0, uint8_t ch,
	plug_midi_t *out, unsigned n, unsigned max)
{
	if (frame < ps->epoch)
		return n;	/* an earlier engine's */
	ps->synced = 1;
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		if (values[i] == PLUG_VALUE_NONE)
			continue;
		/* a value beyond the range (a card's own) is its max, as the host can show it */
		const uint8_t fw = values[i] > plug_params[i].max ? plug_params[i].max : values[i];
		plug_paramSync_t *p = &ps->p[i];
		/* the engine's first report of this parameter: one that has no value for it (above) does not count */
		const int first = p->first;
		p->first = 0;
		if (p->want == fw)
		{
			if (first)
				p->sent = fw;	/* the new engine has it */
			p->pending = 0;
			continue;
		}
		if (first && p->want >= 0)
		{
			/* a new engine: the host's value, unless it was given to this engine already */
			if (p->sent == p->want && p->sentFrame >= ps->epoch)
				p->pending = 0;
			else if (ch == PLUG_VALUE_NONE || !send(ps, i, (uint8_t)p->want, 0, frame0, ch, out, &n, max))
			{
				p->pending = 1;	/* no channel or no room: the next block's first frame */
				p->sent = -1;	/* this engine has not got it (what was sent went to the one before) */
			}
			continue;
		}
		if (!first && p->sent >= 0 && frame < p->sentFrame + ps->settle)
			continue;	/* a value given to the engine, not in its report yet */
		p->want = fw;	/* the firmware's own: the host is to get it */
		p->sent = fw;
		p->shown = 0;
		p->pending = 0;
	}
	return n;
}

unsigned plug_paramsPending(plug_params_t *ps, uint8_t ch, uint64_t frame, plug_midi_t *out, unsigned max)
{
	unsigned n = 0;
	if (ch == PLUG_VALUE_NONE)
		return 0;
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		plug_paramSync_t *p = &ps->p[i];
		if (!p->pending)
			continue;
		if (p->want < 0 || p->want == p->sent)
		{
			p->pending = 0;
			continue;
		}
		(void)send(ps, i, (uint8_t)p->want, 0, frame, ch, out, &n, max);
	}
	return n;
}

int plug_paramsToShow(const plug_params_t *ps, unsigned *from, uint8_t *value)
{
	for (unsigned i = *from; i < PLUG_PARAM_COUNT; i++)
		if (!ps->p[i].shown && ps->p[i].want >= 0)
		{
			*value = (uint8_t)ps->p[i].want;
			*from = i + 1u;
			return (int)i;
		}
	*from = PLUG_PARAM_COUNT;
	return -1;
}

static void le32(uint8_t *b, uint32_t v)
{
	for (unsigned i = 0; i < 4; i++)
		b[i] = (uint8_t)(v >> (8u * i));
}

static uint32_t get32(const uint8_t *b)
{
	return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

int plug_paramsWrite(plug_paramsPut_t put, void *ctx, const int *values)
{
	uint8_t b[12 + 5 * PLUG_PARAM_COUNT];
	uint32_t count = 0;
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		if (values[i] >= 0 && values[i] <= plug_params[i].max)
		{
			le32(&b[12 + 5 * count], plug_params[i].id);
			b[12 + 5 * count + 4] = (uint8_t)values[i];
			count++;
		}
	memcpy(b, PLUG_STATE_VALUES_MAGIC, 4);
	le32(&b[4], PLUG_STATE_VALUES_VERSION);
	le32(&b[8], count);
	return put(ctx, b, 12 + 5 * count);
}

int plug_paramsRead(plug_paramsGet_t get, void *ctx, int *values)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		values[i] = -1;
	uint8_t h[12];
	if (get(ctx, h, 4) != 0 || memcmp(h, PLUG_STATE_VALUES_MAGIC, 4) != 0)
		return 0;	/* the prototype's state, or not ours: no values */
	if (get(ctx, h + 4, 8) != 0 || get32(h + 4) != PLUG_STATE_VALUES_VERSION || get32(h + 8) > 4096u)
		return -1;
	const uint32_t count = get32(h + 8);
	for (uint32_t k = 0; k < count; k++)
	{
		uint8_t e[5];
		if (get(ctx, e, 5) != 0)
			return -1;
		const int i = plug_paramIndex(get32(e));
		if (i >= 0 && e[4] <= plug_params[i].max)
			values[i] = e[4];
	}
	return 0;
}
