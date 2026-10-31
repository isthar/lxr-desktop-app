/*
 * test_delay.c
 *
 * Host test of the delay's pointers into FX_BUFFER when the delay type changes
 * while the delay runs (Core/Src/Soundengine/DSPAudio/FX/Delay.c, task E4.8).
 *
 * The Normal delay uses the whole buffer: its write and read pointers run over
 * 0..0x7FFF. The Ping-pong delay keeps the right line in the lower half and the
 * left line in the upper half, so its pointers run over 0..0x3FFF and the left
 * line is at FX_BUFFER[0x4000 + pointer]. The delay type is only a byte that the
 * CC handler sets (CC2_FX_DELAY_TYPE in MidiParser.c: the FX page, a kit load).
 * In fw 1.7 the first Ping-pong block after a switch used the Normal pointers as
 * they were, so with a pointer at 0x4000 or above it stored the left sample up
 * to 32 KB past FX_BUFFER (on the device: the audio DMA buffers, fxBus, the
 * stack, the HAL handles and the kit edit buffer) and read the left line from
 * there.
 *
 * Delay.c is compiled as it is, against the stand-in headers in tests/stubs/
 * (with the CMSIS intrinsics) and the stand-in tone filter below. One name is
 * changed for this target (tests/CMakeLists.txt): FX_BUFFER becomes
 * (*delay_fxBuffer), a pointer to an array of its size, which the test points
 * into a larger buffer. The 0x4000 entries after it are guard entries: a store
 * past the buffer lands there, also without a sanitizer, and the test sees it.
 * The input is silent and the buffer starts at 0, so the output is 0 unless the
 * delay reads a guard entry.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include "Soundengine/DSPAudio/FX/Delay.h"

#define BUFFER_ENTRIES (FX_BUFFER_SIZE + 1)
#define HALF (FX_BUFFER_SIZE >> 1)		/* the Ping-pong mask, 0x3FFF */
#define GUARD_AFTER (HALF + 1 + 64)		/* past FX_BUFFER[0x4000 + 0x7FFF] */
#define GUARD_BYTE 0xA5

_Static_assert(sizeof FX_BUFFER == BUFFER_ENTRIES * sizeof(__fp16), "the buffer of FxRamBuffer.c");

//------------------------------------------------------------------------------
// The buffer and its guard entries
//------------------------------------------------------------------------------
static __fp16 memoryAround[BUFFER_ENTRIES + GUARD_AFTER];
static Delay delay;
static int16_t input[2 * OUTPUT_DMA_SIZE];		/* silent */
static int16_t output[2 * OUTPUT_DMA_SIZE];

/** a Normal delay as FxBus_init() and a kit set it up, a buffer of 0, guard entries after it */
static void reset(void)
{
	delay_fxBuffer = (__fp16 (*)[BUFFER_ENTRIES])memoryAround;
	memset(memoryAround, 0, sizeof(__fp16) * BUFFER_ENTRIES);
	memset((unsigned char *)memoryAround + sizeof(__fp16) * BUFFER_ENTRIES, GUARD_BYTE,
	       sizeof(__fp16) * GUARD_AFTER);
	memset(&delay, 0, sizeof delay);
	Delay_init(&delay);
	delay.delayType = DELAY_NORMAL;
	delay.range = 1.f;
	Delay_setFeedback(&delay, 0.7f);
	Delay_setDelayTime(&delay, 0.5f);
}

/** guard bytes that a store changed */
static long changedAfter(void)
{
	const unsigned char *guard = (const unsigned char *)memoryAround + sizeof(__fp16) * BUFFER_ENTRIES;
	long n = 0;
	for (size_t i = 0; i < sizeof(__fp16) * GUARD_AFTER; i++)
		if (guard[i] != GUARD_BYTE)
			n++;
	return n;
}

/** output samples that are not 0: the delay read something other than the silent buffer */
static int loudOutput(void)
{
	int n = 0;
	for (int i = 0; i < 2 * OUTPUT_DMA_SIZE; i++)
		if (output[i] != 0)
			n++;
	return n;
}

/** one block of size samples, stereo, fully wet, of the delay type set, as FxBus_process() runs it */
static void block(uint16_t size)
{
	memset(output, 0, sizeof output);
	if (delay.delayType == DELAY_NORMAL)
		Delay_process(&delay, input, output, output + 1, size, 2, 1.f);
	else
		Delay_processPingPong(&delay, input, output, output + 1, size, 2, 1.f);
}

/** Normal blocks until the write pointer is at write */
static void runNormalTo(uint32_t write)
{
	while (delay.write != write)
		block(((write - delay.write) & FX_BUFFER_SIZE) >= OUTPUT_DMA_SIZE ? OUTPUT_DMA_SIZE : 1);
}

/** switch to Ping-pong as the CC handler does, then Ping-pong blocks: nothing past the buffer */
static void switchToPingPongAndRun(void)
{
	delay.delayType = DELAY_PINGPONG;
	for (int b = 0; b < 4; b++) {
		block(OUTPUT_DMA_SIZE);
		ASSERT_EQ(changedAfter(), 0);
		ASSERT_EQ(loudOutput(), 0);
		ASSERT_TRUE(delay.write <= HALF);
		ASSERT_TRUE(delay.oldRead <= HALF);
		ASSERT_TRUE(delay.newRead <= HALF);
	}
}

