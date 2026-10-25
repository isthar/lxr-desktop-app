/*
 * test_harness.c
 *
 * Self-test of the host test harness: the runner (seqc_test.h) and the fake
 * host (fake_host.c), which implements the seqc_host_t callbacks the way
 * sequencer.c will. The callbacks are called directly here, so these tests do
 * not depend on the timing core.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "fake_host.h"
#include "seqc_test.h"

static seqc_pat_t patternEnd(fake_host_t *h) { return h->host.patternEnd(h->host.ctx); }
static bool peek(fake_host_t *h, uint8_t *p) { return h->host.peekNextPattern(h->host.ctx, p); }
static uint32_t seek(fake_host_t *h, uint32_t absStep) { return h->host.seekPosition(h->host.ctx, absStep); }

TEST(harness_runner)
{
	ASSERT_TRUE(1 + 1 == 2);
	ASSERT_FALSE(0);
	ASSERT_EQ(-3, -3);
	ASSERT_EQ((uint64_t)1 << 40, (uint64_t)1 << 40);
	ASSERT_NEAR(1.0, 1.0 + 1e-12, 1e-9);
}

/* a failing ASSERT_ sets the flag and returns from its function; this checks the
 * runner without its own macros, and prints four expected failure lines */
static int afterFailingAssert;
static void failingAssert(void) { ASSERT_EQ(1, 2); afterFailingAssert = 1; }
static void failingTrue(void) { ASSERT_TRUE(0); afterFailingAssert = 1; }
static void failingFalse(void) { ASSERT_FALSE(1); afterFailingAssert = 1; }
static void failingNear(void) { ASSERT_NEAR(1.0, 2.0, 0.5); afterFailingAssert = 1; }
TEST(harness_runner_failure)
{
	void (*const helpers[])(void) = { failingAssert, failingTrue, failingFalse, failingNear };
	int ok = 1;
	printf("harness_runner_failure: 4 expected failures follow\n");
	for (size_t i = 0; i < sizeof helpers / sizeof helpers[0]; i++)
	{
		seqc_test_failed_ = 0;
		afterFailingAssert = 0;
		helpers[i]();
		if (seqc_test_failed_ != 1 || afterFailingAssert != 0)
		{
			printf("harness_runner_failure: helper %zu did not fail and return\n", i);
			ok = 0;
		}
	}
	seqc_test_failed_ = !ok;
}

/* The exit status of TEST_MAIN(), which is what ctest sees: main() is called
 * again with a filter. selftest_fails_when_armed fails only while armed; its
 * name does not match the ctest filter "harness", so ctest never runs it on its
 * own. tests/CMakeLists.txt also fails the harness test on any "FAIL harness_"
 * line, in case the exit status itself is broken. */
int main(int argc, char **argv);
static int verdictArmed;
TEST(selftest_fails_when_armed)
{
	ASSERT_FALSE(verdictArmed);
}

TEST(harness_runner_verdict)
{
	char prog[] = "test_harness", failing[] = "selftest_fails_when_armed", none[] = "no_such_test";
	char *withFailure[] = { prog, failing, NULL };
	char *withNone[] = { prog, none, NULL };
	printf("harness_runner_verdict: 1 expected failure follows\n");
	verdictArmed = 1;
	const int failed = main(2, withFailure);
	verdictArmed = 0;
	const int unmatched = main(2, withNone);
	seqc_test_failed_ = 0;
	ASSERT_EQ(failed, 1);
	ASSERT_EQ(unmatched, 2);
}

TEST(harness_runner_filter)
{
	ASSERT_TRUE(seqc_test_matches_("T2", "T2"));
	ASSERT_TRUE(seqc_test_matches_("T2_grid", "T2"));
	ASSERT_FALSE(seqc_test_matches_("T20", "T2"));
	ASSERT_FALSE(seqc_test_matches_("T20_x", "T2"));
}

