# LICHOMAT plugin E9.4: two stereo buses and automatable parameters — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build draft §7's two parts on branch `plugin-v07`: the device's two stereo outputs as the buses "Out 1" and "Out 2" of `LICHOMAT.vst3` (Out 1 the sum of both while the host has not activated Out 2), and 36 automatable parameters that reach the firmware as MIDI at their frames, with the firmware's own changes of them (a kit or a pattern loaded) reported back to the host without a loop and the values kept in the set.

**Architecture:** The engine writes the device's two outputs as four channels into a version-2 shared memory (resampled four at a time) and, after each block, a table of the firmware's values of the parameters, read from its memory by `eng_fw.c`, the engine's one file with firmware headers. The processor plays Out 1 and Out 2 from the four channels, or their sum on Out 1. A pure C module, `plug_params.c`, holds the 36 parameters (VST3 id, CC, NRPN or Program Change, range), turns the host's changes into MIDI on the firmware's global channel, which the processor pushes into the prototype's event ring at their frames, and compares the engine's table with the host's values: a value the firmware takes by itself goes to the host as an output parameter change, the host's values go to every new engine, and the values follow the card in the state.

**Tech Stack:** as the prototype: C11 and C++17 with Apple clang 21 (Command Line Tools 26.6, no Xcode), CMake 3.31, Ninja, ctest, the vendored VST 3 SDK 3.8.1 build 84 (MIT), AudioToolbox, POSIX shared memory and pipes, Python 3, bash.

| | |
|---|---|
| Date | 2026-11-06 |
| Draft | `docs/superpowers/specs/2026-11-05-plugin-draft.md` (Polish), §7 (E9.4 without AU); the brief of 2026-11-06 |
| Base | `plugin-v07` at `728dfff` (the prototype E9.2 with firmware v0.7, E9.6), worktree `build/wt-plugin-v07`, pushed as `origin/plugin-v07`. Its gates: 72 plugin ids in Debug with UBSan and in Release, the SDK's validator 47/0, CHECK 135 host ids, EMU-CHECK 353 ids, 41 goldens |
| The prototype | `docs/superpowers/plans/2026-11-05-plugin-prototype.md`: its decisions P-1–P-24 and every fix recorded in its "Changes during execution" hold. This plan extends P-7 and P-11 (B-2, B-3: four channels), P-10 (B-3), P-17 (B-9), P-19 (B-10, B-11), P-20 (B-5, B-17) and P-22 (B-14); nothing else |
| Tasks | 11; Task N is tracker row **E9.(30+N)** (E9.31–E9.41) under the feature row E9.4; decisions B-1–B-21, facts F1–F18, measured M1–M15 |
| Execution (lean) | one implementer per group: **A** Tasks 1–4, **B** 5–6, **C** 7–9, **D** 10–11. Full review (spec, then quality) after Task 6 (the shared memory and the engine, Tasks 2–6) and after Task 9 (the buses, the parameters and the state in the plugin, Tasks 7–9); light review after Task 4 and after Tasks 10–11 |
| Tests | 92 plugin ctest ids at the end: the prototype's 72 and 20 new ones (label `plugin`); the emulator's 353 and the host tests' 135 unchanged |

**Everything in this plan was compiled and run while planning.** First as one prototype in a scratch copy of `728dfff` (`git archive 728dfff | tar -x`, made a local git repository so that `P6_engine_version` finds its commit, the toolchain link as in the worktree): PLUGIN-CHECK 91 of 91 in Debug with UBSan and in Release, 0 warnings of our files. Then the code blocks below were replayed task by task from this plan's text into a second fresh copy (each task's failing step, its tests, then the full Debug ctest), and PLUGIN-CHECK ran there after Task 10; "Measured while planning" gives the numbers. The load average was 2.7–4.3 with no other heavy job. What could not be checked here is Ableton Live: the owner's checks P9–P10 at the end.

## Decisions taken while planning

