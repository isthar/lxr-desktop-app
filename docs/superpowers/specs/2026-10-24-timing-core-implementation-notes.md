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
   ARM and REWIND. When one change reports several of them, the order is STOP, then REWIND, then ARM,
   START or CONTINUE, as `seq_core.h` documents. START and CONTINUE come before any other callback of
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

## N16. The tempo estimate follows a large tempo jump

N9's outlier band applies to clock intervals, not to the tempo. An interval is outside ±50% only when
the tempo more than doubles or drops below two thirds, for example 90 to 200 BPM or 200 to 90 BPM.
Smaller jumps, such as 120 to 190 BPM, stay inside the band, and the mean follows them within one
window (24 clocks, one beat). After a jump outside the band, N9's rule alone would leave every new
interval out, and the estimate would stay at the old tempo for good. So:

- Count consecutive outliers that lie on the same side of the estimate (all longer or all shorter).
- After 4 in a row (24 PPQ sources) or 2 in a row (jack at 4 PPQ), restart the estimate from those
  intervals alone, as if after a dropout.
- An interval inside the ±50% band, or an outlier on the other side, resets the count.
- An interval of 0 (clocks bunched into one USB packet share a stamp) is still left out. It ends a row
  without starting one: the packet came late, so the interval before the 0 is no sign of a new tempo.
  - A 0 that extended a row could restart the estimate at a mean of 0.
  - A 0 that left the row alone would let a few late packets of 2 clocks in a row restart the estimate
    at half the tempo.

## N17. A dropout sends the pending note-offs

After a slave dropout (N9, N15) the core sends every pending note-off at once, as it does at a stop.
Otherwise a note on an external MIDI device can hang until the next clock or Stop.

N16 and N17 came out of writing the plan, after the E1.10 section was finished. Task **E1.14**, after
E1.13, implements both test-first, with tests named `T16_tempo_jump_*` and `T18_dropout_note_off_*`.

The plan sections for the tasks added this way are written and reviewed separately, then inserted into
the plan after its review:
- `build/plan/40-E1.14.md`: E1.14, which implements N16, N17 and N18.1;
- `build/plan/41-E2.7.md`: E2.7, the global-menu step-button bug;
- `build/plan/42-E2.8.md`: E2.8, the USB MIDI out defects.

A plan review that finds these sections missing is therefore expected. N18.2–N18.4 are not part of
them: they change E2.1–E2.3, and the plan fix applies them there.

## N18. Defaults settled while writing the plan

These choices came up while writing the integration section. Each keeps fw 1.7 behaviour unless it
says otherwise. The owner can still change them.

1. **Skip-once marker per track.** `seqc_skipOnce(track, step)` keeps one marker per track; a new
   call replaces only that track's marker. With a single marker, a chord recorded on two tracks in
   the second half of a step would lose the first marker, and that track would play its recorded note
   twice. The signature is unchanged. E1.14 implements it test-first (`T14_skip_once_per_track_*`).
2. **MIDI transport out only as master.**
   - In internal sync the `transport()` callback sends Start, Stop and Continue on MIDI out, as fw 1.7
     did.
   - In the slave modes (1 and 2) it sends nothing. MIDI thru, when on, forwards what arrives, so
     downstream gear gets no duplicates.
   - A resume therefore never sends Continue in practice, because only the slave modes resume.
   - The owner confirmed this on 2026-10-24 (Q7): stay MIDI-compliant; the existing MIDI handling
     needs no change.
3. **One-shot roll velocity.** The MIDI Note On of a one-shot roll uses the current step's volume, as
   in fw 1.7, not a fixed roll velocity.
4. **USB realtime with CIN 0x5.** `USBD_CDC_DataOut()` also takes realtime bytes 0xF8–0xFF that a host
   sends with CIN 0x5 instead of CIN 0xF. Some hosts do this.
5. **Echo of an overridden note.** It stays note 60, as in fw 1.7.

