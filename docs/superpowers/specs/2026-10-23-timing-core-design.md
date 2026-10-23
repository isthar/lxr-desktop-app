# LICHOMAT stage 1: timing core, sync and bug fixes

| | |
|---|---|
| Date | 2026-10-23 |
| Status | Design approved in brainstorming; waiting for spec review |
| Base | `erica-synths/lxr-desktop-app` `main` at `a43cd4f` (firmware 1.7) |
| Branch | `timing-core` in `isthar/lxr-desktop-app` |

## 1. Context

LICHOMAT is unofficial custom firmware for the Erica Synths LXR-02. It is aimed at groove techno in the
style of the BCCO and Mutual Rytm labels: rolling percussion, swung hats over a straight kick,
polymetric loops, ghost notes and accents, and long tool-style loops shaped live with mutes, filters
and fills.

The work is split into three stages. Each stage gets its own spec, plan and implementation:

1. **Timing core, sync and bug fixes** (this document).
2. **Groove features:**
   - per-track swing with templates
   - track delay in ms and full-resolution microtiming
   - humanize
   - recording that keeps microtiming
   - trig conditions
   - per-track clock division
   - rotation, fill and mute scenes
   - DJ filter
3. **Effects:** reverb, tempo-synced echo and "ambient".

Stage 1 replaces the sequencer's timing mechanism with a core that can be tested on a computer, so the
later stages can build on it. It also fixes the timing and MIDI defects found during analysis (§3). It
adds the tooling needed to verify and measure later work: host tests, CI and a CPU meter.

## 2. Goals and non-goals

### Goals

- **G1.** Swing (shuffle) moves complete steps, including their ratchets and microshift. The MIDI clock
  output and the clock output jack stay straight.
- **G2.** Tempo is exact in master mode. In slave mode the incoming tempo is tracked as a fractional
  value.
- **G3.** The LXR works robustly as a MIDI clock slave over DIN and over USB. Clock jitter is low, no
  note is dropped, and Start, Stop, Continue and Song Position Pointer (SPP) are supported.
- **G4.** Defects D1–D8 (§3) are fixed.
- **G5.** The sequencer core is pure C, compiles on the host and is covered by host tests. CI builds
  the firmware and runs the tests.
- **G6.** The device shows a CPU meter.
- **G7.** The firmware is branded as modified: name LICHOMAT, its own splash screen and version.
- **G8.** Kit, pattern, project, song and global files written by firmware 1.7 load unchanged. No file
  format changes in this stage.

### Non-goals

- New per-step data or new sequencer features. These belong to stage 2.
- Sample-accurate voice triggering. Audio onsets remain quantized to the 64-sample render block
  (≈1.45 ms). This is a candidate for after stage 2, once CPU numbers exist.
- New effects or DSP changes. These belong to stage 3.
- Fractional BPM in the user interface. Master tempo stays an integer BPM, but it becomes exact.
- Any change to the bootloader.

## 3. Current behaviour and defects

These findings come from reading `Core/Src/Soundengine/Sequencer/sequencer.c` and the surrounding
code of firmware 1.7. D1–D3 were also reproduced in a host-side replica of `seq_tick()`,
`seq_calcDeltaT()` and `seq_processFlam()`. The replica copies the logic literally and uses the timer
rates from `main.c`, at 130 BPM.

**Timing internals of fw 1.7**
- The internal clock runs at 96 PPQN, which is 24 pulses per 16th-note step.
- Pulses are timed by `systick_ticks`, a 4 kHz counter driven by TIM14.
- `seq_tick()` is polled from the TIM2 interrupt at about 36 kHz, right before each audio block is
  rendered.