| # | Decision | Why |
|---|---|---|
| B-1 | **Branch `plugin-v07`, worktree `build/wt-plugin-v07`; only `plugin/` changes** (and, in Task 11 on `timing-core`, the owner's checks, HANDOFF and the tracker). No firmware, emulator or renderer file changes: the firmware already takes everything through MIDI (F2–F6), and the engine reads its memory from a file of the plugin's own (B-6). | The brief; the draft's header ("Nie zmienia"). CHECK and EMU-CHECK at Tasks 1 and 10 prove it. |
| B-2 | **The shared memory is version 2:** the audio ring holds four channels, L1 R1 L2 R2, each the device's int16 sample / 32,768, and a value table follows the counters (B-6). An engine and a plugin of different versions refuse each other: the engine checks the header (`plug_shmCheck()`: "version 1 size …, this engine needs version 2 size …") and maps nothing smaller than its layout (`plug_shmMap()`), exits 2, and the plugin logs that and, after 5 such deaths in a minute, gives up ("failed"). | Both come from one bundle (`Contents/Helpers`), so a mismatch means a hand-copied engine; it must fail loudly, not play garbage. `P3_shm_header` and `P3_shm_create` check both ways. 590,040 bytes instead of 327,824 (M2). |
| B-3 | **The sum is computed by the reader**, `plug_sum(a, b) = (a + b) × 0.5`: for the device's samples it is exactly the prototype's (L1 + L2) / 65,536 (P-10), so the native output without Out 2 is still `lichomat-render --stereo sum`'s bit for bit; the engine converts the four channels at once, and each pair comes out exactly as the two-channel converter gives it (M3). | The engine cannot know whether the host activated Out 2 (that is the processor's), and one ring for both cases keeps the engine as it is. Exactness: a and b are multiples of 2^-15 below 1, their sum needs 17 bits of a float's 24 (`P3_shm_split` checks 65,000 pairs). |
| B-4 | **Two buses: "Out 1" (main, active by default) and "Out 2" (auxiliary, inactive by default).** Out 1 and Out 2 are the device's outputs when the host has activated Out 2 (`activateBus()`, kept in an atomic) and gives its buffers; else Out 1 is the sum and Out 2, if the host gives buffers anyway, is written silent with its silence flags. `setBusArrangements()` takes two stereo buses only. The latency is unchanged (one ring, one read offset). | Draft §7.1. The SDK's host gives buffers for every bus, active or not (F14), so the processor must write them. |
| B-5 | **The parameters reach the firmware as MIDI through the prototype's event ring, on the firmware's global channel:** a CC, an NRPN (CC 99 = number >> 7, CC 98 = number & 127, CC 6 = value, one USB transfer at one frame) or, for the pattern, a Program Change, at the frame of the host's point. Not a direct call. | The firmware takes these parameters from MIDI as from an external controller (F2–F6) at their cycle, like the notes (P-8); a direct call could only run at a pass boundary and would need the engine to run pass by pass. The renderer gets the same MIDI from a script, so the plugin's output with automation is checked bit for bit (`P10_plugin_params`). The global channel is accepted by every one of them, voice CCs included (F3), and is the only one a Program Change is taken on (F5). |
| B-6 | **The engine reads the firmware's values from its memory in the tier hook** (`eng_fw.c`, its one file with firmware headers, an object library with `emu_fw_headers`' flags, as `render_fw.c`): `activeParameters[par]` for each parameter, `seq_pendingPattern` for the pattern, `midi_MidiChannels[7]` (the global channel) and the receive filter's bits. It writes them with the host frame the firmware has reached (`epoch + plug_framesCovering(now - t0)`) under a sequence count (a seqlock) at t0, before it reports running, and after every block request. `eng_fwCheck()` compares the plugin's table with the firmware's `PAR_` names when the engine starts (exit 2, state failed). | The hook may read but never call the firmware (F12); the renderer reads its counters from the hook the same way. Reading costs nothing in the firmware's timing, so every bit-exact test holds. The front panel's `activeParameters` is what the device shows and saves: a CC reaches it through the front parser, an NRPN directly (F4). The check turns a renumbered `Parameters.h` into a refusal instead of wrong values. |
| B-7 | **The step of the host's values and the firmware's (`plug_params.c`, pure functions; the processor and the test host run the same code).** Per parameter: the host's value (`want`), whether the host has it (`shown`), the value last given to the engine (`sent`) and its frame. (1) A host point whose value differs from `sent` goes to the engine; one that equals it does not (a host's echo of a reported value sends nothing). (2) The first report of a new engine (from its epoch) gives it every host value that differs from the firmware's; parameters the host has no value for take the firmware's. (3) Any later report that differs from `want` is the firmware's own change, except for a parameter the host gave a value less than *settle* (a quarter of a second) before the report's frame: then `want` and `sent` become the firmware's value and the host gets it, once, as an output parameter change at the block's first frame. | Draft §7.2's report without a loop: nothing the host set is reported back (it is in `want`), nothing reported is sent again (it is in `sent`), and a value in flight is not reported back as the old one. A CC reaches the table 16–65 frames after its frame (M6), a kit load takes tens of milliseconds: a quarter of a second covers both. `P12_*` check each rule; `P10_plugin_params` checks with a host that echoes every report. |
| B-8 | **The report reads the next pattern, not the playing one** (`seq_pendingPattern`): a Program Change is shown at once, and played at once when stopped, else at the end of the playing pattern, with its kit when the pattern mode is "auto" (F5). | Otherwise the host's pattern would be reported back as the old one for up to a pattern's length, longer than any settle time. |
| B-9 | **The values follow the card in the state:** after the prototype's `LCHS` archive (P-17), an `LCHV` block: version 1, the count, each value's VST3 id (u32) and value (u8), little-endian; all 36 values the host has. `setState()` restores them (the host gets them at the first block, a new engine those that differ); a state without the block (the prototype's, 0.1.0) gives no values; an `LCHV` block of another version or cut short is refused; ids this version does not know are left out. | A knob turned in Live and the set saved must come back: the card holds the kit as the firmware last saved it, not the edits since. Old sets keep opening. The controller's `setComponentState()` stays empty: the processor reports the values once its engine runs. |
| B-10 | **The 36 parameters** (table under "The parameters"): pattern; per voice volume, pan, filter and decay (the hi-hat has two decays, closed and open: both); the DJ filter; the reverb's level and decay; the FX amount; per voice the output. Shuffle and the reverb's size are left out. Ids grouped by tens (1, 10–15, 20–25, 30–35, 40–46, 50–53, 60–65), never renumbered; 0 stays the read-only "Engine". | Draft §7.2 with the firmware's real parameters (F2–F11); deviations 1–5 give the reasons. |
| B-11 | **The controller's parameters are the firmware's integers:** `stepCount` = max, the default the init kit's value (M5), shown as the device shows them: pan −63..64, the pattern 1–64, a voice's output by its name (St1, St2, L1, R1, L2, R2, FX); `kCanAutomate`, and `kIsList` for the pattern and the outputs. | Live then shows what the device's display shows, and a host's normalized value maps to one integer both ways (`P12_params_table` checks every value of every parameter). |
| B-12 | **Order at a frame: the transport, then the parameters, then the notes**; a parameter's MIDI goes into the ring only while the engine runs (as notes, P-20). A parameter flush (`process()` with 0 frames) takes the host's changes at the next block's first frame. | A note at the frame of a change plays with the new value; a Program Change at a downbeat comes after the Start of that frame. VST3 hosts may deliver parameter changes between blocks. |
| B-13 | **The monitor thread logs, once per engine, the channel and the filter it finds** ("parameters: MIDI channel 1, receive filter: CC on, Program Change on", or why the host cannot set them). | The plugin has no window; a card with another global channel or a filter that drops CCs is then in the log the owner sends (P8's log). |
| B-14 | **The test host:** `bus2` (Out 2 activated, `FILE-out2.wav`), `param FRAME ID VALUE`, `channel CH` (for its expected MIDI; default 0, the cards'), `echo` (every reported value back as a host change at the next block), and a line for every block with reports. Its expected script (P-22) gets the parameters' MIDI from its own `plug_params_t`, fed the plugin's reports, through the same `pointsOf()` and `plug_paramsHost()`. Its parameter queues hold 64 parameters, so that no queue is allocated inside `process()`. | One tool for the bit-exact comparison with automation, the reports, the echo and the real-time check; the queues of the SDK's `ParameterChanges` grow with `new` beyond their first size (F14). |
| B-15 | **Real time:** `process()` adds relaxed atomic loads and stores, the seqlock copy (58 bytes, one try a block), the pure functions of `plug_params.c` and the host's own queues. No allocation, lock or system call: `P10_plugin_rtparams` runs P10_plugin_rt's 10 s with three parameters changed every block and the pattern every second: 0 forbidden calls of the 45 (M14). | The prototype's rule for the audio thread (its "Real-time safety"). |
| B-16 | **Version 0.2.0** (`PLUG_VERSION`, the project's version, `Info.plist`); the class ids and the bundle's identity stay those of `plugin`. | The bus layout and the parameters changed; a host may cache a plugin's description by its version. |
| B-17 | **No IMidiMapping:** CCs and Program Changes in Live's clips still do not reach the firmware (P-20); the automatable parameters do. | Keeps the scope; a clip's CC or Program Change could later map to these same parameters (README, "Known limits"). |
| B-18 | **New test groups:** `P12_params_*` (the parameters' unit tests, the free number after P11), the rest in the prototype's groups (P3, P6, P7, P10). No new id extends an existing one (the `seqc_test.h` filter matches `<name>_…`); `P10_plugin_rtparams` and `P10_plugin_params_*` match the regexes of `P10_plugin_rt` and `P10_plugin_params` in ctest, which only means `-R` runs both. | The prototype's rule. |
| B-19 | **The prototype's tests stay as they are, but for five changes, each named in its task:** the P3 cases take the four-channel API (same values checked), `P3_shm_header` checks a version-1 header instead of a version-2 one, `P7_converter_*` pass the channel count, `P6_engine_objects` counts `eng_fw.c`'s object as the engine's own, and `P6_engine_version` expects 0.2.0. | The brief: every existing test passing as before. Their checks are unchanged in substance; each change is forced by an interface. |
| B-20 | **`plug_drive` gets `split` (the four channels, as `lichomat-render` without `--stereo`) and `values FILE`** (each change of the engine's table, the driver then keeping the engine within two blocks and 128 frames of what it published so that it sees each report). | The engine's two outputs and its report are checked without the VST3 layer, against the renderer, as P6 and P7 do. |
| B-21 | **Task 11 works on `timing-core` in `/Users/tk/Code/lxr`**, as the prototype's Task 11: the owner's checks P9–P10 appended to `docs/lichomat/plugin-checks.md` (Polish), HANDOFF, the tracker. | The checks document lives with the docs; the plugin branch keeps code only. |

## Draft deviations and refinements

1. **Shuffle is not a parameter.** The firmware sets it only from its menu: `menu_parseGlobalParam(PAR_SHUFFLE)` sends `SEQ_SHUFFLE` to the sequencer over its internal panel link (F6); no CC or NRPN reaches `seq_setShuffle()` (NRPN 133 only writes `activeParameters[261]`, which nothing reads back). A direct call would run at a pass boundary, not at its frame, and need the engine to run pass by pass. It can come with the panel (E9.5) or a firmware NRPN in a later version.
2. **"Delay" is the FX amount** (NRPN 108, 0–100): the kit's FX bus holds one effect (off, distortion, ring modulator, compressor, delay; F8), and its amount is the delay's level when the kit's FX is the delay. The parameter is named "FX Amount".
3. **The reverb is its level (NRPN 98) and its decay (NRPN 116).** Its size (and tone, pre-delay, modulation, ducking) is in the kit's block (parameters 302–308), which the firmware takes only from its menu: the front parser refuses them from NRPN (F7).
4. **The hi-hat is one voice with two decays**: both are parameters (closed CC 61, open CC 62), so seven decays for six voices.
5. **Six output parameters more** (a voice's output, NRPN 87–92): 36 parameters, not about 30. Without the panel (E9.5) they are the only way to send a voice to Out 2 in Live, which the owner's check P9 needs: the plugin's start card (`init`) plays every voice on out 1.
6. **The pattern's value is the next pattern** (B-8), and a Program Change switches as the firmware does: at once when stopped, else at the end of the playing pattern, with the pattern's kit in the cards' "auto" pattern mode.
7. **The values settle**: a value the host set is not reported back, and a firmware change of a parameter the host set less than a quarter of a second before reaches the host only after that time (B-7). Draft §7.2 ("Live pokazuje to, co gra") holds after that.
8. **The values are in the set** (B-9): draft §7.3 says "stan w secie" unchanged; with automatable parameters the state must hold them, or a knob turned and saved would come back as the card's.
9. **The global channel is the firmware's**, read from its memory (B-6). On the five test cards and the plugin's start card it is 0, channel 1 (M5): their `glo.cfg` has the global channel 0 ("all"), which the menu's send never stores, so `midi_MidiChannels[7]` keeps its zero (F11).
10. **Out 1 carries the sum until the host activates Out 2** as the draft says; whether Live activates Out 2 only when a track takes its audio from it is the owner's check P9 (Risks).

## Facts found while planning

Read at `728dfff` (worktree `build/wt-plugin-v07`); line numbers are of that tree.

- **F1 The two outputs.** The audio sink gets each block as out 1 (`dma_buffer2`: L1, R1) and out 2 (`dma_buffer`: L2, R2), 64 interleaved int16 frames each (`emu/core/emu_core.h:70-78`); the renderer writes `--stereo out1`, `out2`, `sum` (`(out1 + out2) >> 1`) or, without `--stereo`, four channels L1 R1 L2 R2 with a plain PCM header (`emu/render/render_out.c:44-59`, `:142-175`).
- **F2 CC and NRPN in the sound engine.** `midiParser_ccHandler()`: a CC n sets parameter n − 1 (`paramNr = msg.data1-1`, `Core/Src/Soundengine/MIDI/MidiParser.c:168-174`); CC 99 and 98 set the NRPN number's high and low 7 bits, CC 6 hands the value on as a `MIDI_CC2` message with the number's low byte (`:138-145`, `:196-209`), whose parameter is that byte + 128 (`:913-920`). The CC numbers are `MidiMessages.h:82-234` (VOL1 89, PAN1 95, FILTER_FREQ_DRUM1 38, VELOD1 51, …), the NRPN numbers `:238-381` (CC2_AUDIO_OUT1 87, CC2_DJ_FILTER 96, CC2_REV_LEVEL 98, CC2_FX_AMOUNT 108, CC2_REV_DECAY 116).
- **F3 Which CC is taken.** A CC of a voice (`midiParser_cc2voiceNr()`, `MidiParser.c:1579-…`) is taken on the voice's channel or the global channel, any other (CC 6, 98, 99 among them) on any channel in `midi_MidiChannels[0..7]`, both only with the receive filter's `MIDI_FILTER_CC` (`:1892-1934`); each taken CC is also forwarded to the front parser.
- **F4 The front's values.** The front parser sets `activeParameters[cc − 1]` for every forwarded CC (`Core/Src/frontPanelParser.c:111-149`) and, for an NRPN's value, `activeParameters[nrpn + 128]` below `END_OF_GLOBALS` (`:47-60`); `activeParameters[NUM_PARAMS]` is the edit buffer the menu shows and saves (`Core/Inc/menu/menu.h:664`).
- **F5 Program Change.** Taken in `seq_drainSyncQueues()` with `MIDI_FILTER_PROG_C` on the global channel, in its place among Start, Continue and Stop (`Core/Src/Soundengine/Sequencer/sequencer.c:2441-2451`); `seq_setNextPattern()` takes 0–63 (`MAX_PATTERN` 64), sets `seq_pendingPattern`, and while stopped switches at once and rewinds (`:411-423`); `seq_setPendingPatternActive()` loads the pattern's kit when `PAR_PATTERN_MODE` is 1 (`:589-641`), which the cards' `glo.cfg` has.
- **F6 Shuffle.** `PAR_SHUFFLE` (261) is a menu parameter: `menu_parseGlobalParam()` sends `SEQ_SHUFFLE` (`Core/Src/menu/menu.c:3759-3761`), which the cortex parser turns into `seq_setShuffle()` (`Core/Src/Soundengine/MIDI/CortexParser.c:872-874`); no CC or NRPN case sets it.
- **F7 The kit block.** The front parser takes an NRPN's value only below `END_OF_GLOBALS` (302): the reverb's size, tone, pre-delay, modulation, ducking and the delay's sync (302–308) only from the menu (`frontPanelParser.c:55-60`).
- **F8 The FX bus.** One effect at a time: `FX_OFF`, `FX_DISTORTION`, `FX_RING`, `FX_COMPRESSOR`, `FX_DELAY` (`Core/Inc/Soundengine/FxBus.h:28-37`); NRPN 108 sets `fxBus.dryWet = value / 100` (`MidiParser.c:1399-1401`), a `DTYPE_0B100` parameter (`menu.c:526`).
- **F9 The ranges and displays.** Volumes, filters, decays, the DJ filter and the reverb are `DTYPE_0B127`; the pans `DTYPE_PM63`, shown as value − 63 (`menu.c:1720-1723`); a pattern is shown as its number + 1 (`menu.c:1138`).
- **F10 The outputs.** A voice's output is one of St1, St2, L1, R1, L2, R2, FX (`Core/Inc/menu/MenuText.h:394-404`); NRPN 87–92 set `mixer_audioRouting[0..5]` (`MidiParser.c:1320-1327`).
- **F11 The global channel.** `midi_MidiChannels[7]` (`MidiParser.h:56`), set from `PAR_MIDI_CHAN_GLOBAL` by `menu_parseGlobalParam()` as `(7 << 4) | (value − 1)` (`menu.c:3726-3736`); for the value 0 that byte is 0xFF, a status byte to the front parser (`frontPanelParser.c:631-648`), so nothing is stored and the array's zero stays: channel 1.
- **F12 The hook may read.** "It never calls the firmware, emu_runUntil() or emu_directCall()" (`emu_core.h:27-35`); the renderer reads its counters from the hook (`emu/render/render_fw.h:96-101`, `rfw_counters()`).
- **F13 The firmware's parameter numbers.** `Core/Inc/Parameters.h`: `PAR_VOL1` 88, `PAR_PAN1` 94, `PAR_PAN4` 99, `PAR_FILTER_FREQ_1` 37, `PAR_VELOD1` 50, `PAR_VELOD6_CLOSED` 60, `PAR_VELOD6_OPEN` 61, `PAR_AUDIO_OUT1` 215, `PAR_DJ_FILTER` 224, `PAR_REV_LEVEL` 226, `PAR_FX_DRY_WET` 236, `PAR_REV_DECAY` 244, `PAR_SHUFFLE` 261, `END_OF_GLOBALS` 302, `NUM_PARAMS` 314 (compiled and printed while planning; the comments in the file are older).
- **F14 The SDK's host.** `HostProcessData::prepare()` gives every bus its buffers, active or not (`plugin/third_party/vst3sdk/public.sdk/source/vst/hosting/processdata.cpp:90-140`); `ParameterChanges` reuses the queues it was made with and makes a new one with `new` beyond them (`hosting/parameterchanges.cpp:114-180`), each queue reserving 5 points (`:26-32`).
- **F15 A kit load.** `preset_loadDrumsetRam()` copies the kit into `activeParameters` (`preset_copyKitToActive()`), which keeps the DJ filter and the reverb's freeze (`Core/Src/Preset/presetManager.c:1127-1157`).
- **F16 The engine's links.** `P6_engine_objects` requires every object the engine links that is not its own to be one of `lichomat-render`'s (`plugin/tests/plugin_test.py:310-322`); its own are those of `lichomat-engine` and `plug_common_san`.
- **F17 The renderer's direct events** (`param`, saves) run at the first pass boundary at or after their time (`emu/render/render_main.c:630-676`, `:1065-1083`): the only way a menu-only parameter such as shuffle could be set (deviation 1).
- **F18 Counts at the base.** 72 plugin ids (Debug and Release), 135 host ids, 353 emulator ids, 41 goldens (E9.6); `~/Library/Audio/Plug-Ins` holds `Components Digidesign VST` and no `VST3`; `~/Library/Application Support/LICHOMAT Plugin` does not exist.

## Measured while planning

In `/private/tmp/claude-501/-Users-tk-Code-lxr/96b76bdc-1e78-46dc-a0e8-44d89ec424a5/scratchpad/e94-plan/` on this Mac: `repo/` the prototype, `repo2/` the replay of this plan (`plansrc/` holds the scripts that replayed it). Debug with UBSan unless Release is named.

- **M1 The base.** PLUGIN-CHECK at `728dfff`: 72 of 72 in both builds, 0 warnings, 3 min 57 s. In a `git archive` copy `P6_engine_version` fails (`Built from commit unknown`) until the copy is a git repository; in the worktree it passes.
- **M2 The shared memory.** `sizeof(plug_shm_t)` 327,824 bytes in version 1, 590,040 in version 2.
- **M3 The converter.** Four channels at once against each pair apart: 0 of 25,576 / 27,838 / 55,676 frames differ at 44.1 / 48 / 96 kHz (`P7_converter_channels`, 400 blocks of device samples); at 48 kHz the engine's out 1 and out 2 against the renderer's `--stereo out1` and `out2` through `plug_resample_wav`: 0 LSB over 287,800 frames (`P7_engine_split`: 8,579 and 57,758 samples at 4,000 or more).
- **M4 The engine's two outputs.** The onset card's render in four channels: samples at 4,000 or more per channel 7,854 / 0 / 26,853 / 26,506 (Drum 1 hard left on out 1, the others on out 2); `plug_drive split` equals it byte for byte, 264,896 frames, blocks of 128 and 37 (`P6_engine_split`).
- **M5 The firmware's values at boot** (`plug_drive values`, plug_params order): `init` and `song`: pattern 0, volumes 127 ×6, pans 63 ×6, filters 127 127 127 127 68 104, decays 21 13 3 10 107 5 17, DJ 64, reverb level 0, reverb decay 64, FX 0, outputs 0 ×6. `onset`: Drum 1's pan 0 and decay 0, outputs 0 1 1 1 1 1. `groove`: Drum 1 and 2's pans and decays 0, outputs 0 1 1 1 1 1. `fx`: FX 64, outputs 6 6 6 6 0 0. Every card: global channel 0, receive filter 15. A card copied with `cp -R` (not in the card's order) boots another kit: the cases copy in order (`ordered_copy`).
- **M6 The report's delay** (`P6_engine_params`): Drum 1 Volume (CC 89) sent at 30,000 seen from 30,016; Drum 1 Filter (CC 38) at 52,000 from 52,032; DJ Filter (NRPN 96) at 60,000 from 60,032 (Release 60,097: `plug_drive` reads the table once a block); Reverb Level (NRPN 98) at 90,000 from 90,048; the pattern (Program Change 1) at 200,000 from 200,064. The engine with the parameters' MIDI equals the renderer with the same script, byte for byte, and differs from the script without them.
- **M7 A Program Change while playing.** The init card as the slave of a USB clock at 120 BPM, Program Change 1 at bar 3's downbeat (frame 176,562): the next pattern is 1 from frame 176,576 in the engine's report, and pattern 1 (Drum 1 alone on the quarter notes) plays from bar 4 (6.0 s: the samples at 4,000 or more per second go from 5,907 to 6,236 and stay there), the end of the bar-long pattern that played; while stopped (`P10_plugin_params`, the onset card) the pattern and its kit load at once.
- **M8 The prototype's outputs, unchanged** (Tasks 3–9): `P6_engine_native` 617,984 frames, 56 onsets in L, == render; `P7_engine_resampled` 1 LSB at 44.1, 48, 96 kHz; `P10_plugin_native` 0 differ (blocks of 128 and 100); `P10_plugin_resampled` 0 differ over 478,000 frames, latency 834; the validator `Result: 47 tests passed, 0 tests failed` with two buses and 37 parameters; `P10_plugin_rt` 3,750 calls, 0 forbidden, 3,750 writes.
- **M9 The buses.** `P10_plugin_buses` (onset, 44,140.625 Hz): Out 1 == `--stereo out1` and Out 2 == `--stereo out2`, 0 differ over 529,471 frames (blocks of 128, latency 385; 23,368 and 758,654 samples not 0) and 529,499 (blocks of 100, latency 357); `P10_plugin_buses48`: 0 differ over 478,000 frames against `plug_drive split`. `P10_plugin_outputs` (init, Drum 1 Output = St2 at 100,000): Out 1 and Out 2 == the renderer's, 0 differ over 264,639 frames, Out 2 0 samples before frame 100,000 + 257 (the mixer takes the change at once, 128 frames before a trigger would sound).
- **M10 The parameters.** `P10_plugin_params`: 0 differ over 264,639 frames against the renderer playing the host's script, which holds 38 CC messages (2 NRPNs) and 1 Program Change; the reports: frame 0 every one of the 36; Drum 1's volume and filter (127, the kit's) at frame 100,352 in Debug, 100,224 or 100,480 in Release runs; the reverb level (0) at 101,376, or 101,248, once its settle time after the host's 90,000 (11,035 frames) is over; each once. The block a report reaches the host in depends on how far the offline engine has run when the plugin reads its table, so the case bounds the frames (1,024) and checks the values exactly; with `echo` the same output and the same values. The instance log: `parameters: MIDI channel 1, receive filter: CC on, Program Change on`, once per engine.
- **M11 A wrong table.** Drum 1 Volume's CC changed to 90 by hand: `lichomat-engine: the parameter Drum 1 Volume (id 10) is firmware parameter 89, PAR_VOL1 is 88`, state failed, `plug_drive` exit 2.
- **M12 The state.** 4,358 bytes: the card's 4,166 and the `LCHV` block's 192 (12 + 36 × 5); restored, the instance plays as one given the values at frame 0, byte for byte, and reports them at frame 0; a version-2 block is refused (`setState refused`).
- **M13 A new engine.** `P10_plugin_params_restart` (48 kHz, paced, killed at 3 s): every volume 0 from frame 0: 0 samples at 1,000 or more before the kill and 0 after; without the parameters 77,659 after; 2 engines per run.
- **M14 Real time with automation.** `P10_plugin_rtparams`: 3,750 calls, 0 forbidden, 3,750 writes, with 11,250 parameter points and 10 pattern changes.
- **M15 The gate.** PLUGIN-CHECK in `repo2` after the replay of Tasks 2-10 from this plan's text: Debug with UBSan 92 of 92, Release 92 of 92, 0 warnings of our files in both, 4 min 43 s (load 3.7-4.4); no engine or host left; `repo2` and `repo` hold the same files. In the replay every task's failing step failed as its Step 2 says, and every task's full Debug ctest passed: 73, 75, 77, 84, 85, 87, 91, 92, 92. An earlier replay found that Task 8 without a state set resetting the values broke `P10_plugin_setstate` (the new card got the old card's values); the reset is in Task 8 since.

## The parameters

The ids are VST3 parameter ids: a host keys a set's automation and the plugin's values by them, so they never change; a new parameter takes a new id and a removed one's id is not used again. `plug_params.c` holds the table; `eng_fw.c` checks its firmware parameters against `Parameters.h` when the engine starts (B-6). MIDI on the firmware's global channel; an NRPN is CC 99 (the number >> 7), CC 98 (the number & 127) and CC 6 (the value) at one frame. Defaults: the `init` card's kit (M5).

| Id | Parameter | MIDI | Firmware parameter | Values (shown) | Default |
|---|---|---|---|---|---|
| 1 | Pattern | Program Change | the next pattern, `seq_pendingPattern` | 0–63 (1–64) | 0 (1) |
| 10–15 | Drum 1, Drum 2, Drum 3, Snare, Cymbal, Hi-hat Volume | CC 89–94 | `PAR_VOL1`–`PAR_VOL6` (88–93) | 0–127 | 127 |
| 20–25 | Drum 1 … Hi-hat Pan | CC 95, 96, 97, 100, 101, 102 | `PAR_PAN1`–`PAR_PAN6` (94–96, 99–101) | 0–127 (−63..64) | 63 (0) |
| 30–35 | Drum 1 … Hi-hat Filter | CC 38–43 | `PAR_FILTER_FREQ_1`–`_6` (37–42) | 0–127 | 127, 127, 127, 127, 68, 104 |
| 40–45 | Drum 1 … Cymbal Decay, Hi-hat Closed Decay | CC 51, 53, 55, 57, 59, 61 | `PAR_VELOD1`–`PAR_VELOD5`, `PAR_VELOD6_CLOSED` (50, 52, 54, 56, 58, 60) | 0–127 | 21, 13, 3, 10, 107, 5 |
| 46 | Hi-hat Open Decay | CC 62 | `PAR_VELOD6_OPEN` (61) | 0–127 | 17 |
| 50 | DJ Filter | NRPN 96 | `PAR_DJ_FILTER` (224) | 0–127, 63–65 flat | 64 |
| 51 | Reverb Level | NRPN 98 | `PAR_REV_LEVEL` (226) | 0–127 | 0 |
| 52 | Reverb Decay | NRPN 116 | `PAR_REV_DECAY` (244) | 0–127 | 64 |
| 53 | FX Amount | NRPN 108 | `PAR_FX_DRY_WET` (236) | 0–100 | 0 |
| 60–65 | Drum 1 … Hi-hat Output | NRPN 87–92 | `PAR_AUDIO_OUT1`–`_6` (215–220) | 0–6 (St1, St2, L1, R1, L2, R2, FX) | 0 (St1) |

The order of `plug_params[]` is the table's, and it is the order of the engine's value table in the shared memory (`values[i]`), of `eng_fw.c`'s check and of the processor's first report.

## The values: host, firmware, no loop

What the processor does with each parameter in each block (`plug_params.c`, B-7), in this order: the host's points of the block (`plug_paramsHost()`), then the engine's last report if it can copy one (`plug_paramsReport()`), then the values the host is to get (`plug_paramsToShow()`, output parameter changes at the block's first frame).

```
host point v at frame f:   want = v, shown; if v != sent: MIDI at f, sent = v, sentFrame = f
first report of an engine: want unknown: want = sent = fw, the host is to get it
                           want == fw:   sent = fw
                           want != fw:   MIDI(want) at the block's first frame, unless sent == want since the epoch
later report:              fw == want:   nothing
                           sent known and report frame < sentFrame + settle: nothing (in flight)
                           else:         want = sent = fw, the host is to get it (the firmware's own change)
a restored state:          want = the state's value, the host is to get it, sent unknown, the next report is a first one
```

Measured in `P6_engine_params` and `P10_plugin_params` (M6, M10): the host sets Drum 1's volume to 40 at frame 30,000; the CC reaches the firmware at its cycle, the table shows 40 from frame 30,016, nothing is reported. Pattern 1 at frame 100,000 (stopped): the firmware loads the pattern and its kit at once; a report a few blocks later (frame 100,224–100,480) gives the host Drum 1's volume and filter back as 127; the reverb level, which the host set at 90,000, reaches the host at 101,248–101,376, once the quarter of a second after 90,000 is over. A host that gives every reported value back (`echo`) changes nothing: the same output byte for byte and the same values.

## Real-time safety of the audio thread

The prototype's rules hold (its "Real-time safety of the audio thread", P-23, the 45 calls of `plug_rtcheck`). `process()` adds, in real time:
- `plug_valuesRead()`: an acquire load, 51 relaxed loads and a fence (the seqlock), one try per block;
- `pointsOf()`, `plug_paramsHost()`, `plug_paramsReport()`, `plug_paramsToShow()`: pure functions on the processor's own fixed arrays (`points`, `paramMidi`, `params`);
- 36 relaxed loads and at most 36 relaxed stores of `hostValues` (for `getState()` on the main thread), and the acquire load of `restoreGen`;
- the host's `addParameterData()` and `addPoint()` for the values the host is to get (at most 36 a block; the test host's queues hold 64, so its `ParameterChanges` allocates nothing, F14);
- the four-channel copy from the ring (`plug_audioRead()`, unchanged in kind).

**The check:** `P10_plugin_rt` (unchanged) and `P10_plugin_rtparams`: 10 s paced at 48 kHz in blocks of 128 under the interposer, with three parameters changed every block (a CC, a CC, an NRPN) and the pattern every second: expected 3,750 calls, 0 forbidden, 3,750 writes.

## Rules for every task

- **Where.** Worktree `/Users/tk/Code/lxr/build/wt-plugin-v07`, branch `plugin-v07` from `728dfff`. Every Bash call starts in a new shell: begin each command with `cd /Users/tk/Code/lxr/build/wt-plugin-v07 &&`, and add `source tools/env.sh >/dev/null &&` where it needs `cmake`, `ctest` or `ninja`. Task 11 works in `/Users/tk/Code/lxr` (branch `timing-core`). Never touch another worktree (`build/wt-plugin`, `build/wt-v07`, …) or another branch.
- **Process (lean).** One implementer per group (A: 1–4, B: 5–6, C: 7–9, D: 10–11). After Task 6 and after Task 9: a spec review, then a quality review; after Task 4 and after Tasks 10–11: a light review. Each reviewer runs at least five mutants of the new code (a comparison flipped, a constant off by one, a line removed: in `plug_params.c` at least the settle comparison, the dedupe `v == sent` and the first-report rule) and each must fail a test; a mutant that survives is a test gap, closed in its own commit before the next group starts.
- **Scope.** Read only what a step names; grep and read line ranges. The code below was compiled and replayed while planning: an edit whose old text is not found, or a compile error, is a finding to note under "Changes during execution", not a reason to redesign.
- **No network.** Nothing is downloaded: no `git clone`, `curl`, `pip`, `brew`; the SDK is vendored. If something more seems needed, stop and report it.
- **The user's folders.** The tests set `LICHOMAT_PLUGIN_HOME` and `LICHOMAT_EMU_HOME` to folders of their own. Nothing in this plan installs the plugin or writes into `~/Library/Audio/Plug-Ins/VST3` or `~/Library/Application Support/LICHOMAT Plugin`, and `~/Library/Application Support/LICHOMAT Emu` stays untouched. After every task `ls ~/Library/Audio/Plug-Ins` shows `Components Digidesign VST` (no `VST3`) and `ls ~/Library/Application\ Support | grep LICHOMAT` shows `LICHOMAT Emu` alone, as at planning (F18).
- **Processes.** Kill every engine and host you start; the test cases do (`kill_all`). After every gate `pgrep -fl 'lichomat-(engine|plughost)'` prints nothing.
- **Files.** New files carry the GPL-3.0-only header of the LICHOMAT files (`Copyright (C) 2026 Tomasz Krzywicki`, the SPDX line, the NOTICE.md sentence), as every file below shows. `plugin/` and `docs/` are LF.
- **Names.** No string of the plugin, the engine or the test host contains "LXR", "Erica" or "Sonic Potions" outside `emu_legal`; `P11_licence` scans them. The parameters' names are the plugin's own ("Drum 1 Volume"). Never name the reference album or its artist.
- **Warnings.** Our files: 0 compiler warnings in Debug and Release (`-Wall -Wextra -Werror`).
- **Timing.** The paced cases run serially (`RUN_SERIAL`): the prototype's eight and `P10_plugin_params_restart`, `P10_plugin_rtparams`. Run a gate only when `uptime`'s first load average is below 6 and no other heavy job (an emulator fuzz sweep, another build) runs; never start a load generator. A timing case that fails under load is run once more alone before it counts as a finding.
- **Test values.** Never `time()`; record measured values in the commit message; never widen a tolerance once committed.
- **Git.** `git add` names each file; never `git add -A`, never `--amend`. Commit messages are `plugin Task N (E9.(30+N)): …` and end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. After a task's commit, `git push origin plugin-v07` (HTTPS); never push anything else, never force-push, never touch `main`.
- **Ableton.** The checks in Live (P9–P10) are the owner's. Never mark one as done.

**PLUGIN** (one regex while working; without `-R` every id):

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && source tools/env.sh >/dev/null && cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Debug > build/plugin.log 2>&1 && ninja -C build/plugin >> build/plugin.log 2>&1; echo "== plugin build: $?"; echo "== plugin warnings: $(grep 'warning:' build/plugin.log | grep -v -e '/plugin/third_party/vst3sdk/' -e '/CommandLineTools/' | wc -l | tr -d ' ')"; ctest --test-dir build/plugin -j4 -R 'REGEX' --output-on-failure 2>&1 | grep -E 'tests passed|\*\*\*|Failed|Errors'
```

**PLUGIN-CHECK** (the gate of every task from Task 2 on: Debug with UBSan and Release, every id):

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && plugin/tools/plugin_check.sh
```

Expected: for each of Debug and Release `100% tests passed, 0 tests failed out of T` with the task's T, and `== compiler warnings, …, our files: 0`; exit 0.

**EMU-CHECK** (Tasks 1 and 10):

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && source tools/env.sh >/dev/null && cmake -S emu -B build/emu -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLICHOMAT_EMU_SANITIZE=undefined > build/emu.log 2>&1 && ninja -C build/emu >> build/emu.log 2>&1; echo "== emu build: $?"; echo "== emu warnings: $(grep -c 'warning:' build/emu.log)"; ctest --test-dir build/emu -j8 2>&1 | tail -n 3
```

Expected: `== emu build: 0`, `== emu warnings: 0`, `100% tests passed, 0 tests failed out of 353`.

**CHECK** (Tasks 1 and 10): `cd /Users/tk/Code/lxr/build/wt-plugin-v07 && source tools/env.sh && tools/check.sh`. Expected as at `728dfff` (E9.6): `100% tests passed, 0 tests failed out of 135` for the host tests, `… out of 353` for the emulator, the `memory:` and `budget: … OK` lines Task 1 records, `== compiler warnings: 0`. The plugin branch changes no firmware file, so Task 10 must print the same lines.

## File structure

| File | Responsibility | Task |
|---|---|---|
| `plugin/engine/eng_resample.{h,c}`, `plugin/tests/test_resample.c`, `plugin/tests/plug_resample_wav.c` | the converter of 2 or 4 channels | 2 |
| `plugin/common/plug_shm.{h,c}`, `plugin/tests/test_shm.c` | the shared memory, version 2: four channels, the sum, the value table | 3 |
| `plugin/engine/eng_run.c` | the sink's four channels (3), the values at t0 and after each block (6) | 2, 3, 6 |
| `plugin/tests/plug_drive.c` | the ring read through `readInto()` (3), `split` (4), `values FILE` (6) | 3, 4, 6 |
| `plugin/tests/plugin_test.py` | the scenarios: P6/P7 split (4), P6 params (6), P10 buses (7), P10 params, outputs, restart, rt (8), state (9), the version (10) | 4, 6–10 |
| `plugin/common/plug_params.{h,c}`, `plugin/tests/test_params.c` | the 36 parameters, their MIDI, their step with the firmware, the state's `LCHV` block | 5 |
| `plugin/engine/eng_fw.{h,c}`, `plugin/engine/eng_main.c`, `plugin/engine/eng_run.h` | the reads of the firmware's memory and the table's check | 6 |
| `plugin/vst3/plugin_processor.{h,cpp}` | the sum read (3), the buses (7), the parameters (8), the state's values (9) | 3, 7–9 |
| `plugin/host/plug_host.cpp` | `bus2` (7); `param`, `channel`, `echo`, the reports, the expected MIDI (8) | 7, 8 |
| `plugin/vst3/plugin_points.h`, `plugin/vst3/plugin_controller.cpp`, `plugin/vst3/plugin_link.cpp` | a block's points, the controller's 36 parameters, the log line | 8 |
| `plugin/CMakeLists.txt` | the new ids, `eng_fw`, the version | 2–10 |
| `plugin/README.md`, `plugin/common/plug_notices.h`, `plugin/vst3/plugin_ids.h` | the README, version 0.2.0, the ids' rule | 10 |
| (timing-core) `docs/lichomat/plugin-checks.md`, `docs/lichomat/HANDOFF.md`, `docs/lichomat/tracker.json` | the owner's checks P9–P10, the handoff, the tracker | 11 |

## Task order

| Task | Contents | Plugin ids (T) | Group, review |
|---|---|---:|---|
| 1 | Baseline on `plugin-v07` | 72 | A |
| 2 | The converter of 2 or 4 channels: `P7_converter_channels` | 73 | A |
| 3 | The shared memory, version 2: `P3_shm_split`, `P3_shm_values` | 75 | A |
| 4 | The engine's four channels end to end: `P6_engine_split`, `P7_engine_split` | 77 | A; light review |
| 5 | The parameters (pure C): `P12_params_*` (7) | 84 | B |
| 6 | The engine reports the firmware's values: `P6_engine_params` | 85 | B; full review of Tasks 2–6 |
| 7 | Out 1 and Out 2: `P10_plugin_buses`, `P10_plugin_buses48` | 87 | C |
| 8 | The parameters in the plugin: `P10_plugin_params`, `_outputs`, `_params_restart`, `_rtparams` | 91 | C |
| 9 | The values in the state: `P10_plugin_params_state` | 92 | C; full review of Tasks 7–9 |
| 10 | README, version 0.2.0, the final gates | 92 | D |
| 11 | The owner's checks P9–P10, HANDOFF, tracker (timing-core) | — | D; light review |

---

### Task 1: Baseline on `plugin-v07` — E9.31

**Files:** none.

- [ ] **Step 1: the worktree.**

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && git status -sb && git log --oneline -1 && ls -la .toolchain && ls ~/Library/Audio/Plug-Ins && ls ~/Library/Application\ Support | grep LICHOMAT
```

  Expected: `## plugin-v07...origin/plugin-v07` and `?? .toolchain` only (the link is never staged); `728dfff plugin-v07: README names firmware 0.7 and this branch as the corresponding source`; `.toolchain -> ../../.toolchain`; `Components Digidesign VST`; `LICHOMAT Emu`. A different head or folder is a finding to report before Task 2.
- [ ] **Step 2: the baseline.** PLUGIN-CHECK (72 in both builds), EMU-CHECK (353), CHECK (135 and 353, `== compiler warnings: 0`); record CHECK's `memory:` and `budget:` lines and the validator's line (`ctest --test-dir build/plugin -R P10_plugin_validator -V | grep Result:`: `Result: 47 tests passed, 0 tests failed`). Then `pgrep -fl 'lichomat-(engine|plughost)'` prints nothing. No commit: nothing changed.

---

### Task 2: The converter of 2 or 4 channels — E9.32

**Files:** Modify `plugin/engine/eng_resample.h`, `plugin/engine/eng_resample.c`, `plugin/engine/eng_run.c` (one argument), `plugin/tests/test_resample.c`, `plugin/tests/plug_resample_wav.c` (one argument), `plugin/CMakeLists.txt`.

The engine will convert the device's two outputs at once (B-3); `plug_resample_wav` keeps converting the renderer's stereo WAVs two channels at a time. The new test proves the two agree sample for sample, which `P7_engine_split` (Task 4) relies on. The engine still converts its stereo sum here (2 channels); Task 3 gives it four.

- [ ] **Step 1: the failing tests.**

`plugin/tests/test_resample.c`, edit 1 of 2: replace

```c
	if (eng_resampleOpen(&r, rate, err, sizeof err) != 0)
```

with

```c
	if (eng_resampleOpen(&r, rate, 2, err, sizeof err) != 0)
```

`plugin/tests/test_resample.c`, edit 2 of 2: replace

```c
TEST(P7_converter_open)
{
	eng_resample_t r;
	char err[160];
	ASSERT_EQ(eng_resampleOpen(&r, 48000.0, err, sizeof err), 0);
	eng_resampleClose(&r);
	ASSERT_EQ(eng_resampleOpen(&r, -1.0, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "AudioConverterNew") != NULL);
}
```

with

```c
TEST(P7_converter_open)
{
	eng_resample_t r;
	char err[160];
	ASSERT_EQ(eng_resampleOpen(&r, 48000.0, 2, err, sizeof err), 0);
	eng_resampleClose(&r);
	ASSERT_EQ(eng_resampleOpen(&r, -1.0, 2, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "AudioConverterNew") != NULL);
	ASSERT_EQ(eng_resampleOpen(&r, 48000.0, 3, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "2 or 4 channels") != NULL);
}

/* the frames a converter gives, into a buffer of its own */
typedef struct
{
	float *x;
	uint32_t frames;
	unsigned channels;
} sink_t;

static void into(void *ctx, const float *x, uint32_t frames)
{
	sink_t *k = ctx;
	memcpy(&k->x[k->channels * k->frames], x, k->channels * frames * sizeof *x);
	k->frames += frames;
}

TEST(P7_converter_channels)
{
	/* plan E9.4: four channels converted at once (the engine's: the device's two outputs) are each pair
	 * converted apart (plug_resample_wav's, two at a time), sample for sample, at 44.1, 48 and 96 kHz */
	const double rates[] = { 44100.0, 48000.0, 96000.0 };
	for (unsigned k = 0; k < 3; k++)
	{
		eng_resample_t r4, ra, rb;
		char err[160];
		ASSERT_EQ(eng_resampleOpen(&r4, rates[k], 4, err, sizeof err), 0);
		ASSERT_EQ(eng_resampleOpen(&ra, rates[k], 2, err, sizeof err), 0);
		ASSERT_EQ(eng_resampleOpen(&rb, rates[k], 2, err, sizeof err), 0);
		sink_t s4 = { calloc(4u * 4u * 64u * BLOCKS, sizeof(float)), 0, 4 };
		sink_t sa = { calloc(2u * 4u * 64u * BLOCKS, sizeof(float)), 0, 2 };
		sink_t sb = { calloc(2u * 4u * 64u * BLOCKS, sizeof(float)), 0, 2 };
		uint32_t seed = 12345u;
		for (unsigned b = 0; b < BLOCKS; b++)
		{
			float in4[4 * 64], ina[2 * 64], inb[2 * 64];
			for (unsigned i = 0; i < 4 * 64; i++)
			{
				seed = seed * 1664525u + 1013904223u;
				in4[i] = (float)(int16_t)(seed >> 16) / 32768.0f;	/* a device sample */
			}
			for (unsigned i = 0; i < 64; i++)
			{
				ina[2 * i] = in4[4 * i];
				ina[2 * i + 1] = in4[4 * i + 1];
				inb[2 * i] = in4[4 * i + 2];
				inb[2 * i + 1] = in4[4 * i + 3];
			}
			ASSERT_EQ(eng_resamplePush(&r4, in4, into, &s4), 0);
			ASSERT_EQ(eng_resamplePush(&ra, ina, into, &sa), 0);
			ASSERT_EQ(eng_resamplePush(&rb, inb, into, &sb), 0);
		}
		eng_resampleClose(&r4);
		eng_resampleClose(&ra);
		eng_resampleClose(&rb);
		uint32_t differ = 0;
		for (uint32_t i = 0; i < s4.frames && i < sa.frames; i++)
			differ += s4.x[4 * i] != sa.x[2 * i] || s4.x[4 * i + 1] != sa.x[2 * i + 1] || s4.x[4 * i + 2] != sb.x[2 * i]
				|| s4.x[4 * i + 3] != sb.x[2 * i + 1];
		printf("P7_converter_channels: %.0f Hz: %u frames of four channels, %u and %u of two, %u differ\n", rates[k],
			s4.frames, sa.frames, sb.frames, differ);
		ASSERT_TRUE(s4.frames == sa.frames && sa.frames == sb.frames);
		ASSERT_TRUE(fabs((double)s4.frames - BLOCKS * 64.0 * rates[k] / PLUG_ENGINE_RATE) <= 1.0);
		ASSERT_EQ(differ, 0);
		free(s4.x);
		free(sa.x);
		free(sb.x);
	}
}
```

`plugin/CMakeLists.txt`: replace

```cmake
plugin_add_test(test_resample IDS P7_converter_delay P7_converter_open)
```

with

```cmake
plugin_add_test(test_resample IDS P7_converter_delay P7_converter_open P7_converter_channels)
```

- [ ] **Step 2: they fail.** PLUGIN with REGEX `P7_`. Expected: `== plugin build: 1`; `build/plugin.log` has `test_resample.c:39:41: error: too many arguments to function call, expected 4, have 5` (and the same at `:83:50`).
- [ ] **Step 3: the code.**

`plugin/engine/eng_resample.h`, edit 1 of 2: replace

```c
 * 44,140.625 Hz to the host's rate, float32 stereo interleaved, Normal complexity at maximum
 * quality (the app's choice, app_coreaudio.c:332-341), prime method None, so that every input
 * frame gives its output at once and the delay is a constant 48 input frames.
```

with

```c
 * 44,140.625 Hz to the host's rate, float32 interleaved, 2 channels or 4 (the engine's: the
 * device's two stereo outputs, plan E9.4), Normal complexity at maximum quality (the app's choice,
 * app_coreaudio.c:332-341), prime method None, so that every input frame gives its output at once
 * and the delay is a constant 48 input frames.
```

`plugin/engine/eng_resample.h`, edit 2 of 2: replace

```c
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
```

with

```c
#define ENG_RESAMPLE_OUT 1024u	/**< output frames per pull, more than 64 frames at 384 kHz */
#define ENG_RESAMPLE_CHANNELS 4u	/**< the most channels */

typedef struct eng_resample
{
	void *conv;		/**< AudioConverterRef */
	unsigned channels;
	float in[ENG_RESAMPLE_CHANNELS * ENG_RESAMPLE_IN];
	unsigned have, used;
	float out[ENG_RESAMPLE_CHANNELS * ENG_RESAMPLE_OUT];
} eng_resample_t;

/** channels 2 or 4; 0, or -1 with the problem in err */
int eng_resampleOpen(eng_resample_t *r, double rate, unsigned channels, char *err, size_t n);

/** Convert one block of 64 interleaved frames; fn receives the output frames (any count, also 0)
 * with ctx. Returns 0, or the converter's error. */
int eng_resamplePush(eng_resample_t *r, const float *x, void (*fn)(void *ctx, const float *x, uint32_t frames),
	void *ctx);
```

`plugin/engine/eng_resample.c`, edit 1 of 5: replace

```c
	io->mBuffers[0].mData = &r->in[2u * r->used];
	io->mBuffers[0].mDataByteSize = *packets * 2u * (UInt32)sizeof(float);
	io->mBuffers[0].mNumberChannels = 2;
```

with

```c
	io->mBuffers[0].mData = &r->in[r->channels * r->used];
	io->mBuffers[0].mDataByteSize = *packets * r->channels * (UInt32)sizeof(float);
	io->mBuffers[0].mNumberChannels = r->channels;
```

`plugin/engine/eng_resample.c`, edit 2 of 5: replace

```c
int eng_resampleOpen(eng_resample_t *r, double rate, char *err, size_t n)
{
	memset(r, 0, sizeof *r);
```

with

```c
int eng_resampleOpen(eng_resample_t *r, double rate, unsigned channels, char *err, size_t n)
{
	memset(r, 0, sizeof *r);
	if (channels != 2u && channels != ENG_RESAMPLE_CHANNELS)
	{
		snprintf(err, n, "the converter takes 2 or 4 channels, not %u", channels);
		return -1;
	}
	r->channels = channels;
```

`plugin/engine/eng_resample.c`, edit 3 of 5: replace

```c
	in.mChannelsPerFrame = 2;
	in.mFramesPerPacket = 1;
	in.mBytesPerFrame = in.mBytesPerPacket = 2u * (UInt32)sizeof(float);
```

with

```c
	in.mChannelsPerFrame = channels;
	in.mFramesPerPacket = 1;
	in.mBytesPerFrame = in.mBytesPerPacket = channels * (UInt32)sizeof(float);
```

`plugin/engine/eng_resample.c`, edit 4 of 5: replace

```c
int eng_resamplePush(eng_resample_t *r, const float *lr, void (*fn)(void *ctx, const float *lr, uint32_t frames),
	void *ctx)
{
	memcpy(r->in, lr, sizeof r->in);
```

with

```c
int eng_resamplePush(eng_resample_t *r, const float *x, void (*fn)(void *ctx, const float *x, uint32_t frames),
	void *ctx)
{
	memcpy(r->in, x, r->channels * ENG_RESAMPLE_IN * sizeof(float));
```

`plugin/engine/eng_resample.c`, edit 5 of 5: replace

```c
		abl.mBuffers[0].mNumberChannels = 2;
		abl.mBuffers[0].mDataByteSize = (UInt32)sizeof r->out;
```

with

```c
		abl.mBuffers[0].mNumberChannels = r->channels;
		abl.mBuffers[0].mDataByteSize = r->channels * ENG_RESAMPLE_OUT * (UInt32)sizeof(float);
```

`plugin/tests/plug_resample_wav.c`: replace

```c
	if (eng_resampleOpen(&r, rate, err, sizeof err) != 0)
```

with

```c
	if (eng_resampleOpen(&r, rate, 2, err, sizeof err) != 0)
```

`plugin/engine/eng_run.c`: replace

```c
	if (!E.native && eng_resampleOpen(&E.rs, s->rate, err, sizeof err) != 0)
```

with

```c
	if (!E.native && eng_resampleOpen(&E.rs, s->rate, 2, err, sizeof err) != 0)
```

- [ ] **Step 4: they pass.** PLUGIN with REGEX `P7_`. Expected: `== plugin build: 0`, `== plugin warnings: 0`, `100% tests passed, 0 tests failed out of 5`; with `-V`: `P7_converter_channels: 44100 Hz: 25576 frames of four channels, 25576 and 25576 of two, 0 differ` (48 kHz 27,838 frames, 96 kHz 55,676; 0 differ).
- [ ] **Step 5: the gate.** PLUGIN-CHECK, T = 73.
- [ ] **Step 6: commit and push.**

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && git add plugin/engine/eng_resample.h plugin/engine/eng_resample.c plugin/engine/eng_run.c plugin/tests/test_resample.c plugin/tests/plug_resample_wav.c plugin/CMakeLists.txt && git commit -m "plugin Task 2 (E9.32): the converter takes 2 or 4 channels; four at once are each pair apart, sample for sample

P7_converter_channels: 0 of 25,576 / 27,838 / 55,676 frames differ at 44.1, 48, 96 kHz.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && git push origin plugin-v07
```

---

### Task 3: The shared memory, version 2 — E9.33

**Files:** Modify `plugin/common/plug_shm.h`, `plugin/common/plug_shm.c`, `plugin/tests/test_shm.c`, `plugin/engine/eng_run.c`, `plugin/vst3/plugin_processor.cpp`, `plugin/tests/plug_drive.c`, `plugin/CMakeLists.txt`.

The ring's four channels and the value table (B-2, B-3, B-6). The engine's sink writes the device's outputs exactly (sample / 32,768) and converts four channels; the processor and `plug_drive` read the sum (`plug_sum()`), so every output stays the prototype's bit for bit: the old P6, P7 and P10 cases pass unchanged. The P3 cases take the new API with the same values checked; `P3_shm_header` now checks that a version-1 header (the prototype's) is refused with both versions named.

- [ ] **Step 1: the failing tests.**

`plugin/tests/test_shm.c`, edit 1 of 12: replace

```c
 * P3: the shared memory between the plugin and the engine (plan E9.2 Task 3): the header check,
 * the event ring, the audio ring with its epoch and underruns, a new engine's epoch, and the two
 * rings between two processes, with a killed child seen as the end of a pipe.
```

with

```c
 * P3: the shared memory between the plugin and the engine (plan E9.2 Task 3): the header check,
 * the event ring, the audio ring with its epoch and underruns, a new engine's epoch, and the two
 * rings between two processes, with a killed child seen as the end of a pipe. Plan E9.4: the
 * ring's four channels read apart or summed, and the engine's values under their sequence count.
```

`plugin/tests/test_shm.c`, edit 2 of 12: replace

```c
	ASSERT_EQ(s->size, sizeof(plug_shm_t));
	s->version = 2;
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "version 2") != NULL);
```

with

```c
	ASSERT_EQ(s->size, sizeof(plug_shm_t));
	/* the prototype's version 1 (two channels, no values): refused, both ways named */
	s->version = 1;
	ASSERT_EQ(plug_shmCheck(s, err, sizeof err), -1);
	ASSERT_TRUE(strstr(err, "version 1") != NULL && strstr(err, "needs version 2") != NULL);
```

`plugin/tests/test_shm.c`, edit 3 of 12: replace

```c
	snprintf(want, sizeof want, "version 1 size %u,", (unsigned)sizeof(plug_shm_t) - 8u);
```

with

```c
	snprintf(want, sizeof want, "version 2 size %u,", (unsigned)sizeof(plug_shm_t) - 8u);
```

`plugin/tests/test_shm.c`, edit 4 of 12: replace

```c
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
```

with

```c
/* frames of four channels: L1 = i, R1 = -i, L2 = 3i, R2 = -3i (the sum: 2i, -2i) */
static void fourOf(float *x, unsigned frames)
{
	for (unsigned i = 0; i < frames; i++)
	{
		x[4 * i] = (float)i;
		x[4 * i + 1] = -(float)i;
		x[4 * i + 2] = 3.0f * (float)i;
		x[4 * i + 3] = -3.0f * (float)i;
	}
}

TEST(P3_shm_audio)
{
	plug_shm_t *s = anon();
	float x[4 * 64], l[100], r[100];
	float *const sum[4] = { l, r, NULL, NULL };
	fourOf(x, 64);
	plug_audioBegin(s, 100);
	plug_audioWrite(s, x, 64);
	ASSERT_EQ(s->audioEnd, 164);
	/* not running: no underrun is counted */
	ASSERT_EQ(plug_audioRead(s, 90, 100, sum, 1), 64);
	ASSERT_EQ_DBL(l[9], 0.0);
	ASSERT_EQ_DBL(l[10], 0.0);
	ASSERT_EQ_DBL(l[11], 2.0);
	ASSERT_EQ_DBL(r[73], -126.0);
	ASSERT_EQ_DBL(l[74], 0.0);
	ASSERT_EQ(s->underruns, 0);
	/* running: the 26 frames from 164 on are missing; the 10 before the epoch are not */
	s->state = PLUG_ENGINE_RUNNING;
	ASSERT_EQ(plug_audioRead(s, 90, 100, sum, 1), 64);
	ASSERT_EQ(s->underruns, 26);
	munmap(s, sizeof *s);
}

TEST(P3_shm_split)
{
	/* the four channels apart, each where the engine wrote it; the frames the ring does not hold 0 in
	 * all four; and the sum is plug_sum() of the two outputs, exact for the device's samples */
	plug_shm_t *s = anon();
	float x[4 * 64], l1[80], r1[80], l2[80], r2[80];
	float *const split[4] = { l1, r1, l2, r2 };
	fourOf(x, 64);
	plug_audioBegin(s, 1000);
	plug_audioWrite(s, x, 64);
	for (unsigned c = 0; c < 4; c++)
		for (unsigned i = 0; i < 80; i++)
			split[c][i] = 0.5f;
	ASSERT_EQ(plug_audioRead(s, 990, 80, split, 0), 64);
	ASSERT_EQ_DBL(l1[9], 0.0);
	ASSERT_EQ_DBL(r2[9], 0.0);
	ASSERT_EQ_DBL(l1[15], 5.0);
	ASSERT_EQ_DBL(r1[15], -5.0);
	ASSERT_EQ_DBL(l2[15], 15.0);
	ASSERT_EQ_DBL(r2[15], -15.0);
	ASSERT_EQ_DBL(l2[74], 0.0);
	ASSERT_EQ_DBL(r2[79], 0.0);
	for (int32_t a = -32768; a <= 32767; a += 257)
		for (int32_t b = -32768; b <= 32767; b += 263)
			ASSERT_EQ_DBL(plug_sum((float)a / 32768.0f, (float)b / 32768.0f), (double)(a + b) / 65536.0);
	munmap(s, sizeof *s);
}

TEST(P3_shm_values)
{
	plug_shm_t *s = anon();
	plug_values_t v;
	ASSERT_EQ(plug_valuesRead(s, &v), 0);	/* none written */
	const uint8_t a[3] = { 7, 127, 0 };
	plug_valuesWrite(s, 4321, a, 3, 15, 0x0F);
	ASSERT_EQ(plug_valuesRead(s, &v), 1);
	ASSERT_EQ(v.frame, 4321);
	ASSERT_EQ(v.values[0], 7);
	ASSERT_EQ(v.values[1], 127);
	ASSERT_EQ(v.values[2], 0);
	ASSERT_EQ(v.values[3], 0xFF);	/* past n */
	ASSERT_EQ(v.values[PLUG_VALUES - 1], 0xFF);
	ASSERT_EQ(v.channel, 15);
	ASSERT_EQ(v.rxFilter, 0x0F);
	ASSERT_EQ(s->valuesSeq, 2);
	/* a write in progress (the count odd) is not taken */
	s->valuesSeq = 3;
	ASSERT_EQ(plug_valuesRead(s, &v), 0);
	s->valuesSeq = 4;
	ASSERT_EQ(plug_valuesRead(s, &v), 1);
	munmap(s, sizeof *s);
}
```

`plugin/tests/test_shm.c`, edit 5 of 12: replace

```c
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
```

with

```c
TEST(P3_shm_new_engine)
{
	plug_shm_t *s = anon();
	float x[4 * 1000], l[100], r[100];
	float *const sum[4] = { l, r, NULL, NULL };
	for (unsigned i = 0; i < 4000; i++)
		x[i] = 1.0f;
	s->state = PLUG_ENGINE_RUNNING;
	plug_audioBegin(s, 100);
	plug_audioWrite(s, x, 1000);
	/* a new engine starts at 5000: the old frames are gone, the gap is not an underrun */
	plug_audioBegin(s, 5000);
	ASSERT_EQ(plug_audioRead(s, 1000, 100, sum, 1), 0);
	ASSERT_EQ_DBL(l[0], 0.0);
	ASSERT_EQ(s->underruns, 0);
	plug_audioWrite(s, x, 64);
	ASSERT_EQ(plug_audioRead(s, 4990, 100, sum, 1), 64);
```

`plugin/tests/test_shm.c`, edit 6 of 12: replace

```c
	plug_shm_t *s = anon();
	static float lr[2 * PLUG_AUDIO_FRAMES];
	float l[8], r[8];
	for (unsigned i = 0; i < PLUG_AUDIO_FRAMES; i++)
		lr[2 * i] = lr[2 * i + 1] = (float)(i + 1);
	plug_audioBegin(s, 0);
	plug_audioWrite(s, lr, PLUG_AUDIO_FRAMES);
	plug_audioWrite(s, lr, 4);	/* frames 0..3 are overwritten by 32768..32771 */
	/* and frame 4 shares its slot with frame 32772, the next the engine writes: the ring holds the
	 * 32,767 frames before audioEnd */
	ASSERT_EQ(plug_audioRead(s, 0, 8, l, r), 3);
```

with

```c
	plug_shm_t *s = anon();
	static float x[4 * PLUG_AUDIO_FRAMES];
	float l[8], r[8];
	float *const sum[4] = { l, r, NULL, NULL };
	for (unsigned i = 0; i < 4 * PLUG_AUDIO_FRAMES; i++)
		x[i] = (float)(i / 4 + 1);
	plug_audioBegin(s, 0);
	plug_audioWrite(s, x, PLUG_AUDIO_FRAMES);
	plug_audioWrite(s, x, 4);	/* frames 0..3 are overwritten by 32768..32771 */
	/* and frame 4 shares its slot with frame 32772, the next the engine writes: the ring holds the
	 * 32,767 frames before audioEnd */
	ASSERT_EQ(plug_audioRead(s, 0, 8, sum, 1), 3);
```

`plugin/tests/test_shm.c`, edit 7 of 12: replace

```c
		static float lr[2 * 1024];
		uint64_t f = 0;
		while (!plug_load(&c->quit))
		{
			for (unsigned i = 0; i < 1024; i++)
				lr[2 * i] = lr[2 * i + 1] = (float)((f + i) & 0xFFFFFFu);
			plug_audioWrite(c, lr, 1024);
			f += 1024;
		}
```

with

```c
		static float x[4 * 1024];
		uint64_t f = 0;
		while (!plug_load(&c->quit))
		{
			for (unsigned i = 0; i < 4 * 1024; i++)
				x[i] = (float)((f + i / 4) & 0xFFFFFFu);
			plug_audioWrite(c, x, 1024);
			f += 1024;
		}
```

`plugin/tests/test_shm.c`, edit 8 of 12: replace

```c
	static float l[1024], r[1024];
	uint64_t reads = 0, copied = 0, wrong = 0;
```

with

```c
	static float l[1024], r[1024];
	float *const sum[4] = { l, r, NULL, NULL };
	uint64_t reads = 0, copied = 0, wrong = 0;
```

`plugin/tests/test_shm.c`, edit 9 of 12: replace

```c
		const uint32_t n = plug_audioRead(s, from, 1024, l, r);
```

with

```c
		const uint32_t n = plug_audioRead(s, from, 1024, sum, 1);
```

`plugin/tests/test_shm.c`, edit 10 of 12: replace

```c
			const float lr[2] = { (float)a, -(float)a };
			plug_audioWrite(s, lr, 1);
```

with

```c
			const float x[4] = { (float)a, -(float)a, (float)a, -(float)a };
			plug_audioWrite(s, x, 1);
```

`plugin/tests/test_shm.c`, edit 11 of 12: replace

```c
		static float l[N_EVENTS / 1000], r[N_EVENTS / 1000];
		const uint64_t end = plug_load(&s->audioEnd);
```

with

```c
		static float l[N_EVENTS / 1000], r[N_EVENTS / 1000];
		float *const sum[4] = { l, r, NULL, NULL };
		const uint64_t end = plug_load(&s->audioEnd);
```

`plugin/tests/test_shm.c`, edit 12 of 12: replace

```c
			ASSERT_EQ(plug_audioRead(s, checked, n, l, r), n);
```

with

```c
			ASSERT_EQ(plug_audioRead(s, checked, n, sum, 1), n);
```

`plugin/CMakeLists.txt`: replace

```cmake
plugin_add_test(test_shm IDS P3_shm_header P3_shm_create P3_shm_events P3_shm_audio P3_shm_new_engine P3_shm_overwritten P3_shm_overrun
    P3_shm_processes P3_shm_killed)
```

with

```cmake
plugin_add_test(test_shm IDS P3_shm_header P3_shm_create P3_shm_events P3_shm_audio P3_shm_new_engine P3_shm_overwritten P3_shm_overrun
    P3_shm_processes P3_shm_killed P3_shm_split P3_shm_values)
```

- [ ] **Step 2: they fail.** PLUGIN with REGEX `P3_`. Expected: `== plugin build: 1`; `build/plugin.log` has `test_shm.c:179:39: error: incompatible pointer types passing 'float *const[4]' to parameter of type 'float *'` (the old `plug_audioRead()`).
- [ ] **Step 3: the code.**

`plugin/common/plug_shm.h`, edit 1 of 7: replace

```c
 * versioned header, a ring of MIDI events from the plugin to the engine and a ring of stereo
 * audio frames from the engine to the plugin. Each ring has one producer and one consumer, in
 * two processes.
```

with

```c
 * versioned header, a ring of MIDI events from the plugin to the engine, a ring of audio frames
 * from the engine to the plugin (the device's two stereo outputs, plan E9.4) and the engine's
 * report of the firmware's parameter values. Each ring has one producer and one consumer, in
 * two processes.
```

`plugin/common/plug_shm.h`, edit 2 of 7: replace

```c
 * - The engine's death is not in the memory: the plugin sees it as the end of a pipe
 *   (plugin_link.cpp), and then writes the engine's state itself.
```

with

```c
 * - The engine's death is not in the memory: the plugin sees it as the end of a pipe
 *   (plugin_link.cpp), and then writes the engine's state itself.
 * - Values: after each block the engine writes the firmware's values of plug_params.h's
 *   parameters, its MIDI channel and receive filter, and the host frame they are of, under a
 *   sequence count that is odd while it writes; the plugin takes a copy whose count did not move.
 * - Version 2 (plan E9.4): four audio channels and the values. The plugin and its engine come
 *   from one bundle; an engine of another version refuses the memory (plug_shmCheck(), and
 *   plug_shmMap() for a smaller object) with exit 2, which the plugin logs.
```

`plugin/common/plug_shm.h`, edit 3 of 7: replace

```c
#define PLUG_SHM_VERSION 1u
```

with

```c
#define PLUG_SHM_VERSION 2u
```

`plugin/common/plug_shm.h`, edit 4 of 7: replace

```c
#define PLUG_AUDIO_FRAMES 32768u	/**< a power of two; 0.68 s at 48 kHz, far more than any latency */
```

with

```c
#define PLUG_AUDIO_FRAMES 32768u	/**< a power of two; 0.68 s at 48 kHz, far more than any latency */
#define PLUG_AUDIO_CHANNELS 4u		/**< out 1 L, out 1 R, out 2 L, out 2 R */
#define PLUG_VALUES 48u				/**< the value table's slots (plug_params.h uses PLUG_PARAM_COUNT) */
```

`plugin/common/plug_shm.h`, edit 5 of 7: replace

```c
	uint64_t dropped;		/**< events left out: before the epoch, or a message without a USB-MIDI code
							 * index number (F0, F4, F5, F7, or a data byte for a status) */
	plug_event_t ev[PLUG_EVENT_SLOTS];
	float audio[PLUG_AUDIO_FRAMES][2];
} plug_shm_t;
```

with

```c
	uint64_t dropped;		/**< events left out: before the epoch, or a message without a USB-MIDI code
							 * index number (F0, F4, F5, F7, or a data byte for a status) */
	uint64_t valuesSeq;		/**< odd while the engine writes the values */
	uint64_t valuesFrame;	/**< the host frame the firmware had reached */
	uint8_t values[PLUG_VALUES];	/**< plug_params[] order; 0xFF: none */
	uint8_t channel;		/**< the firmware's global MIDI channel 0-15, 0xFF: none */
	uint8_t rxFilter;		/**< the firmware's MIDI receive filter: bit 2 CC, bit 3 Program Change */
	uint8_t pad1[6];
	plug_event_t ev[PLUG_EVENT_SLOTS];
	float audio[PLUG_AUDIO_FRAMES][PLUG_AUDIO_CHANNELS];	/**< the device's int16 outputs / 32,768 */
} plug_shm_t;

/** a copy of the engine's values */
typedef struct plug_values
{
	uint64_t frame;
	uint8_t values[PLUG_VALUES];
	uint8_t channel;
	uint8_t rxFilter;
} plug_values_t;
```

`plugin/common/plug_shm.h`, edit 6 of 7: replace

```c
/** Copy the frames [from, from + frames) into l and r. A frame the engine has not written, or has
 * written over (more than PLUG_AUDIO_FRAMES - 1 before audioEnd, also while it was copied), is 0;
 * one that it should have written by now (from its epoch on, while it runs) counts in underruns.
 * Returns the frames copied from the ring. */
uint32_t plug_audioRead(plug_shm_t *s, uint64_t from, uint32_t frames, float *l, float *r);
```

with

```c
/** the stereo sum of the two outputs at -6 dB, as the prototype's one output: (a + b) / 2, exact
 * for the device's samples (a and b are multiples of 2^-15) */
static inline float plug_sum(float a, float b)
{
	return (a + b) * 0.5f;
}

/** Copy the frames [from, from + frames): with sum 0, out 1 L and R into ch[0] and ch[1] and out 2
 * L and R into ch[2] and ch[3]; with sum 1, plug_sum() of the outputs' L into ch[0] and of their R
 * into ch[1] (ch[2] and ch[3] unused). A frame the engine has not written, or has written over
 * (more than PLUG_AUDIO_FRAMES - 1 before audioEnd, also while it was copied), is 0; one that it
 * should have written by now (from its epoch on, while it runs) counts in underruns. Returns the
 * frames copied from the ring. */
uint32_t plug_audioRead(plug_shm_t *s, uint64_t from, uint32_t frames, float *const ch[PLUG_AUDIO_CHANNELS], int sum);

/** a copy of the engine's last values: 1, or 0 when it has written none or was writing them */
int plug_valuesRead(plug_shm_t *s, plug_values_t *v);
```

`plugin/common/plug_shm.h`, edit 7 of 7: replace

```c
/** append frames, interleaved L R, at audioEnd, publishing audioEnd after each */
void plug_audioWrite(plug_shm_t *s, const float *lr, uint32_t frames);
```

with

```c
/** append frames, PLUG_AUDIO_CHANNELS floats each (L1 R1 L2 R2), at audioEnd, publishing audioEnd
 * after each */
void plug_audioWrite(plug_shm_t *s, const float *x, uint32_t frames);

/** the firmware's values at host frame frame: n of them (at most PLUG_VALUES; the rest 0xFF), its
 * global channel and receive filter */
void plug_valuesWrite(plug_shm_t *s, uint64_t frame, const uint8_t *values, unsigned n, uint8_t channel,
	uint8_t rxFilter);
```

`plugin/common/plug_shm.c`, edit 1 of 5: replace

```c
uint32_t plug_audioRead(plug_shm_t *s, uint64_t from, uint32_t frames, float *l, float *r)
{
```

with

```c
uint32_t plug_audioRead(plug_shm_t *s, uint64_t from, uint32_t frames, float *const ch[PLUG_AUDIO_CHANNELS], int sum)
{
	const unsigned outs = sum ? 2u : PLUG_AUDIO_CHANNELS;
```

`plugin/common/plug_shm.c`, edit 2 of 5: replace

```c
		if (f >= start && f < end && end - f < PLUG_AUDIO_FRAMES)
		{
			const float *p = s->audio[f & (PLUG_AUDIO_FRAMES - 1u)];
			l[i] = p[0];
			r[i] = p[1];
			copied++;
		}
		else
		{
			l[i] = r[i] = 0.0f;
```

with

```c
		if (f >= start && f < end && end - f < PLUG_AUDIO_FRAMES)
		{
			const float *p = s->audio[f & (PLUG_AUDIO_FRAMES - 1u)];
			if (sum)
			{
				ch[0][i] = plug_sum(p[0], p[2]);
				ch[1][i] = plug_sum(p[1], p[3]);
			}
			else
				for (unsigned c = 0; c < PLUG_AUDIO_CHANNELS; c++)
					ch[c][i] = p[c];
			copied++;
		}
		else
		{
			for (unsigned c = 0; c < outs; c++)
				ch[c][i] = 0.0f;
```

`plugin/common/plug_shm.c`, edit 3 of 5: replace

```c
			if (f >= start && f < end && end - f < PLUG_AUDIO_FRAMES && f + PLUG_AUDIO_FRAMES <= now)
			{
				l[i] = r[i] = 0.0f;
				copied--;
			}
```

with

```c
			if (f >= start && f < end && end - f < PLUG_AUDIO_FRAMES && f + PLUG_AUDIO_FRAMES <= now)
			{
				for (unsigned c = 0; c < outs; c++)
					ch[c][i] = 0.0f;
				copied--;
			}
```

`plugin/common/plug_shm.c`, edit 4 of 5: replace

```c
void plug_audioWrite(plug_shm_t *s, const float *lr, uint32_t frames)
{
```

with

```c
void plug_audioWrite(plug_shm_t *s, const float *x, uint32_t frames)
{
```

`plugin/common/plug_shm.c`, edit 5 of 5: replace

```c
		float *p = s->audio[end & (PLUG_AUDIO_FRAMES - 1u)];
		p[0] = lr[2u * i];
		p[1] = lr[2u * i + 1u];
		plug_store(&s->audioEnd, end + 1u);
	}
}
```

with

```c
		float *p = s->audio[end & (PLUG_AUDIO_FRAMES - 1u)];
		for (unsigned c = 0; c < PLUG_AUDIO_CHANNELS; c++)
			p[c] = x[PLUG_AUDIO_CHANNELS * i + c];
		plug_store(&s->audioEnd, end + 1u);
	}
}

/* The values under a sequence count (a seqlock): odd while written. The bytes are relaxed atomics,
 * ordered by the fences, so that neither side reads a byte the other writes as a plain one. */
void plug_valuesWrite(plug_shm_t *s, uint64_t frame, const uint8_t *values, unsigned n, uint8_t channel,
	uint8_t rxFilter)
{
	const uint64_t q = __atomic_load_n(&s->valuesSeq, __ATOMIC_RELAXED);
	__atomic_store_n(&s->valuesSeq, q + 1u, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_RELEASE);
	__atomic_store_n(&s->valuesFrame, frame, __ATOMIC_RELAXED);
	for (unsigned i = 0; i < PLUG_VALUES; i++)
		__atomic_store_n(&s->values[i], i < n ? values[i] : (uint8_t)0xFFu, __ATOMIC_RELAXED);
	__atomic_store_n(&s->channel, channel, __ATOMIC_RELAXED);
	__atomic_store_n(&s->rxFilter, rxFilter, __ATOMIC_RELAXED);
	__atomic_store_n(&s->valuesSeq, q + 2u, __ATOMIC_RELEASE);
}

int plug_valuesRead(plug_shm_t *s, plug_values_t *v)
{
	const uint64_t q = __atomic_load_n(&s->valuesSeq, __ATOMIC_ACQUIRE);
	if (q == 0 || (q & 1u))
		return 0;
	v->frame = __atomic_load_n(&s->valuesFrame, __ATOMIC_RELAXED);
	for (unsigned i = 0; i < PLUG_VALUES; i++)
		v->values[i] = __atomic_load_n(&s->values[i], __ATOMIC_RELAXED);
	v->channel = __atomic_load_n(&s->channel, __ATOMIC_RELAXED);
	v->rxFilter = __atomic_load_n(&s->rxFilter, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	return __atomic_load_n(&s->valuesSeq, __ATOMIC_RELAXED) == q;
}
```

`plugin/engine/eng_run.c`, edit 1 of 4: replace

```c
static void eng_ring(void *ctx, const float *lr, uint32_t frames)
{
	(void)ctx;
	plug_audioWrite(E.s, lr, frames);
}
```

with

```c
static void eng_ring(void *ctx, const float *x, uint32_t frames)
{
	(void)ctx;
	plug_audioWrite(E.s, x, frames);
}
```

`plugin/engine/eng_run.c`, edit 2 of 4: replace

```c
/* the audio sink: t0 as the renderer finds it (render_main.c:355-380), then each block as the
 * stereo sum, (L1 + L2) / 65536 and (R1 + R2) / 65536: the renderer's --stereo sum before its
 * shift, exactly, and -6 dB of the device's two outputs added */
```

with

```c
/* the audio sink: t0 as the renderer finds it (render_main.c:355-380), then each block as the
 * device's two outputs, L1 R1 L2 R2, each sample / 32,768 (plan E9.4): exact, so that the plugin's
 * sum of them, plug_sum(), is the prototype's (L1 + L2) / 65,536, the renderer's --stereo sum
 * before its shift */
```

`plugin/engine/eng_run.c`, edit 3 of 4: replace

```c
	float lr[2u * EMU_BLOCK_FRAMES];
	for (unsigned i = 0; i < 2u * EMU_BLOCK_FRAMES; i++)
		lr[i] = (float)((int32_t)o1[i] + o2[i]) * (1.0f / 65536.0f);
	if (E.native)
		plug_audioWrite(E.s, lr, EMU_BLOCK_FRAMES);
	else if (eng_resamplePush(&E.rs, lr, eng_ring, NULL) != 0)
```

with

```c
	float x[PLUG_AUDIO_CHANNELS * EMU_BLOCK_FRAMES];
	for (unsigned i = 0; i < EMU_BLOCK_FRAMES; i++)
	{
		x[4u * i] = (float)o1[2u * i] * (1.0f / 32768.0f);
		x[4u * i + 1u] = (float)o1[2u * i + 1u] * (1.0f / 32768.0f);
		x[4u * i + 2u] = (float)o2[2u * i] * (1.0f / 32768.0f);
		x[4u * i + 3u] = (float)o2[2u * i + 1u] * (1.0f / 32768.0f);
	}
	if (E.native)
		plug_audioWrite(E.s, x, EMU_BLOCK_FRAMES);
	else if (eng_resamplePush(&E.rs, x, eng_ring, NULL) != 0)
```

`plugin/engine/eng_run.c`, edit 4 of 4: replace

```c
	if (!E.native && eng_resampleOpen(&E.rs, s->rate, 2, err, sizeof err) != 0)
```

with

```c
	if (!E.native && eng_resampleOpen(&E.rs, s->rate, PLUG_AUDIO_CHANNELS, err, sizeof err) != 0)
```

`plugin/vst3/plugin_processor.cpp`: replace

```cpp
		if (s)
			copied = plug_audioRead (s, frames + skip - readOffset, n - skip, l + skip, r + skip);
		else
```

with

```cpp
		if (s)
		{
			float* const at[PLUG_AUDIO_CHANNELS] = {l + skip, r + skip, nullptr, nullptr};
			copied = plug_audioRead (s, frames + skip - readOffset, n - skip, at, 1);	/* the outputs' sum */
		}
		else
```

`plugin/tests/plug_drive.c`, edit 1 of 6: replace

```c
int main(int argc, char **argv)
{
```

with

```c
/* the ring's frames [from, from + n) into ch[c][at...]: the sum into ch[0] and ch[1], or with split
 * the four channels */
static void readInto(plug_shm_t *s, uint64_t from, uint32_t n, float *const *ch, uint64_t at, int split)
{
	float *p[PLUG_AUDIO_CHANNELS];
	for (unsigned c = 0; c < PLUG_AUDIO_CHANNELS; c++)
		p[c] = &ch[c][at];
	(void)plug_audioRead(s, from, n, p, !split);
}

int main(int argc, char **argv)
{
```

`plugin/tests/plug_drive.c`, edit 2 of 6: replace

```c
	/* play */
	float *l = calloc(frames + block, sizeof *l), *r = calloc(frames + block, sizeof *r);
```

with

```c
	/* play */
	float *ch[PLUG_AUDIO_CHANNELS];
	for (unsigned c = 0; c < PLUG_AUDIO_CHANNELS; c++)
		ch[c] = calloc(frames + block, sizeof(float));
```

`plugin/tests/plug_drive.c`, edit 3 of 6: replace

```c
			if (end > base + got)
			{
				const uint64_t to = end - base < frames ? end - base : frames;
				(void)plug_audioRead(s, base + got, (uint32_t)(to - got), &l[got], &r[got]);
				got = to;
			}
			if (end != lastEnd)
```

with

```c
			if (end > base + got)
			{
				const uint64_t to = end - base < frames ? end - base : frames;
				readInto(s, base + got, (uint32_t)(to - got), ch, got, 0);
				got = to;
			}
			if (end != lastEnd)
```

`plugin/tests/plug_drive.c`, edit 4 of 6: replace

```c
					(void)plug_audioRead(s, from, take, &l[from], &r[from]);
```

with

```c
					readInto(s, from, take, ch, from, 0);
```

`plugin/tests/plug_drive.c`, edit 5 of 6: replace

```c
			if (end > base + got)
			{
				const uint64_t to = end - base < frames ? end - base : frames;
				(void)plug_audioRead(s, base + got, (uint32_t)(to - got), &l[got], &r[got]);
				got = to;
			}
		}
		n += block;
```

with

```c
			if (end > base + got)
			{
				const uint64_t to = end - base < frames ? end - base : frames;
				readInto(s, base + got, (uint32_t)(to - got), ch, got, 0);
				got = to;
			}
		}
		n += block;
```

`plugin/tests/plug_drive.c`, edit 6 of 6: replace

```c
		put16(o, (uint16_t)sample(l[i], native));
		put16(o, (uint16_t)sample(r[i], native));
```

with

```c
		put16(o, (uint16_t)sample(ch[0][i], native));
		put16(o, (uint16_t)sample(ch[1][i], native));
```

- [ ] **Step 4: they pass.** PLUGIN with REGEX `P3_`. Expected: `== plugin build: 0`, `== plugin warnings: 0`, `100% tests passed, 0 tests failed out of 11`. Then PLUGIN without `-R`: `100% tests passed, 0 tests failed out of 75` (the outputs unchanged: `P6_engine_native`, `P10_plugin_native`, `P10_plugin_resampled` byte for byte as before).
- [ ] **Step 5: the gate.** PLUGIN-CHECK, T = 75.
- [ ] **Step 6: commit and push.**

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && git add plugin/common/plug_shm.h plugin/common/plug_shm.c plugin/tests/test_shm.c plugin/engine/eng_run.c plugin/vst3/plugin_processor.cpp plugin/tests/plug_drive.c plugin/CMakeLists.txt && git commit -m "plugin Task 3 (E9.33): the shared memory, version 2: the device's two outputs as four channels, their sum read exactly as before, the engine's value table under a sequence count

590,040 bytes (327,824 in version 1); a version-1 header is refused. The prototype's outputs unchanged: 75 of 75.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && git push origin plugin-v07
```

---

### Task 4: The engine's four channels end to end — E9.34

**Files:** Modify `plugin/tests/plug_drive.c`, `plugin/tests/plugin_test.py`, `plugin/CMakeLists.txt`.

`plug_drive split` writes the ring's four channels as a four-channel WAV, the renderer's layout without `--stereo` (B-20). `P6_engine_split` compares it with `lichomat-render` byte for byte at the native rate; `P7_engine_split` compares each output at 48 kHz with the renderer's `--stereo out1` and `out2` through `plug_resample_wav`.

- [ ] **Step 1: the failing tests.**

`plugin/tests/plugin_test.py`, edit 1 of 2: replace

```python
    def render(self, card, script, seconds, out):
        self.run([self.a.render, '--card', card, '--bpm', 'mid', '--no-start', '--as-booted', '--stereo', 'sum',
                  '--seconds', str(seconds), '--events', script, '--out', out])

    def drive(self, card, rate, block, script, frames, out, realtime=None, preroll=False):
        cmd = [self.a.drive, self.a.engine, card, str(rate), str(block), script, str(frames), out]
        if realtime is not None:
            cmd += ['realtime', str(realtime)]
        if preroll:
            cmd += ['preroll']
```

with

```python
    def render(self, card, script, seconds, out, stereo='sum'):
        """lichomat-render as the engine runs it: --stereo sum, out1 or out2, or None for its four channels"""
        self.run([self.a.render, '--card', card, '--bpm', 'mid', '--no-start', '--as-booted']
                 + (['--stereo', stereo] if stereo else []) + ['--seconds', str(seconds), '--events', script, '--out', out])

    def drive(self, card, rate, block, script, frames, out, realtime=None, preroll=False, split=False):
        cmd = [self.a.drive, self.a.engine, card, str(rate), str(block), script, str(frames), out]
        if realtime is not None:
            cmd += ['realtime', str(realtime)]
        if preroll:
            cmd += ['preroll']
        if split:
            cmd += ['split']
```

`plugin/tests/plugin_test.py`, edit 2 of 2: replace

```python
# --- the bundle (Task 8) ----------------------------------------------------------------------
```

with

```python
# --- the two outputs and the parameters in the engine (plan E9.4) ----------------------------------

def channels(x, n, k):
    """channel k of the interleaved samples x of n channels"""
    return x[k::n]


@case
def P6_engine_split(t):
    # the engine's four channels: plug_drive's split WAV at 44,140.625 Hz is lichomat-render's four-channel WAV (L1,
    # R1, L2, R2) byte for byte, blocks of 128 and 37; the onset card plays on both outputs (Drum 1 on out 1, the
    # voices on out 2)
    script = write_script(t, 'script.txt')
    frames = blocks_frames(6)
    t.render(t.card('render'), script, 6, t.path('render4.wav'), stereo=None)
    _, n, x = wav(t.path('render4.wav'))
    loud = [sum(1 for v in channels(x, 4, k) if abs(v) >= 4000) for k in range(4)]
    print('the render: %d channels, samples at 4,000 or more per channel %r' % (n, loud))
    check(n == 4 and loud[0] > 1000 and loud[2] > 1000 and loud[3] > 1000, 'both outputs loud: %r' % loud)
    for b in (128, 37):
        out = t.path('split-%d.wav' % b)
        s = t.drive(t.card('s%d' % b), RATE_TEXT, b, script, frames, out, split=True)
        check(s.get('late') == 0 and s.get('dropped') == 0, 'blocks of %d: counters %r' % (b, s))
        d = same_files(t.path('render4.wav'), out)
        check(d is None, 'blocks of %d: the four channels %s' % (b, d))
    print('blocks of 128, 37: the four channels == render')


@case
def P7_engine_split(t):
    # 48 kHz: the engine converts the four channels at once; each output is the renderer's out1 and out2 through the
    # same converter two channels at a time (plug_resample_wav) within 2 LSB, as P7_engine_resampled's sum
    seconds, rate = 6, 48000
    frames = int(blocks_frames(seconds) * fractions.Fraction(rate) / RATE) - 256
    t.drive(t.card('d'), str(rate), 256, write_script(t, 'drive.txt', rate), frames, t.path('drive4.wav'), split=True)
    _, n, d = wav(t.path('drive4.wav'))
    check(n == 4, '%d channels' % n)
    for k, out in ((0, 'out1'), (2, 'out2')):
        t.render(t.card('r' + out), write_script(t, 'render-%s.txt' % out, rate, render=True), seconds,
                 t.path('render-%s.wav' % out), stereo=out)
        t.run([t.a.resample, t.path('render-%s.wav' % out), str(rate), t.path('ref-%s.wav' % out), str(frames)])
        _, _, ref = wav(t.path('ref-%s.wav' % out))
        a = [v for pair in zip(channels(d, 4, k), channels(d, 4, k + 1)) for v in pair]
        check(len(a) == len(ref), '%s: %d and %d samples' % (out, len(a), len(ref)))
        worst = max(abs(x - y) for x, y in zip(a, ref))
        loud = sum(1 for x in a if abs(x) >= 4000)
        print('%s at %d Hz: %d frames, %d samples at 4,000 or more, the largest difference %d LSB' % (out, rate, frames, loud, worst))
        check(worst <= 2 and loud > 1000, '%s: the largest difference %d LSB' % (out, worst))


# --- the bundle (Task 8) ----------------------------------------------------------------------
```

`plugin/CMakeLists.txt`: replace

```cmake
plugin_py_tests(P6_engine_version P6_engine_objects P6_engine_native P6_engine_blocks P6_engine_paced P6_engine_seed P6_engine_epoch
    P6_engine_quit P6_engine_orphan P7_engine_resampled P7_engine_realtime)
```

with

```cmake
plugin_py_tests(P6_engine_version P6_engine_objects P6_engine_native P6_engine_blocks P6_engine_paced P6_engine_seed P6_engine_epoch
    P6_engine_quit P6_engine_orphan P7_engine_resampled P7_engine_realtime P6_engine_split P7_engine_split)
```

- [ ] **Step 2: they fail.** PLUGIN with REGEX `P6_engine_split|P7_engine_split`. Expected: `== plugin build: 0`, then the two new cases fail: `FAIL P6_engine_split: plug_drive exited 1:` and `FAIL P7_engine_split: plug_drive exited 1:`, each with `usage: plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll]`.
- [ ] **Step 3: the code.**

`plugin/tests/plug_drive.c`, edit 1 of 6: replace

```c
 * the VST 3 layer. It starts lichomat-engine on a card, waits until it runs (epoch 0), then
 * plays a script of lichomat-render's usb lines ("<frame>f usb XX ..", host frames) in blocks,
 * and writes the ring's frames [0, FRAMES) as a 16-bit stereo WAV.
 *
 *   plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll]
 *
```

with

```c
 * the VST 3 layer. It starts lichomat-engine on a card, waits until it runs (epoch 0), then
 * plays a script of lichomat-render's usb lines ("<frame>f usb XX ..", host frames) in blocks,
 * and writes the ring's frames [0, FRAMES) as a 16-bit WAV: the stereo sum of the two outputs,
 * as the plugin's Out 1 without Out 2, or with split the four channels L1 R1 L2 R2, as
 * lichomat-render without --stereo (plan E9.4).
 *
 *   plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll] [split]
 *
```

`plugin/tests/plug_drive.c`, edit 2 of 6: replace

```c
int main(int argc, char **argv)
{
	const int realtime = argc == 10 && !strcmp(argv[8], "realtime");
	const int preroll = argc == 9 && !strcmp(argv[8], "preroll");
	if (argc != 8 && !realtime && !preroll)
	{
		fprintf(stderr, "usage: plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll]\n");
		return 1;
	}
	const double rate = strtod(argv[3], NULL);
	const uint32_t block = (uint32_t)strtoul(argv[4], NULL, 10);
	const uint64_t frames = strtoull(argv[6], NULL, 10);
	const uint32_t margin = realtime ? (uint32_t)strtoul(argv[9], NULL, 10) : 0u;
```

with

```c
int main(int argc, char **argv)
{
	int realtime = 0, preroll = 0, split = 0, bad = argc < 8;
	uint32_t margin = 0;
	for (int a = 8; a < argc && !bad; a++)
	{
		if (!strcmp(argv[a], "realtime") && a + 1 < argc)
			realtime = 1, margin = (uint32_t)strtoul(argv[++a], NULL, 10);
		else if (!strcmp(argv[a], "preroll"))
			preroll = 1;
		else if (!strcmp(argv[a], "split"))
			split = 1;
		else
			bad = 1;
	}
	if (bad || (realtime && preroll))
	{
		fprintf(stderr, "usage: plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll] [split]\n");
		return 1;
	}
	const double rate = strtod(argv[3], NULL);
	const uint32_t block = (uint32_t)strtoul(argv[4], NULL, 10);
	const uint64_t frames = strtoull(argv[6], NULL, 10);
```

`plugin/tests/plug_drive.c`, edit 3 of 6: replace

```c
				readInto(s, base + got, (uint32_t)(to - got), ch, got, 0);
				got = to;
			}
			if (end != lastEnd)
```

with

```c
				readInto(s, base + got, (uint32_t)(to - got), ch, got, split);
				got = to;
			}
			if (end != lastEnd)
```

`plugin/tests/plug_drive.c`, edit 4 of 6: replace

```c
					readInto(s, from, take, ch, from, 0);
```

with

```c
					readInto(s, from, take, ch, from, split);
```

`plugin/tests/plug_drive.c`, edit 5 of 6: replace

```c
				readInto(s, base + got, (uint32_t)(to - got), ch, got, 0);
				got = to;
			}
		}
		n += block;
```

with

```c
				readInto(s, base + got, (uint32_t)(to - got), ch, got, split);
				got = to;
			}
		}
		n += block;
```

`plugin/tests/plug_drive.c`, edit 6 of 6: replace

```c
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
		put16(o, (uint16_t)sample(ch[0][i], native));
		put16(o, (uint16_t)sample(ch[1][i], native));
	}
	fclose(o);
```

with

```c
	FILE *o = fopen(argv[7], "wb");
	const unsigned nch = split ? PLUG_AUDIO_CHANNELS : 2u;
	const uint32_t data = (uint32_t)(frames * 2u * nch);
	fwrite("RIFF", 1, 4, o);
	put32(o, 36u + data);
	fwrite("WAVEfmt ", 1, 8, o);
	put32(o, 16);
	put16(o, 1);
	put16(o, nch);
	const uint32_t hz = native ? 44141u : (uint32_t)rate;
	put32(o, hz);
	put32(o, hz * 2u * nch);
	put16(o, 2u * nch);
	put16(o, 16);
	fwrite("data", 1, 4, o);
	put32(o, data);
	for (uint64_t i = 0; i < frames; i++)
		for (unsigned c = 0; c < nch; c++)
			put16(o, (uint16_t)sample(ch[c][i], native));
	fclose(o);
```

- [ ] **Step 4: they pass.** PLUGIN with REGEX `P6_engine_split|P7_engine_split|P6_engine_native|P7_engine_resampled|P6_engine_paced`. Expected: `100% tests passed, 0 tests failed out of 5`; with `-V`: `the render: 4 channels, samples at 4,000 or more per channel [7854, 0, 26853, 26506]`, `blocks of 128, 37: the four channels == render`, `out1 at 48000 Hz: 287800 frames, 8579 samples at 4,000 or more, the largest difference 0 LSB`, `out2 at 48000 Hz: 287800 frames, 57758 samples at 4,000 or more, the largest difference 0 LSB`.
- [ ] **Step 5: the gate.** PLUGIN-CHECK, T = 77. **Light review** of Tasks 1–4.
- [ ] **Step 6: commit and push.**

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && git add plugin/tests/plug_drive.c plugin/tests/plugin_test.py plugin/CMakeLists.txt && git commit -m "plugin Task 4 (E9.34): plug_drive split; the engine's two outputs are the renderer's, byte for byte at 44,140.625 Hz and through the converter at 48 kHz

P6_engine_split: 264,896 frames of four channels == render (blocks of 128 and 37); P7_engine_split: out1 and out2 0 LSB.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && git push origin plugin-v07
```

---

### Task 5: The parameters, their MIDI and their step with the firmware — E9.35

**Files:** Create `plugin/common/plug_params.h`, `plugin/common/plug_params.c`, `plugin/tests/test_params.c`; modify `plugin/CMakeLists.txt`.

Pure C with no VST3 and no firmware header, in `plug_common` (the plugin's and the test host's) and `plug_common_san` (the engine's and the tests'): the table (B-10, "The parameters"), `plug_paramMidi()` (B-5), the step (B-7, "The values: host, firmware, no loop") and the state's `LCHV` block (B-9). `test_params.c` checks every rule with exact values.

- [ ] **Step 1: the failing tests.**

Create `plugin/tests/test_params.c`:

```c
/*
 * test_params.c
 *
 * P12: the plugin's parameters (plan E9.4 Task 5): the table, their MIDI, the step of the host's
 * values and the firmware's (plug_paramsHost(), plug_paramsReport()) and their block in the state.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <math.h>
#include <string.h>

#include "plug_params.h"
#include "seqc_test.h"

#define CH 3u
#define SETTLE 1000u

/* indices in plug_params[] */
enum { I_PATTERN = 0, I_VOL1 = 1, I_PAN4 = 10, I_DJ = 26, I_REV_DECAY = 28, I_FX = 29, I_OUT1 = 30, I_OUT6 = 35 };

static void fwValues(uint8_t *v)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		v[i] = plug_params[i].def;
}

TEST(P12_params_table)
{
	/* ids unique, none 0 (the Engine's); the firmware's parameter of a CC n is n - 1, of an NRPN n
	 * n + 128; defaults within range */
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		const plug_param_t *p = &plug_params[i];
		ASSERT_TRUE(p->id != 0);
		ASSERT_EQ(plug_paramIndex(p->id), (int)i);
		ASSERT_TRUE(p->def <= p->max && p->max > 0);
		if (p->kind == PLUG_PK_CC)
			ASSERT_EQ(p->par, p->number - 1u);
		else if (p->kind == PLUG_PK_NRPN)
			ASSERT_EQ(p->par, p->number + 128u);
		else
			ASSERT_EQ(p->par, PLUG_PAR_PATTERN);
		for (unsigned j = 0; j < i; j++)
			ASSERT_TRUE(plug_params[j].id != p->id);
	}
	ASSERT_EQ(plug_paramIndex(0), -1);
	ASSERT_EQ(plug_paramIndex(9999), -1);
	ASSERT_EQ(plug_params[I_VOL1].number, 89);
	ASSERT_EQ(plug_params[I_PAN4].number, 100);	/* CC 98 and 99 are the NRPN's */
	ASSERT_EQ(plug_params[I_DJ].number, 96);
	ASSERT_EQ(plug_params[I_FX].max, 100);
	ASSERT_EQ(plug_params[I_PATTERN].max, 63);
	/* the voices' outputs: NRPN 87-92, firmware parameters 215-220, seven named values */
	ASSERT_TRUE(plug_params[I_OUT1].number == 87 && plug_params[I_OUT1].par == 215 && plug_params[I_OUT6].par == 220);
	ASSERT_TRUE(plug_params[I_OUT1].max == 6 && plug_params[I_OUT1].names != NULL);
	ASSERT_TRUE(!strcmp(plug_params[I_OUT1].names[1], "St2") && !strcmp(plug_params[I_OUT6].names[6], "FX"));
	ASSERT_TRUE(plug_params[I_VOL1].names == NULL && plug_params[I_PATTERN].names == NULL);
	/* the host's 0-1 to the nearest value */
	ASSERT_EQ(plug_paramValue(I_VOL1, 0.0), 0);
	ASSERT_EQ(plug_paramValue(I_VOL1, 1.0), 127);
	ASSERT_EQ(plug_paramValue(I_VOL1, 40.0 / 127.0), 40);
	ASSERT_EQ(plug_paramValue(I_VOL1, 0.5), 64);
	ASSERT_EQ(plug_paramValue(I_VOL1, 1.5), 127);
	ASSERT_EQ(plug_paramValue(I_VOL1, -1.0), 0);
	ASSERT_EQ(plug_paramValue(I_VOL1, NAN), 0);
	ASSERT_EQ(plug_paramValue(I_PATTERN, 5.0 / 63.0), 5);
	ASSERT_EQ(plug_paramValue(I_FX, 0.5), 50);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		for (unsigned v = 0; v <= plug_params[i].max; v++)
			ASSERT_EQ(plug_paramValue(i, (double)v / plug_params[i].max), v);
}

TEST(P12_params_midi)
{
	plug_midi_t m[3];
	ASSERT_EQ(plug_paramMidi(I_VOL1, 40, CH, 7, m), 1);
	ASSERT_TRUE(m[0].offset == 7 && m[0].len == 3 && m[0].b[0] == 0xB3 && m[0].b[1] == 89 && m[0].b[2] == 40);
	ASSERT_EQ(plug_paramMidi(I_REV_DECAY, 127, 15, 0, m), 3);
	ASSERT_TRUE(m[0].b[0] == 0xBF && m[0].b[1] == 99 && m[0].b[2] == 0);
	ASSERT_TRUE(m[1].b[0] == 0xBF && m[1].b[1] == 98 && m[1].b[2] == 116);
	ASSERT_TRUE(m[2].b[0] == 0xBF && m[2].b[1] == 6 && m[2].b[2] == 127);
	ASSERT_EQ(plug_paramMidi(I_PATTERN, 63, 0x13, 5, m), 1);	/* the channel's low 4 bits */
	ASSERT_TRUE(m[0].offset == 5 && m[0].len == 2 && m[0].b[0] == 0xC3 && m[0].b[1] == 63);
}

TEST(P12_params_host)
{
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	plug_midi_t out[16];
	/* in the queues' order; the MIDI in frame order, stable */
	const plug_paramPoint_t a[] = { { I_DJ, 50, 20 }, { I_DJ, 90, 21 }, { I_VOL1, 10, 40 }, { I_VOL1, 50, 41 } };
	unsigned n = plug_paramsHost(&ps, a, 4, 1000, CH, out, 0, 16);
	ASSERT_EQ(n, 8);
	ASSERT_TRUE(out[0].offset == 10 && out[0].b[1] == 89 && out[0].b[2] == 40);
	ASSERT_TRUE(out[1].offset == 50 && out[1].b[1] == 99);
	ASSERT_TRUE(out[3].offset == 50 && out[3].b[1] == 6 && out[3].b[2] == 20);
	ASSERT_TRUE(out[4].offset == 50 && out[4].b[1] == 89 && out[4].b[2] == 41);
	ASSERT_TRUE(out[7].offset == 90 && out[7].b[2] == 21);
	ASSERT_EQ(ps.p[I_VOL1].want, 41);
	ASSERT_EQ(ps.p[I_VOL1].sent, 41);
	ASSERT_EQ(ps.p[I_VOL1].sentFrame, 1050);
	ASSERT_EQ(ps.p[I_VOL1].shown, 1);
	/* the value the engine has already: no MIDI */
	const plug_paramPoint_t same[] = { { I_VOL1, 0, 41 } };
	ASSERT_EQ(plug_paramsHost(&ps, same, 1, 2000, CH, out, 0, 16), 0);
	/* no channel: the value is the host's, nothing goes to the engine */
	const plug_paramPoint_t pan[] = { { I_PAN4, 3, 9 } };
	ASSERT_EQ(plug_paramsHost(&ps, pan, 1, 3000, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_PAN4].want == 9 && ps.p[I_PAN4].sent == -1 && ps.p[I_PAN4].shown == 1);
	/* full: an NRPN's three messages all or none */
	const plug_paramPoint_t two[] = { { I_VOL1, 0, 1 }, { I_DJ, 0, 2 } };
	ASSERT_EQ(plug_paramsHost(&ps, two, 2, 4000, CH, out, 0, 3), 1);
	ASSERT_EQ(ps.p[I_DJ].sent, 21);	/* left out: still the last it got */
	/* a value beyond the parameter's range is its max; an index beyond the table is left out */
	const plug_paramPoint_t big[] = { { I_FX, 0, 120 }, { PLUG_PARAM_COUNT, 0, 1 } };
	ASSERT_EQ(plug_paramsHost(&ps, big, 2, 5000, CH, out, 0, 16), 3);
	ASSERT_EQ(out[2].b[2], 100);
}

TEST(P12_params_first_report)
{
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	plug_paramsEngine(&ps, 500);
	uint8_t fw[PLUG_PARAM_COUNT];
	fwValues(fw);
	fw[I_FX] = PLUG_VALUE_NONE;
	fw[I_OUT6] = 9;	/* beyond the outputs' 0-6: taken as 6 */
	plug_midi_t out[16];
	/* an earlier engine's report changes nothing */
	ASSERT_EQ(plug_paramsReport(&ps, fw, 499, 600, CH, out, 0, 16), 0);
	ASSERT_EQ(ps.synced, 0);
	/* the host set Drum 1's volume before the engine ran (no channel then) and the DJ filter to the
	 * engine's own value; its first report: the volume goes to it, the others are the host's to get */
	const plug_paramPoint_t a[] = { { I_VOL1, 0, 40 }, { I_DJ, 0, plug_params[I_DJ].def } };
	ASSERT_EQ(plug_paramsHost(&ps, a, 2, 100, PLUG_VALUE_NONE, out, 0, 16), 0);
	ASSERT_EQ(plug_paramsReport(&ps, fw, 500, 600, CH, out, 0, 16), 1);
	ASSERT_TRUE(out[0].offset == 0 && out[0].b[1] == 89 && out[0].b[2] == 40);
	ASSERT_TRUE(ps.synced && ps.p[I_VOL1].sent == 40 && ps.p[I_VOL1].sentFrame == 600);
	ASSERT_EQ(ps.p[I_DJ].sent, plug_params[I_DJ].def);
	unsigned from = 0, shows = 0;
	uint8_t v;
	for (int i; (i = plug_paramsToShow(&ps, &from, &v)) >= 0; shows++)
	{
		ASSERT_TRUE(i != I_VOL1 && i != I_DJ && i != I_FX);
		ASSERT_EQ(v, i == I_OUT6 ? 6 : plug_params[i].def);
		ps.p[i].shown = 1;
	}
	ASSERT_EQ(shows, PLUG_PARAM_COUNT - 3);
	ASSERT_EQ(ps.p[I_FX].want, -1);	/* the engine gave no value */
	ASSERT_EQ(ps.p[I_OUT6].want, 6);
	/* a value the host gave to this engine already is not given again */
	plug_paramsEngine(&ps, 700);
	const plug_paramPoint_t b[] = { { I_VOL1, 5, 41 } };
	ASSERT_EQ(plug_paramsHost(&ps, b, 1, 700, CH, out, 0, 16), 1);
	ASSERT_EQ(plug_paramsReport(&ps, fw, 700, 700, CH, out, 0, 16), 0);
}

TEST(P12_params_own_change)
{
	/* after the first report: a value the firmware changed by itself goes to the host, but not one the
	 * host gave less than SETTLE frames before the report's frame */
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	plug_paramsEngine(&ps, 0);
	uint8_t fw[PLUG_PARAM_COUNT];
	fwValues(fw);
	plug_midi_t out[16];
	ASSERT_EQ(plug_paramsReport(&ps, fw, 0, 0, CH, out, 0, 16), 0);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		ps.p[i].shown = 1;
	const plug_paramPoint_t a[] = { { I_VOL1, 0, 40 } };
	ASSERT_EQ(plug_paramsHost(&ps, a, 1, 10000, CH, out, 0, 16), 1);
	/* the report before the engine took it, and a kit loaded at once: the volume is in flight */
	fw[I_PAN4] = 5;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 10000 + SETTLE - 1, 11000, CH, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].want == 40 && ps.p[I_VOL1].shown == 1);
	ASSERT_TRUE(ps.p[I_PAN4].want == 5 && ps.p[I_PAN4].sent == 5 && ps.p[I_PAN4].shown == 0);
	/* it took it: nothing to tell */
	fw[I_VOL1] = 40;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 10000 + SETTLE - 1, 11000, CH, out, 0, 16), 0);
	ASSERT_EQ(ps.p[I_VOL1].shown, 1);
	/* the firmware sets it back (a kit) after the settle time: the host gets 127 */
	fw[I_VOL1] = 127;
	ASSERT_EQ(plug_paramsReport(&ps, fw, 10000 + SETTLE, 12000, CH, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].want == 127 && ps.p[I_VOL1].sent == 127 && ps.p[I_VOL1].shown == 0);
	/* the host's echo of it is not sent back: no loop */
	const plug_paramPoint_t echo[] = { { I_VOL1, 0, 127 }, { I_PAN4, 0, 5 } };
	ASSERT_EQ(plug_paramsHost(&ps, echo, 2, 12128, CH, out, 0, 16), 0);
	ASSERT_TRUE(ps.p[I_VOL1].shown == 1 && ps.p[I_PAN4].shown == 1);
	/* and the next report, the same values, changes nothing */
	ASSERT_EQ(plug_paramsReport(&ps, fw, 12200, 12256, CH, out, 0, 16), 0);
	unsigned from = 0;
	uint8_t v;
	ASSERT_EQ(plug_paramsToShow(&ps, &from, &v), -1);
}

TEST(P12_params_restore)
{
	/* values from a state: the host is to get them, the engine too when it has others */
	plug_params_t ps;
	plug_paramsInit(&ps, SETTLE);
	int values[PLUG_PARAM_COUNT];
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		values[i] = -1;
	values[I_VOL1] = 40;
	values[I_PATTERN] = 3;
	values[I_FX] = 101;	/* beyond its range: none */
	plug_paramsRestore(&ps, values);
	ASSERT_TRUE(ps.p[I_VOL1].want == 40 && !ps.p[I_VOL1].shown && ps.p[I_VOL1].sent == -1);
	ASSERT_EQ(ps.p[I_FX].want, -1);
	plug_paramsEngine(&ps, 0);
	uint8_t fw[PLUG_PARAM_COUNT];
	fwValues(fw);
	plug_midi_t out[16];
	ASSERT_EQ(plug_paramsReport(&ps, fw, 0, 0, CH, out, 0, 16), 2);
	ASSERT_TRUE(out[0].b[0] == 0xC3 && out[0].b[1] == 3);	/* the table's order: the pattern first */
	ASSERT_TRUE(out[1].b[1] == 89 && out[1].b[2] == 40);
	unsigned from = 0;
	uint8_t v;
	ASSERT_EQ(plug_paramsToShow(&ps, &from, &v), I_PATTERN);
	ASSERT_EQ(v, 3);
	ASSERT_EQ(plug_paramsToShow(&ps, &from, &v), I_VOL1);
	ASSERT_EQ(v, 40);
	/* a restore while the engine runs: its next report is a first one again */
	plug_paramsRestore(&ps, values);
	ASSERT_EQ(ps.synced, 0);
}

/* a stream in memory */
static uint8_t buf[1024];
static size_t at, len;

static int put(void *ctx, const void *p, size_t n)
{
	(void)ctx;
	if (len + n > sizeof buf)
		return -1;
	memcpy(buf + len, p, n);
	len += n;
	return 0;
}

static int get(void *ctx, void *p, size_t n)
{
	(void)ctx;
	if (at + n > len)
		return -1;
	memcpy(p, buf + at, n);
	at += n;
	return 0;
}

TEST(P12_params_state)
{
	int values[PLUG_PARAM_COUNT], back[PLUG_PARAM_COUNT];
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		values[i] = -1;
	values[I_VOL1] = 40;
	values[I_FX] = 100;
	values[I_PAN4] = 128;	/* beyond its range: left out */
	len = 0;
	ASSERT_EQ(plug_paramsWrite(put, NULL, values), 0);
	ASSERT_EQ(len, 12 + 2 * 5);
	ASSERT_TRUE(!memcmp(buf, "LCHV\1\0\0\0\2\0\0\0\12\0\0\0\50", 17));
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), 0);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		ASSERT_EQ(back[i], i == I_VOL1 ? 40 : i == I_FX ? 100 : -1);
	/* the prototype's state ends after the card: no values */
	len = at = 0;
	back[0] = 7;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), 0);
	ASSERT_EQ(back[0], -1);
	/* something else after the card: no values */
	memcpy(buf, "ABCDxxxx", 8);
	len = 8;
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), 0);
	/* another version, a count beyond 4,096, a block cut short: damaged */
	memcpy(buf, "LCHV\2\0\0\0\0\0\0\0", 12);
	len = 12;
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), -1);
	memcpy(buf, "LCHV\1\0\0\0\1\20\0\0", 12);
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), -1);
	memcpy(buf, "LCHV\1\0\0\0\2\0\0\0\12\0\0\0\50", 17);
	len = 17;
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), -1);
	/* an id this version does not know, and a value beyond a known one's range: left out */
	memcpy(buf, "LCHV\1\0\0\0\2\0\0\0\77\77\0\0\5\12\0\0\0\200", 22);
	len = 22;
	at = 0;
	ASSERT_EQ(plug_paramsRead(get, NULL, back), 0);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		ASSERT_EQ(back[i], -1);
}

