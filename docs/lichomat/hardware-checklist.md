# LICHOMAT 0.1: hardware checklist (H1–H13)

These are the checks of spec §13.2, written out as procedures for the LXR-02. Only the owner can run
them, on the real device; nothing here is ever marked as passed by an agent. Record every result in
the [run sheet](#run-sheet) at the end and report it back (a message, or the tracker).

The spec is `docs/superpowers/specs/2026-10-23-timing-core-design.md`, the decisions beyond it are in
`docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md` (N1–N21), and every change
against fw 1.7 is listed in section 2 of `docs/lichomat/reports/stage1-report.md`.

## Before you start

**Back up the SD card.** Copy the whole card to the computer. LICHOMAT reads and writes the same
files as 1.7 (no format changes), but it is unofficial firmware.

**What you need**

- The LXR-02, its SD card and a card reader.
- The two update files of the [run sheet](#run-sheet): `LXR_V2_LICHOMAT_0.1.img` (LICHOMAT) and
  `LXR_V2_V1.7.img` (official 1.7, for H13 and for comparisons).
- A computer with a DAW that can record the LXR's audio output and send and receive MIDI over USB
  (and over DIN through a MIDI interface).
- A MIDI monitor that shows time stamps, for example MIDI Monitor on macOS. Watch the LXR's MIDI
  out on DIN, through the MIDI interface: H5, H6, H8, H10 and H11 are written for it. X4 checks the
  LXR's USB MIDI out.
- For H6: a MIDI clock master on DIN (the DAW through a MIDI interface, or a hardware sequencer).
- For H11: a synth, or the MIDI monitor, on the LXR's MIDI out.
- For H7 and X4: SendMIDI, a command-line tool that sends MIDI messages to a port.

**Installing an update file** (as for any update; also in README.md)

1. Copy the `.img` file to the top folder of the card. Delete every other `LXR*.img` file there: the
   updater installs the first one it finds.
2. Switch the LXR off, put the card in, hold down the encoder button and switch the LXR on. Keep
   holding the button until `LXR Updater` appears.
3. The display shows `Checking Firmware...`, then `Loading Firmware` with a percentage, then
   `Update OK!`, and the LXR restarts by itself.

If an update fails, switch off and install again; the updater itself is never changed. If LICHOMAT
misbehaves badly, go back to 1.7 with H13 and report what happened.

**Where things are on the LXR**

- Global menu: hold SHIFT and press step button 12. Each further SHIFT + step 12 moves on, in this
  order: the first sub-page (BPM, shuffle, MIDI channel, screensaver), its second screen (MIDI
  transmit filter, receive filter, routing, follow), the second sub-page (kit autoload, clock in PPQ,
  clock out PPQ), **the new CPU page**, and back to the first sub-page. Coming from the second
  screen of another page, the menu opens on the MIDI filter screen, so the CPU page is one press
  sooner.
- Sync mode: the global menu's BPM value. Below 2 it shows `MID` (1: MIDI clock, DIN and USB) or
  `EXT` (0: clock in jack). 2 and above is the internal tempo.
- Step timing: in step mode select a step; its page shows velocity, note and probability, and its
  second screen flam (ratchet: `off`, `x2`, `x3`, `x4`, `x6`, `x8`) and shift (`-7`…`+7`, in 1/8
  of a step).
- A voice's MIDI channel and note (the note override) are on the second screen of its MIX sub-page
  (select button 8).

**Measuring times in a recording.** Record the LXR's main output at 48 kHz (H7 step 6 at 44.1
kHz). Find a hit's onset by zooming in on the waveform (or with the DAW's transient detection), and
measure from a reference hit.
One audio block of the LXR is 64 samples at 44.14 kHz, 1.45 ms: in fw 1.7 and in LICHOMAT a voice
starts at the beginning of the block after its event is due, so ±1.5 ms around the expected time is
on time. At 120 BPM a step (a 16th) is 125 ms, and 1/96 of a step (a tick) is 1.30 ms.

## H1: start-up screen

1. Install `LXR_V2_LICHOMAT_0.1.img`.
2. Watch the display while the LXR starts after `Update OK!`, and once more after switching it off
   and on.

**Expected:** for about a second line 1 shows `LICHOMAT` and line 2 `v0.1 unofficial`; then the
normal screen. Nothing says `Sonic Potions` or `LXR2 Drums`.

**Record:** both lines as shown; anything unexpected during the update.