//------------------------------------------------------------------------------
// The tests
//------------------------------------------------------------------------------
static const uint32_t writePointers[] = { 0x0100, 0x3FFF, 0x4000, 0x4001, 0x411C, 0x5555, 0x7FC0, 0x7FFF };

TEST(delay_switch_to_pingpong_stays_in_the_buffer)
{
	for (unsigned i = 0; i < sizeof writePointers / sizeof writePointers[0]; i++) {
		reset();
		runNormalTo(writePointers[i]);
		ASSERT_EQ(changedAfter(), 0);
		switchToPingPongAndRun();
	}
}

TEST(delay_switch_to_pingpong_in_a_crossfade_stays_in_the_buffer)
{
	/* a new delay time just before the switch: both read pointers are in use */
	for (unsigned i = 0; i < sizeof writePointers / sizeof writePointers[0]; i++) {
		reset();
		runNormalTo(writePointers[i]);
		Delay_setDelayTime(&delay, 0.1f);
		ASSERT_TRUE(delay.interpValue > 0.f);
		switchToPingPongAndRun();
	}
}

TEST(delay_switch_back_and_forth_stays_in_the_buffer)
{
	/* the FX page turned to and fro while the delay runs */
	reset();
	for (int b = 0; b < 4000; b++) {
		if (b % 37 == 0)
			delay.delayType = delay.delayType == DELAY_NORMAL ? DELAY_PINGPONG : DELAY_NORMAL;
		if (b % 5 == 0)
			Delay_setDelayTime(&delay, (float)(b % 11) / 10.f);
		block(OUTPUT_DMA_SIZE);
		ASSERT_EQ(changedAfter(), 0);
		ASSERT_EQ(loudOutput(), 0);
	}
}

//------------------------------------------------------------------------------
// LICHOMAT v0.4 (plan D38): the time and range knobs while the delay is synced. The audio
// interrupt sets the synced time through Delay_setSync(), which calls Delay_setDelayTime();
// the main loop sets the two knobs through Delay_setRangeAndTime(), with interrupts masked,
// so that the two cannot interleave inside Delay_setDelayTime() (an interleave left the
// main loop's offset, at the old tempo, under the new synced time). The race is the
// device's; this is the deterministic part.
//------------------------------------------------------------------------------
float calcDelayTime(Delay *d, float timeCv);	/* Delay.c */

/** blocks until the crossfade and any pending time are done */
static void settle(void)
{
	for (int b = 0; b < 200 && (delay.interpValue > 0.f || delay.timePending); b++)
		block(OUTPUT_DMA_SIZE);
	ASSERT_TRUE(delay.interpValue <= 0.f && !delay.timePending);
}

/** the delay in effect is samples: whole samples from the read pointer to the write pointer.
 * One more when the time came in pending (fw 1.7: Delay_process() applies it after the
 * sample's read, so the read pointer is set one sample late) */
static int delayInEffect(uint32_t samples)
{
	const uint32_t d = (delay.write - 1 - delay.oldRead) & FX_BUFFER_SIZE;
	return (d == samples) || (d == samples + 1);
}

TEST(DY2_knobs_while_synced_keep_the_synced_time)
{
	reset();
	settle();
	Delay_setSync(&delay, 11035.f);		/* an eighth at 120 BPM */
	settle();
	ASSERT_TRUE(delayInEffect(11035));
	/* the range and time knobs move: the synced time stays, the knobs are kept */
	Delay_setRangeAndTime(&delay, 0.3f, 0.9f);
	ASSERT_TRUE(delay.range == 0.3f);
	settle();
	ASSERT_TRUE(delayInEffect(11035));
	ASSERT_TRUE(delay.delayTime == 0.9f);
	/* a new synced time, and the knobs during its crossfade */
	Delay_setSync(&delay, 7357.f);		/* an eighth triplet */
	Delay_setRangeAndTime(&delay, 0.4f, 0.2f);
	settle();
	ASSERT_TRUE(delayInEffect(7357));
	ASSERT_TRUE(delay.range == 0.4f && delay.delayTime == 0.2f);
	/* sync off: the knobs' last time is in effect */
	Delay_setSync(&delay, 0.f);
	settle();
	ASSERT_TRUE(delayInEffect((uint32_t)calcDelayTime(&delay, delay.delayTime * delay.range)));
	ASSERT_TRUE(!delayInEffect(7357));
}

TEST_MAIN()

//------------------------------------------------------------------------------
// Firmware state that Delay.c reads and writes: FX_BUFFER, as a pointer here
//------------------------------------------------------------------------------
__fp16 (*delay_fxBuffer)[BUFFER_ENTRIES];

//------------------------------------------------------------------------------
// Stand-ins for the functions it calls: the tone filter passes the signal on
//------------------------------------------------------------------------------
void initOnePole(OnePoleFilter *filter) { filter->b = 0.0; filter->r = 1.0; }
float calcOnePole(OnePoleFilter *filter, double input) { filter->b = input; return (float)input; }
void setOnePoleCoef(OnePoleFilter *filter, double val) { filter->r = val; }