TEST_MAIN()
```

`plugin/CMakeLists.txt`: replace

```cmake
target_sources(plug_common PRIVATE common/plug_time.c)
target_sources(plug_common_san PRIVATE common/plug_time.c)
plugin_add_test(test_time IDS P5_time_native P5_time_integer P5_time_covering P5_time_latency P5_time_margin P5_time_wide)
```

with

```cmake
target_sources(plug_common PRIVATE common/plug_time.c)
target_sources(plug_common_san PRIVATE common/plug_time.c)
plugin_add_test(test_time IDS P5_time_native P5_time_integer P5_time_covering P5_time_latency P5_time_margin P5_time_wide)

# --- the parameters (plan E9.4 Task 5) --------------------------------------------------------
plugin_add_test(test_params IDS P12_params_table P12_params_midi P12_params_host P12_params_first_report P12_params_own_change
    P12_params_restore P12_params_state)
```

- [ ] **Step 2: they fail.** PLUGIN with REGEX `P12_`. Expected: `== plugin build: 1`; `build/plugin.log` has `test_params.c:17:10: fatal error: 'plug_params.h' file not found`.
- [ ] **Step 3: the code.**

Create `plugin/common/plug_params.h`:

```c
/*
 * plug_params.h
 *
 * The plugin's automatable parameters (plan E9.4 Task 5): each one a firmware parameter that the
 * firmware takes over MIDI, as from an external controller on its global channel: a CC, an NRPN
 * (CC 99, 98 and 6) or, for the pattern, a Program Change. The engine reports the firmware's
 * values back (plug_shm.h, values), and plug_paramsSync() keeps the host's values and the
 * firmware's in step without a loop: a value the host sets goes to the firmware once; a value the
 * firmware takes by itself (a kit or a pattern loaded) goes to the host once; a new engine gets
 * the host's values. Pure functions of their arguments: the processor and the test host run the
 * same code.
 *
 * The ids are the VST3 parameter ids: a host keys a set's automation and state by them. Never
 * change one; a new parameter takes a new id, and a removed one's id is not used again.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef PLUG_PARAMS_H_
#define PLUG_PARAMS_H_

#include <stddef.h>
#include <stdint.h>

#include "plug_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_PARAM_COUNT 36u
#define PLUG_VALUE_NONE 0xFFu		/**< no value: the engine cannot read it, or no channel */
#define PLUG_PAR_PATTERN 0xFFFFu	/**< the firmware's next pattern, not a parameter of Parameters.h */

