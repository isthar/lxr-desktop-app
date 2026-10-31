# LICHOMAT Stage 1b, R4: LICHOMAT Emu.app (emulator tier 2) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build tier 2 of spec §6, `LICHOMAT Emu.app`: the firmware thread with its rings, CoreAudio out with the time map, CoreMIDI in and out with the slave delay D, the card folder with import and export, power cycle, crash note, settings and diagnostics, the clock-out LED, the clock-in button with its pulse generator and the reset switch, then the Dear ImGui panel, the About box, the bundle with the nested `lichomat-render`, and the CI wiring. ctest then covers M18 of §12.1, and the owner's checks A1–A11 of §12.3 can run.

**Architecture:** Everything that can run without a device is plain C11 under `emu/app/engine/`: the rings, the time map, L and D, the MIDI conversions, the card folder, settings, the crash note, and the engine, which is the firmware thread's code (tier hook, slices, sinks, commands). The engine reaches the outside only through a host interface (`app_host_t`), so M18 runs it on one thread with a fake clock and a fake output device. The CoreAudio, CoreMIDI and Mach-thread glue is C under `emu/app/mac/`; a shell (`app_shell`) wires it all and is driven first by a non-shipped terminal harness (`lichomat-emu-dev`), later by the Cocoa and Metal GUI (`emu/app/gui/`), the only Objective-C++.

**Tech Stack:** C11 (gnu11) with blocks, Apple clang 17 from the Command Line Tools; AudioToolbox (AUHAL), CoreAudio, CoreMIDI (MIDI 1.0 UMP API of macOS 11), Mach semaphores and `thread_policy_set`; Objective-C++ with ARC, AppKit, Metal and MetalKit, Dear ImGui v1.92.9b with its OSX and Metal backends; CMake 3.31, Ninja, ctest; Python 3 for test drivers; GitHub Actions `macos-15`.

---

The spec is `docs/superpowers/specs/2026-10-27-emulator-design.md`. A `§` is a section of it. The tier-1 plan is `docs/superpowers/plans/2026-10-29-emulator-r2-r3-renderer-goldens.md`; its section "The end state, which R4 starts from" holds. R5 (§6.8's two audio options) is not in this plan.

## Decisions taken while planning

| # | Decision | Why |
|---|---|---|
| E1 | A non-shipped terminal harness, `lichomat-emu-dev` (C), drives the app before the panel. It is not in the bundle or the CI artifact | It needs no Cocoa, runs headless in CI, and lets A4–A11 be tried before Dear ImGui is vendored |
| E2 | CoreAudio and CoreMIDI glue is C with blocks; Objective-C++ appears only in `emu/app/gui/` | Both APIs are C; the glue stays in the C warning gate and is callable from C tests |
| E3 | The engine reaches time, waits, the output device and the MIDI-out wake only through `app_host_t` | M18 then drives the real firmware-thread code single-threaded, deterministic, with a fake device that calls the same `app_audioRender()` as the callback |
| E4 | The plain C units (rings, time map, MIDI, engine, card, settings, crash) and their tests build in every configuration; `LICHOMAT_EMU_APP` gates the framework glue, the harness, the GUI and the bundle | §9.3's sanitizer job builds "without the app" and still tests the timing model under ASan |
| E5 | The audio ring holds raw 4-channel int16 frames (L1 R1 L2 R2); the callback applies the mix | A mix change is then instant and leaves L unchanged (§6.4); only the 4-channel mode reopens the unit |
| E6 | Until the first callback, the largest request is the nominal one, ⌈buffer × 44,140.625 / device rate⌉ | The ring wait of §6.2 needs a bound before the first callback |
| E7 | L is fixed after the first 32 callbacks of an open, from their largest request and largest `mHostTime` lead; a later, larger request counts as a buffer change and recomputes L (and D, if D is automatic) | §6.4: D then stays fixed and the shown D is constant |
| E8 | A packet timestamp of 0 becomes the host time of that read-proc call, one value per call | Equal stamps then still share a transfer (§6.4), and the entry rule needs a time |
| E9 | The 100 ms drop of §4.3 applies to events stamped more than 100 ms before the last re-anchor or reopen | Applied always, it would drop every event at buffers of about 2,048 frames and more, where the render lead exceeds 100 ms |
| E10 | A MIDI ring entry is cut, at message boundaries and with the same stamp, at 16 USB-MIDI packets' worth: 1 per MT 1 or MT 2 message, 3 per SysEx packet of up to 6 bytes (so at most 16 words). An entry that still does not fit an empty transfer (a malformed SysEx) is counted in `midiUnsent`, never dropped silently | A transfer then carries up to 16 packets, as §4.3 says, and never splits an entry |
| E11 | The MIDI-out thread hands DIN messages to CoreMIDI as soon as they reach the ring's head, stamped with their presentation time; USB waits until its presentation time | §6.5: CoreMIDI schedules DIN by the stamp; receivers of the virtual source may ignore stamps |
| E12 | D's sync condition reads `seq_getExtSync()` once per slice by a direct call (`app_fw.c`, the app's only file with firmware headers) | The hook never calls the firmware (§3.3) |
| E13 | The clock-in button and the reset switch carry the click's host time and take the entry rule on a third port, `APP_PORT_JACK`; the pulse generator runs in virtual time from its command's slice | §6.4 applies D to jack input; a generator needs no map |
| E14 | Every GUI command is applied between slices, at a pass boundary | The card switch must not land inside a pass (§6.7); one rule for all commands |
| E15 | A snapshot (LCD, LEDs, clock-out latch, diagnostics) goes out every 8 blocks (11.6 ms) and every 10 ms in time hooks | More than 60 per second; the LED latch keeps short pulses (§4.7). The diagnostics travel in it |
| E16 | Import, export card and export project use the app's own ordered copier: source order is `readdir()` order on msdos or exfat volumes, the backend's order elsewhere; birth times are set in order, and a failure to set them is ignored only on a FAT destination. All three take the virtual card out first | §6.7 step 3 and §4.8. `emu_hostCopyCard()` knows only the backend order and fails where `setattrlist()` fails |
| E17 | Backups are `Card backups/YYYYMMDD-HHMMSS` in local time, then `-2`, `-3`, … when taken; a replaced project goes to `YYYYMMDD-HHMMSS-PROJnn` | Two imports within one second; one folder for both kinds |
| E18 | Power cycle: card out, settings saved, MIDI endpoints disposed, then a new process: `execv()` in the harness, `NSWorkspace openApplicationAtURL:` with `createsNewApplicationInstance` and `--after-pid N` in the app, which waits up to 5 s for that PID before it creates its endpoints | No write is cut; the stored unique IDs are free again (§6.9) |
| E19 | Settings are a UTF-8 `key = value` file, `settings.txt`, written to a temporary file and renamed; unknown keys are kept | No Foundation in C; R5's options can be added without losing them |
| E20 | The crash note's time is CLOCK_REALTIME plus a UTC offset that the main thread stores at start and every minute; the date comes from days-from-civil arithmetic; the shown note's name is kept in the settings | `localtime_r()` is not async-signal-safe; the name format must stay `crash-YYYYMMDD-HHMMSS.txt` |
| E21 | `LICHOMAT_EMU_HOME` overrides `~/Library/Application Support/LICHOMAT Emu` | Tests and the harness never touch the user's folder |
| E22 | Test ids: `M18_*` for the timing model, `R4_*` for the rest. Device tests fork a probe, give it 5 s and exit 77 (ctest `SKIP_RETURN_CODE`) when there is no device or server, or no answer | CI has no audio device; the sandbox may refuse or hang CoreAudio and CoreMIDI |
| E23 | Dear ImGui compiles with `-w`, its `.mm` backends and our `.mm` with `-fobjc-arc`, C++17; `IMGUI_DISABLE_DEFAULT_FONT` is defined and Menlo is loaded at run time through CoreText | No third-party warnings in our count; no embedded font blob whose printable run M21's name scan could trip on, and no font licence to ship |
| E24 | The LCD's ASCII glyphs come from a 5×7 dot table written for this plan (`panel_font.c`), plus the two arrows and the hollow box | Q18's generic font; nothing to license |
| E25 | No app icon; `NSMicrophoneUsageDescription` joins `Info.plist` in R5 with the option that needs it | §10.2: no artwork; R4 opens no input |
| E26 | `emu/common/emu_notices.h` holds the tagline, the legal lines and `EMU_FIRMWARE_CHANGES` = `F1-F18 and F6b`, for `--version` and the About box | One source for both binaries; F13–F18 landed in R3, so "F1-F12 and F6b" is stale (§10.1 wants the state it runs) |
| E27 | The app's seed is CLOCK_REALTIME's seconds and nanoseconds mixed with `mach_absolute_time()` | §4.9: seeded from the wall clock; `time()` is shadowed by `graphic.c`'s `float time` |
| E28 | DIN bindings are re-checked once per second by the shell (`app_bindPick()`: stored unique ID first, then the stored name) | No CoreMIDI notification run loop is needed, in the harness or the GUI |
| E29 | If `thread_policy_set()` refuses the time-constraint policy, the thread runs at default priority and the diagnostics say so | A refusal must not stop the app |
| E30 | The bundle's version is the firmware version that the splash shows (`EMU_FIRMWARE_VERSION`, 0.1) | The release tag is `emu-v0.1` (§10.3) |
| E31 | The GUI tasks (11–13) stay last, although the owner has approved the download | Tasks 1–10 must build and pass without Dear ImGui; the order needs no permission any more, so Task 11 may also run earlier, alone |

Note to E26: §10.1's wording "LICHOMAT 0.1 with the emulator changes F1–F12 and F6b" is superseded by "F1-F18 and F6b" in both binaries.

## Rules for every task

- **Where.** Work in `/Users/tk/Code/lxr/build/wt-emulator`, branch `emulator`, head `570090b` at the start. Every Bash call starts in a new shell: begin with `cd /Users/tk/Code/lxr/build/wt-emulator &&`, and add `source tools/env.sh >/dev/null &&` where `cmake`, `ctest` or `ninja` is needed.
- **No `Core/` changes.** No task needs one. A firmware bug found here stops the task and goes to the controller (§7).
- **Files.** New files carry the GPL-3.0-only header of R1's files (`Copyright (C) 2026 Tomasz Krzywicki`, the SPDX line, the NOTICE.md sentence; `#` comments in scripts). The code blocks below leave the header out. Everything under `emu/`, `tools/` and `.github/` uses LF; the vendored Dear ImGui files stay byte for byte as fetched.
- **Only `emu_core.h`.** App code calls the virtual device only through `emu/core/emu_core.h` and `emu_hostfs.h`. `app_fw.c` is the only app file that includes firmware headers, and it runs only through `emu_directCall()`.
- **Never in the callback, the listeners or the read procs:** a lock, `malloc`, a file call, `printf`, an Objective-C message. They push into a ring and may signal one Mach semaphore.
- **Sanitizers.** Never configure `address` locally: ASan binaries hang in this sandbox. The Debug default is `undefined`.
- **`time()`.** Never call it (`Core/Src/graphic.c:25` defines `float time`). Wall time comes from `clock_gettime()` or `mach_absolute_time()`.
- **Device tests** skip with exit code 77 and a line `SKIP <id>: <reason>`; they never fail for a missing device or a refused server, and never wait longer than 5 s for one.
- **Test names.** No `TEST()` name equals another's plus `_` and more (the filter of `tests/seqc_test.h`).
- **Measured values.** A value a task must find (a count, a time) is measured, checked against the rule given here and written into the commit message. Never widen a tolerance to make a test pass; report the value instead.
- **Git.** `git add` names each file; never `git add -A`, never `--amend`. Each commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. After a task's commit, `git push origin emulator` is allowed (Q19); never force-push, never touch `main` or `upstream`.
- **Hardware and owner checks** are the owner's. Never mark A1–A11 as done.

**EMU-CHECK**, the command every task runs:

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && cmake -S emu -B build/emu -G Ninja > build/emu.log 2>&1 && cmake --build build/emu >> build/emu.log 2>&1; echo "== emu build: $?"; echo "== emu warnings: $(grep -c 'warning:' build/emu.log)"; ctest --test-dir build/emu -j8 2>&1 | grep -E 'tests passed|tests failed'
```

Expected: `== emu build: 0`, `== emu warnings: 0`, `100% tests passed, 0 tests failed out of N` with the task's N (device tests count as passed when skipped; ctest lists them as "Not Run").

**The gate** of every task: EMU-CHECK as expected; `tools/check.sh` prints the host line `100% tests passed, 0 tests failed out of 57`, the emulator line with the task's N, the image line with `OK` and `== compiler warnings: 0`.

## Facts the tasks rely on

Read at `570090b` while planning.

- **Counts.** ctest has 164 emulator ids in Debug and 165 in Release (`M14_speed`), and 57 host tests.
- **Blocks and frames** (`emu_codec.c:28–48`). Block request k (k = 1, 2, …) comes at `codecStart + emu_framesToCycles(64k)`; its audio sink call carries the block that starts playing then, frames 64k…64k+63. `blocks` and `underruns` are counted before the sink; the tier hook (`EMU_HOOK_BLOCK`) follows it. Virtual frame f is at `codecStart + f × 552,960 / 113` cycles.
- **Inputs** (`emu_sched.c:240–272`). An input is a heap entry at its time; one with a time before `emu_now()` enters at `emu_now()` and counts as `late`. `emu_inCard(emu_now(), 0)` between two `emu_runUntil()` calls is applied before the next pass starts (`emu_card.c:315–320`); `BSP_SD_IsDetected()` then returns `SD_NOT_PRESENT` (0).
- **`emu_ledSnapshot()`** returns 'on at any time since the previous call' and resets that; only the engine may call it.
- **The renderer's notices** are static strings in `render_main.c:224–247`, and `render_test.py:185` expects "Runs LICHOMAT %s with the emulator changes F1-F12 and F6b." F13–F18 are on `emulator` since R3 (E26).
- **The licence gate** (`emu/tests/licence_gate.py`) checks every repository file in `ninja -t deps` (GPL-3.0-only or its `ALLOWED` list), requires `-ivfsoverlay <build>/main_overlay.yaml` in every compile command but the libm and canary ones, and scans every non-code section of each binary passed to it for `lxr|erica|sonic\s*potions`.
- **`emu_fw_headers`** adds `-Dsection(x)=unused`, which must never reach a file that includes Apple framework headers (§3.4). `emu_test_options` carries it, so tests that include CoreMIDI or CoreAudio headers get their own options.
- **Sync values.** `GLO.CFG` byte 0 is 0 for EXT, 1 for MID, else a tempo; `seq_getExtSync()` then returns 1 (jack), 2 (MIDI) or 0 (internal). The fixture `emu/tests/cards/init` plays steps 1, 5, 9 and 13 on voices 1–7, and its TX filter sends Note Ons on DIN and USB (G5).
- **CoreMIDI.** `MIDIEventPacketNext()` is inline in `MIDIServices.h`, so a file that walks a `MIDIEventList` needs the header only; `MIDIEventListInit()` and `MIDIEventListAdd()` are library functions that need no MIDI server. The plan does not rely on `MIDIEventListAdd()` merging equal stamps into one packet: `app_usbTransfer()` groups by equal stamps.
- **§6.2–§6.4's measurements** (this Mac: built-in output at 48 kHz, 512 frames, `mHostTime` lead 12.74 ms, latencies 60 + 690 frames, mach timebase 125/3 ns) are the fake device's numbers in M18.

## File structure

| File | Responsibility | Task |
|---|---|---|
| `emu/app/engine/app_ring.{h,c}` | SPSC ring of fixed-size entries | 1 |
| `emu/app/engine/app_timemap.{h,c}` | Time map and inverse, L, the entry rule | 2 |
| `emu/app/engine/app_midi.{h,c}`, `app_midilist.{h,c}` | UMP ↔ USB-MIDI, UMP → bytes, DIN assembler, event list → entries, transfer grouping | 3 |
| `emu/app/engine/app_types.h`, `app_engine.{h,c}`, `app_fw.{h,c}` | The plain types the GUI shares (C++-safe); the firmware thread: slices, hook, sinks, rings, commands, snapshots, MIDI-out step | 4, 5 |
| `emu/app/engine/app_card.{h,c}`, `app_time.{h,c}` | Paths, seeding, import, export, backups; local time and stamped names | 9 |
| `emu/app/engine/app_settings.{h,c}`, `app_crash.{h,c}`, `app_bind.{h,c}` | Settings file, crash note, DIN rebinding | 10, 10, 8 |
| `emu/app/mac/app_mach.{h,c}` | Host time, semaphores, real-time threads, firmware, MIDI-out and watchdog threads | 6 |
| `emu/app/mac/app_coreaudio.{h,c}`, `app_coremidi.{h,c}` | AUHAL output and listeners; virtual ports, DIN ports, sending | 7, 8 |
| `emu/app/mac/app_shell.{h,c}` | Start-up, per-frame work, card operations, power cycle; an opaque C API for the harness and the GUI | 6, grows to 10 |
| `emu/app/dev/dev_main.c` | `lichomat-emu-dev` | 6, grows to 10 |
| `emu/third_party/imgui/` | Dear ImGui v1.92.9b, 15 files and `README.md` | 11 |
| `emu/app/gui/main.mm`, `panel.{h,cpp}`, `panel_font.{h,c}`, `Info.plist.in` | Cocoa shell, panel, About, font, plist | 12, 13 |
| `emu/common/emu_notices.h` | Tagline, legal lines, firmware state | 12 |
| `emu/tests/test_app_*.c`, `app_fake.h`, `app_test.py` | Tests | 1–13 |
| `emu/CMakeLists.txt`, `emu/tests/licence_gate.py`, `emu/tests/name_allow.txt`, `tools/check.sh`, `.github/workflows/emulator.yml`, `tools/ci_dry_run.rb` | Build, gate, CI | 1, 6, 11–13 |

## Task order

| Task | Contents | ids after it (Debug / Release) | Serves |
|---|---|---|---|
| 1 | Build skeleton, SPSC ring | 167 / 168 | M18 |
| 2 | Time map, L and D, entry rule | 173 / 174 | M18; A5, A6 |
| 3 | MIDI conversions, event lists | 180 / 181 | M18; A4–A7 |
| 4 | Engine I: firmware thread, audio ring, pacing, commands, snapshots, generator | 187 / 188 | M18; A1, A8, A11 |
| 5 | Engine II: MIDI in and out, M18's scenarios | 193 / 194 | M18; A4–A7 |
| 6 | Mach host, threads, watchdog, shell, `lichomat-emu-dev` | 194 / 195 | A1, A11 |
| 7 | CoreAudio output | 195 / 196 | A2, A11 |
| 8 | CoreMIDI, MIDI-out thread, bindings | 197 / 198 | A4–A7, A10 |
| 9 | Card folder: first run, import, export | 205 / 206 | A2, A9 |
| 10 | Settings, crash note, power cycle, diagnostics | 210 / 211 | A10, A11 |
| 11 | Vendor Dear ImGui — **approved by the owner (2026-10-31 06:51)**; may run at any point, nothing before Task 12 needs it | 211 / 212 | A1 |
| 12 | GUI: panel, About and notices, splash | 212 / 213 | A1, A3, A8 |
| 13 | Bundle, signing, M21, `check.sh`, CI | 213 / 214 | A1; §9.2, §9.3 |

Without the app (`-DLICHOMAT_EMU_APP=OFF`, CI's sanitizer job) the eight ids `R4_dev_headless`, `R4_dev_coreaudio`, `R4_dev_coremidi`, `R4_dev_import`, `R4_dev_crash`, `R4_dev_power`, `R4_panel_font` and `R4_bundle` are not defined: 205 at the end.

---

### Task 1: The build skeleton and the SPSC ring

**Files:**
- Modify: `emu/CMakeLists.txt` (a new section right before the comment `# The temporary folders: every test above gets TMPDIR`)
- Create: `emu/app/engine/app_ring.h`, `emu/app/engine/app_ring.c`, `emu/tests/test_app_ring.c`

- [ ] **Step 1: The CMake section.** Insert:

```cmake
# --- LICHOMAT Emu, tier 2 (spec 6, 9.2; plan R4) ---------------------------------------
# The app's plain C units and the engine build in every configuration (plan E4): they
# need no framework, so the sanitizer job tests them. LICHOMAT_EMU_APP adds the CoreAudio
# and CoreMIDI glue, the dev harness, the GUI and the bundle.
option(LICHOMAT_EMU_APP "Build LICHOMAT Emu.app, its device glue and the dev harness" ON)
set(EMU_APP_DIR "${EMU_DIR}/app")
set(EMU_APP_FLAGS ${EMU_SAN_FLAGS} -Wall -Wextra -Werror "SHELL:-ivfsoverlay ${EMU_OVERLAY}")
set(EMU_APP_UNIT_SOURCES "${EMU_APP_DIR}/engine/app_ring.c")
add_library(emu_app_units OBJECT ${EMU_APP_UNIT_SOURCES})
target_include_directories(emu_app_units PUBLIC "${EMU_APP_DIR}/engine" "${EMU_DIR}/core" "${EMU_DIR}/common")
target_compile_options(emu_app_units PRIVATE ${EMU_APP_FLAGS})

# The options of the app's tests: no emu_fw_headers, whose section define must not reach
# Apple's headers (spec 3.4)
add_library(emu_app_test_options INTERFACE)
target_include_directories(emu_app_test_options INTERFACE "${EMU_APP_DIR}/engine" "${EMU_APP_DIR}/mac"
    "${EMU_DIR}/common" "${EMU_DIR}/core" "${EMU_DIR}/tests" "${LXR_ROOT}/tests" "${CMAKE_BINARY_DIR}")
target_compile_options(emu_app_test_options INTERFACE ${EMU_APP_FLAGS})
target_compile_definitions(emu_app_test_options INTERFACE "EMU_TEST_CARDS=\"${EMU_DIR}/tests/cards\"")

# emu_add_app_test(<name> [ENGINE] [MAC] [FRAMEWORKS <f>...] IDS <id>...): emu/tests/<name>.c
# with the app's units; ENGINE adds the engine and the whole virtual device; MAC the glue.
# The targets named here appear in Tasks 4 and 6; a generator expression is only
# resolved for a test that uses it.
function(emu_add_app_test name)
    cmake_parse_arguments(ARG "ENGINE;MAC" "" "IDS;FRAMEWORKS" ${ARGN})
    set(objs $<TARGET_OBJECTS:emu_app_units>)
    if(ARG_ENGINE)
        list(APPEND objs $<TARGET_OBJECTS:emu_app_engine> $<TARGET_OBJECTS:emu_app_fw> ${EMU_DEVICE_OBJECTS})
    else()
        list(APPEND objs $<TARGET_OBJECTS:emu_hostfs>)
    endif()
    if(ARG_MAC)
        list(APPEND objs $<TARGET_OBJECTS:emu_app_mac>)
    endif()
    add_executable(${name} tests/${name}.c ${objs})
    target_link_libraries(${name} PRIVATE emu_app_test_options)
    foreach(f IN LISTS ARG_FRAMEWORKS)
        target_link_libraries(${name} PRIVATE "-framework ${f}")
    endforeach()
    foreach(id IN LISTS ARG_IDS)
        add_test(NAME ${id} COMMAND ${name} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    endforeach()
endfunction()

emu_add_app_test(test_app_ring IDS M18_ring_fill_drop M18_ring_wrap M18_ring_threads)
```

- [ ] **Step 2: The test** `emu/tests/test_app_ring.c`:

```c
#include <pthread.h>
#include <stdint.h>

#include "app_ring.h"
#include "seqc_test.h"

TEST(M18_ring_fill_drop)
{
	app_ring_t r;
	ASSERT_EQ(app_ringInit(&r, sizeof(uint32_t), 6u), -1);	/* not a power of two */
	ASSERT_EQ(app_ringInit(&r, sizeof(uint32_t), 8u), 0);
	for (uint32_t v = 0; v < 8u; v++)
		ASSERT_EQ(app_ringPush(&r, &v), 0);
	const uint32_t extra = 99u;
	ASSERT_EQ(app_ringPush(&r, &extra), -1);	/* full: dropped and counted (spec 11) */
	ASSERT_EQ(app_ringDropped(&r), 1);
	ASSERT_EQ(app_ringCount(&r), 8);
	ASSERT_EQ(*(const uint32_t *)app_ringPeek(&r, 7u), 7);
	ASSERT_TRUE(app_ringPeek(&r, 8u) == NULL);
	app_ringPop(&r, 3u);
	ASSERT_EQ(*(const uint32_t *)app_ringPeek(&r, 0u), 3);
	ASSERT_EQ(app_ringPopped(&r), 3);
	app_ringReset(&r);
	ASSERT_EQ(app_ringCount(&r), 0);
	ASSERT_EQ(app_ringPopped(&r), 0);
	app_ringFree(&r);
}

/* bulk writes through slots, far past the capacity */
TEST(M18_ring_wrap)
{
	app_ring_t r;
	ASSERT_EQ(app_ringInit(&r, sizeof(uint64_t), 16u), 0);
	uint64_t next = 0, want = 0;
	for (unsigned round = 0; round < 1000u; round++)
	{
		unsigned n = 0;
		uint64_t *slot;
		while (n < 5u && (slot = app_ringSlot(&r, n)) != NULL)
			*slot = next + n++;
		app_ringCommit(&r, n);
		next += n;
		for (unsigned i = 0; i < 3u && app_ringCount(&r); i++, want++)
		{
			ASSERT_EQ(*(const uint64_t *)app_ringPeek(&r, 0u), want);
			app_ringPop(&r, 1u);
		}
	}
	ASSERT_EQ(app_ringPopped(&r), want);
	ASSERT_EQ(app_ringDropped(&r), 0);
	app_ringFree(&r);
}

#define N_THREADED 1000000u
static app_ring_t shared;

static void *producer(void *arg)
{
	(void)arg;
	for (uint64_t i = 0; i < N_THREADED;)
	{
		uint64_t *slot = app_ringSlot(&shared, 0u);
		if (!slot)
			continue;	/* full: the consumer catches up */
		*slot = i++;
		app_ringCommit(&shared, 1u);
	}
	return NULL;
}

TEST(M18_ring_threads)
{
	ASSERT_EQ(app_ringInit(&shared, sizeof(uint64_t), 256u), 0);
	pthread_t t;
	ASSERT_EQ(pthread_create(&t, NULL, producer, NULL), 0);
	uint64_t want = 0;
	int ordered = 1;
	while (want < N_THREADED)
	{
		const uint64_t *v = app_ringPeek(&shared, 0u);
		if (!v)
			continue;
		ordered &= *v == want;
		want++;
		app_ringPop(&shared, 1u);
	}
	pthread_join(t, NULL);
	ASSERT_TRUE(ordered);
	app_ringFree(&shared);
}

TEST_MAIN()
```

- [ ] **Step 3:** Run EMU-CHECK. Expected: the build fails, `app_ring.h` not found.

- [ ] **Step 4: The header** `emu/app/engine/app_ring.h`:

```c
/*
 * app_ring.h
 *
 * A lock-free single-producer, single-consumer ring of fixed-size entries
 * (stage 1b spec 6.1). The counters run freely in 64 bits; the capacity is a
 * power of two. Memory is allocated only by app_ringInit(). A full ring drops
 * the new entry and counts it (spec 11). The producer calls Push, Slot and
 * Commit; the consumer Peek and Pop; Reset only while neither side runs.
 */
#ifndef APP_RING_H_
#define APP_RING_H_

#include <stdalign.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

typedef struct app_ring
{
	alignas(64) _Atomic uint64_t head;		/**< entries pushed; written by the producer */
	alignas(64) _Atomic uint64_t tail;		/**< entries popped; written by the consumer */
	alignas(64) _Atomic uint64_t dropped;	/**< written by the producer */
	uint8_t *buf;
	size_t size;	/**< bytes per entry */
	uint32_t mask;	/**< capacity - 1 */
} app_ring_t;

int app_ringInit(app_ring_t *r, size_t entrySize, uint32_t capacity);	/**< 0, or -1 */
void app_ringFree(app_ring_t *r);
void app_ringReset(app_ring_t *r);
int app_ringPush(app_ring_t *r, const void *entry);		/**< 0, or -1 when full */
void *app_ringSlot(app_ring_t *r, uint32_t i);			/**< the i-th free slot, or NULL */
void app_ringCommit(app_ring_t *r, uint32_t n);
const void *app_ringPeek(app_ring_t *r, uint32_t i);	/**< the i-th pending entry, or NULL */
void app_ringPop(app_ring_t *r, uint32_t n);
uint32_t app_ringCount(app_ring_t *r);
uint64_t app_ringPopped(app_ring_t *r);
uint64_t app_ringDropped(app_ring_t *r);

#endif /* APP_RING_H_ */
```

The functions take non-const pointers: clang rejects atomic loads through a pointer to const `_Atomic`.

- [ ] **Step 5: The implementation** `emu/app/engine/app_ring.c`:

```c
#include "app_ring.h"

#include <stdlib.h>
#include <string.h>

int app_ringInit(app_ring_t *r, size_t entrySize, uint32_t capacity)
{
	memset(r, 0, sizeof *r);
	if (capacity == 0u || (capacity & (capacity - 1u)) != 0u || entrySize == 0u)
		return -1;
	r->buf = calloc(capacity, entrySize);
	if (!r->buf)
		return -1;
	r->size = entrySize;
	r->mask = capacity - 1u;
	atomic_init(&r->head, 0u);
	atomic_init(&r->tail, 0u);
	atomic_init(&r->dropped, 0u);
	return 0;
}

void app_ringFree(app_ring_t *r)
{
	free(r->buf);
	r->buf = NULL;
}

void app_ringReset(app_ring_t *r)
{
	atomic_store(&r->head, 0u);
	atomic_store(&r->tail, 0u);
}

void *app_ringSlot(app_ring_t *r, uint32_t i)
{
	const uint64_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
	const uint64_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
	if (head - tail + i > (uint64_t)r->mask)
		return NULL;
	return r->buf + (size_t)((head + i) & r->mask) * r->size;
}

void app_ringCommit(app_ring_t *r, uint32_t n)
{
	const uint64_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
	atomic_store_explicit(&r->head, head + n, memory_order_release);
}

int app_ringPush(app_ring_t *r, const void *entry)
{
	void *slot = app_ringSlot(r, 0u);
	if (!slot)
	{
		atomic_fetch_add_explicit(&r->dropped, 1u, memory_order_relaxed);
		return -1;
	}
	memcpy(slot, entry, r->size);
	app_ringCommit(r, 1u);
	return 0;
}

const void *app_ringPeek(app_ring_t *r, uint32_t i)
{
	const uint64_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
	const uint64_t head = atomic_load_explicit(&r->head, memory_order_acquire);
	if ((uint64_t)i >= head - tail)
		return NULL;
	return r->buf + (size_t)((tail + i) & r->mask) * r->size;
}

void app_ringPop(app_ring_t *r, uint32_t n)
{
	const uint64_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
	atomic_store_explicit(&r->tail, tail + n, memory_order_release);
}

uint32_t app_ringCount(app_ring_t *r)
{
	const uint64_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
	return (uint32_t)(atomic_load_explicit(&r->head, memory_order_acquire) - tail);
}

uint64_t app_ringPopped(app_ring_t *r)
{
	return atomic_load_explicit(&r->tail, memory_order_acquire);
}

uint64_t app_ringDropped(app_ring_t *r)
{
	return atomic_load_explicit(&r->dropped, memory_order_relaxed);
}
```

- [ ] **Step 6:** Run EMU-CHECK. Expected: N = 167, 0 warnings. Then the gate.

- [ ] **Step 7: Commit**

```bash
git add emu/CMakeLists.txt emu/app/engine/app_ring.h emu/app/engine/app_ring.c emu/tests/test_app_ring.c
git commit -m "Emulator app: build skeleton and the SPSC ring (E5.5 R4, M18)"
```

---

### Task 2: The time map, L and D, and the entry rule (§6.3, §6.4)

**Files:**
- Create: `emu/app/engine/app_timemap.h`, `app_timemap.c`, `emu/tests/test_app_timemap.c`
- Modify: `emu/CMakeLists.txt` (append `"${EMU_APP_DIR}/engine/app_timemap.c"` to `EMU_APP_UNIT_SOURCES`; register the test)