TEST(harness_defaults)
{
	fake_host_t *h = fake_init();
	ASSERT_TRUE(h->host.ctx == h);
	ASSERT_EQ(h->steps[3][2][5].volume, 127);
	ASSERT_EQ(h->steps[3][2][5].prob, 100);
	ASSERT_EQ(h->steps[3][2][5].note, FAKE_DEFAULT_NOTE);
	ASSERT_EQ(h->steps[3][2][5].param1Nr, FAKE_NO_AUTOMATION);
	ASSERT_EQ(h->timing[3][2][5].shift, SEQC_SHIFT_CENTER);
	ASSERT_EQ(h->length[63][6], 16);
	ASSERT_EQ(h->song[0], 0);
	ASSERT_EQ(h->song[1], FAKE_SONG_END);
	ASSERT_EQ(h->songPos, FAKE_NO_SONG_POS);
	ASSERT_FALSE(h->host.stepOn(h, 0, 0, 0));
	fake_setStep(h, 0, 0, 63, true);
	ASSERT_TRUE(h->host.stepOn(h, 0, 0, 63));
	ASSERT_EQ(h->errors, 0);
	(void)h->host.stepOn(h, 64, 0, 0);		/* out of range: counted */
	ASSERT_EQ(h->errors, 1);
}

TEST(harness_conversions)
{
	ASSERT_EQ(fake_msToCycles(1.0), 216000);
	ASSERT_NEAR(fake_cyclesToMs(216000), 1.0, 1e-12);
	ASSERT_NEAR(fake_stepCycles(120.0), 216e6 * 0.125, 1e-6);
	ASSERT_NEAR(fake_tickCycles(120.0) * 96.0, fake_stepCycles(120.0), 1e-6);
	ASSERT_NEAR(fake_clockCycles(120.0) * 6.0, fake_stepCycles(120.0), 1e-6);
}

TEST(harness_timing_setter)
{
	fake_host_t *h = fake_init();
	fake_setTiming(h, 1, 2, 3, 5, -7);
	ASSERT_EQ(h->timing[1][2][3].flam, 5);
	ASSERT_EQ(h->timing[1][2][3].shift, 0);
	fake_setTiming(h, 1, 2, 3, 0, 7);
	ASSERT_EQ(h->timing[1][2][3].shift, 14);
	fake_setTiming(h, 1, 2, 3, 0, 8);		/* out of range: counted, not stored */
	ASSERT_EQ(h->errors, 1);
	ASSERT_EQ(h->timing[1][2][3].shift, 14);
}

TEST(harness_random)
{
	fake_host_t *h = fake_init();
	uint32_t a[4], b[4];
	for (int i = 0; i < 4; i++)
		a[i] = h->host.random(h);
	ASSERT_TRUE(a[0] != a[1]);
	fake_seed(h, 1);
	for (int i = 0; i < 4; i++)
		b[i] = h->host.random(h);
	for (int i = 0; i < 4; i++)
		ASSERT_EQ(a[i], b[i]);
	static const uint32_t script[] = { 49, 50 };
	fake_scriptRandom(h, script, 2);
	ASSERT_EQ(h->host.random(h), 49);
	ASSERT_EQ(h->host.random(h), 50);
	ASSERT_EQ(h->reads[FAKE_READ_RANDOM], 10);
}

TEST(harness_master_track)
{
	fake_host_t *h = fake_init();
	ASSERT_EQ(fake_masterTrack(h, 0), 0);		/* all 16: the lowest track */
	fake_setLength(h, 0, 3, 32);
	ASSERT_EQ(fake_masterTrack(h, 0), 3);
	for (uint8_t t = 0; t < 7; t++)
		fake_setLength(h, 1, t, 0);
	fake_setLength(h, 1, 5, 8);
	ASSERT_EQ(fake_masterTrack(h, 1), 5);		/* raw 0 (16 steps) loses to raw 8 */
	ASSERT_EQ(fake_effectiveLength(0), 16);
	ASSERT_EQ(fake_effectiveLength(70), 64);
}

