/*
 * test_dynamics.c
 *
 * LICHOMAT v0.7 (plan v0.7 Task 3): the velocity templates and the own templates in the timing
 * core.
 * - vel_scale: seqc_velScale() against the formula in double for every velocity, dB and depth,
 *   and seqc_velDb() against -20 log10 for every pair of velocities (V7-4, V7-7);
 * - vel_templates: the four tables through vtmpl(), the position q (track lengths, x2, shared
 *   with a timing template), the depths, ratchet hits sharing one velocity, the humanize volume
 *   after the template, a volume of 0, rolls without it; with vtmpl() set but off or at depth
 *   0, queue_equiv's random patterns give v0.6's events field for field (V7-4, V7-5); what
 *   vtmpl() returns checked; no trackLength() read with both templates off (V7-5);
 * - own_templates: the timing ids 8..11 and the velocity ids 5..8 through userTemplates, values
 *   out of range as 0, no pointer as 0, and seqc_recordPlace() leaving an own template out
 *   (V7-3, V7-6, V7-7); seqc_templateTicksAt() and seqc_templateVelAt() (V7-10).
 * The fake host has vtmpl() and userTemplates only after fake_useDynamics(true, ...) (V7-23);
 * every test here sets it as it needs and leaves it off.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "fake_host.h"
#include "queue_equiv.h"
#include "seqc_test.h"

/* the draft's tables (docs/superpowers/specs/2026-11-04-v0.7-draft.md 2.2) and v0.6's lay,
 * copied: the core's copies are not read here */
static const int vtab[VEL_TEMPLATE_BUILTIN][TEMPLATE_STEPS] = {
	{ 0 },
	{ -10, -15, 0, -14, -10, -14, 0, -13, -10, -16, 0, -14, -10, -15, 0, -13 },	/* 1 of8 */
	{ -7, -15, 0, -5, -7, -9, -1, -10, -7, -14, 0, -7, -6, -10, -1, -10 },		/* 2 ful */
	{ -14, -12, 0, -19, -14, -11, 0, -19, -15, -12, 0, -16, -13, -12, 0, -21 },	/* 3 dep */
	{ -5, -15, 0, -20, -6, -17, 0, -16, -5, -17, 0, -15, -6, -17, -1, -16 },	/* 4 qtr */
};
static const int lay[TEMPLATE_STEPS] = { 0, 18, 0, 5, 0, 18, 0, 5, 0, 18, 0, 5, 0, 18, 0, 5 };
#define OF8 1
#define DEP 3
#define LAY 2		/* tpl lay */
#define TPL_US1 8	/* tpl us1 */

#define CYCLES_PER_TICK_120 281250u	/* 216 MHz x 60 / (120 x 384), exact */

/** the formula in double: vel x 10^(dB x pct / 2000), dB floored at -42 */
static double velExact(int vel, int db, int pct)
{
	if (db < VEL_TEMPLATE_DB_MIN)
		db = VEL_TEMPLATE_DB_MIN;
	return vel * pow(10.0, db * pct / 2000.0);
}

/** x rounded half up, clamped to 1..127 */
static int velRound(double x)
{
	const int v = (int)floor(x + 0.5);
	return v < 1 ? 1 : (v > 127 ? 127 : v);
}

/** the velocity the template gives (V7-4): the formula rounded; 0, dB >= 0 or pct 0 leave vel */
static int velModel(int vel, int db, int pct)
{
	return (vel == 0 || db >= 0 || pct == 0) ? vel : velRound(velExact(vel, db, pct));
}

/** a / b rounded half away from zero (the core's rhaDiv) */
static int rha(int a, int b)
{
	const double x = (double)a / (double)b;
	return x >= 0.0 ? (int)floor(x + 0.5) : -(int)floor(-x + 0.5);
}

/** an own template's timing value as the core reads it (V7-6) */
static int ownTiming(int v)
{
	return (v < -84 || v > 84) ? 0 : v;
}

/** an own template's velocity value as the core reads it (V7-7) */
static int ownVel(int v)
{
	return (v < -42 || v > 0) ? 0 : v;
}

/** a fresh fake host with vtmpl() and userTemplates (all off and empty), logging the triggers */
static fake_host_t *dynHost(void)
{
	fake_useDynamics(true, (TrackTemplate){ 0, 0 }, NULL);
	fake_host_t *h = fake_init();
	h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER);
	return h;
}

/** every test's end: both settings off */
static void dynOff(void)
{
	fake_useDynamics(false, (TrackTemplate){ 0, 0 }, NULL);
	fake_useMicro(false);
}

/** start at time 0 (step 0 at tick 0) and run for ms */
static void play(fake_host_t *h, double ms)
{
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_runUntil(h, fake_msToCycles(ms), fake_msToCycles(0.5));
}

/** every step of a track on */
static void allOn(fake_host_t *h, uint8_t pattern, uint8_t track)
{
	for (uint8_t s = 0; s < FAKE_NUM_STEPS; s++)
		fake_setStep(h, pattern, track, s, true);
}

