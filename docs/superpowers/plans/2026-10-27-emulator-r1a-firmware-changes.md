# LICHOMAT Stage 1b, R1 part 1: firmware changes F1–F10 and the strict timing core — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the ten firmware changes F1–F10 that the stage 1b emulator needs, each as one small commit with its own test and the acceptance of spec §7, and run stage 1's T-tests also against a strict-FP copy of the timing core (§8.2, §12.1), on the new local branch `emulator`.

**Architecture:** The device firmware stays one build. Five changes keep the release `.bin` byte-identical (F5, F6, F7, F8, F10); their task compares it with `cmp` against the build of the commit before. F4 moves `main()`'s boot and loop body into `appMain.c` with a checked script, so that `git diff --color-moved` shows a move plus the listed edits. F1, F2, F3 and F9 change the binary as §7 describes. The host tests go into the existing `tests/` project (CMake, ctest, UBSan): as in E4.5–E4.9, they compile the firmware file under test against `tests/stubs/`. Part 2 (`docs/superpowers/plans/2026-10-27-emulator-r1b-virtual-device.md`) starts where this plan ends and uses the interfaces listed in its last section.

**Tech Stack:** C (gnu11 for the STM32F765 with Arm GNU 14.3.rel1; C11 for the host tests with Apple clang, and with GCC on Linux in CI), CMake 3.31 and Ninja, ctest with UBSan, Python 3 for the edit and check scripts, git 2.39.

---

The spec is `docs/superpowers/specs/2026-10-27-emulator-design.md`, accepted on 2026-10-27 with the defaults of Q12–Q19. A `§` below means a section of it. It gives the reasons; this plan gives the steps.

**Rules for every task**

- Work in `/Users/tk/Code/lxr` on the branch `emulator`. Nothing is pushed: the owner has not answered Q19.
- Every Bash call starts in a new shell. Start each command with `cd /Users/tk/Code/lxr &&`, and add `source tools/env.sh >/dev/null &&` where it needs `cmake`, `ctest` or `arm-none-eabi-*`.
- Line numbers are those at `timing-core` 67ec609, unless a task says otherwise.
- C files and headers in `Core/` use CRLF. `Core/CMakeLists.txt` and the new `Core/sources.cmake` use LF, and so does everything outside `Core/`. The edits to `Core/` files are Python scripts that write CRLF bytes; a check after each one counts CRLF and LF.
- `git add` names each file. Never `git add -A`, never `git commit --amend`. Each commit message ends with the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- The gate of each task that changes code: `tools/check.sh` is green (every ctest id passes, the image check prints OK, `== compiler warnings: 0`); `tools/fw_checks.sh` prints `PASS` for E2.2, E2.3, E2.4, E2.5, E2.6, E2.7, E2.8, E3.3 and E4.9; ITCM fits; DTCM + RAM stays at most 391824 B (review row R6.2; it is 391788 B at the base).
- "The base build" is the release image of the commit before the change (§7).

## File structure

| File | Responsibility | Tasks |
|---|---|---|
| `Core/sources.cmake` (new, LF) | The nine module source lists, `LXR_SOURCES_PLATFORM` … `LXR_SOURCES_USB_APP`, used by the firmware build and by the emulator | R1a.2 (F8), R1a.3 (adds `Src/appMain.c`) |
| `Core/CMakeLists.txt` (LF) | Builds the module libraries from the lists; passes the splash name and version to `appMain.c` | R1a.2, R1a.3 |
| `tools/fw_checks.sh` | E2.6's tree check also reads `Core/sources.cmake` | R1a.2 |
| `Core/Src/appMain.c` (new) | `appMain_boot()` and `appMain_loopOnce()`: main.c's boot and loop body, moved; the morph target without a card | R1a.3 (F4), R1a.5 (F2) |
| `Core/Inc/appMain.h` (new) | Declares the two functions | R1a.3 |
| `Core/Src/main.c` | The CubeMX init and `MX_USB_DEVICE_Init()`, then `appMain_boot()` and a loop of `appMain_loopOnce()` | R1a.3 |
| `Core/Src/Soundengine/DSPAudio/modulationNode.c` | A node at `PAR_NONE` keeps its original value | R1a.4 (F1) |
| `tests/test_modnode.c` (new) | ctest id `modnode` | R1a.4 |
| `Core/Src/menu/menu.c` | The clear page's automation text stays within row 0 | R1a.6 (F3) |
| `Core/Src/Soundengine/Sequencer/sequencer.c` | `seq_cbRandom()` returns `SEQ_RANDOM()`, which defaults to `GetRngValue()` | R1a.7 (F5) |
| `tests/stubs/seq_random_test.h` (new) | Forced into `sequencer.c` by `test_sequencer`: `SEQ_RANDOM()` becomes `seqTest_random()` | R1a.7 |
| `tests/test_sequencer.c` | The test of the hook | R1a.7 |
| `Core/Inc/Soundengine/DSPAudio/f2u32.h` (new) | `f2u32_sat()`, the saturating float-to-uint32 conversion | R1a.8 (F6) |
| `Core/Src/Soundengine/DSPAudio/Oscillator.c` | `f2u32_sat()` at the 9 sites (F6); the row's own first sample after sample 1023 (F9) | R1a.8, R1a.10 |
| `tests/test_f2u32.c` (new) | ctest id `f2u32` | R1a.8 |
| `Core/Src/Soundengine/AudioCodecManager.c` | 4-byte aligned DMA buffers | R1a.9 (F7) |
| `tests/test_dmabuf.c` (new) | ctest id `dmabuf` | R1a.9 |
| `tests/test_dsp.c` | The test of the wavetable read | R1a.10 |
| `Core/Src/Preset/presetManager.c`, `Core/Inc/Preset/PresetManager.h` | `uint8_t preset_morphBusy(void)` | R1a.11 (F10) |
| `tests/test_presets.c` | The test of `preset_morphBusy()` | R1a.11 |
| `tests/CMakeLists.txt` | Registers the new tests; the strict copies of the core and the `STRICT_CORE` option | R1a.4, R1a.7, R1a.8, R1a.9, R1a.12 |

## Task order

The spec lists F1–F10. This plan runs F8 and F4 first, then the others in the spec's order, then the strict core:

1. **F8 before F4**, as §7 allows: F4 then adds `appMain.c` to `LXR_SOURCES_PLATFORM`, and F8's `.bin` and `.map` comparison runs on the untouched base.
2. **F4 before F2.** F4's move script relies on main.c's line numbers at the HEAD, and F2 would shift them. F2 then edits the no-card branch in its new place, `appMain.c`.
3. **The strict core last.** It touches only `tests/`.

Each byte-identical check compares with the commit just before it, so the order does not weaken any check.

| Task | Change | ctest ids after it |
|---|---|---|
| R1a.1 | the branch, the base check | 32 |
| R1a.2 | F8 | 32 |
| R1a.3 | F4 | 32 |
| R1a.4 | F1 | 33 (`modnode`) |
| R1a.5 | F2 | 33 |
| R1a.6 | F3 | 33 |
| R1a.7 | F5 | 33 (a new test in `sequencer`) |
| R1a.8 | F6 | 34 (`f2u32`) |
| R1a.9 | F7 | 35 (`dmabuf`) |
| R1a.10 | F9 | 35 (a new test in `dsp`) |
| R1a.11 | F10 | 35 (a new test in `presets`) |
| R1a.12 | the strict core | 55 (20 `<id>_strict`) |

---

### Task R1a.1: The `emulator` branch and the base check

§13: `emulator` is created from `timing-core`, which stays frozen for the owner's checks.

**Files:** none.

- [ ] **Step 1: Check the start state**

Run: `cd /Users/tk/Code/lxr && git status --short -- Core tests tools cmake CMakeLists.txt CMakePresets.json && git branch --show-current && git branch --list emulator`

Expected: no `git status` line, then `timing-core`, and no `emulator` line. The tracker, `docs/lichomat/tracker.json`, may show as changed: the controller updates it during the run, and it does not touch the build. If a build file has changes or `emulator` exists, stop and report.

- [ ] **Step 2: Create the branch**

Run: `cd /Users/tk/Code/lxr && git switch -c emulator timing-core && git log --oneline -1`

Expected: `Switched to a new branch 'emulator'`, then the tip of `timing-core` (67ec609, or a later tracker commit).

- [ ] **Step 3: Run the base check**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected, among the output (the firmware has not changed since 9353a68):

```
100% tests passed, 0 tests failed out of 32
memory: ITCM 13232/16384 B (80.8%, free 3152)  DTCM 107388/131072 B (81.9%, rest is stack)  RAM 284400/393216 B (72.3%, free 108816)  image 300756/1015808 B (29.6%)
image: LXR_V2_LICHOMAT_0.1.img OK (300756 bytes of firmware, magic LXRV2IMG, splash "LICHOMAT" / "v0.1 unofficial")
== compiler warnings: 0
PASS E2.2
...
PASS E4.9
```

DTCM + RAM is 107388 + 284400 = 391788 B. This build in `build/release/` is the base build of Task R1a.2.

No commit.

---

### Task R1a.2: F8, the module source lists in `Core/sources.cmake`

§7 F8, §9.1. The lists leave `Core/CMakeLists.txt` with the same files in the same order. The paths stay relative to `Core/`, as they are today. Acceptance: the release `.bin` and `.map` are byte-identical with the base build. It is a pure refactor, so its test is that comparison.

**Files:**
- Create: `Core/sources.cmake` (LF)
- Modify: `Core/CMakeLists.txt:20-151` (LF; from the comment above `add_library(lxr_platform` to the `set_property` of `usb_midiTx.c`)
- Modify: `tools/fw_checks.sh:258`

- [ ] **Step 1: Save the base build**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build --preset release > build/base.log 2>&1 && mkdir -p build/base && cp build/release/LXR_V2_STM32F765.bin build/release/LXR_V2_STM32F765.map build/base/ && git status --short -- Core tests tools cmake CMakeLists.txt && echo BASE_SAVED`

Expected: `BASE_SAVED` and no `git status` line. Task R1a.1 built this commit, so the build has nothing to do.

- [ ] **Step 2: Create `Core/sources.cmake`**

Write this file (LF):

```cmake
# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only

# The source files of the LXR V2 application modules, one list per module, in
# link order. Core/CMakeLists.txt builds one OBJECT library from each list. The
# emulator (emu/CMakeLists.txt, stage 1b) includes this file too and compiles
# the same C files on the host, so a file added here reaches both builds. The
# paths are relative to this directory, Core/.

# Startup, clocks, CubeMX peripheral init, interrupt vectors, newlib stubs.
set(LXR_SOURCES_PLATFORM
    Startup/startup_stm32f765vgtx.s
    Src/main.c
    Src/stm32f7xx_it.c
    Src/stm32f7xx_hal_msp.c
    Src/system_stm32f7xx.c
    Src/syscalls.c
    Src/sysmem.c
    Src/delay.c
    Src/flash_if.c
)

# Hardware inputs/outputs: shift-register buttons/LEDs, pots & faders.
set(LXR_SOURCES_IO
    Src/IO/adcPots.c
    Src/IO/DIO.c
    Src/IO/uart.c
)

# User interface: LCD, encoder, buttons, LEDs, menu pages, screensaver.
set(LXR_SOURCES_UI
    Src/lcd.c
    Src/encoder.c
    Src/buttonHandler.c
    Src/ledHandler.c
    Src/graphic.c
    Src/frontPanelParser.c
    Src/Parameters.c
    Src/menu/Cc2Text.c
    Src/menu/copyClearTools.c
    Src/menu/menu.c
    Src/menu/screensaver.c
)

# Kit/pattern/project storage on the SD card.
set(LXR_SOURCES_PRESET
    Src/Preset/initKit.c
    Src/Preset/presetManager.c
)

# DSP building blocks, drum voices and master effects.
set(LXR_SOURCES_DSP
    Src/Soundengine/DSPAudio/1PoleLp.c
    Src/Soundengine/DSPAudio/automationNode.c
    Src/Soundengine/DSPAudio/BufferTools.c
    Src/Soundengine/DSPAudio/CymbalVoice.c
    Src/Soundengine/DSPAudio/Decay.c
    Src/Soundengine/DSPAudio/distortion.c
    Src/Soundengine/DSPAudio/dither.c
    Src/Soundengine/DSPAudio/DrumVoice.c
    Src/Soundengine/DSPAudio/HiHat.c
    Src/Soundengine/DSPAudio/lfo.c
    Src/Soundengine/DSPAudio/mixer.c
    Src/Soundengine/DSPAudio/modulationNode.c
    Src/Soundengine/DSPAudio/Oscillator.c
    Src/Soundengine/DSPAudio/random.c
    Src/Soundengine/DSPAudio/ResonantFilter.c
    Src/Soundengine/DSPAudio/Samples.c
    Src/Soundengine/DSPAudio/SlopeEg2.c
    Src/Soundengine/DSPAudio/snapEg.c
    Src/Soundengine/DSPAudio/Snare.c
    Src/Soundengine/DSPAudio/squareRootLut.c
    Src/Soundengine/DSPAudio/TiltEq.c
    Src/Soundengine/DSPAudio/transientGenerator.c
    Src/Soundengine/DSPAudio/transientTables.c
    Src/Soundengine/DSPAudio/wavetable.c
    Src/Soundengine/DSPAudio/FX/Compressor.c
    Src/Soundengine/DSPAudio/FX/Delay.c
    Src/Soundengine/DSPAudio/FX/Distortion.c
    Src/Soundengine/DSPAudio/FX/FxRamBuffer.c
    Src/Soundengine/DSPAudio/FX/Reverb.c
    Src/Soundengine/DSPAudio/FX/Ringmod.c
    Src/Soundengine/DSPAudio/FX/VerbTools/AllpassDelay.c
    Src/Soundengine/DSPAudio/FX/VerbTools/sinCos.c
    Src/Soundengine/DSPAudio/FX/VerbTools/VerbDelay.c
    Src/Soundengine/DSPAudio/FX/VerbTools/VerbModDelay.c
)

# Audio engine: codec/I2S DMA, sample block rendering, FX bus, clock/trigger I/O,
# CPU meter.
set(LXR_SOURCES_ENGINE
    Src/Soundengine/AudioCodecManager.c
    Src/Soundengine/cpuMeter.c
    Src/Soundengine/cs4344_cs5343.c
    Src/Soundengine/FIFO.c
    Src/Soundengine/FxBus.c
    Src/Soundengine/Soundengine.c
    Src/Soundengine/TriggerOut.c
)

