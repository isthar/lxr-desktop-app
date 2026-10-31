/*
 * test_vsched.c: the voice scheduler (plan v0.5 Task 3): the offsets of spec 5.3, the
 * window (S5), the rings with their collapse, overflow and cap (S2, S10, S16, S20) and
 * the 16-bit MIDI stamps (S9). Built with VSCHED_TEST; built twice: cap 6, and
 * VSCHED_SPLIT_CAP=3 (ctest vsched_cap3).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include "Soundengine/vsched.h"

#define L0 VSCHED_BLOCK_CYCLES

/* the calls of vsched.c: 'p' p-locks (a = p1Nr, b = p1Val), 's' slide (a = mode), 't' trigger (a = note) */
typedef struct { char what; uint8_t track, a, b; } call_t;
static call_t calls[64];
static int ncalls;
static uint8_t inTick = 1;
static void note(char what, uint8_t track, uint8_t a, uint8_t b)
{
	if (ncalls < 64)
		calls[ncalls] = (call_t){ what, track, a, b };
	ncalls++;
}
uint8_t seq_isInTick(void) { return inTick; }
void seq_applyPlocks(uint8_t track, uint8_t p1Nr, uint8_t p1Val, uint8_t p2Nr, uint8_t p2Val)
{
	(void)p2Nr; (void)p2Val;
	note('p', track, p1Nr, p1Val);
}
void Drum_setSlide(const uint8_t voiceNr, const uint8_t mode) { note('s', voiceNr, mode, 0); }
void voiceControl_trigger(uint8_t track, uint8_t n, uint8_t vel) { note('t', track, n, vel); }

typedef struct { uint8_t track, k, late; uint32_t serial; } traced_t;
static traced_t traced[64];
static int ntraced;
static void trace(const vsched_rec_t *r, uint32_t serial, uint8_t k, uint8_t late)
{
	if (ntraced < 64)
		traced[ntraced] = (traced_t){ r->track, k, late, serial };
	ntraced++;
}

static void reset(void)
{
	vsched_testReset();
	inTick = 1;
	ncalls = ntraced = 0;
	vsched_setTrace(trace);
}

static void postTimed(uint8_t track, uint32_t t, uint8_t n)
{
	vsched_post(&(vsched_rec_t){ .time = t, .track = track, .note = n, .vel = 100, .flags = VS_TIMED });
}

static int triggers(void)
{
	int n = 0;
	for (int i = 0; i < ncalls && i < 64; i++)
		n += calls[i].what == 't';
	return n;
}

/** two completions one block apart: the window is [w0, w0 + L0) */
static void windowAt(uint32_t w0)
{
	vsched_blockDone(w0);
	vsched_blockDone(w0 + L0);
}

/** the mixer's calls for one block */
static void block(void)
{
	vsched_beginBlock();
	vsched_applyPhase0();
	for (uint8_t v = 0; v < VSCHED_VOICES; v++)
		if (vsched_split(v))
			vsched_applySplit(v);
		else if (vsched_phase(v))
			vsched_endPhase(v);
}

