# LICHOMAT Stage 1b, R1 part 2: the virtual device — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `emu/`, the virtual device of spec §3–§4: the stub headers, the `main.h` overlay, the clock and scheduler with the tier hook, the register, codec, LCD, panel, MIDI wire, USB, jack, random and card models, and the vendored libm. The whole firmware boots and runs main-loop passes in host tests, and ctest in `emu/` covers M1–M13, M15 and M19–M22 of spec §12.1.

**Architecture:** `emu/` is a CMake project of its own, built with Apple clang for arm64 (§9.2). It compiles the 70 firmware files of `Core/sources.cmake` (69 of `Core/Src` plus `appMain.c`, §3.4) into one OBJECT library with the strict flags of §8.2, the stub directory first on the include path and `-ivfsoverlay` for `main.h`. The core (`emu/core`, C11, `-Wall -Wextra -Werror`, with its POSIX half `emu_hostfs.c` apart) and Arm Optimized Routines' five libm functions are more OBJECT libraries. Each test executable links all of them, so pure tests (the core with fake firmware entries) and firmware tests (a real power-on) share one build. A firmware test powers on once per process, so ctest runs every `TEST()` as its own process. The starting material is an unreviewed draft (`build/r1b-draft/`); each task copies its files, applies the listed corrections and adds the tests.

**Tech Stack:** C (gnu11 for the firmware objects, C11 for the core and tests) with Apple clang 17 from the Command Line Tools, CMake 3.31 and Ninja, ctest, UBSan, Python 3 for the drift and licence scripts, Arm Optimized Routines v23.01 (MIT), git 2.39.

---

The spec is `docs/superpowers/specs/2026-10-27-emulator-design.md`, accepted with the defaults of Q12–Q19. A `§` below is a section of it. Part 1 is `docs/superpowers/plans/2026-10-27-emulator-r1a-firmware-changes.md`; its section "The end state, which part 2 starts from" lists the interfaces used here (`appMain_boot()`, `appMain_loopOnce()`, `SEQ_RANDOM()`, `f2u32_sat()`, `Core/sources.cmake`, `preset_morphBusy()`).

## Rules for every task

- **Where.** Work in the worktree `/Users/tk/Code/lxr/build/wt-emulator`, branch `emulator`, head `0273b17` at the start. Every Bash call starts in a new shell: begin each command with `cd /Users/tk/Code/lxr/build/wt-emulator &&`, and add `source tools/env.sh >/dev/null &&` where it needs `cmake`, `ctest` or `ninja`.
- **The draft** is `/Users/tk/Code/lxr/build/r1b-draft/emu/` (written `$DRAFT/` below; it is not a shell variable, write the full path). Copy from it; never edit it. It compiles: in a check for this plan all 70 firmware files compiled against its stubs, its core compiled with `-Wall -Wextra -Werror`, and a boot without a card linked and ran. Its `CMakeLists.txt` stops after the source lists, `libm/` is empty, and it has tests for M1–M5 only.
- **Part 2 does not touch `Core/`, `tests/` or `tools/`.** No task needs to. A firmware bug found here gets its own task in a later plan, after the controller asks the owner (§7, "Further sanitizer findings").
- **Files.** New files carry the GPL-3.0-only header of the draft files (`Copyright (C) 2026 Tomasz Krzywicki`, the SPDX line, the NOTICE.md sentence), except the vendored libm. Everything under `emu/` uses LF.
- **Sanitizers.** The Debug default is `LICHOMAT_EMU_SANITIZE=undefined`. Never configure `address` locally: ASan binaries hang in this sandbox. ASan runs in CI (R3).
- **Git.** `git add` names each file; never `git add -A` (the worktree shows `.toolchain` as untracked). Never `--amend`, never push. Each commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Test names.** ctest runs `<binary> <TEST name>`, and the filter of `tests/seqc_test.h` also runs every test named `<name>_…`. So no `TEST()` name may equal another's name plus `_` and more.

