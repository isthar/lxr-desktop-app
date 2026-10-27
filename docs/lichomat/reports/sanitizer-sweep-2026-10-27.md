# LICHOMAT v0.1 — Sanitizer sweep report

## Status after E4.6–E4.9 (2026-10-27, `3527cbc`)

*Added when the report was copied into the repository. Everything below this section is the sweep's
report as it was written in the night, with `8af1ece` as HEAD (before E4.7–E4.9); this section
supersedes its fixed and open remarks.*

**Reproduction paths.** The harnesses, scenarios and build directories named in sections 2 and 3
(`/private/tmp/claude-501/…/scratchpad/sweep/…`, and paths relative to it such as `build/ub/sweep`,
`build3/midi_fuzz`, `scen/s_saveproj.c` or `ui/obj/emu_ui`) were in the controller's scratch
directory of the night's session. They are not in the repository. For the fixed bugs the host tests
named below replace them.

**fw 1.7.** `ResonantFilter.c`, `Decay.c`, `Oscillator.c` and `Delay.c` were unchanged from fw 1.7
(`a43cd4f`) before the fixes, and the official build uses the same compiler options, `-ffast-math`
included, so B2 and B8–B10 (*not checked* in the tables) are fw 1.7 bugs too; so is B4 (its commit
names the same unchecked read in fw 1.7).

