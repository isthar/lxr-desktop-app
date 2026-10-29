# LICHOMAT Stage 1b, R2 and R3: the renderer and the goldens — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build tier 1 of spec §5, `lichomat-render` (options, card copy, timeline, event scripts, outputs, exit codes, `--seed-card`, `--param`, the save events), then the golden tests of §8 on it (card fixtures and `make.sh`, G1–G10 with expectations, `goldens.json`, `tools/emu_golden.py`, `emulator.yml`, the ping-pong scenario, the `tools/check.sh` step). ctest in `emu/` then covers M14, M16, M17 and M23 of §12.1 and the goldens of §12.2, on top of R1's 103 ids.

**Architecture:** The renderer is one process per render on the virtual device of R1. `emu/render/` holds it; only `render_fw.c` includes firmware headers and calls the firmware, always through `emu_directCall()` at a pass boundary. The other renderer files see `emu_core.h` and `emu_hostfs.h` only. A second executable, `lichomat-render-test`, is the same sources with `EMU_RENDER_TEST_HOOKS=1`, which adds the test-only options of §5.3's last table row. The host tests of the renderer are a Python driver (`emu/tests/render_test.py`) that runs both binaries; the goldens are run by `tools/emu_golden.py`, one ctest id per golden.

**Tech Stack:** C11 (gnu11) with Apple clang 17 from the Command Line Tools, CMake 3.31 and Ninja, ctest, UBSan (ASan in CI only), pthreads, Python 3 standard library (`wave`, `zlib`, `json`, `subprocess`), bash, Ruby for the CI dry run, GitHub Actions on `macos-15`.

---

The spec is `docs/superpowers/specs/2026-10-27-emulator-design.md`, accepted with the defaults of Q12–Q19. A `§` below is a section of it; a `P`-section is a reference section of this plan. R1's end state is in `docs/superpowers/plans/2026-10-27-emulator-r1b-virtual-device.md`, section "The end state, which R2 starts from", and in `docs/lichomat/reports/M8-emulator-R1.md`.

## Rules for every task

- **Where.** Work in the worktree `/Users/tk/Code/lxr/build/wt-emulator`, branch `emulator`, head `22827f7` at the start. Every Bash call starts in a new shell: begin each command with `cd /Users/tk/Code/lxr/build/wt-emulator &&`, and add `source tools/env.sh >/dev/null &&` where it needs `cmake`, `ctest` or `ninja`.
- **No `Core/` changes.** No task needs one. The three fw 1.7 bugs that R1 and the planning of R2 found are fixed on `emulator` after R2.4, with the owner's approval, as firmware changes of §7: F12 (`7f5732f`, Q23), F11 (`7399c7d`, Q21) and F6b (`cf8fb4d`, Q22). A firmware bug found here is stopped around (an ignorelist, a fixture that avoids it) only where this plan says so; anything else stops the task and goes to the controller as an owner question (§7, "Further sanitizer findings").
- **`tools/`** changes only in Task R3.2 (`tools/emu_golden.py`, new) and Task R3.6 (`tools/check.sh`, `tools/ci_dry_run.rb`).
- **Files.** New files carry the GPL-3.0-only header of R1's files (`Copyright (C) 2026 Tomasz Krzywicki`, the SPDX line, the NOTICE.md sentence; `#` comments in scripts). Everything under `emu/` and `tools/` uses LF.
- **Sanitizers.** The Debug default is `LICHOMAT_EMU_SANITIZE=undefined`. Never configure `address` locally: ASan binaries hang in this sandbox. ASan runs in CI: `emulator.yml` (Task R3.6) runs on every push of `emulator`, which Q19 allows.
- **`time()`.** `Core/Src/graphic.c:25` defines `float time`, which takes the place of libc `time()` in every binary that links the firmware. Never call `time()`; wall time comes from `clock_gettime(CLOCK_MONOTONIC, …)`.
- **Q22, fixed by F6b.** A voice above 44,140 Hz (coarse pitch times the pitch envelope, up to ×33) made `freq2PhaseIncr1024()` (`Oscillator.c:47–50`) convert a float of 2^32 or more to `uint32_t`, which aborted a Debug build. F6b (`cf8fb4d`) gives it and `freq2PhaseIncr32767()` F6's `f2u32_sat()`, and `M15_f9_band10` plays U8's recipe (414 kHz) under UBSan. Fixture scripts may raise pitches; a UBSan report from `Oscillator.c` is a new finding.
- **Q21, fixed by F11.** A power-on with a knob off 0° no longer reads the morph target through NULL (F11, `7399c7d`; `M15_f11_knobs_at_boot`). The renderer still powers on with the knobs at 0° (`emu_configDefaults()`; §4.6, §6.6), so that no render depends on a knob's start; no option sets a knob angle.
- **Measured values.** A value this plan marks *measured* was measured while writing it, at `22827f7`. Every other number a task must find (a hash, a threshold, a time) the task measures, checks against the rule this plan gives for it, and writes into the commit message. Never widen a range to make a test pass; report a value that breaks its rule.
- **Git.** `git add` names each file; never `git add -A` (the worktree shows `.toolchain` as untracked). Never `--amend`, never push. Each commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Test names.** ctest runs `<binary> <TEST name>`, and the filter of `tests/seqc_test.h` also runs every test named `<name>_…`. So no `TEST()` name may equal another's name plus `_` and more. This plan keeps the same rule for the Python-driven ids.
- **Parallel ctest.** `ctest -j8` passes as the serial run does (`c253669`). `render_test.py` gives every case its own TMPDIR, `$TMPDIR/lichomat-case-CASE`, which it removes when it is empty; a case looks only at its own work folder and its own TMPDIR.