/** how a parameter reaches the firmware */
enum
{
	PLUG_PK_CC = 0,			/**< CC number, value */
	PLUG_PK_NRPN = 1,		/**< CC 99 number >> 7, CC 98 number & 127, CC 6 value */
	PLUG_PK_PROGRAM = 2		/**< Program Change value */
};

typedef struct plug_param
{
	uint32_t id;			/**< the VST3 parameter id */
	const char *name;		/**< "Drum 1 Volume" */
	const char *shortName;	/**< "D1 Vol" */
	uint8_t kind;			/**< PLUG_PK_* */
	uint16_t number;		/**< the CC or the NRPN; 0 for the Program Change */
	uint16_t par;			/**< the firmware's parameter: CC - 1, NRPN + 128 (MidiParser.c), or PLUG_PAR_PATTERN */
	uint8_t max;			/**< the values are 0..max */
	int8_t offset;			/**< shown as value + offset: the device's own numbers */
	uint8_t def;			/**< the default: the init kit's value */
	const char *const *names;	/**< the values' names (max + 1 of them), or NULL: shown as numbers */
} plug_param_t;

extern const plug_param_t plug_params[PLUG_PARAM_COUNT];

/** the index of the parameter with VST3 id id, or -1 */
int plug_paramIndex(uint32_t id);

/** a host's normalized value (0-1) of parameter i as its value 0..max, the nearest */
uint8_t plug_paramValue(unsigned i, double normalized);

/** The MIDI messages that set parameter i to value v on channel ch (0-15) at offset, into out:
 * returns the count, 1 or 3 */
unsigned plug_paramMidi(unsigned i, uint8_t v, uint8_t ch, uint32_t offset, plug_midi_t *out);

/** one parameter's step with the firmware */
typedef struct plug_paramSync
{
	int want;				/**< the value the host has, or is to get (shown 0); -1: none yet */
	int shown;				/**< 1: the host has want */
	int sent;				/**< the value last given to the engine; -1: none */
	uint64_t sentFrame;		/**< the host frame it was given at */
} plug_paramSync_t;

/** every parameter's step, and the engine they follow */
typedef struct plug_params
{
	plug_paramSync_t p[PLUG_PARAM_COUNT];
	uint64_t epoch;			/**< the running engine's first frame */
	int synced;				/**< 1 once a report of that engine was taken */
	uint64_t settle;		/**< host frames a value given to the engine may take to show in its report */
} plug_params_t;

/** nothing known; settle in host frames (the processor: a quarter of a second) */
void plug_paramsInit(plug_params_t *ps, uint64_t settle);

/** A new engine runs from host frame epoch: its first report decides, for every parameter, whether
 * the host's value goes to it (the host has one) or its value goes to the host. */
void plug_paramsEngine(plug_params_t *ps, uint64_t epoch);

/** values restored from a state (or -1, none): the host is to get them and a new engine too */
void plug_paramsRestore(plug_params_t *ps, const int *values);

/** one point of a host's parameter change */
typedef struct plug_paramPoint
{
	unsigned index;			/**< plug_params[] */
	uint32_t offset;		/**< the frame in the block */
	uint8_t value;			/**< plug_paramValue() */
} plug_paramPoint_t;

/** The host's changes of a block, in the order its queues give them (each queue's points in
 * order), at host frame frame0: each value that differs from the one last given to the engine
 * goes to it, as MIDI on channel ch, inserted into out[0..n) in frame order (after the messages of
 * the same offset). Returns the new count, at most max (a parameter's messages all or none); with
 * ch PLUG_VALUE_NONE (no channel) none is sent. */
unsigned plug_paramsHost(plug_params_t *ps, const plug_paramPoint_t *pts, unsigned npts, uint64_t frame0, uint8_t ch,
	plug_midi_t *out, unsigned n, unsigned max);

/** The engine's report, the firmware's values at host frame frame (values[i], PLUG_VALUE_NONE:
 * unknown, a value beyond the range: its max), read at the block of host frame frame0. A report
 * from before the epoch is an earlier engine's and changes nothing. The first report of an
 * engine gives it the host's values that differ (as MIDI at offset 0, inserted into out[0..n) as
 * plug_paramsHost() does); any later difference that is not a value given to the engine less
 * than settle frames before is the firmware's own, and the host is to get it. Returns the new
 * count. */
unsigned plug_paramsReport(plug_params_t *ps, const uint8_t *values, uint64_t frame, uint64_t frame0, uint8_t ch,
	plug_midi_t *out, unsigned n, unsigned max);

/** the next parameter whose value the host is to get (not shown), from index *from on: its index
 * and value, *from past it; -1 when none is left. The caller sets shown once the host has it. */
int plug_paramsToShow(const plug_params_t *ps, unsigned *from, uint8_t *value);

/* --- the values in the plugin's state, after the card archive -------------------------------- */

#define PLUG_STATE_VALUES_MAGIC "LCHV"
#define PLUG_STATE_VALUES_VERSION 1u

/** writes n bytes; returns 0, or -1 (plug_archive.h's plug_put_t) */
typedef int (*plug_paramsPut_t)(void *ctx, const void *p, size_t n);
/** reads exactly n bytes; returns 0, or -1 (plug_archive.h's plug_get_t) */
typedef int (*plug_paramsGet_t)(void *ctx, void *p, size_t n);

/** values[PLUG_PARAM_COUNT] (-1: none, left out) as "LCHV", version 1 (u32), the count (u32), then
 * the id (u32) and the value (u8) of each, little-endian. Returns 0, or -1 when put fails. */
int plug_paramsWrite(plug_paramsPut_t put, void *ctx, const int *values);

/** Read them into values (-1 for each one not there). Returns 0, also for a stream that ends
 * before the magic (a state of the prototype) or holds something else after the archive (values
 * none); -1 for an LCHV block that is damaged or of another version. Ids this version does not
 * know are left out. */
int plug_paramsRead(plug_paramsGet_t get, void *ctx, int *values);

#ifdef __cplusplus
}
#endif

#endif /* PLUG_PARAMS_H_ */
```

Create `plugin/common/plug_params.c`:

```c
/*
 * plug_params.c
 *
 * The plugin's automatable parameters (plan E9.4 Task 5); see plug_params.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_params.h"

#include <math.h>
#include <string.h>

/* The firmware's MIDI implementation (MidiMessages.h, MidiParser.c): a CC n sets parameter n - 1,
 * NRPN n parameter n + 128; Parameters.h numbers them, and eng_fw.c checks this table against it
 * when the engine starts. def: the init kit's values (lichomat-render --seed-card). */
#define CC(id, name, sn, cc, max, off, def) { id, name, sn, PLUG_PK_CC, cc, (cc) - 1, max, off, def, NULL }
#define NRPN(id, name, sn, nr, max, def) { id, name, sn, PLUG_PK_NRPN, nr, (nr) + 128, max, 0, def, NULL }

/* a voice's output (MenuText.h, outputNames): both of out 1 or of out 2, one side of either, or the FX bus */
static const char *const plug_outputs[] = { "St1", "St2", "L1", "R1", "L2", "R2", "FX" };
#define OUT(id, name, sn, nr, def) { id, name, sn, PLUG_PK_NRPN, nr, (nr) + 128, 6, 0, def, plug_outputs }

