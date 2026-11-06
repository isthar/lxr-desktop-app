/*
 * plug_params.h
 *
 * The plugin's automatable parameters (plan E9.4 Task 5): each one a firmware parameter that the
 * firmware takes over MIDI, as from an external controller on its global channel: a CC, an NRPN
 * (CC 99, 98 and 6) or, for the pattern, a Program Change. The engine reports the firmware's
 * values back (plug_shm.h, values), and plug_paramsSync() keeps the host's values and the
 * firmware's in step without a loop: a value the host sets goes to the firmware once; a value the
 * firmware takes by itself (a kit or a pattern loaded) goes to the host once; a new engine gets
 * the host's values. Pure functions of their arguments: the processor and the test host run the
 * same code.
 *
 * The ids are the VST3 parameter ids: a host keys a set's automation and state by them. Never
 * change one; a new parameter takes a new id, and a removed one's id is not used again.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_PARAMS_H_
#define PLUG_PARAMS_H_

#include <stddef.h>
#include <stdint.h>

#include "plug_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_PARAM_COUNT 36u
#define PLUG_VALUE_NONE 0xFFu		/**< no value: the engine cannot read it, or no channel */
#define PLUG_PAR_PATTERN 0xFFFFu	/**< the firmware's next pattern, not a parameter of Parameters.h */

/** how a parameter reaches the firmware */
enum
{
	PLUG_PK_CC = 0,			/**< CC number, value */
	PLUG_PK_NRPN = 1,		/**< CC 99 number >> 7, CC 98 number & 127, CC 6 value */
	PLUG_PK_PROGRAM = 2		/**< Program Change value */
};

typedef struct plug_param
{
	uint32_t id;			/**< the VST3 parameter id */
	const char *name;		/**< "Drum 1 Volume" */
	const char *shortName;	/**< "D1 Vol" */
	uint8_t kind;			/**< PLUG_PK_* */
	uint16_t number;		/**< the CC or the NRPN; 0 for the Program Change */
	uint16_t par;			/**< the firmware's parameter: CC - 1, NRPN + 128 (MidiParser.c), or PLUG_PAR_PATTERN */
	uint8_t max;			/**< the values are 0..max */
	int8_t offset;			/**< shown as value + offset: the device's own numbers */
	uint8_t def;			/**< the default: the init kit's value */
	const char *const *names;	/**< the values' names (max + 1 of them), or NULL: shown as numbers */
} plug_param_t;

extern const plug_param_t plug_params[PLUG_PARAM_COUNT];

/** the index of the parameter with VST3 id id, or -1 */
int plug_paramIndex(uint32_t id);

/** a host's normalized value (0-1) of parameter i as its value 0..max, the nearest */
uint8_t plug_paramValue(unsigned i, double normalized);

/** The MIDI messages that set parameter i to value v on channel ch (0-15) at offset, into out:
 * returns the count, 1 or 3 */
unsigned plug_paramMidi(unsigned i, uint8_t v, uint8_t ch, uint32_t offset, plug_midi_t *out);

/** one parameter's step with the firmware */
typedef struct plug_paramSync
{
	int want;				/**< the value the host has, or is to get (shown 0); -1: none yet */
	int shown;				/**< 1: the host has want */
	int sent;				/**< the value last given to the engine; -1: none */
	uint64_t sentFrame;		/**< the host frame it was given at */
	int first;				/**< 1: the running engine has not reported this parameter yet (its first report is to come) */
	int pending;			/**< 1: the host's value is to go to the engine at the next block's first frame
							 * (plug_paramsPending()): a host point taken with no channel to send it on (no running
							 * engine, or a flush) or no room for its MIDI, or a new engine's first report that found it
							 * lacking and could not send it (no channel, no room). 0 once it is sent, or when the engine
							 * has it (a host point equal to sent, a report equal to want), when the firmware changed it
							 * by itself, and when a state is restored */
} plug_paramSync_t;

/** every parameter's step, and the engine they follow */
typedef struct plug_params
{
	plug_paramSync_t p[PLUG_PARAM_COUNT];
	uint64_t epoch;			/**< the running engine's first frame */
	int synced;				/**< 1 once a report of that engine was taken */
	uint64_t settle;		/**< host frames a value given to the engine may take to show in its report */
} plug_params_t;

/** nothing known; settle in host frames (the processor: a quarter of a second) */
void plug_paramsInit(plug_params_t *ps, uint64_t settle);

