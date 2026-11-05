# LICHOMAT plugin prototype (E9.2): VST3, an engine process per instance — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the prototype of draft §4 row E9.2 (`docs/superpowers/specs/2026-11-05-plugin-draft.md`): a VST3 instrument for Ableton Live on Apple Silicon, `LICHOMAT.vst3`, that plays the LICHOMAT firmware through the emulator's virtual device in an engine process per instance, with MIDI notes at their frames, sync to the host's transport through MIDI clock and Song Position, a card folder per instance, its state in the set, a reported latency, resampling to the host's rate, and a native mode that is `lichomat-render`'s output bit for bit.

**Architecture:** A new top-level `plugin/` CMake project builds the emulator's device objects and `lichomat-render` through `emu/CMakeLists.txt` (which stops after them behind a 4-line guard) and a pinned, vendored subset of the VST 3 SDK. `lichomat-engine` runs the renderer's prelude, then the tier hook waits for the plugin's published events and hands them to the device as USB transfers at their cycles; its audio goes through Apple's AudioConverter into a ring in shared memory. The plugin's audio thread turns the transport into MIDI clock (a pure function), writes events, publishes the block, wakes the engine with one non-blocking `write()`, and reads the ring a fixed offset behind; a monitor thread restarts a dead or hung engine.

**Tech Stack:** C11 and C++17 with Apple clang 21 (Command Line Tools 26.6, no Xcode), CMake 3.31, Ninja, ctest, the VST 3 SDK 3.8.1 build 84 (MIT; `base`, `pluginterfaces`, `public.sdk`, `cmake`), AudioToolbox, POSIX shared memory and pipes, Python 3 (scenario tests), bash.