# MIDI: UART driver, parsers, parameter mapping, voice control.
set(LXR_SOURCES_MIDI
    Src/Soundengine/MIDI/CortexParser.c
    Src/Soundengine/MIDI/MidiMessages.c
    Src/Soundengine/MIDI/MidiParser.c
    Src/Soundengine/MIDI/MidiVoiceControl.c
    Src/Soundengine/MIDI/ParameterArray.c
    Src/Soundengine/MIDI/Uart.c
)

# Step sequencer: the timing core (seq_core) and the sync input queues (seq_syncq),
# which the host tests in tests/ build as well, the platform layer (seq_platform:
# DWT time base, queue instances), the glue (sequencer) and the Euclidean generator.
set(LXR_SOURCES_SEQUENCER
    Src/Soundengine/Sequencer/EuklidGenerator.c
    Src/Soundengine/Sequencer/seq_core.c
    Src/Soundengine/Sequencer/seq_platform.c
    Src/Soundengine/Sequencer/seq_syncq.c
    Src/Soundengine/Sequencer/sequencer.c
)

# USB-MIDI message queue on top of the USB device stack, and the USB MIDI output
# packets and transmit ring (usb_midiTx), which the host tests in tests/ build as well.
set(LXR_SOURCES_USB_APP
    Src/Soundengine/USB/usb_manager.c
    Src/Soundengine/USB/usb_midiTx.c
)
```

- [ ] **Step 3: Build the modules from the lists in `Core/CMakeLists.txt`**

Replace lines 20–151 (from `# Startup, clocks, CubeMX peripheral init, interrupt vectors, newlib stubs.` to `set_property(SOURCE Src/Soundengine/USB/usb_midiTx.c APPEND PROPERTY COMPILE_OPTIONS -Wextra)`, both included) with:

```cmake
# The module source lists, LXR_SOURCES_PLATFORM ... LXR_SOURCES_USB_APP. The
# emulator (emu/, stage 1b) compiles the same lists on the host.
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

add_library(lxr_platform OBJECT ${LXR_SOURCES_PLATFORM})
# Shown on the boot splash screen; LICHOMAT_NAME and project(VERSION) come from the
# top-level CMakeLists.txt.
set_source_files_properties(Src/main.c PROPERTIES
    COMPILE_DEFINITIONS "FIRMWARE_NAME=\"${LICHOMAT_NAME}\";FIRMWARE_VERSION=\"${PROJECT_VERSION}\""
)

add_library(lxr_io OBJECT ${LXR_SOURCES_IO})
add_library(lxr_ui OBJECT ${LXR_SOURCES_UI})
add_library(lxr_preset OBJECT ${LXR_SOURCES_PRESET})
add_library(lxr_dsp OBJECT ${LXR_SOURCES_DSP})
add_library(lxr_engine OBJECT ${LXR_SOURCES_ENGINE})
add_library(lxr_midi OBJECT ${LXR_SOURCES_MIDI})

add_library(lxr_sequencer OBJECT ${LXR_SOURCES_SEQUENCER})
# The host tests build seq_core.c and seq_syncq.c with -Wall -Wextra -Werror; the
# firmware warns about the same for them and for seq_platform.c.
set_property(SOURCE
    Src/Soundengine/Sequencer/seq_core.c
    Src/Soundengine/Sequencer/seq_platform.c
    Src/Soundengine/Sequencer/seq_syncq.c
    APPEND PROPERTY COMPILE_OPTIONS -Wextra)

add_library(lxr_usb_app OBJECT ${LXR_SOURCES_USB_APP})
# The host tests build usb_midiTx.c with -Wall -Wextra -Werror; warn about the same here.
set_property(SOURCE Src/Soundengine/USB/usb_midiTx.c APPEND PROPERTY COMPILE_OPTIONS -Wextra)
```

Lines 1–19 (the header and `lxr_app_common`) and everything from `set(LXR_APP_MODULES` on stay as they are.

- [ ] **Step 4: Let `fw_checks.sh`'s E2.6 check read the new file**

In `tools/fw_checks.sh:258`, replace `Core/Src/Soundengine Core/Inc/Soundengine Core/CMakeLists.txt` with `Core/Src/Soundengine Core/Inc/Soundengine Core/CMakeLists.txt Core/sources.cmake` (the leading tabs stay).

- [ ] **Step 5: Build and compare with the base build**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build --preset release > build/f8.log 2>&1 && cmp build/base/LXR_V2_STM32F765.bin build/release/LXR_V2_STM32F765.bin && cmp build/base/LXR_V2_STM32F765.map build/release/LXR_V2_STM32F765.map && echo BIN_AND_MAP_IDENTICAL`

Expected: `BIN_AND_MAP_IDENTICAL`. The build re-runs CMake by itself. If `cmp` reports a difference, run `diff build/base/LXR_V2_STM32F765.map build/release/LXR_V2_STM32F765.map | head -20`: a file is missing or out of order in a list.

- [ ] **Step 6: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `out of 32` with 100 % passed, the same memory line as in Task R1a.1, the image check OK, `== compiler warnings: 0`, and nine `PASS` lines.

- [ ] **Step 7: Commit**

```bash
cd /Users/tk/Code/lxr && git add Core/sources.cmake Core/CMakeLists.txt tools/fw_checks.sh && git commit -F - <<'EOF'
Build: the module source lists move to Core/sources.cmake (E5.2 F8)

Core/CMakeLists.txt builds its nine OBJECT libraries from the lists
LXR_SOURCES_PLATFORM ... LXR_SOURCES_USB_APP in the new Core/sources.cmake,
with the same files in the same order and paths relative to Core/. The
emulator (stage 1b spec §9.1) includes the same file, so a firmware file
added there reaches both builds. tools/fw_checks.sh's E2.6 tree check
reads the new file too.

The release .bin and .map are byte-identical with the base build.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

Expected: `[emulator …] Build: the module source lists move to Core/sources.cmake (E5.2 F8)`, `3 files changed`.

---

### Task R1a.3: F4, `appMain_boot()` and `appMain_loopOnce()` in `appMain.c`

§7 F4, §4.10. `main()` keeps the CubeMX init and `MX_USB_DEVICE_Init()`, then calls `appMain_boot();` and runs `while (1)` with `appMain_loopOnce();` in it. A script moves the code, so that the move is exact. The only edits that are not moves are §7's (a)–(d), the new header, main.c's include and two calls, and appMain.c's include block, plus the two function frames and the comments that label the new parts.

Acceptance:
- `git diff --color-moved` shows moves plus these edits only;
- the image check finds the splash;
- the F4TEST check: a build with `-DLICHOMAT_NAME=F4TEST` shows `F4TEST` (it fails before edit (d), which proves that the check sees the defines);
- a scratch copy with `UPDATE_BOOTLOADER` set to 1 compiles and prints its `#warning`;
- ITCM is unchanged.

U7 is the device check, in R6.