//------------------------------------------------------------------------------
// vel_scale
//------------------------------------------------------------------------------
TEST(vel_scale_matches_the_formula)
{
	/* every velocity 0..127, dB -45..+2 and depth 0..200 %: the formula rounded, except within
	 * 0.005 of a rounding tie, where either neighbour is right (planning: 565 such inputs, the
	 * farthest 0.0029 from the tie, and no other difference) */
	int ties = 0;
	for (int vel = 0; vel < 128; vel++)
		for (int db = -45; db <= 2; db++)
			for (int pct = 0; pct <= 200; pct++)
			{
				const int got = seqc_velScale((uint8_t)vel, db, (uint8_t)pct);
				if (vel == 0 || db >= 0 || pct == 0)
				{
					ASSERT_EQ(got, vel);
					continue;
				}
				const double x = velExact(vel, db, pct);
				if (got != velRound(x))
				{
					ASSERT_TRUE(fabs(x - floor(x) - 0.5) < 0.005);
					ASSERT_TRUE(got == velRound(floor(x)) || got == velRound(ceil(x)));
					ties++;
				}
			}
	printf("vel_scale: %d inputs within 0.005 of a tie round the other way\n", ties);
	ASSERT_TRUE(ties <= 600);
	ASSERT_EQ(seqc_velScale(100, -10, 201), seqc_velScale(100, -10, 200));	/* above 200 % as 200 */
	ASSERT_EQ(seqc_velScale(100, -10, 255), seqc_velScale(100, -10, 200));
	ASSERT_EQ(seqc_velScale(127, -17, 201), 3);	/* 3 at 200 % (2.54); 201 % would give 2 (2.49) */
	ASSERT_EQ(seqc_velScale(127, -60, 100), seqc_velScale(127, -42, 100));	/* below -42 dB as -42 */
	/* the draft's of8 at 100 % from 127, and the edges */
	static const uint8_t of8[16] = { 40, 23, 127, 25, 40, 25, 127, 28, 40, 20, 127, 25, 40, 23, 127, 28 };
	for (int q = 0; q < 16; q++)
		ASSERT_EQ(seqc_velScale(127, vtab[OF8][q], 100), of8[q]);
	ASSERT_EQ(seqc_velScale(127, -16, 200), 3);	/* -32 dB: 3.19 */
	ASSERT_EQ(seqc_velScale(1, -42, 200), 1);	/* never below 1 */
	ASSERT_EQ(seqc_velScale(0, -42, 200), 0);	/* a silent step stays silent */
	ASSERT_EQ(seqc_velScale(120, -14, 100), 24);
}

TEST(vel_scale_above_200_as_200)
{
	/* review of Task 3: a depth above 200 % plays as 200 %, for every velocity and dB (a clamp to
	 * 199 % would make 22 at -8 dB 4 instead of 3: 3.47 at 200 %, 3.51 at 199 %) */
	for (int pct = 201; pct <= 255; pct++)
		for (int vel = 0; vel < 128; vel++)
			for (int db = -45; db <= 2; db++)
				ASSERT_EQ(seqc_velScale((uint8_t)vel, db, (uint8_t)pct), seqc_velScale((uint8_t)vel, db, 200));
	ASSERT_EQ(seqc_velScale(22, -8, 200), 3);
	ASSERT_EQ(seqc_velScale(22, -8, 201), 3);
	ASSERT_EQ(velModel(22, -8, 200), 3);
}

TEST(vel_scale_db_of_two_velocities)
{
	/* seqc_velDb(): -20 log10(vel / loudest) rounded to whole dB, -42..0; 0 for vel >= loudest or
	 * loudest 0, -42 for vel 0; a near-tie (within 0.005) may round to the neighbour (planning: 2) */
	int ties = 0;
	for (int loudest = 0; loudest < 128; loudest++)
		for (int vel = 0; vel < 128; vel++)
		{
			const int got = seqc_velDb((uint8_t)vel, (uint8_t)loudest);
			if (loudest == 0 || vel >= loudest)
			{
				ASSERT_EQ(got, 0);
				continue;
			}
			if (vel == 0)
			{
				ASSERT_EQ(got, -42);
				continue;
			}
			const double x = -20.0 * log10((double)vel / loudest);
			const int want = -(int)fmin(42.0, floor(x + 0.5));
			if (got != want)
			{
				ASSERT_TRUE(fabs(x - floor(x) - 0.5) < 0.005);
				ASSERT_EQ(abs(got - want), 1);
				ties++;
			}
		}
	ASSERT_TRUE(ties <= 4);
	static const struct { uint8_t vel, loudest; int db; } want[] = {
		{ 127, 127, 0 }, { 100, 127, -2 }, { 64, 127, -6 }, { 40, 127, -10 }, { 20, 127, -16 },
		{ 10, 127, -22 }, { 3, 127, -33 }, { 1, 127, -42 }, { 0, 127, -42 }, { 60, 120, -6 },
		{ 60, 127, -7 }, { 90, 127, -3 }, { 100, 50, 0 }, { 0, 0, 0 },
	};
	for (unsigned k = 0; k < sizeof want / sizeof want[0]; k++)
		ASSERT_EQ(seqc_velDb(want[k].vel, want[k].loudest), want[k].db);
}

