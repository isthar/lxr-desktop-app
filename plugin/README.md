<!--
Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
-->

# LICHOMAT plugin (prototype)

A VST3 and Audio Unit instrument for Ableton Live, Logic and other hosts on macOS with Apple
silicon that plays LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02: the same firmware code as on the device, run by the
emulator's virtual device (`emu/`). It is an unofficial emulation, not a product of Erica Synths,
and not affiliated with, approved or supported by the maker of the device.

## What it does

- **Two formats, one plugin:** `LICHOMAT.vst3` and `LICHOMAT.component`, an Audio Unit v2
  instrument (below, "The Audio Unit"). Both are the same processor, engine, outputs, parameters
  and state; a set's state from one opens in the other.
- **One engine process per instance.** The plugin (`LICHOMAT.vst3`) is a thin layer in the host's
  process; `Contents/Helpers/lichomat-engine` runs the firmware. They share memory (an event ring,
  an audio ring of the device's four channels and a table of the firmware's parameter values) and
  two pipes: one wakes the engine each block, the other's end tells the plugin that the engine is
  gone. An engine that dies or hangs is started again after 0.5 s, the plugin silent meanwhile;
  after 5 deaths in a minute, or 3 stalls in 10 minutes (an engine that hung, did not start in 30 s
  or kept an offline render waiting 10 s), it gives up (the read-only parameter "Engine" shows
  "failed").
- **Sync** is MIDI clock with Song Position, made from the host's transport at the exact frames:
  Start at position 0, Song Position and Continue elsewhere, and Stop, Song Position and Continue
  at a jump (a loop's end). The firmware plays as a MIDI-clock slave. Each new engine (the first,
  one started again, one for a state set while active) first gets the host's parameter values that
  differ from its card's; when any did, its clock waits at least 1,024 frames at 48 kHz (21.3 ms
  at any rate, rounded up to whole blocks) after them and then resumes on the next 16th, and the
  host's notes in that time are left out; not so for the first engine of an offline render (see
  "Known limits").
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
- **Offline** (bounce, freeze, export) the plugin waits for its engine, 10 s at most for each
  engine.

## Build

With the Command Line Tools (`xcode-select --install`) and CMake and Ninja in the repository's
`.toolchain/` (`tools/setup-toolchain.sh`; `tools/env.sh` puts them on PATH), from the repository's
root:

```bash
source tools/env.sh
plugin/tools/plugin_check.sh         # Debug with UBSan and Release, every test, 0 warnings
```

The bundles are then `build/plugin-release/VST3/Release/LICHOMAT.vst3` and
`build/plugin-release/AU/LICHOMAT.component`. The build needs no network and no Xcode: the parts of
the VST 3 SDK and of Apple's AudioUnitSDK it uses are in `plugin/third_party/vst3sdk/` and
`plugin/third_party/AudioUnitSDK/`, each pinned by its `SHA256SUMS`.

## Install and uninstall

Quit Live and Logic first: the script refuses while a `lichomat-engine` runs (a host with the
plugin in a set). From the repository's root:

```bash
plugin/tools/install_plugin.sh                 # into ~/Library/Audio/Plug-Ins/VST3 and .../Components
plugin/tools/install_plugin.sh --uninstall     # removes LICHOMAT.vst3 and LICHOMAT.component there
plugin/tools/install_plugin.sh --help          # prints the destinations, touches nothing
```

