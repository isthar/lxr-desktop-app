/*
 * test_app_engine.c
 *
 * M18 and R4 (stage 1b spec 6.1-6.3, 6.7, 6.8, 12.1): the engine on one thread with a
 * fake clock and a fake output device: the callback's mix and underruns, the
 * splash of the boot, the audio ring and the block rate, the wall clock without a
 * device, the card switch between passes, and the clock-in pulse generator.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_fake.h"

uint8_t BSP_SD_IsDetected(void);	/* emu/stubs/fatfs.h: SD_PRESENT 1, SD_NOT_PRESENT 0 */

static app_engine_t E;

/* boot() in two steps, so that a test can set the fake device's knobs between them */
static void bootStart(int device)
{
	app_host_t h;
	fakeHost(&h, device);
	if (app_engineInit(&E, &h) != 0)
		exit(2);
}

static void bootGo(int dAuto, double dSec)
{
	static char card[1024];
	fakeCard(card, sizeof card, 120u);
	const app_engineConfig_t c = { .cardDir = card, .seed = 1u, .dAuto = dAuto, .dSec = dSec, .mix = APP_MIX_SUM };
	app_enginePowerOn(&E, &c);
}

static void boot(int device)
{
	bootStart(device);
	bootGo(1, 0.0);
}

static app_snapshot_t last;	/* the newest snapshot: runTo() empties the ring at every slice */

static void runTo(double sec)
{
	while ((double)fk.now < 1000.0 + sec * FAKE_TPS)
	{
		app_engineSlice(&E);
		(void)app_engineSnapshot(&E, &last);	/* the ring drops new entries when full (E15) */
	}
}

static int lcdHas(const app_snapshot_t *s, const char *text)
{
	char r[2][17];
	for (unsigned i = 0; i < 2u; i++)
	{
		memcpy(r[i], s->lcd.chars[i], 16);
		r[i][16] = '\0';
	}
	return strstr(r[0], text) != NULL || strstr(r[1], text) != NULL;
}

static int nextAck(app_ack_t *a)	/* the next card acknowledgement; warnings skipped */
{
	while (app_engineAck(&E, a))
		if (a->kind == APP_ACK_CARD_OUT || a->kind == APP_ACK_CARD_IN)
			return 1;
	return 0;
}

static void audioInit(app_audio_t *a, unsigned frames, const int16_t f[4])
{
	memset(a, 0, sizeof *a);
	if (app_ringInit(&a->frames, 8u, 1024u) != 0 || app_ringInit(&a->points, sizeof(app_point_t), 64u) != 0)
		exit(2);
	for (unsigned i = 0; i < frames; i++)
		memcpy(app_ringSlot(&a->frames, i), f, 8u);
	app_ringCommit(&a->frames, frames);
}

TEST(M18_render_mix)
{
	app_audio_t a;
	const int16_t f[4] = { 1000, -2000, 3001, -3 };
	const app_audioTime_t ts = { 5000u, 4000u, 1.0 };
	float out[8];
	audioInit(&a, 4u, f);
	app_audioRender(&a, &ts, 1u, out, 2u);			/* sum, the default (spec 6.2) */
	ASSERT_EQ_DBL(out[0], 2000.0 / 32768.0);		/* (1000 + 3001) >> 1 */
	ASSERT_EQ_DBL(out[1], -1002.0 / 32768.0);		/* (-2000 - 3) >> 1, as --stereo sum */
	atomic_store(&a.mix, APP_MIX_OUT1);
	app_audioRender(&a, &ts, 1u, out, 2u);
	ASSERT_EQ_DBL(out[1], -2000.0 / 32768.0);
	atomic_store(&a.mix, APP_MIX_OUT2);
	app_audioRender(&a, &ts, 1u, out, 2u);
	ASSERT_EQ_DBL(out[0], 3001.0 / 32768.0);
	atomic_store(&a.mix, APP_MIX_FOUR);
	app_audioRender(&a, &ts, 1u, out, 4u);
	ASSERT_EQ_DBL(out[3], -3.0 / 32768.0);
	ASSERT_EQ(atomic_load(&a.underruns), 0);
}