const plug_param_t plug_params[PLUG_PARAM_COUNT] = {
	{ 1, "Pattern", "Pattern", PLUG_PK_PROGRAM, 0, PLUG_PAR_PATTERN, 63, 1, 0, NULL },
	CC(10, "Drum 1 Volume", "D1 Vol", 89, 127, 0, 127),
	CC(11, "Drum 2 Volume", "D2 Vol", 90, 127, 0, 127),
	CC(12, "Drum 3 Volume", "D3 Vol", 91, 127, 0, 127),
	CC(13, "Snare Volume", "SN Vol", 92, 127, 0, 127),
	CC(14, "Cymbal Volume", "CY Vol", 93, 127, 0, 127),
	CC(15, "Hi-hat Volume", "HH Vol", 94, 127, 0, 127),
	CC(20, "Drum 1 Pan", "D1 Pan", 95, 127, -63, 63),
	CC(21, "Drum 2 Pan", "D2 Pan", 96, 127, -63, 63),
	CC(22, "Drum 3 Pan", "D3 Pan", 97, 127, -63, 63),
	CC(23, "Snare Pan", "SN Pan", 100, 127, -63, 63),
	CC(24, "Cymbal Pan", "CY Pan", 101, 127, -63, 63),
	CC(25, "Hi-hat Pan", "HH Pan", 102, 127, -63, 63),
	CC(30, "Drum 1 Filter", "D1 Filt", 38, 127, 0, 127),
	CC(31, "Drum 2 Filter", "D2 Filt", 39, 127, 0, 127),
	CC(32, "Drum 3 Filter", "D3 Filt", 40, 127, 0, 127),
	CC(33, "Snare Filter", "SN Filt", 41, 127, 0, 127),
	CC(34, "Cymbal Filter", "CY Filt", 42, 127, 0, 68),
	CC(35, "Hi-hat Filter", "HH Filt", 43, 127, 0, 104),
	CC(40, "Drum 1 Decay", "D1 Dec", 51, 127, 0, 21),
	CC(41, "Drum 2 Decay", "D2 Dec", 53, 127, 0, 13),
	CC(42, "Drum 3 Decay", "D3 Dec", 55, 127, 0, 3),
	CC(43, "Snare Decay", "SN Dec", 57, 127, 0, 10),
	CC(44, "Cymbal Decay", "CY Dec", 59, 127, 0, 107),
	CC(45, "Hi-hat Closed Decay", "HH Dec", 61, 127, 0, 5),
	CC(46, "Hi-hat Open Decay", "HH Open", 62, 127, 0, 17),
	NRPN(50, "DJ Filter", "DJ Filt", 96, 127, 64),
	NRPN(51, "Reverb Level", "Rev Lvl", 98, 127, 0),
	NRPN(52, "Reverb Decay", "Rev Dec", 116, 127, 64),
	NRPN(53, "FX Amount", "FX Amt", 108, 100, 0),
	OUT(60, "Drum 1 Output", "D1 Out", 87, 0),
	OUT(61, "Drum 2 Output", "D2 Out", 88, 0),
	OUT(62, "Drum 3 Output", "D3 Out", 89, 0),
	OUT(63, "Snare Output", "SN Out", 90, 0),
	OUT(64, "Cymbal Output", "CY Out", 91, 0),
	OUT(65, "Hi-hat Output", "HH Out", 92, 0),
};

int plug_paramIndex(uint32_t id)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		if (plug_params[i].id == id)
			return (int)i;
	return -1;
}

uint8_t plug_paramValue(unsigned i, double normalized)
{
	if (!(normalized > 0.0))
		return 0;	/* also not a number */
	const unsigned max = plug_params[i].max;
	const double v = floor(normalized * max + 0.5);
	return (uint8_t)(v >= max ? max : v);
}

unsigned plug_paramMidi(unsigned i, uint8_t v, uint8_t ch, uint32_t offset, plug_midi_t *out)
{
	const plug_param_t *p = &plug_params[i];
	const uint8_t cc = (uint8_t)(0xB0u | (ch & 15u));
	if (p->kind == PLUG_PK_PROGRAM)
	{
		out[0] = (plug_midi_t){ offset, 2, { (uint8_t)(0xC0u | (ch & 15u)), (uint8_t)(v & 0x7Fu), 0 } };
		return 1;
	}
	if (p->kind == PLUG_PK_NRPN)
	{
		out[0] = (plug_midi_t){ offset, 3, { cc, 99, (uint8_t)(p->number >> 7) } };
		out[1] = (plug_midi_t){ offset, 3, { cc, 98, (uint8_t)(p->number & 0x7Fu) } };
		out[2] = (plug_midi_t){ offset, 3, { cc, 6, (uint8_t)(v & 0x7Fu) } };
		return 3;
	}
	out[0] = (plug_midi_t){ offset, 3, { cc, (uint8_t)p->number, (uint8_t)(v & 0x7Fu) } };
	return 1;
}

void plug_paramsInit(plug_params_t *ps, uint64_t settle)
{
	memset(ps, 0, sizeof *ps);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		ps->p[i].want = -1;
		ps->p[i].sent = -1;
	}
	ps->settle = settle;
}

void plug_paramsEngine(plug_params_t *ps, uint64_t epoch)
{
	ps->epoch = epoch;
	ps->synced = 0;
}

void plug_paramsRestore(plug_params_t *ps, const int *values)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		plug_paramSync_t *p = &ps->p[i];
		const int v = values[i];
		p->want = v >= 0 && v <= plug_params[i].max ? v : -1;
		p->shown = 0;
		p->sent = -1;
		p->sentFrame = 0;
	}
	ps->synced = 0;
}

/* m into out[0..*n) after the messages of its offset or an earlier one; 0 when out is full */
static int insertMidi(plug_midi_t *out, unsigned *n, unsigned max, const plug_midi_t *m)
{
	if (*n >= max)
		return 0;
	unsigned k = (*n)++;
	while (k > 0 && out[k - 1].offset > m->offset)
	{
		out[k] = out[k - 1];
		k--;
	}
	out[k] = *m;
	return 1;
}

/* parameter i to v at offset into out, all its messages or none */
static int send(plug_params_t *ps, unsigned i, uint8_t v, uint32_t offset, uint64_t frame0, uint8_t ch,
	plug_midi_t *out, unsigned *n, unsigned max)
{
	plug_midi_t m[3];
	const unsigned k = plug_paramMidi(i, v, ch, offset, m);
	if (*n + k > max)
		return 0;
	for (unsigned j = 0; j < k; j++)
		(void)insertMidi(out, n, max, &m[j]);
	ps->p[i].sent = v;
	ps->p[i].sentFrame = frame0 + offset;
	return 1;
}

unsigned plug_paramsHost(plug_params_t *ps, const plug_paramPoint_t *pts, unsigned npts, uint64_t frame0, uint8_t ch,
	plug_midi_t *out, unsigned n, unsigned max)
{
	for (unsigned j = 0; j < npts; j++)
	{
		const unsigned i = pts[j].index;
		if (i >= PLUG_PARAM_COUNT)
			continue;
		plug_paramSync_t *p = &ps->p[i];
		const uint8_t v = pts[j].value > plug_params[i].max ? plug_params[i].max : pts[j].value;
		p->want = v;
		p->shown = 1;
		if (v == p->sent || ch == PLUG_VALUE_NONE)
			continue;
		(void)send(ps, i, v, pts[j].offset, frame0, ch, out, &n, max);
	}
	return n;
}

unsigned plug_paramsReport(plug_params_t *ps, const uint8_t *values, uint64_t frame, uint64_t frame0, uint8_t ch,
	plug_midi_t *out, unsigned n, unsigned max)
{
	if (frame < ps->epoch)
		return n;	/* an earlier engine's */
	const int first = !ps->synced;
	ps->synced = 1;
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		if (values[i] == PLUG_VALUE_NONE)
			continue;
		/* a value beyond the range (a card's own) is its max, as the host can show it */
		const uint8_t fw = values[i] > plug_params[i].max ? plug_params[i].max : values[i];
		plug_paramSync_t *p = &ps->p[i];
		if (p->want == fw)
		{
			if (first)
				p->sent = fw;	/* the new engine has it */
			continue;
		}
		if (first && p->want >= 0)
		{
			/* a new engine: the host's value, unless it was given to this engine already */
			if (!(p->sent == p->want && p->sentFrame >= ps->epoch) && ch != PLUG_VALUE_NONE)
				(void)send(ps, i, (uint8_t)p->want, 0, frame0, ch, out, &n, max);
			continue;
		}
		if (!first && p->sent >= 0 && frame < p->sentFrame + ps->settle)
			continue;	/* a value given to the engine, not in its report yet */
		p->want = fw;	/* the firmware's own: the host is to get it */
		p->sent = fw;
		p->shown = 0;
	}
	return n;
}

int plug_paramsToShow(const plug_params_t *ps, unsigned *from, uint8_t *value)
{
	for (unsigned i = *from; i < PLUG_PARAM_COUNT; i++)
		if (!ps->p[i].shown && ps->p[i].want >= 0)
		{
			*value = (uint8_t)ps->p[i].want;
			*from = i + 1u;
			return (int)i;
		}
	*from = PLUG_PARAM_COUNT;
	return -1;
}

static void le32(uint8_t *b, uint32_t v)
{
	for (unsigned i = 0; i < 4; i++)
		b[i] = (uint8_t)(v >> (8u * i));
}