## H2: files from 1.7

Use the card with your own projects saved by fw 1.7.

1. Switch on with LICHOMAT. The LXR loads the global settings and the last used project, as 1.7
   does.
2. Play a few patterns of that project, switch patterns while it plays, and check that the kits sound
   as before (the kit of a pattern loads on the pattern change when kit autoload is on).
3. Load another project and play it.
4. Play a song in song mode: hold SHIFT and press START.

**Expected:** no `glo.cfg error` at start-up; the global settings are the ones you set in 1.7 (MIDI
channels, filters, sync mode); every pattern, kit, song and project loads and plays as in 1.7. Swing
and ratchets may sound different where both are used together: that is D1, see H4.

**Record:** project names used; any file that fails to load or sounds different (and how).

## H3: straight timing and exact tempo

1. LXR: internal sync, BPM **137**, shuffle 0. A pattern with one short sound (e.g. the closed hat,
   short decay) on all 16 steps, nothing else.
2. DAW: tempo 137 BPM, record the LXR for at least 30 s while it plays (press START after the
   recording runs).
3. Move the recording so that the first hit sits exactly on a bar line.
4. Measure the onset of hit 257 (16 bars later; the first hit is hit 1) against its grid line, and
   look at the hits in between.

**Expected:**
- Hit 257 is 28.029 s after hit 1 and lies on its grid line within ±3 ms (±1.5 ms block,
  plus up to about 1.5 ms for the difference between the two devices' crystals). fw 1.7 plays 137 BPM
  as 137.29 BPM, so with 1.7 hit 257 comes about 59 ms early (D3).
- Every hit lies within ±1.5 ms of its grid line (spread of one audio block).

**Record:** offset of hit 257 in ms; the largest offset of any hit.

## H4: shuffle with ratchets and shift

1. LXR: internal sync, BPM 120, shuffle **64** (first sub-page of the global menu).
2. Pattern:
   - voice 1 (kick): steps 1 and 9;
   - voice 6 (closed hat): all 16 steps;
   - voice 4 (snare): step 2 with flam `x4`;
   - voice 2: step 4 with shift `+3`.
3. Record the LXR in the DAW and measure from the kick on step 1.

**Expected** (shuffle 64 is a swing of 32 ticks, 41.7 ms; steps 2, 4, 6 … are the swung ones):
- hats on odd steps at 0, 250, 500 … ms; on even steps 41.7 ms late: 166.7, 416.7 … ms;
- the four snare hits at 166.7, 197.9, 229.2 and 260.4 ms: the whole ratchet moves with the swing and
  stays evenly spaced (31.25 ms). fw 1.7 played them as a burst at the start of the step (D1);
- voice 2 at 463.5 ms (375 + 41.7 swing + 46.9 shift);
- the kick on step 9 at 1000.0 ms: the straight steps do not move;
- each time within ±1.5 ms.

**Record:** the measured times of the four snare hits, of voice 2 and of the step 9 kick.

## H5: MIDI clock out with shuffle

1. LXR: internal sync, BPM 120, shuffle **127**, a pattern running.
2. MIDI out of the LXR (DIN, through the interface) to the MIDI monitor and to a device or DAW that
   follows MIDI clock and shows the tempo it receives.
3. Watch for a minute; then stop the LXR and watch again.

**Expected:**
- The receiving device shows a steady 120 BPM, without wobble. With fw 1.7 and shuffle on, the clock
  intervals ranged from 0.1 to 58 ms (D1).
- In the MIDI monitor, on DIN through the MIDI interface, the clock (F8) intervals are 20.8 ms, nearly
  all within ±2 ms, and never below 15 or above 27 ms.
- While stopped the clock keeps running, as in 1.7. START sends Start (FA) followed by a clock.

**Record:** the tempo the device shows; the smallest and largest F8 interval in the monitor.

## H6: MIDI clock slave over DIN

1. LXR: BPM set to `MID`; MIDI receive filter passes realtime messages (the default does). The
   master's clock goes to the LXR's DIN MIDI in. The LXR's DIN MIDI out goes to the MIDI monitor,
   with MIDI routing (thru) off.
2. Pattern: voice 1 on steps 1, 5, 9, 13 (quarter notes); voice 6 on all 16 steps; all tracks 16
   steps long.
3. Master at 120 BPM. In the DAW record the LXR together with the master's click (or a reference
   track on the beats).
