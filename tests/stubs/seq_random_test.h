/*
 * seq_random_test.h
 *
 * Forced into sequencer.c for test_sequencer (-include, tests/CMakeLists.txt):
 * SEQ_RANDOM(), the source of the probability draw (stage 1b F5), becomes the
 * test's seqTest_random() instead of the hardware RNG.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef SEQ_RANDOM_TEST_H_
#define SEQ_RANDOM_TEST_H_

#include <stdint.h>

uint32_t seqTest_random(void);
#define SEQ_RANDOM() seqTest_random()

#endif /* SEQ_RANDOM_TEST_H_ */