**Files:**
- Create: `Core/Inc/appMain.h` (CRLF)
- Create: `Core/Src/appMain.c` (CRLF, made by the script from main.c's lines 1–8, 10–12, 15–33, 35, 37–50, 56–63, 112–188, 237, 319–921 and 924–995)
- Modify: `Core/Src/main.c` (CRLF): lines 15–33, 56–63, 112–188, 237, 319–921 and 924–995 leave; `#include "appMain.h"` after line 50; `appMain_boot();` after line 318; `appMain_loopOnce();` after line 923
- Modify: `Core/sources.cmake` (`LXR_SOURCES_PLATFORM`: `Src/appMain.c` after `Src/main.c`)
- Modify: `Core/CMakeLists.txt` (edit (d): `set_source_files_properties(Src/main.c PROPERTIES` names `Src/appMain.c`; at `Core/CMakeLists.txt:34` before Task R1a.2)

- [ ] **Step 1: Note the base memory line**

The last `tools/check.sh` (Task R1a.2 Step 6) printed it: ITCM 13232 B, DTCM 107388 B, RAM 284400 B. ITCM must stay at 13232 B.

- [ ] **Step 2: Write `Core/Inc/appMain.h`**

Write this file, then convert it to CRLF and count:

```c
/*
 * appMain.h
 *
 * The application's boot and one pass of its main loop, moved out of main()
 * (main.c), which keeps the CubeMX initialisation and calls appMain_boot()
 * once, then appMain_loopOnce() forever. The emulator of stage 1b runs the
 * same two functions without main.c (F4).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef APPMAIN_H_
#define APPMAIN_H_

/** the boot: main.c's code from dio_init() to the EXTI enables; it returns
 *  with the EXTI lines enabled, before the first pass */
void appMain_boot(void);

/** one pass of the main loop: the card check, the pots, the sound engine, the
 *  buttons, the encoder, the LEDs, the screensaver, the panel and the save timer */
void appMain_loopOnce(void);

#endif /* APPMAIN_H_ */
```

Run: `cd /Users/tk/Code/lxr && perl -pi -e 's/\r?\n/\r\n/' Core/Inc/appMain.h && perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "CRLF $c LF $l\n" }' Core/Inc/appMain.h`

Expected: `CRLF 27 LF 27`.

- [ ] **Step 3: Move the code with the split script**

The script checks each boundary line, then writes both files with CRLF. (b) becomes the two assignments; (a) becomes the brace before `#else`, the `#endif` at the end of `appMain_boot()` and the `#if !UPDATE_BOOTLOADER` at the start of `appMain_loopOnce()`; (c) becomes the three `extern` lines.

Run:

```bash
cd /Users/tk/Code/lxr && python3 - <<'EOF'
# F4 (stage 1b spec §7): main.c's boot and loop body move to appMain.c.
# Run from the repository root. The line numbers are main.c's at timing-core
# 67ec609; the tasks before this one do not change main.c.
from pathlib import Path

MAIN = Path("Core/Src/main.c")
APP = Path("Core/Src/appMain.c")
text = MAIN.read_bytes().decode("ascii")
assert text.endswith("\r\n") and "\n" not in text.replace("\r\n", ""), "main.c is not all CRLF"
lines = text[:-2].split("\r\n")
assert len(lines) == 1714, len(lines)


def L(n):
    """line n, counted from 1"""
    return lines[n - 1]


def R(a, b):
    """lines a to b, both included"""
    return lines[a - 1:b]


anchors = {
    10: "/* Includes ------------------------------------------------------------------*/",
    11: '#include "main.h"',
    12: '#include "fatfs.h"',
    15: "//used for bootloader update.",
    17: "#define UPDATE_BOOTLOADER 0",
    33: "#endif",
    35: "/* Private includes ----------------------------------------------------------*/",
    36: '#include "usb_device.h"',
    37: '#include "lcd.h"',
    50: '#include "Soundengine/MIDI/Uart.h"',
    56: "// The CMake build passes FIRMWARE_NAME (LICHOMAT_NAME) and FIRMWARE_VERSION (project(VERSION));",
    63: "#endif",
    112: "void sd_formatRoutine()",
    188: "}",
    237: "volatile static int8_t checkMainLoc = 0;",
    318: "  MX_USB_DEVICE_Init();",
    319: "  dio_init(&hspi1);",
    371: "FRESULT res = f_mount((FATFS*)&SDFatFS, (TCHAR const*)SDPath,1);",
    540: "if(sd_card_ok)",
    543: "#if UPDATE_BOOTLOADER",
    806: "\t\t\twhile(1);",
    807: "#else",
    897: "uint8_t lastSdState = BSP_SD_IsDetected();",
    921: "  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);",
    922: "  while (1)",
    923: "  {",
    924: "  checkMainLoc = -14;",
    995: "#endif",
    996: "    /* USER CODE BEGIN 3 */",
}
for n, s in anchors.items():
    assert L(n) == s, "main.c:%d is %r, expected %r" % (n, L(n), s)

# the boot, with edit (b): main()'s locals res and lastSdState become the file's
# statics, so their declarations become assignments; and edit (a): the bootloader
# build closes if(sd_card_ok) before the #else, where the loop's brace did it
boot = R(319, 921)
boot[371 - 319] = "res = f_mount((FATFS*)&SDFatFS, (TCHAR const*)SDPath,1);"
boot[897 - 319] = "lastSdState = BSP_SD_IsDetected();"
boot.insert(807 - 319, "\t}\t//LICHOMAT: closes if(sd_card_ok) in the bootloader build (F4)")

app = (
    R(1, 8)
    + [
        "",
        "/* The application's boot and one pass of its main loop, moved out of main.c,",
        " * which keeps the CubeMX initialisation and calls them (stage 1b F4). */",
        "",
    ]
    + R(10, 12) + ['#include "appMain.h"', ""]
    + R(15, 33) + [""]
    + R(35, 35) + R(37, 50) + [""]
    + R(56, 63)
    + [
        "",
        "/* CubeMX handles that main.c defines and main.h does not declare */",
        "extern ADC_HandleTypeDef hadc1;",
        "extern SPI_HandleTypeDef hspi1;",
        "extern TIM_HandleTypeDef htim14;",
        "",
        "/* locals of main() before F4: the main loop reads them */",
        "static FRESULT res;",
        "static uint8_t lastSdState;",
        "",
    ]
    + R(112, 188) + [""]
    + R(237, 237)
    + [
        "",
        "/** the boot: main.c's code from dio_init() to the EXTI enables */",
        "void appMain_boot(void)",
        "{",
    ]
    + boot
    + [
        "#endif",
        "}",
        "",
        "/** one pass of the main loop */",
        "void appMain_loopOnce(void)",
        "{",
        "#if !UPDATE_BOOTLOADER",
    ]
    + R(924, 995)
    + ["}"]
)

main = (
    R(1, 14)
    + R(34, 50) + ['#include "appMain.h"']
    + R(51, 55)
    + R(64, 111)
    + R(189, 236)
    + R(238, 318) + ["  appMain_boot();"]
    + R(922, 923) + ["    appMain_loopOnce();"]
    + R(996, 1714)
)

APP.write_bytes(("\r\n".join(app) + "\r\n").encode("ascii"))
MAIN.write_bytes(("\r\n".join(main) + "\r\n").encode("ascii"))
print("appMain.c: %d lines, main.c: %d lines" % (len(app), len(main)))
EOF
```

Expected: `appMain.c: 838 lines, main.c: 937 lines`. An `AssertionError` names the line that differs: stop and report it, do not adjust the numbers.

The result: `main()` now ends with

```c
  /* USER CODE BEGIN 2 */
  MX_USB_DEVICE_Init();
  appMain_boot();
  while (1)
  {
    appMain_loopOnce();
    /* USER CODE BEGIN 3 */
  }


  /* USER CODE END 3 */
}
```

and `appMain.c` is: main.c's licence header, the include block (`main.h`, `fatfs.h`, `appMain.h`), the `UPDATE_BOOTLOADER` block, main.c's private includes without `usb_device.h`, the splash defines, the three `extern` handles, `static FRESULT res;` and `static uint8_t lastSdState;`, `sd_formatRoutine()`, `checkMainLoc`, `appMain_boot()` (lines 153–760) and `appMain_loopOnce()` (lines 763–838).

- [ ] **Step 4: Add `appMain.c` to `LXR_SOURCES_PLATFORM`**

In `Core/sources.cmake`, replace the line `    Src/main.c` with the two lines:

```cmake
    Src/main.c
    Src/appMain.c
```

- [ ] **Step 5: Run the F4TEST check and see it fail**

The splash code is now in `appMain.c`, and `Core/CMakeLists.txt` still passes `FIRMWARE_NAME` to `main.c` only. A normal build hides that: the fallback 'LICHOMAT' equals the configured name. A build named F4TEST shows it.

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake -S . -B build/f4test -G Ninja -DCMAKE_BUILD_TYPE=Release -DARM_TOOLCHAIN_DIR="$ARM_TOOLCHAIN_DIR" -DLICHOMAT_NAME=F4TEST > build/f4test.log 2>&1 && cmake --build build/f4test >> build/f4test.log 2>&1 && python3 tools/check_image.py build/f4test`

Expected: the build succeeds, then `image check FAILED: splash line 'F4TEST' not found in LXR_V2_STM32F765.bin` (exit status 1).

- [ ] **Step 6: Pass the splash defines to `appMain.c` (edit (d))**

In `Core/CMakeLists.txt`, replace `set_source_files_properties(Src/main.c PROPERTIES` with `set_source_files_properties(Src/appMain.c PROPERTIES`. The comment above it and the `COMPILE_DEFINITIONS` line stay.

- [ ] **Step 7: Run the F4TEST check and see it pass**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build build/f4test >> build/f4test.log 2>&1 && python3 tools/check_image.py build/f4test`

Expected: `image: LXR_V2_F4TEST_0.1.img OK (… bytes of firmware, magic LXRV2IMG, splash "F4TEST" / "v0.1 unofficial")`.

- [ ] **Step 8: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `out of 32` with 100 % passed; `image: LXR_V2_LICHOMAT_0.1.img OK (… splash "LICHOMAT" / "v0.1 unofficial")`; `== compiler warnings: 0`; nine `PASS` lines. The memory line shows `ITCM 13232/16384 B` (unchanged). RAM grows by a few bytes (`res` and `lastSdState` now sit in `.bss`), and DTCM + RAM must stay at most 391824 B.

- [ ] **Step 9: Compile a copy with `UPDATE_BOOTLOADER` set to 1**

The script takes appMain.c's compile command from the release build and compiles a changed copy in `build/f4-bootloader/`.

Run:

```bash
cd /Users/tk/Code/lxr && python3 - <<'EOF'
import json, shlex, subprocess
from pathlib import Path
src = Path("Core/Src/appMain.c").read_bytes()
assert src.count(b"#define UPDATE_BOOTLOADER 0") == 1
copy = Path("build/f4-bootloader/appMain.c")
copy.parent.mkdir(parents=True, exist_ok=True)
copy.write_bytes(src.replace(b"#define UPDATE_BOOTLOADER 0", b"#define UPDATE_BOOTLOADER 1"))
entry = next(e for e in json.load(open("build/release/compile_commands.json"))
             if e["file"].endswith("/Core/Src/appMain.c"))
args = shlex.split(entry["command"])
args[args.index("-o") + 1] = "/dev/null"
assert args[-1].endswith("/Core/Src/appMain.c")
args[-1] = str(copy.resolve())
r = subprocess.run(args, cwd=entry["directory"], capture_output=True, text=True)
for line in r.stderr.splitlines():
    if "warning:" in line or "error:" in line:
        print(line.split("appMain.c:", 1)[-1][:110])
print("exit", r.returncode)
EOF
```

Expected (the order may differ):

```
23:2: warning: #warning "UPDATE_BOOTLOADER is enabled: this firmware removes the bootloader's write prot
382:36: warning: unused variable 'JumpAddress' [-Wunused-variable]
381:37: warning: unused variable 'Jump_To_Application' [-Wunused-variable]
70:16: warning: 'lastSdState' defined but not used [-Wunused-variable]
exit 0
```

The first three come from the base: main.c with `UPDATE_BOOTLOADER 1` gives them too. The fourth is new: in that build, `lastSdState` is used only in code it leaves out. No `error:` line may appear.

- [ ] **Step 10: Stage the change and list what is not a move**

Run:

```bash
cd /Users/tk/Code/lxr && git add Core/Src/main.c Core/Src/appMain.c Core/Inc/appMain.h Core/sources.cmake Core/CMakeLists.txt && git -c color.diff.old=red -c color.diff.new=green -c color.diff.oldMoved=magenta -c color.diff.newMoved=cyan diff --cached --color=always --color-moved=blocks -- Core/Src/main.c Core/Src/appMain.c | python3 -c '
import re, sys
n = 0
for raw in sys.stdin.buffer.read().decode("ascii", "replace").split("\n"):
    if raw.startswith("\x1b[31m-") or raw.startswith("\x1b[32m+"):
        n += 1
        print(re.sub(r"\x1b\[[0-9;]*m", "", raw).rstrip("\r"))
print("--", n, "lines not moved")
'
```

Expected with git 2.39 (lines that git marks as moved are not printed):

```
+/*
+ * Copyright (C) 2012-2026 Erica Synths
+ * SPDX-License-Identifier: GPL-3.0-only
+ *
+ * This file is part of the LXR firmware. It is licensed under the GNU General
+ * Public License version 3, with the additional permission and terms in
+ * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
+ */
+
+/* The application's boot and one pass of its main loop, moved out of main.c,
+ * which keeps the CubeMX initialisation and calls them (stage 1b F4). */
+
+/* Includes ------------------------------------------------------------------*/
+#include "main.h"
+#include "fatfs.h"
+#include "appMain.h"
+
+
+/* Private includes ----------------------------------------------------------*/
+#include "lcd.h"
+#include "menu/menu.h"
+#include "IO/DIO.h"
+#include "menu/screensaver.h"
+#include "menu/copyClearTools.h"
+#include "ledHandler.h"
+#include "buttonHandler.h"
+#include "encoder.h"
+#include "IO/adcPots.h"
+#include "Preset/PresetManager.h"
+#include "Soundengine/Soundengine.h"
+#include "Soundengine/DSPAudio/random.h"
+#include "Soundengine/USB/usb_manager.h"
+#include "Soundengine/MIDI/Uart.h"
+
+
+/* CubeMX handles that main.c defines and main.h does not declare */
+extern ADC_HandleTypeDef hadc1;
+extern SPI_HandleTypeDef hspi1;
+extern TIM_HandleTypeDef htim14;
+
+/* locals of main() before F4: the main loop reads them */
+static FRESULT res;
+static uint8_t lastSdState;
+
+
+
+/** the boot: main.c's code from dio_init() to the EXTI enables */
+void appMain_boot(void)
+{
+res = f_mount((FATFS*)&SDFatFS, (TCHAR const*)SDPath,1);
+	}	//LICHOMAT: closes if(sd_card_ok) in the bootloader build (F4)
+lastSdState = BSP_SD_IsDetected();
+#endif
+}
+
+/** one pass of the main loop */
+void appMain_loopOnce(void)
+{
+#if !UPDATE_BOOTLOADER
+}
+#include "appMain.h"
-FRESULT res = f_mount((FATFS*)&SDFatFS, (TCHAR const*)SDPath,1);
-uint8_t lastSdState = BSP_SD_IsDetected();
+  appMain_boot();
+    appMain_loopOnce();
-- 65 lines not moved
```

The licence header and the include block are copies: main.c keeps them, so git shows them as added. Another git version may print a blank line more or less. Any other line is a defect: find its cause before the commit.

- [ ] **Step 11: Check the line endings**

Run: `cd /Users/tk/Code/lxr && for f in Core/Src/main.c Core/Src/appMain.c Core/Inc/appMain.h; do perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "$ARGV CRLF $c LF $l\n" }' $f; done`

Expected: `Core/Src/main.c CRLF 937 LF 937`, `Core/Src/appMain.c CRLF 838 LF 838`, `Core/Inc/appMain.h CRLF 27 LF 27`.

- [ ] **Step 12: Remove the scratch builds**

Run: `cd /Users/tk/Code/lxr && rm -rf build/f4test build/f4-bootloader build/f4test.log`

- [ ] **Step 13: Commit**

```bash
cd /Users/tk/Code/lxr && git add Core/Src/main.c Core/Src/appMain.c Core/Inc/appMain.h Core/sources.cmake Core/CMakeLists.txt && git commit -F - <<'EOF'
Boot and main loop move to appMain.c (E5.2 F4)

main() keeps the CubeMX initialisation and MX_USB_DEVICE_Init(), then
calls appMain_boot() once and appMain_loopOnce() in its loop. appMain.c
holds the boot from dio_init() to the EXTI enables, the loop body,
sd_formatRoutine(), checkMainLoc, the splash defines with their fallbacks
and the UPDATE_BOOTLOADER code, moved as they were. The emulator (stage 1b
spec §7 F4, §4.10) runs the same two functions without main.c.

The only edits that are not moves: the UPDATE_BOOTLOADER conditional
closes at the end of appMain_boot() and opens again around the loop body;
res and lastSdState become statics of appMain.c; appMain.c declares
hadc1, hspi1 and htim14 extern; Core/CMakeLists.txt passes FIRMWARE_NAME
and FIRMWARE_VERSION to appMain.c; the new appMain.h; main.c's include
and two calls; appMain.c's include block, main.c's without usb_device.h.

Checked: git diff --color-moved=blocks shows only these edits as not
moved. A build with LICHOMAT_NAME=F4TEST shows F4TEST on the splash, and
failed before the defines moved to appMain.c. A copy with
UPDATE_BOOTLOADER 1 compiles with its #warning. ITCM is unchanged at
13232 B. Device check: U7.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

Expected: `5 files changed`, with `create mode 100644 Core/Inc/appMain.h` and `create mode 100644 Core/Src/appMain.c`.

---

### Task R1a.4: F1, modulation nodes at `PAR_NONE` keep their original value

§7 F1. Every node starts at destination 0, `PAR_NONE`, whose parameter has no pointer. A CC 1 makes `MidiParser.c` call `modNode_originalValueChanged(0)`, and `modNode_setOriginalValueChanged()` then reads through the NULL pointer (`modulationNode.c:49`, `:58` or `:62`, by type). On the device that reads ITCM at address 0; on a host it crashes. `modNode_setDestination()` and `modNode_updateValue()` already test `p->ptr`. F1 adds the same test here.

Later acceptance: M15 in part 2 (CC 1 with nodes at destination 0 in the emulator) and U4 on the device. This task adds a host test in `tests/`.

**Files:**
- Modify: `Core/Src/Soundengine/DSPAudio/modulationNode.c:43-45` (a test after line 44)
- Create: `tests/test_modnode.c`
- Modify: `tests/CMakeLists.txt` (a block after the last line, 251)

- [ ] **Step 1: Write the failing test**

Create `tests/test_modnode.c`:

```c
/*
 * test_modnode.c
 *
 * Host test of the modulation nodes (Core/Src/Soundengine/DSPAudio/
 * modulationNode.c, stage 1b F1): the six velocity modulators and the LFO
 * targets of the six voices. Every node starts at destination 0, PAR_NONE, the
 * mod wheel's slot, and that parameter has no pointer. A CC 1 (the mod wheel)
 * makes the CC handler call modNode_originalValueChanged(0) (MidiParser.c), and
 * fw 1.7 then read the original value of every node at destination 0 through
 * that NULL pointer: from address 0, which is ITCM on the device, and a crash
 * on a host. Such a node now keeps its original value.
 *
 * modulationNode.c is compiled as it is, against the stand-in headers in
 * tests/stubs/ and the stand-ins below. parameterArray is the test's own and
 * starts zeroed, as the firmware's does: entry 0 stays { NULL, TYPE_UINT8 }
 * there too, because parameterArray_init() never sets it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include "menu/menu.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/ParameterArray.h"

#define NODES 12

//------------------------------------------------------------------------------
// Firmware state that modulationNode.c reads and writes
//------------------------------------------------------------------------------
uint8_t activeParameters[NUM_PARAMS];
Parameter parameterArray[END_OF_SOUND_PARAMETERS];
DrumVoice voiceArray[NUM_DRUM_VOICES];
SnareVoice snareVoice;
CymbalVoice cymbalVoice;
HiHatVoice hatVoice;

//------------------------------------------------------------------------------
// Stand-ins for the functions it calls
//------------------------------------------------------------------------------
void paramArray_setParameter(uint16_t idx, ptrValue newValue)
{
	(void)idx;
	(void)newValue;
}

void midiParser_ccHandler(MidiMsg msg, uint8_t updateOriginalValue, uint8_t isFloatModulator, float v)
{
	(void)msg; (void)updateOriginalValue; (void)isFloatModulator; (void)v;
}

//------------------------------------------------------------------------------
// The nodes
//------------------------------------------------------------------------------
/** node i: the six velocity modulators, then the LFO targets of the six voices */
static ModulationNode *node(int i)
{
	ModulationNode *const lfoTargets[6] = {
		&voiceArray[0].lfo.modTarget, &voiceArray[1].lfo.modTarget, &voiceArray[2].lfo.modTarget,
		&snareVoice.lfo.modTarget, &cymbalVoice.lfo.modTarget, &hatVoice.lfo.modTarget,
	};
	return i < 6 ? &velocityModulators[i] : lfoTargets[i - 6];
}

/** every node at destination 0, as modNode_init() leaves it at boot, with the
 *  original value 100 + i */
static void resetNodes(void)
{
	memset(parameterArray, 0, sizeof parameterArray);
	for (int i = 0; i < NODES; i++) {
		modNode_init(node(i));
		node(i)->originalValue.itg = 100u + (uint32_t)i;
	}
}

TEST(modnode_cc1_keeps_the_nodes_at_destination_0)
{
	resetNodes();
	ASSERT_TRUE(parameterArray[PAR_NONE].ptr == NULL);
	modNode_originalValueChanged(PAR_NONE);	/* CC 1: paramNr = data1 - 1 = 0 */
	for (int i = 0; i < NODES; i++) {
		ASSERT_EQ(node(i)->destination, PAR_NONE);
		ASSERT_EQ(node(i)->originalValue.itg, 100u + (uint32_t)i);
	}
}

TEST(modnode_a_node_with_a_parameter_takes_its_new_value)
{
	static uint8_t value = 7;
	resetNodes();
	parameterArray[5].ptr = &value;
	parameterArray[5].type = TYPE_UINT8;
	modNode_setDestination(node(3), 5);
	ASSERT_EQ(node(3)->originalValue.itg, 7u);
	value = 42;
	modNode_originalValueChanged(5);
	ASSERT_EQ(node(3)->originalValue.itg, 42u);
	ASSERT_EQ(node(4)->originalValue.itg, 104u);	/* at destination 0: unchanged */
}

TEST_MAIN()
```

- [ ] **Step 2: Register the test**

Append to `tests/CMakeLists.txt`, after its last line:

```cmake

# The modulation nodes (stage 1b F1): test_modnode compiles the firmware's
# modulationNode.c with the stand-in headers in stubs/. A node at destination 0,
# PAR_NONE, has no parameter pointer, and fw 1.7 read its original value through
# it at every CC 1.
seqc_add_test(test_modnode   IDS modnode EXTRA ${LXR_ROOT}/Core/Src/Soundengine/DSPAudio/modulationNode.c)  # stage 1b F1  nodes at PAR_NONE keep their value
if(TARGET test_modnode)
    target_include_directories(test_modnode BEFORE PRIVATE stubs)
    target_include_directories(test_modnode SYSTEM PRIVATE ${LXR_ROOT}/Core/Inc)
    set_source_files_properties(${LXR_ROOT}/Core/Src/Soundengine/DSPAudio/modulationNode.c PROPERTIES COMPILE_OPTIONS -w)
endif()
```

- [ ] **Step 3: Run the test and see it fail**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake -S tests -B build/host-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build build/host-tests --target test_modnode >/dev/null && build/host-tests/test_modnode modnode; echo "exit $?"`

Expected: `…/Core/Src/Soundengine/DSPAudio/modulationNode.c:49:30: runtime error: load of null pointer of type 'uint8_t' (aka 'unsigned char')`, then `exit 134`. Without UBSan the test dies of a segmentation fault instead.

- [ ] **Step 4: Add the test for the pointer**

The code after the change:

```c
	if(vm->destination == idx) { //parameter is active in this modulator
		const Parameter *p=&parameterArray[idx];

		//LICHOMAT: PAR_NONE, where every node starts, has no pointer: keep the original value (F1)
		if(!p->ptr)
			return;

			//update orig value
			switch(p->type)
```

Run:

```bash
cd /Users/tk/Code/lxr && python3 - <<'EOF'
from pathlib import Path
p = Path("Core/Src/Soundengine/DSPAudio/modulationNode.c")
s = p.read_bytes()
old = (b"\t\tconst Parameter *p=&parameterArray[idx];\r\n"
       b"\r\n"
       b"\t\t\t//update orig value\r\n")
new = (b"\t\tconst Parameter *p=&parameterArray[idx];\r\n"
       b"\r\n"
       b"\t\t//LICHOMAT: PAR_NONE, where every node starts, has no pointer: keep the original value (F1)\r\n"
       b"\t\tif(!p->ptr)\r\n"
       b"\t\t\treturn;\r\n"
       b"\r\n"
       b"\t\t\t//update orig value\r\n")
assert s.count(old) == 1
p.write_bytes(s.replace(old, new))
EOF
perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "CRLF $c LF $l\n" }' Core/Src/Soundengine/DSPAudio/modulationNode.c
```

Expected: `CRLF 243 LF 243`.

- [ ] **Step 5: Run the test and see it pass**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build build/host-tests --target test_modnode >/dev/null && build/host-tests/test_modnode modnode`

Expected:

```
PASS modnode_cc1_keeps_the_nodes_at_destination_0
PASS modnode_a_node_with_a_parameter_takes_its_new_value
2 of 2 tests passed
```

- [ ] **Step 6: Compile the test as CI's GCC would (strict C11)**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && arm-none-eabi-gcc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -Itests/stubs -Itests -isystem Core/Inc -c -o /dev/null tests/test_modnode.c && echo GCC_OK`

Expected: `GCC_OK`.

- [ ] **Step 7: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `100% tests passed, 0 tests failed out of 33`, the image check OK, `== compiler warnings: 0`, nine `PASS` lines. ITCM stays at 13232 B: the changed function is not in ITCM. The image changes (the new `if`), as §7 expects for F1.

- [ ] **Step 8: Commit**

```bash
cd /Users/tk/Code/lxr && git add Core/Src/Soundengine/DSPAudio/modulationNode.c tests/test_modnode.c tests/CMakeLists.txt && git commit -F - <<'EOF'
Modulation nodes at PAR_NONE keep their original value (E5.2 F1)

Every modulation node starts at destination 0, PAR_NONE, whose parameter
has no pointer. A CC 1 (the mod wheel) makes the CC handler call
modNode_originalValueChanged(0), and modNode_setOriginalValueChanged()
read each such node's original value through that NULL pointer: from
ITCM at address 0 on the device, where the value is never used, and a
crash on a host. It now returns first, as modNode_setDestination() and
modNode_updateValue() already do. No effect on the device's sound.

Test: tests/test_modnode.c (ctest id modnode) compiles modulationNode.c on
the host; it stopped on UBSan's null-pointer load before the fix. Later:
M15 in the emulator (stage 1b spec §12.1); device check U4.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task R1a.5: F2, the morph target after a boot without a card

§7 F2, §4.10. The boot sets `morphParameters = mainParameters[0]` only on the card path (`main.c:520` at the HEAD, `appMain.c:356` after Task R1a.3). Without a card, the global stays NULL (`menu.c:707`), and a turn of the morph knob interpolates towards bytes from address 0 (`presetManager.c:2166`). The no-card branch (`main.c:857–862`, now `appMain.c:694–699`) gets the same assignment, first, before any preset code runs.

This change has no host test in this plan: the boot runs only in the emulator. Part 2's M15 boots without a card and turns the morph (§12.1), and U5 checks it on the device (§12.4).

**Files:**
- Modify: `Core/Src/appMain.c:694-699` (the no-card `else` of the boot)

- [ ] **Step 1: See the gap**

Run: `cd /Users/tk/Code/lxr && grep -n 'morphParameters' Core/Src/appMain.c`

Expected: one line, `356:		morphParameters = mainParameters[0];`. It is inside `if(sd_card_ok)`, which starts at line 329.

- [ ] **Step 2: Set the morph target without a card too**

The branch after the change:

```c
else
{
  //LICHOMAT: without a card the morph target is kit 0 too; it was NULL, and a morph read from address 0 (F2)
  morphParameters = mainParameters[0];
  copyClear_clearCurrentPattern();
  preset_init(0);
  preset_loadDefaultGlobals();
}
```

Run:

```bash
cd /Users/tk/Code/lxr && python3 - <<'EOF'
from pathlib import Path
p = Path("Core/Src/appMain.c")
s = p.read_bytes()
old = (b"else\r\n"
       b"{\r\n"
       b"  copyClear_clearCurrentPattern();\r\n"
       b"  preset_init(0);\r\n")
new = (b"else\r\n"
       b"{\r\n"
       b"  //LICHOMAT: without a card the morph target is kit 0 too; it was NULL, and a morph read from address 0 (F2)\r\n"
       b"  morphParameters = mainParameters[0];\r\n"
       b"  copyClear_clearCurrentPattern();\r\n"
       b"  preset_init(0);\r\n")
assert s.count(old) == 1
p.write_bytes(s.replace(old, new))
EOF
perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "CRLF $c LF $l\n" }' Core/Src/appMain.c && grep -n 'morphParameters' Core/Src/appMain.c
```

Expected: `CRLF 840 LF 840`, then two lines, `356:		morphParameters = mainParameters[0];` and `697:  morphParameters = mainParameters[0];`.

- [ ] **Step 3: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `out of 33` with 100 % passed, the image check OK, `== compiler warnings: 0`, nine `PASS` lines, ITCM 13232 B.

- [ ] **Step 4: Commit**

```bash
cd /Users/tk/Code/lxr && git add Core/Src/appMain.c && git commit -F - <<'EOF'
The morph target is kit 0 after a boot without a card too (E5.2 F2)

The boot set morphParameters = mainParameters[0] only when the card
mounted. Without a card it stayed NULL, and turning the morph knob
interpolated towards bytes read from address 0. The no-card branch of
appMain_boot() now sets it first. The device's behaviour changes only
for a morph after a boot without a card.

Test: M15 boots the emulator without a card and turns the morph (stage 1b
spec §12.1); device check U5.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task R1a.6: F3, the clear page's automation text stays within its row

§7 F3. `editDisplayBuffer` is `char [2][17]` (`menu.c:684`). At `menu.c:1079` and `:1083`, `strcpy(&editDisplayBuffer[0][7], "autom. 1]?")` writes 11 bytes into the 10 left in row 0: the `'?'` lands in byte 16, which the display never shows (it sends 16 columns, `menu.c:800–816`), and the NUL lands in `[1][0]`. The clear menu is drawn after a full clear (`copyClear_armClearMenu()` calls `menu_repaintAll()`, which fills both rows with spaces, `menu.c:1021`), and the display shows a NUL as a space (`menu.c:805–808`). So the text without the `'?'` gives the same screen. macOS's fortify check aborts on the old `strcpy` in the emulator.

The failing check here is a one-off compile with clang's fortify warning. Later acceptance: M15 compares the LCD snapshots of both clear-automation pages with the base build built with `-D_FORTIFY_SOURCE=0`; U6 on the device.

**Files:**
- Modify: `Core/Src/menu/menu.c:1078-1083`

- [ ] **Step 1: Run the fortify check and see it fail**

Run: `cd /Users/tk/Code/lxr && cc -std=gnu11 -fsyntax-only -Wno-everything -Wfortify-source -Itests/stubs/presets -Itests/stubs -isystem Core/Inc -isystem Middlewares/Third_Party/FatFs/src -isystem FATFS/Target Core/Src/menu/menu.c 2>&1 | grep 'warning:'`

Expected exactly two lines:

```
Core/Src/menu/menu.c:1079:5: warning: 'strcpy' will always overflow; destination buffer has size 10, but the source string has length 11 (including NUL byte) [-Wfortify-source]
Core/Src/menu/menu.c:1083:5: warning: 'strcpy' will always overflow; destination buffer has size 10, but the source string has length 11 (including NUL byte) [-Wfortify-source]
```

- [ ] **Step 2: Write the text within the row**

The cases after the change:

```c
			case CLEAR_AUTOMATION1:
				//LICHOMAT: "autom. 1]?" ran one byte past row 0: its '?' fell into the 17th byte,
				//off the display, and its NUL into row 1. Row 0 shows the same 16 characters (F3)
				strcpy(&editDisplayBuffer[0][7], "autom. 1]");
			break;

			case CLEAR_AUTOMATION2:
				strcpy(&editDisplayBuffer[0][7], "autom. 2]");
			break;
```

Run:

```bash
cd /Users/tk/Code/lxr && python3 - <<'EOF'
from pathlib import Path
p = Path("Core/Src/menu/menu.c")
s = p.read_bytes()
old = (b"\t\t\tcase CLEAR_AUTOMATION1:\r\n"
       b"\t\t\t\tstrcpy(&editDisplayBuffer[0][7], \"autom. 1]?\");\r\n"
       b"\t\t\tbreak;\r\n"
       b"\r\n"
       b"\t\t\tcase CLEAR_AUTOMATION2:\r\n"
       b"\t\t\t\tstrcpy(&editDisplayBuffer[0][7], \"autom. 2]?\");\r\n")
new = (b"\t\t\tcase CLEAR_AUTOMATION1:\r\n"
       b"\t\t\t\t//LICHOMAT: \"autom. 1]?\" ran one byte past row 0: its '?' fell into the 17th byte,\r\n"
       b"\t\t\t\t//off the display, and its NUL into row 1. Row 0 shows the same 16 characters (F3)\r\n"
       b"\t\t\t\tstrcpy(&editDisplayBuffer[0][7], \"autom. 1]\");\r\n"
       b"\t\t\tbreak;\r\n"
       b"\r\n"
       b"\t\t\tcase CLEAR_AUTOMATION2:\r\n"
       b"\t\t\t\tstrcpy(&editDisplayBuffer[0][7], \"autom. 2]\");\r\n")
assert s.count(old) == 1
p.write_bytes(s.replace(old, new))
EOF
perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "CRLF $c LF $l\n" }' Core/Src/menu/menu.c
```

Expected: `CRLF 4507 LF 4507`.

- [ ] **Step 3: Run the fortify check and see it pass**

Run: `cd /Users/tk/Code/lxr && cc -std=gnu11 -fsyntax-only -Wno-everything -Werror=fortify-source -Itests/stubs/presets -Itests/stubs -isystem Core/Inc -isystem Middlewares/Third_Party/FatFs/src -isystem FATFS/Target Core/Src/menu/menu.c && echo FORTIFY_CLEAN`

Expected: `FORTIFY_CLEAN` and nothing else. (Before the fix this command printed two `error:` lines and failed.)

- [ ] **Step 4: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `out of 33` with 100 % passed, the image check OK, `== compiler warnings: 0`, nine `PASS` lines, ITCM 13232 B.

- [ ] **Step 5: Commit**

```bash
cd /Users/tk/Code/lxr && git add Core/Src/menu/menu.c && git commit -F - <<'EOF'
Clear page: the automation text stays within its row (E5.2 F3)

strcpy(&editDisplayBuffer[0][7], "autom. 1]?") wrote 11 bytes into the
10 left in row 0 of the 2 x 17 display buffer: the '?' landed in the 17th
byte, which the display never sends, and the NUL in row 1, which a full
clear has just filled with spaces and which the display shows as a space.
The two clear-automation pages now write "autom. 1]" and "autom. 2]",
which fit and show the same 16 characters. macOS's fortify check aborted
on the old copy in the emulator.

Test: clang's -Wfortify-source flagged menu.c:1079 and :1083 and is clean
now. Later: M15 compares both pages' LCD snapshots with the base build
built with -D_FORTIFY_SOURCE=0 (stage 1b spec §12.1); device check U6.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task R1a.7: F5, the probability draw takes `SEQ_RANDOM()`

§7 F5, §4.9. `seq_cbRandom()` (`sequencer.c:2115–2119`) returns `SEQ_RANDOM()`, a macro that defaults to `GetRngValue()`. A host build defines it before that point, to a random stream of its own: the emulator's sequencer stream (part 2), here a test function. Acceptance: the release `.bin` is byte-identical with the base build.

`test_sequencer` gets the definition from `tests/stubs/seq_random_test.h`, which CMake forces into `sequencer.c` with `-include`. That header also declares the function, so `sequencer.c` needs no edit for the test.

**Files:**
- Modify: `Core/Src/Soundengine/Sequencer/sequencer.c:2114-2119`
- Create: `tests/stubs/seq_random_test.h`
- Modify: `tests/test_sequencer.c:23` (header comment), `:48` (include), `:277-278` (the test before `TEST_MAIN()`)
- Modify: `tests/CMakeLists.txt:180-189`

- [ ] **Step 1: Save the base build**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build --preset release > build/base.log 2>&1 && mkdir -p build/base && cp build/release/LXR_V2_STM32F765.bin build/base/ && git status --short -- Core tests tools cmake CMakeLists.txt && echo BASE_SAVED`

Expected: `BASE_SAVED` and no `git status` line.

- [ ] **Step 2: Write `tests/stubs/seq_random_test.h`**

```c
/*
 * seq_random_test.h
 *
 * Forced into sequencer.c for test_sequencer (-include, tests/CMakeLists.txt):
 * SEQ_RANDOM(), the source of the probability draw (stage 1b F5), becomes the
 * test's seqTest_random() instead of the hardware RNG.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef SEQ_RANDOM_TEST_H_
#define SEQ_RANDOM_TEST_H_

#include <stdint.h>

uint32_t seqTest_random(void);
#define SEQ_RANDOM() seqTest_random()

#endif /* SEQ_RANDOM_TEST_H_ */
```

- [ ] **Step 3: Force it into `sequencer.c` for `test_sequencer`**

In `tests/CMakeLists.txt`, replace lines 180–189 (from `# The sequencer glue (E4.6): …` to the `endif()` of `test_sequencer`) with:

```cmake
# The sequencer glue (E4.6): test_sequencer compiles the firmware's sequencer.c
# with the timing core, the stand-in headers in stubs/ (the HAL with the interrupt
# mask of the mailbox) and the firmware's own FX bus header. The song it edits is
# a buffer of the test with guard bytes after the 64 entries. stubs/seq_random_test.h
# is forced into sequencer.c: its probability draw, SEQ_RANDOM(), becomes the
# test's seqTest_random() (stage 1b F5).
seqc_add_test(test_sequencer IDS sequencer EXTRA ${LXR_ROOT}/Core/Src/Soundengine/Sequencer/sequencer.c)  # E4.6  song page position, automation step; stage 1b F5 SEQ_RANDOM()
if(TARGET test_sequencer)
    target_include_directories(test_sequencer BEFORE PRIVATE stubs)
    target_include_directories(test_sequencer SYSTEM PRIVATE ${LXR_ROOT}/Core/Inc)
    set_source_files_properties(${LXR_ROOT}/Core/Src/Soundengine/Sequencer/sequencer.c PROPERTIES
        COMPILE_OPTIONS "-w;-include;${CMAKE_CURRENT_SOURCE_DIR}/stubs/seq_random_test.h")
endif()
```

- [ ] **Step 4: Write the failing test**

In `tests/test_sequencer.c`:

(a) After the header line ` * now disarms.` (line 23), add:

```c
 *
 * The probability draw (stage 1b F5): seq_cbRandom() returns SEQ_RANDOM(), which
 * a host build sets to a random stream of its own. stubs/seq_random_test.h, which
 * tests/CMakeLists.txt forces into sequencer.c, makes it seqTest_random() here.
```

(b) After `#include "Soundengine/USB/usb_manager.h"` (line 48), add `#include "seq_random_test.h"`.

(c) Before the line `TEST_MAIN()` (line 278), add:

```c
//------------------------------------------------------------------------------
// The probability draw (stage 1b F5): seq_cbRandom() returns SEQ_RANDOM(), which
// stubs/seq_random_test.h makes seqTest_random() for this target. The firmware
// keeps GetRngValue(), the hardware RNG.
//------------------------------------------------------------------------------
static int seqTestRandomCalls;

uint32_t seqTest_random(void)
{
	seqTestRandomCalls++;
	return 0;
}

TEST(sequencer_probability_draw_takes_seq_random)
{
	reset();
	seq_setMainStep(0, 0, 0, 1);		/* pattern 0, track 1, step 1 */
	seqTestRandomCalls = 0;
	playAndStop(500);
	seq_setMainStep(0, 0, 0, 0);
	ASSERT_TRUE(seqTestRandomCalls > 0);
}

```

- [ ] **Step 5: Run the test and see it fail**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake -S tests -B build/host-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build build/host-tests --target test_sequencer >/dev/null && build/host-tests/test_sequencer sequencer | tail -3`

Expected:

```
…/tests/test_sequencer.c:303: ASSERT_TRUE(seqTestRandomCalls > 0) failed
FAIL sequencer_probability_draw_takes_seq_random
5 of 6 tests passed
```

The line number may differ by one or two.

- [ ] **Step 6: Add the hook to `sequencer.c`**

The code after the change (lines 2114–2125):

```c
/* LICHOMAT: the source of the probability draw. A host build defines
 * SEQ_RANDOM() before this point, to a random stream of its own (stage 1b F5,
 * emulator spec 4.9); the firmware takes the hardware RNG. */
#ifndef SEQ_RANDOM
#define SEQ_RANDOM() GetRngValue()
#endif
/** the probability draw: the core uses 1 + random() % 100 */
static uint32_t seq_cbRandom(void *ctx)
{
	(void)ctx;
	return SEQ_RANDOM();
}
```

Run:

```bash
cd /Users/tk/Code/lxr && python3 - <<'EOF'
from pathlib import Path
p = Path("Core/Src/Soundengine/Sequencer/sequencer.c")
s = p.read_bytes()
old = (b"/** the probability draw: the core uses 1 + random() % 100 */\r\n"
       b"static uint32_t seq_cbRandom(void *ctx)\r\n"
       b"{\r\n"
       b"\t(void)ctx;\r\n"
       b"\treturn GetRngValue();\r\n"
       b"}\r\n")
new = (b"/* LICHOMAT: the source of the probability draw. A host build defines\r\n"
       b" * SEQ_RANDOM() before this point, to a random stream of its own (stage 1b F5,\r\n"
       b" * emulator spec 4.9); the firmware takes the hardware RNG. */\r\n"
       b"#ifndef SEQ_RANDOM\r\n"
       b"#define SEQ_RANDOM() GetRngValue()\r\n"
       b"#endif\r\n"
       b"/** the probability draw: the core uses 1 + random() % 100 */\r\n"
       b"static uint32_t seq_cbRandom(void *ctx)\r\n"
       b"{\r\n"
       b"\t(void)ctx;\r\n"
       b"\treturn SEQ_RANDOM();\r\n"
       b"}\r\n")
assert s.count(old) == 1
p.write_bytes(s.replace(old, new))
EOF
perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "CRLF $c LF $l\n" }' Core/Src/Soundengine/Sequencer/sequencer.c
```

Expected: `CRLF 2350 LF 2350`.

- [ ] **Step 7: Run the test and see it pass**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build build/host-tests --target test_sequencer >/dev/null && build/host-tests/test_sequencer sequencer | tail -2`

Expected:

```
PASS sequencer_probability_draw_takes_seq_random
6 of 6 tests passed
```

- [ ] **Step 8: Compile the test as CI's GCC would (strict C11)**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && arm-none-eabi-gcc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -Itests/stubs -Itests -isystem Core/Inc -c -o /dev/null tests/test_sequencer.c && echo GCC_OK`

Expected: `GCC_OK`.

- [ ] **Step 9: Compare the release image with the base build**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build --preset release > build/after.log 2>&1 && cmp build/base/LXR_V2_STM32F765.bin build/release/LXR_V2_STM32F765.bin && echo BIN_IDENTICAL`

Expected: `BIN_IDENTICAL`.

- [ ] **Step 10: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `out of 33` with 100 % passed, the image check OK, `== compiler warnings: 0`, nine `PASS` lines.

- [ ] **Step 11: Commit**

```bash
cd /Users/tk/Code/lxr && git add Core/Src/Soundengine/Sequencer/sequencer.c tests/stubs/seq_random_test.h tests/test_sequencer.c tests/CMakeLists.txt && git commit -F - <<'EOF'
Sequencer: the probability draw takes SEQ_RANDOM() (E5.2 F5)

seq_cbRandom() returns SEQ_RANDOM(), a macro that defaults to
GetRngValue(), the hardware RNG. A host build defines it before that
point: the emulator gives the sequencer its own random stream, so that a
probability edit does not change the noise of the DSP (stage 1b spec
§4.9). The release .bin is byte-identical with the base build.

Test: test_sequencer forces stubs/seq_random_test.h into sequencer.c,
which makes SEQ_RANDOM() the test's seqTest_random(); a pattern with one
step must draw from it.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task R1a.8: F6, the saturating float-to-uint32 conversion `f2u32_sat()`

§7 F6, §8.2. Nine float-to-uint32 conversions in `Oscillator.c` can be out of range: the 8 FM sites (lines 118, 141, 166, 194, 232, 516, 543 and 577) and `freq2PhaseIncr()` (line 25). In C that is undefined; the M7's `VCVT.U32.F32` saturates, and so does arm64's `FCVTZU`. `f2u32_sat()` is the plain cast on the device and the saturating conversion in C on a host. Lines 516 and 543 are inside a comment and 577 under `#if 0`; they change too, as §7 lists them, so that no unsafe copy remains.

Acceptance: the release `.bin` is byte-identical with the base build, and M1's host check of F6: the host version equals `FCVTZU` for edge values and NaN. `tests/test_f2u32.c` does this check on every host and compares with `FCVTZU` itself on an arm64 host.

**Files:**
- Create: `Core/Inc/Soundengine/DSPAudio/f2u32.h` (CRLF)
- Modify: `Core/Src/Soundengine/DSPAudio/Oscillator.c:15` (an include after it), `:25`, `:118`, `:141`, `:166`, `:194`, `:232`, `:516`, `:543`, `:577`
- Create: `tests/test_f2u32.c`
- Modify: `tests/CMakeLists.txt` (a block after the last line)

- [ ] **Step 1: Save the base build**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build --preset release > build/base.log 2>&1 && mkdir -p build/base && cp build/release/LXR_V2_STM32F765.bin build/base/ && git status --short -- Core tests tools cmake CMakeLists.txt && echo BASE_SAVED`

Expected: `BASE_SAVED` and no `git status` line.

- [ ] **Step 2: Write the failing test**

Create `tests/test_f2u32.c`:

```c
/*
 * test_f2u32.c
 *
 * Host test of f2u32_sat() (Core/Inc/Soundengine/DSPAudio/f2u32.h, stage 1b
 * F6), the float-to-uint32 conversion of the oscillators' FM offsets and phase
 * increments. It must give what the Cortex-M7's VCVT.U32.F32 gives, and arm64's
 * FCVTZU gives the same: truncation towards zero, 0 for NaN and for everything
 * below 1, UINT32_MAX from 2^32 up. The edge values are checked on every host.
 * On an arm64 host the conversion is also compared with FCVTZU itself, for
 * every sign and exponent with many mantissas, NaNs and infinities included
 * (emulator spec §12.1, M1).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include <float.h>
#include <math.h>
#include <stdint.h>

#include "Soundengine/DSPAudio/f2u32.h"

/** the float with the bit pattern bits */
static float fromBits(uint32_t bits)
{
	float f;
	memcpy(&f, &bits, sizeof f);
	return f;
}

TEST(f2u32_edge_values)
{
	ASSERT_EQ(f2u32_sat(0.f), 0u);
	ASSERT_EQ(f2u32_sat(-0.f), 0u);
	ASSERT_EQ(f2u32_sat(fromBits(0x00000001u)), 0u);	/* the smallest denormal */
	ASSERT_EQ(f2u32_sat(0.75f), 0u);
	ASSERT_EQ(f2u32_sat(1.f), 1u);
	ASSERT_EQ(f2u32_sat(1.75f), 1u);
	ASSERT_EQ(f2u32_sat(-0.75f), 0u);
	ASSERT_EQ(f2u32_sat(-1.f), 0u);
	ASSERT_EQ(f2u32_sat(-1e30f), 0u);
	ASSERT_EQ(f2u32_sat(-INFINITY), 0u);
	ASSERT_EQ(f2u32_sat(16777216.f), 16777216u);
	ASSERT_EQ(f2u32_sat(4294967040.f), 4294967040u);	/* the largest float below 2^32 */
	ASSERT_EQ(f2u32_sat(4294967296.f), UINT32_MAX);	/* 2^32 */
	ASSERT_EQ(f2u32_sat(5e9f), UINT32_MAX);
	ASSERT_EQ(f2u32_sat(FLT_MAX), UINT32_MAX);
	ASSERT_EQ(f2u32_sat(INFINITY), UINT32_MAX);
	ASSERT_EQ(f2u32_sat(NAN), 0u);
	ASSERT_EQ(f2u32_sat(fromBits(0xFFC00000u)), 0u);	/* a negative quiet NaN */
	ASSERT_EQ(f2u32_sat(fromBits(0x7F800001u)), 0u);	/* a signalling NaN */
}

#if defined(__aarch64__)
/** arm64's FCVTZU (to 32 bits, towards zero), which saturates as the M7 does */
static uint32_t fcvtzu(float x)
{
	uint32_t r;
	__asm__("fcvtzu %w0, %s1" : "=r"(r) : "w"(x));
	return r;
}

TEST(f2u32_equals_fcvtzu)
{
	/* every high half-word: each sign, exponent and top of the mantissa, with
	 * three low half-words */
	static const uint32_t low[3] = { 0x0000u, 0x0001u, 0xFFFFu };
	for (uint32_t high = 0; high <= 0xFFFFu; high++) {
		for (int k = 0; k < 3; k++) {
			const uint32_t bits = high << 16 | low[k];
			const float x = fromBits(bits);
			if (f2u32_sat(x) != fcvtzu(x))
				printf("0x%08X: %u, FCVTZU %u\n", (unsigned)bits, (unsigned)f2u32_sat(x), (unsigned)fcvtzu(x));
			ASSERT_EQ(f2u32_sat(x), fcvtzu(x));
		}
	}
}
#endif

TEST_MAIN()
```

- [ ] **Step 3: Register the test**

Append to `tests/CMakeLists.txt`, after its last line:

```cmake

# The float-to-uint32 conversion of the oscillators (stage 1b F6): test_f2u32
# checks f2u32_sat() (Core/Inc/Soundengine/DSPAudio/f2u32.h) at its edge values,
# and against arm64's FCVTZU on an arm64 host.
seqc_add_test(test_f2u32     IDS f2u32)  # stage 1b F6  saturating float-to-uint32
```

- [ ] **Step 4: Build the test and see it fail**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake -S tests -B build/host-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build build/host-tests --target test_f2u32 2>&1 | grep -m1 'error'`

Expected: `…/tests/test_f2u32.c:26:10: fatal error: 'Soundengine/DSPAudio/f2u32.h' file not found`.

- [ ] **Step 5: Write `Core/Inc/Soundengine/DSPAudio/f2u32.h`**

```c
/*
 * f2u32.h
 *
 * f2u32_sat(): a float-to-uint32 conversion that saturates as the Cortex-M7's
 * VCVT.U32.F32 does. It truncates towards zero; NaN and everything below 1
 * give 0, everything from 2^32 up gives UINT32_MAX. arm64's FCVTZU gives the
 * same. On the device (32-bit Arm) it is the plain cast, so the firmware's code
 * does not change. In C an out-of-range conversion is undefined: x86-64 wraps,
 * a compiler may fold it to anything, and the sanitizers stop on it. So a host
 * build (tests/, the emulator) gets the saturation in C (stage 1b F6, emulator
 * spec 7 and 8.2). Under -ffinite-math-only the NaN case is not kept.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef F2U32_H_
#define F2U32_H_

#include <stdint.h>

static inline __attribute__((always_inline)) uint32_t f2u32_sat(float x)
{
#if defined(__arm__)
	return (uint32_t)x;
#else
	if (!(x >= 1.f))		/* NaN, and everything below 1 */
		return 0;
	if (x >= 4294967296.f)	/* 2^32 */
		return UINT32_MAX;
	return (uint32_t)x;
#endif
}

#endif /* F2U32_H_ */
```

Run: `cd /Users/tk/Code/lxr && perl -pi -e 's/\r?\n/\r\n/' Core/Inc/Soundengine/DSPAudio/f2u32.h && perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "CRLF $c LF $l\n" }' Core/Inc/Soundengine/DSPAudio/f2u32.h`

Expected: `CRLF 38 LF 38`.

- [ ] **Step 6: Run the test and see it pass**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build build/host-tests --target test_f2u32 >/dev/null && build/host-tests/test_f2u32 f2u32`

Expected on the Mac (arm64):

```
PASS f2u32_edge_values
PASS f2u32_equals_fcvtzu
2 of 2 tests passed
```

On x86-64 (CI) only `f2u32_edge_values` runs.

- [ ] **Step 7: Use `f2u32_sat()` at the nine sites of `Oscillator.c`**

After the change, line 25 reads `return f2u32_sat((((TABLESIZE*f)/REAL_FS))*1048576) ; // 1048576 <-> (<<20)`, and each FM site reads like line 119 (line 118 before the include):

```c
		const uint32_t index =  oscPhase + ((f2u32_sat(modBuffer[i]*osc->fmMod))<<17);
```

Run:

```bash
cd /Users/tk/Code/lxr && python3 - <<'EOF'
from pathlib import Path
p = Path("Core/Src/Soundengine/DSPAudio/Oscillator.c")
s = p.read_bytes()

def sub(old, new, count):
    global s
    assert s.count(old) == count, (old, s.count(old))
    s = s.replace(old, new)

sub(b'#include "Soundengine/DSPAudio/Oscillator.h"\r\n',
    b'#include "Soundengine/DSPAudio/Oscillator.h"\r\n#include "Soundengine/DSPAudio/f2u32.h"\r\n', 1)
sub(b"\treturn (((TABLESIZE*f)/REAL_FS))*1048576 ; // 1048576 <-> (<<20)",
    b"\treturn f2u32_sat((((TABLESIZE*f)/REAL_FS))*1048576) ; // 1048576 <-> (<<20)", 1)
sub(b"(uint32_t)(modBuffer[i]*osc->fmMod)", b"f2u32_sat(modBuffer[i]*osc->fmMod)", 5)
sub(b"(uint32_t)(modOsc->output*osc->fmMod)", b"f2u32_sat(modOsc->output*osc->fmMod)", 3)
p.write_bytes(s)
print("f2u32_sat calls:", s.count(b"f2u32_sat("))
EOF
perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "CRLF $c LF $l\n" }' Core/Src/Soundengine/DSPAudio/Oscillator.c
```

Expected: `f2u32_sat calls: 9`, then `CRLF 825 LF 825`.

- [ ] **Step 8: Compare the release image with the base build**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build --preset release > build/after.log 2>&1 && cmp build/base/LXR_V2_STM32F765.bin build/release/LXR_V2_STM32F765.bin && echo BIN_IDENTICAL`

Expected: `BIN_IDENTICAL`. On the M7 `f2u32_sat()` is the cast, inlined.

- [ ] **Step 9: Compile the test as CI's GCC would (strict C11)**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && arm-none-eabi-gcc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -Itests/stubs -Itests -isystem Core/Inc -c -o /dev/null tests/test_f2u32.c && echo GCC_OK`

Expected: `GCC_OK`.

- [ ] **Step 10: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `out of 34` with 100 % passed (`dsp` compiles the changed `Oscillator.c`), the image check OK, `== compiler warnings: 0`, nine `PASS` lines.

- [ ] **Step 11: Commit**

```bash
cd /Users/tk/Code/lxr && git add Core/Inc/Soundengine/DSPAudio/f2u32.h Core/Src/Soundengine/DSPAudio/Oscillator.c tests/test_f2u32.c tests/CMakeLists.txt && git commit -F - <<'EOF'
Oscillators: saturating float-to-uint32 conversion f2u32_sat() (E5.2 F6)

The 8 FM sites of Oscillator.c and freq2PhaseIncr() convert a float that
can be out of range to uint32_t. In C that is undefined; the M7's
VCVT.U32.F32 saturates, and so does arm64's FCVTZU. f2u32_sat() in the new
Core/Inc/Soundengine/DSPAudio/f2u32.h is the plain cast on the device and
the saturating conversion in C on a host, so the emulator's sanitizers can
keep their float-cast check for all other code (stage 1b spec §7, §8.2).
Two of the FM sites are in comments and one under #if 0; they change too.

The release .bin is byte-identical with the base build. Test:
tests/test_f2u32.c (ctest id f2u32) checks the edge values, NaN included,
on every host and compares with FCVTZU on an arm64 host (M1's check of F6).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task R1a.9: F7, the DMA buffers get 4-byte alignment

§7 F7. `dma_buffer` and `dma_buffer2` (`AudioCodecManager.c:17–18`) are `int16_t` arrays that the mixer writes as `int32_t`. On the device GCC places them at 4-byte aligned addresses (`0x20019e1c` and `0x2001a01c` in the release map), but their declaration asks for 2 bytes, and a host compiler may use any even address; the emulator's UBSan alignment check would then stop. Acceptance: the release `.bin` is byte-identical with the base build.

§7 also covers "any other `int16_t` buffer accessed as `int32_t` that UBSan's alignment check finds". Only the emulator runs that code under UBSan, so part 2 adds any such buffer as its own commit, following §7's rule for further sanitizer findings.

The alignment of a definition is visible only in its own translation unit. So `tests/test_dmabuf.c` includes `AudioCodecManager.c` itself.

**Files:**
- Modify: `Core/Src/Soundengine/AudioCodecManager.c:16-19`
- Create: `tests/test_dmabuf.c`
- Modify: `tests/CMakeLists.txt` (a block after the last line)

- [ ] **Step 1: Save the base build**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build --preset release > build/base.log 2>&1 && mkdir -p build/base && cp build/release/LXR_V2_STM32F765.bin build/base/ && git status --short -- Core tests tools cmake CMakeLists.txt && echo BASE_SAVED`

Expected: `BASE_SAVED` and no `git status` line.

- [ ] **Step 2: Write the failing test**

Create `tests/test_dmabuf.c`:

```c
/*
 * test_dmabuf.c
 *
 * Host test of the codec's DMA buffers (stage 1b F7): dma_buffer and
 * dma_buffer2 in Core/Src/Soundengine/AudioCodecManager.c are int16_t arrays,
 * and the mixer writes them as int32_t. On the device they sit at 4-byte
 * aligned addresses only because GCC places arrays so; their declaration asked
 * for 2. A host compiler may place them at any even address, and UBSan's
 * alignment check in the emulator stops on such a store. The definition now
 * asks for 4 bytes.
 *
 * The alignment of a definition is visible only in its own translation unit,
 * so this file includes AudioCodecManager.c itself. It casts a pointer to
 * uint32_t, which a 64-bit host warns about.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include <stdint.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#include "../Core/Src/Soundengine/AudioCodecManager.c"
#pragma GCC diagnostic pop

/* CodecInit() hands the buffers to the codec driver (cs4344_cs5343.c) */
void codec_initCsCodec(uint32_t Addr1, uint32_t Size1, uint32_t Addr2, uint32_t Size2)
{
	(void)Addr1; (void)Size1; (void)Addr2; (void)Size2;
}

TEST(dmabuf_buffers_are_4_byte_aligned)
{
	ASSERT_TRUE(__alignof__(dma_buffer) >= 4);
	ASSERT_TRUE(__alignof__(dma_buffer2) >= 4);
	ASSERT_EQ((uintptr_t)dma_buffer % 4, 0);
	ASSERT_EQ((uintptr_t)dma_buffer2 % 4, 0);
}

TEST_MAIN()
```

- [ ] **Step 3: Register the test**

Append to `tests/CMakeLists.txt`, after its last line:

```cmake

# The codec's DMA buffers (stage 1b F7): test_dmabuf includes the firmware's
# AudioCodecManager.c itself, because the alignment of a definition is visible
# only in its own translation unit. The stand-in headers in stubs/ come first.
seqc_add_test(test_dmabuf    IDS dmabuf)  # stage 1b F7  4-byte aligned DMA buffers
if(TARGET test_dmabuf)
    target_include_directories(test_dmabuf BEFORE PRIVATE stubs)
    target_include_directories(test_dmabuf SYSTEM PRIVATE ${LXR_ROOT}/Core/Inc)
endif()
```

- [ ] **Step 4: Run the test and see it fail**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake -S tests -B build/host-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build build/host-tests --target test_dmabuf >/dev/null && build/host-tests/test_dmabuf dmabuf`

Expected:

```
…/tests/test_dmabuf.c:40: ASSERT_TRUE(__alignof__(dma_buffer) >= 4) failed
FAIL dmabuf_buffers_are_4_byte_aligned
0 of 1 tests passed
```

- [ ] **Step 5: Align the buffers**

The code after the change:

```c
#if DMA_MODE_ACTIVE
//LICHOMAT: the mixer writes these int16_t buffers as int32_t, so they are 4-byte aligned (F7)
INDTCMZ __attribute__((aligned(4))) int16_t dma_buffer[OUTPUT_DMA_SIZE*4];
INDTCMZ __attribute__((aligned(4))) int16_t dma_buffer2[OUTPUT_DMA_SIZE*4];
#endif
```

Run:

```bash
cd /Users/tk/Code/lxr && python3 - <<'EOF'
from pathlib import Path
p = Path("Core/Src/Soundengine/AudioCodecManager.c")
s = p.read_bytes()
old = (b"#if DMA_MODE_ACTIVE\r\n"
       b"INDTCMZ int16_t dma_buffer[OUTPUT_DMA_SIZE*4];\r\n"
       b"INDTCMZ int16_t dma_buffer2[OUTPUT_DMA_SIZE*4];\r\n"
       b"#endif\r\n")
new = (b"#if DMA_MODE_ACTIVE\r\n"
       b"//LICHOMAT: the mixer writes these int16_t buffers as int32_t, so they are 4-byte aligned (F7)\r\n"
       b"INDTCMZ __attribute__((aligned(4))) int16_t dma_buffer[OUTPUT_DMA_SIZE*4];\r\n"
       b"INDTCMZ __attribute__((aligned(4))) int16_t dma_buffer2[OUTPUT_DMA_SIZE*4];\r\n"
       b"#endif\r\n")
assert s.count(old) == 1
p.write_bytes(s.replace(old, new))
EOF
perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "CRLF $c LF $l\n" }' Core/Src/Soundengine/AudioCodecManager.c
```

Expected: `CRLF 31 LF 31`. The declarations in `Core/Inc/Soundengine/cs4344_cs5343.h:19–20` stay as they are: an alignment there would let GCC assume it in other files and could change their code.

- [ ] **Step 6: Run the test and see it pass**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build build/host-tests --target test_dmabuf >/dev/null && build/host-tests/test_dmabuf dmabuf`