| ID | Defect | Evidence | Effect |
|---|---|---|---|
| D1 | Shuffle delays only pulse 0 of a swung step. Pulses 1 to ⌊16·s⌋ then fire in a catch-up burst, because the next `seq_deltaT` becomes negative. | Replica, shuffle 64/127:<br>• pulses 0–8 fire within 0.3 ms<br>• ratchet ×4 hits at 0 / 0.2 / 19 / 48 ms instead of 0 / 29 / 58 / 86 ms<br>• a +9-pulse shift lands 4.5 ms after the hit instead of 43 ms<br>• MIDI clock out intervals range from 0.11 to 57.8 ms instead of a steady 19.23 ms | Shuffle can't be combined with ratchets or microshift. Slaved gear sees a wobbling clock. |
| D2 | `chokeCurrent` / `chokeNext` (`sequencer.c:1089`) are global instead of per track. | Replica:<br>• a snare step shifted −2 suppresses the 4th hit of a hat ×4 ratchet<br>• a kick suppresses a hat hit that spills over from the previous step<br>• with per-track flags both hits play | One track's notes swallow other tracks' ratchet hits and spill-over notes. |
| D3 | `seq_calcDeltaT()` computes `(1000*60)/bpm` in integer arithmetic. | Replica: 137 BPM plays as 137.29 (+0.21%), 128 BPM as 128.20. | Drift against other sources when the LXR is not synced. |
| D4 | Every sequencer trigger sends MIDI Note On twice: once in `voiceControl_noteOn()` and once in `seq_triggerVoice()`. | Code reading:<br>• with a per-voice MIDI note override, the first of the two notes never gets a Note Off<br>• in record mode, an incoming MIDI note is echoed twice | Duplicate and hanging notes on external gear. |
| D5 | `seq_triggerVoice()` takes the p-locks and the MIDI velocity from the track's *current* step index. It does this also for events that belong to the previous or next step (shift or ratchet spill-over). | Code reading. | Shifted notes play with the wrong step's p-locks and the wrong MIDI velocity. |
| D6 | `seq_stepToSkip` is set when a recorded note is quantized forward, but it is only checked inside `#if 0` code. | Code reading. | A note recorded slightly early plays twice: once from the live hit and once from the sequencer. |
| D7 | Problems in slave mode. | Code reading:<br>• the tempo estimate is an integer BPM (`sync_calcBpm()` returns `uint16_t`)<br>• on swung steps the next clock can start the next step before all 24 pulses are processed, so the remaining pulses are dropped<br>• SPP is not implemented (`TODO` in `MidiParser.c`) | Wrong timing inside the step, dropped late notes, and the wrong position after locating in the DAW. |
| D8 | MIDI input threading. | Code reading:<br>• `uart_processMidi()` consumes the DIN receive FIFO from the TIM2 interrupt *and* from the main loop with no mutual exclusion<br>• USB MIDI is parsed in the main loop, one message per loop iteration, while DIN is parsed in the interrupt; both share the parser state and the voice state | Rare duplicated or garbled MIDI bytes; a duplicated clock byte moves the position. USB clock jitter depends on main-loop load, and USB clock stalls while presets are written to SD. |

Accepted for stage 1: audio onsets spread by about 1.7 ms today (64-sample block plus 0.25 ms systick
quantization). After stage 1 only the block quantization remains.

## 4. Architecture

```
 MIDI DIN / USB realtime ─┐   (timestamped in the ISR)
 clock in jack ───────────┤
                          ▼
        musical clock, 96 ticks per step ──► MIDI clock out, clock out jack (straight)
                          │
                          ▼
        event generator (one step lookahead: swing, shift, ratchet, probability)
                          │
                          ▼
        event queue ──► per-track choke ──► trigger callback (voice + one MIDI Note On)
                          │
                          ▼
        sequencer.c glue: patterns, song mode, pattern switching, LEDs, recording (API unchanged)
```

### 4.1 Modules

**`seq_core.c` / `seq_core.h`** (new, in `Core/Src|Inc/Soundengine/Sequencer/`)
- Plain C11. It includes only `<stdint.h>`, `<stdbool.h>`, `<string.h>` and `Preset/SeqStepTypes.h`,
  so no HAL, CMSIS, menu, LED or preset code.
- It owns the musical clock, the per-track step counters, the event generator, the event queue, the
  choke rule, note-off timing, roll events, sync-input processing (tempo estimate, interpolation,
  transport) and the recording phase.

**`Preset/SeqStepTypes.h`** (new)
- Holds the `Step` and `StepTiming` typedefs, moved out of `SeqStep.h` without any layout change.
- `SeqStep.h` includes it, because `SeqStep.h` itself includes `main.h` and therefore the HAL.

**`seq_platform.c` / `.h`** (new, firmware only)
- Enables the DWT cycle counter and provides the 64-bit `seqp_now()`.
- Provides one single-producer/single-consumer (SPSC) queue of timestamped sync events per source:
  DIN, USB and the clock jack.

**`sequencer.c`** (glue)
- Keeps the pattern data (`seq_patternSet`), the song data, the mutes and every public `seq_*`
  function. `menu.c`, `buttonHandler.c`, `CortexParser.c` and `presetManager.c` keep compiling
  unchanged, apart from the call sites listed in §8.
- Implements the core's callbacks: pattern-switch side effects, kit autoload, LEDs, MIDI sends and
  clock out.

**`clockSync.c` / `.h`** are removed. Their job moves into `seq_core` and `seq_platform`.

**`tests/`** (new): a host CMake project with the core tests (§9.1).

### 4.2 Core interface (sketch)

Names may still change in implementation, but the split of responsibilities below is binding.

