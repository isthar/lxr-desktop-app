/*
 * test_app_m18.c
 *
 * M18 (stage 1b spec 6.4, 6.5, 12.1): the engine's MIDI paths on one thread with a
 * fake clock, a fake output device and a fake DAW that hands each MIDI clock over
 * at its exact time: the input latency D (+ 0 to 1 block) at 90, 128 and 174 BPM
 * on USB, at 128 BPM on DIN, with D = 0 the arrival phase, and the MIDI-out step; then
 * the MIDI out of the master at 120 BPM: the wall clock's render lead, a held-up
 * firmware thread, a reopen, a hang and a step of the audio line.
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
		ASSERT_EQ(app_ringPush(&E.midiOut[m[i].port == APP_PORT_DIN], &m[i]), 0);
	const app_midiOutSink_t sink = { NULL, recordSent };
	/* DIN goes at once, with its stamp, though a USB entry that is not due sits at the head of the ring (E11, spec 6.5):
	 * CoreMIDI schedules it by the stamp; USB waits for its presentation time */
	ASSERT_EQ(app_midiOutStep(&E, 500u, &sink), 1000);
	ASSERT_EQ(sent.n, 1);
	ASSERT_EQ(sent.port[0], APP_PORT_DIN);
	ASSERT_EQ(sent.when[0], 5000);
	const app_midiEntry_t later = { 5500u, APP_PORT_DIN, 1u, { 0x20903E64u } };
	ASSERT_EQ(app_ringPush(&E.midiOut[1], &later), 0);	/* it arrives while the thread waits for the USB entry */
	ASSERT_EQ(app_midiOutStep(&E, 500u, &sink), 1000);
	ASSERT_EQ(sent.n, 2);
	ASSERT_EQ(sent.when[1], 5500);
	ASSERT_EQ(app_midiOutStep(&E, 1000u, &sink), 6000);	/* the USB entry that is due; the next waits */
	ASSERT_EQ(sent.n, 3);
	ASSERT_EQ(sent.port[2], APP_PORT_USB);
	ASSERT_EQ(app_midiOutStep(&E, 7000u, &sink), 0);
	ASSERT_EQ(sent.n, 4);
}

/* --- MIDI out at 120 BPM in wall-clock pacing (spec 6.3, 6.5; A4) -------------------------------------------------- */

#define OUTMAX 512u
#define MS (FAKE_TPS / 1000.0)
static struct
{
	uint64_t f8Stamp[OUTMAX], f8At[OUTMAX], dinStamp[OUTMAX], dinAt[OUTMAX], last[2], faStamp;
	unsigned nF8, nDin, f8BeforeFa, backwards[2];
	int fa;
} out;
static app_snapshot_t seen;	/* the newest snapshot */

/* the MIDI-out thread as a perfect one: it drains at every push, so the fake clock of a push is the entry's arrival */
static void logOut(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n)
{
	(void)ctx;
	out.backwards[port == APP_PORT_DIN] += when < out.last[port == APP_PORT_DIN];
	out.last[port == APP_PORT_DIN] = when;
	for (unsigned i = 0; i < n; i++)
	{
		if (port == APP_PORT_USB && w[i] == 0x10F80000u && out.nF8 < OUTMAX)
		{
			out.f8Stamp[out.nF8] = when;
			out.f8At[out.nF8++] = fk.now;
		}
		else if (port == APP_PORT_USB && w[i] == 0x10FA0000u && !out.fa)
		{
			out.fa = 1;
			out.faStamp = when;
			out.f8BeforeFa = out.nF8;
		}
		else if (port == APP_PORT_DIN && (w[i] & 0xFFF00000u) == 0x20900000u && (w[i] & 0x7Fu) != 0u && out.nDin < OUTMAX)
		{
			out.dinStamp[out.nDin] = when;
			out.dinAt[out.nDin++] = fk.now;
		}
	}
}

static void drainOnPush(void *ctx)
{
	(void)ctx;
	const app_midiOutSink_t sink = { NULL, logOut };
	(void)app_midiOutStep(&E, UINT64_MAX, &sink);
}

/* The init card at 120 BPM on its own clock, Start at 1.5 s, until 8 s. With a device, it stops calling back at hangAt
 * seconds and the engine falls back to the wall clock; at reopenAt the output is reopened, and the new unit takes 100 ms
 * to its first callback; at jumpAt its clock steps back 30 ms (the audio line moves back). stall: the firmware thread is held up that long every 400 ms from 2 s on (a page fault, the
 * card's file I/O; the first clocks of R4_dev_midiout were up to 17 ms late) */
