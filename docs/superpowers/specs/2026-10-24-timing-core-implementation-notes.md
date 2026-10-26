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
- The anchor moves only on a tempo change, a start, a slave clock, a switch to internal sync (the
  internal timeline is anchored at the position reached, `seqc_setSyncMode()`) and the core's first
  time, at its first `seqc_process()` or `seqc_syncEvent()` call (`seqc_advance()`; plan, epic E1,
  decision 2). Because the position is never accumulated step by step, it has no drift. (Amended in
  the E4.1 review, F10: the list named only the first three.)
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
- **Merge.** At the end of the boundary the staged events join the ring in one pass, from one of its
  ends (wording corrected in E2.9, after the E1.18 quality review):
  - a merge from the tail moves every pending event from the first staged one's place to the tail,
    that is every one that does not fire before it, towards the tail; in the common case that is
    none, and the merge is an append;
  - a merge from the head moves every pending event before the last staged one's place, that is
    every one that fires before it, towards the head;
  - the merge takes whichever of the two groups is smaller.

  A new event is written twice (staged, merged), a moved pending event once.
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
  tail, whichever are fewer. A choke that removes nothing writes nothing. When the queue is cleared
  (a rewind, a song end, a song position, a slave Start or jack reset), the events a choke of that call
  removed are counted first, without moving anything. `chokeDrops` and `queueDepth` are therefore as
  before between calls; inside a callback both can lag: `queueDepth` can still count events that a
  choke of the same call removed, and `chokeDrops` counts them only when they reach the head, at the
  compaction, or when the queue is cleared.
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
  20138 B (with the E1.18 review fixes). Review row R6.2 allows it, as it allows N20 and N21 (E2.9).
