# LICHOMAT 0.4: kit recipes for washes

Four kits to build by hand on LICHOMAT 0.4, for its reverb, freeze, rumble and the tempo-synced
delay (plan `docs/superpowers/plans/2026-10-31-v0.4-atmosphere.md`, D44). They are text written for
LICHOMAT, not kits or data from the factory content: start from an init kit or any kit you can
spare, set the values below, and save the result in a slot of your own. The names are the ones the
display shows. Nothing here has been tried on the device yet; U26 of
`docs/lichomat/hardware-checklist.md` is the check that builds these recipes.

Each recipe has an emulator render in `build/demo/` (`tools/demo_v04.sh` on branch `v0.4`). The
renders play the emulator's fx card, whose pattern has every voice on steps 1, 5, 9 and 13, at
125 BPM. The renderer cannot set an LFO target, so where a recipe uses an LFO the render steps the
same parameter every 80 ms instead; on the device the LFO moves smoothly.

## Where the settings are

- **Voice pages:** press a voice button, then a select button: 1 the oscillator, 2 the amp EG, 3
  the modulation (pitch EG), 4 FM, 6 the filter, 7 the LFO, 8 MIX. On MIX, the second screen has
  `out`: `FX` sends the voice to the FX bus, `St1` is dry on output 1.
- **FX page:** SHIFT + step 15 opens the slot's sub-page (`typ`, `out`, `d/w`; for the delay `typ`
  on knob 4 and `tim rng ton fbk` on the second screen). SHIFT + step 15 again opens the reverb's
  sub-page: `lvl siz dec ton` on the first screen, `pre mod frz syn` on the second. Once more opens
  `dck ddp djf djr` (rumble and the DJ filter); once more goes back to the slot.
- **Performance mode** (the second mode button): SHIFT + step 3 toggles the freeze, and step 3's
  LED shows it while SHIFT is held. The second screen of its page has `djf` and `djr`.

## What to keep in mind

- **The reverb's rate is provisional.** The build on `v0.4` runs the reverb at half rate until the
  owner's CPU readings H12 and U20 fix the rate (plan D41). At half rate `siz` gives a bigger room
  and `pre` reaches 2–92 ms; at full rate the same values give a smaller room and 1–46 ms. `dec`
  stays in seconds at both rates. If the rate changes, retune `siz` and `pre` by ear.
- **`ton`** goes from a dark low-pass of about 1.5 kHz at 0, through flat at 64, to a gentle
  high-pass of about 400 Hz at 127. The steps are equal intervals: 20 is about 3.3 kHz, 30 about
  4.9 kHz, 40 about 7.3 kHz, 50 about 11 kHz. The recipes keep to 20–50.
- **`dec`** is the reverb's RT60: 0.3 s at 0, doubling every 21 steps, 19.2 s at 127 (80 about
  4.1 s, 105 about 9.3 s, 115 about 13 s).
- **No LFO on the five live settings.** `djf`, `djr`, `lvl`, `frz` and `dec` take no LFO, velocity
  or p-lock (tracker Q29); move them by hand or by NRPN 96, 97, 98, 99 and 116. The recipes' LFOs
  go to voice parameters only.
- With the slot's `typ` `off`, the FX bus passes to the reverb unchanged; with `del`, the reverb
  hears the delay's echoes.
- A kit always saves `djf` at 64 and `frz` off.

## 1. Noise wash

A bed of filtered noise that keeps moving, frozen at the end of a phrase.

- **Voices:** voice 4 (snare) on a sparse pattern, for example steps 1, 5, 9 and 13. Mute the
  others, or keep a dry kick under it.
