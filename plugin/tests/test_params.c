/*
 * test_params.c
 *
 * P12: the plugin's parameters (plan E9.4 Task 5): the table, their MIDI, the step of the host's
 * values and the firmware's (plug_paramsHost(), plug_paramsReport()) and their block in the state.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <string.h>

#include "plug_params.h"
#include "seqc_test.h"

#define CH 3u
#define SETTLE 1000u

/* indices in plug_params[] */
enum { I_PATTERN = 0, I_VOL1 = 1, I_PAN4 = 10, I_DJ = 26, I_REV_DECAY = 28, I_FX = 29, I_OUT1 = 30, I_OUT6 = 35 };

static void fwValues(uint8_t *v)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		v[i] = plug_params[i].def;
}

TEST(P12_params_table)
{
	/* ids unique, none 0 (the Engine's); the firmware's parameter of a CC n is n - 1, of an NRPN n
	 * n + 128; defaults within range */
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		const plug_param_t *p = &plug_params[i];
		ASSERT_TRUE(p->id != 0);
		ASSERT_EQ(plug_paramIndex(p->id), (int)i);
		ASSERT_TRUE(p->def <= p->max && p->max > 0);
		if (p->kind == PLUG_PK_CC)
			ASSERT_EQ(p->par, p->number - 1u);
		else if (p->kind == PLUG_PK_NRPN)
			ASSERT_EQ(p->par, p->number + 128u);
		else
			ASSERT_EQ(p->par, PLUG_PAR_PATTERN);
		for (unsigned j = 0; j < i; j++)
			ASSERT_TRUE(plug_params[j].id != p->id);
	}
	ASSERT_EQ(plug_paramIndex(0), -1);
	ASSERT_EQ(plug_paramIndex(9999), -1);
	ASSERT_EQ(plug_params[I_VOL1].number, 89);
	ASSERT_EQ(plug_params[I_PAN4].number, 100);	/* CC 98 and 99 are the NRPN's */
	ASSERT_EQ(plug_params[I_DJ].number, 96);
	ASSERT_EQ(plug_params[I_FX].max, 100);
	ASSERT_EQ(plug_params[I_PATTERN].max, 63);
	/* the voices' outputs: NRPN 87-92, firmware parameters 215-220, seven named values */
	ASSERT_TRUE(plug_params[I_OUT1].number == 87 && plug_params[I_OUT1].par == 215 && plug_params[I_OUT6].par == 220);
	ASSERT_TRUE(plug_params[I_OUT1].max == 6 && plug_params[I_OUT1].names != NULL);
	ASSERT_TRUE(!strcmp(plug_params[I_OUT1].names[1], "St2") && !strcmp(plug_params[I_OUT6].names[6], "FX"));
	ASSERT_TRUE(plug_params[I_VOL1].names == NULL && plug_params[I_PATTERN].names == NULL);
	/* the host's 0-1 to the nearest value */
	ASSERT_EQ(plug_paramValue(I_VOL1, 0.0), 0);
	ASSERT_EQ(plug_paramValue(I_VOL1, 1.0), 127);
	ASSERT_EQ(plug_paramValue(I_VOL1, 40.0 / 127.0), 40);
	ASSERT_EQ(plug_paramValue(I_VOL1, 0.5), 64);
	ASSERT_EQ(plug_paramValue(I_VOL1, 1.5), 127);
	ASSERT_EQ(plug_paramValue(I_VOL1, -1.0), 0);
	ASSERT_EQ(plug_paramValue(I_VOL1, NAN), 0);
	ASSERT_EQ(plug_paramValue(I_PATTERN, 5.0 / 63.0), 5);
	ASSERT_EQ(plug_paramValue(I_FX, 0.5), 50);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		for (unsigned v = 0; v <= plug_params[i].max; v++)
			ASSERT_EQ(plug_paramValue(i, (double)v / plug_params[i].max), v);
}