4. Press Start on the master; let it run 2 minutes; Stop.
5. Continue on the master (without moving its position).
6. Change the master's tempo while running, from 120 to 140 BPM (a ramp if it can do one).
7. Swung ratchets past the next clock (D7). Master back at 120 BPM, stopped. LXR: shuffle 127; voice 4
   on step 2 with flam `x8` and shift `+7`. Start the master and record 4 bars.

**Expected:**
- After Start, the LXR starts at step 1 on the master's first beat; its kicks stay on the master's
  beats for the 2 minutes, without drift, within ±2 ms around a constant MIDI delay.
- Stop stops the LXR.
- Continue resumes where it stopped (the step after the last one played). fw 1.7 restarted from
  step 1 (an intended change, N4).
- The LXR follows the tempo change; no hat is missing: count the hats over 8 bars, 128.
- The monitor shows the LXR sending no Start, Stop or Continue of its own (N18.2; fw 1.7 answered
  a Start with Stop and Start). It sends no MIDI clock either.
- 7: all 8 hits of voice 4 play in every bar, 15.6 ms apart, the first 317.7 ms after step 1 (125 ms
  to step 2, 83.3 ms of swing, 109.4 ms of shift), the last 427.1 ms after it, inside step 4. fw 1.7
  dropped the hits that the next clock's step left unprocessed (D7).

**Record:** the kick offsets at the start and after 2 minutes; Continue behaviour; hats counted
during the tempo change; anything the LXR sent in the monitor; in 7, the voice 4 hits counted per
bar and the spread of their spacing.

## H7: MIDI clock slave over USB, Song Position Pointer, SD saves

The DAW sends MIDI clock and Song Position Pointer to the LXR's USB MIDI port (it still calls itself
`Sonic Potions USB MIDI`). LXR BPM set to `MID`.

1. Start, Stop, Continue as in H6.
2. Locate in pattern mode. Make a 64-step pattern (all tracks 64 steps long, four bars): step 1
   voice 1, step 17 voice 4, step 33 voice 6, step 49 voice 5, nothing else. With the DAW stopped,
   put its playhead on bar 3, beat 1, and start the DAW from there.
3. Locate inside a step run: playhead on bar 2, beat 3, then start.
4. Locate in song mode. Song: pattern A (16 steps), A again, then pattern B (32 steps, voice 4 on its
   step 1). On the LXR hold SHIFT and press START, then hold SHIFT and press START again: it is now in
   song mode and stopped. Locate the DAW to bar 3, beat 1 and start the DAW.
5. Saving while synced: with the DAW playing and the LXR following, save the current pattern (and
   then a kit) to the SD card.
6. Tempo from a computer (N21). Some hosts send the MIDI clocks of one audio buffer together, in one
   USB transfer; a tempo measured per transfer instead of per clock would read, for example, about
   108 BPM for 120 with 1024-sample buffers. The LXR shows `MID` instead of a tempo, but its
   tempo-synced LFOs run at the tempo it measures.
   - DAW: sample rate 44.1 kHz for this step (not the 48 kHz of the other recordings: the figures
     here and under **Expected** hold for 44.1 kHz), audio buffer 1024 samples (the most clocks per
     transfer), 120 BPM, MIDI clock to the LXR's USB port.
   - LXR: voice 6 on all 16 steps, nothing else. On voice 6's LFO page: sync `1/1` (one cycle per
     bar), retrigger `off`, a clear waveform (saw or square) and a large amount on a parameter you
     hear well (for example the pitch or the decay).
   - Start the DAW and record 18 bars. Measure the time of 16 LFO cycles: from the start of one
     cycle to the start of the cycle 16 cycles later, each to the nearest hat hit (125 ms).
   - If the DAW's audio buffer can be set to 256 samples, do it again there, still at 44.1 kHz.
7. Program Change just before a Start (N14). DAW stopped, LXR stopped on pattern 1 (in `MID`).
   - Pattern 1: voice 1 on step 1 only. Pattern 3: voice 4 on step 1 only.
   - The global MIDI channel (first sub-page of the global menu) is the channel you send on; the
     receive filter passes Program Change (the default does).
   - Send a Program Change 2 (the third pattern), a Start and two beats of clock to the LXR's USB
     port, one right after the other, so that they usually share a USB transfer:
     `sendmidi dev "Sonic Potions USB MIDI" ch <global channel> pc 2 start clock 120 stop`
     (`clock 120` sends two beats of MIDI clock at 120 BPM).
   - Repeat it 5 times, with `pc 0` in between to go back to pattern 1.