| ID | Severity | Status | Commit and host test, or why it stays open |
|---|---|---|---|
| B1 | corrupts-data | fixed | `2446532` (the card's kit slot bytes: pattern files, `tmp.pat`, `tmp.snd`; `preset_loadDrumsetRam()` and `preset_saveDrumsetRam()` ignore a kit past 63) and `a6bd8b7` (`frontPanel_ccHandler()` ignores a Bank Select of 64–127); `test_presets` |
| B2 | corrupts-data | fixed | `2bb5d80` (the ping-pong pointers are masked to their half once per block); `test_delay` |
| B3 | corrupts-data | fixed | `040cdea` (a local 64-byte path; `empty_directory()` takes its buffer size); `test_presets` |
| B4 | corrupts-data | fixed | `b0b53ec` (`PROJ.IDX` past 63 → project 0; `preset_loadProject()` and `preset_saveProject()` refuse a project past 63); `test_presets` |
| B5 | corrupts-data | fixed | `0712d9a` (the song cursor goes to entry 0 for a pointer past the song); `test_sequencer` |
| B6 | corrupts-data | fixed | `8af1ece` (`buttonHandler_armTimerActionStep()` ignores a step past 63, `seq_armAutomationStep()` disarms); `test_buttons`, `test_sequencer`. The `*8 + part` encoding is unchanged: see the open item below the table |
| B7 | corrupts-data | fixed | `5b2c224` (`frontParser_parseNrpn()` ignores `PAR_ACTIVE_STEP` ≥ 64, `midiParser_noteOn()` writes no note for such a step); `test_midicc`, `test_presets` |
| B8 | wrong-behaviour | fixed | `eb9712a` (the guard tests the stored state's exponent bits through a volatile read); `test_dsp`, `tools/fw_checks.sh E4.9`. While a runaway setting is held the filter still sounds broken (a buzz or drop-outs, where fw 1.7 was silent); after it is turned back the voice sounds again within about 8 audio blocks |
| B9 | wrong-behaviour | fixed | `6a74b81` (the amount is limited to 0.99, so slope 127 gives a very steep curve; slope 0 returns 0, its flat curve, instead of the proposed −0.99 limit, which would bend it); `test_dsp` |
| B10 | wrong-behaviour | fixed | `9353a68` (both default cases clear the buffer: a waveform of 6 or more is silent; 0–5 unchanged); `test_dsp` |
| B11 | wrong-behaviour | open | Needs a blank or foreign card, reads flash only, no fault: a known limitation in the release notes, for a later version |
| B12 | wrong-behaviour | open | A wrong label only, no sound change, nothing stored: a known limitation in the release notes |
| B13 | wrong-behaviour | open | Part of fw 1.7's PWM sound; the fix changes it. Goes with the owner's listening check (Q13, with `Oscillator.c:471`/`:175`, lines 464 and 174 before `9353a68`), v0.2 at the earliest |
| B14 | wrong-behaviour | open | As B13: the PWM table read goes with the same listening check (Q13) |
| B15 | harmless-on-device | open | VCVT saturates to the intended value on the device; only x86-64 hosts differ. For a clean UBSan baseline in stage 1b |
| B16 | harmless-on-device | open | The read is multiplied by 0. For a clean UBSan baseline later |
| B17 | harmless-on-device | open | The write lands in fill bytes; needs malformed USB-MIDI packets. For a clean UBSan baseline later |
| B18 | harmless-on-device | open | The values read are neither shown, saved nor sent. For a clean UBSan baseline later |
| B19 | host-only | open | Device and arm64 hosts agree; on the host, stage 1b's `-fno-strict-float-cast-overflow` (spec §8.2) gives the device's saturation at these sites. Spec F6 makes only the 8 FM sites and `freq2PhaseIncr()` defined C, not `freqToMidiNote()`, `freq2PhaseIncr1024()` or `freq2PhaseIncr32767()` |

Also open, not filed as bugs above:
- SHIFT + a held select button with one of steps 1–8 selected still arms step `selectedStep * 8 +
  part`, an in-range step of the same track, mostly another one (the fw 1.x sub-step leftover of
  B6). A real
  fix changes behaviour; a known limitation in the release notes.
- The off-by-one guard at `menu.c:2590` (`> MAX_SONG_LENGTH`, meant `>=`): unreachable since
  `0712d9a`, which keeps the song cursor inside the song.

The night's fixes added the host tests `test_sequencer`, `test_presets`, `test_delay` and `test_dsp`
and more cases in `test_buttons` and `test_midicc` (32 ctest ids). The release file with these fixes
is the artifact of CI run 40435442579 (`55091864…`); the hardware checklist's U3 covers the fixes a
user can hear or see. The plan's execution amendment 27 lists the commits.

---

*LXR-02 custom firmware (STM32F765, Cortex-M7). Branch `timing-core`. Written 2026-10-27, the night before the v0.1 hardware checks. For the owner to triage before the release.*

## What this is

The emulator probe from stage 1b runs 68 firmware files on the Mac with the real interrupt bodies, the boot and main loop, a RAM-disk SD card and the real FatFs. Tonight's seed was a fw-1.7 bug surfaced by UBSan: `midiParser_ccHandler()` wrote one byte 64 KB past a 255-byte table into pattern memory on every kit load and every MIDI Bank Select (now fixed, commit `2680991`, test `tests/test_midicc.c`). This sweep looked for more of the same kind across six areas: MIDI-in, presets, UI, sequencer, DSP and sync.

**28 findings were filed across the six areas; every one reproduced** (each area ran a second, independent verification pass). After removing cross-area duplicates (the same defect found by up to three areas), they are **19 unique bugs**.

### How to read the severities
- **corrupts-data** — an out-of-bounds write (or a read whose value is later written to RAM/card). Can change a sound, a stored file, or — in two cases — put the CPU into a BusFault/HardFault and freeze the unit.
- **wrong-behaviour** — wrong sound or display, no memory damage. Recovers on its own, or on a value change / reboot.
- **harmless-on-device** — real UB on the host, but the device's linker layout, VCVT saturation or `*0` arithmetic makes it a no-op on hardware. Worth fixing for a clean CI/UBSan baseline.
- **host-only** — only the macOS/x86-64 host diverges; arm64 and the device agree. Matters for emulator golden renders, nothing else.

### Device facts used for the verdicts
STM32F765 maps ITCM at address 0 (a NULL read returns code bytes, no trap), integer `/0` returns 0, float→int saturates (VCVT/FCVTZU), unaligned normal loads are allowed. So an out-of-bounds **read** returns a neighbour's bytes (named per bug), and an out-of-bounds **write** corrupts whatever the linker put there (victim symbol named per bug). Addresses come from `/Users/tk/Code/lxr/build/release/LXR_V2_STM32F765.elf` and its map; every area confirmed the ELF has no Core/Drivers layout change since the sweep base, so the addresses hold for the CI release too. (The on-disk ELF was built one minute before `2680991`; CI rebuilds v0.1 from current HEAD.)

### IMPORTANT — two corrupts-data bugs are already fixed on current HEAD
The sweeps ran against `da64603`/`17d9199`. Current HEAD is **`8af1ece`**, which landed two fixes since:
- **`0712d9a`** fixes the song-position-255 write (B5).
- **`8af1ece`** fixes the SHIFT+select automation-step write (B6).

Both were re-verified fixed on `8af1ece`. They are listed below for completeness and regression tests, but the owner does **not** need to act on them. **Five corrupts-data bugs remain open.**

---

## 1. Ranked bug table

### corrupts-data (7: 5 open, 2 already fixed)

| ID | Bug & location | What the user does | On the device | fw 1.7? | Smallest fix |
|----|----------------|--------------------|---------------|---------|--------------|
| **B1** | **Unbounded kit slot → OOB read+write of `mainParameters[64][247]`.** `presetManager.c:844` (load) / `:894` (save); entry via `frontPanelParser.c:112` (MIDI), `presetManager.c:1964`/`451` (.PAT `kitNr`), `:259` (tmp.snd). | **MIDI Bank Select MSB (CC 0) of 64–127** on the global channel (default CC filter on) — a single message any DAW/controller can send. Also a corrupt/foreign card whose pattern `kitNr` or tmp.snd index ≥ 64, with kit-autoload (default) on. | Reads row v≥64 (247 B) into `activeParameters` → junk sound. `menu_selectedPresetNr[0]` stays ≥64, so the **next kit/project save writes 247 B out of bounds** over `savefileNames`, `parameterArray` pointers (→ later wild writes / BusFault when modulated), the MIDI/UART FIFOs, or sequencer core state, and stores it to card. The bad slot survives a reboot via tmp.snd (`preset_LoadTmpKit` doesn't bound it). | **yes** (`e6cfbe9`) | Guard the top of `preset_loadDrumsetRam()`/`preset_saveDrumsetRam()`: `if(presetNr>=NUM_KIT_SLOTS) return;` (covers MIDI **and** card). Also clamp the tmp.snd byte (`:259`) and the MIDI path in `frontPanel_ccHandler` before `menu_selectedPresetNr[0]` is set. |
| **B2** | **Ping-pong delay writes one `__fp16` up to ~94 KB past `FX_BUFFER`.** `Delay.c:152` (write), `:55`/`:63`/`:64` (reads); cause `MidiParser.c:1433-1434` sets `delayType` but leaves `write`/`oldRead`/`newRead` in Normal-mode range. | With FX = Delay running, **switch delay type Normal→Ping-pong** on the FX page (or load a kit with ping-pong over one with normal, or select FX Delay after the type was changed under another FX). ~50 % of switches hit (whenever the write pointer ≥ 16385; it sweeps the buffer every 0.74 s). | One 2-byte store at `0x20011E16+2w`. Victims by w: DTCM stack (mostly dead), SRAM1 `.data/.bss` incl. **FatFs `SDFile`/`SDFatFS` (card corruption on next write)**, `mainParameters`/`activeParameters` (bad kit saved), `parameterArray`, USB/I2S/SD handles; **6 positions hit the delay pointers → a high-halfword value sends the next access into uninitialised FMC/QSPI → BusFault/HardFault `while(1)` → the unit freezes until power-off.** | not checked (upstream `Delay.c`) | Mask once per block at the top of `Delay_processPingPong` (`Delay.c:94`): `d->write &= FX_BUFFER_SIZE>>1; d->oldRead &= …; d->newRead &= …;`. |
| **B3** | **Project re-save overruns the 13-byte `currentFolder` into `savefileNames`.** `empty_directory()` `presetManager.c:715`, called with `currentFolder` at `:2351`. | **Save a project a second time** (or over a factory project) — the folder-exists branch wipes it first. Fires on normal workflow: after any project load/boot, unnamed kit/song slots are named so files are ≥12 chars (`NN-EMPTY.SND`, `00-INITK.SND`). | `/projNN/`+12-char name+NUL writes indices 13–20 → `savefileNames[0][KIT_NAMES][0..4]`. Kit 0's name becomes e.g. `.SND`/`.SNG`; kit 0 is rewritten on card as `00-_SND.SND`; the bad name persists in RAM and in the project's kit-0 file. Sound data untouched, no crash. (tmp.snd has no name, so the autosave is unaffected.) | **yes** (`e6cfbe9`) | Don't hand the 13-byte global to `empty_directory()` as scratch. Use a local `char path[64]` (needs ≥34 for one user subfolder; 24 covers files only), as `preset_clearTmpFolder` does with `delete_node(buff, sizeof buff, …)`. |
| **B4** | **Unbounded last-used-project byte from `PROJ.IDX` → 8-byte OOB write of `savefileNames`.** `presetManager.c:1083`/`1094` (read, no check) → boot sets `menu_selectedPresetNr[PROJECT]`; writes at `menu.c:730`, `presetManager.c:2403`; read at `:2770`. | **Boot from a corrupt/foreign card** whose `/PROJ.IDX` byte ≥ 64 (bypasses the encoder's 0–63 cap), then **press Save on the project page**. | Writes 8 bytes at `savefileNames + 32*k`. Many victim rows are live: k=67 overwrites all 8 `midi_MidiChannels`; k=76–137 overwrites a whole `parameterArray{ptr,type}` entry → **loading that project and aiming an LFO/velocity at it dereferences a bogus pointer in the reserved FMC range → BusFault/HardFault, unit locks up**, and it persists in `prj.nfo`. Also a stray `/proj255` folder. | not checked (upstream preset code) | Clamp after reading `PROJ.IDX` (`:1094 if(*projNr>=NUM_KIT_SLOTS) *projNr=0;`) and guard `preset_loadProject()`/`preset_saveProject()` with `if(presetNr>=NUM_KIT_SLOTS) return FR_INVALID_PARAMETER;`. |
| **B5** | **Song page at position 255 after a stop → OOB write past the active song.** `sequencer.c:878`; cause `:702` copies `seq_songPositionPointer` (0xFF after a rewind `:2083` / song-mode-on `:872`) into `menu_songMenuPosition` unchecked. *(Found by UI, sequencer and sync.)* | **Play, Stop, Play** (broader than first reported: any stop rewinds; the first step after a rewind counts as a pattern end, so no pattern-select is needed). Open the song page (SHIFT+step 10 — shows `[255]`) and press a step to add a pattern, or turn the encoder in edit mode. | `seq_activeSong[255]`: for song slot k≤60 it writes `seq_songData[k+3][63]` (another song's last row, stored with the project); k=61–63 writes into `patternBackupBuffer` (a step's volume/prob/note, copied into the live pattern on the next pattern change). | **yes** | **Already fixed on HEAD (`0712d9a`):** clamp at `sequencer.c:705` `menu_songMenuPosition = (seq_songPositionPointer<MAX_SONG_LENGTH)? seq_songPositionPointer : 0;`. (`:620` is dead code.) |
| **B6** | **SHIFT + held select button arms automation step `selectedStep*8+part` (up to 127).** `buttonHandler.c:1422` (fw-1.x 8-substep leftover); reads `buttonHandler.c:107/118`; write `sequencer.c:1224-1234` via `seq_armAutomationStep()` (no range check). *(Found by UI.)* | In voice mode press a step 9–16 (sets `selectedStep` 8–15), **hold SHIFT + a select button >0.5 s and turn a knob.** | Writes `param1/param2` of step (n−64) on **track+1** (voice 7 → next pattern; pattern 64 → `seq_stepTiming` of pattern 1). The lock plays on that other step and is saved. Stays inside `seq_patternSet` (no fault). | **yes** | **Already fixed on HEAD (`8af1ece`):** both range checks added — drop the request when `stepNr>=NUM_STEPS` in `buttonHandler_armTimerActionStep()` and `seq_armAutomationStep()`; fix the `*8+part` encoding for 64 steps. |
| **B7** | **Note On on the SEQ page after NRPN 121 set the active step to 64–127.** `MidiParser.c:1492` (`seq_subStepPattern[..][step].note`, step from `PAR_ACTIVE_STEP`); set by `frontParser_parseNrpn()` `frontPanelParser.c:51`. | On any configured channel send **NRPN 121 = 64–127** (CC99=0, CC98=121, CC6=64–127), then a **Note On** while the step (SEQ) page is shown. The panel can only set step 0–63, so MIDI is the only way in. | The note lands in step (s−64) of the next track; from track 7 → track 1 of the next pattern; worst case the flam/shift nibbles of pattern 1. **Never leaves `seq_patternSet`** — a wrong pitch/flam/shift on that step, stored on the next pattern save or tmp.pat autosave. | **yes** (`e6cfbe9`) | `if(step>=NUM_STEPS) return;` before the write in `midiParser_noteOn()`, **and** reject `PAR_ACTIVE_STEP>=NUM_STEPS` in `frontParser_parseNrpn()` (the same NRPN also indexes automation edits via `CortexParser.c:873/501-534/545`). |

### wrong-behaviour (7, all open)

| ID | Bug & location | What the user does | On the device | fw 1.7? | Smallest fix |
|----|----------------|--------------------|---------------|---------|--------------|
| **B8** | **Snare/cymbal/hi-hat filter latches NaN → voice silent until power-off.** `ResonantFilter.c` `SVF_calcBlockZDF` (state `:276`/`:282`, outputs `:299`/`:313`); the `removeMaliciousSample` guard (`:130`) is folded to a plain `return` by `-ffinite-math-only` on the device. | On the hi-hat/cymbal page set **filter type HP, FREQ ≈122–126, RESO ≈80–127, DRIVE 127** (all inside the panel's 0–127), and play. Modulation that reaches those values does the same. | The filter state goes NaN and never clears (`SVF_init` runs only at boot). Every later sample → 0: the voice is **silent in LP/HP/BP/UBP/Nch/Pek until reboot**; kit loads don't clear it. Snare keeps tone+transient, loses noise. Drum 1-3 recover on their next trigger. No memory damage. | not checked (device guard disabled by `-ffast-math`) | Make the guard survive fast-math by testing the exponent bits, as the commented `ResonantFilter.c:140` does: `uint32_t u; memcpy(&u,&in,4); if((u&0x7F800000)==0x7F800000) return 0.f;` (also catches Inf). |
| **B9** | **Pitch EG slope 127 (or slope 0 + pitch decay 127) → NaN pitch, oscillator stops.** `Decay.c:77` (`2*amount/(1-amount)` = +inf at 127) and `:23`; → `Oscillator.c:24`/`35` phaseInc 0. | On a **Drum 1-3 or snare page turn the pitch EG slope to 127** (or MIDI CC 79-82 = 127, or an LFO/velocity mod reaching the top; or slope 0 with pitch-EG decay 127). | The Drum 1-3 / snare oscillator gets phase increment 0 and FM goes too: only transient+noise remain (snare keeps its noise). **Recovers the moment the value leaves 127.** No memory write. | not checked (upstream `Decay.c`) | Clamp `amount` before the divide in `DecayEg_setSlope` (`Decay.c:76`): `if(amount>0.99f)amount=0.99f; else if(amount<-0.99f)amount=-0.99f;` (changes the sound only at the extremes). |
| **B10** | **Oscillator waveform value ≥6 → the voice plays a stale stack buffer.** `Oscillator.c` default cases (`calcNextOscSampleBlock` ~`:375-410`, FM `calcUserSampleOscFmBlock` `#if 0`) never write `buf`; CC sites `MidiParser.c:362/375/388/401/414/427/440/659` store `value.itg` unclamped. | **Send a MIDI CC for an oscillator or mod waveform with value ≥6** (e.g. a DAW lane sweeping 0-127), or load a kit holding such a value. The panel stops at 6 entries. | The voice processes whatever its shared stack buffer `sampleData[]`/`modBuf[]` holds — often the previous voice's finished block through this voice's filter/envelope: **crosstalk or noise up to full scale**. Reads only. | not checked (upstream `Oscillator.c`) | `memset(buf,0,size*sizeof(int16_t));` in both default cases (unknown waveform = silence), or clamp `value.itg` in the CC handler as the modulator path already does. |
| **B11** | **LFO target DST while the LFO's voice is still 0 reads `modTargetVoiceOffsets[255]`.** `menu.c:4258-4266` (knob) and `:2991-2998` (encoder): `voiceNr = activeParameters[PAR_VOICE_LFO1+n]-1` = 255 when the param is 0. | **Boot from a blank/foreign card** (no `/proj00/prj.nfo` → boot's `preset_loadProject()` fails, kit never initialised, `PAR_VOICE_LFOn` stay 0), then on the LFO sub-page **turn knob 4 (DST) or edit DST with the encoder.** Touching `voi` first (clamps 1–6) cures it. | Read from flash. DST (and `CC_LFO_TARGET`) can only toggle **off ↔ voice 2's LFO retrigger** instead of the real target list; the chosen value is stored in the kit. No fault, no sound-engine crash. | **yes** | Clamp at both sites (`menu.c:2991`, `4258`): `if(voiceNr>5) voiceNr=0;`, or init `PAR_VOICE_LFO1..6` to 1–6 when the boot project load fails (as `presetManager.c:1278` does on a normal load). |
| **B12** | **Out-of-range roll rate from NRPN 119 reads past `rollRateNames` on the PERF page.** `menu.c:2729` `getMenuItemNameForValue` (`rollRateNames[curParmVal+1]`, no bound; table is `[15][4]`). | With the **PERF page shown, send NRPN 119 = 14–127** (CC99=0, CC98=119, CC6=14–127). PAR_ROLL = parameter 247. | OOB read inside `.rodata` (no fault). Only the 3-char roll label on the PERF page is wrong (shows wave names, transient names, `-7..+7`, or CGRAM glyphs). No sound change, nothing stored. The wrong label persists and reappears when PERF is opened later. | **yes** (`e6cfbe9`) | Bound the index in `getMenuItemNameForValue()` (`if(curParmVal+1>=ARRAY_LEN) curParmVal=0;`) and the sibling name tables, or reject out-of-range in `frontParser_parseNrpn()` by dtype. |
| **B13** | **PWM oscillator: the second phase is computed in float and saturates instead of wrapping.** `Oscillator.c:665` (`calcPwmBlock`) and `:194` (`calcPwmFmBlock`): `phase + pwm*(UINT32_MAX/2)`. | Play **any voice with waveform PWM and nonzero PWM amount** — every cycle. | In the last `pwm/2` of each cycle `phase2` pins at `0xFFFFFFFF`, so the second saw holds ~`sawTable[row][1024]`; the output follows the first saw's ramp instead of a pulse (up to ~29-32k LSB off at high pwm). Deterministic, same on every unit. | **yes** (it is fw-1.7's PWM timbre) | Add the offset as an integer so it wraps: `phase + (uint32_t)(pwm*(UINT32_MAX/2))` at both sites. **Changes the PWM sound → the owner should listen before shipping** (same class as the Q13 `Oscillator.c:464` decision). |
| **B14** | **PWM oscillator reads `sawTable[row][1024]` — four more sites of the known `:464`/`:174` read.** `Oscillator.c:677`/`683` (`calcPwmBlock`), `:204`/`:210` (`calcPwmFmBlock`). | Play **any PWM voice** — once per cycle at the wrap. | Read-only. Bit-identical to the intended wrap **below 14,080 Hz** (rows 0-9 read 0 = the intended value). Only **row 10 (osc ≥14,080 Hz — very high notes or pitch-EG peaks)** reads `6192`, giving a −22 to −23.5 dBFS error for `pwm/2` of each cycle. | yes (same class as the known table read) | Use the wrap planned for `:464`/`:174` at all four sites: `[(itg+1)&1023]`. Bit-identical below 14,080 Hz → no listening check needed there. |

### harmless-on-device (4)

| ID | Bug & location | What the user does | On the device | fw 1.7? | Smallest fix |
|----|----------------|--------------------|---------------|---------|--------------|
| **B15** | **LFO phase offset at value 127 overflows float→uint32.** `MidiParser.c:1303/1306/1309/1312` (`value.flt * 0xffffffff`; `1.0f*4294967295` rounds to `2^32`). *(Found by MIDI-in, UI, sequencer; the dsp area also files the wider cast family, B18.)* | Set an **LFO phase offset to 127** (knob/encoder `ofs`, NRPN 57–62, or a kit/p-lock holding it). | VCVT saturates to `0xFFFFFFFF` — exactly the intended maximum. Arm64 hosts agree. **Only x86-64 hosts give 0** → breaks emulator golden bit-exactness. No device effect. | **yes** | Multiply by a representable constant: `value.flt * 4294967040.0f` (largest float < 2³²) at the four sites. |
| **B16** | **RAT distortion reads `table_1n914[257]` on a +1.0 clip.** `Distortion.c:143` `diode()`. *(Found by MIDI-in and DSP.)* | Set **master FX = Distortion, type = RAT (≥2), drive into clipping** (drive 30 already clips the init kit). | Reads `0x20009084` (`_eccm`, one word past the 257-entry table). `frac` is exactly 0 at the clamp, so the word is multiplied by 0 → result is `table[256]`, harmless. **Residual risk:** if that uninitialised word is an Inf/NaN pattern, the RAT wet signal goes NaN → silent until reboot (the word holds power-on SRAM / bootloader leftovers, not measurable here). | yes (upstream DSP) | After `uint16_t itg=(int16_t)index;` add `if(itg>255)itg=255;` (then `frac`=1, result still `table[256]`). |
| **B17** | **USB-MIDI data bytes not masked to 7 bits → CC number 0xFF writes one byte past `midiParser_originalCcValues[255]`.** `MidiParser.c:171`; unmasked copy at `usbd_cdc.c:955-956`. | A **USB host sends a malformed USB-MIDI packet** with a data byte ≥0x80 (e.g. `0B B0 FF xx`). DIN cannot produce this. | 1-byte write to a `*fill* 0x1` byte in the release link → **nothing corrupted**. CC 128-254 stay in bounds (overwrite CC2 restore values); data2 128-255 saturate via VCVT. Needs packets a class-compliant host never sends. | **yes** | In `midiParser_parseMidiMessage()` channel branch: `if((msg.data1|msg.data2)&0x80) return;`, or mask `data1`/`data2` with `0x7F` in `USBD_CDC_DataOut()` for CINs 0x8-0xE. |
| **B18** | **Pattern-select passes the menu page as the voice → `euklid_length/steps/rotation[7..21]` read OOB.** `buttonHandler.c:1196-1197` (`menu_activePage` instead of the commented `menu_getActiveVoice()`). *(Found by UI and sequencer.)* | **The normal way to change patterns:** SHIFT+step 16, then press a step to pick a pattern (page 16). Also the pattern-save success page (page 18). | Reads `seqc_sl`/`euklid_length[0]` bytes in SRAM (no fault). The values go into `activeParameters[253..255]`, which aren't saved/sent and aren't shown on the pattern page; the only path onto the Euclid page re-requests the active voice first. **No audible/visible/stored effect.** | **yes** (`e6cfbe9`) | Send `menu_getActiveVoice()` instead of `menu_activePage` at `buttonHandler.c:1196-1197`, as the inline comment intends. |

### host-only (1)

| ID | Bug & location | What the user does | On the device | fw 1.7? | Smallest fix |
|----|----------------|--------------------|---------------|---------|--------------|
| **B19** | **More float→uint32 conversions out of range: `freqToMidiNote`, `freq2PhaseIncr1024/32767`** (and the corrupt-kit float-cast class, ~30 sites, filed by the presets area). `Oscillator.c:35`/`47`/`52`; corrupt-kit sites across `mixer.c`, `lfo.c`, `Snare.c`, `DrumVoice.c`, `CymbalVoice.c`, `transientGenerator.c`, `ResonantFilter.c`. | High notes combined with pitch EG/FM; or a corrupt/foreign kit/pattern supplying out-of-range parameter bytes (the normal path clamps, the DSP then overflows a cast). | Deterministic VCVT/FCVTZU saturation — **identical to fw 1.7 and to arm64 hosts**; NaN→0, negatives→0, ≥2³²→max. A corrupt kit just sounds wrong/loud. **Only x86-64 diverges** (wraps). No memory error, no trap. Table indices are separately clamped (phase `>>20/22`, row ≤10). | **yes** | Route these casts through the planned saturating float→uint32 helper; keep CI's UBSan baseline clean by excluding `float-cast-overflow` or clamping parameter bytes on load. |

---

## 2. Reproductions and proposed host tests (corrupts-data + wrong-behaviour)

All repros build on the stage-1b probes. The MIDI harness is `/private/tmp/claude-501/-Users-tk-Code-lxr/96b76bdc-1e78-46dc-a0e8-44d89ec424a5/scratchpad/sweep/midi-in/harness/midi_fuzz.c`; the preset scenarios are under `/private/tmp/claude-501/-Users-tk-Code-lxr/96b76bdc-1e78-46dc-a0e8-44d89ec424a5/scratchpad/sweep/presets/scen/`; the DSP harness is `/private/tmp/claude-501/-Users-tk-Code-lxr/96b76bdc-1e78-46dc-a0e8-44d89ec424a5/scratchpad/sweep/dsp/harness/sweep.c`; the UI/sequencer drivers are under `.../sweep/ui/` and `.../sweep/sequencer/`. A proposed test models the existing `tests/test_midicc.c`: drive the exact trigger, then assert the victim is untouched (the assertion fails — or the sanitizer traps — before the fix).

**B1 — unbounded kit slot.**
Repro: `build3/midi_fuzz din B0 00 41 dump` (CC0 = 0x41 = 65), USB `midi_fuzz usb 0B B0 00 7F`; card path `EMU_LAYOUT=roomy EMU_SD_IN=ref.img EMU_CASE=patkit_notmp EMU_K=255 ./s_slot` and `EMU_CASE=tmpkit`; MIDI→save `v_bank.c` (v=100).
Test: send CC0 data2=65, then drive the kit-save path (`menu_setLoadSaveCategory(SAVE_TYPE_KIT)`, `menu_switchPage(SAVE_PAGE)`, encoder confirm). Assert `menu_selectedPresetNr[0] < NUM_KIT_SLOTS` after the CC and that the save `memcpy` index is `< 64`. Pre-fix: UBSan reports `index 65 out of bounds for uint8_t[64][247]` at `presetManager.c:844` and again at `:894`.

**B2 — ping-pong delay.**
Repro: `build/ub/sweep pp 0` (switch at write=0x411c → `Delay.c:55`/`:152` OOB), `sweep pp 2` (SEGV at `:63`), plus `ppfx` (FX change) and `ppkit` (kit load).
Test: run the delay in Normal until the write pointer ≥ 16385, switch to Ping-pong, process one block. Assert every `FX_BUFFER[...]` index `< FX_BUFFER_SIZE`. Pre-fix: the `-fsanitize=bounds` build traps; `pp 2` SEGVs after the pointers are overwritten.

**B3 — empty_directory currentFolder overrun.**
Repro: `scen/s_saveproj.c` — `EMU_LAYOUT=guard ./s_saveproj` (BUS write at `presetManager.c:715`), `EMU_LAYOUT=device ./s_saveproj` (kit 0 name → `.SND`, file rewritten `00-_SND.SND`).
Test: format a card, save a project, reload, save again (folder now holds a ≥12-char kit file). Assert `savefileNames[0][KIT_NAMES]` is unchanged and the path written by `empty_directory` stays within `currentFolder`'s 13 bytes. Pre-fix: guard-page build faults on the second save; device-layout build shows the renamed kit.

**B4 — PROJ.IDX unbounded project slot.**
Repro: `scen/s_slot.c` — `EMU_CASE=projidx EMU_K=255` (`menu.c:730` write + fault), `EMU_CASE=projidx2 … EMU_SD_OUT=idx255.img` then `EMU_SD_IN=idx255.img EMU_CASE=reboot` (`presetManager.c:2403` then `:2770`).
Test: boot with `/PROJ.IDX` byte = 255, press Save on the project page. Assert the `savefileNames` write index `< 64` and no `/proj255` directory is created. Pre-fix: UBSan `index 255 for char[64][4][8]` at `menu.c:730` / `presetManager.c:2403` / `:2770`; a `v_paramarr2.c`-style follow-up (LFO aimed at the smashed `parameterArray` entry) SEGVs in `modNode_setDestination`.

**B5 — song position 255 (already fixed `0712d9a`; keep as a regression test).**
Repro: `ui/obj/emu_ui repro_song`, `sequencer/bin-ubsan/drv_song panel 1 0`, `sync/repro_song_ptr.sh` (scenarios 0x80/0x200, song slot 63).
Test: Play, Stop, Play; open the song page; assert `menu_songMenuPosition < MAX_SONG_LENGTH` and that `seq_addPatternToSong` writes only within `seq_activeSong[0..63]`. Passes on HEAD; fails on `17d9199` (ASan: WRITE 191 B after `seq_songData`).

**B6 — SHIFT+select automation step (already fixed `8af1ece`; keep as a regression test).**
Repro: `ui/obj/emu_ui repro_autom` (track 2 step 16 param1 255/0 → 9/11).
Test: voice 1, step 10, SHIFT+select 8 held 0.7 s, knob +20°; assert the armed step `< NUM_STEPS` and the `seq_subStepPattern` write is in the intended step. Passes on HEAD; fails on `17d9199` (UBSan `index 79 for Step[64]` at `buttonHandler.c:107/118`, `sequencer.c:1224/1227`).

**B7 — Note On on SEQ page after NRPN 121.**
Repro: `build3/midi_fuzz din B0 63 00 B0 62 79 B0 06 50 idle=20 page=11 dump 90 24 64 dump` (dump shows track 2 step 17 note 60 → 36).
Test: send NRPN 121 = 80, switch to the SEQ page, send Note On; assert the write index `< NUM_STEPS` (the note of the neighbouring track/step is unchanged). Pre-fix: UBSan `index 80 out of bounds for Step[64]` at `MidiParser.c:1492`.

**B8 — SVF NaN latch.**
Repro (device-like `-ffinite-math-only` build): `build_devnan.sh` then `NOTE=63 build/ubfm/sweep filtv 5 125 100 127 1` — hat rms 865.1 → 0.0, `s1=s2=nan`; `filtv 4` (cymbal) 1473.7 → 0.0. The strict build (`build/ub/sweep filtv 5 125 100 127 1`) recovers (863.4).
Test: on the hi-hat, set HP / FREQ 125 / RESO 100 / DRIVE 127, render two blocks, restore the init-kit filter, render again. Assert the post-restore RMS recovers to its pre-latch level. Pre-fix (device-like build): RMS stays 0.0 and `s1/s2` are NaN.

**B9 — pitch EG slope 127.**
Repro: `build/ub/sweep slope` (rms Drum 1 7111.8 → 15.4 at slope 127, pitchMod=nan, phaseInc=0), `sweep slope0 127`, `sweep slopeback` (126/127/126 → recovers).
Test: set `PAR_PITCH_SLOPE1 = 127`, trigger Drum 1, render; assert the oscillator phase increment ≠ 0 and the voice RMS stays in band. Pre-fix: UBSan `Decay.c:77:25 division by zero` and RMS collapses.

**B10 — waveform ≥6 stale stack.**
Repro: `build/ub/sweep uninit 0 6` vs `sweep uninit 0x7fff7fff 6` (rms 0.0 vs 8283.7 — the output depends on stack garbage); `crosstalk` (Drum 2 wave 6 adds energy only when Drum 1 plays); `uninitmod`.
Test: set an oscillator waveform to 6 via CC, render the same block with two different stack fills; assert the output is identical and silent. Pre-fix: the two fills give different, non-silent RMS.

**B11 — LFO DST voice 0.**
Repro: `ui/obj/emu_ui repro_lfo` (boot blank card → `PAR_VOICE_LFO1..6 = 0`; move DST → UBSan `index 255 for ModTargetVoiceOffset[6]` at `menu.c:4260/4264/4265` and `:2992/2996`).
Test: boot a blank FAT card, open an LFO sub-page, move DST by knob and by encoder; assert `voiceNr < 6` at `menu.c:2991`/`4258`. Pre-fix: UBSan reports index 255.

**B12 — roll rate label (PERF).**
Repro: `build3/midi_fuzz din page=10 B0 63 00 B0 62 77 B0 06 35 idle=50`.
Test: show the PERF page, send NRPN 119 = 53; assert the `rollRateNames` index is within `[0,15)`. Pre-fix: UBSan `menu.c:2729 index 54 out of bounds for const char[15][4]`.

**B13 — PWM float saturation (timbre; needs a listening decision).**
Repro: `build/ub/sweep pwm 0` / `pwm 1` (`Oscillator.c:665`/`:194` overflow), `sweep pwmwave 0.5/0.9/1.0` (109/198/220 of 441 samples per cycle diverge from the integer wrap, up to 32577 LSB).
Test: render one PWM cycle, compare the second-saw phase against an integer-wrap reference; assert within tolerance. Pre-fix: up to ~32k LSB divergence. **The fix changes the sound** — gate it behind a listening check, do not ship blind.

**B14 — PWM `sawTable[1024]`.**
Repro: `build/ub/sweep pwm 0` / `pwm 1` → `Oscillator.c:677/683/204/210` index 1024.
Test: play a PWM voice at ≥14,080 Hz (row 10) and assert the table read uses `[(itg+1)&1023]` (output matches the wrapped reference). Pre-fix: UBSan `index 1024 for const int16_t[1024]`; row 10 output differs by up to 6192 LSB.

---

## 3. What each area ran, for how long, and what it did not cover

**Shared method / environment note (applies to every area; relevant to CI):** ASan does not run out of the box on this macOS 26 — every ASan binary, even a 3-line program, deadlocks in its own init (`AsanInitInternal` / `FindDynamicShadowStart`; x86-64 under Rosetta dies with SIGILL). The UI and sync areas found the workaround: a ~30-line interposer in `DYLD_INSERT_LIBRARIES` (`sweep/sync/interpose.dylib`), loaded from inside the build script because SIP strips `DYLD_*` from the shell; **and `-fno-common` is required**, or Apple clang emits the firmware's uninitialised globals as common symbols with no redzones and ASan misses every global-array overflow. Areas without that workaround ran UBSan (`bounds`, `float-cast-overflow`, `float-divide-by-zero`) plus a guard-page / device-layout harness or explicit in-range index checks to catch pointer-based OOB.

- **MIDI-in** — Apple clang 17, UBSan + float-cast + a second `-fsanitize=bounds` build. ~19,000 s virtual (~5 h), ~500 M DIN bytes, ~19 M USB packets, ~30 min wall. Ran: every channel message on all 16 channels × all data values; CC, NRPN/RPN, Program Change, Bank Select, Song Select, SPP, MTC; SysEx edge cases (empty/5000-byte/unterminated/nested/realtime-inside); broken streams; 800k-byte bursts against the 256-byte FIFO; a USB sweep over all CINs/cables/status bytes; random DIN at 1× and 10× and random USB with page/track/run/record/routing/channel/filter varied. **Did not cover:** pointer-based overflows (no ASan) — so B1/B7's writes were judged from the ELF, not trapped; DSP casts/NaNs it saw were left to the DSP area; the known `Oscillator.c` table reads.

- **presets** — UBSan + float-cast + a guard-page/device-layout harness (`emu_layout.c`), real FatFs on a 64 MiB RAM disk. ~55 min wall, a 232-scenario fuzz campaign (10 parallel) over `GLO.CFG`/`PRJ.NFO`/`PROJ.IDX`/`.SND`/`.PAT`/`.SNG`/`tmp.snd`/`tmp.pat` (fixed values, xorshift mutation, truncation, V1/V2/V3 pattern versions, out-of-range slot/kit/project/index bytes) plus targeted double-save, autoload-on-pattern-change, song mode, morph, full/failing card. **Verified safe (zero preset-side OOB):** `glo.cfg`/`.sng`/song-data read paths and all length-truncation fuzzing (fixed-count reads into correctly-sized buffers, slot loops bounded 0–63). **Did not cover:** anything needing ASan.

- **UI** — full firmware (79 objects) with ASan+UBSan, `-fno-common`, register-level panel driving (74HC165 buttons, Gray-code encoder, dual-wiper ADC knobs, faders). 27 runs, ~55,000 s virtual, ~40 min wall. Ran: a systematic sweep of every voice/sub-page/knob/encoder/MODE/SHIFT+step; the Euclid page; the step editor; save/load for 9 categories; 16 random fuzz seeds of 5000–8000 actions. **Did not cover:** DSP casts (left to DSP). Also surfaced a morph-NULL during boot `adc_checkPots` the driver had to guard.

- **sequencer** — UBSan only (ASan unusable), real FatFs on a RAM disk, plus a byte-compare write detector on all 64 `seq_songData` rows and post-action index checks on every pattern/track/step/song index. 21 fuzz seeds, 11 MIDI/jack slave runs, 2 long runs (2 h pattern + 4 h song virtual), targeted panel/p-lock scenarios. ~110,000 s virtual (~31 h), ~1.2 M UI+MIDI actions, ~25 min wall. The index checks never fired except `menu_songMenuPosition` (B5); the core reported no queue overflow. **Did not cover:** pointer OOB except via the index checks. **Flagged but out of scope:** `euklid_calcRecursive` never terminates for Euclid length ≥18 (uint16 group overflow → stack overflow on the device) — not panel-reachable today (menu clamps `DTYPE_1B16` to 1–16, `PAR_EUKLID_*` isn't stored), but a latent hazard if any future path lets length exceed 16.

- **DSP** — UBSan (bounds + float-cast + float-divide-by-zero), a device-NaN build (`-ffinite-math-only`, to fold the guard as the device does), and an op-by-op transcription of the release ELF's `SVF_calcBlockZDF`, `DecayEg`, `osc_setFreq`, `calcPwmBlock` and `diode` to confirm device arithmetic. ~2.9 M blocks (~70 min of audio), ~40 min wall. Ran: every sound parameter over 0–127 and 0–255 (each in its own process), each FX type, whole-range sweeps, every note×velocity, every LFO wave on all 229 mod targets, random kits in and out of panel range, ping-pong switches, targeted modes. **Did not cover:** MSan-class uninitialised reads (no ASan/MSan) — B10's stale-stack read was caught by a fill-pattern instead.

- **sync** — ASan+UBSan+bounds+float-cast, `-fno-common`, an event-queue scheduler delivering jack edges / DIN bytes / USB transfers in NVIC-priority time order (incl. during SD access), SD read 150 µs / write 1.5 ms virtual. ~25 runs, ~1,300 s virtual. Ran: clock-in at 4/24 PPQ with double-triggers, BPM sweeps, reset bounce, PPQ switches; clock-out at all PPQ/BPM; MIDI-clock slave (clocks inside SysEx, bursts, 1/12/25 s gaps, 3000 clocks/ms overflow, SPP seeks in song mode, wire-rate); USB realtime; kit autoload at pattern end under 3.3 kHz pulses while saving to card; SD under TIM2; MTC. **Did not cover:** CPU time (the meter reads 0), an interrupt landing inside the front-panel byte parser (map risk 2), USB SysEx.

---

## 4. Verification status and candidates that were not filed

**No filed finding failed to reproduce.** Each area ran an independent second pass (the `verify-*` directories) from a fresh `git archive` of HEAD; all 28 findings reproduced with the reported sanitizer output and device analysis. The second pass did, however, **sharpen several verdicts** — the decision-relevant corrections are folded into the tables above, notably:
- **B5** triggers more easily than first written (Play/Stop/Play within the first step; no pattern-select needed) — and is **already fixed on HEAD**.
- **B1/B4** victims include `parameterArray` pointer entries → a later modulated write hits the reserved FMC range → **BusFault/HardFault / lock-up**, not just a wrong sound; B1's bad slot persists across a reboot via tmp.snd.
- **B2** lands at most ~94 KB past `FX_BUFFER` (not 128 KB), and a high-halfword hit on the delay pointers freezes the unit.
- **B3** corruption starts from the *second* save (the first, into a `GLO.CFG`-only folder, reaches harmless fill bytes); the fix buffer needs ≥34 bytes to allow recursion into a user subfolder.
- **B8** latch region confirmed op-by-op against the ELF (FREQ 122–126, RESO ≥80–115 by drive) — a structural instability, not host rounding.

**Observed but deliberately not filed** (known, out-of-area, or not device-reachable — the skeptic's reason in each case):
- **`Oscillator.c:464`/`:174` table reads** — known, pre-existing (the Q13 interpolation-wrap item); negligible one-sample-past-row read in flash.
- **DSP float→int casts / NaNs** (`ResonantFilter.c:313-363`, more `Oscillator.c`, `Snare.c`, `Compressor.c:180`) — the device stores these with saturating `vcvt.s32.f32`+`strh`; wraps/loud, no memory error. Collected as the B19 host/cast family.
- **`transientGenerator.c:60`** left shift of a negative value — DSP, not reduced to a panel setting.
- **`menu.c:2683`/`:2708`** (transientNames/syncRateNames reads) — same class as B12 but the path that set the raw value wasn't pinned down; `seq_setRollRate`/sync-rate panel edits clamp first.
- **`frontPanelParser.c:317`/`:326`** `paramToModTarget[247]` reads for p-lock destinations 247–254 — only synthetic step data produced those; the firmware records only destinations <247.
- **`Delay.c` index reports** (MIDI-in runs) — appeared only together with B1's junk kits and vanished once B1 was fixed; a symptom, not a separate bug.
- **`Uart.c:94`** post-decrement whose value is unused — benign (truncation pass).
- **`morphParameters` NULL during boot `adc_checkPots`** (UI) — a known-class NULL; on the device a NULL read returns ITCM code bytes (no trap), but a one-line guard before `main.c:520` is cheap insurance.

---

## Appendix — cross-area duplicates

The same defect was independently filed by several areas; counts above are deduplicated.
- **B5** (song-255): UI + sequencer + sync (3×).
- **B15** (LFO offset 127): MIDI-in + UI + sequencer (3×); the DSP cast family (B19) overlaps.
- **B1** (kit slot): MIDI-in (Bank Select path) + presets (card path).
- **B3** (currentFolder): presets + UI.
- **B16** (RAT diode): MIDI-in + DSP.
- **B18** (Euclid page/voice): UI + sequencer.
