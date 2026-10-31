/*
 * app_engine.c
 *
 * The firmware thread of LICHOMAT Emu (stage 1b spec 6.1-6.5, plan R4): the audio
 * callback's render, L and the time map, the entry rule of an input event, the
 * slices, the tier hook, the sinks and the commands.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_engine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_fw.h"

/* D in cycles: a number from 0 to 1 s; NaN is 0 and a value out of range is cut. A negative D would wrap to 2^64 cycles
 * and reach a float to integer conversion that has no result */
static uint64_t app_dCycles(double sec)
{
	sec = isnan(sec) || sec < 0.0 ? 0.0 : sec > APP_D_MAX_SEC ? APP_D_MAX_SEC : sec;
	return (uint64_t)llround(sec * EMU_CPU_HZ);
}

static uint64_t app_ticks(const app_engine_t *e, double sec)
{
	return (uint64_t)(sec * e->host.ticksPerSec);
}

/* a max loop: the callback and the open both raise maxRequest, and neither may lower what the other stored */
static void app_maxRequest(app_audio_t *a, uint32_t v)
{
	uint32_t cur = atomic_load_explicit(&a->maxRequest, memory_order_relaxed);
	while (v > cur && !atomic_compare_exchange_weak_explicit(&a->maxRequest, &cur, v, memory_order_relaxed, memory_order_relaxed))
		;
}

void app_audioRender(app_audio_t *a, const app_audioTime_t *ts, unsigned frames, float *out, unsigned channels)
{
	const uint64_t first = app_ringPopped(&a->frames);
	const uint32_t avail = app_ringCount(&a->frames);
	const unsigned n = frames < avail ? frames : avail;
	const int mix = atomic_load_explicit(&a->mix, memory_order_relaxed);
	const int mute = atomic_load_explicit(&a->mute, memory_order_relaxed);
	const unsigned used = mix == APP_MIX_FOUR && channels >= 4u ? 4u : 2u;
	for (unsigned i = 0; i < n; i++)
	{
		const int16_t *f = app_ringPeek(&a->frames, i);
		int32_t v[4] = { f[0], f[1], f[2], f[3] };
		if (mix == APP_MIX_SUM || (mix == APP_MIX_FOUR && used == 2u))
		{
			v[0] = (v[0] + v[2]) >> 1;	/* (out 1 + out 2) / 2 in int32, as --stereo sum */
			v[1] = (v[1] + v[3]) >> 1;
		}
		else if (mix == APP_MIX_OUT2)
		{
			v[0] = v[2];
			v[1] = v[3];
		}
		float *o = out + (size_t)i * channels;
		for (unsigned c = 0; c < channels; c++)
			o[c] = mute || c >= used ? 0.0f : (float)v[c] / 32768.0f;
	}
	for (size_t i = (size_t)n * channels; i < (size_t)frames * channels; i++)
		out[i] = 0.0f;
	app_ringPop(&a->frames, n);
	if (n < frames && first + avail > 0u)
		atomic_fetch_add_explicit(&a->underruns, 1u, memory_order_relaxed);
	const app_point_t p = { first, ts->hostTime, ts->entry, ts->rateScalar, frames, n };
	(void)app_ringPush(&a->points, &p);
	app_maxRequest(a, frames);
	atomic_fetch_add_explicit(&a->callbacks, 1u, memory_order_relaxed);
	if (a->wake)
		a->wake(a->wakeCtx);
}

static void app_engineSetL(app_engine_t *e)
{
	const app_latencyIn_t in = { e->lLead, e->out.deviceRate, e->out.deviceLatency, e->out.streamLatency, e->lReq, 0 };
	e->L = app_latencyL(&in);
	if (e->dAuto)
		e->dCycles = (uint64_t)llround((e->L + APP_D_MARGIN) * EMU_CPU_HZ);
}