static void master120(int device, uint64_t stall, double hangAt, double reopenAt, double jumpAt)
{
	app_host_t h;
	fakeHost(&h, device);
	h.midiOutWake = drainOnPush;
	if (app_engineInit(&E, &h) != 0)
		exit(2);
	static char card[1024];
	fakeCard(card, sizeof card, 120u);
	const app_engineConfig_t c = { .cardDir = card, .seed = 1u, .dAuto = 1, .mix = APP_MIX_SUM };
	app_enginePowerOn(&E, &c);
	memset(&out, 0, sizeof out);
	app_panelEvent_t ev = { .kind = APP_P_BUTTON, .index = 33u };
	int pressed = 0, released = 0, hung = 0, reopened = 0, jumped = 0;
	uint64_t nextStall = 1000u + (uint64_t)(2.0 * FAKE_TPS);
	while (fk.now < 1000u + (uint64_t)(8.0 * FAKE_TPS))
	{
		if (!pressed && fk.now >= 1000u + (uint64_t)(1.5 * FAKE_TPS))
		{
			ev.value = 1;
			(void)app_enginePanel(&E, &ev);
			pressed = 1;
		}
		else if (pressed && !released && fk.now >= 1000u + (uint64_t)(1.55 * FAKE_TPS))
		{
			ev.value = 0;
			(void)app_enginePanel(&E, &ev);
			released = 1;
		}
		if (hangAt > 0.0 && !hung && fk.now >= 1000u + (uint64_t)(hangAt * FAKE_TPS))
		{
			fk.nextCallback = UINT64_MAX;
			fk.device = 0;
			hung = 1;
		}
		if (reopenAt > 0.0 && !reopened && fk.now >= 1000u + (uint64_t)(reopenAt * FAKE_TPS))
		{
			const app_cmd_t cmd = { .kind = APP_CMD_OUTPUT, .a = 256 };
			fk.firstDelay = (uint64_t)(0.1 * FAKE_TPS);
			(void)app_engineCommand(&E, &cmd);
			reopened = 1;
		}
		if (jumpAt > 0.0 && !jumped && fk.now >= 1000u + (uint64_t)(jumpAt * FAKE_TPS))
		{
			fk.nextCallback -= (uint64_t)(0.030 * FAKE_TPS);
			jumped = 1;
		}
		if (stall && fk.now >= nextStall)
		{
			fk.now += stall;
			nextStall += (uint64_t)(0.4 * FAKE_TPS);
		}
		app_engineSlice(&E);
		(void)app_engineSnapshot(&E, &seen);
	}
}

/* A4: F8 intervals of 20.83 +- 1.5 ms. The firmware restarts its clock phase at Start (below), so the one interval that
 * spans the FA is left out */
static void spacingOk(const uint64_t *t, unsigned n, unsigned skip, unsigned skip2)
{
	unsigned bad = 0;
	ASSERT_TRUE(n >= 100u);
	for (unsigned i = 1; i < n; i++)
	{
		const double d = (double)(t[i] - t[i - 1u]) / MS;
		if (i != skip && i != skip2 && !(d >= 20.8333 - 1.5 && d <= 20.8333 + 1.5))
		{
			printf("F8 %u: %.3f ms after the one before, at %.3f ms\n", i, d, (double)t[i] / MS);
			bad++;
		}
	}
	ASSERT_EQ(bad, 0);
}

/* what the firmware itself does at Start, in virtual time: the FA, and one ms later an F8 that restarts the phase of the
 * clocks, wherever the free-running ones were. The interval from the last F8 before Start to that one is anything from 0
 * to 20.8 ms (2.7 ms at this Start, 3.5 ms in some runs of R4_dev_midiout); the next one is about 20 ms */
TEST(M18_out_start_phase)
{
	master120(0, 0, 0.0, 0.0, 0.0);
	ASSERT_TRUE(out.fa);
	ASSERT_TRUE(out.f8BeforeFa >= 20u && out.f8BeforeFa + 100u < out.nF8);
	const unsigned k = out.f8BeforeFa;	/* the first F8 after the FA */
	ASSERT_NEAR((double)(out.f8Stamp[k] - out.faStamp) / MS, 1.0, 0.01);
	ASSERT_TRUE((double)(out.f8Stamp[k] - out.f8Stamp[k - 1u]) / MS < 20.8333 + 1.5);
	ASSERT_NEAR((double)(out.f8Stamp[k + 1u] - out.f8Stamp[k]) / MS, 20.0, 0.5);
	spacingOk(out.f8Stamp, out.nF8, k, 0u);
	ASSERT_EQ(out.backwards[0], 0);
	ASSERT_EQ(out.backwards[1], 0);
}

