# LICHOMAT 0.1: hardware checklist (H1–H13, U1–U3, X4, optional X1–X3)

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
  out on DIN, through the MIDI interface: H5, H6, H8, H9, H10 and H11 are written for it. X4 checks
  the LXR's USB MIDI out.
- For H6: a MIDI clock master on DIN (the DAW through a MIDI interface, or a hardware sequencer).
- For H11: a synth on the LXR's DIN MIDI out, with its MIDI thru (or a MIDI thru box) on to the MIDI
  interface. Without a synth the monitor alone will do: a Note On without its Note Off is a hanging
  note.
- For H7, U2 (step 3), X4 and the optional step 8 of U3: SendMIDI, a command-line tool that sends
  MIDI messages to a port.
- For H10: a MIDI keyboard (or any other MIDI note source) on the LXR's MIDI in.
- For the optional X1–X3: a clock source for the clock in jack at 4 and 24 PPQ that can swing, a
  gate source for the reset input that can give a square wave or a burst of short triggers at 100 Hz
  or faster, and a scope or a clocked module on the clock out jack.

**Installing an update file** (as for any update; also in README.md)

1. Check the file first: `shasum -a 256 <file>` must print the SHA-256 in the run sheet (a local
   build in `build/release/` is a different file); if it does not, do not use it and report it. Copy the `.img`
   file to the top folder of the card without renaming it (the updater installs only files whose
   name starts with `LXR` and ends with `.img`). Delete every other `LXR*.img` file there: the
   updater installs the first one it finds.
2. Switch the LXR off, put the card in, hold down the encoder button and switch the LXR on. Keep
   holding the button until `LXR Updater` appears.
3. The display shows `Checking Firmware...`, then `Loading Firmware` with a percentage, then
   `Update OK!`, and the LXR restarts by itself.

If an update fails, switch off and install again; the updater itself is never changed. If LICHOMAT
misbehaves badly, go back to 1.7 with H13 and report what happened.

**Where things are on the LXR**

- Global menu: hold SHIFT and press step button 12. Each further SHIFT + step 12 moves on, in this
  order: the first sub-page (`bpm` BPM, `shu` shuffle, `ch` MIDI channel, `ssv` screensaver), its
  second screen (`mtx` MIDI transmit filter, `mrx` receive filter, `mrt` routing, `flw` follow), the
  second sub-page (`kcm` kit autoload, `Aut` or `off`; `cki` clock in PPQ; `cko` clock out PPQ),
  **the new CPU page**, and back to the first sub-page. Coming from the second screen of another
  page, the menu opens on the MIDI filter screen, so the CPU page is one press sooner.
- MIDI filters: `mtx` and `mrx` show the messages they pass: `N` notes, `R` realtime (clock, Start,
  Stop, Continue; received, also Song Position Pointer and MTC), `C` control changes, `P` Program
  Change, `all` for all four (the default). The checks need `mtx` with N and R (`RN`, `CRN`, `PRN`
  or `all`).
- Sync mode: the global menu's BPM value. Below 2 it shows `MID` (1: MIDI clock, DIN and USB) or
  `EXT` (0: clock in jack). 2 and above is the internal tempo.
- Step timing: in step mode select a step; its page shows velocity, note and probability, and its
  second screen flam (ratchet: `off`, `x2`, `x3`, `x4`, `x6`, `x8`) and shift (`-7`…`+7`, in 1/8
  of a step).
- A voice's MIDI channel and note (the note override) are on the second screen of its MIX sub-page
  (select button 8).
- Shuffle belongs to the pattern: selecting a pattern loads the shuffle saved with it, and a value
  set in the global menu lasts until the next pattern change unless you save the pattern. Set the
  shuffle a check asks for after selecting its pattern. A pattern change also discards a pattern's
  unsaved edits: save a pattern before you select another one.
- Track length: lengths are saved with the pattern, and a pattern runs as long as its longest track
  (H7 step 2 makes one with 64 steps). Unless a check says otherwise, all tracks are 16 steps long:
  SHIFT + step 14 twice shows `Last Step All`; with the first bar shown (both page LEDs off), step 16
  sets every track to 16 steps.

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
   as before (exceptions under Expected; the kit of a pattern loads on the pattern change when kit
   autoload is on).
3. Load another project and play it.
4. Play a song in song mode: hold SHIFT and press START.

**Expected:** no `glo.cfg error` at start-up; the global settings are the ones you set in 1.7 (MIDI
channels, filters, sync mode); every pattern, kit, song and project loads and plays as in 1.7, with
these exceptions. Swing and ratchets may sound different where both are used together: that is D1,
see H4. A kit whose Drum 1–3 or snare has a pitch EG slope of 127 (or 0 with the pitch EG decay at
127) keeps the tone that 1.7 took away. An oscillator waveform of 6 or more, set by MIDI and saved
in a kit, is silent where 1.7 played other voices' sound or noise (B9, B10; U3 steps 7 and 8).

**Record:** project names used; any file that fails to load or sounds different (and how).

## H3: straight timing and exact tempo

1. LXR: internal sync, BPM **137**, shuffle 0. A pattern with all tracks 16 steps long and one short
   sound (e.g. the closed hat, short decay) on all 16 steps, nothing else.
2. DAW: tempo 137 BPM, record the LXR for at least 35 s while it plays (press START after the
   recording runs; hit 257 comes 28.03 s after hit 1).
3. Move the recording so that the first hit sits exactly on a bar line.
4. Measure the onset of hit 257 (16 bars later; the first hit is hit 1) against its grid line, and
   look at the hits in between.

**Expected:**
- Hit 257 is 28.029 s after hit 1 and lies on its grid line within ±5 ms: ±1.5 ms for the audio
  block, up to 2.8 ms for the difference between the clocks of the LXR and the audio interface (two
  ±50 ppm crystals differ by up to 100 ppm, 2.8 ms over 28 s), and some room for reading the onsets.
  fw 1.7 plays 137 BPM as 137.29 BPM, so with 1.7 hit 257 comes about 59 ms early (D3).