TEST(harness_pattern_mode)
{
	fake_host_t *h = fake_init();
	uint8_t p = 0xAA;
	ASSERT_FALSE(peek(h, &p));
	ASSERT_EQ(p, 0);
	ASSERT_EQ(patternEnd(h), SEQC_PAT_SAME);
	seqc_start();					/* running: no immediate switch */
	fake_setNextPattern(h, 5);
	ASSERT_EQ(h->activePattern, 0);
	ASSERT_TRUE(peek(h, &p));
	ASSERT_EQ(p, 5);
	ASSERT_EQ(patternEnd(h), SEQC_PAT_CHANGED);
	ASSERT_EQ(h->activePattern, 5);
	ASSERT_EQ(patternEnd(h), SEQC_PAT_SAME);
	fake_setNextPattern(h, 5);			/* reload the same pattern */
	ASSERT_TRUE(peek(h, &p));
	ASSERT_EQ(patternEnd(h), SEQC_PAT_CHANGED);
	seqc_stop();
	fake_setNextPattern(h, 7);			/* stopped: switches at once */
	ASSERT_EQ(h->activePattern, 7);
	ASSERT_FALSE(h->reload);
	ASSERT_EQ(h->patternChanges, 3);
}

TEST(harness_song_mode)
{
	fake_host_t *h = fake_init();
	static const uint8_t song[] = { 1, 1, 2 };
	fake_setSong(h, song, 3);
	fake_setSongMode(h, true);
	uint8_t p;
	/* the first pattern end after a start moves the pointer from -1 to 0 */
	ASSERT_TRUE(peek(h, &p));
	ASSERT_EQ(p, 1);
	ASSERT_EQ(patternEnd(h), SEQC_PAT_CHANGED);
	ASSERT_EQ(h->songPos, 0);
	ASSERT_FALSE(peek(h, &p));			/* an identical entry: no restart */
	ASSERT_EQ(p, 1);
	ASSERT_EQ(patternEnd(h), SEQC_PAT_SAME);
	ASSERT_TRUE(peek(h, &p));
	ASSERT_EQ(p, 2);
	ASSERT_EQ(patternEnd(h), SEQC_PAT_CHANGED);
	ASSERT_FALSE(peek(h, &p));
	ASSERT_EQ(p, SEQC_NO_PATTERN);			/* the song ends next */
	ASSERT_EQ(patternEnd(h), SEQC_PAT_STOP);
	ASSERT_EQ(h->pendingPattern, 2);
	ASSERT_EQ(h->activePattern, 2);
	/* the core rewinds at a song end; the next start plays entry 0 again */
	h->host.transport(h, SEQC_TRANSPORT_REWIND);
	ASSERT_EQ(h->songPos, FAKE_NO_SONG_POS);
	ASSERT_EQ(patternEnd(h), SEQC_PAT_CHANGED);
	ASSERT_EQ(h->activePattern, 1);
	ASSERT_EQ(h->songPos, 0);
}

TEST(harness_song_loop)
{
	fake_host_t *h = fake_init();
	static const uint8_t song[] = { 1, 2, FAKE_SONG_LOOP };
	fake_setSong(h, song, 3);
	fake_setSongMode(h, true);
	uint8_t p;
	ASSERT_EQ(patternEnd(h), SEQC_PAT_CHANGED);	/* 1 */
	ASSERT_EQ(patternEnd(h), SEQC_PAT_CHANGED);	/* 2 */
	ASSERT_TRUE(peek(h, &p));
	ASSERT_EQ(p, 1);
	ASSERT_EQ(patternEnd(h), SEQC_PAT_CHANGED);	/* loops to 1 */
	ASSERT_EQ(h->songPos, 0);
	ASSERT_EQ(h->activePattern, 1);
	/* a loop onto the same pattern keeps the indices running */
	static const uint8_t song2[] = { 3, FAKE_SONG_LOOP };
	fake_setSong(h, song2, 2);
	h->songPos = FAKE_NO_SONG_POS;
	ASSERT_EQ(patternEnd(h), SEQC_PAT_CHANGED);	/* 3 */
	ASSERT_FALSE(peek(h, &p));
	ASSERT_EQ(p, 3);
	ASSERT_EQ(patternEnd(h), SEQC_PAT_SAME);
}

