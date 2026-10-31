/*
 * test_app_m18.c
 *
 * M18 (stage 1b spec 6.4, 6.5, 12.1): the engine's MIDI paths on one thread with a
 * fake clock, a fake output device and a fake DAW that hands each MIDI clock over
 * at its exact time: the input latency D (+ 0 to 1 block) at 90, 128 and 174 BPM
 * on USB, at 128 BPM on DIN, with D = 0 the arrival phase, and the MIDI-out step.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_fake.h"

#define MAXK 4096u
static app_engine_t E;
/* the fake DAW (spec 12.1 M18): each clock stamped with its exact time and handed over at it (spec 6.4) */
static struct { uint64_t t0, start, k, stamps[MAXK]; double per; int started; uint8_t port; } daw;
static struct { uint64_t t[256]; unsigned n; } notes;	/* the first USB Note On of each step, presented */
static struct { uint64_t when[8]; uint8_t port[8]; unsigned n; } sent;
static app_snapshot_t snap;	/* the newest snapshot: slave() empties the ring at every slice */

static uint64_t dawClock(uint64_t k)
{
	return daw.t0 + (uint64_t)llround((double)k * daw.per);
}

static void dawEvent(fake_t *f)
{
	uint32_t w = 0x10F80000u;
	if (!daw.started && f->nextEvent == daw.start)
	{
		w = 0x10FA0000u;
		daw.started = 1;
	}
	else if (daw.k < MAXK)
		daw.stamps[daw.k++] = f->nextEvent;
	app_midiSplit(f->nextEvent, daw.port, &w, 1u, app_engineMidiPush, &E);
	const uint64_t next = dawClock(daw.k);
	f->nextEvent = !daw.started && daw.start < next ? daw.start : next;
}

static void recordNotes(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n)
{
	(void)ctx;
	for (unsigned i = 0; i < n; i++)
		if (port == APP_PORT_USB && (w[i] & 0xFFF00000u) == 0x20900000u && (w[i] & 0x7Fu) != 0u && notes.n < 256u
			&& (notes.n == 0u || when > notes.t[notes.n - 1u] + 5u * 24000u))
			notes.t[notes.n++] = when;	/* 7 voices share a step */
}

static void recordSent(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n)
{
	(void)ctx;
	(void)w;
	(void)n;
	sent.when[sent.n] = when;
	sent.port[sent.n++] = port;
}

/* the init card in MID sync, a DAW clock from host 2 s, Start 1 ms before clock 48, until host 10 s */
static void slave(double bpm, int dAuto, uint8_t port)
{
	app_host_t h;
	fakeHost(&h, 1);
	if (app_engineInit(&E, &h) != 0)
		exit(2);
	static char card[1024];
	fakeCard(card, sizeof card, 1u);
	const app_engineConfig_t c = { .cardDir = card, .seed = 1u, .dAuto = dAuto, .mix = APP_MIX_SUM };
	app_enginePowerOn(&E, &c);
	memset(&daw, 0, sizeof daw);
	memset(&notes, 0, sizeof notes);
	daw.port = port;
	daw.per = FAKE_TPS * 60.0 / (bpm * 24.0);
	daw.t0 = 1000u + 2u * 24000000u;
	daw.start = dawClock(48u) - 24000u;
	fk.event = dawEvent;
	fk.nextEvent = daw.t0;
	const app_midiOutSink_t sink = { NULL, recordNotes };
	while (fk.now < 1000u + 10u * 24000000u)
	{
		app_engineSlice(&E);
		(void)app_midiOutStep(&E, UINT64_MAX, &sink);
		(void)app_engineSnapshot(&E, &snap);	/* the ring drops new entries when full (E15) */
	}
}

/* latency of step n's Note On against its clock: steps 1, 5, 9 and 13 fall every 24 clocks from clock 48 */
static double latency(unsigned n)
{
	return ((double)notes.t[n] - (double)daw.stamps[48u + 24u * n]) / FAKE_TPS;
}

static void constant(double bpm, uint8_t port)
{
	slave(bpm, 1, port);
	const double D = (double)E.dCycles / EMU_CPU_HZ;
	ASSERT_NEAR(E.L * 1e3, 57.0, 0.1);
	ASSERT_NEAR(D, E.L + APP_D_MARGIN, 1e-6);	/* D's default */
	ASSERT_TRUE(notes.n >= 8u);
	for (unsigned n = 0; n < notes.n && 48u + 24u * n < daw.k; n++)
		ASSERT_TRUE(latency(n) >= D - 0.0001 && latency(n) <= D + 0.00145);	/* D + 0-1.45 ms */
	/* L is at least every measured lead; 0.05 ms covers the points' rounding (about 11 us) */
	ASSERT_TRUE(snap.diag.leadMax <= E.L + 0.00005);
	ASSERT_NEAR(snap.diag.inLatMin, D, 0.0002);	/* with D at least L, the input latency is D (spec 6.9) */
	ASSERT_NEAR(snap.diag.inLatMax, D, 0.0002);
	ASSERT_EQ(snap.diag.midiStale, 0);
}

TEST(M18_slave_90) { constant(90.0, APP_PORT_USB); }
TEST(M18_slave_128) { constant(128.0, APP_PORT_USB); }
TEST(M18_slave_174) { constant(174.0, APP_PORT_USB); }
TEST(M18_din_128) { constant(128.0, APP_PORT_DIN); }	/* A6's path: the DIN wire model */

TEST(M18_zero_d)
{
	slave(128.0, 0, APP_PORT_USB);
	ASSERT_TRUE(notes.n >= 8u);
	for (unsigned n = 0; n < notes.n && 48u + 24u * n < daw.k; n++)
		ASSERT_TRUE(latency(n) >= 0.0 && latency(n) <= E.L + 0.00145);
	ASSERT_TRUE(snap.diag.inLatMax - snap.diag.inLatMin >= 0.010);	/* D = 0: the arrival phase shows (spec 12.1 M18) */
}

TEST(M18_midi_out_step)
{
	app_host_t h;
	fakeHost(&h, 1);
	ASSERT_EQ(app_engineInit(&E, &h), 0);
	const app_midiEntry_t m[3] = { { 1000u, APP_PORT_USB, 1u, { 0x10F80000u } },
		{ 5000u, APP_PORT_DIN, 1u, { 0x20903C64u } }, { 6000u, APP_PORT_USB, 1u, { 0x10F80000u } } };
	for (unsigned i = 0; i < 3u; i++)
		ASSERT_EQ(app_ringPush(&E.midiOut, &m[i]), 0);
	const app_midiOutSink_t sink = { NULL, recordSent };
	ASSERT_EQ(app_midiOutStep(&E, 500u, &sink), 1000);	/* USB waits for its presentation time */
	ASSERT_EQ(sent.n, 0);
	ASSERT_EQ(app_midiOutStep(&E, 1000u, &sink), 6000);	/* DIN goes at once, with its stamp (E11) */
	ASSERT_EQ(sent.n, 2);
	ASSERT_EQ(sent.port[1], APP_PORT_DIN);
	ASSERT_EQ(sent.when[1], 5000);
	ASSERT_EQ(app_midiOutStep(&E, 7000u, &sink), 0);
	ASSERT_EQ(sent.n, 3);
}

TEST_MAIN()