/* the render lead of wall-clock pacing: an entry is in the ring about 25 ms before its stamp, so that a firmware thread
 * that is held up for 17 ms still delivers on time */
TEST(M18_out_lead)
{
	master120(0, 0, 0.0, 0.0, 0.0);
	ASSERT_TRUE(out.nF8 >= 100u && out.nDin >= 8u);
	for (unsigned i = 0; i < out.nF8; i++)
	{
		const double lead = ((double)out.f8Stamp[i] - (double)out.f8At[i]) / MS;
		ASSERT_TRUE(lead >= 24.0 && lead <= 27.5);	/* 25 ms and up to a block (1.45 ms) of the hook's spacing */
	}
	for (unsigned i = 0; i < out.nDin; i++)
		ASSERT_TRUE((double)out.dinStamp[i] - (double)out.dinAt[i] >= 24.0 * MS);
}

TEST(M18_out_stall)
{
	master120(0, (uint64_t)(0.017 * FAKE_TPS), 0.0, 0.0, 0.0);
	ASSERT_TRUE(out.nF8 >= 100u && out.nDin >= 8u);
	for (unsigned i = 0; i < out.nF8; i++)
		ASSERT_TRUE(out.f8Stamp[i] >= out.f8At[i]);	/* never later than the stamp */
	for (unsigned i = 0; i < out.nDin; i++)
		ASSERT_TRUE(out.dinStamp[i] >= out.dinAt[i]);
}

/* The index of the one F8 interval that is longer than 100 ms: the firmware thread's wait for a callback */
static unsigned longWait(void)
{
	unsigned wait = 0;
	for (unsigned i = 1; i < out.nF8; i++)
	{
		if ((double)(out.f8Stamp[i] - out.f8Stamp[i - 1u]) / MS > 100.0)
		{
			if (wait)
				return 0u;	/* more than one */
			wait = i;
		}
	}
	return wait;
}

/* The audio line is ahead of the host clock by the render lead (60 ms and more). A reopen puts the wall clock in its
 * place until the ring is full and the new unit's first callback sets the line again; the wall clock carries on from
 * where the audio line was, or the stamps would step back by the lead (spec 6.3). Left out of the spacing: the F8 where
 * the audio line first replaces the wall clock's, at the boot, and the wait for the new unit's first callback */
TEST(M18_out_reopen)
{
	master120(1, 0, 0.0, 3.0, 0.0);
	ASSERT_EQ(fk.opens, 2);
	ASSERT_TRUE(E.map.audio);
	ASSERT_EQ(out.backwards[0], 0);
	ASSERT_EQ(out.backwards[1], 0);
	for (unsigned i = 1; i < out.nF8; i++)
		ASSERT_TRUE(out.f8Stamp[i] > out.f8Stamp[i - 1u]);
	const unsigned wait = longWait();
	ASSERT_TRUE(wait > 0u);
	spacingOk(out.f8Stamp, out.nF8, 1u, wait);
}

/* The device stops: the firmware thread waits half a second for a callback, then the wall clock takes over from where the
 * audio line was. That interval is the wait; every other one keeps its 20.83 ms */
TEST(M18_out_hang)
{
	master120(1, 0, 3.0, 0.0, 0.0);
	ASSERT_FALSE(E.map.audio);
	for (unsigned i = 1; i < out.nF8; i++)
		ASSERT_TRUE(out.f8Stamp[i] > out.f8Stamp[i - 1u]);
	const unsigned wait = longWait();
	ASSERT_TRUE(wait > 0u);
	ASSERT_EQ(out.backwards[0], 0);
	ASSERT_EQ(out.backwards[1], 0);
	spacingOk(out.f8Stamp, out.nF8, 1u, wait);
}

/* The audio line moves back (the device's clock steps by 30 ms): the map re-anchors, and what it stamps next is earlier than
 * what went out. A port's stamps are held at the last one, so the receiver gets the messages in order, and a burst is all
 * that the step costs */
TEST(M18_out_jump)
{
	master120(1, 0, 0.0, 0.0, 3.0);
	ASSERT_TRUE(E.map.audio);
	ASSERT_TRUE(E.map.jumps >= 1u);
	ASSERT_EQ(out.backwards[0], 0);
	ASSERT_EQ(out.backwards[1], 0);
	for (unsigned i = 1; i < out.nF8; i++)
		ASSERT_TRUE(out.f8Stamp[i] >= out.f8Stamp[i - 1u]);
}

TEST_MAIN()