**EMU-CHECK**, the command every task runs:

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && cmake -S emu -B build/emu -G Ninja > build/emu.log 2>&1 && cmake --build build/emu >> build/emu.log 2>&1; echo "== emu build: $?"; echo "== emu warnings: $(grep -c 'warning:' build/emu.log)"; ctest --test-dir build/emu 2>&1 | tail -n 3
```

Expected: `== emu build: 0`, `== emu warnings: 0`, then `100% tests passed, 0 tests failed out of N` with the task's N.

**The gate** of every task: EMU-CHECK as expected, then `tools/check.sh` prints `100% tests passed, 0 tests failed out of 55`, the image line with `OK`, `== compiler warnings: 0`, and the same `memory:` line that Task R1b.1 recorded (DTCM + RAM at most 391824 B). The firmware does not change in this plan, so `check.sh` only proves that nothing leaked into its build.

## File structure

| File | Responsibility | Task |
|---|---|---|
| `emu/CMakeLists.txt` | The project: arch and compiler checks, options, flags, the libraries, `emu_add_test()`, the ctest entries | 1, grows in every task |
| `emu/cmake/LxrSources.cmake` | Reads `Core/sources.cmake`; the exclusion list; M22's errors | 1 |
| `emu/stubs/*.h` | The HAL and CMSIS, USB, FatFs glue and LCD stand-ins; `emu_seq_random.h` | 2 |
| `emu/overlay/main.h`, `main_overlay.yaml.in` | The fresh `main.h` and the overlay that maps `Core/Inc/main.h` to it | 2 |
| `emu/sanitize/pwm_ignorelist.txt` | UBSan's bounds check off in the two PWM oscillators (B13, B14) | 2 |
| `emu/libm/` | AOR v23.01: 13 sources, `LICENSE`, `SHA256SUMS` | 3 |
| `emu/core/emu_core.h`, `emu_internal.h`, `emu_*.c` | The virtual device | 3, fixes in 5, 11 (if the knob work-up needs one), 14 |
| `emu/tests/emu_test.h` | Helpers: core power-on, boot, temp cards, sinks, LCD text | 3, grows |
| `emu/tests/test_*.c`, `libm_canary.c`, `check_*.{sh,cmake}`, `drift.py`, `drift.json`, `licence_gate.py`, `name_allow.txt` | The host tests | 1, 3–16 |

## Task order

The core models call each other through `emu_powerOn()` and the firmware calls all of them, so the core is copied whole in Task 3 and each later task tests and corrects one model. The licence gate needs a binary that links the firmware, so it follows the boot. Changes against the order the controller proposed: M1 and M13 move to Task 3 (they need nothing but the stubs and `emu_random.c`, and give the harness its first users); M19 moves to Task 3, because its symbol check needs the whole core; the output-jack part of M8 joins the sync jacks (M12) in Task 15, both being audio and jack routing.

| Task | Contents | M-rows | ctest ids after it |
|---|---|---|---|
| R1b.1 | Skeleton, source lists | M22 | 3 |
| R1b.2 | Stubs, overlay, firmware objects | — | 3 |
| R1b.3 | Core, libm, harness | M1, M13, M19 | 14 |
| R1b.4 | Drift | M20 | 15 |
| R1b.5 | Clock, scheduler, TIM2 | M2, M3 (core), M4 (core) | 36 |
| R1b.6 | Codec, the paced hook | M5 (core), M3 (hook) | 41 |
| R1b.7 | Boot | M6, M3 (codec start), M5 (firmware) | 45 |
| R1b.8 | Long firmware calls | M3 (firmware), M4 (firmware) | 50 |
| R1b.9 | Licence gate | M21 | 52 |
| R1b.10 | LCD, LEDs | M7 | 60 |
| R1b.11 | Panel input | M8 (without the output jacks) | 68 |
| R1b.12 | Card backend | M9 | 77 |
| R1b.13 | DIN | M10 | 84 |
| R1b.14 | USB | M11 | 89 |
| R1b.15 | Output and sync jacks | M8 (output jacks), M12 | 94 |
| R1b.16 | Firmware changes in the emulator | M15 | 98 |

---

### Task R1b.1: The skeleton and the source lists (M22)

§9.1, §9.2, §12.1 M22. `emu/` configures on its own, reads `Core/sources.cmake`, and fails the configure step on any listed file that is neither compiled nor excluded.

**Files:**
- Create: `emu/CMakeLists.txt`, `emu/cmake/LxrSources.cmake`, `emu/tests/check_sources.cmake`

**Start from:** `cp $DRAFT/CMakeLists.txt emu/`, `cp $DRAFT/cmake/LxrSources.cmake emu/cmake/`, `cp $DRAFT/tests/check_sources.cmake emu/tests/`.

**Change against the draft:**
- `emu/CMakeLists.txt` ends after the version message. Append the tests (M22). The negative modes must fail for their own reason, so they match the message instead of `WILL_FAIL`, which would also pass on a typo:

```cmake

# --- host tests (spec 12.1) -------------------------------------------------------
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# M22: every file in Core/sources.cmake is compiled or excluded; an extra header and
# a dropped exclusion must both be found
foreach(mode real extra stale)
    add_test(NAME M22_${mode} COMMAND ${CMAKE_COMMAND} -DLXR_ROOT=${LXR_ROOT} -DMODE=${mode}
        -DWORK=${CMAKE_BINARY_DIR}/m22 -P ${EMU_DIR}/tests/check_sources.cmake)
endforeach()
set_tests_properties(M22_extra PROPERTIES PASS_REGULAR_EXPRESSION "config.h \\(LXR_SOURCES_UI\\) is neither compiled nor excluded")
set_tests_properties(M22_stale PROPERTIES PASS_REGULAR_EXPRESSION "the exclusion .*/Core/Src/lcd.c is in no list")
```

- `check_sources.cmake` is right as it is (70 files, the three modes).

- [ ] **Step 1: Record the start state.** Run `cd /Users/tk/Code/lxr/build/wt-emulator && git status --short && git log --oneline -1 && tools/check.sh | grep -E 'tests passed|^memory:|image:|warnings'`. Expected: only `?? .toolchain`, `0273b17 …`, `100% tests passed, 0 tests failed out of 55`, a `memory:` line (write it into the task report; later tasks compare with it), the image line with `OK`, `== compiler warnings: 0`.
- [ ] **Step 2: Copy the three files and append the block above.**
- [ ] **Step 3: Configure.** Run `cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && cmake -S emu -B build/emu -G Ninja | grep emulator:`. Expected: `-- emulator: 70 firmware files` and `-- emulator: LICHOMAT 0.1`.
- [ ] **Step 4: See M22 catch a stray file and a dropped exclusion.** `Core/` stays untouched: the negative modes write their own lists into `build/emu/m22/`. Run `cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && ctest --test-dir build/emu -R M22 -V | grep 'M22:'`. Expected among the lines: `M22: /…/Core/Inc/config.h (LXR_SOURCES_UI) is neither compiled nor excluded`, `M22: the exclusion /…/Core/Src/lcd.c is in no list`, and `M22: 70 files compiled, real lists consistent`. If the path form differs, adjust the two regular expressions to the printed text.
- [ ] **Step 5: EMU-CHECK.** N = 3.
- [ ] **Step 6: The gate.**
- [ ] **Step 7: Commit.**

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && git add emu/CMakeLists.txt emu/cmake/LxrSources.cmake emu/tests/check_sources.cmake && git commit -F - <<'EOF'
Emulator: the CMake project and the shared source lists (E5.2 R1b, M22)

emu/ is a CMake project of its own for Apple clang on arm64 (stage 1b spec
9.2). It reads the module lists of Core/sources.cmake and compiles every
listed C file except the ten the virtual device replaces (spec 3.4): 69
files of Core/Src and appMain.c. A listed file that is neither compiled nor
excluded, or an exclusion that no list holds, fails the configure step.
M22 checks the real lists and both failures.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task R1b.2: The stubs, the overlay and the firmware objects

§3.4, §8.2, §9.2. The 70 firmware files compile into one OBJECT library with the strict flags.

**Files:**
- Create: `emu/stubs/` (9 headers), `emu/overlay/main.h`, `emu/overlay/main_overlay.yaml.in`, `emu/sanitize/pwm_ignorelist.txt`
- Modify: `emu/CMakeLists.txt`

**Start from:** `cp $DRAFT/stubs/*.h emu/stubs/`, `cp $DRAFT/overlay/main.h $DRAFT/overlay/main_overlay.yaml.in emu/overlay/`. The ignorelist is new.

**Change against the draft:**
- `emu/overlay/main.h`, header comment: replace "The firmware's main.h carries ST's SLA0044 notice" with "The firmware's main.h carries ST's licence notice". The licence gate (Task 9) fails on any compiled file that names SLA0044 (§12.1 M21).
- **B13 and B14** (tracker Q13). `calcPwmFmBlock()` and `calcPwmBlock()` read `sawTable[row][itg]` and `sawTable[row][itg2]` with an index of 1024 (`Oscillator.c:207`, `:213`, `:687`, `:693`). The default kit reaches them in the first second after a boot without a card, so with UBSan's bounds check every firmware test would stop there (seen in a check for this plan). The owner decides them with the F9 listening check (U8), so the emulator keeps them and turns off only that check in those two functions. ASan in CI still sees a read past the table's end in row 10. Write `emu/sanitize/pwm_ignorelist.txt`:

```
# UBSan's array-bounds check off in the two PWM oscillators of Oscillator.c,
# which read sawTable[row][1024] (B13, B14 of the sanitizer sweep). They wait for
# the owner's listening check with F9 (stage 1b spec 7, U8; tracker Q13).
# Every other function of the file keeps the check, F9's two sites included.
[array-bounds]
fun:calcPwmFmBlock
fun:calcPwmBlock
```

- Append to `emu/CMakeLists.txt`, before the tests block:

```cmake
# --- flags (spec 8.2) -----------------------------------------------------------------
set(EMU_FP_FLAGS -ffp-contract=off -fno-fast-math -fno-strict-float-cast-overflow
    -fno-builtin-sinf -fno-builtin-cosf -fno-builtin-expf -fno-builtin-exp -fno-builtin-logf
    -funsigned-char -fno-strict-aliasing)
if(LICHOMAT_EMU_FP STREQUAL "firmware-like")
    list(APPEND EMU_FP_FLAGS -freciprocal-math)
endif()
# A sanitizer report is a firmware fault (spec 11), so none recovers.
set(EMU_SAN_FLAGS "")
if(LICHOMAT_EMU_SANITIZE)
    set(EMU_SAN_FLAGS -fsanitize=${LICHOMAT_EMU_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${LICHOMAT_EMU_SANITIZE})
endif()
message(STATUS "emulator: ${CMAKE_BUILD_TYPE}, sanitizers '${LICHOMAT_EMU_SANITIZE}', fp ${LICHOMAT_EMU_FP}")

# Core/Inc/main.h -> emu/overlay/main.h (spec 3.4)
configure_file(overlay/main_overlay.yaml.in "${CMAKE_BINARY_DIR}/main_overlay.yaml" @ONLY)
set(EMU_OVERLAY "${CMAKE_BINARY_DIR}/main_overlay.yaml")

# What every object that includes a firmware header needs: the stubs before Core/Inc,
# the overlay, the flags, and no TCM sections (Mach-O rejects them, spec 3.4).
add_library(emu_fw_headers INTERFACE)
target_include_directories(emu_fw_headers BEFORE INTERFACE "${EMU_DIR}/stubs")
target_include_directories(emu_fw_headers SYSTEM INTERFACE
    "${LXR_ROOT}/Core/Inc" "${LXR_ROOT}/Middlewares/Third_Party/FatFs/src")
target_compile_options(emu_fw_headers INTERFACE ${EMU_FP_FLAGS} ${EMU_SAN_FLAGS}
    "-Dsection(x)=unused" "SHELL:-ivfsoverlay ${EMU_OVERLAY}")

# --- the firmware objects (spec 3.4, 9.2) ------------------------------------------------
add_library(lxr_firmware OBJECT ${EMU_FW_SOURCES})
target_link_libraries(lxr_firmware PRIVATE emu_fw_headers)
target_compile_options(lxr_firmware PRIVATE -w)	# the firmware build is their warning gate
if(LICHOMAT_EMU_SANITIZE)
    # ASan and UBSan do not see reads of uninitialised locals (spec 9.2)
    target_compile_options(lxr_firmware PRIVATE -ftrivial-auto-var-init=pattern)
endif()
set(EMU_SRC "${LXR_ROOT}/Core/Src")
# the splash shows the firmware's own name and version (spec 9.1)
set_source_files_properties("${EMU_SRC}/appMain.c" PROPERTIES COMPILE_DEFINITIONS
    "FIRMWARE_NAME=\"${EMU_FIRMWARE_NAME}\";FIRMWARE_VERSION=\"${EMU_FIRMWARE_VERSION}\"")
# the probability draw takes the sequencer stream (spec 4.9, firmware change F5)
set_source_files_properties("${EMU_SRC}/Soundengine/Sequencer/sequencer.c" PROPERTIES
    COMPILE_OPTIONS "SHELL:-include ${EMU_DIR}/stubs/emu_seq_random.h")
if(LICHOMAT_EMU_SANITIZE MATCHES "undefined")
    # arm64 gives x % 0 the device's result, x (EuklidGenerator.c:235-236, spec 8.1, 9.2)
    set_source_files_properties("${EMU_SRC}/Soundengine/Sequencer/EuklidGenerator.c" PROPERTIES
        COMPILE_OPTIONS -fno-sanitize=integer-divide-by-zero)
    # B13, B14: see sanitize/pwm_ignorelist.txt
    set_source_files_properties("${EMU_SRC}/Soundengine/DSPAudio/Oscillator.c" PROPERTIES
        COMPILE_OPTIONS "-fsanitize-ignorelist=${EMU_DIR}/sanitize/pwm_ignorelist.txt")
endif()
```

- [ ] **Step 1: Copy the files, write the ignorelist, edit the overlay comment, append the block.**
- [ ] **Step 2: Build the firmware objects.** Run `cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && cmake -S emu -B build/emu -G Ninja >/dev/null && cmake --build build/emu --target lxr_firmware 2>&1 | tail -1 && find build/emu/CMakeFiles/lxr_firmware.dir -name '*.o' | wc -l && find build/emu/CMakeFiles/lxr_firmware.dir -iname 'distortion.c.o' | wc -l`. Expected: a `Linking`-free last line (`[70/70] Building C object …`), `70`, `2` (object paths mirror source paths, §3.4: `DSPAudio/distortion.c` and `DSPAudio/FX/Distortion.c`, also `IO/uart.c` and `MIDI/Uart.c`).
- [ ] **Step 3: Check the flags.** Run:

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && python3 - <<'EOF'
import json
cc = json.load(open("build/emu/compile_commands.json"))
fw = [c for c in cc if "/Core/Src/" in c["file"]]
need = ["-ffp-contract=off", "-fno-strict-float-cast-overflow", "-funsigned-char", "-w",
        "-ivfsoverlay", "-ftrivial-auto-var-init=pattern", "-fsanitize=undefined"]
print(len(fw), all(all(n in c["command"] for n in need) for c in fw))
seq = [c for c in fw if c["file"].endswith("sequencer.c")][0]["command"]
app = [c for c in fw if c["file"].endswith("appMain.c")][0]["command"]
print("emu_seq_random.h" in seq, 'FIRMWARE_NAME=\\"LICHOMAT\\"' in app)
EOF
```

Expected: `70 True`, then `True True`.
- [ ] **Step 4: EMU-CHECK.** N = 3.
- [ ] **Step 5: The gate.**
- [ ] **Step 6: Commit** `emu/CMakeLists.txt`, the 9 stub headers by name (`stm32f7xx_hal.h stm32f7xx.h usbd_def.h usbd_cdc.h usbd_cdc_if.h fatfs.h ffconf.h lcd.h emu_seq_random.h`), the two overlay files and the ignorelist, with the message:

```
Emulator: stub headers, the main.h overlay and the firmware objects (E5.2 R1b)

The 70 firmware files compile unchanged on the host (stage 1b spec 3.4):
emu/stubs comes first on the include path, Core/Inc/main.h is replaced by a
fresh header through clang's -ivfsoverlay, and -Dsection(x)=unused drops the
TCM sections. The objects get the strict floating-point flags of spec 8.2,
F5's SEQ_RANDOM() hook in sequencer.c and the splash name and version in
appMain.c. A sanitizer build adds pattern-initialised locals. Two per-file
sanitizer exceptions carry their reasons: EuklidGenerator.c's x % 0, and the
bounds check in the PWM oscillators, whose read of sawTable[row][1024]
(B13, B14) waits for the owner's listening check with F9.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

---

### Task R1b.3: The core, the vendored libm and the test harness (M1, M13, M19)

§3.3, §4.9, §8.1, §8.3, §12.1 M1, M13, M19.

**Files:**
- Create: `emu/core/` (15 files: `emu_core.h`, `emu_internal.h`, `emu_hostfs.h` and 12 `.c` files), `emu/libm/` (13 sources, `LICENSE`, `SHA256SUMS`), `emu/tests/emu_test.h`, `emu/tests/test_intrinsics.c`, `emu/tests/test_random.c`, `emu/tests/libm_canary.c`, `emu/tests/check_libm.sh`
- Modify: `emu/CMakeLists.txt`

**Start from:** `cp $DRAFT/core/* emu/core/`, `cp $DRAFT/tests/{emu_test.h,test_intrinsics.c,libm_canary.c,check_libm.sh} emu/tests/`. `test_random.c` and the libm files are new.

**The libm (§8.1).** Arm Optimized Routines at tag `v23.01`, commit `56e3bf05c19c4e28e1f5edd9093c712f16c5c32a`, `math/`. Its `LICENSE` and SPDX lines say `MIT OR Apache-2.0 WITH LLVM-exception`; the emulator takes it under MIT, as §10.1 lists it. Fetching it is the plan's only network access. Write `emu/libm/SHA256SUMS`:

```
650afbf29f214451e02241adc42534e82c9d6ae2b38e2444b92b5a1ffcaf9346  LICENSE
8fcde1b1c5f64f7a624b398639b33215f5e76c1ab5be55e3d26b011e51c3bfb1  math_config.h
202a5f0b4983da16567bf707b8da02f9c1aa84bfaf1e9e2931009153b4a86676  math_err.c
2c5d68148296649b10743b2fec80bf4234fbb14e8d00a05a2dc0ba25930fedb8  math_errf.c
91f93f192850de64845bee4d7362d7ee1c8f8bad0086f99983bbcc595014e5a7  sincosf.h
9887da2fb996658a88e98ec7ea5142be2b4a8db5650f7fd3290228d67d405cdb  sincosf_data.c
1311271d66f5575a76fc0b40f50646982e99c2f2adba2a26be7c3fa67bf76b76  sinf.c
d416d8f2baf463605ceddd3bc97ce52102bd393391ab9caae263be3782d949b7  cosf.c
68db844370d18d47413009df553fb8bfd73f73bdf0e25fa6e71493c22d00c60d  expf.c
b8974df5886979f7bdd0fd83b0b7453786b9f715d36e593174ab94f0f9438d71  exp2f_data.c
2234cb61aa91b1baeb3c197c245f0b14e7aad7faf4444a5785f7a93effa9a754  exp.c
6c93f800d0d3fdbbc8a1c3a623043800ffc3cd9306c6cfc98351e66be75799a3  exp_data.c
cfcb2b76731f6b1416c0105fd9720b2cec48b858db19749b1799ed5955873a61  logf.c
f974460c11346573572735293e0f6e8e80c9097e60dbc1320291c7959ea1a909  logf_data.c
```

The sources need `-DUSE_GLIBC_ABI=0`: their glibc aliases (`strong_alias (expf, __expf_finite)`) are an error on Darwin ("aliases are not supported on darwin", seen in a check for this plan). With it, the 11 `.c` files compile with `-Wall -Wextra -Werror` and define `_sinf _cosf _expf _exp _logf` plus their `__math_*` helpers and tables.

**Change against the draft:**
- `emu_core.h`, `emu_config_t`: add, after `faders`, the knob angles at power-on (Task 11's positive control for "no edit at boot"; §4.10 keeps 0° as the default):

```c
	int knobs[4];			/**< knob angles at power-on in degrees; 0 (spec 4.10) unless a test sets them */
```

  and in `emu_panel.c`, `emu_panelReset()`, before the knob loop: `for (unsigned k = 0; k < 4u; k++) emu_panel.angle[k] = emu_panel.target[k] = cfg->knobs[k];`.
- `emu_firmware.c`, `emu_powerOn()`: the firmware's static state survives only a new process (§3.3), so a second power-on is a fault. Add at its top:

```c
	static int poweredOn;
	if (poweredOn)
		emu_fault("emu_powerOn: once per process; the firmware's state survives only a new process");
	poweredOn = 1;
```

- `test_intrinsics.c`: add M1's F6 part, F6's conversion against `FCVTZU` (§12.1 M1; `tests/test_f2u32.c` checks it in the firmware's host tests, this repeats it under the emulator's flags):

```c
#include <math.h>
#include "Soundengine/DSPAudio/f2u32.h"

static uint32_t fcvtzu(float x)
{
	uint32_t r;
	__asm__("fcvtzu %w0, %s1" : "=r"(r) : "w"(x));
	return r;
}

TEST(M1_f2u32)
{
	const float v[] = { 0.0f, -0.0f, 0.5f, 1.0f, 1.99f, -1.0f, -1e30f, 4294967040.0f, 4294967296.0f,
		5e9f, 1e30f, INFINITY, -INFINITY, NAN, 3.0e9f, 2147483648.0f };
	for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
		ASSERT_EQ(f2u32_sat(v[i]), fcvtzu(v[i]));
	ASSERT_EQ(f2u32_sat(NAN), 0);
	ASSERT_EQ(f2u32_sat(5e9f), 4294967295u);
}
```

- `emu_test.h`: keep the draft's helpers; Task 7 adds the boot helpers.
- `check_libm.sh`: right as it is (the allow-list matches the imports seen in the check: `fmod atoi bzero memcpy memset strcmp`, the `___*_chk` forms, the stack checks and `___ubsan_*`).
- Append to `emu/CMakeLists.txt`, before the tests block:

```cmake
# --- the vendored libm (spec 8.1): Arm Optimized Routines v23.01 under MIT ---------------
file(GLOB EMU_LIBM_SOURCES CONFIGURE_DEPENDS "${EMU_DIR}/libm/*.c")
add_library(emu_libm OBJECT ${EMU_LIBM_SOURCES})
target_compile_options(emu_libm PRIVATE ${EMU_FP_FLAGS} ${EMU_SAN_FLAGS} -w)
target_compile_definitions(emu_libm PRIVATE USE_GLIBC_ABI=0)	# Mach-O has no symbol aliases

# --- the virtual device (spec 3.3): C11, -Wall -Wextra -Werror ---------------------------
file(GLOB EMU_CORE_SOURCES CONFIGURE_DEPENDS "${EMU_DIR}/core/*.c")
list(REMOVE_ITEM EMU_CORE_SOURCES "${EMU_DIR}/core/emu_hostfs.c")
add_library(emu_core OBJECT ${EMU_CORE_SOURCES})
target_link_libraries(emu_core PRIVATE emu_fw_headers)
target_compile_options(emu_core PRIVATE -Wall -Wextra -Werror)
# The card backend's POSIX half: <dirent.h> clashes with FatFs's DIR (spec 4.8), and it
# includes no firmware header, so it gets no section define.
add_library(emu_hostfs OBJECT "${EMU_DIR}/core/emu_hostfs.c")
target_compile_options(emu_hostfs PRIVATE ${EMU_FP_FLAGS} ${EMU_SAN_FLAGS} -Wall -Wextra -Werror
    "SHELL:-ivfsoverlay ${EMU_OVERLAY}")