```c
#define SEQC_TICKS_PER_STEP        96u   /* 384 PPQN */
#define SEQC_TICKS_PER_MIDI_CLOCK  16u
#define SEQC_TICKS_PER_PULSE        4u   /* 96 PPQN grid, clock out */

typedef uint64_t seqc_time_t;            /* CPU cycles at 216 MHz, monotonic */

typedef enum { SEQC_SYNC_INTERNAL = 0, SEQC_SYNC_ANALOG = 1, SEQC_SYNC_MIDI = 2 } seqc_sync_t;
typedef enum { SEQC_EV_CLOCK, SEQC_EV_START, SEQC_EV_CONTINUE, SEQC_EV_STOP } seqc_sync_ev_t;

typedef struct seqc_event {      /* 20 bytes */
    uint32_t tick;               /* absolute tick, compared wrap-safe */
    uint32_t absStep;            /* n */
    uint8_t  track, pattern, step, vel, note, flags;  /* flags: SEQC_EVF_ROLL */
    uint8_t  p1Nr, p1Val, p2Nr, p2Val;                /* copy of the step's p-locks */
} seqc_event_t;

typedef struct {
    void *ctx;
    /* pattern data, read only */
    bool        (*stepOn)(void *ctx, uint8_t pattern, uint8_t track, uint8_t step);
    const Step *(*step)(void *ctx, uint8_t pattern, uint8_t track, uint8_t step);
    StepTiming  (*timing)(void *ctx, uint8_t pattern, uint8_t track, uint8_t step);
    uint8_t     (*trackLength)(void *ctx, uint8_t pattern, uint8_t track); /* 0 means 16 */
    uint8_t     (*shuffle)(void *ctx);                                     /* PAR_SHUFFLE, 0..127 */
    bool        (*muted)(void *ctx, uint8_t track);
    uint8_t     (*rollRate)(void *ctx, uint8_t track);                     /* 0 = off, else steps */
    /* pattern sequencing; song mode stays in sequencer.c */
    uint8_t     (*activePattern)(void *ctx);
    uint8_t     (*peekNextPattern)(void *ctx);        /* pattern that will start at the next pattern end */
    bool        (*patternEnd)(void *ctx);             /* commit the switch; true if the pattern changed */
    uint8_t     (*songPatternAt)(void *ctx, uint8_t songPos); /* for SPP in song mode */
    bool        (*songMode)(void *ctx);
    /* outputs */
    void        (*trigger)(void *ctx, const seqc_event_t *ev);
    void        (*noteOff)(void *ctx, uint8_t track);
    void        (*stepBoundary)(void *ctx, uint32_t absStep);  /* LEDs, follow, MTC check, erase */
    void        (*midiClockOut)(void *ctx);
    void        (*clockOutPulse)(void *ctx, uint8_t pulseInStep); /* 0..23 */
    uint32_t    (*random)(void *ctx);
} seqc_host_t;

void     seqc_init(const seqc_host_t *host);
void     seqc_command(const seqc_cmd_t *cmd);   /* mailbox, any context (§4.3) */
void     seqc_syncEvent(seqc_sync_ev_t ev, seqc_time_t stamp);
void     seqc_process(seqc_time_t now);         /* advance, fire everything due up to now */
uint8_t  seqc_trackStep(uint8_t track);         /* track-local step index */
uint32_t seqc_absStep(void);
float    seqc_stepPhase(void);                  /* 0..1 within the current step */
float    seqc_currentBpm(void);                 /* master tempo or slave estimate */
void     seqc_skipOnce(uint8_t track, uint8_t step);
void     seqc_stats(seqc_stats_t *out);         /* queue depth max, overflow counts */
```

`seqc_cmd_t` carries one of the following:
- start, stop or continue
- reset to step 0
- set sync mode (0, 1 or 2, the same values as `seq_setExtSync()`)
- set tempo (BPM)
- set analog clock-in PPQ (4 or 24)
- song position (SPP, in 16ths)

### 4.3 Execution contexts

- **`seq_tick()` is the only context that calls `seqc_process()` and `seqc_syncEvent()`, and the only
  one that reads core state.** It keeps both of its call sites: the TIM2 interrupt (about 36 kHz) and
  `soundengine_process()` with the TIM2 interrupt disabled. On each call it:
  1. applies pending mailbox commands;
  2. drains the three sync queues, merged in timestamp order;
  3. calls `seqc_process(seqp_now())`;
  4. parses pending MIDI input (§7.2).
- **Interrupt handlers only produce sync events.**
  - USART3 RX handles DIN realtime bytes.
  - `USBD_CDC_DataOut` handles USB realtime messages.
  - The EXTI handler handles clock jack pulses.
  - Each writes `{event type, 32-bit cycle stamp}` into its own SPSC queue (64 entries).
- **Other contexts talk to the core only through the mailbox.** This applies to the main loop
  (buttons, menu) and to MIDI parsing:
  - Transport and settings commands go through `seqc_command()`. The mailbox has one slot per command
    type, and each slot is written with interrupts masked (PRIMASK) for those few instructions, so
    `seqc_command()` is safe to call from any context. A repeated command of the same type overwrites
    the previous one. Pending commands are applied at the start of the next `seq_tick()`.
  - Pattern edits keep writing `seq_patternSet` directly, as today. The core reads step data when it
    generates or regenerates a step (§6.2).
  - Mutes, shuffle, roll and pending-pattern setters keep writing their variables, as today. The core
    reads them through callbacks.

### 4.4 Time base

- **Clock source.** `DWT->CYCCNT` is enabled at boot: `TRCENA`, then unlock `DWT->LAR` (required on
  Cortex-M7), then `CYCCNTENA`. It counts at 216 MHz, derived from the 16 MHz HSE crystal (PLLM 8,
  PLLN 216, PLLP 2), so it has crystal accuracy.
- **64-bit extension.** `seqp_now()` extends the counter to 64 bits. It must be called at least every
  19.8 s; in practice it is called about 36 000 times per second.
- **Interrupt stamps.** Stamps from interrupt handlers are raw 32-bit values. The consumer unwraps
  them relative to `now`; a stamp is never more than a few ms old. A stamp later than `now`, possible
  through preemption, counts as `now`.
- **`systick_ticks` stays** for the UI timers. The sequencer no longer uses it.