The install builds the Release bundles (the build's output in `build/plugin-release.install.log`),
copies them to a temporary folder and checks them there: their ad hoc signatures with
`codesign --verify --deep --strict` and the VST 3 SDK's validator (its `Result:` line is printed).
Then the component replaces an earlier `LICHOMAT.component` in `LICHOMAT_COMPONENT_DIR`, else
`~/Library/Audio/Plug-Ins/Components`, and Apple's validator runs on it there, the only place it can
find it: `auval -v aumu Lcht Lchm` (its `AU VALIDATION SUCCEEDED.` line is printed; if it does not
find the new component yet, the script restarts the system's `AudioComponentRegistrar` and asks
again once). A component that fails is taken out and the earlier one put back. Only then does the
VST3 bundle replace an earlier `LICHOMAT.vst3` in the destination (a folder given as the first
argument, or `LICHOMAT_VST3_DIR`, else `~/Library/Audio/Plug-Ins/VST3`). On any failure it prints
`install_plugin: FAILED (...), ...; see <log>`, saying which bundle is as it was, and the earlier
bundles stay. An unknown option stops it before it builds anything. If the plugin has no start card
yet it puts the `init` fixture (four on the floor on all seven voices) into `BASE/Card`.

In Live: Settings > Plug-Ins, turn on "Use VST3 Plug-in System Folders" (the plugin is in
`~/Library/Audio/Plug-Ins/VST3`), then Rescan, or start Live again. LICHOMAT, an instrument, is then
among the browser's Plug-Ins, under its vendor name LICHOMAT.

In Logic: Logic checks new Audio Units when it starts (Settings > Plug-in Manager lists LICHOMAT
under its manufacturer LICHOMAT); the instrument is then in a software instrument track's menu,
AU Instruments > LICHOMAT > LICHOMAT, as Stereo or Multi-Output.

The uninstall (Live and Logic quit too) removes the two bundles only. The plugin's folder `BASE`
(the start card and the logs) stays; remove `~/Library/Application Support/LICHOMAT Plugin` by hand
to remove them too.

## Folders and logs

`BASE` is `~/Library/Application Support/LICHOMAT Plugin`, or `LICHOMAT_PLUGIN_HOME` if set (the
tests set it).

| Path | What |
|---|---|
| `BASE/<id>/` | an instance's folder, while it lives: `card/` (its engine's card) and `lock` |
| `BASE/logs/<id>.log` | an instance's log: each engine started and ended (exit, signal, why), underruns, late events, the parameters' MIDI channel and receive filter; it outlives the instance. The newest 32 logs are kept |
| `BASE/Card` | the start card of a new instance, if present |

An instance removes its folder when the host closes it. A folder whose lock nobody holds and that
is older than a minute is left from a host that died, and the next instance removes it.

`LICHOMAT_PLUGIN_MARGIN` (16-4096 host frames) sets the margin of the latency below.

## A new instance's card

1. `BASE/Card`, if there is one (the install puts the `init` fixture there);
2. else LICHOMAT Emu's card, `~/Library/Application Support/LICHOMAT Emu/Card` (or
   `LICHOMAT_EMU_HOME/Card`), so that the plugin starts where the app left off;
3. else an empty card, which the engine seeds with the init card.

The card is copied in the card's order (the files' birth times, which the firmware's directory
listings follow). An instance whose set holds its state starts on that state's card instead.

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

## The Audio Unit

`LICHOMAT.component` is an AU v2 instrument of type `aumu`, subtype `Lcht`, manufacturer `Lchm`,
"LICHOMAT: LICHOMAT", built on Apple's AudioUnitSDK around the VST3's processor (`au/`): the same
engine per instance, the same outputs, parameters, latency and state.

- **Out 1 and Out 2** are its two output elements. AU v2 has no bus activation: Out 2 counts as
  used while the host renders it. Until then, and from two render cycles after the host stops,
  Out 1 is the sum of both outputs, as the VST3's Out 1 before Out 2 is activated. A host may also
  take one output element (Out 1 alone, always the sum).
- **Parameters** have the VST3's ids, and their values are the device's numbers: the pattern
  1-64, a pan -63..64, a voice's output St1 ... FX by name. A value the firmware changes by itself
  becomes the parameter's value at once and the host's listeners hear of it within 20 ms.
  Parameter events the host schedules (`AudioUnitScheduleParameters`) reach the firmware at their
  frames, a ramp as its two ends; a value set (`AudioUnitSetParameter`) at its offset in the next
  render, the last one of a parameter in a render winning.
