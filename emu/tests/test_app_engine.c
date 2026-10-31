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
	if (app_ringInit(&a->frames, sizeof(app_frame_t), 1024u) != 0 || app_ringInit(&a->points, sizeof(app_point_t), 64u) != 0)
		exit(2);
	for (unsigned i = 0; i < frames; i++)
	{
		app_frame_t *s = app_ringSlot(&a->frames, i);
		memcpy(s->s, f, sizeof s->s);
		s->clock = (int16_t)(i & 1u);	/* the clock-out jack high on the odd frames (R5) */
	}
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
		seen |= lcdHas(s, "LICHOMAT") && lcdHas(s, "v" EMU_TEST_FW_VERSION " unofficial");
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


/* E32: with a device the block rate is the time map's (frames over host time over 64), not a count of blocks over 2 s.
 * A callback takes a whole request, so the engine renders about 7 blocks at once, and a count over a window that starts
 * before one and ends after another is a block off: 690.16 in every window here, 690.1 on the real CoreAudio, while the
 * map's slope is the device's own 689.70. */
TEST(R4_engine_blockrate)
{
	boot(1);
	uint64_t wh = E.winHost, wb = E.winBlocks;
	unsigned windows = 0;
	double countMin = 1e9;
	for (unsigned n = 0; windows < 5u && n < 200000u; n++)
	{
		app_engineSlice(&E);
		(void)app_engineSnapshot(&E, &last);
		if (E.winHost == wh)
			continue;
		const double count = (double)(E.winBlocks - wb) / ((double)(E.winHost - wh) / FAKE_TPS);	/* the old rule */
		if (windows++ > 0u)	/* the first window spans the boot's wall-clock pacing */
		{
			ASSERT_TRUE(E.map.audio);
			ASSERT_NEAR(last.diag.blockRate, 689.70, 0.02);
			countMin = count < countMin ? count : countMin;
		}
		wh = E.winHost;
		wb = E.winBlocks;
	}
	ASSERT_EQ(windows, 5);
	ASSERT_TRUE(countMin >= 690.0 && countMin < 691.0);	/* what the old rule said in every one of them */
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

/* an acknowledgement carries the number of its command, one per command and in order, so that the sender can tell its own
 * from one of an operation that has timed out */
TEST(R4_engine_cardseq)
{
	boot(1);
	runTo(1.5);
	app_cmd_t c = { .kind = APP_CMD_CARD_OUT, .a = 7 };
	ASSERT_EQ(app_engineCommand(&E, &c), 0);
	c.kind = APP_CMD_CARD_IN;	/* both in one slice: two acknowledgements */
	c.a = 9;
	ASSERT_EQ(app_engineCommand(&E, &c), 0);
	app_engineSlice(&E);
	app_ack_t a;
	ASSERT_TRUE(nextAck(&a));
	ASSERT_EQ(a.kind, APP_ACK_CARD_OUT);
	ASSERT_EQ(a.seq, 7);
	ASSERT_TRUE(nextAck(&a));
	ASSERT_EQ(a.kind, APP_ACK_CARD_IN);
	ASSERT_EQ(a.seq, 9);
	ASSERT_FALSE(nextAck(&a));
	c.kind = APP_CMD_CARD_OUT;
	c.a = 11;
	command(&c);
	ASSERT_TRUE(nextAck(&a));
	ASSERT_EQ(a.seq, 11);
}

/* from CARD_OUT to CARD_IN the card is the operation's: the panel's switch is left alone until then */
TEST(R4_engine_cardheld)
{
	boot(1);
	runTo(1.5);
	app_cmd_t c = { .kind = APP_CMD_CARD_OUT, .a = 1 };
	command(&c);
	ASSERT_EQ(BSP_SD_IsDetected(), 0);
	app_panelEvent_t ev = { .kind = APP_P_CARD, .value = 1 };
	ASSERT_EQ(app_enginePanel(&E, &ev), 0);
	for (unsigned i = 0; i < 20u; i++)
		app_engineSlice(&E);
	ASSERT_EQ(BSP_SD_IsDetected(), 0);	/* a click on the switch does not put it in */
	c.kind = APP_CMD_CARD_IN;
	c.a = 2;
	command(&c);
	ASSERT_EQ(BSP_SD_IsDetected(), 1);
	ev.value = 0;
	ASSERT_EQ(app_enginePanel(&E, &ev), 0);
	for (unsigned i = 0; i < 20u; i++)
		app_engineSlice(&E);
	ASSERT_EQ(BSP_SD_IsDetected(), 0);	/* and afterwards the switch works again */
}

/* from CARD_OUT to CARD_IN the panel's switch is not applied, but it is not lost either: CARD_IN puts the card in as the
 * switch stands (review of d6fbcd7) */
TEST(R4_engine_cardswitch)
{
	boot(1);
	runTo(1.5);
	app_cmd_t c = { .kind = APP_CMD_CARD_OUT, .a = 1 };
	command(&c);
	ASSERT_EQ(BSP_SD_IsDetected(), 0);
	app_panelEvent_t ev = { .kind = APP_P_CARD, .value = 0 };	/* the user flips the switch out while the operation runs */
	ASSERT_EQ(app_enginePanel(&E, &ev), 0);
	for (unsigned i = 0; i < 20u; i++)
		app_engineSlice(&E);
	c.kind = APP_CMD_CARD_IN;
	c.a = 2;
	command(&c);
	for (unsigned i = 0; i < 20u; i++)
		app_engineSlice(&E);
	ASSERT_EQ(BSP_SD_IsDetected(), 0);	/* the switch is out: the card stays out (it used to come in) */
	ev.value = 1;
	ASSERT_EQ(app_enginePanel(&E, &ev), 0);
	for (unsigned i = 0; i < 20u; i++)
		app_engineSlice(&E);
	ASSERT_EQ(BSP_SD_IsDetected(), 1);	/* and the switch works again */
	c.kind = APP_CMD_CARD_OUT;
	c.a = 3;
	command(&c);
	ev.value = 0;	/* out and in again during the operation: the last position counts */
	ASSERT_EQ(app_enginePanel(&E, &ev), 0);
	app_engineSlice(&E);
	ev.value = 1;
	ASSERT_EQ(app_enginePanel(&E, &ev), 0);
	app_engineSlice(&E);
	c.kind = APP_CMD_CARD_IN;
	c.a = 4;
	command(&c);
	for (unsigned i = 0; i < 20u; i++)
		app_engineSlice(&E);
	ASSERT_EQ(BSP_SD_IsDetected(), 1);
}

/* A8, E15: the main thread drains the snapshots every 2 or 3 and keeps the newest. The latch of the ones it drops
 * (the clock-out pulse, the LEDs) goes into it, or a pulse in a dropped snapshot is never seen. 120 BPM at 4 PPQ: 8 a second */
TEST(R4_engine_clockled)
{
	boot(1);
	runTo(1.5);
	uint64_t next = emu_now();
	unsigned pulses = 0, seen = 0, target = 2;
	const uint64_t end = emu_now() + 6u * EMU_CPU_HZ;
	app_snapshot_t s;
	for (unsigned tail = 0; tail < 60u;)	/* after the pulses, 60 slices for the last one to come out */
	{
		if (emu_now() < end)
		{
			if (emu_now() >= next)
			{
				E.clockSeen = 1;	/* what the firmware's clock-out sink latches (app_sinkClock) */
				pulses++;
				next += EMU_CPU_HZ / 8u;
			}
		}
		else
			tail++;
		app_engineSlice(&E);
		if (app_ringCount(&E.snap) >= target)
		{
			seen += app_engineSnapshot(&E, &s) && s.clockOut;
			target = target == 2u ? 3u : 2u;
		}
	}
	while (app_engineSnapshot(&E, &s))
		seen += s.clockOut;
	ASSERT_EQ(pulses, 48);
	ASSERT_EQ(seen, pulses);	/* about 40 % of them were lost */
}

static void fakeSnap(uint64_t leds, uint8_t clockOut, uint64_t vt)
{
	app_snapshot_t s;
	memset(&s, 0, sizeof s);
	s.leds = leds;
	s.clockOut = clockOut;
	s.vt = vt;
	s.diag.callbacks = vt;
	ASSERT_EQ(app_ringPush(&E.snap, &s), 0);
}

TEST(R4_engine_snapshot_latch)
{
	bootStart(1);	/* the rings only */
	fakeSnap(1u, 1u, 10u);
	fakeSnap(2u, 0u, 20u);
	fakeSnap((uint64_t)1 << 40, 0u, 30u);
	app_snapshot_t s;
	ASSERT_EQ(app_engineSnapshot(&E, &s), 1);
	ASSERT_EQ(s.vt, 30);							/* the newest one's own values */
	ASSERT_EQ(s.diag.callbacks, 30);
	ASSERT_EQ(s.leds, ((uint64_t)1 << 40) | 3u);	/* with the latch of the two before it */
	ASSERT_EQ(s.clockOut, 1);
	ASSERT_EQ(app_engineSnapshot(&E, &s), 0);
	fakeSnap(4u, 0u, 40u);
	ASSERT_EQ(app_engineSnapshot(&E, &s), 1);
	ASSERT_EQ(s.leds, 4);							/* what was drained is not latched again */
	ASSERT_EQ(s.clockOut, 0);
}

/* a lead that a closed audio line carries over to the wall clock lasts that wall stretch only (review of d6fbcd7): the
 * next time the callbacks put the map back on the audio line it is the nominal one again, so that it cannot only grow */
TEST(R4_engine_leadcarry)
{
	boot(1);
	runBounded(2.0, 20000u);
	ASSERT_TRUE(E.map.audio);
	const uint64_t nominal = (uint64_t)(APP_WALL_LEAD_SEC * FAKE_TPS);
	ASSERT_EQ(E.wallLead, nominal);
	const app_cmd_t c = { .kind = APP_CMD_OUTPUT, .a = 256 };	/* a reopen while the audio line runs, as a device change does */
	command(&c);
	ASSERT_EQ(fk.opens, 2);
	ASSERT_TRUE(E.wallLead > nominal);	/* the audio line's render lead is carried over to the wall clock */
	ASSERT_FALSE(E.map.audio);
	runBounded(wallSec() + 1.0, 20000u);
	ASSERT_TRUE(E.map.audio);	/* the callbacks of the new unit have put the map on the audio line */
	ASSERT_EQ(E.wallLead, nominal);
}

/* --- R5: the clock-out jack as audio (spec 6.8, plan R5) ---------------------------------------------------------- */

TEST(R5_render_clock)	/* spec 6.8, F3, F4 */
{
	app_audio_t a;
	const int16_t f[4] = { 1000, -2000, 3001, -3 };
	const app_audioTime_t ts = { 5000u, 4000u, 1.0 };
	float out[16];
	audioInit(&a, 16u, f);
	atomic_store(&a.clockChannel, 2);
	app_audioRender(&a, &ts, 2u, out, 2u);	/* frames 0, 1 */
	ASSERT_EQ_DBL(out[0], 499.0 / 32768.0);	/* the sum (2000, -1002) in mono: 998 >> 1 */
	ASSERT_EQ_DBL(out[1], 0.0);
	ASSERT_EQ_DBL(out[3], 1.0);
	atomic_store(&a.clockChannel, 1);
	app_audioRender(&a, &ts, 2u, out, 2u);	/* frames 2, 3 */
	ASSERT_EQ_DBL(out[1], 499.0 / 32768.0);
	ASSERT_EQ_DBL(out[2], 1.0);
	atomic_store(&a.clockChannel, 3);		/* a third channel: the stereo sum stays on 1 and 2 */
	app_audioRender(&a, &ts, 2u, out, 4u);	/* frames 4, 5 */
	ASSERT_EQ_DBL(out[0], 2000.0 / 32768.0);
	ASSERT_EQ_DBL(out[1], -1002.0 / 32768.0);
	ASSERT_EQ_DBL(out[3], 0.0);
	ASSERT_EQ_DBL(out[6], 1.0);
	atomic_store(&a.mix, APP_MIX_FOUR);
	atomic_store(&a.clockChannel, 4);		/* the 4-channel mix: the clock replaces R2 */
	app_audioRender(&a, &ts, 1u, out, 4u);	/* frame 6 */
	ASSERT_EQ_DBL(out[2], 3001.0 / 32768.0);
	ASSERT_EQ_DBL(out[3], 0.0);
	atomic_store(&a.mix, APP_MIX_SUM);
	atomic_store(&a.clockChannel, 3);		/* a channel the unit does not have: as without the option */
	app_audioRender(&a, &ts, 1u, out, 2u);	/* frame 7 */
	ASSERT_EQ_DBL(out[1], -1002.0 / 32768.0);
	atomic_store(&a.clockChannel, 2);
	atomic_store(&a.mute, 1);				/* a hang: the clock is silent too */
	app_audioRender(&a, &ts, 2u, out, 2u);	/* frames 8, 9 */
	ASSERT_EQ_DBL(out[3], 0.0);
	atomic_store(&a.mute, 0);
	app_audioRender(&a, &ts, 8u, out, 2u);	/* frames 10-15, then 2 of underrun: the clock low */
	ASSERT_EQ_DBL(out[11], 1.0);
	ASSERT_EQ_DBL(out[13], 0.0);
	ASSERT_EQ(atomic_load(&a.underruns), 1);
}

static uint64_t ringEnd(void) { return E.frameBase + app_ringPopped(&E.audio.frames) + app_ringCount(&E.audio.frames); }
static uint64_t blocksNow(void) { emu_counters_t c; emu_getCounters(&c); return c.blocks; }

TEST(R5_engine_holdback)	/* F2: block k enters at request k + 1; L and D's default grow by a block (spec 6.4) */
{
	boot(1);
	runTo(2.0);
	ASSERT_EQ(ringEnd(), 64u * (blocksNow() + 1u));
	const double L0 = E.L, D0 = (double)E.dCycles / EMU_CPU_HZ;
	app_cmd_t c = { .kind = APP_CMD_CLOCK_OUT, .a = 2 };
	command(&c);
	for (unsigned i = 0; i < 300u; i++)
	{
		ASSERT_EQ(ringEnd(), 64u * blocksNow());
		app_engineSlice(&E);
	}
	ASSERT_NEAR((E.L - L0) * 1e3, 1.44991, 1e-4);
	ASSERT_NEAR(((double)E.dCycles / EMU_CPU_HZ - D0) * 1e3, 1.44991, 1e-3);
	ASSERT_EQ(fk.opens, 1);	/* channel 2 of a stereo unit: no reopen */
	c.a = 0;
	command(&c);
	ASSERT_EQ(ringEnd(), 64u * (blocksNow() + 1u));	/* the held block and the new one, in order */
	ASSERT_NEAR(E.L, L0, 1e-12);
	ASSERT_EQ(atomic_load(&E.audio.underruns), 0);
	c.a = 3;	/* 3 channels asked for; the stereo fake gives 2 */
	command(&c);
	ASSERT_EQ(fk.opens, 2);
	ASSERT_EQ(E.req.channels, 3);
	ASSERT_EQ(fk.channels, 2);
}

static struct { uint64_t frame, riseAt; unsigned rises, widthBad, other; int level; double energy; } clk;

static void clockRendered(const float *buf, unsigned frames, unsigned channels)	/* clock on 2, the mix in mono on 1 */
{
	for (unsigned i = 0; i < frames; i++, clk.frame++)
	{
		const float m = buf[(size_t)i * channels], v = buf[(size_t)i * channels + 1u];
		clk.energy += (double)m * m;
		clk.other += v != 0.0f && v != 1.0f;	/* the mix on the clock's channel */
		const int high = v == 1.0f;
		if (high && !clk.level)
		{
			clk.rises++;
			clk.riseAt = clk.frame;
		}
		else if (!high && clk.level)	/* a 96-PPQN pulse at 120 BPM: 229.9 frames; each edge within a pass (11) */
			clk.widthBad += clk.frame - clk.riseAt < 207u || clk.frame - clk.riseAt > 253u;
		clk.level = high;
	}
}

TEST(R5_engine_clockout)	/* spec 6.8: the firmware's own clock out at 120 BPM, as audio */
{
	boot(1);
	runTo(1.5);
	memset(&clk, 0, sizeof clk);
	fk.rendered = clockRendered;
	const app_cmd_t c = { .kind = APP_CMD_CLOCK_OUT, .a = 2 };
	command(&c);
	emu_counters_t c0, c1;
	emu_getCounters(&c0);
	app_panelEvent_t b = { .kind = APP_P_BUTTON, .index = 33, .value = 1 };	/* START */
	ASSERT_EQ(app_enginePanel(&E, &b), 0);
	runTo(1.6);
	b.value = 0;
	ASSERT_EQ(app_enginePanel(&E, &b), 0);
	runTo(5.6);
	emu_getCounters(&c1);
	const unsigned pulses = (unsigned)((c1.clockEdges - c0.clockEdges) / 2u);
	ASSERT_TRUE(clk.rises >= 8u);
	ASSERT_TRUE(clk.rises + 1u >= pulses && clk.rises <= pulses + 1u);	/* one may still be in the ring */
	ASSERT_EQ(clk.widthBad, 0);
	ASSERT_EQ(clk.other, 0);
	ASSERT_TRUE(clk.energy > 0.0);
	ASSERT_EQ(E.clkOut.dropped, 0);
}

static unsigned highFrames, framesSeen;

static void clockHighCount(const float *buf, unsigned frames, unsigned channels)	/* the clock on channel 2 */
{
	for (unsigned i = 0; i < frames; i++, framesSeen++)
		highFrames += buf[(size_t)i * channels + 1u] != 0.0f;
}

/* F1: the clock-out edges are drained while the output is down, or the queue of 32 fills in a few seconds and the clock
 * level is wrong for a pulse period after the reopen (here: for good, the sequencer being stopped) */
TEST(R5_engine_clockdown)
{
	boot(1);
	runTo(1.5);
	const app_cmd_t c = { .kind = APP_CMD_CLOCK_OUT, .a = 2 };
	command(&c);
	app_panelEvent_t b = { .kind = APP_P_BUTTON, .index = 33, .value = 1 };	/* START */
	ASSERT_EQ(app_enginePanel(&E, &b), 0);
	runTo(1.6);
	b.value = 0;
	ASSERT_EQ(app_enginePanel(&E, &b), 0);
	runTo(2.5);
	ASSERT_TRUE(E.clkOut.n < 3u);
	hang();	/* the output goes, and no open succeeds */
	const app_cmd_t o = { .kind = APP_CMD_OUTPUT };
	command(&o);
	ASSERT_FALSE(E.outputRunning);
	runTo(8.0);	/* 5.5 s of pulses: 22 or so */
	emu_counters_t c0;
	emu_getCounters(&c0);
	ASSERT_TRUE(c0.clockEdges > 40u);
	ASSERT_EQ(E.clkOut.dropped, 0);
	ASSERT_TRUE(E.clkOut.n < 3u);
	b.value = 1;	/* STOP, with the jack low from here on */
	ASSERT_EQ(app_enginePanel(&E, &b), 0);
	runTo(8.1);
	b.value = 0;
	ASSERT_EQ(app_enginePanel(&E, &b), 0);
	runTo(10.0);
	ASSERT_EQ(E.clkOut.dropped, 0);
	ASSERT_FALSE(E.clockLevel);
	fk.device = 1;	/* the device is back: the engine's next try opens it */
	runTo(13.0);
	ASSERT_EQ(fk.opens, 2);
	highFrames = framesSeen = 0;
	fk.rendered = clockHighCount;
	runTo(14.0);
	ASSERT_TRUE(framesSeen > 40000u);
	ASSERT_EQ(highFrames, 0);	/* the jack is low: no stale edge of the seconds without output */
}

/* spec 6.8 (review of F3): a clock channel that the device does not have is said, not dropped in silence */
TEST(R5_engine_clocknone)
{
	boot(1);
	runTo(1.0);
	app_ack_t a;
	while (app_engineAck(&E, &a))
		;
	const app_cmd_t c = { .kind = APP_CMD_CLOCK_OUT, .a = 3 };	/* the fake device has two outputs */
	command(&c);
	ASSERT_EQ(fk.opens, 2);
	ASSERT_EQ(E.out.deviceChannels, 2);
	int told = 0;
	while (app_engineAck(&E, &a))
		if (a.kind == APP_MSG_NOTICE && strstr(a.text, "clock out") && strstr(a.text, "channel 3") && strstr(a.text, "2 channels"))
			told++;
	ASSERT_EQ(told, 1);
	const app_cmd_t two = { .kind = APP_CMD_CLOCK_OUT, .a = 2 };	/* a channel it has: nothing to say */
	command(&two);
	while (app_engineAck(&E, &a))
		ASSERT_FALSE(a.kind == APP_MSG_NOTICE);
}

/* review of 876c9f6: the input callbacks that brought no samples (a buffer larger than the unit's, no host time, a render that
 * failed) are counted by the input and reach the diagnostics, next to the edges that found the ring full */
TEST(R5_engine_inskipped)
{
	boot(1);
	runTo(1.5);
	ASSERT_EQ(last.diag.jackInSkipped, 0);
	atomic_store(&E.jackIn.skipped, 7u);
	runTo(2.5);
	ASSERT_EQ(last.diag.jackInSkipped, 7);
}

TEST_MAIN()