TEST(vel_scale_db_loudest_past_127)
{
	/* review of Task 3: the floor -42 dB holds for any uint8_t: from loudest 134 on, vel 1 is more
	 * than 42.5 dB below (1/134: 42.54 dB) and still reads -42; every value stays in -42..0, 0 for
	 * vel >= loudest. Not compared with the formula here: past 127 the near-ties exceed the bound */
	for (int loudest = 128; loudest < 256; loudest++)
		for (int vel = 0; vel < 256; vel++)
		{
			const int got = seqc_velDb((uint8_t)vel, (uint8_t)loudest);
			if (vel >= loudest)
				ASSERT_EQ(got, 0);
			else
				ASSERT_TRUE(got >= VEL_TEMPLATE_DB_MIN && got <= 0);
		}
	ASSERT_EQ(seqc_velDb(1, 255), -42);
	ASSERT_EQ(seqc_velDb(1, 134), -42);
	ASSERT_EQ(seqc_velDb(1, 133), -42);	/* 42.48 dB */
	ASSERT_EQ(seqc_velDb(0, 255), -42);
	ASSERT_EQ(seqc_velDb(3, 255), -39);	/* 38.59 dB */
	ASSERT_EQ(seqc_velDb(128, 255), -6);	/* 5.99 dB */
}

//------------------------------------------------------------------------------
// vel_templates
//------------------------------------------------------------------------------
TEST(vel_templates_tables_through_vtmpl)
{
	/* ids 1..4 at 100 % on tracks 0-3, dep at 200 % on track 4, of8 at depth 0 on track 5, off on
	 * track 6; every step on at volume 127, x1, two bars: hit k at q = k mod 16, the timing kept */
	static const struct { uint8_t id; int pct; } set[FAKE_NUM_TRACKS] = {
		{ 1, 100 }, { 2, 100 }, { 3, 100 }, { 4, 100 }, { DEP, 200 }, { OF8, 0 }, { 0, 100 },
	};
	fake_host_t *h = dynHost();
	for (uint8_t t = 0; t < FAKE_NUM_TRACKS; t++)
	{
		allOn(h, 0, t);
		fake_setVelTemplate(h, 0, t, set[t].id, set[t].pct);
	}
	play(h, 3990.0);
	for (uint8_t t = 0; t < FAKE_NUM_TRACKS; t++)
	{
		const fake_rec_t *r[40];
		ASSERT_EQ(fake_triggers(h, t, r, 40), 32);
		for (int k = 0; k < 32; k++)
		{
			ASSERT_EQ(r[k]->ev.tick, 96 * k);
			ASSERT_EQ(r[k]->ev.vel, velModel(127, vtab[set[t].id][k % 16], set[t].pct));
		}
	}
	ASSERT_TRUE(h->reads[FAKE_READ_VTMPL] > 0);
	ASSERT_EQ(h->errors, 0);
	dynOff();
}

TEST(vel_templates_out_of_range_read_as_defaults)
{
	/* the core checks what vtmpl() returns (velTemplate_checked()): ids 9 and 255 play as off, an
	 * amount of +101 or -101 as 100 %; -100 is depth 0 and +100 is 200 %, both in range */
	static const TrackTemplate raw[6] = {
		{ 9, 0 }, { 255, 0 }, { OF8, 101 }, { OF8, -101 }, { OF8, -100 }, { OF8, 100 },
	};
	static const struct { uint8_t id; int pct; } want[6] = {
		{ 0, 100 }, { 0, 100 }, { OF8, 100 }, { OF8, 100 }, { OF8, 0 }, { OF8, 200 },
	};
	fake_host_t *h = dynHost();
	for (uint8_t t = 0; t < 6; t++)
	{
		allOn(h, 0, t);
		h->vtpl[0][t] = raw[t];
	}
	play(h, 1990.0);
	for (uint8_t t = 0; t < 6; t++)
	{
		const fake_rec_t *r[20];
		ASSERT_EQ(fake_triggers(h, t, r, 20), 16);
		for (int k = 0; k < 16; k++)
			ASSERT_EQ(r[k]->ev.vel, velModel(127, vtab[want[t].id][k], want[t].pct));
	}
	ASSERT_EQ(h->errors, 0);
	dynOff();
}

TEST(vel_templates_off_reads_no_length)
{
	/* V7-5: q is computed (one trackLength() read) only when a template is on. With tmpl() and
	 * vtmpl() set and both off, the host is read as v0.6 reads it, but for vtmpl(); with of8 on,
	 * one more trackLength() per step occurrence */
	uint32_t length[3], vtmpl[3];
	for (int run = 0; run < 3; run++)
	{
		fake_useMicro(true);
		fake_useDynamics(run > 0, (TrackTemplate){ .id = (uint8_t)(run == 2 ? OF8 : 0), .amount = 0 }, NULL);
		fake_host_t *h = fake_init();
		h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER);
		allOn(h, 0, 0);
		play(h, 1990.0);
		const fake_rec_t *r[20];
		ASSERT_EQ(fake_triggers(h, 0, r, 20), 16);
		ASSERT_EQ(r[1]->ev.vel, run == 2 ? 23 : 127);
		length[run] = h->reads[FAKE_READ_LENGTH];
		vtmpl[run] = h->reads[FAKE_READ_VTMPL];
		ASSERT_EQ(h->errors, 0);
	}
	ASSERT_EQ(vtmpl[0], 0);
	ASSERT_TRUE(vtmpl[1] >= 16);
	ASSERT_EQ(length[1], length[0]);
	ASSERT_TRUE(length[2] >= length[0] + 16);
	dynOff();
}

