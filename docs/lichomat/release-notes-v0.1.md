# LICHOMAT v0.1: release notes (draft)

> **Draft.** Not published. It becomes the text of the GitHub release `v0.1` of `isthar/lxr-desktop-app` after the owner has run the hardware checklist (`docs/lichomat/hardware-checklist.md`) and agreed to release it. The branch `timing-core` is on the fork already; nothing has been tagged or released.
>
> The text assumes the defaults of the open owner questions Q9 (the CPU page shows no sync queue overflow count: the clause on `ovf` under "New: CPU page"), Q10 (the USB name stays `Sonic Potions USB MIDI`: "Known limitations") and Q11 (as a slave the LXR plays up to the predicted clock, so a swung 4 PPQ clock plays straight: the bullet "As a MIDI clock or clock jack slave …" under "Changed behaviour"). A different answer changes the firmware: then that text changes, and the release needs a new build, its hash under "Files" and in the run sheet, and the checks it affects again (Q9: H12; Q10: H7 and X4, which name the USB port; Q11: X2).

LICHOMAT is unofficial custom firmware for the Erica Synths LXR-02. It is not made, approved or supported by Erica Synths. Version 0.1 is based on the official firmware 1.7 (`erica-synths/lxr-desktop-app` at commit `a43cd4f`). It gives the step sequencer a new timing core, on which the next versions (groove features, effects) build, and it fixes the timing and MIDI defects of 1.7 listed below.

Kits, patterns, projects, songs and global settings saved with 1.7 load unchanged, and 1.7 loads everything LICHOMAT saves: no file format has changed.

## Installing

Before you install, copy the whole SD card to a computer: LICHOMAT reads and writes the same files as 1.7, but it is unofficial firmware.

1. Copy `LXR_V2_LICHOMAT_0.1.img` to the top folder of the SD card without renaming it (the updater installs only files whose name starts with `LXR` and ends with `.img`), and delete every other `LXR*.img` file there: the updater installs the first one it finds.
2. Switch the LXR off, put the card in, hold down the encoder button and switch the LXR on. Keep holding it until `LXR Updater` appears.
3. Wait for `Update OK!`; the LXR restarts by itself. The start-up screen shows `LICHOMAT` and `v0.1 unofficial`.

## Going back to the official firmware

The updater is never changed by an update, so you can always go back. Install the official update file `LXR_V2_V1.7.img` the same way, after deleting `LXR_V2_LICHOMAT_*.img` from the card. Take it from Erica Synths, or build it from the upstream sources at commit `a43cd4f` as README.md describes. The start-up screen then shows `Sonic Potions` and `LXR2 Drums V 1.7`.

## Fixed