set(EMU_DEVICE_OBJECTS $<TARGET_OBJECTS:lxr_firmware> $<TARGET_OBJECTS:emu_core>
    $<TARGET_OBJECTS:emu_hostfs> $<TARGET_OBJECTS:emu_libm>)

# --- the test harness -------------------------------------------------------------------------
add_library(emu_test_options INTERFACE)
target_link_libraries(emu_test_options INTERFACE emu_fw_headers)
target_include_directories(emu_test_options INTERFACE "${EMU_DIR}/core" "${EMU_DIR}/tests" "${LXR_ROOT}/tests")
target_compile_options(emu_test_options INTERFACE -Wall -Wextra -Werror)

# emu_add_test(<name> IDS <test>...): emu/tests/<name>.c with the whole virtual
# device. Each TEST() is a ctest test of its own and runs in its own process: a
# firmware test powers on once per process (spec 3.3).
function(emu_add_test name)
    cmake_parse_arguments(ARG "" "" "IDS" ${ARGN})
    add_executable(${name} tests/${name}.c ${EMU_DEVICE_OBJECTS})
    target_link_libraries(${name} PRIVATE emu_test_options)
    foreach(id IN LISTS ARG_IDS)
        add_test(NAME ${id} COMMAND ${name} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT 300)
    endforeach()
endfunction()
```

- And in the tests block:

```cmake
emu_add_test(test_intrinsics IDS M1_ssat M1_qadd_qsub M1_qadd16_qsub16 M1_pkhbt M1_clz M1_smlald M1_apsr M1_f2u32)
emu_add_test(test_random IDS M13_first_values M13_streams_apart)

# M19: the firmware objects import no libm name but the vendored ones and fmod
add_library(emu_libm_canary OBJECT tests/libm_canary.c)
target_compile_options(emu_libm_canary PRIVATE ${EMU_FP_FLAGS} -w)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/m19/firmware.txt" CONTENT "$<JOIN:$<TARGET_OBJECTS:lxr_firmware>,\n>\n")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/m19/core.txt"
    CONTENT "$<JOIN:$<TARGET_OBJECTS:emu_core>,\n>\n$<JOIN:$<TARGET_OBJECTS:emu_hostfs>,\n>\n")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/m19/libm.txt" CONTENT "$<JOIN:$<TARGET_OBJECTS:emu_libm>,\n>\n")
add_test(NAME M19 COMMAND /bin/bash "${EMU_DIR}/tests/check_libm.sh" "${CMAKE_BINARY_DIR}/m19"
    "${CMAKE_BINARY_DIR}/m19/firmware.txt" "${CMAKE_BINARY_DIR}/m19/core.txt" "${CMAKE_BINARY_DIR}/m19/libm.txt"
    "$<TARGET_OBJECTS:emu_libm_canary>" "$<TARGET_FILE:test_intrinsics>")
set_tests_properties(M19 PROPERTIES PASS_REGULAR_EXPRESSION "M19: [0-9]+ imports, all on the allow-list")
```

**Tests** — new `emu/tests/test_random.c` (M13; the values were computed for this plan from §4.9's definition):

```c
#include "stm32f7xx_hal.h"
#include "emu_seq_random.h"
#include "emu_test.h"

static void seedWith(uint64_t seed)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.seed = seed;
	emu_corePowerOn(&cfg);
	emu_randomReset(&cfg);
}

TEST(M13_first_values)
{
	static const uint32_t dsp1[] = { 0x91D9AC0Cu, 0x2F57BA20u, 0xDA691309u, 0xF901AEADu };
	static const uint32_t seq1[] = { 0x3F9350C9u, 0xE4C11B8Cu, 0x31AFF2D4u, 0x3157B0EEu };
	static const uint32_t seq2[] = { 0xB57708F3u, 0xD9411A27u, 0xBAB56385u, 0xCE7CD407u };
	seedWith(1);
	for (int i = 0; i < 4; i++)
		ASSERT_EQ(HAL_RNG_GetRandomNumber(NULL), dsp1[i]);
	for (int i = 0; i < 4; i++)
		ASSERT_EQ(SEQ_RANDOM(), seq1[i]);
	seedWith(2);	/* the DSP stream of seed 2 starts from SplitMix64(2), as the sequencer stream of seed 1 */
	for (int i = 0; i < 4; i++)
		ASSERT_EQ(HAL_RNG_GetRandomNumber(NULL), seq1[i]);
	for (int i = 0; i < 4; i++)
		ASSERT_EQ(SEQ_RANDOM(), seq2[i]);
}

/* a probability edit draws more sequencer numbers; the noise stays the same */
TEST(M13_streams_apart)
{
	uint32_t plain[64], mixed[64];
	seedWith(1);
	for (int i = 0; i < 64; i++)
		plain[i] = HAL_RNG_GetRandomNumber(NULL);
	seedWith(1);
	for (int i = 0; i < 64; i++)
	{
		for (int k = 0; k < i % 5; k++)
			(void)SEQ_RANDOM();
		mixed[i] = HAL_RNG_GetRandomNumber(NULL);
	}
	ASSERT_EQ(memcmp(plain, mixed, sizeof plain), 0);
}

TEST_MAIN()
```

- [ ] **Step 1: Copy the core and the tests; apply the three changes; write `test_random.c`.**
- [ ] **Step 2: Vendor the libm.** Run:

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && T=$(mktemp -d) && git clone -q --depth 1 --branch v23.01 https://github.com/ARM-software/optimized-routines "$T/aor" && git -C "$T/aor" rev-parse HEAD && mkdir -p emu/libm && cp "$T/aor/LICENSE" emu/libm/ && for f in math_config.h math_err.c math_errf.c sincosf.h sincosf_data.c sinf.c cosf.c expf.c exp2f_data.c exp.c exp_data.c logf.c logf_data.c; do cp "$T/aor/math/$f" emu/libm/; done && rm -rf "$T"
```

Write `SHA256SUMS` as above, then `cd /Users/tk/Code/lxr/build/wt-emulator/emu/libm && shasum -a 256 -c SHA256SUMS | grep -c ': OK$'`. Expected: the commit `56e3bf05c19c4e28e1f5edd9093c712f16c5c32a`, then `14`.
- [ ] **Step 3: See M19 catch a system libm call.** Before adding `emu_libm` to `EMU_DEVICE_OBJECTS`, the canary's names are undefined in the device binary: build once with `$<TARGET_OBJECTS:emu_libm>` left out of the list, run `ctest --test-dir build/emu -R M19 --output-on-failure | grep M19:`, and expect `M19: test_intrinsics does not define _expf` (and `_sinf`, `_cosf`). Put the objects back.
- [ ] **Step 4: EMU-CHECK.** N = 14 (3 + 8 + 2 + 1).
- [ ] **Step 5: The gate.**
- [ ] **Step 6: Commit** the 15 core files, the 15 libm files (13 sources, `LICENSE`, `SHA256SUMS`), the five test files and `emu/CMakeLists.txt` by name:

```
Emulator: the virtual device's core, the vendored libm, M1, M13, M19 (E5.2 R1b)

emu/core is the virtual device of stage 1b spec 3.3, copied from the
unreviewed draft and tested model by model in the following commits. A
second power-on in one process is now a fault, and the configuration gets
the knob angles at power-on (0 degrees by default, spec 4.10).

emu/libm holds sinf, cosf, expf, exp and logf from Arm Optimized Routines
v23.01 (56e3bf0, MIT), with checksums, built with -DUSE_GLIBC_ABI=0 because
Mach-O has no aliases (spec 8.1). Tests: the intrinsics and F6's conversion
against FCVTZU (M1), the two random streams (M13), and the firmware's
imports with the libm canary (M19).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

---

### Task R1b.4: The drift test (M20)

§4.2, §12.1 M20. The hashes of the firmware text that the models re-implement or take their constants from.

**Files:**
- Create: `emu/tests/drift.py`, `emu/tests/drift.json`
- Modify: `emu/CMakeLists.txt`

**Start from:** `cp $DRAFT/tests/drift.py emu/tests/`. `drift.json` is made by `record`.

**Change against the draft:** none in its items; they match M20's list (the OTG_FS priority is at `stm32f7xx_hal_msp.c:844`, checked). Add the test:

```cmake
add_test(NAME M20 COMMAND ${Python3_EXECUTABLE} "${EMU_DIR}/tests/drift.py" check "${LXR_ROOT}")
```

- [ ] **Step 1: Record.** Run `cd /Users/tk/Code/lxr/build/wt-emulator && python3 emu/tests/drift.py record . && python3 emu/tests/drift.py check .`. Expected: `drift: recorded 18 items`, then `drift: 18 items unchanged` (both seen on a scratch copy for this plan).
- [ ] **Step 2: See it catch a change.** Copy the hashed files into a scratch root with one changed character, and check against it:

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && R=$(mktemp -d) && for f in Core/Src/stm32f7xx_it.c Core/Src/stm32f7xx_hal_msp.c Core/Src/main.c Core/Src/Soundengine/cs4344_cs5343.c Middlewares/JS/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c USB_DEVICE_MIDI/App/usbd_cdc_if.c Core/Inc/stm32f7xx_hal_conf.h emu/tests/drift.json; do mkdir -p "$R/$(dirname $f)" && cp "$f" "$R/$f"; done && sed -i '' 's/screensaver_timer++;/screensaver_timer += 1;/' "$R/Core/Src/stm32f7xx_it.c" && python3 emu/tests/drift.py check "$R"; echo "exit $?"; rm -rf "$R"
```

Expected: `drift: SysTick_Handler changed (spec 4.2: check the emulator against it, then record)`, then `exit 1`.
- [ ] **Step 3: EMU-CHECK.** N = 15.
- [ ] **Step 4: The gate.**
- [ ] **Step 5: Commit** `emu/tests/drift.py emu/tests/drift.json emu/CMakeLists.txt`: "Emulator: the drift test of the re-implemented firmware text (E5.2 R1b, M20)", body: the 18 items (interrupt bodies, the I2S callback, the USB-MIDI walk and transmit, the CubeMX rates and priorities), and that a changed hash means: read the change, update the model, then `drift.py record`.

---

### Task R1b.5: The clock and the scheduler (M2, M3 core, M4 core)

§3.1, §3.3, §4.2, §12.1 M2–M4. The draft's scheduler follows §4.2; three gaps can move the clock backwards or loop.

**Files:**
- Create: `emu/tests/test_clock.c`, `emu/tests/test_sched.c`
- Modify: `emu/core/emu_sched.c`, `emu/core/emu_usb.c`, `emu/CMakeLists.txt`

**Start from:** `cp $DRAFT/tests/test_clock.c $DRAFT/tests/test_sched.c emu/tests/`.

**Change against the draft:**
1. **A stuck source loops or turns the clock back** (§3.1: time passes only forward, in the scheduler). `emu_step()` sets the clock to the earliest due time, which a source whose fire leaves its due time in the past makes earlier than now. It happens in `emu_usb.c`: while an OTG_FS out body is pending (PRIMASK set, which `usb_startTransfer()` holds around a `HAL_GetTick()` call, `usb_manager.c:116–140`), `emu_usbFire()` takes no transfer but `emu_usbSchedule()` keeps the next one's past time. In `emu_step()`, after the fire loop, add:

```c
	for (int s = 0; s < EMU_NUM_SRC; s++)
		if (emu_src[s].due <= t)
		{
			char what[80];
			snprintf(what, sizeof what, "scheduler: source %d did not move its due time past %llu", s, (unsigned long long)t);
			emu_fault(what);
			emu_src[s].due = EMU_NEVER;
		}
```

   and in `emu_usbSchedule()`, schedule the head transfer only while no out body is pending (its body calls `emu_usbFire()` again at its end):

```c
	if (emu_usb.count && !emu_src[EMU_SRC_OTG_OUT].pending)
```

2. **The grid after a direct call** (§4.2: a pass runs at the first multiple of 54,000 cycles after the previous pass ended). A direct call (§3.3) may advance the clock past `emu_nextGridPass`; `emu_runUntil()` then calls `emu_setNow(emu_nextGridPass)`, which is in the past. That pass is overdue and runs at once. In `emu_runUntil()`, before the inner `while`, add `if (emu_nextGridPass < emu_clock) emu_nextGridPass = emu_clock;`.
3. **Calls from the wrong place** (§3.3: the hook never calls the firmware; direct calls only at a pass boundary). At the top of `emu_directCall()` and `emu_runUntil()`: `if (emu_hookDepth || emu_bodyDepth || emu_passing) { emu_fault("emu_runUntil/emu_directCall from the tier hook, a body or a pass"); return; }`.

**Tests** — the draft's `test_clock.c` (3 tests) and `test_sched.c` (14 tests) stay. Check that they cover: §4.2's periods (SysTick 216,000; TIM14 54,004 after its start; TIM2 6,004), priorities and order at equal times (`M3_order_at_equal_times`: SysTick before TIM14 at a common multiple, then USART3, then TIM2, then the block request); PRIMASK deferral; a release inside a body runs only higher-priority bodies; time only in thread context; the `HAL_GetTick()` rule (512 free calls, then 216 cycles each); the pass rule; the hook at least once per ms and at every block; the watchdog; the NVIC check; the trail; TIM2's counter and `HAL_TIM_Base_Stop()` leaving UIE set (M4). Add to `test_sched.c`:

```c
/* change 1: time passes in a PRIMASK section with a USB transfer waiting (no loop, no fault) */
TEST(M3_spin_under_primask)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.fault = emu_testOnFault;
	emu_corePowerOn(&cfg);
	emu_usbReset(&cfg);
	emu_testFaults = 0;
	static const uint8_t note[4] = { 0x09, 0x90, 0x3C, 0x64 };
	ASSERT_EQ(emu_inUsb(1000u, note, 4), 0);
	ASSERT_EQ(emu_inUsb(1000u, note, 4), 0);
	__disable_irq();
	for (int i = 0; i < 2000; i++)		/* 1,488 spin calls of 1 us each */
		HAL_GetTick();
	ASSERT_EQ(emu_now(), 1488u * 216u);
	__enable_irq();						/* both transfers' bodies run now */
	ASSERT_EQ(emu_testFaults, 0);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.usbIn, 2);
}

static void delay3ms(void *arg) { (void)arg; HAL_Delay(3); }

/* change 2: after a direct call that took 3 ms, the overdue grid pass runs at once */
TEST(M3_direct_call_grid)
{
	setup();
	emu_fw.pass = fakePass;
	passCount = 0;
	passDelayMs = 0;
	emu_runUntil(1u);					/* boot, the first pass at 0, the grid pass at 54,000 */
	ASSERT_EQ(passCount, 2);
	emu_directCall(delay3ms, NULL);		/* now 702,000; the grid's next pass was due at 108,000 */
	emu_runUntil(emu_now() + 1u);
	ASSERT_EQ(passCount, 3);
	ASSERT_EQ(passTimes[2], 702000);	/* the draft turned the clock back to 108,000 */
}

static void spinWait(void *arg)
{
	unsigned *it = arg;
	const uint32_t start = HAL_GetTick();	/* call 1 of the segment */
	while (HAL_GetTick() - start < 2u)		/* two calls per iteration, as screensaver.c:383 */
	{
		(void)HAL_GetTick();
		(*it)++;
	}
}

/* M3: a HAL_GetTick() wait ends: 512 free calls, then 432 cycles per iteration */
TEST(M3_spin_wait)
{
	setup();
	unsigned it = 0;
	emu_directCall(spinWait, &it);
	ASSERT_EQ(emu_now(), 432000);	/* call 2,512 is the first to read tick 2 */
	ASSERT_EQ(it, 1255);
}

static void reenter(void) { emu_runUntil(emu_now() + 100u); }

/* change 3 */
TEST(M3_no_run_from_a_body)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.fault = emu_testOnFault;
	emu_corePowerOn(&cfg);
	emu_fw.sysTick = reenter;
	emu_testFaults = 0;
	HAL_Delay(1);
	ASSERT_EQ(emu_testFaults, 1);
}
```

`M3_spin_under_primask` uses the USB model's reset directly, because `emu_corePowerOn()` sets up only the clock, the scheduler and the registers. `setup()`, `fakePass`, `passCount`, `passTimes` and `passDelayMs` are the draft's; move `M3_direct_call_grid` below them.

- Add to `emu/CMakeLists.txt`:

```cmake
emu_add_test(test_clock IDS M2_frames M2_cyccnt_and_tick M2_seqp_now_wraps)
emu_add_test(test_sched IDS M3_periods M3_order_at_equal_times M3_primask_defers M3_primask_in_body
    M3_time_only_in_thread M3_tick_rule M3_pass_rule M3_pass_after_slow_pass M3_hook M3_watchdog
    M3_nvic M3_trail M3_spin_under_primask M3_direct_call_grid M3_spin_wait M3_no_run_from_a_body
    M4_tim2_counter M4_tim2_stop)
```

- [ ] **Step 1: Copy the tests, add the four new ones and the CMake lines; run the three that test the changes and see them fail.** Run `cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && cmake --build build/emu >/dev/null; ctest --test-dir build/emu -R 'M3_(spin_under|direct|no_run)' --timeout 20 | tail -4`. Expected: `M3_spin_under_primask` times out, `M3_direct_call_grid` fails (`passTimes[2]` is 108,000), `M3_no_run_from_a_body` fails with 0 faults. `M3_spin_wait` passes on the draft.
- [ ] **Step 2: Apply the three changes; run again.** Expected: all three pass.
- [ ] **Step 3: EMU-CHECK.** N = 36 (15 + 3 + 18).
- [ ] **Step 4: The gate.**
- [ ] **Step 5: Commit** `emu/core/emu_sched.c emu/core/emu_usb.c emu/tests/test_clock.c emu/tests/test_sched.c emu/CMakeLists.txt`: "Emulator: clock and scheduler tests, and three scheduler fixes (E5.2 R1b, M2-M4)". Body: the 64-bit clock and its views (M2); the periods, priorities, PRIMASK, the `HAL_GetTick()` rule, the pass rule, the hook and the watchdog (M3); TIM2's counter and stop (M4). Fixes: a source that leaves its due time behind is a fault instead of a loop that turns the clock back (the USB model did so while its body waited behind PRIMASK); an overdue grid pass after a direct call runs at once; `emu_runUntil()` and `emu_directCall()` refuse calls from the hook, a body or a pass.

---

### Task R1b.6: The codec and the paced hook (M5 core, M3 hook with fakes)

§3.3, §4.1, §6.2's pacing as a fake, §12.1 M3 (the tier hook) and M5.

**Files:**
- Create: `emu/tests/test_codec.c`
- Modify: `emu/tests/emu_test.h`, `emu/CMakeLists.txt`

**Start from:** `cp $DRAFT/tests/test_codec.c emu/tests/`.

**Change against the draft:** none in `emu_codec.c`: block request k at the codec start plus ⌊64k × 552,960 / 113⌋, an underrun when no half is valid, the half `dmaPtr & 1` to the sink, the other half freed, TIM2 CNT 1450 (§4.1, checked against `cs4344_cs5343.c:86–141` with `SWITCH_FLAG 1`).

**Tests** — the draft's four tests stay (`M5_requests`, `M5_halves`, `M4_tim2_after_request`, `M5_hook_per_block`). Add the fake tier to `emu_test.h`, for this task and Task 8. It models §3.3's hook: before the codec starts, or while no output runs, it sleeps to the wall clock; after, it waits while a ring of `cap` blocks is full, and the fake device takes one block per `blockSec` of wall time. Each wait is bracketed for the watchdog, which it polls inside the wait.

```c
/* a fake tier for the hook tests (spec 3.3, 6.2): a wall clock and an audio ring */
typedef struct emu_testTier
{
	double wall;			/* seconds */
	unsigned fill, cap;		/* blocks in the ring, its size */
	double blockSec;		/* the fake device takes one block per blockSec */
	uint64_t blockHooks, timeHooks;
	double maxWait;			/* the longest single wait */
	emu_watchdog_t dog;
	int dogFired;			/* the watchdog fired during a wait */
	void (*extra)(uint64_t now, emu_hookReason_t why);	/* a test's own work */
} emu_testTier_t;

__attribute__((unused)) static emu_testTier_t emu_tier;

static inline void emu_testTierWait(double until)
{
	if (until <= emu_tier.wall)
		return;
	emu_waitBegin();
	if (until - emu_tier.wall > emu_tier.maxWait)
		emu_tier.maxWait = until - emu_tier.wall;
	emu_tier.wall = until;
	if (emu_watchdogPoll(&emu_tier.dog, emu_tier.wall))
		emu_tier.dogFired = 1;
	emu_waitEnd();
}

static inline void emu_testTierHook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	emu_counters_t c;
	emu_getCounters(&c);
	if (why == EMU_HOOK_BLOCK)
	{
		emu_tier.blockHooks++;
		if (++emu_tier.fill > emu_tier.cap)
		{
			emu_testTierWait(emu_tier.wall + emu_tier.blockSec);
			emu_tier.fill--;
		}
	}
	else
	{
		emu_tier.timeHooks++;
		if (c.codecStart == EMU_NEVER)
			emu_testTierWait((double)now / EMU_CPU_HZ);
	}
	if (emu_tier.extra)
		emu_tier.extra(now, why);
}

static inline void emu_testTierReset(unsigned cap, double blockSec)
{
	memset(&emu_tier, 0, sizeof emu_tier);
	emu_tier.cap = cap;
	emu_tier.blockSec = blockSec;
}
```

Add to `test_codec.c`:

```c
/* M3, the tier hook: the clock before the codec start follows the wall clock; after
 * it, a full ring paces the blocks; the waits never count for the watchdog */
TEST(M3_paced_by_ring)
{
	emu_testTierReset(8u, 0.020);	/* a slow device: one block per 20 ms */
	setup(emu_testTierHook);
	HAL_Delay(1000);				/* before the codec start: sleeps to the wall clock */
	ASSERT_NEAR(emu_tier.wall, 1.0, 1e-9);
	codec_initCsCodec(0, 128, 0, 128);
	valid = 0xFFu;
	HAL_Delay(1000);				/* 689 blocks; the ring holds 8 */
	ASSERT_EQ(emu_tier.blockHooks, 689);
	ASSERT_NEAR(emu_tier.wall, 1.0 + (689 - 8) * 0.020, 1e-6);	/* 14.6 s of wall time in waits */
	ASSERT_FALSE(emu_tier.dogFired);
	ASSERT_EQ(emu_watchdogPoll(&emu_tier.dog, emu_tier.wall + 1.0), 0);		/* sees the last progress */
	ASSERT_EQ(emu_watchdogPoll(&emu_tier.dog, emu_tier.wall + 10.0), 0);	/* 9 s without progress */
	ASSERT_EQ(emu_watchdogPoll(&emu_tier.dog, emu_tier.wall + 11.5), 1);	/* 10.5 s outside a wait: a hang */
}
```

- Add `emu_add_test(test_codec IDS M5_requests M5_halves M4_tim2_after_request M5_hook_per_block M3_paced_by_ring)` (N grows by 5).

Note: `M5_hook_per_block` and `M3_paced_by_ring` both count 689 blocks in 1 s: 216,000,000 / 313,180.88 = 689.70.

- [ ] **Step 1: Copy, add the helper and the test, register.**
- [ ] **Step 2: Run `ctest --test-dir build/emu -R 'M5_|M4_tim2_after|M3_paced' --output-on-failure | tail -3`.** Expected: 5 of 5 pass. If `M3_paced_by_ring` reports `emu_tier.wall` off by one block, count when the hook runs relative to the block's sink call (§3.3: the hook runs after the block request) and fix the test's arithmetic, not the scheduler.
- [ ] **Step 3: EMU-CHECK.** N = 41.
- [ ] **Step 4: The gate.**
- [ ] **Step 5: Commit** `emu/tests/test_codec.c emu/tests/emu_test.h emu/CMakeLists.txt`: "Emulator: codec tests and the tier hook with a fake ring (E5.2 R1b, M3, M5)", body: block requests at exact frame times, underruns, the order of the halves, out 1 from `dma_buffer2`'s position (M5); TIM2 204 cycles after a request (M4); a fake wall clock and audio ring drive the hook: wall-clock pacing before the codec start, ring pacing after it, waits not counted by the watchdog (M3).

---

### Task R1b.7: The boot (M6; M3's codec start; M5's firmware part)

§4.1, §4.8, §4.10, §12.1 M6, M3 ("the codec starts at exactly 216,000,000 cycles with a card and 432,000,000 without"), M5 ("half 0 rendered by `soundengine_init()`", "out 1 from `dma_buffer2`").

**Files:**
- Create: `emu/tests/test_boot.c`
- Modify: `emu/tests/emu_test.h`, `emu/CMakeLists.txt`

**Start from:** new.

**What the check for this plan saw** with the draft: without a card, the boot ends at 432,000,000 cycles, the codec started then, no underrun follows, and 1 s later the LCD shows the voice page (`COA fin wav pwm`). With an empty card folder, the codec starts at 216,000,000, the boot creates `PROJ00/GLO.CFG` and `TMP/`, and at the boot's end 689 blocks have been requested with 688 underruns.

**Change against the draft:** none expected. Add to `emu_test.h`:

```c
#include <sys/stat.h>

/* the whole firmware: power-on and boot; once per process */
static inline void emu_testBoot(emu_config_t *cfg)
{
	emu_powerOn(cfg);
	emu_runUntil(1u);	/* returns at the end of the boot */
}

/* run for ms of virtual time from now, to the next pass boundary */
static inline void emu_testRunMs(double ms)
{
	emu_runUntil(emu_now() + (uint64_t)(ms * (double)EMU_CYCLES_PER_MS));
}

/* 1 if row 0 or row 1 of the LCD contains text */
static inline int emu_testLcdHas(const char *text)
{
	char r0[17], r1[17];
	emu_testLcdRow(0, r0);
	emu_testLcdRow(1, r1);
	return strstr(r0, text) != NULL || strstr(r1, text) != NULL;
}

static inline void emu_testWrite(const char *dir, const char *name, const void *data, size_t n)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "wb");
	if (!f || fwrite(data, 1, n, f) != n || fclose(f) != 0)
	{
		perror(path);
		exit(2);
	}
}

/* A minimal card (fixtures come in R3): /PROJ00/PRJ.NFO, and GLO.CFG with a tempo
 * when bpm is not 0; byte 29, the kit autoload, is 0. */
static inline void emu_testCard(char *dir, size_t size, uint8_t bpm)
{
	emu_testTempDir(dir, size);
	char proj[1024];
	snprintf(proj, sizeof proj, "%s/PROJ00", dir);
	mkdir(proj, 0755);
	emu_testWrite(proj, "PRJ.NFO", "TESTPROJ", 8);
	if (bpm)
	{
		uint8_t glo[30] = { bpm };
		emu_testWrite(proj, "GLO.CFG", glo, sizeof glo);
	}
}
```

**Tests** — `emu/tests/test_boot.c`:

```c
#include "stm32f7xx_hal.h"
#include "Soundengine/AudioCodecManager.h"
#include "emu_test.h"

static char rows[2][17];
static int seen;
static uint64_t when;

/* snapshot the LCD once, at the first hook at or after `when` */
static void snapAt(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	(void)why;
	if (!seen && now >= when)
	{
		emu_testLcdRow(0, rows[0]);
		emu_testLcdRow(1, rows[1]);
		seen = 1;
	}
}

static const int16_t *out1Seen, *out2Seen;
static void audio(void *ctx, uint64_t t, const int16_t *out1, const int16_t *out2)
{
	(void)ctx;
	(void)t;
	out1Seen = out1;
	out2Seen = out2;
}

TEST(M6_splash)
{
	char card[512];
	emu_testCard(card, sizeof card, 120);
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	cfg.hook = snapAt;
	when = 108000000u;	/* 0.5 s: inside the HAL_Delay(1000) before the codec start */
	emu_testBoot(&cfg);
	ASSERT_TRUE(seen);
	ASSERT_EQ(strncmp(rows[0], "LICHOMAT", 8), 0);
	ASSERT_EQ(strncmp(rows[1], "v0.1 unofficial", 15), 0);
}

TEST(M6_no_card)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	cfg.hook = snapAt;
	cfg.sinks.audio = audio;
	when = 108000000u;	/* inside the HAL_Delay(1000) after 'No Card!' (appMain.c, FR_NOT_READY) */
	emu_testBoot(&cfg);
	ASSERT_EQ(strncmp(rows[0], "SD error:", 9), 0);
	ASSERT_EQ(strncmp(rows[1], "No Card!", 8), 0);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.codecStart, 432000000u);	/* M3: two HAL_Delay(1000) without a card */
	ASSERT_EQ(c.bootEnd, 432000000u);
	emu_testRunMs(1000);
	emu_getCounters(&c);
	ASSERT_EQ(c.blocks, 689);
	ASSERT_EQ(c.underruns, 0);	/* M5: half 0 came from soundengine_init(), then every pass rendered */
	ASSERT_TRUE(c.passes > 3500);	/* a pass at every block and on the 250 us grid */
	ASSERT_TRUE(emu_testLcdHas("COA"));
	/* M5: out 1 is dma_buffer2 (DAC1), out 2 dma_buffer (DAC2) */
	ASSERT_TRUE(out1Seen == &dma_buffer2[0] || out1Seen == &dma_buffer2[128]);
	ASSERT_TRUE(out2Seen == &dma_buffer[0] || out2Seen == &dma_buffer[128]);
}

TEST(M6_card)
{
	char card[512];
	emu_testCard(card, sizeof card, 120);
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	emu_testBoot(&cfg);
	emu_counters_t c0, c;
	emu_getCounters(&c0);
	ASSERT_EQ(c0.codecStart, 216000000u);	/* M3: one HAL_Delay(1000) with a card */
	emu_testRunMs(1000);
	emu_getCounters(&c);
	ASSERT_EQ(c.underruns, c0.underruns);	/* none once the passes run */
	char path[1024];
	struct stat st;
	snprintf(path, sizeof path, "%s/TMP", card);
	ASSERT_EQ(stat(path, &st), 0);		/* the /TMP overlay (boot step 13) */
	snprintf(path, sizeof path, "%s/PROJ.IDX", card);
	ASSERT_EQ(stat(path, &st), 0);		/* written by the project load */
	ASSERT_EQ(st.st_size, 1);
	ASSERT_FALSE(emu_testLcdHas("error"));
}

static emu_counters_t atDelayEnd;
static int delayEndSeen;

/* the LCD inside the glo.cfg error's HAL_Delay(1000), the counters at its end */
static void gloHook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	snapAt(ctx, now, why);
	if (!delayEndSeen && now >= 432000000u)
	{
		emu_getCounters(&atDelayEnd);
		delayEndSeen = 1;
	}
}

TEST(M6_no_globals)
{
	char card[512];
	emu_testCard(card, sizeof card, 0);	/* PRJ.NFO without GLO.CFG */
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	cfg.hook = gloHook;
	when = 216000000u + 108000000u;		/* the delay runs from the codec start to 432,000,000 */
	emu_testBoot(&cfg);
	ASSERT_TRUE(strstr(rows[0], "glo.cfg") != NULL || strstr(rows[1], "glo.cfg") != NULL);
	ASSERT_TRUE(delayEndSeen);
	ASSERT_EQ(atDelayEnd.codecStart, 216000000u);
	ASSERT_EQ(atDelayEnd.blocks, 689);		/* all inside the HAL_Delay(1000): no pass, no TIM2 */
	ASSERT_EQ(atDelayEnd.underruns, 688);	/* only half 0 was rendered (spec 4.10, step 8) */
}

TEST_MAIN()
```

- Add `emu_add_test(test_boot IDS M6_splash M6_no_card M6_card M6_no_globals)`.

- [ ] **Step 1: Add the helpers and the test; register; build.**
- [ ] **Step 2: Run `ctest --test-dir build/emu -R M6_ --output-on-failure | tail -3`.** Expected: 4 of 4. Two checks may need the firmware's own text: the exact 'glo.cfg error' line (`grep -n 'glo.cfg' Core/Src/appMain.c`) and whether `PROJ.IDX` is written on this card (§4.8: "written only on a load"; if the load stops early, read `preset_loadProject()` and record why in the test's comment instead of dropping the check).
- [ ] **Step 3: EMU-CHECK.** N = 45.
- [ ] **Step 4: The gate.**
- [ ] **Step 5: Commit** `emu/tests/test_boot.c emu/tests/emu_test.h emu/CMakeLists.txt`: "Emulator: the boot runs in a test (E5.2 R1b, M6)", body: the splash, the boot without a card, with a minimal card and without `GLO.CFG` (689 block requests, 688 underruns); the codec starts at exactly 216,000,000 cycles with a card and 432,000,000 without (M3); half 0 from `soundengine_init()` and out 1 from `dma_buffer2` (M5).

---

### Task R1b.8: Long firmware calls and the hook (M3 firmware, M4 firmware)

§3.3, §4.2, §12.1 M3 (a normal pass and a kit load take no time; the screensaver wait; the tier hook during `menu_showSdError()` and a screensaver wake; the splash paced to 1 s; the waits outside the watchdog; the screensaver test hook) and M4 (parked by `soundengine_process()`, every 6,004 cycles inside a `HAL_Delay()`).

**Files:**
- Create: `emu/tests/test_hook.c`
- Modify: `emu/CMakeLists.txt`

**Start from:** new. **Change against the draft:** none expected.

**Tests** — `emu/tests/test_hook.c`. Each test boots without a card (fast, no files) unless it says otherwise. A wrapper counts TIM2 bodies around the real one that `emu_powerOn()` installed:

```c
#include "stm32f7xx_hal.h"
#include "menu/menu.h"
#include "menu/screensaver.h"
#include "Preset/PresetManager.h"
#include "emu_test.h"

static void (*realTim2)(void);
static uint64_t tim2Bodies, tim2First, tim2Last;
static void countTim2(void)
{
	if (!tim2Bodies++)
		tim2First = emu_now();
	tim2Last = emu_now();
	realTim2();
}

static void bootNoCard(emu_hook_t hook)
{
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardIn = 0;
	cfg.hook = hook;
	emu_powerOn(&cfg);
	realTim2 = emu_fw.tim2;
	emu_fw.tim2 = countTim2;
	emu_runUntil(1u);
}

/* the last pass started at the time it ended: no time passed in it */
static int lastPassTookNoTime(void)
{
	emu_trailEntry_t e[EMU_TRAIL_LENGTH];
	const unsigned n = emu_trail(e, EMU_TRAIL_LENGTH);
	for (unsigned i = n; i-- > 0;)
		if (e[i].kind == EMU_TRAIL_PASS)
			return e[i].t == emu_now();
	return 0;
}

extern TIM_HandleTypeDef htim2;
/* USE_INTERRUPT_ONLY_DURING_SAVE_PROJ (config.h:30): the boot leaves TIM2 off; the
 * save and load paths start it, as this does */
static void startTim2(void *arg) { (void)arg; HAL_TIM_Base_Start_IT(&htim2); }

TEST(M3_fw_normal_pass)
{
	bootNoCard(NULL);
	emu_directCall(startTim2, NULL);
	emu_testRunMs(100);
	tim2Bodies = 0;	/* from the start until the first pass parked it, TIM2 ran freely */
	for (int i = 0; i < 2000; i++)
	{
		emu_runUntil(emu_now() + 1u);
		ASSERT_TRUE(lastPassTookNoTime());
		emu_counters_t c;
		emu_getCounters(&c);
		ASSERT_TRUE(c.tickCalls <= 512u);
	}
	ASSERT_EQ(tim2Bodies, 0);	/* M4: every pass parks TIM2 before it fires (CNT 1950) */
}

static void kitLoad(void *arg) { (void)arg; preset_loadDrumsetRam(0, 0, 1); }

TEST(M3_fw_kit_load)
{
	bootNoCard(NULL);
	const uint64_t before = emu_now();
	emu_directCall(kitLoad, NULL);	/* the immediate load: soundengine_process() after every parameter */
	ASSERT_EQ(emu_now(), before);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_TRUE(c.tickCalls > 100u && c.tickCalls <= 512u);	/* about 250 (spec 4.2) */
}

/* the hook injects a DIN byte at its 100th block call after the test zeroes
 * blockCalls, and reads the counters at the 300th: the input arrived while the
 * firmware call ran */
static uint64_t blockCalls, dinAt300;
static void injectExtra(uint64_t now, emu_hookReason_t why)
{
	if (why != EMU_HOOK_BLOCK)
		return;
	if (++blockCalls == 100u)
	{
		static const uint8_t clock = 0xF8;
		emu_inDin(now + 1000u, &clock, 1);
	}
	if (blockCalls == 300u)
	{
		emu_counters_t c;
		emu_getCounters(&c);
		dinAt300 = c.dinIn;
	}
}
static const double blockSec = 64.0 / 44140.625;	/* a fake device at the audio rate */
static void sdError(void *arg) { (void)arg; menu_showSdError(FR_DISK_ERR); }

TEST(M3_fw_sd_error)
{
	emu_testTierReset(8u, blockSec);
	bootNoCard(emu_testTierHook);
	emu_counters_t c0, c1;
	emu_getCounters(&c0);
	blockCalls = 0;
	emu_tier.extra = injectExtra;
	const double wall0 = emu_tier.wall;
	const uint64_t start = emu_now();
	emu_directCall(sdError, NULL);	/* TIM2 started, HAL_Delay(2000) (menu.c:1994, :2039) */
	emu_getCounters(&c1);
	const double blocks = (double)(c1.blocks - c0.blocks);
	ASSERT_TRUE(emu_now() - start >= 2000u * EMU_CYCLES_PER_MS);
	ASSERT_NEAR(blocks, 1379.4, 1.5);					/* about 1,378 blocks inside one call */
	ASSERT_NEAR((double)blockCalls, blocks, 0.5);		/* the hook ran at each of them */
	ASSERT_NEAR(emu_tier.wall - wall0, 2.0, 0.02);		/* paced by the ring in real time */
	ASSERT_EQ(c1.underruns, c0.underruns);				/* TIM2 renders every block */
	ASSERT_EQ(dinAt300, c0.dinIn + 1u);					/* the hook's input arrived during the call */
	ASSERT_EQ(c1.passes, c0.passes);					/* no pass inside a direct call */
	/* M4: TIM2 fires every 6,004 cycles inside the HAL_Delay(), and 204 after each request */
	ASSERT_TRUE(tim2Bodies > 70000u);
	ASSERT_NEAR((double)(tim2Last - tim2First) / (double)(tim2Bodies - 1u), 6004.0, 300.0);
	ASSERT_FALSE(emu_tier.dogFired);
}

static void nearTimeout(void *arg) { (void)arg; screensaver_timer = 2u * 60u * 1000u - 50u; }

TEST(M3_fw_screensaver)
{
	emu_testTierReset(8u, blockSec);
	bootNoCard(emu_testTierHook);
	emu_directCall(nearTimeout, NULL);	/* the test hook of spec 5.3: near its 2-minute timeout */
	emu_testRunMs(200);
	ASSERT_EQ(screensaver_Active, 1);	/* it started */
	emu_counters_t c0, c1;
	emu_getCounters(&c0);
	const uint64_t timeHooks0 = emu_tier.timeHooks;
	const double wall0 = emu_tier.wall;
	blockCalls = 0;
	emu_tier.extra = injectExtra;
	emu_inButton(emu_now(), 1u, 1);		/* voice 1 wakes it */
	emu_inButton(emu_now() + 50u * EMU_CYCLES_PER_MS, 1u, 0);
	emu_testRunMs(1000);
	ASSERT_EQ(screensaver_Active, 0);
	/* the wake loop (screensaver.c:75) runs 361-465 ms inside one pass; the hook runs in it */
	ASSERT_TRUE(emu_tier.timeHooks - timeHooks0 >= 361u);
	ASSERT_TRUE(emu_tier.wall - wall0 > 0.99);	/* the blocks were paced throughout */
	emu_getCounters(&c1);
	ASSERT_EQ(c1.underruns, c0.underruns);
	ASSERT_EQ(dinAt300, c0.dinIn + 1u);
	const uint64_t passes = c1.passes;
	emu_testRunMs(100);
	emu_getCounters(&c1);
	ASSERT_TRUE(c1.passes - passes > 300u);	/* the passes go on */
}

TEST(M3_fw_splash_paced)
{
	char card[512];
	emu_testCard(card, sizeof card, 120);
	emu_testTierReset(8u, blockSec);
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	cfg.hook = emu_testTierHook;
	emu_testBoot(&cfg);
	/* the wall clock followed the virtual clock to the codec start: the splash showed 1 s */
	ASSERT_NEAR(emu_tier.wall, 1.0, 0.002);
	ASSERT_FALSE(emu_tier.dogFired);
}

TEST_MAIN()
```