## 5. Musical clock

### Position

- The position `pos` is counted in ticks as unsigned 64-bit fixed point (Q48.16). One step is 96
  ticks.

### Master mode

- **Advance rate.** The rate is BPM × 384 / 60 ticks per second. `pos` advances by
  `elapsed_cycles × rate`, computed in 64-bit integer arithmetic. The only error is the rounding of a
  single update: it doesn't accumulate.
- **Acceptance.** Over 60–200 BPM the tempo error is below 0.01% and the drift is below 1 tick over 10
  minutes.

### Slave modes (MIDI clock, or the clock in jack)

- **Ticks per clock.** One MIDI clock is 16 ticks. One jack pulse is 96 ticks at 4 PPQ or 16 ticks at
  24 PPQ (`PAR_PRESCALER_CLOCK_IN`, values `PRE_4_PPQ_IN` and `PRE_24_PPQ_IN`).
- **Tempo estimate.**
  - It is the mean of the last 24 clock intervals (MIDI or 24 PPQ) or the last 4 intervals (4 PPQ).
  - Intervals that deviate more than 50% from the current estimate are excluded from the mean.
  - The estimate is clamped to 20–300 BPM.
  - The first interval after a start uses the last known tempo as its initial estimate.
- **Between clocks.** `pos = posAtLastClock + elapsed × estimatedRate`, clamped to
  `posAtLastClock + ticksPerClock`, so the position never runs ahead of the incoming clock.
- **On each clock.** The target is `posAtLastClock + ticksPerClock`. If `pos` is behind the target, it
  jumps to the target, and everything due in between is processed in chronological order: late, but
  never dropped.
- **Dropout.** After 500 ms without a clock, the estimate resets and the position holds. The next clock
  resumes playback.
- **`seqc_currentBpm()`** (float) replaces `sync_calcBpm()`. It is rounded for display and used for LFO
  tempo sync, through `lfo_recalcSync()`, whenever the rounded value changes.

### Transport

**Internal start** (start button, or `seq_setRunning(1)` in internal sync):
- `pos` is set to 0 and step 0 is processed immediately, as today.
- MIDI Start is sent, as today.

**MIDI Start:**
- `pos` is set to 0 and the sequencer is armed.
- The next clock starts playback at step 0, as today.

**MIDI Continue:**
- The sequencer is armed, and the next clock resumes from the current `pos`.
- That `pos` was set by SPP, or kept from the last Stop.

**MIDI Stop:**
- Playback stops and `pos` is kept.

**SPP:**
- Accepted while stopped or armed. An SPP received while running is ignored; the MIDI specification
  only expects SPP while stopped.
- `pos` becomes SPP × 96 ticks (one SPP unit is one 16th, which is one step).
- Track indices and the song position follow §6.5.

**Clock jack reset input:**
- The meaning is unchanged: the position goes back to step 0 at the next pulse.

### Outputs

- **MIDI clock out** (internal sync only):
  - It is driven by a free-running tick counter that always advances at the master tempo, whether or
    not the transport runs, and sends one `0xF8` every 16 ticks. It therefore keeps running while
    stopped, as today.
  - On an internal start, the counter is re-phased to the transport: MIDI Start is sent first, then a
    clock at `pos` 0, then a clock every 16 ticks of `pos`. This matches fw 1.7, where Start was
    followed by a clock on the first pulse of step 0.
  - It is not sent in slave modes, as today. Thru routing is not affected.
- **Clock out jack** (while running): `trigger_clockTick(0, pulseInStep)` on every 4-tick pulse.
  - The existing output-PPQ prescaler is used unchanged (2, 4, 8, 16, 24 or 48 PPQN).
  - The output is straight regardless of swing.

## 6. Events

### 6.1 Generation

**What is generated.** For track `t` at absolute step `n` (counted from 0 at transport start or SPP),
the core reads pattern `p` and the track-local step `s`. A step is generated only if its main-step bit
is set. Mute does not affect generation; it is checked at fire time (§6.4).

**Probability.**
- `r = 1 + random() % 100` is drawn once per `(t, n)` and stored together with `(p, s)`.
- The step plays when `r ≤ Step.prob`.
- All ratchet hits of a step play together or not at all.

**Event timing.**
- **Ratchets.** `StepTiming.flam` 0..5 gives `k` ∈ {1, 2, 3, 4, 6, 8}. Any other value counts as 1.
- **Shift.** `shiftTicks = (StepTiming.shift − 7) × 12` for shift values 0..14. A value of 15 counts as
  0. The range is ±84 ticks, the existing ±7/8 of a step.
- **Swing.**
  - `swingTicks = round(shuffle × 64 / 127)` on odd `n`, otherwise 0.
  - At the maximum this is two thirds of a step, the same musical range as fw 1.7.
  - The parity of `n` is global, from transport start or SPP, as in fw 1.7 (`seq_masterStepCnt`).
- **Hit `j`** (0..k−1) is at tick `n·96 + swingTicks + shiftTicks + j·96/k`.

**Event payload.** Each event carries:
- `tick`, `absStep n`, `track`, `pattern`, `step`
- `vel = Step.volume & 0x7F`, `note = Step.note`
- a copy of the step's two p-locks (`param1Nr/Val`, `param2Nr/Val`)
- a roll flag