| | |
|---|---|
| Date | 2026-11-05 |
| Draft | `docs/superpowers/specs/2026-11-05-plugin-draft.md` (Polish), §2 and §4 row E9.2; the brief of 2026-11-05 |
| Base | `v0.6` at `a856883` (worktree `build/wt-v06`); `build/wt-v07` is v0.7 in progress and is not touched |
| Branch, worktree | `plugin` from `a856883`, `build/wt-plugin`: Task 1 creates it. Task 11 works on `timing-core` in `/Users/tk/Code/lxr` |
| SDK | `build/vst3sdk` (git-ignored): `steinbergmedia/vst3sdk` tag `v3.8.1_build_84`, commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`, submodules `base` `fcf9da0`, `pluginterfaces` `4f547e8`, `public.sdk` `586dc5e`, `cmake` `054c914` (shallow; `doc`, `tutorials`, `vstgui4` not fetched). Nothing else is downloaded |
| Tasks | 11; Task N is tracker row **E9.(10+N)** (E9.11–E9.21), under the feature row E9.2 (one numbering, HANDOFF); plan decisions P-1–P-24 |
| Execution (lean) | one implementer per group: **A** Tasks 1–2, **B** 3–5, **C** 6–7, **D** 8–10, **E** 11. Full review (spec, then quality) after Task 6 (the engine) and after Task 9 (the plugin and its host); light review after group B and after Tasks 10–11 |
| Tests | 49 ctest ids in `build/plugin` at the end (P2–P8, P10, P11; label `plugin`); the emulator's 346 and the host tests' 125 unchanged |

**Everything in this plan was compiled and run while planning**, in a scratch copy of `a856883` (`git archive a856883 | tar -x`) with the SDK subset vendored as Task 2 does: Debug with UBSan and Release, the SDK's validator on the bundle, the test host, and every ctest id below (49 of 49 passed in Debug and in Release, "Measured while planning"). The code blocks are those files. What could not be checked here is Ableton Live itself: the owner's checks P1–P8 at the end.

## Decisions taken while planning

| # | Decision | Why |
|---|---|---|
| P-1 | **Branch `plugin` from `a856883`, worktree `build/wt-plugin`**; `plugin/` is new; outside it only `emu/CMakeLists.txt` changes (4 lines, Task 2). | The brief. The firmware, the emulator's app and v0.4–v0.7 stay as they are (draft header "Nie zmienia"). |
| P-2 | **`plugin/CMakeLists.txt` is a project of its own that adds `emu/` as a subdirectory with `LICHOMAT_EMU_DEVICE_ONLY` set; `emu/CMakeLists.txt` returns right after `lichomat-render` and `lichomat-render-test` when it is set** (a 4-line guard before "the test harness"). The plugin uses the targets `lxr_firmware`, `emu_core`, `emu_hostfs`, `emu_libm`, `emu_fw_headers` and `lichomat-render`. | The engine's firmware objects are then the renderer's, with the same flags (`-ffp-contract=off -fno-fast-math …`, the overlay, `-w`, `graphic.c`'s rename), in the same build type, which the bit-exact tests need (M2). Rejected: copying the flags into `plugin/` (drift), or adding all of `emu/` (its 317–346 test ids and the app would join the plugin's ctest). Without the variable `emu/` builds as before (Task 2 runs EMU-CHECK: 346). |
| P-3 | **The SDK is vendored**: `plugin/tools/vendor_vst3sdk.sh build/vst3sdk` checks the five pinned commits and copies the part the plugin builds with into `plugin/third_party/vst3sdk/` (341 files, 2.9 MB) with `SHA256SUMS`; test `P2_sdk_pin` checks every file. The VST logo (`cmake/templates/VST_Logo_Steinberg.ico`, Windows only) is left out. | GPL-3.0's corresponding source is then the repository itself, and CI and the owner build offline. The project already vendors with a pin (Dear ImGui `R4_imgui_pin`, the libm's `SHA256SUMS`). A submodule needs the network at every clone; copying at build time from a git-ignored folder leaves the source outside the repository. Draft §2.10: no VST logo. |
| P-4 | **The SDK's CMake is used through a glue file of ours, `plugin/third_party/CMakeLists.txt`** (the SDK's libraries `base`, `pluginterfaces`, `sdk_common`, `sdk`, `sdk_hosting`, the validator and `moduleinfotool`), not the SDK's top-level `CMakeLists.txt`. It sets `XCODE_VERSION` from the Command Line Tools (26.6), makes the SDK's include directories `SYSTEM` for our targets, and the top level forces `SMTG_ENABLE_VSTGUI_SUPPORT`, `SMTG_CREATE_PLUGIN_LINK` and `SMTG_RUN_VST_VALIDATOR` off. | F17–F20: the SDK's configure stops without Xcode ("XCode 9 or newer is required"); its default links every plugin into `~/Library/Audio/Plug-Ins/VST3` and **created that folder while planning** (removed again, it was empty); the top level adds folders the subset leaves out; clang 21 warns in `hostclasses.h` under our `-Werror`. A subdirectory keeps the SDK's `add_compile_options()` away from the firmware's objects. |
| P-5 | **The licence gate is the emulator's M21 with two additions** (`plugin/tests/plugin_licence_gate.py`, test `P11_licence`): `plugin/third_party/vst3sdk/` is on the allow-list (Steinberg, MIT), and the SDK's own compile commands need not carry the main.h overlay. Every other file must be GPL-3.0-only, every other command must have the overlay, and the name scan covers the plugin's binary, the engine, the test host and `plug_drive`. | M21 rejects any file without the SPDX line (planning run: 252 SDK files and 111 SDK commands before the additions; after them: 504 files, 126 commands, 4 binaries pass). The SDK's MIT notice goes into the bundle (`Contents/Resources/VST3-SDK-LICENSE.txt`, with `LICENSE` and `NOTICE.md`). |
| P-6 | **The shared memory is a POSIX shared memory object whose name is unlinked at once**; the plugin keeps its descriptor and gives it to each engine as **fd 5**. **fd 3** is the read end of a wake pipe (the plugin writes one byte per block, `O_NONBLOCK`; EOF tells the engine the plugin is gone), **fd 4** the write end of a life pipe that the engine holds and never writes (EOF tells the plugin's monitor thread the engine is gone, also after `SIGKILL`). | No name is left behind when either process dies (a named object would outlive a crashed Live until reboot); no `waitpid()` or `SIGCHLD` is needed to see a death, which a host may handle itself. Measured: EOF 0.28 ms after `SIGKILL` (M6). |
| P-7 | **Rings with 64-bit counters that only grow** (`plug_shm.h`): events (4,096 × 16 B: frame, 1–3 MIDI bytes) and audio (32,768 stereo float frames at the host rate). The plugin writes a block's events, then `hostFrames` (release); the engine writes frames, then `audioEnd`. A new engine sets `audioStart = audioEnd = epoch` and skips old events, so the plugin's side never resets anything. | One producer and one consumer each; restarting an engine needs no handshake with the audio thread. |
| P-8 | **The engine's timing:** the renderer's prelude for `--no-start --bpm mid --as-booted`, call for call; t0 at the first block request 100 ms after it, and `epoch = hostFrames` there. From then on **the tier hook waits until the plugin has published the events up to now + 66 engine frames** and hands every published event to the device: the messages of one frame as **one USB transfer** at `t0 + plug_framesToCycles(frame − epoch)`. | The hook runs at least every block (64 frames) and every 1 ms of a delay (`emu_core.h:27-35`), so no input comes late (`late 0` in every run). USB, not DIN: a transfer has one stamp for all its messages, at most 16 a ms (`emu_usb.c:58-145`), where DIN serialises bytes 320 µs apart (`emu_core.h:19-23`). Measured: the engine's output equals the renderer's bit for bit, with every input given at t0 or hook by hook (M2, M3). |
| P-9 | **Native mode is a host rate of exactly 44,140.625 Hz**, no switch: then nothing is resampled, a host frame is an engine frame, and the engine's output is the renderer's `--stereo sum`. Any other rate goes through AudioConverter. | The test host can ask for that rate (a VST3 rate is a double); Live never does. Nothing test-only in the plugin. |
| P-10 | **The stereo sum is (L1 + L2) / 65,536 and (R1 + R2) / 65,536** in float. | −6 dB of the device's two outputs added, without the renderer's rounding: floor(x × 32,768) is the renderer's `(L1 + L2) >> 1` exactly, which the tests use. |
| P-11 | **Resampling:** AudioConverter, float32 stereo interleaved, Normal complexity, maximum quality (the app's choice, `app_coreaudio.c:332-341`), prime method **None**, 64 engine frames a push. | With None every input frame gives its output at once and the delay is a constant 48 engine frames (M8); Normal priming holds 52 frames back (it needs look-ahead), Mastering adds 576–585 frames. |
| P-12 | **Latency** = sound delay + read offset. Sound delay: 128 engine frames (the voice queue) native, `round(176 × rate / 44,140.625)` resampled. Read offset: `maxBlock + margin + lag`, lag 1 native or `ceil(rate / 44,140.625) + 1`. Margin 128 host frames (`LICHOMAT_PLUGIN_MARGIN` 16–4096 overrides it). See "Latency". | The engine has a block's events only after that block; with the events up to frame H published it has written at least up to H − 1 engine frames. Measured exactly: onsets at the note's frame + latency (−1 on odd frames) (M12). |
| P-13 | **Transport → MIDI clock** (`plug_clock.c`, pure): clock k at position k/24 at the nearest frame; Start with the clock of position 0 (also after a count-in); from p > 0 Song Position ceil(4p) and Continue, clocks from that 16th; a jump (the block's position more than half a clock from the expected one, or the loop's end inside the block) is Stop, Song Position, Continue at its frame; Stop when the transport stops or loses its tempo or position; clocks only while playing; at a frame the transport's messages come before the notes. A new engine starts the slave afresh. | The slave takes a Song Position only while stopped and plays step s with the next clock after it (`seq_core.h:752-765`, `:625-641`), and Start arms it for step 0 at the next clock (`:625-626`). A loop that ends inside a block and one that the host splits at its end give the same messages at the same frames (`P4_clock_loop`). |
| P-14 | **Offline (`kOffline`: Live's bounce, freeze, export) the processor waits for its engine** (first until it runs, then until the frames it reads are written, 10 s at most each); in real time it never waits. | The draft (§1) expects bounce and freeze to work. Offline a host calls `process()` far faster than real time; without the wait the output was silent (planning run: 0 of 717,462 samples non-zero). |
| P-15 | **The engine's thread takes the app's time-constraint policy** (`app_mach.c:106-117`: the period of the host's block, a quarter of it to compute) **only when the host plays in real time** (`realtime` in the header, from the host's process mode). | Real time is where a late block is a dropout; one run of four paced engines under a heavy load had 282 underrun frames before the policy, none after (M16). Offline the engine computes without pause, longer than a quarter of each period, which the kernel may punish; forced on for an unpaced 14 s run it changed nothing measurable (0.899 s against 0.889 s), so this is caution. |
| P-16 | **The engine restarts:** the monitor thread sees a death (life pipe EOF) or a hang (state running, the published events 0.5 s ahead of what it took, for 2 s; not running after 30 s) and starts a new engine on the same card after 0.5 s; after 5 deaths in a minute, or a failure it reported, it gives up (state failed). | Draft §2.2: silence, then the engine again. Measured: about 0.7 s of silence after a `SIGKILL` (M15). |
| P-17 | **The state is the card folder as one archive** (`LCHS`, version 1, `plug_archive.c`): its directories and files in the card's order (birth time, then name, `emu_hostfs.h:38-42`), 8.3 names only; a restore makes each entry in that order and orders it last (`emu_hostOrderLast()`), into a fresh folder, and restarts the engine on it. A file that changes while it is read restarts the archive (3 times). | Draft §2.8 (the firmware reads directories in that order). The whole card, not the project folder alone: the firmware's current kit and pattern live in `/TMP`, and it is small (4,166 B for the onset card; 780 KB, 195 files, for a seeded init card). |
| P-18 | **Each instance works in `BASE/<16 hex digits>/`** (`card/`, `engine.log`, `plugin.log`, `lock` held with `flock()`); BASE is `$LICHOMAT_PLUGIN_HOME` or `~/Library/Application Support/LICHOMAT Plugin`. `terminate()` removes the folder; a folder whose lock nobody holds and older than a minute is a dead host's, removed by the next instance. **A new instance's card is a copy of `BASE/Card` if there is one, else of LICHOMAT Emu's card** (`$LICHOMAT_EMU_HOME/Card` or `~/Library/Application Support/LICHOMAT Emu/Card`), **else empty**, and the engine seeds the init card in a process of its own (`lichomat-engine --seed-card`, the renderer's `rfw_seedCard()`). `install_plugin.sh` puts the `init` fixture (four on the floor on all voices) into `BASE/Card` if it is missing. | Draft §2.8. The prototype has no window: without a card with patterns the owner could not hear the sequencer follow Live (the seeded init card's patterns are empty). The owner can replace `BASE/Card` with any card. Seeding in its own process because the firmware powers on once per process (`emu_firmware.c:66-74`). |
| P-19 | **One read-only parameter, "Engine"** (a list: stopped, starting, running, failed), reported as an output parameter change when it changes. No automation in the prototype. | The test host waits for "running" before its frame 0 (so the epoch is 0); the owner sees it in Live's device view if Live lists it. Draft §2.7 puts parameters later. |
| P-20 | **MIDI in: Note On and Note Off**, the host's channel kept, velocity round(v × 127), a Note On of velocity 0 sent as a Note Off. No CC, no Program Change. | VST3 delivers CCs and program changes as parameters (IMidiMapping), not events; the firmware's Program Change (F12) waits for E9.4's parameters. |
| P-21 | **Names:** the class "LICHOMAT" by "LICHOMAT", category `Instrument|Drum`, bundle `LICHOMAT.vst3`, identifier `org.lichomat.plugin`, version 0.1.0, no URL or e-mail in the factory, fixed class ids. | Draft §2.10: no maker's names; the name scan of M21 finds "lxr" in the repository's URL, so the factory has none. |
| P-22 | **The test host `lichomat-plughost`** (SDK hosting classes: `Module`, `PlugProvider`, `HostProcessData`, `EventList`, `ParameterChanges`, `MemoryStream`) plays a plan file: instances, a card or a state, a transport with a loop and tempo changes, notes, a kill, a renewal, real time or offline, paced or not. It also writes the MIDI the plugin must have given its engine, through the same `plug_clockBlock()`, as a renderer script. | One tool for the bit-exact comparison (P10_plugin_native: the renderer plays that script), the latency, the state, two instances, the renewal, the kill and the real-time check. |
| P-23 | **The real-time check is an interposer**, `libplug_rtcheck.dylib`, loaded into the test host with `DYLD_INSERT_LIBRARIES`: it counts allocation, locks, waits, reads, polls and sleeps made on the thread the host marks as inside `process()`, and `write()` apart. It checks itself first (one `malloc()` and one `free()` count 2). | Draft §3 wants the audio thread checked. The app has no such tool (F23); `__thread` cannot be used in the interposer (its first use allocates, which recursed while planning). |
| P-24 | **The plugin's process installs no signal handler; the engine installs the renderer's** (fault signals on an alternate stack, exit 3). The engine runs the firmware on its main thread with an 8 MB stack (`-stack_size 0x800000`). | Draft §2.2; `app_crash.c:108-133` is process-wide. The app gives the firmware 8 MB (`app_shell.c:37`). |

## Draft deviations and refinements

1. **Bounce, freeze and export need the offline wait** (P-14). The draft (§1) takes them for granted; without it they were silent.
2. **The latency target holds for host blocks up to 256 frames.** At 44.1 and 48 kHz the latency above the voice queue's 2.9 ms is 6.5–6.9 ms at 128 frames and 9.1–9.8 ms at 256, but 14.5–15.7 ms at 512 (table under "Latency"): the read offset holds a whole host block, because the engine has a block's events only after the block, and the audio thread must not wait for it. The draft's "≤ 10 ms above 2.9 ms" needs Live's buffer at 256 or less; E9.3 can measure whether a smaller margin holds.
3. **USB MIDI, not DIN** (P-8): exact stamps per transfer and no 320 µs serialisation.
4. **No "test mode" switch** (draft §2.5): the native rate is the test mode (P-9).
5. **The state stores the whole card**, not only the project folder from `/tmp` (P-17).
6. **A new instance starts on the plugin's own card or LICHOMAT Emu's** before the init card (P-18); the draft names only the per-instance folder.
7. **Draft §3 "checked with a blocking-detection tool, as the app's audio buffer"**: the app has no such tool (F23); the plan builds one (P-23).
8. **Program Change** (draft §2.7, "the plan will check what it does"): the firmware switches the pattern on a Program Change on the global channel with the PC filter, in its place among Start, Continue and Stop (F12). The prototype does not map it (P-20).
9. **Vendor name**: "LICHOMAT" (the draft's default); `PLUG_VENDOR` in `plug_notices.h` changes it.

## Facts found while planning

Read at `a856883` in `build/wt-v06` and in the SDK at `build/vst3sdk`; line numbers are of those trees.

- **F1 One firmware per process.** `emu_powerOn()` faults on a second call (`emu/core/emu_firmware.c:66-74`, `emu_core.h:118-121`); the emulator spec says a plugin needs its own process per instance (`docs/superpowers/specs/2026-10-27-emulator-design.md:1887-1890`).
- **F2 The scheduler.** `emu_runUntil(t)` boots on its first call and returns at the first pass boundary at or after t (`emu/core/emu_sched.c:485-520`); `emu_directCall()` starts a new `HAL_GetTick()` segment (`:522-533`, `:447-451`); inputs sit in a min-heap with exact times, a time before now enters now and counts as late (`:241-273`); the hook runs after every block request (`:351-357`) and at every 1 ms while `HAL_Delay()` or a spin advance the clock (`:410-425`, `emu_core.h:27-35`). `emu_framesToCycles()` is floor(frames × 552,960 / 113) (`emu_sched.c:62-66`).
- **F3 The codec.** A block request every 64 frames at `codecStart + emu_framesToCycles(64k)`; the sink gets the block that starts playing then, out 1 (L1, R1) and out 2 (L2, R2) interleaved (`emu/core/emu_codec.c:28-50`, `emu_core.h:70-84`).
- **F4 The renderer's timeline.** Boot, a settle of 100 ms with `rfw_morphBusy()` after every pass, `rfw_bpm()`, the `--param` edits, `rfw_selected()`, then t0 at the first block request that meets the settle rule; the script's inputs are given in the t0 hook; `--stereo sum` writes `(out1 + out2) >> 1` (`emu/render/render_main.c:355-380`, `:762-800`, `:1005-1080`, `render_out.c:160-163`). Script times take `Nf` frames (`render_script.c:101-125`) or seconds with up to 6 decimals (whole µs, 216 cycles). `--as-booted` keeps `/TMP` in the card's copy (`render_card.c:159-160`).
- **F5 USB in.** One transfer is one OTG body with one stamp for all its messages; at most 16 packets enter in a 1 ms window that starts at its first packet (`emu/core/emu_usb.c:58-145`); transfers keep their order (`:172-173`).
- **F6 The sync queue.** Start, Continue, Stop, clocks and Song Positions are drained in order in `seq_tick()` (`Core/Src/Soundengine/Sequencer/sequencer.c:2410-2453`, `:2506-2536`); a Song Position is taken in the slave modes while stopped or armed (`Core/Inc/Soundengine/Sequencer/seq_core.h:752-765`); Start arms for step 0, Continue resumes one clock after the last position (`:625-641`); clocks with one stamp form one point of the tempo fit (`:657-661`); the slave never runs ahead of the last clock plus one (`:646-652`).
- **F7 Sync mode.** `rfw_bpm(1)` is the menu edit `PAR_BPM = 1`, MIDI clock (`emu/render/render_fw.c:248-273`); `--bpm mid` in the renderer.
- **F8 Default MIDI.** All seven voices listen on channel 1 to note 48 (`Core/Src/Preset/presetManager.c:1257-1275`); the RX filter passes everything (`:1287`). A Note On of note 48 on channel 1 at frame f sounds at f + 128 (planning render of the onset card: 4,410 → 4,538).
- **F9 The voice grid.** An event sounds on an even frame: shifting an input by one frame changed nothing, by three frames changed 10,885 bytes of a 14 s render (planning run).
- **F10 Seeding.** `lichomat-render --seed-card` boots, runs 100 ms and calls `rfw_seedCard()` (`render_main.c:328-350`, `render_fw.h:24-28`); the app runs it as a child (`emu/app/engine/app_card.c:116-138`).
- **F11 Faults.** `emu_fault()` calls the configured handler or prints and aborts (`emu_sched.c:147-157`); the renderer installs fault-signal handlers on an alternate stack and sanitizer options that abort (`render_main.c:177-221`); the app's are process-wide (`emu/app/engine/app_crash.c:108-133`).
- **F12 Program Change.** Taken on the global channel (`midi_MidiChannels[7]`) with `MIDI_FILTER_PROG_C`, switching the pattern in its place among Start, Continue and Stop (`sequencer.c:2423-2436`, `seq_syncq.h:48`).
- **F13 The card's POSIX half.** `emu_hostList()` lists in the card's order and skips names that are not 8.3; `emu_hostOrderLast()` and `emu_hostCopyCard()` keep the order through birth times (`emu/core/emu_hostfs.h:23-70`, `emu_hostfs.c:185-230`). It includes no firmware header and has no `extern "C"` guards.
- **F14 The build.** `emu/CMakeLists.txt`: arm64 and Apple clang only (`:19-30`), the flags (`:81-91`), the device objects (`:109-153`), the renderer (`:155-191`), "the test harness" from `:193`. The licence gate (`emu/tests/licence_gate.py`): files, the overlay on every compile command except the libm's, the name scan of the binaries (`:1-199`); `emu/common/emu_notices.h` holds the allow-listed notices.
- **F15 The app's real-time policy and converter.** `app_threadRealtime()` (`emu/app/mac/app_mach.c:106-117`); the HAL unit's converter at Normal complexity and maximum quality (`app_coreaudio.c:332-341`).
- **F16 The firmware's stack.** The app gives its firmware thread 8 MB, the main thread's size (`emu/app/mac/app_shell.c:37`).
- **F17 The SDK without Xcode.** `SMTG_DetectPlatform.cmake:41-61` runs `xcodebuild -version` unless `ENV{XCODE_VERSION}` is defined and stops below 9; the Command Line Tools are 26.6 (`pkgutil --pkg-info=com.apple.pkg.CLTools_Executables`). `XCODE_VERSION` decides only the ad hoc signing step of Xcode 15 and later (`public.sdk/samples/vst-hosting/validator/CMakeLists.txt:146-154`).
- **F18 The SDK's plugin links.** `SMTG_CREATE_PLUGIN_LINK` defaults on and makes `~/Library/Audio/Plug-Ins/VST3` at configure time (`cmake/modules/SMTG_AddVST3Library.cmake:117-132`, `SMTG_AddCommonOptions.cmake:26`); `SMTG_RUN_VST_VALIDATOR` defaults on (`SMTG_AddVST3Library.cmake:7-11`).
- **F19 The SDK's platform setup** adds compile options with `add_compile_options()` in its directory (`cmake/modules/SMTG_PlatformToolset.cmake`); in a subdirectory they do not reach the firmware (the planning build's 73 firmware commands carry `-fno-fast-math` and no `-ffast-math`).
- **F20 The bundle.** `smtg_add_vst3plugin()` needs `PROJECT_VERSION`, `public_sdk_SOURCE_DIR` in the caller's scope (`SMTG_AddSMTGLibrary.cmake:188-205`, `:240-255`), adds `macmain.cpp` (the `bundleEntry`), runs `moduleinfotool` after the link, and signs only `moduleinfo.json`; without a plist of ours the bundle's identifier is empty and its type `APPL`. The validator needs `public.sdk/samples/vst-hosting/audiohost/source/media/miditovst.h`.
- **F21 Live.** Live 12 on macOS loads AU, VST2 and VST3, not CLAP (the brief).
- **F22 Counts at the base.** 125 host ids, 346 emulator ids (317 without the app), 38 goldens (v0.7 plan F1).
- **F23 The app's audio ring** is a lock-free SPSC ring with a two-thread test (`emu/app/engine/app_ring.h:4`, `emu/tests/test_app_ring.c:85-101`); no tool checks the audio thread for blocking calls.

## Measured while planning

In `/private/tmp/claude-501/-Users-tk-Code-lxr/96b76bdc-1e78-46dc-a0e8-44d89ec424a5/scratchpad/plugin-plan/` on this Mac. Another heavy job (an emulator fuzz sweep with ASan) ran at the same time for much of it, load average 12–19: the timing cases are serial for that reason (Rules).

- **M1 The SDK.** A copy of `build/vst3sdk` configured with `XCODE_VERSION=26.0` (both the variable and the environment), VSTGUI off, links off; `validator` and `again-sample-accurate` built in 5.6 s; the validator on `again-sample-accurate`: `Result: 112 tests passed, 0 tests failed`; on `mda-vst3` (instruments): `1598 tests passed, 0 tests failed`. The SDK's own sources give 4 warnings (`fstring.cpp:3371`, `wstring_convert` deprecated ×3).
- **M2 The engine is the renderer.** A separate executable linking the device objects and `render_fw.c`, with the renderer's prelude: its stereo sum of a 14 s script (USB clock at 128 then 140 BPM with Start, a loop jump Stop + Song Position 16 + Continue, notes, 617,984 frames) is `lichomat-render --card onset --bpm mid --no-start --as-booted --stereo sum`'s byte for byte; t0 1.200 s (cycle 259,218,962); 91x real time (Release).
- **M3 Input by hook.** The same with every input given in the hook that precedes it by 65 frames, instead of all at t0: byte for byte the same. Blocks of 1, 37, 128, 512, 4,096 host frames: the same.
- **M4 Sensitivity.** Start one frame later: identical (the even-frame grid, F9); three frames later: 10,885 bytes differ.
- **M5 The ring.** Two processes, 2,000,000 events and frames, checked in order: 0.013 s; the ring full 27,094 times without a loss.
- **M6 A death.** The life pipe gives EOF 0.28 ms after `SIGKILL`.
- **M7 The engine.** Debug with UBSan 15–16x real time, Release 127x; from spawn to running 0.06 s, 0.66 s with seeding.
- **M8 The converter** (Normal, maximum quality, prime None), an impulse at frame 1,000: delay 47.94, 52.27, 95.90, 104.38 output frames at 44.1, 48, 88.2, 96 kHz, i.e. 47.99–48.07 engine frames; leading frames reported 48–49; prime Normal: 0 delay but 52 frames held back; Mastering: 576–585 frames.
- **M9 Resampled = the renderer converted.** At 44.1, 48 and 96 kHz the engine's output equals the renderer's for the same input times through the same converter within 1 LSB (6 s each).
- **M10 The skeleton and the bundle.** A skeleton instrument through `smtg_add_vst3plugin()` in our project: validator `47 tests passed, 0 tests failed`; the final `LICHOMAT.vst3` (engine in `Contents/Helpers`): the same, 7.2 s, no folder or engine left; `codesign --verify --deep --strict`: valid, also after `ditto`.
- **M11 The plugin is the renderer.** Through the test host, offline, at 44,140.625 Hz in blocks of 128 and of 100: from frame latency − 128 on, 529,471 frames equal the renderer's for the MIDI the plugin gave its engine (5 loop jumps, a tempo change, notes); latency 385 and 357.
- **M12 Latency exact.** Ten isolated notes: onset − note frame = 385 or 384 (blocks of 128), 590 or 589 (333): the reported latency, −1 on odd frames. The init card's four on the floor as the slave of a looped transport (5 loops of a bar, 120 then 140 BPM): all 22 beats, computed in Python without `plug_clock`, at beat + latency −1 to +1.
- **M13 48 kHz.** Blocks of 512: the plugin's output from frame 643 on equals the engine's through `plug_drive` for the same MIDI, byte for byte; latency 834 = 191 + 643.
- **M14 State and instances.** Two instances in one host each equal one instance; a state (4,166 B) restored by a new host equals it; a renewed instance sounds 4,713 frames after its start; no instance folder left.
- **M15 A kill.** Real time, paced, 48 kHz: after `SIGKILL` at 3.0 s the output is silent until 3.6–3.75 s, then plays again.
- **M16 Real time.** Paced 10 s at 48 kHz, blocks of 128, under the interposer: 3,750 `process()` calls, 0 forbidden calls, 3,750 writes. Engines paced by `plug_drive realtime` at 48 kHz, blocks of 128, margin 128, 10 s each: 0 underruns in 2 runs of one engine and 6 runs of four at once; one earlier run of four, under the heavy load and before P-15, had 282 underrun frames in one engine.
- **M17 Sizes.** The bundle 920 KB (plugin 359 KB, engine 519 KB, Release); the vendored subset 341 files, 2,932 KB.
- **M18 All tests.** `plugin/tools/plugin_check.sh` from clean build folders: Debug with UBSan 49 of 49 in 39 s, Release 49 of 49 in 45 s (`-j4`, the timing cases serial); our files 0 compiler warnings in both. A planning bug found by it: `plug_drive` first throttled on `frontier`, which follows `hostFrames`, so it ran ahead without bound and the engine played 28 times the frames asked (a 2-s case took 52 s); it throttles on `audioEnd`.

## Latency

An event at host frame m reaches the firmware at its cycle (P-8) and sounds 128 engine frames later (the voice queue, `tools/emu_onsets.py:43-45`), 48 more through the converter when resampled (M8). The plugin plays the ring `plug_readOffset()` frames behind the frame it outputs: the engine gets a block's events only once that block is published, and with the events up to frame H it has written the ring up to at least H − 1 engine frames (a hook at block request k needs the events up to frame 64k + 66 and is followed by block k + 1, frames 64k + 64 to 64k + 127). The margin is wall-clock slack for the engine's wake-up and computation.

```
latency     = sound delay + read offset                                 (plug_latency(), P5_time_latency)
sound delay = 128                     at 44,140.625 Hz (native)
            = round(176 x rate / 44,140.625)       resampled             (128 + the converter's 48)
read offset = maxBlock + margin + 1                native
            = maxBlock + margin + ceil(rate / 44,140.625) + 1   resampled
margin      = 128 host frames, LICHOMAT_PLUGIN_MARGIN (16-4096) overrides it
```

| Host rate | Block | Sound delay | Read offset | Reported | ms | above the queue's 2.90 ms |
|---|---:|---:|---:|---:|---:|---:|
| 44,140.625 (test host) | 128 | 128 | 257 | 385 | 8.72 | 5.82 |
| 44,100 | 128 | 176 | 258 | 434 | 9.84 | 6.94 |
| 44,100 | 256 | 176 | 386 | 562 | 12.74 | 9.84 |
| 44,100 | 512 | 176 | 642 | 818 | 18.55 | 15.65 |
| 48,000 | 128 | 191 | 259 | 450 | 9.38 | 6.48 |
| 48,000 | 256 | 191 | 387 | 578 | 12.04 | 9.14 |
| 48,000 | 512 | 191 | 643 | 834 | 17.38 | 14.48 |
| 96,000 | 256 | 383 | 388 | 771 | 8.03 | 5.13 |

- **Set:** `setupProcessing()` computes it from the rate, `maxSamplesPerBlock` and the margin; `getLatencySamples()` returns it; it changes only with a new setup, which the host follows with a new query (no `restartComponent()` needed).
- **Measured:** `P10_plugin_latency` (native: each onset at the note's frame + the reported latency, −1 on odd frames, blocks of 128 and 333), `P10_plugin_onsets` (the sequencer's beats through loops and a tempo change at beat + latency, −1 to +1), `P10_plugin_resampled` (48 kHz: the read offset exactly, against `plug_drive`), `P7_converter_delay` (the converter's 48 ± 0.5 engine frames at 44.1, 48, 88.2, 96 kHz), and the owner's P7 in Live.
- **Target:** ≤ 10 ms above the queue's 2.90 ms holds at host blocks of up to 256 frames (draft deviation 2).
- **If the engine needs more margin:** underruns are counted in the shared memory (`underruns`) and written into the instance's `plugin.log` when an engine ends. `P7_engine_realtime` (four engines paced at 48 kHz) and `P10_plugin_kill`/`P10_plugin_rt` run serially. If `P7_engine_realtime` shows underruns on an otherwise idle machine, raise `PLUG_MARGIN_DEFAULT` by 64 (with `P5_time_latency`'s values, the table above and `plugin/README.md`), up to 512; beyond 512 it is a finding to report, not a margin to raise. The owner's P8 asks for `plugin.log` if he hears dropouts.

## Real-time safety of the audio thread

`Processor::process()` in real time:
- reads the engine's state and pid (atomic loads), reports a change of the state as an output parameter change;
- turns the transport into at most 1,024 MIDI messages (`plug_clockBlock()`, a pure function) and the block's notes into at most 1,024 (`notesOf()`, into a fixed array of the processor);
- writes them into the event ring (stores and one release store each), publishes the block (a release store), wakes the engine with **one non-blocking `write()` of one byte** to a pipe (`EngineLink::wake()`; a full pipe already holds a pending wake-up);
- copies its frames out of the audio ring (loads), or silence.

No allocation, no lock, no wait, no other system call; the memory is mapped and zeroed (resident) in `setActive(true)`, before the first `process()`. Everything else (spawning, `mmap()`, files, logs, the card) is on the host's main thread or the plugin's monitor thread. Offline (`kOffline`), `process()` waits with `nanosleep()` by design (P-14).

**The check:** `P10_plugin_rt` plays 10 s paced in real time at 48 kHz in blocks of 128 through the test host with `libplug_rtcheck.dylib` interposed (P-23): `malloc`, `calloc`, `realloc`, `free`, `pthread_mutex_lock`, `pthread_cond_wait`, `pthread_cond_timedwait`, `pthread_rwlock_rdlock`, `os_unfair_lock_lock`, `sem_wait`, `nanosleep`, `usleep`, `read`, `poll`, `select`, `dispatch_sync` made on the thread the host marks as inside `process()` count as forbidden, and `write` is counted apart. Expected: 3,750 calls, 0 forbidden, 3,750 writes. The interposer checks itself first (a `malloc()` and a `free()` inside count 2); its exit report includes those two.

## Rules for every task

- **Where.** Worktree `/Users/tk/Code/lxr/build/wt-plugin`, branch `plugin` from `a856883`. Every Bash call starts in a new shell: begin each command with `cd /Users/tk/Code/lxr/build/wt-plugin &&`, and add `source tools/env.sh >/dev/null &&` where it needs `cmake`, `ctest` or `ninja`. Task 11 works in `/Users/tk/Code/lxr` (branch `timing-core`). Never touch `build/wt-v07` (v0.7 in progress) or any other worktree.
- **Process (lean).** One implementer per group (A: 1–2, B: 3–5, C: 6–7, D: 8–10, E: 11). After Task 6 and after Task 9: a spec review, then a quality review; after group B and after Tasks 10–11: a light review. Each reviewer runs at least five mutants of the new code (a comparison flipped, a constant off by one, a line removed) and each must fail a test; a mutant that survives is a test gap, closed in its own commit before the next group starts.
- **Scope.** Read only what a step names; grep and read line ranges. No prototypes outside the task. The code below was compiled and tested while planning: treat a compile error as a finding to note under "Changes during execution", not as a reason to redesign.
- **No network.** Nothing is downloaded: no `git clone`, `curl`, `pip`, `brew`. The SDK comes from `build/vst3sdk` through `plugin/tools/vendor_vst3sdk.sh`. If something more seems needed, stop and report it.
- **The user's folders.** Tests set `LICHOMAT_PLUGIN_HOME` and `LICHOMAT_EMU_HOME` to their own folders. Nothing writes into `~/Library/Audio/Plug-Ins/VST3` or `~/Library/Application Support/LICHOMAT Plugin` except Task 10's install step; `SMTG_CREATE_PLUGIN_LINK` stays off. After every task `ls ~/Library/Audio/Plug-Ins` shows what it showed before Task 1 (at planning: `Components Digidesign VST`).
- **Files.** New files carry the GPL-3.0-only header of the LICHOMAT files (`Copyright (C) 2026 Tomasz Krzywicki`, the SPDX line, the NOTICE.md sentence; `#` comments in scripts and CMake, `<!-- -->` in Markdown), as every file below shows. The vendored SDK keeps Steinberg's headers. `plugin/`, `emu/`, `tools/`, `docs/` are LF.
- **Names.** No string of the plugin, the engine or the test host contains "LXR", "Erica" or "Sonic Potions" outside `emu_legal` (allow-listed in `emu/tests/name_allow.txt`); `P11_licence` scans them. Never name the reference album or its artist.
- **Warnings.** Our files: 0 compiler warnings in Debug and Release (`-Wall -Wextra -Werror`); the SDK's own files warn (4 lines: `fstring.cpp`, `wstring_convert`), which the gate leaves out.
- **Timing.** `P7_engine_realtime`, `P10_plugin_kill` and `P10_plugin_rt` are paced by the wall clock and run serially (`RUN_SERIAL`). Run a gate when no other heavy job (an emulator fuzz sweep, another build) runs; a timing case that fails under load is run once more alone before it counts as a finding.
- **Test values.** Never `time()`; record measured values in the commit message; never widen a tolerance once committed.
- **Git.** `git add` names each file (the vendored SDK by its folder, `plugin/third_party/vst3sdk`); never `git add -A`, never `--amend`. Commit messages are `plugin Task N (E9.(10+N)): …` and end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. After a task's commit, `git push origin plugin` (Task 2: `git push -u origin plugin`); never push anything else, never force-push, never touch `main`.
- **Ableton.** The checks in Live (P1–P8) are the owner's. Never mark one as done.

**PLUGIN** (one regex while working; without `-R` every id):

```bash
cd /Users/tk/Code/lxr/build/wt-plugin && source tools/env.sh >/dev/null && cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Debug > build/plugin.log 2>&1 && ninja -C build/plugin >> build/plugin.log 2>&1; echo "== plugin build: $?"; echo "== plugin warnings: $(grep 'warning:' build/plugin.log | grep -v -e '/plugin/third_party/vst3sdk/' -e '/CommandLineTools/' | wc -l | tr -d ' ')"; ctest --test-dir build/plugin -j4 -R 'REGEX' --output-on-failure 2>&1 | grep -E 'tests passed|\*\*\*|Failed|Errors'
```

**PLUGIN-CHECK** (the gate of every task from Task 2 on: Debug with UBSan and Release, every id):

```bash
cd /Users/tk/Code/lxr/build/wt-plugin && plugin/tools/plugin_check.sh
```

Expected: for each of Debug and Release `100% tests passed, 0 tests failed out of T` with the task's T, and `== compiler warnings, …, our files: 0`; exit 0.

**EMU-CHECK** (Tasks 1, 2 and 10):

```bash
cd /Users/tk/Code/lxr/build/wt-plugin && source tools/env.sh >/dev/null && cmake -S emu -B build/emu -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLICHOMAT_EMU_SANITIZE=undefined > build/emu.log 2>&1 && ninja -C build/emu >> build/emu.log 2>&1; echo "== emu build: $?"; echo "== emu warnings: $(grep -c 'warning:' build/emu.log)"; ctest --test-dir build/emu -j8 2>&1 | tail -n 3
```

Expected: `== emu build: 0`, `== emu warnings: 0`, `100% tests passed, 0 tests failed out of 346`.

**CHECK** (Tasks 1 and 10): `cd /Users/tk/Code/lxr/build/wt-plugin && source tools/env.sh && tools/check.sh`, expected as at `a856883` (v0.7 plan F1, F2): `100% tests passed, 0 tests failed out of 125`, the emulator's 346, `== compiler warnings: 0`, `budget: DTCM+RAM 469132/471032 B, DTCM 107396/107900 B, RAM 361736/389120 B OK` and the image line `OK`. The plugin branch changes no firmware file.

## File structure

| File | Responsibility | Task |
|---|---|---|
| `emu/CMakeLists.txt` | 4 lines (and 2 of comment): `return()` after the renderer when `LICHOMAT_EMU_DEVICE_ONLY` | 2 |
| `plugin/tools/vendor_vst3sdk.sh`, `plugin/third_party/vst3sdk/` (341 files and `SHA256SUMS`), `plugin/third_party/CMakeLists.txt`, `plugin/tests/sdk_pin.py` | the vendored, pinned SDK and its CMake glue | 2 |
| `plugin/CMakeLists.txt` | the project; a block per task | 2–10 |
| `plugin/tools/plugin_check.sh` | the gate | 2 |
| `plugin/common/plug_shm.{h,c}`, `plugin/tests/test_shm.c` | shared memory: header, event ring, audio ring | 3 |
| `plugin/common/plug_clock.{h,c}`, `plugin/tests/test_clock.c` | the transport as MIDI clock | 4 |
| `plugin/common/plug_time.{h,c}`, `plugin/tests/test_time.c` | frames, cycles, latency | 5 |
| `plugin/common/plug_spawn.{h,c}`, `plugin/common/plug_notices.h`, `plugin/engine/eng_main.c`, `eng_run.{h,c}`, `eng_resample.{h,c}`; `plugin/tests/plug_drive.c`, `plug_resample_wav.c`, `test_resample.c`, `plugin_test.py` | `lichomat-engine`, its driver and its tests | 6 |
| `plugin/common/plug_archive.{h,c}`, `plugin/tests/test_archive.c` | the card as one archive | 7 |
| `plugin/vst3/plugin_ids.h`, `plugin_card.{h,cpp}`, `plugin_link.{h,cpp}`, `plugin_processor.{h,cpp}`, `plugin_controller.{h,cpp}`, `plugin_factory.cpp`, `Info.plist.in`; `plugin_test.py` | `LICHOMAT.vst3` | 8 |
| `plugin/host/plug_host.cpp`, `plugin/tests/plug_rtcheck.c`; `plugin_test.py` | the test host, the real-time check, the scenarios | 9 |
| `plugin/tests/plugin_licence_gate.py`, `plugin/tools/install_plugin.sh`, `plugin/README.md`; `plugin_test.py` | the licence gate, the install, the README | 10 |
| (timing-core) `docs/lichomat/plugin-checks.md`, `docs/lichomat/HANDOFF.md`, `docs/lichomat/tracker.json` | the owner's checks P1–P8, the handoff, the tracker | 11 |

## Task order

| Task | Contents | Plugin ids (T) | Group, review |
|---|---|---:|---|
| 1 | Worktree, baseline | — | A |
| 2 | The SDK vendored and pinned, the project, the emulator's guard: `P2_sdk_pin` | 1 | A |
| 3 | Shared memory: `P3_*` | 8 | B |
| 4 | Transport → MIDI clock: `P4_*` | 21 | B |
| 5 | Frames, cycles, latency: `P5_*` | 26 | B; light review |
| 6 | `lichomat-engine`, native and resampled: `P6_*`, `P7_*` | 35 | C; full review |
| 7 | The card archive: `P8_*` | 38 | C |
| 8 | `LICHOMAT.vst3`: `P10_plugin_validator` | 39 | D |
| 9 | The test host, the real-time check, the scenarios: `P10_*` | 48 | D; full review |
| 10 | The licence gate, README, install, the final gates: `P11_licence` | 49 | D; light review |
| 11 | The owner's checks P1–P8, handoff, tracker (timing-core) | — | E; light review |

---

### Task 1: Worktree and baseline — E9.11

**Files:** none (the worktree only).

- [ ] **Step 1: the worktree** (P-1).

```bash
cd /Users/tk/Code/lxr && git worktree list | grep -c wt-plugin; git branch --list plugin | wc -l
```

  Expected: `0` and `0`. Then:

```bash
cd /Users/tk/Code/lxr && git worktree add build/wt-plugin -b plugin a856883 && ln -s /Users/tk/Code/lxr/.toolchain build/wt-plugin/.toolchain && git -C build/wt-plugin log --oneline -1 && git -C build/wt-plugin status --short
```

  Expected: `a856883 E4.13 review fixes: B17 decides the data bytes by the status, B12's comment, B11's test without UBSan` and `?? .toolchain` (the link is never staged).
- [ ] **Step 2: the SDK checkout** (it is git-ignored in `/Users/tk/Code/lxr/build/vst3sdk`; nothing is fetched):

```bash
cd /Users/tk/Code/lxr/build/vst3sdk && git rev-parse HEAD && git submodule status && ls ~/Library/Audio/Plug-Ins
```

  Expected: `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`; ` fcf9da0bd27a16f7f03773a3a39822f28f5c8477 base (v3.8.1_build_84)`, ` 054c9143cbb8d47fc4694e473f2ee3b4d951a8f5 cmake (v3.8.1_build_84)`, `-8bfca19… doc`, ` 4f547e8e102b47de4a8b8aaf343c73b700786372 pluginterfaces (v3.8.1_build_84)`, ` 586dc5e6c8012c3e4b01c79389375cbe96bdb1da public.sdk (v3.8.1_build_84)`, `-33b73df… tutorials`, `-5db2722… vstgui4`; record the `ls` line (at planning `Components Digidesign VST`: the rule "The user's folders" compares against it).
- [ ] **Step 3: the baseline.** CHECK and EMU-CHECK. Expected as in the Rules (125 host ids, 346 emulator ids, the budget line, `OK`). A different number is a finding to report before Task 2. No commit: nothing changed.

---

### Task 2: The SDK vendored and pinned, the project, the emulator's guard — E9.12

**Files:** Modify `emu/CMakeLists.txt` (before `# --- the test harness`, `:193`). Create `plugin/tools/vendor_vst3sdk.sh`, `plugin/third_party/vst3sdk/` (by the script), `plugin/third_party/CMakeLists.txt`, `plugin/CMakeLists.txt`, `plugin/tests/sdk_pin.py`, `plugin/tools/plugin_check.sh`.

- [ ] **Step 1: the failing pin test.** `plugin/tests/sdk_pin.py`:

```python
#!/usr/bin/env python3
"""P2: the vendored VST 3 SDK is the pinned subset (plan E9.2 Task 2): every file of
plugin/third_party/vst3sdk is listed in its SHA256SUMS with that hash, and SHA256SUMS lists no
file that is missing. plugin/tools/vendor_vst3sdk.sh made both from Steinberg's vst3sdk at
v3.8.1_build_84 (commit 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96).

    sdk_pin.py <repository root>

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import hashlib
import os
import sys


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    root = os.path.join(sys.argv[1], 'plugin', 'third_party', 'vst3sdk')
    want = {}
    with open(os.path.join(root, 'SHA256SUMS')) as f:
        for line in f:
            digest, path = line.rstrip('\n').split('  ', 1)
            want[path] = digest
    have = {}
    for d, _, files in os.walk(root):
        for name in files:
            path = os.path.relpath(os.path.join(d, name), root)
            if path == 'SHA256SUMS':
                continue
            with open(os.path.join(d, name), 'rb') as f:
                have[path] = hashlib.sha256(f.read()).hexdigest()
    errors = ['%s is not in SHA256SUMS' % p for p in sorted(set(have) - set(want))]
    errors += ['%s is missing' % p for p in sorted(set(want) - set(have))]
    errors += ['%s has changed' % p for p in sorted(want) if p in have and have[p] != want[p]]
    for e in errors:
        print('sdk pin: ' + e)
    if errors:
        print('sdk pin: %d errors' % len(errors))
        sys.exit(1)
    print('sdk pin: the %d files of the VST 3 SDK subset match SHA256SUMS' % len(have))


if __name__ == '__main__':
    main()
```

  Run: `cd /Users/tk/Code/lxr/build/wt-plugin && python3 plugin/tests/sdk_pin.py .` — Expected: a `FileNotFoundError` for `plugin/third_party/vst3sdk/SHA256SUMS`.
- [ ] **Step 2: the vendor script** (P-3). `plugin/tools/vendor_vst3sdk.sh`:

```bash
#!/bin/bash
# Copy the parts of the VST 3 SDK that the plugin builds with into plugin/third_party/vst3sdk
# (plan E9.2 P-3): Steinberg's steinbergmedia/vst3sdk at the tag v3.8.1_build_84, MIT.
#
#   plugin/tools/vendor_vst3sdk.sh /path/to/vst3sdk
#
# The checkout must be that commit, with its submodules base, pluginterfaces, public.sdk and
# cmake at the commits below. The copy keeps the SDK's paths; SHA256SUMS lists every file.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -euo pipefail

SRC="${1:?usage: vendor_vst3sdk.sh /path/to/vst3sdk}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DST="$ROOT/plugin/third_party/vst3sdk"

want() {
	local got
	got="$(git -C "$SRC/$1" rev-parse HEAD)"
	if [ "$got" != "$2" ]; then
		echo "vendor_vst3sdk: $SRC/$1 is at $got, not $2" >&2
		exit 1
	fi
}
want . 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96
want base fcf9da0bd27a16f7f03773a3a39822f28f5c8477
want pluginterfaces 4f547e8e102b47de4a8b8aaf343c73b700786372
want public.sdk 586dc5e6c8012c3e4b01c79389375cbe96bdb1da
want cmake 054c9143cbb8d47fc4694e473f2ee3b4d951a8f5

PARTS=(
	LICENSE.txt README.md
	base pluginterfaces cmake/modules cmake/templates
	public.sdk/LICENSE.txt public.sdk/README.md
	public.sdk/source/main public.sdk/source/common
	public.sdk/source/vst/hosting public.sdk/source/vst/utility public.sdk/source/vst/moduleinfo
	public.sdk/source/vst/testsuite
	public.sdk/samples/vst-hosting/validator public.sdk/samples/vst-utilities/moduleinfotool
	public.sdk/samples/vst-hosting/audiohost/source/media/miditovst.h
)
rm -rf "$DST"
mkdir -p "$DST"
(cd "$SRC" && tar --exclude .git --exclude .gitignore --exclude .github --exclude VST_Logo_Steinberg.ico -cf - "${PARTS[@]}" public.sdk/source/vst/*.cpp public.sdk/source/vst/*.h) | tar -xf - -C "$DST"
(cd "$DST" && find . -type f ! -name SHA256SUMS | LC_ALL=C sort | sed 's|^\./||' | xargs shasum -a 256 > SHA256SUMS)
echo "vendor_vst3sdk: $(wc -l < "$DST/SHA256SUMS" | tr -d ' ') files, $(du -sk "$DST" | cut -f1) KB in plugin/third_party/vst3sdk"
```

  Run: `cd /Users/tk/Code/lxr/build/wt-plugin && chmod +x plugin/tools/vendor_vst3sdk.sh && plugin/tools/vendor_vst3sdk.sh /Users/tk/Code/lxr/build/vst3sdk && python3 plugin/tests/sdk_pin.py . && find plugin/third_party/vst3sdk -name '.*' | wc -l && grep -rIl -e Erica -e 'Sonic Potions' plugin/third_party/vst3sdk | wc -l`
  Expected: `vendor_vst3sdk: 341 files, 2932 KB in plugin/third_party/vst3sdk`, `sdk pin: the 341 files of the VST 3 SDK subset match SHA256SUMS`, `0`, `0`. `head -1 plugin/third_party/vst3sdk/LICENSE.txt` prints `MIT License`.
- [ ] **Step 3: the emulator's guard** (P-2). In `emu/CMakeLists.txt`, insert before the line `# --- the test harness -------------------------------------------------------------------------` (`:193`):

```cmake
# The plugin's project (plugin/CMakeLists.txt, plan E9.2 P-2) takes the device objects and the renderer
# and stops here: the emulator's tests, the app and the licence gate stay this project's.
if(LICHOMAT_EMU_DEVICE_ONLY)
    return()
endif()

```

  Run EMU-CHECK. Expected: `== emu build: 0`, `== emu warnings: 0`, `100% tests passed, 0 tests failed out of 346` (the guard is off in the emulator's own build).
- [ ] **Step 4: the SDK's glue** (P-4). `plugin/third_party/CMakeLists.txt`:

```cmake
# The VST 3 SDK's libraries, the validator and moduleinfotool, built from plugin/third_party/vst3sdk
# (plan E9.2 P-4). A directory of its own: smtg_setup_platform_toolset() adds -ffast-math and
# -ffp-contract=fast with add_compile_options(), which must not reach the firmware's objects.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.

set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk")
set(SDK_ROOT "${SDK_ROOT}" PARENT_SCOPE)
# smtg_add_vst3plugin() in the parent reads these three
set(public_sdk_SOURCE_DIR "${SDK_ROOT}/public.sdk")
set(public_sdk_SOURCE_DIR "${public_sdk_SOURCE_DIR}" PARENT_SCOPE)
set(pluginterfaces_SOURCE_DIR "${SDK_ROOT}/pluginterfaces")
set(pluginterfaces_SOURCE_DIR "${pluginterfaces_SOURCE_DIR}" PARENT_SCOPE)
list(APPEND CMAKE_MODULE_PATH "${SDK_ROOT}/cmake/modules")
# The SDK reads Xcode's version; the Command Line Tools have no xcodebuild (plan F17). Their own
# version stands in, and decides only the SDK's ad hoc signing step of Xcode 15 or later.
execute_process(COMMAND pkgutil --pkg-info=com.apple.pkg.CLTools_Executables OUTPUT_VARIABLE clt ERROR_QUIET)
string(REGEX MATCH "version: ([0-9]+\\.[0-9]+)" clt "${clt}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "the plugin builds with the Command Line Tools; pkgutil names no CLTools_Executables")
endif()
set(ENV{XCODE_VERSION} "${CMAKE_MATCH_1}")
set(XCODE_VERSION "${CMAKE_MATCH_1}")
include(SMTG_VST3_SDK)
smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()
include_directories("${SDK_ROOT}")
smtg_create_lib_base_target()
smtg_create_pluginterfaces_target()
smtg_create_public_sdk_common_target()
smtg_create_public_sdk_target()
smtg_create_public_sdk_hosting_target()
# the SDK's headers are system headers to our targets: our -Wall -Wextra -Werror is not theirs
set_target_properties(base pluginterfaces sdk_common sdk sdk_hosting PROPERTIES SYSTEM TRUE)
add_subdirectory("${SDK_ROOT}/public.sdk/samples/vst-hosting/validator" validator)
add_subdirectory("${SDK_ROOT}/public.sdk/samples/vst-utilities/moduleinfotool" moduleinfotool)
```

- [ ] **Step 5: the project.** `plugin/CMakeLists.txt`, the first block (each later task appends one):

```cmake
# The LICHOMAT plugin (plan E9.2): a VST3 instrument that plays the LICHOMAT firmware in an
# engine process per instance. A CMake project of its own; it builds the emulator's device
# objects and lichomat-render through emu/CMakeLists.txt, which stops after them here
# (LICHOMAT_EMU_DEVICE_ONLY), and the VST 3 SDK from plugin/third_party/vst3sdk.
#
#   source tools/env.sh
#   cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/plugin
#   ctest --test-dir build/plugin --output-on-failure
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.

cmake_minimum_required(VERSION 3.25)	# the VST 3 SDK's minimum
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "The plugin is built for arm64 only")
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "macOS 13 or later, as the emulator")
project(lichomat_plugin VERSION 0.1.0 LANGUAGES C CXX OBJCXX)

get_filename_component(LXR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# --- the device objects and lichomat-render (emu/CMakeLists.txt up to its guard) ---------
set(LICHOMAT_EMU_DEVICE_ONLY ON)
add_subdirectory("${LXR_ROOT}/emu" "${CMAKE_BINARY_DIR}/emu")
set(PLUGIN_OVERLAY "${CMAKE_BINARY_DIR}/main_overlay.yaml")	# emu writes it into the top build dir
set(PLUGIN_DEVICE_OBJECTS $<TARGET_OBJECTS:lxr_firmware> $<TARGET_OBJECTS:emu_core>
    $<TARGET_OBJECTS:emu_hostfs> $<TARGET_OBJECTS:emu_libm>)
# Our own C and C++: the licence gate wants the overlay on every compile command (M21), the
# emulator's warnings gate, and the sanitizers of the emulator's build for the engine
set(PLUGIN_SAN_FLAGS "")
if(LICHOMAT_EMU_SANITIZE)
    set(PLUGIN_SAN_FLAGS -fsanitize=${LICHOMAT_EMU_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
endif()
set(PLUGIN_FLAGS -Wall -Wextra -Werror "SHELL:-ivfsoverlay ${PLUGIN_OVERLAY}")

# --- the VST 3 SDK: its libraries, the validator, moduleinfotool ---------------------------
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "No VSTGUI: the prototype has no window" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "The SDK would link each plugin into ~/Library/Audio/Plug-Ins/VST3" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "P10_plugin_validator runs it" FORCE)
add_subdirectory(third_party)

# --- tests ------------------------------------------------------------------------------------
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# plugin_add_test(<name> IDS <id>...): plugin/tests/<name>.c with the shared code; each id is a
# ctest test that runs the executable with the id as its filter (tests/seqc_test.h)
function(plugin_add_test name)
    cmake_parse_arguments(ARG "" "" "IDS" ${ARGN})
    add_executable(${name} tests/${name}.c $<TARGET_OBJECTS:plug_common>)
    target_include_directories(${name} PRIVATE "${PLUGIN_DIR}/common" "${LXR_ROOT}/tests")
    target_compile_options(${name} PRIVATE ${PLUGIN_FLAGS})
    foreach(id IN LISTS ARG_IDS)
        add_test(NAME ${id} COMMAND ${name} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT 120 LABELS plugin)
    endforeach()
endfunction()
add_test(NAME P2_sdk_pin COMMAND ${Python3_EXECUTABLE} "${PLUGIN_DIR}/tests/sdk_pin.py" "${LXR_ROOT}")
set_tests_properties(P2_sdk_pin PROPERTIES LABELS plugin)
```

  and the gate, `plugin/tools/plugin_check.sh`:

```bash
#!/bin/bash
# The LICHOMAT plugin's gate (plan E9.2): its project in Debug with UBSan (build/plugin) and in
# Release (build/plugin-release), every ctest id of each, and the compiler warnings of our own
# files, which must be 0 (the vendored SDK's own files warn; they are not counted).
#
#   plugin/tools/plugin_check.sh
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -u -o pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tools/env.sh" >/dev/null
status=0
for cfg in Debug Release; do
  if [ "$cfg" = Debug ]; then B="$ROOT/build/plugin"; else B="$ROOT/build/plugin-release"; fi
  LOG="$B.log"
  echo "== plugin, $cfg ($B)"
  if cmake -S "$ROOT/plugin" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE="$cfg" > "$LOG" 2>&1 && ninja -C "$B" >> "$LOG" 2>&1; then
    (cd "$B" && ctest -j4 --output-on-failure 2>&1) | tee -a "$LOG" | grep -E '^[0-9]+% tests passed|\*\*\*|^Errors' || status=1
  else
    echo "plugin build FAILED"; grep -E "error" "$LOG" | head -n 20; status=1
  fi
  w=$(grep 'warning:' "$LOG" | grep -v -e '/plugin/third_party/vst3sdk/' -e '/CommandLineTools/' | wc -l | tr -d ' ')
  echo "== compiler warnings, $cfg, our files: $w"
  [ "$w" = 0 ] || status=1
done
exit $status
```

- [ ] **Step 6: build and run.** `chmod +x plugin/tools/plugin_check.sh plugin/tests/sdk_pin.py`, then PLUGIN with `-R P2` and `ls build/plugin/bin/Debug/validator build/plugin/bin/Debug/moduleinfotool && ls ~/Library/Audio/Plug-Ins`.
  Expected: `== plugin build: 0`, `== plugin warnings: 0`, `100% tests passed, 0 tests failed out of 1`; both tools exist; the `ls` line is Task 1's (no `VST3` folder appeared: F18). The configure prints `[SMTG] Building with Xcode version: 26.6`.
- [ ] **Step 7: the gate.** PLUGIN-CHECK (T 1). **Step 8: commit** `plugin Task 2 (E9.12): the VST 3 SDK 3.8.1 vendored and pinned, the plugin's project, the emulator's device-only guard` with `emu/CMakeLists.txt plugin/CMakeLists.txt plugin/third_party/CMakeLists.txt plugin/third_party/vst3sdk plugin/tools/vendor_vst3sdk.sh plugin/tools/plugin_check.sh plugin/tests/sdk_pin.py`; `git push -u origin plugin`.

---

### Task 3: Shared memory — E9.13

**Files:** Create `plugin/common/plug_shm.h`, `plugin/common/plug_shm.c`, `plugin/tests/test_shm.c`; modify `plugin/CMakeLists.txt` (append).

- [ ] **Step 1: the failing test.** `plugin/tests/test_shm.c`:

```c
/*
 * test_shm.c
 *
 * P3: the shared memory between the plugin and the engine (plan E9.2 Task 3): the header check,
 * the event ring, the audio ring with its epoch and underruns, a new engine's epoch, and the two
 * rings between two processes, with a killed child seen as the end of a pipe.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "plug_shm.h"
#include "seqc_test.h"

static plug_shm_t *anon(void)
{
	plug_shm_t *s = mmap(NULL, sizeof *s, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	if (s == MAP_FAILED)
		abort();
	plug_shmInit(s, 48000.0, 512);
	return s;
}

TEST(P3_shm_header)
{
	plug_shm_t *s = anon();
	char err[160];
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), 0);
	ASSERT_EQ(s->size, sizeof(plug_shm_t));
	s->version = 2;
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "version 2") != NULL);
	plug_shmInit(s, 1000.0, 512);
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	plug_shmInit(s, 44100.0, 0);
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	s->magic = 0;
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "LCHP") != NULL);
	munmap(s, sizeof *s);
}

TEST(P3_shm_events)
{
	plug_shm_t *s = anon();
	plug_event_t e;
	ASSERT_EQ(plug_evPop(s, &e), 0);
	ASSERT_EQ(plug_evNextFrame(s), UINT64_MAX);
	for (unsigned i = 0; i < PLUG_EVENT_SLOTS; i++)
	{
		const uint8_t b[3] = { 0x90, (uint8_t)(i & 0x7F), 100 };
		ASSERT_EQ(plug_evPush(s, 10u * i, b, 3), 0);
	}
	const uint8_t f8 = 0xF8;
	ASSERT_EQ(plug_evPush(s, 99999, &f8, 1), -1);	/* full */
	ASSERT_EQ(s->evDropped, 1);
	ASSERT_EQ(plug_evPush(s, 99999, &f8, 0), -1);	/* no bytes */
	ASSERT_EQ(plug_evPush(s, 99999, &f8, 4), -1);	/* too many */
	ASSERT_EQ(s->evDropped, 3);
	ASSERT_EQ(plug_evNextFrame(s), 0);
	for (unsigned i = 0; i < 10; i++)
	{
		ASSERT_EQ(plug_evPop(s, &e), 1);
		ASSERT_EQ(e.frame, 10u * i);
		ASSERT_EQ(e.len, 3);
		ASSERT_EQ(e.b[1], i);
	}
	ASSERT_EQ(plug_evPush(s, 50000, &f8, 1), 0);	/* room again */
	plug_evSkipAll(s);
	ASSERT_EQ(plug_evPop(s, &e), 0);
	ASSERT_EQ(s->evRead, s->evWrite);
	plug_publish(s, 1234);
	ASSERT_EQ(plug_load(&s->hostFrames), 1234);
	munmap(s, sizeof *s);
}

TEST(P3_shm_audio)
{
	plug_shm_t *s = anon();
	float lr[2 * 64], l[100], r[100];
	for (unsigned i = 0; i < 64; i++)
	{
		lr[2 * i] = (float)i;
		lr[2 * i + 1] = -(float)i;
	}
	plug_audioBegin(s, 100);
	plug_audioWrite(s, lr, 64);
	ASSERT_EQ(s->audioEnd, 164);
	/* not running: no underrun is counted */
	ASSERT_EQ(plug_audioRead(s, 90, 100, l, r), 64);
	ASSERT_EQ_DBL(l[9], 0.0);
	ASSERT_EQ_DBL(l[10], 0.0);
	ASSERT_EQ_DBL(l[11], 1.0);
	ASSERT_EQ_DBL(r[73], -63.0);
	ASSERT_EQ_DBL(l[74], 0.0);
	ASSERT_EQ(s->underruns, 0);
	/* running: the 26 frames from 164 on are missing; the 10 before the epoch are not */
	s->state = PLUG_ENGINE_RUNNING;
	ASSERT_EQ(plug_audioRead(s, 90, 100, l, r), 64);
	ASSERT_EQ(s->underruns, 26);
	munmap(s, sizeof *s);
}

TEST(P3_shm_new_engine)
{
	plug_shm_t *s = anon();
	float lr[2 * 1000], l[100], r[100];
	for (unsigned i = 0; i < 2000; i++)
		lr[i] = 1.0f;
	s->state = PLUG_ENGINE_RUNNING;
	plug_audioBegin(s, 100);
	plug_audioWrite(s, lr, 1000);
	/* a new engine starts at 5000: the old frames are gone, the gap is not an underrun */
	plug_audioBegin(s, 5000);
	ASSERT_EQ(plug_audioRead(s, 1000, 100, l, r), 0);
	ASSERT_EQ_DBL(l[0], 0.0);
	ASSERT_EQ(s->underruns, 0);
	plug_audioWrite(s, lr, 64);
	ASSERT_EQ(plug_audioRead(s, 4990, 100, l, r), 64);
	ASSERT_EQ_DBL(l[9], 0.0);
	ASSERT_EQ_DBL(l[10], 1.0);
	ASSERT_EQ(s->underruns, 26);
	munmap(s, sizeof *s);
}

TEST(P3_shm_overwritten)
{
	plug_shm_t *s = anon();
	static float lr[2 * PLUG_AUDIO_FRAMES];
	float l[8], r[8];
	for (unsigned i = 0; i < PLUG_AUDIO_FRAMES; i++)
		lr[2 * i] = lr[2 * i + 1] = (float)(i + 1);
	plug_audioBegin(s, 0);
	plug_audioWrite(s, lr, PLUG_AUDIO_FRAMES);
	plug_audioWrite(s, lr, 4);	/* frames 0..3 are overwritten by 32768..32771 */
	ASSERT_EQ(plug_audioRead(s, 0, 8, l, r), 4);
	ASSERT_EQ_DBL(l[3], 0.0);
	ASSERT_EQ_DBL(l[4], 5.0);
	munmap(s, sizeof *s);
}

#define N_EVENTS 2000000u

/* the child: each event in order, one audio frame per published host frame */
static int child(plug_shm_t *s, int wake)
{
	uint64_t a = 0, k = 0;
	char b[256];
	for (;;)
	{
		plug_event_t e;
		int any = 0;
		while (plug_evPop(s, &e))
		{
			if (e.frame != k || e.b[0] != (uint8_t)(0x80u | (k & 0x7Fu)))
				return 4;
			k++;
			any = 1;
		}
		const uint64_t hf = plug_load(&s->hostFrames);
		while (a < hf)
		{
			const float lr[2] = { (float)a, -(float)a };
			plug_audioWrite(s, lr, 1);
			a++;
		}
		if (!any && read(wake, b, sizeof b) == 0)
			return k == N_EVENTS ? 0 : 5;	/* the parent closed the pipe */
	}
}

TEST(P3_shm_processes)
{
	char err[160];
	int fd = -1;
	plug_shm_t *s = plug_shmCreate(48000.0, 512, &fd, err, sizeof err);
	ASSERT_TRUE(s != NULL);
	int wake[2], life[2];
	ASSERT_EQ(pipe(wake), 0);
	ASSERT_EQ(pipe(life), 0);
	ASSERT_EQ(fcntl(wake[1], F_SETFL, O_NONBLOCK), 0);
	const pid_t pid = fork();
	if (pid == 0)
	{
		close(wake[1]);
		close(life[0]);
		plug_shm_t *c = plug_shmMap(fd, err, sizeof err);	/* a mapping of its own, as the engine's */
		_exit(c && plug_shmCheck(c, err, sizeof err) == 0 ? child(c, wake[0]) : 3);
	}
	close(wake[0]);
	close(life[1]);
	uint64_t w = 0, checked = 0;
	while (checked < N_EVENTS)
	{
		const uint64_t burst = w + 300u > N_EVENTS ? N_EVENTS : w + 300u;
		while (w < burst)
		{
			const uint8_t b = (uint8_t)(0x80u | (w & 0x7Fu));
			if (plug_evPush(s, w, &b, 1) != 0)
				break;
			w++;
		}
		plug_publish(s, w);
		(void)!write(wake[1], "w", 1);
		static float l[N_EVENTS / 1000], r[N_EVENTS / 1000];
		const uint64_t end = plug_load(&s->audioEnd);
		while (checked < end)
		{
			const uint32_t n = end - checked > 2000u ? 2000u : (uint32_t)(end - checked);
			ASSERT_EQ(plug_audioRead(s, checked, n, l, r), n);
			for (uint32_t i = 0; i < n; i++)
				ASSERT_EQ_DBL(r[i], -(double)(checked + i));
			checked += n;
		}
	}
	const uint64_t dropped = s->evDropped;
	close(wake[1]);	/* the child ends */
	int status = 0;
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);
	/* the child held life[1]; it is closed now: the read end gives EOF */
	char c;
	ASSERT_EQ(read(life[0], &c, 1), 0);
	printf("P3_shm_processes: %u events and frames, %llu pushes refused while full\n", N_EVENTS,
		(unsigned long long)dropped);
	plug_shmClose(s);
	close(fd);
}

TEST(P3_shm_killed)
{
	int life[2];
	ASSERT_EQ(pipe(life), 0);
	const pid_t pid = fork();
	if (pid == 0)
	{
		close(life[0]);
		for (;;)
			pause();
	}
	close(life[1]);
	struct pollfd p = { life[0], POLLIN, 0 };
	ASSERT_EQ(poll(&p, 1, 50), 0);	/* alive: nothing to read */
	ASSERT_EQ(kill(pid, SIGKILL), 0);
	ASSERT_EQ(poll(&p, 1, 2000), 1);
	char c;
	ASSERT_EQ(read(life[0], &c, 1), 0);	/* EOF: the child is gone */
	int status = 0;
	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
}

TEST_MAIN()
```

  Append to `plugin/CMakeLists.txt`:

```cmake
# --- the code the plugin, the engine and the tests share (Tasks 3-6) -----------------------
add_library(plug_common OBJECT common/plug_shm.c)
target_include_directories(plug_common PUBLIC "${PLUGIN_DIR}/common")
target_compile_options(plug_common PRIVATE ${PLUGIN_FLAGS})
# the engine's copy, with the sanitizers of the device objects it links with
add_library(plug_common_san OBJECT common/plug_shm.c)
target_include_directories(plug_common_san PUBLIC "${PLUGIN_DIR}/common")
target_compile_options(plug_common_san PRIVATE ${PLUGIN_FLAGS} ${PLUGIN_SAN_FLAGS})
plugin_add_test(test_shm IDS P3_shm_header P3_shm_events P3_shm_audio P3_shm_new_engine P3_shm_overwritten
    P3_shm_processes P3_shm_killed)
```

- [ ] **Step 2: run it.** PLUGIN with `-R P3`. Expected: `== plugin build: 1`; the configure stops at `Cannot find source file: common/plug_shm.c` (`build/plugin.log`).
- [ ] **Step 3: the header** (P-6, P-7). `plugin/common/plug_shm.h`:

```c
/*
 * plug_shm.h
 *
 * The shared memory between the LICHOMAT plugin and its engine process (plan E9.2 Task 3): a
 * versioned header, a ring of MIDI events from the plugin to the engine and a ring of stereo
 * audio frames from the engine to the plugin. Each ring has one producer and one consumer, in
 * two processes. The counters only grow, and each side writes only its own fields; they are
 * plain 64-bit integers read and written with the __atomic builtins, so the header is the same
 * in C11 and C++17.
 *
 * Time is counted in host frames: frame 0 is the plugin's first frame after its activation.
 * - Events: the plugin writes the events of a block, then publishes hostFrames, the end of the
 *   block: every event before that frame is in the ring. Events are written in frame order.
 * - Audio: the engine writes the frames [audioStart, audioEnd) of the host timeline; frame f
 *   lies at audio[f % PLUG_AUDIO_FRAMES]. A new engine moves audioStart and audioEnd to its
 *   epoch, the host frame of its first block.
 * - The memory is a POSIX shared memory object whose name is unlinked at once: the plugin keeps
 *   its descriptor and gives it to each engine it starts as fd 5, so nothing is left behind when
 *   either process dies.
 * - The engine's death is not in the memory: the plugin sees it as the end of a pipe
 *   (plugin_link.cpp), and then writes the engine's state itself.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_SHM_H_
#define PLUG_SHM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_SHM_MAGIC 0x5048434Cu	/**< "LCHP" in memory */
#define PLUG_SHM_VERSION 1u
#define PLUG_EVENT_SLOTS 4096u		/**< a power of two */
#define PLUG_AUDIO_FRAMES 32768u	/**< a power of two; 0.68 s at 48 kHz, far more than any latency */

/** the engine's state, written by the engine, and by the plugin once the engine is gone */
enum
{
	PLUG_ENGINE_NONE = 0,		/**< no engine, or it died */
	PLUG_ENGINE_BOOTING = 1,	/**< it has mapped the memory and boots the firmware */
	PLUG_ENGINE_RUNNING = 2,	/**< it plays from epoch on */
	PLUG_ENGINE_FAILED = 3		/**< it stopped on an error it reported in its log */
};

/** one MIDI message at a host frame */
typedef struct plug_event
{
	uint64_t frame;
	uint8_t len;		/**< 1-3 */
	uint8_t b[3];
	uint32_t pad;
} plug_event_t;

typedef struct plug_shm
{
	/* set by the plugin before the engine starts */
	uint32_t magic;
	uint32_t version;
	uint32_t size;			/**< sizeof(plug_shm_t) */
	uint32_t maxBlock;		/**< the host's largest block, frames */
	double rate;			/**< host frames per second */
	uint32_t realtime;		/**< 1: the host plays in real time (the engine takes a time-constraint policy) */
	uint32_t pad0;
	/* the plugin's, from the audio thread */
	uint64_t hostFrames;	/**< every event before this host frame is in the ring */
	uint64_t evWrite;		/**< events written */
	uint64_t evDropped;		/**< events the full ring refused */
	uint64_t underruns;		/**< frames the plugin read while the engine ran, before the engine wrote them */
	uint64_t quit;			/**< 1: the plugin asks the engine to end */
	/* the engine's */
	uint64_t state;			/**< PLUG_ENGINE_* */
	uint64_t pid;
	uint64_t evRead;		/**< events read */
	uint64_t epoch;			/**< the host frame of the engine's first block */
	uint64_t audioStart;	/**< the first frame the engine wrote */
	uint64_t audioEnd;		/**< the frame after the last one it wrote */
	uint64_t frontier;		/**< the host frames whose events it has given to the firmware */
	uint64_t late;			/**< inputs that reached the firmware after their time */
	uint64_t dropped;		/**< events before the epoch, left out */
	plug_event_t ev[PLUG_EVENT_SLOTS];
	float audio[PLUG_AUDIO_FRAMES][2];
} plug_shm_t;

static inline uint64_t plug_load(const uint64_t *p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static inline void plug_store(uint64_t *p, uint64_t v)
{
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

/** set the header of s, zero the rest (the plugin, before the engine starts); realtime 0 */
void plug_shmInit(plug_shm_t *s, double rate, uint32_t maxBlock);

/** 0, or -1 with the problem in err (the engine, after mapping) */
int plug_shmCheck(const plug_shm_t *s, char *err, size_t n);

/** Create a shared memory object, unlink its name, size, map and initialise it. Returns the
 * mapping and its descriptor in *fd (close-on-exec), or NULL with the problem in err. */
plug_shm_t *plug_shmCreate(double rate, uint32_t maxBlock, int *fd, char *err, size_t n);

/** map the object of descriptor fd (the engine: fd 5); NULL with the problem in err */
plug_shm_t *plug_shmMap(int fd, char *err, size_t n);

/** unmap */
void plug_shmClose(plug_shm_t *s);

/* --- the plugin's side: the audio thread. No allocation, no lock, no system call. --- */

/** Write one message at a frame (frames in order). Returns 0, or -1 when the ring is full: the
 * message is counted in evDropped and left out. */
int plug_evPush(plug_shm_t *s, uint64_t frame, const uint8_t *b, uint8_t len);

/** every event before hostFrames is written */
void plug_publish(plug_shm_t *s, uint64_t hostFrames);

/** Copy the frames [from, from + frames) into l and r. A frame the engine has not written is 0; one
 * that it should have written by now (from its epoch on, while it runs) counts in underruns.
 * Returns the frames copied from the ring. */
uint32_t plug_audioRead(plug_shm_t *s, uint64_t from, uint32_t frames, float *l, float *r);

/* --- the engine's side --- */

/** the next event, or 0 when the ring holds none */
int plug_evPop(plug_shm_t *s, plug_event_t *e);

/** the frame of the next event without taking it, or UINT64_MAX when none */
uint64_t plug_evNextFrame(plug_shm_t *s);

/** leave out every event in the ring (a new engine) */
void plug_evSkipAll(plug_shm_t *s);

/** an engine's first frame: the audio ring starts at epoch, empty */
void plug_audioBegin(plug_shm_t *s, uint64_t epoch);

/** append frames, interleaved L R, at audioEnd */
void plug_audioWrite(plug_shm_t *s, const float *lr, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_SHM_H_ */
```

  and `plugin/common/plug_shm.c`:

```c
/*
 * plug_shm.c
 *
 * The shared memory between the plugin and its engine (plan E9.2 Task 3); see plug_shm.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_shm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

_Static_assert(sizeof(plug_event_t) == 16, "plug_event_t is 16 bytes");
_Static_assert((PLUG_EVENT_SLOTS & (PLUG_EVENT_SLOTS - 1u)) == 0, "PLUG_EVENT_SLOTS is a power of two");
_Static_assert((PLUG_AUDIO_FRAMES & (PLUG_AUDIO_FRAMES - 1u)) == 0, "PLUG_AUDIO_FRAMES is a power of two");
_Static_assert(offsetof(plug_shm_t, hostFrames) % 8 == 0, "the counters are 8-byte aligned");

void plug_shmInit(plug_shm_t *s, double rate, uint32_t maxBlock)
{
	memset(s, 0, sizeof *s);
	s->version = PLUG_SHM_VERSION;
	s->size = (uint32_t)sizeof *s;
	s->maxBlock = maxBlock;
	s->rate = rate;
	__atomic_store_n(&s->magic, PLUG_SHM_MAGIC, __ATOMIC_RELEASE);
}

int plug_shmCheck(const plug_shm_t *s, char *err, size_t n)
{
	if (__atomic_load_n(&s->magic, __ATOMIC_ACQUIRE) != PLUG_SHM_MAGIC)
	{
		snprintf(err, n, "shared memory: no LCHP header");
		return -1;
	}
	if (s->version != PLUG_SHM_VERSION || s->size != sizeof *s)
	{
		snprintf(err, n, "shared memory: version %u size %u, this engine needs version %u size %zu", s->version,
			s->size, PLUG_SHM_VERSION, sizeof *s);
		return -1;
	}
	if (!(s->rate >= 8000.0 && s->rate <= 384000.0) || s->maxBlock == 0 || s->maxBlock > 8192u)
	{
		snprintf(err, n, "shared memory: rate %.3f, block %u", s->rate, s->maxBlock);
		return -1;
	}
	return 0;
}

plug_shm_t *plug_shmMap(int fd, char *err, size_t n)
{
	void *p = mmap(NULL, sizeof(plug_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
	{
		snprintf(err, n, "mmap of the shared memory: %s", strerror(errno));
		return NULL;
	}
	return p;
}

plug_shm_t *plug_shmCreate(double rate, uint32_t maxBlock, int *fd, char *err, size_t n)
{
	static unsigned counter;
	char name[32];	/* macOS allows 31 characters */
	snprintf(name, sizeof name, "/lchp.%d.%u", (int)getpid(), __atomic_fetch_add(&counter, 1u, __ATOMIC_RELAXED));
	const int f = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (f < 0)
	{
		snprintf(err, n, "shm_open %s: %s", name, strerror(errno));
		return NULL;
	}
	shm_unlink(name);	/* the descriptors keep it */
	if (ftruncate(f, (off_t)sizeof(plug_shm_t)) != 0 || fcntl(f, F_SETFD, FD_CLOEXEC) != 0)
	{
		snprintf(err, n, "ftruncate %s: %s", name, strerror(errno));
		close(f);
		return NULL;
	}
	plug_shm_t *s = plug_shmMap(f, err, n);
	if (!s)
	{
		close(f);
		return NULL;
	}
	plug_shmInit(s, rate, maxBlock);
	*fd = f;
	return s;
}

void plug_shmClose(plug_shm_t *s)
{
	if (s)
		munmap(s, sizeof *s);
}

/* --- the plugin's side --------------------------------------------------------------- */

int plug_evPush(plug_shm_t *s, uint64_t frame, const uint8_t *b, uint8_t len)
{
	const uint64_t w = __atomic_load_n(&s->evWrite, __ATOMIC_RELAXED);	/* our own field */
	if (len == 0 || len > 3 || w - plug_load(&s->evRead) >= PLUG_EVENT_SLOTS)
	{
		__atomic_store_n(&s->evDropped, __atomic_load_n(&s->evDropped, __ATOMIC_RELAXED) + 1u, __ATOMIC_RELAXED);
		return -1;
	}
	plug_event_t *e = &s->ev[w & (PLUG_EVENT_SLOTS - 1u)];
	e->frame = frame;
	e->len = len;
	e->b[0] = b[0];
	e->b[1] = len > 1 ? b[1] : 0;
	e->b[2] = len > 2 ? b[2] : 0;
	e->pad = 0;
	plug_store(&s->evWrite, w + 1u);
	return 0;
}

void plug_publish(plug_shm_t *s, uint64_t hostFrames)
{
	plug_store(&s->hostFrames, hostFrames);
}

uint32_t plug_audioRead(plug_shm_t *s, uint64_t from, uint32_t frames, float *l, float *r)
{
	/* audioEnd first: the engine stores audioStart before it, so a new engine's start is seen with
	 * its end or after it, and a range with start > end is empty */
	const uint64_t end = plug_load(&s->audioEnd);
	const uint64_t start = plug_load(&s->audioStart);
	const int running = plug_load(&s->state) == PLUG_ENGINE_RUNNING;
	uint32_t copied = 0, missing = 0;
	for (uint32_t i = 0; i < frames; i++)
	{
		const uint64_t f = from + i;
		if (f >= start && f < end && end - f <= PLUG_AUDIO_FRAMES)
		{
			const float *p = s->audio[f & (PLUG_AUDIO_FRAMES - 1u)];
			l[i] = p[0];
			r[i] = p[1];
			copied++;
		}
		else
		{
			l[i] = r[i] = 0.0f;
			if (running && f >= start && f >= end)
				missing++;
		}
	}
	if (missing)
		__atomic_store_n(&s->underruns, __atomic_load_n(&s->underruns, __ATOMIC_RELAXED) + missing, __ATOMIC_RELAXED);
	return copied;
}

/* --- the engine's side --------------------------------------------------------------- */

int plug_evPop(plug_shm_t *s, plug_event_t *e)
{
	const uint64_t r = __atomic_load_n(&s->evRead, __ATOMIC_RELAXED);
	if (r == plug_load(&s->evWrite))
		return 0;
	*e = s->ev[r & (PLUG_EVENT_SLOTS - 1u)];
	plug_store(&s->evRead, r + 1u);
	return 1;
}

uint64_t plug_evNextFrame(plug_shm_t *s)
{
	const uint64_t r = __atomic_load_n(&s->evRead, __ATOMIC_RELAXED);
	if (r == plug_load(&s->evWrite))
		return UINT64_MAX;
	return s->ev[r & (PLUG_EVENT_SLOTS - 1u)].frame;
}

void plug_evSkipAll(plug_shm_t *s)
{
	plug_store(&s->evRead, plug_load(&s->evWrite));
}

void plug_audioBegin(plug_shm_t *s, uint64_t epoch)
{
	plug_store(&s->audioStart, epoch);
	plug_store(&s->audioEnd, epoch);
}

void plug_audioWrite(plug_shm_t *s, const float *lr, uint32_t frames)
{
	const uint64_t end = __atomic_load_n(&s->audioEnd, __ATOMIC_RELAXED);
	for (uint32_t i = 0; i < frames; i++)
	{
		float *p = s->audio[(end + i) & (PLUG_AUDIO_FRAMES - 1u)];
		p[0] = lr[2u * i];
		p[1] = lr[2u * i + 1u];
	}
	plug_store(&s->audioEnd, end + frames);
}
```

- [ ] **Step 4: pass.** PLUGIN with `-R P3`. Expected: `100% tests passed, 0 tests failed out of 7`; `build/plugin/test_shm P3_shm_processes` prints `P3_shm_processes: 2000000 events and frames, N pushes refused while full` (N varies) and `PASS P3_shm_processes`.
- [ ] **Step 5: the gate.** PLUGIN-CHECK (T 8). **Step 6: commit** `plugin Task 3 (E9.13): the shared memory between the plugin and its engine: header, event ring, audio ring; P3` with the four files; push.

---

### Task 4: The transport as MIDI clock — E9.14

**Files:** Create `plugin/common/plug_clock.h`, `plugin/common/plug_clock.c`, `plugin/tests/test_clock.c`; modify `plugin/CMakeLists.txt` (append).

- [ ] **Step 1: the failing test.** `plugin/tests/test_clock.c` (each case drives the generator block by block and checks absolute frames computed here):

```c
/*
 * test_clock.c
 *
 * P4: the host's transport as MIDI clock (plan E9.2 Task 4). Each case drives plug_clockBlock()
 * block by block with a transport the test computes, and checks the messages at their absolute
 * frames against values computed here.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <stdlib.h>

#include "plug_clock.h"
#include "seqc_test.h"

#define MAXEV 20000

typedef struct
{
	uint64_t frame;
	uint8_t len, b[3];
} ev_t;

static ev_t evs[MAXEV];
static unsigned nev;

/* one block at absolute frame at, its messages appended to evs */
static void block(plug_clock_t *c, const plug_transport_t *t, uint64_t at, uint32_t frames)
{
	plug_midi_t out[512];
	const unsigned n = plug_clockBlock(c, t, frames, out, 512);
	for (unsigned i = 0; i < n && nev < MAXEV; i++)
	{
		if (i > 0 && out[i].offset < out[i - 1].offset)
			abort();	/* the messages come in frame order */
		evs[nev++] = (ev_t){ at + out[i].offset, out[i].len, { out[i].b[0], out[i].b[1], out[i].b[2] } };
	}
}

static plug_transport_t playing(double tempo, double ppq)
{
	return (plug_transport_t){ 1, 1, tempo, ppq, 0, 0.0, 0.0 };
}

/* the count of messages with status st */
static unsigned count(uint8_t st)
{
	unsigned n = 0;
	for (unsigned i = 0; i < nev; i++)
		n += evs[i].b[0] == st;
	return n;
}

TEST(P4_clock_start_at_zero)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 40; b++)
	{
		const plug_transport_t t = playing(120.0, (double)(b * 512u) / 24000.0);
		block(&c, &t, b * 512u, 512);
	}
	/* 1,000 frames a clock: Start and clock 0 at 0, then clocks 1..20 */
	ASSERT_EQ(nev, 22);
	ASSERT_EQ(evs[0].b[0], 0xFA);
	ASSERT_EQ(evs[0].frame, 0);
	for (unsigned k = 0; k <= 20; k++)
	{
		ASSERT_EQ(evs[k + 1].b[0], 0xF8);
		ASSERT_EQ(evs[k + 1].frame, 1000u * k);
	}
}

TEST(P4_clock_rounding_44k)
{
	plug_clock_t c;
	plug_clockInit(&c, 44100.0);
	nev = 0;
	const double fpq = 44100.0 * 60.0 / 120.0;
	for (unsigned b = 0; b < 3600; b++)
	{
		const plug_transport_t t = playing(120.0, (double)(b * 256u) / fpq);
		block(&c, &t, b * 256u, 256);
	}
	/* 918.75 frames a clock: clock k at the nearest frame (a half, 1,837.5 for clock 2, either way:
	 * the position is a double), each clock once */
	ASSERT_EQ(evs[0].b[0], 0xFA);
	ASSERT_EQ(count(0xF8), nev - 1);
	ASSERT_EQ(nev - 1, 1004);	/* the clocks before 921,600 = 3,600 x 256 */
	for (unsigned k = 0; k < nev - 1; k++)
		ASSERT_NEAR((double)evs[k + 1].frame, 918.75 * k, 0.5);
	ASSERT_EQ(evs[2].frame, 919);
	ASSERT_EQ(evs[4].frame, 2756);
	ASSERT_EQ(evs[5].frame, 3675);
}

TEST(P4_clock_start_later)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 20; b++)
	{
		const plug_transport_t t = playing(120.0, 1.1 + (double)(b * 512u) / 24000.0);
		block(&c, &t, b * 512u, 512);
	}
	/* Song Position 5 (ceil 4.4) and Continue at 0; clock 30 (position 1.25) at 0.15 x 24,000 */
	ASSERT_EQ(evs[0].b[0], 0xF2);
	ASSERT_EQ(evs[0].b[1], 5);
	ASSERT_EQ(evs[0].b[2], 0);
	ASSERT_EQ(evs[0].frame, 0);
	ASSERT_EQ(evs[1].b[0], 0xFB);
	ASSERT_EQ(evs[1].frame, 0);
	ASSERT_EQ(evs[2].b[0], 0xF8);
	ASSERT_EQ(evs[2].frame, 3600);
	ASSERT_EQ(evs[3].frame, 4600);
	ASSERT_EQ(count(0xFA), 0);
}

TEST(P4_clock_stop)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 30; b++)
	{
		plug_transport_t t = playing(120.0, (double)(b * 512u) / 24000.0);
		t.playing = b < 10;
		block(&c, &t, b * 512u, 512);
	}
	ASSERT_EQ(count(0xFC), 1);
	ASSERT_EQ(evs[nev - 1].b[0], 0xFC);
	ASSERT_EQ(evs[nev - 1].frame, 5120);
	ASSERT_EQ(count(0xF8), 6);	/* clocks 0..5 before 5,120 */
}

/* a loop [4, 8) quarter notes at 120 BPM, 48 kHz (24,000 frames a quarter), from position 4,
 * in blocks of the given size; split 1 ends a block exactly at the loop's end */
static void loopRun(unsigned size, int split)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	uint64_t at = 0;
	while (at < 200000u)
	{
		uint32_t n = size;
		if (split && at < 96000u && at + n > 96000u)
			n = (uint32_t)(96000u - at);
		plug_transport_t t = playing(120.0, 4.0 + fmod((double)at / 24000.0, 4.0));
		t.cycle = 1;
		t.cycleStart = 4.0;
		t.cycleEnd = 8.0;
		block(&c, &t, at, n);
		at += n;
	}
}

TEST(P4_clock_loop)
{
	static ev_t inBlock[MAXEV];
	loopRun(700, 0);	/* the block [95,900, 96,600) holds the loop's end */
	const unsigned n = nev;
	memcpy(inBlock, evs, sizeof evs);
	loopRun(700, 1);
	ASSERT_EQ(nev, n);
	for (unsigned i = 0; i < n; i++)
	{
		ASSERT_EQ(evs[i].frame, inBlock[i].frame);
		ASSERT_EQ(evs[i].b[0], inBlock[i].b[0]);
		ASSERT_EQ(evs[i].b[1], inBlock[i].b[1]);
	}
	/* from 4: Song Position 16, Continue; at the loop's end, 96,000: Stop, 16, Continue, clock 96 */
	ASSERT_EQ(evs[0].b[0], 0xF2);
	ASSERT_EQ(evs[0].b[1], 16);
	unsigned i = 0;
	while (i < n && !(evs[i].b[0] == 0xFC))
		i++;
	ASSERT_TRUE(i + 3 < n);
	ASSERT_EQ(evs[i].frame, 96000);
	ASSERT_EQ(evs[i + 1].b[0], 0xF2);
	ASSERT_EQ(evs[i + 1].b[1], 16);
	ASSERT_EQ(evs[i + 2].b[0], 0xFB);
	ASSERT_EQ(evs[i + 3].b[0], 0xF8);
	ASSERT_EQ(evs[i + 3].frame, 96000);
	ASSERT_EQ(evs[i - 1].b[0], 0xF8);
	ASSERT_EQ(evs[i - 1].frame, 95000);	/* the last clock before the end, position 7 23/24 */
	ASSERT_EQ(count(0xFC), 2);			/* 96,000 and 192,000 */
}

TEST(P4_clock_locate)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 40; b++)
	{
		/* at block 20 the host jumps from 10.24 to 2.3 */
		const double p = b < 20 ? 10.0 + (double)(b * 512u) / 24000.0 : 2.3 + (double)((b - 20u) * 512u) / 24000.0;
		const plug_transport_t t = playing(120.0, p);
		block(&c, &t, b * 512u, 512);
	}
	unsigned i = 0;
	while (i < nev && evs[i].b[0] != 0xFC)
		i++;
	ASSERT_EQ(evs[i].frame, 20u * 512u);
	ASSERT_EQ(evs[i + 1].b[0], 0xF2);
	ASSERT_EQ(evs[i + 1].b[1], 10);	/* ceil(9.2) */
	ASSERT_EQ(evs[i + 2].b[0], 0xFB);
	ASSERT_EQ(evs[i + 3].b[0], 0xF8);
	ASSERT_EQ(evs[i + 3].frame, 20u * 512u + 4800u);	/* clock 60, position 2.5 */
	ASSERT_EQ(count(0xFC), 1);
}

TEST(P4_clock_tempo)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	double p = 0.0;
	for (unsigned b = 0; b < 100; b++)
	{
		const double tempo = b < 50 ? 120.0 : 150.0;
		const plug_transport_t t = playing(tempo, p);
		block(&c, &t, b * 480u, 480);
		p += 480.0 * tempo / (60.0 * 48000.0);
	}
	ASSERT_EQ(count(0xFC) + count(0xF2) + count(0xFB), 0);
	/* 24,000 frames from 0 at 120 (24 clocks, the 24th at 24,000 is the next block's), then 800 a clock */
	ASSERT_EQ(evs[1].frame, 0);
	ASSERT_EQ(evs[24].frame, 23000);
	ASSERT_EQ(evs[26].frame, 24800);
	ASSERT_EQ(evs[27].frame, 25600);
}

TEST(P4_clock_no_context)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 20; b++)
	{
		plug_transport_t t = playing(120.0, (double)(b * 512u) / 24000.0);
		t.valid = b < 5 || b >= 10;
		if (b >= 10)
			t.playing = 0;
		block(&c, &t, b * 512u, 512);
	}
	ASSERT_EQ(count(0xFC), 1);
	ASSERT_EQ(evs[nev - 1].frame, 5u * 512u);
	plug_transport_t t = { 0 };
	nev = 0;
	plug_clockInit(&c, 48000.0);
	block(&c, &t, 0, 512);
	ASSERT_EQ(nev, 0);
}

TEST(P4_clock_count_in)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 60; b++)
	{
		const plug_transport_t t = playing(120.0, -1.0 + (double)(b * 512u) / 24000.0);
		block(&c, &t, b * 512u, 512);
	}
	ASSERT_EQ(evs[0].b[0], 0xFA);
	ASSERT_EQ(evs[0].frame, 24000);
	ASSERT_EQ(evs[1].b[0], 0xF8);
	ASSERT_EQ(evs[1].frame, 24000);
	ASSERT_EQ(evs[2].frame, 25000);
}

TEST(P4_clock_jitter)
{
	plug_clock_t c;
	plug_clockInit(&c, 44100.0);
	nev = 0;
	srand(7);
	const double fpq = 44100.0 * 60.0 / 137.0;
	for (unsigned b = 0; b < 10000; b++)
	{
		const double noise = ((double)rand() / RAND_MAX - 0.5) * 2e-9;
		const plug_transport_t t = playing(137.0, (double)(b * 128u) / fpq + noise);
		block(&c, &t, b * 128u, 128);
	}
	/* clock k at the frame nearest to k x per: the clocks before frame 1,280,000 */
	const double per = fpq / 24.0;
	const unsigned clocks = count(0xF8);
	ASSERT_EQ(clocks, (unsigned)floor((1280000.0 - 0.5) / per) + 1u);
	for (unsigned k = 0; k < clocks; k++)
		ASSERT_NEAR((double)evs[k + 1].frame, k * per, 1.0);
	ASSERT_EQ(count(0xFC) + count(0xF2) + count(0xFB), 0);
}

TEST(P4_clock_reset)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	for (unsigned b = 0; b < 20; b++)
	{
		if (b == 10)
			plug_clockReset(&c);	/* a new engine */
		const plug_transport_t t = playing(120.0, 3.0 + (double)(b * 512u) / 24000.0);
		block(&c, &t, b * 512u, 512);
	}
	ASSERT_EQ(count(0xF2), 2);
	ASSERT_EQ(count(0xFC), 0);	/* a new engine is stopped already */
	unsigned i = 1;
	while (evs[i].b[0] != 0xF2)
		i++;
	ASSERT_EQ(evs[i].frame, 5120);
	ASSERT_EQ(evs[i].b[1], 13);	/* ceil(4 x 3.2133) */
	ASSERT_EQ(evs[i + 1].b[0], 0xFB);
}

TEST(P4_clock_spp_bytes)
{
	plug_clock_t c;
	plug_clockInit(&c, 48000.0);
	nev = 0;
	plug_transport_t t = playing(120.0, 700.0);
	block(&c, &t, 0, 64);
	ASSERT_EQ(evs[0].b[1], 0x70);	/* 2,800 = 21 x 128 + 112 */
	ASSERT_EQ(evs[0].b[2], 0x15);
	plug_clockInit(&c, 48000.0);
	nev = 0;
	t = playing(120.0, 5000.0);
	block(&c, &t, 0, 64);
	ASSERT_EQ(evs[0].b[1], 0x7F);	/* 20,000 is past 16,383 */
	ASSERT_EQ(evs[0].b[2], 0x7F);
}

TEST(P4_clock_max)
{
	plug_clock_t c;
	plug_clockInit(&c, 8000.0);
	plug_midi_t out[4];
	const plug_transport_t t = playing(300.0, 0.0);
	ASSERT_EQ(plug_clockBlock(&c, &t, 8192, out, 4), 4);	/* 125 clocks would come */
	ASSERT_EQ(out[0].b[0], 0xFA);
}

TEST_MAIN()
```

  Append to `plugin/CMakeLists.txt`:

```cmake
target_sources(plug_common PRIVATE common/plug_clock.c)
target_sources(plug_common_san PRIVATE common/plug_clock.c)
plugin_add_test(test_clock IDS P4_clock_start_at_zero P4_clock_rounding_44k P4_clock_start_later P4_clock_stop
    P4_clock_loop P4_clock_locate P4_clock_tempo P4_clock_no_context P4_clock_count_in P4_clock_jitter
    P4_clock_reset P4_clock_spp_bytes P4_clock_max)
```

- [ ] **Step 2: run it.** PLUGIN with `-R P4`. Expected: `== plugin build: 1`; the configure stops at `Cannot find source file: common/plug_clock.c`.
- [ ] **Step 3: the generator** (P-13). `plugin/common/plug_clock.h`:

```c
/*
 * plug_clock.h
 *
 * The host's transport as MIDI clock (plan E9.2 Task 4): for each block, the MIDI Timing Clocks
 * (24 a quarter note), Start, Continue, Stop and Song Position Pointers that put the firmware's
 * sequencer, a MIDI slave, where the host's transport is. A pure function of the block's
 * transport and the generator's state: no allocation, no system call.
 *
 * - Clock k is at position k / 24 quarter notes. Its frame in a block that starts at position p0
 *   at tempo T is the nearest to (k / 24 - p0) x 60 x rate / T. Clocks go out in order, each
 *   once: one that falls before the block (the host's rounding) goes out at frame 0.
 * - Playing from position 0 (or from a count-in before it): Start, with the clock of position 0.
 * - Playing from p > 0: Song Position s = ceil(4p) sixteenths and Continue at the first frame,
 *   then the clocks from 16th s on (the slave's next clock plays step s, seq_core.h:752-765).
 * - A jump while playing, between blocks (the block's position is more than half a clock from
 *   the one expected) or at the loop's end inside a block: Stop, Song Position, Continue at the
 *   jump's frame, then the clocks from the new 16th on. The slave takes a Song Position only
 *   while stopped (seq_core.h:752-765).
 * - Stopped, or no tempo or position from the host: Stop once, then nothing.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_CLOCK_H_
#define PLUG_CLOCK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_CLOCK_PPQ 24
#define PLUG_SPP_MAX 16383	/**< the largest Song Position, in sixteenths */

/** the host's transport at a block's first frame */
typedef struct plug_transport
{
	int valid;			/**< the host gave the tempo and the musical position */
	int playing;
	double tempo;		/**< quarter notes per minute */
	double ppq;			/**< the musical position, quarter notes */
	int cycle;			/**< the loop is on and its bounds are given */
	double cycleStart, cycleEnd;	/**< quarter notes */
} plug_transport_t;

/** one MIDI message at a frame of the block */
typedef struct plug_midi
{
	uint32_t offset;
	uint8_t len;
	uint8_t b[3];
} plug_midi_t;

typedef struct plug_clock
{
	double rate;		/**< host frames per second */
	int running;		/**< Start or Continue went out */
	int startPending;	/**< Start goes out with the clock of position 0 */
	int64_t next;		/**< the index of the next clock */
	double expect;		/**< the position expected at the next block's first frame */
} plug_clock_t;

void plug_clockInit(plug_clock_t *c, double rate);

/** forget what went out: the next playing block starts the slave afresh (a new engine) */
void plug_clockReset(plug_clock_t *c);

/** The MIDI of one block of frames frames, in frame order, into out. Returns the count, at most
 * max (the rest is left out). */
unsigned plug_clockBlock(plug_clock_t *c, const plug_transport_t *t, uint32_t frames, plug_midi_t *out, unsigned max);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_CLOCK_H_ */
```

  and `plugin/common/plug_clock.c`:

```c
/*
 * plug_clock.c
 *
 * The host's transport as MIDI clock (plan E9.2 Task 4); see plug_clock.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_clock.h"

#include <math.h>

#define PLUG_JUMP (1.0 / 48.0)	/**< half a clock, in quarter notes: a larger difference is a jump */
#define PLUG_AT_ZERO 1e-6		/**< a start closer than this to position 0 is a start at 0 */

typedef struct
{
	plug_midi_t *out;
	unsigned n, max;
} sink_t;

static void put(sink_t *s, uint32_t offset, uint8_t a, uint8_t b, uint8_t c, uint8_t len)
{
	if (s->n < s->max)
		s->out[s->n++] = (plug_midi_t){ offset, len, { a, b, c } };
}

void plug_clockInit(plug_clock_t *c, double rate)
{
	c->rate = rate;
	plug_clockReset(c);
}

void plug_clockReset(plug_clock_t *c)
{
	c->running = 0;
	c->startPending = 0;
	c->next = 0;
	c->expect = 0.0;
}

/* the 16th that a start or a jump to p resumes at: the first at or after p */
static int sixteenth(double p)
{
	if (p <= 0.0)
		return 0;
	const double s = ceil(p * 4.0 - PLUG_AT_ZERO);
	return s > PLUG_SPP_MAX ? PLUG_SPP_MAX : (int)s;
}

/* Song Position s and Continue at offset; the clocks resume at 16th s */
static void resume(plug_clock_t *c, sink_t *s, uint32_t offset, double p)
{
	const int six = sixteenth(p);
	put(s, offset, 0xF2, (uint8_t)(six & 0x7F), (uint8_t)(six >> 7), 3);
	put(s, offset, 0xFB, 0, 0, 1);
	c->next = (int64_t)six * (PLUG_CLOCK_PPQ / 4);
	c->running = 1;
	c->startPending = 0;
}

/* the slave starts at p */
static void start(plug_clock_t *c, sink_t *s, uint32_t offset, double p)
{
	if (p < PLUG_AT_ZERO)
	{
		c->next = 0;	/* Start with the clock of position 0, also after a count-in */
		c->startPending = 1;
		c->running = 0;
	}
	else
		resume(c, s, offset, p);
}

/* the clocks whose frames fall in [f0, f1), position q0 at frame fo */
static void clocks(plug_clock_t *c, sink_t *s, double fpq, uint32_t fo, double q0, uint32_t f0, uint32_t f1)
{
	for (;;)
	{
		const double x = (double)fo + ((double)c->next / PLUG_CLOCK_PPQ - q0) * fpq;
		double off = floor(x + 0.5);
		if (off < (double)f0)
			off = (double)f0;	/* due before the block: now */
		if (off >= (double)f1)
			return;
		if (c->startPending && c->next == 0)
		{
			put(s, (uint32_t)off, 0xFA, 0, 0, 1);
			c->startPending = 0;
			c->running = 1;
		}
		put(s, (uint32_t)off, 0xF8, 0, 0, 1);
		c->next++;
	}
}

unsigned plug_clockBlock(plug_clock_t *c, const plug_transport_t *t, uint32_t frames, plug_midi_t *out, unsigned max)
{
	sink_t s = { out, 0, max };
	if (!t->valid || !t->playing || !(t->tempo > 0.0))
	{
		if (c->running)
			put(&s, 0, 0xFC, 0, 0, 1);
		c->running = 0;
		c->startPending = 0;
		return s.n;
	}
	const double fpq = c->rate * 60.0 / t->tempo;	/* frames per quarter note */
	const double p0 = t->ppq;
	if (!c->running && !c->startPending)
		start(c, &s, 0, p0);
	else if (fabs(p0 - c->expect) > PLUG_JUMP)
	{
		if (c->running)
		{
			put(&s, 0, 0xFC, 0, 0, 1);
			resume(c, &s, 0, p0);
		}
		else
			start(c, &s, 0, p0);	/* a jump during a count-in */
	}
	const double end = p0 + (double)frames / fpq;
	const double w = t->cycle && t->cycleEnd > t->cycleStart && p0 < t->cycleEnd ? floor((t->cycleEnd - p0) * fpq + 0.5) : -1.0;
	if (w >= 0.0 && w < (double)frames)
	{
		/* the loop's end inside the block: a jump at its frame. One at or after the block's end is
		 * the next block's jump, as when the host splits its blocks there */
		const uint32_t wf = (uint32_t)w;
		clocks(c, &s, fpq, 0, p0, 0, wf);
		if (c->running)
		{
			put(&s, wf, 0xFC, 0, 0, 1);
			resume(c, &s, wf, t->cycleStart);
		}
		else
			start(c, &s, wf, t->cycleStart);
		clocks(c, &s, fpq, wf, t->cycleStart, wf, frames);
		c->expect = t->cycleStart + (double)(frames - wf) / fpq;
	}
	else
	{
		clocks(c, &s, fpq, 0, p0, 0, frames);
		c->expect = end;
	}
	return s.n;
}
```

- [ ] **Step 4: pass.** PLUGIN with `-R P4`. Expected: `100% tests passed, 0 tests failed out of 13`. The cases' numbers, for the reviewer: 1,000 frames a clock at 120 BPM and 48 kHz; 918.75 at 44.1 kHz (clock 2 at 1,837.5 may round either way); from 1.1 quarter notes Song Position 5 and clock 30 at 3,600; the loop [4, 8) wraps at 96,000 with Stop, Song Position 16, Continue and clock 96 there, the last clock before it at 95,000, the same whether the block holds the end or ends on it; a locate from 10.24 to 2.3 gives Song Position 10 and clock 60 at 4,800 after the jump; 800 frames a clock at 150 BPM; a count-in from −1 gives Start and clock 0 at 24,000.
- [ ] **Step 5: the gate.** PLUGIN-CHECK (T 21). **Step 6: commit** `plugin Task 4 (E9.14): the host's transport as MIDI clock, Start, Continue, Stop and Song Position; P4` with the four files; push.

---

### Task 5: Frames, cycles, latency — E9.15

**Files:** Create `plugin/common/plug_time.h`, `plugin/common/plug_time.c`, `plugin/tests/test_time.c`; modify `plugin/CMakeLists.txt` (append).

- [ ] **Step 1: the failing test.** `plugin/tests/test_time.c`:

```c
/*
 * test_time.c
 *
 * P5: host frames, cycles and the latency (plan E9.2 Task 5).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <stdlib.h>

#include "plug_time.h"
#include "seqc_test.h"

TEST(P5_time_native)
{
	ASSERT_TRUE(plug_isNative(44140.625));
	ASSERT_FALSE(plug_isNative(44100.0));
	ASSERT_FALSE(plug_isNative(44140.0));
	/* emu_framesToCycles(): floor(frames x 552,960 / 113) */
	ASSERT_EQ(plug_framesToCycles(0, PLUG_ENGINE_RATE), 0);
	ASSERT_EQ(plug_framesToCycles(1, PLUG_ENGINE_RATE), 4893);
	ASSERT_EQ(plug_framesToCycles(64, PLUG_ENGINE_RATE), 313180);	/* 313,180.9 */
	ASSERT_EQ(plug_framesToCycles(113, PLUG_ENGINE_RATE), 552960);
	ASSERT_EQ(plug_framesToCycles(353125, PLUG_ENGINE_RATE), 8u * 216000000u);
	for (uint64_t f = 0; f < 100000; f += 7)
		ASSERT_EQ(plug_framesToCycles(f, PLUG_ENGINE_RATE), f * 552960u / 113u);
}

TEST(P5_time_integer)
{
	ASSERT_EQ(plug_framesToCycles(1, 48000.0), 4500);
	ASSERT_EQ(plug_framesToCycles(48000, 48000.0), 216000000u);
	ASSERT_EQ(plug_framesToCycles(1, 44100.0), 4897);	/* 4,897.96 */
	ASSERT_EQ(plug_framesToCycles(44100, 44100.0), 216000000u);
	ASSERT_EQ(plug_framesToCycles(10000000000ull, 96000.0), 22500000000000ull);
	ASSERT_EQ(plug_framesToCycles(2, 44100.5), 9795);	/* not an integer rate: 9,795.9 */
}

TEST(P5_time_covering)
{
	const double rates[] = { PLUG_ENGINE_RATE, 44100.0, 48000.0, 96000.0, 44100.5 };
	for (unsigned i = 0; i < 5; i++)
		for (uint64_t c = 0; c < 3000000; c += 997)
		{
			const uint64_t m = plug_framesCovering(c, rates[i]);
			ASSERT_TRUE(plug_framesToCycles(m, rates[i]) >= c);
			ASSERT_TRUE(m == 0 || plug_framesToCycles(m - 1, rates[i]) < c);
		}
	ASSERT_EQ(plug_framesCovering(0, 48000.0), 0);
	ASSERT_EQ(plug_framesCovering(1, 48000.0), 1);
	ASSERT_EQ(plug_framesCovering(4500, 48000.0), 1);
	ASSERT_EQ(plug_framesCovering(4501, 48000.0), 2);
}

TEST(P5_time_latency)
{
	ASSERT_EQ(plug_soundDelay(PLUG_ENGINE_RATE), 128);
	ASSERT_EQ(plug_readOffset(PLUG_ENGINE_RATE, 128, 128), 257);
	ASSERT_EQ(plug_latency(PLUG_ENGINE_RATE, 128, 128), 385);
	/* round(176 x rate / 44,140.625) */
	ASSERT_EQ(plug_soundDelay(44100.0), 176);	/* 175.84 */
	ASSERT_EQ(plug_soundDelay(48000.0), 191);	/* 191.39 */
	ASSERT_EQ(plug_soundDelay(88200.0), 352);	/* 351.68 */
	ASSERT_EQ(plug_soundDelay(96000.0), 383);	/* 382.78 */
	ASSERT_EQ(plug_readOffset(48000.0, 512, 128), 643);	/* the lag: ceil(1.087) + 1 */
	ASSERT_EQ(plug_readOffset(192000.0, 512, 128), 646);	/* ceil(4.35) + 1 */
	ASSERT_EQ(plug_latency(44100.0, 128, 128), 434);	/* 9.84 ms */
	ASSERT_EQ(plug_latency(48000.0, 256, 128), 578);
}

TEST(P5_time_margin)
{
	unsetenv("LICHOMAT_PLUGIN_MARGIN");
	ASSERT_EQ(plug_marginFromEnv(), 128);
	setenv("LICHOMAT_PLUGIN_MARGIN", "64", 1);
	ASSERT_EQ(plug_marginFromEnv(), 64);
	setenv("LICHOMAT_PLUGIN_MARGIN", "4096", 1);
	ASSERT_EQ(plug_marginFromEnv(), 4096);
	setenv("LICHOMAT_PLUGIN_MARGIN", "8", 1);
	ASSERT_EQ(plug_marginFromEnv(), 128);
	setenv("LICHOMAT_PLUGIN_MARGIN", "12x", 1);
	ASSERT_EQ(plug_marginFromEnv(), 128);
	unsetenv("LICHOMAT_PLUGIN_MARGIN");
}

TEST_MAIN()
```

  Append to `plugin/CMakeLists.txt`:

```cmake
target_sources(plug_common PRIVATE common/plug_time.c)
target_sources(plug_common_san PRIVATE common/plug_time.c)
plugin_add_test(test_time IDS P5_time_native P5_time_integer P5_time_covering P5_time_latency P5_time_margin)
```

- [ ] **Step 2: run it.** PLUGIN with `-R P5`. Expected: `== plugin build: 1`; the configure stops at `Cannot find source file: common/plug_time.c`.
- [ ] **Step 3: the code** (P-9, P-12). `plugin/common/plug_time.h`:

```c
/*
 * plug_time.h
 *
 * Host frames, the engine's virtual cycles and the plugin's latency (plan E9.2 Task 5).
 *
 * - The engine runs at 44,140.625 Hz (353,125 / 8), 216 MHz cycles. At exactly that host rate
 *   the plugin passes the engine's frames through ("native": no resampling, bit for bit the
 *   renderer's); at any other rate the engine resamples (eng_resample.c).
 * - An event at host frame m reaches the firmware at cycle t0 + plug_framesToCycles(m - epoch).
 *   It sounds 128 engine frames later (the voice queue, emu_onsets.py:43-45); resampled, the
 *   converter adds 48 engine frames (Normal complexity, prime method None: measured while
 *   planning, 47.9-48.1 frames at 44.1-96 kHz).
 * - The plugin reads the ring plug_readOffset() frames behind the frame it plays: the engine
 *   has the events of a block only after that block, and needs one block more of events than
 *   it renders (eng_run.c). The reported latency is the sound's delay plus that offset, so an
 *   event at host frame m sounds at frame m + plug_latency() of the plugin's output.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_TIME_H_
#define PLUG_TIME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_ENGINE_RATE (353125.0 / 8.0)	/**< 44,140.625 Hz */
#define PLUG_VOICE_DELAY 128u				/**< engine frames from an input to its sound */
#define PLUG_CONVERTER_DELAY 48u			/**< engine frames the converter adds */
#define PLUG_MARGIN_DEFAULT 128u			/**< host frames of slack for the engine's scheduling */

/** 1 when rate is exactly 44,140.625 Hz: no resampling */
int plug_isNative(double rate);

/** host frames from an event to its sound: 128, or round(176 x rate / 44,140.625) */
uint32_t plug_soundDelay(double rate);

/** How far behind the played frame the plugin reads the ring: maxBlock + margin + the engine's lag,
 * 1 frame native and ceil(rate / 44,140.625) + 1 resampled. With the events up to host frame H
 * published, the engine has written at least up to H - 1 engine frames (eng_run.c: a hook at block
 * request k needs the events up to 64k + 66 and then gives block k + 1, frames 64k + 64 to 64k + 127). */
uint32_t plug_readOffset(double rate, uint32_t maxBlock, uint32_t margin);

/** the reported latency: plug_soundDelay() + plug_readOffset() */
uint32_t plug_latency(double rate, uint32_t maxBlock, uint32_t margin);

/** host frames after the epoch to cycles after t0, rounded down; exact at the native rate and
 * at an integer rate */
uint64_t plug_framesToCycles(uint64_t frames, double rate);

/** the host frames after the epoch that cover cycles after t0: the smallest m with
 * plug_framesToCycles(m) >= cycles */
uint64_t plug_framesCovering(uint64_t cycles, double rate);

/** the margin: LICHOMAT_PLUGIN_MARGIN (16-4096 host frames) if set, else PLUG_MARGIN_DEFAULT */
uint32_t plug_marginFromEnv(void);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_TIME_H_ */
```

  and `plugin/common/plug_time.c`:

```c
/*
 * plug_time.c
 *
 * Host frames, cycles and the latency (plan E9.2 Task 5); see plug_time.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_time.h"

#include <math.h>
#include <stdlib.h>

#define PLUG_CPU_HZ 216000000ull

int plug_isNative(double rate)
{
	return rate == PLUG_ENGINE_RATE;
}

uint32_t plug_soundDelay(double rate)
{
	if (plug_isNative(rate))
		return PLUG_VOICE_DELAY;
	return (uint32_t)floor((double)(PLUG_VOICE_DELAY + PLUG_CONVERTER_DELAY) * rate / PLUG_ENGINE_RATE + 0.5);
}

uint32_t plug_readOffset(double rate, uint32_t maxBlock, uint32_t margin)
{
	const uint32_t lag = plug_isNative(rate) ? 1u : (uint32_t)ceil(rate / PLUG_ENGINE_RATE) + 1u;
	return maxBlock + margin + lag;
}

uint32_t plug_latency(double rate, uint32_t maxBlock, uint32_t margin)
{
	return plug_soundDelay(rate) + plug_readOffset(rate, maxBlock, margin);
}

static int integerRate(double rate)
{
	return rate == floor(rate) && rate >= 1.0 && rate <= 1e7;
}

uint64_t plug_framesToCycles(uint64_t frames, double rate)
{
	if (plug_isNative(rate))	/* emu_framesToCycles(): a frame is 552,960 / 113 cycles */
		return frames / 113u * 552960u + frames % 113u * 552960u / 113u;
	if (integerRate(rate))
		return (uint64_t)((unsigned __int128)frames * PLUG_CPU_HZ / (uint64_t)rate);
	return (uint64_t)floor((double)frames * ((double)PLUG_CPU_HZ / rate));
}

uint64_t plug_framesCovering(uint64_t cycles, double rate)
{
	uint64_t m;
	if (plug_isNative(rate))
		m = (uint64_t)(((unsigned __int128)cycles * 113u + 552959u) / 552960u);
	else if (integerRate(rate))
		m = (uint64_t)(((unsigned __int128)cycles * (uint64_t)rate + PLUG_CPU_HZ - 1u) / PLUG_CPU_HZ);
	else
		m = (uint64_t)ceil((double)cycles * rate / (double)PLUG_CPU_HZ);
	while (plug_framesToCycles(m, rate) < cycles)
		m++;
	while (m > 0 && plug_framesToCycles(m - 1u, rate) >= cycles)
		m--;
	return m;
}

uint32_t plug_marginFromEnv(void)
{
	const char *v = getenv("LICHOMAT_PLUGIN_MARGIN");
	if (!v || !*v)
		return PLUG_MARGIN_DEFAULT;
	char *end;
	const long m = strtol(v, &end, 10);
	return (*end == '\0' && m >= 16 && m <= 4096) ? (uint32_t)m : PLUG_MARGIN_DEFAULT;
}
```

- [ ] **Step 4: pass.** PLUGIN with `-R P5`. Expected: `100% tests passed, 0 tests failed out of 5`.
- [ ] **Step 5: the gate.** PLUGIN-CHECK (T 26). **Step 6: commit** `plugin Task 5 (E9.15): host frames, cycles and the latency; P5` with the four files; push. **Review: light** (group B), with the mutants of the Rules.

---

### Task 6: `lichomat-engine`, native and resampled — E9.16

**Files:** Create `plugin/common/plug_spawn.h`, `plugin/common/plug_spawn.c`, `plugin/common/plug_notices.h`, `plugin/engine/eng_run.h`, `plugin/engine/eng_run.c`, `plugin/engine/eng_main.c`, `plugin/engine/eng_resample.h`, `plugin/engine/eng_resample.c`, `plugin/tests/test_resample.c`, `plugin/tests/plug_drive.c`, `plugin/tests/plug_resample_wav.c`, `plugin/tests/plugin_test.py`; modify `plugin/CMakeLists.txt` (append).

- [ ] **Step 1: the failing tests.** The converter's unit test, `plugin/tests/test_resample.c`:

```c
/*
 * test_resample.c
 *
 * P7: the engine's sample-rate conversion (plan E9.2 Task 6): the converter's delay, which
 * plug_soundDelay() reports, and its frame count, at the rates Live uses.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <stdlib.h>

#include "eng_resample.h"
#include "plug_time.h"
#include "seqc_test.h"

#define BLOCKS 400u

static float *out;
static uint32_t nout;

static void collect(void *ctx, const float *lr, uint32_t frames)
{
	(void)ctx;
	memcpy(&out[2u * nout], lr, 2u * frames * sizeof *lr);
	nout += frames;
}

/* an impulse at input frame 1,000 through the converter, 64 frames a push: the output's peak
 * (between frames, by a parabola) less 1,000 x r, in output frames */
static double delayAt(double rate)
{
	eng_resample_t r;
	char err[160];
	if (eng_resampleOpen(&r, rate, err, sizeof err) != 0)
		return -1.0;
	out = calloc(2u * 8u * 64u * BLOCKS, sizeof *out);
	nout = 0;
	for (unsigned b = 0; b < BLOCKS; b++)
	{
		float in[128] = { 0 };
		if (b == 1000u / 64u)
			in[2u * (1000u % 64u)] = in[2u * (1000u % 64u) + 1u] = 1.0f;
		if (eng_resamplePush(&r, in, collect, NULL) != 0)
			return -1.0;
	}
	eng_resampleClose(&r);
	uint32_t peak = 1;
	for (uint32_t i = 1; i + 1 < nout; i++)
		if (fabsf(out[2u * i]) > fabsf(out[2u * peak]))
			peak = i;
	const double a = out[2u * (peak - 1u)], b = out[2u * peak], c = out[2u * (peak + 1u)];
	const double frac = 0.5 * (a - c) / (a - 2.0 * b + c);
	const double ratio = rate / PLUG_ENGINE_RATE;
	printf("P7_converter: %.0f Hz: %u frames out of %u (%.1f), delay %.2f output frames = %.2f input frames\n", rate,
		nout, BLOCKS * 64u, BLOCKS * 64u * ratio, (double)peak + frac - 1000.0 * ratio,
		((double)peak + frac - 1000.0 * ratio) / ratio);
	const double expectFrames = BLOCKS * 64u * ratio;
	free(out);
	if (fabs((double)nout - expectFrames) > 1.0)
		return -2.0;
	return ((double)peak + frac - 1000.0 * ratio) / ratio;
}

TEST(P7_converter_delay)
{
	const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
	for (unsigned i = 0; i < 4; i++)
	{
		const double d = delayAt(rates[i]);
		ASSERT_TRUE(d >= (double)PLUG_CONVERTER_DELAY - 0.5 && d <= (double)PLUG_CONVERTER_DELAY + 0.5);
	}
}

TEST(P7_converter_open)
{
	eng_resample_t r;
	char err[160];
	ASSERT_EQ(eng_resampleOpen(&r, 48000.0, err, sizeof err), 0);
	eng_resampleClose(&r);
	ASSERT_EQ(eng_resampleOpen(&r, -1.0, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "AudioConverterNew") != NULL);
}

TEST_MAIN()
```

  The engine's driver, the plugin's side of the shared memory without VST3, `plugin/tests/plug_drive.c`:

```c
/*
 * plug_drive.c
 *
 * The engine's test driver (plan E9.2 Task 6): the plugin's side of the shared memory without
 * the VST 3 layer. It starts lichomat-engine on a card, waits until it runs (epoch 0), then
 * plays a script of lichomat-render's usb lines ("<frame>f usb XX ..", host frames) in blocks,
 * and writes the ring's frames [0, FRAMES) as a 16-bit stereo WAV.
 *
 *   plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN]
 *
 * Without realtime it waits for each frame; with it, it paces the blocks by the wall clock,
 * reads each block plug_readOffset(RATE, BLOCK, MARGIN) frames behind as the plugin does, and
 * counts the underruns.
 * At 44,140.625 Hz a sample is floor(x x 32,768), the renderer's --stereo sum exactly.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "plug_shm.h"
#include "plug_spawn.h"
#include "plug_time.h"

typedef struct
{
	uint64_t frame;
	unsigned n;
	uint8_t b[64];
} line_t;

static double wall(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static unsigned msgLen(uint8_t st)
{
	if (st >= 0x80u && st < 0xF0u)
		return ((st >> 4) == 0xC || (st >> 4) == 0xD) ? 2u : 3u;
	if (st == 0xF2u)
		return 3u;
	if (st == 0xF1u || st == 0xF3u)
		return 2u;
	return 1u;
}

static void put16(FILE *f, unsigned v)
{
	fputc((int)(v & 255u), f);
	fputc((int)((v >> 8) & 255u), f);
}

static void put32(FILE *f, uint32_t v)
{
	put16(f, v & 0xFFFFu);
	put16(f, v >> 16);
}

static int16_t sample(float x, int native)
{
	const double v = native ? floor((double)x * 32768.0) : floor((double)x * 32768.0 + 0.5);
	return (int16_t)(v > 32767.0 ? 32767.0 : v < -32768.0 ? -32768.0 : v);
}

int main(int argc, char **argv)
{
	if (argc != 8 && !(argc == 10 && !strcmp(argv[8], "realtime")))
	{
		fprintf(stderr, "usage: plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN]\n");
		return 1;
	}
	const double rate = strtod(argv[3], NULL);
	const uint32_t block = (uint32_t)strtoul(argv[4], NULL, 10);
	const uint64_t frames = strtoull(argv[6], NULL, 10);
	const int realtime = argc == 10;
	const uint32_t margin = realtime ? (uint32_t)strtoul(argv[9], NULL, 10) : 0u;
	const int native = plug_isNative(rate);
	/* the script */
	FILE *sf = fopen(argv[5], "r");
	if (!sf)
	{
		perror(argv[5]);
		return 1;
	}
	line_t *lines = NULL;
	unsigned nlines = 0;
	char text[1024];
	while (fgets(text, sizeof text, sf))
	{
		unsigned long long f;
		char *p = strstr(text, "f usb");
		if (text[0] == '#' || !p || sscanf(text, "%llu", &f) != 1)
			continue;
		lines = realloc(lines, (nlines + 1u) * sizeof *lines);
		line_t *l = &lines[nlines++];
		l->frame = f;
		l->n = 0;
		p += 5;
		unsigned v;
		int k;
		while (l->n < sizeof l->b && sscanf(p, " %x%n", &v, &k) == 1)
		{
			l->b[l->n++] = (uint8_t)v;
			p += k;
		}
	}
	fclose(sf);
	/* the shared memory, the pipes, the engine */
	char err[256];
	int shmFd = -1;
	plug_shm_t *s = plug_shmCreate(rate, block, &shmFd, err, sizeof err);
	if (!s)
	{
		fprintf(stderr, "plug_drive: %s\n", err);
		return 1;
	}
	s->realtime = (uint32_t)realtime;
	int wake[2], life[2];
	if (pipe(wake) || pipe(life) || fcntl(wake[1], F_SETFL, O_NONBLOCK))
		return 1;
	signal(SIGPIPE, SIG_IGN);
	const double w0 = wall();
	const pid_t pid = plug_spawnEngine(argv[1], argv[2], wake[0], life[1], shmFd, NULL, err, sizeof err);
	if (pid < 0)
	{
		fprintf(stderr, "plug_drive: %s\n", err);
		return 1;
	}
	close(wake[0]);
	close(life[1]);
	while (plug_load(&s->state) != PLUG_ENGINE_RUNNING)
	{
		struct pollfd p = { life[0], POLLIN, 0 };
		if (poll(&p, 1, 5) > 0 || plug_load(&s->state) == PLUG_ENGINE_FAILED || wall() - w0 > 60.0)
		{
			fprintf(stderr, "plug_drive: the engine did not start (state %llu)\n", (unsigned long long)s->state);
			return 2;
		}
	}
	const double wRun = wall();
	/* play */
	float *l = calloc(frames + block, sizeof *l), *r = calloc(frames + block, sizeof *r);
	uint64_t n = 0, got = 0, firstMiss = 0, lastMiss = 0;
	unsigned next = 0;
	const uint32_t lp = plug_readOffset(rate, block, margin);
	while (got < frames)
	{
		/* waiting: at most 8,192 frames ahead of the engine's audio, so that neither ring overflows
		 * and the engine has no more to play than the frames asked for */
		while (!realtime && n + block > plug_load(&s->audioEnd) + 8192u)
		{
			struct pollfd p = { life[0], POLLIN, 0 };
			if (poll(&p, 1, 0) > 0)
				break;
			const uint64_t end = plug_load(&s->audioEnd);
			if (end > got)
			{
				const uint64_t to = end < frames ? end : frames;
				(void)plug_audioRead(s, got, (uint32_t)(to - got), &l[got], &r[got]);
				got = to;
			}
			nanosleep(&(const struct timespec){ 0, 50000L }, NULL);
		}
		while (next < nlines && lines[next].frame < n + block)
		{
			const line_t *li = &lines[next++];
			for (unsigned i = 0; i < li->n;)
			{
				const unsigned len = msgLen(li->b[i]);
				(void)plug_evPush(s, li->frame, &li->b[i], (uint8_t)len);
				i += len;
			}
		}
		plug_publish(s, n + block);
		(void)!write(wake[1], "w", 1);
		if (realtime)
		{
			/* the plugin's read of the block [n, n + block): plug_readOffset() frames behind */
			if (n >= lp)
			{
				const uint64_t from = n - lp;
				const uint32_t take = from + block > frames ? (uint32_t)(frames > from ? frames - from : 0u) : block;
				const uint64_t before = s->underruns;
				if (take)
					(void)plug_audioRead(s, from, take, &l[from], &r[from]);
				if (s->underruns != before)
				{
					if (!firstMiss)
						firstMiss = from;
					lastMiss = from;
				}
				got = from + take;
			}
			const double due = wRun + (double)(n + block) / rate;
			while (wall() < due)
				nanosleep(&(const struct timespec){ 0, 100000L }, NULL);
		}
		else
		{
			const uint64_t end = plug_load(&s->audioEnd);
			if (end > got)
			{
				const uint64_t to = end < frames ? end : frames;
				(void)plug_audioRead(s, got, (uint32_t)(to - got), &l[got], &r[got]);
				got = to;
			}
		}
		n += block;
		struct pollfd p = { life[0], POLLIN, 0 };
		if (poll(&p, 1, 0) > 0)
		{
			fprintf(stderr, "plug_drive: the engine died at host frame %llu\n", (unsigned long long)n);
			return 3;
		}
	}
	const double wEnd = wall();
	plug_store(&s->quit, 1);
	close(wake[1]);
	int status = 0;
	waitpid(pid, &status, 0);
	FILE *o = fopen(argv[7], "wb");
	const uint32_t data = (uint32_t)(frames * 4u);
	fwrite("RIFF", 1, 4, o);
	put32(o, 36u + data);
	fwrite("WAVEfmt ", 1, 8, o);
	put32(o, 16);
	put16(o, 1);
	put16(o, 2);
	const uint32_t hz = native ? 44141u : (uint32_t)rate;
	put32(o, hz);
	put32(o, hz * 4u);
	put16(o, 4);
	put16(o, 16);
	fwrite("data", 1, 4, o);
	put32(o, data);
	for (uint64_t i = 0; i < frames; i++)
	{
		put16(o, (uint16_t)sample(l[i], native));
		put16(o, (uint16_t)sample(r[i], native));
	}
	fclose(o);
	printf("plug_drive: epoch %llu, %llu frames, late %llu, dropped %llu, ring full %llu, underruns %llu (blocks from "
		"%llu to %llu); start %.3f s, play %.3f s (%.1fx real time), engine exit %d\n",
		(unsigned long long)s->epoch, (unsigned long long)frames, (unsigned long long)s->late,
		(unsigned long long)s->dropped, (unsigned long long)s->evDropped, (unsigned long long)s->underruns,
		(unsigned long long)firstMiss, (unsigned long long)lastMiss, wRun - w0, wEnd - wRun,
		(double)frames / rate / (wEnd - wRun), WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	return 0;
}
```

  The reference for the resampled cases, `plugin/tests/plug_resample_wav.c`:

```c
/*
 * plug_resample_wav.c
 *
 * A 16-bit stereo WAV at 44,140.625 Hz (lichomat-render's --stereo sum) through the engine's
 * converter, 64 frames a push, into a 16-bit WAV at RATE (plan E9.2 Task 6). P7 compares the
 * engine's resampled output with this.
 *
 *   plug_resample_wav IN.wav RATE OUT.wav FRAMES
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eng_resample.h"

static FILE *o;
static uint64_t left;

static void put16(unsigned v)
{
	fputc((int)(v & 255u), o);
	fputc((int)((v >> 8) & 255u), o);
}

static void put32(uint32_t v)
{
	put16(v & 0xFFFFu);
	put16(v >> 16);
}

static void emit(void *ctx, const float *lr, uint32_t frames)
{
	(void)ctx;
	for (uint32_t i = 0; i < 2u * frames && left; i += 2)
	{
		for (unsigned c = 0; c < 2; c++)
		{
			double v = floor((double)lr[i + c] * 32768.0 + 0.5);
			v = v > 32767.0 ? 32767.0 : v < -32768.0 ? -32768.0 : v;
			put16((uint16_t)(int16_t)v);
		}
		left--;
	}
}

int main(int argc, char **argv)
{
	if (argc != 5)
	{
		fprintf(stderr, "usage: plug_resample_wav IN.wav RATE OUT.wav FRAMES\n");
		return 1;
	}
	FILE *in = fopen(argv[1], "rb");
	unsigned char h[44];
	if (!in || fread(h, 1, 44, in) != 44 || memcmp(h, "RIFF", 4) || h[22] != 2 || h[34] != 16)
	{
		fprintf(stderr, "plug_resample_wav: %s is not a 16-bit stereo WAV\n", argv[1]);
		return 1;
	}
	const double rate = strtod(argv[2], NULL);
	const uint64_t frames = strtoull(argv[4], NULL, 10);
	eng_resample_t r;
	char err[160];
	if (eng_resampleOpen(&r, rate, err, sizeof err) != 0)
	{
		fprintf(stderr, "plug_resample_wav: %s\n", err);
		return 1;
	}
	o = fopen(argv[3], "wb");
	const uint32_t data = (uint32_t)(frames * 4u);
	fwrite("RIFF", 1, 4, o);
	put32(36u + data);
	fwrite("WAVEfmt ", 1, 8, o);
	put32(16);
	put16(1);
	put16(2);
	put32((uint32_t)rate);
	put32((uint32_t)rate * 4u);
	put16(4);
	put16(16);
	fwrite("data", 1, 4, o);
	put32(data);
	left = frames;
	int16_t s[128];
	size_t got;
	while (left && (got = fread(s, 4, 64, in)) > 0)
	{
		float lr[128] = { 0 };
		for (size_t i = 0; i < 2u * got; i++)
			lr[i] = (float)s[i] / 32768.0f;
		if (eng_resamplePush(&r, lr, emit, NULL) != 0)
			return 1;
	}
	while (left)	/* past the input's end: silence */
	{
		const float lr[128] = { 0 };
		if (eng_resamplePush(&r, lr, emit, NULL) != 0)
			return 1;
	}
	eng_resampleClose(&r);
	fclose(o);
	return 0;
}
```

  The scenarios, `plugin/tests/plugin_test.py` (Tasks 8–10 add their sections before `def main():`):

```python
#!/usr/bin/env python3
"""The LICHOMAT plugin's scenario tests (plan E9.2): the engine through plug_drive against
lichomat-render, and (from Task 9) the VST3 bundle through lichomat-plughost.

    plugin_test.py --root R --build B --render R --engine E --drive D [--host H --bundle V] --work W CASE

Each case prints what it measured and exits 0 when it passes, 1 when it fails.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import argparse
import array
import fractions
import math
import os
import shutil
import signal
import subprocess
import sys
import time
import wave

RATE = fractions.Fraction(353125, 8)    # 44,140.625 Hz
RATE_TEXT = '44140.625'
LOUD, QUIET, QUIET_FRAMES = 4000, 64, 32
CASES = {}


class Fail(Exception):
    pass


def case(fn):
    CASES[fn.__name__] = fn
    return fn


def check(cond, what):
    if not cond:
        raise Fail(what)


class T:
    def __init__(self, a):
        self.a = a
        self.work = os.path.join(a.work, a.case)
        shutil.rmtree(self.work, ignore_errors=True)
        os.makedirs(self.work)

    def path(self, name):
        return os.path.join(self.work, name)

    def card(self, tag, name='onset'):
        """a fresh copy of the committed card fixture name, as card-TAG"""
        dst = self.path('card-' + tag)
        ordered_copy(os.path.join(self.a.root, 'emu', 'tests', 'cards', name), dst)
        return dst

    def run(self, cmd, timeout=600, ok=(0,)):
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        if p.returncode not in ok:
            raise Fail('%s exited %d:\n%s%s' % (os.path.basename(cmd[0]), p.returncode, p.stdout, p.stderr))
        return p

    def render(self, card, script, seconds, out):
        self.run([self.a.render, '--card', card, '--bpm', 'mid', '--no-start', '--as-booted', '--stereo', 'sum',
                  '--seconds', str(seconds), '--events', script, '--out', out])

    def drive(self, card, rate, block, script, frames, out, realtime=None):
        cmd = [self.a.drive, self.a.engine, card, str(rate), str(block), script, str(frames), out]
        if realtime is not None:
            cmd += ['realtime', str(realtime)]
        line = self.run(cmd).stdout.strip()
        print(line)
        return parse_drive(line)


def ordered_copy(src, dst):
    """copy a card folder in the card's order (emu_hostfs.h:38-42: birth time, then name), so that the
    copy's birth times keep it, as emu_hostCopyCard() does"""
    os.makedirs(dst)
    entries = sorted(os.scandir(src), key=lambda e: (e.stat().st_birthtime, e.name))
    for e in entries:
        if e.name.startswith('.'):
            continue
        if e.is_dir():
            ordered_copy(e.path, os.path.join(dst, e.name))
        else:
            shutil.copyfile(e.path, os.path.join(dst, e.name))


def parse_drive(line):
    """the counters of plug_drive's summary line"""
    out = {}
    for part in line.replace(';', ',').split(','):
        w = part.split()
        for i in range(len(w) - 1):
            if w[i] in ('epoch', 'late', 'dropped', 'underruns', 'full'):
                try:
                    out[w[i]] = int(w[i + 1])
                except ValueError:
                    pass
    return out


def blocks_frames(seconds):
    """lichomat-render's length: whole blocks of 64 frames, rounded up"""
    f = -(-fractions.Fraction(seconds) * RATE // 1)
    return int(-(-f // 64) * 64)


def script_events():
    """The engine's test script (P6, P7) as {time in seconds (a Fraction): [messages]}: USB MIDI clock at
    128 BPM with Start at 4,410 frames, 140 BPM from beat 8, a loop jump at beat 12 back to beat 4 (Stop,
    Song Position 16, Continue with the clock), 12.6 s of clocks; Note Ons and Offs on channel 1, note 48 (the
    onset card's Drum 1 and the voices on out 2), at irregular frames and on and around block boundaries
    (64k - 1, 64k, 64k + 1 at 44,140.625 Hz)."""
    msgs = {}

    def add(f, b):
        msgs.setdefault(fractions.Fraction(f) / RATE, []).append(b)
    t, beat, first, jumped = fractions.Fraction(4410), fractions.Fraction(0), True, False
    while beat < 20:
        bpm = 128 if beat < 8 else 140
        f = int(t)
        if first:
            add(f, 'FA F8')
            first = False
        elif beat == 12 and not jumped:
            add(f, 'FC F2 10 00 FB F8')
            jumped = True
            beat = fractions.Fraction(4)
        else:
            add(f, 'F8')
        t += RATE * 60 / (bpm * 24)
        beat += fractions.Fraction(1, 24)
    notes = [9000, 15000, 22222, 30001, 41234, 52345, 60000, 71111, 64 * 1500 - 1, 64 * 1600, 64 * 1700 + 1,
             64 * 2000, 64 * 3333, 64 * 4000 - 1, 500003, 555555]
    for i, f in enumerate(notes):
        add(f, '90 30 7F')
        add(f + 3000 + 7 * i, '80 30 00')
    return msgs


# the frames of a host rate whose times are whole microseconds, which a script line's time "S.UUUUUUs" gives
# the renderer exactly (render_script.c:101-125): 6 frames at 48 kHz (125 us), 12 at 96 kHz, 441 at 44.1 kHz
GRID = {44100: 441, 48000: 6, 96000: 12}


def write_script(t, name, rate=None, render=False):
    """The script at 44,140.625 Hz in frames (rate None); at a host rate, in host frames on GRID[rate] (for
    plug_drive), or with render, the same times in seconds (for lichomat-render). One line per time: one USB
    transfer each, in both."""
    merged = {}
    for s, b in script_events().items():
        if rate is None:
            key = int(s * RATE)
        else:
            g = GRID[rate]
            key = int(s * rate / g + fractions.Fraction(1, 2)) * g
        merged.setdefault(key, []).extend(b)
    lines = []
    for k in sorted(merged):
        when = '%df' % k
        if rate is not None and render:
            us = fractions.Fraction(k * 1000000, rate)
            check(us.denominator == 1, 'frame %d at %d Hz is not a whole microsecond' % (k, rate))
            when = '%d.%06ds' % (us.numerator // 1000000, us.numerator % 1000000)
        lines.append('%s usb %s' % (when, ' '.join(merged[k])))
    p = t.path(name)
    with open(p, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return p


def wav(path):
    """(rate, channels, the samples interleaved)"""
    with wave.open(path, 'rb') as w:
        x = array.array('h')
        x.frombytes(w.readframes(w.getnframes()))
        if sys.byteorder == 'big':
            x.byteswap()
        return w.getframerate(), w.getnchannels(), x


def onsets(x):
    """emu_onsets.py's: the first frame with |x| >= 4,000 after at least 32 frames below 64"""
    out, quiet = [], 0
    for f, v in enumerate(x):
        a = -v if v < 0 else v
        if a >= LOUD and quiet >= QUIET_FRAMES:
            out.append(f)
            quiet = 0
        elif a < QUIET:
            quiet += 1
        else:
            quiet = 0
    return out


def attacks(x, loud=1000, still=500, ramp=4, gap=32):
    """onsets of a kit whose tails stay above 64 and whose attacks rise over a few frames: the first frame at loud
    or more, after a run of gap frames below still that ended at most ramp frames before it, once per attack"""
    out, run, runs, was = [], 0, [], False
    for f, v in enumerate(x):
        a = -v if v < 0 else v
        runs.append(run)    # the quiet frames right before f
        if a >= loud and not was and (not out or f - out[-1] > gap) and max(runs[max(0, f - ramp):f + 1]) >= gap:
            out.append(f)
        was = a >= loud
        run = run + 1 if a < still else 0
    return out


def same_files(a, b):
    with open(a, 'rb') as f, open(b, 'rb') as g:
        x, y = f.read(), g.read()
    if x == y:
        return None
    n = min(len(x), len(y))
    i = next((k for k in range(n) if x[k] != y[k]), n)
    return 'differ from byte %d (frame %d) of %d, %d' % (i, (i - 44) // 4, len(x), len(y))


# --- the engine (Task 6) ------------------------------------------------------------------

@case
def P6_engine_version(t):
    out = t.run([t.a.engine, '--version']).stdout
    print(out.strip())
    check(out.startswith('lichomat-engine 0.1.0, the engine of the LICHOMAT plugin\n'), 'the first line')
    check('Not affiliated with Erica Synths.' in out, 'the notices')
    p = subprocess.run([t.a.engine, '--bogus'], capture_output=True, text=True)
    check(p.returncode == 1 and p.stderr.startswith('usage: lichomat-engine'), 'usage: %d %r' % (p.returncode, p.stderr))


@case
def P6_engine_native(t):
    # the engine through plug_drive at 44,140.625 Hz in blocks of 128 is lichomat-render, byte for byte
    script = write_script(t, 'script.txt')
    frames = blocks_frames(14)
    t.render(t.card('render'), script, 14, t.path('render.wav'))
    s = t.drive(t.card('drive'), RATE_TEXT, 128, script, frames, t.path('drive.wav'))
    check(s.get('epoch') == 0 and s.get('late') == 0 and s.get('dropped') == 0, 'counters %r' % s)
    d = same_files(t.path('render.wav'), t.path('drive.wav'))
    check(d is None, 'the engine and the renderer %s' % d)
    _, _, x = wav(t.path('render.wav'))
    on = onsets(x[0::2])
    print('render == drive: %d frames, %d onsets in L' % (frames, len(on)))
    check(len(on) > 20, 'only %d onsets' % len(on))


@case
def P6_engine_blocks(t):
    # the block size does not change the output: 1, 37, 512 and 4,096 frames
    script = write_script(t, 'script.txt')
    frames = blocks_frames(3)
    t.render(t.card('render'), script, 3, t.path('render.wav'))
    for b in (1, 37, 512, 4096):
        out = t.path('drive-%d.wav' % b)
        t.drive(t.card('b%d' % b), RATE_TEXT, b, script, frames, out)
        d = same_files(t.path('render.wav'), out)
        check(d is None, 'blocks of %d: %s' % (b, d))
    print('blocks of 1, 37, 512, 4096: each == render')


@case
def P6_engine_seed(t):
    # an empty card folder: the engine seeds it as lichomat-render --seed-card does, then plays it
    seeded = t.path('seeded')
    t.run([t.a.render, '--seed-card', seeded])
    empty = t.path('empty')
    os.makedirs(empty)
    script = write_script(t, 'script.txt')
    frames = blocks_frames(3)
    t.render(seeded, script, 3, t.path('render.wav'))
    t.drive(empty, RATE_TEXT, 256, script, frames, t.path('drive.wav'))
    check(os.path.isfile(os.path.join(empty, 'PROJ00', 'PRJ.NFO')), 'no PROJ00/PRJ.NFO in the seeded folder')
    d = same_files(t.path('render.wav'), t.path('drive.wav'))
    check(d is None, 'seeded by the engine vs by the renderer: %s' % d)
    print('the engine seeded %s: %s' % (empty, sorted(os.listdir(empty))))


@case
def P6_engine_quit(t):
    # plug_drive ends the engine (quit, its pipe closed: exit 0); a killed driver leaves no engine behind
    script = write_script(t, 'script.txt')
    s = t.run([t.a.drive, t.a.engine, t.card('q'), RATE_TEXT, '128', script, '44160', t.path('q.wav')]).stdout
    check('engine exit 0' in s, s)
    card = t.card('k')
    p = subprocess.Popen([t.a.drive, t.a.engine, card, RATE_TEXT, '128', script, str(blocks_frames(60)), t.path('k.wav'),
                          'realtime', '128'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(2.0)
    engines = subprocess.run(['pgrep', '-f', 'lichomat-engine --card ' + card], capture_output=True, text=True).stdout.split()
    check(len(engines) == 1, 'engines while the driver runs: %r' % engines)
    p.send_signal(signal.SIGKILL)
    p.wait()
    t0 = time.time()
    while time.time() - t0 < 3.0:
        left = subprocess.run(['pgrep', '-f', 'lichomat-engine --card ' + card], capture_output=True, text=True).stdout.split()
        if not left:
            break
        time.sleep(0.05)
    print('the engine ended %.2f s after its driver was killed' % (time.time() - t0))
    check(not left, 'the engine outlived its driver: %r' % left)


@case
def P7_engine_resampled(t):
    # 44.1, 48 and 96 kHz: the engine's output is the renderer's for the same input times through the same
    # converter, 64 frames a push (plug_resample_wav), within 2 LSB: the renderer's WAV rounds (L1 + L2) / 2
    # down, the engine converts (L1 + L2) / 65,536. A frame out of place would differ by thousands. The
    # input times lie on GRID, which both reach at the same cycle.
    seconds = 6
    frames441 = blocks_frames(seconds)
    for rate in (44100, 48000, 96000):
        t.render(t.card('render%d' % rate), write_script(t, 'render%d.txt' % rate, rate, render=True), seconds,
                 t.path('render%d.wav' % rate))
        frames = int(frames441 * fractions.Fraction(rate) / RATE) - 256
        s = t.drive(t.card('r%d' % rate), str(rate), 256, write_script(t, 'drive%d.txt' % rate, rate), frames,
                    t.path('drive%d.wav' % rate))
        check(s.get('late') == 0 and s.get('epoch') == 0, 'counters %r' % s)
        t.run([t.a.resample, t.path('render%d.wav' % rate), str(rate), t.path('ref%d.wav' % rate), str(frames)])
        _, _, a = wav(t.path('drive%d.wav' % rate))
        _, _, b = wav(t.path('ref%d.wav' % rate))
        check(len(a) == len(b), '%d Hz: %d and %d samples' % (rate, len(a), len(b)))
        worst = max(abs(x - y) for x, y in zip(a, b))
        loud = sum(1 for x in a if abs(x) >= 4000)
        print('%d Hz: %d frames, %d samples at 4,000 or more, the largest difference %d LSB' % (rate, frames, loud, worst))
        check(worst <= 2 and loud > 1000, '%d Hz: the largest difference %d LSB' % (rate, worst))


@case
def P7_engine_realtime(t):
    # paced like a host at 48 kHz in blocks of 128 with the default margin, four engines at once: no underrun
    script = write_script(t, 'script48.txt', 48000)
    frames = 48000 * 10
    procs = []
    for i in range(4):
        procs.append(subprocess.Popen([t.a.drive, t.a.engine, t.card('rt%d' % i), '48000', '128', script, str(frames),
                                       t.path('rt%d.wav' % i), 'realtime', '128'], stdout=subprocess.PIPE, text=True))
    for i, p in enumerate(procs):
        out, _ = p.communicate(timeout=120)
        print('engine %d: %s' % (i, out.strip()))
        s = parse_drive(out)
        check(p.returncode == 0 and s.get('underruns') == 0, 'engine %d: %s' % (i, out))


def main():
    ap = argparse.ArgumentParser()
    for o in ('root', 'build', 'render', 'engine', 'drive', 'resample', 'work'):
        ap.add_argument('--' + o, required=True)
    for o in ('host', 'bundle', 'validator', 'rtcheck'):
        ap.add_argument('--' + o)
    ap.add_argument('case', choices=sorted(CASES))
    a = ap.parse_args()
    t = T(a)
    try:
        CASES[a.case](t)
    except Fail as e:
        print('FAIL %s: %s' % (a.case, e))
        return 1
    print('PASS %s' % a.case)
    return 0


if __name__ == '__main__':
    sys.exit(main())
```

  Append to `plugin/CMakeLists.txt`:

```cmake
# --- lichomat-engine (Task 6) --------------------------------------------------------------
target_sources(plug_common PRIVATE common/plug_spawn.c)
add_library(eng_fw OBJECT "${LXR_ROOT}/emu/render/render_fw.c")	# the renderer's direct calls
target_link_libraries(eng_fw PRIVATE emu_fw_headers)
target_include_directories(eng_fw PRIVATE "${LXR_ROOT}/emu/core" "${LXR_ROOT}/emu/render")
target_compile_options(eng_fw PRIVATE -Wall -Wextra -Werror)
add_executable(lichomat-engine engine/eng_main.c engine/eng_run.c engine/eng_resample.c
    $<TARGET_OBJECTS:eng_fw> $<TARGET_OBJECTS:plug_common_san> ${PLUGIN_DEVICE_OBJECTS})
target_include_directories(lichomat-engine PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/engine"
    "${LXR_ROOT}/emu/core" "${LXR_ROOT}/emu/render" "${LXR_ROOT}/emu/common")
target_compile_options(lichomat-engine PRIVATE ${PLUGIN_FLAGS} ${PLUGIN_SAN_FLAGS} -ffp-contract=off -fno-fast-math)
target_link_options(lichomat-engine PRIVATE ${PLUGIN_SAN_FLAGS} "LINKER:-stack_size,0x800000")
target_link_libraries(lichomat-engine PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
plugin_add_test(test_resample IDS P7_converter_delay P7_converter_open)
target_sources(test_resample PRIVATE engine/eng_resample.c)
target_include_directories(test_resample PRIVATE "${PLUGIN_DIR}/engine")
target_link_libraries(test_resample PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
add_executable(plug_resample_wav tests/plug_resample_wav.c engine/eng_resample.c)
target_include_directories(plug_resample_wav PRIVATE "${PLUGIN_DIR}/engine")
target_compile_options(plug_resample_wav PRIVATE ${PLUGIN_FLAGS})
target_link_libraries(plug_resample_wav PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
add_executable(plug_drive tests/plug_drive.c $<TARGET_OBJECTS:plug_common>)
target_include_directories(plug_drive PRIVATE "${PLUGIN_DIR}/common")
target_compile_options(plug_drive PRIVATE ${PLUGIN_FLAGS})
set(PLUGIN_TEST ${Python3_EXECUTABLE} "${PLUGIN_DIR}/tests/plugin_test.py" --root "${LXR_ROOT}"
    --build "${CMAKE_BINARY_DIR}" --render $<TARGET_FILE:lichomat-render> --engine $<TARGET_FILE:lichomat-engine>
    --drive $<TARGET_FILE:plug_drive> --resample $<TARGET_FILE:plug_resample_wav>
    --work "${CMAKE_BINARY_DIR}/plugin-test")
# plugin_py_tests(<id>...): a case of plugin/tests/plugin_test.py each
function(plugin_py_tests)
    foreach(id IN LISTS ARGN)
        add_test(NAME ${id} COMMAND ${PLUGIN_TEST} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT 600 LABELS plugin)
    endforeach()
endfunction()
plugin_py_tests(P6_engine_version P6_engine_native P6_engine_blocks P6_engine_seed P6_engine_quit
    P7_engine_resampled P7_engine_realtime)
set_tests_properties(P7_engine_realtime PROPERTIES RUN_SERIAL TRUE)	# paced by the wall clock
```

- [ ] **Step 2: run it.** PLUGIN with `-R 'P6|P7'`. Expected: `== plugin build: 1`; the configure stops at `Cannot find source file` for `common/plug_spawn.c` and the engine's files.
- [ ] **Step 3: starting the engine** (P-6). `plugin/common/plug_spawn.h`:

```c
/*
 * plug_spawn.h
 *
 * Starting lichomat-engine (plan E9.2 Tasks 6 and 8), for the plugin and for plug_drive.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_SPAWN_H_
#define PLUG_SPAWN_H_

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start engine --card card with wakeRead as fd 3, lifeWrite as fd 4 and shmFd as fd 5, stdin
 * from /dev/null, stdout and stderr appended to log (NULL: the caller's), no other descriptor,
 * every signal at its default and none blocked: the caller may be a host's thread with its own
 * mask. Returns the pid, or -1 with the problem in err. */
pid_t plug_spawnEngine(const char *engine, const char *card, int wakeRead, int lifeWrite, int shmFd, const char *log,
	char *err, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_SPAWN_H_ */
```

  `plugin/common/plug_spawn.c`:

```c
/*
 * plug_spawn.c
 *
 * Starting lichomat-engine (plan E9.2 Tasks 6 and 8); see plug_spawn.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_spawn.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

pid_t plug_spawnEngine(const char *engine, const char *card, int wakeRead, int lifeWrite, int shmFd, const char *log,
	char *err, size_t n)
{
	/* The file actions run in order, so a source that is already 3, 4 or 5 would be overwritten
	 * before it is moved: each goes through a copy at 64 or above first. */
	const int src[3] = { wakeRead, lifeWrite, shmFd };
	int tmp[3] = { -1, -1, -1 };
	for (int i = 0; i < 3; i++)
		if ((tmp[i] = fcntl(src[i], F_DUPFD_CLOEXEC, 64)) < 0)
		{
			snprintf(err, n, "fcntl(F_DUPFD_CLOEXEC): %s", strerror(errno));
			for (int k = 0; k < i; k++)
				close(tmp[k]);
			return -1;
		}
	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	posix_spawn_file_actions_init(&fa);
	posix_spawnattr_init(&at);
	posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
	if (log)
	{
		posix_spawn_file_actions_addopen(&fa, 1, log, O_WRONLY | O_CREAT | O_APPEND, 0644);
		posix_spawn_file_actions_adddup2(&fa, 1, 2);
	}
	else
	{
		posix_spawn_file_actions_addinherit_np(&fa, 1);
		posix_spawn_file_actions_addinherit_np(&fa, 2);
	}
	for (int i = 0; i < 3; i++)
		posix_spawn_file_actions_adddup2(&fa, tmp[i], 3 + i);
	sigset_t none, all;
	sigemptyset(&none);
	sigfillset(&all);
	posix_spawnattr_setsigmask(&at, &none);
	posix_spawnattr_setsigdefault(&at, &all);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
	char *const argv[] = { (char *)engine, "--card", (char *)card, NULL };
	pid_t pid = -1;
	const int rc = posix_spawn(&pid, engine, &fa, &at, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&at);
	for (int i = 0; i < 3; i++)
		close(tmp[i]);
	if (rc != 0)
	{
		snprintf(err, n, "posix_spawn %s: %s", engine, strerror(rc));
		return -1;
	}
	return pid;
}
```

  and the notices, `plugin/common/plug_notices.h`:

```c
/*
 * plug_notices.h
 *
 * The LICHOMAT plugin's name, version and notices (plan E9.2 Tasks 6 and 8). The legal lines
 * are emu_legal (emu/common/emu_notices.h), allow-listed for M21's name scan; the plugin adds
 * the VST 3 SDK's notice, which names no maker.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_NOTICES_H_
#define PLUG_NOTICES_H_

#define PLUG_NAME "LICHOMAT"
#define PLUG_VENDOR "LICHOMAT"
#define PLUG_VERSION "0.1.0"
#define PLUG_TAGLINE "LICHOMAT plugin prototype: the LICHOMAT firmware in a separate engine process"
#define PLUG_SDK_NOTICE "VST 3 SDK: Copyright (c) 2026, Steinberg Media Technologies GmbH. MIT licence."

#endif /* PLUG_NOTICES_H_ */
```

- [ ] **Step 4: the converter** (P-11). `plugin/engine/eng_resample.h`:

```c
/*
 * eng_resample.h
 *
 * The engine's sample-rate conversion (plan E9.2 Task 6): Apple's AudioConverter from
 * 44,140.625 Hz to the host's rate, float32 stereo interleaved, Normal complexity at maximum
 * quality (the app's choice, app_coreaudio.c:332-341), prime method None, so that every input
 * frame gives its output at once and the delay is a constant 48 input frames.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef ENG_RESAMPLE_H_
#define ENG_RESAMPLE_H_

#include <stddef.h>
#include <stdint.h>

#define ENG_RESAMPLE_IN 64u		/**< engine frames per push: one block */
#define ENG_RESAMPLE_OUT 1024u	/**< output frames per pull, more than 64 frames at 384 kHz */

typedef struct eng_resample
{
	void *conv;		/**< AudioConverterRef */
	float in[2 * ENG_RESAMPLE_IN];
	unsigned have, used;
	float out[2 * ENG_RESAMPLE_OUT];
} eng_resample_t;

/** 0, or -1 with the problem in err */
int eng_resampleOpen(eng_resample_t *r, double rate, char *err, size_t n);

/** Convert one block of 64 interleaved frames; fn receives the output frames (any count, also 0)
 * with ctx. Returns 0, or the converter's error. */
int eng_resamplePush(eng_resample_t *r, const float *lr, void (*fn)(void *ctx, const float *lr, uint32_t frames),
	void *ctx);

void eng_resampleClose(eng_resample_t *r);

#endif /* ENG_RESAMPLE_H_ */
```

  `plugin/engine/eng_resample.c`:

```c
/*
 * eng_resample.c
 *
 * The engine's sample-rate conversion (plan E9.2 Task 6); see eng_resample.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "eng_resample.h"

#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <string.h>

#define ENG_NO_DATA 0x6E6F6474	/* 'nodt': the block is used up; the converter keeps its state */

static OSStatus eng_input(AudioConverterRef c, UInt32 *packets, AudioBufferList *io, AudioStreamPacketDescription **d,
	void *ctx)
{
	(void)c;
	(void)d;
	eng_resample_t *r = ctx;
	const unsigned left = r->have - r->used;
	if (left == 0)
	{
		*packets = 0;
		return ENG_NO_DATA;
	}
	if (*packets > left)
		*packets = left;
	io->mBuffers[0].mData = &r->in[2u * r->used];
	io->mBuffers[0].mDataByteSize = *packets * 2u * (UInt32)sizeof(float);
	io->mBuffers[0].mNumberChannels = 2;
	r->used += *packets;
	return noErr;
}

int eng_resampleOpen(eng_resample_t *r, double rate, char *err, size_t n)
{
	memset(r, 0, sizeof *r);
	AudioStreamBasicDescription in;
	memset(&in, 0, sizeof in);
	in.mSampleRate = 353125.0 / 8.0;
	in.mFormatID = kAudioFormatLinearPCM;
	in.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
	in.mBitsPerChannel = 32;
	in.mChannelsPerFrame = 2;
	in.mFramesPerPacket = 1;
	in.mBytesPerFrame = in.mBytesPerPacket = 2u * (UInt32)sizeof(float);
	AudioStreamBasicDescription out = in;
	out.mSampleRate = rate;
	AudioConverterRef c;
	OSStatus st = AudioConverterNew(&in, &out, &c);
	if (st != noErr)
	{
		snprintf(err, n, "AudioConverterNew to %.3f Hz: %d", rate, (int)st);
		return -1;
	}
	const struct { AudioConverterPropertyID id; UInt32 v; const char *name; } props[] = {
		{ kAudioConverterSampleRateConverterComplexity, kAudioConverterSampleRateConverterComplexity_Normal, "complexity" },
		{ kAudioConverterSampleRateConverterQuality, kAudioConverterQuality_Max, "quality" },
		{ kAudioConverterPrimeMethod, kConverterPrimeMethod_None, "prime method" },
	};
	for (size_t i = 0; i < sizeof props / sizeof props[0]; i++)
	{
		st = AudioConverterSetProperty(c, props[i].id, sizeof props[i].v, &props[i].v);
		if (st != noErr)
		{
			snprintf(err, n, "AudioConverterSetProperty(%s): %d", props[i].name, (int)st);
			AudioConverterDispose(c);
			return -1;
		}
	}
	r->conv = c;
	return 0;
}

int eng_resamplePush(eng_resample_t *r, const float *lr, void (*fn)(void *ctx, const float *lr, uint32_t frames),
	void *ctx)
{
	memcpy(r->in, lr, sizeof r->in);
	r->have = ENG_RESAMPLE_IN;
	r->used = 0;
	for (;;)
	{
		UInt32 packets = ENG_RESAMPLE_OUT;
		AudioBufferList abl;
		abl.mNumberBuffers = 1;
		abl.mBuffers[0].mNumberChannels = 2;
		abl.mBuffers[0].mDataByteSize = (UInt32)sizeof r->out;
		abl.mBuffers[0].mData = r->out;
		const OSStatus st = AudioConverterFillComplexBuffer((AudioConverterRef)r->conv, eng_input, r, &packets, &abl, NULL);
		if (packets)
			fn(ctx, r->out, packets);
		if (st == ENG_NO_DATA)
			return 0;
		if (st != noErr)
			return (int)st;
		if (packets == 0)
			return 0;
	}
}

void eng_resampleClose(eng_resample_t *r)
{
	if (r->conv)
		AudioConverterDispose((AudioConverterRef)r->conv);
	r->conv = NULL;
}
```

- [ ] **Step 5: the engine** (P-8, P-15, P-18, P-24). `plugin/engine/eng_run.h`:

```c
/*
 * eng_run.h
 *
 * The engine of the LICHOMAT plugin (plan E9.2 Task 6): the firmware on the virtual device,
 * fed from the plugin's event ring and writing the plugin's audio ring.
 *
 * - The prelude is lichomat-render's for --no-start --bpm mid --as-booted (render_main.c:
 *   1005-1047): the boot, the settle, the edit PAR_BPM = 1 (MIDI clock sync), the same direct
 *   calls in the same order, then t0 at the first block request 100 ms after it. So at
 *   44,140.625 Hz the engine's output is the renderer's, bit for bit, for the same inputs.
 * - At t0 the engine takes the plugin's hostFrames as its epoch: host frame epoch + m is cycle
 *   t0 + plug_framesToCycles(m).
 * - From then on the tier hook, which the device calls at least every block (1.45 ms) and every
 *   1 ms of a delay (emu_core.h:27-35), waits until the plugin has published the events up to
 *   66 engine frames past now, and hands every published event to the device at its cycle: the
 *   messages of one frame as one USB transfer. No input comes late. The wait is unpaced: the
 *   engine runs as soon as the plugin has published.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef ENG_RUN_H_
#define ENG_RUN_H_

#include "plug_shm.h"

/** Boot the firmware on the card folder and play until the plugin ends it. Exits: 0 when the
 * plugin sets quit, closes the wake pipe (wake, a read end; -1 for none) or goes away; 3 on a
 * firmware fault; 4 when the resampler fails. */
void eng_run(plug_shm_t *s, int wake, const char *card) __attribute__((noreturn));

/** --seed-card (lichomat-render's render_seed(), render_main.c:328-350): an init card from the
 * firmware's own code into dir, missing or empty. Returns 0, or 2 with a message on stderr. */
int eng_seed(const char *dir);

/** 1 if dir is missing or holds nothing but dot entries, 0 if it holds entries, -1 if it is not
 * a folder that can be read */
int eng_cardEmpty(const char *dir);

#endif /* ENG_RUN_H_ */
```

  `plugin/engine/eng_run.c`:

```c
/*
 * eng_run.c
 *
 * The engine of the LICHOMAT plugin (plan E9.2 Task 6); see eng_run.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "eng_run.h"

#include <dirent.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "emu_core.h"
#include "eng_resample.h"
#include "plug_time.h"
#include "render_fw.h"

#define ENG_C100 21600000ull	/* 100 ms, the renderer's settle (render_main.c:38) */
#define ENG_AHEAD 66u			/* engine frames past now whose events the next hook may need */
#define ENG_PACKETS 16u			/* USB-MIDI packets in one transfer (emu_core.h:143) */

static struct
{
	plug_shm_t *s;
	int wake;
	double rate;
	int native;
	eng_resample_t rs;
	uint64_t preludeEnd, busyLast, t0, epoch;
	int phase;				/* 0 the prelude, 1 waiting for t0, 2 playing */
	uint8_t pk[4u * ENG_PACKETS];
	unsigned npk;
	uint64_t pkFrame;
	uint64_t dropped;
} E;

static void eng_exit(int code) __attribute__((noreturn));
static void eng_exit(int code)
{
	fflush(NULL);
	_exit(code);
}

static void eng_fail(int code, const char *what) __attribute__((noreturn));
static void eng_fail(int code, const char *what)
{
	fprintf(stderr, "lichomat-engine: %s at cycle %llu\n", what, (unsigned long long)emu_now());
	fflush(stderr);
	emu_trailWrite(2, 32u);
	if (E.s)
		plug_store(&E.s->state, PLUG_ENGINE_FAILED);
	eng_exit(code);
}

static void eng_onFault(void *ctx, const char *what)
{
	(void)ctx;
	char line[512];
	snprintf(line, sizeof line, "firmware fault: %s", what);
	eng_fail(3, line);
}

static void eng_onWarn(void *ctx, const char *what)
{
	(void)ctx;
	fprintf(stderr, "lichomat-engine: warning: %s\n", what);
}

/* the time-constraint policy of the app's firmware thread (app_mach.c:106-117): a period of the host's
 * largest block, a quarter of it to compute; a refusal is not an error */
static int eng_threadRealtime(double periodSec)
{
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	thread_time_constraint_policy_data_t p;
	p.period = (uint32_t)(periodSec * 1e9 * (double)tb.denom / (double)tb.numer);
	p.computation = p.period / 4u;
	p.constraint = p.period;
	p.preemptible = 1;
	return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
		(thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS ? 0 : -1;
}

/* --- the output ------------------------------------------------------------------- */

static void eng_ring(void *ctx, const float *lr, uint32_t frames)
{
	(void)ctx;
	plug_audioWrite(E.s, lr, frames);
}

static int eng_t0Due(uint64_t t)
{
	return t - E.preludeEnd >= ENG_C100 && (E.busyLast == 0 || t - E.busyLast > ENG_C100);
}

/* the audio sink: t0 as the renderer finds it (render_main.c:355-380), then each block as the
 * stereo sum, (L1 + L2) / 65536 and (R1 + R2) / 65536: the renderer's --stereo sum before its
 * shift, exactly, and -6 dB of the device's two outputs added */
static void eng_sink(void *ctx, uint64_t t, const int16_t *o1, const int16_t *o2)
{
	(void)ctx;
	if (E.phase == 1 && eng_t0Due(t))
	{
		E.t0 = t;
		E.epoch = plug_load(&E.s->hostFrames);
		plug_evSkipAll(E.s);	/* a dead engine's events, and any written before this one ran */
		plug_audioBegin(E.s, E.epoch);
		plug_store(&E.s->epoch, E.epoch);
		plug_store(&E.s->frontier, E.epoch);
		plug_store(&E.s->state, PLUG_ENGINE_RUNNING);
		E.phase = 2;
		/* in real time only: a time-constraint thread that computes for longer than its share, as an
		 * engine fed offline does, is throttled */
		if (E.s->realtime && eng_threadRealtime((double)E.s->maxBlock / E.rate) != 0)
			fprintf(stderr, "lichomat-engine: the time-constraint policy was refused; the engine runs as a normal thread\n");
	}
	if (E.phase != 2)
		return;
	float lr[2u * EMU_BLOCK_FRAMES];
	for (unsigned i = 0; i < 2u * EMU_BLOCK_FRAMES; i++)
		lr[i] = (float)((int32_t)o1[i] + o2[i]) * (1.0f / 65536.0f);
	if (E.native)
		plug_audioWrite(E.s, lr, EMU_BLOCK_FRAMES);
	else if (eng_resamplePush(&E.rs, lr, eng_ring, NULL) != 0)
		eng_fail(4, "the sample-rate converter failed");
}

/* --- the input ----------------------------------------------------------------------- */

/* the USB-MIDI 1.0 code index number of a status byte, 0 for one the plugin never sends */
static uint8_t eng_cin(uint8_t st)
{
	if (st >= 0x80u && st < 0xF0u)
		return (uint8_t)(st >> 4);
	switch (st)
	{
	case 0xF1:
	case 0xF3:
		return 0x2;
	case 0xF2:
		return 0x3;
	case 0xF6:
		return 0x5;
	default:
		return st >= 0xF8u ? 0xFu : 0u;
	}
}

static void eng_flush(void)
{
	if (!E.npk)
		return;
	emu_inUsb(E.t0 + plug_framesToCycles(E.pkFrame - E.epoch, E.rate), E.pk, 4u * E.npk);
	E.npk = 0;
}

/* every published event before frame hf: those of one frame as one transfer, in ring order */
static void eng_inject(uint64_t hf)
{
	for (;;)
	{
		if (plug_evNextFrame(E.s) >= hf)
			break;
		plug_event_t e;
		(void)plug_evPop(E.s, &e);
		const uint8_t cin = eng_cin(e.b[0]);
		if (e.frame < E.epoch || cin == 0u)
		{
			E.dropped++;
			continue;
		}
		if (E.npk && (e.frame != E.pkFrame || E.npk == ENG_PACKETS))
			eng_flush();
		E.pkFrame = e.frame;
		uint8_t *p = &E.pk[4u * E.npk++];
		p[0] = cin;
		p[1] = e.b[0];
		p[2] = e.b[1];
		p[3] = e.b[2];
	}
	eng_flush();
	emu_counters_t c;
	emu_getCounters(&c);
	plug_store(&E.s->late, c.late);
	plug_store(&E.s->dropped, E.dropped);
	plug_store(&E.s->frontier, hf);
}

/* until the plugin has published host frame need: the wake pipe, the quit flag, the parent */
static void eng_waitFor(uint64_t need)
{
	if (plug_load(&E.s->hostFrames) >= need)
		return;
	emu_waitBegin();
	while (plug_load(&E.s->hostFrames) < need)
	{
		if (plug_load(&E.s->quit) || getppid() == 1)
			eng_exit(0);
		if (E.wake < 0)
		{
			nanosleep(&(const struct timespec){ 0, 200000L }, NULL);
			continue;
		}
		struct pollfd p = { E.wake, POLLIN, 0 };
		if (poll(&p, 1, 100) > 0)
		{
			char b[4096];
			if (read(E.wake, b, sizeof b) == 0)
				eng_exit(0);	/* the plugin closed its end */
		}
	}
	emu_waitEnd();
}

static void eng_hook(void *ctx, uint64_t now, emu_hookReason_t why)
{
	(void)ctx;
	(void)why;
	if (E.phase != 2)
		return;
	const uint64_t need = plug_framesToCycles(ENG_AHEAD, PLUG_ENGINE_RATE) + (now - E.t0);
	eng_waitFor(E.epoch + plug_framesCovering(need, E.rate));
	eng_inject(plug_load(&E.s->hostFrames));
}

/* --- the prelude ---------------------------------------------------------------------- */

/* a pass of the renderer's prelude (render_main.c:787-795) */
static void eng_pass(void)
{
	uint8_t busy = 0;
	emu_runUntil(emu_now() + 1u);
	emu_directCall(rfw_morphBusy, &busy);
	if (busy)
		E.busyLast = emu_now();
}

void eng_run(plug_shm_t *s, int wake, const char *card)
{
	memset(&E, 0, sizeof E);
	E.s = s;
	E.wake = wake;
	E.rate = s->rate;
	E.native = plug_isNative(s->rate);
	plug_store(&s->pid, (uint64_t)getpid());
	plug_store(&s->state, PLUG_ENGINE_BOOTING);
	char err[256];
	if (!E.native && eng_resampleOpen(&E.rs, s->rate, err, sizeof err) != 0)
		eng_fail(4, err);
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = card;
	cfg.cardIn = 1;
	cfg.sinks.audio = eng_sink;
	cfg.hook = eng_hook;
	cfg.fault = eng_onFault;
	cfg.warn = eng_onWarn;
	emu_powerOn(&cfg);
	/* the renderer's prelude for --no-start --bpm mid (render_main.c:1005-1047), call for call:
	 * a direct call starts a HAL_GetTick() segment (emu_sched.c:522-533), so the same calls give
	 * the same firmware timing */
	emu_runUntil(1u);
	E.preludeEnd = emu_now();
	while (!(emu_now() - E.preludeEnd >= ENG_C100 && (E.busyLast == 0 || emu_now() - E.busyLast > ENG_C100)))
		eng_pass();
	uint8_t mid = 1;
	emu_directCall(rfw_bpm, &mid);
	E.preludeEnd = emu_now();
	render_selected_t sel;
	emu_directCall(rfw_selected, &sel);
	E.phase = 1;
	while (E.phase == 1)
		eng_pass();
	for (;;)
		emu_runUntil(emu_now() + EMU_CPU_HZ);
}

/* --- the card --------------------------------------------------------------------------- */

int eng_cardEmpty(const char *dir)
{
	struct stat st;
	if (stat(dir, &st) != 0)
		return errno == ENOENT ? 1 : -1;
	if (!S_ISDIR(st.st_mode))
		return -1;
	DIR *d = opendir(dir);
	if (!d)
		return -1;
	int empty = 1;
	for (const struct dirent *e; (e = readdir(d)) != NULL;)
		if (e->d_name[0] != '.')
			empty = 0;
	closedir(d);
	return empty;
}

int eng_seed(const char *dir)
{
	const int empty = eng_cardEmpty(dir);
	if (empty != 1)
	{
		fprintf(stderr, "lichomat-engine: --seed-card: %s %s\n", dir,
			empty == 0 ? "holds entries" : "is not a folder that can be read");
		return 2;
	}
	if (mkdir(dir, 0755) != 0 && errno != EEXIST)
	{
		fprintf(stderr, "lichomat-engine: --seed-card: cannot make %s: %s\n", dir, strerror(errno));
		return 2;
	}
	emu_config_t cfg;
	emu_configDefaults(&cfg);
	cfg.cardDir = dir;
	cfg.cardIn = 1;
	cfg.fault = eng_onFault;
	cfg.warn = eng_onWarn;
	emu_powerOn(&cfg);
	emu_runUntil(1u);
	emu_runUntil(emu_now() + 100u * EMU_CYCLES_PER_MS);
	int res = -1;
	emu_directCall(rfw_seedCard, &res);
	if (res != 0)
	{
		fprintf(stderr, "lichomat-engine: --seed-card: the project save failed with FatFs result %d\n", res);
		return 2;
	}
	return 0;
}
```

  `plugin/engine/eng_main.c`:

```c
/*
 * eng_main.c
 *
 * lichomat-engine, the LICHOMAT plugin's engine process (plan E9.2 Task 6):
 *
 *   lichomat-engine --card DIR              play for the plugin that started it: fd 3 is the read
 *                                           end of its wake pipe, fd 4 the write end of its life
 *                                           pipe, held open and never written, fd 5 the shared
 *                                           memory (plug_shm.h)
 *   lichomat-engine --seed-card DIR         an init card into DIR, missing or empty
 *   lichomat-engine --version
 *
 * With --card, a missing or empty card folder is seeded first, by a process of its own: the
 * firmware powers on once per process (emu_firmware.c:66-74). Exit codes: 0 the plugin ended it,
 * 1 usage, 2 the card or the shared memory, 3 a firmware fault or a fault signal, 4 the converter.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <errno.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "emu_notices.h"
#include "eng_run.h"
#include "plug_notices.h"

extern char **environ;

#define ENG_WAKE_FD 3
#define ENG_LIFE_FD 4
#define ENG_SHM_FD 5

static plug_shm_t *eng_shm;

/* a fault signal: a line, the state, the exit; write(2) only, on the alternate stack */
static void eng_onSignal(int sig)
{
	static const char msg[] = "lichomat-engine: fault signal\n";
	(void)!write(2, msg, sizeof msg - 1u);
	if (eng_shm)
		__atomic_store_n(&eng_shm->state, (uint64_t)PLUG_ENGINE_FAILED, __ATOMIC_RELEASE);
	(void)sig;
	_exit(3);
}

static void eng_signals(void)
{
	static char altStack[1u << 17];
	stack_t ss;
	memset(&ss, 0, sizeof ss);
	ss.ss_sp = altStack;
	ss.ss_size = sizeof altStack;
	(void)sigaltstack(&ss, NULL);
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = eng_onSignal;
	sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	static const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
	for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
		(void)sigaction(sigs[i], &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
}

/* a sanitizer report is a fault, as in the renderer (render_main.c:209-221) */
const char *__asan_default_options(void) __attribute__((used, visibility("default")));
const char *__asan_default_options(void)
{
	return "abort_on_error=1";
}

const char *__ubsan_default_options(void) __attribute__((used, visibility("default")));
const char *__ubsan_default_options(void)
{
	return "abort_on_error=1";
}

static void eng_version(void)
{
	printf("lichomat-engine %s, the engine of the LICHOMAT plugin\n", PLUG_VERSION);
	printf("%s\n", PLUG_TAGLINE);
	for (size_t i = 0; i < EMU_LEGAL_COUNT; i++)
		printf("%s\n", emu_legal[i]);
}

/* seed the card in a process of our own: --seed-card, with only stdout and stderr */
static int eng_seedChild(const char *card)
{
	char self[4096];
	uint32_t size = sizeof self;
	if (_NSGetExecutablePath(self, &size) != 0)
	{
		fprintf(stderr, "lichomat-engine: the executable's path is too long\n");
		return 2;
	}
	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addinherit_np(&fa, 1);
	posix_spawn_file_actions_addinherit_np(&fa, 2);
	posix_spawnattr_init(&at);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT);
	char *const argv[] = { self, "--seed-card", (char *)card, NULL };
	pid_t pid;
	const int rc = posix_spawn(&pid, self, &fa, &at, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&at);
	if (rc != 0)
	{
		fprintf(stderr, "lichomat-engine: cannot start the seeding: %s\n", strerror(rc));
		return 2;
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			return 2;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 2;
}

static int eng_usage(void)
{
	fprintf(stderr, "usage: lichomat-engine --card DIR | --seed-card DIR | --version\n");
	return 1;
}

int main(int argc, char **argv)
{
	eng_signals();
	if (argc == 2 && !strcmp(argv[1], "--version"))
	{
		eng_version();
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "--seed-card"))
		return eng_seed(argv[2]);
	if (argc != 3 || strcmp(argv[1], "--card"))
		return eng_usage();
	const char *card = argv[2];
	char err[256];
	eng_shm = plug_shmMap(ENG_SHM_FD, err, sizeof err);
	if (!eng_shm || plug_shmCheck(eng_shm, err, sizeof err) != 0)
	{
		fprintf(stderr, "lichomat-engine: %s\n", err);
		return 2;
	}
	const int empty = eng_cardEmpty(card);
	if (empty < 0 || (empty == 1 && eng_seedChild(card) != 0))
	{
		fprintf(stderr, "lichomat-engine: card %s: %s\n", card, empty < 0 ? "not a folder that can be read" : "seeding failed");
		plug_store(&eng_shm->state, PLUG_ENGINE_FAILED);
		return 2;
	}
	(void)ENG_LIFE_FD;	/* held open by the process, never used */
	eng_run(eng_shm, ENG_WAKE_FD, card);
}
```

- [ ] **Step 6: pass.** PLUGIN with `-R 'P6|P7'` and `ctest --test-dir build/plugin -R 'P6_engine_native|P7_converter_delay|P7_engine_resampled' -V | grep -E 'plug_drive|render ==|P7_converter|Hz:'`.
  Expected: `100% tests passed, 0 tests failed out of 9`; `plug_drive: epoch 0, 617984 frames, late 0, dropped 0, ring full 0, underruns 0 …` and `render == drive: 617984 frames, 55 onsets in L`; the converter's delay `47.94`, `52.27`, `95.90`, `104.38` output frames (`47.99`–`48.07` input frames); at 44,100, 48,000 and 96,000 Hz `the largest difference 1 LSB`. In Release (`build/plugin-release`, from PLUGIN-CHECK) `P6_engine_native` prints about `127x real time` (Debug about 15x).
- [ ] **Step 7: the gate.** PLUGIN-CHECK (T 35). **Step 8: commit** `plugin Task 6 (E9.16): lichomat-engine, the renderer's prelude and the tier hook's input, native and resampled; P6, P7` with the twelve files; push. **Review: full** (spec, then quality), the mutants of the Rules among them: the hook's 66 frames at 64, the epoch's load moved after `plug_audioBegin()`, the `/ 65536` at `/ 32768`, the converter's prime method at Normal.

---

### Task 7: The card as one archive — E9.17

**Files:** Create `plugin/common/plug_archive.h`, `plugin/common/plug_archive.c`, `plugin/tests/test_archive.c`; modify `plugin/CMakeLists.txt` (append).

- [ ] **Step 1: the failing test.** `plugin/tests/test_archive.c`:

```c
/*
 * test_archive.c
 *
 * P8: a card folder as the plugin's state (plan E9.2 Task 7): a round trip keeps the files, the
 * folders and the card's order (birth times, not names); names the device cannot see are left
 * out; a damaged archive is refused.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "emu_hostfs.h"
#include "plug_archive.h"
#include "seqc_test.h"

typedef struct
{
	uint8_t *p;
	size_t n, cap, at;
} mem_t;

static int memPut(void *ctx, const void *p, size_t n)
{
	mem_t *m = ctx;
	if (m->n + n > m->cap)
	{
		m->cap = (m->n + n) * 2u;
		m->p = realloc(m->p, m->cap);
	}
	memcpy(m->p + m->n, p, n);
	m->n += n;
	return 0;
}

static int memGet(void *ctx, void *p, size_t n)
{
	mem_t *m = ctx;
	if (m->at + n > m->n)
		return -1;
	memcpy(p, m->p + m->at, n);
	m->at += n;
	return 0;
}

static char root[512];

/* a fresh folder's path under the test's own temporary folder, in a buffer the next call reuses */
static const char *fresh(const char *name)
{
	static char p[1024];
	if (!root[0])
	{
		snprintf(root, sizeof root, "%s/plug-archive-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
		if (!mkdtemp(root))
			abort();
	}
	snprintf(p, sizeof p, "%s/%s", root, name);
	return p;
}

static void file(const char *dir, const char *name, const char *text)
{
	char p[1024];
	snprintf(p, sizeof p, "%s/%s", dir, name);
	const int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0 || write(fd, text, strlen(text)) != (ssize_t)strlen(text))
		abort();
	close(fd);
	nanosleep(&(const struct timespec){ 0, 2000000L }, NULL);	/* distinct birth times */
}

/* "NAME:size NAME/ ..." of a folder in the card's order, recursively */
static void listing(const char *dir, char *out, size_t n)
{
	emu_hostEntry_t *list;
	const int count = emu_hostList(dir, &list, NULL);
	for (int i = 0; i < count; i++)
	{
		char p[1024];
		snprintf(p, sizeof p, "%s/%s", dir, list[i].host);
		const size_t len = strlen(out);
		snprintf(out + len, n - len, "%s%s ", list[i].name, list[i].isDir ? "/" : "");
		if (list[i].isDir)
		{
			listing(p, out, n);
			strncat(out, "| ", n - strlen(out) - 1u);
		}
		else
		{
			char text[64] = { 0 };
			const int fd = open(p, O_RDONLY);
			(void)!read(fd, text, sizeof text - 1u);
			close(fd);
			const size_t l2 = strlen(out);
			snprintf(out + l2, n - l2, "=%s ", text);
		}
	}
	free(list);
}

TEST(P8_archive_roundtrip)
{
	char src[1024];
	snprintf(src, sizeof src, "%s", fresh("src"));
	char dirs[1024];
	ASSERT_EQ(mkdir(src, 0755), 0);
	/* the card's order: ZZZ.TXT, PROJ00, AAA.TXT; in PROJ00: 01-B.PAT, 00-A.PAT */
	file(src, "ZZZ.TXT", "zzz");
	snprintf(dirs, sizeof dirs, "%s/PROJ00", src);
	ASSERT_EQ(mkdir(dirs, 0755), 0);
	nanosleep(&(const struct timespec){ 0, 2000000L }, NULL);
	file(src, "AAA.TXT", "aaa");
	file(dirs, "01-B.PAT", "pattern one");
	file(dirs, "00-A.PAT", "pattern zero");
	char want[2048] = "";
	listing(src, want, sizeof want);
	ASSERT_EQ(strcmp(want, "ZZZ.TXT =zzz PROJ00/ 01-B.PAT =pattern one 00-A.PAT =pattern zero | AAA.TXT =aaa "), 0);
	mem_t m = { 0 };
	char err[256];
	ASSERT_EQ(plug_archiveWrite(src, memPut, &m, err, sizeof err), 0);
	ASSERT_EQ(memcmp(m.p, "LCHS\x01\x00\x00\x00", 8), 0);
	const char *dst = fresh("dst");
	ASSERT_EQ(plug_archiveRead(dst, memGet, &m, err, sizeof err), 0);
	ASSERT_EQ(m.at, m.n);
	char got[2048] = "";
	listing(dst, got, sizeof got);
	printf("P8_archive_roundtrip: %zu bytes; %s\n", m.n, got);
	ASSERT_EQ(strcmp(got, want), 0);
	free(m.p);
}

TEST(P8_archive_skips)
{
	char src[1024];
	snprintf(src, sizeof src, "%s", fresh("skips"));
	ASSERT_EQ(mkdir(src, 0755), 0);
	file(src, "GOOD.TXT", "g");
	file(src, ".hidden", "h");
	file(src, "a name too long.txt", "x");
	mem_t m = { 0 };
	char err[256];
	ASSERT_EQ(plug_archiveWrite(src, memPut, &m, err, sizeof err), 0);
	const char *dst = fresh("skips-out");
	ASSERT_EQ(plug_archiveRead(dst, memGet, &m, err, sizeof err), 0);
	char got[512] = "";
	listing(dst, got, sizeof got);
	ASSERT_EQ(strcmp(got, "GOOD.TXT =g "), 0);
	free(m.p);
}

TEST(P8_archive_refused)
{
	char src[1024];
	snprintf(src, sizeof src, "%s", fresh("ref"));
	ASSERT_EQ(mkdir(src, 0755), 0);
	file(src, "ONE.TXT", "one");
	mem_t m = { 0 };
	char err[256];
	ASSERT_EQ(plug_archiveWrite(src, memPut, &m, err, sizeof err), 0);
	/* cut short */
	mem_t cut = m;
	cut.n -= 3;
	cut.at = 0;
	ASSERT_EQ(plug_archiveRead(fresh("ref-cut"), memGet, &cut, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "early") != NULL);
	/* another magic, another version */
	m.p[0] = 'X';
	m.at = 0;
	ASSERT_EQ(plug_archiveRead(fresh("ref-magic"), memGet, &m, err, sizeof err), -1);
	m.p[0] = 'L';
	m.p[4] = 2;
	m.at = 0;
	ASSERT_EQ(plug_archiveRead(fresh("ref-version"), memGet, &m, err, sizeof err), -1);
	m.p[4] = 1;
	/* a path that leaves the card: "../X.TXT" in place of "ONE.TXT" (same length) */
	memcpy(m.p + 11, "../X.TX", 7);
	m.at = 0;
	ASSERT_EQ(plug_archiveRead(fresh("ref-path"), memGet, &m, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "not a card's") != NULL);
	/* into a folder that holds something */
	memcpy(m.p + 11, "ONE.TXT", 7);
	m.at = 0;
	ASSERT_EQ(plug_archiveRead(src, memGet, &m, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "not empty") != NULL);
	free(m.p);
}

TEST_MAIN()
```

  Append to `plugin/CMakeLists.txt`:

```cmake
# --- the card archive (the plugin's state, Task 7) -------------------------------------------
# with its own copy of the card's POSIX half, built without the emulator's sanitizers: the
# plugin runs in the host's process
add_library(plug_card OBJECT common/plug_archive.c "${LXR_ROOT}/emu/core/emu_hostfs.c")
target_include_directories(plug_card PUBLIC "${PLUGIN_DIR}/common" "${LXR_ROOT}/emu/core")
target_compile_options(plug_card PRIVATE ${PLUGIN_FLAGS})
plugin_add_test(test_archive IDS P8_archive_roundtrip P8_archive_skips P8_archive_refused)
target_sources(test_archive PRIVATE $<TARGET_OBJECTS:plug_card>)
target_include_directories(test_archive PRIVATE "${LXR_ROOT}/emu/core")
```

- [ ] **Step 2: run it.** PLUGIN with `-R P8`. Expected: `== plugin build: 1`; the configure stops at `Cannot find source file: common/plug_archive.c`.
- [ ] **Step 3: the archive** (P-17). `plugin/common/plug_archive.h`:

```c
/*
 * plug_archive.h
 *
 * A card folder as one block of bytes, the plugin's state (plan E9.2 Task 7). The firmware reads
 * a directory in the card's order, the files' birth times (emu_hostfs.h:38-42), so the archive
 * keeps that order and a restore recreates it.
 *
 * Format, little-endian: "LCHS", version 1 (u32), then the entries of each directory in the
 * card's order, a directory's own entries right after it:
 *   kind (u8: 1 directory, 2 file), path length (u16), the path relative to the card ('/'
 *   between names), and for a file its size (u64) and its bytes;
 * then kind 0. Names that are not 8.3 and dot entries are left out: the device does not see
 * them (emu_hostList()).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_ARCHIVE_H_
#define PLUG_ARCHIVE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_ARCHIVE_MAGIC "LCHS"
#define PLUG_ARCHIVE_VERSION 1u
#define PLUG_ARCHIVE_MAX ((uint64_t)64 << 20)	/**< the most bytes of files an archive holds */

/** writes n bytes; returns 0, or -1 */
typedef int (*plug_put_t)(void *ctx, const void *p, size_t n);
/** reads exactly n bytes; returns 0, or -1 */
typedef int (*plug_get_t)(void *ctx, void *p, size_t n);

/** Archive the folder dir. A file that changes while it is read (its size or modification time)
 * makes the whole archive start again, up to 3 times. Returns 0, or -1 with the problem in err. */
int plug_archiveWrite(const char *dir, plug_put_t put, void *ctx, char *err, size_t n);

/** Restore an archive into dir, which must be missing or empty; each entry is made in the
 * archive's order and ordered last in its directory (emu_hostOrderLast()). Returns 0, or -1 with
 * the problem in err (dir may then hold part of the archive). */
int plug_archiveRead(const char *dir, plug_get_t get, void *ctx, char *err, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_ARCHIVE_H_ */
```

  `plugin/common/plug_archive.c`:

```c
/*
 * plug_archive.c
 *
 * A card folder as one block of bytes (plan E9.2 Task 7); see plug_archive.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_archive.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "emu_hostfs.h"

enum { KIND_END = 0, KIND_DIR = 1, KIND_FILE = 2 };

typedef struct
{
	uint8_t *p;
	size_t n, cap;
	int failed;		/* out of memory or past PLUG_ARCHIVE_MAX */
	int changed;	/* a file changed while it was read */
} buf_t;

static void bput(buf_t *b, const void *p, size_t n)
{
	if (b->failed)
		return;
	if (b->n + n > PLUG_ARCHIVE_MAX + 65536u)
	{
		b->failed = 1;
		return;
	}
	if (b->n + n > b->cap)
	{
		size_t cap = b->cap ? b->cap : 65536u;
		while (cap < b->n + n)
			cap *= 2u;
		uint8_t *q = realloc(b->p, cap);
		if (!q)
		{
			b->failed = 1;
			return;
		}
		b->p = q;
		b->cap = cap;
	}
	memcpy(b->p + b->n, p, n);
	b->n += n;
}

static void bputLe(buf_t *b, uint64_t v, unsigned bytes)
{
	uint8_t x[8];
	for (unsigned i = 0; i < bytes; i++)
		x[i] = (uint8_t)(v >> (8u * i));
	bput(b, x, bytes);
}

static int fail(char *err, size_t n, const char *fmt, const char *a, const char *b)
{
	snprintf(err, n, fmt, a, b);
	return -1;
}

/* the entries of root/rel, then each directory's own, depth first */
static int addDir(buf_t *b, const char *root, const char *rel, char *err, size_t n)
{
	char path[EMU_HOST_PATH];
	snprintf(path, sizeof path, "%s%s%s", root, *rel ? "/" : "", rel);
	emu_hostEntry_t *list;
	const int count = emu_hostList(path, &list, NULL);
	if (count < 0)
		return fail(err, n, "card archive: cannot list %s%s", path, "");
	int rc = 0;
	for (int i = 0; i < count && rc == 0; i++)
	{
		char child[EMU_HOST_PATH], full[EMU_HOST_PATH];
		snprintf(child, sizeof child, "%s%s%s", rel, *rel ? "/" : "", list[i].host);
		snprintf(full, sizeof full, "%s/%s", root, child);
		const size_t len = strlen(child);
		bput(b, (const uint8_t[]){ (uint8_t)(list[i].isDir ? KIND_DIR : KIND_FILE) }, 1);
		bputLe(b, len, 2);
		bput(b, child, len);
		if (list[i].isDir)
		{
			rc = addDir(b, root, child, err, n);
			continue;
		}
		struct stat before, after;
		const int fd = open(full, O_RDONLY);
		if (fd < 0 || fstat(fd, &before) != 0)
		{
			if (fd >= 0)
				close(fd);
			rc = fail(err, n, "card archive: cannot read %s: %s", full, strerror(errno));
			break;
		}
		bputLe(b, (uint64_t)before.st_size, 8);
		uint8_t chunk[65536];
		uint64_t left = (uint64_t)before.st_size;
		while (left && !b->failed)
		{
			const ssize_t got = read(fd, chunk, left < sizeof chunk ? (size_t)left : sizeof chunk);
			if (got <= 0)
			{
				b->changed = 1;	/* shorter than its size: it is being written */
				break;
			}
			bput(b, chunk, (size_t)got);
			left -= (uint64_t)got;
		}
		if (stat(full, &after) != 0 || after.st_size != before.st_size
			|| after.st_mtimespec.tv_sec != before.st_mtimespec.tv_sec
			|| after.st_mtimespec.tv_nsec != before.st_mtimespec.tv_nsec || left)
			b->changed = 1;
		close(fd);
	}
	free(list);
	return rc;
}

int plug_archiveWrite(const char *dir, plug_put_t put, void *ctx, char *err, size_t n)
{
	for (int attempt = 0; attempt < 3; attempt++)
	{
		buf_t b = { 0 };
		bput(&b, PLUG_ARCHIVE_MAGIC, 4);
		bputLe(&b, PLUG_ARCHIVE_VERSION, 4);
		int rc = addDir(&b, dir, "", err, n);
		bput(&b, (const uint8_t[]){ KIND_END }, 1);
		if (rc == 0 && b.failed)
			rc = fail(err, n, "card archive: %s is larger than 64 MB, or out of memory%s", dir, "");
		if (rc == 0 && !b.changed)
			rc = put(ctx, b.p, b.n) == 0 ? 0 : fail(err, n, "card archive: the stream refused %s%s", "the bytes", "");
		free(b.p);
		if (rc != 0 || !b.changed)
			return rc;
	}
	return fail(err, n, "card archive: the files of %s kept changing while they were read%s", dir, "");
}

static int getLe(plug_get_t get, void *ctx, uint64_t *v, unsigned bytes)
{
	uint8_t x[8];
	if (get(ctx, x, bytes) != 0)
		return -1;
	*v = 0;
	for (unsigned i = 0; i < bytes; i++)
		*v |= (uint64_t)x[i] << (8u * i);
	return 0;
}

/* a relative path of 8.3 names: no empty, dot or other component */
static int goodPath(const char *p)
{
	char part[256];
	for (;;)
	{
		const char *slash = strchr(p, '/');
		const size_t len = slash ? (size_t)(slash - p) : strlen(p);
		if (len == 0 || len >= sizeof part)
			return 0;
		memcpy(part, p, len);
		part[len] = '\0';
		char name[13];
		if (part[0] == '.' || !emu_host83(part, name))
			return 0;
		if (!slash)
			return 1;
		p = slash + 1;
	}
}

int plug_archiveRead(const char *dir, plug_get_t get, void *ctx, char *err, size_t n)
{
	char magic[4];
	uint64_t version;
	if (get(ctx, magic, 4) != 0 || memcmp(magic, PLUG_ARCHIVE_MAGIC, 4) != 0 || getLe(get, ctx, &version, 4) != 0
		|| version != PLUG_ARCHIVE_VERSION)
		return fail(err, n, "card archive: not an LCHS archive of version 1%s%s", "", "");
	if (mkdir(dir, 0755) != 0 && errno != EEXIST)
		return fail(err, n, "card archive: cannot make %s: %s", dir, strerror(errno));
	DIR *d = opendir(dir);
	if (!d)
		return fail(err, n, "card archive: cannot read %s: %s", dir, strerror(errno));
	for (const struct dirent *e; (e = readdir(d)) != NULL;)
		if (e->d_name[0] != '.')
		{
			closedir(d);
			return fail(err, n, "card archive: %s is not empty%s", dir, "");
		}
	closedir(d);
	uint64_t total = 0;
	for (;;)
	{
		uint8_t kind;
		uint64_t len;
		if (get(ctx, &kind, 1) != 0)
			return fail(err, n, "card archive: it ends early%s%s", "", "");
		if (kind == KIND_END)
			return 0;
		char rel[EMU_HOST_PATH], full[EMU_HOST_PATH], parent[EMU_HOST_PATH];
		if ((kind != KIND_DIR && kind != KIND_FILE) || getLe(get, ctx, &len, 2) != 0 || len == 0 || len >= 512u
			|| get(ctx, rel, (size_t)len) != 0)
			return fail(err, n, "card archive: a damaged entry%s%s", "", "");
		rel[len] = '\0';
		if (!goodPath(rel))
			return fail(err, n, "card archive: the path %s is not a card's%s", rel, "");
		snprintf(full, sizeof full, "%s/%s", dir, rel);
		snprintf(parent, sizeof parent, "%s", full);
		*strrchr(parent, '/') = '\0';
		if (kind == KIND_DIR)
		{
			if (mkdir(full, 0755) != 0)
				return fail(err, n, "card archive: cannot make %s: %s", full, strerror(errno));
		}
		else
		{
			uint64_t size;
			if (getLe(get, ctx, &size, 8) != 0 || size > PLUG_ARCHIVE_MAX - total)
				return fail(err, n, "card archive: %s is too large%s", rel, "");
			total += size;
			const int fd = open(full, O_WRONLY | O_CREAT | O_EXCL, 0644);
			if (fd < 0)
				return fail(err, n, "card archive: cannot make %s: %s", full, strerror(errno));
			uint8_t chunk[65536];
			while (size)
			{
				const size_t k = size < sizeof chunk ? (size_t)size : sizeof chunk;
				if (get(ctx, chunk, k) != 0 || write(fd, chunk, k) != (ssize_t)k)
				{
					close(fd);
					return fail(err, n, "card archive: %s ends early, or cannot be written%s", rel, "");
				}
				size -= k;
			}
			if (close(fd) != 0)
				return fail(err, n, "card archive: cannot write %s: %s", full, strerror(errno));
		}
		emu_hostOrderLast(parent, full);
	}
}
```

- [ ] **Step 4: pass.** PLUGIN with `-R P8`. Expected: `100% tests passed, 0 tests failed out of 3`; `P8_archive_roundtrip: 135 bytes; ZZZ.TXT =zzz PROJ00/ 01-B.PAT =pattern one 00-A.PAT =pattern zero | AAA.TXT =aaa`.
- [ ] **Step 5: the gate.** PLUGIN-CHECK (T 38). **Step 6: commit** `plugin Task 7 (E9.17): the card folder as one archive in the card's order; P8` with the four files; push.

---

### Task 8: `LICHOMAT.vst3` — E9.18

**Files:** Create `plugin/vst3/plugin_ids.h`, `plugin/vst3/plugin_card.h`, `plugin/vst3/plugin_card.cpp`, `plugin/vst3/plugin_link.h`, `plugin/vst3/plugin_link.cpp`, `plugin/vst3/plugin_processor.h`, `plugin/vst3/plugin_processor.cpp`, `plugin/vst3/plugin_controller.h`, `plugin/vst3/plugin_controller.cpp`, `plugin/vst3/plugin_factory.cpp`, `plugin/vst3/Info.plist.in`; modify `plugin/tests/plugin_test.py`, `plugin/CMakeLists.txt` (append).

- [ ] **Step 1: the failing test.** In `plugin/tests/plugin_test.py`, before `def main():`:

```python
# --- the bundle (Task 8) ----------------------------------------------------------------------

@case
def P10_plugin_validator(t):
    e = dict(os.environ)
    e['LICHOMAT_PLUGIN_HOME'] = t.path('home')
    e['LICHOMAT_EMU_HOME'] = t.path('no-emu')
    p = subprocess.run([t.a.validator, t.a.bundle], capture_output=True, text=True, timeout=600, env=e)
    result = [l for l in p.stdout.splitlines() if l.startswith('Result:')]
    print('\n'.join(result))
    check(p.returncode == 0 and result and result[-1].endswith(' 0 tests failed'), 'the validator: %d %r' % (p.returncode, result))
    check(not os.listdir(t.path('home')), 'instance folders left: %r' % os.listdir(t.path('home')))
```

  Append to `plugin/CMakeLists.txt`:

```cmake
# --- the bundle LICHOMAT.vst3 (Task 8) ---------------------------------------------------------
smtg_add_vst3plugin(LICHOMAT vst3/plugin_factory.cpp vst3/plugin_processor.cpp vst3/plugin_controller.cpp
    vst3/plugin_link.cpp vst3/plugin_card.cpp)
target_sources(LICHOMAT PRIVATE $<TARGET_OBJECTS:plug_common> $<TARGET_OBJECTS:plug_card>)
target_link_libraries(LICHOMAT PRIVATE sdk)
target_include_directories(LICHOMAT PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/vst3" "${LXR_ROOT}/emu/core")
target_compile_options(LICHOMAT PRIVATE ${PLUGIN_FLAGS})
target_compile_features(LICHOMAT PRIVATE cxx_std_17)
configure_file(vst3/Info.plist.in "${CMAKE_BINARY_DIR}/LICHOMAT-Info.plist" @ONLY)
set_target_properties(LICHOMAT PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/LICHOMAT-Info.plist")
# Before the link, the engine into Contents/Helpers and the licences into Contents/Resources (the MIT
# licence asks for its notice with every copy); after the SDK's steps (moduleinfo.json), the signature
# of the helper and then of the whole bundle, ad hoc
add_dependencies(LICHOMAT lichomat-engine)
add_custom_command(TARGET LICHOMAT PRE_LINK
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Helpers"
        "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:lichomat-engine>" "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Helpers/lichomat-engine"
    COMMAND ${CMAKE_COMMAND} -E copy "${LXR_ROOT}/LICENSE" "${LXR_ROOT}/NOTICE.md" "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Resources/"
    COMMAND ${CMAKE_COMMAND} -E copy "${PLUGIN_DIR}/third_party/vst3sdk/LICENSE.txt"
        "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Resources/VST3-SDK-LICENSE.txt"
    VERBATIM)
add_custom_command(TARGET LICHOMAT POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Helpers/lichomat-engine"
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:LICHOMAT>"
    VERBATIM)
set(PLUGIN_BUNDLE_TEST ${PLUGIN_TEST} --bundle $<TARGET_BUNDLE_DIR:LICHOMAT> --validator $<TARGET_FILE:validator>)
add_test(NAME P10_plugin_validator COMMAND ${PLUGIN_BUNDLE_TEST} P10_plugin_validator)
set_tests_properties(P10_plugin_validator PROPERTIES TIMEOUT 600 LABELS plugin)
```

- [ ] **Step 2: run it.** PLUGIN with `-R P10_plugin_validator`. Expected: `== plugin build: 1`; the configure stops at `Cannot find source file: vst3/plugin_factory.cpp`.
- [ ] **Step 3: ids, folders, the link** (P-6, P-16, P-18, P-19). `plugin/vst3/plugin_ids.h`:

```cpp
/*
 * plugin_ids.h
 *
 * The LICHOMAT plugin's class ids and parameters (plan E9.2 Task 8).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace lichomat {

/* random, made once for this plugin; a host keys a set's devices by them: never change them */
static const Steinberg::FUID kProcessorUID (0x7A3C1E54, 0x9B2D4F61, 0x8E0A5C37, 0x41D6B9F2);
static const Steinberg::FUID kControllerUID (0x2F8B6D13, 0xC4E1A970, 0x5D3F82B6, 0x9A7C0E45);

/** the engine's state, read only: 0 stopped, 1 starting, 2 running, 3 failed (the steps of a list
 * of 4, plug_shm.h's PLUG_ENGINE_* in that order) */
enum : Steinberg::Vst::ParamID
{
	kParamEngine = 0
};

} // namespace lichomat
```

  `plugin/vst3/plugin_card.h`:

```cpp
/*
 * plugin_card.h
 *
 * Each plugin instance's folder (plan E9.2 Task 8): BASE/<16 hex digits>/ holds card/ (the
 * engine's card), engine.log, plugin.log and lock, which the instance holds with flock() for its
 * life. BASE is $LICHOMAT_PLUGIN_HOME, or ~/Library/Application Support/LICHOMAT Plugin. A folder
 * whose lock nobody holds and that is older than a minute is left from a host that died: the
 * next instance removes it.
 *
 * A new instance's card is a copy, in the card's order and with /TMP, of BASE/Card, the
 * plugin's own start card, if there is one; else of LICHOMAT Emu's card ($LICHOMAT_EMU_HOME/Card,
 * or ~/Library/Application Support/LICHOMAT Emu/Card, app_card.c:55-77), so that it starts where
 * the app left off; else it stays empty and the engine seeds the init card. A restored state
 * replaces it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include <string>

namespace lichomat {

class CardFolder
{
public:
	~CardFolder () { release (); }

	/** BASE, made if missing; empty on an error */
	static std::string base ();
	/** remove the folders of BASE that no instance holds and that are older than 60 s */
	static void cleanStale ();

	/** a new folder with an empty card/, its lock held; false with the problem in err */
	bool create (std::string& err);
	/** the card of a new instance: BASE/Card or LICHOMAT Emu's, copied into card/; true if copied */
	bool copyStartCard ();
	/** delete the folder and let go of the lock */
	void remove ();
	/** let go of the lock and keep the folder (it is the next instance's to clean) */
	void release ();

	bool valid () const { return lockFd >= 0; }
	const std::string& dir () const { return dirPath; }
	std::string card () const { return dirPath + "/card"; }
	std::string engineLog () const { return dirPath + "/engine.log"; }
	std::string pluginLog () const { return dirPath + "/plugin.log"; }

	/** move other's folder and lock into this one, which lets go of its own first */
	void take (CardFolder& other);

private:
	std::string dirPath;
	int lockFd = -1;
};

} // namespace lichomat
```

  `plugin/vst3/plugin_card.cpp`:

```cpp
/*
 * plugin_card.cpp
 *
 * Each plugin instance's folder (plan E9.2 Task 8); see plugin_card.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plugin_card.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "emu_hostfs.h"	/* a C header without its own extern "C" */
}

namespace lichomat {

static int mkdirs (const std::string& path)
{
	std::string p;
	size_t at = 0;
	while (at != std::string::npos)
	{
		at = path.find ('/', at + 1);
		p = path.substr (0, at);
		if (mkdir (p.c_str (), 0755) != 0 && errno != EEXIST)
			return -1;
	}
	return 0;
}

static int removeEntry (const char* path, const struct stat*, int, struct FTW*)
{
	return ::remove (path) == 0 ? 0 : -1;
}

static void removeTree (const std::string& dir)
{
	if (!dir.empty ())
		nftw (dir.c_str (), removeEntry, 16, FTW_DEPTH | FTW_PHYS);
}

std::string CardFolder::base ()
{
	std::string b;
	const char* env = getenv ("LICHOMAT_PLUGIN_HOME");
	if (env && *env)
		b = env;
	else if (const char* home = getenv ("HOME"))
		b = std::string (home) + "/Library/Application Support/LICHOMAT Plugin";
	else
		return {};
	return mkdirs (b) == 0 ? b : std::string ();
}

static bool instanceName (const char* name)
{
	if (strlen (name) != 16)
		return false;
	for (const char* p = name; *p; p++)
		if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
			return false;
	return true;
}

void CardFolder::cleanStale ()
{
	const std::string b = base ();
	DIR* d = b.empty () ? nullptr : opendir (b.c_str ());
	if (!d)
		return;
	const time_t now = time (nullptr);
	for (const struct dirent* e; (e = readdir (d)) != nullptr;)
	{
		if (!instanceName (e->d_name))
			continue;
		const std::string dir = b + "/" + e->d_name;
		struct stat st;
		if (stat (dir.c_str (), &st) != 0 || !S_ISDIR (st.st_mode) || now - st.st_mtime < 60)
			continue;
		const int fd = open ((dir + "/lock").c_str (), O_RDWR | O_CLOEXEC);
		if (fd >= 0 && flock (fd, LOCK_EX | LOCK_NB) == 0)
			removeTree (dir);	/* nobody holds it */
		if (fd >= 0)
			close (fd);
	}
	closedir (d);
}

bool CardFolder::create (std::string& err)
{
	release ();
	const std::string b = base ();
	if (b.empty ())
	{
		err = "no folder for the plugin's cards (HOME is not set, or it cannot be made)";
		return false;
	}
	for (int attempt = 0; attempt < 8; attempt++)
	{
		uint8_t id[8];
		arc4random_buf (id, sizeof id);
		char name[17];
		for (int i = 0; i < 8; i++)
			snprintf (name + 2 * i, 3, "%02x", id[i]);
		const std::string dir = b + "/" + name;
		if (mkdir (dir.c_str (), 0755) != 0)
			continue;	/* taken: another id */
		const int fd = open ((dir + "/lock").c_str (), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
		if (fd < 0 || flock (fd, LOCK_EX | LOCK_NB) != 0 || mkdir ((dir + "/card").c_str (), 0755) != 0)
		{
			err = std::string ("cannot make ") + dir + ": " + strerror (errno);
			if (fd >= 0)
				close (fd);
			removeTree (dir);
			return false;
		}
		dirPath = dir;
		lockFd = fd;
		return true;
	}
	err = "cannot make a folder in " + b;
	return false;
}

bool CardFolder::copyStartCard ()
{
	std::string app;
	const char* env = getenv ("LICHOMAT_EMU_HOME");
	if (env && *env)
		app = std::string (env) + "/Card";
	else if (const char* home = getenv ("HOME"))
		app = std::string (home) + "/Library/Application Support/LICHOMAT Emu/Card";
	const std::string own = base () + "/Card";
	const std::string& from = emu_hostKind (own.c_str ()) == 1 ? own : app;
	if (from.empty () || emu_hostKind (from.c_str ()) != 1 || dirPath.empty ())
		return false;
	if (emu_hostCopyCard (from.c_str (), card ().c_str (), 0) == 0)
		return true;
	removeTree (card ());	/* a part of it: the engine seeds the init card instead */
	mkdir (card ().c_str (), 0755);
	return false;
}

void CardFolder::remove ()
{
	removeTree (dirPath);
	release ();
}

void CardFolder::release ()
{
	if (lockFd >= 0)
		close (lockFd);
	lockFd = -1;
	dirPath.clear ();
}

void CardFolder::take (CardFolder& other)
{
	release ();
	dirPath = other.dirPath;
	lockFd = other.lockFd;
	other.dirPath.clear ();
	other.lockFd = -1;
}

} // namespace lichomat
```

  `plugin/vst3/plugin_link.h`:

```cpp
/*
 * plugin_link.h
 *
 * The plugin's side of its engine process (plan E9.2 Task 8): the shared memory, the wake pipe,
 * and a monitor thread that starts lichomat-engine, sees it die (the end of its life pipe) or
 * hang (2 s without taking the published events), starts it again after 0.5 s, and gives up
 * (failed) after 5 deaths in a minute. While no engine runs the audio thread reads silence: the
 * memory stays mapped from start() to stop().
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include "plug_shm.h"

namespace lichomat {

class EngineLink
{
public:
	~EngineLink () { stop (); }

	/* the host's main thread */

	/** map the memory and start the monitor, which starts the engine on card; realtime: the host
	 * plays in real time; false with err */
	bool start (double rate, uint32_t maxBlock, bool realtime, const std::string& card, const std::string& engineLog,
	            const std::string& pluginLog, std::string& err);
	/** the engine starts again on another card; returns once the old engine is gone (at most 3 s) */
	void restart (const std::string& card, const std::string& engineLog, const std::string& pluginLog);
	/** end the engine (quit, then SIGKILL after 2 s), the monitor, the memory */
	void stop ();

	/* the audio thread: no allocation, no lock, one non-blocking write */

	plug_shm_t* shm () const { return mem; }
	void wake () const
	{
		if (wakeFd[1] >= 0)
			(void)!write (wakeFd[1], "w", 1);	/* O_NONBLOCK: a full pipe is a wake-up already pending */
	}

	/** the running engine's pid, -1 when none (tests) */
	pid_t pid () const { return enginePid.load (); }

	/** lichomat-engine next to the plugin's binary: Contents/Helpers/ of the bundle */
	static std::string enginePath ();

private:
	void monitor ();
	void note (const char* fmt, ...) __attribute__ ((format (printf, 2, 3)));

	plug_shm_t* mem = nullptr;
	int memFd = -1;
	int wakeFd[2] = {-1, -1};
	std::thread thread;
	std::atomic<bool> stopping {false};
	std::atomic<bool> restartWanted {false};
	std::atomic<bool> restarted {false};
	std::atomic<pid_t> enginePid {-1};
	std::mutex paths;	/* the monitor thread and restart(); never the audio thread */
	std::string card, engineLog, pluginLog, engine;
};

} // namespace lichomat
```

  `plugin/vst3/plugin_link.cpp`:

```cpp
/*
 * plugin_link.cpp
 *
 * The plugin's side of its engine process (plan E9.2 Task 8); see plugin_link.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plugin_link.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <vector>

#include "plug_spawn.h"

namespace lichomat {

static double wallNow ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

std::string EngineLink::enginePath ()
{
	Dl_info info;
	if (!dladdr ((const void*)&EngineLink::enginePath, &info) || !info.dli_fname)
		return {};
	std::string p = info.dli_fname;	/* .../LICHOMAT.vst3/Contents/MacOS/LICHOMAT */
	const size_t slash = p.rfind ('/');
	if (slash == std::string::npos)
		return {};
	return p.substr (0, slash) + "/../Helpers/lichomat-engine";
}

void EngineLink::note (const char* fmt, ...)
{
	std::string path;
	{
		std::lock_guard<std::mutex> g (paths);
		path = pluginLog;
	}
	FILE* f = path.empty () ? nullptr : fopen (path.c_str (), "a");
	if (!f)
		return;
	char when[32];
	const time_t now = time (nullptr);
	strftime (when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime (&now));
	fprintf (f, "%s ", when);
	va_list ap;
	va_start (ap, fmt);
	vfprintf (f, fmt, ap);
	va_end (ap);
	fputc ('\n', f);
	fclose (f);
}

bool EngineLink::start (double rate, uint32_t maxBlock, bool realtime, const std::string& cardDir,
                        const std::string& eLog, const std::string& pLog, std::string& err)
{
	stop ();
	char e[256];
	mem = plug_shmCreate (rate, maxBlock, &memFd, e, sizeof e);
	if (!mem)
	{
		err = e;
		return false;
	}
	mem->realtime = realtime ? 1u : 0u;
	if (pipe (wakeFd) != 0 || fcntl (wakeFd[0], F_SETFD, FD_CLOEXEC) != 0 || fcntl (wakeFd[1], F_SETFD, FD_CLOEXEC) != 0
	    || fcntl (wakeFd[1], F_SETFL, O_NONBLOCK) != 0)
	{
		err = std::string ("pipe: ") + strerror (errno);
		stop ();
		return false;
	}
	{
		std::lock_guard<std::mutex> g (paths);
		card = cardDir;
		engineLog = eLog;
		pluginLog = pLog;
		engine = enginePath ();
	}
	stopping = false;
	restartWanted = false;
	thread = std::thread ([this] { monitor (); });
	return true;
}

void EngineLink::restart (const std::string& cardDir, const std::string& eLog, const std::string& pLog)
{
	{
		std::lock_guard<std::mutex> g (paths);
		card = cardDir;
		engineLog = eLog;
		pluginLog = pLog;
	}
	if (!thread.joinable ())
		return;
	restarted = false;
	restartWanted = true;
	const double t0 = wallNow ();
	while (!restarted && wallNow () - t0 < 3.0)
		std::this_thread::sleep_for (std::chrono::milliseconds (5));
}

void EngineLink::stop ()
{
	if (thread.joinable ())
	{
		stopping = true;
		if (mem)
			plug_store (&mem->quit, 1);
		wake ();
		thread.join ();
	}
	for (int& fd : wakeFd)
	{
		if (fd >= 0)
			close (fd);
		fd = -1;
	}
	if (mem)
		plug_shmClose (mem);
	mem = nullptr;
	if (memFd >= 0)
		close (memFd);
	memFd = -1;
}

/* the monitor thread: one engine after the other until stop() */
void EngineLink::monitor ()
{
	std::vector<double> deaths;
	while (!stopping)
	{
		int life[2];
		if (pipe (life) != 0)
		{
			note ("pipe: %s", strerror (errno));
			plug_store (&mem->state, PLUG_ENGINE_FAILED);
			break;
		}
		fcntl (life[0], F_SETFD, FD_CLOEXEC);
		fcntl (life[1], F_SETFD, FD_CLOEXEC);
		std::string c, log, exe;
		{
			std::lock_guard<std::mutex> g (paths);
			c = card;
			log = engineLog;
			exe = engine;
		}
		plug_store (&mem->quit, 0);
		plug_store (&mem->state, PLUG_ENGINE_NONE);
		char err[256];
		const pid_t pid = plug_spawnEngine (exe.c_str (), c.c_str (), wakeFd[0], life[1], memFd, log.c_str (), err, sizeof err);
		close (life[1]);
		if (pid < 0)
		{
			note ("cannot start the engine: %s", err);
			close (life[0]);
			plug_store (&mem->state, PLUG_ENGINE_FAILED);
			break;
		}
		enginePid = pid;
		restarted = true;
		note ("engine %d started on %s", (int)pid, c.c_str ());
		/* watch it */
		const double started = wallNow ();
		double lastMove = started;
		uint64_t lastFrontier = plug_load (&mem->frontier);
		const char* why = "it died";
		for (;;)
		{
			struct pollfd p = {life[0], POLLIN, 0};
			const int r = poll (&p, 1, 100);
			if (r > 0)
			{
				char b;
				if (read (life[0], &b, 1) <= 0)
					break;	/* the end of the pipe: the engine is gone */
			}
			if (stopping)
			{
				why = "the plugin stopped it";
				plug_store (&mem->quit, 1);
				wake ();
				struct pollfd q = {life[0], POLLIN, 0};
				if (poll (&q, 1, 2000) <= 0)
					kill (pid, SIGKILL);
				break;
			}
			if (restartWanted)
			{
				why = "a new card";
				kill (pid, SIGKILL);
				continue;	/* until the end of the pipe */
			}
			const double now = wallNow ();
			const uint64_t st = plug_load (&mem->state), frontier = plug_load (&mem->frontier);
			if (frontier != lastFrontier)
			{
				lastFrontier = frontier;
				lastMove = now;
			}
			const bool behind = plug_load (&mem->hostFrames) > frontier + (uint64_t)(mem->rate / 2.0);
			if ((st == PLUG_ENGINE_RUNNING && behind && now - lastMove > 2.0)
			    || (st != PLUG_ENGINE_RUNNING && now - started > 30.0))
			{
				why = st == PLUG_ENGINE_RUNNING ? "it hung (2 s without taking events)" : "it did not start in 30 s";
				kill (pid, SIGKILL);
			}
		}
		close (life[0]);
		int status = 0;
		(void)waitpid (pid, &status, 0);	/* ECHILD when the host reaps its children itself */
		enginePid = -1;
		const bool failed = plug_load (&mem->state) == PLUG_ENGINE_FAILED;
		note ("engine %d ended: %s; underruns %llu, late %llu, dropped %llu, ring full %llu", (int)pid, why,
		      (unsigned long long)mem->underruns, (unsigned long long)mem->late, (unsigned long long)mem->dropped,
		      (unsigned long long)mem->evDropped);
		if (stopping)
			break;
		plug_store (&mem->state, PLUG_ENGINE_NONE);	/* the audio thread reads silence */
		if (restartWanted)
		{
			restartWanted = false;
			continue;
		}
		const double now = wallNow ();
		deaths.push_back (now);
		while (!deaths.empty () && now - deaths.front () > 60.0)
			deaths.erase (deaths.begin ());
		if (deaths.size () >= 5 || failed)
		{
			note ("the engine is not started again: %s", failed ? "it reported a failure" : "5 deaths in a minute");
			plug_store (&mem->state, PLUG_ENGINE_FAILED);
			while (!stopping && !restartWanted)
				std::this_thread::sleep_for (std::chrono::milliseconds (50));
			restartWanted = false;
			deaths.clear ();
			continue;
		}
		for (int i = 0; i < 10 && !stopping; i++)
			std::this_thread::sleep_for (std::chrono::milliseconds (50));
	}
	restarted = true;
}

} // namespace lichomat
```

- [ ] **Step 4: the processor, the controller, the factory** (P-10, P-12, P-13, P-14, P-20, P-21). `plugin/vst3/plugin_processor.h`:

```cpp
/*
 * plugin_processor.h
 *
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8): one stereo output, one event input.
 * Each block it turns the host's transport into MIDI clock (plug_clock.h) and the block's notes
 * into MIDI, writes both into the engine's ring at their frames, publishes the block, wakes the
 * engine, and plays the ring plug_readOffset() frames behind; the reported latency is
 * plug_latency(). In real time it never waits; offline (kOffline: Live's bounce, freeze and
 * export) it waits for the engine. The state is the card folder as an LCHS archive
 * (plug_archive.h).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include "plug_clock.h"
#include "plugin_card.h"
#include "plugin_link.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

namespace lichomat {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
	Processor ();
	static Steinberg::FUnknown* create (void*);

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
	                                                  Steinberg::Vst::SpeakerArrangement* outputs,
	                                                  Steinberg::int32 numOuts) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
	Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE { return latency; }
	Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE { return Steinberg::Vst::kInfiniteTail; }
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	static constexpr unsigned kMaxMidi = 1024;	/**< MIDI messages of one block: clocks and notes */

private:
	unsigned notesOf (Steinberg::Vst::IEventList* events, Steinberg::int32 frames);
	void reportState (Steinberg::Vst::ProcessData& data, uint64_t state);

	EngineLink link;
	CardFolder folder;
	plug_clock_t clock {};
	Steinberg::uint32 latency = 0;
	uint32_t readOffset = 0;
	uint32_t margin = 0;
	uint64_t frames = 0;		/**< host frames since the activation */
	uint64_t runningPid = 0;	/**< the engine that the clock generator last started */
	int reported = -1;			/**< the engine state last sent to the controller */
	bool active = false;
	plug_midi_t clocks[kMaxMidi];
	plug_midi_t notes[kMaxMidi];
};

} // namespace lichomat
```

  `plugin/vst3/plugin_processor.cpp`:

```cpp
/*
 * plugin_processor.cpp
 *
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8); see plugin_processor.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plugin_processor.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "plug_archive.h"
#include "plug_time.h"
#include "plugin_ids.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace lichomat {

Processor::Processor ()
{
	setControllerClass (kControllerUID);
	processContextRequirements.needTempo ().needTransportState ().needProjectTimeMusic ().needCycleMusic ();
}

FUnknown* Processor::create (void*)
{
	return (IAudioProcessor*)new Processor;
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
	const tresult r = AudioEffect::initialize (context);
	if (r != kResultOk)
		return r;
	addAudioOutput (STR16 ("Out"), SpeakerArr::kStereo);
	addEventInput (STR16 ("MIDI In"), 16);
	CardFolder::cleanStale ();
	return kResultOk;
}

tresult PLUGIN_API Processor::terminate ()
{
	link.stop ();
	folder.remove ();
	return AudioEffect::terminate ();
}

tresult PLUGIN_API Processor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs,
                                                   int32 numOuts)
{
	if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize (int32 symbolicSampleSize)
{
	return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing (ProcessSetup& setup)
{
	if (setup.maxSamplesPerBlock < 1 || setup.maxSamplesPerBlock > 8192 || !(setup.sampleRate >= 8000.0)
	    || setup.sampleRate > 384000.0)
		return kResultFalse;
	margin = plug_marginFromEnv ();
	latency = plug_latency (setup.sampleRate, (uint32_t)setup.maxSamplesPerBlock, margin);
	readOffset = plug_readOffset (setup.sampleRate, (uint32_t)setup.maxSamplesPerBlock, margin);
	return AudioEffect::setupProcessing (setup);
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
	if (state)
	{
		std::string err;
		if (!folder.valid ())
		{
			if (folder.create (err))
				folder.copyStartCard ();	/* a new instance: the plugin's start card or LICHOMAT Emu's */
			else
				fprintf (stderr, "LICHOMAT: %s\n", err.c_str ());
		}
		plug_clockInit (&clock, processSetup.sampleRate);
		frames = 0;
		runningPid = 0;
		reported = -1;
		if (folder.valid ()
		    && !link.start (processSetup.sampleRate, (uint32_t)processSetup.maxSamplesPerBlock,
		                    processSetup.processMode == kRealtime, folder.card (), folder.engineLog (), folder.pluginLog (),
		                    err))
			fprintf (stderr, "LICHOMAT: the engine cannot start: %s\n", err.c_str ());
		active = true;
	}
	else
	{
		link.stop ();
		active = false;
	}
	return AudioEffect::setActive (state);
}

/* the block's Note Ons and Offs as MIDI, in frame order */
unsigned Processor::notesOf (IEventList* events, int32 n)
{
	unsigned count = 0;
	const int32 total = events ? events->getEventCount () : 0;
	for (int32 i = 0; i < total && count < kMaxMidi; i++)
	{
		Event e;
		if (events->getEvent (i, e) != kResultOk)
			continue;
		uint8_t st, pitch, vel;
		if (e.type == Event::kNoteOnEvent)
		{
			const long v = lrintf (e.noteOn.velocity * 127.0f);
			vel = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
			st = (uint8_t)((vel ? 0x90 : 0x80) | (e.noteOn.channel & 15));
			pitch = (uint8_t)(e.noteOn.pitch & 0x7F);
		}
		else if (e.type == Event::kNoteOffEvent)
		{
			const long v = lrintf (e.noteOff.velocity * 127.0f);
			vel = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
			st = (uint8_t)(0x80 | (e.noteOff.channel & 15));
			pitch = (uint8_t)(e.noteOff.pitch & 0x7F);
		}
		else
			continue;
		const uint32_t off = (uint32_t)(e.sampleOffset < 0 ? 0 : e.sampleOffset >= n ? n - 1 : e.sampleOffset);
		/* insertion in frame order, after the messages of the same frame */
		unsigned k = count++;
		while (k > 0 && notes[k - 1].offset > off)
		{
			notes[k] = notes[k - 1];
			k--;
		}
		notes[k] = plug_midi_t {off, 3, {st, pitch, vel}};
	}
	return count;
}

void Processor::reportState (ProcessData& data, uint64_t state)
{
	if ((int)state == reported || !data.outputParameterChanges)
		return;
	int32 index = 0;
	IParamValueQueue* q = data.outputParameterChanges->addParameterData (kParamEngine, index);
	int32 point = 0;
	if (q && q->addPoint (0, (double)state / 3.0, point) == kResultOk)
		reported = (int)state;
}

/* Offline (a bounce, a freeze, an export) the host does not wait for real time, nor need we:
 * process() waits for the engine, which runs at least 15 times real time. 10 s at most each. */
static void offlineWait (plug_shm_t* s, uint64_t audioEnd)
{
	for (int i = 0; i < 100000; i++)
	{
		const uint64_t st = plug_load (&s->state);
		if (st == PLUG_ENGINE_FAILED || (st == PLUG_ENGINE_RUNNING && plug_load (&s->audioEnd) >= audioEnd))
			return;
		struct timespec d = {0, 100000L};
		nanosleep (&d, nullptr);
	}
}

tresult PLUGIN_API Processor::process (ProcessData& data)
{
	plug_shm_t* s = link.shm ();
	const bool offline = processSetup.processMode == kOffline;
	if (offline && s && data.numSamples > 0)
		offlineWait (s, 0);	/* running, so that the first block's events reach it */
	const uint64_t state = s ? plug_load (&s->state) : PLUG_ENGINE_NONE;
	reportState (data, state);
	if (data.numSamples <= 0 || data.numOutputs < 1 || data.outputs[0].numChannels < 2)
		return kResultOk;
	const uint32_t n = (uint32_t)data.numSamples;
	const bool running = s && state == PLUG_ENGINE_RUNNING;
	if (running)
	{
		const uint64_t pid = plug_load (&s->pid);
		if (pid != runningPid)
		{
			plug_clockReset (&clock);	/* a new engine: the slave starts afresh */
			runningPid = pid;
		}
	}
	plug_transport_t t {};
	if (const ProcessContext* c = data.processContext)
	{
		t.valid = (c->state & ProcessContext::kTempoValid) && (c->state & ProcessContext::kProjectTimeMusicValid);
		t.playing = (c->state & ProcessContext::kPlaying) != 0;
		t.tempo = c->tempo;
		t.ppq = c->projectTimeMusic;
		t.cycle = (c->state & ProcessContext::kCycleActive) && (c->state & ProcessContext::kCycleValid);
		t.cycleStart = c->cycleStartMusic;
		t.cycleEnd = c->cycleEndMusic;
	}
	const unsigned nc = plug_clockBlock (&clock, &t, n, clocks, kMaxMidi);
	const unsigned nn = notesOf (data.inputEvents, (int32)n);
	if (running)
		for (unsigned i = 0, j = 0; i < nc || j < nn;)
		{
			/* the transport's messages first at a frame: Start before a note on the downbeat */
			const plug_midi_t& m = (j == nn || (i < nc && clocks[i].offset <= notes[j].offset)) ? clocks[i++] : notes[j++];
			(void)plug_evPush (s, frames + m.offset, m.b, m.len);
		}
	if (s)
	{
		plug_publish (s, frames + n);
		link.wake ();
		if (offline && frames + n > readOffset)
			offlineWait (s, frames + n - readOffset);
	}
	float* l = data.outputs[0].channelBuffers32[0];
	float* r = data.outputs[0].channelBuffers32[1];
	uint32_t copied = 0;
	const uint32_t skip = frames >= readOffset ? 0u : (uint32_t)std::min<uint64_t> (n, readOffset - frames);
	for (uint32_t i = 0; i < skip; i++)
		l[i] = r[i] = 0.0f;
	if (skip < n)
	{
		if (s)
			copied = plug_audioRead (s, frames + skip - readOffset, n - skip, l + skip, r + skip);
		else
			for (uint32_t i = skip; i < n; i++)
				l[i] = r[i] = 0.0f;
	}
	data.outputs[0].silenceFlags = copied ? 0 : 3;
	frames += n;
	return kResultOk;
}

static int streamPut (void* ctx, const void* p, size_t n)
{
	int32 written = 0;
	return static_cast<IBStream*> (ctx)->write (const_cast<void*> (p), (int32)n, &written) == kResultOk
	           && written == (int32)n
	           ? 0
	           : -1;
}

static int streamGet (void* ctx, void* p, size_t n)
{
	int32 got = 0;
	return static_cast<IBStream*> (ctx)->read (p, (int32)n, &got) == kResultOk && got == (int32)n ? 0 : -1;
}

tresult PLUGIN_API Processor::getState (IBStream* state)
{
	std::string err;
	if (!state)
		return kResultFalse;
	if (!folder.valid ())
	{
		if (!folder.create (err))
			return kResultFalse;
		folder.copyStartCard ();
	}
	char e[512];
	if (plug_archiveWrite (folder.card ().c_str (), streamPut, state, e, sizeof e) != 0)
	{
		fprintf (stderr, "LICHOMAT: getState: %s\n", e);
		return kResultFalse;
	}
	return kResultOk;
}

tresult PLUGIN_API Processor::setState (IBStream* state)
{
	CardFolder fresh;
	std::string err;
	if (!state || !fresh.create (err))
		return kResultFalse;
	char e[512];
	if (plug_archiveRead (fresh.card ().c_str (), streamGet, state, e, sizeof e) != 0)
	{
		fprintf (stderr, "LICHOMAT: setState: %s\n", e);
		fresh.remove ();
		return kResultFalse;
	}
	if (active)
		link.restart (fresh.card (), fresh.engineLog (), fresh.pluginLog ());
	CardFolder old;
	old.take (folder);
	folder.take (fresh);
	old.remove ();
	return kResultOk;
}

} // namespace lichomat
```

  `plugin/vst3/plugin_controller.h`:

```cpp
/*
 * plugin_controller.h
 *
 * The LICHOMAT plugin's edit controller (plan E9.2 Task 8).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace lichomat {

class Controller : public Steinberg::Vst::EditControllerEx1
{
public:
	static Steinberg::FUnknown* create (void*);
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
};

} // namespace lichomat
```

  `plugin/vst3/plugin_controller.cpp`:

```cpp
/*
 * plugin_controller.cpp
 *
 * The LICHOMAT plugin's edit controller (plan E9.2 Task 8): no window of its own; one read-only
 * parameter, the engine's state, for the host's generic view and for the tests.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plugin_controller.h"

#include "plugin_ids.h"
#include "public.sdk/source/vst/vstparameters.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace lichomat {

FUnknown* Controller::create (void*)
{
	return (IEditController*)new Controller;
}

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
	const tresult r = EditControllerEx1::initialize (context);
	if (r != kResultOk)
		return r;
	auto* p = new StringListParameter (STR16 ("Engine"), kParamEngine, nullptr, ParameterInfo::kIsReadOnly | ParameterInfo::kIsList);
	p->appendString (STR16 ("stopped"));
	p->appendString (STR16 ("starting"));
	p->appendString (STR16 ("running"));
	p->appendString (STR16 ("failed"));
	parameters.addParameter (p);
	return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState (IBStream*)
{
	return kResultOk;	/* the processor's state is the card; no parameter follows from it */
}

} // namespace lichomat
```

  `plugin/vst3/plugin_factory.cpp`:

```cpp
/*
 * plugin_factory.cpp
 *
 * The LICHOMAT plugin's class factory (plan E9.2 Task 8): one instrument, "LICHOMAT" by
 * "LICHOMAT", in the host's Instrument|Drum category. No URL or address: the strings of the
 * binary name no maker (M21).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_notices.h"
#include "plugin_controller.h"
#include "plugin_ids.h"
#include "plugin_processor.h"
#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF (PLUG_VENDOR, "", "")

DEF_CLASS2 (INLINE_UID_FROM_FUID (lichomat::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass, PLUG_NAME,
            Vst::kDistributable, PlugType::kInstrumentDrum, PLUG_VERSION, kVstVersionString, lichomat::Processor::create)

DEF_CLASS2 (INLINE_UID_FROM_FUID (lichomat::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            PLUG_NAME " Controller", 0, "", PLUG_VERSION, kVstVersionString, lichomat::Controller::create)

END_FACTORY
```

  and the bundle's plist, `plugin/vst3/Info.plist.in`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleExecutable</key>
	<string>${MACOSX_BUNDLE_EXECUTABLE_NAME}</string>
	<key>CFBundleIdentifier</key>
	<string>org.lichomat.plugin</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>LICHOMAT</string>
	<key>CFBundlePackageType</key>
	<string>BNDL</string>
	<key>CFBundleShortVersionString</key>
	<string>@PROJECT_VERSION@</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleVersion</key>
	<string>@PROJECT_VERSION@</string>
	<key>NSHumanReadableCopyright</key>
	<string>GPL-3.0-only</string>
</dict>
</plist>
```

- [ ] **Step 5: pass.** PLUGIN with `-R P10_plugin_validator`, then:

```bash
cd /Users/tk/Code/lxr/build/wt-plugin && find build/plugin/VST3/Debug/LICHOMAT.vst3 -type f | sed 's|.*LICHOMAT.vst3/||' | sort && codesign --verify --deep --strict build/plugin/VST3/Debug/LICHOMAT.vst3 && plutil -extract CFBundleIdentifier raw build/plugin/VST3/Debug/LICHOMAT.vst3/Contents/Info.plist && ls ~/Library/Audio/Plug-Ins
```

  Expected: `100% tests passed, 0 tests failed out of 1` (`Result: 47 tests passed, 0 tests failed` in `-V`); the files `Contents/Helpers/lichomat-engine`, `Contents/Info.plist`, `Contents/MacOS/LICHOMAT`, `Contents/Resources/LICENSE`, `Contents/Resources/NOTICE.md`, `Contents/Resources/VST3-SDK-LICENSE.txt`, `Contents/Resources/moduleinfo.json`, `Contents/_CodeSignature/CodeResources`; codesign silent (valid); `org.lichomat.plugin`; the `ls` line of Task 1.
- [ ] **Step 6: the gate.** PLUGIN-CHECK (T 39). **Step 7: commit** `plugin Task 8 (E9.18): LICHOMAT.vst3, the processor, the controller, the engine link and the instance folders; the validator` with the eleven files, `plugin/tests/plugin_test.py` and `plugin/CMakeLists.txt`; push.

---

### Task 9: The test host, the real-time check, the scenarios — E9.19

**Files:** Create `plugin/host/plug_host.cpp`, `plugin/tests/plug_rtcheck.c`; modify `plugin/tests/plugin_test.py`, `plugin/CMakeLists.txt` (append).

- [ ] **Step 1: the failing tests.** In `plugin/tests/plugin_test.py`, after Task 8's section and before `def main():`:

```python
# --- the VST3 bundle through lichomat-plughost (Task 9) -----------------------------------

def plan(t, name, lines, rate=RATE_TEXT, block=128, frames=None, card=True):
    """a plughost plan in the case's folder: the bundle, the rate, the block, the frames, the onset card"""
    body = ['bundle ' + t.a.bundle, 'rate ' + str(rate), 'block %d' % block, 'frames %d' % (frames or blocks_frames(12))]
    if card:
        body.append('card ' + os.path.join(t.a.root, 'emu', 'tests', 'cards', 'onset'))
    path = t.path(name)
    with open(path, 'w') as f:
        f.write('\n'.join(body + lines) + '\n')
    return path


def host(t, plan_path, env=None):
    e = dict(os.environ)
    e['LICHOMAT_PLUGIN_HOME'] = t.path('home')
    e['LICHOMAT_EMU_HOME'] = t.path('no-emu')	# no LICHOMAT Emu card unless a case gives one
    e.update(env or {})
    p = subprocess.run([t.a.host, plan_path], capture_output=True, text=True, timeout=600, env=e)
    print(p.stdout.strip())
    if p.returncode != 0:
        raise Fail('lichomat-plughost exited %d:\n%s%s' % (p.returncode, p.stdout, p.stderr))
    lat = [int(l.split('latency ')[1].split(',')[0]) for l in p.stdout.splitlines() if 'latency ' in l]
    return lat, p.stdout + p.stderr


def shifted_equal(a, b, shift):
    """a's frames from shift on against b's from 0, both interleaved stereo: (frames compared, samples that differ)"""
    n = min(len(a) // 2 - shift, len(b) // 2)
    pa, pb = a[2 * shift:2 * (shift + n)], b[:2 * n]
    return n, sum(1 for x, y in zip(pa, pb) if x != y), sum(1 for x in pa if x)


# a 12 s timeline: Start at 4,410 (the block from 4,480 on), 128 BPM, the loop [4, 8) quarter notes, 140 BPM
# from 300,000; Note Ons and Offs on Drum 1's note
TIMELINE = ['play 4410 128 0', 'loop 4 8', 'tempo 300000 140', 'note 9000 0 48 127', 'note 12000 0 48 0',
            'note 64000 0 48 100', 'note 67000 0 48 0', 'note 400003 0 48 127']


@case
def P10_plugin_native(t):
    # at 44,140.625 Hz, offline, blocks of 128 and of 100: the plugin's output from frame latency - 128 on is
    # lichomat-render's for the MIDI the plugin gave its engine (plughost's script), byte for byte
    for block in (128, 100):
        script = t.path('script%d.txt' % block)
        lat, _ = host(t, plan(t, 'plan%d.txt' % block, TIMELINE + ['out ' + t.path('plugin%d.wav' % block), 'script ' + script],
                              block=block))
        t.render(t.card('render%d' % block), script, 12, t.path('render%d.wav' % block))
        _, _, a = wav(t.path('plugin%d.wav' % block))
        _, _, b = wav(t.path('render%d.wav' % block))
        n, diff, loud = shifted_equal(a, b, lat[0] - 128)
        with open(script) as f:
            stops = sum(1 for l in f if ' FC' in l)
        print('blocks of %d: latency %d, %d frames compared, %d differ, %d loop jumps' % (block, lat[0], n, diff, stops))
        check(diff == 0 and loud > 100000 and stops >= 4, 'blocks of %d: %d samples differ' % (block, diff))
        check(all(x == 0 for x in a[:2 * (lat[0] - 128)]), 'sound before the latency')


def beat_frames(rate, block, frames, tempo0, change, tempo1, loop_end):
    """The frames of the quarter notes of plughost's timeline (plug_host.cpp's Timeline), computed here and not
    by plug_clock: play from position 0 at tempo0, tempo1 from frame change on, the loop [0, loop_end) quarter
    notes; the position at each block's first frame, wrapped at the loop's end, the beat at its nearest frame"""
    out, pos, f = [], 0.0, 0
    while f < frames:
        n = min(block, frames - f)
        tempo = tempo1 if f >= change else tempo0
        fpq = rate * 60.0 / tempo
        end = pos + n / fpq
        q = math.ceil(pos - 1e-9)
        while q < end and q < loop_end:
            out.append(f + int(math.floor((q - pos) * fpq + 0.5)))
            q += 1
        if end >= loop_end:
            w = int(math.floor((loop_end - pos) * fpq + 0.5))
            if w < n:
                out.append(f + w)    # the loop's start, the jump's frame
            end = (end - loop_end) % loop_end
        pos = end
        f += n
    return sorted(set(out))


@case
def P10_plugin_onsets(t):
    # the init card's pattern (all seven voices on every quarter note) as the slave of a looped transport: each
    # onset at its beat's frame plus the latency, the beats computed here (beat_frames), through 5 loops of a bar
    # and a tempo change from 120 to 140 BPM. Its tails stay at 65-89 and its attacks rise over 2-3 frames: the
    # onsets are attacks()
    init = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'init')
    frames = 44140 * 10
    lines = ['card ' + init, 'play 0 120 0', 'loop 0 4', 'tempo 220000 140', 'out ' + t.path('beats.wav')]
    lat, _ = host(t, plan(t, 'beats.txt', lines, card=False, frames=frames))
    _, _, x = wav(t.path('beats.wav'))
    on = attacks(x[0::2])
    want = [b + lat[0] for b in beat_frames(float(RATE), 128, frames, 120.0, 220000, 140.0, 4.0) if b + lat[0] < frames - 64]
    res = [o - w for o, w in zip(on, want)]
    print('%d onsets, %d beats; onset - (beat + latency %d): %r' % (len(on), len(want), lat[0], sorted(set(res))))
    # -1..+1: the voices start on even frames, and the first sample at 1,000 of a ramp may come a frame later
    check(len(on) == len(want) and all(-1 <= r <= 1 for r in res), 'onsets %r beats %r' % (on[:12], want[:12]))


@case
def P10_plugin_latency(t):
    # isolated notes, no transport: each onset is at its note's frame plus the reported latency (minus 1 for an
    # odd frame: the voices start on even frames), for blocks of 128 and 333
    notes = [10000 + 40001 * k for k in range(10)]
    lines = []
    for m in notes:
        lines += ['note %d 0 48 127' % m, 'note %d 0 48 0' % (m + 5000)]
    for block in (128, 333):
        lat, _ = host(t, plan(t, 'plan%d.txt' % block, lines + ['out ' + t.path('lat%d.wav' % block)], block=block,
                              frames=441406))
        _, _, x = wav(t.path('lat%d.wav' % block))
        on = onsets(x[0::2])
        d = [o - m for o, m in zip(on, notes)]
        print('blocks of %d: latency %d, onset - note %r' % (block, lat[0], d))
        check(len(on) == len(notes) and all(v in (lat[0], lat[0] - 1) for v in d), 'blocks of %d: %r' % (block, d))


@case
def P10_plugin_resampled(t):
    # 48 kHz, blocks of 512: the plugin's output from plug_readOffset() on is the engine's through plug_drive for
    # the same MIDI, byte for byte; the latency is round(176 x r) + the read offset
    script = t.path('s48.txt')
    lat, _ = host(t, plan(t, 'plan48.txt', TIMELINE + ['out ' + t.path('p48.wav'), 'script ' + script], rate=48000,
                          block=512, frames=480000))
    t.drive(t.card('d48'), '48000', 512, script, 478000, t.path('d48.wav'))
    _, _, a = wav(t.path('p48.wav'))
    _, _, b = wav(t.path('d48.wav'))
    read = 512 + 128 + 3
    n, diff, loud = shifted_equal(a, b, read)
    print('48 kHz: latency %d (191 + %d), %d frames compared, %d differ' % (lat[0], read, n, diff))
    check(lat[0] == 191 + read and diff == 0 and loud > 100000, '48 kHz: latency %d, %d differ' % (lat[0], diff))


@case
def P10_plugin_state(t):
    # two instances in one host: each is the single one; its state (the card) restored by a new host plays the same
    one = t.path('one.wav')
    host(t, plan(t, 'one.txt', TIMELINE + ['out ' + one]))
    host(t, plan(t, 'two.txt', TIMELINE + ['out ' + t.path('two.wav'), 'instances 2', 'state-out ' + t.path('state.bin')]))
    restored = t.path('restored.wav')
    host(t, plan(t, 'restore.txt', TIMELINE + ['out ' + restored, 'state-in ' + t.path('state.bin')], card=False))
    for other in ('two.wav', 'two-1.wav', 'restored.wav'):
        d = same_files(one, t.path(other))
        check(d is None, '%s and one.wav %s' % (other, d))
    with open(t.path('state.bin'), 'rb') as f:
        head = f.read(8)
    print('two instances == one; the state, %d bytes (%r...), restores it' % (os.path.getsize(t.path('state.bin')), head))
    check(head == b'LCHS\x01\x00\x00\x00', 'the state header %r' % head)
    check(not os.listdir(t.path('home')), 'instance folders left: %r' % os.listdir(t.path('home')))


@case
def P10_plugin_app_card(t):
    # a new instance without a state starts on a copy of LICHOMAT Emu's card: with LICHOMAT_EMU_HOME/Card the onset
    # card, it plays as the onset card given as a state does; the plugin's own BASE/Card (groove) comes first
    emu = t.path('emu')
    ordered_copy(os.path.join(t.a.root, 'emu', 'tests', 'cards', 'onset'), os.path.join(emu, 'Card'))
    host(t, plan(t, 'state.txt', TIMELINE + ['out ' + t.path('state.wav')]))
    host(t, plan(t, 'app.txt', TIMELINE + ['out ' + t.path('app.wav')], card=False), env={'LICHOMAT_EMU_HOME': emu})
    d = same_files(t.path('state.wav'), t.path('app.wav'))
    check(d is None, 'the app card and the state %s' % d)
    host(t, plan(t, 'init.txt', TIMELINE + ['out ' + t.path('init.wav')], card=False))
    check(same_files(t.path('state.wav'), t.path('init.wav')) is not None, 'without the app card it played the onset card')
    groove = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'groove')
    host(t, plan(t, 'groove.txt', TIMELINE + ['out ' + t.path('groove.wav'), 'card ' + groove], card=False))
    ordered_copy(groove, os.path.join(t.path('home'), 'Card'))
    host(t, plan(t, 'own.txt', TIMELINE + ['out ' + t.path('own.wav')], card=False), env={'LICHOMAT_EMU_HOME': emu})
    d = same_files(t.path('groove.wav'), t.path('own.wav'))
    check(d is None, 'the plugin\'s own card and the groove card as a state %s' % d)
    print("LICHOMAT Emu's card == the same card as a state; without it, the init card; BASE/Card comes first")


@case
def P10_plugin_renew(t):
    # delete and add again: instance 0 is terminated at 4.5 s and a new one takes its place; it plays from its own
    # frame 0 (latency silence) and starts the slave afresh where the transport is
    lat, _ = host(t, plan(t, 'renew.txt', TIMELINE + ['out ' + t.path('renew.wav'), 'renew 198656 0']))
    _, _, x = wav(t.path('renew.wav'))
    left = x[0::2]
    back = next((i for i in range(198656, len(left)) if abs(left[i]) >= 4000), None)
    print('the new instance (latency %d) sounds %d frames after its start' % (lat[0], (back or 0) - 198656))
    check(back is not None and back - 198656 < 22070, 'no sound in 0.5 s after the renewal')


def longest_silence(x):
    best = run = 0
    for v in x:
        run = run + 1 if v == 0 else 0
        best = max(best, run)
    return best


@case
def P10_plugin_kill(t):
    # real time, paced, 48 kHz: the engine killed at 3 s; the plugin goes silent, starts a new engine, and plays
    # again within 1.5 s; the host is not disturbed
    lines = ['mode realtime', 'paced', 'play 4800 120 0', 'kill 144000', 'out ' + t.path('kill.wav')]
    lines += ['note %d 0 48 127' % (36000 * k + 1000) for k in range(13)]
    host(t, plan(t, 'kill.txt', lines, rate=48000, block=256, frames=480000))
    _, _, x = wav(t.path('kill.wav'))
    left = x[0::2]
    silent = longest_silence(left[144000:240000])
    back = next((i for i in range(144000 + silent, len(left)) if abs(left[i]) >= 4000), None)
    print('silent %d frames after the kill, sound again at %.2f s' % (silent, (back or 0) / 48000))
    check(silent >= 4800 and back is not None and back < 144000 + 72000, 'silent %d, back at %r' % (silent, back))


@case
def P10_plugin_rt(t):
    # the audio thread under the interposer, paced in real time for 10 s at 48 kHz in blocks of 128: no allocation,
    # lock, wait, read or sleep inside process(), one write (the wake-up) per call
    lines = ['mode realtime', 'paced', 'rtcheck', 'out ' + t.path('rt.wav')] + TIMELINE
    _, out = host(t, plan(t, 'rt.txt', lines, rate=48000, block=128, frames=480000),
                  env={'DYLD_INSERT_LIBRARIES': t.a.rtcheck})
    line = [l for l in out.splitlines() if 'rtcheck:' in l and 'process calls' in l]
    check(line, 'no rtcheck line in %r' % out)
    w = line[0].split()
    calls, bad, writes = int(w[2]), int(w[5]), int(w[8])
    check(calls == 3750 and bad == 0 and writes == calls, line[0])
```

  Append to `plugin/CMakeLists.txt`:

```cmake
# --- lichomat-plughost, the test host, and the real-time check (Task 9) ---------------------
add_executable(lichomat-plughost host/plug_host.cpp "${SDK_ROOT}/public.sdk/source/vst/hosting/module_mac.mm"
    "${SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp" "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp"
    $<TARGET_OBJECTS:plug_common> $<TARGET_OBJECTS:plug_card>)
set_source_files_properties("${SDK_ROOT}/public.sdk/source/vst/hosting/module_mac.mm" PROPERTIES COMPILE_FLAGS "-fobjc-arc")
target_include_directories(lichomat-plughost PRIVATE "${PLUGIN_DIR}/common" "${LXR_ROOT}/emu/core")
target_compile_options(lichomat-plughost PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${PLUGIN_FLAGS}> "SHELL:-ivfsoverlay ${PLUGIN_OVERLAY}")
target_compile_features(lichomat-plughost PRIVATE cxx_std_17)
target_link_libraries(lichomat-plughost PRIVATE sdk_hosting "-framework Cocoa")
add_library(plug_rtcheck SHARED tests/plug_rtcheck.c)
target_compile_options(plug_rtcheck PRIVATE ${PLUGIN_FLAGS})
set(PLUGIN_HOST_TEST ${PLUGIN_BUNDLE_TEST} --host $<TARGET_FILE:lichomat-plughost> --rtcheck $<TARGET_FILE:plug_rtcheck>)
foreach(id P10_plugin_native P10_plugin_onsets P10_plugin_latency P10_plugin_resampled P10_plugin_state P10_plugin_app_card
        P10_plugin_renew P10_plugin_kill P10_plugin_rt)
    add_test(NAME ${id} COMMAND ${PLUGIN_HOST_TEST} ${id})
    set_tests_properties(${id} PROPERTIES TIMEOUT 600 LABELS plugin)
endforeach()
set_tests_properties(P10_plugin_kill P10_plugin_rt PROPERTIES RUN_SERIAL TRUE)	# paced by the wall clock
```

- [ ] **Step 2: run it.** PLUGIN with `-R P10`. Expected: `== plugin build: 1`; the configure stops at `Cannot find source file: host/plug_host.cpp`.
- [ ] **Step 3: the test host** (P-22). `plugin/host/plug_host.cpp`:

```cpp
/*
 * plug_host.cpp
 *
 * lichomat-plughost, the plugin's test host (plan E9.2 Task 9), on the VST 3 SDK's hosting
 * classes: it loads a bundle, plays a timeline of transport and notes through one or more
 * instances in blocks, and writes each instance's output as a 16-bit stereo WAV. With script, it
 * also writes the MIDI the plugin must have given its engine, as lichomat-render's usb lines in
 * frames from the engine's epoch: the transport through the same plug_clockBlock(), the notes as
 * the processor converts them. Before frame 0 it calls process() with 0 frames every 10 ms until
 * every instance reports its engine running, so that the epoch is frame 0.
 *
 *   lichomat-plughost PLAN
 *
 * PLAN has one command a line ('#' starts a comment); frames are host frames:
 *   bundle PATH            rate R            block B            frames N         instances K
 *   card DIR               state-in FILE     state-out FILE     out FILE.wav     script FILE
 *   play FRAME TEMPO PPQ   stop FRAME        tempo FRAME TEMPO  loop START END (quarter notes)
 *   note FRAME CH PITCH VELOCITY (0-127; 0 is a Note Off)
 *   kill FRAME             SIGKILL every engine of this process
 *   renew FRAME I          terminate instance I and put a new one in its place
 *   rtcheck                count the calls of process() that the interposer (plug_rtcheck) sees
 *   mode realtime|offline  the ProcessSetup's mode (default offline: the plugin waits for its engine)
 *   paced                  realtime: each block at its wall-clock time, as an audio device would
 * Instance I > 0 writes FILE-I.wav. The summary on stdout gives each instance's latency.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <libproc.h>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "plug_archive.h"
#include "plug_clock.h"
#include "plug_time.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

struct NoteCmd
{
	uint64_t frame;
	int ch, pitch, vel;
};

struct TransportCmd
{
	uint64_t frame;
	enum { Play, Stop, Tempo } kind;
	double tempo, ppq;
};

struct Plan
{
	std::string bundle, card, stateIn, stateOut, out, script;
	double rate = 44140.625;
	int block = 128;
	uint64_t frames = 0;
	int instances = 1;
	bool loop = false, rtcheck = false, offline = true, paced = false;
	double loopStart = 0, loopEnd = 0;
	std::vector<NoteCmd> notes;
	std::vector<TransportCmd> transport;
	std::vector<uint64_t> kills;
	std::vector<std::pair<uint64_t, int>> renews;
};

[[noreturn]] void die (const std::string& what)
{
	fprintf (stderr, "lichomat-plughost: %s\n", what.c_str ());
	exit (1);
}

Plan readPlan (const char* path)
{
	std::ifstream f (path);
	if (!f)
		die (std::string ("cannot read ") + path);
	Plan p;
	std::string line;
	while (std::getline (f, line))
	{
		std::istringstream s (line.substr (0, line.find ('#')));
		std::string cmd;
		if (!(s >> cmd))
			continue;
		bool ok = true;
		if (cmd == "bundle") ok = !!(s >> p.bundle);
		else if (cmd == "rate") ok = !!(s >> p.rate);
		else if (cmd == "block") ok = !!(s >> p.block);
		else if (cmd == "frames") ok = !!(s >> p.frames);
		else if (cmd == "instances") ok = !!(s >> p.instances);
		else if (cmd == "card") ok = !!(s >> p.card);
		else if (cmd == "state-in") ok = !!(s >> p.stateIn);
		else if (cmd == "state-out") ok = !!(s >> p.stateOut);
		else if (cmd == "out") ok = !!(s >> p.out);
		else if (cmd == "script") ok = !!(s >> p.script);
		else if (cmd == "rtcheck") p.rtcheck = true;
		else if (cmd == "paced") p.paced = true;
		else if (cmd == "mode") { std::string m; ok = !!(s >> m) && (m == "realtime" || m == "offline"); p.offline = m == "offline"; }
		else if (cmd == "loop") ok = !!(s >> p.loopStart >> p.loopEnd), p.loop = true;
		else if (cmd == "note") { NoteCmd n; ok = !!(s >> n.frame >> n.ch >> n.pitch >> n.vel); p.notes.push_back (n); }
		else if (cmd == "play") { TransportCmd t {0, TransportCmd::Play, 0, 0}; ok = !!(s >> t.frame >> t.tempo >> t.ppq); p.transport.push_back (t); }
		else if (cmd == "stop") { TransportCmd t {0, TransportCmd::Stop, 0, 0}; ok = !!(s >> t.frame); p.transport.push_back (t); }
		else if (cmd == "tempo") { TransportCmd t {0, TransportCmd::Tempo, 0, 0}; ok = !!(s >> t.frame >> t.tempo); p.transport.push_back (t); }
		else if (cmd == "kill") { uint64_t k; ok = !!(s >> k); p.kills.push_back (k); }
		else if (cmd == "renew") { uint64_t k; int i; ok = !!(s >> k >> i); p.renews.push_back ({k, i}); }
		else ok = false;
		if (!ok)
			die ("plan: cannot read the line: " + line);
	}
	if (p.bundle.empty () || p.out.empty () || !p.frames || p.block < 1 || p.instances < 1)
		die ("plan: bundle, out, frames, block and instances are needed");
	return p;
}

/* the host's transport, as Live gives it: the position at a block's first frame, wrapped at the
 * loop's end, the tempo of the block's first frame */
struct Timeline
{
	const Plan& p;
	bool playing = false;
	double tempo = 120.0, pos = 0.0;
	size_t next = 0;

	explicit Timeline (const Plan& plan) : p (plan) {}

	/* the context of the block at frame, then the position advanced over frames n */
	plug_transport_t block (uint64_t frame, uint32_t n)
	{
		while (next < p.transport.size () && p.transport[next].frame <= frame)
		{
			const TransportCmd& t = p.transport[next++];
			if (t.kind == TransportCmd::Play) { playing = true; tempo = t.tempo; pos = t.ppq; }
			else if (t.kind == TransportCmd::Stop) playing = false;
			else tempo = t.tempo;
		}
		plug_transport_t t {1, playing ? 1 : 0, tempo, pos, p.loop ? 1 : 0, p.loopStart, p.loopEnd};
		if (playing)
		{
			pos += (double)n * tempo / (60.0 * p.rate);
			if (p.loop && p.loopEnd > p.loopStart && t.ppq < p.loopEnd && pos >= p.loopEnd)
				pos = p.loopStart + std::fmod (pos - p.loopEnd, p.loopEnd - p.loopStart);
		}
		return t;
	}
};

struct Instance
{
	IPtr<PlugProvider> provider;
	IPtr<IComponent> component;
	FUnknownPtr<IAudioProcessor> processor;
	HostProcessData data;
	EventList events {512};
	ParameterChanges outParams {8};
	double engine = 0.0;	/* the Engine parameter: 2/3 running */
	std::vector<float> l, r;
	uint32_t latency = 0;
};

int memPut (void* ctx, const void* p, size_t n)
{
	int32 w = 0;
	return static_cast<IBStream*> (ctx)->write (const_cast<void*> (p), (int32)n, &w) == kResultOk && w == (int32)n ? 0 : -1;
}

void readEngineParam (Instance& in)
{
	for (int32 i = 0; i < in.outParams.getParameterCount (); i++)
	{
		IParamValueQueue* q = in.outParams.getParameterData (i);
		ParamValue v;
		int32 off;
		if (q && q->getParameterId () == 0 && q->getPointCount () > 0 && q->getPoint (q->getPointCount () - 1, off, v) == kResultOk)
			in.engine = v;
	}
}

std::vector<uint8_t> readFile (const std::string& path)
{
	std::ifstream f (path, std::ios::binary);
	if (!f)
		die ("cannot read " + path);
	return std::vector<uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
}

/* a component at the plan's rate and block, with its state, active and processing */
void open (Instance& in, const VST3::Hosting::PluginFactory& factory, const VST3::Hosting::ClassInfo& ci, const Plan& p)
{
	in.provider = owned (new PlugProvider (factory, ci, true));
	if (!in.provider->initialize ())
		die ("the plugin did not initialize");
	in.component = in.provider->getComponentPtr ();
	in.processor = FUnknownPtr<IAudioProcessor> (in.component);
	ProcessSetup setup {p.offline ? kOffline : kRealtime, kSample32, p.block, p.rate};
	if (in.processor->setupProcessing (setup) != kResultOk)
		die ("setupProcessing refused");
	in.component->activateBus (kAudio, kOutput, 0, true);
	in.component->activateBus (kEvent, kInput, 0, true);
	std::vector<uint8_t> state;
	if (!p.stateIn.empty ())
		state = readFile (p.stateIn);
	else if (!p.card.empty ())
	{
		IPtr<MemoryStream> m = owned (new MemoryStream);
		char err[512];
		if (plug_archiveWrite (p.card.c_str (), memPut, m.get (), err, sizeof err) != 0)
			die (err);
		state.assign (m->getData (), m->getData () + m->getSize ());
	}
	if (!state.empty ())
	{
		IPtr<MemoryStream> m = owned (new MemoryStream (state.data (), (TSize)state.size ()));
		if (in.component->setState (m) != kResultOk)
			die ("setState refused the card");
	}
	if (in.component->setActive (true) != kResultOk)
		die ("setActive refused");
	in.latency = in.processor->getLatencySamples ();
	in.processor->setProcessing (true);
	in.data.prepare (*in.component, p.block, kSample32);
	in.data.inputEvents = &in.events;
	in.data.outputParameterChanges = &in.outParams;
	in.engine = 0.0;
}

void close (Instance& in)
{
	in.processor->setProcessing (false);
	in.component->setActive (false);
	in.data.unprepare ();
	in.processor = nullptr;
	in.component = nullptr;
	in.provider = nullptr;
}

/* process() with 0 frames every 10 ms until the instance reports its engine running */
void waitRunning (Instance& in, ProcessContext& ctx)
{
	const auto t0 = std::chrono::steady_clock::now ();
	while (in.engine < 0.6)
	{
		in.outParams.clearQueue ();
		in.events.clear ();
		in.data.numSamples = 0;
		in.data.processContext = &ctx;
		in.processor->process (in.data);
		readEngineParam (in);
		if (std::chrono::steady_clock::now () - t0 > std::chrono::seconds (60))
			die ("the engine did not run within 60 s");
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
}

void put16 (FILE* f, unsigned v)
{
	fputc ((int)(v & 255u), f);
	fputc ((int)((v >> 8) & 255u), f);
}

void put32 (FILE* f, uint32_t v)
{
	put16 (f, v & 0xFFFFu);
	put16 (f, v >> 16);
}

void writeWav (const std::string& path, const Instance& in, double rate)
{
	FILE* f = fopen (path.c_str (), "wb");
	if (!f)
		die ("cannot write " + path);
	const bool native = plug_isNative (rate);
	const uint32_t frames = (uint32_t)in.l.size (), data = frames * 4u, hz = native ? 44141u : (uint32_t)rate;
	fwrite ("RIFF", 1, 4, f);
	put32 (f, 36u + data);
	fwrite ("WAVEfmt ", 1, 8, f);
	put32 (f, 16);
	put16 (f, 1);
	put16 (f, 2);
	put32 (f, hz);
	put32 (f, hz * 4u);
	put16 (f, 4);
	put16 (f, 16);
	fwrite ("data", 1, 4, f);
	put32 (f, data);
	for (uint32_t i = 0; i < frames; i++)
		for (float x : {in.l[i], in.r[i]})
		{
			/* native: floor, lichomat-render's (L1 + L2) >> 1 exactly; else the nearest */
			double v = native ? std::floor ((double)x * 32768.0) : std::floor ((double)x * 32768.0 + 0.5);
			v = v > 32767.0 ? 32767.0 : v < -32768.0 ? -32768.0 : v;
			put16 (f, (uint16_t)(int16_t)v);
		}
	fclose (f);
}

void killEngines ()
{
	pid_t kids[64];
	memset (kids, 0, sizeof kids);
	(void)proc_listchildpids (getpid (), kids, sizeof kids);
	for (pid_t k : kids)
		if (k > 0)
			kill (k, SIGKILL);
}

} // namespace

int main (int argc, char** argv)
{
	if (argc != 2)
		die ("usage: lichomat-plughost PLAN");
	const Plan p = readPlan (argv[1]);
	std::string err;
	auto module = VST3::Hosting::Module::create (p.bundle, err);
	if (!module)
		die (err);
	IPtr<HostApplication> app = owned (new HostApplication);
	PluginContextFactory::instance ().setPluginContext (app);
	const auto& factory = module->getFactory ();
	VST3::Hosting::ClassInfo cls;
	bool found = false;
	for (auto& ci : factory.classInfos ())
		if (ci.category () == kVstAudioEffectClass)
		{
			cls = ci;
			found = true;
		}
	if (!found)
		die ("no audio module class in " + p.bundle);
	using RtFn = void (*) (int);
	using RtCount = unsigned long long (*) (void);
	RtFn rtInside = p.rtcheck ? (RtFn)dlsym (RTLD_DEFAULT, "plug_rtInside") : nullptr;
	RtCount rtViolations = p.rtcheck ? (RtCount)dlsym (RTLD_DEFAULT, "plug_rtViolations") : nullptr;
	RtCount rtWrites = p.rtcheck ? (RtCount)dlsym (RTLD_DEFAULT, "plug_rtWrites") : nullptr;
	if (p.rtcheck && (!rtInside || !rtViolations || !rtWrites))
		die ("rtcheck: run with DYLD_INSERT_LIBRARIES=libplug_rtcheck.dylib");
	unsigned long long probe = 0;
	if (rtInside)
	{
		/* the check's own check: an allocation and a free inside count 2 */
		rtInside (1);
		void* volatile q = malloc (16);
		free (q);
		rtInside (0);
		probe = rtViolations ();
		if (probe != 2)
			die ("rtcheck: the probe counted " + std::to_string (probe) + " calls, not 2");
	}
	std::vector<std::unique_ptr<Instance>> inst;
	for (int i = 0; i < p.instances; i++)
	{
		inst.push_back (std::make_unique<Instance> ());
		open (*inst.back (), factory, cls, p);
	}
	ProcessContext ctx {};
	ctx.sampleRate = p.rate;
	for (auto& in : inst)
		waitRunning (*in, ctx);
	/* the timeline */
	Timeline tl (p);
	plug_clock_t expect;
	plug_clockInit (&expect, p.rate);
	std::map<uint64_t, std::vector<std::string>> script;
	size_t nextNote = 0, nextKill = 0, nextRenew = 0;
	unsigned long long processCalls = 0;
	const auto wall0 = std::chrono::steady_clock::now ();
	for (uint64_t f = 0; f < p.frames; f += (uint64_t)p.block)
	{
		if (p.paced)
			std::this_thread::sleep_until (wall0 + std::chrono::duration<double> ((double)f / p.rate));
		const uint32_t n = (uint32_t)std::min<uint64_t> ((uint64_t)p.block, p.frames - f);
		while (nextKill < p.kills.size () && p.kills[nextKill] <= f)
		{
			killEngines ();
			nextKill++;
		}
		while (nextRenew < p.renews.size () && p.renews[nextRenew].first <= f)
		{
			Instance& in = *inst[(size_t)p.renews[nextRenew].second];
			close (in);
			open (in, factory, cls, p);
			nextRenew++;
		}
		const plug_transport_t t = tl.block (f, n);
		ctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
		if (t.playing)
			ctx.state |= ProcessContext::kPlaying;
		if (t.cycle)
			ctx.state |= ProcessContext::kCycleActive | ProcessContext::kCycleValid;
		ctx.tempo = t.tempo;
		ctx.projectTimeMusic = t.ppq;
		ctx.cycleStartMusic = t.cycleStart;
		ctx.cycleEndMusic = t.cycleEnd;
		ctx.projectTimeSamples = (TSamples)f;
		/* the expected MIDI: the same generator, the same note conversion (plugin_processor.cpp) */
		plug_midi_t m[1024];
		const unsigned nm = plug_clockBlock (&expect, &t, n, m, 1024);
		for (unsigned i = 0; i < nm; i++)
		{
			char b[16];
			snprintf (b, sizeof b, m[i].len == 3 ? "%02X %02X %02X" : "%02X", m[i].b[0], m[i].b[1], m[i].b[2]);
			script[f + m[i].offset].push_back (b);
		}
		for (auto& in : inst)
			in->events.clear ();
		while (nextNote < p.notes.size () && p.notes[nextNote].frame < f + n)
		{
			const NoteCmd& c = p.notes[nextNote++];
			Event e {};
			e.sampleOffset = (int32)(c.frame - f);
			if (c.vel > 0)
			{
				e.type = Event::kNoteOnEvent;
				e.noteOn = {(int16)c.ch, (int16)c.pitch, 0.0f, (float)c.vel / 127.0f, 0, -1};
			}
			else
			{
				e.type = Event::kNoteOffEvent;
				e.noteOff = {(int16)c.ch, (int16)c.pitch, 0.0f, -1, 0.0f};
			}
			for (auto& in : inst)
				in->events.addEvent (e);
			const long v = lrintf ((float)c.vel / 127.0f * 127.0f);
			char b[16];
			snprintf (b, sizeof b, "%02X %02X %02X", (unsigned)((v ? 0x90 : 0x80) | (c.ch & 15)), (unsigned)(c.pitch & 0x7F),
			          (unsigned)v);
			script[c.frame].push_back (b);
		}
		for (auto& in : inst)
		{
			in->outParams.clearQueue ();
			in->data.numSamples = (int32)n;
			in->data.processContext = &ctx;
			if (rtInside)
				rtInside (1);
			in->processor->process (in->data);
			if (rtInside)
				rtInside (0);
			processCalls++;
			readEngineParam (*in);
			const float* l = in->data.outputs[0].channelBuffers32[0];
			const float* r = in->data.outputs[0].channelBuffers32[1];
			in->l.insert (in->l.end (), l, l + n);
			in->r.insert (in->r.end (), r, r + n);
		}
	}
	if (!p.stateOut.empty ())
	{
		IPtr<MemoryStream> m = owned (new MemoryStream);
		if (inst[0]->component->getState (m) != kResultOk)
			die ("getState failed");
		std::ofstream o (p.stateOut, std::ios::binary);
		o.write (m->getData (), (std::streamsize)m->getSize ());
	}
	for (size_t i = 0; i < inst.size (); i++)
	{
		writeWav (i == 0 ? p.out : p.out.substr (0, p.out.size () - 4) + "-" + std::to_string (i) + ".wav", *inst[i], p.rate);
		printf ("lichomat-plughost: instance %zu: latency %u, %zu frames\n", i, inst[i]->latency, inst[i]->l.size ());
		close (*inst[i]);
	}
	if (!p.script.empty ())
	{
		FILE* s = fopen (p.script.c_str (), "w");
		for (auto& [frame, msgs] : script)
		{
			fprintf (s, "%lluf usb", (unsigned long long)frame);
			for (auto& b : msgs)
				fprintf (s, " %s", b.c_str ());
			fputc ('\n', s);
		}
		fclose (s);
	}
	if (rtViolations)
		printf ("lichomat-plughost: rtcheck: %llu process calls, %llu forbidden calls, %llu writes\n", processCalls,
		        rtViolations () - probe, rtWrites ());
	PluginContextFactory::instance ().setPluginContext (nullptr);
	return 0;
}
```

- [ ] **Step 4: the real-time check** (P-23). `plugin/tests/plug_rtcheck.c`:

```c
/*
 * plug_rtcheck.c
 *
 * libplug_rtcheck.dylib, the real-time check of the plugin's audio thread (plan E9.2 Task 9):
 * loaded into lichomat-plughost with DYLD_INSERT_LIBRARIES, it interposes the calls an audio
 * thread must not make (allocation, locks, waits, file and pipe reads, sleeps) and counts those
 * made while the host has marked its thread as inside process() (plug_rtInside()). write() is
 * counted apart: the plugin makes one, non-blocking, to wake its engine. Each forbidden function
 * that was called is printed at exit.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <dispatch/dispatch.h>
#include <os/lock.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

/* the marked thread; no thread-local variable: their first use allocates, which recurses here */
static pthread_t marked;
static int inside;
static unsigned long long violations, writes;
static unsigned long long counts[16];
static const char *const names[16] = { "malloc", "calloc", "realloc", "free", "pthread_mutex_lock", "pthread_cond_wait",
	"pthread_cond_timedwait", "os_unfair_lock_lock", "sem_wait", "nanosleep", "usleep", "read", "poll", "select",
	"dispatch_sync", "pthread_rwlock_rdlock" };

static int marking(void)
{
	return __atomic_load_n(&inside, __ATOMIC_ACQUIRE) && pthread_equal(pthread_self(), marked);
}

static void hit(int k)
{
	if (marking())
	{
		__atomic_fetch_add(&violations, 1u, __ATOMIC_RELAXED);
		__atomic_fetch_add(&counts[k], 1u, __ATOMIC_RELAXED);
	}
}

__attribute__((visibility("default"))) void plug_rtInside(int on)
{
	marked = pthread_self();
	__atomic_store_n(&inside, on, __ATOMIC_RELEASE);
}

__attribute__((visibility("default"))) unsigned long long plug_rtViolations(void)
{
	return __atomic_load_n(&violations, __ATOMIC_RELAXED);
}

__attribute__((visibility("default"))) unsigned long long plug_rtWrites(void)
{
	return __atomic_load_n(&writes, __ATOMIC_RELAXED);
}

__attribute__((destructor)) static void report(void)
{
	for (int k = 0; k < 16; k++)
		if (counts[k])
			fprintf(stderr, "plug_rtcheck: %s %llu times inside process()\n", names[k], counts[k]);
}

static void *r_malloc(size_t n) { hit(0); return malloc(n); }
static void *r_calloc(size_t a, size_t b) { hit(1); return calloc(a, b); }
static void *r_realloc(void *p, size_t n) { hit(2); return realloc(p, n); }
static void r_free(void *p) { if (p) hit(3); free(p); }
static int r_mutex_lock(pthread_mutex_t *m) { hit(4); return pthread_mutex_lock(m); }
static int r_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { hit(5); return pthread_cond_wait(c, m); }
static int r_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t) { hit(6); return pthread_cond_timedwait(c, m, t); }
static void r_unfair_lock(os_unfair_lock_t l) { hit(7); os_unfair_lock_lock(l); }
static int r_sem_wait(sem_t *s) { hit(8); return sem_wait(s); }
static int r_nanosleep(const struct timespec *a, struct timespec *b) { hit(9); return nanosleep(a, b); }
static int r_usleep(useconds_t u) { hit(10); return usleep(u); }
static ssize_t r_read(int fd, void *p, size_t n) { hit(11); return read(fd, p, n); }
static int r_poll(struct pollfd *f, nfds_t n, int t) { hit(12); return poll(f, n, t); }
static int r_select(int n, fd_set *a, fd_set *b, fd_set *c, struct timeval *t) { hit(13); return select(n, a, b, c, t); }
static void r_dispatch_sync(dispatch_queue_t q, dispatch_block_t b) { hit(14); dispatch_sync(q, b); }
static int r_rdlock(pthread_rwlock_t *l) { hit(15); return pthread_rwlock_rdlock(l); }
static ssize_t r_write(int fd, const void *p, size_t n)
{
	if (marking())
		__atomic_fetch_add(&writes, 1u, __ATOMIC_RELAXED);
	return write(fd, p, n);
}