static void offsetsAround(uint32_t w0, uint32_t len)
{
	uint8_t k;
	for (uint32_t f = 0; f < 64; f++)			/* each frame boundary, one cycle either side */
		for (int dd = (f ? -1 : 0); dd <= 1; dd++)
		{
			const uint32_t d = (uint32_t)(((uint64_t)f * len) / 64u) + (uint32_t)dd;
			const vsched_fate_t fate = vsched_offset(w0 + d, w0, len, &k);
			if (fate == VS_WAIT)
			{
				ASSERT_TRUE(32u * d + len / 2u >= 32u * len);	/* only j = 32 waits */
				continue;
			}
			ASSERT_EQ(fate, VS_DUE);
			ASSERT_EQ(k % 2, 0);
			ASSERT_TRUE(k <= 62);
			const double err = (double)k * len / 64.0 - (double)d;
			ASSERT_TRUE(err <= len / 64.0 && -err <= len / 64.0);
		}
	ASSERT_EQ(vsched_offset(w0, w0, len, &k), VS_DUE);
	ASSERT_EQ(k, 0);
	ASSERT_EQ(vsched_offset(w0 + len - 1u, w0, len, &k), VS_WAIT);		/* j = 32 */
	ASSERT_EQ(vsched_offset(w0 + len - 1u, w0 + len, len, &k), VS_DUE);	/* ... at 0 next, uncounted */
	ASSERT_EQ(k, 0);
	ASSERT_EQ(vsched_offset(w0 + len, w0, len, &k), VS_WAIT);
	ASSERT_EQ(vsched_offset(w0 + 3u * len, w0, len, &k), VS_WAIT);
	ASSERT_EQ(vsched_offset(w0 - len / 32u + 1u, w0, len, &k), VS_DUE);
	ASSERT_EQ(vsched_offset(w0 - len / 32u, w0, len, &k), VS_DUE);
	ASSERT_EQ(k, 0);
	ASSERT_EQ(vsched_offset(w0 - len / 32u - 1u, w0, len, &k), VS_LATE);
	ASSERT_EQ(vsched_offset(w0 - 5u * len, w0, len, &k), VS_LATE);
}

TEST(vsched_offsets)
{
	static const uint32_t lens[] = { L0 - 2000u, L0 - 1u, L0, L0 + 1u, L0 + 2000u };
	static const uint32_t starts[] = { 1000u, 0x7FFFFFF0u, 0xFFFFFFFFu - L0 / 2u, 0xFFFFFFFFu - 10u };
	for (unsigned i = 0; i < 5; i++)
		for (unsigned s = 0; s < 4 && !seqc_test_failed_; s++)
			offsetsAround(starts[s], lens[i]);
}

/** the offset of a record of voice 0 at t in the next block's plan; it is applied */
static uint8_t kOfRecordAt(uint32_t t)
{
	postTimed(0, t, 1);
	vsched_beginBlock();
	const uint8_t k = vsched_split(0);
	vsched_applyPhase0();
	vsched_applySplit(0);
	vsched_endPhase(0);
	return k;
}

TEST(vsched_window)
{
	uint32_t w0, len;
	vsched_stats_t st;
	reset();
	ASSERT_FALSE(vsched_window(&w0, &len));			/* no completion yet */
	uint32_t t = 5000u;
	for (int i = 0; i < 10; i++, t += L0)
		vsched_blockDone(t);
	ASSERT_TRUE(vsched_window(&w0, &len));			/* exact periods: the last period */
	ASSERT_EQ(w0, t - 2u * L0);
	ASSERT_EQ(len, L0);
	ASSERT_EQ(kOfRecordAt(w0 + L0 / 2u), 32);
	vsched_blockDone(t + 6480u);				/* 30 us late: the estimate moves 1/8 of it */
	ASSERT_TRUE(vsched_window(&w0, &len));
	ASSERT_EQ(w0 + len, t + 810u);
	t += L0;
	vsched_blockDone(t);					/* on time: earlier than predicted, taken */
	ASSERT_TRUE(vsched_window(&w0, &len));
	ASSERT_EQ(w0 + len, t);
	vsched_blockDone(t + L0 - 1000u);			/* early: taken at once */
	ASSERT_TRUE(vsched_window(&w0, &len));
	ASSERT_EQ(w0 + len, t + L0 - 1000u);

	reset();						/* a slip of one frame, followed within 16 blocks */
	t = 100000u;
	for (int i = 0; i < 4; i++, t += L0)
		vsched_blockDone(t);
	t += 4893u;
	for (int i = 0; i < 16; i++, t += L0)
		vsched_blockDone(t);
	ASSERT_TRUE(vsched_window(&w0, &len));
	ASSERT_TRUE((t - L0) - (w0 + len) < 2447u);		/* 4893 x (7/8)^16 = 578 < half a frame */

	reset();						/* 10^6 periods of 313,180.885: no drift */
	double exact = 7.0;
	uint32_t stamp = 0;
	for (int i = 0; i < 1000000; i++, exact += 313180.885)
		vsched_blockDone(stamp = (uint32_t)(uint64_t)exact);
	ASSERT_TRUE(vsched_window(&w0, &len));
	ASSERT_TRUE(stamp - (w0 + len) <= 1u || (w0 + len) - stamp <= 1u);

	reset();						/* a 10 ms stall re-seeds: no window, late at 0 */
	t = 3000u;
	for (int i = 0; i < 4; i++, t += L0)
		vsched_blockDone(t);
	t += 2160000u;
	vsched_blockDone(t);
	ASSERT_FALSE(vsched_window(&w0, &len));
	ASSERT_EQ(kOfRecordAt(t - 1000u), 0);
	vsched_stats(&st);
	ASSERT_EQ(st.late, 1);
	vsched_blockDone(t + L0);
	ASSERT_TRUE(vsched_window(&w0, &len));
	ASSERT_EQ(w0, t);
	ASSERT_EQ(len, L0);
}