- **Notes** are the host's MIDI Note Ons and Offs (`MusicDeviceMIDIEvent`) at their offsets (from any thread, through a lock-free queue the render thread empties; a reset drops the notes queued before it). The AU
  declares no MIDI 2.0 protocol, so hosts send it MIDI 1.0. As for the VST3, other MIDI from the
  host (CCs, Program Changes) does not reach the firmware.
- **Sync** comes from the host callbacks: the beat and the tempo, the transport state (its second
  form if the host has it) with the cycle. A host without them gets notes but no sync.
- **State:** the AU's ClassInfo holds the VST3's state bytes (`LCHS` and `LCHV`) under the key
  `lichomat-state`, next to the keys every AU's state has. A ClassInfo given back wins over the values set before it that no render has taken yet; a value set after it reaches the firmware.
- **Latency** is the VST3's, with the host's maximum frames per render
  (`kAudioUnitProperty_MaximumFramesPerSlice`) as the largest block. **Offline** rendering
  (`kAudioUnitProperty_OfflineRender`, Logic's bounce) waits for the engine as the VST3's offline
  mode does; the AU takes it before or after it is initialized.
- The component is not sandbox safe: it starts a process and writes `BASE`. A sandboxed host runs
  it out of its own process or not at all.

## Latency

An event at host frame m sounds at frame m + the reported latency (one frame less on an odd
frame at the native rate):

```
latency     = sound delay + read offset
sound delay = 128 engine frames (the firmware's voice queue) at 44,140.625 Hz,
              round(176 x rate / 44,140.625) resampled (the converter adds 48 engine frames)
read offset = largest block + margin + 1                               at 44,140.625 Hz
            = largest block + margin + ceil(rate / 44,140.625) + 1     resampled
margin      = 128 host frames
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

## Known limits

- macOS 13 or later on Apple silicon (arm64) only; VST3 and AU v2 (no AU v3, no CLAP).
- A prototype: its checks in Ableton Live (loading, sync, latency, state, freeze and export,
  dropouts, the two outputs and the parameters) are the owner's, and not all are done yet.
- A jump of the transport (a loop's end, a locate while playing) reaches the firmware as Stop,
  Song Position and Continue: the sequencer restarts from the new 16th.
- MIDI CCs and Program Changes in the host's clips do not reach the firmware: VST3 hands them to
  a plugin as parameters, and the plugin maps none of them to its own; its automatable parameters
  do (above). The plugin has no window.
- Out 1 is the sum only while the host has not activated Out 2: a host that activates every bus
  at once gets out 1 alone on Out 1 (the owner's check P9 shows what Live does).
- A value the firmware changes by itself goes to the host as the plugin's own change; a host may
  take that as the user's (in Live, an automated parameter may then show its automation as
  overridden: the owner's check P10).
- The AU's latency counts the host's maximum frames per render, which a host may set larger than
  the blocks it renders (Logic's process buffer): the latency is then that much larger, and
  compensated as any.
- The first engine of an offline render (a bounce, an export or a freeze from a new activation)
  gets the set's values at the render's first frame, with the transport, and the firmware takes a
  few milliseconds to settle them: a note at that very frame can play at the card's level for 2-3 ms
  where a value differs much from the card's (a voice turned to 0).
- At 44.1 and 48 kHz the latency stays within 10 ms above the voice queue's 2.90 ms only with host
  buffers of up to 256 frames (table above).
- The plugin's process installs no signal handler; the engine runs in its own process, so a
  firmware fault ends the engine, not the host.

## Layout

| Path | What |
|---|---|
| `common/` | C shared by the plugin, the engine and the tests: the shared memory, MIDI clock from a transport, time and latency, the parameters and their step with the firmware (`plug_params`), the card archive, starting the engine |
| `engine/` | `lichomat-engine`: the renderer's prelude, the tier hook that feeds the events and reports the firmware's values, the resampler, the reads of the firmware's memory (`eng_fw.c`, its one file with firmware headers) |
| `vst3/` | the VST3 processor, controller, factory, the engine link, the instance folders, a block's parameter changes as points (`plugin_points.h`) |
| `au/` | the Audio Unit: `au_unit.cpp` (a MusicDeviceBase around the VST3's processor), `au_lists.h` (its fixed event list and parameter changes), its `Info.plist.in`; `common/plug_au.c` holds its pure functions |
| `host/` | `lichomat-plughost`, the test host on the SDK's hosting classes, and `lichomat-auhost`, the AU's, which loads the component into its own process only; `host_plan.h`, what they share |
| `tests/` | the unit tests (P3-P8, P12, P13), `sdk_pin.py` (P2, the SDKs' pins), `plugin_test.py` (P6, P7, P10, P11, P13), its tools `plug_drive` (the engine without the plugin) and `plug_resample_wav` (the converter on a WAV), the real-time check `plug_rtcheck.c`, the licence gate `plugin_licence_gate.py` |
| `tools/` | `plugin_check.sh` (the gate), `install_plugin.sh`, `vendor_vst3sdk.sh`, `vendor_ausdk.sh` |
| `third_party/vst3sdk/` | the VST 3 SDK 3.8.1 (build 84), the part the plugin builds with, pinned by `SHA256SUMS` |
| `third_party/AudioUnitSDK/` | Apple's AudioUnitSDK 1.4.0, its headers, sources and licence, pinned by `SHA256SUMS` |

## Licence and source

GPL-3.0-only with the additional permission and terms in `NOTICE.md`, as the rest of LICHOMAT;
`LICENSE` and `NOTICE.md` are also in the bundles' `Contents/Resources/`. The corresponding source
of a build is this repository at the commit it was built from, the vendored SDKs included:
`https://github.com/isthar/lxr-desktop-app`, branch `plugin-v07` (the plugin with LICHOMAT 0.7; branch
`plugin` has it with 0.6). The bundle's engine names that commit:

```bash
~/Library/Audio/Plug-Ins/VST3/LICHOMAT.vst3/Contents/Helpers/lichomat-engine --version
```

prints, after its name, `Runs LICHOMAT 0.7. Built from commit <12 hex digits>.` (the component's
`Contents/Helpers/lichomat-engine` the same). If you received
the plugin without its source, ask the person who gave it to you for that commit, or build it from
the repository yourself.

Third-party code, each with its notice in the bundles' `Contents/Resources/`:

| Code | Licence | In the repository | In the bundles |
|---|---|---|---|
| the VST 3 SDK (the plugin, the AU's processor and the test hosts), Steinberg Media Technologies GmbH | MIT | `plugin/third_party/vst3sdk/LICENSE.txt` | `VST3-SDK-LICENSE.txt` |
| AudioUnitSDK (the AU), Apple Inc. | Apache-2.0 | `plugin/third_party/AudioUnitSDK/LICENSE.txt` | `AudioUnitSDK-LICENSE.txt` (the component only) |
| Arm Optimized Routines (the engine's maths, `emu/libm/`), Arm Limited | MIT OR Apache-2.0 WITH LLVM-exception | `emu/libm/LICENSE` | `LICENSE.arm-optimized-routines` |
| FatFs `ff.h` and `integer.h` (the firmware's card), ChaN | ChaN's FatFs licence | the first 18 lines of `Middlewares/Third_Party/FatFs/src/ff.h` | `LICENSE.fatfs` |

VST is a trademark of Steinberg Media Technologies GmbH; the plugin uses no VST logo. The names of
the device and its maker belong to their owner and are used here only to say what the firmware
runs on; see `NOTICE.md`.
