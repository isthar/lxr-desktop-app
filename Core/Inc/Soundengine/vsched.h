/*
 * vsched.h
 *
 * LICHOMAT v0.5: the voice scheduler of sample-accurate triggering (tracker E6.10, spec
 * docs/superpowers/specs/2026-10-31-sample-accurate-triggering.md, plan v0.5). A voice's
 * trigger is a record with the event's time. The render takes the records of the block
 * period before its own, the window [W0, W1) between the last two DMA completions (S4,
 * S5), and starts each voice at its offset in the block on a 2-sample grid (S3). Plain
 * C11, built on the host (tests/test_vsched.c).
 * Contexts (spec 5.4, S17): vsched_post(), the records, phases and counters only in the
 * seq_tick() context (the core's trigger callback, the MIDI parser, the mailbox) and the
 * mixer, which runs there too; vsched_blockDone() in the DMA completion interrupt
 * (priority 5) and the emulator's block body, publishing two slots under a serial.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef VSCHED_H_
#define VSCHED_H_

#include <stdint.h>

#define VSCHED_VOICES		6	/**< Drum 1-3, snare, cymbal, hi-hat (tracks 5 and 6 share it) */
#define VSCHED_RING		4	/**< records per voice (S16) */
#define VSCHED_BLOCK_CYCLES	313181u	/**< 64 frames: 64 x 552,960 / 113 = 313,180.9 cycles */
#ifndef VSCHED_SPLIT_CAP
#define VSCHED_SPLIT_CAP	6	/**< S20: voices whose records may split in one block (a phase does not
					 * count); 6 = no cap, 3 = the fix-up */
#endif

/* vsched_rec_t.flags */
#define VS_TIMED		0x01u	/**< time is the event's; else offset 0 of the next block (S10) */
#define VS_PLOCKS		0x02u	/**< the event's own p-locks, applied first (D5) */
#define VS_SLIDE_SHIFT		2	/**< bits 2-3: DRUM_SLIDE_OFF, _ON or _KEEP (Drum 1-3) */
#define VS_SLIDE_MASK		0x0Cu
#define VS_SRC_MASK		0x30u	/**< bits 4-5: the producer, for the trigger log */
#define VS_SRC_SEQ		0x00u
#define VS_SRC_MIDI		0x10u
#define VS_SRC_ONESHOT		0x20u

/** one trigger (spec 4.4), 12 B */
typedef struct
{
	uint32_t time;		/**< event time, DWT cycles (low 32 bits); 0 when untimed */
	uint8_t track;		/**< 0..6; tracks 5 (closed) and 6 (open) are the hi-hat voice */
	uint8_t note, vel;
	uint8_t flags;		/**< VS_* */
	uint8_t p1Nr, p1Val, p2Nr, p2Val;
} vsched_rec_t;
_Static_assert(sizeof(vsched_rec_t) == 12, "a record is 12 bytes (spec 4.4)");

typedef struct
{
	uint32_t late;		/**< records more than 2 frames before their window, at offset 0 (S11) */
	uint32_t overflow;	/**< records dropped by a full ring, the oldest (S16) */
	uint32_t splits;	/**< record splits */
	uint32_t phaseSplits;	/**< phase splits: a voice's control step at its phase, one per voice and block (S13) */
	uint32_t capFallbacks;	/**< blocks that needed more than VSCHED_SPLIT_CAP record splits (S20) */
	uint32_t offTick;	/**< posts outside seq_tick(), dropped (S17, P9) */
} vsched_stats_t;

typedef enum
{
	VS_DUE = 0,	/**< in this block, at *k */
	VS_LATE,	/**< more than L/32 before the window: offset 0, counted */
	VS_WAIT,	/**< a later block */
} vsched_fate_t;

/** spec 5.3: the fate of a record of time t in the window [w0, w0 + len), and its offset
 * k in {0, 2, ..., 62} when due; modulo 2^32 */
static inline vsched_fate_t vsched_offset(uint32_t t, uint32_t w0, uint32_t len, uint8_t *k)
{
	const int32_t d = (int32_t)(t - w0);
	*k = 0;
	if (d < -(int32_t)(len / 32u))
		return VS_LATE;
	if (d < 0)
		return VS_DUE;		/* rounded into this block by the one before: offset 0, not counted */
	if ((uint32_t)d >= len)
		return VS_WAIT;
	const uint32_t j = (32u * (uint32_t)d + len / 2u) / len;	/* below 2^25 */
	if (j >= 32u)
		return VS_WAIT;		/* the next block gives it -L/32 <= d < 0 */
	*k = (uint8_t)(2u * j);
	return VS_DUE;
}