**EMU-CHECK**, the command every task runs:

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && cmake -S emu -B build/emu -G Ninja > build/emu.log 2>&1 && cmake --build build/emu >> build/emu.log 2>&1; echo "== emu build: $?"; echo "== emu warnings: $(grep -c 'warning:' build/emu.log)"; ctest --test-dir build/emu 2>&1 | tail -n 3
```

Expected: `== emu build: 0`, `== emu warnings: 0`, then `100% tests passed, 0 tests failed out of N` with the task's N.

**The gate** of every task: EMU-CHECK as expected, then `tools/check.sh` prints `100% tests passed, 0 tests failed out of 55`, the image line with `OK`, `== compiler warnings: 0`, and the same `memory:` line that Task R2.1 recorded. From Task R3.6 on, `check.sh` also prints the emulator's ctest line with the task's N.

## Facts measured while writing this plan

A scratch worktree at `22827f7` with a throw-away test file found these. Tasks rely on them.

- **A fw 1.7 read past an array (tracker Q23), fixed by F12.** Selecting a pattern with the step buttons (`buttonHandler.c:1201`) sent `SEQ_REQUEST_EUKLID_PARAMS` with `menu_activePage` (the pattern page, 16) as the track, and `CortexParser.c:887–895` passed it to `euklid_getLength()`, `euklid_getSteps()` and `euklid_getRotation()`, which index `uint8_t[7]` arrays (`EuklidGenerator.c:21–23`, `:171`). UBSan stopped there: `index 16 out of bounds for type 'uint8_t[7]'`. The owner approved F12 (§7), which sends `menu_getActiveVoice()`, as the commented-out argument meant and the other senders do (`7f5732f`; `M15_f12_euklid` runs this walk under UBSan). So the walks of Task R2.5 need no ignorelist.
- **The `--pattern` walk** (Task R2.5) selects P on a fresh boot: SHIFT held + step 16 enters the pattern page (`buttonHandler.c:873–877`), bar− three times sets bank 0, bar+ ⌊P/16⌋ times, then step (P mod 16) + 1. Checked for 0, 5, 16, 21, 47 and 63; the LCD then reads `Select Pattern  ` / `Current:  22    ` for 21.
- **The `--song` walk** selects S: MODE3 (LOAD) held + step 10 opens the load page for songs (`buttonHandler.c:1004–1010`), encoder −64 then +S (`menu.c` clamps at 0 and 63), then a push loads it (`menu.c`, `case SAVE_TYPE_SONG`: `seq_activeSong = seq_songData[S]`). Checked for 0, 2, 5 and 63; the LCD reads `Load: Song      ` / `[  5]           `.
- **`--seed-card`'s steps** (§6.7) on an empty folder give `PROJ00/` with 194 files and 255,398 bytes (`PRJ.NFO` = `Init` and four NUL bytes; `GLO.CFG` 30 bytes, byte 0 = 120, byte 29 = 1; kit 0 is `00-INITK.SND`; the other slot files are `NN-.PAT`, `NN-.SND`, `NN-.SNG`), an empty `TMP/`, and no `PROJ.IDX`.
- **The seeded card is silent:** its patterns are empty. Pressing voice n, then steps 1, 5, 9 and 13, for n = 1…7, then START, gave 56 Note Ons in 4 s at 120 BPM, all `90 30 7F` (channel 1, note 48), and −15.7 dBFS RMS on out 1, digital silence on out 2.
- **A card with only `PRJ.NFO` and a zero `GLO.CFG`** sends no MIDI at all (its TX filter is 0) and renders silence (missing kits are all zero). Tests use the seeded card.
- **Kit transfers.** After a boot without a card, `preset_morphBusy()` returned 1 for 81 passes, up to 17.4 ms after the boot. After a boot from the seeded card, and after a pattern walk on it, it never returned 1. A kit reload (SHIFT + step 13, `buttonHandler.c:884–893`, `preset_loadDrumsetRam(…, 0, 0)`) and `preset_morph()` start one (`presetManager.c:2226`).
- **UBSan's report** ends the process through `abort()`, so a `SIGABRT` handler sees it (a probe program: the handler ran, exit 3).
- **Order inside a block request** (`emu_codec.c:36–42`): `blocks` and `underruns` are counted before the audio sink is called; the tier hook follows the sink.

## File structure

| File | Responsibility | Task |
|---|---|---|
| `emu/render/render_opts.{c,h}` | P1: the options, their checks and defaults | 1 |
| `emu/render/render_main.c` | `main()`, the timeline of P3, the sinks, the hook, signals and the watchdog | 1, grows to 6 |
| `emu/render/render_build.h.in` | Commit, toolchain and version, filled in at configure time | 1 |
| `emu/render/render_card.{c,h}` | §5.2: the card copy, `PROJ.IDX`, the `PRJ.NFO` check, `--keep-card`, removal | 2 |
| `emu/render/render_fw.{c,h}` | Every firmware call: seed, `preset_morphBusy()`, walks' checks, the edit path, saves, counters | 2, grows to 6 |
| `emu/render/render_out.{c,h}` | P5: WAV, MIDI, clock and panel logs, the summary | 3, 4 |
| `emu/render/render_script.{c,h}` | P4: the event-script parser | 4 |
| `emu/render/gen_params.py`, `render_params.h` | The parameter-name table from `Parameters.h` | 5 |
| `emu/tests/render_test.py` | M17, M14 and M23 driver | 1, grows to R3.2 |
| `emu/tests/test_pingpong.c` | M16 | R3.5 |
| `emu/tests/cards/make.sh`, `cards/scripts/*.txt`, `cards/{init,fx,song}/` | P8: fixtures | R3.1 |
| `emu/tests/goldens/*.txt`, `goldens.json`, `emu/tests/golden_selftest.txt` | P6, P7: goldens | R3.2–R3.5 |
| `tools/emu_golden.py` | `check`, `bless`, `diff` (§8.5) | R3.2 |
| `.github/workflows/emulator.yml`, `tools/ci_dry_run.rb`, `tools/check.sh` | §9.2, §9.3 | R3.6 |
| `emu/CMakeLists.txt`, `emu/tests/name_allow.txt`, `emu/tests/drift.{py,json}` | Targets and ctest ids; M21's allow-list; M20's new items | 1, 5, 6, R3.x |

## Task order

| Task | Contents | Spec rows | ctest ids after it (Debug / Release) |
|---|---|---|---|
| R2.1 | Targets, options, `--help`, `--version`, M21 with the renderer | M17 (options) | 105 / 105 |
| R2.2 | Card copy, `--seed-card`, exit code 2 | M17 (card, seed) | 107 / 107 |
| R2.3 | Timeline, t0, WAV, summary core, `--keep-card`, `/TMP`, exit code 5 | M17 (t0, defaults, WAV, card copy, 5) | 112 / 112 |
| R2.4 | Event scripts, timed inputs, MIDI, clock and panel logs, `--stereo` | M17 (script, stereo) | 118 / 118 |
| R2.5 | `--pattern`, `--song`, `--bpm`, `--param`, `param`, `--bars`, the sync fallback | M17 (prelude, params) | 130 / 130 |
| R2.6 | Save events, faults, the watchdog, exit codes 3 and 4, M20's new items | M17 (saves, 3, 4), M20 | 133 / 133 |
| R3.1 | Fixtures, `make.sh` | M23 | 134 / 134 |
| R3.2 | `emu_golden.py`, `goldens.json`, G1, determinism, slicing, speed | M14, G1 | 138 / 139 |
| R3.3 | G2 (seven renders), G3, G4 | G2–G4 | 147 / 148 |
| R3.4 | G5, G6, G7, G8 (two renders) | G5–G8 | 152 / 153 |
| R3.5 | G9 (two renders), G10, the ping-pong scenario | G9, G10, M16 | 156 / 157 |
| R3.6 | `emulator.yml`, its dry run, the `check.sh` step, the end check | §9.2, §9.3 | 156 / 157 |

Release has one id more from R3.2 on: `M14_speed` exists only in a Release build (§9.2: Release "runs M14's speed check").

From R2.5 on the counts hold four ids that the rows of R2.1–R2.4 leave out: `emu_tmp_setup` and `emu_tmp_clean`, the temporary-folder fixture that `e37ae9e` added after R2.2, and `M15_f11_knobs_at_boot` and `M15_f12_euklid`, the tests of F11 and F12. R2.4 ended at 120 ids, and the three firmware changes and the parallel-ctest fix (`c253669`) leave 122 before R2.5, in Debug and in Release, serial and with `ctest -j8`.

---

## P1. The command line (§5.1)

```
lichomat-render --card DIR --out OUT.wav [options]
lichomat-render --seed-card DIR
lichomat-render --version | --help
```

Every option takes its value as the next argument. A later occurrence replaces an earlier one, except `--param`, which accumulates in order; `--pattern` and `--song` replace each other, and so do `--seconds` and `--bars`. This lets a golden's `#broken` options override its `#!` options (P6).

| Option | Value | Default | Rejected (exit 1) |
|---|---|---|---|
| `--card DIR` | the card folder | required to render | missing with `--out` |
| `--out FILE` | the WAV | required to render | missing with `--card` |
| `--seed-card DIR` | a folder that is missing or holds nothing but dot entries | — | with `--card`; a folder with other entries; a file |
| `--project N` | 0–63 | `PROJ.IDX`'s byte (64–255 count as 0, as E4.7 does), else 0 | not an integer 0–63 |
| `--pattern P` | 0–63 | the pattern active after boot | not 0–63 |
| `--song S` | 0–63 | — | not 0–63 |
| `--bpm B` | 2–255, `ext` or `mid` | the card's | anything else |
| `--param NAME=VALUE` | NAME as in P3 (the edit path); VALUE 0–255, the stored value | — | no `=`; VALUE not 0–255 (the name is checked in Task R2.5) |
| `--seconds T` | decimal > 0 and ≤ 86400, up to 6 decimals | 10 | otherwise |
| `--bars N` | integer 1–9999 | — | otherwise; and after the prelude, an EXT or MID tempo (P3) |
| `--events FILE` | a script (P4) | none | unreadable (exit 1, naming the file) |
| `--no-start` | — | off | — |
| `--faders F1,…,F6` | six decimals 0–1 | 1,1,1,1,1,1 | not six, or out of range |
| `--jacks all\|none\|LIST` | LIST of L1, R1, L2, R2 joined by `,` | all | an unknown name |
| `--seed N` | 0–2^64−1 | 1 | not an unsigned integer |
| `--as-booted` | — | off | — |
| `--wav-rate R` | 44141 or 44100 | 44141 | other |
| `--stereo M` | out1, out2 or sum | 4 channels | other |
| `--midi-log`, `--clock-log`, `--panel-log`, `--summary`, `--keep-card` | a path; `--keep-card` a folder that is missing or empty | none | `--keep-card` on a folder with entries |
| `--version`, `--help` | — | — | print to stdout, exit 0, whatever else is given |

Test builds (`lichomat-render-test`) add: `--test-fault segv|abort|model|ubsan`, `--test-hang`, `--test-slice`, `--test-prelude-reload` (P3). In `lichomat-render` they are unknown options.

`--help` prints the block above with one line per option, as in §5.1. `--version` prints these lines, each one C string of its own (so each is one run for M21's name scan), with `%s` filled from `render_build.h`:

```
lichomat-render %s, the renderer of LICHOMAT Emu
emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02
Runs LICHOMAT %s with the emulator changes F1-F12 and F6b. Built from commit %s.
Copyright (C) 2012-2026 Erica Synths
Copyright (C) 2026 Tomasz Krzywicki
Modified from the LXR firmware 1.7 by Erica Synths; LICHOMAT and its emulator by Tomasz Krzywicki, 2026
Arm Optimized Routines: Copyright (c) 1999-2022, Arm Limited. MIT licence.
FatFs ff.h and integer.h: Copyright (C) 2017, ChaN. FatFs licence.
This program comes with ABSOLUTELY NO WARRANTY. It is free software under the GNU
General Public License version 3 only, with the additional permission and terms
in NOTICE.md. See LICENSE and NOTICE.md.
Source: https://github.com/isthar/lxr-desktop-app at the commit above.
Not affiliated with Erica Synths.
```

The dashes are ASCII: an en dash would split the line into two runs for the name scan. The source line names the repository, whose name holds `lxr`, so it joins §10.2's allow-list as one of §10.1's notices.

## P2. Exit codes and messages (§5.7, §11)

| Code | When | Message on stderr |
|---|---|---|
| 0 | rendered, seeded, `--version`, `--help` | the short summary (P5) |
| 1 | an option (P1), a script line (P4), a refused or unknown `--param` or `param` | `lichomat-render: --OPTION: problem` or `lichomat-render: FILE:LINE: problem` |
| 2 | the card folder is missing, unreadable or not a folder; the project to load has no `PRJ.NFO`; `--seed-card`'s project save fails | `lichomat-render: card DIR: problem` |
| 3 | a fault signal (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, so also a sanitizer report), a model check (`emu_config_t.fault`), a failed walk check (P3) | `lichomat-render: firmware fault: WHAT at cycle C`, then the last 32 trail lines (`emu_trailWrite(2, 32)`: `cycles kind arg`), then `lichomat-render: the card copy is kept in DIR` |
| 4 | the watchdog (§4.2): no scheduler progress for 10 s of wall time outside the hook's waits | `lichomat-render: hang: no progress for 10 s at cycle C`, the trail, the card-copy line |
| 5 | an output file cannot be opened (checked before power-on) or written (checked at the end) | `lichomat-render: FILE: strerror` |

Codes 3 and 4 leave with `_exit()` from the handler, without cleanup, so the card copy stays for inspection. The handlers write with `write(2)` only; `emu_trailWrite()` is async-signal-safe. Signals run on an alternate stack (`sigaltstack`), so a stack overflow is reported too. The renderer defines `const char *__asan_default_options(void)` and `__ubsan_default_options(void)`, both returning `"abort_on_error=1"`, so that both sanitizers end in `abort()` on every platform.

## P3. The timeline (§5.3)

Constants: `C100 = 21,600,000` cycles (100 ms); `MS = EMU_CYCLES_PER_MS`.

1. **Power-on** with `emu_configDefaults()`, then `cardDir` (the copy), the jacks, the faders, the seed, the sinks, the hook, `fault` and `warn`. `emu_runUntil(1)` boots and returns at the boot's end.
2. **A pass** of the prelude is `emu_runUntil(emu_now() + 1)`, which returns after the next pass, then a direct call of `preset_morphBusy()`; if it returned 1, `busyLast = emu_now()`. Every pass from the boot to t0 is such a pass.
3. **Settle**: passes until `emu_now() ≥ E + C100` and (`busyLast == 0` or `emu_now() − busyLast > C100`), where E is the prelude's last event (initially the boot's end).
4. **The prelude**, after one settle:
   - `--pattern P`: inputs at w = `emu_now()` plus, in ms: SHIFT (32) down 0; step 16 (12) down 20, up 70; SHIFT up 100; bar− (40) pressed (50 ms) at 150, 250, 350; bar+ (0) pressed at 450 + 100·j for j < ⌊P/16⌋; step (P mod 16) + 1 pressed at 450 + 100·⌊P/16⌋. E = the last release. Passes until E, then settle. Then `seq_activePattern` must equal P, else exit 3 with `the --pattern walk ended on pattern X, not P`.
   - `--song S`: MODE3 (26) down 0; step 10 (22) down 20, up 70; MODE3 up 100; encoder −64 at 150; encoder +S at 500 when S > 0; push of 50 ms at 800. E = 850. Passes until E, settle. Then `seq_activeSong == seq_songData[S]`, else exit 3 (`the --song walk ended on song X, not S`).
   - Then, at one pass boundary and in this order: `--bpm`, the sync fallback, each `--param` in command-line order, each through the edit path below. E = that boundary.
   - **The sync fallback** (§5.1): with no `--bpm`, if `seq_getExtSync()` is 1 (EXT, the clock-in jack) and the script has no `jack-clock` or `jack-pulse`, or it is 2 (MID) and the script has no `din-clock`, `usb-clock` and no `F8` byte in a `din` or `usb` line, the renderer applies `--bpm 120` and warns: `the project syncs to EXT (MID); the script sends no clock to that input: 120 BPM internal`.
   - **The length**: blocks = ⌈⌈T × 353,125 / 8⌉ / 64⌉ for `--seconds T` (exact rational arithmetic on T's decimal digits), and for `--bars N` T = N × 240 / B with B the numeric `--bpm`, else `seq_getBpm()` when `seq_getExtSync()` is 0, else exit 1 (`--bars: needs an internal tempo; the project syncs to EXT|MID`). 10 s gives 6,897 blocks (441,408 frames); 4 bars at 120 BPM give 5,518 blocks (353,152 frames).
   - The summary's `selected` and `params` (P5) are read now.
   - Test builds: `--test-prelude-reload` adds, after the direct calls, SHIFT down at +0, step 13 (15) pressed at +20 ms, SHIFT up at +100 ms: a kit reload (`buttonHandler.c` `case SEQ_RELOAD`). These inputs are not prelude events, so E stays at the direct calls' boundary and the reload's transfer runs after E; t0 then waits for `busyLast`.
5. **t0** is found in the audio sink, which is called at each block request with the request's time t: the first t with `t − E ≥ C100` and (`busyLast == 0` or `t − busyLast > C100`). There the sink sets t0 = t, k0 = `emu_getCounters().blocks` (it must hold that `codecStart + emu_framesToCycles(64·k0) == t0`, else exit 3) and `tEnd = codecStart + emu_framesToCycles(64·(k0 + blocks))`.
6. **In the hook of that block request**: unless `--no-start`, START (33) down at t0 and up at t0 + 50 ms; for `--song`, SHIFT (32) down at t0 and up at t0 + 60 ms as well. Then every timed script event (P4) as an `emu_in*()` input at t0 + its time, with the clock generators expanded up to their `off` or tEnd. Then `emu_ledSnapshot()` once, to start the LED latch.
7. **Playing**: `emu_runUntil(min(t0 + time of the next direct event, tEnd + EMU_CPU_HZ))`; after each return, every direct event (`param`, `save-*`, `mark`) whose t0 + time ≤ `emu_now()` runs, in script order. A return past tEnd + 1 s is exit 3 (`the render did not end at its end time`).
8. **The end**: the first `EMU_HOOK_BLOCK` hook with `now ≥ tEnd` closes the WAV and the logs, reads `seqc_stats()`, `seqp_syncOverflows()` and `usb_midiTxDropped()` (pure getters that copy counters; the one firmware read from the hook, made so the render ends "wherever the firmware is", §5.3 step 7), writes the summary, copies the card for `--keep-card`, removes the copy, and calls `exit()` with 0, or 5 after a write error.
9. **What is kept**: audio blocks with t0 ≤ t < tEnd, so exactly `blocks` of them; log lines with t0 ≤ t < tEnd, their times relative to t0. Output before t0 is dropped; underruns are counted before and after t0 by the value the previous block hook saw.
10. **Test builds**: `--test-slice` caps every `emu_runUntil()` of step 7 at t0 + `emu_framesToCycles(F)`, with F growing by 470 and 471 frames in turn, and makes the hook call `emu_waitBegin(); emu_waitEnd();` at every call (M14). `--test-fault X` makes a direct call at the first boundary at or after t0 + 100 ms that does X: `segv` reads through a volatile NULL pointer, `abort` calls `abort()`, `model` calls `emu_fault("test fault")`, `ubsan` adds 1 to a volatile `INT_MAX`. `--test-hang` makes a direct call there that spins forever without calling the HAL.

**The edit path** (§5.3, `menu.c` `menu_encoderChangeParameter()`, the clamp switch and the send at its end). NAME is a `PAR_` enum name without `PAR_`, in any case, or a parameter number below `NUM_PARAMS`. Refused (exit 1, `parameter NAME: menu_encoderChangeParameter() has a special case for it (spec 5.3)`): the parameters whose `parameter_dtypes[p] & 0xFF` is `DTYPE_TARGET_SELECTION_VELO`, `DTYPE_VOICE_LFO` or `DTYPE_TARGET_SELECTION_LFO`. Otherwise:

```c
/* render_fw.c: the menu's clamp, menu.c menu_encoderChangeParameter() (M20 hashes the original) */
static uint8_t rfw_clamp(uint16_t p, unsigned v)
{
	switch (parameter_dtypes[p] & 0xFF)
	{
	case DTYPE_0B255:	return (uint8_t)v;
	case DTYPE_1B16:	return (uint8_t)(v < 1 ? 1 : v > 16 ? 16 : v);
	case DTYPE_1B64:	return (uint8_t)(v < 1 ? 1 : v > 64 ? 64 : v);
	case DTYPE_0B15:	return (uint8_t)(v > 15 ? 15 : v);
	case DTYPE_ON_OFF:
	case DTYPE_0b1:		return (uint8_t)(v > 1 ? 1 : v);
	case DTYPE_MENU:
	{
		const unsigned n = rfw_menuEntries((uint8_t)(parameter_dtypes[p] >> 8));
		return (uint8_t)(v >= n ? n - 1u : v);	/* n == 0 gives 255, as the menu's (uint8_t)(0 - 1) */
	}
	case DTYPE_AUTOM_TARGET:
	{
		const unsigned n = getNumModTargets();
		return (uint8_t)(v >= n ? n - 1u : v);
	}
	case DTYPE_PM63:
	case DTYPE_0B127:	return (uint8_t)(v > 127 ? 127 : v);
	default:		return (uint8_t)(v > 100 ? 100 : v);	/* default falls into DTYPE_0B100 */
	}
}

/* the edit: activeParameters, then the menu's send (menu.c, end of menu_encoderChangeParameter()) */
static void rfw_edit(uint16_t p, uint8_t v)
{
	activeParameters[p] = v;
	if (p < 128)
		frontPanel_sendData(MIDI_CC, (uint8_t)p, v);
	else if (p < END_OF_SOUND_PARAMETERS)
		frontPanel_sendData(CC_2, (uint8_t)(p - 128), v);
	else
		menu_parseGlobalParam(p, activeParameters[p]);
}
```

`rfw_menuEntries()` copies `getMaxEntriesForMenu()` (`menu.c`, static): one `case` per menu id returning `xNames[0][0]`, with `MENU_WAVEFORM` adding `menu_numSamples`. The name tables are external `const char [][4]` definitions in `MenuText.h`, which only `menu.c` includes, so `render_fw.c` declares each one `extern const char xNames[][4];` and must not include `MenuText.h`. `--bpm B` is the edit `PAR_BPM` = B, `ext` = 0, `mid` = 1 (`CortexParser.c` `FRONT_SET_BPM`).

## P4. Event scripts (§5.4)

```
script  = { line }
line    = [ time  event { arg } ]  [ "#" comment ]  newline      (fields separated by spaces or tabs)
time    = [ "+" ] decimal ( "ms" | "s" )  |  [ "+" ] integer "f"
decimal = digits [ "." digits ]            (at most 6 decimals)
```

- A time counts from t0; `+` counts from the previous line's time (from 0 on the first line). Times must not decrease. Cycles are exact rational results rounded down: `ms` × 216,000, `s` × 216,000,000, `f` through `emu_framesToCycles()`.
- `#` starts a comment anywhere, so the golden header lines `#!`, `#expect`, `#broken`, `#broken-drop`, `#pin` and `#keep` are comments to the renderer.
- A button is 0–40 or a name: `bar+` 0, `voice1`–`voice7` 1–7, `select5`–`select8` 8–11, `select1`–`select4` 16–19, `mode1`–`mode4` 24–27, `shift` 32, `start` 33, `rec` 34, `copy` 35, `bar-` 40, and `stepN`: step 16–13 are 12–15, 12–9 are 20–23, 8–5 are 28–31, 4–1 are 36–39 (§4.6).
- An error stops the renderer with exit 1 as `lichomat-render: FILE:LINE: problem`.