#define INTERPOSE(rep, orig) \
	__attribute__((used)) static const struct { const void *r; const void *o; } interpose_##rep \
	__attribute__((section("__DATA,__interpose"))) = { (const void *)&rep, (const void *)&orig }

INTERPOSE(r_malloc, malloc);
INTERPOSE(r_calloc, calloc);
INTERPOSE(r_realloc, realloc);
INTERPOSE(r_free, free);
INTERPOSE(r_mutex_lock, pthread_mutex_lock);
INTERPOSE(r_cond_wait, pthread_cond_wait);
INTERPOSE(r_cond_timedwait, pthread_cond_timedwait);
INTERPOSE(r_unfair_lock, os_unfair_lock_lock);
INTERPOSE(r_sem_wait, sem_wait);
INTERPOSE(r_nanosleep, nanosleep);
INTERPOSE(r_usleep, usleep);
INTERPOSE(r_read, read);
INTERPOSE(r_poll, poll);
INTERPOSE(r_select, select);
INTERPOSE(r_dispatch_sync, dispatch_sync);
INTERPOSE(r_rdlock, pthread_rwlock_rdlock);
INTERPOSE(r_write, write);
```

- [ ] **Step 5: pass.** PLUGIN with `-R P10`, then `ctest --test-dir build/plugin -R P10 -V | grep -E 'blocks of|onsets,|onset - note|48 kHz|two instances|LICHOMAT Emu|new instance|silent|rtcheck:'`.
  Expected: `100% tests passed, 0 tests failed out of 10` (with Task 8's validator); `blocks of 128: latency 385, 529471 frames compared, 0 differ, 5 loop jumps` and `blocks of 100: latency 357, 529499 frames compared, 0 differ, 5 loop jumps`; `22 onsets, 22 beats; onset - (beat + latency 385): [-1, 0, 1]`; `onset - note [385, 384, …]` and `[590, 589, …]`; `48 kHz: latency 834 (191 + 643), 478000 frames compared, 0 differ`; `two instances == one; the state, 4166 bytes …`; the start-card line; `the new instance (latency 385) sounds 4713 frames after its start`; `silent N frames after the kill, sound again at T s` (N ≥ 4,800, T < 4.5); `rtcheck: 3750 process calls, 0 forbidden calls, 3750 writes`.
- [ ] **Step 6: the gate.** PLUGIN-CHECK (T 48). **Step 7: commit** `plugin Task 9 (E9.19): the test host on the SDK's hosting classes, the real-time interposer, the bundle's scenarios` with `plugin/host/plug_host.cpp plugin/tests/plug_rtcheck.c plugin/tests/plugin_test.py plugin/CMakeLists.txt`; record the measured lines; push. **Review: full** (spec, then quality), mutants among them: `notesOf()`'s Note On of velocity 0 as a Note On, the clock reset on a new pid removed, the read offset's lag at 0, the offline wait removed, a `usleep(1)` or a `std::vector` in `process()` (`P10_plugin_rt` must fail). The wake pipe's `O_NONBLOCK` has no test (a full pipe would make the write block; the engine drains it at every wait): the reviewer reads it.

---

### Task 10: The licence gate, README, install, the final gates — E9.20

**Files:** Create `plugin/tests/plugin_licence_gate.py`, `plugin/tools/install_plugin.sh`, `plugin/README.md`; modify `plugin/tests/plugin_test.py`, `plugin/CMakeLists.txt` (append).

- [ ] **Step 1: the failing test.** In `plugin/tests/plugin_test.py`, after Task 9's section and before `def main():`:

```python
# --- the licence gate and the name scan (Task 10) ----------------------------------------------

