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

*Amended 2026-10-25 (task E1.16).* The final review of the core measured the layout above on the
firmware: newlib-nano's byte-wise `memmove` moves the whole queue on almost every insert, which costs up
to 0.8–1.2 ms per boundary on a dense pattern. The queue becomes earliest-first:
- a ring with a head index, where pop is O(1);
- the common insert, later than everything pending, is an append;
- elements are moved with struct copies, not `memmove`;
- an insert between pending events moves the events on its shorter side one slot: those before it
  towards the head, or those after it towards the tail, so an insert before every pending event moves
  nothing either (E1.16 review);
- the choke writes nothing when it removes nothing.
The ordering key, the choke, the same-tick rule and the overflow behaviour are unchanged.

*Amended 2026-10-25 (task E1.18): one merge per boundary.* Each new event was still inserted on its
own, so when a boundary's events interleave with the pending ones every insert can move many of them;
and every fire's choke compacted the queue. The E1.16 reviews measured up to 3861 element writes in one
boundary call (a hill-climbed legal pattern, 608 of them choke compaction, about 70–90k cycles) and up
to 448 in a call without a boundary. The queue now works as follows. It stays in `seq_core.c`, in its
own section: there is no LTO, so a separate translation unit would turn its small functions (the key
compare, the choke test, the front of the queue), now inlined into the processing loop, into calls.
- **Stage.** At a step boundary the generator stages its events in a buffer instead of the ring. The
  buffer holds the most one boundary generates, `SEQC_STAGE_SIZE` = 112 (a static assert): per track
  one roll event, the late part of step `n` (at most 8 hits) and the early part of step `n+1` (at most
  7 hits: the earliest is 84 ticks before its boundary, and ×8 hits are 12 ticks apart).
- **Sort.** An index of the staged events is kept in firing order: binary search, and the indices on
  the shorter side move, since a boundary generates the late hits of step `n` before the early hits of
  step `n+1`, which fire first.
- **Merge.** At the end of the boundary the staged events join the ring in one pass. The pending
  events that fire before the first staged one stay in place, and so do those that fire after the last
  one; of the events in between, those on the shorter side move, towards the tail (an append in the
  common case) or towards the head. A new event is written twice (staged, merged), a moved pending event
  once.
- **Ties.** Of events with equal keys, the staged ones fire before the pending ones, and of the staged
  ones the one staged last: the order of one insert per event, where the one queued last fires first.