TEST(P12_params_midi)
{
	plug_midi_t m[3];
	ASSERT_EQ(plug_paramMidi(I_VOL1, 40, CH, 7, m), 1);
	ASSERT_TRUE(m[0].offset == 7 && m[0].len == 3 && m[0].b[0] == 0xB3 && m[0].b[1] == 89 && m[0].b[2] == 40);
	ASSERT_EQ(plug_paramMidi(I_REV_DECAY, 127, 15, 0, m), 3);
	ASSERT_TRUE(m[0].b[0] == 0xBF && m[0].b[1] == 99 && m[0].b[2] == 0);
	ASSERT_TRUE(m[1].b[0] == 0xBF && m[1].b[1] == 98 && m[1].b[2] == 116);
	ASSERT_TRUE(m[2].b[0] == 0xBF && m[2].b[1] == 6 && m[2].b[2] == 127);
	ASSERT_EQ(plug_paramMidi(I_PATTERN, 63, 0x13, 5, m), 1);	/* the channel's low 4 bits */
	ASSERT_TRUE(m[0].offset == 5 && m[0].len == 2 && m[0].b[0] == 0xC3 && m[0].b[1] == 63);
}

TEST(P12_params_host)
{
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	plug_midi_t out[16];
	/* in the queues' order; the MIDI in frame order, stable */
	const plug_paramPoint_t a[] = { { I_DJ, 50, 20 }, { I_DJ, 90, 21 }, { I_VOL1, 10, 40 }, { I_VOL1, 50, 41 } };
	unsigned n = plug_paramsHost(&ps, a, 4, 1000, CH, out, 0, 16);
	ASSERT_EQ(n, 8);
	ASSERT_TRUE(out[0].offset == 10 && out[0].b[1] == 89 && out[0].b[2] == 40);
	ASSERT_TRUE(out[1].offset == 50 && out[1].b[1] == 99);
	ASSERT_TRUE(out[3].offset == 50 && out[3].b[1] == 6 && out[3].b[2] == 20);
	ASSERT_TRUE(out[4].offset == 50 && out[4].b[1] == 89 && out[4].b[2] == 41);
	ASSERT_TRUE(out[7].offset == 90 && out[7].b[2] == 21);
	ASSERT_EQ(ps.p[I_VOL1].want, 41);
	ASSERT_EQ(ps.p[I_VOL1].sent, 41);
	ASSERT_EQ(ps.p[I_VOL1].sentFrame, 1050);
	ASSERT_EQ(ps.p[I_VOL1].shown, 1);
	/* the value the engine has already: no MIDI */
	const plug_paramPoint_t same[] = { { I_VOL1, 0, 41 } };
	ASSERT_EQ(plug_paramsHost(&ps, same, 1, 2000, CH, out, 0, 16), 0);
	/* no channel: the value is the host's, nothing goes to the engine */
	const plug_paramPoint_t pan[] = { { I_PAN4, 3, 9 } };
	ASSERT_EQ(plug_paramsHost(&ps, pan, 1, 3000, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_PAN4].want == 9 && ps.p[I_PAN4].sent == -1 && ps.p[I_PAN4].shown == 1);
	/* full: an NRPN's three messages all or none; the one left out is pending, for the next block's first frame */
	const plug_paramPoint_t two[] = { { I_VOL1, 0, 1 }, { I_DJ, 0, 2 } };
	ASSERT_EQ(plug_paramsHost(&ps, two, 2, 4000, CH, out, 0, 3), 1);
	ASSERT_EQ(ps.p[I_DJ].sent, 21);	/* left out: still the last it got */
	ASSERT_TRUE(ps.p[I_DJ].pending && !ps.p[I_VOL1].pending);
	/* with Drum 4's pan, taken with no channel above, in the table's order */
	ASSERT_EQ(plug_paramsPending(&ps, CH, 4100, out, 16), 4);
	ASSERT_TRUE(out[0].b[2] == 9 && out[3].b[1] == 6 && out[3].b[2] == 2 && ps.p[I_DJ].sent == 2 && !ps.p[I_DJ].pending);
	/* a value beyond the parameter's range is its max; an index beyond the table is left out */
	const plug_paramPoint_t big[] = { { I_FX, 0, 120 }, { PLUG_PARAM_COUNT, 0, 1 } };
	ASSERT_EQ(plug_paramsHost(&ps, big, 2, 5000, CH, out, 0, 16), 3);
	ASSERT_EQ(out[2].b[2], 100);
}