static uint32_t get32(const uint8_t *b)
{
	return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

int plug_paramsWrite(plug_paramsPut_t put, void *ctx, const int *values)
{
	uint8_t b[12 + 5 * PLUG_PARAM_COUNT];
	uint32_t count = 0;
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		if (values[i] >= 0 && values[i] <= plug_params[i].max)
		{
			le32(&b[12 + 5 * count], plug_params[i].id);
			b[12 + 5 * count + 4] = (uint8_t)values[i];
			count++;
		}
	memcpy(b, PLUG_STATE_VALUES_MAGIC, 4);
	le32(&b[4], PLUG_STATE_VALUES_VERSION);
	le32(&b[8], count);
	return put(ctx, b, 12 + 5 * count);
}

int plug_paramsRead(plug_paramsGet_t get, void *ctx, int *values)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		values[i] = -1;
	uint8_t h[12];
	if (get(ctx, h, 4) != 0 || memcmp(h, PLUG_STATE_VALUES_MAGIC, 4) != 0)
		return 0;	/* the prototype's state, or not ours: no values */
	if (get(ctx, h + 4, 8) != 0 || get32(h + 4) != PLUG_STATE_VALUES_VERSION || get32(h + 8) > 4096u)
		return -1;
	const uint32_t count = get32(h + 8);
	for (uint32_t k = 0; k < count; k++)
	{
		uint8_t e[5];
		if (get(ctx, e, 5) != 0)
			return -1;
		const int i = plug_paramIndex(get32(e));
		if (i >= 0 && e[4] <= plug_params[i].max)
			values[i] = e[4];
	}
	return 0;
}
```

`plugin/CMakeLists.txt`: replace

```cmake
# --- the parameters (plan E9.4 Task 5) --------------------------------------------------------
plugin_add_test(test_params IDS
```

with

```cmake
# --- the parameters (plan E9.4 Task 5) --------------------------------------------------------
target_sources(plug_common PRIVATE common/plug_params.c)
target_sources(plug_common_san PRIVATE common/plug_params.c)
plugin_add_test(test_params IDS
```

- [ ] **Step 4: they pass.** PLUGIN with REGEX `P12_`. Expected: `100% tests passed, 0 tests failed out of 7`.
- [ ] **Step 5: the gate.** PLUGIN-CHECK, T = 84.
- [ ] **Step 6: commit and push.**

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && git add plugin/common/plug_params.h plugin/common/plug_params.c plugin/tests/test_params.c plugin/CMakeLists.txt && git commit -m "plugin Task 5 (E9.35): the 36 parameters, their MIDI on the firmware's global channel, their step with the firmware without a loop, their block in the state

P12_params_*: 7 of 7.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && git push origin plugin-v07
```

---

### Task 6: The engine reports the firmware's values — E9.36

**Files:** Create `plugin/engine/eng_fw.h`, `plugin/engine/eng_fw.c`; modify `plugin/engine/eng_run.c`, `plugin/engine/eng_run.h`, `plugin/engine/eng_main.c`, `plugin/tests/plug_drive.c`, `plugin/tests/plugin_test.py`, `plugin/CMakeLists.txt`.

`eng_fw.c` is the engine's one file with firmware headers (B-6), an object library with `emu_fw_headers`, so `P6_engine_objects` counts its object among the engine's own (B-19). The engine writes the values at t0 before it reports running and after each block request; `eng_main.c` refuses a parameter table that is not the firmware's before anything starts (exit 2, state failed: the plugin gives up at once, P-16). `P6_engine_params` sends the parameters' MIDI from a script to the engine and to the renderer: byte for byte the same, different from the script without them, and each value in the engine's report within 512 frames of its frame (B-20).

- [ ] **Step 1: the failing tests.**

`plugin/tests/plugin_test.py`, edit 1 of 4: replace

```python
    def drive(self, card, rate, block, script, frames, out, realtime=None, preroll=False, split=False):
```

with

```python
    def drive(self, card, rate, block, script, frames, out, realtime=None, preroll=False, split=False, values=None):
```

`plugin/tests/plugin_test.py`, edit 2 of 4: replace

```python
        if split:
            cmd += ['split']
```

with

```python
        if split:
            cmd += ['split']
        if values:
            cmd += ['values', values]
```

`plugin/tests/plugin_test.py`, edit 3 of 4: replace

```python
    own = [o for o in eng if o.startswith(('CMakeFiles/lichomat-engine.dir/', 'CMakeFiles/plug_common_san.dir/'))]
```

with

```python
    own = [o for o in eng if o.startswith(('CMakeFiles/lichomat-engine.dir/', 'CMakeFiles/plug_common_san.dir/',
                                          'CMakeFiles/eng_fw.dir/'))]
```

`plugin/tests/plugin_test.py`, edit 4 of 4: replace

```python
# --- the bundle (Task 8) ----------------------------------------------------------------------
```

with

```python
# a script of the plugin's parameters as MIDI on channel 1 (the fixture cards' global channel, 0): Drum 1's volume
# (CC 89) and filter (CC 38) and three voices' decays (CC 51, 53, 57), the DJ filter (NRPN 96) and the reverb level
# (NRPN 98) at their frames, then the pattern (Program Change 1): the next pattern at once, played from the end of
# the one that plays
PARAM_SCRIPT = ['30000f usb B0 59 28', '52000f usb B0 26 30', '60000f usb B0 63 00 B0 62 60 B0 06 14',
                '70000f usb B0 33 7F B0 35 7F B0 39 7F', '90000f usb B0 63 00 B0 62 62 B0 06 70', '200000f usb C0 01']


def param_script(t, name, lines):
    """write_script()'s script and lines merged, one line per frame"""
    merged = {}
    for l in open(write_script(t, name + '.base', None)).read().split('\n') + lines:
        if l.strip():
            f, rest = l.split(' usb ')
            merged.setdefault(int(f[:-1]), []).append(rest)
    p = t.path(name)
    with open(p, 'w') as f:
        f.write(''.join('%df usb %s\n' % (k, ' '.join(merged[k])) for k in sorted(merged)))
    return p


@case
def P6_engine_params(t):
    # The parameters reach the firmware as MIDI at their frames, as from an external controller: with PARAM_SCRIPT the
    # engine is lichomat-render byte for byte, and the output differs from the script without them. The engine's
    # values (plug_drive's values file) show each change within 512 frames of its frame: Drum 1's volume 40 from
    # frame 30,000, its filter 48 from 52,000, the DJ filter 20 from 60,000, the reverb level 112 from 90,000 and
    # the next pattern 1 from 200,000; the firmware's global channel is 0, its receive filter 15 (all)
    script = param_script(t, 'script.txt', PARAM_SCRIPT)
    plain = write_script(t, 'plain.txt')
    frames = blocks_frames(6)
    t.render(t.card('render'), script, 6, t.path('render.wav'))
    t.render(t.card('plain'), plain, 6, t.path('plain.wav'))
    s = t.drive(t.card('drive'), RATE_TEXT, 128, script, frames, t.path('drive.wav'), values=t.path('values.txt'))
    check(s.get('late') == 0 and s.get('dropped') == 0, 'counters %r' % s)
    d = same_files(t.path('render.wav'), t.path('drive.wav'))
    check(d is None, 'the engine and the renderer %s' % d)
    check(same_files(t.path('render.wav'), t.path('plain.wav')) is not None, 'the parameters changed nothing')
    rows = []
    for line in open(t.path('values.txt')):
        w = line.split()
        rows.append((int(w[0][:-1]), [int(v) for v in w[1:-4]], int(w[-3]), int(w[-1])))
    print('%d changes of the values; the first at frame %d: %r, channel %d, rx %d' % (len(rows), rows[0][0], rows[0][1],
                                                                                   rows[0][2], rows[0][3]))

    def first(index, value, after):
        return next((f for f, v, _, _ in rows if f >= after and v[index] == value), None)
    seen = {'Drum 1 Volume': (1, 40, 30000), 'Drum 1 Filter': (13, 48, 52000), 'DJ Filter': (26, 20, 60000),
            'Reverb Level': (27, 112, 90000), 'Pattern': (0, 1, 200000)}
    for name, (i, v, at) in sorted(seen.items()):
        f = first(i, v, at)
        print('%s = %d: from frame %s (sent at %d)' % (name, v, f, at))
        check(f is not None and f - at <= 512, '%s: %r' % (name, f))
    check(rows[0][2] == 0 and rows[0][3] == 15, 'channel and filter %r' % (rows[0][2:],))


# --- the bundle (Task 8) ----------------------------------------------------------------------
```

`plugin/CMakeLists.txt`: replace

```cmake
    P6_engine_quit P6_engine_orphan P7_engine_resampled P7_engine_realtime P6_engine_split P7_engine_split)
```

with

```cmake
    P6_engine_quit P6_engine_orphan P7_engine_resampled P7_engine_realtime P6_engine_split P7_engine_split P6_engine_params)
```

- [ ] **Step 2: they fail.** PLUGIN with REGEX `P6_engine_`. Expected: `== plugin build: 0`, then `91% tests passed, 1 tests failed out of 11`: `FAIL P6_engine_params: plug_drive exited 1:` with `usage: plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll] [split]`.
- [ ] **Step 3: the code.**

Create `plugin/engine/eng_fw.h`:

```c
/*
 * eng_fw.h
 *
 * The engine's reads of the firmware's memory (plan E9.4 Task 6): the values of the plugin's
 * parameters, the global MIDI channel and the receive filter. The only file of the engine that
 * includes firmware headers. They are reads, not calls: the tier hook may make them (emu_core.h),
 * as lichomat-render reads its counters from the hook (render_fw.h, rfw_counters()). The
 * firmware runs on the hook's thread and stands still while it runs, so each value is whole; one
 * read in the middle of a kit load may hold part of the new kit, and the next block's the rest.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef ENG_FW_H_
#define ENG_FW_H_

#include <stddef.h>
#include <stdint.h>

#include "plug_params.h"

/** the firmware's values of plug_params[] (activeParameters[par], the next pattern for the
 * Pattern), its global MIDI channel (0-15, or PLUG_VALUE_NONE) and the receive filter's bits */
void eng_fwValues(uint8_t values[PLUG_PARAM_COUNT], uint8_t *channel, uint8_t *rxFilter);

/** 0 when every parameter of plug_params[] is the firmware's parameter it names (Parameters.h);
 * else -1 with the first that is not in err */
int eng_fwCheck(char *err, size_t n);

#endif /* ENG_FW_H_ */
```

Create `plugin/engine/eng_fw.c`:

```c
/*
 * eng_fw.c
 *
 * The engine's reads of the firmware's memory (plan E9.4 Task 6); see eng_fw.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "eng_fw.h"

#include <stdio.h>

#include "main.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/Sequencer/sequencer.h"

/* plug_params[]'s parameters by their firmware names, in its order */
static const struct
{
	uint16_t par;
	const char *name;
} eng_pars[PLUG_PARAM_COUNT] = {
	{ PLUG_PAR_PATTERN, "the next pattern" },
	{ PAR_VOL1, "PAR_VOL1" }, { PAR_VOL2, "PAR_VOL2" }, { PAR_VOL3, "PAR_VOL3" },
	{ PAR_VOL4, "PAR_VOL4" }, { PAR_VOL5, "PAR_VOL5" }, { PAR_VOL6, "PAR_VOL6" },
	{ PAR_PAN1, "PAR_PAN1" }, { PAR_PAN2, "PAR_PAN2" }, { PAR_PAN3, "PAR_PAN3" },
	{ PAR_PAN4, "PAR_PAN4" }, { PAR_PAN5, "PAR_PAN5" }, { PAR_PAN6, "PAR_PAN6" },
	{ PAR_FILTER_FREQ_1, "PAR_FILTER_FREQ_1" }, { PAR_FILTER_FREQ_2, "PAR_FILTER_FREQ_2" },
	{ PAR_FILTER_FREQ_3, "PAR_FILTER_FREQ_3" }, { PAR_FILTER_FREQ_4, "PAR_FILTER_FREQ_4" },
	{ PAR_FILTER_FREQ_5, "PAR_FILTER_FREQ_5" }, { PAR_FILTER_FREQ_6, "PAR_FILTER_FREQ_6" },
	{ PAR_VELOD1, "PAR_VELOD1" }, { PAR_VELOD2, "PAR_VELOD2" }, { PAR_VELOD3, "PAR_VELOD3" },
	{ PAR_VELOD4, "PAR_VELOD4" }, { PAR_VELOD5, "PAR_VELOD5" }, { PAR_VELOD6_CLOSED, "PAR_VELOD6_CLOSED" },
	{ PAR_VELOD6_OPEN, "PAR_VELOD6_OPEN" },
	{ PAR_DJ_FILTER, "PAR_DJ_FILTER" }, { PAR_REV_LEVEL, "PAR_REV_LEVEL" }, { PAR_REV_DECAY, "PAR_REV_DECAY" },
	{ PAR_FX_DRY_WET, "PAR_FX_DRY_WET" },
	{ PAR_AUDIO_OUT1, "PAR_AUDIO_OUT1" }, { PAR_AUDIO_OUT2, "PAR_AUDIO_OUT2" }, { PAR_AUDIO_OUT3, "PAR_AUDIO_OUT3" },
	{ PAR_AUDIO_OUT4, "PAR_AUDIO_OUT4" }, { PAR_AUDIO_OUT5, "PAR_AUDIO_OUT5" }, { PAR_AUDIO_OUT6, "PAR_AUDIO_OUT6" },
};

void eng_fwValues(uint8_t values[PLUG_PARAM_COUNT], uint8_t *channel, uint8_t *rxFilter)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		const uint16_t par = plug_params[i].par;
		values[i] = par == PLUG_PAR_PATTERN ? seq_pendingPattern : par < NUM_PARAMS ? activeParameters[par] : PLUG_VALUE_NONE;
	}
	*channel = midi_MidiChannels[7] < 16u ? midi_MidiChannels[7] : (uint8_t)PLUG_VALUE_NONE;
	*rxFilter = (uint8_t)(midiParser_txRxFilter & 0x0Fu);
}

int eng_fwCheck(char *err, size_t n)
{
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		if (plug_params[i].par != eng_pars[i].par)
		{
			snprintf(err, n, "the parameter %s (id %u) is firmware parameter %u, %s is %u", plug_params[i].name,
				(unsigned)plug_params[i].id, (unsigned)plug_params[i].par, eng_pars[i].name, (unsigned)eng_pars[i].par);
			return -1;
		}
	return 0;
}
```

`plugin/CMakeLists.txt`: replace

```cmake
add_executable(lichomat-engine engine/eng_main.c engine/eng_run.c engine/eng_resample.c
    $<TARGET_OBJECTS:lichomat-render_fw> $<TARGET_OBJECTS:plug_common_san> ${PLUGIN_DEVICE_OBJECTS})
```

with

```cmake
# the engine's reads of the firmware's memory (plan E9.4): the one file of the engine that includes
# firmware headers, with the flags every such object has (emu_fw_headers), as render_fw.c
add_library(eng_fw OBJECT engine/eng_fw.c)
target_link_libraries(eng_fw PRIVATE emu_fw_headers)
target_include_directories(eng_fw PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/engine")
target_compile_options(eng_fw PRIVATE -Wall -Wextra -Werror)
add_executable(lichomat-engine engine/eng_main.c engine/eng_run.c engine/eng_resample.c $<TARGET_OBJECTS:eng_fw>
    $<TARGET_OBJECTS:lichomat-render_fw> $<TARGET_OBJECTS:plug_common_san> ${PLUGIN_DEVICE_OBJECTS})
```

`plugin/engine/eng_run.c`, edit 1 of 5: replace

```c
#include "emu_core.h"
#include "eng_resample.h"
```

with

```c
#include "emu_core.h"
#include "eng_fw.h"
#include "eng_resample.h"
```

`plugin/engine/eng_run.c`, edit 2 of 5: replace

```c
#define ENG_PACKETS 16u			/* USB-MIDI packets in one transfer (emu_core.h:143) */
```

with

```c
#define ENG_PACKETS 16u			/* USB-MIDI packets in one transfer (emu_core.h:143) */

_Static_assert(PLUG_PARAM_COUNT <= PLUG_VALUES, "the shared memory holds every parameter's value");
```

`plugin/engine/eng_run.c`, edit 3 of 5: replace

```c
	plug_audioWrite(E.s, x, frames);
}
```

with

```c
	plug_audioWrite(E.s, x, frames);
}

/* the firmware's values at host frame frame (plan E9.4): read from its memory (eng_fw.c) */
static void eng_values(uint64_t frame)
{
	uint8_t v[PLUG_PARAM_COUNT], channel, rx;
	eng_fwValues(v, &channel, &rx);
	plug_valuesWrite(E.s, frame, v, PLUG_PARAM_COUNT, channel, rx);
}
```

`plugin/engine/eng_run.c`, edit 4 of 5: replace

```c
		plug_store(&E.s->frontier, E.epoch);
		plug_store(&E.s->state, PLUG_ENGINE_RUNNING);
```

with

```c
		plug_store(&E.s->frontier, E.epoch);
		eng_values(E.epoch);	/* before running: the plugin's first block of this engine has them */
		plug_store(&E.s->state, PLUG_ENGINE_RUNNING);
```

`plugin/engine/eng_run.c`, edit 5 of 5: replace

```c
	(void)ctx;
	(void)why;
	if (E.phase != 2)
		return;
	const uint64_t need = plug_framesToCycles(ENG_AHEAD, PLUG_ENGINE_RATE) + (now - E.t0);
	eng_waitFor(E.epoch + plug_framesCovering(need, E.rate));
	eng_inject(plug_load(&E.s->hostFrames));
}
```

with

```c
	(void)ctx;
	if (E.phase != 2)
		return;
	const uint64_t need = plug_framesToCycles(ENG_AHEAD, PLUG_ENGINE_RATE) + (now - E.t0);
	eng_waitFor(E.epoch + plug_framesCovering(need, E.rate));
	eng_inject(plug_load(&E.s->hostFrames));
	if (why == EMU_HOOK_BLOCK)
		eng_values(E.epoch + plug_framesCovering(now - E.t0, E.rate));
}
```

`plugin/engine/eng_run.h`, edit 1 of 2: replace

```c
 * The engine of the LICHOMAT plugin (plan E9.2 Task 6): the firmware on the virtual device,
 * fed from the plugin's event ring and writing the plugin's audio ring.
```

with

```c
 * The engine of the LICHOMAT plugin (plan E9.2 Task 6): the firmware on the virtual device,
 * fed from the plugin's event ring and writing the plugin's audio ring: the device's two stereo
 * outputs, and after each block the firmware's values of the plugin's parameters (plan E9.4).
```

`plugin/engine/eng_run.h`, edit 2 of 2: replace

```c
 *   messages of one frame as one USB transfer. No input comes late. The wait is unpaced: the
 *   engine runs as soon as the plugin has published.
```

with

```c
 *   messages of one frame as one USB transfer. No input comes late. The wait is unpaced: the
 *   engine runs as soon as the plugin has published.
 * - After each block request, and at t0 before it reports running, the hook writes the
 *   firmware's values (eng_fw.c) at the host frame of now (plan E9.4).
```

`plugin/engine/eng_main.c`, edit 1 of 3: replace

```c
 * plugin ended it or went away, 1 usage, 2 the card or the shared memory, 3 a firmware fault or a
 * fault signal, 4 the converter.
```

with

```c
 * plugin ended it or went away, 1 usage, 2 the card, the shared memory (another version's: plan
 * E9.4) or the parameter table, 3 a firmware fault or a fault signal, 4 the converter.
```

`plugin/engine/eng_main.c`, edit 2 of 3: replace

```c
#include "emu_notices.h"
#include "eng_run.h"
```

with

```c
#include "emu_notices.h"
#include "eng_fw.h"
#include "eng_run.h"
```

`plugin/engine/eng_main.c`, edit 3 of 3: replace

```c
	if (!eng_shm || plug_shmCheck(eng_shm, err, sizeof err) != 0)
	{
		fprintf(stderr, "lichomat-engine: %s\n", err);
		return 2;
	}
```

with

```c
	if (!eng_shm || plug_shmCheck(eng_shm, err, sizeof err) != 0)
	{
		fprintf(stderr, "lichomat-engine: %s\n", err);
		return 2;
	}
	if (eng_fwCheck(err, sizeof err) != 0)
	{
		/* plan E9.4: a parameter the plugin would set is not the firmware's; reported, so not started again */
		fprintf(stderr, "lichomat-engine: %s\n", err);
		plug_store(&eng_shm->state, PLUG_ENGINE_FAILED);
		return 2;
	}
```

`plugin/tests/plug_drive.c`, edit 1 of 8: replace

```c
 *   plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll] [split]
 *
```

with

```c
 *   plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll] [split]
 *       [values FILE]
 *
 * With values, each change of the engine's report of the firmware's values goes into FILE as a
 * line "<frame>f <value> ... channel <c> rx <bits>" (plug_params[] order), the frame the report's;
 * the driver then keeps the engine within two blocks and 128 frames of what it published (not
 * 8,192), so that it reads every report about once a block.
```

`plugin/tests/plug_drive.c`, edit 2 of 8: replace

```c
#include "plug_shm.h"
#include "plug_spawn.h"
```

with

```c
#include "plug_params.h"
#include "plug_shm.h"
#include "plug_spawn.h"
```

`plugin/tests/plug_drive.c`, edit 3 of 8: replace

```c
int main(int argc, char **argv)
{
	int realtime = 0, preroll = 0, split = 0, bad = argc < 8;
	uint32_t margin = 0;
	for (int a = 8; a < argc && !bad; a++)
```

with

```c
/* the engine's values into f when they changed since last */
static void dumpValues(plug_shm_t *s, FILE *f, plug_values_t *last)
{
	plug_values_t v;
	if (!f || !plug_valuesRead(s, &v))
		return;
	if (last->frame != UINT64_MAX && !memcmp(v.values, last->values, sizeof v.values) && v.channel == last->channel
		&& v.rxFilter == last->rxFilter)
		return;
	*last = v;
	fprintf(f, "%lluf", (unsigned long long)v.frame);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		fprintf(f, " %u", v.values[i]);
	fprintf(f, " channel %u rx %u\n", v.channel, v.rxFilter);
}

int main(int argc, char **argv)
{
	int realtime = 0, preroll = 0, split = 0, bad = argc < 8;
	uint32_t margin = 0;
	FILE *vf = NULL;
	for (int a = 8; a < argc && !bad; a++)
```

`plugin/tests/plug_drive.c`, edit 4 of 8: replace

```c
		else if (!strcmp(argv[a], "split"))
			split = 1;
		else
			bad = 1;
	}
	if (bad || (realtime && preroll))
	{
		fprintf(stderr, "usage: plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll] [split]\n");
		return 1;
	}
	const double rate = strtod(argv[3], NULL);
	const uint32_t block = (uint32_t)strtoul(argv[4], NULL, 10);
	const uint64_t frames = strtoull(argv[6], NULL, 10);
```

with

```c
		else if (!strcmp(argv[a], "split"))
			split = 1;
		else if (!strcmp(argv[a], "values") && a + 1 < argc)
			bad = (vf = fopen(argv[++a], "w")) == NULL;
		else
			bad = 1;
	}
	if (bad || (realtime && preroll))
	{
		fprintf(stderr, "usage: plug_drive ENGINE CARD RATE BLOCK SCRIPT FRAMES OUT.wav [realtime MARGIN | preroll] [split] "
			"[values FILE]\n");
		return 1;
	}
	const double rate = strtod(argv[3], NULL);
	const uint32_t block = (uint32_t)strtoul(argv[4], NULL, 10);
	const uint64_t frames = strtoull(argv[6], NULL, 10);
	plug_values_t lastValues;
	lastValues.frame = UINT64_MAX;
```

`plugin/tests/plug_drive.c`, edit 5 of 8: replace

```c
		while (!realtime && n + block > plug_load(&s->audioEnd) + 8192u)
```

with

```c
		while (!realtime && n + block > plug_load(&s->audioEnd) + (vf ? 2u * block + 128u : 8192u))
```

`plugin/tests/plug_drive.c`, edit 6 of 8: replace

```c
			if (end != lastEnd)
			{
				lastEnd = end;
				lastMove = wall();
			}
```

with

```c
			dumpValues(s, vf, &lastValues);
			if (end != lastEnd)
			{
				lastEnd = end;
				lastMove = wall();
			}
```

`plugin/tests/plug_drive.c`, edit 7 of 8: replace

```c
		plug_publish(s, n + block);
		(void)!write(wake[1], "w", 1);
		if (realtime)
```

with

```c
		plug_publish(s, n + block);
		(void)!write(wake[1], "w", 1);
		dumpValues(s, vf, &lastValues);
		if (realtime)
```

`plugin/tests/plug_drive.c`, edit 8 of 8: replace

```c
	waitpid(pid, &status, 0);
	FILE *o = fopen(argv[7], "wb");
```

with

```c
	waitpid(pid, &status, 0);
	if (vf)
		fclose(vf);
	FILE *o = fopen(argv[7], "wb");
```

- [ ] **Step 4: they pass.** PLUGIN with REGEX `P6_engine_`. Expected: `100% tests passed, 0 tests failed out of 11`; with `-V` (`P6_engine_params`): `Drum 1 Volume = 40: from frame 30016 (sent at 30000)`, `Drum 1 Filter = 48: from frame 52032 (sent at 52000)`, `DJ Filter = 20: from frame 60032 (sent at 60000)` (60,097 in Release), `Reverb Level = 112: from frame 90048 (sent at 90000)`, `Pattern = 1: from frame 200064 (sent at 200000)`; the first row's channel 0, rx 15.
- [ ] **Step 5: the table's check.** Once, by hand, that a wrong table stops the engine: in `plugin/common/plug_params.c` change Drum 1 Volume's CC 89 to 90, run

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && source tools/env.sh >/dev/null && ninja -C build/plugin lichomat-engine plug_drive >/dev/null && T=$(mktemp -d) && cp -R emu/tests/cards/init "$T/card" && echo > "$T/s.txt" && build/plugin/plug_drive build/plugin/lichomat-engine "$T/card" 44140.625 128 "$T/s.txt" 4416 "$T/x.wav"; echo "exit $?"; rm -rf "$T"
```

  Expected: `lichomat-engine: the parameter Drum 1 Volume (id 10) is firmware parameter 89, PAR_VOL1 is 88`, `plug_drive: the engine did not start (state 3)`, `exit 2`. Then `git checkout plugin/common/plug_params.c` and `git status --short` shows no change to it.
- [ ] **Step 6: the gate.** PLUGIN-CHECK, T = 85. **Full review of Tasks 2–6** (spec, then quality): B-2, B-3, B-6, B-7, B-19, B-20 against the code; the seqlock (`plug_valuesWrite()`, `plug_valuesRead()`); the hook's read (only reads, never a call); the mutants of the Rules.
- [ ] **Step 7: commit and push.**

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && git add plugin/engine/eng_fw.h plugin/engine/eng_fw.c plugin/engine/eng_run.c plugin/engine/eng_run.h plugin/engine/eng_main.c plugin/tests/plug_drive.c plugin/tests/plugin_test.py plugin/CMakeLists.txt && git commit -m "plugin Task 6 (E9.36): the engine reports the firmware's values, its global channel and receive filter after each block; the parameters' MIDI through the engine is the renderer's

P6_engine_params: == render byte for byte; in the report Drum 1 Volume +16, Filter +32, DJ Filter +32 (Release +97), Reverb Level +48, the pattern +64 frames; channel 0, filter 15.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && git push origin plugin-v07
```

---

### Task 7: Out 1 and Out 2 — E9.37

**Files:** Modify `plugin/vst3/plugin_processor.h`, `plugin/vst3/plugin_processor.cpp`, `plugin/host/plug_host.cpp`, `plugin/tests/plugin_test.py`, `plugin/CMakeLists.txt`.

The two buses (B-4) and the test host's `bus2` (B-14). The onset card plays Drum 1 on out 1 and the other voices on out 2, so both buses carry sound. Without `bus2` every prototype case still sees the sum on Out 1.

- [ ] **Step 1: the failing tests.**

`plugin/tests/plugin_test.py`: replace

```python
# --- the licence gate and the name scan (Task 10) ----------------------------------------------
```

with

```python
# --- the two outputs and the parameters through the plugin (plan E9.4) ------------------------------

@case
def P10_plugin_buses(t):
    # Out 2 activated: at 44,140.625 Hz, offline, blocks of 128 and 100, the plugin's Out 1 is lichomat-render's
    # --stereo out1 and its Out 2 --stereo out2 for the MIDI the plugin gave its engine, from frame latency - 128 on,
    # byte for byte, both loud (the onset card: Drum 1 on out 1, the other voices on out 2). Out 1 without Out 2 is
    # the sum (P10_plugin_native)
    for block in (128, 100):
        script = t.path('script%d.txt' % block)
        out = t.path('plugin%d.wav' % block)
        lat, _ = host(t, plan(t, 'plan%d.txt' % block, TIMELINE + ['bus2', 'out ' + out, 'script ' + script], block=block))
        for name, wavname in (('out1', out), ('out2', out[:-4] + '-out2.wav')):
            t.render(t.card('%s%d' % (name, block)), script, 12, t.path('render-%s%d.wav' % (name, block)), stereo=name)
            _, _, a = wav(wavname)
            _, _, b = wav(t.path('render-%s%d.wav' % (name, block)))
            n, diff, loud = shifted_equal(a, b, lat[0] - 128)
            print('blocks of %d, %s: latency %d, %d frames compared, %d differ, %d samples not 0' % (block, name, lat[0], n,
                                                                                                     diff, loud))
            check(diff == 0 and loud > 10000, 'blocks of %d, %s: %d samples differ' % (block, name, diff))
            check(all(x == 0 for x in a[:2 * (lat[0] - 128)]), '%s: sound before the latency' % name)


@case
def P10_plugin_buses48(t):
    # Out 2 activated at 48 kHz, blocks of 512: Out 1 and Out 2 from plug_readOffset() on are the engine's four
    # channels through plug_drive (split) for the same MIDI, byte for byte
    script = t.path('s48.txt')
    out = t.path('p48.wav')
    host(t, plan(t, 'plan48.txt', TIMELINE + ['bus2', 'out ' + out, 'script ' + script], rate=48000, block=512,
                 frames=480000))
    t.drive(t.card('d48'), '48000', 512, script, 478000, t.path('d48.wav'), split=True)
    _, _, d = wav(t.path('d48.wav'))
    read = 512 + 128 + 3
    for k, wavname in ((0, out), (2, out[:-4] + '-out2.wav')):
        _, _, a = wav(wavname)
        b = [v for pair in zip(channels(d, 4, k), channels(d, 4, k + 1)) for v in pair]
        n, diff, loud = shifted_equal(a, b, read)
        print('Out %d at 48 kHz: %d frames compared, %d differ, %d samples not 0' % (k // 2 + 1, n, diff, loud))
        check(diff == 0 and loud > 10000, 'Out %d: %d differ' % (k // 2 + 1, diff))


# --- the licence gate and the name scan (Task 10) ----------------------------------------------
```

`plugin/CMakeLists.txt`: replace

```cmake
        P10_plugin_offline_hang)
```

with

```cmake
        P10_plugin_offline_hang P10_plugin_buses P10_plugin_buses48)
```

- [ ] **Step 2: they fail.** PLUGIN with REGEX `P10_plugin_buses`. Expected: `== plugin build: 0`, then `0% tests passed, 2 tests failed out of 2`: `FAIL P10_plugin_buses: lichomat-plughost exited 1:` with `lichomat-plughost: plan: cannot read the line: bus2`, the same for `P10_plugin_buses48`.
- [ ] **Step 3: the code.**

`plugin/vst3/plugin_processor.h`, edit 1 of 4: replace

```c
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8): one stereo output, one event input.
 * Each block it turns the host's transport into MIDI clock (plug_clock.h) and the block's notes
 * into MIDI, writes both into the engine's ring at their frames, publishes the block, wakes the
 * engine, and plays the ring plug_readOffset() frames behind; the reported latency is
 * plug_latency(). In real time it never waits; offline (kOffline: Live's bounce, freeze and
 * export) it waits for the engine. The state is the card folder as an LCHS archive
 * (plug_archive.h).
```

with

```c
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8): two stereo outputs, Out 1 and Out 2
 * (plan E9.4), one event input. Each block it turns the host's transport into MIDI clock
 * (plug_clock.h) and the block's notes into MIDI, writes both into the engine's ring at their
 * frames, publishes the block, wakes the engine, and plays the ring plug_readOffset() frames
 * behind: Out 1 and Out 2 are the device's outputs when the host has activated Out 2, else Out 1
 * is their sum; the reported latency is plug_latency(). In real time it never waits; offline
 * (kOffline: Live's bounce, freeze and export) it waits for the engine. The state is the card
 * folder as an LCHS archive (plug_archive.h).
```

`plugin/vst3/plugin_processor.h`, edit 2 of 4: replace

```c
#pragma once

#include "plug_clock.h"
```

with

```c
#pragma once

#include <atomic>

#include "plug_clock.h"
```

`plugin/vst3/plugin_processor.h`, edit 3 of 4: replace

```c
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
```

with

```c
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API activateBus (Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir,
	                                           Steinberg::int32 index, Steinberg::TBool state) SMTG_OVERRIDE;
```

`plugin/vst3/plugin_processor.h`, edit 4 of 4: replace

```c
	bool active = false;
	plug_midi_t clocks[kMaxMidi];
```

with

```c
	bool active = false;
	std::atomic<bool> out2Active {false};	/**< the host activated Out 2 */
	plug_midi_t clocks[kMaxMidi];
```

`plugin/vst3/plugin_processor.cpp`, edit 1 of 3: replace

```cpp
	addAudioOutput (STR16 ("Out"), SpeakerArr::kStereo);
	addEventInput (STR16 ("MIDI In"), 16);
```

with

```cpp
	/* the device's two outputs (plan E9.4): Out 2 inactive until the host activates it, and Out 1 the
	 * sum of both meanwhile */
	addAudioOutput (STR16 ("Out 1"), SpeakerArr::kStereo, kMain, BusInfo::kDefaultActive);
	addAudioOutput (STR16 ("Out 2"), SpeakerArr::kStereo, kAux, 0);
	addEventInput (STR16 ("MIDI In"), 16);
```

`plugin/vst3/plugin_processor.cpp`, edit 2 of 3: replace

```cpp
	if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	return kResultFalse;
}
```

with

```cpp
	if (numIns == 0 && numOuts == 2 && outputs[0] == SpeakerArr::kStereo && outputs[1] == SpeakerArr::kStereo)
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	return kResultFalse;
}

tresult PLUGIN_API Processor::activateBus (MediaType type, BusDirection dir, int32 index, TBool state)
{
	const tresult r = AudioEffect::activateBus (type, dir, index, state);
	if (r == kResultTrue && type == kAudio && dir == kOutput && index == 1)
		out2Active.store (state != 0);
	return r;
}
```

`plugin/vst3/plugin_processor.cpp`, edit 3 of 3: replace

```cpp
	float* l = data.outputs[0].channelBuffers32[0];
	float* r = data.outputs[0].channelBuffers32[1];
	uint32_t copied = 0;
	const uint32_t skip = frames >= readOffset ? 0u : (uint32_t)std::min<uint64_t> (n, readOffset - frames);
	for (uint32_t i = 0; i < skip; i++)
		l[i] = r[i] = 0.0f;
	if (skip < n)
	{
		if (s)
		{
			float* const at[PLUG_AUDIO_CHANNELS] = {l + skip, r + skip, nullptr, nullptr};
			copied = plug_audioRead (s, frames + skip - readOffset, n - skip, at, 1);	/* the outputs' sum */
		}
		else
			for (uint32_t i = skip; i < n; i++)
				l[i] = r[i] = 0.0f;
	}
	data.outputs[0].silenceFlags = copied ? 0 : 3;
	frames += n;
	return kResultOk;
}
```

with

```cpp
	/* Out 1 and Out 2 the device's outputs when the host activated Out 2 and gave its buffers, else
	 * Out 1 their sum (the prototype's output) and Out 2, if given, silent */
	const bool hasOut2 = data.numOutputs > 1 && data.outputs[1].numChannels >= 2 && data.outputs[1].channelBuffers32;
	const bool split = hasOut2 && out2Active.load (std::memory_order_relaxed);
	float* ch[PLUG_AUDIO_CHANNELS] = {data.outputs[0].channelBuffers32[0], data.outputs[0].channelBuffers32[1],
	                                   split ? data.outputs[1].channelBuffers32[0] : nullptr,
	                                   split ? data.outputs[1].channelBuffers32[1] : nullptr};
	const unsigned outs = split ? PLUG_AUDIO_CHANNELS : 2u;
	uint32_t copied = 0;
	const uint32_t skip = frames >= readOffset ? 0u : (uint32_t)std::min<uint64_t> (n, readOffset - frames);
	for (unsigned c = 0; c < outs; c++)
		for (uint32_t i = 0; i < skip; i++)
			ch[c][i] = 0.0f;
	if (skip < n)
	{
		if (s)
		{
			float* at[PLUG_AUDIO_CHANNELS];
			for (unsigned c = 0; c < PLUG_AUDIO_CHANNELS; c++)
				at[c] = ch[c] ? ch[c] + skip : nullptr;
			copied = plug_audioRead (s, frames + skip - readOffset, n - skip, at, split ? 0 : 1);
		}
		else
			for (unsigned c = 0; c < outs; c++)
				for (uint32_t i = skip; i < n; i++)
					ch[c][i] = 0.0f;
	}
	data.outputs[0].silenceFlags = copied ? 0 : 3;
	if (hasOut2)
	{
		if (!split)
			for (unsigned c = 0; c < 2; c++)
				for (uint32_t i = 0; i < n; i++)
					data.outputs[1].channelBuffers32[c][i] = 0.0f;
		data.outputs[1].silenceFlags = split && copied ? 0 : 3;
	}
	frames += n;
	return kResultOk;
}
```

`plugin/host/plug_host.cpp`, edit 1 of 10: replace

```cpp
 *   paced                  each block at its wall-clock time, as an audio device gives it: the
 *                          thread takes the time-constraint policy of the block's period
 * Instance I > 0 writes FILE-I.wav. The summary on stdout gives each instance's latency and the
 * Engine parameter it last reported. Before each process() the output buffers are filled with
 * 0.5, as a host may leave anything in them.
```

with

```cpp
 *   paced                  each block at its wall-clock time, as an audio device gives it: the
 *                          thread takes the time-constraint policy of the block's period
 *   bus2                   activate Out 2 (plan E9.4): each instance writes it as FILE-out2.wav
 * Instance I > 0 writes FILE-I.wav (and FILE-I-out2.wav). The summary on stdout gives each
 * instance's latency and the Engine parameter it last reported. Before each process() the output
 * buffers are filled with 0.5, as a host may leave anything in them.
```

`plugin/host/plug_host.cpp`, edit 2 of 10: replace

```cpp
	bool loop = false, rtcheck = false, offline = true, paced = false;
```

with

```cpp
	bool loop = false, rtcheck = false, offline = true, paced = false, bus2 = false;
```

`plugin/host/plug_host.cpp`, edit 3 of 10: replace

```cpp
		else if (cmd == "paced") p.paced = true;
```

with

```cpp
		else if (cmd == "paced") p.paced = true;
		else if (cmd == "bus2") p.bus2 = true;
```

`plugin/host/plug_host.cpp`, edit 4 of 10: replace

```cpp
	std::vector<float> l, r;
	uint32_t latency = 0;
```

with

```cpp
	std::vector<float> l, r, l2, r2;
	uint32_t latency = 0;
```

`plugin/host/plug_host.cpp`, edit 5 of 10: replace

```cpp
	in.component->activateBus (kAudio, kOutput, 0, true);
	in.component->activateBus (kEvent, kInput, 0, true);
```

with

```cpp
	in.component->activateBus (kAudio, kOutput, 0, true);
	if (p.bus2)
		in.component->activateBus (kAudio, kOutput, 1, true);
	in.component->activateBus (kEvent, kInput, 0, true);
```

`plugin/host/plug_host.cpp`, edit 6 of 10: replace

```cpp
void writeWav (const std::string& path, const Instance& in, double rate)
{
	FILE* f = fopen (path.c_str (), "wb");
	if (!f)
		die ("cannot write " + path);
	const bool native = plug_isNative (rate);
	const uint32_t frames = (uint32_t)in.l.size (), data = frames * 4u, hz = native ? 44141u : (uint32_t)rate;
```

with

```cpp
void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double rate)
{
	FILE* f = fopen (path.c_str (), "wb");
	if (!f)
		die ("cannot write " + path);
	const bool native = plug_isNative (rate);
	const uint32_t frames = (uint32_t)l.size (), data = frames * 4u, hz = native ? 44141u : (uint32_t)rate;
```

`plugin/host/plug_host.cpp`, edit 7 of 10: replace

```cpp
	for (uint32_t i = 0; i < frames; i++)
		for (float x : {in.l[i], in.r[i]})
```

with

```cpp
	for (uint32_t i = 0; i < frames; i++)
		for (float x : {l[i], r[i]})
```

`plugin/host/plug_host.cpp`, edit 8 of 10: replace

```cpp
			/* a host's buffers hold anything: the plugin must write every frame (P10_plugin_native) */
			for (uint32_t i = 0; i < n; i++)
				in->data.outputs[0].channelBuffers32[0][i] = in->data.outputs[0].channelBuffers32[1][i] = 0.5f;
```

with

```cpp
			/* a host's buffers hold anything: the plugin must write every frame (P10_plugin_native) */
			for (int32 b = 0; b < in->data.numOutputs; b++)
				for (int32 c = 0; c < in->data.outputs[b].numChannels; c++)
					for (uint32_t i = 0; i < n; i++)
						in->data.outputs[b].channelBuffers32[c][i] = 0.5f;
```

`plugin/host/plug_host.cpp`, edit 9 of 10: replace

```cpp
			in->l.insert (in->l.end (), l, l + n);
			in->r.insert (in->r.end (), r, r + n);
		}
	}
```

with

```cpp
			in->l.insert (in->l.end (), l, l + n);
			in->r.insert (in->r.end (), r, r + n);
			if (p.bus2)
			{
				const float* l2 = in->data.outputs[1].channelBuffers32[0];
				const float* r2 = in->data.outputs[1].channelBuffers32[1];
				in->l2.insert (in->l2.end (), l2, l2 + n);
				in->r2.insert (in->r2.end (), r2, r2 + n);
			}
		}
	}
```

`plugin/host/plug_host.cpp`, edit 10 of 10: replace

```cpp
		writeWav (i == 0 ? p.out : p.out.substr (0, p.out.size () - 4) + "-" + std::to_string (i) + ".wav", *inst[i], p.rate);
```

with

```cpp
		const std::string base = p.out.substr (0, p.out.size () - 4) + (i == 0 ? "" : "-" + std::to_string (i));
		writeWav (base + ".wav", inst[i]->l, inst[i]->r, p.rate);
		if (p.bus2)
			writeWav (base + "-out2.wav", inst[i]->l2, inst[i]->r2, p.rate);
```

- [ ] **Step 4: they pass.** PLUGIN with REGEX `P10_plugin_buses|P10_plugin_native|P10_plugin_resampled|P10_plugin_validator`. Expected: `100% tests passed, 0 tests failed out of 5`; with `-V`: `blocks of 128, out1: latency 385, 529471 frames compared, 0 differ, 23368 samples not 0`, `blocks of 128, out2: latency 385, 529471 frames compared, 0 differ, 758654 samples not 0`, blocks of 100 the same with latency 357 over 529,499 frames, `Out 1 at 48 kHz: 478000 frames compared, 0 differ, 27861 samples not 0`, `Out 2 at 48 kHz: 478000 frames compared, 0 differ, 729179 samples not 0`. The validator: `Result: 47 tests passed, 0 tests failed` (`ctest --test-dir build/plugin -R P10_plugin_validator -V | grep Result:`).
- [ ] **Step 5: the gate.** PLUGIN-CHECK, T = 87.
- [ ] **Step 6: commit and push.**

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && git add plugin/vst3/plugin_processor.h plugin/vst3/plugin_processor.cpp plugin/host/plug_host.cpp plugin/tests/plugin_test.py plugin/CMakeLists.txt && git commit -m "plugin Task 7 (E9.37): Out 1 and Out 2, the device's two outputs once the host activates Out 2, else their sum on Out 1

P10_plugin_buses: Out 1 == --stereo out1, Out 2 == --stereo out2, 0 differ of 529,471 frames (blocks of 128) and 529,499 (100); 48 kHz: 0 of 478,000. Validator 47/0.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && git push origin plugin-v07
```

---

### Task 8: The parameters in the plugin — E9.38

**Files:** Create `plugin/vst3/plugin_points.h`; replace `plugin/vst3/plugin_controller.cpp`; modify `plugin/vst3/plugin_processor.h`, `plugin/vst3/plugin_processor.cpp`, `plugin/vst3/plugin_link.cpp`, `plugin/host/plug_host.cpp`, `plugin/tests/plugin_test.py`, `plugin/CMakeLists.txt`.

The controller's 36 parameters (B-11); the processor's step (B-7, B-12, B-15): the host's points as MIDI into the ring, the engine's report compared, the values the host is to get as output parameter changes, the host's values kept through a new activation and given to every new engine, and a state set while active (a preset, an undo) taking the new card's values instead of the host's (`setState()` hands the audio thread an empty set of values through `stateValues` and the counter `restoreGen`; Task 9 fills them from the state); the monitor's log line (B-13); the test host's `param`, `channel`, `echo`, its reports and its expected MIDI (B-14). Four cases: the parameters bit for bit with the renderer and the reports with and without an echo (`P10_plugin_params`), a voice sent to Out 2 by its output parameter (`P10_plugin_outputs`), the host's values given to an engine started after a kill (`P10_plugin_params_restart`), the audio thread with automation (`P10_plugin_rtparams`).

- [ ] **Step 1: the failing tests.**

`plugin/tests/plugin_test.py`: replace

```python
# --- the licence gate and the name scan (Task 10) ----------------------------------------------
```

with

```python
def reports(out):
    """the parameter values the plugin reported, as [(frame, {id: value})], from plughost's lines"""
    rows = []
    for l in out.splitlines():
        m = re.match(r'lichomat-plughost: instance 0: frame (\d+):((?: \d+=\d+)+)$', l)
        if m:
            rows.append((int(m.group(1)), dict((int(a), int(b)) for a, b in (w.split('=') for w in m.group(2).split()))))
    return rows


# the onset card stopped, a Note On of Drum 1's note every 20,000 frames, and the host's changes: Drum 1's volume
# (id 10) 40 at 30,000, its filter (30) 48 at 52,000, the DJ filter (50) 20 at 60,000, the reverb level (51) 112 at
# 90,000, then pattern 1 (id 1) at 100,000, which the firmware plays at once with its kit (stopped), and Drum 2's
# decay (41) as a ramp of a step every 1,000 frames from 120,000
PARAM_PLAN = (['note %d 0 48 127' % (20000 * k + 5000) for k in range(25)]
              + ['param 30000 10 40', 'param 52000 30 48', 'param 60000 50 20', 'param 90000 51 112', 'param 100000 1 1']
              + ['param %d 41 %d' % (120000 + 1000 * k, 20 + 3 * k) for k in range(30)])


@case
def P10_plugin_params(t):
    # The host's parameter changes reach the firmware at their frames: at 44,140.625 Hz, offline, blocks of 128, the
    # plugin's output is lichomat-render's for the MIDI the plugin gave its engine (plughost's script, which has the
    # CCs, NRPNs and the Program Change at the changes' frames), byte for byte. The plugin reports every value at its
    # first block (the host has none yet), then only those the firmware changed by itself: pattern 1's kit, loaded
    # with it, sets Drum 1's volume and filter and the reverb level back to the kit's, reported within 1,024 frames of
    # the load or, for a value the host gave less than a quarter of a second before (the reverb level at 90,000), of
    # the end of that time (plug_params.h, settle), each once; the values the host set are not reported back. With echo
    # (the host gives every reported value back to the plugin) the output and the reports are the same: no loop
    runs = {}
    for name, extra in (('plain', []), ('echo', ['echo'])):
        script = t.path(name + '.txt')
        lat, out = host(t, plan(t, name + '.plan', PARAM_PLAN + extra + ['out ' + t.path(name + '.wav'), 'script ' + script],
                                frames=blocks_frames(6)))
        runs[name] = (lat[0], reports(out), script)
    lat, rep, script = runs['plain']
    t.render(t.card('render'), script, 6, t.path('render.wav'))
    _, _, a = wav(t.path('plain.wav'))
    _, _, b = wav(t.path('render.wav'))
    n, diff, loud = shifted_equal(a, b, lat - 128)
    with open(script) as f:
        text = f.read()
    ccs, nrpns, pcs = text.count(' B0 '), text.count(' B0 63 '), text.count(' C0 ')
    print('latency %d, %d frames compared, %d differ; the script: %d CC messages (%d NRPNs), %d Program Changes'
          % (lat, n, diff, ccs, nrpns, pcs))
    check(diff == 0 and loud > 1000 and nrpns == 2 and pcs == 1 and ccs == 2 + 2 * 3 + 30, 'differ %d, %r' % (diff, (ccs, nrpns, pcs)))
    # the block a report reaches the host in depends on how far the engine has run when the plugin reads its table
    # (offline it runs ahead freely): the frames are bounded, the values exact, each parameter reported once
    settle = int(RATE / 4)
    for name in ('plain', 'echo'):
        rows = runs[name][1]
        later = {}
        for _, v in rows[1:]:
            later.update(v)
        ids = [i for _, v in rows[1:] for i in v]
        frames = [f for f, _ in rows[1:]]
        print('%s: the first report at frame %d, %d values; then %r at frames %r' % (name, rows[0][0], len(rows[0][1]),
                                                                                  later, frames))
        check(rows[0][0] == 0 and len(rows[0][1]) == 36, '%s: the first report %r' % (name, rows[:1]))
        check(later == {10: 127, 30: 127, 51: 0} and len(ids) == len(set(ids)), '%s: the later reports %r' % (name, rows[1:]))
        check(all(100000 <= f < 100000 + 1024 or 90000 + settle <= f < 90000 + settle + 1024 for f in frames),
              '%s: the later reports at frames %r' % (name, frames))
    _, _, e = wav(t.path('echo.wav'))
    check(e == a, 'with echo the output differs')
    print('with echo: the same output and the same reports')
    log = ''.join(instance_logs(t.path('home')).values())
    said = re.findall(r'parameters: [^\n]*', log)
    print('the logs: %r' % sorted(set(said)))
    check(len(said) == 2 and all(x == 'parameters: MIDI channel 1, receive filter: CC on, Program Change on' for x in said),
          'the logs %r' % said)


@case
def P10_plugin_outputs(t):
    # A voice moved to out 2 by its output parameter (id 60, Drum 1 Output, St2) on the init card, whose voices all
    # play on out 1: Out 2 silent before the change (the mixer takes it at once, not after the voice queue's 128
    # frames), then Drum 1, and Out 1 the others, each the renderer's, byte for byte
    init = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'init')
    out = t.path('init.wav')
    lines = ['card ' + init, 'bus2', 'play 0 120 0', 'param 100000 60 1', 'out ' + out, 'script ' + t.path('init.txt')]
    lat, _ = host(t, plan(t, 'init.plan', lines, card=False, frames=blocks_frames(6)))
    for name, wavname in (('out1', out), ('out2', out[:-4] + '-out2.wav')):
        r = t.path('render-%s.wav' % name)
        t.run([t.a.render, '--card', t.card('i' + name, 'init'), '--bpm', 'mid', '--no-start', '--as-booted', '--stereo', name,
               '--seconds', '6', '--events', t.path('init.txt'), '--out', r])
        _, _, a = wav(wavname)
        _, _, b = wav(r)
        n, diff, loud = shifted_equal(a, b, lat[0] - 128)
        early = sum(1 for v in a[:2 * (100000 + lat[0] - 128)] if v)  # the read offset: the mixer takes it at once
        print('init, Drum 1 to St2 at 100,000, %s: %d frames compared, %d differ, %d samples not 0 (%d before the change)'
              % (name, n, diff, loud, early))
        check(diff == 0 and loud > 10000, 'init, %s: %d differ' % (name, diff))
        check(name == 'out1' or early == 0, 'Out 2 before the change: %d samples' % early)


@case
def P10_plugin_params_restart(t):
    # A new engine (here after a kill) gets the host's values: real time, paced, 48 kHz, the init card's four on the
    # floor, every voice's volume 0 from frame 0 (before the transport plays): silent before the kill at 3 s and
    # silent after it, though the new engine starts on the card's kit (volume 127) and plays the beats again
    init = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'init')
    vols = ['param 0 %d 0' % i for i in range(10, 16)]
    lines = ['card ' + init, 'mode realtime', 'paced', 'play 4800 120 0', 'kill 144000', 'out ' + t.path('r.wav')]
    host(t, plan(t, 'quiet.txt', lines + vols, rate=48000, block=256, frames=48000 * 6, card=False))
    host(t, plan(t, 'loud.txt', lines[:-1] + ['out ' + t.path('l.wav')], rate=48000, block=256, frames=48000 * 6, card=False))
    _, _, q = wav(t.path('r.wav'))
    _, _, l = wav(t.path('l.wav'))
    before = sum(1 for v in q[:2 * 144000] if abs(v) >= 1000)
    after = sum(1 for v in q[2 * 144000:] if abs(v) >= 1000)
    loud = sum(1 for v in l[2 * 144000:] if abs(v) >= 1000)
    log = ''.join(instance_logs(t.path('home')).values())
    print('volumes 0: %d loud samples before the kill, %d after; without them %d after; engines started %d'
          % (before, after, loud, len(re.findall(r'engine \d+ started on ', log)) ))
    check(before == 0 and after == 0 and loud > 10000, 'before %d, after %d, without %d' % (before, after, loud))


