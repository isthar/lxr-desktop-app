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

static void boot(int device)
{
	app_host_t h;
	fakeHost(&h, device);
	if (app_engineInit(&E, &h) != 0)
		exit(2);
	static char card[1024];
	fakeCard(card, sizeof card, 120u);
	const app_engineConfig_t c = { .cardDir = card, .seed = 1u, .dAuto = 1, .mix = APP_MIX_SUM };
	app_enginePowerOn(&E, &c);
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
	ASSERT_EQ(E.genK, 9);	/* 120 BPM at 4 PPQ: t0 + k x 125 ms for k = 0..8, pulses up to 1 ms ahead */
	ASSERT_EQ(E.genNext, t0 + 9u * 27000000u);
}

TEST_MAIN()
