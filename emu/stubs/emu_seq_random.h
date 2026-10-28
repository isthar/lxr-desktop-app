/*
 * emu_seq_random.h
 *
 * Included before Core/Src/Soundengine/Sequencer/sequencer.c in the LICHOMAT
 * emulator: the probability draw takes the sequencer's random stream (stage 1b
 * spec 4.9, firmware change F5).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef EMU_SEQ_RANDOM_H_
#define EMU_SEQ_RANDOM_H_
#include <stdint.h>

uint32_t emu_seqRandom(void);
#define SEQ_RANDOM() emu_seqRandom()

#endif /* EMU_SEQ_RANDOM_H_ */
