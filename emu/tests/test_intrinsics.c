/*
 * test_intrinsics.c
 *
 * M1 (stage 1b spec 8.3, 12.1): the CMSIS intrinsics of the stand-in HAL header
 * give the Cortex-M7's results at their edges, and F6's conversion f2u32_sat()
 * gives arm64's FCVTZU, NaN included. tests/test_f2u32.c checks it in the
 * firmware's host tests; this repeats it under the emulator's flags.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <stdint.h>

#include "stm32f7xx_hal.h"
#include "Soundengine/DSPAudio/f2u32.h"
#include "seqc_test.h"

TEST(M1_ssat)
{
	ASSERT_EQ(__SSAT(40000, 16), 32767);
	ASSERT_EQ(__SSAT(-40000, 16), -32768);
	ASSERT_EQ(__SSAT(-32768, 16), -32768);
	ASSERT_EQ(__SSAT(100, 16), 100);
	ASSERT_EQ(__SSAT(128, 8), 127);
	ASSERT_EQ(__SSAT(-129, 8), -128);
	ASSERT_EQ(__SSAT(INT32_MAX, 32), INT32_MAX);
	ASSERT_EQ(__SSAT(INT32_MIN, 32), INT32_MIN);
}

TEST(M1_qadd_qsub)
{
	ASSERT_EQ(__QADD(INT32_MAX, 1), INT32_MAX);
	ASSERT_EQ(__QADD(INT32_MIN, -1), INT32_MIN);
	ASSERT_EQ(__QADD(5, -7), -2);
	ASSERT_EQ(__QSUB(INT32_MIN, 1), INT32_MIN);
	ASSERT_EQ(__QSUB(INT32_MAX, -1), INT32_MAX);
	ASSERT_EQ(__QSUB(5, 7), -2);
}

TEST(M1_qadd16_qsub16)
{
	ASSERT_EQ(__QADD16(0x7FFF0001u, 0x00010001u), 0x7FFF0002u);	/* high half saturates */
	ASSERT_EQ(__QADD16(0x00007FFFu, 0x00000001u), 0x00007FFFu);	/* low half saturates */
	ASSERT_EQ(__QADD16(0x80000000u, 0xFFFF0000u), 0x80000000u);	/* -32768 + -1 */
	ASSERT_EQ(__QADD16(0x0001FFFFu, 0x00010001u), 0x00020000u);	/* -1 + 1 = 0, no carry */
	ASSERT_EQ(__QSUB16(0x80000000u, 0x00010000u), 0x80000000u);	/* -32768 - 1 */
	ASSERT_EQ(__QSUB16(0x00008000u, 0x00000001u), 0x00008000u);
	ASSERT_EQ(__QSUB16(0x7FFF0000u, 0xFFFF0000u), 0x7FFF0000u);	/* 32767 - -1 */
	ASSERT_EQ(__QSUB16(0x00050003u, 0x00020001u), 0x00030002u);
}

TEST(M1_pkhbt)
{
	ASSERT_EQ(__PKHBT(0x12345678u, 0x9ABCDEF0u, 16), 0xDEF05678u);
	ASSERT_EQ(__PKHBT(0x0000FFFFu, 0x00000001u, 16), 0x0001FFFFu);
	ASSERT_EQ(__PKHBT(0xFFFF1234u, 0x00000000u, 16), 0x00001234u);
}

TEST(M1_clz)
{
	ASSERT_EQ(__CLZ(0u), 32);	/* the M7's CLZ; __builtin_clz(0) is undefined */
	ASSERT_EQ(__CLZ(1u), 31);
	ASSERT_EQ(__CLZ(0x00010000u), 15);
	ASSERT_EQ(__CLZ(0x80000000u), 0);
	ASSERT_EQ(__CLZ(0xFFFFFFFFu), 0);
}

TEST(M1_smlald)
{
	ASSERT_EQ(__SMLALD(0x00020003u, 0x00040005u, 10u), 10 + 3 * 5 + 2 * 4);
	ASSERT_EQ(__SMLALD(0xFFFF0001u, 0x00020003u, 0u), 1 * 3 + -1 * 2);
	ASSERT_EQ((int64_t)__SMLALD(0x80008000u, 0x80008000u, 0u), 2LL * 32768 * 32768);
	ASSERT_EQ((int64_t)__SMLALD(0x00000001u, 0x0000FFFFu, 0u), -1);
}

TEST(M1_apsr)
{
	ASSERT_EQ(__get_APSR(), 0);
}

static uint32_t fcvtzu(float x)
{
	uint32_t r;
	__asm__("fcvtzu %w0, %s1" : "=r"(r) : "w"(x));
	return r;
}

TEST(M1_f2u32)
{
	const float v[] = { 0.0f, -0.0f, 0.5f, 1.0f, 1.99f, -1.0f, -1e30f, 4294967040.0f, 4294967296.0f,
		5e9f, 1e30f, INFINITY, -INFINITY, NAN, 3.0e9f, 2147483648.0f };
	for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
		ASSERT_EQ(f2u32_sat(v[i]), fcvtzu(v[i]));
	ASSERT_EQ(f2u32_sat(NAN), 0);
	ASSERT_EQ(f2u32_sat(5e9f), 4294967295u);
}

TEST_MAIN()
