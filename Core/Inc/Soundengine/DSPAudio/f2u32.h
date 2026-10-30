/*
 * f2u32.h
 *
 * f2u32_sat(): a float-to-uint32 conversion that saturates as the Cortex-M7's
 * VCVT.U32.F32 does. It truncates towards zero; NaN and everything below 1
 * give 0, everything from 2^32 up gives UINT32_MAX. arm64's FCVTZU gives the
 * same. On the device (32-bit Arm) it is the plain cast, so the firmware's code
 * does not change. In C an out-of-range conversion is undefined: x86-64 wraps,
 * a compiler may fold it to anything, and the sanitizers stop on it. So a host
 * build (tests/, the emulator) gets the saturation in C (stage 1b F6, emulator
 * spec 7 and 8.2). Under -ffinite-math-only the NaN case is not kept.
 *
 * f2i32_sat(): a float-to-int32 conversion that saturates as the M7's
 * VCVT.S32.F32 does (LICHOMAT F15): towards zero, NaN 0, INT32_MIN and
 * INT32_MAX at the ends; the filters' outputs before their __SSAT().
 *
 * f2i16_wrap(): a float stored into an int16_t as the device does it (LICHOMAT
 * F15): VCVT.S32.F32 truncates towards zero and saturates at the int32 range
 * (NaN gives 0), then STRH keeps the low 16 bits, so a sample beyond +-32767
 * wraps round. On the device it is the plain conversion; a host build gets the
 * same result in C, where the out-of-range conversion is undefined.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef F2U32_H_
#define F2U32_H_

#include <stdint.h>

static inline __attribute__((always_inline)) uint32_t f2u32_sat(float x)
{
#if defined(__arm__)
	return (uint32_t)x;
#else
	if (!(x >= 1.f))		/* NaN, and everything below 1 */
		return 0;
	if (x >= 4294967296.f)	/* 2^32 */
		return UINT32_MAX;
	return (uint32_t)x;
#endif
}

static inline __attribute__((always_inline)) int32_t f2i32_sat(float x)
{
#if defined(__arm__)
	return (int32_t)x;
#else
	if (!(x == x))				/* NaN */
		return 0;
	if (x >= 2147483648.f)			/* 2^31 */
		return INT32_MAX;
	if (x < -2147483648.f)
		return INT32_MIN;
	return (int32_t)x;
#endif
}

static inline __attribute__((always_inline)) int16_t f2i16_wrap(float x)
{
#if defined(__arm__)
	return (int16_t)x;
#else
	return (int16_t)(uint16_t)(uint32_t)f2i32_sat(x);	/* the low 16 bits */
#endif
}

#endif /* F2U32_H_ */
