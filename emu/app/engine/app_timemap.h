/*
 * app_timemap.h
 *
 * The firmware thread's map between host time (mach ticks) and virtual time
 * (216 MHz cycles), and back (stage 1b spec 6.3); L, the largest render lead
 * (spec 6.4); and the entry rule of an input event (spec 6.4). Firmware thread
 * only.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APP_TIMEMAP_H_
#define APP_TIMEMAP_H_

#include <stdint.h>

#define APP_TM_HIST 64u			/**< history points, one per 1/32 of the window */
#define APP_TM_WINDOW 2.0		/**< s: the slope's window */
#define APP_TM_JUMP 216000.0	/**< a point more than 1 ms off the line re-anchors */
#define APP_FS 44140.625		/**< the device's sample rate */
#define APP_FRAME_CYCLES (552960.0 / 113.0)	/**< virtual cycles of one device frame */
#define APP_D_MARGIN 0.001		/**< D's default is L + 1 ms */

typedef struct app_timemap
{
	double ticksPerSec;
	double nominal;			/**< cycles per tick: EMU_CPU_HZ / ticksPerSec */
	double slope;			/**< cycles per tick of the line */
	double rate;			/**< cycles per tick over the newest span of at least 2 s; 0 while there is none (E32) */
	double c0;				/**< the anchor: virtual cycles at host time h0 */
	uint64_t h0;
	int valid;
	int audio;				/**< 1: anchored on callback points; 0: the wall clock */
	struct { double c; uint64_t h; } hist[APP_TM_HIST];	/**< [0] is the newest point at least 2 s old, if any */
	unsigned nhist;
	uint64_t jumps;			/**< re-anchors on a point off the line */
	uint64_t jumpHost;		/**< host time of the last re-anchor or wall anchor */
} app_timemap_t;

void app_tmInit(app_timemap_t *m, double ticksPerSec);
/** anchor on the wall clock: virtual time c at host time h, nominal slope */
void app_tmWall(app_timemap_t *m, double c, uint64_t h);
/** a callback point: virtual time c is presented at host time h. Returns 1 if
 * the point was more than 1 ms off the line and re-anchored the map. */
int app_tmPoint(app_timemap_t *m, double c, uint64_t h);
double app_tmVirtual(const app_timemap_t *m, uint64_t h);
uint64_t app_tmHost(const app_timemap_t *m, double c);
/** virtual cycles of virtual frame f: codecStart + f * 552,960 / 113 */
double app_frameCycles(uint64_t codecStart, double frame);

typedef struct app_latencyIn
{
	double hostLead;			/**< s: largest mHostTime minus callback entry */
	double deviceRate;
	uint32_t deviceLatency;		/**< frames at the device rate */
	uint32_t streamLatency;
	uint32_t maxRequest;		/**< largest callback request, in 44,140.625 Hz frames */
	int clockOutAudio;			/**< R5's clock-out-as-audio: one block more */
} app_latencyIn_t;

/** L in seconds: the presentation offset of a callback's first frame plus
 * (2 x the largest request + 5 blocks) / 44,140.625 Hz (spec 6.4) */
double app_latencyL(const app_latencyIn_t *in);

/** the entry time max(mapped + d, now, prev) of an event (spec 6.4) */
uint64_t app_entryTime(double mapped, uint64_t d, uint64_t now, uint64_t prev);

#endif /* APP_TIMEMAP_H_ */
