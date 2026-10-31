# LICHOMAT 0.5: the click, a kit recipe for the timing checks

One kit to build by hand on LICHOMAT 0.5 for its sample-accurate triggering (spec
`docs/superpowers/specs/2026-10-31-sample-accurate-triggering.md` §10.1, plan
`docs/superpowers/plans/2026-10-31-v0.5-sample-accurate.md`). Checks U27–U29 of
`docs/lichomat/hardware-checklist.md` read onsets at sample level, and a drum hit with a slow attack
or a long tail makes the onsets hard to read. The click starts sharply and is gone within 3 ms. It is text written for LICHOMAT, not a kit or data from the factory content: start
from an init kit or any kit you can spare, set the values below, and save the result in a slot of
your own. The names are the ones the display shows. Nothing here has been tried on the device yet;
U27 is the first check that uses it.

The recipe is the kit of the emulator's `onset` card (`emu/tests/cards/scripts/onset.txt` on branch
`v0.5`), on which the goldens G16–G19 measure the onsets. Its render is
`v05-groove-onset-smp.wav` (see "Renders" below).

## Where the settings are

- **Voice pages:** press a voice button, then a select button: 1 the oscillator, 2 the amp EG, 3
  the modulation (pitch EG), 4 FM, 5 the transient, 6 the filter, 7 the LFO, 8 MIX. On MIX, the
  second screen has `out`: `St1` is output 1, `St2` output 2.
- **`trg`:** the global menu: hold SHIFT and press step 12, again until its second sub-page shows
  `kcm cki cko trg`; knob 4. `smp` starts each voice at the sample of its event, 128 samples
  (2.90 ms) after it; `blk` starts it at the next audio block, as 0.4 did.

## The click (Drum 1)

- **Voice:** voice 1 (Drum 1). Leave everything not listed here as the kit has it.
- **Oscillator (select 1):** `wav` `Sin`.
- **Amp EG (select 2):** `atk` 0, `dec` 0. The decay must be the shortest: in the emulator the click
  then lasts 127 samples (2.9 ms) from its onset until it falls below 64 of 32,767. At `dec` 1 it
  lasts 702 samples (15.9 ms), at 2 1,197, at 5 2,809 and at 10 5,554 (measured on the onset card,
  plan v0.5 Task 9). So from `dec` 1 the hits of a ×8 ratchet (690 samples apart at 120 BPM) run
  together, and from `dec` 2 those of a ×6 (920 apart) too.
- **Transient (select 5):** `wav` `Clk`, `vol` 127.
- **Filter (select 6):** `typ` `off`.
- **MIX (select 8):** `vol` 127, `pan` −63 (hard left); on the second screen `out` `St1`.
- **The other voices:** MIX second screen `out` `St2`, so that output 1 carries the click alone.
  The one exception is the copy on Drum 2 below, when a check uses it.

## The copy on Drum 2 (U27 step 2)

- **Voice:** voice 2 (Drum 2), with the same values on its own pages: select 1 `wav` `Sin`; select 2
  `atk` 0, `dec` 0; select 5 `wav` `Clk`, `vol` 127; select 6 `typ` `off`; select 8 `vol` 127.
- **MIX (select 8):** `pan` +64 (hard right), second screen `out` `St1`. Output 1 then carries voice
  1 on the left channel and voice 2 on the right, so the gap between the two clicks can be read
  sample by sample in a stereo recording.
- Save the kit with both clicks in one slot; U27 step 1 simply leaves voice 2 off.

## What to keep in mind

- Record output 1 (the main output) in stereo at 44.1 or 48 kHz. An onset is the first sample that
  rises clearly above the noise.
- With `trg smp` each click sounds 128 samples (2.90 ms) after its event, within one sample. Two
  clicks one tick apart (1.30 ms at 120 BPM) land 56 or 58 samples apart at the LXR's rate
  (1.27–1.31 ms). With `trg blk` the same clicks land 0 or 64 samples apart.
- The click has no tail, so it does not tell you about the sound of a note. U31 listens to real
  sounds for that.

## Renders

The emulator's renders of the onset card are in `/Users/tk/Code/lxr/build/wt-v05/build/demo-v05/`
(git-ignored, on the owner's Mac; `tools/demo_v05.sh` on branch `v0.5`, described in
`README-v0.5.txt` there):

- `v05-groove-onset-smp.wav`: the onset card at 120 BPM with G16's script: the click with the shifts
  −7..+7, the ratchets ×2–×8, shuffle, the track delay and humanize, a 1/32 and a triplet roll. Each
  click sits at its own time. Drum 2 plays at ×3/4 on output 2, which the render leaves out.
- `v05-groove-onset-blk.wav`: the same with `trg blk`: the clicks jump by up to a block (64 samples).