Because the payload is self-contained, an event plays correctly even after the pattern has switched
(D5).

**Skip marker (D6).** If a skip-once marker is set for `(t, s)`, that occurrence of the step is not
generated, and the marker is cleared.

### 6.2 Lookahead and regeneration

At the boundary of step `n` the core does two things for each track:

1. **Regenerate step `n`.**
   - Pending events of `(t, n)` with tick ≥ `n·96` are removed.
   - Step `n` is generated again from the current state, and only the events with tick ≥ `n·96` are
     kept.
   - The stored probability draw is reused if `(p, s)` is unchanged; otherwise a new draw is made.
2. **Speculatively generate step `n+1`.**
   - The earliest possible event is at `(n+1)·96 − 84`, which is after boundary `n`. One step of
     lookahead is therefore enough.

As a result:
- Edits, pattern changes, track length changes, erase and mutes made before a step's boundary apply to
  that step.
- Only notes shifted early, which play before their own boundary, use the state from one step earlier.

**Order at boundary `n`**, the same order as `seq_nextStep()` in fw 1.7:
1. Advance the absolute step and every track-local index (§6.5).
2. If the master track wrapped, call `patternEnd()`. If the pattern changed, reset all track indices to
   0.
3. Call `stepBoundary(n)`: LEDs, follow-bar, MTC check, and erase on the active track.
4. Add the roll events (§6.6).
5. Regenerate step `n`.
6. Speculatively generate step `n+1`.

### 6.3 Processing order

`seqc_process(now)` first moves `pos` to its new value. It then repeatedly processes the earliest due
item, until nothing due at or before `pos` is left. The candidates are:
- the next step boundary
- the earliest queued event
- pending note-off times
- the next clock-out pulse
- the next MIDI clock, in internal sync

At equal ticks the order is: step boundary, note-offs, note events, clock outputs.

### 6.4 Firing and choke

**Mutes.** An event of a muted track is dropped when it fires, so muting also silences pending ratchet
hits.

**Choke** (per track):
- Firing an event of `(t, n)` removes every pending event of track `t` with a step lower than `n`.
- If several events of one track are due at the same tick, only the one with the highest `n` fires.
  If they also share the same `n`, only one fires, and a note event wins over a roll event. In fw 1.7
  both retriggered the voice in the same pulse, which sounded the same.
- This is the per-track form of fw 1.7's "next > current > prev" priority, which fixes D2.

**Trigger callback** (`sequencer.c`):
1. Sends a MIDI Note Off for the track's previous note, if one is still sounding, as today.
2. Applies the event's own p-lock copy to the automation nodes. This replaces
   `seq_parseAutomationNodes()` on the current step index (D5).
3. Triggers the voice through `voiceControl_noteOn()`.
4. Sends exactly one MIDI Note On (D4):
   - on the voice's channel
   - with the note override applied when `PAR_MIDI_NOTE1+t` ≠ 0
   - with the event's velocity

**Note-off.** Each track has one pending MIDI note-off time, set to the fire tick + 4 ticks, matching
fw 1.7's "one pulse later". A newer trigger on the track sends the pending note-off first and restarts
the timer.

**Open/closed hi-hat choke** stays in the voice (`HiHat_trigger`).

### 6.5 Track counters, pattern ends and song position

**Track counters.**
- At each boundary every track-local index advances by one.
- It wraps to 0 when it reaches the track length (length 0 means 16) or 64, as today.
- `seqc_trackStep()` replaces direct reads of `seq_stepIndex[]` in sequencer, recording and LED code.

**Pattern end.**
- The master track is the longest track of the active pattern, as in `seq_getLongestTrack()`.
- When the master track wraps at boundary `n`, the core calls `patternEnd()`.
- `sequencer.c` then picks the next pattern (song mode or pending pattern). It applies the same side
  effects as `seq_setPendingPatternActive()` does today: backup buffer copy, kit autoload, Program
  Change, LEDs and all notes off.
- If the pattern changed, or a reload was requested, all track indices restart at 0, as today. If the
  same pattern continues, the indices are not reset, which keeps today's free-running polymeter.

**First step of the next pattern.**
- It is generated speculatively with `peekNextPattern()` and the indices that pattern implies.
- Regeneration at the boundary (§6.2) corrects any mismatch.

**Late events from the previous pattern** still fire after the switch, because each event carries its
own data (§6.1). The kit of the new pattern loads at the boundary, as today.

**SPP in pattern mode.** Track index = `SPP mod length(t)`, which corresponds to a pattern that has
been running since step 0.

**SPP in song mode.**
- The core walks the song list from the start. Each entry lasts the master length of its pattern.
- Consecutive identical entries count as one continuous run without an index reset, matching today's
  behaviour.
- The core activates the entry that contains SPP, with the normal pattern-change side effects, and sets
  each track index to `offset mod length(t)`.
- A `SONG_LOOP` marker wraps the walk back to the first entry.
- If SPP lies beyond a `SONG_END` marker, the position is set to the end of the last entry. The
  transport then stops at the next pattern end, as fw 1.7 does when it reaches `SONG_END`.

### 6.6 Roll