TEST(M18_render_underrun)
{
	app_audio_t a;
	const int16_t f[4] = { 100, 100, 100, 100 };
	const app_audioTime_t ts = { 9000u, 2000u, 1.0 };
	static float out[2 * 470];
	audioInit(&a, 0u, f);
	app_audioRender(&a, &ts, 470u, out, 2u);	/* before the first frame: silence, no underrun */
	ASSERT_EQ(atomic_load(&a.underruns), 0);
	audioInit(&a, 100u, f);
	app_audioRender(&a, &ts, 470u, out, 2u);
	ASSERT_EQ(atomic_load(&a.underruns), 1);
	ASSERT_EQ_DBL(out[199], 100.0 / 32768.0);
	ASSERT_EQ_DBL(out[200], 0.0);
	const app_point_t *p = app_ringPeek(&a.points, 0u);
	ASSERT_EQ(p->frame, 0);
	ASSERT_EQ(p->request, 470);
	ASSERT_EQ(p->got, 100);
	ASSERT_EQ(p->hostTime, 9000);
	ASSERT_EQ(atomic_load(&a.maxRequest), 470);
}

TEST(R4_engine_splash)
{
	boot(1);
	app_engineSlice(&E);	/* the boot: emu_runUntil() returns at its end */
	ASSERT_TRUE(fk.now >= 1000u + 24000000u);	/* paced: 1 s of wall time (spec 4.10) */
	int seen = 0;	/* the ring keeps the boot's first 16 snapshots, taken during HAL_Delay(1000) */
	for (unsigned i = 0; i < app_ringCount(&E.snap); i++)
	{
		const app_snapshot_t *s = app_ringPeek(&E.snap, i);
		seen |= lcdHas(s, "LICHOMAT") && lcdHas(s, "v0.1 unofficial");
	}
	ASSERT_TRUE(seen);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.codecStart, 216000000u);
}

TEST(R4_engine_ring)
{
	boot(1);
	runTo(2.0);
	emu_counters_t c;
	emu_getCounters(&c);
	const uint64_t b0 = c.blocks, h0 = fk.now;
	for (; (double)fk.now < (double)h0 + 2.0 * FAKE_TPS; app_engineSlice(&E))
		ASSERT_TRUE(app_ringCount(&E.audio.frames) <= atomic_load(&E.audio.maxRequest) + APP_RING_SLACK + EMU_BLOCK_FRAMES);
	emu_getCounters(&c);
	ASSERT_NEAR((double)(c.blocks - b0) / ((double)(fk.now - h0) / FAKE_TPS), 689.697, 2.0);	/* spec 6.2 */
	ASSERT_EQ(atomic_load(&E.audio.underruns), 0);
	ASSERT_TRUE(c.underruns <= 1u);
	ASSERT_TRUE(E.map.audio);
	ASSERT_NEAR(E.L * 1e3, 57.0, 0.1);	/* spec 6.4's first row */
}

TEST(R4_engine_wall)
{
	boot(0);	/* no output device, as on the CI runner */
	runTo(5.0);
	ASSERT_NEAR((double)emu_now() / EMU_CPU_HZ, (double)(fk.now - 1000u) / FAKE_TPS, 0.002);
	ASSERT_EQ(last.diag.outputRunning, 0);
	ASSERT_NEAR(last.diag.blockRate, 689.7, 0.5);	/* the window [2 s, 4 s) */
}