- **Oscillator (select 1):** `mix` 127 (noise only); `noi` to taste.
- **Amp EG (select 2):** `dec` 90, a long decay.
- **Filter (select 6):** `typ` `BP`, `frq` 40, `res` 40.
- **LFO (select 7, voice 4's LFO):** `voi` 4, `dst` the filter frequency, `wav` `tri`, `snc` `2/1`
  (one sweep up and down every 2 bars), `mod` so that the sweep spans about `frq` 40 to 100, `ofs`
  0.
- **Routing:** MIX `out` `FX`. FX page: `typ` `off`, `out` `St1`.
- **Reverb:** `lvl` 100, `siz` 127, `dec` 115, `ton` 40, `pre` 30, `mod` 80. Rumble sub-page:
  `dck` `off`.
- **Playing it:** after the last hit of a phrase, stop the sequencer and press SHIFT + step 3: the
  wash holds, losing about 0.3 dB a minute. Hits played while frozen are heard dry. SHIFT + step 3
  again lets it decay.
- **Render:** `v04-8-przepis-noise-wash.wav` (4 bars, stopped, frozen for 6 s, thawed).

## 2. FM drone

A metallic drone from one drum voice, its FM slowly opening and closing.

- **Voices:** voice 2 (Drum 2) on steps 1, 5, 9 and 13; the others muted.
- **Amp EG (select 2):** `dec` 105.
- **Modulation (select 3):** `mod` 0, no pitch sweep.
- **FM (select 4):** `amt` 20, `frq` 40.
- **LFO (select 7, voice 2's LFO):** `voi` 2, `dst` the FM amount (`amt`), `wav` `tri`, `snc`
  `4/1` (one round every 4 bars), `mod` so that `amt` swings from about 20 to 90.
- **Routing:** MIX `out` `FX`. FX page: `typ` `off`.
- **Reverb:** `lvl` 100, `siz` 110, `dec` 105, `ton` 30, `pre` 10, `mod` 60.
- **Playing it:** let it run for a few bars; lower `lvl` by hand or by NRPN 98 to bring the drone
  forward, raise it to push it back.
- **Render:** `v04-9-przepis-fm-drone.wav` (4 bars, then the tail).

## 3. Rumble kick

The kick's own reverb, dark and ducked by the kick, as in check U22.

- **Voices:** voice 1 (kick) on steps 1, 5, 9 and 13. Snare, cymbal and hats as you like, dry.
- **Routing:** voice 1's MIX `out` `FX`; every other voice `out` `St1`. FX page: `typ` `off` (no
  delay).
- **Reverb:** `lvl` 120, `siz` 110, `dec` 80, `ton` 20, `pre` 10, `mod` 40. For a darker rumble take
  `ton` towards 0 (about 1.5 kHz).
- **Rumble (the sub-page after the reverb):** `dck` `dr1`, `ddp` 110. The tail swells between the
  kicks and ducks under each one; `ddp` 127 ducks deepest, 0 leaves a steady wash.
- **Playing it:** for a build-up, turn `djf` on the performance page down towards 40 by hand (or
  NRPN 96) and back to 64; it has no LFO.
- **Renders:** `v04-7-rumble-stopa.wav` (the kick alone) and `v04-10-przepis-rumble-kick.wav` (with
  the snare, cymbal and hats dry).

## 4. Dub stab

A short cymbal stab thrown into tempo-synced echoes, the echoes reverberated.

- **Voices:** voice 5 (cymbal) on a few steps, for example the off-beats 3, 7, 11 and 15; voice 1
  (kick) on steps 1, 5, 9 and 13.
- **Cymbal:** amp EG (select 2) `dec` 60 or shorter; MIX `vol` 127, `out` `FX`. Kick: MIX `out`
  `St1`, its volume low enough to leave room for the stab.
- **Delay (the slot):** `typ` `del`, knob 4 `typ` `Del`, `fbk` 90, `ton` 64. Reverb sub-page, second
  screen: `syn` `8d` (360 ms at 125 BPM); `tim` and `rng` then do nothing.
- **Reverb:** `lvl` 110, `siz` 100, `dec` 80, `ton` 50, `pre` 10, `mod` 40.
- **Ping-pong:** knob 4 `typ` `Pp` spreads the echoes. Its line holds about 370 ms, so `8d` fits
  from 122 BPM; below that the time is halved to a 1/16 dotted. The normal line takes `8d` at any
  tempo from 61 BPM.
- **Render:** `v04-11-przepis-dub-stab.wav` (on the render's pattern the stab plays on the beat).