- Consecutive hits are 109.5 ms apart within ±1.5 ms (spread of one audio block). Against the DAW's
  grid the hits drift evenly from hit 1 to hit 257, so a hit can be off its grid line by up to
  1.5 ms plus the drift at that hit, which grows evenly to its value at hit 257. Put differently,
  every hit lies within ±1.5 ms of the straight line from hit 1 to hit 257.

**Record:** offset of hit 257 in ms; the shortest and the longest interval between consecutive hits.

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
  stays evenly spaced (31.25 ms). fw 1.7 played the first two together at about 167 ms and the other
  two at their unswung times, about 187.5 and 218.8 ms (D1);
- voice 2 at 463.5 ms (375 + 41.7 swing + 46.9 shift); fw 1.7 played it unswung, at about 421.9 ms;
- the kick on step 9 at 1000.0 ms: the straight steps do not move;
- each time within ±1.5 ms.

**Record:** the measured times of the four snare hits, of voice 2 and of the step 9 kick.

## H5: MIDI clock out with shuffle

1. LXR: internal sync, BPM 120, shuffle **127**, the pattern of H4 running (it has at most two voices
   on one step).
2. MIDI out of the LXR (DIN, through the interface) to the MIDI monitor and to a device or DAW that
   follows MIDI clock and shows the tempo it receives.
3. Watch for a minute; then stop the LXR and watch again.
4. Song end. A song of two entries, pattern A then pattern B (any two patterns with a few notes),
   with the song end after them (no loop). Hold SHIFT and press START, and let the song play to its
   end. Then hold SHIFT and press START once.

**Expected:**
- The receiving device shows a steady 120 BPM, without wobble. With fw 1.7 at these settings
  (120 BPM, shuffle 127) the clock intervals range from well under a millisecond (clocks back to
  back) to 104 ms (D1; 0.1 to 58 ms at 130 BPM and shuffle 64).
- In the MIDI monitor, on DIN through the MIDI interface, the clock (F8) intervals are 20.8 ms, nearly
  all within ±2 ms, and never below 15 or above 27 ms. A Note On that the LXR sends at the time of a
  clock goes out before that clock and delays it by about 1 ms on DIN (3 bytes at 31250 baud): with
  the H4 pattern the intervals at the LXR's output are 18.4 to 23.7 ms; a pattern with all seven
  voices on one step spreads them to about 13.6 to 28.5 ms, which is no fault.
- While stopped the clock keeps running, as in 1.7. START sends Start (FA) followed by a clock.
- 4: after B the LXR stops by itself, the monitor shows Stop (FC), and the START LED goes out. The
  one SHIFT + START starts the song at once (Start, FA): A plays once, then B, then it stops again.
  fw 1.7 sent no Stop at the song end, and its START button still counted the song as running. So
  its first press only stopped it (FC), and the second played A twice.

**Record:** the tempo the device shows; the smallest and largest F8 interval in the monitor; for 4,
what the monitor showed at the song end, how many presses the restart took, and the patterns played.

## H6: MIDI clock slave over DIN

H7 step 6 also sends clock pairs to DIN MIDI in: that run is N21's check of joined clocks under H6.

1. LXR: BPM set to `MID`; MIDI receive filter (`mrx`) with R, for example `all` (the default). The
   master's clock goes to the LXR's DIN MIDI in. The LXR's DIN MIDI out goes to the MIDI monitor,
   with MIDI routing (thru) off. The master's clock reaches the LXR on DIN only: nothing sends MIDI
   clock to its USB port (in the DAW, clock sync to Sonic Potions USB MIDI off).
2. Pattern: voice 1 on steps 1, 5, 9, 13 (quarter notes); voice 6 on all 16 steps; all tracks 16
   steps long.
3. Master at 120 BPM; if the master is a DAW, set its audio buffer to 64 samples. In the DAW record
   the LXR together with the master's click (or a reference track on the beats).
4. Press Start on the master; let it run 2 minutes; Stop.
5. Continue on the master (without moving its position).
6. Change the master's tempo while running, from 120 to 140 BPM (a ramp if it can do one).
7. Swung ratchets past the next clock (D7). Master back at 120 BPM, stopped. LXR: shuffle 127; voice 4
   on step 2 with flam `x8` and shift `+7`. Start the master and record 4 bars.
8. Thru (D8). Master stopped; the LXR stays in `MID`. Set MIDI routing (`mrt`) to `M2M`. If the
   master is a hardware sequencer, connect the MIDI interface's MIDI out to the LXR's DIN MIDI in in
   its place for this step; a DAW master sends no clock meanwhile. Through the MIDI interface, send
   to the LXR's DIN MIDI in a Note On on channel 16 (no voice and not the global channel use it), two
   beats of clock and the Note Off:
   `sendmidi dev "<name of the MIDI interface>" ch 16 on 60 100 clock 120 off 60 0`. Then set the
   routing back to off, and connect the master again.
9. Start the master. After a bar, press START on the LXR once; after another bar, press it again.
   Then Stop the master.

**Expected:**
- After Start, the LXR starts at step 1 on the master's first beat; its kicks stay on the master's
  beats for the 2 minutes, without drift, within ±2 ms around a constant MIDI delay. A DAW that
  sends the MIDI clocks of one audio buffer together (some do, see H7 step 6) delays each kick by 0
  to almost one buffer, because the LXR follows each clock when it arrives: up to about 0.9 ms at 64
  samples, 2.4 ms at 128, 3.5–4.7 ms at 256, 9–10 ms at 512 and 20–21 ms at 1024 (44.1–48 kHz), on
  top of the 1.45 ms block. With 64 samples the kicks stay within ±2 ms; if the DAW cannot go below
  128 samples, allow ±2.5 ms.
- Stop stops the LXR.
- Continue resumes where it stopped (the step after the last one played). fw 1.7 restarted from
  step 1 (an intended change, N4).
- The LXR follows the tempo change; no hat is missing: count the hats over 8 bars, 128.
- The monitor shows the LXR sending no Start, Stop or Continue of its own (N18.2; fw 1.7 answered
  a Start with Stop and Start). It sends no MIDI clock either.
