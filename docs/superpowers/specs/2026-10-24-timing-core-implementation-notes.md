# LICHOMAT stage 1: implementation notes

These notes go with `2026-10-23-timing-core-design.md` (the spec). They record decisions taken while
designing the code, mostly where the spec left room or where reading more of fw 1.7 showed a detail
the spec had wrong. **Where the two disagree, these notes win.** The deviations are also listed in
the stage 1 report.

## N1. Position arithmetic: double, from an anchor

The spec asked for Q48.16 fixed point. The STM32F765 has a hardware double-precision FPU
(`-mfpu=fpv5-d16`), so the position is a `double` in ticks instead, computed from an anchor:

- Internal mode: `pos(now) = anchorPos + (double)(now − anchorTime) × ticksPerCycle`, where
  `ticksPerCycle = bpm × 384 / 60 / cyclesPerSecond`.
- The anchor moves only on a tempo change, a start or a slave clock. Because the position is never
  accumulated step by step, it has no drift.
- A double holds integer ticks exactly far beyond any session length (2^53 ticks).
- Test T1 is unchanged: tempo error below 0.01%, drift below 1 tick in 10 min.

## N2. Event queue

- Event ticks are `int64_t`, which removes the wrap concerns. An event is 24 bytes, so 256 events take
  6 KB.
- The array is kept sorted so that the next event to fire is at the end: `pop` is `count--`, insert is
  a binary search plus `memmove`.
- Ordering key, earliest first:
  1. tick ascending;
  2. then absStep descending, so the newest step of a track fires first;
  3. then a note event before a roll event;
  4. then track ascending.
- Choke on fire (per track): remove pending events of the same track with a lower absStep.
- Same-tick rule: for each track the core remembers `(lastFiredTick, lastFiredStep)`. An event of
  that track with the same tick and an absStep ≤ `lastFiredStep` is dropped.
- When the queue is full, the new event is rejected and `stats.queueOverflow` is incremented.

## N3. Early and late parts instead of removing and regenerating

The spec regenerates step `n` at its boundary by removing its pending on-time events and generating
them again. This is equivalent and simpler:

- At boundary `n` the core generates the **late part** of step `n`: hits with tick ≥ `n·96`.
- At the same boundary it generates the **early part** of step `n+1`: hits with tick < `(n+1)·96`,
  which can only come from a negative shift.
- No event is ever generated twice, so nothing needs removing.
- The probability draw for `(track, n)` is stored together with `(pattern, step)` when the early part
  is generated, and reused for the late part if they match. Otherwise a new draw is made.
- Each track keeps two slots, for `n` and `n+1`.

## N4. Starting, and the first boundary

**Internal start** (button, or `seq_setRunning(1)` in internal sync):
- Every track index starts at −1 and absStep at −1.
- The first boundary at pos 0 runs fw 1.7's master-wrap logic: `−1 + 1 = 0` counts as a wrap, so
  `patternEnd()` is called at step 0. In song mode this advances the song pointer from −1 to entry 0,
  exactly as fw 1.7 does.
- Early hits of step 0 (tick < 0) are dropped, as in fw 1.7, which never played them at start.

**MIDI Start:**
- `pos = −16` and the transport is armed.
- The first clock brings pos to 0, and step 0 runs through the same first-boundary logic.

**MIDI Continue:**
- The transport is armed.
- With an SPP received since the last stop, `posAtLastClock = SPP·96 − 16` and the track indices come
  from `seekPosition()` (N6).
- Without an SPP, the transport resumes from the kept position: indices, absStep and pos are
  unchanged.
- The next clock adds +16.

**MIDI Stop:**
- Stops and keeps everything for a later Continue.
- fw 1.7 restarted from step 0 on Continue (`seq_setRunning(1)` reset the indices). The spec asks for
  resuming, and that is what we do. This is listed as an intended change.

**Local start in a slave mode:**
- The core counts incoming clocks while stopped, in `clockCount`. MIDI Start and SPP reset the count.
- A local start arms the transport until the next clock with `clockCount % clocksPerStep == 0`, then
  starts at step 0.
- `clocksPerStep` is 6 for MIDI and for jack 24 PPQ, and 1 for jack 4 PPQ.
- This matches fw 1.7, where `sync_clockCnt` aligned a local start to the master's 16th grid.

**Local stop** (button): stop and reset to the start, as fw 1.7 `FRONT_SEQ_RUN_STOP 0` →
`seq_resetPosition()` + `seq_setRunning(0)`.

**Clock jack reset input (correction to the spec):**
- In fw 1.7 this input is a *run gate*, not a reset pulse (`EXTI9_5_IRQHandler`):
  - pin active → stop and reset;
  - pin released → start, armed until the next jack pulse.
- This is kept exactly. It acts only in sync mode 1.

**What each sync mode accepts** (as in fw 1.7):
- MIDI Start, Stop and Continue in sync modes 1 and 2 (`seq_getExtSync() != 0`).
- MIDI clocks only in mode 2.
- Jack pulses only in mode 1.

## N5. Host callbacks (final list)

