/*
 * plug_shm.h
 *
 * The shared memory between the LICHOMAT plugin and its engine process (plan E9.2 Task 3): a
 * versioned header, a ring of MIDI events from the plugin to the engine, a ring of audio frames
 * from the engine to the plugin (the device's two stereo outputs, plan E9.4) and the engine's
 * report of the firmware's parameter values. Each ring has one producer and one consumer, in
 * two processes. The counters only grow, and each side writes only its own fields; they are
 * plain 64-bit integers read and written with the __atomic builtins, so the header is the same
 * in C11 and C++17.
 *
 * Time is counted in host frames: frame 0 is the plugin's first frame after its activation.
 * - Events: the plugin writes the events of a block, then publishes hostFrames, the end of the
 *   block: every event before that frame is in the ring. Events are written in frame order.
 * - Audio: the engine writes the frames [audioStart, audioEnd) of the host timeline; frame f
 *   lies at audio[f % PLUG_AUDIO_FRAMES], so the ring holds the frames less than PLUG_AUDIO_FRAMES
 *   before audioEnd. The engine publishes audioEnd frame by frame, before it writes the next one;
 *   the plugin loads audioEnd again after a copy and leaves out the frames that may have been
 *   written over meanwhile. A new engine moves audioStart and audioEnd to its epoch, the host
 *   frame of its first block.
 * - The memory is a POSIX shared memory object whose name is unlinked at once: the plugin keeps
 *   its descriptor and gives it to each engine it starts as fd 5, so nothing is left behind when
 *   either process dies.
 * - The engine's death is not in the memory: the plugin sees it as the end of a pipe
 *   (plugin_link.cpp), and then writes the engine's state itself.
 * - Values: after each block the engine writes the firmware's values of plug_params.h's
 *   parameters, its MIDI channel and receive filter, and the host frame they are of, under a
 *   sequence count that is odd while it writes; the plugin takes a copy whose count did not move.
 * - Version 2 (plan E9.4): four audio channels and the values. The plugin and its engine come
 *   from one bundle; an engine of another version refuses the memory (plug_shmCheck(), and
 *   plug_shmMap() for a smaller object) with exit 2, which the plugin logs.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_SHM_H_
#define PLUG_SHM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_SHM_MAGIC 0x5048434Cu	/**< "LCHP" in memory */
#define PLUG_SHM_VERSION 2u
#define PLUG_EVENT_SLOTS 4096u		/**< a power of two */
#define PLUG_AUDIO_FRAMES 32768u	/**< a power of two; 0.68 s at 48 kHz, far more than any latency */
#define PLUG_AUDIO_CHANNELS 4u		/**< out 1 L, out 1 R, out 2 L, out 2 R */
#define PLUG_VALUES 48u				/**< the value table's slots (plug_params.h uses PLUG_PARAM_COUNT) */

/** the engine's state, written by the engine, and by the plugin once the engine is gone */
enum
{
	PLUG_ENGINE_NONE = 0,		/**< no engine, or it died */
	PLUG_ENGINE_BOOTING = 1,	/**< it has mapped the memory and boots the firmware */
	PLUG_ENGINE_RUNNING = 2,	/**< it plays from epoch on */
	PLUG_ENGINE_FAILED = 3		/**< it stopped on an error it reported in its log */
};

/** one MIDI message at a host frame */
typedef struct plug_event
{
	uint64_t frame;
	uint8_t len;		/**< 1-3 */
	uint8_t b[3];
	uint32_t pad;
} plug_event_t;

typedef struct plug_shm
{
	/* set by the plugin before the engine starts */
	uint32_t magic;
	uint32_t version;
	uint32_t size;			/**< sizeof(plug_shm_t) */
	uint32_t maxBlock;		/**< the host's largest block, frames */
	double rate;			/**< host frames per second */
	uint32_t realtime;		/**< 1: the host plays in real time (the engine takes a time-constraint policy) */
	uint32_t pad0;
	/* the plugin's, from the audio thread */
	uint64_t hostFrames;	/**< every event before this host frame is in the ring */
	uint64_t evWrite;		/**< events written */
	uint64_t evDropped;		/**< events the full ring refused */
	uint64_t underruns;		/**< frames the plugin read while the engine ran, before the engine wrote them */
	uint64_t quit;			/**< 1: the plugin asks the engine to end */
	/* the engine's */
	uint64_t state;			/**< PLUG_ENGINE_* */
	uint64_t pid;
	uint64_t evRead;		/**< events read */
	uint64_t epoch;			/**< the host frame of the engine's first block */
	uint64_t audioStart;	/**< the first frame the engine wrote */
	uint64_t audioEnd;		/**< the frame after the last one it wrote */
	uint64_t frontier;		/**< the host frames whose events it has given to the firmware */
	uint64_t late;			/**< inputs that reached the firmware after their time */
	uint64_t dropped;		/**< events left out: before the epoch, or a message without a USB-MIDI code
							 * index number (F0, F4, F5, F7, or a data byte for a status) */
	uint64_t valuesSeq;		/**< odd while the engine writes the values */
	uint64_t valuesFrame;	/**< the host frame the firmware had reached */
	uint8_t values[PLUG_VALUES];	/**< plug_params[] order; 0xFF: none */
	uint8_t channel;		/**< the firmware's global MIDI channel 0-15, 0xFF: none */
	uint8_t rxFilter;		/**< the firmware's MIDI receive filter: bit 2 CC, bit 3 Program Change */
	uint8_t pad1[6];
	plug_event_t ev[PLUG_EVENT_SLOTS];
	float audio[PLUG_AUDIO_FRAMES][PLUG_AUDIO_CHANNELS];	/**< the device's int16 outputs / 32,768 */
} plug_shm_t;

