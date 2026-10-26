# LICHOMAT stage 1: review

The review of stage 1 (tracker E4.1), before the hardware checks and the release. Every row of the
checklist is checked against the code and gets a result; every problem found becomes a finding, and
findings are fixed as described under [Fixing findings](#fixing-findings). The review is complete when
no row is `open` and no finding is `fix`:

```sh
grep -cE '\| (open|fix) \|' docs/lichomat/reports/stage1-review.md    # must print 0
```

| | |
|---|---|
| Spec | `docs/superpowers/specs/2026-10-23-timing-core-design.md` |
| Decisions beyond the spec | `docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md`, N1–N21; they win where the two differ |
| Base | `a43cd4f`, the official firmware 1.7 |
| Changes reviewed | `git diff a43cd4f..HEAD` on branch `timing-core` |

## How to review

1. From the repository root run `tools/check.sh`: it builds `build/host-tests` and `build/release`,
   which the rows use. Then run every row marked **A** (automated), in a shell that has sourced
   `tools/env.sh` (bash or zsh) for `cmake` and `ctest`, and write the result into the row: `pass`, or
   `F<n>` with a new finding. Commands with a pipe are under [Commands](#commands).
2. Review the rows marked **M** (manual) by reading the code. They are grouped by dimension, R1–R11,
   so that one reviewer per dimension can work in parallel; give each reviewer the spec, the notes,
   this file and `git diff a43cd4f..HEAD`, and ask for findings with file and line.
3. Fix the findings (below), then rerun the rows a fix touches.

A result is `pass` only if the row's "Pass when" holds as written. If it holds with a reservation,
write the finding anyway and decide it as `accepted`.

## Checklist

### R1 Spec and notes conformance

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R1.1 | M | Spec §4–§11, item by item, against the code | every item is implemented, or differs as a note N1–N21 says | open |
| R1.2 | M | Notes N1–N21, item by item, against the code | every note is implemented as written; for N13 the reference is `seq_core.h`, as N13 says (the `transport()` order is STOP, then REWIND, then ARM, START or CONTINUE) | open |
| R1.3 | M | The comments of `seq_core.h` against `seq_core.c` | they describe what the code does | open |
| R1.4 | M | Deviations from the spec, against `docs/lichomat/reports/stage1-report.md` | the report lists each deviation with the note or plan decision that allows it, and every intended change against fw 1.7; a deviation in the code that is in neither list is a finding | open |

### R2 Defects D1–D8 (spec §3)

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R2.1 | A | D1 swing: `ctest --test-dir build/host-tests -R '^T[35]$' --output-on-failure` | T3 and T5 pass | open |
| R2.2 | A | D2 choke: `ctest --test-dir build/host-tests -R '^T7$' --output-on-failure` | passes | open |
| R2.3 | A | D3 tempo: `ctest --test-dir build/host-tests -R '^T1$' --output-on-failure` | passes | open |
| R2.4 | A | D4: `grep -n 'seq_sendMidiNoteOn' Core/Src/Soundengine/MIDI/MidiVoiceControl.c` | no output | open |
| R2.5 | M | D4: the trigger callback and `seq_triggerVoice()` send one Note On each; `midiParser_noteOn()` echoes once (N11) | as stated | open |
| R2.6 | M | D5: the trigger callback applies the event's own p-locks and velocity, never the current step index | as stated | open |
| R2.7 | A | D6: `ctest --test-dir build/host-tests -R '^T14$' --output-on-failure`, and `grep -n -e seq_postSkipOnce -e seqc_skipOnce Core/Src/Soundengine/Sequencer/sequencer.c` | test passes; `seq_addNote()` hands the marker to `seq_postSkipOnce()`, which calls `seqc_skipOnce()` at once inside `seq_tick()` and posts it otherwise, and `seq_applyMailbox()` passes a posted marker to `seqc_skipOnce()`, one per track | open |
| R2.8 | A | D7: `ctest --test-dir build/host-tests -R '^T1[678]$' --output-on-failure` | T16, T17 and T18 pass | open |
| R2.9 | A | D8: `grep -rn -e uart_processMidi -e usb_getMidi -e midiParser_parseMidiMessage Core/Src` | apart from the definitions, comments and the parser's own calls in `MidiParser.c`, the only calls are in `seq_parseMidiInput()` in `sequencer.c`, which only `seq_tick()` calls | open |

### R3 Host tests

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R3.1 | A | `cmake -S tests -B build/review-host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSEQC_REQUIRE_ALL_TESTS=ON && cmake --build build/review-host && ctest --test-dir build/review-host` | `100% tests passed, 0 tests failed out of 27` | open |
| R3.2 | A | `ruby tools/ci_dry_run.rb host-tests` | ends with `dry run of host-tests: 3 steps passed` | open |
| R3.3 | M | Each spec test T1–T20 asserts what spec §13.1 asks for (tolerances, scenarios) | every test does | open |
| R3.4 | A | Determinism: run R3.1's `ctest` twice | the same tests pass both times | open |

### R4 Execution contexts (spec §4.3, N7, N12, N13.9)

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R4.1 | A | Calls into the core, see [Commands](#commands) | every call is in the `seq_tick()` path of `sequencer.c` or in its callbacks, or is `seqc_init()` in `seq_initCore()` at boot, before `seq_tick()` runs, or `seqc_stats()` in `soundengine_cpuEnd()`, or `seqc_skipOnce()` in `seq_postSkipOnce()`, which calls it only while `seq_inTick` is set | open |
| R4.2 | M | The USART3, `USBD_CDC_DataOut`, EXTI4 and EXTI9_5 handlers | they only take a stamp and push sync events (and keep writing the RX FIFO) | open |
| R4.3 | M | The mailbox of `sequencer.c` | each slot is written with interrupts masked and applied at the top of `seq_tick()` | open |
| R4.4 | M | `seq_tick()` nesting guard (N12) | a nested call skips the mailbox, the drain, `seqc_process()` and MIDI parsing | open |
| R4.5 | A | `grep -rn 'GetRngValue' Core/Src --include='*.c'` | only the seq_tick and render context calls it | open |
| R4.6 | A | `grep -n 'seqc_' Core/Src/menu/menu.c Core/Src/buttonHandler.c Core/Src/ledHandler.c Core/Src/Soundengine/MIDI/CortexParser.c` | no output: the UI reads glue copies and `cpuMeter_get()` only | open |

### R5 Real time and CPU

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R5.1 | M | `seqc_process()` early exit (N10), and `seq_tick()` with nothing to do | nothing due: the 64-bit compare and the store of `now` (epic E1, decision 1), then return; `seq_tick()` without a post or a queued event masks no interrupt and refreshes only the copies that changed (epic E2, decision 11) | open |
| R5.2 | M | Interrupt handlers | no loops over queues or patterns, no blocking calls | open |
| R5.3 | A | `ctest --test-dir build/host-tests -R '^cpumeter$' --output-on-failure` | passes | open |

### R6 Memory budget (spec §10, N2, N14)

The fw 1.7 baseline, as the `memory:` line of `tools/check.sh` reports it: ITCM 15000 B, DTCM 107388 B,
RAM 271032 B, image 274688 B.

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R6.1 | A | `tools/check.sh`, the `memory:` line | ITCM fits (≤ 16384 B) | open |
| R6.2 | A | the same line | DTCM + RAM is at most 391824 B: the baseline 378420 B plus 8704 B (spec §10: the event queue, the sync queues and other state), 444 B (the USB MIDI transmit ring, N20), 1192 B (N21's tempo fit), 24 B (E1.17's 101-point fit ring), 2952 B (E1.18's staging buffer, N2 amended) and 88 B (E1.19's clock counts and merge state, N21); measured at `3f2a038`: 391660 B | open |
| R6.3 | A | the same line | the image grew by about 24 KB: 299092 B at `3f2a038` against the baseline's 274688 B, +24404 B (the core alone has 22.4 KB of code: `seq_core.c.obj` `text` 22898 B, measured with the firmware's compile command, against 15.7 KB in E1.14 Step 8, with N21's estimator and its E1.19 rules and the ring queue with its merge; the USB MIDI out of E2.8 adds 752 B; spec §10: +10–15 KB); more than 30 KB is a finding | open |

### R7 Compatibility (G8: no file format changes)

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R7.1 | A | `ctest --test-dir build/host-tests -R '^layout$' --output-on-failure` | passes | open |
| R7.2 | A | `git diff --stat a43cd4f -- Core/Inc/Parameters.h Core/Src/Preset Core/Inc/Preset` | only `SeqStep.h` and `SeqStepTypes.h` (E1.1, E1.2), and any other change is a reviewed one that does not touch file I/O | open |
| R7.3 | M | `presetManager.c` reads and writes | unchanged sizes and order for kits, patterns, songs, projects and `glo.cfg` | open |

### R8 MIDI behaviour (spec §7, N11, N14)

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R8.1 | M | Realtime and SPP intercepts in the USART3 handler and in `USBD_CDC_DataOut` | the bytes still reach the RX FIFO and the USB message queue, so routing and thru work | open |
| R8.2 | M | Sync event gating | the RX filter `MIDI_FILTER_SYSTEM` and the sync mode gate each event once, at the drain or in the core (N4) | open |
| R8.3 | M | SPP (N14) | it travels in the sync queues, in order with Continue and Clock | open |
| R8.4 | M | Note-offs | stop and rewind send the pending note-offs; muting a track sends the note-off of that track's voice | open |
| R8.5 | M | USB MIDI out (`usb_sendMidi()`, `usb_flushMidi()`, `usb_midiTxComplete()`, `usb_midiTx.c`; E2.8, N20) | every packet has the CIN of USB MIDI 1.0 table 4-1; the ring and the endpoint are used only with interrupts masked; nothing is sent past a buffer; nothing waits for a host that is not there or has stopped reading | open |

### R9 Build and code hygiene

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R9.1 | A | `tools/check.sh` | exit 0 and `== compiler warnings: 0` | open |
| R9.2 | A | `ruby tools/ci_dry_run.rb firmware` | ends with `dry run of firmware: 5 steps passed` | open |
| R9.3 | A | `usbd_cdc.c` is built with `-w`: compile it once with `-Wall`, see [Commands](#commands) | `usbd_cdc.c: no warnings without -w` | open |
| R9.4 | A | Line endings, see [Commands](#commands) | Core sources CRLF only (no mixed files); tests, tools, docs, CI and CMake files LF only | open |
| R9.5 | A | Licence headers of new files, see [Commands](#commands) | no output | open |
| R9.6 | A | `grep -rn -e seq_processFlam -e seq_calcDeltaT -e seq_prescaleCounter -e trigger_clockReceived -e clockSync Core` | no output, or only comments that explain the removal: the pulse machine and `clockSync` are gone (spec §8) | open |
| R9.7 | M | Debug leftovers | no `printf`, test hooks or commented-out new code in the firmware | open |

### R10 Branding and licence (G7, NOTICE.md)

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R10.1 | A | `python3 tools/check_image.py build/release` | `image: LXR_V2_LICHOMAT_0.1.img OK …` | open |
| R10.2 | A | `grep -c 'unofficial custom firmware for the Erica Synths LXR-02' README.md` | at least 1 | open |
| R10.3 | M | README and release notes | LICHOMAT is never presented as official, and "Erica Synths" and "LXR" appear only in truthful references | open |
| R10.4 | M | Source headers | new files: `Copyright (C) 2026 Tomasz Krzywicki`, SPDX and the NOTICE.md reference; modified files keep their headers | open |

### R11 Documentation

| Row | Kind | Check | Pass when | Result |
|---|---|---|---|---|
| R11.1 | M | README: install, rollback and build instructions | they match the code and the tools | open |
| R11.2 | M | `docs/lichomat/HANDOFF.md` and the notes | current; every new tool is mentioned where it is used | open |
| R11.3 | M | Tracker | every task of the plan up to E3.5 done, E1.14–E1.19, E2.7, E2.8 and E2.9 included; a task the owner moved out of stage 1 is marked so in the tracker and named in the release notes; E4.1 in progress; questions current | open |

## Commands

R4.1, every call into the core outside `seq_core.c`:

```sh
grep -rn 'seqc_[A-Za-z]*(' Core/Src --include='*.c' | grep -v 'Sequencer/seq_core.c'
```

R9.3, `usbd_cdc.c` without `-w` (it is part of `usb_midi`, which is built with `-w`):

```sh
python3 - <<'EOF'
import json, shlex, subprocess
db = json.load(open("build/release/compile_commands.json"))
e = next(x for x in db if x["file"].endswith("Class/CDC/Src/usbd_cdc.c"))
args = [a for a in shlex.split(e["command"]) if a != "-w"]
i = args.index("-o"); args[i + 1] = "/dev/null"
args.insert(1, "-Wall")
r = subprocess.run(args, cwd=e["directory"], capture_output=True, text=True)
print(r.stderr.strip() or "usbd_cdc.c: no warnings without -w")
EOF
```

R9.4, line endings (each loop must print nothing):

```sh
git diff --name-only a43cd4f -- Core ':!Core/CMakeLists.txt' | while read -r f; do [ -f "$f" ] && file "$f"; done | grep -v 'with CRLF line terminators'
git diff --name-only a43cd4f -- tests tools docs .github CMakeLists.txt Core/CMakeLists.txt README.md | while read -r f; do [ -f "$f" ] && file "$f"; done | grep 'CRLF'
```

R9.5, licence headers (must print nothing):

```sh
git diff --name-only --diff-filter=A a43cd4f -- Core tests tools .github | while read -r f; do grep -L 'SPDX-License-Identifier: GPL-3.0-only' "$f"; done
```

## Fixing findings

1. Add the finding to the table below with the next number, the row, the file and line, what is
   wrong, and its severity: `blocker` (wrong behaviour, a broken build or a broken file format),
   `major` (a spec or note requirement not met) or `minor` (anything else).
2. Decide it:
   - `fix`: fixed now;
   - `accepted`: a known limitation, listed in the release notes (E4.3) under "Known limitations";
   - `question`: the owner decides. Add it to `questions` in `docs/lichomat/tracker.json` with the
     reversible default you keep working with (HANDOFF.md).
   Blockers and majors are `fix`, unless the owner has to decide.
3. For each `fix`, test first:
   - logic of `seq_core` or `cpuMeter`: a host test in `tests/` that fails, named after the spec test
     it extends (`TEST(T7_...)`), or after the module (`TEST(cpumeter_...)`);
   - firmware glue: the smallest check that fails, such as a `grep` on the source, a build that shows
     the warning, or a host test of logic that can be moved out of the glue.
   Run it and see it fail.
4. Fix it with the smallest change; run the test, then `tools/check.sh` (zero warnings).
5. Commit each finding on its own, adding the files by name:
   `git commit -m "Fix F<n>: <what was wrong>" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.
6. Set the finding to `fixed <short commit>` and rerun the checklist rows it touches.

## Findings

| F | Row | Where | Finding | Severity | Status |
|---|---|---|---|---|---|

## Result

Filled in when the review is complete: the reviewed commit, the date, and the counts of findings by
severity and status.