/* the callback's points: warm-up and L (E7), then the time map (spec 6.3) */
static void app_enginePoints(app_engine_t *e)
{
	const app_point_t *p;
	while ((p = app_ringPeek(&e->audio.points, 0u)) != NULL)
	{
		if (!e->outputRunning)
		{
			app_ringPop(&e->audio.points, 1u);	/* a point of a stopped unit: it would put the map on a line no callback keeps */
			continue;
		}
		e->cbSeen = 1;
		if (p->entry > e->lastCallback)
			e->lastCallback = p->entry;
		const double lead = (double)(int64_t)(p->hostTime - p->entry) / e->host.ticksPerSec;
		if (e->warm < APP_WARMUP)
		{
			e->lLead = lead > e->lLead ? lead : e->lLead;
			e->lReq = p->request > e->lReq ? p->request : e->lReq;
			if (++e->warm == APP_WARMUP)
				app_engineSetL(e);
		}
		else if (p->request > e->lReq)
		{
			e->lReq = p->request;	/* a larger buffer */
			app_engineSetL(e);
		}
		if (p->got > 0u)
		{
			emu_counters_t c;
			emu_getCounters(&c);
			/* mHostTime holds the safety offset and the converter; the latencies are added once */
			const double lat = (double)(e->out.deviceLatency + e->out.streamLatency) / e->out.deviceRate
				* e->host.ticksPerSec * p->rateScalar;
			if (app_tmPoint(&e->map, app_frameCycles(c.codecStart, (double)(e->frameBase + p->frame)),
					p->hostTime + (uint64_t)llround(lat)))
				e->staleFrom = e->host.now(e->host.ctx);	/* host time now, not the presentation time (E9) */
		}
		app_ringPop(&e->audio.points, 1u);
	}
}

static void app_minMax(double *lo, double *hi, double v)
{
	*lo = v < *lo ? v : *lo;
	*hi = v > *hi ? v : *hi;
}

/* the entry time of an event on a port (spec 6.4), 0 if it is stale (E9) */
static int app_engineEntry(app_engine_t *e, unsigned port, uint64_t stamp, uint64_t now, uint64_t *t)
{
	if (e->staleFrom && stamp + app_ticks(e, APP_STALE_SEC) < e->staleFrom)
	{
		e->diag.midiStale++;
		return 0;
	}
	const uint64_t d = e->extSync ? e->dCycles : 0u;	/* D only in sync modes 1 and 2 (E12) */
	*t = app_entryTime(app_tmVirtual(&e->map, stamp), d, now, e->prev[port]);
	e->prev[port] = *t;
	const double tps = e->host.ticksPerSec;
	app_minMax(&e->diag.leadMin, &e->diag.leadMax, ((double)app_tmHost(&e->map, (double)now) - (double)stamp) / tps);
	app_minMax(&e->winInMin, &e->winInMax, ((double)app_tmHost(&e->map, (double)*t) - (double)stamp) / tps);
	return 1;
}

static void app_engineSnap(app_engine_t *e, uint64_t now);

/* the output is not running, and neither is its callback: a fresh ring and a fresh warm-up (spec 6.2, 6.4) */
static void app_engineOpen(app_engine_t *e)
{
	emu_counters_t c;
	emu_getCounters(&c);
	app_ringReset(&e->audio.frames);
	app_ringReset(&e->audio.points);
	e->frameBase = (uint64_t)EMU_BLOCK_FRAMES * (c.blocks + 1u);	/* the next block request's first frame */
	e->warm = e->lReq = 0u;
	e->lLead = 0.0;
	e->cbSeen = 0;
	e->diag.leadMin = 1e9;
	e->diag.leadMax = -1e9;
	e->req.channels = atomic_load(&e->audio.mix) == APP_MIX_FOUR ? 4u : 2u;
	memset(&e->out, 0, sizeof e->out);
	atomic_store(&e->audio.maxRequest, 0u);	/* no callback runs between two opens */
	emu_waitBegin();	/* a slow open is a wait, not a hang (spec 4.2) */
	e->outputRunning = e->host.outputOpen(e->host.ctx, &e->audio, &e->req, &e->out) == 0;
	emu_waitEnd();
	const uint64_t opened = e->host.now(e->host.ctx);
	e->lastCallback = opened;	/* the stall test and the first callback's 3 s count from the open's return */
	if (!e->map.valid)
		app_tmWall(&e->map, 0.0, opened + e->wallLead);
	if (e->outputRunning)
		app_maxRequest(&e->audio, (uint32_t)ceil((double)e->out.bufferFrames * APP_FS / e->out.deviceRate));	/* E6 */
	else
		e->retryAt = opened + app_ticks(e, APP_RETRY_SEC);
}