**Expected:**
- 1: as in H6.
- 2: the first sound is voice 6 (step 33) exactly on the DAW's bar 3.
- 3: nothing for 2 beats, then voice 6 on bar 3 in time: the LXR started at step 25 of the pattern.
- 4: pattern B starts on bar 3 with voice 4 on its first step (A and A last 32 steps together).
- 5: the LXR stays in time with the DAW during and after the save; no drift and no stutter in the
  clock (D8: in 1.7 USB clock stalled while the card was written).
- 6: 16 LFO cycles take 32.0 s: within ±1.0 s with 1024-sample buffers (2.8%, the worst case of N21
  for them, plus one hat hit of reading) and ±0.45 s with 256 samples (1%, plus one hit). A tempo
  measured per transfer gives 35.7 s or more. All of these hold for 44.1 kHz; at 48 kHz a tempo
  measured per transfer would read about 117 BPM (32.8 s) and pass the band, so the step does not
  test anything there.
- 7: every time, the first sound is voice 4 (pattern 3's step 1), never voice 1, and pattern 3 stays
  selected. The Program Change acts before the Start, also when both come in one USB transfer, as
  in fw 1.7 (N14: the Program Change now travels with the Start in the sync queues).

**Record:** for 2–4 what played first and where; for 5 any drift you hear or measure; for 6 the time
of 16 LFO cycles and the DAW's buffer size and sample rate; for 7 the first sound of each try.

## H8: per-track choke (D2)

1. Internal sync, BPM 120, shuffle 0. Set each voice to its own MIDI channel (voice n on channel n)
   and watch the LXR's MIDI out in the MIDI monitor; record the audio as well.
2. Scenario a: voice 6 (closed hat) step 1 with flam `x4`; voice 4 (snare) step 2 with shift `-2`.
   Nothing else.
3. Scenario b: voice 1 (kick) step 4; voice 6 step 3 with flam `x2` and shift `+4`. Nothing else.

**Expected:**
- a: four hat hits at 0, 31.25, 62.5 and 93.75 ms, and the snare together with the fourth hat hit
  (93.75 ms). In fw 1.7 the snare swallowed the fourth hat hit.
- b: hat hits at 312.5 and 375.0 ms after step 1, the second together with the kick on step 4. In
  fw 1.7 the kick swallowed the second hat hit.
- The monitor shows one Note On per hit on the hat's channel: 4 per loop in a, 2 in b.

**Record:** hits counted per voice in each scenario.

## H9: pattern change with shifted first and last steps (D5)

1. Internal sync, BPM 120.
2. Pattern A: voice 2 step 16, shift `+7`, with a parameter lock that changes its sound clearly
   (e.g. a much higher pitch) and velocity 127.
3. Pattern B: voice 3 step 1, shift `-7`, with a different, clearly audible parameter lock, and
   velocity 40.
4. Play A, select B during the first half of A, and let A end. Record, and watch MIDI out.

**Expected:**
- Voice 3's note from B plays 109.4 ms before the bar line (7/8 of a step early), already while A
  is playing, with B's parameter lock and MIDI velocity 40.
- Voice 2's note from A plays 15.6 ms before the bar line with A's parameter lock and velocity 127.
- Each plays exactly once. Then B continues normally.
- Repeat with A looping (no pattern change): voice 3 does not play.

**Record:** order and times of the two notes before the bar line; whether each had its own sound and
velocity.

## H10: live recording (D6) and the MIDI echo

1. Internal sync, BPM 100, shuffle 0, a pattern running with voice 6 on all steps as a guide; voice
   1's track empty. Turn recording on (REC).
2. Play voice 1 slightly **before** step 5, in the second half of step 4, a few times on different
   loops:
   - from the LXR: press the second mode button (performance mode), set the roll rate on its page
     to `One`, and press step button 1, which plays voice 1 once;
   - from a MIDI keyboard on voice 1's channel.
3. Watch the LXR's MIDI out in the monitor in play mode and in record mode while you play notes from
   the keyboard.
4. Stop the LXR. Still in performance mode with the roll rate `One`, press step button 1 a few
   times, and play a few notes from the keyboard; watch MIDI out.

**Expected:**
- The note is recorded on step 5 and you hear it once when you play it; the sequencer does not play
  step 5 again right after it in the same loop (in fw 1.7 it played twice). From the next loop on,
  step 5 plays from the pattern.
- Every incoming note that triggers a voice comes back on MIDI out exactly once, as one Note On on the
  voice's channel, in play mode and in record mode, and its Note Off follows about 6 ms later (one
  pulse, 1/24 of a step, at 100 BPM), as for the sequencer's own notes. fw 1.7 sent it twice and
  sent its Note Off only with the voice's next note, a mute, a stop press or a pattern change.