TEST(vel_templates_every_depth)
{
	/* review of Task 3: every depth 0..200 % of each built-in at each position, from velocities 127,
	 * 64 and 1, through seqc_templateVelAt() (seqc_templateVel(), which a step plays through):
	 * seqc_velScale() at that depth, and the formula rounded except within 0.005 of a tie (planning
	 * model: 70 such inputs, the farthest 0.0028 from the tie). Only 0 % leaves the velocity: 1 %
	 * already scales (127 at -15 dB is 125) */
	static const uint8_t vels[3] = { 127, 64, 1 };
	dynHost();
	int ties = 0;
	for (uint8_t id = 1; id < VEL_TEMPLATE_BUILTIN; id++)
		for (uint8_t q = 0; q < TEMPLATE_STEPS; q++)
			for (int pct = 0; pct <= 200; pct++)
				for (int k = 0; k < 3; k++)
				{
					const TrackTemplate vt = { .id = id, .amount = (int8_t)(pct - 100) };
					const int got = seqc_templateVelAt(vels[k], vt, q);
					ASSERT_EQ(got, seqc_velScale(vels[k], vtab[id][q], (uint8_t)pct));
					const int want = velModel(vels[k], vtab[id][q], pct);
					if (got != want)
					{
						const double x = velExact(vels[k], vtab[id][q], pct);
						ASSERT_TRUE(fabs(x - floor(x) - 0.5) < 0.005);
						ASSERT_TRUE(got == velRound(floor(x)) || got == velRound(ceil(x)));
						ties++;
					}
				}
	printf("vel_templates_every_depth: %d inputs within 0.005 of a tie round the other way\n", ties);
	ASSERT_TRUE(ties <= 70);
	ASSERT_EQ(seqc_templateVelAt(127, (TrackTemplate){ OF8, -99 }, 1), 125);	/* 1 %, -15 dB */
	ASSERT_EQ(seqc_templateVelAt(127, (TrackTemplate){ OF8, -75 }, 0), 95);	/* 25 %, -10 dB */
	ASSERT_EQ(seqc_templateVelAt(127, (TrackTemplate){ OF8, -100 }, 1), 127);	/* 0 % */
	dynOff();
}

TEST(vel_templates_ids_9_to_11_read_as_off)
{
	/* review of Task 3: the velocity ids are checked as velocity ids (velTemplate_checked(), 0..8),
	 * not as timing ids (0..11), on the play path and in seqc_templateVelAt(). userTemplates points
	 * at 7 templates here, the 3 past us4 at -20 dB: ids 9, 10 and 11 play and read as off, while
	 * id 8 (us4, -6 dB) reads us4 */
	static UserTemplate wide[USER_TEMPLATES + 3];
	memset(wide, 0, sizeof wide);
	for (int u = USER_TEMPLATES - 1; u < USER_TEMPLATES + 3; u++)
		memset(wide[u].vel, u == USER_TEMPLATES - 1 ? -6 : -20, sizeof wide[u].vel);
	fake_host_t *h = dynHost();
	h->host.userTemplates = wide;
	seqc_init(&h->host, SEQC_CPU_HZ);		/* the host table again, with the pointer */
	static const TrackTemplate raw[4] = { { 9, 0 }, { 10, 0 }, { 11, 0 }, { 8, 0 } };
	for (uint8_t t = 0; t < 4; t++)
	{
		allOn(h, 0, t);
		h->vtpl[0][t] = raw[t];
	}
	play(h, 1990.0);
	for (uint8_t t = 0; t < 4; t++)
	{
		const fake_rec_t *r[20];
		ASSERT_EQ(fake_triggers(h, t, r, 20), 16);
		for (int k = 0; k < 16; k++)
			ASSERT_EQ(r[k]->ev.vel, t == 3 ? 64 : 127);	/* 127 at -6 dB: 63.65 */
	}
	for (uint8_t q = 0; q < TEMPLATE_STEPS; q++)
	{
		for (int t = 0; t < 3; t++)
			ASSERT_EQ(seqc_templateVelAt(120, raw[t], q), 120);
		ASSERT_EQ(seqc_templateVelAt(120, raw[3], q), 60);	/* 120 at -6 dB: 60.14 */
	}
	ASSERT_EQ(velModel(127, -6, 100), 64);
	ASSERT_EQ(velModel(120, -6, 100), 60);
	ASSERT_EQ(h->errors, 0);
	dynOff();
}