/* the wall clock paces until new callback points re-anchor the map */
static void app_engineClose(app_engine_t *e)
{
	if (e->outputRunning)
	{
		emu_waitBegin();	/* stopping a unit can take a while: a wait, not a hang (spec 4.2) */
		e->host.outputClose(e->host.ctx);
		emu_waitEnd();
	}
	e->outputRunning = 0;
	app_ringReset(&e->audio.points);	/* the callback has stopped; a callback that was in flight left its point here */
	const uint64_t host = e->host.now(e->host.ctx);
	e->retryAt = host + app_ticks(e, APP_RETRY_SEC);	/* the wall clock paces virtual time between the tries */
	if (e->map.audio)
	{
		/* the audio line is ahead of the host clock by the render lead; the wall map goes on from there, or the
		 * stamps would step back by it (spec 6.3) */
		const uint64_t shown = app_tmHost(&e->map, (double)emu_now());
		const uint64_t lead = shown > host ? shown - host : 0u;
		if (lead > e->wallLead && lead <= app_ticks(e, APP_WALL_LEAD_MAX_SEC))
			e->wallLead = lead;
	}
	app_tmWall(&e->map, (double)emu_now(), host + e->wallLead);
	e->staleFrom = host;	/* E9 */
}

static void app_engineReopen(app_engine_t *e)
{
	app_engineClose(e);
	app_engineOpen(e);
}

/* spec 6.2: the firmware thread waits while the ring holds the largest request and 4 blocks */
static void app_engineWaitRing(app_engine_t *e)
{
	const uint64_t t0 = e->host.now(e->host.ctx);
	emu_waitBegin();
	while (app_ringCount(&e->audio.frames) >= atomic_load_explicit(&e->audio.maxRequest, memory_order_relaxed) + APP_RING_SLACK)
	{
		const uint64_t host = e->host.now(e->host.ctx);
		/* a stall: no callback while the ring is full. Before the first callback of an open it is a failed open */
		if (host > e->lastCallback + app_ticks(e, e->cbSeen ? APP_STALL_SEC : APP_FIRST_SEC))
		{
			app_engineClose(e);
			break;
		}
		e->host.waitAudio(e->host.ctx, host + app_ticks(e, 0.1));
		app_enginePoints(e);
	}
	emu_waitEnd();
	e->waitTicks += e->host.now(e->host.ctx) - t0;
}

/* the wall clock's pacing of virtual time now (spec 6.3): the map is anchored a lead ahead of the host clock, so the
 * firmware thread runs that far ahead of the stamps and a stall of up to the lead still delivers on time */
static void app_enginePaceWall(app_engine_t *e, uint64_t now)
{
	const uint64_t host = e->host.now(e->host.ctx);
	const uint64_t shown = app_tmHost(&e->map, (double)now);
	const uint64_t until = shown > e->wallLead ? shown - e->wallLead : 0u;
	if (host > until + app_ticks(e, 0.1))
	{
		app_tmWall(&e->map, (double)now, host + e->wallLead);	/* no catching up */
		e->staleFrom = host;
	}
	else if (until > host)
	{
		emu_waitBegin();
		e->host.sleepUntil(e->host.ctx, until);
		emu_waitEnd();
		e->waitTicks += e->host.now(e->host.ctx) - host;
	}
}