/** A new engine runs from host frame epoch: its first report decides, for every parameter, whether
 * the host's value goes to it (the host has one) or its value goes to the host. */
void plug_paramsEngine(plug_params_t *ps, uint64_t epoch);

/** values restored from a state (or -1, none): the host is to get them and a new engine too */
void plug_paramsRestore(plug_params_t *ps, const int *values);

/** one point of a host's parameter change */
typedef struct plug_paramPoint
{
	unsigned index;			/**< plug_params[] */
	uint32_t offset;		/**< the frame in the block */
	uint8_t value;			/**< plug_paramValue() */
} plug_paramPoint_t;

/** The host's changes of a block, in the order its queues give them (each queue's points in
 * order), at host frame frame0: each value that differs from the one last given to the engine
 * goes to it, as MIDI on channel ch, inserted into out[0..n) in frame order (after the messages of
 * the same offset). Returns the new count, at most max (a parameter's messages all or none); with
 * ch PLUG_VALUE_NONE (no channel) none is sent. */
unsigned plug_paramsHost(plug_params_t *ps, const plug_paramPoint_t *pts, unsigned npts, uint64_t frame0, uint8_t ch,
	plug_midi_t *out, unsigned n, unsigned max);

/** The pending values (plug_paramSync_t's pending: host points taken with no channel, as plug_paramsHost() with ch
 * PLUG_VALUE_NONE when no engine runs or in a flush between blocks, or with no room, and values a first report could
 * not send) go to the engine in the next block, at its first frame: after that block's transport messages and before
 * its own points and notes (B-12's order). Writes the MIDI of every pending parameter as
 * plug_paramsHost() would, at offset 0, in the table's order, into out[0..max) (an NRPN's three messages all or none;
 * the rest stay pending), and returns the count. Each one sent counts as given to the engine at host frame frame
 * (sent, sentFrame), so the first report of a new engine does not give it a second time. With ch PLUG_VALUE_NONE
 * nothing is sent. */
unsigned plug_paramsPending(plug_params_t *ps, uint8_t ch, uint64_t frame, plug_midi_t *out, unsigned max);

/** The engine's report, the firmware's values at host frame frame (values[i], PLUG_VALUE_NONE:
 * unknown, a value beyond the range: its max), read at the block of host frame frame0. A report
 * from before the epoch is an earlier engine's and changes nothing. The first report of an
 * engine gives it the host's values that differ (as MIDI at offset 0, inserted into out[0..n) as
 * plug_paramsHost() does); any later difference that is not a value given to the engine less
 * than settle frames before is the firmware's own, and the host is to get it. Returns the new
 * count. */
unsigned plug_paramsReport(plug_params_t *ps, const uint8_t *values, uint64_t frame, uint64_t frame0, uint8_t ch,
	plug_midi_t *out, unsigned n, unsigned max);

/** the next parameter whose value the host is to get (not shown), from index *from on: its index
 * and value, *from past it; -1 when none is left. The caller sets shown once the host has it. */
int plug_paramsToShow(const plug_params_t *ps, unsigned *from, uint8_t *value);

/* --- the values in the plugin's state, after the card archive -------------------------------- */

#define PLUG_STATE_VALUES_MAGIC "LCHV"
#define PLUG_STATE_VALUES_VERSION 1u

/** writes n bytes; returns 0, or -1 (plug_archive.h's plug_put_t) */
typedef int (*plug_paramsPut_t)(void *ctx, const void *p, size_t n);
/** reads exactly n bytes; returns 0, or -1 (plug_archive.h's plug_get_t) */
typedef int (*plug_paramsGet_t)(void *ctx, void *p, size_t n);

/** values[PLUG_PARAM_COUNT] (-1: none, left out) as "LCHV", version 1 (u32), the count (u32), then
 * the id (u32) and the value (u8) of each, little-endian. Returns 0, or -1 when put fails. */
int plug_paramsWrite(plug_paramsPut_t put, void *ctx, const int *values);

/** Read them into values (-1 for each one not there). Returns 0, also for a stream that ends
 * before the magic (a state of the prototype) or holds something else after the archive (values
 * none); -1 for an LCHV block that is damaged or of another version. Ids this version does not
 * know are left out. */
int plug_paramsRead(plug_paramsGet_t get, void *ctx, int *values);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_PARAMS_H_ */
