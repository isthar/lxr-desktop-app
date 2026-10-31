/*
 * app_fw.c
 *
 * The app's only file with firmware headers (plan E12, spec 3.3): the sync mode
 * and the firmware's counters, read through emu_directCall().
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_fw.h"
#include "app_engine.h"
#include "main.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/seq_core.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include "Soundengine/USB/usb_manager.h"

void app_fwExtSync(void *out)
{
	*(uint8_t *)out = seq_getExtSync();
}

void app_fwCounters(void *out)
{
	app_fwCounters_t *c = out;
	seqc_stats_t st;
	seqc_stats(&st);
	c->queueOverflow = st.queueOverflow;
	c->dropouts = st.dropouts;
	c->syncIgnored = st.syncIgnored;
	c->intervalsExcluded = st.intervalsExcluded;
	c->tempoRestarts = st.tempoRestarts;
	c->syncOverflows = seqp_syncOverflows();
	c->usbTxDropped = usb_midiTxDropped();
}