static void app_enginePanelEvent(app_engine_t *e, const app_panelEvent_t *p, uint64_t now)
{
	uint64_t t;
	switch (p->kind)
	{
	case APP_P_BUTTON:
		emu_inButton(now, p->index, p->value);
		break;
	case APP_P_ENCODER:
		emu_inEncoder(now, p->value);
		break;
	case APP_P_PUSH:
		emu_inPush(now, (unsigned)p->value);
		break;
	case APP_P_KNOB:
		emu_inKnob(now, p->index, p->value);
		break;
	case APP_P_FADER:
		emu_inFader(now, p->index, p->level);
		break;
	case APP_P_JACKS:
		emu_inJacks(now, (uint8_t)p->value);
		break;
	case APP_P_CARD:
		emu_inCard(now, p->value);
		break;
	case APP_P_CLOCK:	/* the jack input takes the entry rule on its own port (E13) */
		if (app_engineEntry(e, APP_PORT_JACK, p->host, now, &t))
			emu_inClockPulse(t);
		break;
	case APP_P_RESET:
		if (app_engineEntry(e, APP_PORT_JACK, p->host, now, &t))
			emu_inReset(t, p->value);
		break;
	default:
		break;
	}
}

static const app_midiEntry_t *app_peekUsb(void *ctx, unsigned i)
{
	return app_ringPeek(&((app_engine_t *)ctx)->midiIn[APP_PORT_USB], i);
}

static void app_engineMidiInputs(app_engine_t *e, uint64_t now)
{
	const app_midiEntry_t *m;
	uint64_t t;
	while ((m = app_ringPeek(&e->midiIn[APP_PORT_DIN], 0u)) != NULL)
	{
		uint8_t b[64];
		const unsigned n = app_umpToBytes(m->words, m->nwords, b, sizeof b);
		if (app_engineEntry(e, APP_PORT_DIN, m->stamp, now, &t) && n)
			emu_inDin(t, b, n);	/* the wire model spaces the bytes from t (spec 4.3) */
		app_ringPop(&e->midiIn[APP_PORT_DIN], 1u);
	}
	while ((m = app_ringPeek(&e->midiIn[APP_PORT_USB], 0u)) != NULL)
	{
		uint8_t data[64];
		unsigned len;
		const uint64_t stamp = m->stamp;
		const unsigned taken = app_usbTransfer(app_peekUsb, e, &e->usbPend, data, &len);
		if (app_engineEntry(e, APP_PORT_USB, stamp, now, &t) && len)
			(void)emu_inUsb(t, data, len);	/* one transfer; the core paces 16 packets per 1 ms */
		if (!taken)
			e->diag.midiUnsent++;	/* E10: it fits no transfer; counted, not dropped silently */
		app_ringPop(&e->midiIn[APP_PORT_USB], taken ? taken : 1u);
	}
}

static void app_engineInputs(app_engine_t *e, uint64_t now)
{
	const app_panelEvent_t *p;
	while ((p = app_ringPeek(&e->panel, 0u)) != NULL)
	{
		const app_panelEvent_t ev = *p;
		app_ringPop(&e->panel, 1u);
		app_enginePanelEvent(e, &ev, now);
	}
	app_engineMidiInputs(e, now);
	/* the pulse generator (spec 6.8): exact in virtual time, so a pulse is injected before its time at
	 * every hook: up to one block (the hooks' spacing, 1.45 ms) and 1 ms ahead. 12,960,000,000,000 is
	 * 216 MHz x 60 s x 1000 (BPM in thousandths) */
	while (e->genOn && e->genNext <= now + emu_framesToCycles(EMU_BLOCK_FRAMES) + EMU_CYCLES_PER_MS)
	{
		emu_inClockPulse(e->genNext);
		e->genK++;
		e->genNext = e->genT0 + (uint64_t)((unsigned __int128)e->genK * 12960000000000ull
			/ ((unsigned __int128)e->genMbpm * e->genPpq));
	}
}

