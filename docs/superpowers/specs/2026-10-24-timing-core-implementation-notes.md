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

## N12. Execution context (correction to the spec and to N10)

The spec assumed `seq_tick()` runs from the TIM2 interrupt at about 36 kHz. In fw 1.7 that is only
true while the SD card is busy:

- `config.h` sets `CALC_SAMPLES_IN_INTERRUPT 1` and `USE_INTERRUPT_ONLY_DURING_SAVE_PROJ 1`, so
  `main.c` does not start TIM2. Only the preset load and save paths start and stop it, so that audio
  keeps playing while the main loop waits on the card.
- Normally `seq_tick()` and `soundengine_calcNextSampleBlock()` run from `soundengine_process()`, which
  the main loop calls 3 times per pass. `soundengine_process()` masks the TIM2 update interrupt around
  its own `seq_tick()` and render.
- The kit autoload in `seq_setPendingPatternActive()` calls `preset_loadDrumsetRam()`
  (sequencer.c:651), which calls `soundengine_process()` again. This nests `seq_tick()`. fw 1.7 has
  no guard against it. The glue's `seq_tick()` gets a nesting guard: a nested call returns at once,
  so the mailbox, the queue drain, `seqc_process()` and MIDI parsing never run twice at the same time.
  The render after it still runs.

What this changes:

- **N10:** `seqc_process()` runs at least once per audio block (64 samples, about 1.45 ms), and
  usually several times, not at a fixed 36 kHz. The `nextDue` early exit stays.
- **Trigger timing:** a voice starts at the beginning of the first block rendered after its event is
  due. Trigger times are therefore quantized to the audio block, with up to 1.45 ms of jitter, as in
  fw 1.7. At 130 BPM a core tick is about 1.2 ms, finer than the block. Sample-accurate triggering
  (rendering a block in parts split at event times) is a later task, E6.10.
- **D8:** the race is real only while TIM2 runs (SD load or save). `uart_processMidi()` at the end of
  `soundengine_process()` runs outside the masked region, and the TIM2 handler also calls it.
- **CPU meter (E3.3):** measure the render in both places it runs, `soundengine_process()` and
  `TIM2_IRQHandler()`.

## N13. Interface decisions taken in the API design

`Core/Inc/Soundengine/Sequencer/seq_core.h`, as written for the plan, is the reference for these.
They extend N2, N4, N5 and N7:

1. `transport(ctx, seqc_transport_t)` replaces the `bool` in N5. Its values are STOP, START, CONTINUE,
   ARM and REWIND, always reported in that order. START and CONTINUE come before any other callback of
   the first step, and N5's "running" means START or CONTINUE.
2. The sync event types include `SEQC_EV_PULSE`, `SEQC_EV_RESET_ON` and `SEQC_EV_RESET_OFF`. The type
   names the source, so the core applies N4's gating by sync mode itself. `EXTI9_5` pushes
   RESET_ON and RESET_OFF into the jack queue instead of calling `seq_setRunning()` and
   `seq_resetPosition()`, which keeps the run gate in order with the pulses.
3. Direct functions replace the spec's `seqc_command()`: `seqc_start`, `seqc_stop`, `seqc_rewind`,
   `seqc_setSyncMode`, `seqc_setTempo(float)`, `seqc_setAnalogPpq` and `seqc_songPosition`. A local stop
   is stop followed by rewind. The glue also rewinds after a pattern switch while stopped.
4. SPP seeks as soon as it arrives (stopped or armed). The state at Continue is as N4 describes.
5. `seqc_stepPhase()` is swing-aware: it reaches 0.5 halfway between the swung grid times of steps n
   and n+1, so a note played on the swung hit is recorded on its own step.
6. Roll events ignore mutes, as in fw 1.7.
7. `seqc_event_t` stays 24 bytes. The spare bytes hold the ratchet hit index and a reserved 0.
8. A stop keeps the event queue for a later Continue. Pending note-offs are sent at stop and at
   rewind. Rewind and seek clear the queue.
9. Mutating calls made from inside a host callback are ignored and counted in `stats.reentries`.
   Queries and `seqc_skipOnce()` are allowed. The glue's nesting guard (N12) stays the first defence.
10. MIDI Start in sync mode 1 resets the clock count, so the next jack pulse starts playback (N4 taken
    literally; fw 1.7 waited for its own 6-pulse grid at 24 PPQ).
11. **Song end:**
    - `patternEnd()` leaves the pending pattern equal to the active one.
    - The REWIND handler resets the song pointer, as `seq_resetPosition()` does.
    - The next start plays entry 0 exactly once.
    - fw 1.7 set pending to `song[0]` with pointer 0, which skips entry 0 when the last entry equals
      `song[0]`.

## N14. SPP travels in the sync queues

If SPP were parsed in step 4 of `seq_tick()` while Continue and Clock are drained in step 2, a
Continue and its first clock drained in the same pass would start the transport before the SPP is
seen. So:

- The USART3 RX interrupt and `USBD_CDC_DataOut()` also intercept Song Position Pointer.
  - DIN uses a small state machine for `F2 lsb msb`; realtime bytes interleaved in the message are
    handled as usual.
  - USB needs only the CIN 0x3 packet.
- The SPP is pushed with its stamp into the same queue as Start, Continue and Clock from that source.
  A queue element becomes `{uint8_t type; uint16_t value; uint32_t stamp}` (8 bytes).
- The drain calls `seqc_songPosition()` at the SPP's place in the merged order.
- The parser no longer acts on SPP itself.

## N15. Dropout threshold

A fixed 500 ms dropout cuts a 4 PPQ jack clock below 30 BPM, where the pulses are more than 500 ms
apart. The dropout time is `max(500 ms, 2 × the expected clock interval)`:

- With an estimate, the expected interval comes from the estimate.
- Without one, it is the interval at 20 BPM, the lowest tempo the estimate accepts. At 4 PPQ that is
  750 ms, so the dropout is 1.5 s, and a slow clock can still build its first estimate.
