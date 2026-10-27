/*
 * appMain.h
 *
 * The application's boot and one pass of its main loop, moved out of main()
 * (main.c), which keeps the CubeMX initialisation and calls appMain_boot()
 * once, then appMain_loopOnce() forever. The emulator of stage 1b runs the
 * same two functions without main.c (F4).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APPMAIN_H_
#define APPMAIN_H_

/** the boot: main.c's code from dio_init() to the EXTI enables; it returns
 *  with the EXTI lines enabled, before the first pass */
void appMain_boot(void);

/** one pass of the main loop: the card check, the pots, the sound engine, the
 *  buttons, the encoder, the LEDs, the screensaver, the panel and the save timer */
void appMain_loopOnce(void);

#endif /* APPMAIN_H_ */
