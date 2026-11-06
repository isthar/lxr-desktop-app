<!--
Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
-->

# LICHOMAT plugin (prototype)

A VST3 instrument for Ableton Live on macOS with Apple silicon that plays LICHOMAT, unofficial
custom firmware for the Erica Synths LXR-02: the same firmware code as on the device, run by the
emulator's virtual device (`emu/`). It is an unofficial emulation, not a product of Erica Synths,
and not affiliated with, approved or supported by the maker of the device.

## What it does

- **One engine process per instance.** The plugin (`LICHOMAT.vst3`) is a thin layer in the host's
  process; `Contents/Helpers/lichomat-engine` runs the firmware. They share memory (an event ring
  and an audio ring) and two pipes: one wakes the engine each block, the other's end tells the
  plugin that the engine is gone. An engine that dies or hangs is started again after 0.5 s, the
  plugin silent meanwhile; after 5 deaths in a minute, or 3 stalls in 10 minutes (an engine that
  hung, did not start in 30 s or kept an offline render waiting 10 s), it gives up (the read-only
  parameter "Engine" shows "failed").
- **Sync** is MIDI clock with Song Position, made from the host's transport at the exact frames:
  Start at position 0, Song Position and Continue elsewhere, and Stop, Song Position and Continue
  at a jump (a loop's end). The firmware plays as a MIDI-clock slave.
- **Notes** from the host (Note On, Note Off) go to the firmware as USB MIDI at their frames.
- **Output:** one stereo bus, the sum of the device's out 1 and out 2 at -6 dB.
- **Rate:** the firmware runs at 44,140.625 Hz; the engine resamples to the host's rate with
  Apple's AudioConverter. At exactly 44,140.625 Hz (the test host) nothing is resampled and the
  output is `lichomat-render --stereo sum`'s, bit for bit.
- **State:** the instance's card folder as one archive (`LCHS`) in the host's set.
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

The bundle is then `build/plugin-release/VST3/Release/LICHOMAT.vst3`. The build needs no network:
the part of the VST 3 SDK it uses is in `plugin/third_party/vst3sdk/`.

## Install and uninstall

```bash
plugin/tools/install_plugin.sh                 # into ~/Library/Audio/Plug-Ins/VST3
plugin/tools/install_plugin.sh --uninstall     # removes ~/Library/Audio/Plug-Ins/VST3/LICHOMAT.vst3
plugin/tools/install_plugin.sh --help          # prints the destination, touches nothing
```

The install builds the Release bundle, copies it to the destination (a folder given as the first
argument, or `LICHOMAT_VST3_DIR`, else `~/Library/Audio/Plug-Ins/VST3`), replacing an earlier
`LICHOMAT.vst3` there, checks its ad hoc signature with `codesign --verify --deep --strict` and
runs the SDK's validator on it. If the plugin has no start card yet it puts the `init` fixture
(four on the floor on all seven voices) into `BASE/Card`. Live finds the plugin after a rescan of
its plug-ins, or at its next start.

The uninstall removes the bundle only. The plugin's folder `BASE` (the start card and the logs)
stays; remove `~/Library/Application Support/LICHOMAT Plugin` by hand to remove them too.

## Folders and logs

`BASE` is `~/Library/Application Support/LICHOMAT Plugin`, or `LICHOMAT_PLUGIN_HOME` if set (the
tests set it).

| Path | What |
|---|---|
| `BASE/<id>/` | an instance's folder, while it lives: `card/` (its engine's card) and `lock` |
| `BASE/logs/<id>.log` | an instance's log: each engine started and ended (exit, signal, why), underruns, late events; it outlives the instance. The newest 32 logs are kept |
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

- macOS 13 or later on Apple silicon (arm64) only; VST3 only.
- A prototype: its checks in Ableton Live (loading, sync, latency, state, freeze and export,
  dropouts) are the owner's, and not all are done yet.
- A jump of the transport (a loop's end, a locate while playing) reaches the firmware as Stop,
  Song Position and Continue: the sequencer restarts from the new 16th.
- No parameters but the read-only "Engine": MIDI CC and Program Change are not mapped yet (VST3
  hands them to a plugin as parameters), and the plugin has no window.
- The latency stays within 10 ms above the voice queue's 2.90 ms only with host buffers of up to
  256 frames (table above).
- The plugin's process installs no signal handler; the engine runs in its own process, so a
  firmware fault ends the engine, not the host.

## Layout

| Path | What |
|---|---|
| `common/` | C shared by the plugin, the engine and the tests: the shared memory, MIDI clock from a transport, time and latency, the card archive, starting the engine |
| `engine/` | `lichomat-engine`: the renderer's prelude, the tier hook that feeds the events, the resampler |
| `vst3/` | the VST3 processor, controller, factory, the engine link and the instance folders |
| `host/` | `lichomat-plughost`, the test host on the SDK's hosting classes |
| `tests/` | the unit tests (P3-P8), `plugin_test.py` (P6, P7, P10, P11), `plug_drive`, the real-time check, the licence gate |
| `tools/` | `plugin_check.sh` (the gate), `install_plugin.sh`, `vendor_vst3sdk.sh` |
| `third_party/vst3sdk/` | the VST 3 SDK 3.8.1 (build 84), the part the plugin builds with, pinned by `SHA256SUMS` |

## Licence and source

GPL-3.0-only with the additional permission and terms in `NOTICE.md`, as the rest of LICHOMAT;
`LICENSE` and `NOTICE.md` are also in the bundle's `Contents/Resources/`. The corresponding source
of a build is this repository at the commit it was built from, the vendored SDK included:
`https://github.com/isthar/lxr-desktop-app`, branch `plugin`. If you received the plugin without
it, ask the person who gave it to you for that commit, or build it from the repository yourself.

Third-party code, each with its notice in the bundle's `Contents/Resources/`:

| Code | Licence | In the repository | In the bundle |
|---|---|---|---|
| the VST 3 SDK (the plugin and the test host), Steinberg Media Technologies GmbH | MIT | `plugin/third_party/vst3sdk/LICENSE.txt` | `VST3-SDK-LICENSE.txt` |
| Arm Optimized Routines (the engine's maths, `emu/libm/`), Arm Limited | MIT OR Apache-2.0 WITH LLVM-exception | `emu/libm/LICENSE` | `LICENSE.arm-optimized-routines` |
| FatFs `ff.h` and `integer.h` (the firmware's card), ChaN | ChaN's FatFs licence | the first 18 lines of `Middlewares/Third_Party/FatFs/src/ff.h` | `LICENSE.fatfs` |

VST is a trademark of Steinberg Media Technologies GmbH; the plugin uses no VST logo. The names of
the device and its maker belong to their owner and are used here only to say what the firmware
runs on; see `NOTICE.md`.