@case
def P11_licence(t):
    binaries = [os.path.join(t.a.bundle, 'Contents', 'MacOS', 'LICHOMAT'), t.a.engine, t.a.host, t.a.drive]
    out = t.run([sys.executable, os.path.join(t.a.root, 'plugin', 'tests', 'plugin_licence_gate.py'), t.a.root, t.a.build]
                + binaries).stdout
    print(out.strip())
```

  Append to `plugin/CMakeLists.txt`:

```cmake
# --- the licence gate and the name scan (Task 10) ---------------------------------------------
add_test(NAME P11_licence COMMAND ${PLUGIN_HOST_TEST} P11_licence)
set_tests_properties(P11_licence PROPERTIES TIMEOUT 600 LABELS plugin)
```

  Run PLUGIN with `-R P11`. Expected: `0% tests passed`, and `FAIL P11_licence: python3… exited 2` (`plugin_licence_gate.py` is missing).
- [ ] **Step 2: the gate** (P-5). `plugin/tests/plugin_licence_gate.py`:

```python
#!/usr/bin/env python3
"""P11: the licence gate and the name scan of the plugin's build (plan E9.2 Task 10), the
emulator's M21 (emu/tests/licence_gate.py) with two additions:

- the vendored VST 3 SDK, plugin/third_party/vst3sdk/, is allowed (MIT; its LICENSE.txt goes into
  the bundle's Contents/Resources);
- the SDK's own compile commands need not pass the main.h overlay: they include no firmware
  header. Every other command of the build must.

    plugin_licence_gate.py <repository root> <build dir> <binary>...

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import importlib.util
import json
import os
import sys

SDK = os.path.join('plugin', 'third_party', 'vst3sdk') + os.sep


def gate(root):
    spec = importlib.util.spec_from_file_location('licence_gate', os.path.join(root, 'emu', 'tests', 'licence_gate.py'))
    g = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(g)
    g.ALLOWED.append((SDK, 'Steinberg VST 3 SDK, MIT'))
    return g


def check_overlay(root, build, errors):
    """licence_gate.check_overlay() less the SDK's commands"""
    overlay = os.path.join(build, 'main_overlay.yaml')
    sdk = os.path.realpath(os.path.join(root, SDK))
    with open(os.path.join(build, 'compile_commands.json')) as f:
        commands = json.load(f)
    checked = 0
    for c in commands:
        path = os.path.realpath(c['file'])
        if os.sep + 'emu' + os.sep + 'libm' + os.sep in c['file'] or path.startswith(sdk + os.sep):
            continue
        checked += 1
        if '-ivfsoverlay ' + overlay not in c['command']:
            errors.append('%s is compiled without the main.h overlay' % c['file'])
    return checked


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    root, build, binaries = sys.argv[1], sys.argv[2], sys.argv[3:]
    g = gate(root)
    errors = []
    files = g.check_files(root, build, errors)
    commands = check_overlay(root, build, errors)
    g.check_names(root, build, binaries, errors)
    for e in errors:
        print('licence gate: ' + e)
    if errors:
        print('licence gate: %d errors' % len(errors))
        sys.exit(1)
    print('licence gate: %d files, %d compile commands and %d binaries pass' % (files, commands, len(binaries)))