Expected: `PASS dmabuf_buffers_are_4_byte_aligned` and `1 of 1 tests passed`.

- [ ] **Step 7: Compare the release image with the base build**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build --preset release > build/after.log 2>&1 && cmp build/base/LXR_V2_STM32F765.bin build/release/LXR_V2_STM32F765.bin && echo BIN_IDENTICAL`

Expected: `BIN_IDENTICAL`. GCC already aligned the `.dtcmz` section of this file to 4 bytes.

- [ ] **Step 8: Compile the test as CI's GCC would (strict C11)**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && arm-none-eabi-gcc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -Itests/stubs -Itests -isystem Core/Inc -c -o /dev/null tests/test_dmabuf.c && echo GCC_OK`

Expected: `GCC_OK`.

- [ ] **Step 9: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `out of 35` with 100 % passed, the image check OK, `== compiler warnings: 0`, nine `PASS` lines.

- [ ] **Step 10: Commit**

```bash
cd /Users/tk/Code/lxr && git add Core/Src/Soundengine/AudioCodecManager.c tests/test_dmabuf.c tests/CMakeLists.txt && git commit -F - <<'EOF'
Codec DMA buffers get 4-byte alignment (E5.2 F7)

dma_buffer and dma_buffer2 are int16_t arrays that the mixer writes as
int32_t. GCC places them at 4-byte aligned addresses on the device, but
their definition asked for 2 bytes, and a host compiler may use any even
address, where the emulator's UBSan alignment check stops (stage 1b spec
§7). The definitions now ask for 4 bytes.

The release .bin is byte-identical with the base build. Test:
tests/test_dmabuf.c (ctest id dmabuf) includes AudioCodecManager.c and
checks the alignment of both definitions.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task R1a.10: F9, the wavetable read at the end of a period

§7 F9. `calcFmBlock()` and `calcWavetableOscBlock()` interpolate between `table[row][itg]` and `table[row][itg + 1]` with `itg` up to 1023, so they read `table[row][1024]`. For rows 0–9 that is the next row's first sample; for row 10 it is past the table, so a host render would depend on the memory layout. The fix reads `table[row][itg & 1023]`: after sample 1023 comes the row's own sample 0. The lines are `Oscillator.c:175` and `:471` at the HEAD, and `:176` and `:472` after Task R1a.8's include.

Not changed:
- `calcFm()` (`:241`) and `calcWavetableOsc()` (`:495`) read the same way, but their only callers are in comments (the blocks that start at `:290` and `:418`); `--gc-sections` drops them.
- The PWM reads (`calcPwmFmBlock()`, `calcPwmBlock()`) are B13 and B14 of the sanitizer sweep, which wait for the owner's U8 listening check.

**A finding beyond the spec.** §7 says the fix changes the output only in band 10. That holds for saw and triangle, whose rows all start with 0 and −32767. The rect table's rows 0–10 start with 0, 0, −1, 0, 3, 10, 38, 136, 396, 2879 and 338 (checked on the host). So the rect waveform's last sample of each period changes in the lower rows too. Only rows 4–10 are used: every wavetable oscillator below 440 Hz takes row 4 (§8.3). The change is 7 LSB at most in row 4, and 260, 2483 and 2541 LSB at most in rows 7, 8 and 9 (1.76–14 kHz). The commit message says so, and the owner's U8 check needs a rect oscillator too (see "Points for the owner and for part 2").

Later acceptance: M15 (no read past the tables under ASan) and the listening check U8, before F9 reaches a release.

**Files:**
- Modify: `Core/Src/Soundengine/DSPAudio/Oscillator.c:176` and `:472` (175 and 471 at the HEAD)
- Modify: `tests/test_dsp.c:25` (header comment), `:46` (include), `:237-238` (the test before `TEST_MAIN()`)

- [ ] **Step 1: Write the failing test**

In `tests/test_dsp.c`:

(a) After the header line ` *   turn (the previous voice's block) or the drum voice's modulation buffer.` (line 25), add:

```c
 * - The wavetable oscillators at the end of a period (stage 1b F9). The last of
 *   the 1024 samples of a row interpolates towards the row's own first sample.
 *   fw 1.7 read table[row][1024]: the next row's first sample, which is the same
 *   for the saw and triangle tables but not for the rect table, and past the
 *   table for row 10.
```

(b) After `#include "Soundengine/DSPAudio/ResonantFilter.h"` (line 46), add `#include "Soundengine/DSPAudio/wavetable.h"`.

(c) Before the line `TEST_MAIN()` (line 238), add:

```c
//------------------------------------------------------------------------------
// The wavetable interpolation at the end of a period (stage 1b F9)
//------------------------------------------------------------------------------
/** the oscillators' interpolation from a towards b, frac in 22 bits */
static int16_t towards(int16_t a, int16_t b, uint32_t frac)
{
	return (int16_t)(a + (int16_t)(((int64_t)(b - a) * frac) >> 22));
}

TEST(dsp_wavetable_period_end_reads_the_row_start)
{
	/* The last of the 1024 samples of a row interpolates towards the first
	 * sample of the same row. fw 1.7 read table[row][1024]: the first sample of
	 * the next row, and past the table for row 10. */
	const int16_t (*const tables[3])[1024] = { sawTable, triTable, recTable };
	for (int t = 0; t < 3; t++) {
		for (uint8_t row = 0; row < 11; row++) {
			const int16_t expected = towards(tables[t][row][1023], tables[t][row][0], 0x200000u);
			OscInfo osc = oscillator(SAW);
			osc.tableOffset = row;
			osc.phase = 0xFFE00000u;	/* sample 1023, half way to the next */
			osc.phaseInc = 0;
			int16_t out[1] = { 0 };
			calcWavetableOscBlock(&osc, tables[t], out, 1, 1.f);
			ASSERT_EQ(out[0], expected);
			int16_t mod[1] = { 0 };		/* no FM: the same phase */
			calcFmBlock(&osc, tables[t], mod, out, 1, 1.f);
			ASSERT_EQ(out[0], expected);
		}
	}
}

```

- [ ] **Step 2: Run the test and see it fail**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake -S tests -B build/host-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build build/host-tests --target test_dsp >/dev/null && build/host-tests/test_dsp dsp; echo "exit $?"`

Expected: the earlier `dsp_*` tests pass, then `…/Core/Src/Soundengine/DSPAudio/Oscillator.c:472:43: runtime error: index 1024 out of bounds for type 'const int16_t[1024]' (aka 'const short[1024]')` and `exit 134`. CI's ASan build stops at row 10 too; without a sanitizer the rect rows fail the `ASSERT_EQ`.

- [ ] **Step 3: Read the row's own first sample**

After the change, both reads look like this one in `calcWavetableOscBlock()`:

```c
			uint32_t frac = oscPhase & 0x3FFFFF;
			//LICHOMAT: sample 1023 interpolates towards the row's own sample 0, not table[row][1024] (F9)
			oscOut = oscOut + (int16_t)(((int64_t)(table[osc->tableOffset][itg & 1023] - oscOut) * frac)   >>22);
```

Run:

```bash
cd /Users/tk/Code/lxr && python3 - <<'EOF'
from pathlib import Path
p = Path("Core/Src/Soundengine/DSPAudio/Oscillator.c")
s = p.read_bytes()
note = b"//LICHOMAT: sample 1023 interpolates towards the row's own sample 0, not table[row][1024] (F9)\r\n"
for tabs, frac in ((b"\t\t", b"index"), (b"\t\t\t", b"oscPhase")):    # calcFmBlock(), calcWavetableOscBlock()
    old = (tabs + b"uint32_t frac = " + frac + b" & 0x3FFFFF;\r\n"
           + tabs + b"oscOut = oscOut + (int16_t)(((int64_t)(table[osc->tableOffset][itg] - oscOut) * frac)   >>22);\r\n")
    new = (tabs + b"uint32_t frac = " + frac + b" & 0x3FFFFF;\r\n"
           + tabs + note
           + tabs + b"oscOut = oscOut + (int16_t)(((int64_t)(table[osc->tableOffset][itg & 1023] - oscOut) * frac)   >>22);\r\n")
    assert s.count(old) == 1, frac
    s = s.replace(old, new)
p.write_bytes(s)
EOF
perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "CRLF $c LF $l\n" }' Core/Src/Soundengine/DSPAudio/Oscillator.c && grep -c 'itg & 1023' Core/Src/Soundengine/DSPAudio/Oscillator.c
```

Expected: `CRLF 827 LF 827`, then `2`.

- [ ] **Step 4: Run the test and see it pass**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build build/host-tests --target test_dsp >/dev/null && build/host-tests/test_dsp dsp | tail -2`