TEST(P12_params_pending)
{
	plug_midi_t out[16];
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	/* taken with no channel (no running engine, or a flush): the host's value, pending, nothing sent */
	const plug_paramPoint_t a[] = { { I_PAN4, 3, 9 }, { I_VOL1, 7, 40 } };
	ASSERT_EQ(plug_paramsHost(&ps, a, 2, 100, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].pending && ps.p[I_PAN4].pending && !ps.p[I_DJ].pending);
	ASSERT_TRUE(ps.p[I_VOL1].want == 40 && ps.p[I_VOL1].sent == -1);
	/* still no channel: they wait */
	ASSERT_EQ(plug_paramsPending(&ps, PLUG_VALUE_NONE, 1000, out, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].pending && ps.p[I_PAN4].pending);
	/* a channel: both go out at offset 0, in the table's order, as plug_paramsHost() would give them */
	ASSERT_EQ(plug_paramsPending(&ps, CH, 1000, out, 16), 2);
	ASSERT_TRUE(out[0].offset == 0 && out[0].len == 3 && out[0].b[0] == 0xB3 && out[0].b[1] == 89 && out[0].b[2] == 40);
	ASSERT_TRUE(out[1].offset == 0 && out[1].b[1] == 100 && out[1].b[2] == 9);
	ASSERT_TRUE(!ps.p[I_VOL1].pending && ps.p[I_VOL1].sent == 40 && ps.p[I_VOL1].sentFrame == 1000);
	ASSERT_TRUE(!ps.p[I_PAN4].pending && ps.p[I_PAN4].sent == 9 && ps.p[I_PAN4].sentFrame == 1000);
	ASSERT_EQ(plug_paramsPending(&ps, CH, 1100, out, 16), 0);
	/* a value taken back before it was sent is not pending (the engine has it), and a later one replaces it */
	const plug_paramPoint_t b[] = { { I_VOL1, 0, 41 } };
	ASSERT_EQ(plug_paramsHost(&ps, b, 1, 1200, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].pending && ps.p[I_VOL1].want == 41);
	const plug_paramPoint_t c[] = { { I_VOL1, 0, 40 } };
	ASSERT_EQ(plug_paramsHost(&ps, c, 1, 1300, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_TRUE(!ps.p[I_VOL1].pending && ps.p[I_VOL1].want == 40);
	ASSERT_EQ(plug_paramsPending(&ps, CH, 1400, out, 16), 0);
	/* a full buffer: an NRPN's three messages all or none, it stays pending; a pattern is one Program Change */
	const plug_paramPoint_t d[] = { { I_DJ, 0, 20 }, { I_PATTERN, 0, 1 } };
	ASSERT_EQ(plug_paramsHost(&ps, d, 2, 1500, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_EQ(plug_paramsPending(&ps, CH, 1600, out, 2), 1);
	ASSERT_TRUE(out[0].len == 2 && out[0].b[0] == 0xC3 && out[0].b[1] == 1 && !ps.p[I_PATTERN].pending);
	ASSERT_TRUE(ps.p[I_DJ].pending && ps.p[I_DJ].sent == -1);
	ASSERT_EQ(plug_paramsPending(&ps, CH, 1700, out, 16), 3);
	ASSERT_TRUE(out[0].b[1] == 99 && out[2].b[1] == 6 && out[2].b[2] == 20 && !ps.p[I_DJ].pending);
	/* with a channel plug_paramsHost() sends at once and leaves nothing pending */
	const plug_paramPoint_t e[] = { { I_VOL1, 5, 50 } };
	ASSERT_EQ(plug_paramsHost(&ps, e, 1, 1800, CH, out, 0, 16), 1);
	ASSERT_TRUE(!ps.p[I_VOL1].pending && ps.p[I_VOL1].sent == 50);

	/* with the first report: a pending value sent in the block before the report is not sent twice, and a new
	 * engine's first report gives a value that was still pending (no channel at the report) when it can */
	plug_params_t q;
	plug_paramsInit(&q, SETTLE);
	plug_paramsEngine(&q, 500);
	uint8_t fw[PLUG_PARAM_COUNT];
	fwValues(fw);
	const plug_paramPoint_t f[] = { { I_VOL1, 0, 40 } };
	ASSERT_EQ(plug_paramsHost(&q, f, 1, 550, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_EQ(plug_paramsPending(&q, CH, 600, out, 16), 1);
	ASSERT_EQ(plug_paramsReport(&q, fw, 500, 600, CH, out, 0, 16), 0);
	ASSERT_TRUE(q.p[I_VOL1].sent == 40 && q.p[I_VOL1].sentFrame == 600 && !q.p[I_VOL1].pending);
	plug_params_t r;
	plug_paramsInit(&r, SETTLE);
	plug_paramsEngine(&r, 500);
	ASSERT_EQ(plug_paramsHost(&r, f, 1, 550, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_EQ(plug_paramsReport(&r, fw, 500, 600, PLUG_VALUE_NONE, out, 0, 16), 0);	/* no channel at the report */
	ASSERT_TRUE(r.p[I_VOL1].pending && r.p[I_VOL1].sent == -1);
	ASSERT_EQ(plug_paramsPending(&r, CH, 700, out, 16), 1);
	ASSERT_TRUE(out[0].b[1] == 89 && out[0].b[2] == 40 && r.p[I_VOL1].sent == 40);
	/* a restored value, not pending (a first report is to give it), whose first report has no channel, then one with no
	 * room: the report makes it pending, and it goes out at the next block's first frame */
	for (int room = 0; room < 2; room++)
	{
		plug_params_t u;
		plug_paramsInit(&u, SETTLE);
		int restored[PLUG_PARAM_COUNT];
		for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
			restored[i] = -1;
		restored[I_VOL1] = 40;
		plug_paramsRestore(&u, restored);
		plug_paramsEngine(&u, 500);
		ASSERT_TRUE(!u.p[I_VOL1].pending && u.p[I_VOL1].want == 40 && fw[I_VOL1] != 40);
		ASSERT_EQ(plug_paramsReport(&u, fw, 500, 600, room ? CH : PLUG_VALUE_NONE, out, 0, room ? 0 : 16), 0);
		ASSERT_TRUE(u.p[I_VOL1].pending && u.p[I_VOL1].sent == -1 && !u.p[I_DJ].pending);
		ASSERT_EQ(plug_paramsPending(&u, CH, 700, out, 16), 1);
		ASSERT_TRUE(out[0].b[1] == 89 && out[0].b[2] == 40 && u.p[I_VOL1].sent == 40 && !u.p[I_VOL1].pending);
	}

	/* a value the firmware changed by itself, or a restored state, ends what is pending */
	plug_params_t o;
	plug_paramsInit(&o, SETTLE);
	plug_paramsEngine(&o, 0);
	ASSERT_EQ(plug_paramsReport(&o, fw, 0, 0, CH, out, 0, 16), 0);
	ASSERT_EQ(plug_paramsHost(&o, f, 1, 10, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_TRUE(o.p[I_VOL1].pending);
	fw[I_VOL1] = 99;
	ASSERT_EQ(plug_paramsReport(&o, fw, 5000, 5000, CH, out, 0, 16), 0);
	ASSERT_TRUE(!o.p[I_VOL1].pending && o.p[I_VOL1].want == 99);
	ASSERT_EQ(plug_paramsPending(&o, CH, 5100, out, 16), 0);
	ASSERT_EQ(plug_paramsHost(&o, f, 1, 5200, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_TRUE(o.p[I_VOL1].pending);
	int values[PLUG_PARAM_COUNT];
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		values[i] = -1;
	plug_paramsRestore(&o, values);
	ASSERT_TRUE(!o.p[I_VOL1].pending);
	ASSERT_EQ(plug_paramsPending(&o, CH, 5300, out, 16), 0);
}

TEST(P12_params_first_report)
{
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	plug_paramsEngine(&ps, 500);
	uint8_t fw[PLUG_PARAM_COUNT];
	fwValues(fw);
	fw[I_FX] = PLUG_VALUE_NONE;
	fw[I_OUT6] = 9;	/* beyond the outputs' 0-6: taken as 6 */
	plug_midi_t out[16];
	/* an earlier engine's report changes nothing */
	ASSERT_EQ(plug_paramsReport(&ps, fw, 499, 600, CH, out, 0, 16), 0);
	ASSERT_EQ(ps.synced, 0);
	/* the host set Drum 1's volume before the engine ran (no channel then) and the DJ filter to the
	 * engine's own value; its first report: the volume goes to it, the others are the host's to get */
	const plug_paramPoint_t a[] = { { I_VOL1, 0, 40 }, { I_DJ, 0, plug_params[I_DJ].def } };
	ASSERT_EQ(plug_paramsHost(&ps, a, 2, 100, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_EQ(plug_paramsReport(&ps, fw, 500, 600, CH, out, 0, 16), 1);
	ASSERT_TRUE(out[0].offset == 0 && out[0].b[1] == 89 && out[0].b[2] == 40);
	ASSERT_TRUE(ps.synced && ps.p[I_VOL1].sent == 40 && ps.p[I_VOL1].sentFrame == 600);
	ASSERT_EQ(ps.p[I_DJ].sent, plug_params[I_DJ].def);
	unsigned from = 0, shows = 0;
	uint8_t v;
	for (int i; (i = plug_paramsToShow(&ps, &from, &v)) >= 0; shows++)
	{
		ASSERT_TRUE(i != I_VOL1 && i != I_DJ && i != I_FX);
		ASSERT_EQ(v, i == I_OUT6 ? 6 : plug_params[i].def);
		ps.p[i].shown = 1;
	}
	ASSERT_EQ(shows, PLUG_PARAM_COUNT - 3);
	ASSERT_EQ(ps.p[I_FX].want, -1);	/* the engine gave no value */
	ASSERT_EQ(ps.p[I_OUT6].want, 6);
	/* a value the host gave to this engine already is not given again */
	plug_paramsEngine(&ps, 700);
	const plug_paramPoint_t b[] = { { I_VOL1, 5, 41 } };
	ASSERT_EQ(plug_paramsHost(&ps, b, 1, 700, CH, out, 0, 16), 1);
	ASSERT_EQ(plug_paramsReport(&ps, fw, 700, 700, CH, out, 0, 16), 0);
}

TEST(P12_params_new_engine)
{
	/* a value the host gave to the first engine is given to the second, whose first report has the
	 * firmware's own: sent == want is not enough, it must have been sent since the new epoch */
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	plug_paramsEngine(&ps, 0);
	uint8_t fw[PLUG_PARAM_COUNT];
	fwValues(fw);
	plug_midi_t out[16];
	ASSERT_EQ(plug_paramsReport(&ps, fw, 0, 0, CH, out, 0, 16), 0);
	const plug_paramPoint_t a[] = { { I_VOL1, 0, 40 } };
	ASSERT_EQ(plug_paramsHost(&ps, a, 1, 100, CH, out, 0, 16), 1);
	ASSERT_TRUE(ps.p[I_VOL1].sent == 40 && ps.p[I_VOL1].sentFrame == 100);
	plug_paramsEngine(&ps, 10000);
	ASSERT_EQ(fw[I_VOL1], 127);
	ASSERT_EQ(plug_paramsReport(&ps, fw, 10000, 10000, CH, out, 0, 16), 1);
	ASSERT_TRUE(out[0].offset == 0 && out[0].len == 3 && out[0].b[0] == 0xB3 && out[0].b[1] == 89 && out[0].b[2] == 40);
	ASSERT_TRUE(ps.p[I_VOL1].sent == 40 && ps.p[I_VOL1].sentFrame == 10000);
}

TEST(P12_params_unknown)
{
	/* a value the engine cannot report yet (PLUG_VALUE_NONE) is not its first report of that parameter: the
	 * host's value waits, and goes to the engine with the first report that has one */
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	int values[PLUG_PARAM_COUNT];
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		values[i] = -1;
	values[I_VOL1] = 40;
	plug_paramsRestore(&ps, values);
	plug_paramsEngine(&ps, 0);
	uint8_t fw[PLUG_PARAM_COUNT];
	fwValues(fw);
	fw[I_VOL1] = PLUG_VALUE_NONE;
	plug_midi_t out[16];
	ASSERT_EQ(plug_paramsReport(&ps, fw, 0, 0, CH, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].want == 40 && ps.p[I_VOL1].sent == -1);
	fw[I_VOL1] = 127;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 64, 64, CH, out, 0, 16), 1);
	ASSERT_TRUE(out[0].offset == 0 && out[0].b[1] == 89 && out[0].b[2] == 40);
	ASSERT_TRUE(ps.p[I_VOL1].want == 40 && ps.p[I_VOL1].sent == 40 && ps.p[I_VOL1].sentFrame == 64);
	/* the engine took it; and the next difference is a later report's, the firmware's own */
	fw[I_VOL1] = 40;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 128, 128, CH, out, 0, 16), 0);
	fw[I_VOL1] = 90;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 64 + SETTLE, 64 + SETTLE, CH, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].want == 90 && ps.p[I_VOL1].shown == 0);
	/* a new engine: pending again for every parameter */
	plug_paramsEngine(&ps, 5000);
	fw[I_VOL1] = PLUG_VALUE_NONE;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 5000, 5000, CH, out, 0, 16), 0);
	fw[I_VOL1] = 127;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 5064, 5064, CH, out, 0, 16), 1);
	ASSERT_TRUE(out[0].b[1] == 89 && out[0].b[2] == 90);
}

TEST(P12_params_own_change)
{
	/* after the first report: a value the firmware changed by itself goes to the host, but not one the
	 * host gave less than SETTLE frames before the report's frame */
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	plug_paramsEngine(&ps, 0);
	uint8_t fw[PLUG_PARAM_COUNT];
	fwValues(fw);
	plug_midi_t out[16];
	ASSERT_EQ(plug_paramsReport(&ps, fw, 0, 0, CH, out, 0, 16), 0);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		ps.p[i].shown = 1;
	const plug_paramPoint_t a[] = { { I_VOL1, 0, 40 } };
	ASSERT_EQ(plug_paramsHost(&ps, a, 1, 10000, CH, out, 0, 16), 1);
	/* the report before the engine took it, and a kit loaded at once: the volume is in flight */
	fw[I_PAN4] = 5;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 10000 + SETTLE - 1, 11000, CH, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].want == 40 && ps.p[I_VOL1].shown == 1);
	ASSERT_TRUE(ps.p[I_PAN4].want == 5 && ps.p[I_PAN4].sent == 5 && ps.p[I_PAN4].shown == 0);
	/* it took it: nothing to tell */
	fw[I_VOL1] = 40;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 10000 + SETTLE - 1, 11000, CH, out, 0, 16), 0);
	ASSERT_EQ(ps.p[I_VOL1].shown, 1);
	/* the firmware sets it back (a kit) after the settle time: the host gets 127 */
	fw[I_VOL1] = 127;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 10000 + SETTLE, 12000, CH, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].want == 127 && ps.p[I_VOL1].sent == 127 && ps.p[I_VOL1].shown == 0);
	/* the host's echo of it is not sent back: no loop */
	const plug_paramPoint_t echo[] = { { I_VOL1, 0, 127 }, { I_PAN4, 0, 5 } };
	ASSERT_EQ(plug_paramsHost(&ps, echo, 2, 12128, CH, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].shown == 1 && ps.p[I_PAN4].shown == 1);
	/* and the next report, the same values, changes nothing */
	ASSERT_EQ(plug_paramsReport(&ps, fw, 12200, 12256, CH, out, 0, 16), 0);
	unsigned from = 0;
	uint8_t v;
	ASSERT_EQ(plug_paramsToShow(&ps, &from, &v), -1);
}

