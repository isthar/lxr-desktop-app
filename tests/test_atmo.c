/* test_atmo.c: the v0.4 reverb stage (plan A2, A7-A9)
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <string.h>
#include "seqc_test.h"
#include "Soundengine/DSPAudio/FX/Reverb.h"

/* the reverb's own buffer, which atmo.c defines in the firmware (this test does not link atmo.c) */
__fp16 REVERB_RAM[REVERB_UNITS * MAX_ALLPASS_SIZE];
#define RV_UNIT(u) (&REVERB_RAM[(u) * MAX_ALLPASS_SIZE])

static int rv_unitWritten(const __fp16 *unit)
{
	for(int i = 0; i < MAX_ALLPASS_SIZE; i++)
		if(unit[i] != 0)
			return 1;
	return 0;
}

TEST(RV1_every_unit_carries_signal)
{
	/* fw 1.7's loop 1 ran its allpass through apDelay[1], loop 2's unit (Reverb.c:375), so
	 * unit 0 (apDelay[0]) was never written although an output tap reads it. A pulse starts
	 * every block: the early reflections (unit 12) and the predelay (unit 13) are plain
	 * delays of about 2,000 samples, so a single pulse leaves them all zero again. */
	static Reverb r;
	int16_t in[2 * 64], out[2 * 64];
	memset(REVERB_RAM, 0, sizeof REVERB_RAM);
	Reverb_init(&r);
	Reverb_setFeedback(&r, 0.8f);
	Reverb_setComposition(&r, 0.5f);
	for(int b = 0; b < 700; b++)	/* about one second */
	{
		memset(in, 0, sizeof in);
		memset(out, 0, sizeof out);
		in[0] = in[1] = 16000;
		Reverb_process(&r, in, out, out + 1, 64, 2, 1.0f);
	}
	for(int u = 0; u < 14; u++)
		ASSERT_EQ(rv_unitWritten(RV_UNIT(u)), 1);
}

TEST_MAIN()