- [ ] **Step 1: The test** `emu/tests/test_app_timemap.c`, registered with `emu_add_app_test(test_app_timemap IDS M18_map_wall M18_map_drift M18_map_jump M18_latency_table M18_entry_rule M18_reanchor_order)`:

```c
#include "app_timemap.h"
#include "seqc_test.h"

#define TPS 24000000.0	/* the mach timebase of this Mac, 125/3 ns per tick (spec 6.3) */

TEST(M18_map_wall)
{
	app_timemap_t m;
	app_tmInit(&m, TPS);
	ASSERT_EQ_DBL(m.nominal, 9.0);	/* 216 MHz over 24 MHz */
	app_tmWall(&m, 216e6, 1000u);
	ASSERT_NEAR(app_tmVirtual(&m, 1000u + 24000000u), 432e6, 1e-6);
	ASSERT_EQ(app_tmHost(&m, 324e6), 1000u + 12000000u);
	ASSERT_EQ(m.audio, 0);
}

/* a device clock 100 ppm fast against the host: callbacks of 512 frames at 48 kHz */
TEST(M18_map_drift)
{
	app_timemap_t m;
	app_tmInit(&m, TPS);
	const double ratio = 1.0001;
	for (unsigned k = 0; k < 400u; k++)	/* 4.3 s */
		ASSERT_EQ(app_tmPoint(&m, 1e9 + (double)k * 256000.0 * 9.0 * ratio, 5000000u + (uint64_t)k * 256000u), 0);
	ASSERT_NEAR(m.slope / 9.0, ratio, 1e-9);	/* the slope of the last 2 s */
	ASSERT_EQ(m.jumps, 0);
	const uint64_t h = 5000000u + 399u * 256000u + 1000u;
	ASSERT_NEAR((double)app_tmHost(&m, app_tmVirtual(&m, h)), (double)h, 1.0);
}

TEST(M18_map_jump)
{
	app_timemap_t m;
	app_tmInit(&m, TPS);
	uint64_t h = 1000000u;
	double c = 5e8;
	for (unsigned k = 0; k < 100u; k++, h += 256000u, c += 256000.0 * 9.0)
		app_tmPoint(&m, c, h);
	ASSERT_EQ(app_tmPoint(&m, c + 108000.0, h), 0);	/* 0.5 ms off the line: no re-anchor */
	h += 256000u;
	c += 256000.0 * 9.0 + 108000.0;
	h += 120000u;										/* the output stalled: the frame comes 5 ms late */
	ASSERT_EQ(app_tmPoint(&m, c, h), 1);
	ASSERT_EQ(m.jumps, 1);
	ASSERT_EQ(m.jumpHost, h);
	ASSERT_EQ_DBL(m.slope, 9.0);						/* a new line starts at the nominal slope */
	ASSERT_NEAR(app_tmVirtual(&m, h), c, 1e-6);
}

/* spec 6.4's table; the 128-frame rows: 2.67 ms buffer + 1.00 ms safety + 1.09 ms converter */
TEST(M18_latency_table)
{
	const app_latencyIn_t rows[4] = {
		{ 0.01274, 48000.0, 60u, 690u, 471u, 0 },
		{ 0.00476, 48000.0, 60u, 690u, 118u, 0 },
		{ 0.01274, 48000.0, 60u, 0u, 471u, 0 },
		{ 0.00476, 48000.0, 60u, 0u, 118u, 0 },
	};
	const double L[4] = { 57.0, 33.0, 42.6, 18.6 }, D[4] = { 58.0, 34.0, 44.0, 20.0 };
	for (unsigned i = 0; i < 4u; i++)
	{
		const double l = app_latencyL(&rows[i]) * 1e3;
		ASSERT_NEAR(l, L[i], 0.05);
		ASSERT_NEAR(l + APP_D_MARGIN * 1e3, D[i], 0.5);	/* "about" D */
	}
	app_latencyIn_t clk = rows[0];
	clk.clockOutAudio = 1;	/* R5's option holds each block back by one (spec 6.8) */
	ASSERT_NEAR((app_latencyL(&clk) - app_latencyL(&rows[0])) * 1e3, 1.44991, 0.0001);
}

TEST(M18_entry_rule)
{
	ASSERT_EQ(app_entryTime(1000.0, 500u, 900u, 0u), 1500u);	/* map(stamp) + D */
	ASSERT_EQ(app_entryTime(1000.0, 500u, 2000u, 0u), 2000u);	/* never before now */
	ASSERT_EQ(app_entryTime(1000.0, 500u, 900u, 1700u), 1700u);	/* never before the port's previous event */
	ASSERT_EQ(app_entryTime(-5e6, 0u, 77u, 0u), 77u);			/* a stamp before the map's origin */
}

/* spec 12.1 M18: a re-anchor that moves the map back by 5 ms keeps a port's messages in order */
TEST(M18_reanchor_order)
{
	app_timemap_t m;
	app_tmInit(&m, TPS);
	app_tmPoint(&m, 1e9, 10000000u);
	const uint64_t d = 58u * 216000u, now = 1000000000u;
	const uint64_t s1 = 10240000u, s2 = s1 + 24000u;	/* two clocks 1 ms apart */
	const uint64_t e1 = app_entryTime(app_tmVirtual(&m, s1), d, now, 0u);
	ASSERT_EQ(app_tmPoint(&m, 1e9 - 5.0 * 216000.0, 10000000u), 1);	/* the map moves back by 5 ms */
	ASSERT_TRUE(app_tmVirtual(&m, s2) + (double)d < (double)e1);	/* without the third term: out of order */
	ASSERT_EQ(app_entryTime(app_tmVirtual(&m, s2), d, now, e1), e1);
}

TEST_MAIN()
```

- [ ] **Step 2:** Run EMU-CHECK. Expected: build fails, `app_timemap.h` not found.

- [ ] **Step 3: The header** `emu/app/engine/app_timemap.h`:

```c
/*
 * app_timemap.h
 *
 * The firmware thread's map between host time (mach ticks) and virtual time
 * (216 MHz cycles), and back (stage 1b spec 6.3); L, the largest render lead
 * (spec 6.4); and the entry rule of an input event (spec 6.4). Firmware thread
 * only.
 */
#ifndef APP_TIMEMAP_H_
#define APP_TIMEMAP_H_

#include <stdint.h>

#define APP_TM_HIST 64u			/**< history points, one per 1/32 of the window */
#define APP_TM_WINDOW 2.0		/**< s: the slope's window */
#define APP_TM_JUMP 216000.0	/**< a point more than 1 ms off the line re-anchors */
#define APP_FS 44140.625		/**< the device's sample rate */
#define APP_D_MARGIN 0.001		/**< D's default is L + 1 ms */

typedef struct app_timemap
{
	double ticksPerSec;
	double nominal;			/**< cycles per tick: EMU_CPU_HZ / ticksPerSec */
	double slope;			/**< cycles per tick of the line */
	double c0;				/**< the anchor: virtual cycles at host time h0 */
	uint64_t h0;
	int valid;
	int audio;				/**< 1: anchored on callback points; 0: the wall clock */
	struct { double c; uint64_t h; } hist[APP_TM_HIST];
	unsigned nhist;
	uint64_t jumps;			/**< re-anchors on a point off the line */
	uint64_t jumpHost;		/**< host time of the last re-anchor or wall anchor */
} app_timemap_t;

void app_tmInit(app_timemap_t *m, double ticksPerSec);
/** anchor on the wall clock: virtual time c at host time h, nominal slope */
void app_tmWall(app_timemap_t *m, double c, uint64_t h);
/** a callback point: virtual time c is presented at host time h. Returns 1 if
 * the point was more than 1 ms off the line and re-anchored the map. */
int app_tmPoint(app_timemap_t *m, double c, uint64_t h);
double app_tmVirtual(const app_timemap_t *m, uint64_t h);
uint64_t app_tmHost(const app_timemap_t *m, double c);
/** virtual cycles of virtual frame f: codecStart + f * 552,960 / 113 */
double app_frameCycles(uint64_t codecStart, double frame);

typedef struct app_latencyIn
{
	double hostLead;			/**< s: largest mHostTime minus callback entry */
	double deviceRate;
	uint32_t deviceLatency;		/**< frames at the device rate */
	uint32_t streamLatency;
	uint32_t maxRequest;		/**< largest callback request, in 44,140.625 Hz frames */
	int clockOutAudio;			/**< R5's clock-out-as-audio: one block more */
} app_latencyIn_t;

/** L in seconds: the presentation offset of a callback's first frame plus
 * (2 x the largest request + 5 blocks) / 44,140.625 Hz (spec 6.4) */
double app_latencyL(const app_latencyIn_t *in);

/** the entry time max(mapped + d, now, prev) of an event (spec 6.4) */
uint64_t app_entryTime(double mapped, uint64_t d, uint64_t now, uint64_t prev);

#endif /* APP_TIMEMAP_H_ */
```

- [ ] **Step 4: The implementation** `emu/app/engine/app_timemap.c`:

```c
#include "app_timemap.h"

#include <math.h>
#include <string.h>

#include "emu_core.h"

void app_tmInit(app_timemap_t *m, double ticksPerSec)
{
	memset(m, 0, sizeof *m);
	m->ticksPerSec = ticksPerSec;
	m->nominal = (double)EMU_CPU_HZ / ticksPerSec;
	m->slope = m->nominal;
}

void app_tmWall(app_timemap_t *m, double c, uint64_t h)
{
	m->valid = 1;
	m->audio = 0;
	m->slope = m->nominal;
	m->c0 = c;
	m->h0 = h;
	m->nhist = 0;
	m->jumpHost = h;
}

double app_tmVirtual(const app_timemap_t *m, uint64_t h)
{
	return m->c0 + (double)(int64_t)(h - m->h0) * m->slope;
}

uint64_t app_tmHost(const app_timemap_t *m, double c)
{
	const double h = (double)m->h0 + (c - m->c0) / m->slope;
	return h <= 0.0 ? 0u : (uint64_t)llround(h);
}

double app_frameCycles(uint64_t codecStart, double frame)
{
	return (double)codecStart + frame * 552960.0 / 113.0;
}

int app_tmPoint(app_timemap_t *m, double c, uint64_t h)
{
	/* a jump only on a line of audio points: the first one after a wall anchor is not a jump */
	const int off = m->valid && m->audio && fabs(c - app_tmVirtual(m, h)) > APP_TM_JUMP;
	if (!m->valid || off || !m->audio)
	{
		/* a new line: the first point, a jump, or the end of the wall clock's pacing */
		m->nhist = 0;
		m->slope = m->nominal;
		m->audio = 1;
		if (off)
		{
			m->jumps++;
			m->jumpHost = h;
		}
	}
	m->valid = 1;
	m->c0 = c;	/* the line goes through the latest point */
	m->h0 = h;
	const double window = APP_TM_WINDOW * m->ticksPerSec;
	if (m->nhist == 0u || (double)(h - m->hist[m->nhist - 1u].h) >= window / 32.0)
	{
		if (m->nhist == APP_TM_HIST)
		{
			memmove(m->hist, m->hist + 1, (APP_TM_HIST - 1u) * sizeof m->hist[0]);
			m->nhist--;
		}
		m->hist[m->nhist].c = c;
		m->hist[m->nhist].h = h;
		m->nhist++;
	}
	unsigned old = 0;
	while (old + 1u < m->nhist && (double)(h - m->hist[old].h) > window)
		old++;
	if (old)
	{
		memmove(m->hist, m->hist + old, (m->nhist - old) * sizeof m->hist[0]);
		m->nhist -= old;
	}
	const double span = (double)(h - m->hist[0].h);
	if (span >= window / 8.0)
	{
		/* the drift of the device's clock against the host's; a wild value keeps the nominal slope */
		const double s = (c - m->hist[0].c) / span;
		m->slope = fabs(s / m->nominal - 1.0) <= 0.01 ? s : m->nominal;
	}
	return off;
}

double app_latencyL(const app_latencyIn_t *in)
{
	const double offset = in->hostLead + (double)(in->deviceLatency + in->streamLatency) / in->deviceRate;
	const double blocks = 5.0 + (in->clockOutAudio ? 1.0 : 0.0);
	return offset + (2.0 * (double)in->maxRequest + blocks * EMU_BLOCK_FRAMES) / APP_FS;
}

uint64_t app_entryTime(double mapped, uint64_t d, uint64_t now, uint64_t prev)
{
	const double t = mapped + (double)d;
	const uint64_t e = t > (double)now ? (uint64_t)t : now;
	return e < prev ? prev : e;
}
```

- [ ] **Step 5:** Run EMU-CHECK. Expected: N = 173, 0 warnings. Then the gate.

- [ ] **Step 6: Commit** `git add` the four files and `emu/CMakeLists.txt`; message `Emulator app: the time map, L and D, the entry rule (E5.5 R4, M18)`.

---

### Task 3: MIDI conversions and event lists (§4.3, §6.4, §6.5)

**Files:**
- Create: `emu/app/engine/app_midi.h`, `app_midi.c`, `app_midilist.h`, `app_midilist.c`, `emu/tests/test_app_midi.c`
- Modify: `emu/CMakeLists.txt` (append `app_midi.c` and `app_midilist.c` to `EMU_APP_UNIT_SOURCES`; `emu_add_app_test(test_app_midi FRAMEWORKS CoreMIDI CoreFoundation IDS M18_midi_split M18_midi_usb_cin M18_midi_usb_transfer M18_midi_bytes M18_midi_din_asm M18_midi_usb_out M18_eventlist)`)

- [ ] **Step 1: The test** `emu/tests/test_app_midi.c`:

```c
#include <string.h>

#include "app_midi.h"
#include "app_midilist.h"
#include "seqc_test.h"

/* a ring stand-in, and a recorder of UMP messages */
typedef struct { app_midiEntry_t e[64]; unsigned n; } list_t;
static int listPush(void *ctx, const app_midiEntry_t *e)
{
	list_t *l = ctx;
	if (l->n == 64u)
		return -1;
	l->e[l->n++] = *e;
	return 0;
}
static const app_midiEntry_t *listPeek(void *ctx, unsigned i)
{
	list_t *l = ctx;
	return i < l->n ? &l->e[i] : NULL;
}
typedef struct { uint64_t t[64]; uint32_t w[64][2]; unsigned n; } umps_t;
static void umpEmit(void *ctx, uint64_t t, const uint32_t *w, unsigned n)
{
	umps_t *u = ctx;
	if (u->n < 64u)
	{
		u->t[u->n] = t;
		memcpy(u->w[u->n], w, n * sizeof *w);
		u->n++;
	}
}

TEST(M18_midi_split)
{
	uint32_t words[32];
	for (unsigned i = 0; i < 30u; i++)
		words[i] = 0x10F80000u;
	list_t l = { .n = 0 };
	ASSERT_EQ(app_midiSplit(42u, APP_PORT_USB, words, 30u, listPush, &l), 2);	/* E10: 16 + 14 */
	ASSERT_EQ(l.e[0].nwords, 16);
	ASSERT_EQ(l.e[1].nwords, 14);
	ASSERT_EQ(l.e[1].stamp, 42);
	for (unsigned i = 0; i < 14u; i++)
		words[i] = 0x20903C64u;
	words[14] = 0x30047E7Fu;	/* a complete SysEx of 4 bytes: 7E 7F 06 01, worth 3 packets */
	words[15] = 0x06010000u;
	l.n = 0;
	ASSERT_EQ(app_midiSplit(7u, APP_PORT_DIN, words, 16u, listPush, &l), 2);	/* 14 + 3 > 16: whole into the next */
	ASSERT_EQ(l.e[0].nwords, 14);
	ASSERT_EQ(l.e[1].nwords, 2);
	ASSERT_EQ(l.e[1].words[0], 0x30047E7Fu);
	ASSERT_EQ(l.e[1].port, APP_PORT_DIN);
	uint32_t sx[12];	/* six SysEx packets of 6 bytes: 18 packets' worth, cut at 5 (15) */
	for (unsigned i = 0; i < 12u; i += 2u)
	{
		sx[i] = (i == 0u ? 0x30160000u : i == 10u ? 0x30360000u : 0x30260000u) | 0x0102u;
		sx[i + 1u] = 0x03040506u;
	}
	l.n = 0;
	ASSERT_EQ(app_midiSplit(9u, APP_PORT_USB, sx, 12u, listPush, &l), 2);
	ASSERT_EQ(l.e[0].nwords, 10);
	app_usbPend_t p = { .n = 0 };
	uint8_t data[64];
	unsigned len;
	ASSERT_EQ(app_usbTransfer(listPeek, &l, &p, data, &len), 2);	/* F0 + 36 bytes + F7: 13 packets, one transfer */
	ASSERT_EQ(len, 52);
	ASSERT_EQ(data[48], 0x06);	/* the end: CIN 0x6, the last data byte and F7 */
	ASSERT_EQ(data[50], 0xF7);
}

TEST(M18_midi_usb_cin)
{
	const uint32_t w[] = { 0x20903C64u, 0x20C00500u, 0x10F20800u, 0x10F10300u, 0x10F60000u, 0x10FA0000u,
		0x30047E7Fu, 0x06010000u };
	const uint8_t want[] = { 0x09, 0x90, 0x3C, 0x64, 0x0C, 0xC0, 0x05, 0x00, 0x03, 0xF2, 0x08, 0x00,
		0x02, 0xF1, 0x03, 0x00, 0x05, 0xF6, 0x00, 0x00, 0x0F, 0xFA, 0x00, 0x00,
		0x04, 0xF0, 0x7E, 0x7F, 0x07, 0x06, 0x01, 0xF7 };
	uint8_t out[64];
	app_usbPend_t p = { .n = 0 };
	ASSERT_EQ(app_umpToUsb(w, 8u, &p, out, sizeof out), (int)sizeof want);
	ASSERT_EQ(memcmp(out, want, sizeof want), 0);
	ASSERT_EQ(p.n, 0);
	ASSERT_EQ(app_umpToUsb(w, 8u, &p, out, 28u), -1);	/* does not fit */
}

TEST(M18_midi_usb_transfer)
{
	list_t l = { .n = 0 };
	const uint32_t clock = 0x10F80000u;
	for (unsigned i = 0; i < 20u; i++)
		app_midiSplit(1000u, APP_PORT_USB, &clock, 1u, listPush, &l);
	app_midiSplit(2000u, APP_PORT_USB, &clock, 1u, listPush, &l);
	app_usbPend_t p = { .n = 0 };
	uint8_t data[64];
	unsigned len;
	ASSERT_EQ(app_usbTransfer(listPeek, &l, &p, data, &len), 16);	/* at most 16 packets (spec 4.3) */
	ASSERT_EQ(len, 64);
	ASSERT_EQ(data[60], 0x0F);
	ASSERT_EQ(data[61], 0xF8);
	memmove(l.e, l.e + 16, 5u * sizeof l.e[0]);
	l.n = 5;
	ASSERT_EQ(app_usbTransfer(listPeek, &l, &p, data, &len), 4);	/* the rest of stamp 1000; 2000 is another */
	ASSERT_EQ(len, 16);
}

TEST(M18_midi_bytes)
{
	const uint32_t w[] = { 0x20903C64u, 0x20C00500u, 0x10F20800u, 0x10F80000u, 0x30047E7Fu, 0x06010000u };
	const uint8_t want[] = { 0x90, 0x3C, 0x64, 0xC0, 0x05, 0xF2, 0x08, 0x00, 0xF8,
		0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
	uint8_t out[32];
	ASSERT_EQ(app_umpToBytes(w, 6u, out, sizeof out), sizeof want);
	ASSERT_EQ(memcmp(out, want, sizeof want), 0);
}

/* spec 6.5: complete messages at the time of their first byte; realtime at its own */
TEST(M18_midi_din_asm)
{
	static const struct { uint64_t t; uint8_t b; } in[] = {
		{ 0, 0x90 }, { 69120, 0x3C }, { 138240, 0xF8 }, { 207360, 0x64 },
		{ 276480, 0x3E }, { 345600, 0x40 },		/* running status: from its first data byte */
		{ 414720, 0xC0 }, { 483840, 0x05 },		/* one data byte */
		{ 552960, 0xF0 }, { 622080, 0x7E }, { 691200, 0x01 }, { 760320, 0x02 }, { 829440, 0x03 },
		{ 898560, 0x04 }, { 967680, 0x05 }, { 1036800, 0x06 }, { 1105920, 0xF7 },
		{ 1175040, 0x40 },						/* a data byte without a status */
	};
	app_dinAsm_t a;
	memset(&a, 0, sizeof a);
	umps_t u = { .n = 0 };
	for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++)
		app_dinByte(&a, in[i].t, in[i].b, umpEmit, &u);
	ASSERT_EQ(u.n, 6);
	ASSERT_EQ(u.w[0][0], 0x10F80000u);
	ASSERT_EQ(u.t[0], 138240);
	ASSERT_EQ(u.w[1][0], 0x20903C64u);
	ASSERT_EQ(u.t[1], 0);
	ASSERT_EQ(u.w[2][0], 0x20903E40u);
	ASSERT_EQ(u.t[2], 276480);
	ASSERT_EQ(u.w[3][0], 0x20C00500u);
	ASSERT_EQ(u.w[4][0], 0x30167E01u);	/* SysEx start, 6 bytes */
	ASSERT_EQ(u.w[4][1], 0x02030405u);
	ASSERT_EQ(u.w[5][0], 0x30310600u);	/* end, 1 byte */
	ASSERT_EQ(u.t[5], 552960);			/* every packet at the time of its F0 */
	ASSERT_EQ(a.orphans, 1);
}

TEST(M18_midi_usb_out)
{
	const uint8_t transfer[] = { 0x09, 0x90, 0x3C, 0x64, 0x0F, 0xF8, 0x00, 0x00, 0x0C, 0xC0, 0x05, 0x00 };
	app_sx_t sx;
	memset(&sx, 0, sizeof sx);
	umps_t u = { .n = 0 };
	app_usbToUmp(5000u, transfer, sizeof transfer, &sx, umpEmit, &u);
	ASSERT_EQ(u.n, 3);
	ASSERT_EQ(u.w[0][0], 0x20903C64u);
	ASSERT_EQ(u.w[1][0], 0x10F80000u);
	ASSERT_EQ(u.w[2][0], 0x20C00500u);
	ASSERT_EQ(u.t[2], 5000);
}

/* spec 12.1 M18: distinct stamps stay distinct; six clocks stamped 0 enter as one transfer */
TEST(M18_eventlist)
{
	static uint32_t buf[256];	/* a MIDIEventList is 4-byte aligned */
	MIDIEventList *list = (MIDIEventList *)buf;
	const UInt32 clock = 0x10F80000u;
	MIDIEventPacket *p = MIDIEventListInit(list, kMIDIProtocol_1_0);
	for (unsigned k = 0; k < 6u; k++)
		p = MIDIEventListAdd(list, sizeof buf, p, 1000u + 100u * k, 1u, &clock);
	ASSERT_TRUE(p != NULL);
	list_t l = { .n = 0 };
	ASSERT_EQ(app_midiFromEventList(list, APP_PORT_USB, 777u, listPush, &l), 6);
	for (unsigned k = 0; k < 6u; k++)
		ASSERT_EQ(l.e[k].stamp, 1000u + 100u * k);
	p = MIDIEventListInit(list, kMIDIProtocol_1_0);
	for (unsigned k = 0; k < 6u; k++)
		p = MIDIEventListAdd(list, sizeof buf, p, 0u, 1u, &clock);
	l.n = 0;
	app_midiFromEventList(list, APP_PORT_USB, 777u, listPush, &l);
	ASSERT_EQ(l.e[0].stamp, 777);	/* E8 */
	app_usbPend_t pend = { .n = 0 };
	uint8_t data[64];
	unsigned len;
	ASSERT_EQ(app_usbTransfer(listPeek, &l, &pend, data, &len), l.n);
	ASSERT_EQ(len, 24);
}

TEST_MAIN()
```

- [ ] **Step 2:** Run EMU-CHECK. Expected: build fails, `app_midi.h` not found.

- [ ] **Step 3: The headers.** `emu/app/engine/app_midi.h`:

```c
/*
 * app_midi.h
 *
 * MIDI conversions of LICHOMAT Emu (stage 1b spec 4.3, 6.4, 6.5), pure C:
 * CoreMIDI's MIDI 1.0 UMP words (message types 1, 2 and 3) to and from
 * USB-MIDI 1.0 packets on cable 0, UMP to DIN bytes, the DIN out bytes to
 * complete messages, and the ring entries of the MIDI rings.
 */
#ifndef APP_MIDI_H_
#define APP_MIDI_H_

#include <stdint.h>

enum { APP_PORT_USB, APP_PORT_DIN, APP_PORT_JACK };

#define APP_MIDI_WORDS 16u		/**< UMP words per ring entry (E10) */
#define APP_MIDI_PACKETS 16u	/**< USB-MIDI packets' worth per ring entry: 1 per MT 1 or 2, 3 per MT 3 (E10) */

/** one entry of a MIDI-in ring (stamp: host time) or of the MIDI-out ring
 * (stamp: presentation host time) */
typedef struct app_midiEntry
{
	uint64_t stamp;
	uint8_t port;
	uint8_t nwords;
	uint32_t words[APP_MIDI_WORDS];
} app_midiEntry_t;

typedef int (*app_midiPush_t)(void *ctx, const app_midiEntry_t *e);
typedef const app_midiEntry_t *(*app_midiPeek_t)(void *ctx, unsigned i);
typedef void (*app_umpEmit_t)(void *ctx, uint64_t t, const uint32_t *w, unsigned n);

/** the words of the UMP message that starts with w0 */
unsigned app_umpLength(uint32_t w0);
/** one event packet into entries of at most APP_MIDI_PACKETS packets' worth, cut at message boundaries; returns the entries */
unsigned app_midiSplit(uint64_t stamp, uint8_t port, const uint32_t *words, unsigned n, app_midiPush_t push, void *ctx);

/** SysEx bytes between F0 and F7 into MT 3 packets of up to 6 bytes */
typedef struct app_sx { uint8_t b[6], n, started, active; } app_sx_t;
void app_sxByte(app_sx_t *sx, uint64_t t, uint8_t b, app_umpEmit_t emit, void *ctx);

/** SysEx bytes on the USB side not yet packed into a CIN 0x4 packet */
typedef struct app_usbPend { uint8_t b[3], n; } app_usbPend_t;
/** UMP to USB-MIDI 1.0 packets with the CIN of each message, SysEx included; the
 * bytes written, or -1 if they do not fit in cap */
int app_umpToUsb(const uint32_t *w, unsigned n, app_usbPend_t *pend, uint8_t *out, unsigned cap);
/** one USB transfer from the ring's head (spec 6.4): consecutive entries with the
 * head's stamp, up to 16 packets; returns the entries it took */
unsigned app_usbTransfer(app_midiPeek_t peek, void *ctx, app_usbPend_t *pend, uint8_t out[64], unsigned *len);

/** UMP to MIDI 1.0 bytes for the DIN wire; returns the bytes */
unsigned app_umpToBytes(const uint32_t *w, unsigned n, uint8_t *out, unsigned cap);

/** a USB out transfer's packets to UMP messages, all at its time t */
void app_usbToUmp(uint64_t t, const uint8_t *data, unsigned len, app_sx_t *sx, app_umpEmit_t emit, void *ctx);

/** the DIN out sink's bytes to complete MIDI 1.0 messages (spec 6.5) */
typedef struct app_dinAsm
{
	app_sx_t sx;
	uint64_t t, sxT;	/**< the time of the message's first byte; of the SysEx's F0 */
	uint64_t orphans;	/**< data bytes without a status */
	uint8_t status, need, have, fresh, d[2];
} app_dinAsm_t;
void app_dinByte(app_dinAsm_t *a, uint64_t t, uint8_t b, app_umpEmit_t emit, void *ctx);

#endif /* APP_MIDI_H_ */
```

`emu/app/engine/app_midilist.h`:

```c
/*
 * app_midilist.h
 *
 * A CoreMIDI MIDIEventList into MIDI ring entries (stage 1b spec 6.4): one
 * entry or more per packet, each with the packet's timestamp; a timestamp of
 * 0 becomes now (plan E8). Header-only use of CoreMIDI: MIDIEventPacketNext()
 * is inline, so this file links no framework.
 */
#ifndef APP_MIDILIST_H_
#define APP_MIDILIST_H_

#include <CoreMIDI/CoreMIDI.h>

#include "app_midi.h"

unsigned app_midiFromEventList(const MIDIEventList *list, uint8_t port, uint64_t now, app_midiPush_t push, void *ctx);

#endif /* APP_MIDILIST_H_ */
```

- [ ] **Step 4: The implementation** `emu/app/engine/app_midilist.c`:

```c
#include "app_midilist.h"

unsigned app_midiFromEventList(const MIDIEventList *list, uint8_t port, uint64_t now, app_midiPush_t push, void *ctx)
{
	unsigned entries = 0;
	const MIDIEventPacket *p = &list->packet[0];
	for (UInt32 i = 0; i < list->numPackets; i++)
	{
		entries += app_midiSplit(p->timeStamp ? p->timeStamp : now, port, p->words, p->wordCount, push, ctx);
		p = MIDIEventPacketNext(p);
	}
	return entries;
}
```

`emu/app/engine/app_midi.c`:

```c
#include "app_midi.h"

#include <string.h>

unsigned app_umpLength(uint32_t w0)
{
	static const uint8_t len[16] = { 1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4 };
	return len[w0 >> 28];
}

unsigned app_midiSplit(uint64_t stamp, uint8_t port, const uint32_t *words, unsigned n, app_midiPush_t push, void *ctx)
{
	app_midiEntry_t e;
	memset(&e, 0, sizeof e);
	e.stamp = stamp;
	e.port = port;
	unsigned entries = 0, packets = 0;
	for (unsigned i = 0; i < n;)
	{
		const unsigned len = app_umpLength(words[i]);
		const unsigned cost = words[i] >> 28 == 0x3u ? 3u : 1u;	/* a SysEx packet: up to 8 bytes with F0 and F7 */
		if (len > n - i)
			break;	/* a cut message */
		if (e.nwords + len > APP_MIDI_WORDS || packets + cost > APP_MIDI_PACKETS)
		{
			(void)push(ctx, &e);	/* a full ring counts the drop */
			entries++;
			e.nwords = 0;
			packets = 0;
		}
		memcpy(&e.words[e.nwords], &words[i], len * sizeof *words);
		e.nwords = (uint8_t)(e.nwords + len);
		packets += cost;
		i += len;
	}
	if (e.nwords)
	{
		(void)push(ctx, &e);
		entries++;
	}
	return entries;
}

/* one MT 3 packet: status 0 complete, 1 start, 2 continue, 3 end */
static void app_sxFlush(app_sx_t *sx, uint64_t t, int last, app_umpEmit_t emit, void *ctx)
{
	const uint32_t st = last ? (sx->started ? 3u : 0u) : (sx->started ? 2u : 1u);
	uint8_t b[6] = { 0 };
	memcpy(b, sx->b, sx->n);
	const uint32_t w[2] = {
		0x30000000u | (st << 20) | ((uint32_t)sx->n << 16) | ((uint32_t)b[0] << 8) | b[1],
		((uint32_t)b[2] << 24) | ((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 8) | b[5],
	};
	emit(ctx, t, w, 2u);
	sx->started = (uint8_t)!last;
	sx->n = 0;
}

void app_sxByte(app_sx_t *sx, uint64_t t, uint8_t b, app_umpEmit_t emit, void *ctx)
{
	if (b == 0xF0u)
	{
		if (sx->active)
			app_sxFlush(sx, t, 1, emit, ctx);
		sx->active = 1;
		sx->started = 0;
		sx->n = 0;
	}
	else if (b == 0xF7u)
	{
		if (sx->active)
			app_sxFlush(sx, t, 1, emit, ctx);
		sx->active = 0;
	}
	else if (sx->active)
	{
		if (sx->n == 6u)
			app_sxFlush(sx, t, 0, emit, ctx);
		sx->b[sx->n++] = b & 0x7Fu;
	}
}

static int app_usbPut(uint8_t *out, unsigned *at, unsigned cap, uint8_t cin, uint8_t b0, uint8_t b1, uint8_t b2)
{
	if (*at + 4u > cap)
		return -1;
	out[(*at)++] = cin;	/* cable 0 */
	out[(*at)++] = b0;
	out[(*at)++] = b1;
	out[(*at)++] = b2;
	return 0;
}

/* CIN 0x4 for three bytes that do not end the SysEx; 0x5-0x7 for its end */
static int app_usbSxByte(app_usbPend_t *p, uint8_t b, uint8_t *out, unsigned *at, unsigned cap)
{
	p->b[p->n++] = b;
	if (b == 0xF7u)
	{
		const unsigned n = p->n;
		p->n = 0;
		return app_usbPut(out, at, cap, (uint8_t)(0x4u + n), p->b[0], n > 1u ? p->b[1] : 0u, n > 2u ? p->b[2] : 0u);
	}
	if (p->n == 3u)
	{
		p->n = 0;
		return app_usbPut(out, at, cap, 0x4u, p->b[0], p->b[1], p->b[2]);
	}
	return 0;
}

int app_umpToUsb(const uint32_t *w, unsigned n, app_usbPend_t *pend, uint8_t *out, unsigned cap)
{
	unsigned at = 0;
	for (unsigned i = 0; i < n; i += app_umpLength(w[i]))
	{
		const uint32_t x = w[i];
		const uint8_t st = (uint8_t)(x >> 16), d1 = (uint8_t)((x >> 8) & 0x7Fu), d2 = (uint8_t)(x & 0x7Fu);
		int r = 0;
		switch (x >> 28)
		{
		case 0x2u:	/* MIDI 1.0 channel voice: CIN = the status's high nibble */
			r = app_usbPut(out, &at, cap, (uint8_t)(st >> 4), st, d1, (st & 0xE0u) == 0xC0u ? 0u : d2);
			break;
		case 0x1u:	/* system common and realtime */
			if (st >= 0xF8u)
				r = app_usbPut(out, &at, cap, 0xFu, st, 0u, 0u);
			else if (st == 0xF2u)
				r = app_usbPut(out, &at, cap, 0x3u, st, d1, d2);
			else if (st == 0xF1u || st == 0xF3u)
				r = app_usbPut(out, &at, cap, 0x2u, st, d1, 0u);
			else if (st == 0xF6u)
				r = app_usbPut(out, &at, cap, 0x5u, st, 0u, 0u);
			break;
		case 0x3u:	/* 7-bit SysEx */
		{
			if (i + 1u >= n)
				return (int)at;
			const uint32_t y = w[i + 1u];
			const unsigned status = (x >> 20) & 0xFu, count = (x >> 16) & 0xFu;
			const uint8_t b[6] = { (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(y >> 24), (uint8_t)(y >> 16),
				(uint8_t)(y >> 8), (uint8_t)y };
			if (status == 0u || status == 1u)
				r |= app_usbSxByte(pend, 0xF0u, out, &at, cap);
			for (unsigned k = 0; k < count && k < 6u; k++)
				r |= app_usbSxByte(pend, b[k] & 0x7Fu, out, &at, cap);
			if (status == 0u || status == 3u)
				r |= app_usbSxByte(pend, 0xF7u, out, &at, cap);
			break;
		}
		default:	/* utility messages, and anything that is not MIDI 1.0 */
			break;
		}
		if (r)
			return -1;
	}
	return (int)at;
}

unsigned app_usbTransfer(app_midiPeek_t peek, void *ctx, app_usbPend_t *pend, uint8_t out[64], unsigned *len)
{
	*len = 0;
	const app_midiEntry_t *first = peek(ctx, 0u);
	unsigned taken = 0;
	for (const app_midiEntry_t *e = first; e && e->stamp == first->stamp; e = peek(ctx, ++taken))
	{
		app_usbPend_t p = *pend;	/* committed only if the entry fits */
		const int got = app_umpToUsb(e->words, e->nwords, &p, out + *len, 64u - *len);
		if (got < 0)
			break;	/* the next transfer takes it; a well-formed entry fits an empty one (E10) */
		*len += (unsigned)got;
		*pend = p;
	}
	return taken;
}

static void app_dinEmit(app_dinAsm_t *a, app_umpEmit_t emit, void *ctx)
{
	const uint32_t w = (a->status < 0xF0u ? 0x20000000u : 0x10000000u) | ((uint32_t)a->status << 16)
		| (a->need > 0u ? (uint32_t)a->d[0] << 8 : 0u) | (a->need > 1u ? a->d[1] : 0u);
	emit(ctx, a->t, &w, 1u);
}

void app_dinByte(app_dinAsm_t *a, uint64_t t, uint8_t b, app_umpEmit_t emit, void *ctx)
{
	if (b >= 0xF8u)
	{
		const uint32_t w = 0x10000000u | ((uint32_t)b << 16);	/* a message of its own, at its own time */
		emit(ctx, t, &w, 1u);
		return;
	}
	if (b == 0xF0u)
	{
		if (a->sx.active)
			app_sxByte(&a->sx, a->sxT, 0xF7u, emit, ctx);
		a->status = 0;
		a->sxT = t;
	}
	if (b == 0xF0u || b == 0xF7u || (a->sx.active && !(b & 0x80u)))
	{
		app_sxByte(&a->sx, a->sxT, b, emit, ctx);	/* every packet carries the time of the F0 */
		return;
	}
	if (b & 0x80u)
	{
		if (a->sx.active)
			app_sxByte(&a->sx, a->sxT, 0xF7u, emit, ctx);	/* a status byte ends a SysEx */
		a->status = b;
		a->have = 0;
		a->t = t;
		a->fresh = 1;
		a->need = b < 0xF0u ? ((b & 0xE0u) == 0xC0u ? 1u : 2u) : b == 0xF2u ? 2u : (b == 0xF1u || b == 0xF3u) ? 1u : 0u;
		if (b == 0xF4u || b == 0xF5u)
			a->status = 0;	/* undefined */
		else if (a->need == 0u)
		{
			app_dinEmit(a, emit, ctx);	/* F6 */
			a->status = 0;
		}
		return;
	}
	if (!a->status)
	{
		a->orphans++;
		return;
	}
	if (a->have == 0u && !a->fresh)
		a->t = t;	/* running status: the message starts at its first data byte */
	a->fresh = 0;
	a->d[a->have++] = b;
	if (a->have == a->need)
	{
		app_dinEmit(a, emit, ctx);
		a->have = 0;
		if (a->status >= 0xF0u)
			a->status = 0;	/* system common has no running status */
	}
}
```

Then add `app_umpToBytes()` and `app_usbToUmp()` to `app_midi.c`, by these rules (M18_midi_bytes and M18_midi_usb_out pin them):

- `app_umpToBytes()`: MT 2 gives the status and d1, and d2 unless the status is Cn or Dn; MT 1 gives the status, d1 for F1, F2 and F3, d2 for F2, and nothing for F4 and F5; MT 3 gives F0 for status 0 or 1, its `count` data bytes (at most 6, masked to 7 bits), and F7 for status 0 or 3. Every byte goes through a bounds check against `cap`; the count of bytes written is returned.
- `app_usbToUmp()`: per 4-byte packet, CIN 0x8–0xE gives `0x20000000 | b0<<16 | b1<<8 | b2`; CIN 0xF, 0x2, 0x3, and 0x5 with b0 = F6, give `0x10000000 | b0<<16`, plus `b1<<8` for 0x2 and 0x3 and `b2` for 0x3; CIN 0x4 passes 3 bytes, 0x5 (other than F6) 1, 0x6 2 and 0x7 3 bytes through `app_sxByte()` with the transfer's time; CIN 0x0 and 0x1 are ignored. Each message goes to `emit` at time t.

- [ ] **Step 5:** Run EMU-CHECK. Expected: N = 180, 0 warnings. If `M18_eventlist` hangs or fails inside `MIDIEventListAdd()`, stop and report (the plan assumes it needs no server). Then the gate.

- [ ] **Step 6: Commit** the five files and `emu/CMakeLists.txt`; message `Emulator app: MIDI conversions, event lists and USB transfers (E5.5 R4, M18)`.

---
### Task 4: Engine I — the firmware thread, the audio ring, pacing, commands, snapshots (§3.3, §6.1–§6.3, §6.8)

**Files:**
- Create: `emu/app/engine/app_types.h`, `app_engine.h`, `app_engine.c`, `app_fw.h`, `app_fw.c`, `emu/tests/app_fake.h`, `emu/tests/test_app_engine.c`
- Modify: `emu/CMakeLists.txt`

- [ ] **Step 1: CMake.** After `emu_app_units`:

```cmake
# the firmware thread's code (plan Task 4); app_fw.c alone sees firmware headers (E12)
add_library(emu_app_engine OBJECT "${EMU_APP_DIR}/engine/app_engine.c")
target_include_directories(emu_app_engine PUBLIC "${EMU_APP_DIR}/engine" "${EMU_DIR}/core")
target_compile_options(emu_app_engine PRIVATE ${EMU_APP_FLAGS})
add_library(emu_app_fw OBJECT "${EMU_APP_DIR}/engine/app_fw.c")
target_link_libraries(emu_app_fw PRIVATE emu_fw_headers)
target_include_directories(emu_app_fw PRIVATE "${EMU_APP_DIR}/engine" "${EMU_DIR}/core")
target_compile_options(emu_app_fw PRIVATE -Wall -Wextra -Werror)
emu_add_app_test(test_app_engine ENGINE IDS M18_render_mix M18_render_underrun R4_engine_splash R4_engine_ring
    R4_engine_wall R4_engine_card R4_engine_generator)
```

- [ ] **Step 2: The fake host** `emu/tests/app_fake.h` (E3), shared with Task 5:

```c
#ifndef APP_FAKE_H_
#define APP_FAKE_H_

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_engine.h"
#include "emu_hostfs.h"
#include "emu_test.h"

#define FAKE_TPS 24000000.0
#define FAKE_PERIOD 256000u	/* 512 frames at 48 kHz, in host ticks */
#define FAKE_LEAD 306160u	/* mHostTime - entry: (512 + 48) / 48 kHz + 1.09 ms (spec 12.1 M18) */

typedef struct fake
{
	uint64_t now;			/* host ticks */
	int device, running;	/* a device exists; the output is open */
	app_audio_t *audio;
	uint64_t nextCallback, callbacks;
	void (*event)(struct fake *f);	/* the test's arrivals; it sets the next nextEvent */
	uint64_t nextEvent;
	float buf[4096 * 4];
} fake_t;
static fake_t fk;

/* callback k's request: 512 frames at 48 kHz in 44,140.625 Hz frames, 470 or 471 */
static inline unsigned fakeRequest(uint64_t k)
{
	return (unsigned)((k + 1u) * 512u * 353125u / 384000u - k * 512u * 353125u / 384000u);
}

/* advance to until, running the callbacks and events due; with stop, return after one callback */
static inline void fakeAdvance(uint64_t until, int stop)
{
	for (;;)
	{
		const uint64_t cb = fk.running ? fk.nextCallback : UINT64_MAX;
		const uint64_t ev = fk.event ? fk.nextEvent : UINT64_MAX;
		const uint64_t next = cb <= ev ? cb : ev;
		if (next > until)
		{
			fk.now = until > fk.now ? until : fk.now;
			return;
		}
		fk.now = next > fk.now ? next : fk.now;
		if (next == cb)
		{
			const app_audioTime_t ts = { cb + FAKE_LEAD, cb, 1.0 };
			app_audioRender(fk.audio, &ts, fakeRequest(fk.callbacks++), fk.buf, 2u);
			fk.nextCallback += FAKE_PERIOD;
			if (stop)
				return;
		}
		else
			fk.event(&fk);
	}
}

static inline uint64_t fakeNow(void *ctx) { (void)ctx; return fk.now; }
static inline void fakeWait(void *ctx, uint64_t until) { (void)ctx; fakeAdvance(until, 1); }
static inline void fakeSleep(void *ctx, uint64_t until) { (void)ctx; fakeAdvance(until, 0); }
static inline void fakeClose(void *ctx) { (void)ctx; fk.running = 0; }
static inline void fakeMidiWake(void *ctx) { (void)ctx; }
/* spec 6.3's built-in output: 48 kHz, 512 frames, latencies 60 + 690 frames, safety offset 48 */
static inline int fakeOpen(void *ctx, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info)
{
	(void)ctx;
	(void)req;
	if (!fk.device)
		return -1;
	memset(info, 0, sizeof *info);
	info->deviceRate = 48000.0;
	info->clientRate = 44140.625;
	info->deviceLatency = 60u;
	info->streamLatency = 690u;
	info->safetyOffset = 48u;
	info->bufferFrames = 512u;
	info->channels = 2u;
	fk.audio = a;
	fk.running = 1;
	fk.nextCallback = fk.now + FAKE_PERIOD;
	return 0;
}

static inline void fakeHost(app_host_t *h, int device)
{
	memset(&fk, 0, sizeof fk);
	fk.device = device;
	fk.now = 1000u;
	fk.nextEvent = UINT64_MAX;
	*h = (app_host_t){ NULL, FAKE_TPS, fakeNow, fakeWait, fakeSleep, fakeOpen, fakeClose, fakeMidiWake };
}

/* the init fixture, copied, with GLO.CFG byte 0 set: a tempo, 0 EXT or 1 MID */
static inline void fakeCard(char *dir, size_t n, uint8_t sync)
{
	emu_testTempDir(dir, n);
	char path[1100];
	snprintf(path, sizeof path, "%s/PROJ00/GLO.CFG", dir);
	FILE *f = NULL;
	if (emu_hostCopyCard(EMU_TEST_CARDS "/init", dir, 0) != 0 || (f = fopen(path, "r+b")) == NULL)
	{
		perror(path);
		exit(2);
	}
	fputc(sync, f);
	fclose(f);
}

#endif /* APP_FAKE_H_ */
```

- [ ] **Step 3: The test** `emu/tests/test_app_engine.c`:

```c
#include "app_fake.h"

uint8_t BSP_SD_IsDetected(void);	/* emu/stubs/fatfs.h: SD_PRESENT 1, SD_NOT_PRESENT 0 */

static app_engine_t E;

static void boot(int device)
{
	app_host_t h;
	fakeHost(&h, device);
	if (app_engineInit(&E, &h) != 0)
		exit(2);
	static char card[1024];
	fakeCard(card, sizeof card, 120u);
	const app_engineConfig_t c = { .cardDir = card, .seed = 1u, .dAuto = 1, .mix = APP_MIX_SUM };
	app_enginePowerOn(&E, &c);
}

static app_snapshot_t last;	/* the newest snapshot: runTo() empties the ring at every slice */

static void runTo(double sec)
{
	while ((double)fk.now < 1000.0 + sec * FAKE_TPS)
	{
		app_engineSlice(&E);
		(void)app_engineSnapshot(&E, &last);	/* the ring drops new entries when full (E15) */
	}
}

static int lcdHas(const app_snapshot_t *s, const char *text)
{
	char r[2][17];
	for (unsigned i = 0; i < 2u; i++)
	{
		memcpy(r[i], s->lcd.chars[i], 16);
		r[i][16] = '\0';
	}
	return strstr(r[0], text) != NULL || strstr(r[1], text) != NULL;
}

static int nextAck(app_ack_t *a)	/* the next card acknowledgement; warnings skipped */
{
	while (app_engineAck(&E, a))
		if (a->kind == APP_ACK_CARD_OUT || a->kind == APP_ACK_CARD_IN)
			return 1;
	return 0;
}

static void audioInit(app_audio_t *a, unsigned frames, const int16_t f[4])
{
	memset(a, 0, sizeof *a);
	if (app_ringInit(&a->frames, 8u, 1024u) != 0 || app_ringInit(&a->points, sizeof(app_point_t), 64u) != 0)
		exit(2);
	for (unsigned i = 0; i < frames; i++)
		memcpy(app_ringSlot(&a->frames, i), f, 8u);
	app_ringCommit(&a->frames, frames);
}

TEST(M18_render_mix)
{
	app_audio_t a;
	const int16_t f[4] = { 1000, -2000, 3001, -3 };
	const app_audioTime_t ts = { 5000u, 4000u, 1.0 };
	float out[8];
	audioInit(&a, 4u, f);
	app_audioRender(&a, &ts, 1u, out, 2u);			/* sum, the default (spec 6.2) */
	ASSERT_EQ_DBL(out[0], 2000.0 / 32768.0);		/* (1000 + 3001) >> 1 */
	ASSERT_EQ_DBL(out[1], -1002.0 / 32768.0);		/* (-2000 - 3) >> 1, as --stereo sum */
	atomic_store(&a.mix, APP_MIX_OUT1);
	app_audioRender(&a, &ts, 1u, out, 2u);
	ASSERT_EQ_DBL(out[1], -2000.0 / 32768.0);
	atomic_store(&a.mix, APP_MIX_OUT2);
	app_audioRender(&a, &ts, 1u, out, 2u);
	ASSERT_EQ_DBL(out[0], 3001.0 / 32768.0);
	atomic_store(&a.mix, APP_MIX_FOUR);
	app_audioRender(&a, &ts, 1u, out, 4u);
	ASSERT_EQ_DBL(out[3], -3.0 / 32768.0);
	ASSERT_EQ(atomic_load(&a.underruns), 0);
}

TEST(M18_render_underrun)
{
	app_audio_t a;
	const int16_t f[4] = { 100, 100, 100, 100 };
	const app_audioTime_t ts = { 9000u, 2000u, 1.0 };
	static float out[2 * 470];
	audioInit(&a, 0u, f);
	app_audioRender(&a, &ts, 470u, out, 2u);	/* before the first frame: silence, no underrun */
	ASSERT_EQ(atomic_load(&a.underruns), 0);
	audioInit(&a, 100u, f);
	app_audioRender(&a, &ts, 470u, out, 2u);
	ASSERT_EQ(atomic_load(&a.underruns), 1);
	ASSERT_EQ_DBL(out[199], 100.0 / 32768.0);
	ASSERT_EQ_DBL(out[200], 0.0);
	const app_point_t *p = app_ringPeek(&a.points, 0u);
	ASSERT_EQ(p->frame, 0);
	ASSERT_EQ(p->request, 470);
	ASSERT_EQ(p->got, 100);
	ASSERT_EQ(p->hostTime, 9000);
	ASSERT_EQ(atomic_load(&a.maxRequest), 470);
}

TEST(R4_engine_splash)
{
	boot(1);
	app_engineSlice(&E);	/* the boot: emu_runUntil() returns at its end */
	ASSERT_TRUE(fk.now >= 1000u + 24000000u);	/* paced: 1 s of wall time (spec 4.10) */
	int seen = 0;	/* the ring keeps the boot's first 16 snapshots, taken during HAL_Delay(1000) */
	for (unsigned i = 0; i < app_ringCount(&E.snap); i++)
	{
		const app_snapshot_t *s = app_ringPeek(&E.snap, i);
		seen |= lcdHas(s, "LICHOMAT") && lcdHas(s, "v0.1 unofficial");
	}
	ASSERT_TRUE(seen);
	emu_counters_t c;
	emu_getCounters(&c);
	ASSERT_EQ(c.codecStart, 216000000u);
}

TEST(R4_engine_ring)
{
	boot(1);
	runTo(2.0);
	emu_counters_t c;
	emu_getCounters(&c);
	const uint64_t b0 = c.blocks, h0 = fk.now;
	for (; (double)fk.now < (double)h0 + 2.0 * FAKE_TPS; app_engineSlice(&E))
		ASSERT_TRUE(app_ringCount(&E.audio.frames) <= atomic_load(&E.audio.maxRequest) + APP_RING_SLACK + EMU_BLOCK_FRAMES);
	emu_getCounters(&c);
	ASSERT_NEAR((double)(c.blocks - b0) / ((double)(fk.now - h0) / FAKE_TPS), 689.697, 2.0);	/* spec 6.2 */
	ASSERT_EQ(atomic_load(&E.audio.underruns), 0);
	ASSERT_TRUE(c.underruns <= 1u);
	ASSERT_TRUE(E.map.audio);
	ASSERT_NEAR(E.L * 1e3, 57.0, 0.1);	/* spec 6.4's first row */
}

TEST(R4_engine_wall)
{
	boot(0);	/* no output device, as on the CI runner */
	runTo(5.0);
	ASSERT_NEAR((double)emu_now() / EMU_CPU_HZ, (double)(fk.now - 1000u) / FAKE_TPS, 0.002);
	ASSERT_EQ(last.diag.outputRunning, 0);
	ASSERT_NEAR(last.diag.blockRate, 689.7, 0.5);	/* the window [2 s, 4 s) */
}

TEST(R4_engine_card)
{
	boot(1);
	runTo(1.5);
	app_cmd_t c = { .kind = APP_CMD_CARD_OUT };
	ASSERT_EQ(app_engineCommand(&E, &c), 0);
	app_ack_t a;
	ASSERT_FALSE(nextAck(&a));
	app_engineSlice(&E);	/* applied between passes (spec 6.7) */
	ASSERT_TRUE(nextAck(&a));
	ASSERT_EQ(a.kind, APP_ACK_CARD_OUT);
	ASSERT_EQ(BSP_SD_IsDetected(), 0);
	c.kind = APP_CMD_CARD_IN;
	app_engineCommand(&E, &c);
	app_engineSlice(&E);
	ASSERT_TRUE(nextAck(&a));
	ASSERT_EQ(a.kind, APP_ACK_CARD_IN);
	ASSERT_EQ(BSP_SD_IsDetected(), 1);
}

TEST(R4_engine_generator)
{
	boot(1);
	runTo(1.5);
	const app_cmd_t c = { .kind = APP_CMD_GENERATOR, .a = 1, .b = 4, .x = 120.0 };
	app_engineCommand(&E, &c);
	app_engineSlice(&E);
	const uint64_t t0 = E.genT0;
	while (emu_now() < t0 + EMU_CPU_HZ + 2u * EMU_CYCLES_PER_MS)	/* the last hook then lies past t0 + 1 s */
		app_engineSlice(&E);
	ASSERT_EQ(E.genK, 9);	/* 120 BPM at 4 PPQ: t0 + k x 125 ms for k = 0..8, pulses up to 1 ms ahead */
	ASSERT_EQ(E.genNext, t0 + 9u * 27000000u);
}

TEST_MAIN()
```

- [ ] **Step 4:** Run EMU-CHECK. Expected: build fails, `app_engine.h` not found.

- [ ] **Step 5: The headers.** `emu/app/engine/app_types.h`:

```c
/*
 * app_types.h
 *
 * The plain types that cross from the firmware thread to the GUI and back
 * (stage 1b spec 6.1): no atomics, so C++ and Objective-C++ include it too.
 */
#ifndef APP_TYPES_H_
#define APP_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "emu_core.h"

typedef enum { APP_MIX_SUM, APP_MIX_OUT1, APP_MIX_OUT2, APP_MIX_FOUR } app_mix_t;

typedef struct app_outputRequest { char uid[128]; uint32_t bufferFrames; unsigned channels; } app_outputRequest_t;
typedef struct app_outputInfo
{
	double deviceRate, clientRate;
	uint32_t deviceLatency, streamLatency, safetyOffset, bufferFrames, channels;	/**< device frames */
	char name[128], format[160];	/**< the device; the formats and the converter (spec 6.9) */
} app_outputInfo_t;

/* the rings' entries (spec 6.1) */
typedef enum { APP_P_BUTTON, APP_P_ENCODER, APP_P_PUSH, APP_P_KNOB, APP_P_FADER, APP_P_JACKS, APP_P_CARD,
	APP_P_CLOCK, APP_P_RESET } app_panelKind_t;
/** BUTTON index 0-40, value 1 down; ENCODER value detents; PUSH value ms; KNOB index 1-4, value
 * degrees; FADER index 1-6, level; JACKS value EMU_JACK_* bits; CARD value 1 in; CLOCK and RESET
 * (value 1 on) carry the click's host time (E13) */
typedef struct app_panelEvent { uint64_t host; double level; int32_t value; uint8_t kind, index; } app_panelEvent_t;
typedef enum { APP_CMD_CARD_OUT, APP_CMD_CARD_IN, APP_CMD_DELAY, APP_CMD_MIX, APP_CMD_GENERATOR, APP_CMD_OUTPUT,
	APP_CMD_STOP } app_cmdKind_t;
/** DELAY a = 1 automatic, else x = D in s; MIX a = app_mix_t; GENERATOR a = on, x = BPM, b = PPQ (4 or 24);
 * OUTPUT text = device UID ("" the default), a = buffer frames (0 the device's) */
typedef struct app_cmd { uint8_t kind; int32_t a, b; double x; char text[128]; } app_cmd_t;
typedef enum { APP_ACK_CARD_OUT, APP_ACK_CARD_IN, APP_MSG_WARNING, APP_MSG_NOTICE } app_ackKind_t;
typedef struct app_ack { uint8_t kind; char text[255]; } app_ack_t;
enum { APP_NOTE_DEVICE, APP_NOTE_OVERLOAD };	/**< the notification ring's entries, one byte */

/** the firmware's counters of spec 5.5 */
typedef struct app_fwCounters
{
	uint32_t queueOverflow, dropouts, syncIgnored, intervalsExcluded, tempoRestarts, syncOverflows, usbTxDropped;
} app_fwCounters_t;

/** the diagnostics (spec 6.9), in every snapshot (E15) */
typedef struct app_diagnostics
{
	double blockRate, load;			/**< the last finished 2-s window */
	double L, D, Deff;				/**< s; Deff is 0 in internal sync */
	double leadMin, leadMax;		/**< s: drained events since the output opened */
	double inLatMin, inLatMax;		/**< s: the last finished 2-s window */
	int dAuto, extSync, outputRunning;
	uint64_t outUnderruns, callbacks, overloads, mapJumps, midiStale, midiInDropped, midiOutDropped,
		midiUnsent, panelDropped, snapDropped, ackDropped, dinOrphans, warnings;
	app_outputInfo_t output;
	emu_counters_t core;
	app_fwCounters_t fw;
} app_diagnostics_t;

typedef struct app_snapshot
{
	emu_lcdSnapshot_t lcd;
	uint64_t leds, vt;		/**< the latched LEDs (spec 4.7); the virtual time */
	uint8_t clockOut;		/**< the clock-out jack high at any time since the last snapshot */
	app_diagnostics_t diag;
} app_snapshot_t;

/** an output device for the GUI's list (Task 7) */
typedef struct app_outputDevice { uint32_t id; char uid[128], name[128]; unsigned channels; } app_outputDevice_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_TYPES_H_ */
```

`emu/app/engine/app_engine.h`:

```c
/*
 * app_engine.h
 *
 * The firmware thread of LICHOMAT Emu (stage 1b spec 6.1-6.5): power-on, slices
 * of one block, the tier hook, the sinks, and every ring between the firmware
 * thread and the outside. Time, waits and the output device come only through
 * app_host_t (plan E3). A function without a thread named runs on the firmware
 * thread.
 */
#ifndef APP_ENGINE_H_
#define APP_ENGINE_H_

#include <stdatomic.h>
#include <stdint.h>

#include "app_midi.h"
#include "app_ring.h"
#include "app_timemap.h"
#include "app_types.h"
#include "emu_core.h"

#define APP_AUDIO_FRAMES 16384u	/**< the audio ring, in 4-channel int16 frames (E5) */
#define APP_RING_SLACK 256u		/**< 4 blocks above the largest request (spec 6.2) */
#define APP_WARMUP 32u			/**< callbacks of an open before L is fixed (E7) */
#define APP_SNAP_BLOCKS 8u		/**< a snapshot every 8 blocks (E15) */
#define APP_STALE_SEC 0.1		/**< spec 4.3, E9 */
#define APP_STALL_SEC 0.5		/**< no callback for this long while the ring is full: a stall */
#define APP_RETRY_SEC 2.0		/**< reopen tries while no output runs */

typedef struct app_audioTime { uint64_t hostTime, entry; double rateScalar; } app_audioTime_t;
/** a time-map point: the ring position of the callback's first frame, its times, request and frames */
typedef struct app_point { uint64_t frame, hostTime, entry; double rateScalar; uint32_t request, got; } app_point_t;

typedef struct app_audio
{
	app_ring_t frames;				/**< int16_t[4] per frame, L1 R1 L2 R2: firmware -> callback */
	app_ring_t points;				/**< app_point_t: callback -> firmware */
	_Atomic int mix, mute;			/**< app_mix_t; 1: silence (a hang, spec 6.9) */
	_Atomic uint32_t maxRequest;	/**< the largest request so far (E6) */
	_Atomic uint64_t underruns, callbacks;	/**< written by the callback */
	void (*wake)(void *ctx);		/**< signals the firmware thread; never blocks */
	void *wakeCtx;
} app_audio_t;

/** Callback thread (spec 6.2): frames out of the ring with the mix, as int16 / 32,768,
 * silence for the rest and an underrun counted once frames were ever pushed; one
 * point; maxRequest; wake. Never blocks, allocates or locks. */
void app_audioRender(app_audio_t *a, const app_audioTime_t *ts, unsigned frames, float *out, unsigned channels);

typedef struct app_host
{
	void *ctx;
	double ticksPerSec;
	uint64_t (*now)(void *ctx);
	void (*waitAudio)(void *ctx, uint64_t until);	/**< returns at a wake() or at until */
	void (*sleepUntil)(void *ctx, uint64_t until);
	int (*outputOpen)(void *ctx, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info);	/**< 0 running */
	void (*outputClose)(void *ctx);
	void (*midiOutWake)(void *ctx);
} app_host_t;

typedef struct app_engineConfig
{
	const char *cardDir;
	uint64_t seed;
	int dAuto;
	double dSec;
	app_mix_t mix;
	app_outputRequest_t output;
	void (*fault)(void *ctx, const char *what);	/**< a model check (spec 11); NULL: print and abort() */
	void *faultCtx;
} app_engineConfig_t;

typedef struct app_engine
{
	app_host_t host;
	app_audio_t audio;
	app_ring_t panel, cmd, snap, ack, notes, midiOut;
	app_ring_t midiIn[2];	/**< [APP_PORT_USB], [APP_PORT_DIN]: one read proc each */
	_Atomic int hung;		/**< written by the watchdog thread */
	/* the firmware thread's */
	app_engineConfig_t cfg;
	app_outputRequest_t req;
	app_timemap_t map;
	app_outputInfo_t out;
	int outputRunning, dAuto, cardAck, clockLevel, clockSeen, genOn;
	uint8_t extSync;
	uint64_t retryAt, lastCallback, frameBase, dCycles, staleFrom, prev[3];
	uint64_t genT0, genK, genNext, genMbpm, genPpq;
	uint64_t blocks, slices, lastSnap, winHost, winBlocks, waitTicks, winWait;
	uint32_t warm, lReq;
	double lLead, L, winInMin, winInMax;
	app_usbPend_t usbPend;
	app_sx_t usbOutSx;
	app_dinAsm_t din;
	app_diagnostics_t diag;
} app_engine_t;

int app_engineInit(app_engine_t *e, const app_host_t *host);	/**< allocates every ring; before the threads */
void app_enginePowerOn(app_engine_t *e, const app_engineConfig_t *cfg);
int app_engineSlice(app_engine_t *e);	/**< one block of virtual time; 1 after APP_CMD_STOP */
int app_enginePanel(app_engine_t *e, const app_panelEvent_t *ev);	/**< GUI thread */
int app_engineCommand(app_engine_t *e, const app_cmd_t *c);		/**< GUI thread */
int app_engineNote(app_engine_t *e, unsigned kind);				/**< CoreAudio's listener queue */
int app_engineMidiPush(void *ctx, const app_midiEntry_t *m);		/**< read procs: into midiIn[m->port] */
int app_engineSnapshot(app_engine_t *e, app_snapshot_t *out);		/**< GUI thread: the newest, older dropped; 1 if any */
int app_engineAck(app_engine_t *e, app_ack_t *out);				/**< GUI thread */
typedef struct app_midiOutSink
{
	void *ctx;
	void (*send)(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n);
} app_midiOutSink_t;
/** MIDI-out thread (spec 6.5, E11): sends what is due at host time now; returns the
 * presentation time of the USB entry it waits for, or 0 when the ring is empty */
uint64_t app_midiOutStep(app_engine_t *e, uint64_t now, const app_midiOutSink_t *sink);

#endif /* APP_ENGINE_H_ */
```