## N19. The global menu fix changes two visible behaviours (E2.7)

Spec §12 says the UI is unchanged apart from the start-up screen and the CPU page. E2.7 is a third
exception: it fixes a fw 1.7 bug.

- **The bug:** `buttonHandler.c:901` stores `SEQ_CONFIG`, a step-button number (11), in the button
  mode. 11 is also `SELECT_MODE_LAST_STEP_ALL`.
- **What the fix changes:**
  - In the global menu (SHIFT + step 12), a step button without SHIFT no longer sets the length of all
    seven tracks.
  - Page up and down in the global menu keep step 12 blinking and show the step view, instead of the
    Last Step page's LEDs.
- **How:** the case stores `SELECT_MODE_MENU` (7), the existing "config menu" mode, whose handling is
  what the menu needs.
- **The host test relaxes warnings for the firmware files it compiles (spec §9.1).**
  - `test_buttons` compiles `buttonHandler.c` with `-w`, and `Core/Inc` is a system include directory
    for it.
  - The reason: the tests build with `-Werror`, and GCC's `-Wextra` (CI builds the tests with GCC;
    checked with arm-none-eabi-gcc 14.3) reports `volatile static` at `buttonHandler.c:57`, a
    fall-through at `:613`, and `const extern` at `Preset/InitKit.h:21`. Apple clang reports none of
    them.
  - The firmware build (`-Wall`, 0 warnings) remains the warning gate for those files, including
    `sequencer.h` with E2's prototypes.
  - `test_buttons.c`, the stand-ins in `tests/stubs/` and every other test keep
    `-Wall -Wextra -Werror`.
- The release notes list it under fixed bugs. Hardware check U1 covers it.

## N20. USB MIDI out (E2.8)

fw 1.7's USB MIDI out had defects outside D1–D8 (E2 open issue 10). Stage 1 fixes them (task
E2.8), beyond spec §8, §10 and §12:

- **Packets:** every message gets the CIN of USB MIDI 1.0 table 4-1, and unused bytes are 0: F1
  and F3 CIN 0x2, F2 0x3, F6 0x5, realtime and SysEx bytes 0xF, so SysEx goes out one byte per
  packet. fw 1.7 sent F1, F2 and F3 without their data bytes, SysEx garbled, and 128 bytes from
  past its ring on every wrap.
- **Transmit ring:** up to 128 packets wait until the IN endpoint is free, and the end of a
  transfer starts the next, so nothing is lost while a transfer is under way, also while the SD
  card is busy. A full ring drops and counts the new packet (spec §11).
- **No old messages:** nothing waits for a host that has not configured the device, or for one
  that has left a transfer untaken for 100 ms: it has stopped reading.
- **Song Select:** the MIDI parser routes F3 with its song number, to USB and to DIN out. fw 1.7
  routed it at once, with the data bytes of the message before it.
- **Files beyond §8:** `usb_manager.c/.h` and the new `usb_midiTx.c/.h`; in `usbd_cdc.c/.h` the
  old ring goes and `USBD_CDC_DataIn()` starts the next transfer.
- **RAM beyond §10:** 444 B for the ring and its transfer buffer, in place of fw 1.7's 136-byte
  ring. Review row R6.2 allows it on top of the timing core's 8704 B.
- The release notes list it under fixed bugs. Hardware check X4 covers it.

## N21. The tempo estimate measures the clock rate when clocks share a stamp (task E1.15)

E2.4 stamps USB MIDI at the packet (`USBD_CDC_DataOut()`), so every clock in one USB transfer gets the
same stamp. Under N9 and N16 a 0 interval is left out, so the estimate then measures the packet rate,
not the clock rate. Some hosts send one transfer per audio buffer, which makes this a normal case for
USB clock sync from a DAW. The E1.10 review found it:
- 3 clocks per packet at 120 BPM read as about 40 BPM;
- one transfer per 23.2 ms audio buffer reads as about 107.67 BPM.