@case
def P10_plugin_rtparams(t):
    # the audio thread under the interposer with automation running: P10_plugin_rt's 10 s, and every block a change of
    # four parameters (two CCs, an NRPN, the pattern every second): no allocation, lock, wait, read or sleep inside
    # process(), one write per call
    auto = []
    for k in range(3750):
        f = 128 * k + 17
        auto += ['param %d 10 %d' % (f, k % 128), 'param %d 30 %d' % (f, 127 - k % 128), 'param %d 50 %d' % (f, (5 * k) % 128)]
        if k % 375 == 0:
            auto.append('param %d 1 %d' % (f, (k // 375) % 4))
    lines = ['mode realtime', 'paced', 'rtcheck', 'out ' + t.path('rt.wav')] + TIMELINE + auto
    _, out = host(t, plan(t, 'rt.txt', lines, rate=48000, block=128, frames=480000),
                  env={'DYLD_INSERT_LIBRARIES': t.a.rtcheck})
    line = [l for l in out.splitlines() if 'rtcheck:' in l and 'process calls' in l]
    check(line, 'no rtcheck line in %r' % out)
    w = line[0].split()
    calls, bad, writes = int(w[2]), int(w[5]), int(w[8])
    check(calls == 3750 and bad == 0 and writes == calls, line[0])


# --- the licence gate and the name scan (Task 10) ----------------------------------------------
```

`plugin/CMakeLists.txt`, edit 1 of 2: replace

```cmake
        P10_plugin_offline_hang P10_plugin_buses P10_plugin_buses48)
```

with

```cmake
        P10_plugin_offline_hang P10_plugin_buses P10_plugin_buses48 P10_plugin_params P10_plugin_outputs
        P10_plugin_params_restart P10_plugin_rtparams)
```

`plugin/CMakeLists.txt`, edit 2 of 2: replace

```cmake
set_tests_properties(P10_plugin_kill P10_plugin_rt P10_plugin_beats P10_plugin_hang P10_plugin_crashes P10_plugin_policy
    P10_plugin_spawn_fail P10_plugin_offline_hang PROPERTIES RUN_SERIAL TRUE)	# paced by the wall clock
```

with

```cmake
set_tests_properties(P10_plugin_kill P10_plugin_rt P10_plugin_beats P10_plugin_hang P10_plugin_crashes P10_plugin_policy
    P10_plugin_spawn_fail P10_plugin_offline_hang P10_plugin_params_restart P10_plugin_rtparams PROPERTIES RUN_SERIAL TRUE)	# paced by the wall clock
```

- [ ] **Step 2: they fail.** PLUGIN with REGEX `P10_plugin_params|P10_plugin_outputs|P10_plugin_rtparams`. Expected: `== plugin build: 0`, then `0% tests passed, 4 tests failed out of 4`: `lichomat-plughost: plan: cannot read the line: param 30000 10 40` (`P10_plugin_params`), `… param 100000 60 1` (`_outputs`), `… param 0 10 0` (`_params_restart`), `… param 17 10 0` (`_rtparams`).
- [ ] **Step 3: the code.**

Create `plugin/vst3/plugin_points.h`:

```c
/*
 * plugin_points.h
 *
 * A block's parameter changes from the host as plug_params.h's points (plan E9.4 Task 8): the
 * processor's, and the test host's for the MIDI it expects the plugin to give its engine, so
 * that both take them in the same order.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#pragma once

#include "plug_params.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

namespace lichomat {

/** The points of the plugin's parameters in changes, a block of n frames (0: a flush between
 * blocks), in the order of its queues and of each queue's points; an offset outside the block is
 * its nearest frame. Returns the count, at most max. */
inline unsigned pointsOf (Steinberg::Vst::IParameterChanges* changes, Steinberg::int32 n, plug_paramPoint_t* out,
                          unsigned max)
{
	unsigned count = 0;
	const Steinberg::int32 queues = changes ? changes->getParameterCount () : 0;
	for (Steinberg::int32 i = 0; i < queues; i++)
	{
		Steinberg::Vst::IParamValueQueue* q = changes->getParameterData (i);
		const int k = q ? plug_paramIndex (q->getParameterId ()) : -1;
		if (k < 0)
			continue;
		for (Steinberg::int32 j = 0; j < q->getPointCount () && count < max; j++)
		{
			Steinberg::int32 offset = 0;
			Steinberg::Vst::ParamValue value = 0;
			if (q->getPoint (j, offset, value) != Steinberg::kResultOk)
				continue;
			const Steinberg::int32 o = offset < 0 || n <= 0 ? 0 : offset >= n ? n - 1 : offset;
			out[count++] = plug_paramPoint_t {(unsigned)k, (uint32_t)o, plug_paramValue ((unsigned)k, value)};
		}
	}
	return count;
}

} // namespace lichomat
```

Replace the whole of `plugin/vst3/plugin_controller.cpp` with:

```cpp
/*
 * plugin_controller.cpp
 *
 * The LICHOMAT plugin's edit controller (plan E9.2 Task 8): no window of its own; the read-only
 * parameter of the engine's state, for the host's generic view and for the tests, and (plan E9.4)
 * the automatable parameters of plug_params.h, each shown as the device shows it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plugin_controller.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "plug_params.h"
#include "plugin_ids.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace lichomat {

/* a firmware parameter: the values 0..max in steps of 1, shown as the device's display shows
 * them: value + offset (pan -63..64, the pattern 1..64) or the value's name (a voice's output) */
class FwParameter : public Parameter
{
public:
	explicit FwParameter (const plug_param_t& p) : param (p)
	{
		info.id = p.id;
		UString (info.title, str16BufferSize (String128)).fromAscii (p.name);
		UString (info.shortTitle, str16BufferSize (String128)).fromAscii (p.shortName);
		info.units[0] = 0;
		info.stepCount = p.max;
		info.defaultNormalizedValue = (double)p.def / p.max;
		info.unitId = kRootUnitId;
		info.flags = ParameterInfo::kCanAutomate | (p.kind == PLUG_PK_PROGRAM || p.names ? ParameterInfo::kIsList : 0);
		setNormalized (info.defaultNormalizedValue);
	}

	void toString (ParamValue normalized, String128 string) const SMTG_OVERRIDE
	{
		char text[16];
		const int v = (int)toPlain (normalized);
		if (param.names)
			snprintf (text, sizeof text, "%s", param.names[v]);
		else
			snprintf (text, sizeof text, "%d", v + param.offset);
		UString (string, str16BufferSize (String128)).fromAscii (text);
	}

	bool fromString (const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE
	{
		char text[16];
		UString (const_cast<TChar*> (string), str16BufferSize (String128)).toAscii (text, sizeof text);
		for (int v = 0; param.names && v <= param.max; v++)
			if (!strcmp (text, param.names[v]))
			{
				normalized = (double)v / param.max;
				return true;
			}
		char* end = nullptr;
		const long v = strtol (text, &end, 10) - param.offset;
		if (end == text || v < 0 || v > param.max)
			return false;
		normalized = (double)v / param.max;
		return true;
	}

	ParamValue toPlain (ParamValue normalized) const SMTG_OVERRIDE
	{
		const ParamValue v = std::floor (normalized * param.max + 0.5);
		return !(v >= 0) ? 0 : v > param.max ? param.max : v;	/* also for a value outside 0-1, or not a number */
	}

	ParamValue toNormalized (ParamValue plain) const SMTG_OVERRIDE
	{
		return plain / param.max;
	}

private:
	const plug_param_t& param;
};

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
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		parameters.addParameter (new FwParameter (plug_params[i]));
	return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState (IBStream*)
{
	/* the processor's state is the card and the values the host had; the processor gives them to the
	 * host as output parameter changes once its engine runs (plan E9.4) */
	return kResultOk;
}

} // namespace lichomat
```

`plugin/vst3/plugin_processor.h`, edit 1 of 4: replace

```c
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8): two stereo outputs, Out 1 and Out 2
 * (plan E9.4), one event input. Each block it turns the host's transport into MIDI clock
 * (plug_clock.h) and the block's notes into MIDI, writes both into the engine's ring at their
 * frames, publishes the block, wakes the engine, and plays the ring plug_readOffset() frames
 * behind: Out 1 and Out 2 are the device's outputs when the host has activated Out 2, else Out 1
 * is their sum; the reported latency is plug_latency(). In real time it never waits; offline
 * (kOffline: Live's bounce, freeze and export) it waits for the engine. The state is the card
 * folder as an LCHS archive (plug_archive.h).
```

with

```c
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8): two stereo outputs, Out 1 and Out 2
 * (plan E9.4), one event input. Each block it turns the host's transport into MIDI clock
 * (plug_clock.h), the block's notes and the host's parameter changes into MIDI (plug_params.h),
 * writes them into the engine's ring at their frames, publishes the block, wakes the engine, and
 * plays the ring plug_readOffset() frames behind: Out 1 and Out 2 are the device's outputs when
 * the host has activated Out 2, else Out 1 is their sum; the reported latency is plug_latency().
 * The firmware's values that changed by themselves go to the host as output parameter changes.
 * In real time it never waits; offline (kOffline: Live's bounce, freeze and export) it waits for
 * the engine. The state is the card folder as an LCHS archive (plug_archive.h).
```

`plugin/vst3/plugin_processor.h`, edit 2 of 4: replace

```c
#include "plug_clock.h"
#include "plugin_card.h"
```

with

```c
#include "plug_clock.h"
#include "plug_params.h"
#include "plugin_card.h"
```

`plugin/vst3/plugin_processor.h`, edit 3 of 4: replace

```c
	static constexpr unsigned kMaxMidi = 1024;	/**< MIDI messages of one block: clocks and notes */

private:
	unsigned notesOf (Steinberg::Vst::IEventList* events, Steinberg::int32 frames);
```

with

```c
	static constexpr unsigned kMaxMidi = 1024;	/**< MIDI messages of one block: clocks, notes, parameters */

private:
	unsigned notesOf (Steinberg::Vst::IEventList* events, Steinberg::int32 frames);
	unsigned paramsOf (Steinberg::Vst::ProcessData& data, plug_shm_t* s, bool running, uint32_t frames);
	void showValues (Steinberg::Vst::ProcessData& data);
	void restoreValues (const std::atomic<int>* from);
```

`plugin/vst3/plugin_processor.h`, edit 4 of 4: replace

```c
	std::atomic<bool> out2Active {false};	/**< the host activated Out 2 */
	plug_midi_t clocks[kMaxMidi];
	plug_midi_t notes[kMaxMidi];
};
```

with

```c
	std::atomic<bool> out2Active {false};	/**< the host activated Out 2 */
	plug_params_t params {};	/**< the audio thread's: the parameters' step with the firmware */
	uint8_t channel = PLUG_VALUE_NONE;	/**< the firmware's global MIDI channel, from its last values */
	/** the host's values (params' want), for getState() and a new activation; the audio thread writes them */
	std::atomic<int> hostValues[PLUG_PARAM_COUNT];
	/** the values of the last state set, for the audio thread, which takes them when restoreGen moves */
	std::atomic<int> stateValues[PLUG_PARAM_COUNT];
	std::atomic<uint64_t> restoreGen {0};
	std::atomic<uint64_t> restoreSeen {0};	/**< the audio thread's (and setActive()'s, while it does not run) */
	plug_midi_t clocks[kMaxMidi];
	plug_midi_t notes[kMaxMidi];
	plug_midi_t paramMidi[kMaxMidi];
	plug_paramPoint_t points[kMaxMidi];
};
```

`plugin/vst3/plugin_processor.cpp`, edit 1 of 7: replace

```cpp
#include "plugin_ids.h"
#include "pluginterfaces/base/ibstream.h"
```

with

```cpp
#include "plugin_ids.h"
#include "plugin_points.h"
#include "pluginterfaces/base/ibstream.h"
```

`plugin/vst3/plugin_processor.cpp`, edit 2 of 7: replace

```cpp
	processContextRequirements.needTempo ().needTransportState ().needProjectTimeMusic ().needCycleMusic ();
}
```

with

```cpp
	processContextRequirements.needTempo ().needTransportState ().needProjectTimeMusic ().needCycleMusic ();
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
	{
		hostValues[i].store (-1);
		stateValues[i].store (-1);
	}
	plug_paramsInit (&params, 0);
}
```

`plugin/vst3/plugin_processor.cpp`, edit 3 of 7: replace

```cpp
		waitedInVain = -2;
		reported = -1;
```

with

```cpp
		waitedInVain = -2;
		reported = -1;
		/* the parameters: a value settles within a quarter of a second; the host keeps its values
		 * through a new activation, or takes those of a state set since */
		plug_paramsInit (&params, (uint64_t)(processSetup.sampleRate / 4.0));
		channel = PLUG_VALUE_NONE;
		const uint64_t gen = restoreGen.load (std::memory_order_acquire);
		restoreValues (gen != restoreSeen.load (std::memory_order_relaxed) ? stateValues : hostValues);
		restoreSeen.store (gen, std::memory_order_relaxed);
```

`plugin/vst3/plugin_processor.cpp`, edit 4 of 7: replace

```cpp
void Processor::reportState (ProcessData& data, uint64_t state)
```

with

```cpp
/* values (-1: none) for the host and the next engine */
void Processor::restoreValues (const std::atomic<int>* from)
{
	int v[PLUG_PARAM_COUNT];
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		v[i] = from[i].load (std::memory_order_relaxed);
	plug_paramsRestore (&params, v);
}

/* The host's parameter changes and the engine's values of a block as MIDI into paramMidi (plan E9.4,
 * plug_params.h): the host's to the engine when running (else kept for it), the engine's values
 * compared with the host's. Returns the MIDI count. */
unsigned Processor::paramsOf (ProcessData& data, plug_shm_t* s, bool running, uint32_t n)
{
	plug_values_t v;
	const bool have = running && n > 0 && plug_valuesRead (s, &v);	/* a flush (n 0) only gives the host's */
	if (have)
		channel = v.channel;
	const unsigned np = pointsOf (data.inputParameterChanges, (int32)n, points, kMaxMidi);
	unsigned nm = plug_paramsHost (&params, points, np, frames, running ? channel : (uint8_t)PLUG_VALUE_NONE, paramMidi,
	                               0, kMaxMidi);
	if (have)
		nm = plug_paramsReport (&params, v.values, v.frame, frames, channel, paramMidi, nm, kMaxMidi);
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		if (hostValues[i].load (std::memory_order_relaxed) != params.p[i].want)
			hostValues[i].store (params.p[i].want, std::memory_order_relaxed);
	return nm;
}

/* the values the host is to get, as output parameter changes at the block's first frame */
void Processor::showValues (ProcessData& data)
{
	if (!data.outputParameterChanges)
		return;
	unsigned from = 0;
	uint8_t v = 0;
	for (int i; (i = plug_paramsToShow (&params, &from, &v)) >= 0;)
	{
		int32 index = 0, point = 0;
		IParamValueQueue* q = data.outputParameterChanges->addParameterData (plug_params[i].id, index);
		if (q && q->addPoint (0, (double)v / plug_params[i].max, point) == kResultOk)
			params.p[i].shown = 1;
	}
}

void Processor::reportState (ProcessData& data, uint64_t state)
```

`plugin/vst3/plugin_processor.cpp`, edit 5 of 7: replace

```cpp
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
```

with

```cpp
	const uint64_t state = s ? plug_load (&s->state) : PLUG_ENGINE_NONE;
	reportState (data, state);
	const uint64_t gen = restoreGen.load (std::memory_order_acquire);
	if (gen != restoreSeen.load (std::memory_order_relaxed))
	{
		restoreValues (stateValues);	/* a state set while active: its values */
		restoreSeen.store (gen, std::memory_order_relaxed);
	}
	const bool running = s && state == PLUG_ENGINE_RUNNING;
	if (running)
	{
		const uint64_t pid = plug_load (&s->pid);
		if (pid != runningPid)
		{
			plug_clockReset (&clock);	/* a new engine: the slave starts afresh */
			plug_paramsEngine (&params, plug_load (&s->epoch));	/* and gets the host's values */
			runningPid = pid;
		}
	}
	if (data.numSamples == 0)
	{
		/* a flush: the host's parameter changes between blocks, at the next block's first frame */
		const unsigned np = paramsOf (data, s, running, 0);
		for (unsigned k = 0; running && k < np; k++)
			(void)plug_evPush (s, frames, paramMidi[k].b, paramMidi[k].len);
		return kResultOk;
	}
	if (data.numSamples < 0 || data.numOutputs < 1 || data.outputs[0].numChannels < 2)
		return kResultOk;
	const uint32_t n = (uint32_t)data.numSamples;
```

`plugin/vst3/plugin_processor.cpp`, edit 6 of 7: replace

```cpp
	const unsigned nc = plug_clockBlock (&clock, &t, n, clocks, kMaxMidi);
	const unsigned nn = notesOf (data.inputEvents, (int32)n);
	if (running)
		for (unsigned i = 0, j = 0; i < nc || j < nn;)
		{
			/* the transport's messages first at a frame: Start before a note on the downbeat */
			const plug_midi_t& m = (j == nn || (i < nc && clocks[i].offset <= notes[j].offset)) ? clocks[i++] : notes[j++];
			(void)plug_evPush (s, frames + m.offset, m.b, m.len);
		}
```

with

```cpp
	const unsigned nc = plug_clockBlock (&clock, &t, n, clocks, kMaxMidi);
	const unsigned nn = notesOf (data.inputEvents, (int32)n);
	const unsigned np = paramsOf (data, s, running, n);
	showValues (data);
	if (running)
		for (unsigned i = 0, j = 0, k = 0; i < nc || j < nn || k < np;)
		{
			/* at a frame the transport's messages first (Start before a note on the downbeat), then the
			 * parameters (a note plays with the value set at its frame), then the notes */
			const uint32_t oc = i < nc ? clocks[i].offset : UINT32_MAX, op = k < np ? paramMidi[k].offset : UINT32_MAX;
			const uint32_t on = j < nn ? notes[j].offset : UINT32_MAX;
			const plug_midi_t& m = (oc <= op && oc <= on) ? clocks[i++] : (op <= on) ? paramMidi[k++] : notes[j++];
			(void)plug_evPush (s, frames + m.offset, m.b, m.len);
		}
```

`plugin/vst3/plugin_processor.cpp`, edit 7 of 7: replace

```cpp
	char e[512];
	if (plug_archiveRead (fresh.card ().c_str (), streamGet, state, e, sizeof e) != 0)
	{
		fprintf (stderr, "LICHOMAT: setState: %s\n", e);
		fresh.remove ();
		return kResultFalse;
	}
```

with

```cpp
	char e[512];
	if (plug_archiveRead (fresh.card ().c_str (), streamGet, state, e, sizeof e) != 0)
	{
		fprintf (stderr, "LICHOMAT: setState: %s\n", e);
		fresh.remove ();
		return kResultFalse;
	}
	/* a new card: its own values, none of the host's (plan E9.4); the audio thread takes them */
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		stateValues[i].store (-1, std::memory_order_relaxed);
	restoreGen.fetch_add (1, std::memory_order_release);
```

`plugin/vst3/plugin_link.cpp`, edit 1 of 3: replace

```cpp
#include "plug_spawn.h"
```

with

```cpp
#include "plug_params.h"
#include "plug_spawn.h"
```

`plugin/vst3/plugin_link.cpp`, edit 2 of 3: replace

```cpp
		char waited[64];	/* why, for a vain offline wait */
		bool stall = false;
```

with

```cpp
		char waited[64];	/* why, for a vain offline wait */
		bool stall = false;
		bool told = false;	/* the parameters' MIDI channel and filter are in the log */
```

`plugin/vst3/plugin_link.cpp`, edit 3 of 3: replace

```cpp
			const uint64_t st = plug_load (&mem->state), frontier = plug_load (&mem->frontier);
```

with

```cpp
			const uint64_t st = plug_load (&mem->state), frontier = plug_load (&mem->frontier);
			plug_values_t v;
			if (!told && st == PLUG_ENGINE_RUNNING && plug_valuesRead (mem, &v))
			{
				/* plan E9.4: the parameters reach the firmware on its global channel, through its filter */
				told = true;
				if (v.channel == PLUG_VALUE_NONE)
					note ("parameters: the firmware has no global MIDI channel; the host cannot set them");
				else
					note ("parameters: MIDI channel %u, receive filter: CC %s, Program Change %s", v.channel + 1u,
					      (v.rxFilter & 4u) ? "on" : "off (the host cannot set them)",
					      (v.rxFilter & 8u) ? "on" : "off (the host cannot set the pattern)");
			}
```

`plugin/host/plug_host.cpp`, edit 1 of 10: replace

```cpp
 *   paced                  each block at its wall-clock time, as an audio device gives it: the
 *                          thread takes the time-constraint policy of the block's period
 *   bus2                   activate Out 2 (plan E9.4): each instance writes it as FILE-out2.wav
 * Instance I > 0 writes FILE-I.wav (and FILE-I-out2.wav). The summary on stdout gives each
 * instance's latency and the Engine parameter it last reported. Before each process() the output
 * buffers are filled with 0.5, as a host may leave anything in them.
```

with

```cpp
 *   paced                  each block at its wall-clock time, as an audio device gives it: the
 *                          thread takes the time-constraint policy of the block's period
 *   bus2                   activate Out 2 (plan E9.4): each instance writes it as FILE-out2.wav
 *   param FRAME ID VALUE   set parameter ID (plug_params.h) to VALUE (0..its max) at FRAME, every
 *                          instance; the script gets the MIDI the plugin must give its engine
 *   channel CH             the firmware's global MIDI channel (0-15) for that MIDI (default 0, the
 *                          test cards')
 *   echo                   give each parameter value the plugin reports back to it as a change at
 *                          the next block's first frame, as a host may
 * Instance I > 0 writes FILE-I.wav (and FILE-I-out2.wav). The summary on stdout gives each
 * instance's latency and the Engine parameter it last reported, and a line for each block in which
 * an instance reported parameter values: "lichomat-plughost: instance I: frame F: ID=VALUE ...".
 * Before each process() the output buffers are filled with 0.5, as a host may leave anything in
 * them.
```

`plugin/host/plug_host.cpp`, edit 2 of 10: replace

```cpp
#include "plug_clock.h"
#include "plug_time.h"
```

with

```cpp
#include "plug_clock.h"
#include "plug_params.h"
#include "plug_time.h"
#include "plugin_points.h"
```

`plugin/host/plug_host.cpp`, edit 3 of 10: replace

```cpp
struct TransportCmd
{
```

with

```cpp
struct ParamCmd
{
	uint64_t frame;
	uint32_t id;
	int value;
};

struct TransportCmd
{
```

`plugin/host/plug_host.cpp`, edit 4 of 10: replace

```cpp
	bool loop = false, rtcheck = false, offline = true, paced = false, bus2 = false;
	double loopStart = 0, loopEnd = 0;
	std::vector<NoteCmd> notes;
```

with

```cpp
	bool loop = false, rtcheck = false, offline = true, paced = false, bus2 = false, echo = false;
	int channel = 0;
	double loopStart = 0, loopEnd = 0;
	std::vector<NoteCmd> notes;
	std::vector<ParamCmd> params;
```

`plugin/host/plug_host.cpp`, edit 5 of 10: replace

```cpp
		else if (cmd == "bus2") p.bus2 = true;
```

with

```cpp
		else if (cmd == "bus2") p.bus2 = true;
		else if (cmd == "echo") p.echo = true;
		else if (cmd == "channel") ok = !!(s >> p.channel) && p.channel >= 0 && p.channel < 16;
		else if (cmd == "param")
		{
			ParamCmd c;
			ok = !!(s >> c.frame >> c.id >> c.value) && plug_paramIndex (c.id) >= 0 && c.value >= 0
			     && c.value <= plug_params[plug_paramIndex (c.id)].max;
			p.params.push_back (c);
		}
```

`plugin/host/plug_host.cpp`, edit 6 of 10: replace

```cpp
	EventList events {512};
	ParameterChanges outParams {8};
	double engine = 0.0;	/* the Engine parameter: 2/3 running */
	std::vector<float> l, r, l2, r2;
	uint32_t latency = 0;
};
```

with

```cpp
	EventList events {512};
	ParameterChanges inParams {64};
	ParameterChanges outParams {64};	/* the Engine parameter and every one of plug_params.h in one block */
	double engine = 0.0;	/* the Engine parameter: 2/3 running */
	std::vector<float> l, r, l2, r2;
	uint32_t latency = 0;
	plug_params_t mirror {};	/* the plugin's step with its engine, for the script (plug_params.h) */
	std::vector<std::pair<uint32_t, double>> echo;	/* reported values for the next block */
};
```

`plugin/host/plug_host.cpp`, edit 7 of 10: replace

```cpp
	in.data.inputEvents = &in.events;
	in.data.outputParameterChanges = &in.outParams;
	in.engine = 0.0;
}
```

with

```cpp
	in.data.inputEvents = &in.events;
	in.data.inputParameterChanges = &in.inParams;
	in.data.outputParameterChanges = &in.outParams;
	in.engine = 0.0;
	plug_paramsInit (&in.mirror, 0);
	in.echo.clear ();
}

/* the parameter values instance i reported in the block at frame: printed, into its mirror (the
 * plugin's report sets the value it gives the engine), and kept for an echo */
void readReports (Instance& in, size_t i, uint64_t frame, bool echo)
{
	std::string line;
	for (int32 k = 0; k < in.outParams.getParameterCount (); k++)
	{
		IParamValueQueue* q = in.outParams.getParameterData (k);
		ParamValue v;
		int32 off;
		const int idx = q ? plug_paramIndex (q->getParameterId ()) : -1;
		if (idx < 0 || q->getPointCount () < 1 || q->getPoint (q->getPointCount () - 1, off, v) != kResultOk)
			continue;
		const uint8_t plain = plug_paramValue ((unsigned)idx, v);
		line += " " + std::to_string (plug_params[idx].id) + "=" + std::to_string (plain);
		in.mirror.p[idx].want = in.mirror.p[idx].sent = plain;
		in.mirror.p[idx].shown = 1;
		if (echo)
			in.echo.push_back ({plug_params[idx].id, v});
	}
	if (!line.empty ())
		printf ("lichomat-plughost: instance %zu: frame %llu:%s\n", i, (unsigned long long)frame, line.c_str ());
}
```

`plugin/host/plug_host.cpp`, edit 8 of 10: replace

```cpp
	size_t nextNote = 0, nextKill = 0, nextHang = 0, nextRenew = 0, nextSet = 0;
```

with

```cpp
	size_t nextNote = 0, nextKill = 0, nextHang = 0, nextRenew = 0, nextSet = 0, nextParam = 0;
```

`plugin/host/plug_host.cpp`, edit 9 of 10: replace

```cpp
		for (unsigned i = 0; i < nm; i++)
		{
			char b[16];
			snprintf (b, sizeof b, m[i].len == 3 ? "%02X %02X %02X" : "%02X", m[i].b[0], m[i].b[1], m[i].b[2]);
			script[f + m[i].offset].push_back (b);
		}
		for (auto& in : inst)
			in->events.clear ();
```

with

```cpp
		auto put = [&script, f] (const plug_midi_t& x) {
			char b[16];
			snprintf (b, sizeof b, x.len == 3 ? "%02X %02X %02X" : x.len == 2 ? "%02X %02X" : "%02X", x.b[0], x.b[1], x.b[2]);
			script[f + x.offset].push_back (b);
		};
		for (unsigned i = 0; i < nm; i++)
			put (m[i]);
		/* the parameters: the plan's changes in this block, and the echo of the last block's reports;
		 * the MIDI the plugin must give its engine, from instance 0's mirror of its step */
		for (auto& in : inst)
		{
			in->inParams.clearQueue ();
			for (auto& [id, v] : in->echo)
			{
				int32 index = 0, point = 0;
				if (IParamValueQueue* q = in->inParams.addParameterData (id, index))
					q->addPoint (0, v, point);
			}
			in->echo.clear ();
		}
		while (nextParam < p.params.size () && p.params[nextParam].frame < f + n)
		{
			const ParamCmd& c = p.params[nextParam++];
			const double v = (double)c.value / plug_params[plug_paramIndex (c.id)].max;
			for (auto& in : inst)
			{
				int32 index = 0, point = 0;
				if (IParamValueQueue* q = in->inParams.addParameterData (c.id, index))
					q->addPoint ((int32)(c.frame - f), v, point);
			}
		}
		{
			static plug_paramPoint_t pts[1024];
			static plug_midi_t pm[1024];
			const unsigned np = lichomat::pointsOf (&inst[0]->inParams, (int32)n, pts, 1024);
			const unsigned nq = plug_paramsHost (&inst[0]->mirror, pts, np, f, (uint8_t)p.channel, pm, 0, 1024);
			for (unsigned i = 0; i < nq; i++)
				put (pm[i]);
		}
		for (auto& in : inst)
			in->events.clear ();
```

`plugin/host/plug_host.cpp`, edit 10 of 10: replace

```cpp
			readEngineParam (*in);
			const float* l = in->data.outputs[0].channelBuffers32[0];
```

with

```cpp
			readEngineParam (*in);
			readReports (*in, (size_t)(&in - &inst[0]), f, p.echo);
			const float* l = in->data.outputs[0].channelBuffers32[0];
