/* test_fxsync.c: the v0.4 synced delay (plan A5) and DJ filter (plan A6)
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"
#include <math.h>
#include "Soundengine/DSPAudio/FX/fxsync.h"

#define FS 44140.f
#define MAXN 32703.f	/* FX_BUFFER_SIZE - 64 */
#define MAXP 16319.f	/* (FX_BUFFER_SIZE >> 1) - 64 */

TEST(DY1_divisions_halving_and_off)
{
	/* 120 BPM: a quarter is 0.5 s = 22070 samples at 44140 Hz */
	ASSERT_EQ(fxsync_samples(FXSYNC_OFF, 120.f, FS, MAXN) == 0.f, 1);
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_16, 120.f, FS, MAXN)), 5518);
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_8T, 120.f, FS, MAXN)), 7357);
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_4, 120.f, FS, MAXN)), 22070);
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_4D, 120.f, FS, MAXN)), 16553);	/* 33105 halved */
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_4D, 122.f, FS, MAXN)), 32562);	/* fits */
	ASSERT_EQ((int)lroundf(fxsync_samples(FXSYNC_4, 120.f, FS, MAXP)), 11035);	/* ping-pong */
	ASSERT_EQ(fxsync_samples(FXSYNC_4, 0.f, FS, MAXN) == 0.f, 1);
	ASSERT_EQ(fxsync_samples(FXSYNC_COUNT, 120.f, FS, MAXN) == 0.f, 1);
	ASSERT_EQ(fxsync_moved(22070.f, 22070.4f), 0);
	ASSERT_EQ(fxsync_moved(22070.f, 22100.f), 1);
}

TEST_MAIN()