TEST(vel_templates_position_counts_the_tracks_steps)
{
	/* q = (loop x length + step) mod 16, v0.6 M6: occurrence k of a track with every step on is at
	 * position k mod 16 at lengths 16, 12, 7 and 64, and at x2; with lay and of8 on one track both
	 * read the same q (V7-5) */
	static const uint8_t lengths[4] = { 16, 12, 7, 64 };
	fake_useMicro(true);
	fake_host_t *h = dynHost();
	for (uint8_t t = 0; t < 4; t++)
	{
		fake_setLength(h, 0, t, lengths[t]);
		allOn(h, 0, t);
		fake_setVelTemplate(h, 0, t, OF8, 100);
	}
	allOn(h, 0, 4);
	fake_setTempo(h, 0, 4, 8);				/* x2: 48 ticks */
	fake_setVelTemplate(h, 0, 4, DEP, 100);
	allOn(h, 0, 5);
	fake_setTemplate(h, 0, 5, LAY, 100);
	fake_setVelTemplate(h, 0, 5, OF8, 100);
	play(h, 5990.0);					/* 48 steps */
	for (uint8_t t = 0; t < 6; t++)
	{
		const int n = t == 4 ? 96 : 48, L = t == 4 ? 48 : 96;
		const fake_rec_t *r[128];
		ASSERT_EQ(fake_triggers(h, t, r, 128), (size_t)n);
		for (int k = 0; k < n; k++)
		{
			ASSERT_EQ(r[k]->ev.tick, L * k + (t == 5 ? lay[k % 16] : 0));
			ASSERT_EQ(r[k]->ev.vel, velModel(127, vtab[t == 4 ? DEP : OF8][k % 16], 100));
		}
	}
	ASSERT_EQ(h->errors, 0);
	dynOff();
}

TEST(vel_templates_ratchets_share_and_humanize_follows)
{
	/* step 3 of track 0 (q 3: of8 -14 dB) at volume 120, x4: every hit 24 (120 x 10^-0.7 = 23.94).
	 * With hmv 25 and the volume draw scripted to +25 %, every hit (24 x 125 + 50) / 100 = 30: the
	 * humanize volume follows the template (the other order gives 25) */
	for (int hum = 0; hum < 2; hum++)
	{
		static const uint32_t draws[2] = { 0, 50 };	/* r = 1 (plays at 100), then 50 % 51 - 25 = +25 % */
		fake_host_t *h = dynHost();
		fake_setStep(h, 0, 0, 3, true);
		fake_setStepData(h, 0, 0, 3, 120, FAKE_DEFAULT_NOTE, 100);
		fake_setTiming(h, 0, 0, 3, 3, 0);		/* flam 3: 4 hits */
		fake_setVelTemplate(h, 0, 0, OF8, 100);
		if (hum)
		{
			fake_setGroove(h, 0, 0, 0, 25, 0);
			fake_scriptRandom(h, draws, 2);
		}
		play(h, 990.0);
		const fake_rec_t *r[8];
		ASSERT_EQ(fake_triggers(h, 0, r, 8), 4);
		for (int j = 0; j < 4; j++)
			ASSERT_EQ(r[j]->ev.vel, hum ? 30 : 24);
		ASSERT_EQ(h->errors, 0);
	}
	ASSERT_EQ(velModel(120, -14, 100), 24);
	dynOff();
}

TEST(vel_templates_leave_volume_0_and_the_rolls)
{
	/* dep at 200 % on steps at volume 0: they stay 0 (V7-4); a roll on a track with of8 keeps
	 * SEQC_ROLL_VELOCITY: rolls take no template (F6) */
	fake_host_t *h = dynHost();
	allOn(h, 0, 0);
	for (uint8_t s = 0; s < 16; s++)
		fake_setStepData(h, 0, 0, s, 0, FAKE_DEFAULT_NOTE, 100);
	fake_setVelTemplate(h, 0, 0, DEP, 200);
	h->rollRate[1] = SEQC_ROLL(1, 2);
	fake_setVelTemplate(h, 0, 1, OF8, 100);
	play(h, 990.0);
	const fake_rec_t *r[32];
	ASSERT_EQ(fake_triggers(h, 0, r, 32), 8);
	for (int k = 0; k < 8; k++)
		ASSERT_EQ(r[k]->ev.vel, 0);
	ASSERT_EQ(fake_triggers(h, 1, r, 32), 16);
	for (int k = 0; k < 16; k++)
	{
		ASSERT_TRUE((r[k]->ev.flags & SEQC_EVF_ROLL) != 0);
		ASSERT_EQ(r[k]->ev.vel, SEQC_ROLL_VELOCITY);
	}
	ASSERT_EQ(h->errors, 0);
	dynOff();
}

