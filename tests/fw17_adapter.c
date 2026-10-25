/*
 * fw17_adapter.c
 *
 * Compiles the fw 1.7 timing replica without its report and gives T20 access to
 * it, see fw17_adapter.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#define FW17_REPLICA_NO_MAIN
#include "reference/fw17_replica.c"

#include "fw17_adapter.h"

void fw17_reset(unsigned bpm, int perTrackChoke)
{
	reset((uint16_t)bpm, 0.0f, perTrackChoke);
}

void fw17_setStep(int track, int step, int flam, int shift)
{
	mainStep[track][step] = 1;
	timing[track][step].flam = (uint8_t)(flam & 0x0f);
	timing[track][step].shift = (uint8_t)(shift & 0x0f);
}

void fw17_setLength(int track, int length)
{
	trackLen[track] = (uint8_t)length;
}

int fw17_run(int steps, fw17_hit_t *out, int max)
{
	run(steps);
	int n = 0;
	for (int i = 0; i < ntrigs; i++)
	{
		if (trigs[i].seqStep >= steps)
			continue;		/* pulse 0 of step `steps` ran as well */
		if (n < max)
			out[n] = (fw17_hit_t){ trigs[i].track, trigs[i].step, trigs[i].seqStep, trigs[i].idx };
		n++;
	}
	return n;
}