Notes for the implementer:
- `M3_fw_screensaver` needs the wake's length in passes. If `timeHooks` does not show it (the hook runs once per ms only while `HAL_Delay()` or a spin advances the clock), measure the longest gap between two consecutive `EMU_TRAIL_PASS` entries instead, from a hook that copies the trail. Expected 361–465 ms (§3.3, measured on the probe). A value outside this range is a finding: report it, do not widen the range.
- `menu_showSdError()` takes a `uint8_t` result (`menu.h:644`); `FR_DISK_ERR` is from `ff.h`. If it returns before its `HAL_Delay(2000)` for this value, read `menu.c:1990–2040` and pick a result that reaches it.
- Add `emu_add_test(test_hook IDS M3_fw_normal_pass M3_fw_kit_load M3_fw_sd_error M3_fw_screensaver M3_fw_splash_paced)`.

- [ ] **Step 1: Write the test, register, run `ctest --test-dir build/emu -R M3_fw --output-on-failure | tail -3`.** Expected: 5 of 5.
- [ ] **Step 2: EMU-CHECK.** N = 50.
- [ ] **Step 3: The gate.**
- [ ] **Step 4: Commit** `emu/tests/test_hook.c emu/CMakeLists.txt`: "Emulator: long firmware calls and the tier hook (E5.2 R1b, M3, M4)", body: a normal pass and an immediate kit load take no time; `menu_showSdError()`'s `HAL_Delay(2000)` renders about 1,379 blocks in TIM2, which fires every 6,004 cycles, while the hook paces the blocks and injects input; the screensaver starts, a button wakes it, the hook paces and injects during the wake, the passes go on; the splash is paced to 1 s; with TIM2 started, the passes park it so that it never fires.

---

### Task R1b.9: The licence gate (M21)

§10.1, §10.2, §12.1 M21.

**Files:**
- Create: `emu/tests/licence_gate.py`, `emu/tests/name_allow.txt`
- Modify: `emu/CMakeLists.txt`

**Start from:** `cp $DRAFT/tests/licence_gate.py emu/tests/`. `name_allow.txt` is new (the draft reads it but lacks it).

**Change against the draft:**
- The binaries of R1 are the test binaries that link the whole firmware; R2 adds `lichomat-render`, R4 the app.
- Add an option `--extra PATH`, repeatable, that adds PATH (relative to the root) to the dependency set. It gives the gate a negative test:

```python
def main():
    args = sys.argv[1:]
    extra = []
    while args and args[0] == '--extra':
        extra.append(args[1])
        args = args[2:]
    if len(args) < 3:
        raise SystemExit(__doc__)
    root, build, binaries = args[0], args[1], args[2:]
    # the draft's main() from here on, with check_files(root, build, errors, extra)
```

  and pass `extra` to `check_files()`, which checks each `os.path.join(root, p)` as given, without the overlay mapping (so `Core/Inc/main.h` means ST's file), next to the set from `dependencies(build)`.
- Print the number of errors last, `licence gate: N errors`, before exiting with 1, so that the negative test can require both of its errors.
- The name scan strips the repository root and the build directory from each string before it searches (the draft does). This matters: UBSan stores each source path as a C string, and `-ffile-prefix-map` does not change them (checked for this plan), so `/Users/tk/Code/lxr/…` and CI's `…/lxr-desktop-app/…` appear in every sanitizer binary.
- `name_allow.txt`, R1's version, holds no string yet:

```
# The C strings that may name Erica Synths, LXR or Sonic Potions (stage 1b spec
# 10.2), one per line: the tagline and the legal notices of --version and the
# About box. R2 adds them with lichomat-render --version. Lines starting with #
# are comments.
```

- Add to `emu/CMakeLists.txt`:

```cmake
# M21: the licence gate over what the binaries were built from, and the name scan
set(EMU_GATE ${Python3_EXECUTABLE} "${EMU_DIR}/tests/licence_gate.py")
add_test(NAME M21 COMMAND ${EMU_GATE} "${LXR_ROOT}" "${CMAKE_BINARY_DIR}"
    $<TARGET_FILE:test_boot> $<TARGET_FILE:test_hook>)
add_test(NAME M21_rejects COMMAND ${EMU_GATE} --extra Core/Inc/lcd.h --extra Core/Inc/main.h
    "${LXR_ROOT}" "${CMAKE_BINARY_DIR}" $<TARGET_FILE:test_boot>)
set_tests_properties(M21_rejects PROPERTIES PASS_REGULAR_EXPRESSION "licence gate: 2 errors")
```

  The two errors are `Core/Inc/lcd.h is neither GPL-3.0-only nor on the allow-list` and `Core/Inc/main.h names SLA0044`; Step 2 checks that they are the two.

  `ctest` needs `ninja` on PATH for `ninja -t deps`; EMU-CHECK sources `tools/env.sh`.

- [ ] **Step 1: Copy, add `--extra`, write `name_allow.txt`, register.**
- [ ] **Step 2: Run `ctest --test-dir build/emu -R M21 -V | grep 'licence gate'`.** Expected: `licence gate: N files, M compile commands and 2 binaries pass` with N about 250 (the check for this plan found 169 firmware dependencies, of which only `ff.h` and `integer.h` lack the GPL line; the core, the tests and the stubs add the rest), then the two rejection lines of `M21_rejects`.
- [ ] **Step 3: EMU-CHECK.** N = 52.
- [ ] **Step 4: The gate.**
- [ ] **Step 5: Commit** `emu/tests/licence_gate.py emu/tests/name_allow.txt emu/CMakeLists.txt`: "Emulator: the licence gate and the name scan (E5.2 R1b, M21)", body: every file that the binaries were compiled from, with the overlay resolved, is GPL-3.0-only or on the allow-list of spec 10.1 (ChaN's `ff.h` and `integer.h`, `emu/libm`), none names SLA0044; every compile command has the overlay; the binaries' C strings name no trademark outside `name_allow.txt`, which R2 fills; a negative test with `lcd.h` and the original `main.h`.

---

### Task R1b.10: The LCD and the LEDs (M7)

§4.7, §12.1 M7.

**Files:**
- Create: `emu/tests/test_lcd.c`
- Modify: `emu/CMakeLists.txt`

**Start from:** new. **Change against the draft:** none expected. Two of §4.7's points are resolved as follows: the core keeps character codes, and the font of Q18 (0xC5 and 0xC6 as arrows, other codes as a box) is drawn by the tiers (R2's panel log writes `\xNN`, R4 draws), so M7's "0xC5 and 0xC6" checks that the codes reach the snapshot unchanged. `_delay_us()` and `_delay_ms()` of §4.7's list are not provided: no kept file calls them (checked), and the link proves it.

**Tests** — `emu/tests/test_lcd.c`, pure tests against the model (`emu_corePowerOn()`, then `emu_lcdReset()` and `emu_panelReset()`; `lcd_*()`, `HAL_SPI_Transmit()`, `emu_ledSnapshot()`), plus one firmware test:

| TEST | Inputs | Expected |
|---|---|---|
| `M7_commands` | `emu_lcdReset()`, `lcd_init()`; `lcd_string("AB")`; `lcd_setcursor(3, 2)`; `lcd_data('C')`; `lcd_command(LCD_SET_ENTRY)` (bit 1 clear: decrement); `lcd_data('D')`; `lcd_home()`; `lcd_turnOn(1, 1)` | row 0 `"AB"` then spaces; row 1 `"   CD"` then spaces ('C' at 0x43 moves the counter to 0x44, where 'D' goes, and the decrement moves it back to 0x43); after the home the cursor is row 0, col 0; `displayOn` 1, `cursorOn` 1, `blinkOn` 0 |
| `M7_clear` | write 32 characters, `lcd_clear()` | both rows spaces, cursor 0,0, entry increment again |
| `M7_cgram` | `lcd_generatechar(2, rows)` with rows `{1,2,3,4,5,6,7,8}`; then `lcd_data(2)` in DDRAM | `cgram[16..23]` = 1..8; `cgram[0..7]` all 0 (character 0 blank after `lcd_init()`); row 0 col 0 holds code 2 |
| `M7_row_wrap` | `lcd_setcursor(15, 1)`, `lcd_string("XY")` | `'X'` at row 0 col 15; `'Y'` at DDRAM 0x10, off screen; row 1 unchanged |
| `M7_arrows` | `lcd_data(0xC5)`, `lcd_data(0xC6)` | snapshot `chars[0][0]` = 0xC5, `[0][1]` = 0xC6 |
| `M7_led_latch` | `HAL_SPI_Transmit(NULL, {0x01,0,0,0,0x80}, 5, 100)`; `GPIOB->ODR = 1u << 8`; snapshot | bits 0, 39 and 40 set, nothing else |
| `M7_led_pulse` | `emu_ledSnapshot()` once to clear; transmit `{0x02,0,0,0,0}` (voice LED 1), `HAL_Delay(50)`, transmit `{0,0,0,0,0}`, `HAL_Delay(150)`; snapshot twice | the first snapshot has bit 1 (on at some time since the previous snapshot, §4.7), the second has no bit |
| `M7_fw_repaint` | boot without a card, run 100 ms; direct call `menu_repaintAll()`; then step `emu_runUntil(emu_now() + 1)` and take an LCD snapshot after each pass for 10 ms | the snapshot stops changing at 6.25 ms ± 0.25 ms after the call (25 TIM14 periods of 4 `menu_tick()` calls, §4.7); its rows equal those before the call |

`M7_fw_repaint` needs a screen that differs while it repaints: before the call, write `lcd_clear()` in the same direct call, so the repaint rebuilds both rows from spaces. Write the tests as `TEST()` functions in the format of `test_sched.c`; the expected values are those in the table.

- Add `emu_add_test(test_lcd IDS M7_commands M7_clear M7_cgram M7_row_wrap M7_arrows M7_led_latch M7_led_pulse M7_fw_repaint)`.

- [ ] **Step 1: Write the tests; register; run `ctest --test-dir build/emu -R M7_ --output-on-failure | tail -3`.** Expected: 8 of 8. A failure in a pure test is a model bug against §4.7 (HD44780 semantics): fix `emu_lcd.c` and say so in the commit.
- [ ] **Step 2: EMU-CHECK.** N = 60.
- [ ] **Step 3: The gate.**
- [ ] **Step 4: Commit** `emu/tests/test_lcd.c emu/CMakeLists.txt` (and `emu/core/emu_lcd.c` if fixed): "Emulator: LCD and LED tests (E5.2 R1b, M7)".

---

### Task R1b.11: The panel input (M8 without the output jacks)

§4.6, §12.1 M8: one transition per scan; Gray changes 1 ms apart; `encoder +10` moves the menu 10 items; a 40 ms push seen in every sampling phase; 134 ± 5 steps per knob turn on a normal parameter and 22 ± 2 on a list parameter; no edit at boot with the knobs at 0°; fader i reaching `adc_getFaderVolume(i−1)`.

**Files:**
- Create: `emu/tests/test_panel.c`
- Modify: `emu/CMakeLists.txt`, possibly `emu/core/emu_panel.c`

**Start from:** new.

**What the check for this plan saw**, with the draft on a boot without a card (voice 1's oscillator page, `COA fin wav pwm`):
- `encoder +10` moved the selection to the page's last item, `pwm`, and `encoder -10` back: that page has 4 items, so it cannot show 10. M8's sub-case therefore runs on the kit load page, whose encoder changes the selected kit number (`menu_handleLoadSaveMenu()`). This plan resolves the spec's wording that way.
- **Knob 1 (COA) made 96 edits in a 360° turn, and knob 3 (`wav`, a list parameter) 26.** M8 expects 134 ± 5 and 22 ± 2. So M8's knob sub-case fails on the draft. Work it out before writing code: §4.6's model is two triangle waves 90° apart, wiper a peaking at 90° and b at 180°, 12-bit values shifted left by 4, at most 1° per pass; `adc_checkPots()` (`adcPots.c:197–235`) detects a change of wiper a above `HYSTERSIS` (40 counts, 1.76°) and edits by ±1 when `calcPosition() >> shift` changes (shift 1, or 4 for a list parameter). Check the draft's `emu_wiper()` against that model and how the edits are counted (once per change of the parameter's value; a clamped value stops counting). If the model is right and the firmware gives another count, stop and report both numbers to the controller: the spec's 134 is the basis of Q16's "about 134 steps per turn" and needs the owner.

**Change against the draft:** whatever the knob work-up finds, named in the commit with §4.6.

**Tests** — `emu/tests/test_panel.c`:

| TEST | Kind | Inputs | Expected |
|---|---|---|---|
| `M8_scan` | pure | `emu_panelReset()`; `emu_inButton(t, 5, 1)`, `(t, 5, 0)`, `(t, 5, 1)` at one time t; three `HAL_SPI_Receive()` calls after t | byte 0 bit 5 reads 1, 0, 1: one transition per scan; button 40 the same on `GPIOB->IDR` bit 7 |
| `M8_gray` | pure | `emu_inEncoder(0, +2)`, then `HAL_Delay(10)` with `emu_fw.sysTick` sampling `GPIOE->IDR` bits 13–14 | 8 changes, one per SysTick at 1, 2, …, 8 ms; the states run 00 → 01 → 11 → 10 → 00 (A = bit 13 as the high bit); then `emu_inEncoder(t, -1)` runs them backwards |
| `M8_fw_encoder10` | firmware | boot without a card; direct call `menu_switchPage(LOAD_PAGE)`; read the kit number shown; `encoder +10`, run 100 ms; `encoder -10`, run 100 ms; ten `encoder +1` 20 ms apart, run 100 ms | the number grows by exactly 10, returns, and grows by 10 again; LCD rows after `+10` equal those after ten `+1` |
| `M8_fw_push_phases` | firmware | boot without a card; 10 pushes of 40 ms (`emu_inPush`), 500 ms apart, starting at k × 0.1 ms after a SysTick, k = 0…9 | each push toggles the edit mode: the LCD alternates between `COA fin wav pwm` and the edit view (`OscilltrCoarse`), 10 toggles |
| `M8_fw_knob_normal` | firmware | boot without a card; `emu_inKnob(t, 1, 360)`; step one pass at a time for 500 passes, counting changes of `activeParameters[PAR_COARSE1]` | 134 ± 5 (see above) |
| `M8_fw_knob_list` | firmware | the same with knob 3 and the waveform parameter of voice 1 (`PAR_OSC_WAVE_DRUM1`) | 22 ± 2 |
| `M8_fw_no_edit_at_boot` | firmware | `fork()`: the child boots without a card with `cfg.knobs[0] = 40` and writes `activeParameters[PAR_COARSE1]` to a pipe; the parent boots with the knobs at 0° | the child's value is the parent's plus 1 (the model's +1 edit at 40°, §4.6), so the parent's boot made no edit |
| `M8_fw_faders` | firmware | boot without a card; for i = 1…6: `emu_inFader(now, i, 0.5)`, run 50 ms | `adc_getFaderVolume(i−1)` = (2047/4096)² = 0.249756 ± 1e-6, and every other fader still (4095/4096)² = 0.999512 ± 1e-6 |

- `M8_fw_no_edit_at_boot` forks before any power-on and waits for the child with `waitpid()`; the child calls `_exit(0)`.
- Add `emu_add_test(test_panel IDS M8_scan M8_gray M8_fw_encoder10 M8_fw_push_phases M8_fw_knob_normal M8_fw_knob_list M8_fw_no_edit_at_boot M8_fw_faders)`.

- [ ] **Step 1: Work out the knob count (above).** Record the cause in the task report.
- [ ] **Step 2: Write the tests; register; run `ctest --test-dir build/emu -R M8_ --output-on-failure | tail -3`.** Expected: 8 of 8, or a stop and report at the knob tests.
- [ ] **Step 3: EMU-CHECK.** N = 68.
- [ ] **Step 4: The gate.**
- [ ] **Step 5: Commit** `emu/tests/test_panel.c emu/CMakeLists.txt` (and `emu/core/emu_panel.c` if fixed): "Emulator: panel input tests (E5.2 R1b, M8)", naming the knob finding.

---

### Task R1b.12: The card backend (M9)

§4.8, §12.1 M9: the storage scenario's operations and results, the order, the lock, the card switch, an order-keeping copy that skips dot entries.

**Files:**
- Create: `emu/tests/test_card.c`
- Modify: `emu/CMakeLists.txt`, possibly `emu/core/emu_card.c`, `emu/core/emu_hostfs.c`

**Start from:** new. The draft's `emu_card.c` and `emu_hostfs.c` already ran the boot's storage path (Task 7); nobody has tested the rest.

**Change against the draft:** whatever the tests find, named with the §4.8 rule it breaks.

**Tests** — `emu/tests/test_card.c`. Pure tests: `emu_corePowerOn()`, `emu_cardReset(&cfg)` with `cfg.cardDir` a temp folder, `cfg.warn` recording the warnings, then the `f_*` calls as `presetManager.c` makes them, through the global `SDFile`:

| TEST | Calls | Expected results |
|---|---|---|
| `M9_scenario` | 1 `f_mount(&SDFatFS, SDPath, 1)`; 2 `f_stat("/proj00", &fno)`; 3 `f_mkdir("/proj00")`; 4 `f_mkdir("/proj00")`; 5 `f_chdir("/proj00")`; 6 `f_open(&SDFile, "PRJ.NFO", FA_CREATE_ALWAYS \| FA_WRITE)`, 7 `f_write` of 8 bytes, 8 `f_close`; 9 `f_open(&SDFile, "glo.cfg", FA_CREATE_ALWAYS \| FA_WRITE)`, 10 write bytes 0–29, 11 close; 12 `f_open(&SDFile, "GLO.CFG", FA_READ)`, 13 `f_read` of 30, 14 `f_lseek(&SDFile, f_tell(&SDFile) − 6)`, 15 `f_read` of 6, 16 close; 17 `f_open(…, "NOSUCH.SND", FA_READ)`; 18 `f_open(…, "/NODIR/A.SND", FA_READ)`; 19 `f_open(…, "A+B.SND", FA_READ)`; 20 `f_unlink("glo.cfg")`; 21 `f_mkfs("", 0, 0, NULL, 0)` | 1 `FR_OK`; 2 `FR_NO_FILE`; 3 `FR_OK`; 4 `FR_EXIST`; 5–13 `FR_OK`, 13 reads 30 bytes and `f_tell` is then 30; 14–16 `FR_OK`, 15 reads bytes 24–29; 17 `FR_NO_FILE`; 18 `FR_NO_PATH`; 19 `FR_INVALID_NAME`; 20 `FR_OK`; 21 `FR_DENIED`. On the host: `PROJ00/` holds `PRJ.NFO` only (names upper case on create) |
| `M9_truncate` | create `A.PAT` with 100 bytes; create-always it again with 10 | its size is 10 (truncated in place) |
| `M9_order` | create `03-ZZZZZ.SND`, `01-AAAAA.SND`, `02-MMMMM.SND` in that order; `f_findfirst(&dir, &fno, "", "0?-*.SND")` then `f_findnext` | `03-ZZZZZ.SND`, `01-AAAAA.SND`, `02-MMMMM.SND`: creation order, not name order (§4.8); `f_readdir` gives the same order |
| `M9_skipped` | on the host: `.DS_Store`, `._X.SND`, `.Spotlight-V100/`, `LongName.snd`, `a.snd`; then `f_readdir` twice over the folder | only `A.SND`; one warning naming `LongName.snd`, not two |
| `M9_duplicate` | on the host, `05-OLDER.SND` then `05-NEWER.SND` (the backend's birth order); `f_findfirst("05-*.SND")` | `05-OLDER.SND`; one warning that names both files |
| `M9_lock` | open `A.SND` for reading; `f_opendir("/proj00")`; `f_opendir("/")`; close the file; `f_opendir("/proj00")` | `FR_TOO_MANY_OPEN_FILES`, `FR_OK` (the root is not counted), then `FR_OK` |
| `M9_card_switch` | `emu_inCard(now, 0)`, `HAL_Delay(1)`; `BSP_SD_IsDetected()`; `f_open`, `f_stat`, `f_mkdir`; `emu_inCard(now, 1)`, `HAL_Delay(1)`; `f_mount`; `f_stat("PRJ.NFO")` after a `f_chdir("/proj00")` before the switch | `SD_NOT_PRESENT`; `FR_NOT_READY` ×3; `SD_PRESENT`; `FR_OK`; `FR_NO_FILE` (the mount reset the current folder to the root) |
| `M9_copy` | a source card: `PROJ00/` with `03-Z.SND`, `01-A.SND` in that order, `.DS_Store`, `TMP/TMP.SND`; `emu_hostCopyCard(src, dst, 1)`; list `dst/PROJ00` with `emu_hostList()` | `03-Z.SND`, `01-A.SND` in that order, no dot entry, no `TMP`; with skipTmp 0, `TMP/TMP.SND` is there too |
| `M9_fw_save_load` | firmware: boot with `emu_testCard(…, 120)`; direct calls `preset_saveDrumsetSD(7, 0)` and `preset_loadDrumsetSD(7, 0)` | both `FR_OK`; one new `.SND` file of 255 bytes whose name starts `07-` (or is `p007.SND`, the fallback of §4.8) in the current folder, `/TMP` after the boot |

- The FatFs prototypes and constants come from `ff.h` through the stub `fatfs.h`. Directory objects are `DIR` and `FILINFO` from `ff.h`.
- Add `emu_add_test(test_card IDS M9_scenario M9_truncate M9_order M9_skipped M9_duplicate M9_lock M9_card_switch M9_copy M9_fw_save_load)`.

- [ ] **Step 1: Write the tests; register; run `ctest --test-dir build/emu -R M9_ --output-on-failure | tail -3`.** Fix the backend where a test shows a break of §4.8, one rule at a time.
- [ ] **Step 2: EMU-CHECK.** N = 77.
- [ ] **Step 3: The gate.**
- [ ] **Step 4: Commit** the test, `emu/CMakeLists.txt`, and any fixed backend file: "Emulator: card backend tests (E5.2 R1b, M9)".

---

### Task R1b.13: DIN in and out (M10)

§4.2 (USART3), §4.3, §4.4, §12.1 M10.

**Files:**
- Create: `emu/tests/test_din.c`
- Modify: `emu/CMakeLists.txt`, possibly `emu/core/emu_usart.c`

**Start from:** new. **Change against the draft:** none expected; the draft's model follows §4.2's RX, TX and flag rules.

**Tests** — `emu/tests/test_din.c`. These run the firmware's own `USART3_IRQHandler()`, FIFOs and DIN sync queue (`Uart.c`, `seq_platform.c`) without a boot: `emu_corePowerOn()`, `emu_usartReset()`, `emu_fw.usart3 = USART3_IRQHandler`, `seqp_init()`, `initMidiUart()`, then `__HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE)` unless the test says otherwise. Output goes through `uart_sendMidiByte()`; the DIN sink is `emu_testDinSink`. Entries of the sync queue come from `seqp_syncPop(seqp_now(), &src, &entry, &stamp)`.

| TEST | Inputs | Expected |
|---|---|---|
| `M10_wire_in` | `emu_inDin(1000, {0xF8, 0xFA, 0xF8}, 3)`; `HAL_Delay(2)` | three sync entries, stamped 1000, 70,120 and 139,240 (low 32 bits); `dinIn` 3 |
| `M10_wire_out` | at t = 5000 send `0x90 0x3C 0x64` ×7 then `0xF8` (22 bytes); `HAL_Delay(10)` | 22 sink bytes at 5000 + k × 69,120; the F8 last, at 5000 + 21 × 69,120 = 1,456,520 (6.72 ms: the F8 behind 7 Note Ons, §4.4); exactly 22 bytes (the last is sent once) |
| `M10_tx_enable_late` | send `0x80` at 1000; at 2000 `__HAL_UART_DISABLE_IT(&huart3, UART_IT_TXE)`; at 3000 send `0x81` | `0x80` at 1000, `0x81` at 70,120: the later of the enable (3000) and the wire-free time |
| `M10_rx_during_tx` | send 3 bytes at 1000 (slots at 1000, 70,120, 139,240; the empty slot at 208,360 clears TXEIE); `emu_inDin(70120, {0xF8}, 1)`, exactly a TX slot; `emu_inDin(169240, {0xFA}, 1)`, between two slots (not earlier: the input wire is busy until 139,240) | 3 sink bytes at 1000, 70,120 and 139,240; 2 sync entries at 70,120 and 169,240 (each input decoded once); no other sink byte |
| `M10_tx_from_rx_body` | `emu_fw.usart3` = a wrapper that calls the real handler and, if RXNE was set before it, `uart_sendMidiByte(0xFE)`; `emu_inDin(1000, {0xF8}, 1)` | one sink byte `0xFE` at 1000 (the output byte of an RX body is not lost) |
| `M10_overrun` | RXNEIE clear; `emu_inDin(1000, {0xFA, 0xF8, 0xFC}, 3)`; `HAL_Delay(2)` | `ISR.ORE` set, `dinIn` 1, `dinLost` 2; then `__HAL_UART_CLEAR_OREFLAG`, `__HAL_UART_ENABLE_IT(RXNE)`: the handler runs at once, one sync entry (`0xFA`, Start), the byte left in RDR — the boot step 11 case (§4.2) |
| `M10_spp_and_pc` | `emu_inDin(1000, {0xF2, 0x10, 0x00, 0xC0, 0x05}, 5)`; `HAL_Delay(3)` | two sync entries in this order: the SPP with value 16, the Program Change with value 5 (N14); each stamped with the time of the byte that `seqq_dinByte()` keeps the stamp of |

- `seqq_entry_t`'s fields, the sync event types and which byte's stamp a message keeps are in `seq_syncq.h`, `seq_syncq.c` and `seq_core.h`; read them for the exact names and assert the byte times (1000 + k × 69,120 for byte k).
- Use `emu_threadAdvance(cycles)` (in `emu_internal.h`) to reach a time like 5000 that is not a whole millisecond.
- Add `emu_add_test(test_din IDS M10_wire_in M10_wire_out M10_tx_enable_late M10_rx_during_tx M10_tx_from_rx_body M10_overrun M10_spp_and_pc)`.

- [ ] **Step 1: Write the tests; register; run `ctest --test-dir build/emu -R M10_ --output-on-failure | tail -3`.** Expected: 7 of 7.
- [ ] **Step 2: EMU-CHECK.** N = 84.
- [ ] **Step 3: The gate.**
- [ ] **Step 4: Commit** `emu/tests/test_din.c emu/CMakeLists.txt`: "Emulator: DIN tests through the firmware's USART3 handler (E5.2 R1b, M10)".

---

### Task R1b.14: USB in and out (M11)

§4.3 (USB), §4.4 (USB), §12.1 M11.

**Files:**
- Create: `emu/tests/test_usb.c`
- Modify: `emu/core/emu_usb.c`, `emu/CMakeLists.txt`

**Start from:** new.

**Change against the draft:**
- **The 1 ms window starts at its first packet** (§4.3: "a packet that would be the 17th within 1 ms of the first packet of that window waits until the window ends"). The draft's first window starts at cycle 0, at power-on, whether or not a packet came: 16 packets at 0.5 ms and one more at 0.9 ms let the 17th in at 1.0 ms instead of 1.5 ms. Later windows do start at a transfer. In `emu_usbFire()`, replace the window test with `if (emu_usb.windowPackets == 0u || now >= emu_usb.window + EMU_CYCLES_PER_MS) { emu_usb.window = now; emu_usb.windowPackets = 0; }`.
- **Transfers stay whole** (resolved here): §4.3 counts packets, but an incoming transfer is one OTG_FS out body. A transfer whose packets would pass 16 in the window waits, whole, until the window ends.

**Tests** — `emu/tests/test_usb.c`. Without a boot: `emu_corePowerOn()`, `emu_usbReset()`, `emu_fw.usbMessageIn = seqp_usbMessageIn`, `emu_fw.usbTxComplete = usb_midiTxComplete`, `seqp_init()`; a USB sink records each transfer's time and length.

| TEST | Inputs | Expected |
|---|---|---|
| `M11_walk` | one transfer at 1000: `09 90 3C 64`, `05 F8 00 00`, `1F F8 00 00` (cable 1), `0F FA 00 00`, `00 00 00 00`, `09 90 3E 64` | `usb_MidiMessages` gets 3 messages: Note On 3C (length 2), F8 (length 0), FA (length 0); the cable-1 packet and everything after the zero packet are skipped; 2 sync entries (F8 with CIN 0x5, FA), both stamped 1000 |
| `M11_window_16` | three transfers of 8 packets each at 1000 | the first two enter at 1000, the third at 1000 + 216,000 |
| `M11_window_start` | one transfer of 16 packets at 108,000 (0.5 ms), one of 1 packet at 194,400 (0.9 ms) | the second enters at 108,000 + 216,000 = 324,000 (the draft: 216,000) |
| `M11_out` | `hUsbDeviceFS` as power-on leaves it; `usb_sendMidi()` 20 Note Ons at 1000 | sink transfers of 60 bytes (15 packets) at 1000 and 20 bytes at 1000 + 216,000; `TxState` 1 between them |
| `M11_no_stall_drop` | one `usb_sendMidi()` every 10 ms for 2 s | 200 transfers, `usb_midiTxDropped()` 0 |

- `usb_sendMidi()`'s argument type is in `usb_manager.h`.
- Add `emu_add_test(test_usb IDS M11_walk M11_window_16 M11_window_start M11_out M11_no_stall_drop)`.

- [ ] **Step 1: Write the tests; register; run them and see `M11_window_start` fail on the draft** (the second transfer enters at 216,000).
- [ ] **Step 2: Apply the window change; run `ctest --test-dir build/emu -R M11_ --output-on-failure | tail -3`.** Expected: 5 of 5.
- [ ] **Step 3: EMU-CHECK.** N = 89.
- [ ] **Step 4: The gate.**
- [ ] **Step 5: Commit** `emu/core/emu_usb.c emu/tests/test_usb.c emu/CMakeLists.txt`: "Emulator: USB tests; the 1 ms window starts at its first packet (E5.2 R1b, M11)".

---

### Task R1b.15: The output jacks and the sync jacks (M8 output jacks, M12)

§4.5, §4.6 (output jacks), §12.1 M8 ("the output jacks: for each single unplugged jack and for L1 and R1 unplugged together, one voice routed to an unplugged output sounds, by per-channel energy, on the output that `mixer_checkOutJackAvailable()` picks for that state and nowhere else") and M12.

**Files:**
- Create: `emu/tests/test_jacks.c`
- Modify: `emu/CMakeLists.txt`, possibly `emu/core/emu_jacks.c`

**Start from:** new. **Change against the draft:** none expected.

**Tests** — `emu/tests/test_jacks.c`:

| TEST | Kind | Inputs | Expected |
|---|---|---|---|
| `M12_clock_in` | pure + `TriggerOut.c` | `emu_corePowerOn()`, `emu_jacksReset()`, `emu_fw.exti4 = EXTI4_IRQHandler`, `seqp_init()`, `seq_init()`; `HAL_NVIC_SetPriority(EXTI4_IRQn, 3, 0)`, `HAL_NVIC_EnableIRQ(EXTI4_IRQn)`; for sync mode 1 at PPQ 4 and at PPQ 24 (`seq_setExtSync(1)`, `seq_setClockInPpq()`), and for sync modes 0 and 2: 16 `emu_inClockPulse()` 10 ms apart | mode 1: 16 `SEQC_EV_PULSE` jack entries, stamped at the pulse times, at either PPQ; modes 0 and 2: none |
| `M12_reset` | same, with `EXTI9_5_IRQHandler` | `emu_inReset(t, 1)` then `(t + 10 ms, 0)` in sync mode 1 | `GPIOD->IDR` bit 5 low, then high; two reset entries in order with the pulses; an edge before `HAL_NVIC_EnableIRQ(EXTI9_5_IRQn)` runs the body once at the enable |
| `M12_clock_out_core` | pure | a fake pass that sets `GPIOC->ODR` bit 13 in pass n and clears it in pass n + 20; another that sets and clears it in one pass | rising and falling edges at those passes' times; the one-pass pulse gives no edge (§4.5) |
| `M12_fw_clock_out` | firmware | boot without a card (120 BPM, §4.10's `seq_setBpm(120)`), press START (button 33, 50 ms); run 2 s | 16 ± 1 rising edges (4 PPQ out, `PRE_4_PPQ_OUT`: 8 per second), each high for 5.21 ms ± 0.25 ms (one 96-PPQN pulse on the 250 µs pass grid) |
| `M8_fw_output_jacks` | firmware | boot without a card; mute voices 2–7 (`seq_setMute()`); route voice 1 to one output (`mixer_audioRouting[0]`, the values in `mixer.h`); press START; for each state — L1, R1, L2, R2 unplugged alone (the routed output among them), and L1 with R1 — set `emu_inJacks()`, run 50 ms, then measure the RMS of each of L1, R1, L2, R2 over 1 s of the audio sink | the energy is on the channel or channels that `mixer_checkOutJackAvailable(dest)` returns for that state (call it in a direct call after the jack change) and below −90 dBFS on the others; with all jacks plugged the routing is unchanged |

- For `M8_fw_output_jacks`, pick the routed output so that each case moves it: route to L1 for the L1, R1 and L1+R1 cases, and to L2 for the L2 and R2 cases. If the default kit's voice 1 is silent, raise its volume with `frontPanel_sendData(MIDI_CC, PAR_VOL1, 127)` in a direct call.
- Add `emu_add_test(test_jacks IDS M12_clock_in M12_reset M12_clock_out_core M12_fw_clock_out M8_fw_output_jacks)`.

- [ ] **Step 1: Write the tests; register; run `ctest --test-dir build/emu -R 'M12_|M8_fw_output' --output-on-failure | tail -3`.** Expected: 5 of 5.
- [ ] **Step 2: EMU-CHECK.** N = 94.
- [ ] **Step 3: The gate.**
- [ ] **Step 4: Commit** `emu/tests/test_jacks.c emu/CMakeLists.txt`: "Emulator: output-jack routing and sync-jack tests (E5.2 R1b, M8, M12)".

---

### Task R1b.16: The firmware changes in the emulator (M15) and the end check

§7, §12.1 M15: F1 (CC 1 with nodes at destination 0), F2 (morph after a boot without a card), F3 (the clear-page LCD equal to the base build's), F9 (no read past the tables under ASan).

**Files:**
- Create: `emu/tests/test_fwchanges.c`
- Modify: `emu/CMakeLists.txt`

**Start from:** new.

**F3's base snapshots.** §7's acceptance compares with the base build, built with `-D_FORTIFY_SOURCE=0`. Do that once here and keep the rows in the test as constants. The base is the commit before F3, `95adb57`; a scratch worktree of it gets this task's `emu/`, so the worktree's `Core/` stays untouched. `M15_f3_clear_pages` prints its four rows, quoted, when `PRINT_ROWS` is defined, and skips its comparison then:

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && T=$(mktemp -d) && git worktree add -q --detach "$T/base" 95adb57 && cp -R emu "$T/base/" && cmake -S "$T/base/emu" -B "$T/build" -G Ninja "-DCMAKE_C_FLAGS=-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DPRINT_ROWS" >/dev/null && cmake --build "$T/build" --target test_fwchanges >/dev/null && "$T/build/test_fwchanges" M15_f3_clear_pages; git worktree remove --force "$T/base"; rm -rf "$T"
```

Expected: four quoted rows, row 0 of each page beginning `clear [autom. 1]` and `clear [autom. 2]` (U6), then `PASS M15_f3_clear_pages`. Put them into the test.

**F9 under ASan.** The sandbox cannot run ASan (Rules). Here `M15_f9_band10` runs under UBSan, whose bounds check stays on in `calcWavetableOscBlock()` and `calcFmBlock()` (Task 2's ignorelist names only the PWM functions) and fires on any `table[row][1024]`, a stricter test than ASan's. CI's `address,undefined` job (R3) runs the same id under ASan.

**Tests** — `emu/tests/test_fwchanges.c`, each test a boot without a card:

| TEST | Inputs | Expected |
|---|---|---|
| `M15_f1_cc1` | after the boot, `emu_inDin()` of `B0 01 7F` on all 16 channels (`B0`…`BF`), run 200 ms | no fault; the passes go on (more than 600 in the 200 ms) |
| `M15_f2_morph` | `morphParameters == mainParameters[0]` after the boot; direct call `preset_morph(127)`, run 200 ms; `preset_morph(0)`, run 200 ms | the pointer test holds; no fault; the passes go on. (No `preset_morphBusy()` here: the scratch builds of Steps 1 and 3 predate F10 and must link.) |
| `M15_f3_clear_pages` | direct call `copyClear_armClearMenu(1)`, `copyClear_setClearTarget(CLEAR_AUTOMATION1)`, `menu_repaintAll()`; run 20 ms; snapshot; the same with `CLEAR_AUTOMATION2` | both rows of each page equal the base build's constants |
| `M15_f9_band10` | voice 1: saw wavetable waveform, coarse 127, pitch modulation amount 127, pitch EG decay 64, pitch slope 64 (U8's recipe; `frontPanel_sendData(MIDI_CC or CC_2, PAR_…, value)` in a direct call; find the parameter names in `Parameters.h` next to `PAR_COARSE1`, `PAR_MODAMNT1` and `PAR_PITCH_SLOPE1`); trigger voice 1 16 times, 100 ms apart, with a Note On on its channel; then the same with the triangle; then with each FM waveform that uses `calcFmBlock()` | no sanitizer report; the voice sounds (RMS above −60 dBFS on its output) |

- Add `emu_add_test(test_fwchanges IDS M15_f1_cc1 M15_f2_morph M15_f3_clear_pages M15_f9_band10)`.

- [ ] **Step 1: Record F3's base rows (above).**
- [ ] **Step 2: Write the tests; register; run `ctest --test-dir build/emu -R M15_ --output-on-failure | tail -3`.** Expected: 4 of 4.
- [ ] **Step 3: See F9's test fail without F9.** The same scratch method at `eb08240`, the commit before F9 (F7): run the command above with `95adb57` replaced by `eb08240`, without the `CMAKE_C_FLAGS` option, and with `M15_f9_band10` as the test. Expected: UBSan's `index 1024 out of bounds for type 'const int16_t[1024]'` at `Oscillator.c:175` or `:471`, and no `PASS` line.
- [ ] **Step 4: EMU-CHECK.** N = 98.
- [ ] **Step 5: The end check.** Run `cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && cmake -S emu -B build/emu-release -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build/emu-release 2>&1 | tail -1 && ctest --test-dir build/emu-release 2>&1 | tail -n 3`. Expected: `100% tests passed, 0 tests failed out of 98` at `-O2` without sanitizers (M19's canary and every test hold at `-O2` too). Then `tools/check.sh && tools/fw_checks.sh`: the gate, and nine `PASS` lines. `git worktree list` shows no scratch worktree.
- [ ] **Step 6: Commit** `emu/tests/test_fwchanges.c emu/CMakeLists.txt`: "Emulator: the firmware changes F1, F2, F3 and F9 in the emulator (E5.2 R1b, M15)", body: CC 1 with every node at destination 0, a morph after a boot without a card, the clear-automation pages equal to the base build's (built with `-D_FORTIFY_SOURCE=0`, rows kept in the test), and a voice in band 10 without a read past the tables (UBSan here, ASan in CI).

---

## The end state, which R2 starts from

- Branch `emulator`, local only, 16 more commits. `tools/check.sh` passes 55 ctest ids with 0 warnings; `emu/` passes 98 in Debug (UBSan) and in Release.
- `emu_core.h` is the tiers' interface: `emu_configDefaults()`, `emu_powerOn()` once per process, `emu_runUntil()`, `emu_directCall()`, the `emu_in*()` inputs, the sinks, the hook with `emu_waitBegin()` and `emu_waitEnd()`, `emu_lcdSnapshot()`, `emu_ledSnapshot()`, `emu_getCounters()`, `emu_watchdogPoll()`, `emu_trailWrite()`.
- `emu/CMakeLists.txt` gives R2 `EMU_DEVICE_OBJECTS`, `emu_fw_headers` and `emu_add_test()`. R2 adds `lichomat-render`, the strings of `name_allow.txt` and the binary to M21; R3 adds the `check.sh` step and `emulator.yml`.
- The renderer's end of render is R2's: the spec's hook "ends the render at its end time" (§5.3, step 7) by closing the outputs and exiting the process from the hook, so the core needs no stop call.

## Points for the owner and for the controller

- **B13 and B14 run in the emulator.** The PWM oscillators' read of `sawTable[row][1024]` trips UBSan on every boot, so `emu/sanitize/pwm_ignorelist.txt` turns off the bounds check in `calcPwmFmBlock()` and `calcPwmBlock()` until the owner's listening check with F9 (U8, Q13). A row-10 read still shows under ASan in CI.
- **The knob count.** If Task 11 confirms that the firmware under §4.6's wiper model gives about 96 edits per turn on COA rather than 134, the spec's figure and Q16's "about 134 steps per turn" need the owner.
- **`encoder +10` on a 4-item page** cannot move 10 items; M8's sub-case runs on the kit load page.
- **The libm's licence** is `MIT OR Apache-2.0 WITH LLVM-exception`; the emulator uses it under MIT, as §10.1 says. The licence gate allows `emu/libm/` by path.