TEST(vel_templates_early_hits_take_it_too)
{
	/* quality review of Task 3: hits before their step's boundary (the early part) take the velocity
	 * template at the occurrence's q, as the late part's do. of8 at 100 %, every step on at 127;
	 * step 1 one unit early (-12 ticks: its one hit early; q 1, -15 dB, 22.58: 23); step 5 x4 two
	 * units early (hits at -24, 0, +24, +48: the first early, three late; q 5, -14 dB, 25.35: 25) */
	fake_host_t *h = dynHost();
	allOn(h, 0, 0);
	fake_setTiming(h, 0, 0, 1, 0, -1);
	fake_setTiming(h, 0, 0, 5, 3, -2);
	fake_setVelTemplate(h, 0, 0, OF8, 100);
	play(h, 1990.0);
	const fake_rec_t *r[24];
	ASSERT_EQ(fake_triggers(h, 0, r, 24), 19);
	ASSERT_EQ(r[1]->ev.step, 1);
	ASSERT_EQ(r[1]->ev.tick, 96 - 12);
	ASSERT_EQ(r[1]->ev.vel, 23);
	for (int j = 0; j < 4; j++)
	{
		ASSERT_EQ(r[5 + j]->ev.step, 5);
		ASSERT_EQ(r[5 + j]->ev.hit, j);
		ASSERT_EQ(r[5 + j]->ev.tick, 5 * 96 - 24 + 24 * j);
		ASSERT_EQ(r[5 + j]->ev.vel, 25);
	}
	ASSERT_EQ(r[9]->ev.step, 6);
	ASSERT_EQ(r[9]->ev.vel, velModel(127, vtab[OF8][6], 100));
	ASSERT_EQ(velModel(127, -15, 100), 23);
	ASSERT_EQ(velModel(127, -14, 100), 25);
	ASSERT_EQ(h->errors, 0);
	dynOff();
}

TEST(vel_templates_follow_the_pattern)
{
	/* quality review of Task 3: vtmpl() is asked about the pattern that plays. Pattern 0 of8,
	 * pattern 1 dep, both at 100 %, every step on at 127, a switch to pattern 1 at the end of the
	 * first bar; pattern 1's step 0 one unit early, so its hit is generated at pattern 0's last
	 * boundary, for the next pattern. Runs to 3950 ms, before pattern 1's second bar (3984 ms) */
	fake_host_t *h = dynHost();
	allOn(h, 0, 0);
	allOn(h, 1, 0);
	fake_setVelTemplate(h, 0, 0, OF8, 100);
	fake_setVelTemplate(h, 1, 0, DEP, 100);
	fake_setTiming(h, 1, 0, 0, 0, -1);
	fake_process(h, 0);
	seqc_start();
	fake_process(h, 0);
	fake_setNextPattern(h, 1);
	fake_runUntil(h, fake_msToCycles(3950.0), fake_msToCycles(0.5));
	const fake_rec_t *r[40];
	ASSERT_EQ(fake_triggers(h, 0, r, 40), 32);
	for (int k = 0; k < 32; k++)
	{
		ASSERT_EQ(r[k]->ev.pattern, k < 16 ? 0 : 1);
		ASSERT_EQ(r[k]->ev.tick, 96 * k - (k == 16 ? 12 : 0));
		ASSERT_EQ(r[k]->ev.vel, velModel(127, vtab[k < 16 ? OF8 : DEP][k % 16], 100));
	}
	ASSERT_EQ(r[16]->ev.vel, 25);			/* dep's -14 dB, not of8's -10 (40) */
	ASSERT_EQ(velModel(127, -10, 100), 40);
	ASSERT_EQ(h->errors, 0);
	dynOff();
}

TEST(vel_templates_bit_identity_off_or_at_depth_0)
{
	/* V7-4: vtmpl() and userTemplates set, every velocity template off, or ids 1..8 at depth 0 with
	 * the own templates full of values, give v0.6's events and counters on 10^3 of queue_equiv's
	 * random patterns (velocities 0..127, F17); of8 at 100 % changes them */
	static UserTemplate full[USER_TEMPLATES];
	for (int u = 0; u < USER_TEMPLATES; u++)
		for (int q = 0; q < TEMPLATE_STEPS; q++)
		{
			full[u].timing[q] = (int8_t)((u * 16 + q) * 7 % 169 - 84);
			full[u].vel[q] = (int8_t)-((u * 16 + q) * 5 % 43);
		}
	int changed = 0;
	for (uint64_t seed = 1; seed <= 1000; seed++)
	{
		seqc_stats_t a, b, c, d;
		fake_useDynamics(false, (TrackTemplate){ 0, 0 }, NULL);
		const uint64_t without = queue_equivRun(seed, &a);
		fake_useDynamics(true, (TrackTemplate){ 0, 0 }, full);
		const uint64_t off = queue_equivRun(seed, &b);
		fake_useDynamics(true, (TrackTemplate){ .id = (uint8_t)(1 + seed % 8), .amount = TEMPLATE_AMOUNT_MIN }, full);
		const uint64_t depth0 = queue_equivRun(seed, &c);
		if (off != without || depth0 != without)
			printf("seed %llu: 0x%016llx off, 0x%016llx at depth 0, 0x%016llx without\n", (unsigned long long)seed,
					(unsigned long long)off, (unsigned long long)depth0, (unsigned long long)without);
		ASSERT_TRUE(off == without);
		ASSERT_TRUE(depth0 == without);
		if (seed <= 20)
		{
			fake_useDynamics(true, (TrackTemplate){ .id = OF8, .amount = 0 }, full);
			changed += queue_equivRun(seed, &d) != without;
		}
	}
	ASSERT_TRUE(changed > 0);
	dynOff();
}