Expected:

```
PASS dsp_wavetable_period_end_reads_the_row_start
5 of 5 tests passed
```

- [ ] **Step 5: Compile the test as CI's GCC would (strict C11)**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && arm-none-eabi-gcc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -Itests/stubs -Itests -isystem Core/Inc -c -o /dev/null tests/test_dsp.c && echo GCC_OK`

Expected: `GCC_OK`.

- [ ] **Step 6: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `out of 35` with 100 % passed, the image check OK, `== compiler warnings: 0`, nine `PASS` lines. ITCM may grow by a few bytes (both functions are in ITCM); it must fit.

- [ ] **Step 7: Commit**

```bash
cd /Users/tk/Code/lxr && git add Core/Src/Soundengine/DSPAudio/Oscillator.c tests/test_dsp.c && git commit -F - <<'EOF'
Wavetable oscillators interpolate towards the row's own start (E5.2 F9)

calcFmBlock() and calcWavetableOscBlock() interpolated sample 1023 of a
row towards table[row][1024]: the next row's first sample, and for row 10
(band 10, from 14,080 Hz, reached only by pitch modulation) whatever the
linker put after the table, so a host render depended on the memory
layout. They now read table[row][itg & 1023], the row's own first sample
(stage 1b spec §7 F9).

The device's sound changes: for saw and triangle only in band 10, since
their rows all start with 0 and -32767. For the rect table also in the
rows below it, whose first samples differ (rows 0-10: 0, 0, -1, 0, 3,
10, 38, 136, 396, 2879, 338): once per period, by at most 7 LSB in row 4
(every pitch below 440 Hz) and up to 2541 LSB in rows 7-9 (1.76-14 kHz).
calcFm() and calcWavetableOsc() read the same way but have no live
caller; the PWM reads are B13/B14 and wait for U8.

Test: test_dsp checks the last sample of every row of the three tables in
both functions; UBSan stopped at index 1024 before the fix. Later: M15
under ASan; the listening check U8 before a release.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task R1a.11: F10, `uint8_t preset_morphBusy(void)`

§7 F10, §5.3. `preset_morphBusy()` returns `morph_state` (`presetManager.c:41`, file-static), which is 1 while `preset_morph_tick()` still sends the parameters of a non-blocking morph or kit transfer. The renderer's t0 rule (M17, R2) waits until it is 0. Nothing in the firmware calls it, and `--gc-sections` drops it. Acceptance: the release `.bin` is byte-identical with the base build.

**Files:**
- Modify: `Core/Src/Preset/presetManager.c:2141` (the function before `preset_morph_tick()`)
- Modify: `Core/Inc/Preset/PresetManager.h:114` (the declaration after it)
- Modify: `tests/test_presets.c:10` (header comment), `:751-752` (the test before `TEST_MAIN()`)

- [ ] **Step 1: Save the base build**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build --preset release > build/base.log 2>&1 && mkdir -p build/base && cp build/release/LXR_V2_STM32F765.bin build/base/ && git status --short -- Core tests tools cmake CMakeLists.txt && echo BASE_SAVED`