static void app_hook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	app_engine_t *e = ctx;
	app_enginePoints(e);
	app_engineInputs(e, now);
	if (why == EMU_HOOK_BLOCK)
	{
		if (++e->blocks % APP_SNAP_BLOCKS == 0u)
			app_engineSnap(e, now);
	}
	else if (now - e->lastSnap >= 10u * EMU_CYCLES_PER_MS)
		app_engineSnap(e, now);
	if (why == EMU_HOOK_BLOCK && e->outputRunning)
		app_engineWaitRing(e);
	else if (!e->outputRunning || !e->map.audio)
		app_enginePaceWall(e, now);
}

static void app_sinkAudio(void *ctx, uint64_t t, const int16_t *out1, const int16_t *out2)
{
	app_engine_t *e = ctx;
	(void)t;
	if (!e->outputRunning)
		return;	/* the next open's frameBase keeps the frames counted */
	uint32_t n = 0;
	for (; n < EMU_BLOCK_FRAMES; n++)
	{
		int16_t *f = app_ringSlot(&e->audio.frames, n);
		if (!f)
			break;
		f[0] = out1[2u * n];
		f[1] = out1[2u * n + 1u];
		f[2] = out2[2u * n];
		f[3] = out2[2u * n + 1u];
	}
	if (n < EMU_BLOCK_FRAMES)
		atomic_fetch_add_explicit(&e->audio.frames.dropped, EMU_BLOCK_FRAMES - n, memory_order_relaxed);
	app_ringCommit(&e->audio.frames, n);
}

/* a message of the DIN or USB out sink goes to its port's MIDI-out ring at its presentation time (spec 6.5). A port's
 * stamps never go back: a map that moves back (a re-anchor, the end of the audio line) would send what follows before
 * what went out already */
static int app_pushOut(void *ctx, const app_midiEntry_t *m)
{
	app_engine_t *e = ctx;
	const unsigned din = m->port == APP_PORT_DIN;
	app_midiEntry_t c = *m;
	if (c.stamp < e->outPrev[din])
		c.stamp = e->outPrev[din];
	e->outPrev[din] = c.stamp;
	const int r = app_ringPush(&e->midiOut[din], &c);	/* a full ring counts the drop */
	e->host.midiOutWake(e->host.ctx);
	return r;
}

static void app_emitDin(void *ctx, uint64_t t, const uint32_t *w, unsigned n)
{
	app_engine_t *e = ctx;
	(void)app_midiSplit(app_tmHost(&e->map, (double)t), APP_PORT_DIN, w, n, app_pushOut, e);
}

static void app_sinkDin(void *ctx, uint64_t t, uint8_t byte)
{
	app_engine_t *e = ctx;
	app_dinByte(&e->din, t, byte, app_emitDin, e);	/* each complete message at the time of its first byte */
}

typedef struct app_usbWords { uint32_t w[64]; unsigned n; } app_usbWords_t;

static void app_collectUsb(void *ctx, uint64_t t, const uint32_t *w, unsigned n)
{
	app_usbWords_t *c = ctx;
	(void)t;
	for (unsigned i = 0; i < n && c->n < sizeof c->w / sizeof c->w[0]; i++)
		c->w[c->n++] = w[i];
}

static void app_sinkUsb(void *ctx, uint64_t t, const uint8_t *data, uint16_t length)
{
	app_engine_t *e = ctx;
	app_usbWords_t c = { { 0 }, 0u };
	app_usbToUmp(t, data, length, &e->usbOutSx, app_collectUsb, &c);
	if (c.n)	/* one event packet per transfer, at its presentation time */
		(void)app_midiSplit(app_tmHost(&e->map, (double)t), APP_PORT_USB, c.w, c.n, app_pushOut, e);
}

static void app_sinkClock(void *ctx, uint64_t t, int level)
{
	app_engine_t *e = ctx;
	(void)t;
	e->clockLevel = level;
	if (level)
		e->clockSeen = 1;	/* the latch of spec 6.8 */
}

static void app_engineFault(void *ctx, const char *what)
{
	app_engine_t *e = ctx;
	e->cfg.fault(e->cfg.faultCtx, what);
}