//------------------------------------------------------------------------------
// own_templates
//------------------------------------------------------------------------------
TEST(own_templates_timing_ids_8_to_11)
{
	/* tpl us1..us4 read the own templates' timing part through userTemplates (V7-3, V7-6): us1 at
	 * 100 % and 50 %, us2 with the reach +-84 (at 200 % P is clamped to +-84), us3 with values out
	 * of range (85, -85, 127, -128: 0), us4 empty; without userTemplates tpl us1 plays straight */
	static const int8_t us1[16] = { 0, 13, -7, 24, 0, 18, 1, 5, 0, -12, 3, 9, 0, 30, -2, 7 };
	static const int8_t us2[16] = { 0, 84, 0, -84, 0, 42, 0, -42, 0, 84, 0, -84, 0, 1, 0, -1 };
	static const int8_t us3[16] = { 0, 85, 0, -85, 0, 127, 0, -128, 0, 84, 0, -84, 0, 0, 0, 0 };
	static const int8_t us4[16] = { 0 };
	static const struct { uint8_t id; int pct; const int8_t *v; } set[6] = {
		{ 8, 100, us1 }, { 8, 50, us1 }, { 9, 100, us2 }, { 9, 200, us2 }, { 10, 100, us3 }, { 11, 100, us4 },
	};
	for (int pointer = 1; pointer >= 0; pointer--)
	{
		static UserTemplate u[USER_TEMPLATES];
		memset(u, 0, sizeof u);
		memcpy(u[0].timing, us1, 16);
		memcpy(u[1].timing, us2, 16);
		memcpy(u[2].timing, us3, 16);
		fake_useMicro(true);
		fake_useDynamics(pointer != 0, (TrackTemplate){ 0, 0 }, u);
		fake_host_t *h = fake_init();
		h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER);
		for (uint8_t t = 0; t < 6; t++)
		{
			allOn(h, 0, t);
			fake_setTemplate(h, 0, t, set[t].id, set[t].pct);
		}
		play(h, 3990.0);
		for (uint8_t t = 0; t < 6; t++)
		{
			const fake_rec_t *r[40];
			ASSERT_EQ(fake_triggers(h, t, r, 40), 32);
			for (int k = 0; k < 32; k++)
			{
				int g = pointer ? rha(ownTiming(set[t].v[k % 16]) * set[t].pct * 96, 9600) : 0;
				g = g < -84 ? -84 : (g > 84 ? 84 : g);
				ASSERT_EQ(r[k]->ev.tick, 96 * k + g);
			}
		}
		ASSERT_EQ(h->errors, 0);
	}
	dynOff();
}

TEST(own_templates_velocity_ids_5_to_8)
{
	/* vtp us1..us4 read the own templates' velocity part (V7-7): us1 at 100 % and 50 %, us2 with
	 * values out of range (+1, -43, 127, -128, +5: 0), us3 at -42 everywhere at 200 % (1), us4
	 * empty; without userTemplates vtp us1 leaves the velocity */
	static const int8_t us1[16] = { 0, -42, -1, -6, 0, -20, -3, -12, 0, -42, -1, -6, 0, -20, -3, -12 };
	static const int8_t us2[16] = { 0, 1, -43, 127, -128, 5, -3, 0, 0, 1, -43, 127, -128, 5, -3, 0 };
	static const int8_t us3[16] = { -42, -42, -42, -42, -42, -42, -42, -42, -42, -42, -42, -42, -42, -42, -42, -42 };
	static const int8_t us4[16] = { 0 };
	static const struct { uint8_t id; int pct; const int8_t *v; } set[5] = {
		{ 5, 100, us1 }, { 5, 50, us1 }, { 6, 100, us2 }, { 7, 200, us3 }, { 8, 100, us4 },
	};
	for (int pointer = 1; pointer >= 0; pointer--)
	{
		static UserTemplate u[USER_TEMPLATES];
		memset(u, 0, sizeof u);
		memcpy(u[0].vel, us1, 16);
		memcpy(u[1].vel, us2, 16);
		memcpy(u[2].vel, us3, 16);
		fake_useDynamics(true, (TrackTemplate){ 0, 0 }, u);
		fake_host_t *h = fake_init();
		h->logMask = FAKE_LOG_BIT(FAKE_TRIGGER);
		if (!pointer)
			h->host.userTemplates = NULL;
		seqc_init(&h->host, SEQC_CPU_HZ);		/* the host table again, with the pointer cleared */
		for (uint8_t t = 0; t < 5; t++)
		{
			allOn(h, 0, t);
			fake_setVelTemplate(h, 0, t, set[t].id, set[t].pct);
		}
		play(h, 1990.0);
		for (uint8_t t = 0; t < 5; t++)
		{
			const fake_rec_t *r[20];
			ASSERT_EQ(fake_triggers(h, t, r, 20), 16);
			for (int k = 0; k < 16; k++)
				ASSERT_EQ(r[k]->ev.vel, pointer ? velModel(127, ownVel(set[t].v[k]), set[t].pct) : 127);
		}
		ASSERT_EQ(h->errors, 0);
	}
	dynOff();
}

