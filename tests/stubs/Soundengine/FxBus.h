/*
 * FxBus.h
 *
 * Host stand-in for Core/Inc/Soundengine/FxBus.h, whose effect headers use the
 * ARM-only __fp16 type. buttonHandler.c only reads the selected effect.
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