TEST(vsched_window_late)
{
	/* review of Task 7: a stamp 0.6 blocks late (TIM2 renders during card access and delays the
	 * SPI3 interrupt) re-seeds the estimate. The window it ends, 1.6 blocks, is none: a record in
	 * it goes to offset 0 and counts as late, as v0.4 would play it; it would otherwise be due
	 * at k 40, 24 frames early */
	uint32_t w0, len;
	vsched_stats_t st;
	reset();
	uint32_t t = 40000u;
	for (int i = 0; i < 6; i++, t += L0)
		vsched_blockDone(t);
	ASSERT_TRUE(vsched_window(&w0, &len));
	ASSERT_EQ(w0, t - 2u * L0);
	const uint32_t late = t + 6u * L0 / 10u;
	vsched_blockDone(late);
	ASSERT_FALSE(vsched_window(&w0, &len));		/* not [t - L0, late) */
	ASSERT_EQ(kOfRecordAt(t), 0);
	vsched_stats(&st);
	ASSERT_EQ(st.late, 1);
	ASSERT_EQ(traced[ntraced - 1].late, 1);
	t += L0;
	vsched_blockDone(t);					/* on time: 0.6 blocks before the estimate, a re-seed */
	ASSERT_FALSE(vsched_window(&w0, &len));
	vsched_blockDone(t + L0);				/* from the re-seeded stamp on: whole */
	ASSERT_TRUE(vsched_window(&w0, &len));
	ASSERT_EQ(w0, t);
	ASSERT_EQ(len, L0);

	reset();						/* 0.4 blocks late: no re-seed, the filter moves 1/20 block */
	t = 40000u;
	for (int i = 0; i < 6; i++, t += L0)
		vsched_blockDone(t);
	vsched_blockDone(t + 4u * L0 / 10u);
	ASSERT_TRUE(vsched_window(&w0, &len));
	ASSERT_EQ(w0, t - L0);
	ASSERT_EQ(len, L0 + 4u * L0 / 10u / 8u);
}

static int hookStep, hookTimes, hookEvery;
static uint32_t hookT;
/* hookTimes completions at the read's step hookStep; then hookEvery at each later read (0: none) */
static void writeBetween(int step)
{
	if (step != hookStep)
		return;
	for (int i = 0; i < hookTimes; i++, hookT += L0)
		vsched_blockDone(hookT);
	hookTimes = hookEvery;
}

