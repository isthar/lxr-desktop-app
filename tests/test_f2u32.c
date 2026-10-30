/*
 * test_f2u32.c
 *
 * Host test of f2u32_sat() (Core/Inc/Soundengine/DSPAudio/f2u32.h, stage 1b
 * F6), the float-to-uint32 conversion of the oscillators' FM offsets and phase
 * increments. It must give what the Cortex-M7's VCVT.U32.F32 gives, and arm64's
 * FCVTZU gives the same: truncation towards zero, 0 for NaN and for everything
 * below 1, UINT32_MAX from 2^32 up. The edge values are checked on every host.
 * On an arm64 host the conversion is also compared with FCVTZU itself, for
 * every sign and exponent with many mantissas, NaNs and infinities included
 * (emulator spec §12.1, M1).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include <float.h>
#include <math.h>
#include <stdint.h>

#include "Soundengine/DSPAudio/f2u32.h"

/** the float with the bit pattern bits */
static float fromBits(uint32_t bits)
{
	float f;
	memcpy(&f, &bits, sizeof f);
	return f;
}

TEST(f2u32_edge_values)
{
	ASSERT_EQ(f2u32_sat(0.f), 0u);
	ASSERT_EQ(f2u32_sat(-0.f), 0u);
	ASSERT_EQ(f2u32_sat(fromBits(0x00000001u)), 0u);	/* the smallest denormal */
	ASSERT_EQ(f2u32_sat(0.75f), 0u);
	ASSERT_EQ(f2u32_sat(1.f), 1u);
	ASSERT_EQ(f2u32_sat(1.75f), 1u);
	ASSERT_EQ(f2u32_sat(-0.75f), 0u);
	ASSERT_EQ(f2u32_sat(-1.f), 0u);
	ASSERT_EQ(f2u32_sat(-1e30f), 0u);
	ASSERT_EQ(f2u32_sat(-INFINITY), 0u);
	ASSERT_EQ(f2u32_sat(16777216.f), 16777216u);
	ASSERT_EQ(f2u32_sat(4294967040.f), 4294967040u);	/* the largest float below 2^32 */
	ASSERT_EQ(f2u32_sat(4294967296.f), UINT32_MAX);	/* 2^32 */
	ASSERT_EQ(f2u32_sat(5e9f), UINT32_MAX);
	ASSERT_EQ(f2u32_sat(FLT_MAX), UINT32_MAX);
	ASSERT_EQ(f2u32_sat(INFINITY), UINT32_MAX);
	ASSERT_EQ(f2u32_sat(NAN), 0u);
	ASSERT_EQ(f2u32_sat(fromBits(0xFFC00000u)), 0u);	/* a negative quiet NaN */
	ASSERT_EQ(f2u32_sat(fromBits(0x7F800001u)), 0u);	/* a signalling NaN */
}

// LICHOMAT F15: f2i32_sat(), the M7's VCVT.S32.F32: towards zero, saturating, NaN 0
TEST(f2i32_sat_edge_values)
{
	ASSERT_EQ(f2i32_sat(0.f), 0);
	ASSERT_EQ(f2i32_sat(-1.75f), -1);
	ASSERT_EQ(f2i32_sat(1.75f), 1);
	ASSERT_EQ(f2i32_sat(2147483520.f), 2147483520);	/* the largest float below 2^31 */
	ASSERT_EQ(f2i32_sat(2147483648.f), INT32_MAX);	/* 2^31 */
	ASSERT_EQ(f2i32_sat(-2147483648.f), INT32_MIN);
	ASSERT_EQ(f2i32_sat(-2.30785e9f), INT32_MIN);		/* the filter of the kit pack */
	ASSERT_EQ(f2i32_sat(FLT_MAX), INT32_MAX);
	ASSERT_EQ(f2i32_sat(-INFINITY), INT32_MIN);
	ASSERT_EQ(f2i32_sat(NAN), 0);
}

// LICHOMAT F15: f2i16_wrap(), a float stored into an int16_t as the M7 stores it:
// VCVT.S32.F32 (towards zero, saturating at the int32 range, NaN 0), then STRH
TEST(f2i16_wrap_edge_values)
{
	ASSERT_EQ(f2i16_wrap(0.f), 0);
	ASSERT_EQ(f2i16_wrap(32767.9f), 32767);
	ASSERT_EQ(f2i16_wrap(-32768.9f), -32768);
	ASSERT_EQ(f2i16_wrap(32768.f), -32768);		/* wraps round */
	ASSERT_EQ(f2i16_wrap(40703.8f), 40703 - 65536);
	ASSERT_EQ(f2i16_wrap(-33595.5f), -33595 + 65536);
	ASSERT_EQ(f2i16_wrap(-1.75f), -1);
	ASSERT_EQ(f2i16_wrap(3e9f), -1);			/* INT32_MAX: 0x7FFFFFFF */
	ASSERT_EQ(f2i16_wrap(-3e9f), 0);			/* INT32_MIN: 0x80000000 */
	ASSERT_EQ(f2i16_wrap(INFINITY), -1);
	ASSERT_EQ(f2i16_wrap(-INFINITY), 0);
	ASSERT_EQ(f2i16_wrap(NAN), 0);
}

#if defined(__aarch64__)
/** arm64's FCVTZS (to 32 bits, towards zero), which saturates as the M7's VCVT.S32.F32 */
static int32_t fcvtzs(float x)
{
	int32_t r;
	__asm__("fcvtzs %w0, %s1" : "=r"(r) : "w"(x));
	return r;
}

TEST(f2i16_wrap_equals_fcvtzs)
{
	static const uint32_t low[3] = { 0x0000u, 0x0001u, 0xFFFFu };
	for (uint32_t high = 0; high <= 0xFFFFu; high++) {
		for (int k = 0; k < 3; k++) {
			const float x = fromBits(high << 16 | low[k]);
			ASSERT_EQ(f2i16_wrap(x), (int16_t)(uint16_t)(uint32_t)fcvtzs(x));
		}
	}
}

TEST(f2i32_sat_equals_fcvtzs)
{
	static const uint32_t low[3] = { 0x0000u, 0x0001u, 0xFFFFu };
	for (uint32_t high = 0; high <= 0xFFFFu; high++) {
		for (int k = 0; k < 3; k++) {
			const float x = fromBits(high << 16 | low[k]);
			ASSERT_EQ(f2i32_sat(x), fcvtzs(x));
		}
	}
}

/** arm64's FCVTZU (to 32 bits, towards zero), which saturates as the M7 does */
static uint32_t fcvtzu(float x)
{
	uint32_t r;
	__asm__("fcvtzu %w0, %s1" : "=r"(r) : "w"(x));
	return r;
}

TEST(f2u32_equals_fcvtzu)
{
	/* every high half-word: each sign, exponent and top of the mantissa, with
	 * three low half-words */
	static const uint32_t low[3] = { 0x0000u, 0x0001u, 0xFFFFu };
	for (uint32_t high = 0; high <= 0xFFFFu; high++) {
		for (int k = 0; k < 3; k++) {
			const uint32_t bits = high << 16 | low[k];
			const float x = fromBits(bits);
			if (f2u32_sat(x) != fcvtzu(x))
				printf("0x%08X: %u, FCVTZU %u\n", (unsigned)bits, (unsigned)f2u32_sat(x), (unsigned)fcvtzu(x));
			ASSERT_EQ(f2u32_sat(x), fcvtzu(x));
		}
	}
}
#endif

TEST_MAIN()