TEST(P12_params_restore)
{
	/* values from a state: the host is to get them, the engine too when it has others */
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	int values[PLUG_PARAM_COUNT];
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		values[i] = -1;
	values[I_VOL1] = 40;
	values[I_PATTERN] = 3;
	values[I_FX] = 101;	/* beyond its range: none */
	plug_paramsRestore(&ps, values);
	ASSERT_TRUE(ps.p[I_VOL1].want == 40 && !ps.p[I_VOL1].shown && ps.p[I_VOL1].sent == -1);
	ASSERT_EQ(ps.p[I_FX].want, -1);
	plug_paramsEngine(&ps, 0);
	uint8_t fw[PLUG_PARAM_COUNT];
	fwValues(fw);
	plug_midi_t out[16];
	ASSERT_EQ(plug_paramsReport(&ps, fw, 0, 0, CH, out, 0, 16), 2);
	ASSERT_TRUE(out[0].b[0] == 0xC3 && out[0].b[1] == 3);	/* the table's order: the pattern first */
	ASSERT_TRUE(out[1].b[1] == 89 && out[1].b[2] == 40);
	unsigned from = 0;
	uint8_t v;
	ASSERT_EQ(plug_paramsToShow(&ps, &from, &v), I_PATTERN);
	ASSERT_EQ(v, 3);
	ASSERT_EQ(plug_paramsToShow(&ps, &from, &v), I_VOL1);
	ASSERT_EQ(v, 40);
	/* a restore while the engine runs: its next report is a first one again */
	plug_paramsRestore(&ps, values);
	ASSERT_EQ(ps.synced, 0);
}

