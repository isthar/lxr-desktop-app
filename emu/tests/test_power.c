/*
 * test_power.c
 *
 * M6 (stage 1b spec 3.3, 4.10): emu_powerOn() runs once per process, because
 * the firmware's static state survives only a new process. A second call is a
 * fault and changes nothing, also when the fault handler returns.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "stm32f7xx_hal.h"
#include "emu_test.h"

TEST(M6_power_on_once)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.fault = emu_testOnFault;
	emu_powerOn(&cfg);
	ASSERT_EQ(emu_testFaults, 0);
	ASSERT_EQ(HAL_RNG_GetRandomNumber(NULL), 0x91D9AC0Cu);	/* seed 1's first DSP value (M13) */
	emu_fw.pass = NULL;			/* a firmware entry that a power-on installs */
	cfg.seed = 2u;
	emu_powerOn(&cfg);
	ASSERT_EQ(emu_testFaults, 1);
	ASSERT_TRUE(strstr(emu_testFault, "emu_powerOn: once per process") != NULL);
	ASSERT_EQ(emu_cfg.seed, 1);		/* the first power-on's configuration stays */
	ASSERT_EQ(HAL_RNG_GetRandomNumber(NULL), 0x2F57BA20u);	/* the stream goes on: seed 1's second value */
	ASSERT_TRUE(emu_fw.pass == NULL);
}

TEST_MAIN()
