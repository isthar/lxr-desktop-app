# LICHOMAT Stage 1b, R5: the clock jacks as audio (emulator tier 2) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add §6.8's two options to `LICHOMAT Emu.app`, both off by default:
- **clock out as audio:** an output channel carries the clock-out jack alone, edge to edge at the edges' frames, with every block held back by one block;
- **clock and reset in from audio:** an input channel carries the clock and another the reset, each with a threshold; the edges enter through the time map plus D.

The owner's new check A12 can then run.

**Architecture:**
- The pure logic is a new plain C unit, `emu/app/engine/app_jackaudio.{h,c}`. It turns clock edges into frames, detects edges with hysteresis, gives an input frame's host time, and holds the input callback's body, which pushes panel-style events into a new SPSC ring.
- The engine writes the clock level into every frame of the audio ring, holds a block back in its audio sink, and drains the new ring through E13's entry rule. The output callback mixes as before and puts the clock on its own channel.
- The input is a second AUHAL unit, owned by the main thread through the shell. The GUI asks macOS for the microphone first.

**Tech Stack:** C11 (gnu11), Apple clang 17; AudioToolbox (AUHAL), CoreAudio; AVFoundation (`AVCaptureDevice`, Objective-C++ only in `emu/app/gui/`); Dear ImGui v1.92.9b; CMake 3.31, Ninja, ctest.

---

The spec is `docs/superpowers/specs/2026-10-27-emulator-design.md`; a `§` is one of its sections. The R4 plan (`docs/superpowers/plans/2026-10-31-emulator-r4-app.md`) still holds: E1–E33, its rules, and its "Changes during execution". R5 is §13's last increment of E5.5.

## Decisions taken while planning

The F-numbers below are this plan's decisions. The emulator's firmware changes ("F1-F18 and F6b", §7) are not meant anywhere in it.