TEST(vsched_window_torn)
{
	/* 1-3 completions between any two loads of the read: always a window the writer published.
	 * One leaves the slot read: the window the read started with. Two or three make it read
	 * once more: the latest window (review of Task 7) */
	for (int step = 0; step < 3; step++)
		for (int times = 1; times <= 3; times++)
		{
			reset();
			for (hookT = 9000u; hookT < 9000u + 3u * L0; hookT += L0)
				vsched_blockDone(hookT);
			uint32_t w0 = 0, len = 0;
			vsched_testReadHook = writeBetween;
			hookStep = step;
			hookTimes = times;
			hookEvery = 0;
			const int ok = vsched_window(&w0, &len);
			vsched_testReadHook = 0;
			ASSERT_TRUE(ok);
			ASSERT_EQ(len, L0);
			ASSERT_EQ(w0, 9000u + (times < 2 ? 1u : 1u + (uint32_t)times) * L0);
		}
	/* two completions inside each of the two reads: no window, not a torn one */
	for (int step = 0; step < 3; step++)
	{
		reset();
		for (hookT = 9000u; hookT < 9000u + 3u * L0; hookT += L0)
			vsched_blockDone(hookT);
		uint32_t w0 = 0, len = 0;
		vsched_testReadHook = writeBetween;
		hookStep = step;
		hookTimes = hookEvery = 2;
		const int ok = vsched_window(&w0, &len);
		vsched_testReadHook = 0;
		ASSERT_FALSE(ok);
	}
}

TEST(vsched_rings)
{
	vsched_stats_t st;
	reset();
	const uint32_t w0 = 1000000u;
	windowAt(w0);
	/* voice 0: two records in one window go to the later one's offset, in order */
	vsched_post(&(vsched_rec_t){ .time = w0 + L0 / 8u, .track = 0, .note = 50, .vel = 1,
		.flags = VS_TIMED | VS_PLOCKS | (1u << VS_SLIDE_SHIFT), .p1Nr = 7, .p1Val = 70 });
	postTimed(0, w0 + L0 / 2u, 51);
	postTimed(1, w0, 60);					/* voice 1: k = 0 and k = 40 both at 40 */
	postTimed(1, w0 + 20u * L0 / 32u, 61);
	vsched_post(&(vsched_rec_t){ .track = 3, .note = 62, .vel = 5, .flags = VS_SRC_ONESHOT });	/* untimed */
	postTimed(6, w0 + L0, 63);				/* the open hat: the next block */
	vsched_beginBlock();
	ASSERT_EQ(vsched_split(0), 32);
	ASSERT_EQ(vsched_split(1), 40);
	ASSERT_EQ(vsched_split(3), 0);
	ASSERT_EQ(vsched_split(5), 0);
	vsched_applyPhase0();
	ASSERT_EQ(ncalls, 1);					/* the snare, before the control path */
	ASSERT_EQ(calls[0].track, 3);
	vsched_applySplit(0);
	ASSERT_EQ(ncalls, 6);					/* p-locks, slide on, 50, slide off, 51 */
	ASSERT_EQ(calls[1].what, 'p');
	ASSERT_EQ(calls[1].a, 7);
	ASSERT_EQ(calls[1].b, 70);
	ASSERT_EQ(calls[2].what, 's');
	ASSERT_EQ(calls[2].a, 1);
	ASSERT_EQ(calls[3].a, 50);
	ASSERT_EQ(calls[4].a, 0);
	ASSERT_EQ(calls[5].a, 51);
	ASSERT_EQ(vsched_phase(0), 32);
	vsched_applySplit(1);
	ASSERT_EQ(ncalls, 10);
	ASSERT_EQ(calls[7].a, 60);
	ASSERT_EQ(calls[9].a, 61);
	ASSERT_EQ(ntraced, 5);
	ASSERT_EQ(traced[0].k, 0);
	ASSERT_EQ(traced[2].k, 32);
	ASSERT_EQ(traced[4].k, 40);
	ASSERT_EQ(traced[4].serial, 2);

	vsched_blockDone(w0 + 2u * L0);				/* the next block: the hat at 0, the phases */
	ncalls = 0;
	vsched_beginBlock();
	ASSERT_EQ(vsched_phase(0), 32);
	ASSERT_EQ(vsched_phase(1), 40);
	vsched_applyPhase0();
	ASSERT_EQ(ncalls, 1);
	ASSERT_EQ(calls[0].track, 6);
	vsched_endPhase(0);
	vsched_endPhase(1);
	vsched_stats(&st);
	ASSERT_EQ(st.splits, 2);
	ASSERT_EQ(st.phaseSplits, 2);
	ASSERT_EQ(st.late, 0);

	postTimed(2, w0 + L0, 70);				/* more than 2 frames before its window: late */
	vsched_blockDone(w0 + 3u * L0);
	block();
	vsched_stats(&st);
	ASSERT_EQ(st.late, 1);
	ASSERT_EQ(traced[ntraced - 1].late, 1);

	reset();						/* the fifth record drops the oldest */
	windowAt(5000u);
	for (uint8_t n = 0; n < 5; n++)
		postTimed(4, 5000u + 3u * L0 + n, (uint8_t)(80 + n));
	vsched_stats(&st);
	ASSERT_EQ(st.overflow, 1);
	block();
	ASSERT_EQ(ncalls, 0);					/* all wait */
	for (uint32_t b = 2; b <= 4; b++)
		vsched_blockDone(5000u + b * L0);
	block();
	ASSERT_EQ(ncalls, 4);
	ASSERT_EQ(calls[0].a, 81);
	ASSERT_EQ(calls[3].a, 84);

	reset();						/* voices independent; a due record behind a waiting one */
	windowAt(5000u);
	postTimed(2, 5000u + 2u * L0, 1);
	postTimed(4, 5000u + 2u, 2);
	postTimed(2, 5000u + 4u, 3);
	block();
	ASSERT_EQ(triggers(), 2);
	ASSERT_EQ(calls[1].track, 2);
	ASSERT_EQ(calls[1].a, 3);
	ASSERT_EQ(calls[2].track, 4);

	reset();						/* a post outside seq_tick() is dropped and counted */
	inTick = 0;
	postTimed(0, 1u, 1);
	inTick = 1;
	vsched_stats(&st);
	ASSERT_EQ(st.offTick, 1);
	block();
	ASSERT_EQ(ncalls, 0);
}

