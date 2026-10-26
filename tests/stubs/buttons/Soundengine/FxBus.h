/*
 * FxBus.h
 *
 * Host stand-in for Core/Inc/Soundengine/FxBus.h, for test_buttons only (its
 * directory, tests/stubs/buttons, is on no other test's include path): it keeps
 * the effect headers out of that test. buttonHandler.c only reads the selected
 * effect.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef HOST_FXBUS_H_
#define HOST_FXBUS_H_

#include <stdint.h>

typedef struct { uint8_t fxType; } FxBus;
extern FxBus fxBus;

#endif /* HOST_FXBUS_H_ */
