/*
 * render_opts.h
 *
 * The command line of lichomat-render (stage 1b spec 5.1): one field per
 * option, the checks of each value and the defaults. A later occurrence of an
 * option replaces an earlier one, except --param, which accumulates in order;
 * --pattern and --song replace each other, and so do --seconds and --bars.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef RENDER_OPTS_H_
#define RENDER_OPTS_H_

#include <stddef.h>
#include <stdint.h>

#ifndef EMU_RENDER_TEST_HOOKS
#define EMU_RENDER_TEST_HOOKS 0
#endif

#define RENDER_MAX_PARAMS 64

/** the stereo modes of --stereo */
enum
{
	RENDER_STEREO_NONE,		/**< 4 channels: L1, R1, L2, R2 */
	RENDER_STEREO_OUT1,
	RENDER_STEREO_OUT2,
	RENDER_STEREO_SUM
};

/** the test-only faults of --test-fault (spec 5.3, test builds) */
enum
{
	RENDER_FAULT_NONE,
	RENDER_FAULT_SEGV,
	RENDER_FAULT_ABORT,
	RENDER_FAULT_MODEL,
	RENDER_FAULT_UBSAN
};

typedef struct render_opts
{
	const char *card;			/**< --card DIR */
	const char *out;			/**< --out FILE */
	const char *seedCard;		/**< --seed-card DIR */
	int project;				/**< 0-63, -1: PROJ.IDX's byte, else 0 */
	int pattern;				/**< 0-63, -1: the pattern active after boot */
	int song;					/**< 0-63, -1: none */
	int bpm;					/**< 2-255; 0 ext, 1 mid; -1 the card's */
	const char *params[RENDER_MAX_PARAMS];	/**< NAME=VALUE, in command-line order */
	unsigned nparams;
	uint64_t secondsNum;		/**< the length is secondsNum / 10^secondsDigits s ... */
	unsigned secondsDigits;
	unsigned bars;				/**< ... unless bars is 1-9999 */
	const char *events;			/**< --events FILE */
	int noStart;
	double faders[6];
	uint8_t jacks;				/**< EMU_JACK_* bits */
	uint64_t seed;
	int asBooted;
	unsigned wavRate;			/**< 44141 or 44100 */
	int stereo;					/**< RENDER_STEREO_* */
	const char *midiLog, *clockLog, *panelLog, *summary, *keepCard;
	int version, help;
	/* test builds only (EMU_RENDER_TEST_HOOKS) */
	int testFault;				/**< RENDER_FAULT_* */
	int testHang, testSlice, testPreludeReload;
} render_opts_t;

/** Parse argv into o, with the defaults of spec 5.1 for what is not given.
 * Returns 0, or -1 with the message of an exit code 1 in err (without the
 * "lichomat-render: " prefix): "--OPTION: problem". With --help or --version
 * anywhere, it sets that flag and returns 0 whatever else is given. */
int render_optsParse(int argc, char **argv, render_opts_t *o, char *err, size_t n);

/** The length in whole blocks (stage 1b plan P3): --seconds T, or for --bars N
 * the seconds N x 240 / bpm, as frames at 44,140.625 Hz rounded up, then blocks
 * of 64 rounded up. bpm is used only with --bars. */
uint64_t render_optsBlocks(const render_opts_t *o, unsigned bpm);

/** The RIFF limit: the WAV's RIFF chunk size, 36 + the data, is 32 bits, so the
 * data of the length (render_optsBlocks()) in the channels of --stereo must be at
 * most 4,294,967,259 bytes. Returns 0, or -1 with "--seconds: ..." or "--bars:
 * ..." in err. render_optsParse() checks it when it knows the length: always for
 * --seconds, for --bars with a numeric --bpm. */
int render_optsLengthCheck(const render_opts_t *o, unsigned bpm, char *err, size_t n);

/** the text of --help */
void render_optsHelp(void);

#endif /* RENDER_OPTS_H_ */