static void app_engineWarn(void *ctx, const char *what)
{
	app_engine_t *e = ctx;
	app_ack_t a;
	memset(&a, 0, sizeof a);
	a.kind = APP_MSG_WARNING;
	snprintf(a.text, sizeof a.text, "%s", what);	/* cut to 254 characters */
	e->diag.warnings++;
	(void)app_ringPush(&e->ack, &a);
}

/* between slices, at a pass boundary (E14); 1 once APP_CMD_STOP is taken and the ring is drained */
static int app_engineCommands(app_engine_t *e)
{
	int stop = 0;
	const app_cmd_t *p;
	while ((p = app_ringPeek(&e->cmd, 0u)) != NULL)
	{
		app_cmd_t c = *p;
		app_ringPop(&e->cmd, 1u);
		c.text[sizeof c.text - 1u] = 0;	/* the GUI's text may come without its NUL */
		switch (c.kind)
		{
		case APP_CMD_CARD_OUT:
			emu_inCard(emu_now(), 0);
			e->cardAck = APP_ACK_CARD_OUT;
			break;
		case APP_CMD_CARD_IN:
			emu_inCard(emu_now(), 1);
			e->cardAck = APP_ACK_CARD_IN;
			break;
		case APP_CMD_DELAY:
			if (c.a == 0 && isnan(c.x))
				break;	/* no number: the command changes nothing */
			e->dAuto = c.a != 0;
			if (!e->dAuto)
				e->dCycles = app_dCycles(c.x);
			else if (e->warm >= APP_WARMUP)
				app_engineSetL(e);
			break;
		case APP_CMD_MIX:
		{
			const int old = atomic_exchange(&e->audio.mix, c.a);
			if ((old == APP_MIX_FOUR) != (c.a == APP_MIX_FOUR))
				app_engineReopen(e);	/* only the 4-channel mode reopens the unit (E5) */
			break;
		}
		case APP_CMD_GENERATOR:
			/* 1 to the firmware's maximum BPM, or the interval is 0 cycles and the loop of pulses never ends; NaN fails both */
			e->genOn = c.a != 0 && c.x >= 1.0 && c.x <= APP_GEN_BPM_MAX && (c.b == 4 || c.b == 24);
			if (e->genOn)
			{
				e->genMbpm = (uint64_t)llround(c.x * 1000.0);
				e->genPpq = (uint64_t)c.b;
				e->genT0 = e->genNext = emu_now();
				e->genK = 0;
			}
			break;
		case APP_CMD_OUTPUT:
			snprintf(e->req.uid, sizeof e->req.uid, "%s", c.text);
			e->req.bufferFrames = (uint32_t)c.a;
			app_engineReopen(e);
			break;
		case APP_CMD_STOP:
			stop = 1;
			break;
		default:
			break;
		}
	}
	return stop;
}

int app_engineInit(app_engine_t *e, const app_host_t *host)
{
	memset(e, 0, sizeof *e);
	e->host = *host;
	app_tmInit(&e->map, host->ticksPerSec);
	e->cardAck = -1;
	e->wallLead = app_ticks(e, APP_WALL_LEAD_SEC);
	e->dAuto = 1;
	e->winInMin = 1e9;
	e->winInMax = -1e9;
	const struct { app_ring_t *r; size_t size; uint32_t n; } rings[] = {
		{ &e->audio.frames, 8u, APP_AUDIO_FRAMES },
		{ &e->audio.points, sizeof(app_point_t), 1024u },
		{ &e->panel, sizeof(app_panelEvent_t), 256u },
		{ &e->cmd, sizeof(app_cmd_t), 64u },
		{ &e->snap, sizeof(app_snapshot_t), 16u },
		{ &e->ack, sizeof(app_ack_t), 64u },
		{ &e->notes, 1u, 64u },
		{ &e->midiIn[0], sizeof(app_midiEntry_t), 2048u },
		{ &e->midiIn[1], sizeof(app_midiEntry_t), 2048u },
		{ &e->midiOut[0], sizeof(app_midiEntry_t), 4096u },
		{ &e->midiOut[1], sizeof(app_midiEntry_t), 4096u },
	};
	for (unsigned i = 0; i < sizeof rings / sizeof rings[0]; i++)
	{
		if (app_ringInit(rings[i].r, rings[i].size, rings[i].n) != 0)
		{
			for (unsigned k = 0; k < sizeof rings / sizeof rings[0]; k++)
				app_ringFree(rings[k].r);	/* a ring that was not allocated holds NULL */
			return -1;
		}
	}
	return 0;
}