- 4: each press of step button 1 and each note from the keyboard gives one Note On and its Note Off
  about 6 ms later, also while stopped; no note stays on.

**Record:** doubled notes heard (yes or no); echoes per incoming note in each mode; for 3 and 4,
whether each Note On had its Note Off and about how much later.

## H11: MIDI out with a note override (D4)

1. Voice 1: MIDI channel 1, note override 36 (MIX page). Its steps keep their own note (default 60).
2. Pattern: voice 1 on steps 1 and 9, step 5 with flam `x4`, step 13 with shift `+3`. Internal
   sync, BPM 120. LXR MIDI out (DIN) to the synth and to the MIDI monitor.
3. Run for a few loops, mute and unmute voice 1 while it plays (in voice mode: SHIFT + voice
   button 1), then STOP.

**Expected:**
- Exactly one Note On per hit, all with note 36: 7 per loop (1 + 4 + 1 + 1). fw 1.7 sent two per hit
  (60 and 36) and never sent a Note Off for the 60.
- Every Note On is followed by a Note Off for note 36 before the next Note On on that channel.
- After muting and after STOP no note keeps sounding on the synth, and the monitor shows no Note On
  without its Note Off.

**Record:** Note Ons per loop; any hanging note.

## H12: CPU meter

1. Open the CPU page: SHIFT + step 12 until the page after clock in and clock out PPQ. It shows

   ```
   CPU  ..% pk  ..%
   Qmax ... ovf ...
   ```

   `CPU` is the mean load of the audio work (sequencer and sound) over the last second, in percent of
   the time one audio block lasts; `pk` the highest load of a single block in that second; `Qmax` the
   most events the sequencer had queued; `ovf` events rejected because the queue was full. The page
   updates twice a second; the knobs and the encoder do nothing on it. `ovf` counts the event queue
   only, not the MIDI sync input queues (owner question Q9). On the sub-page before it (kit
   autoload, clock in and out PPQ) the scroll marker is `*`, since a page follows.
2. Read the values stopped, with the kit you use most.
3. Heavy case: a kit with all 7 voices busy (distortion and filters on), the delay effect on, all 7
   tracks playing 16ths with several `x8` ratchets, BPM 140. Read the values for 30 s.
4. The same with the delay off.
5. Change the pattern while it plays, with kit autoload on.

**Expected:** `CPU` and `pk` below 100 in steps 2–4, and `ovf` 0. In step 5 the values stay
plausible: the meter leaves the kit load itself out. These numbers are the input for stage 3
(effects); there is no pass mark beyond `ovf` 0 and no audible dropouts.

**Record:** `CPU`, `pk`, `Qmax`, `ovf` for steps 2–5, with the kit name, the effect settings and the
BPM.

## H13: back to the official firmware

1. Install `LXR_V2_V1.7.img`.
2. Start the LXR, load the project from H2 and play it.
3. Afterwards install `LXR_V2_LICHOMAT_0.1.img` again if you want to keep using LICHOMAT.

**Expected:** the start-up screen shows `Sonic Potions` and `LXR2 Drums V 1.7`; the project loads and
plays as before; nothing written by LICHOMAT disturbs 1.7.

**Record:** the start-up screen; whether the project played normally.

## U1: step buttons in the global menu

Not in the spec's list: a 1.7 bug that LICHOMAT fixes (tracker E2.7). In 1.7 a step button pressed
without SHIFT while the global menu was open set the length of every track. The check changes track
lengths and sets them back; use a pattern you can spare.

1. Internal sync, BPM 120. Press voice button 1. Pattern: every track 16 steps long, voice 1 (kick)
   on step 1 only, voice 6 (closed hat) on step 16 only. Press START.
2. Hold SHIFT and press step 12: the global menu opens (BPM, shuffle, …) and step 12 blinks.
   Release SHIFT.
