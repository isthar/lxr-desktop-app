<!--
Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
-->

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

## The panel

The panel is arranged roughly like the control layout of the device, for muscle memory: your hands
find the controls where they expect them. The jacks are along the top, the display with the encoder
and the four knobs on the left, the six faders and the voice buttons on the right, then the buttons
and the steps. It keeps none of the device's names, logos, printing, graphics or colours: the labels
are plain function names, the colours are its own, and the "LICHOMAT" wordmark above the display is
set in a system font that the app loads at run time.

There is no VOLUME fader, because the firmware reads six faders, one for each voice. The Mac's own
volume controls the level.

## The card folder

The app works on its own folder, `~/Library/Application Support/LICHOMAT Emu/Card`, never on your
card. Card > Import card folder copies a card or a folder in. Card > Export card writes the card to an
empty folder, and Card > Export project copies one project into a card folder and keeps the projects
that are there. Every card or project that an import or an export replaces goes to `Card backups`
first.

## How close it is

The emulator sounds very close to the device, not identical. Renders are bit-identical between builds
on a Mac, never with the device: floating point, the math library and the conversion to your audio
device's rate differ. The display is instant (the device needs about 51 ms for a full repaint), and
the CPU and the SD card take no time.

These parts are models, not measurements of the device: the display's font (a generic 5x8 font), the
knobs' wipers (about 134 steps per turn, 22 on a list), the order of the faders, the true sample rate
(44,140.625 Hz from the clock tree), flush-to-zero in the sound engine, the main loop's rate (a pass
every 250 us) and the timer during saves.

## MIDI clock out

As a master the app sends 24 clocks (F8) per quarter note: 20.83 ms apart at 120 BPM, within 1.5 ms
as they arrive at the virtual port. The one interval that contains Start is the exception. The
firmware sends an F8 exactly 1 ms after the Start (FA), whatever the phase of its free-running
clocks, so that interval is anywhere from 0 to 20.8 ms. Leave it out when you measure the intervals.

## Latency as a MIDI clock slave

The app delays MIDI and clock input by D, which its MIDI window shows: about 58 ms at the built-in
output's 512-frame buffer, about 34 ms at 128 frames. Set your DAW's MIDI clock sync delay to D, or
lower D at the cost of an uneven latency.

## Licence and source

GPL-3.0-only, with the additional permission and terms in NOTICE.md. The source is
https://github.com/isthar/lxr-desktop-app at the commit that About and `lichomat-render --version`
name. Third-party code: Dear ImGui (MIT), Arm Optimized Routines (MIT), FatFs `ff.h` (ChaN's licence).