- 7: all 8 hits of voice 4 play in every bar, 15.6 ms apart, the first 317.7 ms after step 1 (125 ms
  to step 2, 83.3 ms of swing, 109.4 ms of shift), the last 427.1 ms after it, inside step 4. fw 1.7
  dropped the hits that the next clock's step left unprocessed (D7).
- 8: the monitor shows the Note On (channel 16, note 60, velocity 100), then 48 clocks (F8) with
  nothing between them, then the Note Off. fw 1.7 sent each clock as `F8 3C 64`, which the monitor
  shows as a clock followed by another Note On, 48 times.
- 9: the first press stops the LXR at once, and it sends no Start, Stop or Continue. The second press
  starts it again from step 1 on the master's next 16th note. fw 1.7's START button did not follow
  the master's Start: its first press restarted the LXR from step 1 and sent a Start (FA), and only
  the second stopped it.

**Record:** the kick offsets at the start and after 2 minutes, and the master (for a DAW, its audio
buffer); Continue behaviour; hats counted during the tempo change; anything the LXR sent in the
monitor; in 7, the voice 4 hits counted per bar and the spread of their spacing; in 8, what came
between the clocks; in 9, what each press did.

## H7: MIDI clock slave over USB, Song Position Pointer, SD saves

The DAW sends MIDI clock and Song Position Pointer to the LXR's USB MIDI port (it still calls itself
`Sonic Potions USB MIDI`) and to no other port: switch the DAW's MIDI clock to the MIDI interface
off, and stop H6's hardware sequencer or take it off DIN MIDI in. In `MID` the LXR counts the clocks
of both ports, so clock on both runs it too fast. LXR BPM set to `MID`, shuffle 0 (set it after
selecting each pattern: H6 step 7 left it at 127). For steps 1 to 5 set the DAW's audio buffer to 64
samples, as in H6.

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
6. Tempo from a computer (N21). This checks that the tempo the LXR measures from MIDI clock stays
   within N21's limits. The LXR shows `MID` instead of a tempo, but its tempo-synced LFOs run at the
   tempo it measures, rounded to a whole BPM, so 16 LFO cycles show it. N21's case is a host that
   sends the MIDI clocks of one audio buffer together, in one USB transfer: a tempo measured per
   transfer instead of per clock would then read about 108 BPM for 120 with 1024-sample buffers. A
   host that sends each clock at its own time gives any estimator the right tempo. fw 1.7 passes the
   DAW runs too: it averaged the last 8 clock intervals (about 121 BPM here, 31.7 s), so a pass of
   them shows N21's limits, not a 1.7 defect fixed. The pairs below test the per-transfer case with
   any host.
   - DAW: sample rate 44.1 kHz for this step (not the 48 kHz of the other recordings: the figures
     here and under **Expected** hold for 44.1 kHz), audio buffer 1024 samples (the most clocks per
     transfer), 120 BPM, MIDI clock to the LXR's USB port only.
   - LXR: pattern mode (press START without SHIFT once and stop again: step 4 left it in song
     mode). A pattern with all tracks 16 steps long (not the 64-step one of step 2), voice 6 on all
     16 steps, nothing else. On voice 6's LFO page (select button 7): `snc` `1/1` (one cycle per
     bar), `rtg` `off`, `wav` `sup` or `sdn` (saws) or `sqr`, `voi` 6 and `dst` a parameter you hear
     well (for example the pitch or the decay), with a large `mod` (amount).
   - Start the DAW and record 18 bars. Measure the time of 16 LFO cycles: from the start of one
     cycle to the start of the cycle 16 cycles later, each to the nearest hat hit (125 ms). Also look
     at the hats against the DAW's grid: they show which kind of host you have (see **Expected**).
   - If the DAW's audio buffer can be set to 256 samples, do it again there, still at 44.1 kHz. With
     5.8 ms buffers every clock (20.8 ms apart) comes in a transfer of its own, so this run checks
     only how exact the measured tempo is.
   - Over DIN: once more with 1024 samples, with the DAW's clock going through the MIDI interface
     into the LXR's DIN MIDI in and no MIDI clock to its USB port. Connect the interface's MIDI out
     to the LXR's DIN MIDI in (where H6's hardware sequencer was), switch the DAW's MIDI clock to the
     interface back on, and its clock to the USB port off. An interface passes the clocks of
     one buffer on as a burst about 0.32 ms apart when the host sends them together.
   - Pairs (any DAW). Stop the DAW and switch its MIDI clock output off, to both ports; LXR as above.
     Make a file of 768 pairs of clocks, the two clocks of a pair sent one right after the other and
     the pairs 41.7 ms apart (16 bars at 120 BPM), and a second file whose two clocks of a pair are
     1 ms apart, so that their transfers fall in consecutive 1 ms frames (the host N21 names):

     `awk 'BEGIN { print "start"; for (i = 0; i < 768; i++) printf "00:00:%06.3f mc mc\n", i * 0.125 / 3; print "stop" }' > pairs.txt`

     `awk 'BEGIN { print "start"; for (i = 0; i < 768; i++) { t = i * 0.125 / 3; printf "00:00:%06.3f mc 00:00:%06.3f mc\n", t, t + 0.001 } print "stop" }' > frames.txt`

     Send `pairs.txt` to the LXR's USB port, then through the MIDI interface to its DIN MIDI in, and
     `frames.txt` to the USB port:

     `sendmidi dev "Sonic Potions USB MIDI" -- < pairs.txt`

     `sendmidi dev "<name of the MIDI interface>" -- < pairs.txt`

     `sendmidi dev "Sonic Potions USB MIDI" -- < frames.txt`

     Each time, count the LFO cycles against the hats while it plays (32 s, 256 hats), by ear or in a
     recording (the DAW can record while its transport runs, as long as it sends no clock).
   - These runs are N21's hardware check of transfers and of its 2 ms join (`SEQC_TEMPO_JOIN_MS`):
     the pairs come in one USB transfer or in transfers less than 2 ms apart, and on DIN 0.32 ms
     apart. The 2 ms value itself does not show here: at 120 BPM these sources read the same with
     or without the join. Host tests pin it.