3. Press page up, then page down.
4. Press step 4, then step 8, without SHIFT. Listen for two bars.
5. Hold SHIFT and press step 14.
6. Press step 8 without SHIFT and listen. Then hold SHIFT and press step 14 again, and press step 16
   without SHIFT.
7. Press the first of the four mode buttons (voice mode; the third and fourth are LOAD and SAVE),
   then step 5.

**Expected** (the remarks on 1.7 are for the same steps run on 1.7):
- 3: step 12 keeps blinking, and after page down the step 1 LED is lit (voice 1's step). With 1.7
  page up stopped the blinking, and page down made step 16 blink, the last step of track 1.
- 4: the display stays on the global menu, step 12 keeps blinking, and the kick and the hat still
  play once per bar each. With 1.7 the first press made every track 4 steps long (the kick on every
  4th step, the hat silent) and step 4 blink, the second made them 8 steps long and step 8 blink.
- 5: the Last Step page shows `Last Step Trk: 1` and `Current:  16`: step 4 changed no length. 1.7
  shows `Current:   8`.
- 6: as in 1.7: step 8 makes track 1 eight steps long (two kicks per bar, the hat unchanged); the
  second SHIFT + step 14 shows `Last Step All`, and step 16 makes every track 16 steps long again.
  With 1.7 every track is 8 steps long already before step 8.
- 7: as in 1.7: the voice page returns, and step 5 adds a kick on step 5 (press step 5 again to
  remove it).

**Record:** whether step 12 kept blinking in step 3; whether a length changed in step 4; what the
Last Step page showed in step 5.

## X4: USB MIDI out

Not in the spec's list: 1.7 defects of the USB MIDI out that LICHOMAT fixes (tracker E2.8).

Setup:
- The LXR is connected to the computer by USB.
- MIDI Monitor shows the LXR's USB port, `Sonic Potions USB MIDI`, with time stamps.
- A DAW records MIDI from that port.
- Every voice sends on its own MIDI channel: voice n on channel n, as in H8.

1. Internal sync, BPM 120, a pattern with voice 1 on steps 1, 5, 9 and 13 only. Leave the LXR
   stopped for 10 s, then press START, wait 30 s, and press STOP.
2. A long stream that wraps the transmit buffer many times: all 7 voices on all 16 steps, and on
   voice 1 every step with flam `x8`; BPM 180. Record the LXR's USB MIDI in the DAW for 16 bars
   (about 8700 messages), then STOP.
3. System common and SysEx passed from DIN to USB:
   - Set the routing (second screen of the global menu's first sub-page) to `M2U`.
   - With the LXR stopped, send from the computer, through the MIDI interface, to the LXR's DIN
     MIDI in: a Song Position Pointer of 32 (`F2 20 00`), a Song Select 5 (`F3 05`), a Tune
     Request (`F6`), an MTC quarter frame (`F1 13`), and the SysEx
     `F0 7D 01 02 03 04 05 06 07 08 09 F7`.
   - With SendMIDI, for example:
     `sendmidi dev "<name of the MIDI interface>" spp 32 ss 5 tun tc 1 3 hex syx 7D 01 02 03 04 05 06 07 08 09`.
   - A DAW can send the SPP (locate while stopped) and MTC to the interface as well.
4. Play the pattern of step 1. Pull the USB cable, wait 10 s, plug it in again, and select the
   port in MIDI Monitor again.
5. Play the pattern of step 1. Quit MIDI Monitor and the DAW, so that no program uses the LXR's
   port, wait 10 s, and start MIDI Monitor again.
6. A Tune Request after a Note On, passed on from DIN (N20). Routing `M2U` as in 3, the LXR
   stopped. Send to the LXR's DIN MIDI in, on a channel that no voice and not the global MIDI
   channel uses (16 here), a Note On, a Tune Request and the Note Off:
   `sendmidi dev "<name of the MIDI interface>" ch 16 on 60 100 tun off 60 0`. If your SendMIDI has
   the `raw` command, also send `hex raw 9F 3C 64 F6 3C 64`: a Note On, a Tune Request and two stray
   data bytes.

**Expected:**
- 1: while stopped, only Clock (F8) messages, 20.8 ms apart. START gives Start (FA), then a Note
  On and its Note Off on channel 1 every 500 ms between the clocks. STOP gives Stop (FC). No
  message of any other type appears at any time. 1.7 sent 128 bytes of its memory every 32
  messages; a host drops most of them, but any it delivers show up here as stray messages.
- 2: in the DAW, channel 1 has 8 evenly spaced short notes (about 3.5 ms long) on every step, 128
  per bar, and channels 2-7 one note per step, 16 per bar. Zoom to one bar at a time and look
  through all 16: no note is missing, and none is long (stuck).
- 3: MIDI Monitor shows each of the five once, with exactly the bytes sent, between the LXR's own
  clocks. With 1.7 the SPP, the Song Select and the quarter frame arrived without their data, and
  the SysEx garbled.
- 4: the LXR plays on while the cable is out. After plugging in, the clock and the notes come back
  at their normal rate, with no group of old messages sharing one time stamp.
- 5: when MIDI Monitor runs again, at most 15 old messages share its first time stamp (the
  transfer that was under way when the computer stopped reading), then the clock and the notes go
  on at their normal rate. If the computer kept reading the port while no program used it, there
  are no old messages at all.
- 6: MIDI Monitor shows the Note On (channel 16, note 60, velocity 100), then the Tune Request
  alone, `F6`, then the Note Off. fw 1.7 passed the Tune Request on with the Note On's data bytes,
  `F6 3C 64`. For the raw bytes: only the Note On and the Tune Request arrive; the two data bytes
  after `F6` are dropped, where fw 1.7 took them as a repeat of the Note On (running status).

**Record:** for 1, any message of another type; for 2, any missing or long note (bar and channel);
for 3, what MIDI Monitor showed for each message; for 4 and 5, what came first, and how many
messages shared the first time stamp; for 6, the bytes of each message MIDI Monitor showed.

## Optional: clock jacks

Not in the spec's list, but stage 1 changed how the jacks are driven (tracker E2.5).

- **X1, clock out.** Internal sync, shuffle 127, clock out PPQ 4 and then 24. On a scope or a
  clocked module the pulses are evenly spaced (at 120 BPM: 125 ms at 4 PPQ, 20.8 ms at 24 PPQ),
  shuffle or not, and only while running.
- **X2, clock in.** BPM set to `EXT`, clock in PPQ 4, a 4 PPQ clock (16ths) into the clock in jack:
  the LXR follows within one pulse and plays every step. Again with 24 PPQ. At 4 PPQ the LXR plays
  each step when its measured tempo says the pulse is due, not when the pulse arrives (owner
  question Q11; fw 1.7 played each step at its pulse). So a swung 4 PPQ clock plays straight, and
  when the clock stops without a reset, one more step plays than pulses arrived.
- **X3, reset input.** In `EXT` mode, while a high gate is at the reset input the LXR stays stopped at
  the start; when the gate goes low it starts with the next clock pulse (the reset input works as a
  run gate, as in 1.7). Then quick edges: with the clock running, drive the reset input for a second
  with a fast square wave or a burst of short triggers (100 Hz or faster), and end the burst with
  the gate held high; then end one with the gate held low. Do each 10 times. Every time, once the
  edges stop, the LXR is stopped while the gate stays high and runs from step 1 while it stays
  low: no edge is lost (`ac08e38`; fw 1.7 could lose one and stay stopped, or running, against the
  gate's level).

**Record:** what you saw for X1–X3: for X2 at 4 PPQ, whether a swung clock played straight and how
many steps played after the clock stopped; for X3, how many of the quick-edge tries ended in the
wrong state.

## Run sheet

Update files under test (filled in when the files were built):

| File | SHA-256 | Built from | Where |
|---|---|---|---|
| `LXR_V2_LICHOMAT_0.1.img` | `639c0a436356d4eaabe13b6279e989c10f3de0ff28f7ace4bfecb3633319baa3` | `timing-core` at `9805d15` | `build/release/` |
| `LXR_V2_V1.7.img` | `cdb1ca371599149083bd0b61564d6864f031b599ccdcead2047a711e0b679db4` | `a43cd4f`, the official 1.7 sources | `build/official-1.7/src/build/release/` |

Results (PASS, FAIL or SKIPPED, with the date and the values asked for under **Record**):

| Check | Result | Date | Values and notes |
|---|---|---|---|
| H1 | | | |
| H2 | | | |
| H3 | | | |
| H4 | | | |
| H5 | | | |
| H6 | | | |
| H7 | | | |
| H8 | | | |
| H9 | | | |
| H10 | | | |
| H11 | | | |
| H12 | | | |
| H13 | | | |
| U1 | | | |
| X1 | | | |
| X2 | | | |
| X3 | | | |
| X4 | | | |