/** a copy of the engine's values */
typedef struct plug_values
{
	uint64_t frame;
	uint8_t values[PLUG_VALUES];
	uint8_t channel;
	uint8_t rxFilter;
} plug_values_t;

static inline uint64_t plug_load(const uint64_t *p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static inline void plug_store(uint64_t *p, uint64_t v)
{
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

/** set the header of s, zero the rest (the plugin, before the engine starts); realtime 0 */
void plug_shmInit(plug_shm_t *s, double rate, uint32_t maxBlock);

/** 0, or -1 with the problem in err (the engine, after mapping) */
int plug_shmCheck(const plug_shm_t *s, char *err, size_t n);

/** Create a shared memory object, unlink its name, size, map and initialise it. Returns the
 * mapping and its descriptor in *fd (close-on-exec), or NULL with the problem in err. A name left
 * by a dead process of the same pid is taken over, once. */
plug_shm_t *plug_shmCreate(double rate, uint32_t maxBlock, int *fd, char *err, size_t n);

/** map the object of descriptor fd (the engine: fd 5); NULL with the problem in err, also for an
 * object smaller than plug_shm_t (another layout's) */
plug_shm_t *plug_shmMap(int fd, char *err, size_t n);

/** unmap */
void plug_shmClose(plug_shm_t *s);

/* --- the plugin's side: the audio thread. No allocation, no lock, no system call. --- */

/** Write one message at a frame (frames in order). Returns 0, or -1 when the ring is full: the
 * message is counted in evDropped and left out. */
int plug_evPush(plug_shm_t *s, uint64_t frame, const uint8_t *b, uint8_t len);

/** every event before hostFrames is written */
void plug_publish(plug_shm_t *s, uint64_t hostFrames);

/** the stereo sum of the two outputs at -6 dB, as the prototype's one output: (a + b) / 2, exact
 * for the device's samples (a and b are multiples of 2^-15) */
static inline float plug_sum(float a, float b)
{
	return (a + b) * 0.5f;
}

/** Copy the frames [from, from + frames): with sum 0, out 1 L and R into ch[0] and ch[1] and out 2
 * L and R into ch[2] and ch[3]; with sum 1, plug_sum() of the outputs' L into ch[0] and of their R
 * into ch[1] (ch[2] and ch[3] unused). A frame the engine has not written, or has written over
 * (more than PLUG_AUDIO_FRAMES - 1 before audioEnd, also while it was copied), is 0; one that it
 * should have written by now (from its epoch on, while it runs) counts in underruns. Returns the
 * frames copied from the ring. */
uint32_t plug_audioRead(plug_shm_t *s, uint64_t from, uint32_t frames, float *const ch[PLUG_AUDIO_CHANNELS], int sum);

/** a copy of the engine's last values: 1, or 0 when it has written none or was writing them */
int plug_valuesRead(plug_shm_t *s, plug_values_t *v);

/* --- the engine's side --- */

/** the next event, or 0 when the ring holds none */
int plug_evPop(plug_shm_t *s, plug_event_t *e);

/** the frame of the next event without taking it, or UINT64_MAX when none */
uint64_t plug_evNextFrame(plug_shm_t *s);

/** leave out every event in the ring (a new engine) */
void plug_evSkipAll(plug_shm_t *s);

/** an engine's first frame: the audio ring starts at epoch, empty */
void plug_audioBegin(plug_shm_t *s, uint64_t epoch);

/** append frames, PLUG_AUDIO_CHANNELS floats each (L1 R1 L2 R2), at audioEnd, publishing audioEnd
 * after each */
void plug_audioWrite(plug_shm_t *s, const float *x, uint32_t frames);

/** the firmware's values at host frame frame: n of them (at most PLUG_VALUES; the rest 0xFF), its
 * global channel and receive filter */
void plug_valuesWrite(plug_shm_t *s, uint64_t frame, const uint8_t *values, unsigned n, uint8_t channel,
	uint8_t rxFilter);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_SHM_H_ */