- At boundary `n`, every track with roll active and `s % rollRate == 0` gets a roll event at
  `n·96 + swingTicks`, with velocity `ROLL_VOLUME` and the step's note.
- The roll hit is recorded through `seq_addNote()`, as today.
- Roll rates are unchanged (1/1 to 1/16), and one-shot mode stays in `seq_setRoll()`.

### 6.7 Recording

- `seq_getElapsedStepTimePercent()` returns `seqc_stepPhase() × 100`. The quantization rule is
  unchanged: at 50% or more of the step the note goes to the next step.
- When `seq_addNote()` moves a note to the next step while running, it calls `seqc_skipOnce(t, s)`
  (D6).
- `seq_recordAutomation()` and the erase mode use `seqc_trackStep()`. Erase is applied in the
  `stepBoundary` callback before regeneration, so an erased step does not play.

## 7. MIDI changes

### 7.1 Note output (D4)

- `voiceControl_noteOn()` stops sending MIDI. It keeps the voice trigger, the `active_voices` flags
  and the front-panel note message.
- Sequencer triggers send exactly one Note On, as described in §6.4.
- An incoming MIDI note that triggers a voice is echoed exactly once on the voice's channel, in both
  play and record mode. In fw 1.7 it was echoed once in play mode and twice in record mode. The echo is
  sent from the MIDI note handler in `MidiParser.c`.

### 7.2 Input threading (D8)

- **DIN realtime bytes** (`F8`, `FA`, `FB`, `FC`) are intercepted in the USART3 RX interrupt handler.
  - They are timestamped into the DIN sync queue.
  - They are also still written to the RX FIFO, so routing and thru keep working.
  - The parser no longer calls sync functions for these bytes.
- **USB realtime messages** (CIN `0xF`, single byte) get the same treatment in `USBD_CDC_DataOut`:
  they go to the USB sync queue and to the normal USB message queue.
- **Clock jack pulses** go from the EXTI handler into the jack sync queue. This replaces
  `trigger_clockReceived` and `seq_triggerNextMasterStep()`.
- **SPP** (`F2`) is not time-critical. It stays on the normal path and becomes a mailbox command.
- **DIN byte parsing and USB message parsing both run only in the `seq_tick()` context**, with a budget
  per call: at most 32 DIN bytes and 8 USB messages.
  - The calls to `uart_processMidi()` and `usb_getMidi()` / `midiParser_parseMidiMessage()` are
    removed from the main loop and from `soundengine_process()`.
  - Incoming notes then trigger voices in the same context as the audio renderer, which also removes
    the main-loop race on voice state.
- **Sources in MIDI sync mode.** Clocks from DIN and from USB are both accepted, as today; the RX
  filter `MIDI_FILTER_SYSTEM` still applies. Sending clock from both sources at once is a
  configuration error, the same as in fw 1.7.

## 8. Changes outside the core

| File | Change |
|---|---|
| `Core/Src/Soundengine/Sequencer/sequencer.c`, `Core/Inc/Soundengine/Sequencer/sequencer.h` | Glue around `seq_core`.<br>• The pulse machine is removed: `seq_processFlam`, `seq_getShiftFlamPattern`, `seq_calcDeltaT`.<br>• `seq_nextStep` becomes the `stepBoundary` / `patternEnd` callbacks.<br>• The public API is kept. |
| `Core/Src/Soundengine/Sequencer/clockSync.c`, `Core/Inc/Soundengine/Sequencer/clockSync.h` | Removed. |
| `Core/Src/Soundengine/Sequencer/seq_core.c`, `seq_platform.c` and their headers | New. |
| `Core/Inc/Preset/SeqStepTypes.h`, `Core/Inc/Preset/SeqStep.h` | Typedefs moved, layout unchanged. |
| `Core/Src/Soundengine/MIDI/MidiVoiceControl.c` | No MIDI send (§7.1). |
| `Core/Src/Soundengine/MIDI/MidiParser.c` | • Note echo once.<br>• Realtime messages no longer drive sync.<br>• SPP becomes a mailbox command. |
| `Core/Src/Soundengine/MIDI/Uart.c` | Realtime intercept in the RX interrupt handler. Parsing only from `seq_tick()`. |
| `Middlewares/JS/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c` | USB realtime intercept. |
| `Core/Src/Soundengine/TriggerOut.c` | • Clock in pulses go to the jack sync queue.<br>• Clock out is driven by `clockOutPulse`. |
| `Core/Src/main.c` | • DWT init.<br>• MIDI calls removed from the main loop.<br>• New splash. |
| `Core/Src/Soundengine/Soundengine.c` | `uart_processMidi()` call removed. |
| `Core/Src/stm32f7xx_it.c` | • CPU meter around the TIM2 work.<br>• The `uart_processMidi()` call in `TIM2_IRQHandler` moves into `seq_tick()`. |
| `Core/Src/menu/menu.c`, `Core/Inc/menu/menuPages.h`, `Core/Inc/menu/MenuText.h` | CPU meter sub-page (§9.3). |
| `STM32F765VGTX_FLASH.ld` | Image size assertion (§9.4). |
| `CMakeLists.txt`, `Core/CMakeLists.txt` | Name, version, image name, new sources. |
| `tests/`, `.github/workflows/build.yml` | New. |
| `README.md` | LICHOMAT section. |