TEST(R4_engine_card)
{
	boot(1);
	runTo(1.5);
	app_cmd_t c = { .kind = APP_CMD_CARD_OUT };
	ASSERT_EQ(app_engineCommand(&E, &c), 0);
	app_ack_t a;
	ASSERT_FALSE(nextAck(&a));
	app_engineSlice(&E);	/* applied between passes (spec 6.7) */
	ASSERT_TRUE(nextAck(&a));
	ASSERT_EQ(a.kind, APP_ACK_CARD_OUT);
	ASSERT_EQ(BSP_SD_IsDetected(), 0);
	c.kind = APP_CMD_CARD_IN;
	app_engineCommand(&E, &c);
	app_engineSlice(&E);
	ASSERT_TRUE(nextAck(&a));
	ASSERT_EQ(a.kind, APP_ACK_CARD_IN);
	ASSERT_EQ(BSP_SD_IsDetected(), 1);
}

TEST(R4_engine_generator)
{
	boot(1);
	runTo(1.5);
	const app_cmd_t c = { .kind = APP_CMD_GENERATOR, .a = 1, .b = 4, .x = 120.0 };
	app_engineCommand(&E, &c);
	app_engineSlice(&E);
	const uint64_t t0 = E.genT0;
	while (emu_now() < t0 + EMU_CPU_HZ + 2u * EMU_CYCLES_PER_MS)	/* the last hook then lies past t0 + 1 s */
		app_engineSlice(&E);
	emu_counters_t cnt;
	emu_getCounters(&cnt);
	ASSERT_TRUE(cnt.late <= 1u);	/* only the first pulse, stamped at the command's time, can be late: the hooks are 1.45 ms apart */
	ASSERT_EQ(E.genK, 9);	/* 120 BPM at 4 PPQ: t0 + k x 125 ms for k = 0..8, pulses up to one block (1.45 ms) and 1 ms ahead */
	ASSERT_EQ(E.genNext, t0 + 9u * 27000000u);
}

/* --- the output's life: opens, stalls, retries (spec 6.2, 6.9) and the commands' limits ------------------------ */

static double virtualSec(void) { return (double)emu_now() / EMU_CPU_HZ; }
static double wallSec(void) { return (double)(fk.now - 1000u) / FAKE_TPS; }

/* slices until the fake clock reaches sec, at most maxSlices: a runaway shows as a clock that stands still */
static void runBounded(double sec, unsigned maxSlices)
{
	for (unsigned n = 0; (double)fk.now < 1000.0 + sec * FAKE_TPS && n < maxSlices; n++)
	{
		app_engineSlice(&E);
		(void)app_engineSnapshot(&E, &last);
	}
}

/* slices until the engine has closed the output, at most maxSlices */
static void runUntilClosed(unsigned maxSlices)
{
	for (unsigned n = 0; E.outputRunning && n < maxSlices; n++)
		app_engineSlice(&E);
}

/* the device stops calling back, and no open succeeds from now on */
static void hang(void)
{
	fk.nextCallback = UINT64_MAX;
	fk.device = 0;
}

static void command(const app_cmd_t *c)
{
	(void)app_engineCommand(&E, c);
	app_engineSlice(&E);
}

/* a slow open is a wait, not a hang: the watchdog's 10 s do not count it (spec 4.2), and the stall test counts from the
 * open's return */
TEST(R4_engine_open_bracket)
{
	bootStart(1);
	fk.openTicks = (uint64_t)(12.0 * FAKE_TPS);
	bootGo(1, 0.0);
	ASSERT_EQ(fk.opens, 1);
	ASSERT_FALSE(fk.dogFired);
	ASSERT_TRUE(E.outputRunning);
	ASSERT_EQ(E.lastCallback, fk.now);
	runBounded(16.0, 20000u);
	ASSERT_EQ(fk.opens, 1);
	ASSERT_TRUE(E.outputRunning);
	ASSERT_TRUE(atomic_load(&E.audio.callbacks) > 100u);
}