Expected: `BASE_SAVED` and no `git status` line.

- [ ] **Step 2: Write the failing test**

In `tests/test_presets.c`:

(a) After the header line ` * took 0-127 for the 64 steps, and the step page wrote pattern data past them.` (line 10), add:

```c
 * And the morph flag of stage 1b's F10: preset_morphBusy() is 1 while
 * preset_morph_tick() still has sound parameters to send.
```

(b) Before the line `TEST_MAIN()` (line 752), add:

```c
//------------------------------------------------------------------------------
// The morph flag (stage 1b F10): preset_morphBusy() is 1 while
// preset_morph_tick() still has sound parameters to send
//------------------------------------------------------------------------------
/** preset_morph_tick() until no morph runs, at most limit times; the ticks made */
static int morphUntilIdle(int limit)
{
	int ticks = 0;
	while (preset_morphBusy() && ticks < limit) {
		preset_morph_tick();
		ticks++;
	}
	return ticks;
}

TEST(presets_morph_busy_while_a_morph_runs)
{
	reset();
	morphUntilIdle(END_OF_SOUND_PARAMETERS);	/* a morph that an earlier test started */
	ASSERT_EQ(preset_morphBusy(), 0);
	preset_morph(64);
	ASSERT_EQ(preset_morphBusy(), 1);
	ASSERT_EQ(morphUntilIdle(1000), END_OF_SOUND_PARAMETERS);	/* one tick per sound parameter */
	ASSERT_EQ(preset_morphBusy(), 0);
}

```