7. Program Change just before a Start (N14). DAW stopped, LXR stopped on pattern 1 (in `MID`).
   - Pattern 1: voice 1 on step 1 only. Pattern 3: voice 4 on step 1 only.
   - The global MIDI channel (`ch`, first sub-page of the global menu) is the channel you send on;
     the receive filter (`mrx`) has P, for example `all` (the default).
   - Send a Program Change 2 (the third pattern), a Start and two beats of clock to the LXR's USB
     port, one right after the other, so that they usually share a USB transfer:
     `sendmidi dev "Sonic Potions USB MIDI" ch <global channel> pc 2 start clock 120 stop`
     (`clock 120` sends two beats of MIDI clock at 120 BPM).
   - Repeat it 5 times, with `pc 0` in between to go back to pattern 1.

**Expected:**
- 1: as in H6 (audio buffer 64 samples).
- 2: the first sound is voice 6 (step 33) exactly on the DAW's bar 3.
- 3: nothing for 2 beats, then voice 6 on bar 3 in time: the LXR started at step 25 of the pattern.
- 4: pattern B starts on bar 3 with voice 4 on its first step (A and A last 32 steps together).
- 5: the LXR stays in time with the DAW during and after the save; no drift and no stutter in the
  clock (D8: in 1.7 USB clock stalled while the card was written).
- 6: 16 LFO cycles take 32.0 s: within ±1.0 s with 1024-sample buffers, over USB and over DIN (2.8%,
  the worst case of N21 for them, plus one hat hit of reading), and within ±0.45 s with 256 samples
  (1%, plus one hit). The LFOs follow the measured tempo rounded to a whole BPM, so the results come
  in steps: 32.0 s at 120 BPM, 31.7 s at 121, 32.3 s at 119. A tempo measured per transfer would
  read 107.67 BPM with 1024-sample buffers, which the LFOs round to 108: 35.6 s; but only if the DAW
  sends the clocks of one buffer together. Nothing on the LXR shows which kind of host you have, but
  the 1024-sample recording does: hats that wobble against the DAW's grid by up to a buffer (up to
  about 20 ms) mean the host sends a buffer's clocks together, and the run tests N21's case; hats
  steady within ±2 ms mean it does not, and then the run gives 32.0 s whether the tempo is measured
  per clock or per transfer. The 256-sample run and 48 kHz (a per-transfer tempo of 117 BPM, 32.8 s)
  do not test it either.
- 6, pairs and frames, over USB and over DIN: 16 LFO cycles in the 32 s, one per 16 hats. A tempo
  that followed the pairs instead of the clocks, as one measured per transfer does when the two
  clocks of a pair share a transfer, reads 60 BPM: 8 cycles, one per 32 hats.
- 7: every time, the first sound is voice 4 (pattern 3's step 1), never voice 1, and pattern 3 stays
  selected. The Program Change acts before the Start, also when both come in one USB transfer, as
  in fw 1.7 (N14: the Program Change now travels with the Start in the sync queues).

**Record:** for 2–4 what played first and where; for 5 any drift you hear or measure; for 6 the time
of 16 LFO cycles in each DAW run, the DAW's buffer size and sample rate, whether the hats wobbled
against the DAW's grid, the LFO cycles counted with `pairs.txt` over USB and over DIN and with
`frames.txt`, and the DAW (with its version) and the operating system; for 7 the first sound of each
try.

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

1. Internal sync, BPM 120. Select pattern A, then set shuffle 0 (first sub-page of the global menu).
   The shuffle is saved with each pattern and selecting a pattern loads it; with shuffle on, A's step
   16 (a swung step) moves later, from shuffle 23 on to the bar line or past it.
2. Pattern A, all tracks 16 steps long: voice 2 step 16, shift `+7`, with a parameter lock that
   changes its sound clearly (e.g. a much higher pitch) and velocity 127. Save A.
3. Pattern B, all tracks 16 steps long: voice 3 step 1, shift `-7`, with a different, clearly
   audible parameter lock, and velocity 40. Save B.
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
   1's track empty. Voice 1 on a MIDI channel of its own that is not the global MIDI channel (as in
   H8, voice n on channel n, and the global channel on one no voice uses, for example 9); voice 1's
   note override `any` (MIX page, second screen; with a note set there, only that note triggers the
   voice, and the default is `C 4`, note 48); MIDI routing (`mrt`) off. The keyboard sends on voice
   1's channel. Turn recording on (REC).
2. Play voice 1 slightly **before** step 5, in the second half of step 4, a few times on different
   loops:
   - from the LXR: press the second mode button (performance mode), set the roll rate on its page
     to `One`, and press step button 1, which plays voice 1 once;
   - from a MIDI keyboard on voice 1's channel.
3. Watch the LXR's MIDI out in the monitor in play mode and in record mode while you play notes from
   the keyboard.
4. Stop the LXR. Still in performance mode with the roll rate `One`, press step button 1 a few
   times, and play a few notes from the keyboard; watch MIDI out.
5. Repeating roll. Remove the notes recorded on voice 1 in 2: in voice mode press voice button 1
   first, then their step buttons (programming the guide may have left voice 6 selected). Running,
   REC on. On the performance page set the roll rate to `1/8`, hold step button 1
   for about a bar, then let go.

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
- 5: while the button is held, voice 1 plays on every second step (1, 3, 5 …). The monitor shows one
  Note On per roll hit on voice 1's channel, velocity 100 (the note, or its override), each followed
  by its Note Off. From the next loop on, those steps play from the pattern. fw 1.7 sent two Note
  Ons per roll hit.

**Record:** doubled notes heard (yes or no); echoes per incoming note in each mode; for 3 and 4,
whether each Note On had its Note Off and about how much later; for 5, Note Ons per roll hit, their
velocity, and the steps recorded.

## H11: MIDI out with a note override (D4)