TEST(harness_seek)
{
	fake_host_t *h = fake_init();
	/* pattern mode: no side effects */
	ASSERT_EQ(seek(h, 1234), 1234);
	ASSERT_EQ(h->patternChanges, 0);
	/* the song A A B LOOP with A = pattern 1 (16 steps), B = pattern 2 (8 steps) */
	for (uint8_t t = 0; t < 7; t++)
		fake_setLength(h, 2, t, 4);
	fake_setLength(h, 2, 4, 8);
	static const uint8_t song[] = { 1, 1, 2, FAKE_SONG_LOOP };
	fake_setSong(h, song, 4);
	fake_setSongMode(h, true);
	ASSERT_EQ(seek(h, 0), 0);
	ASSERT_EQ(h->songPos, 0);
	ASSERT_EQ(h->activePattern, 1);
	ASSERT_EQ(seek(h, 20), 20);			/* entry 1; the run began at 0 */
	ASSERT_EQ(h->patternChanges, 1);		/* the active pattern: no reload */
	ASSERT_EQ(h->songPos, 1);
	ASSERT_EQ(seek(h, 32), 0);			/* B */
	ASSERT_EQ(h->songPos, 2);
	ASSERT_EQ(h->activePattern, 2);
	ASSERT_EQ(seek(h, 39), 7);
	ASSERT_EQ(seek(h, 40), 0);			/* looped to entry 0 */
	ASSERT_EQ(h->songPos, 0);
	ASSERT_EQ(seek(h, 40 + 40 + 17), 17);		/* second loop, entry 1 */
	ASSERT_EQ(h->songPos, 1);
	/* past SONG_END: the last step of the last entry */
	static const uint8_t song2[] = { 1, 2 };
	fake_setSong(h, song2, 2);
	ASSERT_EQ(seek(h, 300), 7);
	ASSERT_EQ(h->songPos, 1);
	ASSERT_EQ(h->activePattern, 2);
	/* a loop that joins the same pattern keeps one run: A B A LOOP */
	static const uint8_t song3[] = { 1, 2, 1, FAKE_SONG_LOOP };
	fake_setSong(h, song3, 4);
	ASSERT_EQ(seek(h, 24 + 3), 3);			/* the second A, its run began at 24 */
	ASSERT_EQ(seek(h, 40 + 5), 16 + 5);		/* the first A after the loop joins it */
	ASSERT_EQ(h->songPos, 0);
	ASSERT_EQ(h->errors, 0);
	/* a song that starts with SONG_END or SONG_LOOP has no entry: the walk
	 * stops at once (the END case first, so that a broken guard fails here and
	 * does not loop forever) and seeks as in pattern mode */
	const uint32_t changes = h->patternChanges;
	static const uint8_t songEnd[] = { FAKE_SONG_END, 1 };
	fake_setSong(h, songEnd, 2);
	ASSERT_EQ(seek(h, 20), 20);
	static const uint8_t songLoop[] = { FAKE_SONG_LOOP, 1 };
	fake_setSong(h, songLoop, 2);
	ASSERT_EQ(seek(h, 20), 20);
	ASSERT_EQ(h->patternChanges, changes);
	ASSERT_EQ(h->errors, 0);
}

