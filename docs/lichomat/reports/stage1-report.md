# LICHOMAT stage 1: report of deviations and intended changes

The implementation notes (`docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md`)
win where they and the spec (`docs/superpowers/specs/2026-10-23-timing-core-design.md`) disagree,
and they say that the deviations are listed in the stage 1 report. This is that report. It lists:

1. every place where stage 1 does something other than the spec says, with the note (N1–N21) or the
   decision of the plan (`docs/superpowers/plans/2026-10-23-timing-core.md`, with its "Execution
   amendments") that allows it;
2. every intended change of behaviour against the official firmware 1.7, and whether the release
   notes (`docs/lichomat/release-notes-v0.1.md`) tell users about it;
3. the resources stage 1 uses (spec §10).

Review row R1.4 (`docs/lichomat/reports/stage1-review.md`) checks it: a deviation in the code that is
in neither list is a finding.

## 1. Deviations from the spec

| Spec | The spec says | Stage 1 does | Allowed by |
|---|---|---|---|
| §5 position | Q48.16 fixed point | a `double` in ticks, computed from an anchor | N1 |
| §4.2, §10 event | 20 bytes, `uint32_t` tick; queue 5 KB | 24 bytes, `int64_t` tick and the ratchet hit index; queue 6 KB | N2, N13.7 |
| §4.2 event queue (layout) | not fixed by the spec; N2 first had a sorted array with the next event at the end and `memmove` inserts | an earliest-first ring with a head index and struct copies (E1.16); a boundary stages its events (at most `SEQC_STAGE_SIZE` = 112), sorts an index of them and merges them into the ring in one pass from the nearer end; a fire records its choke, and the removed events are compacted once per staging and at the end of the call (E1.18). The ordering key, the choke, the same-tick rule and the overflow count are those of N2 | N2 (amended in E1.16 and E1.18); plan, execution amendments 4, 16 and 19 |
| §6.4, §11 counts | a choke removes the events at once; the queue counts are exact | between `seqc_process()` calls `chokeDrops` and `queueDepth` are as before; inside a callback `queueDepth` can still count events a choke of the same call removed, and `chokeDrops` counts them only when they reach the head, at the compaction or when the queue is cleared | N2 (amended in E1.18) |
| §6.2 regeneration | remove the on-time events of step n at its boundary and generate them again | an early part (at boundary n−1) and a late part (at boundary n), each hit generated once | N3 |
| §6.2 edits before the boundary | apply to the step | apply to the step, except a change of the shuffle or of the step's timing after the step's early part queued a hit, or one that would put a hit before the boundary: then the step keeps the timing of its early part (no hit twice, none lost) | plan, epic E1, decision 9 |
| §6.2 edits, erase and the step bit before the boundary | "Edits, pattern changes, track length changes, erase and mutes made before a step's boundary apply to that step" | a step whose early part played a hit before `n·96` follows that early part at boundary n: it plays all its hits or none, whatever `stepOn()` and `Step.prob` read then. An erase or a probability edit made before boundary n does not stop its late hits, and a step switched on then does not add them. The step data (velocity, note, p-locks) is still read at boundary n. Exceptions: a late change of the pattern or of the track length (the late part is that of the step actually reached) and skip-once (it drops the hits not played yet). A step without an early hit keeps the spec rule | N3 (amended, E1.17 Part D); plan, execution amendment 17 |
| §6.5 first step of the next pattern | generated with `peekNextPattern()`; "regeneration at the boundary corrects any mismatch" | early hits played from the prediction stay played; when the pending pattern changes after them, the late part comes from the step actually reached, so that step plays partly from each pattern (`T11_late_change_of_the_pending_pattern`) | N3 (amended, E1.17 Part D, first exception); plan, execution amendment 4 |
| §4.2 interface | `seqc_command()` mailbox in the core; `peekNextPattern()`, `songPatternAt()`, `songMode()`; `transport(bool)` | the mailbox in `sequencer.c`; direct functions; the callbacks of N5 with `seekPosition()` and `transport(seqc_transport_t)` | N5, N7, N13.1, N13.3 |
| §4.3 execution context | `seq_tick()` from the TIM2 interrupt at about 36 kHz | from `soundengine_process()` in the main loop, and from TIM2 only while the SD card is busy; a nesting guard | N12 |
| §4.3, §6.7 skip-once and one-shot roll | other contexts reach the core only through the mailbox; `seq_addNote()` calls `seqc_skipOnce()` | two more mailbox slots, the skip-once markers (one per track) and the one-shot roll; inside `seq_tick()` (the one-shot roll, MIDI recording) `seq_postSkipOnce()` calls `seqc_skipOnce()` at once instead of posting. `seq_addNote()` posts the marker whenever the phase is at least 50% | plan, epic E2, decisions 4 and 13; N18.1; execution amendment 17 (leftover [113]) |
| §4.1 platform layer | `seq_platform.c` | `seq_syncq.c` (host-tested queues and decoding) and `seq_platform.c` (DWT, instances) | plan, epic E2, decision 3 |
| §5 MIDI Start | `pos` 0, armed | `pos` one clock before step 0, armed; the first clock brings it to 0 | N4 |
| §5 SPP | accepted while stopped or armed; `pos` becomes SPP × 96 ticks | accepted in the slave modes only, while stopped or armed (in internal sync it is ignored and counted in `syncIgnored`); the last clock position becomes SPP × 96 minus one clock (16 ticks, 96 for the jack at 4 PPQ), so the next clock plays step SPP | plan, task E1.11; N4 (`posAtLastClock = SPP·96 − 16`) |
| §5 reset input of the clock jack | back to step 0 at the next pulse | a run gate, as in fw 1.7: active stops and rewinds, released arms a start | N4, N13.2 |
| §5 transport messages out | MIDI Start at an internal start; it says nothing about the slave modes (fw 1.7 sent Start and Stop there too) | MIDI Start, Stop and Continue go out only in internal sync; none in the slave modes, also not for the reset gate | N18.2 |
| §5 dropout | 500 ms | `max(500 ms, 2 × the expected clock interval)` | N15 |
| §5, §11 tempo estimate | the mean of the last 24 clock intervals (4 at 4 PPQ); intervals more than 50% off the estimate are left out | the slope of a least-squares fit of the clock stamps against the clock count: clocks with one stamp form one transfer, one point of the fit; windows of 24 and 96 clocks (at most 16 s; 4 pulses at 4 PPQ), the long one cut to the short one on a step the stamps cannot explain; outlier tests on the line (MIDI: half a clock or 1.25 step amplitudes; the jack: the interval test with the line re-anchored), none in the first 12 clocks; a MIDI clock less than `SEQC_TEMPO_JOIN_MS` (2 ms) after the last transfer joins it | N21 (E1.15, amended in E1.17); plan, execution amendments 8, 13–15 |
| §5 tempo jumps | (the ±50% band only) | after 4 outliers in a row on one side (2 for the jack at 4 PPQ) the fit restarts from them; on MIDI, in-band steps of about 15% or more restart it too (`tempoRestarts`). N16's rule for a 0 interval no longer applies | N16 (E1.14), N21 |
| §5, §11 "the position still follows every clock" | every clock feeds the estimate | the position counts every clock, but the fit has its own clock count and leaves out one spurious MIDI clock (a row early in the count that a quarter band explains) or one pulse of a jack double trigger (the sum test, merged tentatively and undone at the next pulse if it was a tempo jump) | N21 (amended in E1.19); plan, task E1.19 |
| N21 requirement | within 0.5% of the clock tempo after two beats | near simple clock-to-buffer ratios not reached: over 20 buffer phases up to 0.97%, 1.35% and 2.80% at 256, 512 and 1024 samples (H6, H7 take these as limits); a lost DIN clock costs about 1.5% for about 4 beats; on the clock jack, a pulse lost right after a jump to 2–2.7 times the tempo, a double trigger followed by a pulse about a third of a pulse early, or two double triggers on adjacent pulses put the tempo off by up to 50% for a few pulses (E4.1 F4 and F49, accepted) | N21 (Limit, costs; E1.19 jack bullet) |
| §5, §11 dropout | the position holds | the position holds and the pending note-offs are sent | N17 (E1.14) |
| §5 between clocks | interpolation clamped to the next clock, "so the position never runs ahead of the incoming clock" | the clamp is included (N9), so the step that begins at the next clock plays when the fitted tempo predicts that clock, before the clock arrives. On the jack at 4 PPQ every pulse begins a step: a pulse later than the fit predicts no longer delays its step (a swung 4 PPQ clock plays straight steps), and a clock that stops without a reset plays one step more than it sent pulses. At 24 PPQ and on MIDI clock the error stays within one clock | N9 (the clamp included); plan, E1 open issue 2; owner question Q11 in the tracker (review F21); the default keeps it |
| §5 clock in PPQ | a command | it restarts the clock count and clears the estimate only in analog sync | plan, epic E1, decision 11 |
| §5 clock out jack | `trigger_clockTick(0, pulseInStep)` | `trigger_clockTick(0, 0)`, after `trigger_setClockPhase()` has set the divider from the position | N8; plan, epic E2, decision 10 |
| §6.4 mutes | an event of a muted track is dropped when it fires | roll events play on muted tracks, as in fw 1.7 | N13.6 |
| §6.5, N6 song position in song mode | the entry is activated with the normal pattern-change side effects | inside the run of the pattern that plays already nothing is activated or reloaded | plan, epic E2, decision 12 |
| §6.6 roll recording | through `seq_addNote()` | at the roll event's own step, with `seq_recordNoteAtStep()` | N11 |
| §6.7 recording phase | `seqc_stepPhase()` × 100 | the same, swing-aware: 0.5 halfway between the swung grid times of steps n and n+1 | N13.5 |
| §6.7, §4.2 skip-once | `seqc_skipOnce(t, s)`, one marker | one marker per track | N18.1 (E1.14) |
| §7.2 SPP | the normal parser path and the mailbox | intercepted in the receive interrupts and queued in order with the realtime events | N14 |
| §4.3, §7.2 Program Change | the sync queues carry the realtime messages only; the parser acts on Program Change (as fw 1.7) | Program Change travels in the sync queues too: the DIN byte decoder and the USB packet decoder queue it with its stamp, and the drain applies it (`seq_setNextPattern()`, the `MIDI_FILTER_PROG_C` and global-channel checks) in stamp order among Start, Continue, Stop, the clocks and SPP, reading the time before every pop; the parser's Program Change case only routes | N14 (amended in E2.9: `fd0afbe`, `23a01ac`); plan, execution amendments 20 and 21 |
| §7.2 USB realtime | CIN 0xF | CIN 0xF, and realtime bytes F8–FF with CIN 0x5 | N18.4 |
| N10 early exit | one 64-bit compare | the compare, and `now` is kept for the queries | plan, epic E1, decision 1 |
| N13.1 transport order | "STOP, START, CONTINUE, ARM and REWIND, always reported in that order" | STOP, then REWIND, then ARM, START or CONTINUE, as `seq_core.h` says | N13 (the header is the reference); plan, epic E1, decision 10 |
| `seq_core.h` | `seqc_masterTrack()` is 0 before the first boundary | after a song position it is the master track of the seeked pattern | plan, epic E1, decision 5 |
| §13.1 T20 | every hit on the same pulse as in fw 1.7; only the tempo error differs | two more differences, which follow from §6.4 and §6.1 and are pinned by tests: the per-track choke drops a spill-over hit after a newer step's early hit (`T20_spill_over_after_a_newer_step_is_choked`), and no spill-over of the last step plays at the start (`T20_no_spill_over_of_the_last_step_at_the_start`) | plan, task E1.13 |
| §13.1 T19 | queue overflow | tested against a second build of the core with a 16-entry queue | plan, epic E1, decision 4 |
| §9.3 CPU page | SHIFT + LOAD/SAVE, then page forward twice | SHIFT + step 12 (the code's real global menu), the 4th press, or the 3rd from a second screen | plan, Part 3 |
| §9.3 what is measured | the work of `seq_tick()`, the render and MIDI parsing in both paths | the same, except the outer piece of work around a kit autoload, so that a pattern change shows no false peak | plan, Part 3 item 9 |
| §9.3 block budget | 313 186 cycles | 313 185 (64 × 216 MHz / 44 140 Hz = 313 185.3) | plan, Part 3 |
| §9.3 the counts shown | the event queue's maximum depth and the overflow counts | the event queue's; the sync queues' overflow count (`seqp_syncOverflows()`) is not on the page | owner question Q9 in the tracker (review F2), plan Part 3 item 1; the default keeps the page as it is |
| §8 `MenuText.h` | changed for the CPU page | unchanged: the page draws its own text | plan, Part 3 |
| §12 user interface | unchanged, except for the splash screen and the CPU sub-page | in the global menu the step buttons without SHIFT do nothing, and page up and down keep step 12 blinking and show the step view | N19 (E2.7) |
| §9.1 host tests | built with `-Wall -Wextra -Werror` | `test_buttons` compiles fw 1.7's `buttonHandler.c` with `-w` and `Core/Inc` as a system include directory; `test_buttons.c` and `tests/stubs/` keep the three flags | N19 (E2.7) |
| §8 changes outside the core | the USB realtime intercept in `usbd_cdc.c`; in `MidiParser.c` the echo, realtime and SPP | also the USB MIDI out: `usb_manager.c/.h`, the new `usb_midiTx.c/.h`, `usbd_cdc.c/.h` (the old ring goes, the end of a transfer starts the next), and in `MidiParser.c` a Song Select that waits for its song number and F4, F5 and F6 routed alone | N20 (E2.8) |
| §4.1, §8 `CortexParser.c` | keeps compiling unchanged, apart from the call sites listed in §8, which does not list it | one new call in the `FRONT_SEQ_TRIGGER_IN_PPQ` handler: `seq_setClockInPpq()` posts the clock-in PPQ to the core through the mailbox of `sequencer.c` (spec §4.2, §4.3) | plan, the E2 Files table and task E2.5 |
| §4.1, §8 files beyond §8's table | `menu.c`, `buttonHandler.c`, `CortexParser.c` and `presetManager.c` keep compiling unchanged, apart from the call sites listed in §8; §8 lists the files stage 1 changes | also changed or new: `buttonHandler.c` (SHIFT + step 12 sets the global menu's own button mode, `SELECT_MODE_MENU`); `presetManager.c` (two dead comment lines that named removed variables go, no code); `Core/Inc/Soundengine/cpuMeter.h` and `Core/Src/Soundengine/cpuMeter.c` (new: the CPU meter), with the hooks `soundengine_cpuBegin()` and `soundengine_cpuEnd()` in `Soundengine.h` and `MENU_CPU_SUB_PAGE` and `TEXT_CPU_METER` in `menu.h`; the headers of §8's files `Uart.h` (`uart_processMidi(maxBytes)`; `midi_process_block` goes) and `TriggerOut.h` (`trigger_setClockPhase()`; dead timing declarations go); the scripts in `tools/`: `check.sh`, `env.sh`, `setup-toolchain.sh`, `check_image.py`, `ci_dry_run.rb`, `fw_checks.sh`, and the tracker and workflow scripts (`tracker_note.py`, `tracker_export.py`, `tools/agent/`). `CortexParser.c`: the row above | `buttonHandler.c`: N19 (E2.7) and the plan's E2 file table; `presetManager.c`: the E2.6 review (`6fe86f0`), comments only (review row R7.3); the CPU meter and `menu.h`: the plan's Part 3 file map (E3.3); `Uart.h`, `TriggerOut.h` and `Soundengine.h`: the plan's E2 file table (E2.2, E2.4–E2.6, E3.3); `tools/`: the plan's file structure (build and tooling: E2.2, E3.1, E3.4, E3.5) and `docs/lichomat/HANDOFF.md` (the tracker and workflow scripts) |
| §10 RAM | about 7.5 KB (review row R6.2 first allowed 8704 B) | 13404 B allowed, at most 391824 B of DTCM + RAM: 8704 B (the event queue, the sync queues and other state), 444 B (the USB MIDI transmit ring and its transfer buffer, N20), 1192 B (N21's tempo fit and its copy for the join), 24 B (E1.17's 101-point fit ring), 2952 B (E1.18's staging buffer, its index and the recorded chokes, N2 amended) and 88 B (E1.19's clock counts and merge state, N21). Measured (§3): +13368 B, 391788 B, 36 B under the bound; the last 56 B are the timed Note Off state of review finding F22 (`seq_voiceOffAt`, 7 × 8 B, and `seq_voiceOffPending`), state of the 8704 B | N2, N20, N21; review row R6.2 (E2.9) |
| §10 flash | about +10–15 KB | about +25 KB, measured +25804 B (§3; review row R6.3 allows up to 30 KB): the core alone has about 22.9 KB of code with N21's estimator, its E1.19 rules and the ring queue with its merge, and the USB MIDI out adds about 0.75 KB | N20, N21; review row R6.3 (E2.9) |

## 2. Intended changes against fw 1.7

"Notes" says whether the release notes list the change (Changed behaviour or Fixed) or why not.

| Change | Where | Notes |
|---|---|---|
| Shuffle moves whole steps, ratchets and shift included (D1) | E1.5, spec §12 | Fixed |
| MIDI clock out and the clock out jack stay straight with shuffle (D1) | E1.8, spec §12 | Fixed, Changed |
| Choke per track (D2) | E1.6, spec §12 | Fixed |
| A spill-over ratchet hit no longer plays after a newer early hit of the same track; at a start, the last step's spill-over no longer plays on step 1 | E1.13 | Changed |
| At a pattern change the early-shifted hits of the first step come from the next pattern's step (`peekNextPattern()`); fw 1.7 took them from step (step+1) % length of the pattern that was playing | E1.7 (execution amendments 4 and 17, leftover [80]) | Changed |
| Exact master tempo (D3) | E1.3, spec §12 | Fixed |
| One MIDI Note On per hit, Note Offs per voice, also for voices that share a channel (D4) | E2.3, spec §12 | Fixed, Changed |
| An incoming MIDI note is echoed once, also in record mode | E2.3, spec §12 | Changed |
| A one-shot roll (a pad, also while stopped) and the echo of an incoming MIDI note get their MIDI Note Off one pulse later, at the current tempo, as the sequencer's notes do (fw 1.7: none until the voice's next note, a mute, a stop press or a pattern change) | E4.1 (review F22), spec §12 | Fixed |
| Muting a track turns off that track's MIDI note (fw 1.7 turned off another voice's) | E2.3 | Changed |
| A slave clock dropout sends the pending MIDI note-offs | E1.14 (N17) | covered by Fixed (D4: no hanging notes) |
| Shifted notes keep their own p-locks and velocity, also across a pattern change (D5) | E1.5, E1.7, spec §12 | Fixed |
| A note recorded slightly early does not play twice (D6) | E1.9, E2.2, spec §12 | Fixed |
| The recording quantize follows the shuffle | E1.9 (N13.5) | Changed |
| Roll notes are recorded at the step where they play (fw 1.7: the current step) | E2.2 (N11) | Changed |
| A periodic roll hit sends one MIDI Note On: the note or its override with velocity 100 (`ROLL_VOLUME`, the velocity the voice plays). fw 1.7 sent two, the note with velocity 100 and then the note or its override with the volume of the track's current step; the one-shot roll keeps that volume (N18.3) | E1.9, E2.3 (spec §6.4, §6.6) | Changed |
| Notes and p-lock automation played before the first step (for example while a slave is armed and waits for its first clock) are not recorded. fw 1.7 wrote them at step index -1: into another track's step 64, and through `seq_setMainStep(…, 255, 1)` about 9.8 KB past `seq_mainSteps`, outside the pattern data; once its armed pulse count had passed half a step, onto step 1 | E2.2 (`seq_addNote()`, `seq_recordAutomation()`; the plan's E2.2 checklist: nothing is written before the first step) | no: a corner of recording while playing |
| Slave mode: fractional tempo, no dropped late notes, SPP (D7) | E1.10, E1.11, E2.4 | Fixed |
| Slave tempo over USB from a computer: clocks that share a USB transfer give the clock tempo, not the packet rate; one spurious clock or a jack double trigger no longer skews the tempo | E1.15, E1.19 (N21) | covered by Fixed (D7) |
| A lost DIN clock costs about 1.5% of the displayed and interpolated tempo for about 4 beats; tempo changes over buffered USB settle in 1 to 3 beats | E1.15 (N21) | no: under the display's resolution most of the time; the step grid counts clocks and stays exact |
| As a slave the LXR plays up to the next clock at the measured tempo: at 4 PPQ a late pulse does not delay its step, so a swung 4 PPQ clock plays straight (use the LXR's shuffle), and one more step plays when the clock stops without a reset (fw 1.7 played each step at its pulse) | E1.10 (N9; plan E1 open issue 2) | Changed; owner question Q11 |
| MIDI Continue resumes where playback stopped or where SPP put it (fw 1.7 restarted from step 1) | E1.11 (N4) | Changed |
| A received Continue is reported as CONTINUE: the clock out reset (`trigger_reset(1)`), the save-timer abort and the start/stop button, with no MIDI message out (sync modes are slave modes); at the start position (after a rewind) it is a START. fw 1.7 handled it as a start (`seq_setRunning(1)`: MIDI Start out, indices back to step 1) | E2.2 (N4, N18.2; execution amendment 20) | covered by Changed (the Continue and the slave transport rows) |
| A stop request that finds the sequencer stopped reports no STOP. The stop button pressed again only rewinds, if the position is not at the start, and sends all notes off: no MIDI Stop, no clock out reset, no MTC check, no pattern save timer and no start/stop button message, which fw 1.7's `seq_resetPosition()` and `seq_setRunning(0)` gave on every press. A MIDI Stop while stopped does nothing (fw 1.7 touched the save timer) | E2.2 (execution amendment 20) | no: the sequencer is stopped already; all notes off still goes out |
| SPP is followed while stopped; in song mode the pattern at that position loads at once | E1.11, E2.2 (N6) | Changed |
| After a song end the next start plays the song from its first entry, and that entry once | E1.4, E2.2 (N13.11) | Changed |
| In song mode a start from the LXR (the start button, an MTC start) after a stop that kept the position (a MIDI Stop received as a slave, the MTC timeout) plays the song from its first entry: the start rewinds, and the rewind puts the song pointer before entry 0. fw 1.7 reset the pointer only in `seq_resetPosition()` (the stop button, a received Start, the reset gate), so it went on with the entry after the one that was playing | E1.4, E2.2 (N4, N13.11) | Changed |
| Song end sends all notes off, and MIDI Stop as master; the start/stop button shows the stop | E2.2 | Changed |
| A looping song whose first entry is a marker (song end or loop) loops to pattern 1; fw 1.7 took the marker value (64 or 65) as the pattern number and indexed past the patterns | E2.2 (`seq_cbPatternEnd()`) | no: only a song that starts with a marker |
| The start/stop button and LED follow every start and stop, also from MIDI and the song end | E2.2 | Changed |
| As a slave the LXR sends no MIDI Start, Stop or Continue (fw 1.7 answered a received Start with Stop and Start, and a Continue with Start), also not for the reset gate in sync mode 1 | E2.2, E2.5 (N18.2) | Changed; owner question Q7 |
| In `EXT` sync a MIDI Start makes the next jack pulse start playback | E1.11 (N13.10) | Changed |
| At 2 PPQ the clock out jack gives 2 PPQ in the slave modes too (fw 1.7: 4), and after a Continue or SPP its pulses stay on the step grid | E2.2, E2.5 | Changed |
| A reset gate edge is no longer lost: `EXTI9_5` clears its pending bit once, before it reads the pin (fw 1.7 cleared it again after the read, which could erase an edge and leave the run gate stopped or running against the gate's level) | E2.5 (`ac08e38`, added in E2.9) | Fixed |
| MIDI input (D8): stamped realtime and SPP, one parsing context, USB MIDI parsed 8 messages per `seq_tick()` instead of 1 per main-loop pass, USB clock keeps going while the card is busy | E2.4 | Fixed |
| A Program Change sent just before a Start selects the pattern that the Start plays, also when both come in one USB transfer | E2.4 review (`fd0afbe`, N14 amended in E2.9) | Unchanged behaviour, kept through the sync queues (the release notes say so, N14) |
| Realtime bytes inside a DIN message no longer break the message | E2.4 | covered by Fixed (D8) |
| MIDI clock, Start, Stop, Continue, Active Sensing and Reset passed on from DIN go out alone on DIN thru (routing M2M, A2M, M2A); fw 1.7 sent them with the data bytes of the last channel message (a clock after a Note On went out as `F8 3C 64`), which a receiver took as running status, so that message repeated on every clock | E2.4 (`midiParser_parseUartData()`; epic E2, decision 9) | Fixed |
| A full USB MIDI input queue drops new messages instead of wiping the queue | E2.4 | no: only under overload |
| The reset gate's side effects (all notes off) run up to one `seq_tick()` later, outside the interrupt | E2.5 | no: under 1.5 ms |
| The clock in PPQ (`PAR_PRESCALER_CLOCK_IN`) is posted to the core when the front panel sends a new value | E2.5 | no: no audible change |
| The one-shot roll triggers up to one `seq_tick()` later, in the core's context; its MIDI Note On has the current step's volume and the voice takes that step's p-locks, and before the first step (while stopped at the start) it has velocity 100 (`ROLL_VOLUME`) and no p-lock | E2.2, E2.3 (N18.3) | no: under 1.5 ms; the velocity is as in fw 1.7 while running; while stopped fw 1.7 read another track's step 64 (velocity and p-locks), LICHOMAT sends 100 and no p-lock |
| A change of the shuffle or of a step's timing while the step has started to play early applies from that step's next turn | E1.5 | no: a corner of editing while playing |
| A step whose early-shifted hits have played plays its late hits too, even if it is erased, switched off or its probability is changed before its boundary; a step switched on then does not add them | E1.17 Part D (N3 amended) | no: a corner of editing while playing |
| An edit of a step's velocity, note or p-locks after its hits were queued (at its boundary, or at the boundary before for early hits) no longer reaches those hits, because the events carry their own data; fw 1.7 read the step at every pulse, so later ratchet hits took the edit | E1.5 (spec §6.1) | no: a corner of editing while playing |
| In the global menu a step button pressed without SHIFT no longer sets the length of every track | E2.7 (N19) | Fixed |
| In the global menu page up and down keep step 12 blinking and show the track's steps (fw 1.7: the Last Step page's LEDs) | E2.7 (N19) | Fixed |
| USB MIDI out: the CIN of USB MIDI 1.0 for every message (system common messages keep their data, SysEx goes out byte by byte), nothing sent past a buffer, nothing lost while a transfer is under way, a full ring (128 packets) drops and counts new packets, nothing kept for a computer that does not read | E2.8 (N20) | Fixed |
| A Song Select passed on from DIN, to USB or to DIN out, carries its song number | E2.8 (N20) | Fixed |
| A Tune Request (F6), and F4 and F5, passed on from DIN go out alone, without the data bytes of the message before them, and stray data bytes after them no longer act as a repeat of that message (they end running status) | E2.8 (`3771319`, N20 amended in E2.9) | Fixed |
| The start-up screen shows `LICHOMAT` and `v0.1 unofficial`; the global menu has a CPU page | E3.1, E3.3 | Changed, New |
| The update file on the SD card is `LXR_V2_LICHOMAT_0.1.img` | E3.1 | Changed (install instructions) |

## 3. Resources

| | fw 1.7 | stage 1 | change |
|---|---|---|---|
| ITCM | 15000 B | 13128 B | -1872 B |
| DTCM | 107388 B | 107388 B | +0 B |
| RAM | 271032 B | 284400 B | +13368 B |
| image | 274688 B | 300492 B | +25804 B |

Spec §10: about +7.5 KB of DTCM and RAM (review row R6.2 allows +8704 B, and 4700 B more: 444 B for the
USB MIDI transmit ring of E2.8 (N20), 1192 B for N21's tempo fit, 24 B for E1.17's 101-point fit ring,
2952 B for E1.18's staging buffer (N2) and 88 B for E1.19 (N21), so at most 391824 B), about +10–15 KB
of flash, and no net growth of ITCM.

Measured at `38681b8`, the code after the fixes of the E4.1 review (the commits after it change
only documents), with Arm GNU 14.3.rel1: DTCM + RAM is 391788 B, +13368 B over fw 1.7's 378420 B
and 36 B under R6.2's 391824 B; the image is +25804 B, under R6.3's 30 KB. Against `9e8ae47`, the
code the review started from (391732 B, image +25260 B), the fixes add 56 B of RAM, the timed Note
Off state of finding F22 (`seq_voiceOffAt`, 7 × 8 B, and `seq_voiceOffPending`), and 544 B of
code.
