/*
 * libm_canary.c
 *
 * M19's canary (stage 1b spec 8.1): compiled with the firmware's flags, it must
 * call the vendored expf(), sinf() and cosf(). At -O1 and -O2 Apple clang would
 * fold expf() of a constant with the build machine's libm and turn sinf(x) and
 * cosf(x) into one call of __sincosf_stret, without -fno-builtin-*.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>

float libm_canary(float x)
{
	return expf(1.5f) + sinf(x) * cosf(x);
}