- [ ] **Step 3: Build the test and see it fail**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake -S tests -B build/host-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build build/host-tests --target test_presets 2>&1 | grep -m1 'error:'`

Expected: `…/tests/test_presets.c:…: error: call to undeclared function 'preset_morphBusy'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]`.

- [ ] **Step 4: Add `preset_morphBusy()`**

In `presetManager.c`, before `preset_morph_tick()`:

```c
//----------------------------------------------------
//LICHOMAT: 1 while a non-blocking morph still sends parameters, else 0 (F10)
uint8_t preset_morphBusy(void)
{
	return morph_state;
}
```

In `PresetManager.h`, after `void preset_morph_tick(); //handler for non blocking morph`:

```c
/** 1 while a non-blocking morph (preset_morph_tick()) still sends parameters, else 0 */
uint8_t preset_morphBusy(void);
```

`PresetManager.h:112` holds a non-ASCII byte sequence, so the edit works on bytes. Run:

```bash
cd /Users/tk/Code/lxr && python3 - <<'EOF'
from pathlib import Path

def sub(path, old, new):
    p = Path(path)
    s = p.read_bytes()
    assert s.count(old) == 1, path
    p.write_bytes(s.replace(old, new))

sub("Core/Src/Preset/presetManager.c",
    b"//----------------------------------------------------\r\n"
    b"//called periodically to process non blocking morph\r\n"
    b"void preset_morph_tick()\r\n",
    b"//----------------------------------------------------\r\n"
    b"//LICHOMAT: 1 while a non-blocking morph still sends parameters, else 0 (F10)\r\n"
    b"uint8_t preset_morphBusy(void)\r\n"
    b"{\r\n"
    b"\treturn morph_state;\r\n"
    b"}\r\n"
    b"//----------------------------------------------------\r\n"
    b"//called periodically to process non blocking morph\r\n"
    b"void preset_morph_tick()\r\n")
sub("Core/Inc/Preset/PresetManager.h",
    b"void preset_morph_tick(); //handler for non blocking morph\r\n",
    b"void preset_morph_tick(); //handler for non blocking morph\r\n"
    b"/** 1 while a non-blocking morph (preset_morph_tick()) still sends parameters, else 0 */\r\n"
    b"uint8_t preset_morphBusy(void);\r\n")
EOF
for f in Core/Src/Preset/presetManager.c Core/Inc/Preset/PresetManager.h; do perl -ne '$c++ if /\r\n$/; $l++ if /\n$/; END { print "$ARGV CRLF $c LF $l\n" }' $f; done
```

Expected: `Core/Src/Preset/presetManager.c CRLF 3221 LF 3221` and `Core/Inc/Preset/PresetManager.h CRLF 123 LF 123`.