TEST(harness_drivers)
{
	fake_host_t *h = fake_init();
	fake_runUntil(h, 1000, 300);			/* 300, 600, 900, then 1000 */
	ASSERT_EQ(h->now, 1000);
	/* the log keeps FAKE_LOG_SIZE entries and counts the rest */
	h->logMask = FAKE_LOG_BIT(FAKE_TEMPO_STEP);
	fake_clearLog(h);
	const uint32_t calls = h->calls[FAKE_TEMPO_STEP];
	for (uint32_t i = 0; i < FAKE_LOG_SIZE + 1; i++)
		h->host.tempoStep(h);
	ASSERT_EQ(h->logCount, FAKE_LOG_SIZE);
	ASSERT_EQ(h->logLost, 1);
	ASSERT_EQ(h->calls[FAKE_TEMPO_STEP], calls + FAKE_LOG_SIZE + 1);
}

TEST(harness_log)
{
	fake_host_t *h = fake_init();
	h->host.tempoStep(h);
	h->host.noteOff(h, 3);
	h->logMask = FAKE_LOG_NO_CLOCKS;
	h->host.clockOutPulse(h, 5);			/* counted, not logged */
	ASSERT_EQ(h->logCount, 2);
	ASSERT_EQ(h->calls[FAKE_CLOCK_PULSE], 1);
	size_t pos = 0;
	const fake_rec_t *r = fake_next(h, FAKE_NOTE_OFF, &pos);
	ASSERT_TRUE(r != NULL);
	ASSERT_EQ(r->track, 3);
	ASSERT_EQ(pos, 2);
	ASSERT_TRUE(fake_next(h, FAKE_NOTE_OFF, &pos) == NULL);
	const seqc_event_t ev = { .tick = 96, .absStep = 1, .track = 2, .pattern = 0, .step = 1, .vel = 100, .note = 60 };
	h->host.trigger(h, &ev);
	const fake_rec_t *trig[4];
	ASSERT_EQ(fake_triggers(h, 2, trig, 4), 1);
	ASSERT_EQ(trig[0]->ev.tick, 96);
	ASSERT_EQ(fake_triggers(h, 1, trig, 4), 0);
	fake_printLog(h, 0, 99);
	fake_clearLog(h);
	ASSERT_EQ(h->logCount, 0);
	ASSERT_EQ(h->calls[FAKE_TRIGGER], 1);		/* the counts keep running */
}

TEST(harness_record)
{
	fake_host_t *h = fake_init();
	h->recordActive = true;
	fake_addNote(h, 0, 90, 61);			/* stopped: nothing */
	ASSERT_EQ(h->mainSteps[0][0], 0);
	const seqc_event_t ev = { .tick = 0, .absStep = 0, .track = 4, .pattern = 0, .step = 6,
			.vel = SEQC_ROLL_VELOCITY, .note = 62, .flags = SEQC_EVF_ROLL };
	fake_setStepData(h, 0, 4, 6, 50, 60, 30);	/* the recording must overwrite these */
	seqc_start();
	fake_addNote(h, 0, 90, 61);			/* running, before the first step: nothing */
	ASSERT_EQ(h->mainSteps[0][0], 0);
	ASSERT_EQ(h->errors, 0);
	h->recordActive = false;
	h->host.trigger(h, &ev);			/* recording off: a roll hit is not recorded */
	ASSERT_EQ(h->mainSteps[0][4], 0);
	h->recordActive = true;
	h->host.trigger(h, &ev);			/* a roll hit is recorded at its own step */
	ASSERT_EQ(h->mainSteps[0][4], 1ull << 6);
	ASSERT_EQ(h->steps[0][4][6].volume, SEQC_ROLL_VELOCITY | FAKE_STEP_ACTIVE_MASK);
	ASSERT_EQ(h->steps[0][4][6].note, 62);
	ASSERT_EQ(h->steps[0][4][6].prob, 100);
	const seqc_event_t hit = { .tick = 0, .absStep = 0, .track = 4, .pattern = 0, .step = 9, .vel = 90, .note = 63 };
	h->host.trigger(h, &hit);			/* an ordinary hit is not recorded */
	ASSERT_EQ(h->mainSteps[0][4], 1ull << 6);
}

TEST_MAIN()