/* a first callback that comes late, but within 3 s, is no stall */
TEST(R4_engine_slow_first)
{
	bootStart(1);
	fk.firstDelay = (uint64_t)(1.5 * FAKE_TPS);
	bootGo(1, 0.0);
	runBounded(4.0, 20000u);
	ASSERT_EQ(fk.opens, 1);
	ASSERT_EQ(fk.closes, 0);
	ASSERT_TRUE(E.outputRunning);
	ASSERT_TRUE(atomic_load(&E.audio.callbacks) > 100u);
	ASSERT_TRUE(E.map.audio);
}

/* a unit that opens and never calls back is a failed open after 3 s; the wall clock paces for 2 s, then it tries again */
TEST(R4_engine_silent_open)
{
	bootStart(1);
	fk.silent = 1;
	bootGo(1, 0.0);
	runUntilClosed(20000u);
	ASSERT_FALSE(E.outputRunning);
	ASSERT_EQ(fk.closes, 1);
	ASSERT_TRUE(wallSec() > 3.0 && wallSec() < 3.2);
	const double v0 = virtualSec(), w0 = wallSec();
	runBounded(w0 + 1.5, 5000u);
	ASSERT_FALSE(E.outputRunning);
	ASSERT_EQ(fk.opens, 1);
	ASSERT_NEAR(virtualSec() - v0, wallSec() - w0, 0.005);
	runBounded(18.0, 20000u);
	ASSERT_EQ(fk.opens, 4);	/* at 0, 5.1, 10.2 and 15.3 s */
	for (unsigned i = 1; i < fk.opens; i++)
	{
		const double gap = (double)(fk.openAt[i] - fk.openAt[i - 1u]) / FAKE_TPS;
		ASSERT_TRUE(gap >= 5.0 && gap < 5.2);	/* 3 s to the first callback, seen at the wait's 0.1 s steps, then 2 s */
	}
}

/* a callback that is in flight while the unit stops leaves a point (with frames) in the ring: it must not put the map on
 * the audio line, which no callback keeps any more */
TEST(R4_engine_close_inflight)
{
	boot(1);
	fk.flushOnClose = 1;
	runBounded(2.0, 20000u);
	ASSERT_TRUE(E.map.audio);
	hang();
	runUntilClosed(20000u);
	ASSERT_FALSE(E.outputRunning);
	ASSERT_EQ(fk.closes, 1);
	const double v0 = virtualSec(), w0 = wallSec();
	runBounded(w0 + 1.0, 5000u);
	ASSERT_FALSE(E.map.audio);
	ASSERT_NEAR(virtualSec() - v0, wallSec() - w0, 0.005);	/* the wall clock paces: 1 s of each */
	ASSERT_TRUE(wallSec() - w0 > 0.99);
}

/* and a point that arrives after the close is dropped, not mapped */
TEST(R4_engine_late_point)
{
	boot(1);
	runBounded(2.0, 20000u);
	hang();
	runUntilClosed(20000u);
	ASSERT_FALSE(E.outputRunning);
	const app_point_t p = { 0u, fk.now, fk.now, 1.0, 470u, 470u };
	ASSERT_EQ(app_ringPush(&E.audio.points, &p), 0);
	const double v0 = virtualSec(), w0 = wallSec();
	runBounded(w0 + 1.0, 5000u);
	ASSERT_FALSE(E.map.audio);
	ASSERT_EQ(app_ringCount(&E.audio.points), 0);
	ASSERT_NEAR(virtualSec() - v0, wallSec() - w0, 0.005);
	ASSERT_TRUE(wallSec() - w0 > 0.99);
}

/* maxRequest has two writers, the callback and the open: a callback that ran before the open returned keeps its larger
 * value, and a reopen starts from the new nominal request (E6) */
TEST(R4_engine_max_request)
{
	bootStart(1);
	fk.earlyRequest = 1024u;
	bootGo(1, 0.0);
	ASSERT_TRUE(atomic_load(&E.audio.maxRequest) >= 1024u);
	fk.earlyRequest = 0;
	const app_cmd_t c = { .kind = APP_CMD_OUTPUT, .a = 256 };
	command(&c);
	ASSERT_EQ(fk.opens, 2);
	ASSERT_EQ(fk.bufferFrames, 256);
	ASSERT_EQ(atomic_load(&E.audio.maxRequest), 471);	/* ceil(512 x 44,140.625 / 48,000) */
}