/* a stream in memory */
static uint8_t buf[1024];
static size_t at, len;

static int put(void *ctx, const void *p, size_t n)
{
	(void)ctx;
	if (len + n > sizeof buf)
		return -1;
	memcpy(buf + len, p, n);
	len += n;
	return 0;
}

static int get(void *ctx, void *p, size_t n)
{
	(void)ctx;
	if (at + n > len)
		return -1;
	memcpy(p, buf + at, n);
	at += n;
	return 0;
}

TEST(P12_params_state)
{
	int values[PLUG_PARAM_COUNT], back[PLUG_PARAM_COUNT];
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		values[i] = -1;
	values[I_VOL1] = 40;
	values[I_FX] = 100;
	values[I_PAN4] = 128;	/* beyond its range: left out */
	len = 0;
	ASSERT_EQ(plug_paramsWrite(put, NULL, values), 0);
	ASSERT_EQ(len, 12 + 2 * 5);
	ASSERT_TRUE(!memcmp(buf, "LCHV\1\0\0\0\2\0\0\0\12\0\0\0\50", 17));
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), 0);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		ASSERT_EQ(back[i], i == I_VOL1 ? 40 : i == I_FX ? 100 : -1);
	/* the prototype's state ends after the card: no values */
	len = at = 0;
	back[0] = 7;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), 0);
	ASSERT_EQ(back[0], -1);
	/* something else after the card: no values */
	memcpy(buf, "ABCDxxxx", 8);
	len = 8;
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), 0);
	/* another version, a count beyond 4,096, a block cut short: damaged */
	memcpy(buf, "LCHV\2\0\0\0\0\0\0\0", 12);
	len = 12;
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), -1);
	memcpy(buf, "LCHV\1\0\0\0\1\20\0\0", 12);
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), -1);
	memcpy(buf, "LCHV\1\0\0\0\2\0\0\0\12\0\0\0\50", 17);
	len = 17;
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), -1);
	/* an id this version does not know, and a value beyond a known one's range: left out */
	memcpy(buf, "LCHV\1\0\0\0\2\0\0\0\77\77\0\0\5\12\0\0\0\200", 22);
	len = 22;
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), 0);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		ASSERT_EQ(back[i], -1);
}