1. Voice 1: MIDI channel 1, note override `C 3`, which is MIDI note 36 (MIX page, second screen,
   `nte`; the LXR counts octaves from note 0, `C 0`, so 36 is `C 3` and 60 is `C 5`, one or two
   octaves above the names most DAWs use). Its steps keep their own note (default `C 5`, note 60).
2. Pattern: voice 1 on steps 1 and 9, step 5 with flam `x4`, step 13 with shift `+3`. Internal
   sync, BPM 120. LXR MIDI out (DIN) to the synth, and the synth's MIDI thru (or a MIDI thru box) on
   to the MIDI interface, so that the MIDI monitor still watches DIN.
3. Run for a few loops, mute and unmute voice 1 while it plays (in voice mode: SHIFT + voice
   button 1), then STOP.
4. Two voices on one channel. Voice 2: MIDI channel 1 as well, note override `D 3` (note 38, MIX
   page); pattern: voice 2 on steps 1 and 9 too. START, run a few loops, STOP.

**Expected:**
- Exactly one Note On per hit, all with note 36: 7 per loop (1 + 4 + 1 + 1). fw 1.7 sent two per hit
  (60 and 36) and never sent a Note Off for the 60.
- Every Note On is followed by a Note Off for note 36 before the next Note On on that channel.
- After muting and after STOP no note keeps sounding on the synth, and the monitor shows no Note On
  without its Note Off.
- 4: per loop, 7 Note Ons with note 36 and 2 with note 38. Every Note On is followed by a Note Off of
  the same note before that note's next Note On, also on steps 1 and 9, where both voices play:
  there the monitor shows both Note Ons, then both Note Offs about 5 ms later. fw 1.7 kept one note
  per channel, so a voice's Note Off went to the note the channel sent last: on steps 1 and 9 voice
  2's hit turned off voice 1's 36 at once (in the first loop not at all), and voice 2's 38 sounded
  until voice 1's next hit; every hit also sent a 60 that never got its Note Off (D4).

**Record:** Note Ons per loop; any hanging note; for 4, any Note On without its Note Off.

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
   only, not the input queues of MIDI and the clock jack (owner question Q9). On the sub-page before
   it (kit autoload, clock in and out PPQ) the scroll marker is `*`, since a page follows. Turn the
   encoder both ways and each of the four knobs, and press the encoder.
2. Read the values stopped, with the kit you use most.
3. Heavy case: a kit with all 7 voices busy (distortion and filters on), the delay effect on, all 7
   tracks playing 16ths with several `x8` ratchets, BPM 140. Read the values for 30 s.
4. The same with the delay off.
5. Change the pattern while it plays, with kit autoload on.

**Expected:** in step 1 the display stays on the CPU page and its values go on updating twice a
second; nothing can be selected or edited, and on the sub-page before it, kit autoload and the clock
in and clock out PPQ keep their values. `CPU` and `pk` below 100 in steps 2–4, and `ovf` 0. In step
5 the values stay plausible: the meter leaves the kit load itself out. These numbers are the input
for stage 3 (effects); there is no pass mark beyond `ovf` 0 and no audible dropouts.

**Record:** in step 1, whether the display stayed on the CPU page and what, if anything, the encoder
and the knobs changed; `CPU`, `pk`, `Qmax`, `ovf` for steps 2–5, with the kit name, the effect
settings and the BPM.

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

1. Internal sync, BPM 120, voice mode (not performance mode, where a voice button mutes its voice).
   Pattern: every track 16 steps long, voice 1 (kick) on step 1 only, voice 6 (closed hat) on step
   16 only. Then press voice button 1, so that voice 1 is the selected voice for the rest of the
   check, and press START.
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

## U2: kit loads and Bank Select leave the patterns alone

Not in the spec's list: a 1.7 bug that LICHOMAT fixes (tracker E4.5). In 1.7 every kit load, and
every MIDI Bank Select (CC 0) the LXR acted on, wrote one byte 64 KB past a table into the pattern
memory; after a kit load that byte holds the kit's parameter 127, which is 0 in practically every
kit. In our build of the official 1.7 sources that byte is the number of the first parameter lock
of step 52 of voice 3 in the 18th pattern; a pattern save kept it on the card. Steps 1–4 check that
kit loads and Bank Selects change no pattern. They do not show the 1.7 bug: a project load is a kit
load too, so 1.7 writes the byte before the save of step 1, and both of its saves hold the same
value. Steps 5 and 6 show it: every step of a new project has the values of an empty step, and 1.7
changes one of them. Use a project you can spare, or a copy, and a project slot shown as `Empty`.

1. With LICHOMAT and kit autoload on (`kcm` `Aut`, second sub-page of the global menu; the
   default): load the project and save it (project save). Switch off, put the card in the computer
   and copy the project's folder (`PROJnn`, nn = the project's slot) to the computer as `before`.
   Put the card back and switch on.
2. Load the kit that the LOAD page offers now 10 times (LOAD, the kit, confirm): with kit autoload
   on it is the active pattern's kit. A kit load from the menu also stores the kit's number in the
   active pattern, so load that same kit: then the pattern keeps its kit number.
3. The receive filter (`mrx`) has C, for example `all` (the default). Send 10 Bank Select messages
   on the LXR's global MIDI channel (`ch`, first sub-page of the global menu) to its USB port:
   `sendmidi dev "Sonic Potions USB MIDI" ch <global channel> cc 0 0 cc 0 0 cc 0 0 cc 0 0 cc 0 0 cc 0 0 cc 0 0 cc 0 0 cc 0 0 cc 0 0`.
   If the kit of step 2 is not kit 0, the LOAD page now offers kit 0: the messages arrived.
4. Save the project again, copy the folder to the computer as `after`, and compare every pattern
   file: `for f in before/*.PAT; do cmp "$f" "after/${f#before/}"; done`.
5. With the sequencer stopped: LOAD, PROJECT, the `Empty` slot, confirm. The LXR makes a new
   project there, with empty patterns and the default global settings. Save it to that slot
   (project save) before you select a pattern, switch off and copy its folder to the computer as
   `new`.