TEST(own_templates_record_place_leaves_them_out)
{
	/* seqc_recordPlace() subtracts an own template as it does a built-in (v0.6 M19): us1 with +24
	 * ticks at position 5 at 100 %: a note 3 ticks after step 5's reference (504) stores +3; with the
	 * value out of range (90) it counts as 0, and the same note stores +27 */
	for (int bad = 0; bad < 2; bad++)
	{
		static UserTemplate u[USER_TEMPLATES];
		memset(u, 0, sizeof u);
		u[0].timing[5] = (int8_t)(bad ? 90 : 24);
		fake_useMicro(true);
		fake_useDynamics(true, (TrackTemplate){ 0, 0 }, u);
		fake_host_t *h = fake_init();
		h->logMask = 0;
		fake_setTemplate(h, 0, 0, TPL_US1, 100);
		fake_process(h, 0);
		seqc_start();
		fake_process(h, 0);
		const seqc_time_t t = (seqc_time_t)(96 * 5 + 24 + 3) * CYCLES_PER_TICK_120;
		fake_runUntil(h, t, fake_msToCycles(0.5));
		seqc_recPlace_t p;
		ASSERT_TRUE(seqc_recordPlace(0, t, &p));
		ASSERT_EQ(p.step, 5);
		ASSERT_EQ(p.micro, bad ? 27 : 3);
		ASSERT_EQ(h->errors, 0);
	}
	dynOff();
}

TEST(own_templates_at_a_position)
{
	/* the capture's reads (V7-10): seqc_templateTicksAt() is G for a step at x1 at position q mod 16,
	 * the template checked, not clamped (P's clamp acts on the sum); seqc_templateVelAt() is the
	 * velocity a step plays at q. Built-ins, own templates through userTemplates, and no pointer */
	for (int pointer = 1; pointer >= 0; pointer--)
	{
		static UserTemplate u[USER_TEMPLATES];
		memset(u, 0, sizeof u);
		u[0].timing[5] = 24;
		u[1].timing[3] = -84;
		u[2].timing[7] = 90;			/* out of range: 0 */
		u[0].vel[2] = -6;
		u[1].vel[2] = -43;			/* out of range: 0 */
		fake_useDynamics(true, (TrackTemplate){ 0, 0 }, u);
		fake_host_t *h = fake_init();
		if (!pointer)
			h->host.userTemplates = NULL;
		seqc_init(&h->host, SEQC_CPU_HZ);
		/* timing: lay at 100 %, 50 %, 200 %, q mod 16, off, an id or amount out of range */
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ LAY, 0 }, 1), 18);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ LAY, -50 }, 1), 9);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ LAY, 100 }, 3), 10);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ LAY, 0 }, 17), 18);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ LAY, 0 }, 255), 5);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ LAY, 0 }, 0), 0);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ 0, 0 }, 1), 0);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ TEMPLATE_COUNT, 0 }, 1), 0);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ LAY, 101 }, 1), 18);
		/* timing: us1..us4 */
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ TPL_US1, 0 }, 5), pointer ? 24 : 0);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ TPL_US1, 0 }, 21), pointer ? 24 : 0);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ TPL_US1, 100 }, 5), pointer ? 48 : 0);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ TPL_US1 + 1, 100 }, 3), pointer ? -168 : 0);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ TPL_US1 + 2, 0 }, 7), 0);
		ASSERT_EQ(seqc_templateTicksAt((TrackTemplate){ TPL_US1 + 3, 0 }, 5), 0);
		/* velocity: of8 at q 3 (-14 dB), q mod 16, depth 0, off, out of range, dep at 200 % */
		ASSERT_EQ(seqc_templateVelAt(120, (TrackTemplate){ OF8, 0 }, 3), 24);
		ASSERT_EQ(seqc_templateVelAt(120, (TrackTemplate){ OF8, 0 }, 19), 24);
		ASSERT_EQ(seqc_templateVelAt(120, (TrackTemplate){ OF8, -100 }, 3), 120);
		ASSERT_EQ(seqc_templateVelAt(120, (TrackTemplate){ 0, 0 }, 3), 120);
		ASSERT_EQ(seqc_templateVelAt(120, (TrackTemplate){ VEL_TEMPLATE_COUNT, 0 }, 3), 120);
		ASSERT_EQ(seqc_templateVelAt(127, (TrackTemplate){ OF8, 101 }, 1), 23);
		ASSERT_EQ(seqc_templateVelAt(127, (TrackTemplate){ DEP, 100 }, 3), velModel(127, -19, 200));
		ASSERT_EQ(seqc_templateVelAt(0, (TrackTemplate){ DEP, 100 }, 3), 0);
		/* velocity: us1 at q 2 (-6 dB), us2's value out of range */
		ASSERT_EQ(seqc_templateVelAt(127, (TrackTemplate){ VEL_TEMPLATE_BUILTIN, 0 }, 2), pointer ? 64 : 127);
		ASSERT_EQ(seqc_templateVelAt(127, (TrackTemplate){ VEL_TEMPLATE_BUILTIN, 0 }, 18), pointer ? 64 : 127);
		ASSERT_EQ(seqc_templateVelAt(127, (TrackTemplate){ VEL_TEMPLATE_BUILTIN + 1, 0 }, 2), 127);
		ASSERT_EQ(h->errors, 0);
	}
	ASSERT_EQ(velModel(127, -6, 100), 64);
	ASSERT_EQ(velModel(127, -19, 200), 2);
	dynOff();
}

TEST_MAIN()