void app_enginePowerOn(app_engine_t *e, const app_engineConfig_t *cfg)
{
	e->cfg = *cfg;
	e->req = cfg->output;
	e->dAuto = cfg->dAuto;
	e->dCycles = app_dCycles(cfg->dSec);
	atomic_store(&e->audio.mix, (int)cfg->mix);
	e->winHost = e->host.now(e->host.ctx);	/* the first diagnostics window starts here */
	emu_config_t c;
	emu_configDefaults(&c);
	c.cardDir = cfg->cardDir;
	c.cardIn = cfg->cardDir != NULL;
	c.seed = cfg->seed;
	c.sinks = (emu_sinks_t){ e, app_sinkAudio, app_sinkDin, app_sinkUsb, app_sinkClock };
	c.hook = app_hook;
	c.hookCtx = e;
	c.fault = cfg->fault ? app_engineFault : NULL;
	c.warn = app_engineWarn;
	c.msgCtx = e;
	emu_powerOn(&c);
	app_engineOpen(e);
}

int app_engineSlice(app_engine_t *e)
{
	if (app_engineCommands(e))
		return 1;
	int reopen = 0;
	const uint8_t *note;
	while ((note = app_ringPeek(&e->notes, 0u)) != NULL)
	{
		if (*note == APP_NOTE_OVERLOAD)
			e->diag.overloads++;
		else if (*note == APP_NOTE_DEVICE)
			reopen = 1;
		app_ringPop(&e->notes, 1u);
	}
	if (reopen)
		app_engineReopen(e);
	else if (!e->outputRunning && e->host.now(e->host.ctx) >= e->retryAt)
		app_engineOpen(e);
	emu_runUntil(emu_now() + emu_framesToCycles(EMU_BLOCK_FRAMES));	/* the first call boots */
	e->slices++;
	if (e->cardAck >= 0)
	{
		app_ack_t a;
		memset(&a, 0, sizeof a);
		a.kind = (uint8_t)e->cardAck;
		(void)app_ringPush(&e->ack, &a);	/* after the pass boundary: the card has switched */
		e->cardAck = -1;
	}
	emu_directCall(app_fwExtSync, &e->extSync);
	if (e->slices % 8u == 0u)
		emu_directCall(app_fwCounters, &e->diag.fw);
	return 0;
}

