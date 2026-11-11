# LXR V2 firmware (desktop, STM32F765VG)

Firmware for the LXR V2 desktop drum machine: an STM32F765 (Cortex-M7, 216 MHz) running the
drum voices, FX bus, step sequencer, MIDI (DIN + USB), SD-card preset storage and the LCD/button UI.

> **Licence and trademarks:** the firmware source code is free software under the GNU GPL version 3
> ([LICENSE](LICENSE)). You're welcome to study, change and share it, for example to write your own
> updates for your LXR. The names **"Erica Synths" and "LXR"** and the **LXR hardware** are the property
> of Erica Synths and are **not** covered by this licence. Building and installing your own firmware is
> at your own risk: Erica Synths provides no support for self-built or modified firmware. See
> [Licence, trademarks and hardware](#licence-trademarks-and-hardware) below and [NOTICE.md](NOTICE.md).

## LICHOMAT

This fork is **LICHOMAT**, unofficial custom firmware for the Erica Synths LXR-02. It is not made,
approved or supported by Erica Synths. It is based on the official firmware 1.7
([erica-synths/lxr-desktop-app](https://github.com/erica-synths/lxr-desktop-app) at commit `a43cd4f`)
and is aimed at groove techno: rolling percussion, swung hats over a straight kick, polymetric loops.

Version 0.4 adds atmosphere. A reverb runs after the FX slot, so the delay's echoes are
reverberated: level, size, decay, tone, predelay, modulation and freeze, on a new FX sub-page
(`lvl siz dec ton pre mod frz syn`; on the FX page SHIFT + step 15 steps from the slot's sub-page to
the reverb, then to `dck ddp djf djr`, then back). The tone goes from a dark low-pass of about 1.5 kHz
at 0, through flat at 64, to a gentle high-pass of about 400 Hz at 127. Freeze holds the tail, losing
about 0.3 dB a minute: SHIFT + step 3 in the performance mode, or NRPN 99. Rumble ducks the reverb by
one voice (`dck` picks it, `ddp` the depth). The delay can follow the tempo (`syn`, 1/16 triplet to
1/4 dotted); a time longer than the delay line is halved until it fits. A DJ filter on both outputs
(`djf`, `djr`, also on the performance page) is a low-pass to the left of the centre, flat at 63-65
and a high-pass to the right. The DJ filter, its resonance, the reverb level, freeze and decay are
reached by the menu and by NRPN 96-99 and 116; LFOs and velocity cannot target them. The other reverb
settings are menu-only. **The reverb runs at half rate for now: the rate is provisional until the
owner's CPU readings on the device (H12, U20) fix it.**

Kits saved with 0.4 carry the new settings in a block after the 247 parameter bytes (a kit file
grows from 255 to 268 bytes). Firmware 1.7 loads them, ignores the block and plays the kit with its
own FX slot; a synced delay plays its free time there. A kit re-saved with 1.7 loses the block, and
0.4 then uses the defaults (reverb off, DJ filter flat). Kits from 1.7 and from 0.1-0.3 load with
those defaults too. A saved kit always stores the DJ filter flat and freeze off. `glo.cfg` stays 30
bytes.

Version 0.5 starts each voice at the sample of its event. Up to 0.4 a voice started with the next
audio block of 64 samples (1.45 ms), so a hit moved by up to a block against its time. Now the
shifts, the swing, humanize, ratchets and rolls, each track's own tempo, the MIDI notes coming in
over DIN and USB and the playing as a clock slave all land where they belong. From event to sound
there is a constant 2.9 ms (128 samples), on average 0.73 ms later than 0.4 against the MIDI out and
the clock out jack. The setting `trg` on the second sub-page of the global menu (hold SHIFT and
press step button 12; after `cko`) chooses `smp`, the new timing, or `blk`, the timing of 0.4. It is
saved in byte 9 of `glo.cfg`, which 1.7 ignores. If `trg` is switched from `smp` to `blk` while
playing, the hits already scheduled in `smp` (about 1.5 blocks, 2 ms) still start after the first
`blk` hits, so for that moment an older hit can restart a voice over its newer one.

Version 0.6 adds microtiming and groove templates. Each step has its own timing `mic`, in ticks of
1/96 of the track's step: on the step page's second screen (`flm shf mic`, after `shf`), `-84` …
`+84`, 0 on the grid. The encoder and the knob step it one tick at a time; in edit mode the top row
reads `Step` and `Micro`, and the bottom row shows the offset in ms at the current tempo and the
track's own tempo. `shf` is now the coarse view of `mic` (a unit of `shf` is 1/8 step, 12 ticks):
a v0.5 step with `shf` +2 shows `mic` +24, and a `shf` edit keeps the fine part. Each voice of a pattern
can also follow a groove template, on the third screen of the track page (select a voice, SHIFT +
the second mode button, then the encoder past `dly`): `tpl` and `amt`, 0–200 % of the template.
The templates move the sixteen 16ths of a bar, the quarters stay on the grid:

- `swg` (even swing): every second 16th 16 ticks late; at 56 % about 10 ms at 142 BPM;
- `lay` (lay back): the 16ths after each quarter 18 ticks late, the ones before it 5;
- `lyb` (lay bar): the same feel over the whole bar, each position its own value;
- `rlx` (relax bar): a relaxed bar with the "&" a little late too;
- `2bt` (two beat): beats 1 and 3 swung, beats 2 and 4 nearly straight;
- `psh` (push off): the offbeat 8ths 4 ticks early, a pushing hat;
- `drg` (drag off): the offbeat 8ths 6 ticks late, a laid-back hat.

`lay`, `lyb`, `rlx` and `2bt` are measured from reference tracks at 142–143 BPM. The step's
`mic`, its `shf` and the template add up and stop at 7/8 of a step either way; the shuffle, the
track delay and humanize come on top, as before. A step that would sound after the next step's first
hit on its track is cut by it. Recording places each note with its microtiming, less the track's
template, so the take plays back as it was played; `rqn` on the recording page (SHIFT + REC, after
`aut`) records onto the grid as 0.5 did. `rqn` is saved in byte 19 of `glo.cfg`. The template's
position restarts with the pattern, while the swing flips at each loop of a pattern of odd length, so
there the two can disagree.

Patterns saved with 0.6 carry the fine parts and the templates in a second block after 0.2's block,
only when they are used, so a pattern without them is the file of 0.5 byte for byte. 0.2–0.5 read
the first block and play each step at its coarse `shf`, the nearest 1/8 step; 1.7 plays the coarse
`shf` only and loses the tempo, humanize, delay, fine parts and templates. Files from 1.7 and from
0.2–0.5 load in 0.6 as they played there. Kits, projects, songs and `glo.cfg` keep their sizes.

Version 0.7 adds groove dynamics. Each voice of a pattern can follow a velocity template, on the
fourth screen of the track page (the encoder past `amt`, or SHIFT + the second mode button once more
from the third screen): `vtp` and `vdp`, 0–200 % of the template. A template lowers the sixteen 16ths
of a bar by a number of dB below the step's own volume; at 0 % nothing changes, at 200 % the dB
double. The four templates are measured on the hats of the reference album, and in each the offbeat
8ths are the loudest (0 or −1 dB):

- `of8` (offbeat 8th): the median of 8 tracks; the quarters 10 dB down, the 16ths between them
  13–16 dB down;
- `ful` (full 16th): two tracks of dense 16ths; the quarters 6–7 dB down, the 16ths 5–15 dB;
- `dep` (deep ghost): one track with deep ghosts; the quarters 13–15 dB down, the 16ths 11–21 dB;
- `qtr` (quarters): the quarters just below the offbeat 8ths, 5–6 dB down, the 16ths 15–20 dB.

The template scales each step once, so a step's ratchets share it, before humanize's volume (`hmv`);
it reaches the MIDI Note On velocity too, and a step of volume 0 stays silent. **A velocity changes a
voice's loudness only with that voice's `vol` (velocity → volume) on**, on the voice page's velocity
screen `dst amt vol`; the velocity modulation and MIDI out get it either way. Recording stores the
velocity as played and the template plays on top of it, so record with `vtp off`. Rolls play at their
fixed velocity, without the template.

Four own templates per project, `us1`–`us4`, hold a timing and a velocity value for each of the sixteen
positions. `sav`, on the same screen, captures the selected voice's track: in edit mode turn it to a
slot (the LCD reads `push: save`) and press the encoder; `sav` reads `---` again. It takes what steps
1–16 play: each step's `mic` and `shf` with the track's groove template at that position (up to ±84
ticks), and its volume through the track's velocity template, in dB below the loudest of those steps. A
step that is off, or past the track's length, leaves its position alone (0 ticks, 0 dB); a track with
no step on empties the slot. `tpl` and `vtp` list `us1`–`us4` after the built-in templates, so a take
recorded on one voice can play on any other. A capture is written to the card at once (`/tmp`) and so
survives switching off; a project save stores the four in `USERTPL.LCH` in the project folder, a
project load brings them back, and a new project starts with them empty. The clear page (SHIFT + COPY,
then the encoder) gains `clear [micro]?` after `autom. 2`: it sets every step of the selected track in
the shown pattern back to the grid (`shf` 0, `mic` 0), keeping its flams and templates.

Patterns saved with 0.7 carry the velocity templates in a third block of 20 bytes after the blocks of
0.2 and 0.6, only when a track's `vtp` or `vdp` is not at `off` and 100 %, so a pattern without them is
the file of 0.6 byte for byte. 0.6 plays a 0.7 pattern without its velocity templates and with a `tpl`
of `us1`–`us4` as `off`; 0.2–0.5 and 1.7 play it as they play a 0.6 pattern. The own templates live
only in `USERTPL.LCH`, which 1.7 and 0.2–0.6 never open: a project saved by them loses its own
templates. Kits, songs and `glo.cfg` keep their sizes.

It keeps everything of 0.6, 0.5 and 0.4, and of 0.3, which added tools for playing live and for acid
lines. In the performance mode (the second mode button) holding step 8 turns the fill mode on, steps
9–16 recall eight mute scenes and COPY + a step stores one; the scenes are saved with the project.
Rolls go from a whole bar to 1/32, triplets included, on each track's own tempo. Drum 1–3 can slide:
`sld` on the step page marks a step, and `slt` on the voice's first page sets the slide time.

It keeps everything of 0.2, which added groove per track: a tempo of its own for each track
(polyrhythm), humanize and track delay, trig conditions and track rotation, together with the fixes
F1–F12 of firmware 1.7 found while building the emulator. And it keeps everything of 0.1, which gave
the step sequencer a new timing core and fixed the timing and MIDI problems of 1.7:

- Shuffle moves whole steps, ratchets and microshift included. MIDI clock out and the clock out jack
  stay straight.
- The master tempo is exact; 1.7 played up to 0.4 % fast.
- As a MIDI clock slave over DIN or USB no note is dropped, and Start, Stop, Continue and Song
  Position Pointer work.
- One track no longer swallows the ratchets of another, shifted notes keep their own parameter locks
  and velocity, and a note recorded slightly early no longer plays twice.
- Exactly one MIDI Note On per hit, and no hanging notes with a MIDI note override.
- A CPU meter on a new page of the global menu: hold SHIFT and press step button 12 until it
  appears, right after the page with the clock in and clock out PPQ.

Kits, patterns, projects, songs and global settings saved with 1.7 load unchanged. The start-up
screen shows `LICHOMAT` and `v0.8 unofficial`. The main changes of 0.1 are listed in its
[release notes](docs/lichomat/release-notes-v0.1.md), and every change against 1.7 in section 2 of the
[stage 1 report](docs/lichomat/reports/stage1-report.md). The release notes of 0.2, 0.3 and 0.4 are
`docs/lichomat/release-notes-v0.2.md`, `docs/lichomat/release-notes-v0.3.md` and
`docs/lichomat/release-notes-v0.4.md` on branch `timing-core`.

### Installing LICHOMAT

Install `LXR_V2_LICHOMAT_<version>.img` like any other update, see
[Installing the update](#installing-the-update). First delete every other `LXR*.img` file from the SD
card: the updater installs the first one it finds. After the update the start-up screen shows
`LICHOMAT` and `v0.8 unofficial`.

### Going back to the official firmware

The updater itself is never changed, so you can always go back:

1. Get the official update file `LXR_V2_V1.7.img`, either from Erica Synths or by building the
   upstream source code. The upstream folder has no `tools/env.sh`: on a Mac set up with
   `tools/setup-toolchain.sh`, first run `source tools/env.sh` in this folder, then `cd ..`, so that
   the build tools are on `PATH` and the clone goes next to this folder, not into it:

   ```sh
   source tools/env.sh                 # in this folder, on a Mac set up with tools/setup-toolchain.sh
   cd ..
   git clone https://github.com/erica-synths/lxr-desktop-app.git lxr-official
   cd lxr-official
   git checkout a43cd4f
   cmake --preset release
   cmake --build --preset release      # writes build/release/LXR_V2_V1.7.img
   ```

2. Delete `LXR_V2_LICHOMAT_*.img` from the SD card, copy `LXR_V2_V1.7.img` to its top folder and
   install it the same way. The start-up screen then shows `Sonic Potions` and `LXR2 Drums V 1.7`.

Your kits, patterns, projects and songs stay on the card, and 1.7 loads them as before. LICHOMAT adds
the track settings at the end of each pattern file and the mute scenes after the name in each
project's `prj.nfo`, after the data 1.7 reads. So 1.7 plays such a pattern with every track at ×1,
without humanize or delay, a step with a trig condition always plays, and a slide step plays without
the slide (at 100 % if it had a probability). Saving a project in 1.7 drops its mute scenes.

### Building LICHOMAT on a Mac

On a Mac with Apple Silicon two scripts install the build tools into this folder and build
everything, with no admin rights. They need the Xcode Command Line Tools, which give git, the host C
compiler of the tests in `tests/` and Python 3; if `xcode-select -p` prints no folder, install them
once with `xcode-select --install` (a system-wide install, which asks for an admin password):

```sh
tools/setup-toolchain.sh   # once: Arm GNU Toolchain 14.3.rel1, CMake 3.31.12 and Ninja 1.13.2 into .toolchain/
tools/check.sh             # host tests, release build, memory summary, update file check, warning count
```

`tools/check.sh` exits with an error if any step fails; the full log is in `build/check.log`, and the
update file is `build/release/LXR_V2_LICHOMAT_<version>.img`. To run CMake, Ninja or the compiler by
hand, first `source tools/env.sh` (bash or zsh). On other systems follow
[Building the firmware](#building-the-firmware) below.

## Updating your LXR

Every LXR has a built-in updater (the bootloader) that installs new firmware from the SD card. You
don't need any extra hardware:

1. [Build the firmware](#building-the-firmware). This creates the update file `LXR_V2_LICHOMAT_<version>.img`.
2. [Copy it to the SD card and install it](#installing-the-update) on the LXR.

### Building the firmware

You need [CMake](https://cmake.org/download/) 3.21 or newer, [Ninja](https://ninja-build.org/),
the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
(`arm-none-eabi-gcc`, tested with 14.3.rel1; fw 1.7 also with 11.3.rel1) and [Python 3](https://www.python.org/downloads/)
(to create the `.img` update file). The build finds them on `PATH`.

**1. Install the tools (once)**

*macOS* (Apple Silicon and Intel), with [Homebrew](https://brew.sh):

```sh
brew install cmake ninja
brew install --cask gcc-arm-embedded
```

Use exactly this `--cask gcc-arm-embedded` package, the official Arm toolchain. The similarly named
`brew install arm-none-eabi-gcc` does **not** include the C library the firmware needs, and the build
fails with it. Python 3 comes with the Xcode Command Line Tools, which Homebrew installs.
If you installed the toolchain with Arm's own `.pkg` installer instead, it isn't on `PATH`: add
`-DARM_TOOLCHAIN_DIR=/Applications/ArmGNUToolchain/<version>/arm-none-eabi/bin` to the first `cmake`
command below.

*Windows:* install CMake, Ninja and Python 3 (e.g. `winget install Kitware.CMake Ninja-build.Ninja
Python.Python.3.12`) and the Arm GNU Toolchain with its Windows installer. On the installer's last page,
tick "Add path to environment variable".

*Linux:* install CMake, Ninja and Python 3 with your package manager (e.g.
`sudo apt install cmake ninja-build python3`) and the Arm GNU Toolchain from Arm's download page, then
add its `bin` folder to `PATH`.

**2. Build**

Open a terminal (Terminal on macOS, PowerShell on Windows) in this folder and run:

```sh
cmake --preset release
cmake --build --preset release
```

When the build has finished it shows where the update file is and how to install it:

```
======================================================================
  Firmware update file:
    .../lxr-desktop-app/build/release/LXR_V2_LICHOMAT_0.8.img
  ...
```

The version in the file name, and on the LXR's start-up screen, is set by `project(VERSION ...)` in
[CMakeLists.txt](CMakeLists.txt), and the name by the CMake option `LICHOMAT_NAME`.

### Installing the update

1. Copy `LXR_V2_LICHOMAT_<version>.img` from `build/release/` to the top folder of the SD card. Delete any
   older `LXR*.img` files from the card: the updater installs the first one it finds.
   On a Mac, eject the card in Finder before you take it out. Finder also leaves hidden files starting
   with `._` on the card; the updater ignores them.
2. Switch the LXR off and put the card in.
3. Hold down the encoder button and switch the LXR on. Keep holding the button until
   "LXR Updater" appears on the display.
4. The display shows "Checking Firmware...", then "Loading Firmware" with the progress in percent.
5. When it shows "Update OK!" the LXR restarts by itself with the new firmware.

The updater itself is never changed by an update. If an update fails or is interrupted (for example by
a power cut), the LXR can always be updated again the same way.

### Troubleshooting

If the updater stops with one of these messages, switch the LXR off, fix the cause and try again:

| Display | Cause |
|---|---|
| `can't mount` / `SD Card` | No SD card, or the card can't be read. The card must be formatted as FAT32. Cards larger than 32 GB usually come formatted as exFAT, which the updater can't read. On a Mac, reformat the card in Disk Utility with the format "MS-DOS (FAT)". |
| `LXRE*.img` / `Not Found!` | No update file in the top folder of the card. The file name must start with `LXR` and end with `.img`. |
| `Can't open` / `LXRE*.img!` | The update file couldn't be read. Copy it to the card again. |
| `LXR*.img` / `Not Valid!` | The file isn't an update for the desktop LXR (for example a Eurorack module update, or a damaged file). |
| `LXR*.img` / `Wrong Size!`, `Wrong` / `Checksum!` | The file is damaged or incomplete. Build it again or copy it to the card again. |
| `Can't erase` / `Flash!`, `Flash write` / `Error!` | Writing the firmware to the LXR's memory failed. Switch the LXR off and try the update again. |

## Build options

| Option | Effect |
|---|---|
| `cmake --preset debug` | Debug build: `-O3 -g3` with `DEBUG` defined, output in `build/debug/`. The release build uses `-O3 -g0`. Both need `-O3` for the audio engine to keep up in real time. |
| `-DARM_TOOLCHAIN_DIR=<bin folder>` | Use a toolchain that isn't on `PATH`, e.g. `/Applications/ArmGNUToolchain/14.3.rel1/arm-none-eabi/bin` (macOS) or `C:/toolchain_arm-none-eabi/11.3rel1/bin` (Windows) |
| `-DLXR_ITCM_OPT=<flag>` | Optimisation for the code in the fast ITCM memory, see [Memory map](#memory-map) (default: empty, the same `-O3` as the rest) |
| `-DLICHOMAT_NAME=<name>` | Firmware name on the start-up screen and in the update file name (default `LICHOMAT`; up to 16 characters of `A-Z a-z 0-9 _ . -`) |
| `-DLXR_GENERATE_LISTING=ON` | Also write a disassembly listing (`.list`) |

Options are passed when configuring, e.g. `cmake --preset release -DLXR_GENERATE_LISTING=ON`.
The toolchain file and the CMake modules and tools are in the `cmake/` folder.

All files the build creates (in `build/<preset>/`):

| File                     | Purpose                                                        |
|--------------------------|----------------------------------------------------------------|
| `LXR_V2_LICHOMAT_<version>.img` | **Update file for the SD card** (see [Installing the update](#installing-the-update)) |
| `LXR_V2_STM32F765.elf`   | Firmware with debug information, for a debugger               |
| `LXR_V2_STM32F765.bin`   | Raw firmware image (from 0x08008000)                          |
| `LXR_V2_STM32F765.hex`   | Intel HEX of the same                                         |
| `LXR_V2_STM32F765.map`   | Linker map                                                    |

## Cleaning the build

**Remove the compiled files** (including the `.img`) but keep the settings. The next build then
compiles everything again:

```sh
cmake --build --preset release --target clean
```

Use `--preset debug` to clean the debug build.

**Start completely from scratch:** delete the build folder, which also forgets all settings such as
`-DARM_TOOLCHAIN_DIR` or `-DLXR_BOOTLOADER_HEX`, then configure and build again:

```sh
rm -rf build                        # Linux, macOS, Git Bash
Remove-Item -Recurse -Force build   # Windows PowerShell
rmdir /s /q build                   # Windows Command Prompt

cmake --preset release
cmake --build --preset release
```

Start from scratch when you switch to a different compiler (another `ARM_TOOLCHAIN_DIR`, or a
different `arm-none-eabi-gcc` on `PATH`), because an existing build folder keeps using the compiler it
was first set up with. Do the same if the build behaves strangely after updating the sources.
With CMake 3.24 or newer, `cmake --preset release --fresh` redoes the setup without deleting the folder.

The `build/` folder is never committed to git.

## Memory map

| Region   | Address                   | Size   | Contents                                              |
|----------|---------------------------|--------|-------------------------------------------------------|
| FLASH    | 0x08000000 – 0x08007FFF   | 32 KB  | Bootloader (`LXR_V2_STM32F765_Bootloader`)            |
| FLASH    | 0x08008000 –              | 992 KB | This firmware (`VECT_TAB_OFFSET` 0x8000)              |
| ITCM     | 0x00000000                | 16 KB  | `IN_ITCM_RAM` hot DSP code, copied there by `main()`  |
| DTCM     | 0x20000000                | 128 KB | `INDTCM`/`INDTCMZ` data, DMA buffers, the stack       |
| RAM      | 0x20020000                | 384 KB | `.data`, `.bss`, heap                                 |

The ITCM is tight. If a compiler generates too much code for it at `-O3`, set `LXR_ITCM_OPT` (for
example `-O2`) for the translation units that contain ITCM code; by default it is empty, and they are
compiled with `-O3` like the rest. The linker prints region usage after every build, and the link
fails with "image too large for FLASH" if the image does not fit.

## Source layout and CMake targets

The project was originally generated with STM32CubeMX/STM32CubeIDE, which explains the directory
layout and the `USER CODE BEGIN/END` comments. It no longer depends on either tool: the CubeMX project
(`.ioc`) was removed because regenerating code from it overwrites application code in `main.c`.
Pins, clocks and peripherals are configured in `Core/Inc/main.h`, `Core/Src/main.c`
(`SystemClock_Config`, `MX_*_Init`) and `Core/Src/stm32f7xx_hal_msp.c`; edit them there.

Every component is an OBJECT library linked into `LXR_V2_STM32F765`:

| Target          | Sources                                         | Contents                                              |
|-----------------|-------------------------------------------------|-------------------------------------------------------|
| `stm32f7_hal`   | `Drivers/`                                      | ST HAL + CMSIS                                         |
| `fatfs`         | `Middlewares/Third_Party/FatFs`, `FATFS/`       | FatFs and the SD card glue                            |
| `usb_midi`      | `Middlewares/JS`, `USB_DEVICE_MIDI/`            | USB device stack with the custom USB-MIDI class       |
| `lxr_platform`  | `Core/Startup`, `Core/Src/main.c`, ...          | Startup, clocks, peripheral init, interrupts          |
| `lxr_io`        | `Core/Src/IO`                                   | Button/LED shift registers, pots and faders           |
| `lxr_ui`        | `Core/Src/menu`, `lcd.c`, `buttonHandler.c`, ...| LCD, encoder, buttons, LEDs, menu pages               |
| `lxr_preset`    | `Core/Src/Preset`                               | Kit/pattern/project load & save on SD                 |
| `lxr_dsp`       | `Core/Src/Soundengine/DSPAudio`                 | Oscillators, filters, envelopes, voices, master FX    |
| `lxr_engine`    | `Core/Src/Soundengine/*.c`                      | Codec/I2S DMA, sample block rendering, FX bus, clock in/out, CPU meter |
| `lxr_midi`      | `Core/Src/Soundengine/MIDI`                     | MIDI UART, parsers, parameter mapping                 |
| `lxr_sequencer` | `Core/Src/Soundengine/Sequencer`                | Timing core, sync queues, glue, Euclidean generator   |
| `lxr_usb_app`   | `Core/Src/Soundengine/USB`                      | USB-MIDI message queue, MIDI out packets and ring     |

Source files are listed explicitly in the `CMakeLists.txt` files. Add new files there.

## Advanced: programming with a debug probe

Not needed for updates: use the [SD card](#installing-the-update) for that. This section is for
development, factory programming and repairs, with an ST-Link debug probe connected to the LXR's
debug header and [OpenOCD](https://openocd.org/) installed (the build finds it on `PATH`; on macOS:
`brew install open-ocd`).

**Firmware only.** Writes just the firmware; the bootloader is left alone:

```sh
cmake --build --preset release --target flash
```

**Blank devices (bootloader + firmware).** A blank device, or replacing the bootloader on purpose,
needs the bootloader as well. The bootloader is a separate project, so by default the build doesn't
include it. Tell the build where to find it in one of two ways:

```sh
# A: a prebuilt bootloader .hex (e.g. from a bootloader release)
cmake --preset release -DLXR_BOOTLOADER_HEX=path/to/LXR_V2_STM32F765_Bootloader.hex

# B: the bootloader source code, which is then built together with the firmware
cmake --preset release -DLXR_BOOTLOADER_DIR=path/to/LXR_V2_STM32F765_Bootloader
```

The build checks that the bootloader is valid for this device: it must fit in the 32 KB bootloader
sector and accept desktop (`LXRV2IMG`) update images, so the firmware, a full image or the Eurorack
bootloader are rejected. It then also writes `LXR_V2_LICHOMAT_<version>_full.hex`/`.bin` (bootloader +
firmware, the whole flash from 0x08000000) and enables:

```sh
cmake --build --preset release --target flash_full
```

The same `.hex`/`.bin` can also be loaded with STM32CubeProgrammer (the `.bin` goes to 0x08000000).
To go back to a firmware-only build, clear the setting: `-DLXR_BOOTLOADER_HEX=` or `-DLXR_BOOTLOADER_DIR=`.

> **Warning:** `flash_full` overwrites the bootloader. If that is interrupted, the device can no longer
> update itself from the SD card and has to be recovered with a debug probe. The target therefore
> prints a warning and only continues after you type `yes`; without an interactive terminal (e.g. a
> build started from an IDE) it refuses. For scripts and factory programming, set the environment
> variable `LXR_CONFIRM_BOOTLOADER_OVERWRITE=yes` to skip the question.

The bootloader write-protects its own flash sector (sector 0) every time it boots, so on a unit that
has run before, sector 0 cannot simply be erased. `flash_full` therefore clears that protection first;
the new bootloader sets it again on its first boot. Other programming tools need the same step
(in STM32CubeProgrammer: Option bytes → Write protection → uncheck nWRP0).

The OpenOCD settings can be changed with `LXR_OPENOCD_INTERFACE`, `LXR_OPENOCD_TRANSPORT`,
`LXR_OPENOCD_SPEED` (default 1000 kHz) and `LXR_OPENOCD_WORKAREA` (default 0x8000). The defaults are
deliberately conservative: with a 64 KB work area at the faster default clock, an ST-Link V2 (firmware
V2J29) failed half-way through programming.

## Licence, trademarks and hardware

Copyright (C) 2012-2026 Erica Synths.

**Firmware licence.** The firmware, the bootloader and the build system are licensed under the
[GNU General Public License version 3](LICENSE) (GPL-3.0-only), with an additional permission for
the STMicroelectronics libraries and the additional terms in [NOTICE.md](NOTICE.md). In short:

- You may use, study and change the firmware, for example to build your own updates for your device.
- If you share or distribute the firmware, or a modified version of it, including pre-installed on a
  device, you must provide the complete source code under the same licence, and the people you give it
  to may install their own modified versions.
- Modified firmware must be marked as modified (its start-up screen must show this, e.g. with a
  different name or version text) and must not be presented as official Erica Synths firmware.
- The firmware comes **without any warranty**. Building and installing your own or modified firmware is
  entirely at your own risk, and Erica Synths provides no support for it.

**Trademarks.** "Erica Synths", "LXR" and "LXR-02" and the associated logos are trademarks of
Erica Synths. The GPL licence does **not** grant any right to use them. They may not be used for any
product, firmware release, service or commercial activity without prior written permission from
Erica Synths. Truthful references such as "custom firmware for the Erica Synths LXR-02" are fine.

**Hardware.** The LXR hardware (schematics, circuit board layouts, front panel, enclosure and industrial
design) is the intellectual property of Erica Synths and is protected by copyright and design rights.
It is **not** part of this repository and is **not** licensed under the GPL or any other licence. It may
not be copied, manufactured or sold, in whole or in part, without prior written permission from
Erica Synths.

**Third-party code.** The repository includes components from STMicroelectronics, Arm and ChaN (FatFs)
under their own licences. See [NOTICE.md](NOTICE.md#third-party-components).