```c
bool        stepOn(ctx, pattern, track, step);        // main-step bit
const Step *step(ctx, pattern, track, step);
StepTiming  timing(ctx, pattern, track, step);
uint8_t     trackLength(ctx, pattern, track);         // raw value; 0 means 16
uint8_t     shuffle(ctx);                             // PAR_SHUFFLE 0..127
bool        muted(ctx, track);
uint8_t     rollRate(ctx, track);                     // 0 = off, else 16/8/4/2/1 steps
uint8_t     activePattern(ctx);
seqc_pat_t  patternEnd(ctx);                          // SEQC_PAT_SAME / SEQC_PAT_CHANGED / SEQC_PAT_STOP (song end)
bool        peekNextPattern(ctx, uint8_t *pattern);   // returns whether indices restart; 0xFF = song ends
uint32_t    seekPosition(ctx, uint32_t absStep);      // SPP: activate the pattern at absStep (song mode),
                                                      // return steps since that pattern run started
void        trigger(ctx, const seqc_event_t *ev);
void        noteOff(ctx, track);
void        stepBoundary(ctx, absStep);               // LEDs, follow-bar, chase light, erase, MTC check
void        tempoStep(ctx);                           // every step of the tempo, also while stopped (LED blink)
void        midiClockOut(ctx);
void        clockOutPulse(ctx, pulseInStep);
void        transport(ctx, bool running);             // glue sends MIDI Start/Stop and trigger_reset()
uint32_t    random(ctx);
```

**Master wrap** mirrors fw 1.7:
- The longest track is chosen by its raw length (so a 0 loses ties).
- Its effective length is 16 when the raw length is 0.
- The track wraps when `((idx+1) & 0x3f) == 0 || idx+1 ≥ len`.

## N6. SPP seek

`seekPosition(absStep)` lives in the glue, because song data does.

- **Pattern mode:** returns `absStep`. The pattern has been looping since step 0, and same-pattern
  repeats never reset indices.
- **Song mode:**
  - It walks `seq_activeSong`, each entry lasting the master length of its pattern.
  - Consecutive identical entries form one run without an index reset.
  - `SONG_LOOP` wraps the walk back to the first entry. Past `SONG_END` the position is clamped to the
    end of the last entry.
  - It activates the target entry with the normal pattern-change side effects, sets
    `seq_songPositionPointer`, and returns the steps since that run started.
- **The core** sets each track index to `steps mod len(t)` and absStep to the SPP.

## N7. Mailbox in the glue, not the core

`seq_core` is single-context and has no CMSIS dependency. Cross-context requests (start, stop, reset,
sync mode, tempo, analog PPQ) are posted with `seq_post*()` in `sequencer.c`:
- one slot per command type;
- each slot written with interrupts masked;
- slots applied at the top of `seq_tick()`.

SPP is parsed in the `seq_tick()` context after D8, so it calls the core directly.

`seq_isRunning()` keeps fw 1.7's meaning: it is set as soon as a start is requested, armed included,
and cleared on stop.

## N8. Clock outputs and the tempo tick

- **MIDI clock out** (internal sync only):
  - It runs on a free-running timeline that is always active.
  - On an internal start it is re-phased: the glue's `transport(true)` sends MIDI Start, then a clock
    is sent at pos 0 and every 16 ticks after that.
- **Clock out jack:** while running, every 4 ticks the core calls `clockOutPulse()`, and the glue
  calls `trigger_clockTick(0, 0)`. fw 1.7 ignores both arguments and counts the pulses itself.
  `trigger_reset(1)` on start keeps the phase.
- **`tempoStep()`:**
  - Internal mode: every 96 ticks of the free-running timeline.
  - Slave modes: every `clocksPerStep` clocks.
  - It fires whether or not the transport runs, because fw 1.7 calls `led_blinkHandler()` from
    `seq_nextStep()` before its running check, so the LEDs blink with the tempo even while stopped.

## N9. Slave tempo estimate

- **Intervals:** the mean of the last 24 clock intervals (24 PPQ sources) or the last 4 (jack 4 PPQ).
  An interval that deviates more than ±50% from the current estimate is excluded, once an estimate
  exists.
- **Clamp:** the tempo is limited to 20–300 BPM.
- **Dropout:** after 500 ms without a clock the estimate is cleared; the next interval measured after
  resuming rebuilds it.
- **Position between clocks:** interpolation is clamped to `posAtLastClock + ticksPerClock`, the clamp
  included, and the position never decreases.
- **Display tempo:** `seq_getBpm()` returns the rounded estimate, and `lfo_recalcSync()` is called when
  the rounded value changes.

## N10. CPU: early exit

`seqc_process(now)` is called about 36 000 times per second.

- The core keeps a `nextDue` time in cycles: the earliest of the next boundary, event, note-off,
  pulse, MIDI clock or tempo step, converted with the current rate.
- A call with `now < nextDue` returns after one 64-bit compare.
- In slave mode `nextDue` is also capped at the clamp time and at the dropout timeout.

## N11. Firmware specifics

- **ITCM:** `IN_ITCM_RAM` comes off `seq_nextStep`, `seq_setPendingPatternActive` and `seq_setMute`.
  `seq_core.c` runs from flash.
- **Roll notes** are recorded at the event's own step with a new `seq_recordNoteAtStep()`, factored out
  of `seq_addNote()`. In fw 1.7 they effectively landed on the current step at phase 0.
- **One-shot roll** and any immediate trigger use `seq_triggerVoice()`, which keeps the current-step
  p-locks and now sends exactly one MIDI Note On.
- **MIDI note echo:**
  - `midiParser_noteOn()` sends the echo once, in both play and record mode.
  - In record mode it keeps the explicit Note Off and then Note On it already sends.
  - The duplicate from `voiceControl_noteOn()` goes away.