/** S9: a MIDI arrival time from its 16-bit stamp (DWT >> 8) and the time now; at most
 * 255 cycles early; a stamp older than 77.7 ms reads as younger, never later than now */
static inline uint32_t vsched_stampTime(uint32_t now, uint16_t s16)
{
	const uint16_t age = (uint16_t)((uint16_t)(now >> 8) - s16);
	return (now & ~0xFFu) - ((uint32_t)age << 8);
}

/** the voice of a track: tracks 5 and 6 share the hi-hat */
static inline uint8_t vsched_voiceOf(uint8_t track)
{
	return track < VSCHED_VOICES ? track : (uint8_t)(VSCHED_VOICES - 1u);
}

/** S5: a block completed at DWT time t; the window estimate with the late-only filter */
void vsched_blockDone(uint32_t t);
/** queue a trigger on its voice's ring; a full ring drops the oldest (S16) */
void vsched_post(const vsched_rec_t *r);
/* The phase (S13, amended after Task 11's kick measurement: the persistent note phase). A note
 * that starts at offset k keeps its control grid: each of its control steps comes at k of a
 * block and lasts 64 samples, its two parts in two blocks, until the voice's next record. A
 * record at k' sets the phase k'; a record at offset 0, also one that the cap's fallback (S20) puts
 * there, a voice that the mixer finds silent and a trigger outside the records (trg blk) set it to
 * 0, the block grid. The cap counts the records due at an offset, not the phases: its fallback
 * leaves the phases of the voices without a record in the block */

/** the mixer, once per block in this order: drop the phase of each voice whose bit is set in
 * silent (its amp EG ended, its step's gain 0); read the window and plan every voice (its
 * records' offsets, the collapse S2, the cap S20); ... */
void vsched_beginBlock(uint8_t silent);
/** ... S12: apply every voice's records due at offset 0 (that voice's phase ends) ... */
void vsched_applyPhase0(void);
/** ... then per voice: its record split 2..62 or 0, its phase 2..62 or 0 (S13) ... */
uint8_t vsched_split(uint8_t v);
uint8_t vsched_phase(uint8_t v);
/** ... its note's control step in this block, before its split: the phase when it has one and
 * no record at k <= phase cuts the step (counted as a phase split), else 0 ... */
uint8_t vsched_phaseStep(uint8_t v);
/** ... at its split: apply its due records in order, and set its phase to the offset */
void vsched_applySplit(uint8_t v);
/** a trigger of the track at once, outside the records (trg blk, v0.4's path, in the
 * seq_tick() context): its voice goes back to the block grid; its records stay */
void vsched_triggerNow(uint8_t track);

void vsched_stats(vsched_stats_t *out);
/** the window the next vsched_beginBlock() reads: 1 and [*w0, *w0 + *len), or 0 if none */
int vsched_window(uint32_t *w0, uint32_t *len);
/** the emulator's trigger log: called for every applied record */
typedef void (*vsched_trace_t)(const vsched_rec_t *r, uint32_t serial, uint8_t k, uint8_t late);
void vsched_setTrace(vsched_trace_t fn);

/* what vsched.c calls; the files that define them include this header, so the compiler
 * checks each declaration against its definition */
uint8_t seq_isInTick(void);						/* sequencer.c */
void seq_applyPlocks(uint8_t track, uint8_t p1Nr, uint8_t p1Val, uint8_t p2Nr, uint8_t p2Val);	/* sequencer.c */
void Drum_setSlide(const uint8_t voiceNr, const uint8_t mode);		/* DrumVoice.c */
void voiceControl_trigger(uint8_t track, uint8_t note, uint8_t vel);	/* MidiVoiceControl.c */

#ifdef VSCHED_TEST
/* tests/test_vsched.c: called between the loads of the window read (step 0..2); a reset */
extern void (*vsched_testReadHook)(int step);
void vsched_testReset(void);
#endif

#endif /* VSCHED_H_ */