| # | Decision | Why |
|---|---|---|
| F1 | The audio ring's frame becomes `app_frame_t` { L1, R1, L2, R2, clock }. The engine writes the clock-out jack's level into every frame, from the clock sink's edges. An edge at time t falls on the largest frame f with codecStart + ⌊f × 552,960 / 113⌋ ≤ t | E5 stays as it is (the callback mixes, so a change is instant), and each edge lands on its own frame |
| F2 | The hold-back is in the audio sink: request k's block goes into the ring at request k+1. An open drops a held block. The option going on holds the next block; going off pushes the held block and the new one. The frames stay contiguous, so `frameBase` and the time map do not change | The core reports each edge at `emu_now()`, and those times only grow. So by request k+1 every edge of block k is known (§6.8) |
| F3 | The clock channel is output channel 1–64. The unit opens with max(the mix's 2 or 4, that channel) channels, up to what the device has. A change reopens the unit only when that count changes | A third output of an interface can carry the clock without the 4-channel mix |
| F4 | When the clock takes channel 1 or 2 under a stereo mix, the mix plays as (left + right) >> 1 on the other channel, on any device. Under the 4-channel mix the clock replaces that output | §6.8 covers only the 2-channel device. The 4-channel mix has no "other channel" |
| F5 | The detector rises above the threshold and falls below threshold − 0.05. It starts low, and NaN changes nothing. The clock counts rises; the reset sends both edges, high = on. The pin's inversion stays in the core: `emu_inReset(t, 1)` drives PD5 low (§4.5) | A noisy or slow edge counts once. The inversion lives in one place |
| F6 | An input frame's host time is mHostTime + (offset − input device and stream latency) × ticksPerSec / rate × mRateScalar. Both channels are scanned in one loop in frame order; at the same frame the reset goes first | §6.8. Pushing one channel at a time would let the entry rule's third term reorder the edges |
| F7 | While clock-in-from-audio runs, L includes the input's lead: the largest (callback entry − mHostTime) + latency over the first 32 input callbacks. D's default follows it, and stays fixed until the input closes or reopens | §6.4's L covers MIDI only. An audio edge reaches the firmware thread up to one input buffer plus its latency late, so with D = L + 1 ms it would enter at 'now' with a jitter of one input buffer. This is a spec gap |
| F8 | Audio edges are `app_panelEvent_t`s (`APP_P_CLOCK`, `APP_P_RESET`, each with a host time) in a new ring, `jackIn.edges`, from the input callback to the firmware thread. They are drained after the panel ring and go through E13's entry rule on `APP_PORT_JACK` | One path for all jack input. The panel ring keeps its single producer |
| F9 | The input is its own AUHAL unit (input element 1 on, output element 0 off). It runs at the device's own rate, so there is no converter, and takes all of the device's inputs up to 64. The main thread owns it through the shell. Listeners set a flag that `app_shellFrame()` polls; a closed input that should run is retried every 2 s | The input device may not be the output device, and only the edges' times matter. R4's output reopen logic stays as it is |
| F10 | Permission: `AVCaptureDevice authorizationStatusForMediaType:` and `requestAccessForMediaType:AVMediaTypeAudio` in `main.mm`. The app asks when the option is turned on, and at launch if the stored option is on. If denied, the option turns off with a message. The C side never opens the input by itself | `LSMinimumSystemVersion` 13.0 rules out `AVAudioApplication` (macOS 14). Without permission CoreAudio gives silence, not an error. E2 keeps Objective-C in `gui/` |
| F11 | While the reset comes from audio, the panel's reset switch is disabled. Turning the option on sends reset off first if the switch was on. Closing the input lets a high reset go (`app_jackInStop`), then the switch is sent again as it stands. The clock button and the generator keep working | One jack, one source. Pulses from two sources just add |
| F12 | Settings keys: `clockout.audio`, `clockout.channel` (2), `clockin.audio`, `clockin.device` ("" = default input), `clockin.channel` (1), `clockin.threshold` (0.5), `reset.channel` (0 = none), `reset.threshold` (0.5). Thresholds are 0.05–0.95 | §6.9. `R4_settings_keep` used `clockout.audio` as its unknown stand-in key; it now uses `future.option` |
| F13 | `R5_dev_input` reads the input's properties, which needs no permission. It opens the unit only with `LICHOMAT_EMU_INPUT_TEST=1`, and skips when there is no input device | Starting input IO prompts the terminal for the microphone. CI has no audio device |
| F14 | Test ids are `R5_*`. A new owner check, A12, goes into §12.3; A8 stays as it is | A8 is the always-present controls; the options need cables |
| F15 | No dev-harness command for the options | The owner checks with the GUI; R5 stays small |

## Rules for every task

R4's rules still hold, with these values:
- **Where.** `/Users/tk/Code/lxr/build/wt-emulator`, branch `emulator`, head `444d845` at the start.
  - Every Bash call starts with `cd /Users/tk/Code/lxr/build/wt-emulator &&`, plus `source tools/env.sh >/dev/null &&` where cmake, ctest or ninja runs.
  - No `Core/` change.
- **New files** get R1's GPL-3.0-only header (the blocks below leave it out) and LF line endings.
- **Never inside a callback or listener:** a lock, `malloc`, a file call, `printf`, an Objective-C message.
- **Never** configure `address` locally, call `time()`, run `git add -A`, or `--amend`.
- **Commits.** Each message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; after each commit, `git push origin emulator`. Measured values go into the message, and no tolerance is widened to make a test pass.
- **A8 and A12 are the owner's checks.** Never mark them done.

**EMU-CHECK** (R4's):

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && cmake -S emu -B build/emu -G Ninja > build/emu.log 2>&1 && cmake --build build/emu >> build/emu.log 2>&1; echo "== emu build: $?"; echo "== emu warnings: $(grep -c 'warning:' build/emu.log)"; ctest --test-dir build/emu -j8 2>&1 | grep -E 'tests passed|tests failed'
```

**The gate:** EMU-CHECK shows `0`, `0` and the task's N; `tools/check.sh` shows 57 host tests, the task's N, the image `OK` and 0 warnings.

## Facts the tasks rely on (read at `444d845`)

- **Counts:** 264 ids in Debug, 265 in Release, 244 without the app (`-DLICHOMAT_EMU_APP=OFF`); 57 host tests.
- **Clock-out sink** (`emu_jacks.c:39–48`): PC13 is sampled after every body and every pass, and `sinks.clockOut` gets `emu_now()`. Those times never go back.
- **Audio sink:** block request k carries frames 64k…64k+63, and `blocks` is counted before the sink. At an open, `frameBase` = 64 × (blocks + 1) (`app_engine.c:167`) and `req.channels` comes from the mix (`:173`).
- **Ring frame:** `int16_t[4]`, 8 bytes, in the `app_engineInit` table, in `app_sinkAudio`, in `app_audioRender` (loop at `:52–68`) and in `audioInit()` (`test_app_engine.c:76–83`).
- **L** already has `clockOutAudio` (+1 block, `app_timemap.c:112`, tested by `M18_latency_table`), but the engine passes 0 (`app_engine.c:85`).
- **Output channels:** 4 only on a 4-channel device, else 2 (`app_coreaudio.c:290`); the `ChannelMap` is set only for 4. The fake reports 2 channels (`app_fake.h:128`).
- **Reset:** `emu_inReset(t, 1)` drives PD5 low (`emu_jacks.c:29–36`). In sync mode 1, RESET_OFF while stopped arms a start at the next pulse that begins a step, and RESET_ON stops and rewinds (`seq_core.h:538–541`). The init card runs at 4 PPQ (G9_ppq4).
- **Signing** is ad hoc with no hardened runtime, so no audio-input entitlement is needed, only `NSMicrophoneUsageDescription` (E25).
- **GUI:** the Audio window is `audioWindow()` (`panel.cpp:648`, 470 × 250); the reset switch is in `jackStrip()` (`panel.cpp:500–506`); `post()` is at `panel.cpp:129`.

## Task order

| Task | Contents | ids after it (Debug / Release / no app) | Serves |
|---|---|---|---|
| 1 | Pure logic `app_jackaudio`, L's input lead | 270 / 271 / 250 | A12 |
| 2 | Engine: clock in the frames, hold-back, callback channel, input edges | 274 / 275 / 254 | A12; A8 unchanged |
| 3 | Settings, CoreAudio input, shell | 276 / 277 / 255 | A12 |
| 4 | GUI, permission, plist, README, spec, A12 | 276 / 277 / 255 | A12, A8 |

`R5_dev_input` exists only in builds with the app, so the build without it ends at 255.

---

### Task 1: The pure logic (§6.8, F1, F5–F7)

**Files:** create `emu/app/engine/app_jackaudio.{h,c}` and `emu/tests/test_app_jackaudio.c`; modify `emu/app/engine/app_timemap.{h,c}`, `emu/tests/test_app_timemap.c` and `emu/CMakeLists.txt` (`EMU_APP_UNIT_SOURCES` and the test lines).

- [ ] **Step 1: The failing tests.** `emu/tests/test_app_jackaudio.c`:

```c
#include <math.h>
#include <string.h>

#include "app_jackaudio.h"
#include "seqc_test.h"

static uint64_t cyc(uint64_t f) { return (uint64_t)((unsigned __int128)f * 552960u / 113u); }	/* spec 4.2 */

TEST(R5_clk_frame)
{
	const uint64_t cs = 216000000u;
	ASSERT_EQ(app_clkFrame(cs, cs - 1u), 0);	/* before the codec start */
	ASSERT_EQ(app_clkFrame(cs, cs + 4892u), 0);	/* frame 1 starts 4,893 cycles in */
	ASSERT_EQ(app_clkFrame(cs, cs + 4893u), 1);
	for (uint64_t f = 1; f < 2000000u; f += 997u)
	{
		ASSERT_EQ(app_clkFrame(cs, cs + cyc(f)), f);
		ASSERT_EQ(app_clkFrame(cs, cs + cyc(f) - 1u), f - 1u);
	}
	ASSERT_EQ(app_clkFrame(cs, cs + cyc(1000000000000u)), 1000000000000u);	/* 262 days: no overflow */
	ASSERT_EQ(app_clkFrame(cs, cs + cyc(64u * 7u)), 448);	/* block request 7's first frame */
}

TEST(R5_clk_render)
{
	static app_clkOut_t c;
	int16_t lv[64];
	app_clkEdge(&c, 100u, 1);
	app_clkEdge(&c, 230u, 0);
	app_clkEdge(&c, 250u, 1);
	app_clkEdge(&c, 251u, 0);	/* a pulse of one frame */
	app_clkEdge(&c, 400u, 1);
	app_clkRender(&c, 0u, 64u, lv);
	for (unsigned i = 0; i < 64u; i++)
		ASSERT_EQ(lv[i], 0);
	app_clkRender(&c, 64u, 64u, lv);
	for (unsigned i = 0; i < 64u; i++)
		ASSERT_EQ(lv[i], 64u + i >= 100u);	/* high from the edge's own frame */
	app_clkRender(&c, 128u, 64u, lv);
	for (unsigned i = 0; i < 64u; i++)
		ASSERT_EQ(lv[i], 1);
	app_clkRender(&c, 192u, 64u, lv);
	for (unsigned i = 0; i < 64u; i++)
		ASSERT_EQ(lv[i], 192u + i < 230u || 192u + i == 250u);
	ASSERT_EQ(c.n, 1);	/* the edge at 400 waits for its block */
	app_clkRender(&c, 384u, 64u, lv);	/* 256-383 were never rendered (an output closed) */
	for (unsigned i = 0; i < 64u; i++)
		ASSERT_EQ(lv[i], 384u + i >= 400u);
	app_clkEdge(&c, 10u, 0);	/* late: it sets the level from the block's first frame */
	app_clkRender(&c, 448u, 64u, lv);
	ASSERT_EQ(lv[0], 0);
	for (unsigned i = 0; i < APP_CLK_EDGES + 1u; i++)
		app_clkEdge(&c, 600u + i, (int)(i & 1u));
	ASSERT_EQ(c.dropped, 1);
}

TEST(R5_edge_step)
{
	app_edgeDet_t d = { 0.5f, 0 };
	ASSERT_EQ(app_edgeStep(&d, 0.5f), 0);	/* at the threshold is not above it */
	ASSERT_EQ(app_edgeStep(&d, 0.51f), 1);
	ASSERT_EQ(app_edgeStep(&d, 0.9f), 0);
	ASSERT_EQ(app_edgeStep(&d, 0.46f), 0);	/* within the hysteresis */
	ASSERT_EQ(app_edgeStep(&d, NAN), 0);
	ASSERT_EQ(app_edgeStep(&d, 0.44f), -1);
	ASSERT_EQ(app_edgeStep(&d, 0.49f), 0);	/* low until above the threshold again */
	ASSERT_EQ(app_edgeStep(&d, -1.0f), 0);	/* an AC-coupled input's undershoot is low */
	ASSERT_EQ(app_edgeStep(&d, 1.0f), 1);
	d.threshold = 0.1f;
	ASSERT_EQ(app_edgeStep(&d, 0.06f), 0);
	ASSERT_EQ(app_edgeStep(&d, 0.04f), -1);
}

TEST(R5_in_host_time)
{
	ASSERT_EQ(app_inHostTime(1000000u, 0u, 40u, 500.0), 980000);	/* 40 frames of latency, 48 kHz on 24 MHz */
	ASSERT_EQ(app_inHostTime(1000000u, 40u, 40u, 500.0), 1000000);
	ASSERT_EQ(app_inHostTime(1000000u, 511u, 40u, 500.05), 1235524);	/* mRateScalar 1.0001 */
	ASSERT_EQ(app_inHostTime(1000u, 0u, 40u, 500.0), 0);			/* before the host clock's start */
	ASSERT_EQ(app_inHostTime(60000000000000u, 1u, 0u, 500.0), 60000000000500u);	/* 29 days up: exact */
}

TEST(R5_jackin_render)
{
	static app_jackIn_t in;
	static float x[64 * 2];
	ASSERT_EQ(app_ringInit(&in.edges, sizeof(app_panelEvent_t), 64u), 0);
	const app_jackInConfig_t cfg = { 24e6, 48000.0, 40u, 2u, 0u, 1u, 0.5f, 0.5f };
	app_jackInStart(&in, &cfg);
	for (unsigned i = 10; i < 15u; i++)
		x[2u * i] = 0.6f;	/* a clock pulse from frame 10 */
	x[2u * 15u] = 0.47f;	/* within the hysteresis */
	x[2u * 20u] = 0.52f;	/* the next pulse */
	x[2u * 25u] = NAN;
	for (unsigned i = 12; i < 40u; i++)
		x[2u * i + 1u] = 0.9f;	/* the reset high, frames 12-39 */
	const uint64_t host = 1000000u, entry = host + 280000u;	/* mHostTime (512 + 48) frames before the callback */
	app_jackInRender(&in, host, entry, 1.0, x, 64u);
	static const struct { uint8_t kind; int32_t value; unsigned frame; } want[4] = {
		{ APP_P_CLOCK, 1, 10u }, { APP_P_RESET, 1, 12u }, { APP_P_CLOCK, 1, 20u }, { APP_P_RESET, 0, 40u } };
	ASSERT_EQ(app_ringCount(&in.edges), 4);
	for (unsigned k = 0; k < 4u; k++)
	{
		const app_panelEvent_t *e = app_ringPeek(&in.edges, k);
		ASSERT_EQ(e->kind, want[k].kind);
		ASSERT_EQ(e->value, want[k].value);	/* a high jack is the reset on (spec 4.5) */
		ASSERT_EQ(e->host, host + 500u * want[k].frame - 20000u);
	}
	app_ringPop(&in.edges, 4u);
	memset(x, 0, sizeof x);
	x[0] = x[1] = 0.8f;	/* a pulse and the reset at one frame: the reset first (F6) */
	app_jackInRender(&in, host, entry, 1.0, x, 1u);
	ASSERT_EQ(((const app_panelEvent_t *)app_ringPeek(&in.edges, 0u))->kind, APP_P_RESET);
	ASSERT_EQ(((const app_panelEvent_t *)app_ringPeek(&in.edges, 1u))->kind, APP_P_CLOCK);
	app_ringPop(&in.edges, 2u);
	ASSERT_EQ(atomic_load(&in.lead), 0);	/* F7: fixed after 32 callbacks */
	for (unsigned k = 2; k < APP_IN_WARMUP; k++)
		app_jackInRender(&in, host, entry, 1.0, x, 0u);
	ASSERT_EQ(atomic_load(&in.lead), 300000);	/* 280,000 ticks + 40 frames */
	app_jackInRender(&in, host, entry + 100000u, 1.0, x, 0u);
	ASSERT_EQ(atomic_load(&in.lead), 300000);	/* a later, longer lead does not move D */
	app_jackInStop(&in, 5555u);	/* stopped with the reset high: it is let go (F11) */
	const app_panelEvent_t *e = app_ringPeek(&in.edges, 0u);
	ASSERT_TRUE(e != NULL && e->kind == APP_P_RESET && e->value == 0 && e->host == 5555u);
	ASSERT_EQ(atomic_load(&in.lead), 0);
	app_ringFree(&in.edges);
}

TEST_MAIN()
```

In `test_app_timemap.c`, after `M18_latency_table`:

```c
TEST(R5_latency_input)	/* F7: L holds the input's lead while clock and reset come in from audio */
{
	app_latencyIn_t in = { 0.01274, 48000.0, 60u, 690u, 471u, 0, 0.0 };
	const double base = app_latencyL(&in);
	in.inputLead = 0.0125;	/* (512 + 48 + 40) frames at 48 kHz */
	ASSERT_NEAR((app_latencyL(&in) - base) * 1e3, 12.5, 1e-9);
	in.clockOutAudio = 1;
	ASSERT_NEAR((app_latencyL(&in) - base) * 1e3, 12.5 + 1.44991, 1e-4);
}
```

CMake: append `"${EMU_APP_DIR}/engine/app_jackaudio.c"` to `EMU_APP_UNIT_SOURCES`, add `R5_latency_input` to `test_app_timemap`'s IDS, and add `emu_add_app_test(test_app_jackaudio IDS R5_clk_frame R5_clk_render R5_edge_step R5_in_host_time R5_jackin_render)`.

- [ ] **Step 2: Run EMU-CHECK.** Expected: a build error (no `app_jackaudio.h`).

- [ ] **Step 3: The unit.** `emu/app/engine/app_jackaudio.h`:

```c
/* app_jackaudio.h: the clock jacks as audio (stage 1b spec 6.8, plan R5). Plain C; the input callback's part never blocks */
#ifndef APP_JACKAUDIO_H_
#define APP_JACKAUDIO_H_

#include <stdatomic.h>
#include <stdint.h>

#include "app_ring.h"
#include "app_types.h"

#define APP_JACK_THRESHOLD 0.5f	/**< the default: half of full scale (spec 6.8) */
#define APP_JACK_HYST 0.05f		/**< a high input falls below the threshold minus this (F5) */
#define APP_CLK_EDGES 32u		/**< clock-out edges waiting for their block */
#define APP_IN_WARMUP 32u		/**< input callbacks before the input's lead is fixed (F7) */

typedef struct app_clkOut
{
	uint64_t frame[APP_CLK_EDGES];
	uint8_t level[APP_CLK_EDGES];
	unsigned head, n;	/**< the oldest waiting edge; how many wait */
	int level0;			/**< the level before the oldest waiting edge */
	uint64_t dropped;	/**< edges that found the queue full */
} app_clkOut_t;

/** the largest frame f with codecStart + floor(f x 552,960 / 113) <= t; 0 before codecStart (F1) */
uint64_t app_clkFrame(uint64_t codecStart, uint64_t t);
/** an edge at a frame; successive frames never go back */
void app_clkEdge(app_clkOut_t *c, uint64_t frame, int level);
/** the level (0 or 1) at frames first .. first + n - 1; uses up the edges before first + n. An edge before first sets the
 * level from the first frame; of several edges at one frame the last counts */
void app_clkRender(app_clkOut_t *c, uint64_t first, unsigned n, int16_t *level);

typedef struct app_edgeDet { float threshold; int high; } app_edgeDet_t;
/** 1 if v rose above the threshold, -1 if it fell below threshold - APP_JACK_HYST, else 0; NaN changes nothing (F5) */
int app_edgeStep(app_edgeDet_t *d, float v);
/** mHostTime + (offset - latency) frames, in host ticks; 0 if before 0 (spec 6.8, F6) */
uint64_t app_inHostTime(uint64_t hostTime, unsigned offset, uint32_t latency, double ticksPerFrame);

typedef struct app_jackInConfig
{
	double ticksPerSec, deviceRate;
	uint32_t latency;			/**< input device + stream latency, device frames */
	unsigned channels;			/**< interleaved channels of the frames */
	unsigned clockCh, resetCh;	/**< 0-based; >= channels: none */
	float clockThreshold, resetThreshold;
} app_jackInConfig_t;

typedef struct app_jackIn
{
	app_ring_t edges;	/**< app_panelEvent_t: input callback -> firmware thread (F8) */
	_Atomic float clockThreshold, resetThreshold;	/**< the main thread may move them while the input runs */
	_Atomic uint64_t callbacks, lead;	/**< lead: host ticks (F7), fixed after APP_IN_WARMUP callbacks; 0 while none */
	app_jackInConfig_t cfg;	/**< from here on the callback's, set by app_jackInStart() while none runs */
	app_edgeDet_t clock, reset;
	int64_t warmMax;
} app_jackIn_t;

void app_jackInStart(app_jackIn_t *in, const app_jackInConfig_t *cfg);	/**< before the unit starts: all low, warm-up anew */
/** Input callback: the edges of x[0 .. frames x channels) in frame order, as APP_P_CLOCK (a rise) and APP_P_RESET (value 1:
 * the jack high, the reset on) at their host times; the warm-up's lead */
void app_jackInRender(app_jackIn_t *in, uint64_t hostTime, uint64_t entry, double rateScalar, const float *x, unsigned frames);
/** after the unit has stopped for good, on that thread: a high reset is let go at host time now (F11); no lead */
void app_jackInStop(app_jackIn_t *in, uint64_t now);

#endif /* APP_JACKAUDIO_H_ */
```

`emu/app/engine/app_jackaudio.c`:

```c
#include "app_jackaudio.h"

#include <math.h>
#include <string.h>

uint64_t app_clkFrame(uint64_t codecStart, uint64_t t)
{
	if (t < codecStart)
		return 0u;
	const unsigned __int128 d = (unsigned __int128)(t - codecStart);	/* floor(f A / B) <= d <=> f A < (d + 1) B */
	return (uint64_t)(((d + 1u) * 113u - 1u) / 552960u);
}

void app_clkEdge(app_clkOut_t *c, uint64_t frame, int level)
{
	if (c->n == APP_CLK_EDGES)
	{
		c->dropped++;
		return;
	}
	const unsigned i = (c->head + c->n) % APP_CLK_EDGES;
	c->frame[i] = frame;
	c->level[i] = (uint8_t)(level != 0);
	c->n++;
}

void app_clkRender(app_clkOut_t *c, uint64_t first, unsigned n, int16_t *level)
{
	for (unsigned i = 0; i < n; i++)
	{
		while (c->n && c->frame[c->head] <= first + i)
		{
			c->level0 = c->level[c->head];
			c->head = (c->head + 1u) % APP_CLK_EDGES;
			c->n--;
		}
		level[i] = (int16_t)c->level0;
	}
}

int app_edgeStep(app_edgeDet_t *d, float v)
{
	if (!d->high && v > d->threshold)
	{
		d->high = 1;
		return 1;
	}
	if (d->high && v < d->threshold - APP_JACK_HYST)
	{
		d->high = 0;
		return -1;
	}
	return 0;	/* NaN fails both comparisons */
}

uint64_t app_inHostTime(uint64_t hostTime, unsigned offset, uint32_t latency, double ticksPerFrame)
{
	const double h = (double)hostTime + ((double)offset - (double)latency) * ticksPerFrame;
	return h <= 0.0 ? 0u : (uint64_t)llround(h);
}

void app_jackInStart(app_jackIn_t *in, const app_jackInConfig_t *cfg)
{
	in->cfg = *cfg;
	in->clock = (app_edgeDet_t){ cfg->clockThreshold, 0 };
	in->reset = (app_edgeDet_t){ cfg->resetThreshold, 0 };
	in->warmMax = 0;
	atomic_store(&in->clockThreshold, cfg->clockThreshold);
	atomic_store(&in->resetThreshold, cfg->resetThreshold);
	atomic_store(&in->callbacks, 0u);
	atomic_store(&in->lead, 0u);
}

static void app_jackPush(app_jackIn_t *in, uint8_t kind, int32_t value, uint64_t host)
{
	app_panelEvent_t ev;
	memset(&ev, 0, sizeof ev);
	ev.kind = kind;
	ev.value = value;
	ev.host = host;
	(void)app_ringPush(&in->edges, &ev);	/* a full ring counts the drop */
}

void app_jackInRender(app_jackIn_t *in, uint64_t hostTime, uint64_t entry, double rateScalar, const float *x, unsigned frames)
{
	const app_jackInConfig_t *g = &in->cfg;
	const double perFrame = g->ticksPerSec / g->deviceRate * rateScalar;	/* host ticks per input frame (spec 6.3) */
	in->clock.threshold = atomic_load_explicit(&in->clockThreshold, memory_order_relaxed);
	in->reset.threshold = atomic_load_explicit(&in->resetThreshold, memory_order_relaxed);
	const int clock = g->clockCh < g->channels, reset = g->resetCh < g->channels;
	for (unsigned i = 0; i < frames; i++)
	{
		const float *f = x + (size_t)i * g->channels;
		const int r = reset ? app_edgeStep(&in->reset, f[g->resetCh]) : 0;
		const int c = clock ? app_edgeStep(&in->clock, f[g->clockCh]) : 0;
		if (r == 0 && c <= 0)
			continue;
		const uint64_t h = app_inHostTime(hostTime, i, g->latency, perFrame);
		if (r != 0)	/* first: a pulse that comes with the release starts the step (F6) */
			app_jackPush(in, APP_P_RESET, r > 0, h);	/* high is the reset on; the core drives PD5 low (spec 4.5) */
		if (c > 0)	/* the clock counts on the rise (spec 6.8) */
			app_jackPush(in, APP_P_CLOCK, 1, h);
	}
	const uint64_t k = atomic_load_explicit(&in->callbacks, memory_order_relaxed);
	if (k < APP_IN_WARMUP)
	{
		/* the buffer's first frame: from its time at the jack to this callback (F7) */
		const int64_t lead = (int64_t)(entry - hostTime) + (int64_t)llround((double)g->latency * perFrame);
		in->warmMax = lead > in->warmMax ? lead : in->warmMax;
		if (k + 1u == APP_IN_WARMUP)
			atomic_store_explicit(&in->lead, (uint64_t)in->warmMax, memory_order_relaxed);
	}
	atomic_store_explicit(&in->callbacks, k + 1u, memory_order_relaxed);
}

void app_jackInStop(app_jackIn_t *in, uint64_t now)
{
	if (in->reset.high)
		app_jackPush(in, APP_P_RESET, 0, now);
	in->clock.high = in->reset.high = 0;
	atomic_store(&in->lead, 0u);
}
```

`app_timemap.h`: `app_latencyIn_t` gets a last field, `double inputLead; /**< s: the input's lead while clock and reset come in from audio (F7); 0 off */`. L's comment adds "one block more with clock out as audio, plus the input's lead". In `app_timemap.c`, `app_latencyL()` returns `offset + (2.0 * (double)in->maxRequest + blocks * EMU_BLOCK_FRAMES) / APP_FS + in->inputLead;`.

- [ ] **Step 4: Run** `ctest --test-dir build/emu -R '^R5_|^M18_latency' --output-on-failure`: expect 7 passed. Then EMU-CHECK: `out of 270`.

- [ ] **Step 5: Commit and push.**

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && git add emu/app/engine/app_jackaudio.h emu/app/engine/app_jackaudio.c emu/app/engine/app_timemap.h emu/app/engine/app_timemap.c emu/tests/test_app_jackaudio.c emu/tests/test_app_timemap.c emu/CMakeLists.txt && git commit -m "Emulator app: clock-jack edges to frames, the input detector, input host times, L's input lead (E5.5 R5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && git push origin emulator
```

---

### Task 2: The engine (§6.2, §6.4, §6.8, F1–F4, F7, F8)

**Files:** modify `emu/app/engine/app_types.h`, `app_engine.{h,c}`, `emu/tests/app_fake.h`, `test_app_engine.c`, `test_app_m18.c` and `emu/CMakeLists.txt` (the two IDS lists).

- [ ] **Step 1: The fake device's channels.**
  - `fake_t` gains `unsigned deviceChannels, channels;` (the device's outputs, 2 if 0; the open unit's) and `void (*rendered)(const float *buf, unsigned frames, unsigned channels);`.
  - `fakeAdvance()` keeps the request in `const unsigned req = fakeRequest(fk.callbacks++);`, renders with `fk.channels`, then runs `if (fk.rendered) fk.rendered(fk.buf, req, fk.channels);`.
  - `fakeCallback()` passes `fk.channels`.
  - In `fakeOpen()`, `info->channels = 2u;` becomes:

```c
	const unsigned dev = fk.deviceChannels ? fk.deviceChannels : 2u;
	fk.channels = req->channels < 2u ? 2u : req->channels > dev ? dev : req->channels;	/* as app_caOpen() (F3) */
	info->channels = fk.channels;
	info->deviceChannels = dev;
```

- [ ] **Step 2: The failing tests.** In `test_app_engine.c`, `audioInit()` initializes the ring with `sizeof(app_frame_t)`, and its loop becomes:

```c
	for (unsigned i = 0; i < frames; i++)
	{
		app_frame_t *s = app_ringSlot(&a->frames, i);
		memcpy(s->s, f, sizeof s->s);
		s->clock = (int16_t)(i & 1u);	/* the clock-out jack high on the odd frames (R5) */
	}
```

Before `TEST_MAIN()`:

```c
TEST(R5_render_clock)	/* spec 6.8, F3, F4 */
{
	app_audio_t a;
	const int16_t f[4] = { 1000, -2000, 3001, -3 };
	const app_audioTime_t ts = { 5000u, 4000u, 1.0 };
	float out[16];
	audioInit(&a, 16u, f);
	atomic_store(&a.clockChannel, 2);
	app_audioRender(&a, &ts, 2u, out, 2u);	/* frames 0, 1 */
	ASSERT_EQ_DBL(out[0], 499.0 / 32768.0);	/* the sum (2000, -1002) in mono: 998 >> 1 */
	ASSERT_EQ_DBL(out[1], 0.0);
	ASSERT_EQ_DBL(out[3], 1.0);
	atomic_store(&a.clockChannel, 1);
	app_audioRender(&a, &ts, 2u, out, 2u);	/* frames 2, 3 */
	ASSERT_EQ_DBL(out[1], 499.0 / 32768.0);
	ASSERT_EQ_DBL(out[2], 1.0);
	atomic_store(&a.clockChannel, 3);		/* a third channel: the stereo sum stays on 1 and 2 */
	app_audioRender(&a, &ts, 2u, out, 4u);	/* frames 4, 5 */
	ASSERT_EQ_DBL(out[0], 2000.0 / 32768.0);
	ASSERT_EQ_DBL(out[1], -1002.0 / 32768.0);
	ASSERT_EQ_DBL(out[3], 0.0);
	ASSERT_EQ_DBL(out[6], 1.0);
	atomic_store(&a.mix, APP_MIX_FOUR);
	atomic_store(&a.clockChannel, 4);		/* the 4-channel mix: the clock replaces R2 */
	app_audioRender(&a, &ts, 1u, out, 4u);	/* frame 6 */
	ASSERT_EQ_DBL(out[2], 3001.0 / 32768.0);
	ASSERT_EQ_DBL(out[3], 0.0);
	atomic_store(&a.mix, APP_MIX_SUM);
	atomic_store(&a.clockChannel, 3);		/* a channel the unit does not have: as without the option */
	app_audioRender(&a, &ts, 1u, out, 2u);	/* frame 7 */
	ASSERT_EQ_DBL(out[1], -1002.0 / 32768.0);
	atomic_store(&a.clockChannel, 2);
	atomic_store(&a.mute, 1);				/* a hang: the clock is silent too */
	app_audioRender(&a, &ts, 2u, out, 2u);	/* frames 8, 9 */
	ASSERT_EQ_DBL(out[3], 0.0);
	atomic_store(&a.mute, 0);
	app_audioRender(&a, &ts, 8u, out, 2u);	/* frames 10-15, then 2 of underrun: the clock low */
	ASSERT_EQ_DBL(out[11], 1.0);
	ASSERT_EQ_DBL(out[13], 0.0);
	ASSERT_EQ(atomic_load(&a.underruns), 1);
}

static uint64_t ringEnd(void) { return E.frameBase + app_ringPopped(&E.audio.frames) + app_ringCount(&E.audio.frames); }
static uint64_t blocksNow(void) { emu_counters_t c; emu_getCounters(&c); return c.blocks; }

TEST(R5_engine_holdback)	/* F2: block k enters at request k + 1; L and D's default grow by a block (spec 6.4) */
{
	boot(1);
	runTo(2.0);
	ASSERT_EQ(ringEnd(), 64u * (blocksNow() + 1u));
	const double L0 = E.L, D0 = (double)E.dCycles / EMU_CPU_HZ;
	app_cmd_t c = { .kind = APP_CMD_CLOCK_OUT, .a = 2 };
	command(&c);
	for (unsigned i = 0; i < 300u; i++)
	{
		ASSERT_EQ(ringEnd(), 64u * blocksNow());
		app_engineSlice(&E);
	}
	ASSERT_NEAR((E.L - L0) * 1e3, 1.44991, 1e-4);
	ASSERT_NEAR(((double)E.dCycles / EMU_CPU_HZ - D0) * 1e3, 1.44991, 1e-3);
	ASSERT_EQ(fk.opens, 1);	/* channel 2 of a stereo unit: no reopen */
	c.a = 0;
	command(&c);
	ASSERT_EQ(ringEnd(), 64u * (blocksNow() + 1u));	/* the held block and the new one, in order */
	ASSERT_NEAR(E.L, L0, 1e-12);
	ASSERT_EQ(atomic_load(&E.audio.underruns), 0);
	c.a = 3;	/* 3 channels asked for; the stereo fake gives 2 */
	command(&c);
	ASSERT_EQ(fk.opens, 2);
	ASSERT_EQ(E.req.channels, 3);
	ASSERT_EQ(fk.channels, 2);
}

static struct { uint64_t frame, riseAt; unsigned rises, widthBad, other; int level; double energy; } clk;

static void clockRendered(const float *buf, unsigned frames, unsigned channels)	/* clock on 2, the mix in mono on 1 */
{
	for (unsigned i = 0; i < frames; i++, clk.frame++)
	{
		const float m = buf[(size_t)i * channels], v = buf[(size_t)i * channels + 1u];
		clk.energy += (double)m * m;
		clk.other += v != 0.0f && v != 1.0f;	/* the mix on the clock's channel */
		const int high = v == 1.0f;
		if (high && !clk.level)
		{
			clk.rises++;
			clk.riseAt = clk.frame;
		}
		else if (!high && clk.level)	/* a 96-PPQN pulse at 120 BPM: 229.9 frames; each edge within a pass (11) */
			clk.widthBad += clk.frame - clk.riseAt < 207u || clk.frame - clk.riseAt > 253u;
		clk.level = high;
	}
}

TEST(R5_engine_clockout)	/* spec 6.8: the firmware's own clock out at 120 BPM, as audio */
{
	boot(1);
	runTo(1.5);
	memset(&clk, 0, sizeof clk);
	fk.rendered = clockRendered;
	const app_cmd_t c = { .kind = APP_CMD_CLOCK_OUT, .a = 2 };
	command(&c);
	emu_counters_t c0, c1;
	emu_getCounters(&c0);
	app_panelEvent_t b = { .kind = APP_P_BUTTON, .index = 33, .value = 1 };	/* START */
	ASSERT_EQ(app_enginePanel(&E, &b), 0);
	runTo(1.6);
	b.value = 0;
	ASSERT_EQ(app_enginePanel(&E, &b), 0);
	runTo(5.6);
	emu_getCounters(&c1);
	const unsigned pulses = (unsigned)((c1.clockEdges - c0.clockEdges) / 2u);
	ASSERT_TRUE(clk.rises >= 8u);
	ASSERT_TRUE(clk.rises + 1u >= pulses && clk.rises <= pulses + 1u);	/* one may still be in the ring */
	ASSERT_EQ(clk.widthBad, 0);
	ASSERT_EQ(clk.other, 0);
	ASSERT_TRUE(clk.energy > 0.0);
	ASSERT_EQ(E.clkOut.dropped, 0);
}
```

In `test_app_m18.c`, before `TEST_MAIN()`:

```c
/* R5 (spec 6.8, F5-F8). A fake input like the output: 48 kHz, 512-frame buffers, mHostTime (512 + 48) frames before
 * the callback, 40 frames of latency. Channel 1 is the clock: 5 ms pulses at 4 PPQ and 120 BPM, 3.6-9 s. Channel 2 is
 * the reset: high 3.0-3.5 s and 6.55-7.55 s. The init card runs in EXT; each release arms a start at the next pulse */
#define IN_LEAD 280000u
#define IN_LATENCY 40u
#define HOST_MS(ms) (1000u + (uint64_t)(ms) * 24000u)
static float inBuf[512 * 2];

static int resetHigh(uint64_t h) { return (h >= HOST_MS(3000) && h < HOST_MS(3500)) || (h >= HOST_MS(6550) && h < HOST_MS(7550)); }
static int pulseHigh(uint64_t h) { return h >= HOST_MS(3600) && h < HOST_MS(9000) && (h - HOST_MS(3600)) % 3000000u < 120000u; }

static void inputEvent(fake_t *f)
{
	const uint64_t entry = f->nextEvent, host = entry - IN_LEAD;
	for (unsigned i = 0; i < 512u; i++)
	{
		const uint64_t jack = host + 500u * i - 500u * IN_LATENCY;	/* frame i at the jack: 500 ticks a frame */
		inBuf[2u * i] = pulseHigh(jack) ? 0.8f : 0.0f;
		inBuf[2u * i + 1u] = resetHigh(jack) ? 0.8f : 0.0f;
	}
	app_jackInRender(&E.jackIn, host, entry, 1.0, inBuf, 512u);
	f->nextEvent = entry + FAKE_PERIOD;
}

TEST(R5_jack_slave)
{
	app_host_t h;
	fakeHost(&h, 1);
	if (app_engineInit(&E, &h) != 0)
		exit(2);
	static char card[1024];
	fakeCard(card, sizeof card, 0u);	/* EXT: sync mode 1 */
	const app_engineConfig_t c = { .cardDir = card, .seed = 1u, .dAuto = 1, .mix = APP_MIX_SUM };
	app_enginePowerOn(&E, &c);
	memset(&notes, 0, sizeof notes);
	const app_jackInConfig_t jc = { FAKE_TPS, 48000.0, IN_LATENCY, 2u, 0u, 1u, 0.5f, 0.5f };
	app_jackInStart(&E.jackIn, &jc);	/* as app_ciOpen() before the unit starts */
	fk.event = inputEvent;
	fk.nextEvent = HOST_MS(2500);
	const app_midiOutSink_t sink = { NULL, recordNotes };
	while (fk.now < HOST_MS(9500))
	{
		app_engineSlice(&E);
		(void)app_midiOutStep(&E, UINT64_MAX, &sink);
		(void)app_engineSnapshot(&E, &snap);
	}
	const double D = (double)E.dCycles / EMU_CPU_HZ;
	ASSERT_NEAR(E.L * 1e3, 57.0 + 12.5, 0.1);	/* the input's lead, (512 + 48 + 40) frames (F7) */
	ASSERT_NEAR(D, E.L + APP_D_MARGIN, 1e-6);
	static const unsigned pulse[9] = { 0, 4, 8, 12, 16, 20, 32, 36, 40 };	/* steps 1, 5, 9, 13; none in the gate */
	ASSERT_EQ(notes.n, 9);
	for (unsigned n = 0; n < 9u; n++)
	{
		const double lat = ((double)notes.t[n] - (double)(HOST_MS(3600) + 3000000u * pulse[n])) / FAKE_TPS;
		ASSERT_TRUE(lat >= D - 0.0001 && lat <= D + 0.00145);
	}
	ASSERT_NEAR(snap.diag.inLatMin, D, 0.0002);	/* every edge entered at map + D */
	ASSERT_NEAR(snap.diag.inLatMax, D, 0.0002);
	ASSERT_EQ(app_ringDropped(&E.jackIn.edges), 0);
	ASSERT_EQ(snap.diag.midiStale, 0);
}
```

CMake: `test_app_engine` gains `R5_render_clock R5_engine_holdback R5_engine_clockout`, and `test_app_m18` gains `R5_jack_slave`.

- [ ] **Step 3: Run EMU-CHECK.** Expected: a build error (`app_frame_t`, `clockChannel`, `jackIn`).

- [ ] **Step 4: Types and header.**
  - `app_types.h`:
    - add `#define APP_CLOCK_CHANNELS 64u /**< the clock-out jack on output channel 1 to this; inputs as many (spec 6.8) */`;
    - `app_cmdKind_t` ends `APP_CMD_STOP, APP_CMD_CLOCK_OUT`, and its comment gains "CLOCK_OUT a = the output channel 1-64 of the clock-out jack, 0 off";
    - `app_outputInfo_t`'s frame list becomes `deviceLatency, streamLatency, safetyOffset, bufferFrames, channels, deviceChannels`;
    - `app_diagnostics_t` gains `double inputLead;` and `uint64_t jackInDropped;`.
  - `app_engine.h`:
    - add `#include "app_jackaudio.h"` and `typedef struct app_frame { int16_t s[4]; int16_t clock; } app_frame_t; /**< E5, F1: L1 R1 L2 R2, the clock-out jack 1 high */`;
    - `app_audio_t`: `_Atomic int mix, mute, clockChannel;` (clockChannel is 1–64, 0 off);
    - `app_engineConfig_t` gains `int clockOutChannel;`;
    - `app_engine_t` gains `app_jackIn_t jackIn;` after `audio`, and for the firmware thread: `app_clkOut_t clkOut; int clockOutAudio, held; int16_t hold[2][2u * EMU_BLOCK_FRAMES]; uint64_t holdFirst; double inLead;`.

- [ ] **Step 5: The engine** (`app_engine.c`, `#include <limits.h>`). In `app_audioRender()`, after `used`, and the loop (the sum and out-2 lines `:56–65` stay as they are):

```c
	const int clk = atomic_load_explicit(&a->clockChannel, memory_order_relaxed);
	const unsigned cc = clk > 0 && (unsigned)clk <= channels ? (unsigned)clk - 1u : UINT_MAX;	/* F3: none if not there */
	const int mono = used == 2u && cc < 2u;	/* F4: the clock takes one of the pair */
	for (unsigned i = 0; i < n; i++)
	{
		const app_frame_t *f = app_ringPeek(&a->frames, i);
		int32_t v[4] = { f->s[0], f->s[1], f->s[2], f->s[3] };
		/* the sum and out 2, unchanged */
		if (mono)
			v[1u - cc] = (v[0] + v[1]) >> 1;	/* (left + right) / 2 in int32, as the sum */
		float *o = out + (size_t)i * channels;
		for (unsigned c = 0; c < channels; c++)
			o[c] = mute || c >= used ? 0.0f : (float)v[c] / 32768.0f;
		if (cc != UINT_MAX)
			o[cc] = !mute && f->clock ? 1.0f : 0.0f;	/* the clock alone, +1.0 while PC13 is high (spec 6.8) */
	}
```

Above `app_engineOpen()`:

```c
static unsigned app_engineChannels(app_engine_t *e)	/* F3: the mix's 2 or 4, or the clock's channel */
{
	const unsigned mix = atomic_load(&e->audio.mix) == APP_MIX_FOUR ? 4u : 2u;
	const unsigned clk = (unsigned)atomic_load(&e->audio.clockChannel);
	return clk > mix ? clk : mix;
}
```

- `app_engineSetL()` passes `e->clockOutAudio, e->inLead` as its last two fields.
- In `app_engineOpen()`, `:173` becomes `e->req.channels = app_engineChannels(e);`, and `e->held = 0;` follows the ring resets.
- In `app_engineInputs()`, after the panel loop, the same loop runs over `&e->jackIn.edges` (copy the entry, pop it, `app_enginePanelEvent(e, &ev, now)`): F8.
- The sinks:

```c
static void app_enginePush(app_engine_t *e, const int16_t *out1, const int16_t *out2, uint64_t first)	/* E5, F1 */
{
	int16_t level[EMU_BLOCK_FRAMES];
	app_clkRender(&e->clkOut, first, EMU_BLOCK_FRAMES, level);
	uint32_t n = 0;
	for (app_frame_t *f; n < EMU_BLOCK_FRAMES && (f = app_ringSlot(&e->audio.frames, n)) != NULL; n++)
		*f = (app_frame_t){ { out1[2u * n], out1[2u * n + 1u], out2[2u * n], out2[2u * n + 1u] }, level[n] };
	if (n < EMU_BLOCK_FRAMES)
		atomic_fetch_add_explicit(&e->audio.frames.dropped, EMU_BLOCK_FRAMES - n, memory_order_relaxed);
	app_ringCommit(&e->audio.frames, n);
}

static void app_sinkAudio(void *ctx, uint64_t t, const int16_t *out1, const int16_t *out2)
{
	app_engine_t *e = ctx;
	(void)t;
	if (!e->outputRunning)
	{
		e->held = 0;
		return;	/* the next open's frameBase keeps the frames counted */
	}
	emu_counters_t c;
	emu_getCounters(&c);
	const uint64_t first = (uint64_t)EMU_BLOCK_FRAMES * c.blocks;	/* request k plays 64k to 64k + 63 */
	if (e->held)	/* the last request's block: every edge of its time is known now (F2) */
	{
		app_enginePush(e, e->hold[0], e->hold[1], e->holdFirst);
		e->held = 0;
	}
	if (!e->clockOutAudio)
	{
		app_enginePush(e, out1, out2, first);
		return;
	}
	memcpy(e->hold[0], out1, sizeof e->hold[0]);
	memcpy(e->hold[1], out2, sizeof e->hold[1]);
	e->holdFirst = first;
	e->held = 1;
}

static void app_sinkClock(void *ctx, uint64_t t, int level)
{
	app_engine_t *e = ctx;
	e->clockLevel = level;
	if (level)
		e->clockSeen = 1;	/* the latch of spec 6.8 */
	emu_counters_t c;
	emu_getCounters(&c);
	if (c.codecStart != EMU_NEVER)
		app_clkEdge(&e->clkOut, app_clkFrame(c.codecStart, t), level);	/* F1 */
}
```

Commands. `APP_CMD_MIX` becomes `atomic_store(&e->audio.mix, c.a); if (app_engineChannels(e) != e->req.channels) app_engineReopen(e);`, and there is a new case:

```c
		case APP_CMD_CLOCK_OUT:
		{
			const int ch = c.a >= 1 && c.a <= (int)APP_CLOCK_CHANNELS ? c.a : 0;
			atomic_store(&e->audio.clockChannel, ch);
			if ((ch != 0) != e->clockOutAudio)
			{
				e->clockOutAudio = ch != 0;	/* the next request holds a block, or lets it go (F2) */
				if (e->warm >= APP_WARMUP)
					app_engineSetL(e);	/* one block more or less (spec 6.4) */
			}
			if (app_engineChannels(e) != e->req.channels)
				app_engineReopen(e);	/* F3 */
			break;
		}
```

- `app_engineInit()`: the frames ring takes `sizeof(app_frame_t)`, and the table gains `{ &e->jackIn.edges, sizeof(app_panelEvent_t), 256u }`.
- `app_enginePowerOn()`, before `app_engineOpen(e)`: store the clamped `cfg->clockOutChannel` (1–64, else 0) in `audio.clockChannel`, and set `e->clockOutAudio = ch != 0`.
- `app_engineSlice()`, after the notes:

```c
	const double inLead = (double)atomic_load_explicit(&e->jackIn.lead, memory_order_relaxed) / e->host.ticksPerSec;
	if (inLead != e->inLead)	/* F7: the input callback fixes it after its warm-up; 0 while no input runs */
	{
		e->inLead = inLead;
		if (e->warm >= APP_WARMUP)
			app_engineSetL(e);
	}
```

- `app_engineSnap()`: `e->diag.inputLead = e->inLead; e->diag.jackInDropped = app_ringDropped(&e->jackIn.edges);`.

- [ ] **Step 6: Run** `ctest --test-dir build/emu -R '^R5_|^M18_|^R4_engine' --output-on-failure`: everything passes. Then EMU-CHECK: `out of 274`. Put `clk.rises`, `pulses` and `R5_jack_slave`'s smallest and largest latency into the commit message.

- [ ] **Step 7: Commit and push.** Message: `Emulator app: the clock-out jack in the audio frames, the hold-back, the clock channel, clock and reset in through the entry rule (E5.5 R5)`.

---

### Task 3: Settings, the CoreAudio input, the shell (§6.8, §6.9, F3, F9, F11–F13)

**Files:** modify `emu/app/engine/app_types.h`, `app_settings.{h,c}`, `emu/app/mac/app_coreaudio.{h,c}`, `app_shell.{h,c}`, `emu/tests/test_app_settings.c`, `test_app_devices.c` and `emu/CMakeLists.txt`.

- [ ] **Step 1: The failing tests.** In `R4_settings_keep`, `clockout.audio = 1\n` becomes `future.option = 1\n`, in the text and in both `strstr()`s (F12). Then:

```c
TEST(R5_settings_clock)
{
	char dir[512], path[1024];
	emu_testTempDir(dir, sizeof dir);
	snprintf(path, sizeof path, "%s/settings.txt", dir);
	app_settings_t s, t;
	app_settingsDefaults(&s);
	ASSERT_EQ(s.clockOutAudio + s.clockInAudio, 0);	/* both off by default (spec 6.8) */
	ASSERT_EQ(s.clockOutChannel, 2);
	ASSERT_EQ(s.clockInChannel, 1);
	ASSERT_EQ(s.resetChannel, 0);
	ASSERT_EQ_DBL(s.clockInThreshold, 0.5);
	ASSERT_EQ_DBL(s.resetThreshold, 0.5);
	const char *text = "clockout.audio = 1\nclockout.channel = 4\nclockin.audio = 1\nclockin.device = Interface:x\\s\n"
		"clockin.channel = 65\nclockin.threshold = 0.3\nreset.channel = 2\nreset.threshold = 0.99\n";
	emu_testWrite(dir, "settings.txt", text, strlen(text));
	ASSERT_EQ(app_settingsLoad(&s, path), 0);
	ASSERT_EQ(s.clockOutAudio, 1);
	ASSERT_EQ(s.clockOutChannel, 4);
	ASSERT_EQ(s.clockInAudio, 1);
	ASSERT_EQ(strcmp(s.clockInDevice, "Interface:x "), 0);
	ASSERT_EQ(s.clockInChannel, 1);			/* 65: out of range, the default stays */
	ASSERT_EQ_DBL(s.clockInThreshold, 0.3);
	ASSERT_EQ(s.resetChannel, 2);
	ASSERT_EQ_DBL(s.resetThreshold, 0.5);	/* above 95 % */
	ASSERT_TRUE(strstr(s.extra, "clock") == NULL);
	ASSERT_EQ(app_settingsSave(&s, path), 0);
	app_settingsDefaults(&t);
	ASSERT_EQ(app_settingsLoad(&t, path), 0);
	ASSERT_EQ(t.clockOutChannel, 4);
	ASSERT_EQ(strcmp(t.clockInDevice, s.clockInDevice), 0);
	ASSERT_EQ_DBL(t.clockInThreshold, 0.3);
}
```

In `test_app_devices.c`:

```c
/* R5 (F13): the input's properties need no permission; the open starts input IO, which prompts the terminal for the
 * microphone, so it runs only with LICHOMAT_EMU_INPUT_TEST=1 */
static int inputChild(void)
{
	app_outputDevice_t d[32];
	app_outputInfo_t info;
	if (app_caInputs(d, 32) == 0 || app_ciProbe("", &info) != 0)
	{
		printf("SKIP R5_dev_input: no input device\n");
		return 77;
	}
	printf("%s: %.0f Hz, %u ch, %u frames, latency %u + %u, safety offset %u\n", info.name, info.deviceRate, info.channels,
		info.bufferFrames, info.deviceLatency, info.streamLatency, info.safetyOffset);
	if (!(info.deviceRate > 0.0 && info.channels >= 1u))
		return 1;
	const char *go = getenv("LICHOMAT_EMU_INPUT_TEST");
	if (!go || strcmp(go, "1") != 0)
	{
		printf("SKIP R5_dev_input: the open needs the microphone permission; LICHOMAT_EMU_INPUT_TEST=1 runs it\n");
		return 77;
	}
	static app_jackIn_t in;
	static app_ci_t ci;
	const app_clockInOptions_t o = { 1, "", 1, 0, 0.5, 0.5 };
	char err[256] = "";
	if (app_ringInit(&in.edges, sizeof(app_panelEvent_t), 256u) != 0 || app_ciOpen(&ci, &in, &o, 24e6, err, sizeof err) != 0)
	{
		printf("open: %s\n", err);
		return 1;
	}
	const double t0 = app_wallSeconds();
	while (atomic_load(&in.callbacks) < APP_IN_WARMUP + 8u && app_wallSeconds() - t0 < 3.0)
		nanosleep(&(const struct timespec){ 0, 5000000L }, NULL);
	app_ciClose(&ci);
	const double lead = (double)atomic_load(&in.lead) / 24e6;
	printf("%llu input callbacks, lead %.2f ms\n", (unsigned long long)atomic_load(&in.callbacks), lead * 1e3);
	return atomic_load(&in.callbacks) >= APP_IN_WARMUP + 8u && lead > 0.0 && lead < 0.5 ? 0 : 1;
}

TEST(R5_dev_input) { PROBE(inputChild, "R5_dev_input", 5); }
```

CMake: `test_app_settings` gains `R5_settings_clock`, and `test_app_devices` gains `R5_dev_input`.

- [ ] **Step 2: Run EMU-CHECK.** Expected: a build error.

- [ ] **Step 3: Settings (F12).**
  - `app_types.h`: `typedef struct app_clockInOptions { int on; char uid[128]; int clockChannel, resetChannel; double clockThreshold, resetThreshold; } app_clockInOptions_t; /**< spec 6.8: inputs 1-64, reset 0 none; 0.05-0.95 */`.
  - `app_settings_t` gains `int clockOutAudio, clockOutChannel, clockInAudio, clockInChannel, resetChannel; char clockInDevice[128]; double clockInThreshold, resetThreshold;`.
  - `app_settingsDefaults()` sets the channels to 2 and 1 and both thresholds to 0.5.
  - `app_fields[]` gains:

```c
	F("clockout.audio", T_INT, clockOutAudio, 0, 1),
	F("clockout.channel", T_INT, clockOutChannel, 1, 64),
	F("clockin.audio", T_INT, clockInAudio, 0, 1),
	F("clockin.device", T_STR, clockInDevice, 0, 0),
	F("clockin.channel", T_INT, clockInChannel, 1, 64),
	F("clockin.threshold", T_DBL, clockInThreshold, 0.05, 0.95),
	F("reset.channel", T_INT, resetChannel, 0, 64),
	F("reset.threshold", T_DBL, resetThreshold, 0.05, 0.95),
```

- [ ] **Step 4: The output's channels (F3)** in `app_coreaudio.c`.
  - `app_caDeviceChannels(dev)` becomes `app_caChannels(AudioObjectID dev, AudioObjectPropertyScope scope)`.
  - `app_caList()`'s body becomes `static int app_caListScope(app_outputDevice_t *out, int max, AudioObjectPropertyScope scope)`; `app_caList()` and the new `app_caInputs()` call it with `APP_CA_OUTPUT` and `kAudioObjectPropertyScopeInput`.
  - The default device and the lookup take the default's selector: `app_caDefaultDevice(AudioObjectPropertySelector def)` and `app_caFindDevice(const char *uid, AudioObjectPropertySelector def, int *followDefault)`. The output passes `kAudioHardwarePropertyDefaultOutputDevice`.
  - In `app_caOpenUnit()`, `:290` becomes:

```c
	const unsigned cap = deviceChannels < APP_CLOCK_CHANNELS ? deviceChannels : APP_CLOCK_CHANNELS;
	ca->channels = req->channels < cap ? req->channels : cap;	/* the mix's 2 or 4, or the clock's channel (F3) */
	ca->channels = ca->channels < 2u ? 2u : ca->channels;
```

  - The `ChannelMap` is set when `ca->channels > 2u`, with `map[i] = i < ca->channels ? (SInt32)i : -1`.
  - The info gets `info->deviceChannels = deviceChannels;`.

- [ ] **Step 5: The input unit (F9).** `app_coreaudio.h` gains:

```c
typedef struct app_ci
{
	AudioUnit unit;
	AudioObjectID device;
	int followDefault, initialized, started;
	unsigned channels;		/**< the client's interleaved channels: the device's inputs, at most 64 */
	UInt32 maxFrames;
	float *buf;
	AudioBufferList abl;	/**< the callback's: one interleaved buffer over buf */
	app_jackIn_t *in;
	dispatch_queue_t queue;
	AudioObjectPropertyListenerBlock listener;
	app_caListen_t listen[4];
	unsigned nListen;
	_Atomic int changed;	/**< a listener saw the device change; the main thread reopens (F9) */
	app_outputInfo_t info;	/**< the input's name, rate, latencies, buffer, channels */
} app_ci_t;

int app_caInputs(app_outputDevice_t *out, int max);	/**< the devices with input channels */
/** AUHAL input of o (F9); app_jackInStart(in) before the start. 0, or -1 with the reason in err and nothing left open.
 * Main thread; only with the microphone permission (F10) */
int app_ciOpen(app_ci_t *ci, app_jackIn_t *in, const app_clockInOptions_t *o, double ticksPerSec, char *err, size_t n);
void app_ciClose(app_ci_t *ci);	/**< the callback has returned for good when it returns */
int app_ciProbe(const char *uid, app_outputInfo_t *info);	/**< the input's properties without IO; -1: no device */
OSStatus app_ciInput(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus, UInt32 frames,
	AudioBufferList *io);
```

At the end of `app_coreaudio.c` (with `#include <limits.h>`):

```c
#define APP_CA_INPUT kAudioObjectPropertyScopeInput
#define APP_CI_TRY(what, call) do { const OSStatus st_ = (call); if (st_ != noErr) return app_caFail(err, n, what, st_); } while (0)

static int app_ciDescribe(const char *uid, AudioObjectID *dev, int *followDefault, app_outputInfo_t *info)	/* no IO */
{
	memset(info, 0, sizeof *info);
	*dev = app_caFindDevice(uid, kAudioHardwarePropertyDefaultInputDevice, followDefault);
	if (*dev == kAudioObjectUnknown || (info->channels = info->deviceChannels = app_caChannels(*dev, APP_CA_INPUT)) == 0u)
		return -1;
	Float64 rate = 0.0;
	UInt32 u[4] = { 0, 0, 0, 0 };
	AudioStreamID stream = kAudioObjectUnknown;
	CFStringRef name = NULL;
	(void)app_caGet(*dev, kAudioDevicePropertyNominalSampleRate, APP_CA_GLOBAL, &rate, sizeof rate);
	(void)app_caGet(*dev, kAudioDevicePropertyLatency, APP_CA_INPUT, &u[0], sizeof u[0]);
	(void)app_caGet(*dev, kAudioDevicePropertySafetyOffset, APP_CA_INPUT, &u[1], sizeof u[1]);
	if (app_caGet(*dev, kAudioDevicePropertyBufferFrameSize, APP_CA_INPUT, &u[2], sizeof u[2]) != noErr)
		(void)app_caGet(*dev, kAudioDevicePropertyBufferFrameSize, APP_CA_GLOBAL, &u[2], sizeof u[2]);
	/* the first input stream's latency (spec 6.8): room for one ID gives the first */
	if (app_caGet(*dev, kAudioDevicePropertyStreams, APP_CA_INPUT, &stream, sizeof stream) == noErr && stream != kAudioObjectUnknown)
		(void)app_caGet(stream, kAudioStreamPropertyLatency, APP_CA_GLOBAL, &u[3], sizeof u[3]);
	if (app_caGet(*dev, kAudioObjectPropertyName, APP_CA_GLOBAL, &name, sizeof name) == noErr)
		app_caCopyString(name, info->name, sizeof info->name);
	info->deviceRate = info->clientRate = rate;
	info->deviceLatency = u[0];
	info->safetyOffset = u[1];
	info->bufferFrames = u[2];
	info->streamLatency = u[3];
	return rate > 0.0 ? 0 : -1;
}

int app_ciProbe(const char *uid, app_outputInfo_t *info)
{
	AudioObjectID dev;
	int follow;
	return app_ciDescribe(uid, &dev, &follow, info);
}

OSStatus app_ciInput(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus, UInt32 frames,
	AudioBufferList *io)
{
	(void)io;	/* NULL for an input callback: the frames are pulled */
	const uint64_t entry = mach_absolute_time();
	app_ci_t *ci = ref;
	if (frames == 0u || frames > ci->maxFrames || !(ts->mFlags & kAudioTimeStampHostTimeValid))
		return noErr;	/* no room, or no time to put the edges at */
	ci->abl.mNumberBuffers = 1u;
	ci->abl.mBuffers[0].mNumberChannels = ci->channels;
	ci->abl.mBuffers[0].mDataByteSize = frames * ci->channels * (UInt32)sizeof(float);
	ci->abl.mBuffers[0].mData = ci->buf;
	const OSStatus st = AudioUnitRender(ci->unit, flags, ts, bus, frames, &ci->abl);
	if (st == noErr)	/* spec 6.8: mHostTime, the frame's offset, the latency */
		app_jackInRender(ci->in, ts->mHostTime, entry, (ts->mFlags & kAudioTimeStampRateScalarValid) ? ts->mRateScalar : 1.0,
			ci->buf, frames);
	return st;
}

void app_ciClose(app_ci_t *ci)
{
	if (ci->started)
		AudioOutputUnitStop(ci->unit);	/* the callback has returned when this does */
	for (unsigned i = 0; i < ci->nListen; i++)
		(void)AudioObjectRemovePropertyListenerBlock(ci->listen[i].obj, &ci->listen[i].addr, ci->queue, ci->listener);
	if (ci->queue)
	{
		dispatch_sync(ci->queue, ^{ });
		dispatch_release(ci->queue);
	}
	if (ci->listener)
		Block_release(ci->listener);
	if (ci->initialized)
		AudioUnitUninitialize(ci->unit);
	if (ci->unit)
		AudioComponentInstanceDispose(ci->unit);
	free(ci->buf);
	app_jackIn_t *const in = ci->in;
	memset(ci, 0, sizeof *ci);
	ci->in = in;
	ci->device = kAudioObjectUnknown;
}

static int app_ciOpenUnit(app_ci_t *ci, const app_clockInOptions_t *o, double ticksPerSec, char *err, size_t n)
{
	if (app_ciDescribe(o->uid, &ci->device, &ci->followDefault, &ci->info) != 0)	/* the UID's, else the default input */
	{
		snprintf(err, n, "no input device");
		return -1;
	}
	ci->channels = ci->info.channels < APP_CLOCK_CHANNELS ? ci->info.channels : APP_CLOCK_CHANNELS;
	if (o->clockChannel < 1 || (unsigned)o->clockChannel > ci->channels || o->resetChannel < 0
		|| (unsigned)o->resetChannel > ci->channels)
	{
		snprintf(err, n, "%s has %u input channels", ci->info.name, ci->channels);
		return -1;
	}
	const AudioComponentDescription desc = { kAudioUnitType_Output, kAudioUnitSubType_HALOutput,
		kAudioUnitManufacturer_Apple, 0, 0 };
	AudioComponent comp = AudioComponentFindNext(NULL, &desc);
	if (!comp || AudioComponentInstanceNew(comp, &ci->unit) != noErr)
	{
		ci->unit = NULL;
		snprintf(err, n, "no HAL unit for the input");
		return -1;
	}
	/* AUHAL: input (element 1) on, output (element 0) off, then the device; in this order */
	const UInt32 on = 1u, off = 0u;
	APP_CI_TRY("AudioUnitSetProperty(EnableIO, input)", AudioUnitSetProperty(ci->unit,
		kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &on, sizeof on));
	APP_CI_TRY("AudioUnitSetProperty(EnableIO, output)", AudioUnitSetProperty(ci->unit,
		kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &off, sizeof off));
	APP_CI_TRY("AudioUnitSetProperty(CurrentDevice)", AudioUnitSetProperty(ci->unit,
		kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &ci->device, sizeof ci->device));
	/* the client side (element 1's output scope): float32 interleaved at the device's rate, no converter */
	const UInt32 bpf = ci->channels * (UInt32)sizeof(float);
	const AudioStreamBasicDescription f = { ci->info.deviceRate, kAudioFormatLinearPCM,
		kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked, bpf, 1u, bpf, ci->channels, 32u, 0u };
	APP_CI_TRY("AudioUnitSetProperty(StreamFormat, input)", AudioUnitSetProperty(ci->unit,
		kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &f, sizeof f));
	const AURenderCallbackStruct cb = { app_ciInput, ci };
	APP_CI_TRY("AudioUnitSetProperty(SetInputCallback)", AudioUnitSetProperty(ci->unit,
		kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cb, sizeof cb));
	APP_CI_TRY("AudioUnitInitialize(input)", AudioUnitInitialize(ci->unit));
	ci->initialized = 1;
	UInt32 size = sizeof ci->maxFrames;	/* the most frames one callback brings */
	if (AudioUnitGetProperty(ci->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
			&ci->maxFrames, &size) != noErr || ci->maxFrames == 0u)
		ci->maxFrames = 4096u;
	if ((ci->buf = calloc((size_t)ci->maxFrames * ci->channels, sizeof(float))) == NULL)
	{
		snprintf(err, n, "out of memory for the input buffer");
		return -1;
	}
	const app_jackInConfig_t jc = { ticksPerSec, ci->info.deviceRate, ci->info.deviceLatency + ci->info.streamLatency,
		ci->channels, (unsigned)o->clockChannel - 1u, o->resetChannel > 0 ? (unsigned)o->resetChannel - 1u : UINT_MAX,
		(float)o->clockThreshold, (float)o->resetThreshold };
	app_jackInStart(ci->in, &jc);	/* before the first callback (F5-F7) */
	APP_CI_TRY("AudioOutputUnitStart(input)", AudioOutputUnitStart(ci->unit));
	ci->started = 1;
	snprintf(ci->info.format, sizeof ci->info.format, "input float32 %u ch %.0f Hz; latency %u + %u frames", ci->channels,
		ci->info.deviceRate, ci->info.deviceLatency, ci->info.streamLatency);
	/* the listeners: the device gone, its rate or its inputs changed, the default input moved (F9) */
	if ((ci->queue = dispatch_queue_create("lichomat-emu.input", DISPATCH_QUEUE_SERIAL)) == NULL)
	{
		snprintf(err, n, "dispatch_queue_create failed");
		return -1;
	}
	app_ci_t *const self = ci;
	ci->listener = Block_copy(^(UInt32 count, const AudioObjectPropertyAddress *addr) {
		(void)count;
		(void)addr;
		atomic_store(&self->changed, 1);
	});
	const AudioObjectPropertySelector sel[3] = { kAudioDevicePropertyDeviceIsAlive, kAudioDevicePropertyNominalSampleRate,
		kAudioDevicePropertyStreamConfiguration };
	const AudioObjectPropertyScope scope[3] = { APP_CA_GLOBAL, APP_CA_GLOBAL, APP_CA_INPUT };
	unsigned want = 0;
	for (unsigned i = 0; i < 3u; i++)
		ci->listen[want++] = (app_caListen_t){ ci->device, app_caAddr(sel[i], scope[i]) };
	if (ci->followDefault)
		ci->listen[want++] = (app_caListen_t){ kAudioObjectSystemObject,
			app_caAddr(kAudioHardwarePropertyDefaultInputDevice, APP_CA_GLOBAL) };
	for (unsigned i = 0; ci->listener && i < want; i++)
	{
		APP_CI_TRY("AudioObjectAddPropertyListenerBlock(input)",
			AudioObjectAddPropertyListenerBlock(ci->listen[i].obj, &ci->listen[i].addr, ci->queue, ci->listener));
		ci->nListen = i + 1u;
	}
	return 0;
}

int app_ciOpen(app_ci_t *ci, app_jackIn_t *in, const app_clockInOptions_t *o, double ticksPerSec, char *err, size_t n)
{
	memset(ci, 0, sizeof *ci);
	ci->in = in;
	err[0] = '\0';
	if (app_ciOpenUnit(ci, o, ticksPerSec, err, n) == 0)
		return 0;
	app_ciClose(ci);
	return -1;
}
```

- [ ] **Step 6: The shell (F9, F11).** `app_shell.h` gains:

```c
/** Clock and reset in from audio (spec 6.8): stores o in the settings, then opens, reopens or closes the input; a threshold
 * alone reopens nothing. The GUI calls it with on = 1 only once the microphone is granted (F10). 0, or -1 with the reason
 * said once as 'notice: clock in from audio: ...'; while on and closed, app_shellFrame() retries every 2 s. Main thread */
int app_shellClockIn(app_shell_t *s, const app_clockInOptions_t *o);
int app_shellInputs(app_shell_t *s, app_outputDevice_t *out, int max);	/**< app_caInputs(); main thread */
int app_shellInputInfo(const app_shell_t *s, app_outputInfo_t *out, const char **err);	/**< 1 running; *err the last failure */
```

In `app_shell.c`:
- `struct app_shell` gains `app_ci_t ci; int haveInput, inputAllowed; double inputRetryAt; char inputErr[256];`.
- `app_shellNoteCommand()` stores `APP_CMD_CLOCK_OUT` as `clockOutAudio = c->a > 0`, and also stores the channel when `c->a` is 1–64.
- `s->cfg` gains `.clockOutChannel = s->set.clockOutAudio ? s->set.clockOutChannel : 0`.
- `static void app_shellInputClose(s)`: if the input is open, `app_ciClose(&s->ci)`, set `haveInput = 0`, then `app_jackInStop(&s->engine.jackIn, mach_absolute_time())`. After the stop, the callback has returned for good, so this thread may push the release of a high reset (F11).
- `static int app_shellInputOpen(s)` builds `app_clockInOptions_t` from the settings and calls `app_ciOpen(&s->ci, &s->engine.jackIn, &o, s->mach.ticksPerSec, err, sizeof err)`.
  - On success: `haveInput = 1`, `inputErr[0] = 0`.
  - On failure: `inputRetryAt = app_wallSeconds() + 2.0`, and if `err` differs from `inputErr`, say `notice: clock in from audio: <err>` and keep it.
- `app_shellClockIn()`:
  1. Computes `reopen` before it stores anything: the input is not open, or the uid or either channel changed.
  2. Stores `on` in both `clockInAudio` and `inputAllowed`, then stores the uid, and the channels and thresholds when they are in range; calls `app_shellChanged(s)`.
  3. Stores the two thresholds into `engine.jackIn`'s atomics.
  4. Off: closes the input and clears `inputErr`. On, with `reopen`: closes, then opens.
- `app_shellFrame()`, before the snapshot: `if (s->haveInput && atomic_exchange(&s->ci.changed, 0)) app_shellInputClose(s);`, then `if (s->set.clockInAudio && s->inputAllowed && !s->haveInput && wall >= s->inputRetryAt) (void)app_shellInputOpen(s);`.
- `app_shellStop()` starts with `app_shellInputClose(s);`, and `app_shellRestart()` calls it right before `app_shellMidiStop(s);` (`:408`).

- [ ] **Step 7: Run** `ctest --test-dir build/emu -R '^R5_|^R4_settings|^R4_dev' --output-on-failure`. `R5_dev_input` prints its device and SKIPs, or SKIPs with "no input device".
  - Once, by hand: `LICHOMAT_EMU_INPUT_TEST=1 build/emu/test_app_devices R5_dev_input` (macOS may ask Terminal for the microphone). Put the lead it prints into the commit message.
  - EMU-CHECK: `out of 276`.
  - `cmake -S emu -B build/emu-noapp -G Ninja -DLICHOMAT_EMU_APP=OFF`, build it, then `ctest --test-dir build/emu-noapp -N`: 255.

- [ ] **Step 8: Commit and push.** Message: `Emulator app: the clock-jack settings, the CoreAudio input unit, app_shellClockIn (E5.5 R5)`.

---

### Task 4: GUI, permission, plist, README, spec and A12 (§6.6, §6.8, F10, F11)

**Files:** modify `emu/app/gui/panel.{h,cpp}`, `main.mm`, `Info.plist.in`, `emu/tests/app_test.py`, `emu/README.md` and `emu/CMakeLists.txt`.

- [ ] **Step 1: The failing check.** In `R4_bundle` (`app_test.py`), add after the plist loop:

```python
    mic = subprocess.run(['plutil', '-extract', 'NSMicrophoneUsageDescription', 'raw', plist], capture_output=True, text=True)
    check(mic.returncode == 0 and 'clock' in mic.stdout, 'Info.plist lacks NSMicrophoneUsageDescription (spec 6.8)')
```

and after `readme = …`, add `check('## The clock jacks as audio' in readme, 'the README lacks the clock jacks as audio')`. Then `ctest --test-dir build/emu -R '^R4_bundle$'`: expect FAIL.

- [ ] **Step 2: The plist and the README.** In `Info.plist.in`, before `</dict>`:

```xml
	<key>NSMicrophoneUsageDescription</key>
	<string>LICHOMAT Emu reads the clock and reset jacks from an audio input when you turn on clock and reset in from audio. Nothing is recorded or kept.</string>
```

In `emu/README.md`, after "## Latency as a MIDI clock slave":

```markdown
## The clock jacks as audio

Two options in the Audio window, both off until you turn them on:

- **Clock out on output channel N.** That channel carries the clock alone, at full scale while the
  clock-out jack is high: one pulse per clock, 5.21 ms at 120 BPM, as on the device's jack. The mix plays
  on the other channels, in mono when the clock takes one of the stereo pair. The audio then runs one
  block (1.45 ms) later, and D grows by the same.
- **Clock and reset in from an input.** One input channel carries the clock (a rise above its threshold
  is a pulse); another, if you pick one, the reset (above its threshold is reset on). macOS asks for the
  microphone permission when you turn this on; nothing is recorded. macOS knows this unsigned app by its
  code hash, so it asks again after each new release. D grows by the input's own latency, about 12 ms at
  512 frames. Most audio inputs are AC-coupled and let a held level fall back within a fraction of a
  second: clock pulses pass on any input, a held reset needs a DC-coupled one.
```

- [ ] **Step 3: The panel.**
  - `panel.h`: the request enum ends `PANEL_REQ_REPLACE, PANEL_REQ_CLOCKIN`. `panel_t` gains `app_clockInOptions_t clockIn; int micDenied, audioReset; app_outputDevice_t inDev[PANEL_DEVICES]; int nInDev; double inDevAt;`. Add `void panel_loadClockIn(panel_t *p, const app_settings_t *set);`, which copies the six `clockin.*` and `reset.*` settings into `p->clockIn`.
  - `audioWindow()` grows to `ImVec2(470.0f, 520.0f)`. Between the mix radio buttons and the closing `Separator`, add `ImGui::SeparatorText("Clock jacks as audio")`, then:
    - **Clock out.** Checkbox "clock out on output channel" (`set->clockOutAudio`). Beside it, inside `BeginDisabled(!on)`, a combo of channels 1..`snap->diag.output.deviceChannels` (2 while no output runs). A change sends `APP_CMD_CLOCK_OUT` with `a = on ? channel : 0`. Below: `TextDisabled("The channel carries the clock alone; the mix plays on the others, in mono when it shares the pair; the audio runs one block (1.45 ms) later.")`. When the stored channel is above the device's count, a warning: "channel %d is not on %s (%u channels): it is not heard".
    - **Clock and reset in.** Checkbox "clock and reset in from an input" (`p->clockIn.on`). Inside `BeginDisabled(!p->clockIn.on)`:
      - an "input device" combo: "system default input", then `p->inDev` as "%s (%u in)", refreshed every 2 s by `app_shellInputs`;
      - a "clock channel" combo (1..N) and a "reset channel" combo ("none", 1..N). N is the running input's `channels`, else the listed device's, else 2;
      - "clock threshold" and "reset threshold" as `SliderFloat` 5–95, `"%.0f %% of full scale"`, `AlwaysClamp`, stored ÷ 100, each sent on `IsItemDeactivatedAfterEdit()`.

      Any change, the checkbox included, sets `p->request = PANEL_REQ_CLOCKIN`.
    - **Status line:** `"Input: %s, %.0f Hz, %u frames. D holds %.1f ms more for it."` (from `app_shellInputInfo` and `snap->diag.inputLead`). With `p->micDenied`: "Microphone access is off for LICHOMAT Emu: System Settings > Privacy & Security > Microphone." Otherwise `"Not running: %s"`. Last line: `TextDisabled("A held reset needs a DC-coupled input.")`.
  - In `jackStrip()`, the reset (`:500–506`) becomes:

```cpp
	/* reset in: the panel's switch, unless the reset comes from audio (F11) */
	const app_settings_t *set = app_shellSettings(s);
	app_outputInfo_t in;
	const char *ierr;
	const int audioReset = set->clockInAudio && set->resetChannel > 0 && app_shellInputInfo(s, &in, &ierr);
	if (audioReset && !p->audioReset && p->resetSent && post(s, APP_P_RESET, 0u, 0, 0.0, mach_absolute_time()) == 0)
		p->resetSent = 0;	/* the jack starts low: the input's level rules from now */
	if (!audioReset && p->audioReset)
		p->resetSent = 0;	/* the shell has let the reset go: the switch is sent again as it stands */
	p->audioReset = audioReset;
	jackLabel(480.0f, audioReset ? "rst in (audio)" : "rst in");
	ImGui::SetCursorScreenPos(P(484.0f, cy));
	ImGui::BeginDisabled(audioReset != 0);
	bool reset = p->resetOn != 0u;
	if (ImGui::Checkbox("##rst", &reset))
		p->resetOn = reset ? 1u : 0u;
	ImGui::EndDisabled();
	if (!audioReset && p->resetOn != p->resetSent && post(s, APP_P_RESET, 0u, p->resetOn, 0.0, mach_absolute_time()) == 0)
		p->resetSent = p->resetOn;
```

- [ ] **Step 4: The permission (F10).** In `main.mm`, `#import <AVFoundation/AVFoundation.h>`, and above `@interface EmuDelegate`:

```objc
/* F10: the microphone first, then the shell; the answer comes on a queue of its own, the shell runs on the main thread */
static void emu_clockInApply(BOOL granted)
{
	app_clockInOptions_t o = gPanel.clockIn;
	gPanel.micDenied = !granted;
	if (!granted)
	{
		gPanel.clockIn.on = o.on = 0;
		panel_message(&gPanel, "notice: clock and reset in from audio stay off: LICHOMAT Emu may not use the microphone "
			"(System Settings > Privacy & Security > Microphone)");
	}
	(void)app_shellClockIn(gShell, &o);
}

static void emu_clockIn(void)
{
	if (!gPanel.clockIn.on)
	{
		(void)app_shellClockIn(gShell, &gPanel.clockIn);
		return;
	}
	switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio])
	{
	case AVAuthorizationStatusAuthorized:
		emu_clockInApply(YES);
		break;
	case AVAuthorizationStatusNotDetermined:	/* macOS asks once per code hash: after every ad hoc signed release */
		[AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
			dispatch_async(dispatch_get_main_queue(), ^{ emu_clockInApply(granted); });
		}];
		break;
	default:	/* denied, or restricted by a profile */
		emu_clockInApply(NO);
		break;
	}
}
```

- In `drawInMTKView:`, `else if (request == PANEL_REQ_CLOCKIN) emu_clockIn();` goes before the folder branch.
- In `applicationDidFinishLaunching:`, after `startShell` succeeds: `panel_loadClockIn(&gPanel, app_shellSettings(gShell)); if (gPanel.clockIn.on) emu_clockIn();`.
- In CMake, `lichomat-emu` links `"-framework AVFoundation"`.

- [ ] **Step 5: Run.** EMU-CHECK: `out of 276`, 0 warnings, with `R4_bundle`, `R4_gui_clash` and `M21` passing. Open `build/emu/LICHOMAT Emu.app` with a fresh `LICHOMAT_EMU_HOME` and check the Audio window by eye: both options are off.

- [ ] **Step 6: The gate.** `tools/check.sh`: 57, 276, `OK`, 0 warnings. `ruby tools/ci_dry_run.rb emu-release` and `emu-sanitize`: `ok`, with 277 and 255 ids. Commit `Emulator app: the clock jacks as audio in the Audio window, the microphone permission, the plist key, the README (E5.5 R5)` and push.

- [ ] **Step 7: Docs (the controller, on `timing-core` in `/Users/tk/Code/lxr`).**
  - Spec §6.4: the recompute sentence adds "or the clock-in-from-audio input (its lead, plan R5 F7)", and L adds "+ the input's lead while clock and reset come in from audio".
  - Spec §6.8: add F3/F4's channel rule, F5's hysteresis, F11's switch and the AC-coupling note.
  - Spec §12.3: add A12 (below).
  - Commit, push `timing-core`, and set tracker E5.5 to "R5 done in code; A12 waits for the owner".

  **A12** | The clock jacks as audio, on an audio interface.
  - (a) The app plays at 120 BPM with clock out 1 at 4 PPQ, and clock out as audio on a free output, cabled to the real LXR-02's clock-in jack (EXT, 4 PPQ). For 2 minutes the device's steps follow the app's without a slip. A DAW recording of that output shows 5.2 ms pulses 125 ms apart, with no mix on them.
  - (b) The device's clock out (4 PPQ, 120 BPM) goes into an input; the app is in EXT at 4 PPQ with clock in from audio. The app's steps follow, and the diagnostics' input latency stays within 1 ms of D. A gate from a DC-coupled output into a second input, used as the reset, stops the app and restarts it from step 1.
  - The first turn-on shows the microphone prompt. After a denial the option stays off, with its message |

---

## The owner's checks and the tests

| Check | The owner, by hand | Tests |
|---|---|---|
| A8 | Unchanged, both options off (the default) | `R4_engine_generator`, `R4_engine_clockled`; M12; G9 |
| A12 (a) | Clock channel → the device's clock in; the pulses recorded in a DAW | `R5_clk_frame`, `R5_clk_render`, `R5_render_clock`, `R5_engine_holdback`, `R5_engine_clockout`, `R5_latency_input` |
| A12 (b) | The device's clock out → an input; a gate → a second input; the prompt | `R5_edge_step`, `R5_in_host_time`, `R5_jackin_render`, `R5_jack_slave`, `R5_settings_clock`, `R5_dev_input` (with `LICHOMAT_EMU_INPUT_TEST=1`), `R4_bundle` |

## Points for the owner and the controller

- **The §6.4/§6.8 gap (F7).** D's default does not cover the input path. Without F7, audio edges would enter at 'now' with up to one input buffer of jitter, so "enters at that time plus D" would not hold.
- **§6.8 names only the 2-channel device** for the mix next to the clock. F3 and F4 extend the rule.
- **AC coupling.** Clock pulses work on any input; a held reset needs a DC-coupled input (README, A12 (b)).
- **The reset jack has two sources** (the switch and the audio), which the spec does not cover; F11 decides.
- **No permission means silence,** not an error, so the GUI checks the permission itself (F10).
- **Size.** About 1,350 lines against the 400–800 asked for. Complete code for the pure logic, every test and the input glue takes about 1,000 of them; the engine glue is code, while the shell and the GUI are prose.
- **Not testable here:** the GUI itself, the real input unit (by hand, with `LICHOMAT_EMU_INPUT_TEST=1`), and the cables (A12).

## Changes during execution (2026-10-31)

- **`R5_jack_slave` expects 10 notes, not 9.** After the last pulse (43, at 8.975 s) the slave position
  carries on to the next target, as `seq_core.h` documents ("clamped to the next target"), so step 13
  plays at the place of pulse 44 (9.1 s). The 48 input edges are as planned; the latency tolerance is
  unchanged (the notes came 0.066–0.134 ms after D).
- **Task 1:** `app_latencyIn_t.inputLead` needed `, 0.0` in the four rows of `M18_latency_table`
  (`-Wmissing-field-initializers` is an error).
- **Task 3 gets the output channel count** (`app_outputInfo_t.deviceChannels`) and opens max(mix, clock
  channel) channels up to the device's, so a clock channel of 3 or more works on a real device; and the
  closed sink drains the clock-edge queue, so a long output stop does not leave the clock level stale.