/* D is 0 to 1 s and a number */
TEST(R4_engine_cmd_delay)
{
	boot(1);
	app_cmd_t c = { .kind = APP_CMD_DELAY, .a = 0, .x = -0.25 };
	command(&c);
	ASSERT_EQ(E.dAuto, 0);
	ASSERT_EQ(E.dCycles, 0);	/* it used to wrap to 2^64 - 54,000,000 cycles */
	c.x = 0.05;
	command(&c);
	ASSERT_EQ(E.dCycles, 10800000);
	c.x = NAN;
	command(&c);
	ASSERT_EQ(E.dCycles, 10800000);	/* refused: nothing changes */
	c.x = 3.5;
	command(&c);
	ASSERT_EQ(E.dCycles, 216000000);
	c.x = INFINITY;
	command(&c);
	ASSERT_EQ(E.dCycles, 216000000);
}

/* D at power-on is a number from 0 to 1 s as well (one boot per process: three ids) */
TEST(R4_engine_power_dneg)
{
	bootStart(1);
	bootGo(0, -0.5);
	ASSERT_EQ(E.dAuto, 0);
	ASSERT_EQ(E.dCycles, 0);
}

TEST(R4_engine_power_dnan)
{
	bootStart(1);
	bootGo(0, NAN);
	ASSERT_EQ(E.dCycles, 0);
}

TEST(R4_engine_power_dbig)
{
	bootStart(1);
	bootGo(0, 9.0);
	ASSERT_EQ(E.dCycles, 216000000);
}

/* the generator runs from 1 BPM to the firmware's maximum, SEQC_SLAVE_BPM_MAX (300) */
TEST(R4_engine_cmd_generator)
{
	boot(1);
	app_cmd_t c = { .kind = APP_CMD_GENERATOR, .a = 1, .b = 4, .x = 301.0 };
	command(&c);
	ASSERT_FALSE(E.genOn);
	c.x = 300.0;
	command(&c);
	ASSERT_TRUE(E.genOn);
	ASSERT_EQ(E.genMbpm, 300000);
	c.x = 1.0;
	command(&c);
	ASSERT_TRUE(E.genOn);
	c.x = 0.5;
	command(&c);
	ASSERT_FALSE(E.genOn);
	c.x = 120.0;
	c.b = 5;
	command(&c);
	ASSERT_FALSE(E.genOn);
	c.b = 24;
	command(&c);
	ASSERT_TRUE(E.genOn);
	c.x = NAN;
	command(&c);
	ASSERT_FALSE(E.genOn);
	c.x = INFINITY;
	command(&c);
	ASSERT_FALSE(E.genOn);
	c.x = 1e12;	/* an interval of 0 cycles: the loop of pulses would never end */
	command(&c);
	ASSERT_FALSE(E.genOn);
	c.x = 120.0;
	c.b = 4;
	command(&c);
	ASSERT_TRUE(E.genOn);
	command(&c);	/* the new command restarts it at the slice's time */
	ASSERT_TRUE(E.genK < 3u);
}

/* a text without its NUL is cut, and read only up to the end of the field */
TEST(R4_engine_cmd_text)
{
	boot(1);
	app_cmd_t c = { .kind = APP_CMD_OUTPUT, .a = 256 };
	memset(c.text, 'A', sizeof c.text);
	command(&c);
	ASSERT_EQ(fk.opens, 2);
	ASSERT_EQ(fk.bufferFrames, 256);
	ASSERT_EQ(strlen(fk.uid), 127);
	ASSERT_EQ(fk.uid[126], 'A');
}

TEST_MAIN()