## 9. Tooling

### 9.1 Host tests

- `tests/CMakeLists.txt` builds with the host compiler using C11 and `-Wall -Wextra -Werror`.
- It compiles `seq_core.c` together with a fake host:
  - pattern data stored in plain arrays
  - a seeded RNG
  - a recorder that logs every callback with its tick and time
- Tests run through `ctest`. Instead of a third-party framework there is a small assert/runner header
  of about 50 lines.
- The fw 1.7 replica used in the analysis is committed as `tests/reference/fw17_replica.c`. It is
  derived from `sequencer.c`, keeps the original Erica Synths copyright notice, and is used by T20.
- The test list is in §13.1.

### 9.2 CI

`.github/workflows/build.yml` runs on push and on pull_request, with two jobs:

1. **Host tests** on `ubuntu-latest`.
2. **Firmware build.**
   - Uses Arm GNU Toolchain 14.3.rel1 for x86_64 Linux, cached.
   - Configures with the same settings as the `release` preset.
   - Uploads the `.img`, `.elf` and `.map` as artifacts.
   - Writes the linker's memory usage to the job summary.

### 9.3 CPU meter

**What is measured.**
- DWT cycles for the work inside `TIM2_IRQHandler`: `seq_tick()`, the block render and MIDI
  parsing.
- The same work in the `soundengine_process()` path.

**How it is reported.**
- Per rendered block, the measured cycles are compared with the block budget:
  64 / 44 140 Hz × 216 MHz ≈ 313 186 cycles.
- The meter shows the average over 1 s and the peak, held for 1 s, both in percent.
- It also shows the event queue's maximum depth and the overflow counts.

**Where it is shown.**
- A new third sub-page of the global menu: SHIFT + LOAD/SAVE as today, then page forward twice.
- The page is drawn by its own display routine. It adds no entries to `activeParameters`, so the
  globals file format does not change. It is read-only and never saved.

### 9.4 Linker check

- Add `ASSERT(_siitcm + SIZEOF(.itcm_text) <= ORIGIN(FLASH) + LENGTH(FLASH), "image too large for FLASH")`.
- Without it, the `.dtcm` and `.itcm_text` load images (placed with `AT(addr)`) are not checked
  against FLASH.
- The produced `.bin` must stay byte-identical to the build without the assertion.

### 9.5 Branding

- **CMake.**
  - A cache variable `LICHOMAT_NAME` defaults to `LICHOMAT`.
  - `project(VERSION 0.1)`, with two components like upstream's `1.7`. The same string is used on the
    splash screen, in the image name and in the release tag `v0.1`.
  - The upstream base is recorded as `LXR_BASE_VERSION 1.7`.
- **Splash screen (16 characters per line).**
  - Line 1: `LICHOMAT`.
  - Line 2: `v0.1 unofficial`. This marks the modified version, as NOTICE.md §7(c) requires.
- **Update file.**
  - It is named `LXR_V2_LICHOMAT_0.1.img`, because the bootloader only accepts files whose name starts
    with `LXR` and ends with `.img`. This prefix is a technical file name, not branding.
  - The image magic stays `LXRV2IMG`.
- **README.** A short LICHOMAT section describes it as "unofficial custom firmware for the Erica Synths
  LXR-02", explains how to install it and how to return to the official firmware.
- **Source headers.**
  - New source files carry `Copyright (C) 2026 Tomasz Krzywicki`, `SPDX-License-Identifier: GPL-3.0-only`
    and the same NOTICE.md reference as the existing files.
  - Modified files keep their existing headers.

## 10. Resource budget

| Resource | Change |
|---|---|
| RAM | Event queue: 256 × 20 B = 5 KB.<br>Sync queues: 3 × 64 × 8 B = 1.5 KB.<br>Other state: under 1 KB.<br>Total ≈ 7.5 KB of the 119 KB free. |
| ITCM (1.35 KB free) | `seq_nextStep` (1 096 B) and `seq_setPendingPatternActive` leave ITCM.<br>`seq_core` runs from flash through the cache.<br>Net change ≤ 0. |
| Flash | About +10–15 KB, out of 724 KB free. |
| CPU | Generation handles at most 7 tracks × 8 events per step, which is every 115 ms at 130 BPM.<br>`seqc_process()` only does work for items that are due.<br>The real figure is measured with the CPU meter (H12). |

## 11. Error handling

| Situation | Behaviour |
|---|---|
| Event queue full | The new event is rejected and counted; the count is shown on the CPU page. |
| Sync queue full | The new sync event is rejected and counted. |
| Clock dropout in slave mode | The position holds at the next-clock boundary. After 500 ms the estimate resets, and the next clock resumes playback. |
| Clock intervals that don't fit | Intervals more than ±50% off the estimate are excluded from it. The tempo is clamped to 20–300 BPM. The position still follows every clock. |
| Stamp later than `now` | Treated as `now`. |
| Counter wrap | Time is 64-bit and event ticks are compared wrap-safe. |

## 12. Compatibility and intended behaviour changes