if __name__ == '__main__':
    main()
```

  Run PLUGIN with `-R P11` and `ctest --test-dir build/plugin -R P11 -V | grep 'licence gate:'`. Expected: `100% tests passed, 0 tests failed out of 1`; `licence gate: 504 files, 126 compile commands and 4 binaries pass` (Debug and Release).
- [ ] **Step 3: the install** (P-18). `plugin/tools/install_plugin.sh`:

```bash
#!/bin/bash
# Build the LICHOMAT plugin in Release and install it for the current user (plan E9.2 Task 10):
# ~/Library/Audio/Plug-Ins/VST3/LICHOMAT.vst3, signed ad hoc (the build signs the engine and the
# bundle), checked with codesign and the SDK's validator. It replaces an earlier LICHOMAT.vst3
# there and touches nothing else, except that it gives the plugin a start card when it has none:
# ~/Library/Application Support/LICHOMAT Plugin/Card, the init fixture (four on the floor on all
# seven voices, emu/tests/cards/init), which every new instance copies (plan P-18).
#
#   plugin/tools/install_plugin.sh
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tools/env.sh" >/dev/null
B="$ROOT/build/plugin-release"
cmake -S "$ROOT/plugin" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release > "$B.install.log" 2>&1
ninja -C "$B" LICHOMAT validator >> "$B.install.log" 2>&1
SRC="$B/VST3/Release/LICHOMAT.vst3"
DST_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
DST="$DST_DIR/LICHOMAT.vst3"
mkdir -p "$DST_DIR"
if [ -e "$DST" ]; then
  rm -rf -- "$DST"