- [ ] **Step 5: Run the test and see it pass**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build build/host-tests --target test_presets >/dev/null && build/host-tests/test_presets presets | tail -2`

Expected:

```
PASS presets_morph_busy_while_a_morph_runs
18 of 18 tests passed
```

- [ ] **Step 6: Compare the release image with the base build**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake --build --preset release > build/after.log 2>&1 && cmp build/base/LXR_V2_STM32F765.bin build/release/LXR_V2_STM32F765.bin && echo BIN_IDENTICAL`

Expected: `BIN_IDENTICAL`.

- [ ] **Step 7: Compile the test as CI's GCC would (strict C11)**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && arm-none-eabi-gcc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 "-DcurrentFolder=(*preset_folderBuf)" "-DmainParameters=(*preset_mainTable)" "-DsavefileNames=(*preset_namesTable)" -Itests/stubs/presets -Itests/stubs -Itests -isystem Core/Inc -isystem Middlewares/Third_Party/FatFs/src -isystem FATFS/Target -c -o /dev/null tests/test_presets.c && echo GCC_OK`

Expected: `GCC_OK`.

- [ ] **Step 8: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `out of 35` with 100 % passed, the image check OK, `== compiler warnings: 0`, nine `PASS` lines.

- [ ] **Step 9: Commit**

```bash
cd /Users/tk/Code/lxr && git add Core/Src/Preset/presetManager.c Core/Inc/Preset/PresetManager.h tests/test_presets.c && git commit -F - <<'EOF'
Preset manager: preset_morphBusy() (E5.2 F10)

uint8_t preset_morphBusy(void) returns morph_state, the file-static flag
that is 1 while preset_morph_tick() still sends the parameters of a
non-blocking morph or kit transfer. The renderer waits for it before its
t0 (stage 1b spec §5.3, M17). Nothing in the firmware calls it, and
--gc-sections drops it: the release .bin is byte-identical with the base
build.

Test: test_presets starts a morph and counts one tick per sound parameter
until the flag is 0.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task R1a.12: Stage 1's T-tests against a strict copy of the timing core

§8.2, §12.1. Stage 1's host tests build `seq_core.c` with the device's `-ffast-math -freciprocal-math`; the emulator builds the firmware strictly. So `tests/` builds the core once more with `-fno-fast-math -ffp-contract=off`, the §8.2 flags that GCC also accepts, plus `-fno-strict-float-cast-overflow` only for Clang (GCC rejects it, and CI builds `tests/` with GCC on Linux). T1–T20 run against that copy as the ctest ids `<id>_strict`. T19 needs its 16-entry queue, so it gets a strict copy of that core too. The night's measurement: all T-tests pass with the cores built strict.

**Files:**
- Modify: `tests/CMakeLists.txt:94` (two strict cores after it), `:96-124` (`seqc_add_test()` with `STRICT_CORE`), `:133-143` (the T-test lines)

- [ ] **Step 1: See that no strict test exists**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && ctest --test-dir build/host-tests -R '_strict$'`

Expected: `No tests were found!!!`

- [ ] **Step 2: Add the strict cores**

In `tests/CMakeLists.txt`, after line 94 (`target_link_libraries(seq_core_queue_test PUBLIC seqc_options)`), add:

```cmake

# The strict copies of the core (stage 1b, emulator spec §8.2). The emulator builds
# the firmware without -ffast-math and without fused multiply-adds, and with
# Clang's saturating float-to-int casts. Stage 1's T-tests run against these copies
# too, as the ctest ids <id>_strict; T19 needs its 16-entry queue here as well.
# GCC, which builds tests/ on Linux in CI, rejects -fno-strict-float-cast-overflow.
set(SEQC_STRICT_FP_OPTIONS -fno-fast-math -ffp-contract=off)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    list(APPEND SEQC_STRICT_FP_OPTIONS -fno-strict-float-cast-overflow)
endif()
add_library(seq_core_strict OBJECT "${SEQC_CORE_SOURCE}")
target_include_directories(seq_core_strict PUBLIC "${LXR_ROOT}/Core/Inc")
target_compile_options(seq_core_strict PRIVATE ${SEQC_STRICT_FP_OPTIONS})
target_link_libraries(seq_core_strict PUBLIC seqc_options)

add_library(seq_core_strict_small_queue OBJECT "${SEQC_CORE_SOURCE}")
target_include_directories(seq_core_strict_small_queue PUBLIC "${LXR_ROOT}/Core/Inc")
target_compile_definitions(seq_core_strict_small_queue PRIVATE SEQC_QUEUE_CAPACITY=16)
target_compile_options(seq_core_strict_small_queue PRIVATE ${SEQC_STRICT_FP_OPTIONS})
target_link_libraries(seq_core_strict_small_queue PUBLIC seqc_options)
```

- [ ] **Step 3: Give `seqc_add_test()` the `STRICT_CORE` option**

Replace lines 96–124 (from `# seqc_add_test(<name> IDS <id>... [EXTRA <source>...] [CORE <library>])` to `endfunction()`) with:

```cmake
# seqc_add_test(<name> IDS <id>... [EXTRA <source>...] [CORE <library>] [STRICT_CORE <library>])
#
# Builds <name>.c (plus the EXTRA sources) with the fake host into the executable
# <name>, and registers one ctest test per id: ctest runs "<name> <id>", which
# runs the tests named <id> and <id>_... (seqc_test.h). The ids are the test ids
# of the spec (§13.1). CORE picks the core library (default seq_core). Until a
# test file exists, its line here only prints a note; with SEQC_REQUIRE_ALL_TESTS
# a missing file is an error. STRICT_CORE builds <name> once more as
# <name>_strict, against that library, and registers "<name>_strict <id>" as the
# ctest test <id>_strict.
function(seqc_add_test name)
    cmake_parse_arguments(ARG "" "CORE;STRICT_CORE" "IDS;EXTRA" ${ARGN})
    if(NOT ARG_CORE)
        set(ARG_CORE seq_core)
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}.c")
        if(SEQC_REQUIRE_ALL_TESTS)
            message(FATAL_ERROR "tests/${name}.c is missing (${ARG_IDS})")
        endif()
        message(STATUS "host tests: tests/${name}.c not written yet (${ARG_IDS})")
        return()
    endif()
    add_executable(${name} ${name}.c ${ARG_EXTRA})
    # Both, directly: CMake links the objects of an OBJECT library only into
    # targets that name it, not through another OBJECT library.
    target_link_libraries(${name} PRIVATE seqc_fake_host ${ARG_CORE})
    foreach(id IN LISTS ARG_IDS)
        add_test(NAME ${id} COMMAND ${name} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT ${SEQC_TEST_TIMEOUT})
    endforeach()
    if(ARG_STRICT_CORE)
        add_executable(${name}_strict ${name}.c ${ARG_EXTRA})
        target_link_libraries(${name}_strict PRIVATE seqc_fake_host ${ARG_STRICT_CORE})
        foreach(id IN LISTS ARG_IDS)
            add_test(NAME ${id}_strict COMMAND ${name}_strict ${id})
            set_tests_properties(${id}_strict PROPERTIES TIMEOUT ${SEQC_TEST_TIMEOUT})
        endforeach()
    endif()
endfunction()
```

- [ ] **Step 4: Register the T-tests against the strict cores**

Replace lines 133–143 (from `seqc_add_test(test_clock …` to `seqc_add_test(test_fw17 …`) with:

```cmake
seqc_add_test(test_clock     IDS T1 STRICT_CORE seq_core_strict)                 # E1.3  master clock accuracy
seqc_add_test(test_tracks    IDS T10 STRICT_CORE seq_core_strict)                # E1.4  track counters, master wrap, pattern end
seqc_add_test(test_generate  IDS T2 T3 T4 T8 STRICT_CORE seq_core_strict)        # E1.5  grid, swing, shift, ratchet, probability
seqc_add_test(test_fire      IDS T7 T9 T15 STRICT_CORE seq_core_strict)          # E1.6  choke, mutes, note-off
seqc_add_test(test_lookahead IDS T11 T12 STRICT_CORE seq_core_strict)            # E1.7  pattern change, edits before the boundary
seqc_add_test(test_clockout  IDS T5 T6 STRICT_CORE seq_core_strict)              # E1.8  MIDI clock out, clock out pulses
seqc_add_test(test_roll      IDS T13 T14 STRICT_CORE seq_core_strict)            # E1.9  roll, recording phase, skip-once
seqc_add_test(test_slave     IDS T16 T18 STRICT_CORE seq_core_strict)            # E1.10 MIDI and jack slave, tempo estimate
seqc_add_test(test_transport IDS T17 STRICT_CORE seq_core_strict)                # E1.11 Start, Continue, Stop, SPP, song mode
seqc_add_test(test_overflow  IDS T19 EXTRA queue_equiv.c CORE seq_core_small_queue STRICT_CORE seq_core_strict_small_queue)  # E1.12 queue overflow, stats
seqc_add_test(test_fw17      IDS T20 EXTRA fw17_adapter.c STRICT_CORE seq_core_strict)  # E1.13 fw 1.7 replica (reference/)
```

- [ ] **Step 5: Build, and check the strict cores' flags**

Run:

```bash
cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && cmake -S tests -B build/host-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build build/host-tests > build/strict.log 2>&1 && python3 - <<'EOF'
import json
for e in json.load(open("build/host-tests/compile_commands.json")):
    words = e["command"].split()
    if any("/seq_core_strict" in w for w in words) and e["file"].endswith("seq_core.c"):
        target = e["command"].split("CMakeFiles/")[1].split(".dir/")[0]
        print(target, [f for f in ("-ffast-math", "-freciprocal-math", "-fno-fast-math",
                                   "-ffp-contract=off", "-fno-strict-float-cast-overflow") if f in words])
EOF
```

Expected with Apple clang:

```
seq_core_strict ['-fno-fast-math', '-ffp-contract=off', '-fno-strict-float-cast-overflow']
seq_core_strict_small_queue ['-fno-fast-math', '-ffp-contract=off', '-fno-strict-float-cast-overflow']
```

Neither `-ffast-math` nor `-freciprocal-math` may appear.

- [ ] **Step 6: Run the strict tests and see them pass**

Run: `cd /Users/tk/Code/lxr && source tools/env.sh >/dev/null && ctest --test-dir build/host-tests -R '_strict$' | tail -3`

Expected: `100% tests passed, 0 tests failed out of 20`.

- [ ] **Step 7: Run the gate**

Run: `cd /Users/tk/Code/lxr && tools/check.sh && tools/fw_checks.sh`

Expected: `100% tests passed, 0 tests failed out of 55`, the image check OK, `== compiler warnings: 0`, nine `PASS` lines. The firmware does not change.

- [ ] **Step 8: Commit**

```bash
cd /Users/tk/Code/lxr && git add tests/CMakeLists.txt && git commit -F - <<'EOF'
Host tests: the T-tests also run against a strict copy of the core (E5.2)

tests/ builds seq_core.c once more with -fno-fast-math -ffp-contract=off,
and -fno-strict-float-cast-overflow when the compiler is Clang (GCC, which
builds tests/ in CI, rejects it): the floating-point model under which the
emulator builds the firmware (stage 1b spec §8.2, §12.1). seqc_add_test()
gets a STRICT_CORE option, and T1-T20 run against the strict copy as the
ctest ids <id>_strict; T19 against a strict copy of its 16-entry core.
55 ctest ids pass.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

## The end state, which part 2 starts from

- Branch `emulator`, local only, 11 commits on `timing-core`: F8, F4, F1, F2, F3, F5, F6, F7, F9, F10, then the strict core. `tools/check.sh` passes 55 ctest ids with 0 warnings.
- The interfaces part 2 uses, with these exact names:
  - `Core/Inc/appMain.h`: `void appMain_boot(void);` and `void appMain_loopOnce(void);`, defined in `Core/Src/appMain.c`. The emulator passes `FIRMWARE_NAME` and `FIRMWARE_VERSION` to `appMain.c`, as `Core/CMakeLists.txt` now does (§9.1).
  - `SEQ_RANDOM()`: `sequencer.c` defines it as `GetRngValue()` only if it is not defined yet (`#ifndef SEQ_RANDOM`, just above `seq_cbRandom()`). A host defines it, and declares its function, before that point: with `-include` of a header (as `test_sequencer` does) or in a header that `sequencer.c` includes. Its first include is `stm32f7xx_hal.h`, which the emulator stubs.
  - `f2u32_sat(float)` in `Core/Inc/Soundengine/DSPAudio/f2u32.h`: F6's saturating conversion. On a non-Arm host it is the C version. `tests/test_f2u32.c` already checks it against `FCVTZU`, which is M1's F6 part.
  - `Core/sources.cmake`: `LXR_SOURCES_PLATFORM`, `LXR_SOURCES_IO`, `LXR_SOURCES_UI`, `LXR_SOURCES_PRESET`, `LXR_SOURCES_DSP`, `LXR_SOURCES_ENGINE`, `LXR_SOURCES_MIDI`, `LXR_SOURCES_SEQUENCER`, `LXR_SOURCES_USB_APP`. The paths are relative to `Core/` (`Src/…`, `Startup/…`), so `emu/CMakeLists.txt` prefixes them with the `Core` directory. `LXR_SOURCES_PLATFORM` also holds `Startup/startup_stm32f765vgtx.s` and `Src/main.c`, and `Src/appMain.c` right after `Src/main.c`. The lists hold 79 C files.
  - `uint8_t preset_morphBusy(void)` in `Core/Inc/Preset/PresetManager.h`.
- M15 (part 2) still checks F1, F2, F3 and F9 in the emulator. F2 has no other test.

## Points for the owner and for part 2

- **F9 and the rect waveform.** §7 says F9 changes the device's output only in band 10. The rect table's rows start with different values (rows 0–10: 0, 0, −1, 0, 3, 10, 38, 136, 396, 2879, 338), so the rect waveform changes in the used rows below band 10 too: once per period, at the last sample of the period, weighted by its fraction. That is at most 7 LSB in row 4 (every pitch below 440 Hz) and 260, 2483 and 2541 LSB in rows 7, 8 and 9 (1.76–3.5, 3.5–7 and 7–14 kHz). U8's listening check names saw and triangle only. It should also play a rect oscillator in rows 7–9, and §7's F9 row should say so.
- **F7's "other buffers".** Only `dma_buffer` and `dma_buffer2` are known. More buffers can show up only when the emulator runs under UBSan, and each gets its own commit then.