TEST(vsched_rings_phase_replaced)
{
	/* spec 4.2, both in one block: a record at k2 in the block where voice 0's phase k1 is pending
	 * splits it at k2 (the control path skipped the voice: the old note's first step runs on to
	 * k2), and its split sets the phase k2; the replaced phase split is not counted (review of
	 * Task 7). k2 after k1 and before it */
	static const uint8_t ks[2][2] = { { 16, 40 }, { 40, 16 } };
	vsched_stats_t st;
	for (unsigned c = 0; c < 2; c++)
	{
		reset();
		windowAt(5000u);
		postTimed(0, 5000u + ks[c][0] * L0 / 64u, 1);
		vsched_beginBlock();
		ASSERT_EQ(vsched_split(0), ks[c][0]);
		vsched_applyPhase0();
		vsched_applySplit(0);
		ASSERT_EQ(vsched_phase(0), ks[c][0]);
		vsched_blockDone(5000u + 2u * L0);
		postTimed(0, 5000u + L0 + ks[c][1] * L0 / 64u, 2);
		vsched_beginBlock();
		ASSERT_EQ(vsched_split(0), ks[c][1]);
		ASSERT_EQ(vsched_phase(0), ks[c][0]);	/* pending: the control path skips voice 0 */
		vsched_applyPhase0();
		ASSERT_EQ(vsched_phase(0), ks[c][0]);
		ASSERT_EQ(triggers(), 1);
		vsched_applySplit(0);
		ASSERT_EQ(vsched_split(0), 0);
		ASSERT_EQ(vsched_phase(0), ks[c][1]);	/* the new note's */
		ASSERT_EQ(triggers(), 2);
		ASSERT_EQ(calls[3].what, 't');		/* slide, 1, slide, 2 */
		ASSERT_EQ(calls[3].a, 2);
		vsched_blockDone(5000u + 3u * L0);
		vsched_beginBlock();
		ASSERT_EQ(vsched_split(0), 0);
		ASSERT_EQ(vsched_phase(0), ks[c][1]);
		vsched_applyPhase0();
		vsched_endPhase(0);
		ASSERT_EQ(vsched_phase(0), 0);
		vsched_stats(&st);
		ASSERT_EQ(st.splits, 2);
		ASSERT_EQ(st.phaseSplits, 1);
	}
	/* a record at offset 0 there: the new note's first control step is the block's (S12), the
	 * phase goes before the control path */
	reset();
	windowAt(5000u);
	postTimed(0, 5000u + L0 / 4u, 1);
	block();
	ASSERT_EQ(vsched_phase(0), 16);
	vsched_blockDone(5000u + 2u * L0);
	postTimed(0, 5000u + L0, 2);
	vsched_beginBlock();
	ASSERT_EQ(vsched_split(0), 0);
	ASSERT_EQ(vsched_phase(0), 16);
	vsched_applyPhase0();
	ASSERT_EQ(vsched_phase(0), 0);
	ASSERT_EQ(triggers(), 2);
	vsched_stats(&st);
	ASSERT_EQ(st.phaseSplits, 0);
}