The step grid and absStep stay correct, because clocks are counted. The wrong tempo reaches:
- the BPM display and `lfo_recalcSync()`;
- the hits played between clocks;
- the dropout time.

**Requirement:**
- With clocks that share stamps, `seqc_currentBpm()` stays close to the true clock tempo: within 0.5%
  after two beats of a steady clock, for packets of 1 to 6 clocks and for one transfer per 5.8–23.2 ms
  audio buffer.
- N16 tempo jumps, the N15 dropout and the jack at 4 PPQ keep working.
- The design is chosen by a separate design pass and implemented test-first in task **E1.15**, after
  E1.14 and before E2.1. Until then the E1.10 and E1.14 behaviour stands; reviewers need not raise this
  case again.

**Design, chosen on 2026-10-25 by a design pass that compared three estimators on 31 harness scenarios
and about 2800 swept ones.** The estimate is the slope of a least-squares fit of the clock stamps against
the clock count.

- **Transfers.**
  - Clocks with one stamp form one transfer, and a transfer is one point of the fit, placed at its last
    clock (the time USB stamps it).
  - A transfer is evaluated once it is complete: at the next clock with a later stamp, at any other sync
    event, or at the next `seqc_process()`.
  - `seq_tick()` drains every queued clock first, and OTG_FS (priority 3) queues a whole transfer above
    TIM2 (priority 4). So no 0 interval exists, and transfers of any size give the clock tempo.
- **Two windows.**
  - The fit uses the last 24 clocks and the last 96 clocks, at most 16 s; at 4 PPQ both are 4 pulses
    (N9). The estimate is the long slope.
  - When the short slope differs from it by more than the steps of the stamps can explain, the long
    window becomes the short one.
  - Exact stamps therefore follow ramps within a beat, and stamps quantised to audio buffers get the
    precision of 96 clocks.
- **Steps of the stamps.** The estimator keeps a decaying peak of how far an interval is from the slope,
  counted when the next interval takes it back: up to one buffer for USB, the jitter for DIN.
- **Outliers (replacing N9's mean and its ±50% band).**
  - MIDI: a point further from the line than half a clock, or than 1.25 step amplitudes, is left out. A
    lasting step is taken in, and a glitch does not widen the band.
  - Jack: the interval test with the line re-anchored, so a lost pulse leaves the estimate alone.
  - Nothing is tested in the first 12 clocks.
- **Jumps (N16, as amended).**
  - 4 outliers in a row (2 at 4 PPQ) restart the fit from those outliers alone, as after a dropout. The
    interval into the first one, which may straddle the change, is not used.
  - On MIDI, in-band steps of about 15% or more restart the same way, and count in `tempoRestarts`.
  - N16's rule for a 0 interval no longer applies.
- **Limit (qualifies the requirement above).** Near simple clock-to-buffer ratios no estimator reaches
  0.5% after two beats; for example 1:1 is 107.67 BPM with 1024-sample buffers.
  - Across the 561 tempos from 60 to 200 BPM, the fit stays within 0.74%, 0.79% and 1.37% at 256, 512
    and 1024 samples.
  - It misses 0.5% at 3, 8 and 42 of those tempos.
  - It is exact for transfers of 1 to 16 clocks.
  - DIN with ±0.3 ms jitter stays within 0.007%.
- **Costs and behaviour changes.**
  - About 928 B of RAM (a 98-point ring) and about 2.5 KB of code. Review row R6.2 is amended for it,
    as for N20.
  - A lost DIN clock now costs about 1.5% for about 4 beats, where it cost 0% before. A lost jack pulse
    still costs 0%.
  - Tempo changes on buffered USB settle in 1 to 3 beats.
  - The tuning constants (band 1.25, switch 1.5, decay 1/64, drift 0.25) come from the harness sweeps.
    Confirm them on hardware (H6, H7 over USB) and measure the CPU cost with the E3.3 meter.