fi
ditto "$SRC" "$DST"
codesign --verify --deep --strict "$DST"
echo "install_plugin: $DST, $(codesign -dv "$DST" 2>&1 | grep '^Signature=')"
CARD="$HOME/Library/Application Support/LICHOMAT Plugin/Card"
if [ ! -e "$CARD" ]; then
  mkdir -p "$(dirname "$CARD")"
  ditto "$ROOT/emu/tests/cards/init" "$CARD"
  echo "install_plugin: the start card $CARD (emu/tests/cards/init)"
fi
HOME_TMP="$(mktemp -d)"
LICHOMAT_PLUGIN_HOME="$HOME_TMP" LICHOMAT_EMU_HOME="$HOME_TMP/no-emu" "$B/bin/Release/validator" "$DST" | grep '^Result:'
rm -rf -- "$HOME_TMP"
```

  and `plugin/README.md`:

````markdown
<!--
Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
-->

# LICHOMAT plugin (prototype)

A VST3 instrument for macOS on Apple Silicon that plays the LICHOMAT firmware, the same code as
on the device, through the emulator's virtual device (`emu/`). Unofficial; not affiliated with
the maker of the hardware.

- **One engine process per instance.** The plugin (`LICHOMAT.vst3`) is a thin layer in the
  host's process; `Contents/Helpers/lichomat-engine` runs the firmware. They share memory (an
  event ring and an audio ring) and two pipes: one wakes the engine each block, the other's end
  tells the plugin that the engine is gone. A dead or hung engine is started again after 0.5 s;
  the plugin is silent meanwhile.
- **Sync** is MIDI clock with Song Position, made from the host's transport at the exact
  frames: Start at position 0, Song Position and Continue elsewhere, Stop, Song Position and
  Continue at a jump (a loop). The firmware plays as a MIDI-clock slave.
- **Notes** from the host go to the firmware as USB MIDI at their frames.
- **Output:** one stereo bus, the sum of the device's out 1 and out 2 at -6 dB.
- **Rate:** the firmware runs at 44,140.625 Hz; the engine resamples to the host's rate with
  Apple's AudioConverter. At exactly 44,140.625 Hz (the test host) nothing is resampled and the
  output is `lichomat-render --stereo sum`'s, bit for bit.
- **Latency** reported to the host: 128 frames of the firmware's voice queue (176 resampled,
  with the converter), plus the host's largest block, a margin of 128 frames and 1-6 frames of
  rounding. At 48 kHz and 128 frames: 450 frames, 9.4 ms.
- **State:** the instance's card folder as one archive (`LCHS`) in the host's set. A new
  instance starts on a copy of LICHOMAT Emu's card if there is one, else on the init card.
- **Offline** (bounce, freeze, export) the plugin waits for its engine.

## Build and install

```bash
source tools/env.sh
plugin/tools/plugin_check.sh         # Debug with UBSan and Release, every test
plugin/tools/install_plugin.sh       # Release into ~/Library/Audio/Plug-Ins/VST3, signed ad hoc
```

Folders: each instance works in `~/Library/Application Support/LICHOMAT Plugin/<id>/` (`card/`,
`engine.log`, `plugin.log`); `LICHOMAT_PLUGIN_HOME` moves them (the tests do). The margin can be
set with `LICHOMAT_PLUGIN_MARGIN` (16-4096 host frames).

## Layout

| Path | What |
|---|---|
| `common/` | C shared by the plugin, the engine and the tests: the shared memory, MIDI clock from a transport, time and latency, the card archive, starting the engine |
| `engine/` | `lichomat-engine`: the renderer's prelude, the tier hook that feeds the events, the resampler |
| `vst3/` | the VST3 processor, controller, factory, the engine link and the instance folders |
| `host/` | `lichomat-plughost`, the test host on the SDK's hosting classes |
| `tests/` | the unit tests (P3-P8), `plugin_test.py` (P6, P7, P10, P11), `plug_drive`, the real-time check |
| `third_party/vst3sdk/` | the VST 3 SDK 3.8.1 (build 84), the part the plugin builds with, pinned by `SHA256SUMS` |

## Licence and source

GPL-3.0-only with the terms in `NOTICE.md`, as the rest of LICHOMAT. Third-party code: the VST 3
SDK (MIT, Steinberg Media Technologies GmbH; its licence is in the bundle's
`Contents/Resources/VST3-SDK-LICENSE.txt`), and through the emulator Arm Optimized Routines
(MIT) and FatFs `ff.h` (ChaN's licence). VST is a trademark of Steinberg Media Technologies GmbH;
the plugin uses no VST logo.
````

- [ ] **Step 4: the final gates.** PLUGIN-CHECK (T 49, both builds), EMU-CHECK (346), CHECK (as in Task 1), and `git diff a856883 --stat -- . ':!plugin'` shows `emu/CMakeLists.txt | 6 ++++++` and nothing else.
- [ ] **Step 5: install.** First try it with a scratch `HOME` (nothing of the owner's is touched), then for real:

```bash
cd /Users/tk/Code/lxr/build/wt-plugin && chmod +x plugin/tools/install_plugin.sh && T=$(mktemp -d) && HOME=$T plugin/tools/install_plugin.sh && ls "$T/Library/Audio/Plug-Ins/VST3" && rm -rf -- "$T"
cd /Users/tk/Code/lxr/build/wt-plugin && plugin/tools/install_plugin.sh && ls ~/Library/Audio/Plug-Ins/VST3 "$HOME/Library/Application Support/LICHOMAT Plugin"
```

  Expected (both): `install_plugin: …/LICHOMAT.vst3, Signature=adhoc`, `install_plugin: the start card …/LICHOMAT Plugin/Card (emu/tests/cards/init)` (only when it was missing), `Result: 47 tests passed, 0 tests failed`; then `LICHOMAT.vst3` and `Card`. If `~/Library/Audio/Plug-Ins/VST3` held other plugins, they are still there.
- [ ] **Step 6: commit** `plugin Task 10 (E9.20): the licence gate over the plugin, the install with an ad hoc signature, README; P11` with `plugin/tests/plugin_licence_gate.py plugin/tools/install_plugin.sh plugin/README.md plugin/tests/plugin_test.py plugin/CMakeLists.txt`; record the gates' lines and the bundle's SHA-256 (`shasum -a 256 build/plugin-release/VST3/Release/LICHOMAT.vst3/Contents/MacOS/LICHOMAT`); push. **Review: light**, with a grep of every new string for the makers' names and a check of every number in README against the tests' output.

---

### Task 11: The owner's checks, handoff, tracker (timing-core) — E9.21

**Files:** Create `docs/lichomat/plugin-checks.md` (Polish); modify `docs/lichomat/HANDOFF.md`, `docs/lichomat/tracker.json` (on `timing-core`, in `/Users/tk/Code/lxr`).

- [ ] **Step 1: the checks.** `docs/lichomat/plugin-checks.md` with the GPL header and the section "Twoje sprawdzenia w Abletonie (P1–P8)" below, word for word, filled with the plugin branch's head and Task 10's install date.
- [ ] **Step 2: handoff and tracker.** `docs/lichomat/HANDOFF.md`: a row for the plugin prototype (branch `plugin` and its head, 49 plugin ids, the latency table's 48 kHz rows, the open owner checks P1–P8, the install path). `python3 tools/tracker_note.py "E9.2 (prototyp wtyczki) zrobione: …"`, row E9.2 done with the head, and the owner's check as open; `python3 tools/tracker_export.py` and the upload of every file it reports.
- [ ] **Step 3: commit** on `timing-core`: `Docs E9.2: the plugin prototype's owner checks P1-P8, handoff, tracker` with those files by name. **Review: light**, every number against Tasks 6–10's recorded output.