6. List the steps of `new` that differ from an empty step (velocity 127, probability 100, note 60,
   no parameter locks), then put the card back, switch on and load your own project again:

   ```
   python3 -c 'import sys, glob
   for p in sorted(glob.glob(sys.argv[1] + "/*.PAT")):
       d = open(p, "rb").read()[14:]
       for t in range(7):
           for s in range(64):
               r = d[t*512 + 7*s : t*512 + 7*s + 7]
               if r[0] & 127 != 127 or r[1:] != bytes([100, 60, 255, 0, 255, 0]):
                   print(p, "track", t + 1, "step", s + 1, r.hex())' new
   ```

**Expected:** 4: `cmp` prints nothing: every `.PAT` file of `after` is byte for byte the same as in
`before`. A Bank Select loads the kit of its value (here kit 0) into the sound engine without
changing a pattern, so the sound may change and the kit files may differ; they are not checked.
1.7 gives the same result. 6: the command prints nothing. Our 1.7 build (`LXR_V2_V1.7.img` of the
run sheet) prints one line, `new/17-….PAT track 3 step 52 7f643c0000ff00`: the first parameter lock
of that step points at parameter 0.

**Record:** the number of pattern files compared and the output of `cmp`; the output of step 6.

## U3: the night's fixes of fw 1.7 bugs

Not in the spec's list: fw 1.7 bugs that LICHOMAT fixes (tracker E4.6–E4.9), B1–B10 of a sanitizer
sweep of the firmware on a Mac (`docs/lichomat/reports/sanitizer-sweep-2026-10-27.md`, B1–B19).
These steps cover B2, B3, B5 and B8–B10; B1, B4, B6 and B7 have host tests only. The remarks on
1.7 are for the same steps run on 1.7. The check takes about 20 minutes. It works in a new project in
an `Empty` slot and changes its kit and songs, so your own projects stay as they are.

1. Kit 0's name (B3). With the sequencer stopped: LOAD, `Project`, an `Empty` slot, confirm (as in
   U2 step 5). Then LOAD, `Kit`, slot `0`: note the name it shows, and leave the page without
   confirming (press the first mode button).
2. Save the project twice: SAVE, `Project`, the same slot, confirm (press the encoder, and once
   more at `Edit Name? [y/N]`); then the same once more. The load of step 1 made the project's
   folder, empty, so the first save has nothing to delete; the second save deletes the first save's
   files first. Then LOAD, `Kit`, slot `0` again, and leave the page without confirming.
3. Song page after a stop (B5). Make a pattern with all tracks 16 steps long: voice 1 (kick) on
   steps 1, 5, 9 and 13, voice 4 (snare) on steps 5 and 13, voice 6 (closed hat) on all 16 steps.
   Internal sync, BPM 120: in the global menu (SHIFT + step 12) turn `bpm` to 121 and back to 120,
   also when it shows 120 already. A new project shows the default global settings, but the LXR
   keeps the sync mode and the tempo of the project before it until the value changes (as in 1.7).
   Press START, STOP and START again. While it plays, hold SHIFT and press step 10: the song page.
   Read line 1. Press STOP. Both page LEDs must be off (the first 16 patterns): press page down
   until they are. Then press step 5 (the pattern of step 5 goes into the song). Turn the encoder
   one step to the left, without pressing it.
4. Song 3 (B5). LOAD, `Song`, slot `3`, confirm. Hold SHIFT and press step 10, and turn the encoder
   to the right until it stops at entry `[063]`, the last one. Then LOAD, `Song`, slot `0`, confirm.
5. Ping-pong delay (B2). Voice 4: MIX sub-page (select button 8), second screen, `out` `FX`. Open
   the FX page: SHIFT + step 15. Knob 1 (`typ`) `del`, `out` `St1`, `d/w` about 50, knob 4 (`typ`,
   the delay type) `Del`. Turn the encoder to the second screen (`tim rng ton fbk`) and set `tim`
   about 60, `rng` 127, `ton` about 100 and `fbk` about 60, so that the snare's echoes are clear: a
   new project keeps `rng`, `ton` and `fbk` of the kits before it, and with `rng` or `ton` low there
   are no echoes. Turn the encoder back to the first screen, where knob 4 is the delay type. Press
   START. While it plays, turn knob 4 from `Del` to `Pp` and back, about 1 s on each, 20 times.
   Press STOP, then START once more.
6. Hi-hat filter (B8). Voice button 6, select button 6 (the filter): knob 1 `frq`, knob 2 `res`,
   knob 3 `typ`, knob 4 `drv`. Note the four values. With the pattern playing, set `typ` `HP`,
   `drv` 127, `res` 127, and then `frq` 125 (122 to 126 do the same). Listen for 2 bars. Then set
   `frq` back to its noted value, then the other three, and listen for 2 bars.
7. Pitch EG slope (B9). Voice button 1, select button 3 (modulation): knob 1 `dec` (the pitch EG
   decay), knob 2 `slp` (its slope). Note both values. With the pattern playing, turn `slp` to 127
   and listen for 2 bars, then to 126. Then `slp` 0 with `dec` 127, 2 bars. Set both back to their
   noted values.