| Event | Arguments | Input or call | Errors |
|---|---|---|---|
| `down`, `up` | button | `emu_inButton(t, b, 1 or 0)` | unknown button |
| `press` | button [ms] | down at t, up at t + ms (default 50) | ms < 1 |
| `encoder` | ±detents (sign optional) | `emu_inEncoder(t, n)` | 0, or above 1000 in size |
| `push` | [ms] | `emu_inPush(t, ms)`, default 50 | ms < 40 |
| `knob` | 1–4, ±degrees | `emu_inKnob(t, k, deg)` | knob out of range |
| `fader` | 1–6, level 0–1 | `emu_inFader(t, i, level)` | out of range |
| `param` | NAME=VALUE | direct: the edit path (P3) | as `--param` (from Task R2.5) |
| `save-kit`, `save-pattern`, `save-project` | 0–63 | direct (Task R2.6) | out of range |
| `din` | hex bytes, two digits each | `emu_inDin(t, bytes, n)`, at most 256 bytes | not hex |
| `usb` | hex bytes, MIDI messages | packed into USB-MIDI packets on cable 0: status 8n–En gives CIN = status >> 4 with 2 data bytes (Cn, Dn: 1); F8–FF CIN F; F2 CIN 3; F1, F3 CIN 2; F6 CIN 5; packets zero-padded; up to 16 packets per transfer, `emu_inUsb()` at t, further transfers at the same t | a data byte without a status; F0 or F7 (SysEx); a short message |
| `din-clock` | BPM (decimal) or `off` | F8 at t + ⌊k × 540,000,000,000 / mBPM⌋, k = 0, 1, …, mBPM being BPM × 1000, until the next `din-clock` line or tEnd | BPM not 1–1000 |
| `usb-clock` | BPM [buffer ms] or `off` | clocks at the same times. Without a buffer, each clock is a transfer `0F F8 00 00` at its time. With buffer b, transfers at t + j·b (cycles rounded down), each holding the clocks of [t + j·b, t + (j+1)·b), none for an empty buffer | more than 16 clocks in a buffer |
| `jack-clock` | BPM and PPQ (4 or 24), or `off` | `emu_inClockPulse()` at t + ⌊k × 12,960,000,000,000 / (PPQ × mBPM)⌋ | PPQ other |
| `jack-pulse` | — | `emu_inClockPulse(t)` | — |
| `reset` | `on` or `off` | `emu_inReset(t, 1 or 0)` | other |
| `card` | `in` or `out` | `emu_inCard(t, 1 or 0)` | other |
| `jacks` | `all`, `none` or LIST | `emu_inJacks(t, mask)` | unknown name |
| `mark` | name (`[A-Za-z0-9_.-]+`) | direct: one panel-log line (P5) | no name, other characters |

## P5. Outputs (§5.5)

- **WAV**: the 44-byte canonical header (`RIFF`, `WAVE`, `fmt ` with format 1, PCM), 16-bit little-endian samples, header rate from `--wav-rate`. 4 channels L1, R1, L2, R2 per frame, out 1 being the sink's `out1` (`dma_buffer2`). `--stereo out1` writes L1 R1, `out2` L2 R2, `sum` `(int16_t)(((int32_t)a + b) >> 1)` per sample, a from out 1 and b from out 2 (§5.1). The header's sizes are written at the end.
- **Times in logs** are relative to t0: `CYCLES MS`, with `MS = ⌊c/216⌋/1000` printed as `%llu.%03llu` (integer arithmetic, rounded down).
- **MIDI log**: `CYCLES MS DIN XX` per byte; `CYCLES MS USB XXXXXXXX …` per transfer, each 4-byte packet as 8 upper-case hex digits in byte order.
- **Clock log**: `CYCLES MS L`, L the new level, 0 or 1.
- **Panel log**: `CYCLES MS NAME "ROW1" "ROW2" CURSOR LEDS`. Rows are the 16 codes; bytes outside 0x20–0x7E, and `"` and `\`, are written `\xNN` (two upper-case hex digits). CURSOR is `R,C` (0-based) while the cursor or blink is on, else `-`. LEDS is `emu_ledSnapshot()` as 11 upper-case hex digits.
- **Summary** (`--summary`): one JSON object with these keys, cycles absolute from power-on:

```
version 1; commit; toolchain {clang, sdk}; inputs {project, pattern, song, bpm, params [..], seed,
faders [6], jacks, asBooted, noStart, blocks, events}; bootEnd; codecStart; preludeEnd; morphBusyLast;
t0; end; underrunsBeforeT0; underrunsAfterT0; selected {pattern, song, bpm, sync}; params {NAME: value};
paramNumbers {NAME: number}; direct [{line, event, due, at}]; syncOverflows; usbTxDropped; seqc {the 11 fields of seqc_stats_t,
by their C names}; counters {the 14 fields of emu_counters_t, by their C names}; wallSeconds;
virtualSeconds; speed; warnings [..]
```

`selected.song` is the index S with `seq_activeSong == seq_songData[S]`, or −1. `params` holds `activeParameters[p]` of each `--param`, by its lower-case table name (also when given by number), and `paramNumbers` its number p. `direct` lists each direct event: its script line, its event, its due time (t0 + its time) and the boundary at which it ran. `speed` = `virtualSeconds` (tEnd / 216 MHz) / `wallSeconds` (from `main()` to the end, `CLOCK_MONOTONIC`).
- **Short form on stderr** at exit 0: `lichomat-render: t0 at S.SSS s, N blocks, U underruns after t0, X.Xx real time`, then each warning as `lichomat-render: warning: …`.

## P6. Golden files and expectations (§8.4)

A golden is `emu/tests/goldens/ID.txt`; ID is its file name. Header lines, then the script (P4):

| Line | Meaning |
|---|---|
| `#! OPTIONS` | renderer options; several lines are joined. Not allowed: `--out`, `--midi-log`, `--clock-log`, `--panel-log`, `--summary`, `--events`, `--keep-card`. `--card NAME` without a `/` is `emu/tests/cards/NAME` |
| `#expect …` | an expectation (below); at least one |
| `#broken OPTIONS` | the broken variant: these options appended to the golden's |
| `#broken-drop EVENT[,EVENT…]` | the broken variant leaves out the script lines with these events |
| `#pin panel` | `goldens.json` pins the panel log |