- [ ] **Step 6: `app_fw.h` and `app_fw.c`**, the app's only firmware calls:

```c
/* app_fw.h: through emu_directCall() only (plan E12) */
#ifndef APP_FW_H_
#define APP_FW_H_
void app_fwExtSync(void *out);	/**< seq_getExtSync() into *(uint8_t *)out */
void app_fwCounters(void *out);	/**< into an app_fwCounters_t */
#endif

/* app_fw.c */
#include "app_fw.h"
#include "app_engine.h"
#include "main.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/seq_core.h"
#include "Soundengine/Sequencer/seq_platform.h"
#include "Soundengine/USB/usb_manager.h"

void app_fwExtSync(void *out)
{
	*(uint8_t *)out = seq_getExtSync();
}

void app_fwCounters(void *out)
{
	app_fwCounters_t *c = out;
	seqc_stats_t st;
	seqc_stats(&st);
	c->queueOverflow = st.queueOverflow;
	c->dropouts = st.dropouts;
	c->syncIgnored = st.syncIgnored;
	c->intervalsExcluded = st.intervalsExcluded;
	c->tempoRestarts = st.tempoRestarts;
	c->syncOverflows = seqp_syncOverflows();
	c->usbTxDropped = usb_midiTxDropped();
}
```

- [ ] **Step 7: `app_engine.c`.** Complete code for the callback, L and the map, and the entry rule; the rest follows as exact rules.

```c
#include "app_engine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_fw.h"

static uint64_t app_ticks(const app_engine_t *e, double sec)
{
	return (uint64_t)(sec * e->host.ticksPerSec);
}

void app_audioRender(app_audio_t *a, const app_audioTime_t *ts, unsigned frames, float *out, unsigned channels)
{
	const uint64_t first = app_ringPopped(&a->frames);
	const uint32_t avail = app_ringCount(&a->frames);
	const unsigned n = frames < avail ? frames : avail;
	const int mix = atomic_load_explicit(&a->mix, memory_order_relaxed);
	const int mute = atomic_load_explicit(&a->mute, memory_order_relaxed);
	const unsigned used = mix == APP_MIX_FOUR && channels >= 4u ? 4u : 2u;
	for (unsigned i = 0; i < n; i++)
	{
		const int16_t *f = app_ringPeek(&a->frames, i);
		int32_t v[4] = { f[0], f[1], f[2], f[3] };
		if (mix == APP_MIX_SUM || (mix == APP_MIX_FOUR && used == 2u))
		{
			v[0] = (v[0] + v[2]) >> 1;	/* (out 1 + out 2) / 2 in int32, as --stereo sum */
			v[1] = (v[1] + v[3]) >> 1;
		}
		else if (mix == APP_MIX_OUT2)
		{
			v[0] = v[2];
			v[1] = v[3];
		}
		float *o = out + (size_t)i * channels;
		for (unsigned c = 0; c < channels; c++)
			o[c] = mute || c >= used ? 0.0f : (float)v[c] / 32768.0f;
	}
	for (size_t i = (size_t)n * channels; i < (size_t)frames * channels; i++)
		out[i] = 0.0f;
	app_ringPop(&a->frames, n);
	if (n < frames && first + avail > 0u)
		atomic_fetch_add_explicit(&a->underruns, 1u, memory_order_relaxed);
	const app_point_t p = { first, ts->hostTime, ts->entry, ts->rateScalar, frames, n };
	(void)app_ringPush(&a->points, &p);
	if (frames > atomic_load_explicit(&a->maxRequest, memory_order_relaxed))
		atomic_store_explicit(&a->maxRequest, frames, memory_order_relaxed);
	atomic_fetch_add_explicit(&a->callbacks, 1u, memory_order_relaxed);
	if (a->wake)
		a->wake(a->wakeCtx);
}

static void app_engineSetL(app_engine_t *e)
{
	const app_latencyIn_t in = { e->lLead, e->out.deviceRate, e->out.deviceLatency, e->out.streamLatency, e->lReq, 0 };
	e->L = app_latencyL(&in);
	if (e->dAuto)
		e->dCycles = (uint64_t)llround((e->L + APP_D_MARGIN) * EMU_CPU_HZ);
}

/* the callback's points: warm-up and L (E7), then the time map (spec 6.3) */
static void app_enginePoints(app_engine_t *e)
{
	const app_point_t *p;
	while ((p = app_ringPeek(&e->audio.points, 0u)) != NULL)
	{
		e->lastCallback = p->entry;
		const double lead = (double)(int64_t)(p->hostTime - p->entry) / e->host.ticksPerSec;
		if (e->warm < APP_WARMUP)
		{
			e->lLead = lead > e->lLead ? lead : e->lLead;
			e->lReq = p->request > e->lReq ? p->request : e->lReq;
			if (++e->warm == APP_WARMUP)
				app_engineSetL(e);
		}
		else if (p->request > e->lReq)
		{
			e->lReq = p->request;	/* a larger buffer */
			app_engineSetL(e);
		}
		if (p->got > 0u)
		{
			emu_counters_t c;
			emu_getCounters(&c);
			/* mHostTime holds the safety offset and the converter; the latencies are added once */
			const double lat = (double)(e->out.deviceLatency + e->out.streamLatency) / e->out.deviceRate
				* e->host.ticksPerSec * p->rateScalar;
			if (app_tmPoint(&e->map, app_frameCycles(c.codecStart, (double)(e->frameBase + p->frame)),
					p->hostTime + (uint64_t)llround(lat)))
				e->staleFrom = e->host.now(e->host.ctx);	/* host time now, not the presentation time (E9) */
		}
		app_ringPop(&e->audio.points, 1u);
	}
}

static void app_minMax(double *lo, double *hi, double v)
{
	*lo = v < *lo ? v : *lo;
	*hi = v > *hi ? v : *hi;
}

/* the entry time of an event on a port (spec 6.4), 0 if it is stale (E9) */
static int app_engineEntry(app_engine_t *e, unsigned port, uint64_t stamp, uint64_t now, uint64_t *t)
{
	if (e->staleFrom && stamp + app_ticks(e, APP_STALE_SEC) < e->staleFrom)
	{
		e->diag.midiStale++;
		return 0;
	}
	const uint64_t d = e->extSync ? e->dCycles : 0u;	/* D only in sync modes 1 and 2 (E12) */
	*t = app_entryTime(app_tmVirtual(&e->map, stamp), d, now, e->prev[port]);
	e->prev[port] = *t;
	const double tps = e->host.ticksPerSec;
	app_minMax(&e->diag.leadMin, &e->diag.leadMax, ((double)app_tmHost(&e->map, (double)now) - (double)stamp) / tps);
	app_minMax(&e->winInMin, &e->winInMax, ((double)app_tmHost(&e->map, (double)*t) - (double)stamp) / tps);
	return 1;
}

```

The glue, each function exactly as stated:

- `app_engineOpen(e)` (the callback is not running): reset `audio.frames` and `audio.points`; `frameBase = 64 × (blocks + 1)` from `emu_getCounters()`, the next block request's first frame; `warm = lReq = 0`, `lLead = 0`, `diag.leadMin = 1e9`, `diag.leadMax = −1e9`; if the map is not valid yet, `app_tmWall(&map, 0, now)` (power-on); `req.channels` = 4 for `APP_MIX_FOUR`, else 2; `outputRunning = (outputOpen(…) == 0)`; `lastCallback = now`; if running, `maxRequest = ⌈bufferFrames × APP_FS / deviceRate⌉` (E6), else `retryAt = now + APP_RETRY_SEC`.
- `app_engineClose(e)`: `outputClose()` if running; `outputRunning = 0`; `retryAt = 0`; `app_tmWall(&map, emu_now(), hostNow)`; `staleFrom = hostNow` (E9). A reopen (device change, `APP_CMD_OUTPUT`, a mix to or from four channels, a stall) is Close then Open, so the wall clock paces until new callback points re-anchor the map; a retry without a device keeps the wall anchor.
- `app_engineWaitRing(e)` (spec 6.2): while `app_ringCount(frames) ≥ maxRequest + APP_RING_SLACK`: if `hostNow > lastCallback + APP_STALL_SEC`, `app_engineClose()` and stop (a stall); else `waitAudio(hostNow + 0.1 s)`, then `app_enginePoints()`. The whole wait sits inside one `emu_waitBegin()`/`emu_waitEnd()` (spec 4.2), and its host time goes into `waitTicks`.
- `app_enginePaceWall(e, now)`: `until = app_tmHost(&map, now)`; if the host is more than 100 ms past `until`, `app_tmWall(&map, now, hostNow)` and `staleFrom = hostNow` (no catching up); else, if `until` is ahead, `sleepUntil(until)` inside `emu_waitBegin()`/`emu_waitEnd()`, its time into `waitTicks`.
- `app_enginePanelEvent(e, p, now)`: BUTTON `emu_inButton(now, index, value)`, ENCODER `emu_inEncoder(now, value)`, PUSH `emu_inPush(now, value)`, KNOB `emu_inKnob(now, index, value)`, FADER `emu_inFader(now, index, level)`, JACKS `emu_inJacks(now, value)`, CARD `emu_inCard(now, value)`; CLOCK and RESET take `app_engineEntry(e, APP_PORT_JACK, p->host, now, &t)`, then `emu_inClockPulse(t)` or `emu_inReset(t, value)` (E13).
- `app_engineInputs(e, now)`: the panel ring through `app_enginePanelEvent()`; `app_engineMidiInputs()` (an empty static function until Task 5); then the generator (spec 6.8): while `genOn` and `genNext ≤ now + 1 ms`: `emu_inClockPulse(genNext)`, `genK++`, `genNext = genT0 + ⌊genK × 12,960,000,000,000 / (genMbpm × genPpq)⌋` in `unsigned __int128` (cycles per minute × 1000, as `lichomat-render`'s `jack-clock`).
- `app_hook(ctx, now, why)`: `app_enginePoints()`; `app_engineInputs()`; a snapshot at every 8th block hook and at a time hook 10 ms or more after the last snapshot; then, at a block hook with the output running, `app_engineWaitRing()`; otherwise, while the map is not on audio points (`!map.audio`), `app_enginePaceWall()`.
- `app_sinkAudio(ctx, t, out1, out2)`: nothing unless the output runs (the next open's `frameBase` keeps the frames counted); else the 64 frames into free slots, L1 `out1[2i]`, R1 `out1[2i+1]`, L2 `out2[2i]`, R2 `out2[2i+1]`, and one commit. `app_sinkClock(ctx, t, level)`: `clockLevel = level`, `clockSeen |= level` (the latch of spec 6.8).
- `app_engineCommands(e)`, between slices (E14): CARD_OUT and CARD_IN `emu_inCard(emu_now(), in)` and `cardAck` = the matching acknowledgement; DELAY `dAuto = a`, then `dCycles = llround(x × EMU_CPU_HZ)` if not automatic, or `app_engineSetL()` if automatic and the warm-up is over; MIX `atomic_exchange()` into `audio.mix`, and a reopen if four channels came or went; GENERATOR `genOn = a && x ≥ 1 && b ∈ {4, 24}`, `genMbpm = llround(x × 1000)`, `genPpq = b`, `genT0 = genNext = emu_now()`, `genK = 0`; OUTPUT `req.uid = text`, `req.bufferFrames = a`, a reopen; STOP: return 1 once the ring is drained.
- `app_engineSlice(e)`: the commands (return 1 on STOP); the notification ring (OVERLOAD counts `diag.overloads`, any DEVICE makes one reopen); else, without output and at `retryAt`, `app_engineOpen()`; `emu_runUntil(emu_now() + emu_framesToCycles(64))` (the first call boots); a pending `cardAck` goes into the acknowledgement ring now, after the pass boundary; `emu_directCall(app_fwExtSync, &extSync)`; every 8th slice `emu_directCall(app_fwCounters, &diag.fw)`; return 0.

- `static void app_engineMidiInputs(app_engine_t *e, uint64_t now)`: `(void)e; (void)now;` until Task 5.
- `app_engineInit()`: `memset(e, 0, sizeof *e)`, copies the host, `app_tmInit(&e->map, host->ticksPerSec)`, `cardAck = -1`, `dAuto = 1`, `winInMin = 1e9`, `winInMax = -1e9`, and initialises the rings, returning -1 if one fails: `audio.frames` 8 B × `APP_AUDIO_FRAMES`; `audio.points` `sizeof(app_point_t)` × 1,024; `panel` 256; `cmd` 64; `snap` 16; `ack` 64; `notes` 1 B × 64; `midiIn[0]` and `midiIn[1]` 2,048 each; `midiOut` 4,096. `audio.wake` stays NULL; the shell sets it (Task 6).
- `app_enginePowerOn()`: copies `cfg` to `e->cfg` and `cfg->output` to `e->req`; `dAuto`, `dCycles = llround(dSec × EMU_CPU_HZ)` and the mix; then `emu_configDefaults()`, `cardDir`, `cardIn = cardDir != NULL`, `seed`, sinks `{ e, app_sinkAudio, NULL, NULL, app_sinkClock }` (Task 5 fills DIN and USB), `hook = app_hook`, `hookCtx = e`, `fault = cfg->fault ? app_engineFault : NULL` (`app_engineFault` calls `e->cfg.fault(e->cfg.faultCtx, what)`; `msgCtx` is shared with `warn`), `warn = app_engineWarn` (pushes `APP_MSG_WARNING` with the text cut to 254 characters, and counts `diag.warnings` also when the ring is full), `msgCtx = e`; `emu_powerOn()`; `app_engineOpen()`.
- `app_engineSnap(e, now)` (declare it before `app_hook`): fills an `app_snapshot_t` with `emu_lcdSnapshot()`, `emu_ledSnapshot()`, `clockOut = clockSeen || clockLevel` (then `clockSeen = 0`), `vt = now` and the diagnostics, pushes it (a full ring drops it), sets `lastSnap`. The diagnostics: when `host.now() − winHost ≥ 2 s`, `blockRate = (core.blocks − winBlocks) / dt`, `load = 1 − (waitTicks − winWait) / dtTicks`, `inLatMin`/`inLatMax` = the window's values (unchanged when the window had no event), then a new window (`winHost`, `winBlocks`, `winWait`, `winInMin = 1e9`, `winInMax = −1e9`); always `L`, `D = dCycles / EMU_CPU_HZ`, `Deff = extSync ? D : 0`, `dAuto`, `extSync`, `outputRunning`, `output = out`, the callback's `underruns` and `callbacks`, `mapJumps = map.jumps`, `midiInDropped` (both rings' drops), `midiOutDropped`, `panelDropped`, `dinOrphans = din.orphans`, `midiUnsent`, `snapDropped` and `ackDropped` = `app_ringDropped()` of those two rings (the firmware thread is their producer, §11), `warnings`, `core` from `emu_getCounters()`.
- `app_enginePanel`, `app_engineCommand`, `app_engineNote`: `app_ringPush()` into `panel`, `cmd`, `notes`. `app_engineMidiPush(ctx, m)`: `app_ringPush(&e->midiIn[m->port == APP_PORT_DIN], m)`.
- `app_engineSnapshot(e, out)`: while more than one entry is pending, pop; copy and pop the last; 1, or 0 if none. `app_engineAck`: copy and pop one; 1, or 0.

- [ ] **Step 8:** Run EMU-CHECK. Expected: N = 187, 0 warnings. `R4_engine_ring`'s `c.underruns` (the firmware's own) goes into the commit message; a value above 1 is a finding, not a tolerance to widen. Then the gate.

- [ ] **Step 9: Commit** the seven files and `emu/CMakeLists.txt`; message `Emulator app: the engine, the audio ring, pacing, commands and snapshots (E5.5 R4)`.

---

### Task 5: Engine II — MIDI in and out, and M18's scenarios (§6.4, §6.5, §12.1 M18)

**Files:**
- Modify: `emu/app/engine/app_engine.c`, `emu/CMakeLists.txt`
- Create: `emu/tests/test_app_m18.c`

- [ ] **Step 1: The test** `emu/tests/test_app_m18.c`, registered with `emu_add_app_test(test_app_m18 ENGINE IDS M18_midi_out_step M18_slave_90 M18_slave_128 M18_slave_174 M18_din_128 M18_zero_d)`:

```c
#include "app_fake.h"

#define MAXK 4096u
static app_engine_t E;
/* the fake DAW (spec 12.1 M18): each clock stamped with its exact time and handed over at it (spec 6.4) */
static struct { uint64_t t0, start, k, stamps[MAXK]; double per; int started; uint8_t port; } daw;
static struct { uint64_t t[256]; unsigned n; } notes;	/* the first USB Note On of each step, presented */
static struct { uint64_t when[8]; uint8_t port[8]; unsigned n; } sent;
static app_snapshot_t snap;	/* the newest snapshot: slave() empties the ring at every slice */

static uint64_t dawClock(uint64_t k)
{
	return daw.t0 + (uint64_t)llround((double)k * daw.per);
}

static void dawEvent(fake_t *f)
{
	uint32_t w = 0x10F80000u;
	if (!daw.started && f->nextEvent == daw.start)
	{
		w = 0x10FA0000u;
		daw.started = 1;
	}
	else if (daw.k < MAXK)
		daw.stamps[daw.k++] = f->nextEvent;
	app_midiSplit(f->nextEvent, daw.port, &w, 1u, app_engineMidiPush, &E);
	const uint64_t next = dawClock(daw.k);
	f->nextEvent = !daw.started && daw.start < next ? daw.start : next;
}

static void recordNotes(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n)
{
	(void)ctx;
	for (unsigned i = 0; i < n; i++)
		if (port == APP_PORT_USB && (w[i] & 0xFFF00000u) == 0x20900000u && (w[i] & 0x7Fu) != 0u && notes.n < 256u
			&& (notes.n == 0u || when > notes.t[notes.n - 1u] + 5u * 24000u))
			notes.t[notes.n++] = when;	/* 7 voices share a step */
}

static void recordSent(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n)
{
	(void)ctx;
	(void)w;
	(void)n;
	sent.when[sent.n] = when;
	sent.port[sent.n++] = port;
}

/* the init card in MID sync, a DAW clock from host 2 s, Start 1 ms before clock 48, until host 10 s */
static void slave(double bpm, int dAuto, uint8_t port)
{
	app_host_t h;
	fakeHost(&h, 1);
	if (app_engineInit(&E, &h) != 0)
		exit(2);
	static char card[1024];
	fakeCard(card, sizeof card, 1u);
	const app_engineConfig_t c = { .cardDir = card, .seed = 1u, .dAuto = dAuto, .mix = APP_MIX_SUM };
	app_enginePowerOn(&E, &c);
	memset(&daw, 0, sizeof daw);
	memset(&notes, 0, sizeof notes);
	daw.port = port;
	daw.per = FAKE_TPS * 60.0 / (bpm * 24.0);
	daw.t0 = 1000u + 2u * 24000000u;
	daw.start = dawClock(48u) - 24000u;
	fk.event = dawEvent;
	fk.nextEvent = daw.t0;
	const app_midiOutSink_t sink = { NULL, recordNotes };
	while (fk.now < 1000u + 10u * 24000000u)
	{
		app_engineSlice(&E);
		(void)app_midiOutStep(&E, UINT64_MAX, &sink);
		(void)app_engineSnapshot(&E, &snap);	/* the ring drops new entries when full (E15) */
	}
}

/* latency of step n's Note On against its clock: steps 1, 5, 9 and 13 fall every 24 clocks from clock 48 */
static double latency(unsigned n)
{
	return ((double)notes.t[n] - (double)daw.stamps[48u + 24u * n]) / FAKE_TPS;
}

static void constant(double bpm, uint8_t port)
{
	slave(bpm, 1, port);
	const double D = (double)E.dCycles / EMU_CPU_HZ;
	ASSERT_NEAR(E.L * 1e3, 57.0, 0.1);
	ASSERT_NEAR(D, E.L + APP_D_MARGIN, 1e-6);	/* D's default */
	ASSERT_TRUE(notes.n >= 8u);
	for (unsigned n = 0; n < notes.n && 48u + 24u * n < daw.k; n++)
		ASSERT_TRUE(latency(n) >= D - 0.0001 && latency(n) <= D + 0.00145);	/* D + 0-1.45 ms */
	/* L is at least every measured lead; 0.05 ms covers the points' rounding (about 11 us) */
	ASSERT_TRUE(snap.diag.leadMax <= E.L + 0.00005);
	ASSERT_NEAR(snap.diag.inLatMin, D, 0.0002);	/* with D at least L, the input latency is D (spec 6.9) */
	ASSERT_NEAR(snap.diag.inLatMax, D, 0.0002);
	ASSERT_EQ(snap.diag.midiStale, 0);
}

TEST(M18_slave_90) { constant(90.0, APP_PORT_USB); }
TEST(M18_slave_128) { constant(128.0, APP_PORT_USB); }
TEST(M18_slave_174) { constant(174.0, APP_PORT_USB); }
TEST(M18_din_128) { constant(128.0, APP_PORT_DIN); }	/* A6's path: the DIN wire model */

TEST(M18_zero_d)
{
	slave(128.0, 0, APP_PORT_USB);
	ASSERT_TRUE(notes.n >= 8u);
	for (unsigned n = 0; n < notes.n && 48u + 24u * n < daw.k; n++)
		ASSERT_TRUE(latency(n) >= 0.0 && latency(n) <= E.L + 0.00145);
	ASSERT_TRUE(snap.diag.inLatMax - snap.diag.inLatMin >= 0.010);	/* D = 0: the arrival phase shows (spec 12.1 M18) */
}

TEST(M18_midi_out_step)
{
	app_host_t h;
	fakeHost(&h, 1);
	ASSERT_EQ(app_engineInit(&E, &h), 0);
	const app_midiEntry_t m[3] = { { 1000u, APP_PORT_USB, 1u, { 0x10F80000u } },
		{ 5000u, APP_PORT_DIN, 1u, { 0x20903C64u } }, { 6000u, APP_PORT_USB, 1u, { 0x10F80000u } } };
	for (unsigned i = 0; i < 3u; i++)
		ASSERT_EQ(app_ringPush(&E.midiOut, &m[i]), 0);
	const app_midiOutSink_t sink = { NULL, recordSent };
	ASSERT_EQ(app_midiOutStep(&E, 500u, &sink), 1000);	/* USB waits for its presentation time */
	ASSERT_EQ(sent.n, 0);
	ASSERT_EQ(app_midiOutStep(&E, 1000u, &sink), 6000);	/* DIN goes at once, with its stamp (E11) */
	ASSERT_EQ(sent.n, 2);
	ASSERT_EQ(sent.port[1], APP_PORT_DIN);
	ASSERT_EQ(sent.when[1], 5000);
	ASSERT_EQ(app_midiOutStep(&E, 7000u, &sink), 0);
	ASSERT_EQ(sent.n, 3);
}

TEST_MAIN()
```

- [ ] **Step 2:** Run EMU-CHECK. Expected: the build fails, `app_midiOutStep` is not defined.

- [ ] **Step 3: MIDI in.** Replace the empty `app_engineMidiInputs()`:

```c
static const app_midiEntry_t *app_peekUsb(void *ctx, unsigned i)
{
	return app_ringPeek(&((app_engine_t *)ctx)->midiIn[APP_PORT_USB], i);
}

static void app_engineMidiInputs(app_engine_t *e, uint64_t now)
{
	const app_midiEntry_t *m;
	uint64_t t;
	while ((m = app_ringPeek(&e->midiIn[APP_PORT_DIN], 0u)) != NULL)
	{
		uint8_t b[64];
		const unsigned n = app_umpToBytes(m->words, m->nwords, b, sizeof b);
		if (app_engineEntry(e, APP_PORT_DIN, m->stamp, now, &t) && n)
			emu_inDin(t, b, n);	/* the wire model spaces the bytes from t (spec 4.3) */
		app_ringPop(&e->midiIn[APP_PORT_DIN], 1u);
	}
	while ((m = app_ringPeek(&e->midiIn[APP_PORT_USB], 0u)) != NULL)
	{
		uint8_t data[64];
		unsigned len;
		const uint64_t stamp = m->stamp;
		const unsigned taken = app_usbTransfer(app_peekUsb, e, &e->usbPend, data, &len);
		if (app_engineEntry(e, APP_PORT_USB, stamp, now, &t) && len)
			(void)emu_inUsb(t, data, len);	/* one transfer; the core paces 16 packets per 1 ms */
		if (!taken)
			e->diag.midiUnsent++;	/* E10: it fits no transfer; counted, not dropped silently */
		app_ringPop(&e->midiIn[APP_PORT_USB], taken ? taken : 1u);
	}
}
```

- [ ] **Step 4: MIDI out.** Set the sinks in `app_enginePowerOn()` to `{ e, app_sinkAudio, app_sinkDin, app_sinkUsb, app_sinkClock }`, with:
  - `app_pushOut(ctx, m)`: push into `midiOut` (a full ring counts), then `host.midiOutWake()`;
  - `app_sinkDin(ctx, t, byte)`: `app_dinByte(&din, t, byte, app_emitDin, e)`, where `app_emitDin(ctx, t, w, n)` is `app_midiSplit(app_tmHost(&map, t), APP_PORT_DIN, w, n, app_pushOut, e)`: each complete message at the presentation time of its first byte (spec 6.5);
  - `app_sinkUsb(ctx, t, data, length)`: `app_usbToUmp(t, data, length, &usbOutSx, …)` collects the transfer's words (at most 64) in a local buffer, then one `app_midiSplit(app_tmHost(&map, t), APP_PORT_USB, …)`: one event packet per transfer, at its presentation time.

  And the MIDI-out step:

```c
uint64_t app_midiOutStep(app_engine_t *e, uint64_t now, const app_midiOutSink_t *sink)
{
	const app_midiEntry_t *m;
	while ((m = app_ringPeek(&e->midiOut, 0u)) != NULL)
	{
		if (m->port == APP_PORT_USB && m->stamp > now)
			return m->stamp;
		sink->send(sink->ctx, m->port, m->stamp, m->words, m->nwords);
		app_ringPop(&e->midiOut, 1u);
	}
	return 0;
}
```

- [ ] **Step 5:** Run EMU-CHECK. Expected: N = 193, 0 warnings. Record in the commit message, per BPM and port, the smallest and largest latency minus D, `L`, `leadMax`, and `M18_zero_d`'s `inLatMax − inLatMin`. If the first step plays at another clock than 48 (the first after Start), stop and report: it is a question about the timing core, not a test to change. Then the gate.

- [ ] **Step 6: Commit** `emu/app/engine/app_engine.c`, `emu/tests/test_app_m18.c`, `emu/CMakeLists.txt`; message `Emulator app: MIDI in and out through the engine; M18 (E5.5 R4)`.

---

### Task 6: The Mach host, the threads, the shell and `lichomat-emu-dev` (§6.1, §4.2, E1)

**Files:**
- Create: `emu/app/mac/app_mach.{h,c}`, `emu/app/mac/app_shell.{h,c}`, `emu/app/dev/dev_main.c`, `emu/tests/app_test.py`
- Modify: `emu/CMakeLists.txt` (the app section; M21's binaries)

- [ ] **Step 1: `app_mach.h`**:

```c
/*
 * app_mach.h
 *
 * The real host of the engine (plan E3): mach_absolute_time(), Mach
 * semaphores that the callback may signal, the time-constraint policy
 * (spec 6.1). The output's open and close come from app_coreaudio.c (Task 7).
 */
#ifndef APP_MACH_H_
#define APP_MACH_H_

#include <mach/mach.h>
#include <stdint.h>

#include "app_engine.h"

typedef struct app_mach
{
	semaphore_t fwWake, midiWake;
	double ticksPerSec;
	app_engine_t *engine;
	void *output;		/**< the app_ca_t of Task 7; NULL: no audio (--no-audio) */
	double period;		/**< s: the firmware thread's period, the output buffer's duration */
	int realtime;		/**< 1 if thread_policy_set() took the policy (E29) */
} app_mach_t;

int app_machInit(app_mach_t *m, app_engine_t *e);
void app_machWake(void *ctx);	/**< the callback's wake: semaphore_signal(fwWake); ctx is the app_mach_t */
void app_machHost(app_mach_t *m, app_host_t *h);
void app_machWaitSem(semaphore_t sem, uint64_t until, double ticksPerSec);
/** THREAD_TIME_CONSTRAINT_POLICY for the calling thread: period, 25 % computation, constraint = period */
int app_threadRealtime(double periodSec);
double app_wallSeconds(void);	/**< CLOCK_MONOTONIC */
uint64_t app_seedNow(void);		/**< E27 */

#endif /* APP_MACH_H_ */
```

- [ ] **Step 2: `app_mach.c`.** `app_machInit()`: `mach_timebase_info()` gives `ticksPerSec = 1e9 × denom / numer` (24 MHz here); `semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0)` for both semaphores; `period = 512 / 48,000`; −1 on a failure. `app_machHost()` fills `{ m, ticksPerSec, now = mach_absolute_time(), waitAudio = app_machWaitSem(fwWake, …), sleepUntil = mach_wait_until(), outputOpen = −1 (no audio until Task 7), outputClose = nothing, midiOutWake = semaphore_signal(midiWake) }`. `app_machWake()` is `semaphore_signal(m->fwWake)`, which the callback may call. The three calls with traps, complete:

```c
void app_machWaitSem(semaphore_t sem, uint64_t until, double ticksPerSec)
{
	const uint64_t now = mach_absolute_time();
	if (until <= now)
		return;
	const double sec = (double)(until - now) / ticksPerSec;
	const mach_timespec_t ts = { (unsigned)sec, (clock_res_t)((sec - floor(sec)) * 1e9) };
	(void)semaphore_timedwait(sem, ts);	/* a relative timeout */
}

int app_threadRealtime(double periodSec)
{
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	thread_time_constraint_policy_data_t p;	/* in mach ticks (spec 6.1) */
	p.period = (uint32_t)(periodSec * 1e9 * (double)tb.denom / (double)tb.numer);
	p.computation = p.period / 4u;
	p.constraint = p.period;
	p.preemptible = 1;
	return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
		(thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS ? 0 : -1;
}

uint64_t app_seedNow(void)	/* E27: never time() */
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ((uint64_t)ts.tv_sec << 30) ^ (uint64_t)ts.tv_nsec ^ mach_absolute_time();
}
```

- [ ] **Step 3: `app_shell.h`**, opaque, so that the C++ GUI includes it (the struct lives in `app_shell.c`; Tasks 7–10 add the fields, options and calls they name):

```c
/*
 * app_shell.h
 *
 * LICHOMAT Emu without its face: start-up, the threads, the per-frame work of
 * the main thread. lichomat-emu-dev (E1) and the GUI (Task 12) both drive it,
 * through this header only.
 */
#ifndef APP_SHELL_H_
#define APP_SHELL_H_

#include <stddef.h>

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_shell app_shell_t;

typedef struct app_shellOptions
{
	const char *cardDir;	/**< the card folder; Task 9: NULL is the app folder's Card */
	int noAudio, noMidi;
	void (*message)(void *ctx, const char *text);	/**< warnings, notices, errors; main thread */
	void *ctx;
} app_shellOptions_t;

app_shell_t *app_shellNew(void);	/**< calloc'd; one per process */
int app_shellStart(app_shell_t *s, const app_shellOptions_t *o, char *err, size_t n);
int app_shellFrame(app_shell_t *s);	/**< main thread: snapshot, messages; 1 if a new snapshot came */
const app_snapshot_t *app_shellSnapshot(const app_shell_t *s);	/**< the newest; NULL before the first */
int app_shellPanel(app_shell_t *s, const app_panelEvent_t *ev);
int app_shellCommand(app_shell_t *s, const app_cmd_t *c);
int app_shellHung(const app_shell_t *s);
int app_shellWarnings(const app_shell_t *s);				/**< the warnings kept, at most the last 64 (spec 11) */
const char *app_shellWarning(const app_shell_t *s, int i);	/**< oldest first */
void app_shellTestFault(app_shell_t *s);	/**< harness only (Task 10): SIGSEGV on the firmware thread */
void app_shellStop(app_shell_t *s);		/**< APP_CMD_STOP; joins the firmware thread for up to 2 s */

#ifdef __cplusplus
}
#endif

#endif /* APP_SHELL_H_ */
```

  In `app_shell.c`: `struct app_shell { app_shellOptions_t o; app_engine_t engine; app_mach_t mach; app_engineConfig_t cfg; pthread_t firmware, watchdog; _Atomic int stopped, testFault; int hungShown, haveSnap; app_snapshot_t snap; };`

- [ ] **Step 4: `app_shell.c`.** The firmware thread: `mach.realtime = app_threadRealtime(mach.period) == 0` (E29); `app_enginePowerOn()`; `while (!app_engineSlice())`, and between slices `raise(SIGSEGV)` when `testFault` is set (Task 10's crash check, on this thread); then `stopped = 1`. The watchdog thread: every 100 ms (`nanosleep`), until `stopped`, `emu_watchdogPoll(&w, app_wallSeconds())`; when it returns 1 (10 s without progress, the hook's waits not counted, spec 4.2), `audio.mute = 1` (the audio stops, spec 6.9), `engine.hung = 1`, and the thread ends. `app_shellStart()`: copies the options; `app_machInit(&s->mach, &s->engine)`, `app_machHost(&s->mach, &host)`, `app_engineInit(&s->engine, &host)` (it zeroes the engine), then `s->engine.audio.wake = app_machWake` and `wakeCtx = &s->mach`; `cfg = { cardDir, app_seedNow(), dAuto 1, dSec 0, APP_MIX_SUM, output { "", 0, 2 }, fault = the shell's fault handler (Task 10; until then NULL) }`; `pthread_create()` for the firmware thread and the watchdog; on any failure "app: …" into `err` and −1. `app_shellFrame()`: every acknowledgement — `APP_MSG_WARNING` as `warning: TEXT`, `APP_MSG_NOTICE` as `notice: TEXT` — goes to `o.message`, and each warning also into a list of the last 64 that `app_shellWarnings()` and `app_shellWarning()` hand to the diagnostics (§11: a skipped non-8.3 name or a duplicate slot shows there, not only as a message); `app_engineSnapshot()` into `s->snap`; when `engine.hung` is first seen, the message `hang: no progress for 10 s; the audio is stopped. Restart LICHOMAT Emu.` (§11). `app_shellPanel` and `app_shellCommand` push and turn a full ring into −1; `app_shellHung()` reads `engine.hung`; `app_shellTestFault()` sets `testFault`.

- [ ] **Step 5: `lichomat-emu-dev`** (`emu/app/dev/dev_main.c`). Options: `--card DIR` (Task 9 adds `--home`, `--render`), `--no-audio`, `--no-midi`, `--seconds S` (end after S s of wall time), `--script FILE` (lines `SECONDS COMMAND`, run at that wall time from the start), `--print-diag` (a `diag` line at the end), `--print-leds`. At start, `setvbuf(stdout, NULL, _IOLBF, 0)`: the tests read stdout through a pipe, which is block-buffered, and Task 9's `execv()` would drop buffered lines. The main thread loops: `poll()` on stdin with a 20 ms timeout, one command per line; the due script lines; `app_shellFrame()`; prints. At the end of stdin (`read()` returns 0, or `POLLHUP`), it stops polling stdin and keeps running without spinning (`nanosleep()` of 20 ms per turn): `--seconds` or `quit` ends the run, and without `--seconds` a signal does. Output lines, one per event, wall time from the start with 3 decimals:
  - `lcd T |ROW0|ROW1|` when the newest snapshot's rows differ from the last printed, 16 characters each, a code outside 0x20–0x7E as `?`;
  - `led T HEX` when the LED mask changes (with `--print-leds`);
  - `warning: …`, `notice: …`, `hang: …` from the shell;
  - `diag blockRate=%.2f load=%.3f L=%.2fms D=%.2fms Deff=%.2fms lead=%.2f..%.2fms inLat=%.2f..%.2fms outUnderruns=%llu fwUnderruns=%llu output=%s realtime=%d vt=%.3f wall=%.3f` for `diag` and at the end with `--print-diag` (`output` is the device name or `none`; `vt` is the newest snapshot's virtual time in s, `wall` the wall time since power-on at which that snapshot was read).

  Commands (each line is one; unknown ones print `error: …`):

  | Command | Effect |
  |---|---|
  | `press N [MS]`, `down N`, `up N` | button N 0–40; `press` holds MS (default 50) |
  | `enc D`, `push [MS]`, `knob K DEG`, `fader F L`, `jacks MASK`, `card in\|out` | the panel events of `app_panelEvent_t` |
  | `pulse`, `reset on\|off` | `APP_P_CLOCK`, `APP_P_RESET` with `mach_absolute_time()` |
  | `gen BPM PPQ`, `gen off` | `APP_CMD_GENERATOR` |
  | `delay auto`, `delay MS` | `APP_CMD_DELAY` |
  | `mix sum\|out1\|out2\|four` | `APP_CMD_MIX` |
  | `diag`, `quit` | print the diagnostics; stop |

  At the end: `app_shellStop()`, exit 0; exit 1 for bad options; exit 2 if the shell does not start.

- [ ] **Step 6: CMake**, after the app tests of Task 5:

```cmake
if(LICHOMAT_EMU_APP)
    set(EMU_APP_MAC_SOURCES "${EMU_APP_DIR}/mac/app_mach.c" "${EMU_APP_DIR}/mac/app_shell.c")
    add_library(emu_app_mac OBJECT ${EMU_APP_MAC_SOURCES})
    target_include_directories(emu_app_mac PUBLIC "${EMU_APP_DIR}/mac" "${EMU_APP_DIR}/engine" "${EMU_DIR}/core"
        "${EMU_DIR}/common")
    target_compile_options(emu_app_mac PRIVATE ${EMU_APP_FLAGS})
    set(EMU_APP_FRAMEWORKS "-framework CoreFoundation")
    set(EMU_APP_OBJECTS $<TARGET_OBJECTS:emu_app_mac> $<TARGET_OBJECTS:emu_app_units> $<TARGET_OBJECTS:emu_app_engine>
        $<TARGET_OBJECTS:emu_app_fw> ${EMU_DEVICE_OBJECTS})
    add_executable(lichomat-emu-dev "${EMU_APP_DIR}/dev/dev_main.c" ${EMU_APP_OBJECTS})
    target_include_directories(lichomat-emu-dev PRIVATE "${EMU_APP_DIR}/mac" "${EMU_APP_DIR}/engine" "${EMU_DIR}/core"
        "${EMU_DIR}/common")
    target_compile_options(lichomat-emu-dev PRIVATE ${EMU_APP_FLAGS})
    target_link_libraries(lichomat-emu-dev PRIVATE ${EMU_APP_FRAMEWORKS})
    set(EMU_APP_TEST ${Python3_EXECUTABLE} "${EMU_DIR}/tests/app_test.py" --dev $<TARGET_FILE:lichomat-emu-dev>
        --render $<TARGET_FILE:lichomat-render> --root "${LXR_ROOT}" --work "${CMAKE_BINARY_DIR}/app-test")
    function(emu_app_tests)
        foreach(id IN LISTS ARGN)
            add_test(NAME ${id} COMMAND ${EMU_APP_TEST} ${id})
            set_tests_properties(${id} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
        endforeach()
    endfunction()
    emu_app_tests(R4_dev_headless)
endif()
```

Later tasks append to `EMU_APP_MAC_SOURCES` and, with `list(APPEND EMU_APP_FRAMEWORKS "-framework X")`, one quoted item per framework, to `EMU_APP_FRAMEWORKS`, both before `add_library`. M21 moves: its `add_test` (the gate over `test_boot`, `test_hook` and `lichomat-render`) goes after this `if` block, still before the temporary-folder block, with its binaries in a list `EMU_GATE_BINARIES`, to which the block appends `$<TARGET_FILE:lichomat-emu-dev>`.

- [ ] **Step 7: The driver** `emu/tests/app_test.py`, with its first case:

```python
#!/usr/bin/env python3
"""The app's tests that run lichomat-emu-dev, the Dear ImGui pin and the bundle
(stage 1b plan R4). Each case is a ctest id:

    app_test.py --dev DEV --render RENDER --root ROOT --work WORK [--app APP] ID
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys


def check(ok, msg):
    if not ok:
        print('FAIL: ' + msg)
        sys.exit(1)


class Case:
    def __init__(self, a):
        self.a = a
        self.work = os.path.join(a.work, a.id)
        shutil.rmtree(self.work, ignore_errors=True)
        os.makedirs(self.work)
        self.home = os.path.join(self.work, 'home')

    def card(self, name='init'):
        dst = os.path.join(self.work, 'card-' + name)
        shutil.copytree(os.path.join(self.a.root, 'emu', 'tests', 'cards', name), dst)
        return dst

    def script(self, lines):
        path = os.path.join(self.work, 'script.txt')
        with open(path, 'w') as f:
            f.write('\n'.join(lines) + '\n')
        return path

    def dev(self, args, timeout=60):
        env = dict(os.environ, LICHOMAT_EMU_HOME=self.home)
        return subprocess.run([self.a.dev] + args, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                              timeout=timeout, env=env)


def lcd_lines(out):
    """(wall seconds, row 0, row 1) of every lcd line"""
    return [(float(m.group(1)), m.group(2), m.group(3))
            for m in re.finditer(r'^lcd (\S+) \|(.{16})\|(.{16})\|$', out, re.M)]


def splash(line):
    return 'LICHOMAT' in line[1] and 'v0.1 unofficial' in line[2]


def R4_dev_headless(t):
    r = t.dev(['--card', t.card(), '--no-audio', '--no-midi', '--seconds', '5', '--print-diag',
               '--script', t.script(['2.0 press 33'])])
    check(r.returncode == 0, 'exit %d: %s' % (r.returncode, r.stderr))
    lcd = lcd_lines(r.stdout)
    check(lcd and lcd[0][0] < 0.3 and splash(lcd[0]), 'the splash is not first: %r' % lcd[:1])
    after = [l for l in lcd if not splash(l)]
    check(after and after[0][0] >= 0.95, 'the splash did not show for 1 s of wall time (spec 10.2): %r' % after[:1])
    # Wall pacing (spec 4.10): virtual time never runs more than one slice and one snapshot (13 ms) ahead
    # of the wall clock, and falls behind only where a slow VM makes it re-anchor, by at most 100 ms each
    # time; 250 ms allows two. The tight 689.7 +- 0.1 blocks per second stays the owner's A11.
    m = re.search(r'^diag .* vt=(\S+) wall=(\S+)$', r.stdout, re.M)
    check(m, 'no diag line: ' + r.stdout)
    drift = float(m.group(2)) - float(m.group(1))
    check(-0.013 <= drift <= 0.25, 'virtual time is %.3f s behind the wall clock' % drift)
    check('warning:' not in r.stdout and 'hang:' not in r.stdout, r.stdout)


CASES = {f.__name__: f for f in [R4_dev_headless]}


def main():
    p = argparse.ArgumentParser()
    for o in ('--dev', '--render', '--root', '--work', '--app'):
        p.add_argument(o)
    p.add_argument('id')
    a = p.parse_args()
    check(a.id in CASES, 'no case ' + a.id)
    CASES[a.id](Case(a))
    print('PASS ' + a.id)


if __name__ == '__main__':
    main()
```

Later tasks add their cases to `CASES`.

- [ ] **Step 8:** Run EMU-CHECK. Expected: N = 194, 0 warnings; `M21` passes with the harness. Then run it by hand: `build/emu/lichomat-emu-dev --card emu/tests/cards/init --no-audio --no-midi --seconds 4 --print-diag`; the splash, the main page after about 1 s, `diag blockRate=689.7…`. Then the gate.

- [ ] **Step 9: Commit** the seven new files and `emu/CMakeLists.txt`; message `Emulator app: Mach host, threads, watchdog, shell and lichomat-emu-dev (E5.5 R4)`.

---
### Task 7: CoreAudio output (§6.2, §6.3)

**Files:**
- Create: `emu/app/mac/app_coreaudio.{h,c}`, `emu/tests/test_app_devices.c`
- Modify: `emu/app/mac/app_shell.{h,c}`, `emu/app/dev/dev_main.c`, `emu/CMakeLists.txt` (`app_coreaudio.c` into `EMU_APP_MAC_SOURCES`; `list(APPEND EMU_APP_FRAMEWORKS "-framework AudioToolbox" "-framework CoreAudio")`; `emu_add_app_test(test_app_devices ENGINE MAC FRAMEWORKS AudioToolbox CoreAudio CoreMIDI CoreFoundation IDS R4_dev_coreaudio)` inside `if(LICHOMAT_EMU_APP)`)

- [ ] **Step 1: The test** `emu/tests/test_app_devices.c` (Task 8 adds `R4_dev_coremidi`):

```c
#include <mach/mach_time.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "app_coreaudio.h"
#include "emu_test.h"

/* fn in a forked child for at most 5 s: its exit code, or 77 without an answer (E22). The parent
 * never touches CoreAudio or CoreMIDI, so the child starts clean. */
static int probe(int (*fn)(void), const char *id)
{
	fflush(stdout);
	const pid_t pid = fork();
	if (pid == 0)
		_exit(fn());
	for (int i = 0; i < 100; i++)
	{
		int status;
		if (waitpid(pid, &status, WNOHANG) == pid)
			return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
		const struct timespec d = { 0, 50000000L };
		nanosleep(&d, NULL);
	}
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	printf("SKIP %s: no answer within 5 s\n", id);
	return 77;
}

#define PROBE(fn, id) do { const int r_ = probe(fn, id); if (r_ == 77) exit(77); ASSERT_EQ(r_, 0); } while (0)

static int coreaudioChild(void)
{
	static app_engine_t e;	/* its audio rings only */
	app_host_t h;
	memset(&h, 0, sizeof h);
	h.ticksPerSec = 24e6;
	if (app_engineInit(&e, &h) != 0)
		return 1;
	app_ca_t ca;
	memset(&ca, 0, sizeof ca);
	ca.engine = &e;
	const app_outputRequest_t req = { "", 0u, 2u };	/* the default output at its own buffer */
	app_outputInfo_t info;
	char err[256];
	if (app_caOpen(&ca, &e.audio, &req, &info, err, sizeof err) != 0)
	{
		printf("SKIP R4_dev_coreaudio: %s\n", err);
		return 77;
	}
	const struct timespec d = { 1, 0 };
	nanosleep(&d, NULL);
	app_caClose(&ca);
	const double want = info.bufferFrames * 44140.625 / info.deviceRate;
	const unsigned got = atomic_load(&e.audio.maxRequest);
	printf("%s: %.0f Hz, %u frames, client %.6f Hz, %s; %llu callbacks, largest request %u (expected %.1f)\n",
		info.name, info.deviceRate, info.bufferFrames, info.clientRate, info.format,
		(unsigned long long)atomic_load(&e.audio.callbacks), got, want);
	return info.clientRate == 44140.625 && atomic_load(&e.audio.callbacks) >= 5u && fabs(got - want) <= 2.0 ? 0 : 1;
}

TEST(R4_dev_coreaudio) { PROBE(coreaudioChild, "R4_dev_coreaudio"); }

TEST_MAIN()
```

- [ ] **Step 2:** Run EMU-CHECK. Expected: build fails, `app_coreaudio.h` not found.

- [ ] **Step 3: The header** `emu/app/mac/app_coreaudio.h`:

```c
/*
 * app_coreaudio.h
 *
 * The output of LICHOMAT Emu (stage 1b spec 6.2): AUHAL at 44,140.625 Hz, float32,
 * Normal converter complexity at maximum quality; listeners on one serial queue
 * push into the engine's notification ring. Open and close run on the firmware thread.
 */
#ifndef APP_COREAUDIO_H_
#define APP_COREAUDIO_H_

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>

#include "app_engine.h"

typedef struct app_ca
{
	AudioUnit unit;
	AudioObjectID device;
	int followDefault;			/**< no UID asked: follow the system's default output */
	unsigned channels;
	app_audio_t *audio;
	app_engine_t *engine;		/**< for app_engineNote() */
	dispatch_queue_t queue;		/**< the listeners' serial queue: one producer (spec 6.1) */
	AudioObjectPropertyListenerBlock listener;	/**< Block_copy()'d: Add and Remove need the same pointer */
} app_ca_t;

int app_caList(app_outputDevice_t *out, int max);	/**< the output devices (app_types.h) */
int app_caOpen(app_ca_t *ca, app_audio_t *a, const app_outputRequest_t *req, app_outputInfo_t *info, char *err, size_t n);
void app_caClose(app_ca_t *ca);

#endif /* APP_COREAUDIO_H_ */
```

- [ ] **Step 4: `app_coreaudio.c`.** `app_caOpen()` does, in this order, and on any failure closes what it opened, writes "the call: the OSStatus" into `err` and returns −1:
  1. the device: `req->uid` empty → `kAudioHardwarePropertyDefaultOutputDevice` on `kAudioObjectSystemObject` (scope global, element main) and `followDefault = 1`; else `kAudioHardwarePropertyTranslateUIDToDevice` with the UID as a `CFStringRef` qualifier, falling back to the default if it is not found. `kAudioObjectUnknown` → `err = "no output device"` (CI, E22).
  2. the buffer: if `req->bufferFrames`, clamp it to `kAudioDevicePropertyBufferFrameSizeRange` (an `AudioValueRange`) and set `kAudioDevicePropertyBufferFrameSize` (scope output) with `AudioObjectSetPropertyData()`.
  3. the unit: `AudioComponentFindNext()` for `{ kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple }`, `AudioComponentInstanceNew()`, `kAudioOutputUnitProperty_CurrentDevice` (scope global).
  4. the client format, `kAudioUnitProperty_StreamFormat`, scope input, element 0: `mSampleRate = 44140.625`, `kAudioFormatLinearPCM`, `kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked`, 32 bits, interleaved, `channels = req->channels`, but 2 if the device's `kAudioDevicePropertyStreamConfiguration` (scope output) has fewer than 4 channels; with 4, `kAudioOutputUnitProperty_ChannelMap` (scope output, element 0) maps client channels 0–3 to device channels 0–3 and the rest to −1.
  5. `kAudioUnitProperty_SampleRateConverterComplexity` = `kAudioUnitSampleRateConverterComplexity_Normal` and `kAudioUnitProperty_RenderQuality` = `kRenderQuality_Max` (both scope global, spec 6.2).
  6. `kAudioUnitProperty_SetRenderCallback` (scope input, element 0) with `{ app_caRender, ca }`; `AudioUnitInitialize()`; `AudioOutputUnitStart()`.
  7. the info: the client rate read back from step 4's property; `kAudioDevicePropertyNominalSampleRate`; `kAudioDevicePropertyLatency` and `kAudioDevicePropertySafetyOffset` (scope output); the first `AudioStreamID` of `kAudioDevicePropertyStreams` (scope output) and its `kAudioStreamPropertyLatency`; `kAudioDevicePropertyBufferFrameSize`; the name from `kAudioObjectPropertyName` with `CFStringGetCString()`; `format` = `client float32 N ch 44140.625 Hz; device R Hz; converter Normal, quality Max`.
  8. the listeners: `dispatch_queue_create("lichomat-emu.audio", DISPATCH_QUEUE_SERIAL)` and one block, `ca->listener = Block_copy(^(UInt32 n, const AudioObjectPropertyAddress *addr) { … })` (`<Block.h>`: in C without ARC a block literal lives on the stack, and Remove would never match it), registered with `AudioObjectAddPropertyListenerBlock(obj, &addr, queue, ca->listener)` on the device for `kAudioDevicePropertyDeviceIsAlive`, `kAudioDeviceProcessorOverload`, `kAudioDevicePropertyNominalSampleRate`, `kAudioDevicePropertyBufferFrameSize` and `kAudioDevicePropertyStreamConfiguration`, and, with `followDefault`, on the system object for `kAudioHardwarePropertyDefaultOutputDevice`. The block pushes `APP_NOTE_OVERLOAD` for the overload address and `APP_NOTE_DEVICE` for every other, then calls `audio->wake()`. `app_caClose()`: `AudioOutputUnitStop()`, `AudioObjectRemovePropertyListenerBlock(obj, &addr, queue, ca->listener)` for each address, `AudioUnitUninitialize()`, `AudioComponentInstanceDispose()`, `Block_release(ca->listener)`, `dispatch_release()`.

  The render callback, complete:

```c
static OSStatus app_caRender(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
	UInt32 frames, AudioBufferList *io)
{
	(void)flags;
	(void)bus;
	const uint64_t entry = mach_absolute_time();
	app_ca_t *ca = ref;
	const app_audioTime_t t = {
		(ts->mFlags & kAudioTimeStampHostTimeValid) ? ts->mHostTime : entry,
		entry,
		(ts->mFlags & kAudioTimeStampRateScalarValid) ? ts->mRateScalar : 1.0,
	};
	app_audioRender(ca->audio, &t, frames, (float *)io->mBuffers[0].mData, ca->channels);
	return noErr;
}
```

  `app_caList()` walks `kAudioHardwarePropertyDevices` and keeps devices whose `kAudioDevicePropertyStreamConfiguration` (scope output) has a channel, with `kAudioDevicePropertyDeviceUID` and the name.

- [ ] **Step 5: The shell.** `app_shellOptions_t` gains `const char *deviceUid` and `uint32_t bufferFrames`; `struct app_shell` gains `app_ca_t ca`; `app_shell.h` gains `int app_shellDevices(app_shell_t *s, app_outputDevice_t *out, int max)` (`app_caList()`, for the GUI and `devices`). Unless `noAudio`, the host's `outputOpen` becomes a function that calls `app_caOpen(&s->ca, …)` and then, since it runs on the firmware thread, `mach.period = info.bufferFrames / info.deviceRate` and `mach.realtime = app_threadRealtime(mach.period) == 0` (spec 6.1); `outputClose` calls `app_caClose()`. An open failure goes to `o.message` once per error text. The harness gains `--device UID`, `--buffer N`, `devices` (one line per device: id, UID, channels, name) and `output UID|default N` (`APP_CMD_OUTPUT`).

- [ ] **Step 6:** Run EMU-CHECK. Expected: N = 195, 0 warnings; `R4_dev_coreaudio` passes on this Mac, or skips with its reason in the sandbox. Record the printed line in the commit message. Then run `build/emu/lichomat-emu-dev --card emu/tests/cards/init --no-midi --seconds 8 --script <(echo "2 press 33") --print-diag` and listen: the pattern plays; the `diag` line shows `blockRate=689.7…`, `L` and `D` near 57 and 58 ms on the built-in output at 512 frames, `outUnderruns=0`. Then the gate.

- [ ] **Step 7: Commit** the new files and the modified ones; message `Emulator app: CoreAudio output, device listeners, real-time firmware thread (E5.5 R4)`.

---

### Task 8: CoreMIDI, the MIDI-out thread and DIN bindings (§6.4, §6.5, §6.9)

**Files:**
- Create: `emu/app/engine/app_bind.{h,c}`, `emu/app/mac/app_coremidi.{h,c}`, `emu/tests/test_app_bind.c`
- Modify: `emu/tests/test_app_devices.c`, `emu/app/mac/app_shell.{h,c}`, `emu/app/dev/dev_main.c`, `emu/CMakeLists.txt` (`app_bind.c` into the units, `app_coremidi.c` into the mac sources, `list(APPEND EMU_APP_FRAMEWORKS "-framework CoreMIDI")`; `emu_add_app_test(test_app_bind IDS R4_bind)`; `R4_dev_coremidi` added to `test_app_devices`)

- [ ] **Step 1: The tests.** `emu/tests/test_app_bind.c`:

```c
#include "app_bind.h"
#include "seqc_test.h"

TEST(R4_bind)
{
	const app_endpoint_t l[3] = { { 11, "Port A" }, { 22, "Port B" }, { 33, "Port A" } };
	ASSERT_EQ(app_bindPick(l, 3, 22, "Port A"), 1);	/* the stored unique ID first (spec 6.9) */
	ASSERT_EQ(app_bindPick(l, 3, 99, "Port A"), 0);	/* else the stored name */
	ASSERT_EQ(app_bindPick(l, 3, 99, "Gone"), -1);	/* 'not connected' */
	ASSERT_EQ(app_bindPick(l, 3, 0, ""), -1);
}

TEST_MAIN()
```

  In `test_app_devices.c`, before `TEST_MAIN()`, with `#include "app_coremidi.h"`:

```c
/* spec 6.4, 6.9: stored unique IDs; packets stamped +10, +20, +30 ms reach the read proc at their stamps */
static int coremidiChild(void)
{
	static app_engine_t e;
	app_host_t h;
	memset(&h, 0, sizeof h);
	h.ticksPerSec = 24e6;
	if (app_engineInit(&e, &h) != 0)
		return 1;
	app_cm_t m;
	char err[256];
	const int32_t uid = (int32_t)(0x4C450000u | ((uint32_t)getpid() & 0xFFFFu));
	if (app_cmInit(&m, &e, uid, uid + 1, err, sizeof err) != 0)
	{
		printf("SKIP R4_dev_coremidi: %s\n", err);
		return 77;
	}
	int ok = m.usbSrcUid == uid && m.usbDstUid == uid + 1;
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	const uint64_t step = (uint64_t)(0.010 * 1e9 * tb.denom / tb.numer), t0 = mach_absolute_time();
	MIDIPortRef out;
	ok &= MIDIOutputPortCreate(m.client, CFSTR("probe"), &out) == noErr;
	uint32_t buf[64];
	MIDIEventList *l = (MIDIEventList *)buf;
	MIDIEventPacket *p = MIDIEventListInit(l, kMIDIProtocol_1_0);
	const UInt32 clock = 0x10F80000u;
	for (unsigned k = 1; k <= 3u; k++)
		p = MIDIEventListAdd(l, sizeof buf, p, t0 + k * step, 1u, &clock);
	ok &= MIDISendEventList(out, m.usbDst, l) == noErr;
	unsigned got = 0;
	while (got < 3u && mach_absolute_time() < t0 + 100u * step)
	{
		const app_midiEntry_t *x = app_ringPeek(&e.midiIn[APP_PORT_USB], 0u);
		if (!x)
			continue;
		const uint64_t now = mach_absolute_time(), want = t0 + (got + 1u) * step;
		printf("packet %u: stamp %+.3f ms, arrived %+.3f ms after it\n", got + 1u,
			(double)(x->stamp - t0) / step * 10.0, ((double)now - (double)want) / step * 10.0);
		ok &= x->stamp == want && now + step / 10u >= want;	/* not before its stamp - 1 ms */
		app_ringPop(&e.midiIn[APP_PORT_USB], 1u);
		got++;
	}
	app_cmDispose(&m);
	return ok && got == 3u ? 0 : 1;
}

TEST(R4_dev_coremidi) { PROBE(coremidiChild, "R4_dev_coremidi"); }
```

- [ ] **Step 2:** Run EMU-CHECK. Expected: build fails, `app_bind.h` not found.

- [ ] **Step 3: `app_bind.h` and `app_bind.c`** (E28):

```c
/* app_bind.h: plain C, safe for the C++ GUI */
#ifndef APP_BIND_H_
#define APP_BIND_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct app_endpoint { int32_t uid; char name[128]; } app_endpoint_t;
/** the endpoint to bind: the stored unique ID, else the first with the stored name; -1 none */
int app_bindPick(const app_endpoint_t *list, int n, int32_t uid, const char *name);
#ifdef __cplusplus
}
#endif
#endif

/* app_bind.c */
#include "app_bind.h"
#include <string.h>

int app_bindPick(const app_endpoint_t *list, int n, int32_t uid, const char *name)
{
	for (int i = 0; uid != 0 && i < n; i++)
		if (list[i].uid == uid)
			return i;
	for (int i = 0; name && name[0] && i < n; i++)
		if (!strcmp(list[i].name, name))
			return i;
	return -1;
}
```

- [ ] **Step 4: `app_coremidi.h`**:

```c
/*
 * app_coremidi.h
 *
 * The MIDI ports of LICHOMAT Emu (stage 1b spec 6.4, 6.5, 6.9): a virtual source
 * and destination 'LICHOMAT Emu' (the USB side), and an input and an output
 * port for the DIN endpoints the user binds. MIDI 1.0 UMP API of macOS 11.
 */
#ifndef APP_COREMIDI_H_
#define APP_COREMIDI_H_

#include <CoreMIDI/CoreMIDI.h>

#include "app_bind.h"
#include "app_engine.h"

typedef struct app_cm
{
	MIDIClientRef client;
	MIDIEndpointRef usbSrc, usbDst;
	MIDIPortRef dinIn, dinOut;
	MIDIEndpointRef dinSrc, dinDst;	/**< bound; 0 'not connected' */
	int32_t usbSrcUid, usbDstUid;	/**< the IDs they have now, to store */
	app_engine_t *engine;
} app_cm_t;

int app_cmInit(app_cm_t *m, app_engine_t *e, int32_t srcUid, int32_t dstUid, char *err, size_t n);
int app_cmList(int sources, app_endpoint_t *out, int max);	/**< the system's endpoints, ours left out */
int app_cmBind(app_cm_t *m, int source, int32_t uid, const char *name);	/**< 1 bound, 0 not connected */
void app_cmSend(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n);	/**< MIDI-out sink */
void app_cmDispose(app_cm_t *m);

#endif /* APP_COREMIDI_H_ */
```

- [ ] **Step 5: `app_coremidi.c`.** `app_cmInit()`: `MIDIClientCreateWithBlock(CFSTR("LICHOMAT Emu"), &client, NULL)` (no notifications, E28; an error or `kMIDIServerStartErr` → "CoreMIDI: OSStatus" into `err`, −1); `MIDISourceCreateWithProtocol(client, CFSTR("LICHOMAT Emu"), kMIDIProtocol_1_0, &usbSrc)`; `MIDIDestinationCreateWithProtocol(client, CFSTR("LICHOMAT Emu"), kMIDIProtocol_1_0, &usbDst, usbBlock)`; `MIDIObjectSetIntegerProperty(usbDst, kMIDIPropertyAdvanceScheduleTimeMuSec, 0)` (spec 6.4); for each endpoint with a stored ID ≠ 0, `MIDIObjectSetIntegerProperty(ep, kMIDIPropertyUniqueID, id)`, whose `kMIDIIDNotUnique` keeps the new ID (spec 6.9); then `MIDIObjectGetIntegerProperty(ep, kMIDIPropertyUniqueID, …)` into `usbSrcUid`, `usbDstUid`; `MIDIInputPortCreateWithProtocol(client, CFSTR("DIN in"), kMIDIProtocol_1_0, &dinIn, dinBlock)`; `MIDIOutputPortCreate(client, CFSTR("DIN out"), &dinOut)`. The two read blocks, complete:

```c
	const MIDIReceiveBlock usbBlock = ^(const MIDIEventList *list, void *ref) {
		(void)ref;
		app_midiFromEventList(list, APP_PORT_USB, mach_absolute_time(), app_engineMidiPush, e);
	};
	const MIDIReceiveBlock dinBlock = ^(const MIDIEventList *list, void *ref) {
		(void)ref;
		app_midiFromEventList(list, APP_PORT_DIN, mach_absolute_time(), app_engineMidiPush, e);
	};
```

  `app_cmList()`: `MIDIGetNumberOfSources()`/`MIDIGetSource()` or `MIDIGetNumberOfDestinations()`/`MIDIGetDestination()`, with `kMIDIPropertyUniqueID` and `kMIDIPropertyDisplayName`, leaving out `usbSrc` and `usbDst` by unique ID. `app_cmBind()`: `app_bindPick()` over that list; for a source, `MIDIPortDisconnectSource(dinIn, old)` and `MIDIPortConnectSource(dinIn, new, NULL)` when it changes; for a destination, it sets `dinDst`. `app_cmSend()`, complete:

```c
void app_cmSend(void *ctx, uint8_t port, uint64_t when, const uint32_t *w, unsigned n)
{
	app_cm_t *m = ctx;
	uint32_t buf[64];
	MIDIEventList *list = (MIDIEventList *)buf;
	MIDIEventPacket *p = MIDIEventListInit(list, kMIDIProtocol_1_0);
	if (!MIDIEventListAdd(list, sizeof buf, p, when, n, w))
		return;
	if (port == APP_PORT_USB)
		MIDIReceivedEventList(m->usbSrc, list);	/* at its presentation time, with it as the stamp (spec 6.5) */
	else if (m->dinDst)
		MIDISendEventList(m->dinOut, m->dinDst, list);	/* early, stamped: CoreMIDI schedules it (E11) */
}
```

  `app_cmDispose()`: `MIDIEndpointDispose()` for both virtual endpoints, `MIDIPortDispose()` for both ports, `MIDIClientDispose()`.

- [ ] **Step 6: The MIDI-out thread** in `app_shell.c` (`struct app_shell` gains `app_cm_t cm` and `pthread_t midiOut`; it starts unless `noMidi`), complete:

```c
static void *app_shellMidiOut(void *arg)
{
	app_shell_t *s = arg;
	(void)app_threadRealtime(s->mach.period);	/* spec 6.1: its wake-up precision is MIDI out's jitter */
	const app_midiOutSink_t sink = { &s->cm, app_cmSend };
	while (!atomic_load(&s->stopped))
	{
		const uint64_t now = mach_absolute_time();
		const uint64_t next = app_midiOutStep(&s->engine, now, &sink);
		const uint64_t limit = now + (uint64_t)(0.02 * s->mach.ticksPerSec);
		if (next == 0u)
			app_machWaitSem(s->mach.midiWake, limit, s->mach.ticksPerSec);
		else
			mach_wait_until(next < limit ? next : limit);
	}
	return NULL;
}
```

  `app_shellOptions_t` gains `int32_t usbSrcUid, usbDstUid, dinSrcUid, dinDstUid` and `const char *dinSrcName, *dinDstName`; `app_shellFrame()` calls `app_cmBind()` for both once per second (E28) and sends `notice: DIN source NAME not connected` once per change; `app_shell.h` gains `int app_shellEndpoints(app_shell_t *s, int sources, app_endpoint_t *out, int max)` and `void app_shellBind(app_shell_t *s, int source, int32_t uid, const char *name)` (stored, then bound) for the GUI and the harness. The harness gains `midi` (lists both kinds with index, unique ID and name), `din-src N|off` and `din-dst N|off`.

- [ ] **Step 7:** Run EMU-CHECK. Expected: N = 197, 0 warnings; `R4_dev_coremidi` passes or skips with its reason; record its packet lines. Then by hand: `build/emu/lichomat-emu-dev --card emu/tests/cards/init --seconds 20`; a DAW (or `MIDI Monitor`) sees the source and destination 'LICHOMAT Emu'; `press 33` sends F8 at 120 BPM; `delay auto` and a DAW clock in MID sync (`--card` with `GLO.CFG` byte 0 = 1) follow it. Then the gate.

- [ ] **Step 8: Commit**; message `Emulator app: CoreMIDI ports, the MIDI-out thread and DIN bindings (E5.5 R4)`.

---

### Task 9: The card folder — first run, import and export (§6.7, §4.8, §11)

**Files:**
- Create: `emu/app/engine/app_time.{h,c}`, `emu/app/engine/app_card.{h,c}`, `emu/tests/test_app_card.c`
- Modify: `emu/app/mac/app_shell.{h,c}`, `emu/app/dev/dev_main.c`, `emu/tests/app_test.py`, `emu/CMakeLists.txt` (both files into the units; `emu_add_app_test(test_app_card IDS R4_card_order R4_card_stamp R4_card_state R4_card_import R4_card_export R4_card_project R4_card_seed)` with `target_compile_definitions(test_app_card PRIVATE "EMU_TEST_RENDER=\"$<TARGET_FILE:lichomat-render>\"")` and `add_dependencies(test_app_card lichomat-render)`; `R4_dev_import` into `emu_app_tests`)

- [ ] **Step 1: The test** `emu/tests/test_app_card.c`:

```c
#include <sys/stat.h>

#include "app_card.h"
#include "app_time.h"
#include "emu_test.h"

static void put(const char *dir, const char *name, const char *text) { emu_testWrite(dir, name, text, strlen(text)); }
static int exists(const char *dir, const char *rel)
{
	char p[2200];
	snprintf(p, sizeof p, "%s/%s", dir, rel);
	return emu_hostKind(p) >= 0;
}
static int holds(const char *dir, const char *rel, const char *text)
{
	char p[2200], b[64] = "";
	snprintf(p, sizeof p, "%s/%s", dir, rel);
	FILE *f = fopen(p, "rb");
	const size_t n = f ? fread(b, 1, sizeof b - 1u, f) : 0u;
	if (f)
		fclose(f);
	b[n] = '\0';
	return !strcmp(b, text);
}
static void countWarn(void *ctx, const char *what) { (void)what; (*(int *)ctx)++; }
static void home(app_paths_t *p)
{
	static char dir[512];
	emu_testTempDir(dir, sizeof dir);
	setenv("LICHOMAT_EMU_HOME", dir, 1);	/* E21 */
	if (app_pathsInit(p) != 0)
		exit(2);
}

#define STAMP 1793451909LL	/* 2026-10-31 13:05:09, local seconds */

TEST(R4_card_order)
{
	app_cardEntry_t l[3] = {
		{ .host = "B.SND", .name = "B.SND", .birthSec = 10, .seq = 0 },
		{ .host = "A.SND", .name = "A.SND", .birthSec = 10, .seq = 1 },
		{ .host = "C.SND", .name = "C.SND", .birthSec = 5, .seq = 2 },
	};
	app_cardOrder(l, 3, 0);	/* the backend's order: birth time, then name (spec 4.8) */
	ASSERT_EQ(strcmp(l[0].name, "C.SND"), 0);
	ASSERT_EQ(strcmp(l[1].name, "A.SND"), 0);
	app_cardOrder(l, 3, 1);	/* a FAT volume: readdir() order, the card's own (spec 6.7) */
	ASSERT_EQ(strcmp(l[0].name, "B.SND"), 0);
	ASSERT_EQ(strcmp(l[2].name, "C.SND"), 0);
}

TEST(R4_card_stamp)
{
	char s[64], dir[512], out[1024];
	ASSERT_EQ(app_stampName(0, "", "", s, sizeof s), 15);
	ASSERT_EQ(strcmp(s, "19700101-000000"), 0);
	app_stampName(11016LL * 86400, "", "", s, sizeof s);
	ASSERT_EQ(strcmp(s, "20000229-000000"), 0);
	app_stampName(STAMP, "crash-", ".txt", s, sizeof s);
	ASSERT_EQ(strcmp(s, "crash-20261031-130509.txt"), 0);
	ASSERT_EQ(app_stampName(0, "", "", s, 10u), 0);	/* does not fit */
	emu_testTempDir(dir, sizeof dir);
	ASSERT_EQ(app_backupName(dir, STAMP, NULL, out, sizeof out), 0);
	ASSERT_TRUE(strstr(out, "/20261031-130509") != NULL);
	mkdir(out, 0755);
	ASSERT_EQ(app_backupName(dir, STAMP, NULL, out, sizeof out), 0);	/* E17: a second one in that second */
	ASSERT_TRUE(strstr(out, "/20261031-130509-2") != NULL);
	ASSERT_EQ(app_backupName(dir, STAMP, "-PROJ03", out, sizeof out), 0);
	ASSERT_TRUE(strstr(out, "/20261031-130509-PROJ03") != NULL);
}

TEST(R4_card_state)
{
	char dir[512], sub[1024], names[256], x[1100];
	emu_testTempDir(dir, sizeof dir);
	snprintf(sub, sizeof sub, "%s/new", dir);
	ASSERT_EQ(app_cardState(sub), 1);	/* missing: seed it */
	mkdir(sub, 0755);
	snprintf(x, sizeof x, "%s/.fseventsd", sub);
	mkdir(x, 0755);
	put(sub, ".DS_Store", "x");
	ASSERT_EQ(app_cardState(sub), 1);	/* a fresh card that macOS mounted */
	ASSERT_EQ(app_cardNonDot(sub, names, sizeof names), 0);
	put(sub, "X.TXT", "x");
	ASSERT_EQ(app_cardState(sub), 0);
	ASSERT_EQ(app_cardNonDot(sub, names, sizeof names), 1);
	ASSERT_EQ(strcmp(names, "X.TXT"), 0);
	snprintf(x, sizeof x, "%s/X.TXT", sub);
	ASSERT_EQ(app_cardState(x), -1);
	char card[512], proj[1024];
	int p = -1;
	emu_testTempDir(card, sizeof card);
	const uint8_t two = 2u, bad = 200u;
	emu_testWrite(card, "PROJ.IDX", &two, 1u);
	snprintf(proj, sizeof proj, "%s/proj02", card);
	mkdir(proj, 0755);
	ASSERT_EQ(app_cardCheck(card, &p), 0);	/* no PRJ.NFO: a notice (spec 11) */
	ASSERT_EQ(p, 2);
	put(proj, "PRJ.NFO", "TESTPROJ");
	ASSERT_EQ(app_cardCheck(card, &p), 1);
	emu_testWrite(card, "PROJ.IDX", &bad, 1u);
	ASSERT_EQ(app_cardCheck(card, &p), 0);	/* 64-255 count as 0 (E4.7) */
	ASSERT_EQ(p, 0);
}

TEST(R4_card_import)
{
	app_paths_t p;
	home(&p);
	mkdir(p.card, 0755);
	put(p.card, "OLD.TXT", "old");
	char src[512], proj[1024], backup[1024], dst[1100];
	emu_testTempDir(src, sizeof src);
	snprintf(proj, sizeof proj, "%s/PROJ00", src);
	mkdir(proj, 0755);
	put(proj, "PRJ.NFO", "TESTPROJ");
	put(proj, "01-B.SND", "b");	/* born before 00-A.SND */
	put(proj, "00-A.SND", "a");
	put(src, ".DS_Store", "x");
	put(src, "Long Name.txt", "x");
	int warnings = 0;
	ASSERT_EQ(app_cardImport(&p, src, STAMP, backup, sizeof backup, countWarn, &warnings), 0);
	ASSERT_TRUE(strstr(backup, "/Card backups/20261031-130509") != NULL);	/* step 2 */
	ASSERT_TRUE(exists(backup, "OLD.TXT"));
	ASSERT_FALSE(exists(p.card, ".DS_Store"));
	ASSERT_EQ(warnings, 1);
	snprintf(dst, sizeof dst, "%s/PROJ00", p.card);
	emu_hostEntry_t *l;
	ASSERT_EQ(emu_hostList(dst, &l, NULL), 3);
	ASSERT_EQ(strcmp(l[1].name, "01-B.SND"), 0);	/* step 3: the source's order */
	free(l);
	ASSERT_EQ(app_cardImport(&p, p.card, STAMP, backup, sizeof backup, NULL, NULL), -1);	/* never its own folder */
}

TEST(R4_card_export)
{
	char card[512], dst[512], err[256], fse[1100];
	emu_testCard(card, sizeof card, 120u);
	emu_testTempDir(dst, sizeof dst);
	snprintf(fse, sizeof fse, "%s/.fseventsd", dst);
	mkdir(fse, 0755);
	ASSERT_EQ(app_cardExport(card, dst, err, sizeof err, NULL, NULL), 0);	/* only dot entries: accepted */
	ASSERT_TRUE(holds(dst, "PROJ00/PRJ.NFO", "TESTPROJ"));
	ASSERT_EQ(app_cardExport(card, dst, err, sizeof err, NULL, NULL), -2);	/* refused, with the names (spec 11) */
	ASSERT_TRUE(strstr(err, "PROJ00") != NULL);
}

TEST(R4_card_project)
{
	app_paths_t p;
	home(&p);
	char path[1200], target[512], err[256];
	mkdir(p.card, 0755);
	snprintf(path, sizeof path, "%s/PROJ01", p.card);
	mkdir(path, 0755);
	put(path, "PRJ.NFO", "NEWPROJ");
	emu_testTempDir(target, sizeof target);	/* the owner's card: PROJ00, proj01, PROJ.IDX */
	snprintf(path, sizeof path, "%s/PROJ00", target);
	mkdir(path, 0755);
	put(path, "PRJ.NFO", "OTHER");
	snprintf(path, sizeof path, "%s/proj01", target);
	mkdir(path, 0755);
	put(path, "PRJ.NFO", "OLDPROJ");
	put(target, "PROJ.IDX", "0");
	ASSERT_EQ(app_cardExportProject(&p, 1, target, 0, STAMP, err, sizeof err, NULL, NULL), 2);	/* the app asks */
	ASSERT_EQ(app_cardExportProject(&p, 1, target, 1, STAMP, err, sizeof err, NULL, NULL), 0);
	snprintf(path, sizeof path, "%s/20261031-130509-PROJ01", p.backups);
	ASSERT_TRUE(holds(path, "PRJ.NFO", "OLDPROJ"));	/* the replaced one, on the Mac */
	ASSERT_TRUE(holds(target, "PROJ01/PRJ.NFO", "NEWPROJ"));
	ASSERT_TRUE(holds(target, "PROJ00/PRJ.NFO", "OTHER"));	/* the other projects stay */
	ASSERT_TRUE(holds(target, "PROJ.IDX", "0"));
}

TEST(R4_card_seed)
{
	char dir[512], card[1100], err[256];
	emu_testTempDir(dir, sizeof dir);
	snprintf(card, sizeof card, "%s/Card", dir);
	ASSERT_EQ(app_cardSeed(EMU_TEST_RENDER, card, err, sizeof err), 0);	/* lichomat-render --seed-card (spec 6.7) */
	int p = -1;
	ASSERT_EQ(app_cardCheck(card, &p), 1);
	ASSERT_EQ(p, 0);
	ASSERT_TRUE(holds(card, "PROJ00/PRJ.NFO", "Init"));
}

TEST_MAIN()
```

- [ ] **Step 2:** Run EMU-CHECK. Expected: build fails, `app_card.h` not found.

- [ ] **Step 3: `app_time.h`, `app_time.c`** (E17, E20), complete:

```c
/* app_time.h: local time for backup and crash note names; app_stampName() and app_localSeconds() are async-signal-safe */
#ifndef APP_TIME_H_
#define APP_TIME_H_
#include <stddef.h>
#include <stdint.h>
void app_civil(int64_t days, int *y, unsigned *m, unsigned *d);	/**< day 0 is 1970-01-01 */
/** PREFIX YYYYMMDD-HHMMSS SUFFIX into out; its length, or 0 if it does not fit */
size_t app_stampName(int64_t localSec, const char *prefix, const char *suffix, char *out, size_t n);
void app_timeZoneRefresh(void);	/**< main thread: at start and every minute */
int64_t app_localSeconds(void);	/**< CLOCK_REALTIME plus the stored UTC offset */
#endif

/* app_time.c */
#include "app_time.h"
#include <stdatomic.h>
#include <time.h>

static _Atomic int64_t app_utcOffset;

void app_civil(int64_t days, int *y, unsigned *m, unsigned *d)
{
	/* the inverse of days-from-civil (H. Hinnant's algorithm), proleptic Gregorian */
	const int64_t z = days + 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = (unsigned)(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
	const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
	const unsigned mp = (5u * doy + 2u) / 153u;
	*d = doy - (153u * mp + 2u) / 5u + 1u;
	*m = mp < 10u ? mp + 3u : mp - 9u;
	*y = (int)((int64_t)yoe + era * 400 + (*m <= 2u));
}

size_t app_stampName(int64_t localSec, const char *prefix, const char *suffix, char *out, size_t n)
{
	int64_t days = localSec / 86400, s = localSec % 86400;
	if (s < 0)
	{
		s += 86400;
		days--;
	}
	int y;
	unsigned mo, d;
	app_civil(days, &y, &mo, &d);
	unsigned v[6] = { (unsigned)y, mo, d, (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60) };
	static const unsigned width[6] = { 4u, 2u, 2u, 2u, 2u, 2u };
	size_t k = 0;
	for (const char *p = prefix; *p; p++)
	{
		if (k + 1u >= n)
			return 0;
		out[k++] = *p;
	}
	for (unsigned i = 0; i < 6u; i++)
	{
		if (i == 3u && k + 1u < n)
			out[k++] = '-';
		if (k + width[i] >= n)
			return 0;
		for (unsigned j = width[i]; j-- > 0u; v[i] /= 10u)
			out[k + j] = (char)('0' + v[i] % 10u);
		k += width[i];
	}
	for (const char *p = suffix; *p; p++)
	{
		if (k + 1u >= n)
			return 0;
		out[k++] = *p;
	}
	out[k] = '\0';
	return k;
}

void app_timeZoneRefresh(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	const time_t t = ts.tv_sec;
	struct tm tm;
	if (localtime_r(&t, &tm))
		atomic_store(&app_utcOffset, (int64_t)tm.tm_gmtoff);
}

int64_t app_localSeconds(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec + atomic_load(&app_utcOffset);
}
```

- [ ] **Step 4: `app_card.h`**:

```c
/*
 * app_card.h
 *
 * The app's card folder (stage 1b spec 6.7): paths, the first run's seeding,
 * import, export card and export project, with copies that keep the order
 * (spec 4.8, plan E16) and backups named by local time (E17). Main thread,
 * only after 'card out applied'.
 */
#ifndef APP_CARD_H_
#define APP_CARD_H_

#include <stddef.h>
#include <stdint.h>

#include "emu_hostfs.h"

typedef struct app_paths
{
	char home[EMU_HOST_PATH], card[EMU_HOST_PATH], backups[EMU_HOST_PATH], settings[EMU_HOST_PATH];
} app_paths_t;

typedef void (*app_warn_t)(void *ctx, const char *what);

/** $LICHOMAT_EMU_HOME, else $HOME/Library/Application Support/LICHOMAT Emu (E21), with Card,
 * Card backups and settings.txt in it; makes home and backups; 0 or -1 */
int app_pathsInit(app_paths_t *p);
int app_cardState(const char *dir);		/**< 1 missing or only dot entries; 0 other entries; -1 not a folder */
int app_cardNonDot(const char *dir, char *names, size_t n);	/**< entries that are not dot entries, names joined by ", " */
int app_cardSeed(const char *render, const char *card, char *err, size_t n);	/**< RENDER --seed-card CARD; 0 */
int app_cardCheck(const char *card, int *project);	/**< PROJ.IDX's project has PRJ.NFO: 1; not: 0; -1 */

typedef struct app_cardEntry
{
	char host[256], name[13];
	int isDir;
	int64_t birthSec;
	long birthNsec;
	unsigned seq;	/**< readdir() order */
} app_cardEntry_t;

int app_cardIsFat(const char *path);	/**< statfs: msdos or exfat */
void app_cardOrder(app_cardEntry_t *list, int n, int fat);
int app_cardList(const char *dir, int fat, app_cardEntry_t **out, app_warn_t warn, void *ctx);
int app_cardCopy(const char *src, const char *dst, app_warn_t warn, void *ctx);	/**< into an existing folder */
int app_backupName(const char *backups, int64_t localSec, const char *suffix, char *out, size_t n);
int app_cardImport(const app_paths_t *p, const char *src, int64_t localSec, char *backup, size_t bn, app_warn_t warn, void *ctx);
int app_cardExport(const char *card, const char *dst, char *err, size_t n, app_warn_t warn, void *ctx);	/**< -2 not empty */
int app_cardExportProject(const app_paths_t *p, int project, const char *dst, int replace, int64_t localSec,
	char *err, size_t n, app_warn_t warn, void *ctx);	/**< 0; 2 dst has PROJnn and replace is 0; -1 */

#endif /* APP_CARD_H_ */
```

- [ ] **Step 5: `app_card.c`.** The order, the copy, the backups, import and export, complete:

```c
#include "app_card.h"

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app_time.h"

int app_cardIsFat(const char *path)
{
	struct statfs fs;
	return statfs(path, &fs) == 0 && (!strcmp(fs.f_fstypename, "msdos") || !strcmp(fs.f_fstypename, "exfat"));
}

static int app_byBirth(const void *pa, const void *pb)
{
	const app_cardEntry_t *a = pa, *b = pb;
	if (a->birthSec != b->birthSec)
		return a->birthSec < b->birthSec ? -1 : 1;
	if (a->birthNsec != b->birthNsec)
		return a->birthNsec < b->birthNsec ? -1 : 1;
	return strcmp(a->name, b->name);
}

static int app_bySeq(const void *pa, const void *pb)
{
	const app_cardEntry_t *a = pa, *b = pb;
	return (a->seq > b->seq) - (a->seq < b->seq);
}

void app_cardOrder(app_cardEntry_t *list, int n, int fat)
{
	if (n > 1)
		qsort(list, (size_t)n, sizeof *list, fat ? app_bySeq : app_byBirth);
}

int app_cardList(const char *dir, int fat, app_cardEntry_t **out, app_warn_t warn, void *ctx)
{
	*out = NULL;
	DIR *d = opendir(dir);
	if (!d)
		return -1;
	int n = 0, cap = 0;
	app_cardEntry_t *list = NULL;
	for (struct dirent *de; (de = readdir(d)) != NULL;)
	{
		if (de->d_name[0] == '.')
			continue;	/* ., .., .DS_Store, ._ files, .fseventsd, .Spotlight-V100 */
		app_cardEntry_t e;
		memset(&e, 0, sizeof e);
		char path[EMU_HOST_PATH];
		snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
		struct stat st;
		if (strlen(de->d_name) >= sizeof e.host || !emu_host83(de->d_name, e.name) || stat(path, &st) != 0)
		{
			if (warn)
			{
				char w[EMU_HOST_PATH + 64];
				snprintf(w, sizeof w, "card: %s is not an 8.3 name; the device would not see it", path);
				warn(ctx, w);
			}
			continue;
		}
		snprintf(e.host, sizeof e.host, "%s", de->d_name);
		e.isDir = S_ISDIR(st.st_mode);
		e.birthSec = (int64_t)st.st_birthtimespec.tv_sec;
		e.birthNsec = st.st_birthtimespec.tv_nsec;
		e.seq = (unsigned)n;
		if (n == cap)
		{
			cap = cap ? 2 * cap : 32;
			app_cardEntry_t *grown = realloc(list, (size_t)cap * sizeof *grown);
			if (!grown)
			{
				free(list);
				closedir(d);
				return -1;
			}
			list = grown;
		}
		list[n++] = e;
	}
	closedir(d);
	app_cardOrder(list, n, fat);
	*out = list;
	return n;
}

static int app_setBirth(const char *path, int64_t sec, long nsec)
{
	struct attrlist a;
	memset(&a, 0, sizeof a);
	a.bitmapcount = ATTR_BIT_MAP_COUNT;
	a.commonattr = ATTR_CMN_CRTIME;
	struct timespec ts = { (time_t)sec, nsec };
	return setattrlist(path, &a, &ts, sizeof ts, FSOPT_NOFOLLOW) == 0 ? 0 : -1;
}

static int app_copyFile(const char *src, const char *dst)
{
	const int in = open(src, O_RDONLY);
	const int out = in < 0 ? -1 : open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	int status = in < 0 || out < 0 ? -1 : 0;
	char buf[65536];
	ssize_t n = 0;
	while (status == 0 && (n = read(in, buf, sizeof buf)) > 0)
		if (write(out, buf, (size_t)n) != n)
			status = -1;
	if (n < 0)
		status = -1;
	if (in >= 0)
		close(in);
	if (out >= 0)
		close(out);
	return status;
}

typedef struct { int srcFat, dstFat; int64_t sec; long nsec; app_warn_t warn; void *ctx; } app_copy_t;

static int app_copyDir(app_copy_t *c, const char *src, const char *dst)
{
	app_cardEntry_t *list;
	const int n = app_cardList(src, c->srcFat, &list, c->warn, c->ctx);
	if (n < 0)
		return -1;
	int status = 0;
	for (int i = 0; i < n && status == 0; i++)
	{
		char from[EMU_HOST_PATH], to[EMU_HOST_PATH];
		snprintf(from, sizeof from, "%s/%s", src, list[i].host);
		snprintf(to, sizeof to, "%s/%s", dst, list[i].host);
		status = list[i].isDir ? mkdir(to, 0755) : app_copyFile(from, to);
		if (status == 0)
		{
			/* births 1 us apart in copy order; on FAT the write order is the order (E16) */
			if ((c->nsec += 1000) >= 1000000000L)
			{
				c->sec++;
				c->nsec -= 1000000000L;
			}
			if (app_setBirth(to, c->sec, c->nsec) != 0 && !c->dstFat)
				status = -1;
		}
		if (status == 0 && list[i].isDir)
			status = app_copyDir(c, from, to);
	}
	free(list);
	return status;
}

int app_cardCopy(const char *src, const char *dst, app_warn_t warn, void *ctx)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	app_copy_t c = { app_cardIsFat(src), app_cardIsFat(dst), (int64_t)now.tv_sec, now.tv_nsec, warn, ctx };
	return app_copyDir(&c, src, dst);
}

int app_backupName(const char *backups, int64_t localSec, const char *suffix, char *out, size_t n)
{
	char base[48];
	if (!app_stampName(localSec, "", suffix ? suffix : "", base, sizeof base))
		return -1;
	for (unsigned k = 1; k < 1000u; k++)
	{
		if (k == 1u)
			snprintf(out, n, "%s/%s", backups, base);
		else
			snprintf(out, n, "%s/%s-%u", backups, base, k);
		if (emu_hostKind(out) < 0)
			return 0;
	}
	return -1;
}

int app_cardImport(const app_paths_t *p, const char *src, int64_t localSec, char *backup, size_t bn, app_warn_t warn, void *ctx)
{
	char rs[PATH_MAX], rh[PATH_MAX];
	backup[0] = '\0';
	if (emu_hostKind(src) != 1 || !realpath(src, rs))
		return -1;
	const size_t hn = realpath(p->home, rh) ? strlen(rh) : 0u;
	if (hn && !strncmp(rs, rh, hn) && (rs[hn] == '/' || rs[hn] == '\0'))
		return -1;	/* never from the app's own folder */
	if (emu_hostKind(p->card) == 1
		&& (app_backupName(p->backups, localSec, NULL, backup, bn) != 0 || rename(p->card, backup) != 0))
		return -1;	/* step 2 */
	if (mkdir(p->card, 0755) != 0)
		return -1;
	return app_cardCopy(src, p->card, warn, ctx);	/* step 3; step 4, the power cycle, is the shell's */
}

int app_cardExport(const char *card, const char *dst, char *err, size_t n, app_warn_t warn, void *ctx)
{
	const int others = app_cardNonDot(dst, err, n);
	if (others != 0)
		return others < 0 ? -1 : -2;
	if (emu_hostKind(dst) < 0 && mkdir(dst, 0755) != 0)
		return -1;
	return app_cardCopy(card, dst, warn, ctx);
}

/* the folder of dir whose 8.3 name is name, as FatFs matches it */
static int app_cardFind(const char *dir, const char *name, char *out, size_t n)
{
	DIR *d = opendir(dir);
	int found = 0;
	char n83[13];
	for (struct dirent *de; d && !found && (de = readdir(d)) != NULL;)
		if (de->d_name[0] != '.' && emu_host83(de->d_name, n83) && !strcmp(n83, name))
		{
			snprintf(out, n, "%s/%s", dir, de->d_name);
			found = emu_hostKind(out) == 1;
		}
	if (d)
		closedir(d);
	return found;
}

static int app_rmEntry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st;
	(void)flag;
	(void)ftw;
	return remove(path);
}

static void app_countWarn(void *ctx, const char *what) { (void)what; (*(int *)ctx)++; }

int app_cardExportProject(const app_paths_t *p, int project, const char *dst, int replace, int64_t localSec,
	char *err, size_t n, app_warn_t warn, void *ctx)
{
	char name[13], sfx[16], from[EMU_HOST_PATH], to[EMU_HOST_PATH], backup[EMU_HOST_PATH];
	snprintf(name, sizeof name, "PROJ%02d", project);
	if (!app_cardFind(p->card, name, from, sizeof from))
	{
		snprintf(err, n, "the card has no %s", name);
		return -1;
	}
	if (app_cardFind(dst, name, to, sizeof to))
	{
		if (!replace)
		{
			snprintf(err, n, "%s already holds %s", dst, name);
			return 2;
		}
		int skipped = 0;	/* a name the copy would drop: nothing is replaced */
		snprintf(sfx, sizeof sfx, "-%s", name);
		if (app_backupName(p->backups, localSec, sfx, backup, sizeof backup) != 0 || mkdir(backup, 0755) != 0
			|| app_cardCopy(to, backup, app_countWarn, &skipped) != 0 || skipped
			|| nftw(to, app_rmEntry, 16, FTW_DEPTH | FTW_PHYS) != 0)
		{
			snprintf(err, n, "%s could not be moved into %s%s", to, backup, skipped ? ": it holds names that are not 8.3" : "");
			return -1;
		}
	}
	snprintf(to, sizeof to, "%s/%s", dst, name);
	if (mkdir(to, 0755) != 0 || app_cardCopy(from, to, warn, ctx) != 0)
	{
		snprintf(err, n, "%s could not be written", to);
		return -1;
	}
	return 0;
}
```

  The rest, exactly: `app_pathsInit()` builds the four paths (`Card`, `Card backups`, `settings.txt`), makes `home` and `backups` if missing, and returns −1 without `$HOME` or on a `mkdir()` failure. `app_cardState()` and `app_cardNonDot()` read the folder with `opendir()` and skip names starting with `.` (as `render_cardEmpty()`); a missing folder has no entries. `app_cardSeed()`: `posix_spawn(&pid, render, NULL, NULL, {"lichomat-render", "--seed-card", card, NULL}, environ)` (`extern char **environ;`), `waitpid()`, 0 only for exit status 0, else "lichomat-render --seed-card exited with N" in `err`. `app_cardCheck()`: `emu_hostList()` of the card; the project is `PROJ.IDX`'s byte when it is below 64, else 0; then `PROJnn` and its `PRJ.NFO` by 8.3 name (as `render_cardPrepare()` does).

- [ ] **Step 6: The shell and the harness.** `app_shellOptions_t` gains `const char *home` (NULL: `app_pathsInit()`), `const char *render` and `int (*relaunch)(void *ctx)`; `struct app_shell` gains `app_paths_t paths` and the card operation's state (`op`, `opPath`, `opProject`, `opReplace`, `opDeadline`). `app_shellStart()`: `app_timeZoneRefresh()`; the paths; with `cardDir` NULL, `app_cardState(paths.card)`: 1 → `app_cardSeed()` (a failure is an error), −1 → "error: the card folder cannot be read" and −1 (the GUI offers a new seeded card, Task 12); `app_cardCheck()` 0 → the notice `project N has no PRJ.NFO: the device boots with an empty project` (§11). `int app_shellCardOp(app_shell_t *s, int op, const char *path, int project, int replace)` (in `app_shell.h`, with `enum { APP_OP_IMPORT, APP_OP_EXPORT, APP_OP_PROJECT }`) posts `APP_CMD_CARD_OUT` and remembers the operation; `app_shellFrame()`, on `APP_ACK_CARD_OUT`, runs it on the main thread with `app_localSeconds()`: import → `app_cardImport()` then `relaunch` (§6.7 step 4; from Task 10 `app_shellRestart()`); export → `app_cardExport()` then `APP_CMD_CARD_IN`; project → `app_cardExportProject()`, whose 2 becomes the message `ask: … (repeat with replace)`, then `APP_CMD_CARD_IN`. Without the acknowledgement within 5 s: `error: the card did not come out; nothing was copied` and `APP_CMD_CARD_IN`. Every result is a message. The harness: `--home`, `--render` (default: `lichomat-render` next to `argv[0]`), the app folder's card when `--card` is missing, the commands `import DIR`, `export DIR`, `export-project N DIR [replace]`, and a `relaunch` that calls `fflush(NULL)` and then `execv()`s itself with the same arguments minus `--script FILE`, plus `--relaunched` (so a script runs once).

- [ ] **Step 7: The harness case** in `app_test.py`, added to `CASES`:

```python
def R4_dev_import(t):
    """a first run seeds the app's card; an import backs it up, copies and power-cycles (spec 6.7)"""
    src = t.card('fx')
    r = t.dev(['--render', t.a.render, '--no-audio', '--no-midi', '--seconds', '6',
               '--script', t.script(['1.5 import ' + src])])
    check(r.returncode == 0, 'exit %d: %s' % (r.returncode, r.stderr))
    backups = os.listdir(os.path.join(t.home, 'Card backups'))
    check(len(backups) == 1 and re.match(r'^\d{8}-\d{6}$', backups[0]), 'backups: %r' % backups)
    seeded = os.listdir(os.path.join(t.home, 'Card backups', backups[0], 'PROJ00'))
    check(len(seeded) == 194, 'the seeded card had %d files in PROJ00, not 194' % len(seeded))
    # both PRJ.NFO hold 'Init'; the file lists tell the fx fixture (4 files) from the seeded card (194)
    card = sorted(os.listdir(os.path.join(t.home, 'Card', 'PROJ00')))
    check(card == sorted(os.listdir(os.path.join(src, 'PROJ00'))) and len(card) == 4, 'PROJ00 holds %r' % card)
    check(sum(1 for l in lcd_lines(r.stdout) if splash(l)) >= 2, 'no second boot after the import')
```

- [ ] **Step 8:** Run EMU-CHECK. Expected: N = 205, 0 warnings. Then the gate.

- [ ] **Step 9: Commit**; message `Emulator app: the card folder, first run, import and export (E5.5 R4)`.

---

### Task 10: Settings, the crash note, the power cycle and the diagnostics (§6.9, §11)

**Files:**
- Create: `emu/app/engine/app_settings.{h,c}`, `emu/app/engine/app_crash.{h,c}`, `emu/tests/test_app_settings.c`
- Modify: `emu/app/mac/app_mach.{h,c}`, `emu/app/mac/app_shell.{h,c}`, `emu/app/dev/dev_main.c`, `emu/tests/app_test.py`, `emu/CMakeLists.txt` (both into the units; `emu_add_app_test(test_app_settings IDS R4_settings_roundtrip R4_settings_keep R4_crash_note)`; `R4_dev_crash R4_dev_power` into `emu_app_tests`)

- [ ] **Step 1: The test** `emu/tests/test_app_settings.c`:

```c
#include <signal.h>
#include <sys/wait.h>

#include "app_crash.h"
#include "app_settings.h"
#include "app_time.h"
#include "emu_test.h"

TEST(R4_settings_roundtrip)
{
	char dir[512], path[1024];
	emu_testTempDir(dir, sizeof dir);
	snprintf(path, sizeof path, "%s/settings.txt", dir);
	app_settings_t s, t;
	app_settingsDefaults(&s);
	ASSERT_EQ(app_settingsLoad(&s, path), 1);	/* the first run */
	ASSERT_EQ(s.dAuto, 1);
	ASSERT_EQ(strcmp(s.mix, "sum"), 0);
	snprintf(s.dinSrcName, sizeof s.dinSrcName, "USB MIDI Interface Port 1");
	s.dinSrcUid = -1234567;
	s.usbSrcUid = 0x4C450001;
	s.dAuto = 0;
	s.dMs = 12.5;
	snprintf(s.mix, sizeof s.mix, "out2");
	snprintf(s.crashShown, sizeof s.crashShown, "crash-20261031-130509.txt");
	ASSERT_EQ(app_settingsSave(&s, path), 0);
	app_settingsDefaults(&t);
	ASSERT_EQ(app_settingsLoad(&t, path), 0);
	ASSERT_EQ(strcmp(t.dinSrcName, s.dinSrcName), 0);
	ASSERT_EQ(t.dinSrcUid, -1234567);
	ASSERT_EQ(t.usbSrcUid, 0x4C450001);
	ASSERT_EQ(t.dAuto, 0);
	ASSERT_EQ_DBL(t.dMs, 12.5);
	ASSERT_EQ(strcmp(t.mix, "out2"), 0);
	ASSERT_EQ(strcmp(t.crashShown, s.crashShown), 0);
	ASSERT_EQ(t.window[2], s.window[2]);
}

TEST(R4_settings_keep)
{
	char dir[512], path[1024];
	emu_testTempDir(dir, sizeof dir);
	const char *text = "# a comment\noutput.mix = four\nclockout.audio = 1\noutput.buffer = banana\nclockin.ppq = 7\n";
	emu_testWrite(dir, "settings.txt", text, strlen(text));
	snprintf(path, sizeof path, "%s/settings.txt", dir);
	app_settings_t s;
	app_settingsDefaults(&s);
	ASSERT_EQ(app_settingsLoad(&s, path), 0);
	ASSERT_EQ(strcmp(s.mix, "four"), 0);
	ASSERT_EQ(s.bufferFrames, 0);	/* a bad value keeps its default */
	ASSERT_EQ(s.genPpq, 24);		/* only 4 or 24 */
	ASSERT_TRUE(strstr(s.extra, "clockout.audio = 1\n") != NULL);	/* R5's key (E19) */
	ASSERT_EQ(app_settingsSave(&s, path), 0);
	app_settingsDefaults(&s);
	ASSERT_EQ(app_settingsLoad(&s, path), 0);
	ASSERT_TRUE(strstr(s.extra, "clockout.audio = 1\n") != NULL);
}

static uint64_t noteNow(void) { return 123456789u; }
static void noteTrail(int fd, unsigned n)
{
	char b[32];
	const int k = snprintf(b, sizeof b, "trail %u\n", n);
	(void)!write(fd, b, (size_t)k);
}

TEST(R4_crash_note)
{
	char dir[512], name[64], path[1100], text[512];
	emu_testTempDir(dir, sizeof dir);
	app_timeZoneRefresh();
	const pid_t pid = fork();
	if (pid == 0)
	{
		app_crashInit(dir, noteNow, noteTrail);
		app_crashThread();
		app_crashSignals();
		raise(SIGSEGV);	/* a fault signal on this thread */
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);	/* the app then ends (spec 6.9) */
	ASSERT_EQ(app_crashLatest(dir, "", name, sizeof name), 1);
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "r");
	ASSERT_TRUE(f != NULL);
	const size_t n = fread(text, 1, sizeof text - 1u, f);
	fclose(f);
	text[n] = '\0';
	ASSERT_TRUE(strstr(text, "LICHOMAT Emu crash: SIGSEGV\nvirtual time: 123456789 cycles\n") != NULL);
	ASSERT_TRUE(strstr(text, "trail 64\n") != NULL);
	ASSERT_EQ(app_crashLatest(dir, name, path, sizeof path), 0);	/* shown once */
}

TEST_MAIN()
```

- [ ] **Step 2:** Run EMU-CHECK. Expected: build fails, `app_settings.h` not found.

- [ ] **Step 3: `app_settings.h`**:

```c
/* app_settings.h: the settings of spec 6.9 in settings.txt, key = value (E19); main thread */
#ifndef APP_SETTINGS_H_
#define APP_SETTINGS_H_
#include <stdint.h>

typedef struct app_settings
{
	char outputUid[128];		/**< "" follows the default output */
	uint32_t bufferFrames;		/**< 0: the device's */
	char mix[8];				/**< sum, out1, out2, four */
	int32_t dinSrcUid, dinDstUid;
	char dinSrcName[128], dinDstName[128];
	int32_t usbSrcUid, usbDstUid;	/**< the virtual endpoints' unique IDs */
	int dAuto;
	double dMs;
	int window[4];
	double genBpm;
	int genPpq;
	char crashShown[64];
	char extra[4096];			/**< other keys' lines, kept as read */
} app_settings_t;

void app_settingsDefaults(app_settings_t *s);	/**< zeroes, then mix sum, dAuto 1, window 100 100 1100 760, 120 BPM, 24 PPQ */
int app_settingsLoad(app_settings_t *s, const char *path);	/**< 0, 1 no file, -1 unreadable */
int app_settingsSave(const app_settings_t *s, const char *path);	/**< PATH.tmp, fsync, rename; 0 or -1 */
#endif
```

- [ ] **Step 4: `app_settings.c`**, complete except `app_settingsDefaults()` (as its comment says):

```c
#include "app_settings.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum { T_STR, T_U32, T_I32, T_INT, T_DBL } app_type_t;
typedef struct { const char *key; app_type_t type; size_t off, size; double lo, hi; } app_field_t;
#define F(k, t, m, lo, hi) { k, t, offsetof(app_settings_t, m), sizeof(((app_settings_t *)0)->m), lo, hi }
static const app_field_t app_fields[] = {
	F("output.device", T_STR, outputUid, 0, 0),
	F("output.buffer", T_U32, bufferFrames, 0, 4096),
	F("output.mix", T_STR, mix, 0, 0),
	F("din.source.uid", T_I32, dinSrcUid, INT32_MIN, INT32_MAX),
	F("din.source.name", T_STR, dinSrcName, 0, 0),
	F("din.destination.uid", T_I32, dinDstUid, INT32_MIN, INT32_MAX),
	F("din.destination.name", T_STR, dinDstName, 0, 0),
	F("usb.source.uid", T_I32, usbSrcUid, INT32_MIN, INT32_MAX),
	F("usb.destination.uid", T_I32, usbDstUid, INT32_MIN, INT32_MAX),
	F("slave.delay.auto", T_INT, dAuto, 0, 1),
	F("slave.delay.ms", T_DBL, dMs, 0, 500),
	F("window.x", T_INT, window[0], -100000, 100000),
	F("window.y", T_INT, window[1], -100000, 100000),
	F("window.width", T_INT, window[2], 200, 100000),
	F("window.height", T_INT, window[3], 200, 100000),
	F("clockin.bpm", T_DBL, genBpm, 1, 300),
	F("clockin.ppq", T_INT, genPpq, 4, 24),
	F("crash.shown", T_STR, crashShown, 0, 0),
};
#define NFIELDS (sizeof app_fields / sizeof app_fields[0])

static char *app_trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		*--e = '\0';
	return s;
}

static void app_setField(app_settings_t *s, const app_field_t *f, const char *v)
{
	char *p = (char *)s + f->off, *end;
	if (f->type == T_STR)
	{
		if (strlen(v) < f->size)
			memcpy(p, v, strlen(v) + 1u);
		return;
	}
	errno = 0;
	if (f->type == T_DBL)
	{
		const double x = strtod(v, &end);
		if (!errno && end != v && !*end && x >= f->lo && x <= f->hi)
			memcpy(p, &x, sizeof x);
		return;	/* a bad value keeps the default */
	}
	const int64_t x = strtoll(v, &end, 10);	/* integers never go through a double */
	if (errno || end == v || *end || x < (int64_t)f->lo || x > (int64_t)f->hi)
		return;
	if (f->type == T_U32)	/* each conversion only in its own branch, on a value in range */
	{
		const uint32_t u = (uint32_t)x;
		memcpy(p, &u, sizeof u);
	}
	else if (f->type == T_I32)
	{
		const int32_t i = (int32_t)x;
		memcpy(p, &i, sizeof i);
	}
	else
	{
		const int n = (int)x;
		memcpy(p, &n, sizeof n);
	}
}

int app_settingsLoad(app_settings_t *s, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return errno == ENOENT ? 1 : -1;
	size_t used = strlen(s->extra);
	char line[1024];
	while (fgets(line, sizeof line, f))
	{
		char *t = app_trim(line), *eq = strchr(t, '=');
		if (!*t || *t == '#' || !eq)
			continue;
		*eq = '\0';
		const char *key = app_trim(t), *val = app_trim(eq + 1);
		size_t k = 0;
		while (k < NFIELDS && strcmp(app_fields[k].key, key))
			k++;
		if (k < NFIELDS)
		{
			app_setField(s, &app_fields[k], val);
			continue;
		}
		const int w = snprintf(s->extra + used, sizeof s->extra - used, "%s = %s\n", key, val);
		if (w > 0 && (size_t)w < sizeof s->extra - used)
			used += (size_t)w;
		else
			s->extra[used] = '\0';	/* no room: the line is dropped whole */
	}
	fclose(f);
	if (strcmp(s->mix, "sum") && strcmp(s->mix, "out1") && strcmp(s->mix, "out2") && strcmp(s->mix, "four"))
		snprintf(s->mix, sizeof s->mix, "sum");
	if (s->genPpq != 4 && s->genPpq != 24)
		s->genPpq = 24;
	return 0;
}

int app_settingsSave(const app_settings_t *s, const char *path)
{
	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	if (!f)
		return -1;
	fputs("# LICHOMAT Emu settings (stage 1b spec 6.9), written by the app\n", f);
	for (size_t k = 0; k < NFIELDS; k++)
	{
		const char *p = (const char *)s + app_fields[k].off;
		uint32_t u;
		int32_t i;
		int n;
		double x;
		fprintf(f, "%s = ", app_fields[k].key);
		switch (app_fields[k].type)
		{
		case T_STR: fputs(p, f); break;
		case T_U32: memcpy(&u, p, sizeof u); fprintf(f, "%u", u); break;
		case T_I32: memcpy(&i, p, sizeof i); fprintf(f, "%d", i); break;
		case T_INT: memcpy(&n, p, sizeof n); fprintf(f, "%d", n); break;
		case T_DBL: memcpy(&x, p, sizeof x); fprintf(f, "%.17g", x); break;
		}
		fputc('\n', f);
	}
	fputs(s->extra, f);
	const int bad = ferror(f) || fflush(f) != 0 || fsync(fileno(f)) != 0;
	if (fclose(f) != 0 || bad)
	{
		unlink(tmp);
		return -1;
	}
	return rename(tmp, path) == 0 ? 0 : -1;
}
```

- [ ] **Step 5: `app_crash.h` and `app_crash.c`** (E20), complete:

```c
/* app_crash.h: the crash note of spec 6.9, written with async-signal-safe calls */
#ifndef APP_CRASH_H_
#define APP_CRASH_H_
#include <stddef.h>
#include <stdint.h>
void app_crashInit(const char *dir, uint64_t (*now)(void), void (*trail)(int fd, unsigned n));
void app_crashThread(void);		/**< each thread that may fault: its own alternate signal stack */
void app_crashSignals(void);	/**< SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP */
int app_crashWrite(const char *what);	/**< the note, once per process; 0 or -1 */
int app_crashLatest(const char *dir, const char *shown, char *name, size_t n);	/**< 1: a note newer than shown */
#endif

/* app_crash.c */
#include "app_crash.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_time.h"

static char app_crashDir[1024];
static uint64_t (*app_crashNow)(void);
static void (*app_crashTrail)(int fd, unsigned n);
static volatile sig_atomic_t app_crashDone;

static size_t app_add(char *b, size_t k, size_t n, const char *s)
{
	while (*s && k + 1u < n)
		b[k++] = *s++;
	return k;
}

static size_t app_addU64(char *b, size_t k, size_t n, uint64_t v)
{
	char d[20];
	unsigned m = 0;
	do
		d[m++] = (char)('0' + v % 10u);
	while ((v /= 10u) != 0u);
	while (m && k + 1u < n)
		b[k++] = d[--m];
	return k;
}

void app_crashInit(const char *dir, uint64_t (*now)(void), void (*trail)(int fd, unsigned n))
{
	snprintf(app_crashDir, sizeof app_crashDir, "%s", dir);
	app_crashNow = now;
	app_crashTrail = trail;
}

int app_crashWrite(const char *what)
{
	if (app_crashDone || !app_crashDir[0])
		return -1;
	app_crashDone = 1;	/* a model check writes, then aborts: one note */
	char name[40], path[1100], text[512];
	if (!app_stampName(app_localSeconds(), "crash-", ".txt", name, sizeof name))
		return -1;
	size_t k = app_add(path, 0u, sizeof path, app_crashDir);
	k = app_add(path, k, sizeof path, "/");
	path[app_add(path, k, sizeof path, name)] = '\0';
	const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	k = app_add(text, 0u, sizeof text, "LICHOMAT Emu crash: ");
	k = app_add(text, k, sizeof text, what);
	k = app_add(text, k, sizeof text, "\nvirtual time: ");
	k = app_addU64(text, k, sizeof text, app_crashNow ? app_crashNow() : 0u);
	k = app_add(text, k, sizeof text, " cycles\nthe last 64 scheduler events and inputs, oldest first:\n");
	(void)!write(fd, text, k);
	if (app_crashTrail)
		app_crashTrail(fd, 64u);
	close(fd);
	return 0;
}

static void app_crashOnSignal(int sig)
{
	const char *name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGFPE ? "SIGFPE"
		: sig == SIGILL ? "SIGILL" : sig == SIGABRT ? "SIGABRT" : "SIGTRAP";
	(void)app_crashWrite(name);
	raise(sig);	/* SA_RESETHAND: the default action ends the app */
}

void app_crashThread(void)
{
	stack_t ss;
	memset(&ss, 0, sizeof ss);
	ss.ss_size = 4u * SIGSTKSZ;
	ss.ss_sp = malloc(ss.ss_size);	/* the thread's, for its life */
	if (ss.ss_sp)
		(void)sigaltstack(&ss, NULL);
}

void app_crashSignals(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = app_crashOnSignal;
	sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	static const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP };
	for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
		(void)sigaction(sigs[i], &sa, NULL);
}

int app_crashLatest(const char *dir, const char *shown, char *name, size_t n)
{
	DIR *d = opendir(dir);
	char best[64] = "";
	for (struct dirent *e; d && (e = readdir(d)) != NULL;)
		if (strlen(e->d_name) == 25u && !strncmp(e->d_name, "crash-", 6) && !strcmp(e->d_name + 21, ".txt")
			&& strcmp(e->d_name, best) > 0)
			snprintf(best, sizeof best, "%s", e->d_name);
	if (d)
		closedir(d);
	if (!best[0] || strcmp(best, shown) <= 0)
		return 0;
	snprintf(name, n, "%s", best);
	return 1;
}
```

- [ ] **Step 6: The shell, the Mach host and the harness.**
  - `app_mach.c` gains `int app_waitPid(int pid, double sec)`, declared in `app_shell.h` (which the Objective-C++ `main()` can include; `app_mach.h` cannot, it pulls in `_Atomic`): `kqueue()`, `EV_SET(&ev, pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, NULL)`; `kevent()` to add it (`ESRCH`: already gone, 0); `kevent()` with a `timespec` of `sec` for the exit; 0 or −1.
  - `app_shellStart()` does not wait for an old process: the GUI's `main()` calls `app_waitPid()` before `NSApplicationMain()` (Task 12), never on the main run loop, and the harness keeps its PID across `execv()`; `app_settingsLoad(paths.settings)`, whose values become the output request, the mix, D, the generator, the endpoint IDs and the DIN bindings (options given on the command line win); `app_crashInit(paths.home, emu_now, emu_trailWrite)` (the trail writer is the core's, async-signal-safe), `app_crashThread()` and `app_crashSignals()` on the main thread, and `app_crashThread()` again first thing on the firmware thread; `cfg.fault` = a handler that calls `app_crashWrite(what)` then `abort()`; after `app_cmInit()`, the actual unique IDs go into the settings (spec 6.9) and are saved; `app_crashLatest(paths.home, settings.crashShown)` → the message `crash note: NAME` followed by the note's first 3 lines, then `crashShown` = NAME, saved.
  - `app_shellFrame()`: `app_timeZoneRefresh()` once a minute; a changed setting (D, mix, output, bindings, generator, window) is saved within 1 s.
  - `void app_shellPowerCycle(app_shell_t *s)` (in `app_shell.h`): `APP_CMD_CARD_OUT`; on its acknowledgement, or after 2 s: `app_shellRestart()`. `void app_shellRestart(app_shell_t *s)` (in `app_shell.h`, also the hang's **Restart**): `app_settingsSave()` and `app_cmDispose()`, which need no firmware thread, then `o.relaunch(o.ctx)` (E18): the old process frees the stored unique IDs before the new one asks for them (A10). Import's step 4 uses `app_shellRestart()`, the card being out already.
  - The harness: `power` (`app_shellPowerCycle()`), `test-fault` (`testFault = 1`; the harness is not shipped), `--after-pid N`, and the `diag` line of Task 6 now from the snapshot's diagnostics with the counters of §5.5 (`core`, `fw`) on a second line `counters …`.
  - Cases in `app_test.py`, added to `CASES`:

```python
def R4_dev_crash(t):
    """a fault on the firmware thread leaves crash-*.txt; the next launch shows it once (spec 6.9)"""
    card = t.card()
    r = t.dev(['--card', card, '--no-audio', '--no-midi', '--seconds', '4', '--script', t.script(['1.5 test-fault'])])
    check(r.returncode < 0, 'the harness did not end by the signal: %d' % r.returncode)
    notes = [n for n in os.listdir(t.home) if re.match(r'^crash-\d{8}-\d{6}\.txt$', n)]
    check(len(notes) == 1, 'crash notes: %r' % os.listdir(t.home))
    with open(os.path.join(t.home, notes[0])) as f:
        text = f.read()
    check(text.startswith('LICHOMAT Emu crash: SIGSEGV\nvirtual time: ') and text.count('\n') >= 10, text)
    r = t.dev(['--card', card, '--no-audio', '--no-midi', '--seconds', '2'])
    check('crash note: ' + notes[0] in r.stdout, 'the next launch did not show the note: ' + r.stdout)
    r = t.dev(['--card', card, '--no-audio', '--no-midi', '--seconds', '2'])
    check('crash note:' not in r.stdout, 'the note was shown twice')


def R4_dev_power(t):
    """a power cycle boots again, and the settings survive it (spec 6.9)"""
    r = t.dev(['--card', t.card(), '--no-audio', '--no-midi', '--seconds', '4',
               '--script', t.script(['0.5 delay 12.5', '1.5 power'])])
    check(r.returncode == 0, 'exit %d: %s' % (r.returncode, r.stderr))
    check(sum(1 for l in lcd_lines(r.stdout) if splash(l)) >= 2, 'no second boot')
    with open(os.path.join(t.home, 'settings.txt')) as f:
        text = f.read()
    check('slave.delay.auto = 0\n' in text and 'slave.delay.ms = 12.5\n' in text, text)
```

- [ ] **Step 7:** Run EMU-CHECK. Expected: N = 210, 0 warnings. Then the gate. Then by hand, for A10: start the harness with MIDI, connect a DAW track to 'LICHOMAT Emu', `power`, and check that the DAW keeps the connection.

- [ ] **Step 8: Commit**; message `Emulator app: settings, crash note, power cycle and diagnostics (E5.5 R4)`.

---

### Task 11: Vendor Dear ImGui — approved by the owner (2026-10-31 06:51)

The owner approved the download on 2026-10-31 at 06:51. The pin stays: tag `v1.92.9b` (released 2026-07-31; v1.93.0 of 2026-10-14 is too fresh for a pin), these 15 files only, 3,303,558 bytes in all. The task may run before Tasks 1–10 end; nothing before Task 12 uses the files.

**Files:**
- Create: `emu/third_party/imgui/` (the 15 files at their release paths), `emu/third_party/imgui/README.md`
- Modify: `emu/tests/licence_gate.py` (`ALLOWED` gains `('emu/third_party/imgui/', 'Dear ImGui, MIT')`), `emu/tests/app_test.py`, `emu/CMakeLists.txt`

- [ ] **Step 1: Fetch into a new, empty folder** (untrusted data; nothing in it is run):

```bash
mkdir -p /Users/tk/Code/lxr/build/imgui-fetch && cd /Users/tk/Code/lxr/build/imgui-fetch && curl -fL --retry 3 -o imgui-v1.92.9b.tar.gz https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz && shasum -a 256 imgui-v1.92.9b.tar.gz && tar -tzf imgui-v1.92.9b.tar.gz | head -n 1
```

Expected: a SHA-256 (record it), and the prefix `imgui-1.92.9b/`.

- [ ] **Step 2: Extract only the 15 files and check their size:**

```bash
cd /Users/tk/Code/lxr/build/imgui-fetch && F="LICENSE.txt imconfig.h imgui.cpp imgui.h imgui_draw.cpp imgui_internal.h imgui_tables.cpp imgui_widgets.cpp imstb_rectpack.h imstb_textedit.h imstb_truetype.h backends/imgui_impl_metal.h backends/imgui_impl_metal.mm backends/imgui_impl_osx.h backends/imgui_impl_osx.mm" && rm -rf x && mkdir x && tar -xzf imgui-v1.92.9b.tar.gz -C x $(for f in $F; do printf 'imgui-1.92.9b/%s ' "$f"; done) && (cd x/imgui-1.92.9b && stat -f %z $F | awk '{s += $1} END {print s}') && grep -n 'IMGUI_DISABLE_DEFAULT_FONT' x/imgui-1.92.9b/imconfig.h
```

Expected: `3303558`, and the `imconfig.h` line that offers `IMGUI_DISABLE_DEFAULT_FONT` (E23). Any other total, or no such line: stop and report; do not vendor.

- [ ] **Step 3: Copy them** into the worktree, paths kept: `mkdir -p emu/third_party/imgui/backends`, then `cp` each file from `x/imgui-1.92.9b/` to `emu/third_party/imgui/` at the same relative path. Then remove `/Users/tk/Code/lxr/build/imgui-fetch`.

- [ ] **Step 4: The README.** Write `emu/third_party/imgui/README.md`: an HTML comment with the GPL-3.0-only header lines of the Rules, then

```markdown
# Dear ImGui v1.92.9b, vendored

Dear ImGui by Omar Cornut, under the MIT licence in `LICENSE.txt`, for the panel of
LICHOMAT Emu (stage 1b spec 3.3, 6.6). Fetched on 2026-10-31 with the owner's approval
from https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz (SHA-256
`ARCHIVE-SHA256`): tag `v1.92.9b`, released 2026-07-31. Only these 15 files, unchanged
and at their paths in the release, 3,303,558 bytes in all. The ctest id `R4_imgui_pin`
checks every row below and that the folder holds nothing else.

| File | Bytes | SHA-256 |
|---|---|---|
```

  followed by the rows that this command prints, with `ARCHIVE-SHA256` replaced by Step 1's value:

```bash
cd /Users/tk/Code/lxr/build/wt-emulator/emu/third_party/imgui && for f in LICENSE.txt imconfig.h imgui.cpp imgui.h imgui_draw.cpp imgui_internal.h imgui_tables.cpp imgui_widgets.cpp imstb_rectpack.h imstb_textedit.h imstb_truetype.h backends/imgui_impl_metal.h backends/imgui_impl_metal.mm backends/imgui_impl_osx.h backends/imgui_impl_osx.mm; do printf '| `%s` | %s | `%s` |\n' "$f" "$(stat -f %z "$f")" "$(shasum -a 256 "$f" | cut -d' ' -f1)"; done
```

- [ ] **Step 5: The pin check** in `app_test.py`, added to `CASES`, and registered outside `if(LICHOMAT_EMU_APP)` with `add_test(NAME R4_imgui_pin COMMAND ${Python3_EXECUTABLE} "${EMU_DIR}/tests/app_test.py" --root "${LXR_ROOT}" --work "${CMAKE_BINARY_DIR}/app-test" R4_imgui_pin)`:

```python
IMGUI = ['LICENSE.txt', 'imconfig.h', 'imgui.cpp', 'imgui.h', 'imgui_draw.cpp', 'imgui_internal.h', 'imgui_tables.cpp',
         'imgui_widgets.cpp', 'imstb_rectpack.h', 'imstb_textedit.h', 'imstb_truetype.h', 'backends/imgui_impl_metal.h',
         'backends/imgui_impl_metal.mm', 'backends/imgui_impl_osx.h', 'backends/imgui_impl_osx.mm']


def R4_imgui_pin(t):
    base = os.path.join(t.a.root, 'emu', 'third_party', 'imgui')
    with open(os.path.join(base, 'README.md')) as f:
        readme = f.read()
    check('tag `v1.92.9b`' in readme, 'the README does not name tag v1.92.9b')
    rows = {m.group(1): (int(m.group(2)), m.group(3))
            for m in re.finditer(r'^\| `([^`]+)` \| (\d+) \| `([0-9a-f]{64})` \|$', readme, re.M)}
    check(sorted(rows) == sorted(IMGUI), 'the README lists %r' % sorted(rows))
    found = sorted(os.path.relpath(os.path.join(d, n), base)
                   for d, _, names in os.walk(base) for n in names if not n.startswith('.'))
    check(found == sorted(IMGUI + ['README.md']), 'the folder holds %r' % found)
    for name, (size, sha) in rows.items():
        with open(os.path.join(base, name), 'rb') as f:
            data = f.read()
        check(len(data) == size and hashlib.sha256(data).hexdigest() == sha, name + ' differs from its pin')
    check(sum(size for size, _ in rows.values()) == 3303558, 'the pinned files do not hold 3,303,558 bytes')
```

- [ ] **Step 6:** Run EMU-CHECK. Expected: N = 211, 0 warnings. Then the gate.

- [ ] **Step 7: Commit** the 16 files, `licence_gate.py`, `app_test.py` and `emu/CMakeLists.txt`; message `Vendor Dear ImGui v1.92.9b: 15 files, pinned by SHA-256 (owner's approval 2026-10-31 06:51; E5.5 R4)`, with the archive's SHA-256 in the body.

---

### Task 12: The GUI — panel, About and notices, splash (§6.6, §6.8, §10.1, §10.2)

**Files:**
- Create: `emu/common/emu_notices.h`, `emu/app/gui/panel_font.{h,c}`, `emu/app/gui/panel.h`, `emu/app/gui/panel.cpp`, `emu/app/gui/main.mm`, `emu/app/gui/Info.plist.in`, `emu/tests/test_app_font.c`
- Modify: `emu/render/render_main.c:224–247`, `emu/tests/render_test.py:185`, `emu/CMakeLists.txt`

- [ ] **Step 1: The notices (E26).** `emu/common/emu_notices.h` holds `#define EMU_TAGLINE "emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02"`, `#define EMU_FIRMWARE_CHANGES "F1-F18 and F6b"` and `static const char *const emu_legal[] __attribute__((unused))` with the ten lines that `render_version()` prints today, in the same order, and `#define EMU_LEGAL_COUNT (sizeof emu_legal / sizeof emu_legal[0])`. `render_version()` then prints `EMU_TAGLINE`, `"Runs LICHOMAT %s with the emulator changes " EMU_FIRMWARE_CHANGES ". Built from commit %s.\n"` and the `emu_legal` lines; `render_test.py:185` expects `F1-F18 and F6b`. `emu_render_exe()` gives both renderer targets the include directory `${EMU_DIR}/common`. `name_allow.txt` needs no change: the lines that name the maker are the same strings.

- [ ] **Step 2: The font test** `emu/tests/test_app_font.c` (inside `if(LICHOMAT_EMU_APP)`: `emu_add_app_test(test_app_font IDS R4_panel_font)` with `target_sources(test_app_font PRIVATE "${EMU_APP_DIR}/gui/panel_font.c")` and the include directory `emu/app/gui`):

```c
#include "panel_font.h"
#include "seqc_test.h"

TEST(R4_panel_font)
{
	static const uint8_t cgram[64] = { [24] = 0x1F, [31] = 0x11 };
	uint8_t r[8];
	panel_glyph('A', cgram, r);
	const uint8_t a[8] = { 0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00 };
	ASSERT_EQ(memcmp(r, a, 8), 0);
	panel_glyph(3u, cgram, r);		/* codes 0-7 from CGRAM (spec 4.7) */
	ASSERT_EQ(r[0], 0x1F);
	ASSERT_EQ(r[7], 0x11);
	panel_glyph(0xC5u, cgram, r);	/* the song page's up arrow */
	ASSERT_EQ(r[0], 0x04);
	ASSERT_EQ(r[1], 0x0E);
	ASSERT_EQ(r[2], 0x15);
	panel_glyph(0x80u, cgram, r);	/* any other code: a hollow box */
	ASSERT_EQ(r[0], 0x1F);
	ASSERT_EQ(r[3], 0x11);
	ASSERT_EQ(r[6], 0x1F);
	for (unsigned c = 0x20u; c <= 0x7Eu; c++)
	{
		panel_glyph((uint8_t)c, cgram, r);
		ASSERT_EQ(r[7], 0);	/* the bottom row stays free for the cursor */
	}
}

TEST_MAIN()
```

- [ ] **Step 3: `panel_font.h` and `panel_font.c`** (E24): the header declares `extern const uint8_t panel_font[95][5], panel_arrowUp[5], panel_arrowDown[5], panel_box[5];` and `void panel_glyph(uint8_t c, const uint8_t cgram[64], uint8_t rows[8]);` (rows of 5 bits, bit 4 the left column). The table is five column bytes per glyph, bit 0 the top row:

```c
#include "panel_font.h"

const uint8_t panel_font[95][5] = {	/* 0x20-0x7E */
	{0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
	{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
	{0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
	{0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
	{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
	{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
	{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
	{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
	{0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
	{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
	{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
	{0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
	{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
	{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
	{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
	{0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
	{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
	{0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
	{0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
	{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
	{0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
	{0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
	{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
	{0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},
};
const uint8_t panel_arrowUp[5] = { 0x04, 0x02, 0x7F, 0x02, 0x04 };		/* 0xC5 */
const uint8_t panel_arrowDown[5] = { 0x10, 0x20, 0x7F, 0x20, 0x10 };	/* 0xC6 */
const uint8_t panel_box[5] = { 0x7F, 0x41, 0x41, 0x41, 0x7F };

void panel_glyph(uint8_t c, const uint8_t cgram[64], uint8_t rows[8])
{
	if (c < 8u)
	{
		for (unsigned r = 0; r < 8u; r++)
			rows[r] = cgram[8u * c + r] & 0x1Fu;
		return;
	}
	const uint8_t *g = c >= 0x20u && c <= 0x7Eu ? panel_font[c - 0x20u]
		: c == 0xC5u ? panel_arrowUp : c == 0xC6u ? panel_arrowDown : panel_box;
	for (unsigned r = 0; r < 8u; r++)
	{
		rows[r] = 0;
		for (unsigned x = 0; x < 5u; x++)
			rows[r] = (uint8_t)(rows[r] | (((g[x] >> r) & 1u) << (4u - x)));
	}
}
```

- [ ] **Step 4: The panel**, `panel.h` (a C interface with a `panel_t` of the GUI's own state: `latched[41]`, `mouse[41]`, `down[41]`, `knobRest[4]`, `knobAngle[4]`, the encoder's rest, the fader levels, the jack strip, the open windows) and `panel.cpp` (ImGui, C++17). It includes only `app_types.h`, `app_bind.h`, `app_shell.h` and `panel_font.h`, all safe for C++ (no `_Atomic`). `panel_frame(panel_t *p, app_shell_t *s)` draws one frame from `app_shellSnapshot(s)` (nothing but the menu bar before the first) and posts through `app_shellPanel()` and `app_shellCommand()`. The rows, by function (§6.6; A1 has the owner confirm it is no copy of the faceplate), in our own colours (a dark slate background, a teal LCD, amber LEDs):
  1. the LCD (16 × 2 cells of 5 × 8 dots from `panel_glyph()`, blank while `displayOn` is 0, the underline cursor at `cursorRow`/`cursorCol` while `cursorOn`, blinking at 2 Hz of wall time while `blinkOn`) and the encoder;
  2. knobs 1–4 under LCD columns 1–4 (16 characters / 4);
  3. faders 1–6, labelled `drum 1`, `drum 2`, `drum 3`, `snare`, `cymbal`, `hi-hats`;
  4. the 41 buttons with their LEDs (bit n of the snapshot's `leds`), labelled from this table and grouped as transport and modes, voices, select 1–8, steps 1–16:

```cpp
static const char *const panel_names[41] = {
	"bar+", "voice 1", "voice 2", "voice 3", "voice 4", "voice 5", "voice 6", "voice 7",
	"select 5", "select 6", "select 7", "select 8", "step 16", "step 15", "step 14", "step 13",
	"select 1", "select 2", "select 3", "select 4", "step 12", "step 11", "step 10", "step 9",
	"voice", "perf", "load", "save", "step 8", "step 7", "step 6", "step 5",
	"shift", "start/stop", "rec", "copy", "step 4", "step 3", "step 2", "step 1", "bar-",
};
```

  5. the jack strip: the four output-jack checkboxes (`APP_P_JACKS`), the card switch (`APP_P_CARD`), the clock-out LED (the snapshot's `clockOut`), the clock-in button (`APP_P_CLOCK` with `mach_absolute_time()`), the generator (BPM, PPQ 4 or 24, on/off: `APP_CMD_GENERATOR`) and the reset switch (`APP_P_RESET`).

  The input rules, as ImGui code (complete for the two with state):

```cpp
// a button: mouse down and up; a right click latches it down until the next click (spec 6.6)
static void panel_button(panel_t *p, app_shell_t *s, unsigned n, ImVec2 size)
{
	ImGui::PushID((int)n);
	ImGui::InvisibleButton(panel_names[n], size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool left = ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
	if (p->latched[n] && (left || right))
		p->latched[n] = false;	// the next click releases the latch
	else if (right)
		p->latched[n] = true;
	p->mouse[n] = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool key = (n == 32u && (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)))
		|| (n == 33u && ImGui::IsKeyDown(ImGuiKey_Space) && !ImGui::GetIO().WantTextInput);
	const bool down = p->mouse[n] || p->latched[n] || key;
	if (down != p->down[n])
	{
		const app_panelEvent_t ev = { 0, 0.0, down ? 1 : 0, APP_P_BUTTON, (uint8_t)n };
		app_shellPanel(s, &ev);
		p->down[n] = down;
	}
	panel_drawButton(n, down, (app_shellSnapshot(s)->leds >> n) & 1u);	// the drawing only
	ImGui::PopID();
}

// a knob: 1 degree per point of vertical drag, 5 per wheel notch; whole degrees go out (spec 6.6)
static void panel_knob(panel_t *p, app_shell_t *s, unsigned k)
{
	ImGui::PushID(100 + (int)k);
	ImGui::InvisibleButton("knob", ImVec2(56, 56));
	ImGuiIO &io = ImGui::GetIO();
	if (ImGui::IsItemActive())
		p->knobRest[k] -= io.MouseDelta.y;	// up is clockwise
	if (ImGui::IsItemHovered())
		p->knobRest[k] += 5.0f * io.MouseWheel;
	const int deg = (int)p->knobRest[k];	// toward 0: the rest stays for the next frame
	if (deg != 0)
	{
		const app_panelEvent_t ev = { 0, 0.0, deg, APP_P_KNOB, (uint8_t)(k + 1u) };
		app_shellPanel(s, &ev);
		p->knobRest[k] -= (float)deg;
		p->knobAngle[k] = (p->knobAngle[k] + deg % 360 + 360) % 360;
	}
	panel_drawKnob(p->knobAngle[k]);
	ImGui::PopID();
}
```

  The encoder: a wheel notch is one detent; a vertical drag gives one detent per 12 points (the rest kept); a click without drag sends `APP_P_PUSH` of 50 ms; every detent goes out (`APP_P_ENCODER`, §4.6). A fader: `ImGui::VSliderFloat` 0–1, `APP_P_FADER` on change, a double click returns it to 1.0. At power-on (§6.6): faders full, all jacks plugged, the card in, the knobs at 0°. The windows from the menu bar: **Diagnostics** (every field of `app_diagnostics_t`: formats and converter, block rate against 689.7, output underruns, effective D, L, the lead range, the input-latency range of the last 2 s, the load, the counters of §5.5, the drops of every ring (`midiInDropped`, `midiOutDropped`, `midiUnsent`, `panelDropped`, `snapDropped`, `ackDropped`), the card warnings from `app_shellWarning()` (§11); and `realtime`, E29), **Audio** (device from `app_shellDevices()`, buffer 64–2048, mix; `APP_CMD_OUTPUT`, `APP_CMD_MIX`), **MIDI** (DIN source and destination from `app_shellEndpoints()` and `app_shellBind()`, shown 'not connected' when unbound; D: automatic, or 0–200 ms, with the effective D in ms, §6.4), **About** (below), and the card and device menus (import, export card, export project; power cycle).

- [ ] **Step 5: About (§10.1).** An ImGui window with, one `TextUnformatted()` each: `LICHOMAT Emu EMU_RENDER_VERSION`; `EMU_TAGLINE`; `Runs LICHOMAT %s with the emulator changes F1-F18 and F6b. Built from commit %s.` (`render_build.h`); the `emu_legal` lines; `Dear ImGui: ` and the copyright line of `emu/third_party/imgui/LICENSE.txt`, then `MIT licence.`; `The emulator sounds very close to the device, not identical.` (risk 7).

- [ ] **Step 6: `main.mm`** (Objective-C++, ARC):
  - `main()`: `--after-pid N` from `argv` and `app_waitPid(N, 5.0)` first, before any Cocoa or CoreMIDI call (E18; never in `applicationDidFinishLaunching:`, which runs on the main run loop); then `NSApplication sharedApplication`, `setActivationPolicy:NSApplicationActivationPolicyRegular`, a delegate, a menu bar with the app menu (About LICHOMAT Emu, Quit with ⌘Q), `[NSApp run]`.
  - `applicationDidFinishLaunching:`: `self.activity = [[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical reason:@"real-time emulation"]` (§6.1, App Nap); `app_shellStart()` with `render` = `[[NSBundle mainBundle] pathForAuxiliaryExecutable:@"lichomat-render"]`, `relaunch` below, `message` collecting lines for an ImGui message list; on −1 with an unreadable card, an `NSAlert` offering "Make a new seeded card" (the old folder goes to `Card backups` first, §11).
  - The window: `NSWindow` from the settings' frame, style titled, closable, miniaturizable, resizable, title `LICHOMAT Emu (unofficial)` (§10.2); content `MTKView` with `MTLCreateSystemDefaultDevice()`, `preferredFramesPerSecond = 60`, `clearColor` our background; `ImGui::CreateContext()`, `ImGui_ImplMetal_Init(device)`, `ImGui_ImplOSX_Init(view)`; the font (E23): `CTFontCreateWithName(CFSTR("Menlo-Regular"), 14.0, NULL)`, `CTFontCopyAttribute(font, kCTFontURLAttribute)`, `CFURLGetFileSystemRepresentation()`, `io.Fonts->AddFontFromFileTTF(path, 15.0f)` (`ImFontConfig.FontNo = 0` for the `.ttc`); without a font, an `NSAlert` and exit.
  - `drawInMTKView:`: `[queue commandBuffer]`; `view.currentRenderPassDescriptor` (nil: commit and return); `ImGui_ImplMetal_NewFrame(rp)`, `ImGui_ImplOSX_NewFrame(view)`, `ImGui::NewFrame()`; `app_shellFrame()`, `panel_frame()`; modal popups for the crash note (its text read from the file), for `hang:` with a **Restart** button (`app_shellRestart()`: settings saved and MIDI disposed, no card out, since the firmware thread is stuck), and for the export-project question; `ImGui::Render()`; `renderCommandEncoderWithDescriptor:`, `ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc)`, `endEncoding`, `presentDrawable:`, `commit`.
  - Folders: `NSOpenPanel` with `canChooseDirectories = YES`, `canChooseFiles = NO`, `canCreateDirectories = YES` for exports; then `app_shellCardOp()`.
  - `relaunch` (E18), complete:

```objc
static int emu_relaunch(void *ctx)
{
	(void)ctx;
	NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration];
	cfg.createsNewApplicationInstance = YES;
	cfg.arguments = @[ @"--after-pid", [NSString stringWithFormat:@"%d", getpid()] ];
	[[NSWorkspace sharedWorkspace] openApplicationAtURL:[[NSBundle mainBundle] bundleURL] configuration:cfg
		completionHandler:^(NSRunningApplication *app, NSError *error) {
			(void)app;
			if (error)
				NSLog(@"LICHOMAT Emu: the power cycle could not start a new process: %@", error);
			_exit(error ? 70 : 0);
		}];
	return 0;
}
```

- [ ] **Step 7: `Info.plist.in`**: `CFBundleDevelopmentRegion` en, `CFBundleExecutable` `LICHOMAT Emu`, `CFBundleIdentifier` `io.github.isthar.lichomat-emu`, `CFBundleInfoDictionaryVersion` 6.0, `CFBundleName` and `CFBundleDisplayName` `LICHOMAT Emu`, `CFBundlePackageType` APPL, `CFBundleShortVersionString` and `CFBundleVersion` `@EMU_FIRMWARE_VERSION@` (E30), `LSMinimumSystemVersion` 13.0, `LSArchitecturePriority` [arm64], `LSApplicationCategoryType` `public.app-category.music`, `NSHighResolutionCapable` true, `NSPrincipalClass` NSApplication, `NSHumanReadableCopyright` `Copyright (C) 2026 Tomasz Krzywicki. Unofficial. GPL-3.0-only.` No icon (E25), no `NSMicrophoneUsageDescription` (R5).

- [ ] **Step 8: CMake**, inside `if(LICHOMAT_EMU_APP)`:

```cmake
    set(EMU_IMGUI "${EMU_DIR}/third_party/imgui")
    if(NOT EXISTS "${EMU_IMGUI}/imgui.h")
        message(FATAL_ERROR "LICHOMAT_EMU_APP needs Dear ImGui in emu/third_party/imgui (plan R4 Task 11); "
            "configure with -DLICHOMAT_EMU_APP=OFF without it")
    endif()
    enable_language(CXX)
    enable_language(OBJCXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD 17)
    add_library(emu_imgui OBJECT "${EMU_IMGUI}/imgui.cpp" "${EMU_IMGUI}/imgui_draw.cpp" "${EMU_IMGUI}/imgui_tables.cpp"
        "${EMU_IMGUI}/imgui_widgets.cpp" "${EMU_IMGUI}/backends/imgui_impl_metal.mm" "${EMU_IMGUI}/backends/imgui_impl_osx.mm")
    target_include_directories(emu_imgui SYSTEM PUBLIC "${EMU_IMGUI}" "${EMU_IMGUI}/backends")	# -Werror stays ours
    target_compile_definitions(emu_imgui PUBLIC IMGUI_DISABLE_DEFAULT_FONT)	# E23
    target_compile_options(emu_imgui PRIVATE -w ${EMU_SAN_FLAGS} "SHELL:-ivfsoverlay ${EMU_OVERLAY}"
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    configure_file("${EMU_APP_DIR}/gui/Info.plist.in" "${CMAKE_BINARY_DIR}/Info.plist" @ONLY)
    add_executable(lichomat-emu MACOSX_BUNDLE "${EMU_APP_DIR}/gui/main.mm" "${EMU_APP_DIR}/gui/panel.cpp"
        "${EMU_APP_DIR}/gui/panel_font.c" $<TARGET_OBJECTS:emu_imgui> ${EMU_APP_OBJECTS})
    target_include_directories(lichomat-emu PRIVATE "${EMU_APP_DIR}/gui" "${EMU_APP_DIR}/mac" "${EMU_APP_DIR}/engine"
        "${EMU_DIR}/core" "${EMU_DIR}/common" "${CMAKE_BINARY_DIR}")
    target_include_directories(lichomat-emu SYSTEM PRIVATE "${EMU_IMGUI}" "${EMU_IMGUI}/backends")	# panel.cpp, main.mm: -Werror
    target_compile_definitions(lichomat-emu PRIVATE IMGUI_DISABLE_DEFAULT_FONT)
    target_compile_options(lichomat-emu PRIVATE ${EMU_APP_FLAGS} $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    set_target_properties(lichomat-emu PROPERTIES OUTPUT_NAME "LICHOMAT Emu"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist")
    target_link_libraries(lichomat-emu PRIVATE ${EMU_APP_FRAMEWORKS} "-framework Cocoa" "-framework Metal"
        "-framework MetalKit" "-framework QuartzCore" "-framework GameController" "-framework CoreText")
    add_dependencies(lichomat-emu emu_render_build)
    # The nested renderer and the signatures (spec 6.7, 9.2) as a command of their own, so that a change
    # of lichomat-render alone copies and signs again: a POST_BUILD of the app would not run then.
    set(B "$<TARGET_BUNDLE_CONTENT_DIR:lichomat-emu>")
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/app-bundle.stamp"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lichomat-render> "${B}/MacOS/lichomat-render"
        COMMAND codesign --force --sign - "${B}/MacOS/lichomat-render"	# the nested renderer first (spec 9.2)
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:lichomat-emu>"
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/app-bundle.stamp"
        DEPENDS lichomat-emu lichomat-render	# targets: rerun when either binary is relinked
        VERBATIM)
    add_custom_target(emu_app_bundle ALL DEPENDS "${CMAKE_BINARY_DIR}/app-bundle.stamp")
```

  If the link reports a symbol of another system framework, add that framework (system frameworks only; Task 13's `otool` check holds it to them).

- [ ] **Step 9:** Run EMU-CHECK. Expected: N = 212, 0 warnings; `M17_cli_info` passes with `F1-F18`. Then open the app: `open "build/emu/LICHOMAT Emu.app"` (a window server is needed; in the sandbox this may be refused, then the controller asks the owner). The splash shows for about 1 s, the panel answers mouse and keys, About lists the notices. Then the gate.

- [ ] **Step 10: Commit**; message `Emulator app: the Dear ImGui panel, the About box and the notices (E5.5 R4)`.

---

### Task 13: Bundle, signing, the licence gate, `check.sh` and CI (§9.2, §9.3, §10)

**Files:**
- Create: `emu/README.md`
- Modify: `emu/CMakeLists.txt`, `emu/tests/app_test.py`, `emu/tests/name_allow.txt` (only if Step 3 says so), `tools/check.sh`, `.github/workflows/emulator.yml`, `tools/ci_dry_run.rb`

- [ ] **Step 1: The README** that §10.3 and §14 (risks 7, 15 and 16) ask for, `emu/README.md`: an HTML comment with the GPL-3.0-only header lines, then this text. Apart from the tagline it names neither the maker nor the device (§10.2).

```markdown
# LICHOMAT Emu

emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02

LICHOMAT Emu runs the whole LICHOMAT firmware on a Mac with Apple silicon and macOS 13 or later: the
menu, the sequencer, MIDI sync and the sound engine, with a virtual panel, CoreAudio and CoreMIDI.
`lichomat-render` renders the same firmware into WAV files. Both are unofficial, and not affiliated
with the maker of the device.

## Opening a downloaded copy

The app and the renderer are signed ad hoc, not notarised, so macOS refuses their first start. Open
System Settings > Privacy & Security and choose Open Anyway, or remove the quarantine attribute:

    xattr -dr com.apple.quarantine "LICHOMAT Emu.app"

Building from source avoids the step.

## Building from source

With the Command Line Tools (`xcode-select --install`), CMake 3.21 or later and Ninja:

    cmake -S emu -B build/emu -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build/emu
    open "build/emu/LICHOMAT Emu.app"

## The card folder

The app works on its own folder, `~/Library/Application Support/LICHOMAT Emu/Card`, never on your
card. Import copies a card or a folder in, Export card and Export project write out, and every folder
they replace goes to `Card backups` first.

## How close it is

The emulator sounds very close to the device, not identical. Renders are bit-identical between builds
on a Mac, never with the device: floating point, the math library and the conversion to your audio
device's rate differ. The display is instant (the device needs about 51 ms for a full repaint), and
the CPU and the SD card take no time.

These parts are models, not measurements of the device: the display's font (a generic 5x8 font), the
knobs' wipers (about 134 steps per turn, 22 on a list), the order of the faders, the true sample rate
(44,140.625 Hz from the clock tree), flush-to-zero in the sound engine, the main loop's rate (a pass
every 250 us) and the timer during saves.

## Latency as a MIDI clock slave

The app delays MIDI and clock input by D, which its MIDI window shows: about 58 ms at the built-in
output's 512-frame buffer, about 34 ms at 128 frames. Set your DAW's MIDI clock sync delay to D, or
lower D at the cost of an uneven latency.

## Licence and source

GPL-3.0-only, with the additional permission and terms in NOTICE.md. The source is
https://github.com/isthar/lxr-desktop-app at the commit that About and `lichomat-render --version`
name. Third-party code: Dear ImGui (MIT), Arm Optimized Routines (MIT), FatFs `ff.h` (ChaN's licence).
```

- [ ] **Step 1b: The bundle's resources.** In the `app-bundle.stamp` command of Task 12, before the two `codesign` lines, and with the copied files added to its `DEPENDS`:

```cmake
        COMMAND ${CMAKE_COMMAND} -E make_directory "${B}/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy "${LXR_ROOT}/LICENSE" "${LXR_ROOT}/NOTICE.md" "${EMU_DIR}/README.md" "${B}/Resources/"
        COMMAND ${CMAKE_COMMAND} -E copy "${EMU_DIR}/libm/LICENSE" "${B}/Resources/LICENSE.arm-optimized-routines"
        COMMAND ${CMAKE_COMMAND} -E copy "${EMU_IMGUI}/LICENSE.txt" "${B}/Resources/LICENSE.dear-imgui"
        COMMAND /bin/sh -c "head -n 18 '${LXR_ROOT}/Middlewares/Third_Party/FatFs/src/ff.h' > '${B}/Resources/LICENSE.fatfs'"
```

  `R4_bundle` runs after `emu_app_bundle` (`ALL`), so its check sees the signed bundle.

- [ ] **Step 2: The bundle check** in `app_test.py` (a new option `--sanitized`, as `render_test.py`'s; registered inside `if(LICHOMAT_EMU_APP)` with `--app "$<TARGET_BUNDLE_DIR:lichomat-emu>" --sanitized ${emu_render_ubsan}`):

```python
def R4_bundle(t):
    contents = os.path.join(t.a.app, 'Contents')
    exe, render = (os.path.join(contents, 'MacOS', n) for n in ('LICHOMAT Emu', 'lichomat-render'))
    for b in (render, t.a.app):
        r = subprocess.run(['codesign', '--verify', '--strict', '--verbose=2', b], capture_output=True, text=True)
        check(r.returncode == 0, 'codesign %s: %s' % (b, r.stderr))
    plist = os.path.join(contents, 'Info.plist')
    for key, want in (('CFBundleIdentifier', 'io.github.isthar.lichomat-emu'), ('CFBundleExecutable', 'LICHOMAT Emu'),
                      ('CFBundleName', 'LICHOMAT Emu'), ('LSMinimumSystemVersion', '13.0')):
        got = subprocess.run(['plutil', '-extract', key, 'raw', plist], capture_output=True, text=True).stdout.strip()
        check(got == want, '%s is %r, not %r' % (key, got, want))
    for name in ('LICENSE', 'NOTICE.md', 'README.md', 'LICENSE.arm-optimized-routines', 'LICENSE.dear-imgui',
                 'LICENSE.fatfs'):
        check(os.path.isfile(os.path.join(contents, 'Resources', name)), 'Resources lacks ' + name)
    for b in (exe, render):
        archs = subprocess.run(['lipo', '-archs', b], capture_output=True, text=True).stdout.split()
        check(archs == ['arm64'], '%s is %r' % (b, archs))
        if t.a.sanitized != '1':  # a sanitizer build links the toolchain's runtime (spec 9.3)
            libs = subprocess.run(['otool', '-L', b], capture_output=True, text=True).stdout.splitlines()[1:]
            bad = [l.split()[0] for l in libs if not l.split()[0].startswith(('/usr/lib/', '/System/Library/'))]
            check(not bad, '%s links %r' % (b, bad))
    out = subprocess.run([render, '--version'], capture_output=True, text=True).stdout
    check('with the emulator changes F1-F18 and F6b.' in out, out)
```

- [ ] **Step 3: M21 with the app.** `EMU_GATE_BINARIES` gains `$<TARGET_FILE:lichomat-emu>`. Run `ctest --test-dir build/emu -R '^M21'`. A string from Dear ImGui or AppKit that holds the letters inside another word (for example 'numerical') is a false positive: add that exact run to `name_allow.txt` under a comment `# false positives in third-party strings, not names (plan R4 Task 13)` with its source file. A string that names the maker is a bug: fix it in our code.

- [ ] **Step 4: `tools/check.sh`.** In the header comment, step 2 becomes "the emulator (emu/, build/emu: Debug with UBSan) with LICHOMAT Emu.app and lichomat-emu-dev, and its ctest ids, on macOS on arm64 only". After the emulator's ctest, add `[ -d "$ROOT/build/emu/LICHOMAT Emu.app" ] && echo "== app: build/emu/LICHOMAT Emu.app"`. Nothing else changes; the warning count already covers the app's build.

- [ ] **Step 5: `emulator.yml`.** In `emu-release`, the step "Check the linked libraries" checks three binaries:

```yaml
      - name: Check the linked libraries
        run: |
          for b in build/emu-release/lichomat-render "build/emu-release/LICHOMAT Emu.app/Contents/MacOS/LICHOMAT Emu" \
                   "build/emu-release/LICHOMAT Emu.app/Contents/MacOS/lichomat-render"; do
            bad=$(otool -L "$b" | tail -n +2 | awk '{print $1}' | grep -v -E '^(/usr/lib/|/System/Library/)' || true)
            if [ -n "$bad" ]; then echo "$b links: $bad"; exit 1; fi
            echo "$b links only system libraries"
          done
```

  After "Upload lichomat-render", two steps:

```yaml
      - name: Package LICHOMAT Emu
        run: |
          d=build/emu-release/package-app
          rm -rf "$d"; mkdir -p "$d"
          codesign --verify --strict "build/emu-release/LICHOMAT Emu.app"
          ditto -c -k --keepParent "build/emu-release/LICHOMAT Emu.app" "$d/LICHOMAT-Emu-${GITHUB_SHA::12}.zip"
      - name: Upload LICHOMAT Emu
        uses: actions/upload-artifact@v7
        with:
          name: lichomat-emu-${{ github.sha }}
          path: build/emu-release/package-app/*.zip
          if-no-files-found: error
```

  In `emu-sanitize`, the configure line ends with `-DLICHOMAT_EMU_SANITIZE=address,undefined -DLICHOMAT_EMU_APP=OFF` (§9.3: without the app).

- [ ] **Step 6: `tools/ci_dry_run.rb`.** Three checks join the emulator hash:

```ruby
  "emu-sanitize builds without the app, emu-release with it" =>
    runs.call("emu-sanitize").match?(/-DLICHOMAT_EMU_APP=OFF(\s|$)/) && !runs.call("emu-release").include?("LICHOMAT_EMU_APP"),
  "emu-release uploads LICHOMAT Emu" =>
    uploads.call("emu-release").any? { |s| s["if"].nil? && s.fetch("with", {})["name"].to_s.start_with?("lichomat-emu-") &&
      s.fetch("with", {})["if-no-files-found"] == "error" },
  "the otool step checks the app and its renderer" =>
    find_step.call("emu-release", "Check the linked libraries").to_s.include?("LICHOMAT Emu.app/Contents/MacOS/lichomat-render"),
```

  The header comment's "Local differences for emulator.yml" needs no new line: the sanitizer job's `-DLICHOMAT_EMU_APP=OFF` runs locally as it is.

- [ ] **Step 7: The end check.**

```bash
cd /Users/tk/Code/lxr/build/wt-emulator && source tools/env.sh >/dev/null && tools/check.sh 2>&1 | tail -n 12 && ruby tools/ci_dry_run.rb emu-release 2>&1 | tail -n 5 && ruby tools/ci_dry_run.rb emu-sanitize 2>&1 | tail -n 5
```

Expected: `check.sh` with 57 host tests, 213 emulator ids, the image `OK`, 0 warnings; both dry runs `ok` on every check and `… steps passed`; the Release dry run 214 ids, the sanitizer one (UBSan locally) 205.

- [ ] **Step 8: Commit and push**; message `Emulator app: bundle with the nested renderer, ad hoc signing, M21, check.sh and CI (E5.5 R4)`; then `git push origin emulator`. The first CI run on GitHub builds the app, uploads it, and runs M18 and the app's units under ASan. A release (`emu-v0.1`) waits for the owner (§10.3).

---

## The owner's checks and the tests

| Check | The owner, by hand | Tests |
|---|---|---|
| A1 | Open Anyway; the splash for about 1 s; About's notices; the panel is no copy of the faceplate | `R4_engine_splash`, `R4_dev_headless` (1 s of wall time), `R4_bundle`, `M21` (names, licences), `R4_imgui_pin`, `R4_panel_font` |
| A2 | An imported copy of the card plays its projects and sounds like the device | `R4_card_import`, `R4_dev_import`, `R4_card_order`, `M18_render_mix`; the goldens (tier 1) |
| A3 | Steps per knob turn within 10 % of the device; fader order; encoder; a latched SHIFT | M8 (the core: 134 and 22 steps, faders); the panel's mapping by hand |
| A4 | Master at 120 BPM: DAW 120.0 ± 0.1; F8 20.83 ± 1.5 ms; start and stop | `M18_midi_out_step`, `M18_midi_usb_out`, `R4_dev_coremidi`; G5 (F8 counts) |
| A5 | Slave over the virtual port at 128 and 140 BPM: leads at most L, input latency within 1 ms of D; transport and SPP | `M18_slave_90/128/174`, `M18_latency_table`, `M18_eventlist`, `M18_reanchor_order`, `M18_entry_rule`; G8 |
| A6 | Slave over a DIN interface | `M18_din_128`, `M18_midi_bytes`; G7 |
| A7 | MIDI out over DIN, 7 tracks, 10 stops: no hanging note | `M18_midi_din_asm`; M10 (the wire); G6 |
| A8 | Clock-out LED 8 per second at 4 PPQ, 120 BPM; 16 clicks, 16 steps; the reset switch | `R4_engine_generator`; M12; G9 |
| A9 | Exports load on the device; export project keeps the other projects | `R4_card_export`, `R4_card_project`, `R4_card_order` |
| A10 | Power cycle keeps the DAW's routing; a crash note at the next launch | `R4_dev_power`, `R4_dev_crash`, `R4_crash_note`, `R4_settings_roundtrip`, `R4_settings_keep`, `R4_dev_coremidi` (stored IDs), `R4_bind` |
| A11 | 689.7 ± 0.1 blocks per second; load below 10 %; no underrun at a project load during playback | `R4_engine_ring`, `R4_engine_wall`, `R4_dev_headless`, `R4_dev_coreaudio` |
| M18 | — | every `M18_*` id: rings, map, L and D, entry rule, conversions, event lists, render, the slave scenarios |

## Points for the owner and the controller

- **Spec readings this plan made** (E-numbers above): D and the 100 ms drop (E9); DIN handed to CoreMIDI early with its stamp (E11), while §6.1's table says the MIDI-out thread sends DIN "at their presentation time"; D on the jack strip's button and switch (E13); the firmware-state line "F1-F18 and F6b" (E26), since §10.1's "F1–F12 and F6b" no longer names what runs; `NSMicrophoneUsageDescription` in R5, not R4 (E25).
- **Size.** The plan is longer than the 1,800 lines asked for: complete code for the interfaces, every test and the listed pure logic take about two thirds of it.
- **Not testable here:** the GUI itself (no window server in CI), the audio and MIDI hardware paths (A2–A11), and Open Anyway (A1). The device tests skip in CI.
- **Q14–Q16** stand as applied: the names, the schematic panel, D's default, the app's own card folder.

## Changes during execution (2026-10-31)

- **E32.** `diag.blockRate` comes from the audio time-map points: the frames between the newest point and
  the oldest point at least 2 s older, over their host-time difference, divided by 64. Counting whole
  blocks over 2 s read 690.1 on real CoreAudio against A11's 689.7 ± 0.1; the rate from the points
  reads 689.70. Wall pacing keeps the count.
- **E33**, the owner's decision of 10:13 and 10:16: the panel follows the device's control layout
  (jack strip on top, LCD with the 4 knobs and the encoder at the upper left, the faders and the bar and
  voice buttons at the right, the three button groups, the 16 steps at the bottom), with no device
  names, logos, printing, graphics or colours, and a 'LICHOMAT' wordmark in a system font loaded at run
  time (DIN Condensed Bold, then Futura, Impact, or the LCD's dot font). The VOLUME fader is left out:
  the firmware reads 6 faders.
- **The `time` clash.** `Core/Src/graphic.c` defines a global `float time`, which bound ImGui's call to
  libc `time()` and crashed the app at start. The emulator builds that file with `time=lxr_graphic_time`
  (the firmware image is unchanged), and `R4_gui_clash` fails on any firmware global that the app's own
  objects reference.
- **Start and F8.** The firmware sends an F8 1 ms after FA, whatever the phase of the free-running
  clocks, so the one F8 interval that contains Start is 0–20.8 ms. A4 leaves that interval out.
- **Reviews.** Three light reviews of the code (Tasks 1–4, 5–9, 10–12) on top of the plan review; their
  fixes added test ids, so the counts above are low: 254 ids in Debug after the fixes of the second
  review.