8. Optional, with a DAW or SendMIDI at hand: waveform 6 or more (B10). The panel's waveforms are 0–5
   (`Sin` to `PWM`); a MIDI CC or a kit can set more. In the global menu set `ch` to 1, and set
   `mrx` to another value and back to `all`: a new project shows `ch` 0 and `mrx` `all`, but the LXR
   keeps the global channel and the receive filter of the project before it until they change. Note
   Drum 1's `wav` (voice button 1, select button 1, knob 3). With the pattern playing, send CC 2
   (Drum 1's waveform) on channel 1 with the value 6, then 127, each for 2 bars, then 0:
   `sendmidi dev "Sonic Potions USB MIDI" ch 1 cc 2 6`, then `cc 2 127`, then `cc 2 0`. Set `wav`
   back to its noted value.
9. Press STOP and load your own project again (LOAD, `Project`, its slot, confirm).

**Expected** (the remarks on 1.7 are for the same steps run on 1.7):
- 2: kit slot `0` shows the name of step 1 after both saves. With 1.7 the second save renames it:
  the name turns into the end of a file name of the folder, such as `.SND` or `.SNG`, and the save
  stores kit 0 under that name (a file such as `00-_SND.SND`).
- 3: line 1 shows `[000]`, the song's first entry. After step 5 the page moves on to `[001]`, and one
  step to the left shows `[000]` with `p04`, the pattern of step 5. With 1.7 the page opens at
  `[255]`, past the song's 64 entries, and step 5 writes `p04` into another song.
- 4: entry `[063]` of song 3 shows `END`, as every entry of the new project's songs except entry
  `[000]` of song 0. With 1.7 it shows `p04`: in step 3 the press went into the last entry of the
  song three slots up.
- 5: the LXR plays on through all 20 switches, the echoes change between one line and left-right
  ping-pong, and nothing else changes: no freeze, no noise that stays, no other sound changed. The
  display and the buttons respond afterwards, and START plays again. With 1.7 about every second
  switch writes past the delay's memory: from no effect to a changed sound, noise, or a freeze
  until power-off. Do not try step 5 on 1.7 with a card you need: the stray write can also hit the
  card driver's memory, and the next save can then damage files on the card.
- 6: while `frq` is at 125 the hat sounds broken, for example a harsh buzz on each hit, or drops
  out (1.7 falls silent there). Within a hit after `frq` goes back the hat plays again (in about 8
  audio blocks, 12 ms), loud and ringing while `res` and `drv` are still at 127; once all four
  values are back it sounds as at the start. With 1.7 the hat stays silent until power-off,
  whatever `frq`, `res` and `drv` are set to afterwards, also after a kit load; only the filter
  types `LP2` and `off`, which bypass the broken filter state, let it play.
- 7: at `slp` 127 Drum 1 keeps its tone and sounds practically as at 126 (its pitch stays high a
  few milliseconds longer); at `slp` 0 with `dec` 127 it keeps its tone too, without a pitch sweep.
  Apart from that case, 0 to 126 sound as in 1.7. With 1.7 Drum 1 loses its tone at `slp` 127 and
  at `slp` 0 with `dec` 127 until the value changes. In this new project, whose init kit sets Drum
  1's transient and FM to 0, it falls practically silent: at most a dull click is left on each
  hit.
- 8: with 6 and 127 Drum 1 loses its tone: at most its transient click is left, and no other voice's
  sound and no noise comes out of it. 0 gives the `Sin` tone again. With 1.7 Drum 1 plays what its
  buffer held before: other voices' sound (crosstalk) or noise, up to full scale. Turn the volume
  down before you try this on 1.7.

**Record:** for 1 and 2, kit slot 0's name before and after the saves; for 3 and 4, the entry the song
page opened at and what entry `[063]` of song 3 showed; for 5, any freeze, noise or changed sound,
and at which switch; for 6, whether the hat played again after `frq` went back, and how soon; for 7,
whether Drum 1 kept its tone at `slp` 127 and at `slp` 0 with `dec` 127; for 8 (or SKIPPED), what
Drum 1 played with the values 6 and 127.

## X4: USB MIDI out

Not in the spec's list: 1.7 defects of the USB MIDI out that LICHOMAT fixes (tracker E2.8).

Setup:
- The LXR is connected to the computer by USB.
- MIDI Monitor shows the LXR's USB port, `Sonic Potions USB MIDI`, with time stamps.
- A DAW records MIDI from that port.
- Every voice sends on its own MIDI channel: voice n on channel n, as in H8.

1. Internal sync, BPM 120, a pattern with all tracks 16 steps long and voice 1 on steps 1, 5, 9 and
   13 only. Leave the LXR stopped for 10 s, then press START, wait 30 s, and press STOP.
2. A long stream that wraps the transmit buffer many times: all 7 voices on all 16 steps, and on
   voice 1 every step with flam `x8`; BPM 180. Record the LXR's USB MIDI in the DAW for 16 bars
   (about 8700 messages), and during bars 5-12 save the pattern and then the kit to the card, as in
   H7 step 5. Then STOP.
3. System common and SysEx passed from DIN to USB:
   - Set the routing (`mrt`, second screen of the global menu's first sub-page) to `M2U`.
   - With the LXR stopped, send from the computer, through the MIDI interface, to the LXR's DIN
     MIDI in: a Song Position Pointer of 32 (`F2 20 00`), a Song Select 5 (`F3 05`), a Tune
     Request (`F6`), an MTC quarter frame (`F1 13`), and the SysEx
     `F0 7D 01 02 03 04 05 06 07 08 09 F7`.
   - With SendMIDI, for example:
     `sendmidi dev "<name of the MIDI interface>" spp 32 ss 5 tun tc 1 3 hex syx 7D 01 02 03 04 05 06 07 08 09`.
   - A DAW can send the SPP (locate while stopped) to the interface as well, with its MTC output
     off. Send the MTC quarter frame with SendMIDI only: a DAW sends a stream of them, and one that
     starts at 00:00:00:00 starts the LXR (its MTC start, as in 1.7).
4. Play the pattern of step 1. Pull the USB cable, wait 10 s, plug it in again, and select the
   port in MIDI Monitor again.
5. Play the pattern of step 1. Quit MIDI Monitor and the DAW, so that no program uses the LXR's
   port, wait 10 s, and start MIDI Monitor again.
6. A Tune Request after a Note On, passed on from DIN (N20). Routing `M2U` as in 3, the LXR
   stopped. Send to the LXR's DIN MIDI in, on a channel that no voice and not the global MIDI
   channel uses (16 here), a Note On, a Tune Request and the Note Off:
   `sendmidi dev "<name of the MIDI interface>" ch 16 on 60 100 tun off 60 0`.

**Expected:**
- 1: while stopped, only Clock (F8) messages, 20.8 ms apart. START gives Start (FA), then a Note
  On and its Note Off on channel 1 every 500 ms between the clocks. STOP gives Stop (FC). No
  message of any other type appears at any time. 1.7 sent 128 bytes of its memory every 32
  messages; a host drops most of them, but any it delivers show up here as stray messages.
- 2: in the DAW, channel 1 has 8 evenly spaced short notes (about 3.5 ms long) on every step, 128
  per bar, and channels 2-7 one note per step, 16 per bar. Zoom to one bar at a time and look
  through all 16: no note is missing, and none is long (stuck). This holds through the saves as
  well.
- 3: MIDI Monitor shows each of the five once, with exactly the bytes sent, between the LXR's own
  clocks. With 1.7 the SPP, the Song Select and the quarter frame arrived without their data, and
  the SysEx garbled.
- 4: the LXR plays on while the cable is out. After plugging in, the clock and the notes come back
  at their normal rate, with no group of old messages sharing one time stamp.
- 5: when MIDI Monitor runs again, at most 15 old messages share its first time stamp (the
  transfer that was under way when the computer stopped reading), then the clock and the notes go
  on at their normal rate. If the computer kept reading the port while no program used it, there
  are no old messages at all.
- 6: MIDI Monitor shows the Note On (channel 16, note 60, velocity 100), then the Tune Request,
  `F6`, then the Note Off. fw 1.7 shows the same: it sent every system message to USB as a one-byte
  packet (CIN 0xF), so the data bytes it put after F6 never reached the computer. This step only
  shows that the Tune Request still arrives. The rest of N20's change (F4, F5 and F6 passed on
  alone, and stray data bytes after them) has no hardware check: on DIN a receiver ignores data
  bytes after F6, as MIDI 1.0 asks, and a computer cannot send stray data bytes to the LXR (USB MIDI
  carries whole messages; on macOS, SendMIDI's `raw` sends a series of several messages as one
  empty packet).

**Record:** for 1, any message of another type; for 2, any missing or long note (bar and channel),
and the bars in which the saves ran; for 3, what MIDI Monitor showed for each message; for 4 and 5,
what came first, and how many messages shared the first time stamp; for 6, what MIDI Monitor showed.

## Optional: clock jacks

Not in the spec's list, but stage 1 changed how the jacks are driven (tracker E2.5).

- **X1, clock out.** Internal sync, shuffle 127, clock out PPQ 4 and then 24. On a scope or a
  clocked module the pulses are evenly spaced (at 120 BPM: 125 ms at 4 PPQ, 20.8 ms at 24 PPQ),
  shuffle or not, and only while running. Then clock out PPQ 2, in internal sync and with BPM `MID`
  following the DAW's MIDI clock as in H6. Both give one pulse every second step (250 ms at 120
  BPM); fw 1.7 gave 4 PPQ in the slave modes. Record the clock out on a second DAW input together
  with the hats of H6's pattern: each pulse comes with the hat of an odd step (1, 3, 5 …), also
  after Stop and Continue on the master (H6 steps 4 and 5).