static void app_engineSnap(app_engine_t *e, uint64_t now)
{
	app_snapshot_t s;
	memset(&s, 0, sizeof s);
	emu_lcdSnapshot(&s.lcd);
	s.leds = emu_ledSnapshot();
	s.clockOut = (uint8_t)(e->clockSeen || e->clockLevel);
	e->clockSeen = 0;
	s.vt = now;
	const uint64_t host = e->host.now(e->host.ctx);
	emu_counters_t c;
	emu_getCounters(&c);
	if (host - e->winHost >= app_ticks(e, 2.0))
	{
		const double ticks = (double)(host - e->winHost);
		/* with a device the rate is the map's: frames over host time, over 64 (E32). A count of blocks over the window is
		 * a block off (690.1 where the slope says 689.7), and without audio there is no map to ask */
		e->diag.blockRate = e->map.audio && e->map.rate > 0.0
			? e->map.rate * e->host.ticksPerSec / APP_FRAME_CYCLES / EMU_BLOCK_FRAMES
			: (double)(c.blocks - e->winBlocks) / (ticks / e->host.ticksPerSec);
		e->diag.load = 1.0 - (double)(e->waitTicks - e->winWait) / ticks;
		if (e->winInMin <= e->winInMax)
		{
			e->diag.inLatMin = e->winInMin;
			e->diag.inLatMax = e->winInMax;
		}
		e->winHost = host;
		e->winBlocks = c.blocks;
		e->winWait = e->waitTicks;
		e->winInMin = 1e9;
		e->winInMax = -1e9;
	}
	e->diag.L = e->L;
	e->diag.D = (double)e->dCycles / EMU_CPU_HZ;
	e->diag.Deff = e->extSync ? e->diag.D : 0.0;
	e->diag.dAuto = e->dAuto;
	e->diag.extSync = e->extSync;
	e->diag.outputRunning = e->outputRunning;
	e->diag.output = e->out;
	e->diag.outUnderruns = atomic_load_explicit(&e->audio.underruns, memory_order_relaxed);
	e->diag.callbacks = atomic_load_explicit(&e->audio.callbacks, memory_order_relaxed);
	e->diag.mapJumps = e->map.jumps;
	e->diag.midiInDropped = app_ringDropped(&e->midiIn[0]) + app_ringDropped(&e->midiIn[1]);
	e->diag.midiOutDropped = app_ringDropped(&e->midiOut[0]) + app_ringDropped(&e->midiOut[1]);
	e->diag.panelDropped = app_ringDropped(&e->panel);
	e->diag.snapDropped = app_ringDropped(&e->snap);
	e->diag.ackDropped = app_ringDropped(&e->ack);
	e->diag.dinOrphans = e->din.orphans;
	e->diag.core = c;
	s.diag = e->diag;
	(void)app_ringPush(&e->snap, &s);	/* a full ring drops it (E15) */
	e->lastSnap = now;
}

int app_enginePanel(app_engine_t *e, const app_panelEvent_t *ev)
{
	return app_ringPush(&e->panel, ev);
}

int app_engineCommand(app_engine_t *e, const app_cmd_t *c)
{
	return app_ringPush(&e->cmd, c);
}

int app_engineNote(app_engine_t *e, unsigned kind)
{
	const uint8_t b = (uint8_t)kind;
	return app_ringPush(&e->notes, &b);
}

int app_engineMidiPush(void *ctx, const app_midiEntry_t *m)
{
	app_engine_t *e = ctx;
	return app_ringPush(&e->midiIn[m->port == APP_PORT_DIN], m);
}

int app_engineSnapshot(app_engine_t *e, app_snapshot_t *out)
{
	if (!app_ringPeek(&e->snap, 0u))
		return 0;
	while (app_ringCount(&e->snap) > 1u)
		app_ringPop(&e->snap, 1u);
	*out = *(const app_snapshot_t *)app_ringPeek(&e->snap, 0u);
	app_ringPop(&e->snap, 1u);
	return 1;
}

int app_engineAck(app_engine_t *e, app_ack_t *out)
{
	const app_ack_t *a = app_ringPeek(&e->ack, 0u);
	if (!a)
		return 0;
	*out = *a;
	app_ringPop(&e->ack, 1u);
	return 1;
}

uint64_t app_midiOutStep(app_engine_t *e, uint64_t now, const app_midiOutSink_t *sink)
{
	const app_midiEntry_t *m;
	/* DIN at once, with its stamp: CoreMIDI schedules it, and only an early message keeps that (spec 6.5, E11) */
	while ((m = app_ringPeek(&e->midiOut[APP_PORT_DIN], 0u)) != NULL)
	{
		sink->send(sink->ctx, m->port, m->stamp, m->words, m->nwords);
		app_ringPop(&e->midiOut[APP_PORT_DIN], 1u);
	}
	while ((m = app_ringPeek(&e->midiOut[APP_PORT_USB], 0u)) != NULL)
	{
		if (m->stamp > now)
			return m->stamp;
		sink->send(sink->ctx, m->port, m->stamp, m->words, m->nwords);
		app_ringPop(&e->midiOut[APP_PORT_USB], 1u);
	}
	return 0;
}