TEST(vsched_cap)
{
	/* S20: VSCHED_SPLIT_CAP voices split; one more makes the block fall back to offset 0 */
	vsched_stats_t st;
	for (unsigned extra = 0; extra <= 1 && VSCHED_SPLIT_CAP + extra <= VSCHED_VOICES; extra++)
	{
		const unsigned voices = VSCHED_SPLIT_CAP + extra;
		reset();
		windowAt(77777u);
		for (uint8_t v = 0; v < voices; v++)
			postTimed(v, 77777u + L0 / 4u, 1);
		vsched_beginBlock();
		for (uint8_t v = 0; v < voices; v++)
			ASSERT_EQ(vsched_split(v), extra ? 0 : 16);
		vsched_applyPhase0();
		ASSERT_EQ(triggers(), extra ? (int)voices : 0);
		vsched_stats(&st);
		ASSERT_EQ(st.capFallbacks, extra);
	}
	if (VSCHED_SPLIT_CAP < VSCHED_VOICES)
	{
		reset();					/* a pending phase counts, and the fallback clears it */
		windowAt(77777u);
		postTimed(0, 77777u + L0 / 4u, 1);
		block();
		ASSERT_EQ(vsched_phase(0), 16);
		vsched_blockDone(77777u + 2u * L0);
		for (uint8_t v = 1; v <= VSCHED_SPLIT_CAP; v++)
			postTimed(v, 77777u + L0 + L0 / 2u, 1);
		vsched_beginBlock();
		ASSERT_EQ(vsched_phase(0), 0);
		ASSERT_EQ(vsched_split(1), 0);
		vsched_stats(&st);
		ASSERT_EQ(st.capFallbacks, 1);
	}
}

TEST(vsched_stamps)
{
	uint32_t rnd = 12345u;
	for (uint32_t i = 0; i < 200000u; i++)
	{
		rnd = rnd * 1664525u + 1013904223u;
		const uint32_t now = rnd ^ (i * 2654435761u);		/* across the wrap too */
		rnd = rnd * 1664525u + 1013904223u;
		const uint32_t stamp = now - rnd % 16761600u;		/* 0 .. 77.6 ms old */
		const uint32_t t = vsched_stampTime(now, (uint16_t)(stamp >> 8));
		ASSERT_TRUE(stamp - t < 256u);
		ASSERT_TRUE((int32_t)(now - t) >= 0);
	}
	const uint32_t now = 0x12345678u;			/* older than 2^24: younger, not later than now */
	const uint32_t t = vsched_stampTime(now, (uint16_t)((now - (1u << 24) - 5000u) >> 8));
	ASSERT_TRUE((int32_t)(now - t) >= 0);
	ASSERT_TRUE(now - t < 6000u);
}

TEST_MAIN()