---

## Twoje sprawdzenia w Abletonie (P1–P8)

Dla właściciela, po polsku, ok. 20 minut. Przed nimi: wtyczka zainstalowana (zadanie 10) w `~/Library/Audio/Plug-Ins/VST3/LICHOMAT.vst3` z kartą startową `~/Library/Application Support/LICHOMAT Plugin/Card` (karta „init”: na wzorcu 0 wszystkie siedem głosów gra na 1, 5, 9 i 13, czyli na każdą ćwierćnutę). Live 12, w Ustawieniach → Audio 48 kHz i bufor 128 albo 256 próbek; w menu Options włączone „Delay Compensation”. Każdy punkt: co zrobić, co powinno się stać, co zapisać.

| # | Co zrobić | Co powinno się stać | Zapisz |
|---|---|---|---|
| **P1** (2 min) | Ustawienia → Plug-Ins: „Use VST3 Plug-In System Folders” włączone, potem „Rescan”. W przeglądarce: Plug-Ins → VST3 → LICHOMAT. | Jest „LICHOMAT” od „LICHOMAT”. Live nie zgłasza błędu. | Czy jest; komunikat Live'a, jeśli był. |
| **P2** (2 min) | Przeciągnij LICHOMAT na nową ścieżkę MIDI. Otwórz Monitor aktywności i wpisz `lichomat`. | Urządzenie bez własnego okna (prototyp go nie ma). W ciągu sekundy jest proces `lichomat-engine`. | Czy się załadowało; czy proces jest. |
| **P3** (3 min) | Klip MIDI, 1 takt, nuta **C2** (MIDI 48) na każdą ósemkę „i” (2., 4., 6., 8. ósemka), kanał 1. Uruchom klip. | Między ćwierćnutami sekwencera karty słychać uderzenia z klipu: wszystkie głosy karty init naraz (każdy słucha nuty 48 na kanale 1). Równo z metronomem. | Czy gra; czy równo. |
| **P4** (4 min) | Transport od 1.1.1 przy 120 BPM. Potem w trakcie tempo 140. Potem zatrzymaj i uruchom od taktu 3 (kliknięcie w linijkę). Potem pętla 1 takt w aranżacji, kilka przejść. | Sekwencer karty gra na ćwierćnuty razem z metronomem; po zmianie tempa po kilku uderzeniach idzie za nowym; od taktu 3 gra od razu we właściwym miejscu; pętla wraca na początek taktu. Przy przeskoku pętli może zginąć uderzenie przesunięte wcześniej niż „raz” (znane: przeskok to stop, pozycja, continue). | Co słychać przy każdej z czterech rzeczy; czy coś się rozjeżdża w czasie. |
| **P5** (2 min) | Druga ścieżka MIDI z drugim LICHOMAT. | Dwa procesy `lichomat-engine`; obie ścieżki grają, każda swoją kopią karty. | Czy obie grają. |
| **P6** (3 min) | Zapisz set, zamknij Live, otwórz set. | Obie ścieżki grają jak przed zapisem (karta jest w secie). | Czy grają; ile trwało otwarcie. |
| **P7** (3 min) | Na ścieżce audio z wejściem „Resampling” nagraj 2 takty; potem na ścieżce LICHOMAT „Freeze” i odsłuch. | Nagrane uderzenia leżą na siatce (opóźnienie wtyczki skompensowane); zamrożona ścieżka gra to samo (eksport czeka na silnik). | Czy na siatce; czy freeze gra. |
| **P8** (2 min) | Usuń urządzenie, cofnij (Cmd+Z); potem dodaj nowe. Opcjonalnie: w Monitorze aktywności „Wymuś zakończenie” procesu `lichomat-engine`. | Wraca i gra; nowe gra. Po zabiciu silnika ok. 1 s ciszy, potem znów gra; Live działa dalej. | Co się stało. Jeśli trzeszczało: plik `plugin.log` z najnowszego folderu w `~/Library/Application Support/LICHOMAT Plugin/`. |