- **Files.** All formats are unchanged: PatV03 patterns, kits, globals, projects and songs.
- **Intended changes against fw 1.7:**
  - **Swing:**
    - Shuffle moves whole steps, so patterns that combine shuffle with ratchets or shift will sound
      different.
    - MIDI clock out and clock out are straight while shuffle is on.
  - **Timing:**
    - Choke works per track.
    - Shifted notes use their own step's p-locks and MIDI velocity.
    - Notes recorded slightly early don't double.
    - The master tempo is exact; fw 1.7 played up to 0.2% fast.
    - In slave mode late notes are no longer dropped, and SPP is supported.
  - **MIDI:**
    - Exactly one Note On per trigger and no hanging notes.
    - An incoming MIDI note is echoed once in record mode as well.
- **User interface.** Unchanged, except for the splash screen and the CPU sub-page.

## 13. Verification

### 13.1 Host tests (must pass in CI)

| ID | Covers |
|---|---|
| T1 | Tempo accuracy, 60–200 BPM: error under 0.01% and drift under 1 tick over 10 min. |
| T2 | Straight grid: one hit per active step at `n·96`. |
| T3 | Shuffle: every hit of a swung step, ratchets included, is shifted by exactly `swingTicks`. Non-swung steps are unaffected. |
| T4 | Shift −7..+7 on every ratchet type, including a negative shift on step 0 of the next pattern. |
| T5 | MIDI clock out has a constant 16-tick spacing with shuffle at 0, 64 and 127. |
| T6 | Clock-out pulses come every 4 ticks and each PPQ prescaler divides them correctly. |
| T7 | Per-track choke: both replica scenarios from §3 D2, plus the same-tick priority rule. |
| T8 | Probability: deterministic with a seeded RNG, and ratchet hits play all together or not at all. |
| T9 | Muting during a ratchet drops the remaining hits. |
| T10 | Polymeter wrap per track. The pattern ends on the master track and indices reset only when the pattern changes. |
| T11 | Pattern change with early notes of the next pattern. Regeneration on a mismatch, for example a pending pattern changed during the last step. |
| T12 | A step edit or erase before the boundary applies to that step. |
| T13 | Roll at every rate, with swing. |
| T14 | Recording phase and the skip-once rule (D6). |
| T15 | Note-off 4 ticks after the trigger, and before a retrigger. |
| T16 | Slave mode:<br>• steady clock<br>• ±1 ms jitter (USB-like)<br>• tempo ramp from 120 to 140 BPM<br>• early and late clocks, with no event lost and the order kept<br>• dropout and resume |
| T17 | Start, Continue, Stop and SPP, in pattern mode and in song mode (identical consecutive entries included). |
| T18 | Clock in jack at 4 PPQ and 24 PPQ. |
| T19 | Queue overflow is rejected and counted, and the core keeps running. |
| T20 | Equivalence: with shuffle 0, every hit lands on the same 1/24-step pulse as in the fw 1.7 replica. The tempo-error difference is allowed. |

### 13.2 Hardware checklist (performed on an LXR-02, installed from SD card)

| ID | Check |
|---|---|
| H1 | Boot shows the LICHOMAT splash with `v0.1 unofficial`. |
| H2 | An existing 1.7 project loads, and its kits, patterns and songs play. |
| H3 | Straight 16ths recorded into a DAW line up with the grid. The spread is about one audio block. |
| H4 | Shuffle with ratchets and shift sounds as designed: whole steps move. |
| H5 | MIDI clock out to an external device with shuffle on: the device shows a stable tempo. |
| H6 | Slave over DIN (DAW or hardware sequencer): playback is tight, and Start, Stop and Continue work. |
| H7 | Slave over USB from a DAW:<br>• locate followed by Continue (SPP)<br>• Start, Stop and Continue<br>• stays in sync while a preset is being saved to SD |
| H8 | The choke scenario from D2 on real voices. |
| H9 | Pattern change with shifted notes on the first and last steps. |
| H10 | Live recording from pads or MIDI: no doubled notes. |
| H11 | MIDI out to a synth with a note override: no hanging notes, and one Note On per hit. |
| H12 | CPU meter readings with a heavy kit and the delay effect. The values are recorded as input for stage 3. |
| H13 | Rollback: the official 1.7 image installs and runs. |

## 14. Rollout

- **Branch and commits.** Work happens on `timing-core` in small commits. The core is developed test
  first, following the implementation plan.
- **Release.** Release `v0.1` is a GitHub release on the fork, using the `.img` built by CI. Its notes
  list D1–D8 and the behaviour changes from §12.
- **Pushing.** Nothing is pushed to GitHub without the owner's go-ahead.

## 15. Later stages (context only, not part of this spec)

- **Stage 2** builds on `seq_core`:
  - per-track swing and groove templates
  - track delay in ms
  - full-resolution microtiming, using a new step format, PatV04
  - humanize and recording that keeps microtiming
  - trig conditions
  - per-track clock division
  - rotation, fill and mute scenes
  - DJ filter
  - roll at 1/32 and triplets
- **Stage 3** covers reverb (the existing, currently disabled code), tempo-synced echo and "ambient".
  It will be designed once the CPU readings from H12 are available.
- **Sample-accurate triggering** is a separate step after stage 2, again based on CPU data.