A golden has `#broken`, `#broken-drop` or both. `emu_golden.py` renders with `--events` = the golden file and every output option. The broken variant **fails** when at least one expectation does not hold on it (G1's underrun expectation holds with the faders at 0, its energy expectation does not).

Every count and energy expectation takes an optional window `FROM..TO` in script times (P4 syntax, FROM included, TO excluded); without one, the whole render. On the WAV, a window is the frames [⌈FROM × 44,140.625⌉, ⌈TO × 44,140.625⌉).

| Expectation | Holds when |
|---|---|
| `#expect midi PORT SS MIN..MAX [W]` | MIN ≤ the messages on PORT (`DIN`, `USB`) whose status byte matches SS ≤ MAX. SS is two hex digits, `n` matching any low nibble. On DIN every byte ≥ 0x80 is a status byte (the firmware sends no running status); on USB, byte 1 of each packet |
| `#expect clock MIN..MAX [W]` | the clock log has MIN to MAX lines with level 1 |
| `#expect energy CH DBFS [W]` | channel CH (L1, R1, L2, R2) has RMS ≥ DBFS, with RMS in dBFS = 20·log10(rms / 32768) |
| `#expect silent CH [W]` | every sample of CH is 0 |
| `#expect summary FIELD MIN..MAX` | FIELD, a dotted path into the summary (`underrunsAfterT0`, `seqc.dropouts`), is a number in range |
| `#expect lcd MARK TEXT` | row 1 or 2 of the panel log at MARK, unescaped, contains TEXT (the rest of the line, trailing spaces stripped) |
| `#expect led MARK MASK VALUE` | (LEDS at MARK & MASK) == VALUE, hex |

A threshold that the firmware does not fix is set from a measurement: an energy threshold is the measured RMS minus 10 dB, rounded down to a multiple of 5 dB; a count the fixture and the rules of §4 fix is exact, and any other count is the measured value ± 2. The broken variant must fail with the threshold.

## P7. `goldens.json` (§8.4, §8.5)

`json.dump(…, indent=1, sort_keys=True)` plus a newline:

```
{ "G1": { "wav":   { "sha256": HEX, "channels": 4, "rate": 44141, "bits": 16, "frames": N,
                     "chunks": [ "8 hex digits", … ], "peak": [4 ints], "rms": [4 dBFS or null] },
          "midi":  { "sha256": HEX, "lines": N, "chunks": [ … ] },
          "clock": { "sha256": HEX, "lines": N, "chunks": [ … ] },
          "panel": { … }   (only with #pin panel),
          "toolchain": { "clang": "…", "sdk": "…" } }, … }
```

- `wav.sha256` is over the sample data only, after the 44-byte header. `chunks` are `zlib.crc32` of each 4,096 frames of sample data (all channels; the last chunk may be short): 108 for 10 s. `peak` is the largest |sample| per channel, `rms` in dBFS with 2 decimals (null for silence).
- A log's `chunks` are CRC-32s of each 64 lines (with their newlines), so `check` can name the first differing block. The exact line is `diff`'s job.
- `toolchain` comes from the summary.

## P8. Fixtures (§5.6)

`emu/tests/cards/scripts/NAME.txt` is a script with header lines: `#! OPTIONS` (renderer options for the fixture's render), and `#keep GLOB…` (one or more lines): the files, relative to the card root, that its goldens need. `make.sh` seeds a card, renders the script on it with `--keep-card`, and copies the kept files that match. Every glob must match at least one file. `/TMP` and `/PROJ.IDX` are kept only if listed.

| Fixture | Contents | Goldens |
|---|---|---|
| `init` | the seeded card; pattern 0 with steps 1, 5, 9, 13 on voices 1–7; pattern 1 with the same steps on voice 1 only. Keeps `PROJ00/PRJ.NFO PROJ00/GLO.CFG PROJ00/00-*.PAT PROJ00/01-*.PAT PROJ00/00-*.SND` | G1, G5–G10 |
| `fx` | `init`'s pattern 0; voices 1–4 sent to the FX bus with the dry/wet at half, stored in kit 0. Keeps `PROJ00/PRJ.NFO PROJ00/GLO.CFG PROJ00/00-*.PAT PROJ00/00-*.SND` | G2 |
| `song` | patterns 0, 1, 2 with different steps, each with its own kit (kits 0, 1, 2, edited in decay and volume only), song 0 = patterns 0, 1, 2, kit autoload on (`GLO.CFG` byte 29 = 1 from the seed). Keeps `PROJ00/PRJ.NFO PROJ00/GLO.CFG PROJ00/0[0-2]-*.PAT PROJ00/0[0-2]-*.SND PROJ00/00-*.SNG` | G3, G4 |

---

### Task R2.1: The targets, the options, `--help` and `--version` (M17 options)

§5.1, §5.7 (code 1), §10.1, §10.2, §12.1 M17 ("options and defaults"), M21.

**Files:**
- Create: `emu/render/render_opts.h`, `emu/render/render_opts.c`, `emu/render/render_main.c`, `emu/render/render_fw.h`, `emu/render/render_fw.c`, `emu/render/render_build.h.in`, `emu/tests/render_test.py`
- Modify: `emu/CMakeLists.txt`, `emu/tests/name_allow.txt`

**What to build.**
- `render_opts.h`: `typedef struct render_opts` with one field per P1 option (strings as `const char *`; `project`, `pattern`, `song` as `int`, −1 for none; `bpm` −1/0/1/2–255; `params[64]`, `nparams`; the length as `secondsNum`, `secondsDigits` (T = num / 10^digits) or `bars`; `faders[6]`; `jacks`; `seed`; `wavRate`; `stereo` 0–3; `version`, `help`; the test options). `int render_optsParse(int argc, char **argv, render_opts_t *o, char *err, size_t n)` returns 0, or −1 with the P2 message (without the `lichomat-render: ` prefix) in `err`. Unknown options include the test options when `EMU_RENDER_TEST_HOOKS` is 0.
- `render_main.c` for now: parse; `--help` and `--version` (P1); any other mode prints `lichomat-render: rendering comes in R2.2` and exits 1 (Task R2.2 replaces it). All messages go through `render_die(code, fmt, …)`, which prints `lichomat-render: ` + the message + a newline to stderr and exits.
- `render_fw.{h,c}`: an empty unit for now (one comment line naming its role), so the targets exist.
- `render_build.h.in`:

```c
#define EMU_RENDER_COMMIT  "@EMU_COMMIT@"
#define EMU_RENDER_CLANG   "@EMU_CLANG@"
#define EMU_RENDER_SDK     "@EMU_SDK@"
#define EMU_RENDER_VERSION "@EMU_FIRMWARE_VERSION@"
```

- Append to `emu/CMakeLists.txt`, before the tests block:

```cmake
# --- lichomat-render (spec 5) -------------------------------------------------------------
# what --version and the summary print (spec 8.4, 10.1), fixed at configure time
execute_process(COMMAND git -C "${LXR_ROOT}" rev-parse --short=12 HEAD
    OUTPUT_VARIABLE EMU_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT EMU_COMMIT)
    set(EMU_COMMIT unknown)
endif()
execute_process(COMMAND ${CMAKE_C_COMPILER} --version OUTPUT_VARIABLE emu_cc_version)
string(REGEX MATCH "^[^\n]*" EMU_CLANG "${emu_cc_version}")
execute_process(COMMAND xcrun --show-sdk-version OUTPUT_VARIABLE EMU_SDK OUTPUT_STRIP_TRAILING_WHITESPACE)
configure_file(render/render_build.h.in "${CMAKE_BINARY_DIR}/render_build.h" @ONLY)
message(STATUS "emulator: commit ${EMU_COMMIT}, ${EMU_CLANG}, SDK ${EMU_SDK}")

file(GLOB EMU_RENDER_SOURCES CONFIGURE_DEPENDS "${EMU_DIR}/render/*.c")
list(REMOVE_ITEM EMU_RENDER_SOURCES "${EMU_DIR}/render/render_fw.c")
# emu_render_exe(<name> <hooks>): the renderer; hooks 1 adds the test-only options
# (spec 5.3). Only render_fw.c includes firmware headers; the other files get the
# overlay flag, which the licence gate requires of every compile command, but not
# the section define (spec 3.4), as emu_hostfs.c.
function(emu_render_exe name hooks)
    add_library(${name}_fw OBJECT "${EMU_DIR}/render/render_fw.c")
    target_link_libraries(${name}_fw PRIVATE emu_fw_headers)
    target_include_directories(${name}_fw PRIVATE "${EMU_DIR}/core" "${EMU_DIR}/render" "${CMAKE_BINARY_DIR}")
    target_compile_options(${name}_fw PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(${name}_fw PRIVATE EMU_RENDER_TEST_HOOKS=${hooks})
    add_executable(${name} ${EMU_RENDER_SOURCES} $<TARGET_OBJECTS:${name}_fw> ${EMU_DEVICE_OBJECTS})
    target_include_directories(${name} PRIVATE "${EMU_DIR}/core" "${EMU_DIR}/render" "${CMAKE_BINARY_DIR}")
    target_compile_options(${name} PRIVATE ${EMU_FP_FLAGS} ${EMU_SAN_FLAGS} -Wall -Wextra -Werror
        "SHELL:-ivfsoverlay ${EMU_OVERLAY}")
    target_compile_definitions(${name} PRIVATE EMU_RENDER_TEST_HOOKS=${hooks})
endfunction()
emu_render_exe(lichomat-render 0)
emu_render_exe(lichomat-render-test 1)
```

- In the tests block: add `$<TARGET_FILE:lichomat-render>` to the binaries of `add_test(NAME M21 …)`, and register the driver:

```cmake
# M17, M14, M23: lichomat-render through emu/tests/render_test.py
set(EMU_RENDER_TEST ${Python3_EXECUTABLE} "${EMU_DIR}/tests/render_test.py"
    --render $<TARGET_FILE:lichomat-render> --render-test $<TARGET_FILE:lichomat-render-test>
    --root "${LXR_ROOT}" --work "${CMAKE_BINARY_DIR}/render-test")
function(emu_render_tests)
    foreach(id IN LISTS ARGN)
        add_test(NAME ${id} COMMAND ${EMU_RENDER_TEST} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT 600)
    endforeach()
endfunction()
emu_render_tests(M17_cli_usage M17_cli_info)
```

- `name_allow.txt`: append the five `--version` lines of P1 that name the maker, the product or the repository (the tagline, `Copyright (C) 2012-2026 Erica Synths`, the `Modified from …` line, the `Source:` line, `Not affiliated with Erica Synths.`), each as one line.
- `render_test.py`, the driver every later task extends:

```python
#!/usr/bin/env python3
"""Host tests of lichomat-render (stage 1b spec 5, 12.1 M14, M17, M23).

    render_test.py --render R --render-test RT --root ROOT --work DIR CASE

Each CASE is a function below; it runs in DIR/CASE, emptied first, and prints
PASS CASE (exit 0) or the failed check (exit 1).
(licence header as in the other files)
"""
import argparse, hashlib, json, os, re, shutil, struct, subprocess, sys, wave

CASES = {}
def case(fn):
    CASES[fn.__name__] = fn
    return fn

class Fail(Exception):
    pass

def check(cond, what):
    if not cond:
        raise Fail(what)

class T:
    def __init__(self, a):
        self.r, self.rt, self.root = a.render, a.render_test, a.root
        self.work = os.path.join(a.work, a.case)
        shutil.rmtree(self.work, ignore_errors=True)
        os.makedirs(self.work)
        self.n = 0

    def path(self, *p):
        return os.path.join(self.work, *p)

    def run(self, args, code=0, test=False, timeout=300):
        cmd = [self.rt if test else self.r] + [str(x) for x in args]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        check(p.returncode == code, 'exit %d, not %d: %s\n%s' % (p.returncode, code, ' '.join(cmd), p.stderr))
        return p

    def script(self, text):
        self.n += 1
        p = self.path('s%d.txt' % self.n)
        with open(p, 'w') as f:
            f.write(text)
        return p

def main():
    ap = argparse.ArgumentParser()
    for o in ('--render', '--render-test', '--root', '--work'):
        ap.add_argument(o, required=True)
    ap.add_argument('case', choices=sorted(CASES))
    a = ap.parse_args()
    try:
        CASES[a.case](T(a))
    except Fail as e:
        print('FAIL %s: %s' % (a.case, e))
        sys.exit(1)
    print('PASS', a.case)

# --- cases ---------------------------------------------------------------------

if __name__ == '__main__':
    main()
```

(Cases go above the `if __name__` line; `main()` is defined first so that later tasks only add cases.)

**Tests:**

| Id | Inputs | Expected |
|---|---|---|
| `M17_cli_usage` | each of: no argument; `--card C`; `--out O`; `--bogus`; `--project 64`; `--pattern -1`; `--bpm 1`; `--bpm 256`; `--bpm fast`; `--faders 1,1,1`; `--faders 1,1,1,1,1,1.5`; `--jacks L3`; `--wav-rate 48000`; `--stereo both`; `--seconds 0`; `--seconds 1.1234567`; `--bars 0`; `--seed x`; `--param shuffle`; `--param shuffle=256`; `--seed-card D --card C`; `--test-slice` (with `lichomat-render`) — each with `--card C --out O` added where the case does not test them | exit 1; stderr's single line starts with `lichomat-render: ` and contains the option's name (`--card` for the first two, `--out` for the third) |
| `M17_cli_info` | `--version`; `--help`; `--help --bogus` | exit 0; `--version`'s stdout equals P1's lines with the version `0.1` and a commit of 12 hex digits (or `unknown`); `--help`'s stdout names every option of P1 and none of the test options; `lichomat-render-test --help` names the four test options too |

- [ ] **Step 1: Record the start state.** Run `cd /Users/tk/Code/lxr/build/wt-emulator && git status --short && git log --oneline -1 && tools/check.sh | grep -E 'tests passed|^memory:|image:|warnings'`. Expected: only `?? .toolchain`, `22827f7 …`, `100% tests passed, 0 tests failed out of 55`, a `memory:` line (write it into the task report; later gates compare with it), the image line with `OK`, `== compiler warnings: 0`.
- [ ] **Step 2: Write the files and the CMake blocks.**
- [ ] **Step 3: Run the two cases** with `cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && cmake --build build/emu && ctest --test-dir build/emu -R 'M17_|M21' --output-on-failure | tail -3`. Expected: 5 of 5 (M21, M21_rejects, M21_negative and the two cases). If M21 names a `--version` string, the allow-list line differs from the run it found; make them equal.
- [ ] **Step 4: EMU-CHECK.** N = 105.
- [ ] **Step 5: The gate.**
- [ ] **Step 6: Commit** the seven new files, `emu/CMakeLists.txt` and `emu/tests/name_allow.txt`:

```
Emulator: lichomat-render's targets, options, --help and --version (E5.3 R2, M17)

lichomat-render and lichomat-render-test (the same sources with the
test-only options of stage 1b spec 5.3) link the whole virtual device. Only
render_fw.c includes firmware headers. The options of spec 5.1 are parsed and
checked; a bad one exits with code 1 and names it. --version prints the
notices of spec 10.1, and their lines that name the maker, the product or the
repository join M21's allow-list; M21 now scans lichomat-render too.
render_test.py is the driver of the renderer's host tests.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

---

### Task R2.2: The card, `--seed-card` and exit code 2 (M17 card, seed)

§5.2, §5.7 (code 2), §6.7, §11, §12.1 M17 (`--project`, `--seed-card`, exit code 2).

**Files:**
- Create: `emu/render/render_card.h`, `emu/render/render_card.c`
- Modify: `emu/render/render_main.c`, `emu/render/render_fw.{h,c}`, `emu/tests/render_test.py`, `emu/CMakeLists.txt`

**What to build.**
- `render_card.c`, on `emu_hostfs.h`:
  - `int render_cardEmpty(const char *dir)`: 1 missing or nothing but dot entries, 0 other entries, −1 not a folder.
  - `int render_cardPrepare(const render_opts_t *o, char copy[EMU_HOST_PATH], int *project, char *err, size_t n)`: checks `--card` is a readable folder; makes `$TMPDIR/lichomat-render-XXXXXX` (`mkdtemp`); `emu_hostCopyCard(card, copy, !o->asBooted)`; picks the project (P1); finds `PROJnn` and its `PRJ.NFO` case-insensitively (`emu_hostList()`'s `name`); with `--project`, writes the byte into the copy's `PROJ.IDX` (created if missing). Returns 0, or 2 with `card DIR: …` in `err` (`project N has no PROJnn/PRJ.NFO`, `not a folder`, `cannot read`).
  - `int render_cardKeep(const char *copy, const char *keep)`: `emu_hostCopyCard(copy, keep, 0)` after creating `keep` if missing; `void render_cardRemove(const char *dir)`: `nftw()` with `FTW_DEPTH | FTW_PHYS`, `remove()` each entry.
- `render_fw.c`: `void rfw_seedCard(void *res)`: `preset_createGlobals(); preset_init(0); sprintf(savefileNames[0][PROJECT_NAMES], "Init"); *(FRESULT *)res = preset_saveProject(0);`, the steps of `sd_formatRoutine()` after its confirmation (`appMain.c`, after `f_mkfs`). `void rfw_morphBusy(void *out)` stores `preset_morphBusy()` into `*(uint8_t *)out`.
- `render_main.c`, `--seed-card DIR`: `render_cardEmpty(DIR)` must give 1 (else exit 1); `mkdir` if missing; power-on with `cardDir = DIR`, `cardIn = 1`, `fault` and `warn` printing; `emu_runUntil(1)`; passes for 100 ms; `emu_directCall(rfw_seedCard, &res)`; `FR_OK` gives exit 0, else exit 2 (`card DIR: the project save failed with FatFs result N`). A render (`--card`, `--out`) runs `render_cardPrepare()` and exits 2 on its error; otherwise it prints `lichomat-render: rendering comes in R2.3` and exits 1, removing the copy (Task R2.3 replaces it).
- `render_test.py`: a helper `T.seed(name='seed')` that runs `--seed-card` into `self.path(name)` once and returns the path; a helper `tree(d)` that returns `{relative path: sha256}` of every file and `{relative path: None}` of every directory.

**Tests:**

| Id | Inputs | Expected |
|---|---|---|
| `M17_card_errors` | `--card` on: a missing path; a file; an empty folder; the seeded card with `--project 3`; the seeded card with `PROJ.IDX` = 05 (no `PROJ05`); the seeded card with `PROJ.IDX` = 200 and a folder `proj00` renamed lower case | exit 2, stderr names the card and, for the third to fifth, `PROJ00`, `PROJ03`, `PROJ05` with `PRJ.NFO`; the last one passes the check (byte 200 counts as 0; the lookup ignores case), so it exits 1 with `rendering comes in R2.3` |
| `M17_seed_card` | `--seed-card` into a new path; into an empty folder holding `.DS_Store`; into a folder with a file `x` | exit 0, 0, 1. The first card (measured): `PROJ00/` with 194 files, 255,398 bytes in all, `PRJ.NFO` = `b'Init\0\0\0\0'`, `GLO.CFG` of 30 bytes with byte 0 = 120 and byte 29 = 1, `PROJ00/00-INITK.SND` of 255 bytes; an empty `TMP/`; no `PROJ.IDX`. The second card's `tree()` equals the first's (apart from `.DS_Store`): seeding is deterministic |

Register both with `emu_render_tests()`.

- [ ] **Step 1: Write the code and the cases; run `ctest --test-dir build/emu -R 'M17_' --output-on-failure | tail -3` after a build.** Expected: 4 of 4.
- [ ] **Step 2: EMU-CHECK.** N = 107.
- [ ] **Step 3: The gate.**
- [ ] **Step 4: Commit** `emu/render/render_card.{h,c}`, `emu/render/render_main.c`, `emu/render/render_fw.{h,c}`, `emu/tests/render_test.py`, `emu/CMakeLists.txt`: "Emulator: lichomat-render's card copy and --seed-card (E5.3 R2, M17)", the body naming spec 5.2 and 6.7, the copy in the card's order, `/TMP` left out, the project rule with E4.7's range, exit code 2, and the measured init card (194 files, 255,398 bytes).

---

### Task R2.3: The timeline, t0, the WAV and the summary (M17 t0, defaults, WAV, card copy, exit 5)

§5.1, §5.2, §5.3 (steps 1–7, without the walks and the script), §5.5 (WAV, summary), §5.7 (code 5), §12.1 M17 ("the t0 rule", "`/TMP` handling", "`--stereo`" in part).

**Files:**
- Create: `emu/render/render_out.h`, `emu/render/render_out.c`
- Modify: `emu/render/render_main.c`, `emu/render/render_fw.{h,c}`, `emu/tests/render_test.py`, `emu/CMakeLists.txt`

**What to build.**
- `render_out.c`: `int render_outOpen(render_out_t *o, const render_opts_t *opts, char *err, size_t n)` opens every given output and writes the WAV header with zero sizes (5 on failure, `err` = `FILE: strerror(errno)`); `void render_outAudio(render_out_t *o, const int16_t *out1, const int16_t *out2)` writes one block in P5's channel layout; `int render_outClose(render_out_t *o, unsigned wavRate)` patches the sizes and closes (5 on any `ferror()` or failed `fclose()`); `int render_summaryWrite(const char *path, const render_summary_t *s)` writes P5's JSON (keys in P5's order; numbers as integers, `wallSeconds`, `virtualSeconds` and `speed` with `%.6f`; strings escaped for `"`, `\` and control bytes). The MIDI, clock and panel logs come in Task R2.4.
- `render_fw.c`: `rfw_selected(render_selected_t *)` (`seq_activePattern`, the song index, `seq_getBpm()`, `seq_getExtSync()`), `rfw_counters(render_fwCounters_t *)` (`seqc_stats()`, `seqp_syncOverflows()`, `usb_midiTxDropped()`).
- `render_main.c`: P3 steps 1–9 without the walks, the script, `--bpm`, `--param` and the fallback (later tasks). The heart of it:

```c
#define C100 21600000ull	/* 100 ms (spec 5.3) */

static struct
{
	const render_opts_t *o;
	render_out_t out;
	uint64_t preludeEnd, busyLast, t0, tEnd, k0, blocks;
	uint64_t lastUnderruns, underrunsBefore;
	enum { PH_PRELUDE, PH_WAIT, PH_PLAY } phase;
	int t0Hook;		/* the sink found t0; the hook of this request starts the script */
	uint64_t sliceFrames;	/* test builds: --test-slice */
} R;

static int render_t0Due(uint64_t t)
{
	return t - R.preludeEnd >= C100 && (R.busyLast == 0 || t - R.busyLast > C100);
}

static void render_audio(void *ctx, uint64_t t, const int16_t *out1, const int16_t *out2)
{
	(void)ctx;
	if (R.phase == PH_WAIT && render_t0Due(t))
	{
		emu_counters_t c;
		emu_getCounters(&c);
		R.t0 = t;
		R.k0 = c.blocks;	/* counted before the sink (emu_codec.c) */
		if (c.codecStart + emu_framesToCycles(64u * R.k0) != t)
			render_fault("t0 is not the time of block request k0");
		R.tEnd = c.codecStart + emu_framesToCycles(64u * (R.k0 + R.blocks));
		R.underrunsBefore = R.lastUnderruns;
		R.phase = PH_PLAY;
		R.t0Hook = 1;
	}
	if (R.phase == PH_PLAY && t >= R.t0 && t < R.tEnd)
		render_outAudio(&R.out, out1, out2);
}

static void render_hook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
#if EMU_RENDER_TEST_HOOKS
	if (R.o->testSlice)
	{
		emu_waitBegin();	/* M14: a tier that waits at every call */
		emu_waitEnd();
	}
#endif
	if (why != EMU_HOOK_BLOCK)
		return;
	if (R.t0Hook)
	{
		R.t0Hook = 0;
		render_startScript(now);	/* START, the script's inputs, the LED latch (P3 step 6) */
	}
	else if (R.phase == PH_PLAY && now >= R.tEnd)
		render_finish(now);		/* P3 step 8; does not return */
	emu_counters_t c;
	emu_getCounters(&c);
	R.lastUnderruns = c.underruns;
}

static void render_pass(void)
{
	uint8_t busy = 0;
	emu_runUntil(emu_now() + 1u);	/* returns after the next pass */
	emu_directCall(rfw_morphBusy, &busy);
	if (busy)
		R.busyLast = emu_now();
}

static void render_settle(void)
{
	while (!(emu_now() - R.preludeEnd >= C100 && (R.busyLast == 0 || emu_now() - R.busyLast > C100)))
		render_pass();
}
```

  `render_fault(const char *what)` prints P2's code-3 message (with `emu_now()` and the trail) and calls `_exit(3)`; it is also `cfg.fault`'s body. Task R2.6 adds the signal handlers and the watchdog. `main()` after the options: `render_cardPrepare()`; `render_outOpen()` (exit 5 before power-on); power-on (P3 step 1, `faders`, `jacks`, `seed` from the options, `sinks.audio = render_audio`, `hook = render_hook`); `emu_runUntil(1)`; `R.preludeEnd = emu_now()`; `render_settle()`; the prelude's later parts (Tasks R2.5); the length (P3); `rfw_selected()`; `R.phase = PH_WAIT; while (R.phase == PH_WAIT) render_pass();`; then the play loop of P3 step 7. For now `render_startScript()` presses START (P3 step 6) and starts the LED latch; Task R2.4 adds the script. `render_finish()` does P3 step 8; `atexit()` is not used: `render_finish()` removes the copy itself before `exit()`.
- Test builds: `--test-prelude-reload` (P3 step 4) as inputs after the prelude.

**Tests** (the seeded card from `T.seed()`; `T.render(card, args, test=False, code=0)` is a new helper that adds `--out`, `--summary` and returns the summary as a dict and the WAV path):

| Id | Inputs | Expected |
|---|---|---|
| `M17_t0` | the seeded card, 1 s; then `lichomat-render-test --test-prelude-reload`, 1 s | in both: t0 is a block request (`codecStart + floor(64k × 552960 / 113) == t0` for some k); `t0 − preludeEnd ≥ 21,600,000`; if `morphBusyLast` ≠ 0, `t0 − morphBusyLast > 21,600,000`; the request before t0 (k − 1) fails at least one of the two rules (t0 is the first). In the second, `morphBusyLast > preludeEnd` (a transfer ran after the prelude's last event), so its t0 is set by the busy rule |
| `M17_defaults` | the seeded card, no option but `--card` and `--out` | `inputs`: `project` 0, `pattern` −1, `song` −1, `seed` 1, `faders` six 1, `jacks` `all`, `asBooted` false, `noStart` false, `blocks` 6,897; `selected.pattern` 0, `selected.bpm` 120, `selected.sync` 0; the WAV: 4 channels, 44,141 Hz, 16 bits, 441,408 frames; `underrunsAfterT0` 0; `end − t0` = `emu_framesToCycles(64 × (k0 + 6897)) − emu_framesToCycles(64 × k0)` computed in Python |
| `M17_wav` | `--seconds 1`; the same with `--wav-rate 44100`; `--stereo out1`, `out2`, `sum` | 690 blocks, 44,160 frames, data of 353,280 bytes; the 44100 header differs only in its rate and byte rate, the samples are byte-equal; the stereo files have 2 channels and 44,160 frames (their contents are checked in Task R2.4) |
| `M17_card_copy` | a copy of the seeded card with `TMP/X.SND` added and a `PROJ01/` that copies `PROJ00/` except `GLO.CFG` byte 0 = 90; render with `--keep-card K1`; with `--as-booted --keep-card K2`; with `--project 1 --keep-card K3`; with `--keep-card` on a folder holding a file | the source's `tree()` is unchanged after every render (no `PROJ.IDX` appears in it); K1 holds `PROJ.IDX` (the boot writes it on the load) and a `TMP/` without `X.SND`; K2's `TMP/` holds `X.SND`; K3's `PROJ.IDX` is one byte 01 and its render has `selected.bpm` 90 (project 1 loaded); the last exits 1. No `lichomat-render-*` folder of these renders is left in `$TMPDIR` |
| `M17_out_errors` | `--out` in a missing folder; `--summary` in a folder made read-only (`chmod 0555`) | exit 5 naming the file, before power-on (wall time under 1 s) |

- [ ] **Step 1: Write the code and the cases.** Run `ctest --test-dir build/emu -R 'M17_' --output-on-failure | tail -3`. Expected: 9 of 9. Record t0 − preludeEnd and `morphBusyLast` − `preludeEnd` of both `M17_t0` renders in the task report.
- [ ] **Step 2: EMU-CHECK.** N = 112.
- [ ] **Step 3: The gate.**
- [ ] **Step 4: Commit** `emu/render/render_out.{h,c}`, `emu/render/render_main.c`, `emu/render/render_fw.{h,c}`, `emu/tests/render_test.py`, `emu/CMakeLists.txt`: "Emulator: lichomat-render's timeline, t0 and WAV (E5.3 R2, M17)", the body naming the t0 rule of spec 5.3 with the measured margins, the whole-block length, the end in the hook, and that the hook reads the three counter getters for the summary.

---

### Task R2.4: Event scripts and the logs (M17 script, stereo)

§5.4, §5.5 (logs), §4.3–§4.6 (the inputs), §12.1 M17.

**Files:**
- Create: `emu/render/render_script.h`, `emu/render/render_script.c`
- Modify: `emu/render/render_main.c`, `emu/render/render_out.{h,c}`, `emu/tests/render_test.py`, `emu/CMakeLists.txt`

**What to build.**
- `render_script.c`: `int render_scriptLoad(const char *path, render_script_t *s, char *err, size_t n)` parses P4 into an array of `render_event_t` (time in cycles from t0, line, kind, arguments) and returns 0 or −1 with `FILE:LINE: problem`. This task accepts every event of P4 except `param`, `save-kit`, `save-pattern` and `save-project`, which stay unknown events until Tasks R2.5 and R2.6 add their rows. `int render_scriptHasClock(const render_script_t *s, int sync)` answers P3's fallback question.
- `render_main.c`: `render_startScript()` injects every timed event (P4's input column) after the START press; the play loop runs the direct events (`mark` now). The DIN, USB and clock sinks write the logs (P3 step 9).
- `render_out.c`: `render_outDin()`, `render_outUsb()`, `render_outClock()`, `render_outMark()` in P5's formats.
- `render_test.py`: `STEPS = '0ms press voice1\n+100ms press step1\n+100ms press step5\n+100ms press step9\n+100ms press step13\n+100ms press start\n'`, for renders with `--no-start` that need sound (voice 1 on out 1; START at 500 ms).

**Tests:**

| Id | Inputs | Expected |
|---|---|---|
| `M17_script_errors` | one script each: `12xs mark a`; `100ms mark a` then `50ms mark b`; `0ms flip 3`; `0ms press 41`; `0ms push 30`; `0ms knob 5 +1`; `0ms fader 1 1.5`; `0ms din 9G`; `0ms usb F0 01 F7`; `0ms usb 30 40`; `0ms usb-clock 255 200`; `0ms reset maybe`; `0ms card sideways`; `0ms jacks L3`; `0ms mark`; `0ms mark a b`; a missing script file | exit 1; stderr names `FILE:LINE` with the failing line's number (line 2 for the second), or the missing file |
| `M17_events_din` | the seeded card, `--no-start --seconds 2 --midi-log`; script `100ms din 90 30 64`, `200ms din-clock 120`, `1200ms din-clock off` | `counters.dinIn` = 51 (3 + 48 clocks); out 1 is digital silence in [0, 100 ms) and above −60 dBFS in [100, 400 ms); every log line matches `^\d+ \d+\.\d{3} (DIN [0-9A-F]{2}|USB( [0-9A-F]{8})+)$` and its MS field is ⌊cycles/216⌋/1000; DIN `F8` lines: 96 ± 1, consecutive ones 4,500,000 ± 54,000 cycles apart (§4.4, the 250 µs grid) |
| `M17_events_usb` | the same with `100ms usb 90 30 64`, `200ms usb-clock 240 50`, `1200ms usb-clock off`; then with `200ms usb-clock 240` | `counters.usbIn` = 21 (the note and 20 buffers of 50 ms); without a buffer 97 (the note and 96 clocks) |
| `M17_events_jacks` | the seeded card, `--bpm ext --no-start --seconds 3 --clock-log`; script `0ms reset off`, `100ms jack-clock 120 4`, `2s reset on`; then the same without the `jack-clock` line; then with 16 `jack-pulse` lines 125 ms apart from 100 ms instead | rising edges of the clock log: some in [100 ms, 2 s) and none after 2.2 s in the first; none in the second; some in the third. Lines match `^\d+ \d+\.\d{3} [01]$` and alternate 1, 0 |
| `M17_events_panel` | the seeded card, `--no-start --seconds 2 --panel-log`; script `0ms mark boot`, `100ms push`, `300ms mark edit`, `400ms push`, `500ms encoder +1`, `600ms knob 1 +20`, `700ms fader 1 0.5`, `800ms press voice1`, `1000ms press step1`, `1200ms mark step`, `1300ms jacks none`, `1400ms card out`, `1600ms card in`, `1800ms mark end` | four lines matching `^\d+ \d+\.\d{3} \S+ ".*" ".*" (\d,\d+\|-) [0-9A-F]{11}$`, each at the first pass boundary at or after its time (its cycles − time < 54,000 + 1 block); `edit`'s rows differ from `boot`'s; at `step`, LED 39 (step 1) is lit; exit 0 after the card events |
| `M17_stereo` | `--no-start --seconds 3` with STEPS plus `1500ms jacks L2,R2`: 4 channels, then `--stereo out1`, `out2`, `sum` | out 1 has energy before 1.5 s and out 2 after it (the voice moves, §4.6); `out1` = channels 0–1 of the 4-channel file, `out2` = channels 2–3, `sum` = `(a + b) >> 1` per sample (Python's `>>` floors like the C shift) |

Register the six with `emu_render_tests()`.

- [ ] **Step 1: Write the parser, the injection and the logs; the cases.** Run the M17 cases. Expected: 15 of 15. If `M17_events_jacks`'s first render shows no edges, EXT sync needs more than the jack clock to run: read `TriggerOut.c` and `seq_platform.c` for the run condition of sync mode 1 (N4), fix the script, and record why.
- [ ] **Step 2: EMU-CHECK.** N = 118.
- [ ] **Step 3: The gate.**
- [ ] **Step 4: Commit** `emu/render/render_script.{h,c}`, `emu/render/render_main.c`, `emu/render/render_out.{h,c}`, `emu/tests/render_test.py`, `emu/CMakeLists.txt`: "Emulator: lichomat-render's event scripts and logs (E5.3 R2, M17)".

---

### Task R2.5: The prelude and the edit path (M17 pattern, song, bpm, param, bars)

§5.1 (`--pattern`, `--song`, `--bpm`, `--param`, `--bars`, the defaults for what the card lacks), §5.3 ("How the prelude acts"), §5.4 (`param`), §12.1 M17.

**Files:**
- Create: `emu/render/gen_params.py`, `emu/render/render_params.h`
- Modify: `emu/render/render_main.c`, `emu/render/render_fw.{h,c}`, `emu/render/render_script.c`, `emu/tests/render_test.py`, `emu/CMakeLists.txt`

**What to build.**
- **No stopgap for Q23.** F12 (`7f5732f`) fixed the cause, the track number of the pattern select's Euclid request (the first fact above), so the `--pattern` walk runs under UBSan with every check on and needs no ignorelist; `emu/CMakeLists.txt`'s EuklidGenerator line (`-fno-sanitize=integer-divide-by-zero`) stays as it is.
- **The parameter table.** `gen_params.py` writes `render_params.h` from `Core/Inc/Parameters.h`:

```python
#!/usr/bin/env python3
"""render_params.h from Core/Inc/Parameters.h: the PAR_ names of enum ParamEnums
as { "name", PAR_NAME } pairs (stage 1b spec 5.3). M17_param_table checks it.
    gen_params.py <repository root>      (writes to stdout)
(licence header)"""
import re, sys

text = open(sys.argv[1] + '/Core/Inc/Parameters.h').read()
text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
text = re.sub(r'//[^\n]*', '', text)
body = re.search(r'enum\s+ParamEnums\s*\{(.*?)\}', text, re.S).group(1)
names = []
for item in body.split(','):
    m = re.match(r'\s*(PAR_[A-Za-z0-9_]+)\s*(=|$)', item)
    if m:
        names.append(m.group(1))
out = ['/* Generated by emu/render/gen_params.py from Core/Inc/Parameters.h. Do not edit. */',
       '/* SPDX-License-Identifier: GPL-3.0-only */',
       'static const struct { const char *name; uint16_t par; } render_params[] = {']
out += ['\t{ "%s", %s },' % (n[4:].lower(), n) for n in names]
out += ['};', '']
sys.stdout.write('\n'.join(out))
```

  `render_fw.c` includes it after `Parameters.h`. `int rfw_paramFind(const char *name)` matches a name case-insensitively, or a decimal number below `NUM_PARAMS`, else −1. `int rfw_paramRefused(int p)` per P3. The edit path of P3 (`rfw_clamp()`, `rfw_menuEntries()`, `rfw_edit()`) runs in a direct call.
- **The prelude** (P3 step 4): the two walks with their checks, `--bpm`, the fallback with its warning, `--param` in order, and the length for `--bars`. `--param` names are checked before power-on (exit 1: `--param NAME: no such parameter` or the refusal).
- **The `param` event**: P4's row, a direct event; its name is checked when the script is loaded.

**Tests:**

| Id | Inputs | Expected |
|---|---|---|
| `M17_pattern` | the seeded card, `--seconds 0.2 --panel-log` with `0ms mark a`, for P = 0, 5, 21, 47, 63 | exit 0; `selected.pattern` = P; row 2 at `a` starts `Current:` and holds P + 1 (measured: `Current:  22    ` for 21) |
| `M17_song` | the seeded card, `--seconds 0.2`, S = 0, 2, 63 | `selected.song` = S |
| `M17_bpm` | the seeded card, `--seconds 2 --midi-log`: `--bpm 137`; `--bpm ext`; `--bpm mid`; then `--bpm 137 --bars 2` | `selected.bpm` 137 and `selected.sync` 0, DIN F8 lines spaced 3,941,606 ± 54,000 cycles on average (540,000,000 / 137); `sync` 1 and 2 with no DIN F8 line; the `--bars` render has ⌈⌈2 × 240 / 137 × 44,140.625⌉ / 64⌉ blocks |
| `M17_param_engine` | the seeded card with STEPS changed to 16 steps (steps 1–16 on voice 1), `--no-start --seconds 3 --midi-log`: `--param bpm=150`; `--param shuffle=0` and `--param shuffle=127`; `--param vol1=0` against no `--param` | `params.bpm` 150 and F8 every 3,600,000 ± 54,000 cycles; `params.shuffle` 0 and 127, with the Note Ons of the odd 16ths (steps 2, 4, …) later at 127 than at 0 and the even ones equal; `params.vol1` 0, out 1 above −60 dBFS after 500 ms without it and digital silence with it |
| `M17_param_refused` | `--param voice_lfo1=2`; `--param target_lfo1=3`; `--param vel_dest_1=3`; `--param nosuch=1`; `--param 999=1`; a script line `0ms param voice_lfo1=2`; `--param SHUFFLE=200`; then `--param N=5` with N the `paramNumbers.shuffle` of the render before | exit 1 naming the parameter for the first five, `FILE:LINE` for the sixth; the seventh renders with `params.shuffle` 127 (`DTYPE_0B127`; the name in upper case works); the eighth gives `params.shuffle` 5 (a number works) |
| `M17_param_table` | `python3 emu/render/gen_params.py ROOT` | equals `emu/render/render_params.h` byte for byte; the table has as many entries as `enum ParamEnums` has `PAR_` enumerators |
| `M17_bars` | the seeded card with `GLO.CFG` byte 0 set to 0 (EXT): `--bars 2`; the same with `--bpm 120 --bars 2`; with byte 0 = 1 (MID), a script `0ms din-clock 120` and `--bars 2` | the first falls back to 120 BPM internal (no jack clock in the script) and renders 2 bars = 4 s = ⌈⌈4 × 44,140.625⌉ / 64⌉ = 2,759 blocks; the second also 2,759; the third exits 1 naming `--bars` (MID with a clock stays MID, so there is no internal tempo) |
| `M17_sync_fallback` | `GLO.CFG` byte 0 = 0, no `--bpm`, `--seconds 1`; the same with `0ms jack-clock 120 4`; byte 0 = 1 with no clock; with `0ms usb-clock 120` | warning in the summary and `selected.sync` 0, `selected.bpm` 120 for the first and third; no warning and `selected.sync` 1 and 2 for the second and fourth |

Register the eight with `emu_render_tests()`; `M17_param_table` runs the generator from `--root`.

- [ ] **Step 1: Write it; run the M17 cases.** Expected: 23 of 23. `M17_pattern` runs the walk under UBSan without a report (F12).
- [ ] **Step 2: EMU-CHECK.** N = 130.
- [ ] **Step 3: The gate.**
- [ ] **Step 4: Commit** the two new files and the modified ones: "Emulator: lichomat-render's prelude and the menu's edit path (E5.3 R2, M17)", the body naming the walks derived from `buttonHandler.c` and `menu.c`, the refused special cases, the clamp and the fallback.

---

### Task R2.6: The save events, faults and the watchdog (M17 saves, exit codes 3 and 4; M20)

§5.3 (the direct-call table), §5.4 (save events), §5.7 (codes 3, 4), §11, §12.1 M17, M20.

**Files:**
- Modify: `emu/render/render_main.c`, `emu/render/render_fw.{h,c}`, `emu/render/render_script.c`, `emu/tests/render_test.py`, `emu/tests/drift.py`, `emu/tests/drift.json`, `emu/CMakeLists.txt`

**What to build.**
- **Saves** (P4's rows), direct calls in `render_fw.c`: `save-kit N` → `preset_saveDrumsetRam(N, 0)`; `save-pattern N` → `preset_savePatternRAM(N)` (a new slot becomes the active pattern, `presetManager.c` `preset_savePatternRAM()`); `save-project N` → the save page's project save (`menu.c`, `case SAVE_TYPE_PROJECT`):

```c
void rfw_saveProject(void *arg)
{
	const uint8_t n = *(const uint8_t *)arg;
	FRESULT res;
	if (!seq_isRunning())
		HAL_TIM_Base_Stop(&htim2);
	res = preset_saveProject(n);
	if (!seq_isRunning())
		HAL_TIM_Base_Start_IT(&htim2);
	if (res != FR_OK)
		menu_showSdError(res);
}
```

- **Faults** (P2): `cfg.fault` prints and `_exit(3)`; handlers for SIGSEGV, SIGBUS, SIGFPE, SIGILL and SIGABRT on an alternate stack; the two `__*_default_options()`; a watchdog thread (`pthread_create` after power-on) that calls `emu_watchdogPoll()` every 100 ms with the `CLOCK_MONOTONIC` time and on 1 prints P2's hang message and `_exit(4)`. The test options of P3 step 10.
- **M20.** Add to `ITEMS` in `drift.py` the firmware text the renderer copies, then `python3 emu/tests/drift.py record /Users/tk/Code/lxr/build/wt-emulator` and check that only these four keys were added to `drift.json`:

```python
    ('menu_encoderChangeParameter', 'Core/Src/menu/menu.c', 'function', 'menu_encoderChangeParameter'),
    ('getMaxEntriesForMenu', 'Core/Src/menu/menu.c', 'function', 'getMaxEntriesForMenu'),
    ('sd_formatRoutine', 'Core/Src/appMain.c', 'function', 'sd_formatRoutine'),
    ('the save page project save', 'Core/Src/menu/menu.c', 'block',
     (r'case SAVE_TYPE_PROJECT:', r'if\(res != FR_OK\) menu_showSdError\(res\);')),
```

  If `function_text()` takes a forward declaration (`menu.c:58`, `:51`) for the definition, use the `block` kind with the definition's first line instead.

**Tests:**

| Id | Inputs | Expected |
|---|---|---|
| `M17_save` | the seeded card, `--no-start --seconds 4 --keep-card K --summary`; script: `0ms press voice1`, `+100ms press step1`, `+100ms save-pattern 5`, `+100ms param vol1=10`, `+100ms save-kit 7`, `1s save-project 0`, `3.5s mark end` | K's `PROJ00/05-*.PAT` differs from the seed's, `00-*.PAT` equals the seed's (pattern 0's edit reverted when slot 5 became active), `07-*.SND` differs from the seed's, `00-INITK.SND` equals it (the project save writes the RAM slots, and slot 0 was not saved into); each of the five `direct` entries has `at ≥ due` and `at − due < 54,000 + 313,181` cycles (the next pass boundary: the grid, or a pass held up by a block) |
| `M17_fault` | `lichomat-render-test --test-fault` with `segv`, `abort`, `model`, and `ubsan` when the build is sanitized (`render_test.py` gets `--sanitized 0 or 1` from CMake: `$<BOOL:${LICHOMAT_EMU_SANITIZE}>`) | exit 3; stderr holds `firmware fault`, `at cycle`, then 32 lines matching `^\d+ (body\|pass\|boot\|input\|direct) \d+$`, then `the card copy is kept in` and a folder that exists (the test removes it) |
| `M17_hang` | `lichomat-render-test --test-hang` | exit 4 after 10–30 s of wall time; stderr holds `hang` |

Register the three; pass `--sanitized $<BOOL:${LICHOMAT_EMU_SANITIZE}>` in `EMU_RENDER_TEST` and parse it in `main()`.

- [ ] **Step 1: Write the code and the cases; run the M17 cases and M20.** Expected: 26 of 26 and M20 passing.
- [ ] **Step 2: See M20 catch a change.** In a scratch copy (`git worktree add --detach` under `$TMPDIR`, removed afterwards), change one clamp bound in `menu_encoderChangeParameter()`; `python3 emu/tests/drift.py check <copy>` must fail naming `menu_encoderChangeParameter`.
- [ ] **Step 3: EMU-CHECK.** N = 133.
- [ ] **Step 4: The end check of R2.** Configure `build/emu-release` with `-DCMAKE_BUILD_TYPE=Release`, build, run ctest. Expected: `100% tests passed, 0 tests failed out of 133` (`M17_fault` without `ubsan`).
- [ ] **Step 5: The gate.**
- [ ] **Step 6: Commit** the modified files: "Emulator: lichomat-render's save events, faults and watchdog (E5.3 R2, M17, M20)".

---

### Task R3.1: The card fixtures and `make.sh` (M23)

§5.6, §8.4 (inputs), §12.1 M23, P8.

**Files:**
- Create: `emu/tests/cards/make.sh`, `emu/tests/cards/scripts/init.txt`, `fx.txt`, `song.txt`, and the fixtures `emu/tests/cards/{init,fx,song}/` that `make.sh` writes
- Modify: `emu/tests/render_test.py`, `emu/CMakeLists.txt`

**`make.sh`:**

```bash
#!/bin/bash
# The card fixtures of the goldens (stage 1b spec 5.6), made by the firmware: a
# seeded card, one lichomat-render script per fixture with --keep-card, then only the
# files that the script's #keep lines name. M23 checks that this gives the
# committed fixtures byte for byte.
#
#   emu/tests/cards/make.sh RENDERER OUTDIR [FIXTURE...]
#
# (licence header)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$1"; OUT="$2"; shift 2
if [ $# -gt 0 ]; then fixtures=("$@"); else
  fixtures=(); for s in "$HERE"/scripts/*.txt; do fixtures+=("$(basename "$s" .txt)"); done; fi
W="$(mktemp -d "${TMPDIR:-/tmp}/lichomat-cards-XXXXXX")"
trap 'rm -rf "$W"' EXIT
for f in "${fixtures[@]}"; do
  s="$HERE/scripts/$f.txt"
  read -r -a opts <<<"$(sed -n 's/^#! //p' "$s" | tr '\n' ' ')"
  keep="$(sed -n 's/^#keep //p' "$s" | tr '\n' ' ')"
  "$R" --seed-card "$W/$f-seed"
  "$R" --card "$W/$f-seed" --events "$s" --keep-card "$W/$f-kept" --out "$W/$f.wav" "${opts[@]}"
  rm -rf "${OUT:?}/$f"; mkdir -p "$OUT/$f"
  ( cd "$W/$f-kept"
    for g in $keep; do
      found=0
      for p in $g; do
        [ -e "$p" ] || continue
        found=1; mkdir -p "$OUT/$f/$(dirname "$p")"; cp "$p" "$OUT/$f/$p"
      done
      [ $found = 1 ] || { echo "make.sh: $f: nothing matches $g" >&2; exit 1; }
    done )
  echo "make.sh: $f: $(cd "$OUT/$f" && find . -type f | wc -l | tr -d ' ') files"
done
```

**The fixture scripts** follow P8's table. Each starts with `#! --no-start --seconds S` (S long enough to end 500 ms after its `save-project 0`) and its `#keep` lines. They edit only through the panel, `param` and the save events (§5.6):
- **`init.txt`**: for n = 1…7: `press voiceN`, then `press step1`, `step5`, `step9`, `step13`, 100 ms apart; `save-pattern 0`; `save-pattern 1` (pattern 1 becomes a copy and the active one); for n = 2…7: `press voiceN`, then the same four steps again, which clears them; `save-pattern 1`; `save-project 0`.
- **`fx.txt`**: `init.txt`'s pattern 0 part; `param` lines that route voices 1–4 to the FX bus (the FX outputs of `PAR_AUDIO_OUT1`…`4`, values in `outputNamesFx`, `MenuText.h`) and set `fx_dry_wet` to 64; `save-kit 0`; `save-pattern 0`; `save-project 0`.
- **`song.txt`**: patterns 0, 1, 2 with different steps (pattern 0 as `init`'s, pattern 1 voices 1–3 on every second step, pattern 2 voice 1 on steps 1–16), each saved with `save-pattern`; kits 1 and 2 as kit 0 with `param` edits of decays and volumes only, saved with `save-kit 1` and `save-kit 2`; each pattern's kit assigned on the load page (LOAD = `mode3` held + step 8 opens the kit load page, `buttonHandler.c` `case SEQ_KIT`; the encoder selects the kit; a push loads it and stores it in the active pattern's `kitNr`, `menu.c` `case SAVE_TYPE_KIT`); song 0 on the song page (SHIFT + step 10 enters song mode, `buttonHandler.c:868–871`; there each step press appends that pattern, `seq_addPatternToSong()`): steps 1, 2, 3; `save-project 0`.

Check each fixture with a render before committing: `init` plays 112 Note Ons on DIN in 4 bars of pattern 0 at 120 BPM (7 voices × 4 steps × 4 bars) and 16 on pattern 1; `song`'s three `.PAT` files differ, and their `kitNr` bytes are 0, 1, 2 (the byte `preset_savePatternSD()` writes; find its offset there).

**Test:** `M23` (`render_test.py`): `make.sh <lichomat-render> <work>/cards`, then for each fixture the `tree()` of `<work>/cards/F` equals that of `emu/tests/cards/F`, and the set of fixtures equals the set of scripts. Register `M23` with `emu_render_tests()` (it needs `--root` for the committed tree).

- [ ] **Step 1: Write `make.sh` and the scripts; run `emu/tests/cards/make.sh build/emu/lichomat-render emu/tests/cards`.** Expected: `make.sh: init: 5 files`, `fx: 4 files`, `song: 9 files`. Each fixture is a few KB (§5.6): check with `du -sk emu/tests/cards/*/` and record the sizes.
- [ ] **Step 2: The fixture checks above.** Record the Note On counts and the `kitNr` bytes in the task report.
- [ ] **Step 3: M23 in both builds.** Run `M23` in `build/emu` (Debug) and in `build/emu-release`. Expected: PASS in both (the fixtures do not depend on the optimisation level or the sanitizer).
- [ ] **Step 4: EMU-CHECK.** N = 134.
- [ ] **Step 5: The gate.**
- [ ] **Step 6: Commit** `make.sh`, the three scripts, every fixture file (`git add emu/tests/cards/init emu/tests/cards/fx emu/tests/cards/song` names the folders, which hold only fixture files), `render_test.py`, `emu/CMakeLists.txt`: "Emulator: card fixtures made by the firmware (E5.4 R3, M23)", listing the fixtures, their files and sizes.

---

### Task R3.2: `emu_golden.py`, `goldens.json` and G1; determinism, slicing and speed (M14)

§8.4, §8.5, §8.6 (G1), §12.1 M14, §12.2, P6, P7.

**Files:**
- Create: `tools/emu_golden.py`, `emu/tests/goldens/G1.txt`, `emu/tests/goldens/goldens.json`, `emu/tests/golden_selftest.txt`
- Modify: `emu/tests/render_test.py`, `emu/CMakeLists.txt`

**`tools/emu_golden.py`** (standard library only; `#!/usr/bin/env python3`, licence header):

```
emu_golden.py check [ID...] [--render PATH] [--out DIR] [--golden-file FILE]
emu_golden.py bless ID... [--render PATH] [--out DIR]
emu_golden.py diff ID [--against REV] [--render PATH] [--out DIR]
```

Defaults: `--render build/emu/lichomat-render`, `--out build/emu/goldens-out`, all goldens of `emu/tests/goldens/*.txt` for `check` without IDs. Exit 0, 1 on any failure, 2 on a usage error. Its parts:
- `parse(path)` → options, expectations, broken options, broken drops, pin flag, per P6; a malformed header line is a usage error naming the line.
- `render(golden, outdir, broken=False, extra=())` → runs the renderer with the golden's options (P6's path rule for `--card`), the broken additions when asked, `--events` (the golden file, or a copy without the dropped events' lines), and `--out`, `--midi-log`, `--clock-log`, `--panel-log`, `--summary` in `outdir` (emptied first). A non-zero exit fails the golden with the renderer's stderr.
- `expectations(golden, outdir)` → a list of `(line, holds, detail)`, by P6's table.
- `entry(golden, outdir)` → P7's dict.
- `check`: for each ID, render into `OUT/ID`, evaluate the expectations (a failure names the line and the measured value), compare with `goldens.json` (a missing entry fails), then render the broken variant into `OUT/ID-broken` and fail if every expectation holds there (`ID: the broken variant (…) passes every expectation`). On a mismatch print, per §8.5: the first differing WAV chunk with its span in seconds; the peak and RMS of each channel, blessed and now; for each log, the line counts and the first differing 64-line block with its line range and its first current line; the blessed and the current toolchain, with `the toolchains differ: a likely cause` when they do. Outputs stay in `OUT`. `--golden-file FILE` checks one file outside the goldens folder, expectations and broken variant only.
- `bless`: as `check`, but writes the entries; it refuses (exit 1, nothing written) when an expectation fails or the broken variant passes.
- `diff`: REV defaults to `git log -1 --format=%H -- emu/tests/goldens/goldens.json`. `git -C ROOT archive REV | tar -x -C TMP`, `cmake -S TMP/emu -B TMP/build -G Ninja -DCMAKE_BUILD_TYPE=Release`, `cmake --build TMP/build --target lichomat-render`; render the working tree's golden (and fixture) with both renderers; print the first differing frame (index, time, channel, both values) and the first differing line of each log, or `ID: no difference between REV and the working tree`.

**G1** (`emu/tests/goldens/G1.txt`):

```
# G1: the init card, pattern 0 at 120 BPM, 4 bars: boot, kit load, every voice (spec 8.6)
#! --card init --bpm 120 --bars 4
#expect energy L1 -100
#expect summary underrunsAfterT0 0..0
#expect midi DIN 9n 112..112
#broken --faders 0,0,0,0,0,0
0ms mark start
```

**`golden_selftest.txt`**: G1's header with `#broken --seed 1` (the default, so the variant changes nothing).

**Tests** (`render_test.py` imports `tools/emu_golden.py` from `--root`):

| Id | Inputs | Expected |
|---|---|---|
| `G1` | `emu_golden.py check G1 --render <lichomat-render> --out <build>/goldens-out` (a ctest of its own, below) | PASS |
| `M14_repeat` | G1 rendered twice | WAV, MIDI, clock and panel logs byte-identical |
| `M14_slices` | G1 with `lichomat-render`, and with `lichomat-render-test --test-slice` | the four outputs byte-identical (§12.1 M14: 470 and 471 frames per call, a hook that waits at every call) |
| `M14_broken` | `emu_golden.py check --golden-file emu/tests/golden_selftest.txt` | exit 1 and `the broken variant (--seed 1) passes every expectation` |
| `M14_speed` (Release only) | G1 | `speed` ≥ 10 (§2 G2, §12.1 M14) |

Register in `emu/CMakeLists.txt`:

```cmake
# the goldens (spec 8.6, 12.2): one ctest id per emu/tests/goldens/*.txt
file(GLOB EMU_GOLDENS CONFIGURE_DEPENDS "${EMU_DIR}/tests/goldens/*.txt")
foreach(g IN LISTS EMU_GOLDENS)
    get_filename_component(id "${g}" NAME_WE)
    add_test(NAME ${id} COMMAND ${Python3_EXECUTABLE} "${LXR_ROOT}/tools/emu_golden.py" check ${id}
        --render $<TARGET_FILE:lichomat-render> --out "${CMAKE_BINARY_DIR}/goldens-out")
    set_tests_properties(${id} PROPERTIES TIMEOUT 600)
endforeach()
emu_render_tests(M14_repeat M14_slices M14_broken)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    emu_render_tests(M14_speed)	# spec 9.2: Release runs M14's speed check
endif()
```

- [ ] **Step 1: Write `emu_golden.py`, G1 as above, and the selftest.** Run `python3 tools/emu_golden.py check G1`. Expected: it fails only because `goldens.json` has no entry, and prints L1's RMS. Replace G1's `-100` by P6's threshold (the RMS minus 10 dB, rounded down to a multiple of 5) and record the RMS. The 112 Note Ons are fixed by the fixture (7 voices × 4 steps × 4 bars).
- [ ] **Step 2: Bless G1 in the Debug build:** `python3 tools/emu_golden.py bless G1`. Then check G1 against the Release renderer: `python3 tools/emu_golden.py check G1 --render build/emu-release/lichomat-render --out build/emu-release/goldens-out`. Expected: PASS: the Debug (-O0, UBSan, pattern-initialised locals) and Release (-O2) renders are byte-identical (§8.5). A mismatch stops the task: report it with the first differing chunk.
- [ ] **Step 3: `diff`.** `python3 tools/emu_golden.py diff G1 --against HEAD`. Expected: `G1: no difference between HEAD and the working tree`.
- [ ] **Step 4: The M14 cases.** Expected: PASS, and `M14_speed` in `build/emu-release` with the measured speed recorded (R1 measured 65–190× at `-O2`).
- [ ] **Step 5: See the slicing check bite.** Temporarily make `--test-slice` skip one direct event's boundary rule (run the due `mark` one return late). `M14_slices` must fail on the panel log. Undo.
- [ ] **Step 6: EMU-CHECK.** N = 138 (Release 139).
- [ ] **Step 7: The gate.**
- [ ] **Step 8: Commit** the four new files, `render_test.py`, `emu/CMakeLists.txt`: "Emulator: golden tests, emu_golden.py and G1 (E5.4 R3, M14)", with G1's measured RMS, the threshold, its SHA-256 prefix, the speed, and that Debug and Release hash the same.

---

### Task R3.3: G2, G3 and G4

§8.6 rows G2–G4, P6, P8.

**Files:**
- Create: `emu/tests/goldens/G2_tube.txt`, `G2_fold.txt`, `G2_rat.txt`, `G2_ringmod.txt`, `G2_comp.txt`, `G2_delay.txt`, `G2_pingpong.txt`, `G3.txt`, `G4.txt`
- Modify: `emu/tests/goldens/goldens.json`

**What each golden is.** Every golden: an explicit tempo or clock (§8.4), `--bars` or `--seconds` up to 10 s.

| Id | Options | Script | Expectations | Broken |
|---|---|---|---|---|
| `G2_tube`, `G2_fold`, `G2_rat` | `--card fx --bpm 120 --bars 2 --param fx_type=1 --param fx_dist_type=T`, T = 0, 1, 2 (`distNames`: `tub`, `fld`, `clp`; the spec's 'rat' is the third, `clp`) | `0ms down shift`, `+20ms press step15` (SHIFT + step 15 opens the FX page, `buttonHandler.c` `case SEQ_FX`), `+100ms up shift`, `+200ms mark fx` | `#expect lcd fx TEXT` with the type's name as the FX page shows it (measured); `#expect energy L1 …` | `--param fx_type=0` |
| `G2_ringmod`, `G2_comp` | `fx_type=2`, `fx_type=3` (`fxNames`: `off drv rng cmp del`) | the same | the same | the same |
| `G2_delay`, `G2_pingpong` | `fx_type=4 --param fx_delay_type=0` or `=1` (`delayNames`: `Del`, `Pp`) | the same | the same | the same |
| `G3` | `--card song --song 0 --bpm 120 --bars 4` | `0ms down shift`, `+20ms press step10` (the song page; no step press there, which would append to the song), `+100ms up shift`; `mark b1` at 100 ms, `b2` at 2.1 s, `b3` at 4.1 s, `b4` at 6.1 s (100 ms into each 2 s bar) | `#expect lcd bN TEXT` with the song page's position text at each mark (measured; it must differ between b1, b2 and b3) | `--pattern 0` (replaces `--song`, P1) |
| `G4` | `--card song --bpm 120 --seconds 8`; `#pin panel` | at 1 s mute voices 2 and 3: `down shift`, `press voice2`, `press voice3`, `up shift` (SHIFT + voice toggles a mute outside the perf page, `buttonHandler.c:1497–1515`), then `mark mutes`; at 2 s a roll (perf mode, step 1 held 500 ms; `buttonHandler.c` `case SELECT_MODE_PERF`); at 3 s a pattern change to pattern 1 (SHIFT + step 16, then step 2), `mark pattern` at 3.5 s; at 4 s `knob 1 +30`; at 5 s `fader 1 0.3`; `mark end` at 7.5 s | `#expect led mutes MASK VALUE` with the voice LEDs that show the mutes (measured); `#expect lcd pattern Current:   2` (P + 1, as in `M17_pattern`) | `#broken-drop down,up,press` |

- [ ] **Step 1: Write the nine goldens; measure and set their expectations.** For each, run `python3 tools/emu_golden.py check ID` (it fails on the missing entry and prints what it measured), read the panel log in `build/emu/goldens-out/ID/`, write each expectation by P6's rule, and run `check` again: it must report the broken variant failing. Record every measured value in the task report.
- [ ] **Step 2: Bless** `python3 tools/emu_golden.py bless G2_tube G2_fold G2_rat G2_ringmod G2_comp G2_delay G2_pingpong G3 G4`, then check all nine against `build/emu-release/lichomat-render`. Expected: PASS (Debug and Release equal).
- [ ] **Step 3: EMU-CHECK.** N = 147 (Release 148).
- [ ] **Step 4: The gate.**
- [ ] **Step 5: Commit** the nine goldens and `goldens.json`: "Emulator: goldens G2-G4, the FX bus, song mode and the panel (E5.4 R3)", with each golden's expectations, measured values and broken variant.

---

### Task R3.4: G5, G6, G7 and G8

§8.6 rows G5–G8, §4.3, §4.4, N14, N21, Q17, P6.

**Files:**
- Create: `emu/tests/goldens/G5.txt`, `G6.txt`, `G7.txt`, `G8_120.txt`, `G8_137.txt`
- Modify: `emu/tests/goldens/goldens.json`

| Id | Options | Script | Expectations (fixed by the rules unless marked measured) | Broken |
|---|---|---|---|---|
| `G5` | `--card init --bpm 120 --seconds 6 --param prescaler_clock_out1=4` (`ppqOutNames`: index 4 is 24 PPQ) | `2s down shift`, `+20ms press step16`, `+80ms up shift`, `+100ms press step2` (pattern 1 from the next bar, with a Program Change on the global channel, §4.4), `5s press start` (Stop) | on DIN and USB: `F8` 287..289 (48 per s for 6 s, also while stopped); `FA` 1..1; `FC` 1..1; `Cn` the measured count exactly, at least 1; `9n` and `8n` the measured counts ± 2, equal to each other; `#expect clock 239..241 0ms..5s` (24 PPQ for the 5 s of running at 2 beats/s) | `--bpm ext` |
| `G6` | `--card init --bpm 120 --bars 1` | `0ms mark start` | `#expect midi DIN 9n 7..7 0ms..20ms` (the 7 Note Ons of step 1); `#expect midi DIN F8 M..M 0ms..20ms` with M the measured count (0 or 1: whether an F8 falls in the window depends on the free-running timeline, §4.4); `#expect midi DIN F8 95..97` over the 2 s | `--pattern 1` (one track) |
| `G7` | `--card init --bpm mid --no-start --seconds 6` | `0ms din C0 01` (a Program Change before the Start, N14; if `GLO.CFG`'s RX filter drops it, add `--param midi_filt_rx=…` with `MIDI_FILTER_PROG_C` and say so), `100ms din-clock 128`, `500ms din FA`, `3s din FC`, `3.5s din F2 08 00` (SPP: bar 1, step 9), `4s din FB` | `energy L1` in 0.6s..2.9s and in 4.1s..6s (measured); `summary syncOverflows 0..0`; `summary seqc.dropouts 0..0`; `summary selected.sync 2..2`; `summary seqc.syncIgnored` measured | `--bpm 120` (internal; with `--no-start` nothing plays) |
| `G8_120`, `G8_137` | `--card init --bpm mid --no-start --seconds 6` | `100ms usb-clock 120 11.6` (or 137), `500ms usb FA`, `3s usb FC`, `3.5s usb F2 08 00`, `4s usb FB` | as G7; plus `summary seqc.intervalsExcluded` measured (N21: one transfer per 11.6 ms buffer) | `--bpm 120` |

- [ ] **Step 1: Write the five; measure and set the expectations (as in Task R3.3, Step 1).** Every expectation marked fixed must hold as written; a different value stops the task with both numbers.
- [ ] **Step 2: Bless the five and check them against the Release renderer.** Expected: PASS.
- [ ] **Step 3: EMU-CHECK.** N = 152 (Release 153).
- [ ] **Step 4: The gate.**
- [ ] **Step 5: Commit** the five goldens and `goldens.json`: "Emulator: goldens G5-G8, master, the F8 behind Note Ons, DIN and USB slave (E5.4 R3)".

---

### Task R3.5: G9, G10 and the ping-pong scenario (M16)

§8.6 rows G9–G10, §7 ("The ping-pong delay report"), §12.1 M16, P6.

**Files:**
- Create: `emu/tests/goldens/G9_ppq4.txt`, `G9_ppq24.txt`, `G10.txt`, `emu/tests/test_pingpong.c`
- Modify: `emu/tests/goldens/goldens.json`, `emu/CMakeLists.txt`

| Id | Options | Script | Expectations | Broken |
|---|---|---|---|---|
| `G9_ppq4`, `G9_ppq24` | `--card init --bpm ext --no-start --seconds 7`, and for 24 PPQ `--param prescaler_clock_in=1` (`ppqInNames`: 0 is 4, 1 is 24) | `0ms reset on`, `10ms jack-clock 120 4` (or 24), `500ms reset off` (the gate opens: runs from step 1), `3s reset on` (closed), `5s reset off`, `6.5s reset on` | `energy L1` in 0.6s..2.9s and 5.1s..6.4s (measured); `silent L1` in 4s..5s. If that window is not digital silence, find where the init kit's tails end and move the window there; report it if none is silent before 5 s | `--bpm 120` |
| `G10` | `--card init --bpm 120 --bars 4 --seed 1` | at 1 s: `param active_step=0` then `param step_prob=30` (the probability of step 1 on the active voice, voice 1; `menu_parseGlobalParam()` `PAR_ACTIVE_STEP`, `PAR_STEP_PROB`); check in the summary's `params` that both took | `#expect midi DIN 9n MIN..111`: below the 112 active steps, which start at 100 %; MIN measured − 2 | `#broken-drop param` |

**M16** — `emu/tests/test_pingpong.c`, a firmware test (`emu_testBoot()` without a card, R1's harness). The scenario of §7: the delay in normal mode, then a switch to ping-pong at write pointers above and below 0x4000, the case fw 1.7 wrote past `FX_BUFFER` (E4.8, `2bb5d80`).

```c
/* M16: the ping-pong delay's mode switch (stage 1b spec 7, 12.1; E4.8). Under ASan
 * (CI) a write past FX_BUFFER is a report; here it checks that the scenario
 * reaches the case: switches at write pointers in [0x4000, 0x4400), whose first
 * ping-pong write lands up to 2 KB past the buffer without E4.8. */
TEST(M16_pingpong)
{
	bootNoCard();					/* emu_testBoot() with cardIn 0, then 100 ms */
	sendCC2(PAR_FX_TYPE, 4);			/* delay (fxNames) */
	sendCC2(PAR_FX_DELAY_TYPE, 0);			/* normal (delayNames) */
	unsigned high = 0, low = 0;
	for (int round = 0; round < 12; round++)
	{
		const int wantHigh = round % 2 == 0;
		/* run one pass at a time until the write pointer is where this round wants it */
		for (int k = 0; k < 20000; k++)
		{
			const uint32_t w = fxBus.delay.write;
			if (wantHigh ? (w >= 0x4000u && w < 0x4400u) : (w < 0x3C00u))
				break;
			emu_runUntil(emu_now() + 1u);
		}
		const uint32_t w = fxBus.delay.write;
		ASSERT_TRUE(wantHigh ? (w >= 0x4000u && w < 0x4400u) : (w < 0x3C00u));
		(wantHigh ? high : low)++;
		sendCC2(PAR_FX_DELAY_TYPE, 1);		/* to ping-pong */
		emu_testRunMs(20);
		ASSERT_TRUE(fxBus.delay.write < 0x4000u);	/* ping-pong runs in one half */
		sendCC2(PAR_FX_DELAY_TYPE, 0);		/* back to normal */
		emu_testRunMs(20);
	}
	ASSERT_EQ(high, 6);
	ASSERT_EQ(low, 6);
}
```

`sendCC2(p, v)` is a direct call of `frontPanel_sendData(CC_2, p − 128, v)`. Before the loop, assert that the pointer moves: after `emu_testRunMs(10)` it has advanced by the frames rendered (mod 0x8000). If 20,000 passes do not reach a window, the delay does not run without input: trigger voice 1 with DIN Note Ons (`90 30 7F`, as R1's tests) every 200 ms, and say so. Add `emu_add_test(test_pingpong IDS M16_pingpong)`.

- [ ] **Step 1: The three goldens: write, measure, set, bless, check against Release** (as in Task R3.3). Expected: PASS.
- [ ] **Step 2: M16.** Run `ctest --test-dir build/emu -R M16 --output-on-failure`. Expected: PASS. Record the 12 pointers in the task report. Its detection power is ASan's, which runs in CI only (Q19); say so in the commit.
- [ ] **Step 3: EMU-CHECK.** N = 156 (Release 157).
- [ ] **Step 4: The gate.**
- [ ] **Step 5: Commit** the three goldens, `goldens.json`, `test_pingpong.c`, `emu/CMakeLists.txt`: "Emulator: goldens G9 and G10, and the ping-pong regression scenario (E5.4 R3, M16)".

---

### Task R3.6: `emulator.yml`, its dry run, the `check.sh` step and the end check

§9.2 (`tools/check.sh`), §9.3, §13 ("Each increment ends").

**Files:**
- Create: `.github/workflows/emulator.yml`
- Modify: `tools/check.sh`, `tools/ci_dry_run.rb`

**`emulator.yml`:**

```yaml
# CI of the LICHOMAT emulator (stage 1b spec 9.3): emu/'s host tests and goldens in a
# Release build and in a sanitizer build, on macOS arm64 with the Command Line Tools.
# It only builds and tests: nothing is pushed, tagged or released from here. It runs
# on every push of the emulator branch (tracker Q19).
#
# (licence header)

name: emulator

on:
  push:
  pull_request:

permissions:
  contents: read

concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

defaults:
  run:
    shell: bash

env:
  DEVELOPER_DIR: /Library/Developer/CommandLineTools
  # the pins of tools/setup-toolchain.sh
  CMAKE_PKG: cmake-3.31.12-macos-universal
  CMAKE_SHA256: 799af7fd545db9bf1b9cfe72f8095880e727a2d4e0df0e3dffc3bc7b95c2d3b0
  NINJA_SHA256: c99048673aa765960a99cf10c6ddb9f1fad506099ff0a0e137ad8960a88f321b

jobs:
  emu-release:
    name: Emulator (Release)
    runs-on: macos-15
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v7
      - name: Fetch CMake and Ninja
        run: |
          mkdir -p .toolchain/dl .toolchain/bin
          curl -fL --retry 3 -o ".toolchain/dl/$CMAKE_PKG.tar.gz" \
            "https://github.com/Kitware/CMake/releases/download/v3.31.12/$CMAKE_PKG.tar.gz"
          echo "$CMAKE_SHA256  .toolchain/dl/$CMAKE_PKG.tar.gz" | shasum -a 256 -c -
          tar -xzf ".toolchain/dl/$CMAKE_PKG.tar.gz" -C .toolchain
          curl -fL --retry 3 -o .toolchain/dl/ninja-mac.zip \
            "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-mac.zip"
          echo "$NINJA_SHA256  .toolchain/dl/ninja-mac.zip" | shasum -a 256 -c -
          unzip -o -q .toolchain/dl/ninja-mac.zip -d .toolchain/bin
          echo "$GITHUB_WORKSPACE/.toolchain/$CMAKE_PKG/CMake.app/Contents/bin" >> "$GITHUB_PATH"
          echo "$GITHUB_WORKSPACE/.toolchain/bin" >> "$GITHUB_PATH"
      - name: Record the toolchain
        run: |
          {
            echo "### Toolchain"; echo; echo '```'
            clang --version | head -n 1
            echo "SDK $(xcrun --show-sdk-version)"
            cmake --version | head -n 1
            echo "ninja $(ninja --version)"
            echo '```'
          } | tee -a "$GITHUB_STEP_SUMMARY"
      - name: Configure
        run: cmake -S emu -B build/emu-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DLICHOMAT_EMU_SANITIZE=
      - name: Build
        run: cmake --build build/emu-release 2>&1 | tee build/emu-release/build.log
      - name: Check for compiler warnings
        run: |
          n=$(grep -c "warning:" build/emu-release/build.log || true)
          echo "compiler warnings: $n"
          if [ "$n" -ne 0 ]; then grep "warning:" build/emu-release/build.log | sort -u; exit 1; fi
      - name: Test
        run: ctest --test-dir build/emu-release --output-on-failure
      - name: Check the linked libraries
        run: |
          b=build/emu-release/lichomat-render
          bad=$(otool -L "$b" | tail -n +2 | awk '{print $1}' | grep -v -E '^(/usr/lib/|/System/Library/)' || true)
          if [ -n "$bad" ]; then echo "$b links: $bad"; exit 1; fi
          echo "$b links only system libraries"
      - name: Package lichomat-render
        run: |
          d="build/emu-release/package/lichomat-render-${GITHUB_SHA::12}"
          rm -rf build/emu-release/package; mkdir -p "$d"
          cp build/emu-release/lichomat-render LICENSE NOTICE.md "$d/"
          cp emu/libm/LICENSE "$d/LICENSE.arm-optimized-routines"
          head -n 18 Middlewares/Third_Party/FatFs/src/ff.h > "$d/LICENSE.fatfs"
          codesign --force --sign - "$d/lichomat-render"
          ditto -c -k --keepParent "$d" "$d.zip"
      - name: Upload lichomat-render
        uses: actions/upload-artifact@v7
        with:
          name: lichomat-render-${{ github.sha }}
          path: build/emu-release/package/*.zip
          if-no-files-found: error
      - name: Upload the golden outputs
        if: failure()
        uses: actions/upload-artifact@v7
        with:
          name: goldens-release-${{ github.sha }}
          path: build/emu-release/goldens-out
          if-no-files-found: ignore

  emu-sanitize:
    name: Emulator (ASan and UBSan)
    runs-on: macos-15
    timeout-minutes: 45
    steps:
      - uses: actions/checkout@v7
      # the next two steps are emu-release's, word for word (ci_dry_run.rb checks it)
      - name: Fetch CMake and Ninja
        run: |
          (the same lines as in emu-release)
      - name: Record the toolchain
        run: |
          (the same lines as in emu-release)
      - name: Configure
        run: cmake -S emu -B build/emu-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLICHOMAT_EMU_SANITIZE=address,undefined
      - name: Build
        run: cmake --build build/emu-sanitize 2>&1 | tee build/emu-sanitize/build.log
      - name: Check for compiler warnings
        run: |
          n=$(grep -c "warning:" build/emu-sanitize/build.log || true)
          echo "compiler warnings: $n"
          if [ "$n" -ne 0 ]; then grep "warning:" build/emu-sanitize/build.log | sort -u; exit 1; fi
      - name: Test
        run: ctest --test-dir build/emu-sanitize --output-on-failure
      - name: Upload the golden outputs
        if: failure()
        uses: actions/upload-artifact@v7
        with:
          name: goldens-sanitize-${{ github.sha }}
          path: build/emu-sanitize/goldens-out
          if-no-files-found: ignore
```

The two `(the same lines as in emu-release)` blocks are written out in the file: copy emu-release's `run:` text of those two steps verbatim (no YAML anchors: Ruby's Psych 4 rejects aliases in `YAML.load_file`). The app (R4) adds its own steps to this file.

**`tools/ci_dry_run.rb`.** Pick the workflow by the job: `emu-release` and `emu-sanitize` read `.github/workflows/emulator.yml`, the others `build.yml` as now. For `emulator.yml`, check (each line printed `ok`/`FAILED`, as now): it runs on push and pull_request; the jobs are `emu-release` and `emu-sanitize` on `macos-15` with timeouts 30 and 45; `DEVELOPER_DIR` is the Command Line Tools; `CMAKE_PKG`, `CMAKE_SHA256` and `NINJA_SHA256` equal the values in `tools/setup-toolchain.sh` (read them from it with a regular expression); the release job configures with `-DCMAKE_BUILD_TYPE=Release -DLICHOMAT_EMU_SANITIZE=` and the sanitize job with `-DLICHOMAT_EMU_SANITIZE=address,undefined`; the two jobs' `Fetch CMake and Ninja` and `Record the toolchain` steps are equal; the release job uploads `lichomat-render` and both upload the golden outputs on failure. Local differences, added to the header comment: the step `Fetch CMake and Ninja` is skipped (the tools come from `.toolchain/` through `tools/env.sh`); `address,undefined` becomes `undefined`; `build/emu-release` and `build/emu-sanitize` become `build/ci-dry-run/emu-release` and `…/emu-sanitize`; `GITHUB_SHA` is `git rev-parse HEAD`; `GITHUB_PATH` is a file under `build/ci-dry-run` that nothing reads.

**`tools/check.sh`.** Add a step after the host tests, and the header comment's step list:

```bash
if [ "$(uname -s)-$(uname -m)" = "Darwin-arm64" ] && [ -f "$ROOT/emu/CMakeLists.txt" ]; then
  echo "== emulator (emu/, Debug with UBSan)"
  if cmake -S "$ROOT/emu" -B "$ROOT/build/emu" -G Ninja >>"$LOG" 2>&1 \
     && cmake --build "$ROOT/build/emu" >>"$LOG" 2>&1; then
    (cd "$ROOT/build/emu" && ctest --output-on-failure 2>&1) | tee -a "$LOG" | tail -n 25 || status=1
  else
    echo "emulator build FAILED"; grep -E "error|warning" "$LOG" | head -n 30; status=1
  fi
else
  echo "== emulator: skipped (macOS on arm64 only)"
fi
```

The warning count at the end now covers the emulator's build too (§9.2: "The warning count stays 0").

- [ ] **Step 1: Write the three changes.**
- [ ] **Step 2: Dry runs.** `ruby tools/ci_dry_run.rb emu-release` and `ruby tools/ci_dry_run.rb emu-sanitize`. Expected: every check `ok`, `== skipped: Fetch CMake and Ninja`, then `dry run of emu-release: 7 steps passed` (Record, Configure, Build, the warnings, Test, the linked libraries, Package) and `dry run of emu-sanitize: 5 steps passed`. Also `ruby tools/ci_dry_run.rb host-tests` still passes. Unzip the package into a temporary folder: it holds `lichomat-render` (which runs `--version` and is signed: `codesign -v` passes), `LICENSE`, `NOTICE.md`, `LICENSE.arm-optimized-routines`, `LICENSE.fatfs`.
- [ ] **Step 3: `tools/check.sh`.** Expected: `100% tests passed, 0 tests failed out of 55`, `100% tests passed, 0 tests failed out of 156`, the image line `OK`, `== compiler warnings: 0`, the recorded `memory:` line. Then `tools/fw_checks.sh`: every task `PASS`.
- [ ] **Step 4: The end check of R2 and R3.** `ctest --test-dir build/emu-release` gives `out of 157`, and so does `ctest --test-dir build/emu-release -j8`; `git status --short` shows only `?? .toolchain`; `git worktree list` shows no scratch worktree; no `lichomat-render-*`, `lichomat-case-*` or `lichomat-cards-*` folder is left in `$TMPDIR`.
- [ ] **Step 5: Commit** `.github/workflows/emulator.yml`, `tools/check.sh`, `tools/ci_dry_run.rb`: "Emulator: CI workflow, its dry run and the check.sh step (E5.4 R3)", the body saying that the workflow runs on every push of `emulator` (Q19 allows pushing), and what the dry run replaces.

---

## The end state, which R4 starts from

- Branch `emulator`, pushed to the owner's fork (`origin`, Q19): R2.4's end, then F12, F11, F6b and the parallel-ctest fix (`7f5732f`, `7399c7d`, `cf8fb4d`, `c253669`), then one commit per task from R2.5 to R3.6. `tools/check.sh` passes 55 host tests and 156 emulator ids (Debug, UBSan) with 0 warnings; Release passes 157; both also with `ctest -j8`.
- `lichomat-render` and `lichomat-render-test` in `build/emu*/`; `emu/render/render_fw.c` is the only renderer file that calls the firmware. R4's first run of the app runs the bundled `lichomat-render --seed-card` (§6.7).
- `emulator.yml` runs on GitHub from the first push after Task R3.6. That run is also the first ASan run of R1's and R2's tests: findings there are new firmware questions.

## Points for the owner and for the controller

- **Q21–Q23, answered.** The owner approved the three firmware fixes for v0.2, and they are on `emulator` before R2.5: F12 (`7f5732f`, Q23) passes the active voice to the pattern select's Euclid request; F11 (`7399c7d`, Q21) sets the morph target before the boot's first pot read; F6b (`cf8fb4d`, Q22) makes `freq2PhaseIncr1024()` and `freq2PhaseIncr32767()` saturate. No ignorelist and no fixture limit stands in for them. They reach the device with v0.2, with the device check U9 for F12 (§12.4); F11 and F6b leave the device's output unchanged (§7).
- **Q19.** `emulator` is pushed, so the CI workflow of Task R3.6 runs on GitHub from its first push. M16's real check, a write past `FX_BUFFER` under ASan, runs only there, and so do R1's tests under ASan.
- **Spec readings this plan made** (to be confirmed or changed): the logs hold only events after t0; the WAV holds whole blocks; the end hook reads three counter getters; expectations take time windows, and `#expect led`, `#broken-drop` and `#pin panel` exist; a broken variant fails when one expectation fails; `goldens.json` keeps per-64-line CRCs of the logs; G2, G8 and G9 are several renders; the `--version` source line joins the name allow-list.
- **Blesses.** Tasks R3.2–R3.5 bless every golden for the first time, in the commits that add them. The milestone report lists them (§8.5), with the measured values, so that the owner can listen to the renders in `build/emu/goldens-out/`.
- **`--bars` and the sync fallback.** A project in EXT or MID sync whose script sends no clock renders at 120 BPM internal (§5.1's default), so `--bars` works there; it is a usage error only when the script does send that clock.
- **Cost.** `M17_hang` waits 10 s of wall time; the goldens and the renderer tests add about a minute to `tools/check.sh`.