```

`plugin/CMakeLists.txt`: replace

```cmake
target_include_directories(lichomat-plughost PRIVATE "${PLUGIN_DIR}/common" "${LXR_ROOT}/emu/core")
```

with

```cmake
target_include_directories(lichomat-plughost PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/vst3" "${LXR_ROOT}/emu/core")
```

- [ ] **Step 4: they pass.** PLUGIN with REGEX `P10_plugin_params|P10_plugin_outputs|P10_plugin_rtparams|P10_plugin_validator|P10_plugin_native`. Expected: `100% tests passed, 0 tests failed out of 6`; with `-V`: `latency 385, 264639 frames compared, 0 differ; the script: 38 CC messages (2 NRPNs), 1 Program Changes`, `plain: the first report at frame 0, 36 values; then {10: 127, 30: 127, 51: 0} at frames [100352, 101376]` (in Release also [100224, 101376] or [100480, 101248]), the same values for `echo`, `with echo: the same output and the same reports`, `the logs: ['parameters: MIDI channel 1, receive filter: CC on, Program Change on']`; `P10_plugin_outputs`: `init, Drum 1 to St2 at 100,000, out1: 264639 frames compared, 0 differ` and `out2: … 0 differ, 181680 samples not 0 (0 before the change)`; `P10_plugin_params_restart` (paced): `volumes 0: 0 loud samples before the kill, 0 after; without them 77659 after` (the last number the planning run's; any above 10,000 passes); `P10_plugin_rtparams`: `rtcheck: 3750 process calls, 0 forbidden calls, 3750 writes`. The validator still `Result: 47 tests passed, 0 tests failed`.
- [ ] **Step 5: the gate.** PLUGIN-CHECK, T = 91.
- [ ] **Step 6: commit and push.**

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && git add plugin/vst3/plugin_points.h plugin/vst3/plugin_controller.cpp plugin/vst3/plugin_processor.h plugin/vst3/plugin_processor.cpp plugin/vst3/plugin_link.cpp plugin/host/plug_host.cpp plugin/tests/plugin_test.py plugin/CMakeLists.txt && git commit -m "plugin Task 8 (E9.38): 36 automatable parameters: the host's changes reach the firmware as MIDI at their frames, the firmware's own changes reach the host once, a new engine gets the host's values

P10_plugin_params: == render byte for byte (264,639 frames; 38 CC messages, 2 NRPNs among them, 1 Program Change); reports: 36 at frame 0, the kit's 10=127 30=127 at 100,224-100,480 and 51=0 at 101,248-101,376, each once; the same with echo. P10_plugin_rtparams: 3,750 calls, 0 forbidden.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && git push origin plugin-v07
```

---

### Task 9: The values in the state — E9.39

**Files:** Modify `plugin/vst3/plugin_processor.h`, `plugin/vst3/plugin_processor.cpp`, `plugin/tests/plugin_test.py`, `plugin/CMakeLists.txt`.

`getState()` writes the `LCHV` block after the archive, `setState()` reads it (B-9) and hands its values to the audio thread through Task 8's `stateValues` and `restoreGen` (release; `process()` and `setActive()` take them with acquire). `getState()` writes `hostValues`, or `stateValues` while a state set is not taken yet. Without this task `P10_plugin_setstate` passes because a set state takes the card's values (Task 8); with it, the values come from the state, which `P10_plugin_setstate`'s states have too.

- [ ] **Step 1: the failing tests.**

`plugin/tests/plugin_test.py`: replace

```python
# --- the licence gate and the name scan (Task 10) ----------------------------------------------
```

with

```python
@case
def P10_plugin_params_state(t):
    # The values the host has are in the state, after the card (LCHV, all 36: those it set and those the plugin
    # reported): a new host restoring it plays as a host that sets them at frame 0, byte for byte, and reports them
    # at its first block. A state of the prototype (the card only) is taken with no values (every case that gives a
    # card does that); a damaged LCHV block is refused
    lines = ['note %d 0 48 127' % (20000 * k + 5000) for k in range(12)]
    host(t, plan(t, 'make.txt', lines + ['param 0 10 40', 'param 0 50 20', 'param 0 33 10', 'out ' + t.path('make.wav'),
                                         'state-out ' + t.path('state.bin')], frames=blocks_frames(1)))
    data = open(t.path('state.bin'), 'rb').read()
    at = data.rfind(b'LCHV')
    values = dict(struct.unpack('<IB', data[k:k + 5]) for k in range(at + 12, len(data), 5))
    print('the state: %d bytes, the values block at %d: %d values, ids 10, 33, 50: %r' % (len(data), at, len(values),
                                                                                       [values.get(i) for i in (10, 33, 50)]))
    check(at > 0 and data[at:at + 12] == b'LCHV\x01\x00\x00\x00\x24\x00\x00\x00' and len(data) == at + 12 + 36 * 5
          and values.get(10) == 40 and values.get(33) == 10 and values.get(50) == 20, 'the values block %r' % data[at:at + 12])
    _, ref = host(t, plan(t, 'ref.txt', lines + ['param 0 10 40', 'param 0 50 20', 'param 0 33 10', 'out ' + t.path('ref.wav')]))
    _, got = host(t, plan(t, 'got.txt', lines + ['state-in ' + t.path('state.bin'), 'out ' + t.path('got.wav')], card=False))
    d = same_files(t.path('ref.wav'), t.path('got.wav'))
    check(d is None, 'the restored values: %s' % d)
    first = reports(got)[0]
    print('the restored instance reports at frame %d: %r' % (first[0], {k: first[1][k] for k in (10, 33, 50)}))
    check(first[0] == 0 and first[1].get(10) == 40 and first[1].get(50) == 20 and first[1].get(33) == 10, 'reports %r' % (first,))
    with open(t.path('bad.bin'), 'wb') as f:
        f.write(data[:at] + b'LCHV\x02\x00\x00\x00\x00\x00\x00\x00')
    p = subprocess.run([t.a.host, plan(t, 'bad.txt', ['state-in ' + t.path('bad.bin'), 'out ' + t.path('bad.wav')],
                                         card=False)], capture_output=True, text=True, timeout=600, env=host_env(t))
    print('a damaged values block: exit %d, %s' % (p.returncode, p.stderr.strip()))
    check(p.returncode == 1 and 'setState refused' in p.stderr, 'the damaged block was taken')
    check(not instance_dirs(t.path('home')), 'instance folders left: %r' % instance_dirs(t.path('home')))


# --- the licence gate and the name scan (Task 10) ----------------------------------------------
```

`plugin/CMakeLists.txt`: replace

```cmake
        P10_plugin_params_restart P10_plugin_rtparams)
```

with

```cmake
        P10_plugin_params_restart P10_plugin_rtparams P10_plugin_params_state)
```

- [ ] **Step 2: they fail.** PLUGIN with REGEX `P10_plugin_params_state`. Expected: `== plugin build: 0`, then `0% tests passed, 1 tests failed out of 1`: `FAIL P10_plugin_params_state: the values block b''` (the state ends after the card).
- [ ] **Step 3: the code.**

`plugin/vst3/plugin_processor.h`: replace

```c
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8): two stereo outputs, Out 1 and Out 2
 * (plan E9.4), one event input. Each block it turns the host's transport into MIDI clock
 * (plug_clock.h), the block's notes and the host's parameter changes into MIDI (plug_params.h),
 * writes them into the engine's ring at their frames, publishes the block, wakes the engine, and
 * plays the ring plug_readOffset() frames behind: Out 1 and Out 2 are the device's outputs when
 * the host has activated Out 2, else Out 1 is their sum; the reported latency is plug_latency().
 * The firmware's values that changed by themselves go to the host as output parameter changes.
 * In real time it never waits; offline (kOffline: Live's bounce, freeze and export) it waits for
 * the engine. The state is the card folder as an LCHS archive (plug_archive.h).
```

with

```c
 * The LICHOMAT plugin's audio processor (plan E9.2 Task 8): two stereo outputs, Out 1 and Out 2
 * (plan E9.4), one event input. Each block it turns the host's transport into MIDI clock
 * (plug_clock.h), the block's notes and the host's parameter changes into MIDI (plug_params.h),
 * writes them into the engine's ring at their frames, publishes the block, wakes the engine, and
 * plays the ring plug_readOffset() frames behind: Out 1 and Out 2 are the device's outputs when
 * the host has activated Out 2, else Out 1 is their sum; the reported latency is plug_latency().
 * The firmware's values that changed by themselves go to the host as output parameter changes.
 * In real time it never waits; offline (kOffline: Live's bounce, freeze and export) it waits for
 * the engine. The state is the card folder as an LCHS archive (plug_archive.h) and the
 * parameters' values after it (plug_params.h).
```

`plugin/vst3/plugin_processor.cpp`, edit 1 of 2: replace

```cpp
		fprintf (stderr, "LICHOMAT: getState: %s\n", e);
		return kResultFalse;
	}
	return kResultOk;
}
```

with

```cpp
		fprintf (stderr, "LICHOMAT: getState: %s\n", e);
		return kResultFalse;
	}
	/* the parameters' values after the card (plan E9.4): those of the host, else those of the last
	 * state set, which no block has taken yet */
	const bool pending = restoreGen.load (std::memory_order_acquire) != restoreSeen.load (std::memory_order_relaxed);
	int v[PLUG_PARAM_COUNT];
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		v[i] = (pending ? stateValues : hostValues)[i].load (std::memory_order_relaxed);
	return plug_paramsWrite (streamPut, state, v) == 0 ? kResultOk : kResultFalse;
}
```

`plugin/vst3/plugin_processor.cpp`, edit 2 of 2: replace

```cpp
	char e[512];
	if (plug_archiveRead (fresh.card ().c_str (), streamGet, state, e, sizeof e) != 0)
	{
		fprintf (stderr, "LICHOMAT: setState: %s\n", e);
		fresh.remove ();
		return kResultFalse;
	}
	/* a new card: its own values, none of the host's (plan E9.4); the audio thread takes them */
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		stateValues[i].store (-1, std::memory_order_relaxed);
	restoreGen.fetch_add (1, std::memory_order_release);
```

with

```cpp
	char e[512];
	int v[PLUG_PARAM_COUNT];
	if (plug_archiveRead (fresh.card ().c_str (), streamGet, state, e, sizeof e) != 0
	    || (plug_paramsRead (streamGet, state, v) != 0 && snprintf (e, sizeof e, "the parameters' values are damaged") > 0))
	{
		fprintf (stderr, "LICHOMAT: setState: %s\n", e);
		fresh.remove ();
		return kResultFalse;
	}
	for (unsigned i = 0; i < PLUG_PARAM_COUNT; i++)
		stateValues[i].store (v[i], std::memory_order_relaxed);
	restoreGen.fetch_add (1, std::memory_order_release);
```

- [ ] **Step 4: they pass.** PLUGIN with REGEX `P10_plugin_params_state|P10_plugin_state|P10_plugin_setstate|P8_`. Expected: `100% tests passed, 0 tests failed out of 6`; with `-V`: `the state: 4358 bytes, the values block at 4166: 36 values, ids 10, 33, 50: [40, 10, 20]`, `the restored instance reports at frame 0: {10: 40, 33: 10, 50: 20}`, `a damaged values block: exit 1, LICHOMAT: setState: the parameters' values are damaged`.
- [ ] **Step 5: the gate.** PLUGIN-CHECK, T = 92. **Full review of Tasks 7–9** (spec, then quality): B-4, B-7–B-15 against the code; the threads (`process()` against `getState()`, `setState()`, `setActive()`); the real-time list; the mutants of the Rules.
- [ ] **Step 6: commit and push.**

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && git add plugin/vst3/plugin_processor.h plugin/vst3/plugin_processor.cpp plugin/tests/plugin_test.py plugin/CMakeLists.txt && git commit -m "plugin Task 9 (E9.39): the parameters' values in the state after the card (LCHV); a state of the prototype opens with the card's values

P10_plugin_params_state: 4,358 bytes (4,166 the card, 192 the values); restored == set at frame 0, byte for byte; reported at frame 0; a damaged block refused.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && git push origin plugin-v07
```

---

### Task 10: README, version 0.2.0, the final gates — E9.40

**Files:** Modify `plugin/README.md`, `plugin/common/plug_notices.h`, `plugin/vst3/plugin_ids.h`, `plugin/tests/plugin_test.py`, `plugin/CMakeLists.txt`.

- [ ] **Step 1: the failing test.**

`plugin/tests/plugin_test.py`: replace

```python
    check(out.startswith('lichomat-engine 0.1.0, the engine of the LICHOMAT plugin\n'), 'the first line')
```

with

```python
    check(out.startswith('lichomat-engine 0.2.0, the engine of the LICHOMAT plugin\n'), 'the first line')
```

- [ ] **Step 2: it fails.** PLUGIN with REGEX `P6_engine_version`. Expected: `== plugin build: 0`, then `0% tests passed, 1 tests failed out of 1`: `FAIL P6_engine_version: the first line`.
- [ ] **Step 3: the version and the README.**

`plugin/common/plug_notices.h`: replace

```c
#define PLUG_VERSION "0.1.0"
```

with

```c
#define PLUG_VERSION "0.2.0"
```

`plugin/CMakeLists.txt`: replace

```cmake
project(lichomat_plugin VERSION 0.1.0 LANGUAGES C CXX OBJCXX)
```

with

```cmake
project(lichomat_plugin VERSION 0.2.0 LANGUAGES C CXX OBJCXX)
```

`plugin/vst3/plugin_ids.h`: replace

```c
/** the engine's state, read only: 0 stopped, 1 starting, 2 running, 3 failed (the steps of a list
 * of 4, plug_shm.h's PLUG_ENGINE_* in that order) */
```

with

```c
/** the engine's state, read only: 0 stopped, 1 starting, 2 running, 3 failed (the steps of a list
 * of 4, plug_shm.h's PLUG_ENGINE_* in that order). The automatable parameters' ids are in
 * plug_params.c (plan E9.4); none is 0, and none may ever change */
```

`plugin/README.md`, edit 1 of 7: replace

```markdown
- **Notes** from the host (Note On, Note Off) go to the firmware as USB MIDI at their frames.
- **Output:** one stereo bus, the sum of the device's out 1 and out 2 at -6 dB.
- **Rate:** the firmware runs at 44,140.625 Hz; the engine resamples to the host's rate with
  Apple's AudioConverter. At exactly 44,140.625 Hz (the test host) nothing is resampled and the
  output is `lichomat-render --stereo sum`'s, bit for bit.
- **State:** the instance's card folder as one archive (`LCHS`) in the host's set.
```

with

```markdown
- **Notes** from the host (Note On, Note Off) go to the firmware as USB MIDI at their frames.
- **Outputs:** two stereo buses, "Out 1" and "Out 2", the device's two outputs: each voice plays
  where its kit's output (St1, St2, L1, R1, L2, R2 or FX) puts it. Until the host activates Out 2
  (in Live: a track that takes its audio from LICHOMAT's Out 2), Out 1 is the sum of both at
  -6 dB, the prototype's one output, and Out 2 is silent.
- **Parameters:** 36 automatable parameters (below). A change from the host goes to the firmware
  as MIDI at its frame, on the firmware's global MIDI channel, as from an external controller. A
  value the firmware changes by itself (a kit or a pattern loaded) goes back to the host, which then
  shows what plays; a value the host set is not sent back to it.
- **Rate:** the firmware runs at 44,140.625 Hz; the engine resamples the four channels to the
  host's rate with Apple's AudioConverter. At exactly 44,140.625 Hz (the test host) nothing is
  resampled: Out 1 is `lichomat-render --stereo sum`'s output bit for bit, or with Out 2 active,
  Out 1 and Out 2 are `--stereo out1`'s and `--stereo out2`'s.
- **State:** the instance's card folder as one archive (`LCHS`) and the parameters' values
  (`LCHV`) in the host's set. A set saved with the prototype (0.1.0) opens with the card's values.
```

`plugin/README.md`, edit 2 of 7: replace

```markdown
| `BASE/logs/<id>.log` | an instance's log: each engine started and ended (exit, signal, why), underruns, late events; it outlives the instance. The newest 32 logs are kept |
```

with

```markdown
| `BASE/logs/<id>.log` | an instance's log: each engine started and ended (exit, signal, why), underruns, late events, the parameters' MIDI channel and receive filter; it outlives the instance. The newest 32 logs are kept |
```

`plugin/README.md`, edit 3 of 7: replace

```markdown
## Latency
```

with

```markdown
## Parameters

On the firmware's global MIDI channel (channel 1 on the test cards and the `init` card); the
firmware takes CCs and NRPNs with its receive filter's CC on, the Program Change with its Program
Change on, and the instance's log says once per engine what it found. An NRPN is CC 99 (the number
divided by 128), CC 98 (the rest) and CC 6 (the value). The ids are the host's keys for a set's
automation: they never change.

| Id | Parameter | MIDI | Values |
|---|---|---|---|
| 1 | Pattern | Program Change | 1-64 (sent as 0-63): the next pattern; stopped, it plays at once, else at the end of the one that plays, and with the pattern mode "auto" (the cards') its kit comes with it |
| 10-15 | Drum 1, Drum 2, Drum 3, Snare, Cymbal, Hi-hat Volume | CC 89-94 | 0-127 |
| 20-25 | ... Pan | CC 95, 96, 97, 100, 101, 102 | -63..64 |
| 30-35 | ... Filter | CC 38-43 | 0-127 |
| 40-45 | ... Decay (the hi-hat's closed decay) | CC 51, 53, 55, 57, 59, 61 | 0-127 |
| 46 | Hi-hat Open Decay | CC 62 | 0-127 |
| 50 | DJ Filter | NRPN 96 | 0-127, 63-65 flat |
| 51 | Reverb Level | NRPN 98 | 0-127 |
| 52 | Reverb Decay | NRPN 116 | 0-127 |
| 53 | FX Amount | NRPN 108 | 0-100: the kit's FX, the delay's level when that is the delay |
| 60-65 | Drum 1 ... Hi-hat Output | NRPN 87-92 | St1, St2, L1, R1, L2, R2, FX |

Shuffle and the reverb's size are not among them: the firmware sets them only from its own menu,
not over MIDI. A value the host gives settles within a quarter of a second; a change of the
firmware's own in that time (a kit loaded just after an automation point) reaches the host then.

## Latency
```

`plugin/README.md`, edit 4 of 7: replace

```markdown
- A prototype: its checks in Ableton Live (loading, sync, latency, state, freeze and export,
  dropouts) are the owner's, and not all are done yet.
```

with

```markdown
- A prototype: its checks in Ableton Live (loading, sync, latency, state, freeze and export,
  dropouts, the two outputs and the parameters) are the owner's, and not all are done yet.
```

`plugin/README.md`, edit 5 of 7: replace

```markdown
- No parameters but the read-only "Engine": MIDI CC and Program Change are not mapped yet (VST3
  hands them to a plugin as parameters), and the plugin has no window.
```

with

```markdown
- MIDI CCs and Program Changes in the host's clips do not reach the firmware: VST3 hands them to
  a plugin as parameters, and the plugin maps none of them to its own; its automatable parameters
  do (above). The plugin has no window.
- Out 1 is the sum only while the host has not activated Out 2: a host that activates every bus
  at once gets out 1 alone on Out 1 (the owner's check P9 shows what Live does).
- A value the firmware changes by itself goes to the host as the plugin's own change; a host may
  take that as the user's (in Live, an automated parameter may then show its automation as
  overridden: the owner's check P10).
```

`plugin/README.md`, edit 6 of 7: replace

```markdown
| `common/` | C shared by the plugin, the engine and the tests: the shared memory, MIDI clock from a transport, time and latency, the card archive, starting the engine |
| `engine/` | `lichomat-engine`: the renderer's prelude, the tier hook that feeds the events, the resampler |
| `vst3/` | the VST3 processor, controller, factory, the engine link and the instance folders |
```

with

```markdown
| `common/` | C shared by the plugin, the engine and the tests: the shared memory, MIDI clock from a transport, time and latency, the parameters and their step with the firmware (`plug_params`), the card archive, starting the engine |
| `engine/` | `lichomat-engine`: the renderer's prelude, the tier hook that feeds the events and reports the firmware's values, the resampler, the reads of the firmware's memory (`eng_fw.c`, its one file with firmware headers) |
| `vst3/` | the VST3 processor, controller, factory, the engine link, the instance folders, a block's parameter changes as points (`plugin_points.h`) |
```

`plugin/README.md`, edit 7 of 7: replace

```markdown
| `tests/` | the unit tests (P3-P8), `sdk_pin.py`
```

with

```markdown
| `tests/` | the unit tests (P3-P8, P12), `sdk_pin.py`
```

- [ ] **Step 4: it passes.** PLUGIN with REGEX `P6_engine_version|P11_licence`. Expected: `100% tests passed, 0 tests failed out of 2`.
- [ ] **Step 5: the final gates.** PLUGIN-CHECK (92 in both builds, 0 warnings), EMU-CHECK (353), CHECK (the lines Task 1 recorded); `ls ~/Library/Audio/Plug-Ins`, `ls ~/Library/Application\ Support | grep LICHOMAT` and `pgrep -fl 'lichomat-(engine|plughost)'` as in the Rules; the bundle's identity: `plutil -p build/plugin-release/VST3/Release/LICHOMAT.vst3/Contents/Info.plist | grep -E 'CFBundleIdentifier|CFBundleShortVersionString'` gives `org.lichomat.plugin` and `0.2.0`.
- [ ] **Step 6: commit and push.** **Light review** of Task 10.

```bash
cd /Users/tk/Code/lxr/build/wt-plugin-v07 && git add plugin/README.md plugin/common/plug_notices.h plugin/vst3/plugin_ids.h plugin/tests/plugin_test.py plugin/CMakeLists.txt && git commit -m "plugin Task 10 (E9.40): version 0.2.0; README: the two outputs, the 36 parameters and their MIDI, the state's values, the limits

PLUGIN-CHECK 92 of 92 in Debug and Release, 0 warnings; EMU-CHECK 353; CHECK 135 / 353 as at 728dfff.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && git push origin plugin-v07
```

---

### Task 11: The owner's checks P9–P10, handoff, tracker (timing-core) — E9.41

**Files:** Modify `docs/lichomat/plugin-checks.md` (Polish), `docs/lichomat/HANDOFF.md`, `docs/lichomat/tracker.json` (on `timing-core`, in `/Users/tk/Code/lxr`).

- [ ] **Step 1: the checks.** Append the section "E9.4: dwa wyjścia i parametry (P9–P10)" below ("Twoje sprawdzenia P9–P10") to `docs/lichomat/plugin-checks.md` word for word, after its last section, with the branch's head from Task 10 in its first table. Change the file's first comment line to `LICHOMAT: sprawdzenia wtyczki w Abletonie (tracker E9.21, P1–P8; E9.41, P9–P10).`; nothing else in P1–P8 changes.
- [ ] **Step 2: handoff and tracker.** `docs/lichomat/HANDOFF.md`, the plugin's row: E9.4 done on `plugin-v07` (the head, 92 plugin ids, version 0.2.0, the two outputs, the 36 parameters, the owner's P9–P10 open; the install command `cd /Users/tk/Code/lxr/build/wt-plugin-v07 && plugin/tools/install_plugin.sh`). In `docs/lichomat/tracker.json`: rows E9.31–E9.41 done (titles in Polish, as E9.11–E9.21), E9.4 done with the head (shuffle and AU named as left out), a row for the owner's P9–P10 (the next free E8 id, E8.13 at planning) "do zrobienia"; `python3 tools/tracker_note.py "E9.4 (dwa wyjścia, 36 parametrów) zrobione: …"`, `python3 tools/tracker_export.py` and the upload of every file it reports.
- [ ] **Step 3: commit** on `timing-core`: `Docs E9.4: the plugin's owner checks P9-P10, handoff, tracker` with those files by name (and the tracker export's files). **Light review**, every number against Tasks 2–10's recorded output.

---

## Twoje sprawdzenia P9–P10

Task 11 dopisuje tę sekcję do `docs/lichomat/plugin-checks.md` słowo w słowo (w pierwszej tabeli głowica gałęzi z zadania 10).

````markdown
## E9.4: dwa wyjścia i parametry (P9–P10)

| | |
|---|---|
| Wersja | gałąź `plugin-v07`, `<głowica>` (`<data>`), wtyczka 0.2.0 z firmware LICHOMAT 0.7 |
| Co nowego | Dwa wyjścia stereo „Out 1” i „Out 2”, jak dwa gniazda urządzenia. 36 parametrów do automatyki: wzorzec; głośność, panorama, filtr, decay i wyjście każdego głosu (hi-hat osobno zamknięty i otwarty); filtr DJ; poziom i decay pogłosu; ilość FX. Wartości, które firmware zmienia samo (np. kit wczytany razem ze wzorcem), Live pokazuje sam |
| Czego nie ma | Shuffle i rozmiaru pogłosu (firmware zmienia je tylko z własnego menu), AU, własnego okna. CC i Program Change z klipów MIDI nadal nie trafiają do firmware; parametry tak |
| Czas | ok. 15 min |

### Przed sprawdzeniami

1. **Instalacja nowej wersji.** Zamknij Live. W Terminalu:

   ```bash
   cd /Users/tk/Code/lxr/build/wt-plugin-v07 && plugin/tools/install_plugin.sh
   ```

   - Na końcu ma być `47 tests passed, 0 tests failed`. Jeśli skrypt napisze `FAILED`, przyślij mi `build/plugin-release.install.log`.
   - Nowa wersja zastępuje poprzednią: w Live to ta sama wtyczka, sety z P1–P8 otwierają się z nią (z wartościami z karty).
   - Karta startowa zostaje: `~/Library/Application Support/LICHOMAT Plugin/Card`, karta „init”. Wszystkie głosy grają na wyjściu 1, a na pierwszym wzorcu na krokach 1, 5, 9 i 13.
2. **Live** jak w P1–P8: 48 kHz, bufor 128 albo 256 próbek, w menu Options włączone „Delay Compensation”.
3. **Parametry w Live.** Wtyczka nie ma okna, więc Live pokazuje jej parametry jako suwaki: rozwiń je trójkątem w nagłówku urządzenia LICHOMAT. Każdy parametr jest też na liście automatyki ścieżki (w aranżacji: przycisk „A”, potem wybór urządzenia LICHOMAT i parametru). Wartości są takie, jak na wyświetlaczu urządzenia: panorama −63..64, wzorzec 1–64, wyjście głosu St1, St2, L1, R1, L2, R2 albo FX.

### Sprawdzenia

| # | Co zrobić | Co powinno się stać | Zapisz |
|---|---|---|---|
| **P9** (6 min) | Ścieżka MIDI z LICHOMAT, transport gra (sekwencer karty, cztery ćwierćnuty). (a) Parametr **Drum 1 Output** ustaw na **St2**. (b) Dodaj ścieżkę audio; w jej „Audio From” wybierz ścieżkę LICHOMAT, a pod spodem **Out 2**; Monitor „In”. (c) Usuń tę ścieżkę audio. (d) Drum 1 Output z powrotem na **St1**. | (a) Nic się nie zmienia: dopóki Live nie bierze dźwięku z Out 2, ścieżka LICHOMAT gra oba wyjścia razem. (b) Drum 1 gra już tylko na nowej ścieżce, a ścieżka LICHOMAT bez niego (wskaźniki poziomu obu ścieżek). (c) Drum 1 wraca na ścieżkę LICHOMAT albo milknie (zależy od tego, czy Live wyłącza wtedy Out 2). (d) Drum 1 gra na ścieżce LICHOMAT, Out 2 nic. | Co słychać po (a), (b), (c) i (d). Najważniejsze (a): jeśli Drum 1 zniknął już wtedy, Live włącza Out 2 od razu i trzeba to zmienić we wtyczce. |
| **P10** (10 min) | W aranżacji na ścieżce LICHOMAT, transport od taktu 1: (a) automatyka **Drum 1 Volume** od 127 na początku taktu 1 do 0 na początku taktu 3, dalej 0; odtwórz. (b) Bez automatyki przesuń ręką **Reverb Level** na ok. 100 i **DJ Filter** w lewo, potem w prawo. (c) Automatyka **Pattern**: na trzeciej ćwierćnucie taktu 3 wartość 2, na trzeciej ćwierćnucie taktu 6 znów 1; odtwórz od taktu 1. (d) „Freeze” ścieżki i odsłuch. (e) Usuń automatykę Drum 1 Volume, ustaw ręką **Hi-hat Volume** na 40, zapisz set, zamknij Live, otwórz set. | (a) Drum 1 cichnie płynnie razem z linią automatyki. (b) Pogłos słychać; filtr DJ zamyka górę albo dół. (c) Od taktu 4 gra wzorzec 2 (na karcie init sam Drum 1 na ćwierćnuty), od taktu 7 znów wzorzec 1: firmware zmienia wzorzec na końcu grającego, jak z Program Change na urządzeniu. Razem ze wzorcem wczytuje jego kit, więc parametry wracają do wartości kitu i Live dostaje je od wtyczki: suwak Reverb Level wraca na 0, a Drum 1 Volume na 127, choć jego automatyka z (a) stoi na 0 (Drum 1 znów głośno). (d) Zamrożona ścieżka gra to samo co (c). (e) Hi-hat Volume pokazuje 40 i hi-hat gra ciszej, jak przed zapisem. | Czy (a)–(e) jak opisano. Przy (c): czy Drum 1 po zmianie wzorca gra głośno; czy przy Drum 1 Volume zapaliła się w Live czerwona lampka (automatyka nadpisana) albo przycisk „Re-Enable Automation”; i co wolisz po wczytaniu kitu: wartość z kitu (jak teraz, jak na urządzeniu) czy ostatnią z automatyki Live'a. |

Jeśli parametr nie działa: w logu instancji (`~/Library/Application Support/LICHOMAT Plugin/logs/<id>.log`, jak w P8) jest linia `parameters: MIDI channel …, receive filter: …`. Na karcie init: `MIDI channel 1, receive filter: CC on, Program Change on`. Przyślij mi tę linię z logiem.
````

## Draft coverage

| Draft §7 / brief | Where |
|---|---|
| §7.1 two stereo buses "Out 1", "Out 2", as the device's jacks; voices by their kit's output | Tasks 3–4 (the engine's four channels, `P6_engine_split`, `P7_engine_split`), 7 (`P10_plugin_buses`, `_buses48`), 8 (`P10_plugin_outputs`); B-2–B-4 |
| §7.1 Out 1 the sum until the host activates Out 2 | Task 7 (B-4); every prototype case (the sum, bit for bit); the owner's P9 (a) |
| §7.1 the engine resamples four channels | Task 2 (`P7_converter_channels`), Task 3 (the sink); B-3 |
| brief: a protocol version bump and how a mismatch is refused | Task 3 (`P3_shm_header`, `P3_shm_create`); B-2 |
| §7.2 about 30 automatable parameters: volume, pan, filter, decay of each voice, the DJ filter, reverb, delay, shuffle, the pattern | Tasks 5, 8; B-10, "The parameters"; deviations 1–5 (shuffle out, delay = FX amount, reverb level and decay, two hi-hat decays, six outputs) |
| §7.2 the pattern 0–63 as a Program Change on the global channel | Tasks 5, 6 (`P6_engine_params`), 8 (`P10_plugin_params`); B-5, B-8; F5 |
| §7.2 a change reaches the firmware at its sample; which CC or NRPN | Tasks 5, 6, 8 (bit for bit with the renderer playing the same MIDI); B-5; "The parameters" |
| §7.2 the engine reports the firmware's values; Live shows what plays after a kit or pattern load; no feedback loop | Tasks 3 (the table), 6 (the engine's report), 5 (`P12_params_*`), 8 (`P10_plugin_params` with and without `echo`); B-6–B-8; "The values: host, firmware, no loop" |
| brief: ids stable across versions; the class ids those of `plugin` | B-10, B-16; `P12_params_table` (unique, none 0); `plugin_ids.h` unchanged but a comment |
| §7.3 one engine per instance, state in the set, latency, the prototype's tests | B-1, B-9 (the state extended), B-19; `P10_plugin_latency`, `_state`, `_renew` and the rest unchanged |
| brief: `plug_rtcheck` at 0 forbidden calls with automation running | Task 8 (`P10_plugin_rtparams`); B-15 |
| §7.4 the owner's P9–P10 in `plugin-checks.md` | Task 11, "Twoje sprawdzenia P9–P10" |

## Risks

- **Live is untested here**, as for the prototype. Two behaviours decide how E9.4 feels in Live, and P9–P10 check them:
  - *Out 2's activation.* If Live activates every output bus of a VST3 instrument when it loads it, Out 1 carries out 1 alone from the start and a voice on out 2 is heard only on a track that takes Out 2 (P9 (a)). The remedy, if P9 shows it: a list parameter "Out 2: separate / summed" (id 70), which decides instead of the activation; small, in the processor alone.
  - *A value the firmware changes on an automated parameter.* A kit loaded with a pattern sets its parameters, also one whose automation in Live stands still (Live sends no new point), so the kit's value plays until the next point, as on the device with an external sequencer; the plugin reports it as its own output parameter change, which Live may take as the user's and mark the automation overridden (P10 (c)). That is a choice for the owner after P10: keep it (the device's way), or give a parameter the host has set the host's last value again after a kit load (a rule in `plug_paramsReport()`: the host wins on every report, not only an engine's first), at the cost of Live's display no longer showing a kit's values for those parameters.
- **The settle time.** A firmware change of a parameter the host set less than a quarter of a second before reaches the host late (B-7). A slower firmware path (a kit load during an SD write) longer than that would show the old value for a block, then the new one: a flicker, not a loop.
- **The cards' global channel.** It is channel 1 on every card made by the firmware with the default globals (F11). A card whose global channel is set (1–16) works the same (the engine reads it); a card whose receive filter drops CC or Program Change cannot be set from the host, and the log says so (B-13).
- **A kit load in the middle of a report.** The engine reads the values from the hook, which may run during a kit load (HAL_Delay); the report may then hold part of the new kit and the next block's the rest (`eng_fw.h`). Each value is whole; the host may see two changes for one load.
- **The state is larger** by 192 bytes; a set with many instances saves the card per instance as before (the prototype's risk).
- **Shuffle** and the reverb's size need a firmware NRPN (a later version) or the panel (E9.5); the owner may ask for them after P10.

## Changes during execution

(Filled in during execution: measured values that differ from this plan, review findings and their fixes, tracker notes.)