- Tests check the order and the rejects against a reference model (batches into a nearly full queue,
  and a batch with ties that merges from the head) and against fire logs recorded from the E1.16 queue
  (random patterns, settings and recorded notes, with the firmware's queue and a 16-entry one). Direct
  tests cover what those runs do not reach: `chokeDrops` when a song end, a slave Start or a jack reset
  clears the queue in the call of the choke, and skip-once from a generation callback.

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

*Amended: N21 replaces the mean and the ±50% band with a least-squares fit and its own outlier test
(E1.15).*

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

## N14. SPP and Program Change travel in the sync queues

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

*Amended in E2.9: Program Change travels in the sync queues too* (`fd0afbe`, an E2.4 review fix, with
`23a01ac` for the time the drain reads). This is a deviation from spec §4.3 and §7.2, where only the
realtime messages travel in the queues (SPP joins them through this note), and from fw 1.7, where the
MIDI parser switched the pattern itself.

- **Why.** In E2.4 as first written (`cca16a9`), `seq_tick()` drained Start and Continue before it
  parsed a Program Change that had arrived before them. A slave that got a Program Change and a
  Start in one USB transfer played the old pattern first. fw 1.7 parsed both in arrival order.
- **Decoding.**
  - DIN: `USART3_IRQHandler` passes every received byte with its stamp to `seqq_dinByte()` (through
    `seqp_dinByteIn()`). A data byte after a `Cn` status byte, also by running status, is a Program
    Change, stamped with that data byte. Realtime bytes leave the running status alone; any other
    status byte, F0–F7 included, ends it.
  - USB: `USBD_CDC_DataOut()` passes every packet of cable 0 whose CIN gives a message length
    (0x2–0xF) with the transfer's stamp to `seqq_usbMessage()` (through `seqp_usbMessageIn()`), up
    to the first packet whose first byte is 0, where it stops reading the transfer. CIN 0x0 and 0x1
    (reserved) carry no Program Change, realtime byte or SPP. `Cn` with any CIN from 0x2 up is a
    Program Change, since the parser takes `Cn` as one whatever the CIN. (Amended in the E4.1 review,
    F8: this said every packet of cable 0.)
- **Queueing.** The entry is `SEQQ_PROGRAM_CHANGE` (0x81) with the value `channel << 8 | program`, in
  the same queue as Clock, Start, Continue, Stop and SPP from that source (`seq_syncq`). Every byte and
  every packet still reaches the parser as well; its Program Change case only routes now (MIDI thru).
- **Applying.** `seq_drainSyncQueues()` pops the entries of the DIN, USB and jack queues in stamp
  order, so a Program Change acts in its place among Start, Continue, Stop, the clocks and SPP.
  - It reads the time (`seqp_now()`) before every pop. A pattern switch can load a kit for tens of
    ms, and an entry stamped during the load would otherwise reach the core at the time read before
    it instead of at its stamp.
  - A Program Change acts only when `MIDI_FILTER_PROG_C` is on and its channel is the global channel
    (`midi_MidiChannels[7]`), as in fw 1.7; `MIDI_FILTER_SYSTEM` does not apply to it. It calls
    `seq_setNextPattern()`: while running, that sets the next pattern; while stopped, it switches the
    pattern at once and posts a rewind to the mailbox, which the drain applies (`seq_applyMailbox()`)
    before it pops the next entry. A Start popped after it therefore starts the new pattern from its
    first step.
  - `seqc_process()` then gets the time of the last pop.
- **Release notes** (unchanged behaviour): a Program Change sent just before a Start selects the
  pattern that the Start plays, as in fw 1.7, also when both come in one USB transfer.

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
E1.13, implements N16, N17 and N18.1 test-first, with tests named `T16_tempo_jump_*` and
`T18_dropout_note_off_*`. N18.2–N18.4 change E2.1–E2.3 and are applied there.

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
   - A resume in a slave mode therefore sends nothing. A switch to internal sync that starts an
     armed transport (`seqc_setSyncMode()`) sends Start, or Continue when the position is kept or
     located (a Continue, or an SPP and a Continue, received before the switch); a slave that
     follows the LXR then plays the step with it. (Amended in the E4.1 review, F7: this said that
     a resume never sends Continue in practice, because only the slave modes resume.)
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
- **F4, F5 and F6 alone** (`3771319`, an E2.4 review fix made in E2.8; added in E2.9): the MIDI
  parser routes these messages without the stale data bytes of the message before them. fw 1.7 kept
  that message's length, so it sent a Tune Request after a Note On as `F6 3C 64`. They now end
  running status, as MIDI 1.0 asks: data bytes after them no longer count as running status of the
  message before (`midiParser_parseUartData()` ignores them). Release notes: a Tune Request (F6)
  passed on from DIN goes out alone, without the data bytes of the message before it, and stray
  data bytes after it no longer act as a repeat of that message.
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
  E1.14 and before E2.1.

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
  - On the jack, a jump to about 2 to 2.7 times the tempo with the pulse right after it lost reads
    the old tempo for three pulses before the restart (E1.19 bullet below, F4 of the E4.1 review).
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
- **One spurious clock or a double trigger (amended in E1.19).**
  - The E1.15–E1.17 final review measured what one spurious clock does to the fit: on MIDI clock,
    one 0.22 to 0.48 of a clock after a real one restarted it (8% to 16% off for 12 to 20 clocks,
    and a `tempoRestarts` although the tempo never changed); on the jack, a double trigger left an
    offset in the line (1.5% to 17.9% for 3 to 5 pulses at 4 PPQ).
  - The fit now has its own clock count, which leaves a spurious clock out (`extraClocks` in the
    fit, so a MIDI join that takes the fit back takes it back too). The position still counts the
    clock: a slave cannot tell a spurious clock from a real one, so the one-clock phase step stays.
  - MIDI: the clocks after a spurious one are one clock early in the count, so they leave the band
    in a row (N16). When the row is early, and its points after the first, counted one clock
    earlier, lie within a quarter band of the line, and so does the nearer of the row's first point
    and the fit's newest point, the fit counts one clock fewer, takes the row in and leaves the
    farther of those two out (the spurious clock: the row's first point up to about half a clock
    after a real one, the newest point of the fit later). The row is the lookahead that tells it
    from a tempo jump, whose clocks move further off the line at every clock. It needs a step
    amplitude before the row of at most that quarter band (stamps precise to 1/8 of a clock).
  - Jack: a double trigger splits one interval into two short ones that add up to the slope. The first
    is an outlier if the second pulse came in the first half of the interval, the second if it came
    in the second half; within about the jitter of half a pulse, both or neither may be. When the
    interval into a pulse and the one before it add up to the slope of the first within a sixteenth
    band, both are short against that slope, and the interval before them was within a quarter band,
    one of the two pulses is left out and the fit counts one clock fewer. It is the one further from
    the line at their common clock: the pulse between, unless that lies nearer than the pulse after
    it, as for a contact bounce just after a real pulse; then the later pulse is left out and the
    fit stays as it was. If the pulse between was in the fit (the second half), its offset leaves
    the step amplitude and the drift too; else a tempo change after it is followed later (amended in
    the second E1.19 review, see below).
    - The jack decides at the pulse after the double trigger, with no row, and the first two
      intervals after a jump to two to three times the tempo (the straddling one and the first new
      one) can add up to one old pulse. So the merge is tentative (amended in the E1.19 review): the
      fit before it is kept, in the copy that MIDI keeps for the join (jack transfers never join).
      The next pulse undoes the merge if it comes more than half a band before one old pulse after
      the merged one: after a double trigger it comes one old pulse later, after such a jump half an
      old pulse or less.
      The undo puts back the fit and the ring slot the merged pulse wrote, takes the merge's own
      count back out of `intervalsExcluded` (so a `seqc_resetStats()` between the merge and the undo
      stays a reset; amended in the E4.1 review, F5), and the merged pulse goes in again without a
      merge. The jump then goes through the fit as it would have without one, a pulse later.
    - Known limitation (the E4.1 review, F4, accepted): the undo misses a jump to about 2 to 2.7
      times the tempo when the pulse right after the merged one is lost. The next pulse then comes
      two new pulses, 0.75 to 1 old pulse, after the merged one, as after a double trigger, so the
      merge stays and the fit takes that pulse in at the old slope. At 4 PPQ, 120 to 240 BPM 0.05
      into a pulse, with the second new pulse lost, the estimate reads 119.1 BPM for three more
      pulses (50% off: inside each of those steps the interpolation runs at half speed, so its
      later hits bunch at the next pulse, which still starts its step) and then restarts at 240
      (N16), exact from the sixth new pulse. With the first new pulse lost instead (no merge) it
      reads 139.9, 158.4, 183.2 and 200.0 BPM and is exact from the same pulse; with no pulse lost,
      197.5 and 237.6, exact from the fourth. It needs a jump of two to three times and a lost
      pulse at the one pulse after it (about 1 in 600 of the E1.19 review's random runs). Keeping
      the merge tentative for one more pulse would tell the two apart (1.5 against 2 old pulses
      after the merge), at the cost of a second fit copy and a rerun of the sweeps below.
    - Guards that no measured jump needs (the E4.1 review, F48 and F49). Tests pin the sum band,
      both sides of the undo threshold near it and the MIDI step gate (F48). Three rules stay as
      they are, though no jump measured needs them, and changing one needs the sweeps below again:
      - The MIDI step gate. Without it, or at 2 or 4 times its bound, 5760 MIDI jumps with ±1 to
        ±4 ms of jitter and 10800 on stamps quantized to buffers of 1 to 23.2 ms give the same
        restarts and estimates. It only keeps some spurious clocks under jitter from merging: the
        row restarts the fit instead, within 0.06%.
      - The undo threshold at half a band, not 0.75 of one (F49, accepted). A jump that merges
        brings the next pulse at least 0.47 of an old pulse early, about 0.94 of a band, and 5820
        jack jumps (4 and 24 PPQ, 60 to 300 BPM, 10 phases, 0, 0.3 and 1 ms of jitter) give the
        same runs either way. A pulse 0.26 to 0.37 of a pulse early after a double trigger undoes
        the merge: 11% to 29% off at 4 PPQ, where keeping it gives 5% to 7%.
      - No split after a merged interval (`!merged` in the steady rule; F49, accepted). The same
        5820 jumps give the same runs without it. A second double trigger on the very next pulse,
        0.75 of a pulse or later into it, is not merged: up to 9.2% off at 4 PPQ and 1.5% at
        24 PPQ, where merging it gives 0%.
    - The first version merged at once and guarded jumps with the tight sum test alone, and with no
      merge of two outliers or of two intervals inside the band. The review found that it still
      merged jumps to about 2 to 2.5 times when the change came early in a pulse, and dropped a
      real pulse: from 120 to 200–300 BPM at 4 PPQ, 47 of 2020 exact jumps restarted where they
      had followed inside the band, and at 24 PPQ with 0.1 to 0.3 ms of jitter the missing pulse
      stayed in the 96-clock window (1.34% two beats after 120 to 240 BPM, 0.32% after four). Its
      figures (1.94 to 2.06 times; the same error within 0.01 points) came from a grid of 2.5 BPM
      and 4 change phases that missed those phases.
    - With the undo, two outliers and two intervals inside the band merge too, so a second pulse at
      about half a pulse is a double trigger like any other. A merged interval is not split again.
    - The second E1.19 review found two more defects, fixed then:
      - A contact bounce right after a real pulse that came even a little early (an in-band
        interval with a negative offset can be split) passed the sum test at the bounce, and the
        merge left out the real pulse and kept the bounce at its clock. At 4 PPQ and 120 BPM, the
        real pulse 0.1 ms early and a bounce 0.25 to 3 ms after it: 0.024% to 0.47% off from the
        next pulse on, where the early pulse alone gives 0.016%. With ±0.1 to ±1 ms of jitter and
        f 0.001 to 0.05 (20 seeds), up to 0.81%, over the lost-pulse error in 114 of 600 runs
        (f 0.002 to 0.04). Now the nearer of the two pulses stays: the same runs are all within the
        lost-pulse error, and the bounce costs exactly what the early pulse alone does. At 24 PPQ
        the sum band is 0.65 ms; a later bounce is an outlier that the next pulse merges (0.004%).
      - The first fix also required a step amplitude of at most the sixteenth band. It changed no
        jump by more than 0.053 points, at one pulse, and only blocked valid merges under jitter,
        so it is gone. At 24 PPQ with ±1 ms (120 BPM, f 0.001 to 0.999, 20 seeds), 211 of 860
        runs are over the lost-pulse error, against 533 with it: there the jitter of the two
        stamps is over the 0.65 ms sum band in about 8 of 20 seeds, and such a double trigger
        costs what it did before (up to 5.0% near f 0.5). With the gate, the offset of a pulse
        between left in the step amplitude or the drift kept a second double trigger from merging;
        without it, it would slow the switch to a new tempo after the merge: at 24 PPQ, 0.24% off
        a beat after 120 to 84 BPM (step amplitude), or the switch a pulse late, 0.43 points more
        after 120 to 114 BPM (drift). Two double triggers at 24 PPQ with ±1 ms (5 by 5 offsets,
        gaps 2 to 32, 3 seeds): 606 of 825 runs better, 12 worse (by up to 1.7 points, where the
        second is not merged either way), the worst 8.2% either way.
  - Measured with exact stamps and a 0.1 ms `seq_tick()`, one spurious clock f of a period after a
    real one, f from 0.05 to 0.95 in steps of 0.05, at 120 BPM (MIDI also at 90 and 180 BPM, with
    the same figures). The error is the worst from the clock after it on; the duration is the
    clocks or pulses until the estimate stays within 0.1%.

    | Source | f | Before | After |
    |---|---|---|---|
    | MIDI | 0.05–0.20, 0.85–0.95 | 1.57%, 93–95 clocks | the same: taken in as a step, as a lost clock (1.52%, 95 clocks) |
    | MIDI | 0.25–0.45 | 8.1%–15.6%, 37–50 clocks, a restart | 0.000%, no restart |
    | MIDI | 0.50–0.80 | 0.013%–0.032%, a restart | 0.013%–0.032%, no restart |
    | Jack, 4 PPQ | 0.05–0.45, 0.55–0.95 | 1.5%–17.9%, 3–5 pulses | 0.000% |
    | Jack, 24 PPQ | 0.05–0.45, 0.55–0.95 | 0.30%–2.74%, 21–92 clocks | 0.000% |
    | Jack | 0.50 | 42.9% for 4 pulses (4 PPQ), 5.0% for 24 clocks (24 PPQ) | 0.000% |

    - With ±0.1, ±0.3 and ±1 ms of jitter (f 0.05 to 0.95), the merged cases stay within 0.034% on
      MIDI, and on the jack within the error of a lost pulse with the same jitter (at ±1 ms and
      4 PPQ, 0.28% to 0.39% against 0.42%); at 24 PPQ with ±1 ms, see the second review above.
      Around half a pulse (f 0.47 to 0.53 in steps of 0.005, 20 jitter seeds each), the jack at
      4 PPQ stays within the lost-pulse error at every jitter. Before E1.19 it was up to 105% off at f 0.5,
      with a restart in 6 of 20 runs; the first E1.19 version still was from f 0.485 to 0.51 at
      ±1 ms.
    - Two double triggers 2 to 8 pulses apart at 4 PPQ, or 2 to 32 clocks apart at 24 PPQ, at f 0.1,
      0.3, 0.5, 0.6 and 0.8: each merges. Before the drift was put back, the second was not merged
      in 96 of the 200 runs (up to 43% at 4 PPQ, 5% at 24 PPQ).
    - While a MIDI row forms (f over 0.5) and between a double trigger and the next pulse (f over
      0.5 on the jack), the estimate includes the spurious clock: for the four clocks of the row, and
      for less than one pulse.
  - Unchanged: the N21 Limit sweep (identical output at buffer phase 0.31, 0.51%, 0.87% and 2.59%,
    and over 20 phases), the burst join, the dropout, a lost clock or pulse, and every test before
    E1.19.
  - Tempo jumps on the jack, from 120 to 200–300 BPM in steps of 1 at 20 change phases (0.05
    steps), at 4 and 24 PPQ, with exact stamps and with ±0.1, ±0.3 and ±1 ms of jitter (1 to 3
    seeds): the restarts, `intervalsExcluded`, the error two beats later, the largest error after
    that, the error at the end and the number of steps played are the same as before E1.19 in all
    36360 runs. From 120 to 60–300 BPM in steps of 2.5 at 4 change phases, with 0, 0.3 and 1 ms of
    jitter, the error after every pulse: MIDI is identical, and on the jack 76 of 2328 runs differ,
    each at one pulse only. That is the pulse between a merge and its undo, where the estimate
    holds the old tempo: up to 50.6 points more error for that pulse at 4 PPQ (a quarter of a
    beat; where the window had followed at once, 120 to 245 BPM 0.05 into a pulse), under 0.07
    points at 24 PPQ. From the next pulse on the runs are identical.
    - Again after the second review, with the bounce rule and without the step gate: the first sweep
      above (its 24240 jittered runs) is identical to before E1.19. From 120 to 200–300 BPM in steps
      of 1 at 20 change phases, at 4 and 24 PPQ, exact and with ±0.1, ±0.3 and ±1 ms (2 seeds), the
      error after every pulse: 2884 of 28280 runs differ from before E1.19, each at one pulse only,
      and the restarts, `intervalsExcluded` and steps played are the same in all. 389 of them come
      from removing the step gate, by at most 0.053 points; the bounce rule changes none.
  - A MIDI merge can take the fit's newest point out, and the next point goes into its ring slot,
    which the fit before the transfer uses. The slot is kept and put back when a clock joins the
    transfer, so the copy stays intact. (With the present bands the joined transfer is two clocks
    early and restarts the fit, which clears the windows; the slot is put back regardless.)
  - Costs: `seq_core.c.obj` text 20138 to 22862 B (+2724) and bss 11221 to 11309 B (+88: 24 B in
    the fit and in its copy for the join, 8 B for the kept slot, 32 B for the tentative jack merge),
    measured with the firmware's compile command and Arm GNU 14.3.rel1. After the second review
    (the bounce rule, no step gate): text 22898 B (+36), bss unchanged. R6.2 allows it (E2.9).
- **Costs and behaviour changes.**
  - 1192 B of bss over E1.14 (7053 to 8245 B): the 98-point ring and the fit (928 B), and the fit copy
    and the join state (264 B). E1.17's 101-point ring adds 24 B (8269 B). Review row R6.2 allows it,
    as it allows N20: E2.9 set it to at most 391824 B of DTCM + RAM, with the measured figures.
  - About 2.7 KB of code: `seq_core.c.obj` text 18602 B after E1.15 (b97c1c6) against 15854 B after
    E1.14, measured with the firmware's compile command and Arm GNU 14.3.rel1.
  - A lost DIN clock now costs about 1.5% for about 4 beats, where it cost 0% before. A lost jack pulse
    still costs 0%. For a spurious clock or pulse see the E1.19 bullet above.
  - Tempo changes on buffered USB settle in 1 to 3 beats.
  - The tuning constants (band 1.25, switch 1.5, decay 1/64, drift 0.25) come from the harness sweeps.
    Confirm them on hardware (H6, H7 over USB) and measure the CPU cost with the E3.3 meter.