Jeśli w P3–P4 nic nie słychać: plik `engine.log` z tego samego folderu. Własną kartę wtyczka bierze z `~/Library/Application Support/LICHOMAT Plugin/Card` (każda nowa instancja ją kopiuje); bez niej z karty LICHOMAT Emu, a bez obu startuje na karcie init bez wzorców.

## Draft coverage

| Draft / brief | Where |
|---|---|
| §2.1 VST3 first, Apple Silicon only | Tasks 2, 8 (arm64, macOS 13); AU and the rest in E9.4 |
| §2.2 an engine process per instance, shared memory, death detection, restart with silence | Tasks 3 (rings, `P3_shm_killed`), 6 (`lichomat-engine`), 8 (`EngineLink`), 9 (`P10_plugin_kill`); P-6, P-7, P-16 |
| §2.3 reported latency, ≤ 10 ms above 2.9 ms | Task 5 (`P5_time_latency`), Task 9 (`P10_plugin_latency`, `P10_plugin_resampled`); "Latency"; deviation 2 |
| §2.4 sync through MIDI clock and Song Position at exact times, no firmware change | Task 4 (`P4_*`), Task 6 (USB at the frame's cycle), Task 9 (`P10_plugin_native`: loops, a tempo change, bit for bit; `P10_plugin_onsets`: the beats on the host's grid); P-8, P-13 |
| §2.5 resampling with AudioConverter; a mode without it for a bit-exact comparison | Task 6 (`P7_*`, `P6_engine_native`), Task 9 (`P10_plugin_native`, `P10_plugin_resampled`); P-9, P-11 |
| §2.6 stereo sum | P-10; Task 6 (`eng_sink()`) |
| §2.7 notes with their sample offsets | Task 8 (`notesOf()`), Task 9 (`P10_plugin_latency`); P-20; Program Change: deviation 8 |
| §2.8 a card folder per instance, the state as one block, restored into a fresh folder, unpaced boot | Tasks 6 (seeding), 7 (`P8_*`), 8 (`CardFolder`, `getState`/`setState`), 9 (`P10_plugin_state`, `P10_plugin_app_card`); P-17, P-18 |
| §2.9 no window | Task 8 (no editor; one read-only parameter, P-19) |
| §2.10 GPL, NOTICE.md's terms, no maker's names, no VST logo, M21 covers the plugin | Tasks 2 (logo left out), 10 (`P11_licence`); P-3, P-5, P-21 |
| §2.11 ad hoc signature locally | Task 8 (the build signs), Task 10 (`install_plugin.sh`) |
| §3 the validator, a test host, bit for bit, onsets through loops and tempo changes, robustness, real time | Tasks 8 (`P10_plugin_validator`), 9 (`P10_plugin_native`, `P10_plugin_onsets` with the beats computed outside `plug_clock`, `P10_plugin_latency`, `P10_plugin_kill`, `P10_plugin_state`, `P10_plugin_renew`, `P10_plugin_rt`), 6 (`P7_engine_realtime`: four engines) |
| §4 E9.2's owner check (≈ 20 min) | Task 11, P1–P8 |
| The brief: branch, worktree, `plugin/` with its own CMake, the SDK's delivery, licence gate, tests first, install, check list | Tasks 1–11; P-1–P-5 |

## Risks

- **Live itself is untested here.** Only the SDK's validator and our host call the plugin. Live may call `process()` in ways neither does (blocks of varying size, `kPrefetch`, `setState()` while processing, a sample-rate change while active); P1–P8 are where that shows. The plugin handles a varying block up to the maximum, and `setState()` while active restarts the engine.
- **The "Engine" parameter may not appear in Live's device view** (read-only parameters are often hidden); P2 uses Activity Monitor instead.
- **Scheduling under load.** The engine is a normal process with a time-constraint thread in real time; a busy Mac can starve it, which the margin covers up to a point (the rule under "Latency"). One planning run under a heavy load had 282 underrun frames before P-15.
- **Loop jumps are Stop, Song Position, Continue** (draft §2.4): hits before the downbeat at the loop's start (negative microtiming) are lost at each jump, Song Position has 16th resolution, positions past bar 1,024 are clamped. E9.3's host mode replaces this.
- **Ad hoc signing.** A locally built bundle is not quarantined and loads; a downloaded one would need Developer ID and notarisation (draft §2.11), the owner's decision.
- **State size and timing.** A seeded init card is 780 KB; every set stores that per instance, and `getState()` reads about 200 files on the main thread (tens of ms). A firmware autosave during `getState()` restarts the archive (3 times, then the save fails for that instance).
- **The SDK's CMake on the Command Line Tools** depends on the `XCODE_VERSION` stand-in (F17); an SDK update must be checked again (`vendor_vst3sdk.sh` pins the commits; `P2_sdk_pin` the files).
- **The emulator's guard** changes `emu/CMakeLists.txt` on the plugin branch; a merge with v0.7 touches the same file at other lines (tests at its end).

## Changes during execution

(Filled in during execution: measured values that differ from this plan, review findings and their fixes, tracker notes.)