- **Shuffle with ratchets and microshift (D1).** In 1.7 shuffle delayed only the first pulse of a swung step, and the rest of the step followed in a burst: at 130 BPM and shuffle 64, a ×4 ratchet played its hits 0, 0.2, 19 and 48 ms after the step instead of 0, 29, 58 and 87 ms, and a shifted note landed far from where it should. Now shuffle moves whole steps, ratchets and shift included.
- **Steady clock outputs with shuffle (D1).** With shuffle on, 1.7's MIDI clock out jumped between intervals of 0.1 and 58 ms (130 BPM, shuffle 64; at shuffle 127 up to 96 ms, and up to 104 ms at 120 BPM). MIDI clock out and the clock out jack now stay straight.
- **Choke between tracks (D2).** In 1.7 the notes of one track could swallow the ratchet hits and the spill-over notes of another, for example a shifted snare took the fourth hit of a hi-hat ratchet. The choke now works within each track only.
- **Exact master tempo (D3).** 1.7 computed the length of a beat in whole milliseconds and so played most tempos slightly fast, by up to 0.4 % (137 BPM as 137.29, 177 as 177.50, 249 as 249.98). The tempo is now exact, within 0.01 %.
- **One MIDI Note On per hit (D4).** 1.7 sent every sequencer note twice, and with a MIDI note override the first of the two never got its Note Off, which left notes hanging on external synths. Now each hit sends one Note On, with the override, and its Note Off, also when the master's clock stops while the LXR follows it.
- **Note Offs of one-shot rolls and of the MIDI echo.** A one-shot roll (in performance mode a step button with the roll rate `One`, or with any roll rate while the LXR is stopped) and the echo of an incoming MIDI note now get their MIDI Note Off one pulse (1/24 of a step) later, as the sequencer's notes do. 1.7 sent none until the voice's next note, a mute, a stop press or a pattern change.
- **Shifted notes keep their own data (D5).** A note moved into the neighbouring step by shift or a ratchet took the parameter locks and the MIDI velocity of the step that was playing. Each note now plays with its own step's parameter locks and velocity, also across a pattern change.
- **No doubled notes when recording (D6).** A note played slightly before a step is recorded on that step; 1.7 then played it a second time. Now it plays once.
- **MIDI clock slave (D7).** The tempo of the master is followed as a fractional value, late notes on swung steps are no longer dropped, and Song Position Pointer works, in pattern mode and in song mode. The tempo is measured per clock, also from a computer that sends several clocks in one USB transfer (a tempo measured per transfer would follow the computer's audio buffers instead). A single spurious MIDI clock no longer throws the tempo off when it comes more than about a fifth of a clock from a real one and the master's clocks arrive within about an eighth of a clock of their time (clocks that a computer sends with its audio buffers often do not), and neither does a single double trigger at the clock in jack; the cases that still do are under Known limitations.
- **MIDI input (D8).** MIDI clock, Start, Continue, Stop and Song Position Pointer are time-stamped when they arrive, over DIN and over USB, and all MIDI input is handled in one place. This removes rare duplicated or garbled bytes: a clock that arrives in the middle of another DIN message no longer breaks that message. USB clock no longer stalls while a preset is saved to the card.
- **Messages passed on from DIN MIDI in.** MIDI clock, Start, Stop, Continue, Active Sensing and Reset passed on from DIN MIDI in to DIN MIDI out (routing `M2M`, `A2M` or `M2A`) go out alone. 1.7 sent them with the data bytes of the last channel message (a clock after a Note On went out as `F8 3C 64`), which a receiver took as a repeat of that message on every clock. A Tune Request (`F6`), and `F4` and `F5`, passed on from DIN, to DIN out or to USB, go out alone too, without the data bytes of the message before them, and stray data bytes after them no longer act as a repeat of that message.
- **Kit loads no longer change a pattern.** In 1.7 every kit load, and every MIDI Bank Select (CC 0) the LXR received, wrote one byte past a table into the pattern memory; in a build of the 1.7 sources that byte is the number of the first parameter lock on step 52 of voice 3 in the 18th pattern, and a pattern save kept it on the card. An NRPN with a low number byte of 127 or more could also overwrite memory past that table. Neither happens any more. Patterns already saved with such a byte keep it.
- **Reset input of the clock jack.** A quick edge at the reset input is no longer lost. 1.7 could miss one and then stay stopped, or keep running, against the level of the gate.
- **Step buttons in the global menu.** In 1.7 a step button pressed without SHIFT while the global menu was open set the length of every track, and page up or down there stopped step 12 blinking. Now a step button pressed without SHIFT does nothing in the global menu, and page up and down keep step 12 blinking and show the track's steps, as on the FX page.
- **USB MIDI out.** 1.7 sent 128 bytes of unrelated memory to the computer each time its USB send buffer wrapped, dropped messages sent while the previous USB transfer was still under way, and passed Song Position Pointer, MTC, Song Select and SysEx from DIN on to USB without their data or garbled. Now every message goes out once and complete while the computer reads the port, also while a preset is being saved. While the computer does not read it (some systems read it only while a program has it open), the LXR drops what it would send, so a program that opens the port later gets at most 15 old messages, not seconds of them. A Song Select passed on from DIN, to USB or to DIN out, now carries its song number.

## Changed behaviour

These are intended; they come with the fixes above.

- Patterns that use shuffle together with ratchets or shift sound different: the whole step moves.
- MIDI clock out and the clock out jack stay straight while shuffle is on.
- A note played on a swung step while recording is recorded on that step (the recording quantize follows the shuffle).
- A ratchet hit that spills over into the next step no longer plays after an early (negatively shifted) hit of a newer step of the same track: the newer step cuts it, as the choke within a track does elsewhere. At a start, the spill-over of a track's last step no longer plays on step 1.
- At a pattern change, the early (negatively shifted) hits of the first step come from the first step of the next pattern. 1.7 took them from the pattern that was playing.
- A repeating roll (any roll rate but `One`) sends one MIDI Note On per hit: the note, or its override, with velocity 100, the velocity the voice plays. 1.7 sent two, the note with velocity 100 and then the note or its override with the volume of the track's current step. A one-shot roll (`One`) keeps that volume.
- As a MIDI clock or clock jack slave the LXR plays on at the measured tempo up to the next clock. With the clock in jack at 4 PPQ, where every pulse starts a step, a late pulse no longer delays its step: a swung 4 PPQ clock plays straight (use the LXR's own shuffle instead), and when the clock stops without a reset one more step plays than pulses arrived. 1.7 played each step at its pulse. At 24 PPQ and with MIDI clock the difference stays within one clock.
- In MIDI slave mode, Continue resumes where playback stopped, or where Song Position Pointer put it. 1.7 restarted from the first step.
- In the slave modes (`MID` and `EXT`) Song Position Pointer is followed while the LXR is stopped; in song mode the pattern at that position is loaded at once, with its kit if kit autoload is on, unless it is the current pattern already.
- After a song ends, the next start plays the song from its first entry, and that entry once. 1.7 played the first entry twice when the song had ended on another pattern, and skipped it when a MIDI Continue or MIDI Time Code started the song again after it had ended on the same pattern.
- In song mode, a start from the LXR after a stop that kept the position (a MIDI Stop received as a slave, or the MTC timeout) plays the song from its first entry. 1.7 went on with the entry after the one that was playing.
- At a song end the LXR turns all notes off, and sends MIDI Stop when it is the master; the start/stop button shows the stop. The button and its LED now follow every start and stop, also those that come from MIDI or from the song end.
- As a MIDI or clock jack slave the LXR sends no MIDI Start, Stop or Continue of its own, so with MIDI thru on the gear behind it gets the master's messages once. 1.7 answered a received Start with Stop and Start, and a Continue with Start.
- In clock in jack mode (`EXT`), a MIDI Start makes the next jack pulse start playback.
- The clock out jack at 2 PPQ gives 2 PPQ in the slave modes too (1.7 gave 4 there), and after a Continue or a Song Position Pointer its pulses stay on the step grid.
- An incoming MIDI note that plays a voice is echoed once on MIDI out, also in record mode (1.7 sent it twice).
- MIDI Note Offs are sent per voice, so voices that share a MIDI channel no longer leave each other's notes hanging, and muting a track turns off that track's note (1.7 turned off the note of the voice whose number matched the channel).
- A roll recorded while playing lands on the step where it played.
- The start-up screen shows `LICHOMAT` and `v0.1 unofficial`, and the global menu has a CPU page (see below).

## Unchanged

- File formats, and the user interface apart from the start-up screen, the CPU page, the global menu fix and the start/stop button, which now shows every start and stop (see Changed behaviour).
- Roll plays on muted tracks, as in 1.7.
- A local start in a slave mode waits for the master's next 16th note, as in 1.7.
- The clock in jack's reset input works as a run gate, as in 1.7: high stops and returns to the start, low starts with the next clock pulse.
- In internal sync MIDI clock out keeps running while the sequencer is stopped, as in 1.7.
- A Program Change sent just before a Start selects the pattern that the Start plays, as in 1.7, also when both come in one USB transfer.

## New: CPU page

Hold SHIFT and press step button 12 to open the global menu, and press it again, still holding SHIFT, until the page after the one with kit autoload (`kcm`) and the clock in and clock out PPQ appears:

```
CPU  42% pk  67%
Qmax  12 ovf   0
```

`CPU` is the mean load of the sequencer and the sound engine over the last second and `pk` the highest load of a single audio block in that second, both in percent of the time one block lasts (64 samples, 1.45 ms). `Qmax` is the most events the sequencer has had queued since the LXR was switched on, and `ovf` the number of events it dropped because its queue was full; `ovf` counts the sequencer's event queue only, not the input queues of MIDI and the clock jack. The page is updated twice a second, the knobs and the encoder do nothing on it, and it is never saved.

## Known limitations

- Voices start at the beginning of an audio block, as in 1.7, so hits are placed to within 1.45 ms (1.7: about 1.7 ms, with its 0.25 ms timer step on top). Sample-accurate triggering is planned for later.
- The tempo is set in whole BPM, and the LXR plays it exactly. As a slave it follows the clock as it comes, but its tempo-synced LFOs follow the measured tempo rounded to a whole BPM, as in 1.7.
- With MIDI clock from a computer that sends the clocks of one audio buffer together (over USB, or through a MIDI interface), the measured tempo is within 0.5 % at most tempos but can stay off near some: by up to 1.0 %, 1.4 % and 2.8 % with audio buffers of 256, 512 and 1024 samples at 44.1 kHz. The steps still follow the clocks; tempo-synced LFOs run that much off. A smaller audio buffer reduces it.
- MIDI clock slave: a lost clock, or a spurious one within about a fifth of a clock of a real one, puts the measured tempo off by about 1.5 % for about four beats; the steps still follow the clocks, but the tempo-synced LFOs run about 2 BPM off at 120 BPM (1 to 3 BPM from 90 to 180 BPM) for most of that time. With clocks that arrive less precisely than about an eighth of a clock (for example from a computer's audio buffers), a spurious clock further from a real one restarts the measurement instead, which puts it off by up to about 16 % for one or two beats.
- Song Position Pointer is ignored in internal sync, where the LXR is the master (1.7 ignored it always), and, in the slave modes, while the LXR is playing, as the MIDI specification expects it only while stopped.
- Sending MIDI clock to the LXR over DIN and USB at the same time is a configuration error, as in 1.7.
- The LXR still reports itself over USB as `Sonic Potions USB MIDI`.
- The CPU page counts the sequencer and the sound engine, including the interrupts that run in between, but not the user interface, and leaves out the kit load of a pattern change.
- Clock in jack: when the clock jumps to about two to three times its tempo and the pulse right after the jump is lost, the LXR keeps about the old tempo for two or three more pulses, so the ratchets and shifted notes inside those steps bunch up at the next pulse; then it follows the new tempo by itself. A clean clock cable and connection, or a tempo change in smaller steps, avoid it.
- Clock in jack: a double trigger followed by a pulse about a third of a pulse early can put the measured tempo off by up to about 60 % at 4 PPQ, and double triggers on two pulses in a row can make it read about twice the tempo (up to 2.1 times), for up to five pulses; then it is exact again. At 24 PPQ both stay within about 15 %, for up to about four beats. Clean clock pulses (one edge per pulse, no bounce) avoid it.

## Files

- `LXR_V2_LICHOMAT_0.1.img`: the SD-card update file, built by CI from the commit tagged `v0.1`, with the SHA-256 `9a7aff2ad9f7284a1b24e14e9700eb822674d1a42a254f8c4b3b74eff9a28c66`, the same file as in the run sheet of the hardware checklist (`docs/lichomat/hardware-checklist.md`).
- Source code: the tag `v0.1` of `isthar/lxr-desktop-app` (the release's "Source code" archives).

## Licence

LICHOMAT is free software under the GNU General Public License version 3 (GPL-3.0-only) with the additional permission and terms in NOTICE.md; the complete source code is in the repository. It comes without any warranty: installing it is at your own risk, and Erica Synths provides no support for it. "Erica Synths", "LXR" and "LXR-02" are trademarks of Erica Synths. They are used here only to name Erica Synths, the device this firmware is for, its updater, its official firmware and that firmware's repository (`erica-synths/lxr-desktop-app`, of which `isthar/lxr-desktop-app` is a fork and keeps the name), and in the names of the update files, which have to start with `LXR` because the updater installs only `LXR*.img` files.

## Publishing (owner only)

1. Make this file the release text: delete the draft note and ` (draft)` in the title. Commit and push it to `timing-core` on `isthar/lxr-desktop-app`; that commit is the one to release. Wait until its CI run (`.github/workflows/build.yml`) has passed.
2. Download the artifact `lichomat-firmware-<commit>` of that run and unzip it. The run's job summary lists the SHA-256 of the update file. If it is the SHA-256 under "Files" (the file of the run sheet in `docs/lichomat/hardware-checklist.md`), it is the file you checked on the LXR: go on. If it is not, it is a different file. If the firmware sources changed after the run sheet's commit, install the CI file, run again the checks those changes touch and record them in the run sheet; if only documents changed, the build did not reproduce: find out why before releasing. Then put the new SHA-256 under "Files" and in the run sheet, commit and push that, and from then on use the new commit and its CI run.
3. Tag that commit `v0.1`, push the tag, and create the GitHub release `v0.1` from it, with the title `LICHOMAT v0.1`, as its text everything from "LICHOMAT is unofficial custom firmware" to the end of "Licence" (as it is in this file: a GitHub release shows every line break of its text), and the `LXR_V2_LICHOMAT_0.1.img` of the artifact attached.