TEST(P12_params_lchv)
{
	/* every byte of the version and the count counts: a value that differs from the good one in byte 2 or 3 only is
	 * refused, with entries behind it that would read fine as the good one */
	static const uint8_t entries[10] = { 0x0A, 0, 0, 0, 0x28, 0x0B, 0, 0, 0, 0x14 };
	int back[PLUG_PARAM_COUNT];
	static const struct { const char *what; uint8_t version[4], count[4]; int result; } c[] = {
		{ "good", { 1, 0, 0, 0 }, { 2, 0, 0, 0 }, 0 },
		{ "count byte 2", { 1, 0, 0, 0 }, { 2, 0, 1, 0 }, -1 },
		{ "count byte 3", { 1, 0, 0, 0 }, { 2, 0, 0, 1 }, -1 },
		{ "version byte 2", { 1, 0, 1, 0 }, { 2, 0, 0, 0 }, -1 },
		{ "version byte 3", { 1, 0, 0, 1 }, { 2, 0, 0, 0 }, -1 },
		{ "version byte 1", { 1, 1, 0, 0 }, { 2, 0, 0, 0 }, -1 },
		{ "count byte 1", { 1, 0, 0, 0 }, { 2, 1, 0, 0 }, -1 },
	};
	for (unsigned k = 0; k < sizeof c / sizeof c[0]; k++)
	{
		memcpy(buf, "LCHV", 4);
		memcpy(buf + 4, c[k].version, 4);
		memcpy(buf + 8, c[k].count, 4);
		memcpy(buf + 12, entries, 10);
		len = 22;
		at = 0;
		const int r = plug_paramsRead(get, NULL, back);
		if (r != c[k].result)
			printf("%s: %d, not %d\n", c[k].what, r, c[k].result);
		ASSERT_EQ(r, c[k].result);
	}
}

TEST_MAIN()