- **Overflow.** An event is rejected when the queue's events and the events staged before it at the
  same boundary fill `SEQC_QUEUE_CAPACITY`. Those are exactly the events that one insert per event
  rejects (the boundary's last ones, in the order they were generated), and they are counted in
  `queueOverflow` the same way; `queueMax` counts the staged events.
- **Choke.** A fire records its choke: per track, the absolute step below which the track's queued
  events are removed. Such an event is dropped when it reaches the head. The others are compacted once,
  before the next event is staged and at the end of the call, from the nearer end: the kept events
  after the first removed one move towards the head, or those before the last removed one towards the
  tail, whichever are fewer. A choke that removes nothing writes nothing. `chokeDrops` and `queueDepth`
  are as before between calls; inside a callback `queueDepth` can still count events that a choke of
  the same call removed.
- **Skip-once** removes its hits the same way, after the recorded chokes and the staged events (it
  can be called from a callback).
- **Single inserts.** The core queues nothing outside a boundary (the one-shot roll stays in
  `seq_setRoll()`), so there is no separate single insert; the test hook queues one event as a batch of
  one.
- **Measured** (the most element writes in one `seqc_process()` call: the boundary calls, then the
  others; `tests/test_queue.c`). The dense ×8 patterns run 16 steps at 120 BPM with a call every
  0.1 ms; the hill-climbed and the random ones 32 steps at 120, 130 and 300 BPM with a call every 0.1, 1
  or 3 ms:

  | Pattern | E1.16 queue | E1.18 queue |
  |---|---|---|
  | ×8 on every track, shift +7 or 0 | 644 or 410, 0 | 112, 0 |
  | ×8, shift +7/−7, shuffle 127 | 1194, 168 | 210, 0 |
  | ×8, shift −7/+7, shuffle 127 | 543, 217 | 140, 0 |
  | ×8, shift +7/+7, shuffle 127 | 1077, 448 | 147, 28 |
  | the hill-climbed pattern | 3861, 289 | 330, 43 |
  | random ±7 shifts, ×8, shuffle 127, 200 seeds | 1691, 229 | 240, 76 |

- **Cycles** (estimated from the firmware disassembly: a copy is a 4+2-word LDM/STM pair, about 30
  cycles a write with its loop and compare; a key compare about 12; an index shift about 4, through
  newlib-nano's byte `memmove`). The worst boundary call, of the hill-climbed pattern, has 330 writes,
  816 compares and 1894 index shifts: about 28k cycles, 9% of the 313 186-cycle block budget (N12),
  against about 70–90k. A dense ×8 boundary with every track on one grid (112 writes, 264 compares,
  354 index shifts) costs about 9k.
- **Size** (`seq_core.c.obj`, the firmware compile command, Arm GNU 14.3.rel1): bss 8269 → 11221 B
  (+2952: the staging buffer 2688 B and its index 224 B, the recorded chokes 32 B), text 20178 →
  20090 B. Review row R6.2 must allow it, as for N20 and N21.
- Tests check the order and the rejects against a reference model (batches into a nearly full queue)
  and against fire logs recorded from the E1.16 queue (random patterns, settings and recorded notes,
  with the firmware's queue and a 16-entry one).

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

*Amended 2026-10-25 (task E1.17 Part D, recorded in E1.18).* **Deviation from spec §6.2**, which says
"Edits, pattern changes, track length changes, erase and mutes made before a step's boundary apply to
that step". A step whose early part played a hit before `n·96` follows that early part at boundary
`n`: it plays all its hits or none, whatever `stepOn()` and `Step.prob` read then. An erase or a
probability edit made before boundary `n` therefore does not stop its late hits, and a step switched
on then does not add them. The step data (velocity, note, p-locks) is still read at boundary `n`. Two
exceptions:
- a late change of the pattern or of the track length: if `(p, s)` at boundary `n` differs from the
  early part's, the early hits came from the prediction, and the late part is that of the step actually
  reached;
- skip-once (`seqc_skipOnce()`) drops the hits that have not played yet.
A step whose early part had no hit before `n·96` keeps the spec rule: an edit made before its boundary
applies, with the same draw. The stage 1 report and E4.1 take this deviation from here.

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
- *Amended 2026-10-25 (E1.17).* That clock also gives `clockCount` the step phase of the position it
  reaches. The master resumes its song position there too, so the tempo steps and a later local start
  follow the steps that play; before, the count kept the phase of the clocks the master sent while
  stopped.

**MIDI Stop:**
- Stops and keeps everything for a later Continue.
- fw 1.7 restarted from step 0 on Continue (`seq_setRunning(1)` reset the indices). The spec asks for
  resuming, and that is what we do. This is listed as an intended change.

**Local start in a slave mode:**
- The core counts incoming clocks while stopped, in `clockCount`. MIDI Start and SPP reset the count
  (and a Continue re-phases it, see above).
- A local start arms the transport until the next clock with `clockCount % clocksPerStep == 0`, then
  starts at step 0.
- `clocksPerStep` is 6 for MIDI and for jack 24 PPQ, and 1 for jack 4 PPQ.
- This matches fw 1.7, where `sync_clockCnt` aligned a local start to the master's 16th grid.

**Local stop** (button): stop and reset to the start, as fw 1.7 `FRONT_SEQ_RUN_STOP 0` →
`seq_resetPosition()` + `seq_setRunning(0)`.

**Clock jack reset input (correction to the spec):**
- In fw 1.7 this input is a *run gate*, not a reset pulse (`EXTI9_5_IRQHandler`):
  - pin active → stop and reset;
  - pin released → start, armed until the next jack pulse that begins a step of the clock count:
    every pulse at 4 PPQ, at 24 PPQ every 6th counted from the gate-on, as fw 1.7's
    `trigger_clockCnt` (wording corrected in E1.17: "the next jack pulse" read as any pulse).
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
- *Amended 2026-10-25 (E1.14 review, commit 1b924ad).* Any interval shorter than half a clock at the
  highest slave tempo, 300 BPM (4.17 ms at 24 PPQ, 25 ms at 4 PPQ), counts as a 0. DIN interfaces that
  forward a burst of clocks back to back, about 0.32 ms apart, would otherwise restart the estimate.
  E1.15's estimator (N21) replaces this rule with transfers and its own outlier test.

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
  - The figures depend on the buffer phase, where the first clock falls within a buffer. The ones
    below are for phase 0.31 of a buffer.
  - Across the 561 tempos from 60 to 200 BPM, the fit stays within 0.51%, 0.87% and 2.59% at 256, 512
    and 1024 samples (the worst at 144.25, 143.75 and 108.25 BPM, the last near 1:1).
  - It misses 0.5% at 1, 8 and 47 of those tempos.
  - Over 20 buffer phases with the same stamp model (the E1.15–E1.17 final review), the worst case is:

    | Buffer | Worst error | Misses of 561 | At phase 0.31 |
    |---|---|---|---|
    | 256 samples | up to 0.97% | up to 3 | 0.51%, 1 |
    | 512 samples | up to 1.35% | up to 17 | 0.87%, 8 |
    | 1024 samples | up to 2.80% | up to 75 | 2.59%, 47 |

    H6 and H7 take the worst case over the phases as their acceptance limits.
  - *Re-measured in E1.17* with the stamp model of the committed tests (`usbStamps()` in
    `tests/test_slave.c`: buffer phase 0.31, 16 beats, the error from clock 48 on), on the core after
    E1.15 and after E1.17 alike. The design pass's harness had given 0.74%, 0.79% and 1.37%, and 3, 8
    and 42 tempos.
  - It is exact for transfers of 1 to 16 clocks.
  - DIN with ±0.3 ms jitter stays within 0.007%.
- **Joining bursts (amended 2026-10-25 during E1.15).**
  - A MIDI clock that comes less than `SEQC_TEMPO_JOIN_MS` (2 ms) after the newest stamp of the last
    transfer joins it, whether that transfer is still open or was evaluated. The transfer then takes
    the clock's later stamp. If it had been evaluated, the fit is restored from the copy taken before
    it, and an outlier is uncounted.
  - A transfer followed by another sync event takes no more clocks.
  - The reason: a DIN burst with clocks 0.32 ms apart reaches the core one clock per `seq_tick()`, so
    the burst's late head would be evaluated alone.
  - Why any evaluated transfer, not only an outlier (amended after the E1.15 quality review): the fit
    tests nothing in its first 12 clocks (after the switch, every dropout and every N16 restart), so a
    burst's head there went into the fit, and the rest of the burst raised the step amplitude to
    about the burst size. A bunch of 6 at clock 8 read 140 BPM at clock 14 and 3.3% off at clock 47.
    Joining only untested heads instead treats the clocks of one audio buffer differently in the first
    clocks and later, which tilts the line: with DIN-forwarded 1024-sample buffers it failed 0.5% at 218
    of 561 tempos, against 47 for USB. With the rule above DIN-forwarded buffers give the USB numbers
    (1, 8 and 47 of 561 tempos at 256, 512 and 1024 samples, as in the Limit bullet), and bunches of 2,
    3 and 6 anywhere in the first 20 clocks stay within 0.03%.
  - Until the next clock of a burst joins it, the estimate includes the head alone: for less than 2 ms.
  - *Amended in E1.17.* A burst whose head is the outlier that restarts the fit joins as well: the
    restart is undone and done again, or not, with the whole burst. Until then the restart wrote over
    the free slot of the 98-point ring, so it could not be undone: a bunch of 6 there read up to 1.45%
    off two beats after a jump, and a host stall (clocks 0.6 to 1.8 clocks late, then a catch-up burst)
    restarted the fit although the tempo had not changed. The ring now has `SEQC_TEMPO_RESTART − 1`
    more points (101), so a restart leaves the points of the fit before it intact; +24 B of bss.
  - Why 2 ms: back-to-back USB transfers in consecutive 1 ms frames join reliably; a few MIDI bytes fit
    between two clocks of a burst; and it is a quarter of a clock at 300 BPM.
    - The trade-off: the join also merges USB transfers of two buffers sent in consecutive 1 ms frames.
      The E1.15 quality reviewer measured it with every second 1024-sample buffer sent 1 ms before the
      next: the settled misses of 0.5% rise from 54 to 119 of 561 tempos, and the worst error falls
      from 4.19% to 2.45%.
  - The jack keeps the plain rule.
  - Check the 2 ms value on hardware, under H6 and H7, also with a host that sends the transfers of
    two buffers in consecutive 1 ms frames.
- **Costs and behaviour changes.**
  - 1192 B of bss over E1.14 (7053 to 8245 B): the 98-point ring and the fit (928 B), and the fit copy
    and the join state (264 B). E1.17's 101-point ring adds 24 B (8269 B). Review row R6.2 must allow
    it, as for N20; the controller amends R6.2 before E4.1 with the measured figures (amendments items
    12, 14 and 17).
  - About 2.7 KB of code: `seq_core.c.obj` text 18602 B after E1.15 (b97c1c6) against 15854 B after
    E1.14, measured with the firmware's compile command and Arm GNU 14.3.rel1.
  - A lost DIN clock now costs about 1.5% for about 4 beats, where it cost 0% before. A lost jack pulse
    still costs 0%.
  - Tempo changes on buffered USB settle in 1 to 3 beats.
  - The tuning constants (band 1.25, switch 1.5, decay 1/64, drift 0.25) come from the harness sweeps.
    Confirm them on hardware (H6, H7 over USB) and measure the CPU cost with the E3.3 meter.