- **X2, clock in.** BPM set to `EXT`, clock in PPQ 4, a 4 PPQ clock (16ths) into the clock in jack:
  the LXR follows within one pulse and plays every step. Again with 24 PPQ. At 4 PPQ the LXR plays
  each step when its pulse arrives or when its measured tempo says the pulse is due, whichever comes
  first, so a late pulse no longer delays its step (owner question Q11; fw 1.7 played each step at
  its pulse). So a swung 4 PPQ clock plays straight, and when the clock stops without a reset, one
  more step plays than pulses arrived.
- **X3, reset input.** In `EXT` mode, while a high gate is at the reset input the LXR stays stopped at
  the start; when the gate goes low it starts with the next clock pulse (the reset input works as a
  run gate, as in 1.7). Then quick edges: with the clock running, drive the reset input for a second
  with a fast square wave or a burst of short triggers (100 Hz or faster), and end the burst with
  the gate held high; then end one with the gate held low. Do each 10 times. Every time, once the
  edges stop, the LXR is stopped while the gate stays high and runs from step 1 while it stays
  low: no edge is lost (`ac08e38`; fw 1.7 could lose one and stay stopped, or running, against the
  gate's level).

**Record:** what you saw for X1–X3: for X1 at 2 PPQ, the pulse spacing in internal sync and in
`MID`, and whether the pulses stayed on the odd steps after the Continue; for X2 at 4 PPQ, whether a
swung clock played straight and how many steps played after the clock stopped; for X3, how many of
the quick-edge tries ended in the wrong state.

## Run sheet

Update files under test (filled in when the files were built):

| File | SHA-256 | Built from | Where |
|---|---|---|---|
| `LXR_V2_LICHOMAT_0.1.img` | `55091864fff78c641a681636f056449caa1fcb3cb68d09baa5cd3e9e6759a262` | `timing-core` at `3527cbc`, built by CI (`.github/workflows/build.yml`; later commits that change only documents build the same file) | the artifact `lichomat-firmware-3527cbc935eac5d629f08c1970336a45e4a5606f` of CI run 40435442579 (300780 B) |
| `LXR_V2_V1.7.img` | `cdb1ca371599149083bd0b61564d6864f031b599ccdcead2047a711e0b679db4` | `a43cd4f`, the official 1.7 sources | `build/official-1.7/src/build/release/` |

The LICHOMAT file is the one CI builds, the file the release attaches (spec §14). Download it with
`gh run download 40435442579 --repo isthar/lxr-desktop-app -n lichomat-firmware-3527cbc935eac5d629f08c1970336a45e4a5606f`
or from the run's page on GitHub (Actions); GitHub keeps the artifact until 2027-01-25, after that
run the workflow again. A local build in `build/release/` is a different file, `0f8621a3…`: the
macOS toolchain links newlib's members in another order. Do not use it for these checks. The earlier
file `9a7aff2a…` (CI run 40416804934) lacks the fixes of U3: do not test it any more.

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
| U2 | | | |
| U3 | | | |
| X1 | | | |
| X2 | | | |
| X3 | | | |
| X4 | | | |
