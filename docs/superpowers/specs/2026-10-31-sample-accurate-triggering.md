# LICHOMAT v0.5: sample-accurate voice triggering (E6.10)

| | |
|---|---|
| Date | 2026-10-31 |
| Status | Draft. Decisions S1–S22 were taken under the owner's delegation of 2026-10-29 ("use your own recommendations"); they are reversible until the plan is written |
| Base | `v0.4` at `91a61b5` (worktree `build/wt-v04`): v0.2–v0.4 and the emulator with the app (R1–R5) |
| Branch | `v0.5`, from `v0.4` (created when the plan starts) |
| Tracker | E6.10 |
| Replaces | the stage 2 draft's deferral of sample-accurate triggering (`2026-10-29-stage2-draft.md` §3 item 4), and, once v0.5 lands, the "Trigger timing" paragraph of implementation note N12 |

## 1. The problem

The timing core `seq_core` gives every event an exact time. Its event times are exact to a CPU cycle,
but a voice starts only where the next audio block begins. Every trigger is therefore quantized to the
64-sample block of 1.45 ms, at 44,140.625 Hz. The voice starts 64 to 128 frames after its event, and
the delay differs for every hit.

Every hit is affected, including hits on the grid. The block grid does not follow the tempo grid: at
130 BPM a step lasts 79.6 blocks. So each step's hits land at a new place inside their block. Hits of
the same tick start together, but the spacing between onsets is off by up to 1.45 ms. Shifted steps,
shuffle, humanize, the track delay, ratchets, rolls (1/32, triplets), the per-track tempo and MIDI-in
notes all inherit the same error.

**Goal.** Each voice starts at the exact sample of its event, within one sample. The device behaves
as before in every other way. There is one deliberate exception (§4.5): the audio comes a constant
128 frames (2.90 ms) after the event. That is v0.4's worst case, where v0.4 varied between 64 and 128
frames.

## 2. Facts (checked at `91a61b5`)

Line numbers refer to the base commit. Facts marked *measured* come from a release build of the base
made outside the repository (`git archive 91a61b5`, the repository's `.toolchain`, `cmake --preset
release`) and its `arm-none-eabi-objdump -dr` listings.

- **SF1. Where the audio is rendered.** The render normally runs in the main loop, not in TIM2.
  - `config.h:29–30` sets `CALC_SAMPLES_IN_INTERRUPT 1` and `USE_INTERRUPT_ONLY_DURING_SAVE_PROJ 1`.
    With these, `appMain.c:728–733` does not start TIM2.
  - `soundengine_process()` (`Soundengine.c:79`) masks the TIM2 update interrupt, then calls
    `seq_tick()` and then `soundengine_calcNextSampleBlock()` (`:23`). The main loop calls it three
    times per pass.
  - Only the preset load and save paths start TIM2. `TIM2_IRQHandler()` (`stm32f7xx_it.c:261`, priority
    4) then runs `seq_tick()` and the render while the main loop waits on the card.
  - A kit autoload inside a core callback calls `soundengine_process()` again, nested. Its `seq_tick()`
    returns at once (the guard at `sequencer.c:2428`), but the render still runs (N12).
- **SF2. The DMA hands out the blocks.**
  - `HAL_I2S_TxCpltCallback()` (`cs4344_cs5343.c:86`, `IN_ITCM_RAM`, from `DMA1_Stream5_IRQHandler` at
    priority 5) restarts the DMA for every block. It is not circular: `HAL_I2S_Transmit_DMA()` runs for
    each half.
  - With `SWITCH_FLAG` 1, the SPI3 branch marks the half that just finished as free:
    `bCurrentSampleValid = 1 - (dmaPtr & 1)` (`:136`).
  - The render that follows the completion of block n plays from completion n+1 on.
  - TIM14 (priority 0, `menu_tick()` up to 4 times), USART3, OTG_FS, EXTI (priority 3), SDMMC (2) and
    TIM2 (4) can all delay this interrupt.
- **SF3. The block render.** `mixer_calcNextSampleBlock()` (`mixer.c:551`, `IN_ITCM_RAM`) works in this
  order:
  1. `modNode_resetTargets()`, `modNode_reassignVeloMod()`;
  2. `lfo_dispatchNextValue()` for the 6 LFOs;
  3. `SVF_recalcFreq()` for the 6 filters;
  4. the six `*_calcAsync()` (`:580`), one control step per voice and block;
  5. for each voice, `*_calcSyncBlock(sampleData, OUTPUT_DMA_SIZE)` (`:605` and on),
     `mixer_decimateBlock()` and the inlined `mixer_addDataToOutput()`, which pans with
     `triggeredPanL/R` (`USE_TRIGGERED_PAN 1`, `mixer.h:22`);
  6. `FxBus_process()` (`:715`), then `djFilter_process()`.

  There are six voices: Drum 1–3, the snare, the cymbal and the hi-hat. Tracks 6 and 7 share the hi-hat
  voice.
- **SF4. Which voice functions take a length.** All four voice renderers take `(buf, size)`. None takes
  a start index, so a caller passes `buf + k`. The functions are:
  - `calcDrumVoiceSyncBlock(v, buf, size)` (`DrumVoice.c:275`);
  - `Snare_calcSyncBlock` (`Snare.c:149`);
  - `Cymbal_calcSyncBlock` (`CymbalVoice.c:157`);
  - `HiHat_calcSyncBlock` (`HiHat.c:165`);
  - and every callee: `calcNextOscSampleBlock`, `calcNextOscSampleFmBlock`, `calcNoiseBlock`,
    `transient_calcBlock`, `SVF_calcBlockZDF`, `SVF_calcBlockCheapZDF`, `calcDistBlock`,
    `calcDistBlockFiltered` and the `bufferTool_*` helpers.

  Their scratch buffers are VLAs of `size`. Three limits apply:
  - **Even lengths only.** These helpers work on int16 pairs through `int32_t*` (`size/2`):
    - `bufferTool_addBuffersSaturating()` (`BufferTools.h:32`);
    - `bufferTool_clearBuffer()` (`:81`);
    - the idle `memset` of `transient_calcBlock()`;
    - the `__QADD16` pair loops of `Cymbal_calcSyncBlock()` and `HiHat_calcSyncBlock()`.

    An odd length drops the last sample, and an odd offset misaligns the pair.
  - **At least 2 samples.** `bufferTool_addGainInterpolated()` (`:121`, the drum voices' amp ramp,
    `DrumVoice.c:316`) divides by `size - 1`, so a size of 1 gives 0/0.
  - **The 64-sample code is per block, not per sample.** These steps loop over `OUTPUT_DMA_SIZE`
    themselves, and all of them work on the whole block after the voice:
    - `mixer_decimateBlock()`, with the rumble follower `atmo_duckSense()`;
    - the inlined pan and mix;
    - `FxBus_process()`, the DJ filter and the reverb.

    Control-rate code counts blocks: `*_calcAsync()`, `_Static_assert(SLIDE_BLOCK == OUTPUT_DMA_SIZE)`
    (`DrumVoice.c:35`), `LFO_SR = REAL_FS/OUTPUT_DMA_SIZE` (`lfo.h:42`), and the `×2` of
    `slopeEg2_calcTime()`.

  No voice needs a rewrite to render in two parts.
- **SF5. What a trigger resets.** All trigger functions run in the event context today
  (`seq_cbTrigger()`, `sequencer.c:1982` → `voiceControl_noteOn()`, `MidiVoiceControl.c:30`).
  - **Drum 1–3** (`Drum_trigger()`, `DrumVoice.c:141`):
    - `lfo_retrigger()`;
    - the velocity modulator (`modNode_updateValue()`, which goes through `midiParser_ccHandler()`);
    - the oscillator and modulator phase. The reset is unconditional, because its `if` is commented
      out;
    - the slide start, keep or stop (v0.3), and the base notes;
    - the pitch EG to 1, the amp EG into attack (`slopeEg2_trigger()` keeps the value, so there is no
      jump to 0);
    - `velo`, the transient phase 0 and the snap EG to 1;
    - `SVF_reset()` and the pan latch.
  - **Snare, cymbal and hi-hat:** the same without the filter reset. The hi-hat also sets `isOpen` and
    its open or closed decay.
  - **Per control step** (`calcDrumVoiceAsync()` `:214`, `Snare_calcAsync()` `:115`,
    `Cymbal_calcAsync()` `:126`, `HiHat_calcAsync()` `:128`): the pitch EG, the snap EG (`snapEg.c`:
    −0.2 × transient pitch per block, so at the highest pitch it lasts 1.25 blocks), the slide factor
    (`expf` while gliding), the amp EG and `osc_setFreq()`.
  - The drum voices ramp their gain linearly over the block, from `lastGain` to `ampFilterInput`. The
    other voices hold one gain per block.
- **SF6. The trigger glue.** `seq_cbTrigger()` does, in this order:
  1. `voiceControl_noteOff()`, which sends MIDI only;
  2. `seq_parseAutomationNodes()`, the p-locks through `midiParser_ccHandler()`;
  3. `Drum_setSlide()`;
  4. `voiceControl_noteOn()`, which runs the voice trigger and sends the front panel LED byte;
  5. `seq_sendVoiceNoteOn()`;
  6. the roll recording.

  Two other paths trigger voices:
  - MIDI-in notes: `MidiParser.c:1527`, inside `seq_parseMidiInput()` in `seq_tick()`.
  - The mailbox one-shot roll: `seq_triggerVoice()` (`sequencer.c:437`, from `:2304`), also inside
    `seq_tick()`.

  No other code triggers a voice. Note-offs never reach a voice: drum voices have no gate.
- **SF7. Event times.** The trigger callback gets `seqc_event_t` with its tick, not its time
  (`seq_core.h`).
  - Internal sync: the core computes the time of a tick as `seqc_internalTime()` (`seq_core.c:532`).
  - Slave modes: `seqc_slaveTarget()` (`:543`) interpolates from the last clock.
  - `seqc_slaveClock()` (`:2623`) jumps to the clock's position and handles every item passed on the
    way.
  - `seqc_runTo()` (`:2474`) handles the items in time order.
  - `seqp_now()` is DWT->CYCCNT, extended to 64 bits.
- **SF8. MIDI input stamps.**
  - `USART3_IRQHandler()` (`Uart.c:47`) stamps each byte. The stamp goes only to the sync queue (for
    realtime, SPP and PC messages). The 256-byte `fifo_midiRx` holds no stamps, and
    `uart_processMidi()` (`:91`) parses later, in `seq_tick()`.
  - USB: `USBD_CDC_DataOut()` (`usbd_cdc.c:1027`) takes one stamp per transfer. The 64-entry
    `usb_MidiMessages` ring holds none either.
- **SF9. The emulator.**
  - `emu_codec.c` replaces `cs4344_cs5343.c`. Block request k fires at `codecStart +
    framesToCycles(64·k)`, with 552,960/113 cycles per frame, at priority 5. Its body does what the
    SPI3 branch does.
  - DWT->CYCCNT is the low 32 bits of the virtual clock. Inside a body, time stands still.
  - The renderer writes a frame-exact 16-bit WAV (`out.wav`, 4 channels) and cycle-stamped `midi.log`,
    `clock.log` and `panel.log` files. So the emulator can measure onsets to the sample.
  - It lacks the exact event time. §9.2 adds a trigger log for it.
  - `emu/tests/drift.json` pins `HAL_I2S_TxCpltCallback`, `USBD_CDC_DataOut` and the menu ids of
    `MenuText.h`. `emu_usb.c` re-implements the MIDI part of `USBD_CDC_DataOut()`. The USART3 handler
    runs unchanged.
- **SF10. Goldens.** 23 goldens have WAV entries, and every one plays voices (all peaks > 0):
  - G1, G3, G4, G5, G6, G7, G10;
  - G2_comp, G2_delay, G2_fold, G2_pingpong, G2_rat, G2_ringmod, G2_tube;
  - G8_120, G8_137, G9_ppq24, G9_ppq4;
  - G11_dsync, G12_djf, G13_reverb, G14_freeze, G15_rumble.
- **SF11. glo.cfg** holds 30 bytes (`PAR_BEGINNING_OF_GLOBALS` … `END_OF_GLOBALS`, D34).
  - `PAR_QUANTISATION` (`Parameters.h:400`) is "UNUSED" in fw 1.7:
    - no menu page lists it;
    - its front panel send is commented out (`menu.c:3637`);
    - its default is 0 (`presetManager.c:1213`);
    - parameters ≥ 256 have no MIDI path (AF7).
  - v0.3 reused fw 1.7's equally unused `PAR_FETCH` byte for `PAR_SCENE_QUANT` (D19).
  - Sub-page 2 of the global settings page (`menuPages.h:378`) has the pattern mode, the clock in PPQ
    and the clock out PPQ, and five free slots.
- **SF12. Memory** (*measured*): ITCM 14,016 / 16,384 B, DTCM 107,396 B, RAM 345,240 B, DTCM + RAM
  452,636 B, image 328,900 B, 0 warnings. These are the numbers the brief gave; the budget of
  `tools/check.sh` is 455,544 / 107,900 / 389,120 B.

## 3. Decisions

| # | Decision | Reason |
|---|---|---|
| S1 | **A per-voice split.** A voice retriggered at offset k renders its old note for [0, k), applies the trigger, and renders the new note for [k, 64). Rejected: an engine-wide split, and per-voice delay lines (§4.1). | The voice renderers already take a length (SF4). An engine split re-runs the FX and changes their control rate, and delay lines skip or repeat a ringing tail. |
| S2 | **One split per voice per block.** A voice's records due in one block are all applied, in order, at the last one's offset. | It bounds the cost at six splits. fw 1.7 already collapses the triggers between two renders to the last one. |
| S3 | **Offsets on a 2-sample grid**, k ∈ {0, 2, …, 62}, rounded to the nearest. | The int16-pair loops and the `size − 1` division (SF4) need even sizes ≥ 2, and the error stays ≤ 1 sample. |
| S4 | **A constant latency of two blocks.** The render after DMA completion n takes the events of the previous block period [T̂ₙ₋₁, T̂ₙ), so an event at t sounds 128 frames after t. | Every event in the window has fired before the render, with no lookahead in the core, and 128 frames is v0.4's worst case. |
| S5 | **The window comes from a stamp at the SPI3 completion**, with a late-only filter: a stamp earlier than predicted is taken at once, a later one moves the estimate by 1/8. The two latest estimates are published in two slots under a serial. | Interrupt latency only delays a stamp, and the two slots need neither masking nor a spin that TIM2 could deadlock. |
| S6 | **The render snapshots the pending block before `seq_tick()`**, in `soundengine_process()` and `TIM2_IRQHandler()`. | `seq_tick()`'s time is then at least the window's end, so no event of a window fires after its render. |
| S7 | **`seqc_itemTime()` in `seq_core`:** inside a callback, the time at which the item being handled is due. Internal sync: `seqc_internalTime(tick)`. Slave modes: the interpolation from the last clock, or that clock's stamp for items passed by its jump. | The callbacks know only the tick, and the core already computes these times for `nextDue`. |
| S8 | **Each trigger is split in two.** The event side stays at fire time: MIDI Note Off and On, LED, recording and the slide bookkeeping. The voice side becomes a record applied at the split: the p-locks, the slide mode and the voice trigger. | The voice side has to act at the onset sample, and the event side keeps v0.4's MIDI and UI timing. |
| S9 | **MIDI-in notes carry the arrival stamp** of their last byte (DIN) or of their packet (USB). The stamps are 16 bits (DWT >> 8), stored beside the receive rings. | The parser runs later than the arrival. 16 bits cover 77 ms at 1.2 µs resolution and cost 640 B rather than 1,280 B. |
| S10 | **Untimed triggers** (the mailbox one-shot roll) start at offset 0 of the next render, as in v0.4. | A button press has no exact time worth keeping. |
| S11 | **Late and early records.** A record due more than 2 frames before its window is late: it starts at offset 0 and is counted. A record due after the window waits for the next block. | It is never worse than v0.4, and the counter shows when it happens. |
| S12 | **Records at offset 0 (and late ones) are applied before the block's control path** (the mod reset, LFOs, filters and `*_calcAsync()`). | It is v0.4's order of the triggers in `seq_tick()`, at no extra cost. |
| S13 | **A new note's first control step lasts a full block.** At the split the voice computes step 1 and renders [k, 64). In the next block it keeps step 1 for [0, k), steps at k (a *phase split*) and then follows the block grid again. | Snap clicks and the first EG steps are one block long (SF5). A shortened first step would change the attack from hit to hit. |
| S14 | **The old tail keeps its own step:** the drum gain ramp stops at its value at k. The pan is applied per segment: the old latch before k, the new one from k. | The old note stays exact up to the onset. |
| S15 | **LFOs that the voice retriggers are dispatched again at the split.** All other control-rate state (the mod-target reset, the velocity reassign, other LFOs, other voices) follows at the next block. | An LFO used as an envelope has to start with its note. The rest is already per block. |
| S16 | **Records live in a ring of 4 per voice** (12 B each) in RAM. On overflow the oldest is dropped and counted. | With collapsing, two blocks need at most 4. DTCM has only 504 B left. |
| S17 | **No locks on the rings.** The producers (the core's callbacks, the MIDI parser, the mailbox) and the consumer (the mixer) all run in the `seq_tick()` and render context. `vsched_post()` checks `seq_inTick`. | The main loop renders with TIM2 masked, and TIM2 renders only during card access, where it runs both (SF1). |
| S18 | **The `trg` setting:** `smp` or `blk`, on the global settings page, sub-page 2, slot 4. It is stored in glo.cfg's `PAR_QUANTISATION` byte, renamed `PAR_TRIG_TIMING`: 0 = `smp` (the default), 1 = `blk`, any other value `smp`. `blk` runs v0.4's code path. | An A/B switch and a CPU fallback with no format change, as in v0.3's D19. It also makes bit-identity with v0.4 testable. |
| S19 | **The CPU cost is not charged to D41's 85 %.** The worst case is 2.45 %, or 3.45 % with six retriggered LFOs. D41's reverb choice stands, and E6.10 uses the room between 85 and U20's 95. | The owner ranks atmosphere first, and D41's worst case plus E6.10 still stays below 95. |
| S20 | **The cheaper variant is a compile-time split cap C:** 6 (no cap) by default, 3 as the fix-up (worst case 1.86 %). A block that needs more than C splits starts all its records at offset 0 and drops its phase splits. The fix-up applies only if U30 reads `pk` ≥ 95. | It bounds the cost and keeps simultaneous hits together. |
| S21 | **Re-bless the goldens once** (§9.3). The 23 goldens are re-blessed in one commit after three checks pass: the logs are identical, the `_blk` copies are identical to v0.4, and the onset errors are ≤ 1 frame. New: G16_onsets, G17_midi_in, G18_slave_onsets, five `_blk` goldens, and the renderer's `--trig-log`. | The re-bless then proves that only the onset timing changed. |
| S22 | **Rollout.** Branch `v0.5` from `v0.4` at `91a61b5`, version 0.5 (`LXR_V2_LICHOMAT_0.5.img`). v0.4's later commits (A7–A11) are merged in before the release. The owner's checks are U27–U32. | E6.10 builds without H12, but its CPU check needs the reverb at its chosen rate. |

## 4. Mechanism

### 4.1 Options

| Option | What it needs | Cost (worst block, D41 factor) | Verdict |
|---|---|---|---|
| (a) Split the whole engine at each distinct offset | The voices, the mixer, `FxBus_process()`, the reverb's per-block LFO, the DJ filter and the delay all render in segments, and their control steps (SF3 1–4) run per segment or stay per block | The whole chain's per-call overhead per distinct offset, about 0.8 % each × up to 7 offsets, plus the FX per-call parts. The FX would also change: their control rate would follow the triggers | Rejected |
| (b) Per-voice split (S1) | The four voice renderers called twice with `buf + k` (SF4), a control step at k, and the pan and mix per segment | 2.45 %, 3.45 % with six retriggered LFOs (§6) | **Chosen** |
| (c) Per-voice delay lines | 64 int16 per voice; the voice renders block-aligned and its output is read d samples late, with d set at each trigger | About 0.6 % every block (6 × ~200 cycles), 768 B | Rejected. When d changes at a retrigger, the old tail is read with a jump of \|d_new − d_old\| samples: up to 63 samples skipped or played twice, a click on any ringing tail or long decay. LFO and cross-voice modulation would also be off by d |

Option (b) keeps the old note exact up to the onset. A new note's first 64 samples equal v0.4's first
block of that note (§4.5). It costs nothing in blocks without triggers.

### 4.2 The per-voice split

A module `vsched` (`Core/Src/Soundengine/vsched.c` and `.h`, plain C11 like `seq_core`, built on the
host) holds the records, the block window, the offset arithmetic and the per-voice phase. The mixer
calls it. For each block:

1. **The cap check (S20).** The mixer counts the voices that need a split: a record at k > 0, or a
   pending φ. If there are more than C, every record due in this block counts as due at offset 0, and
   every φ is cleared. The block then plays as in v0.4.
2. **Phase 0, before the control path (S12).** For each voice, every record due at offset 0 is applied
   in order: the late ones, the untimed ones, those whose rounded offset is 0, and, after a cap
   fallback, all of them. Records due later in the block are left in their rings; they are applied at
   their split in step 4.
3. **The control path as today (SF3 1–4).** A voice with a pending phase φ (S13) skips its
   `SVF_recalcFreq()` and its `*_calcAsync()` here, since its note's first control step carries on.
4. **For each voice, in mixer order.**
   - **A record split at k** (S2, S3). The voice renders [0, k) with the block's control values. For a
     drum voice the gain ramp ends at the value it would reach at k (S14). Then it:
     1. saves its pan latch;
     2. applies all its records of this block in order, as described in §5.5;
     3. re-dispatches the LFOs whose `retrigger` is this voice (S15);
     4. runs `SVF_recalcFreq()` and `*_calcAsync()`, which gives step 1;
     5. renders [k, 64) with the ramp ending at its value after 64 − k samples;
     6. sets φ = k.
   - **A phase split at φ** (S13). The voice renders [0, φ) with step 1's values, which finishes step
     1's 64 samples. Then it runs `SVF_recalcFreq()` and `*_calcAsync()` (step 2), renders [φ, 64), and
     clears φ. Step 2 ends at the next block boundary, so it is shorter than a block by φ. After that
     the voice follows the block grid.
   - **Both in one block:** a new record at k′ replaces the phase split. The old note's step 1 runs on
     to k′, at most 1.45 ms longer, and the new record's split sets φ = k′.
   - **Otherwise** the voice renders [0, 64) as today.

   After that come `mixer_decimateBlock()` over the whole 64 samples (the decimator's counter and the
   rumble follower run on) and the mix:
   - a split voice is mixed in two parts, [0, k) with the saved pan and [k, 64) with the new one;
   - any other voice in one part.
5. **`FxBus_process()`, the DJ filter and the reverb as today**, once per block.

The mixer's six hand-unrolled voice sections (`mixer.c:605–710`) become one per-voice helper with the
segment and pan arguments. `mixer_addDataToOutput()` gets a start and a length. In `blk` mode (S18)
there are no records, no splits and no φ. The helper then has to compute exactly what the old code
computes, and the `_blk` goldens check that (§9.3).

**Drum gain ramp.** A partial step must keep the linear ramp of `bufferTool_addGainInterpolated()`
continuous:
- a part of n samples ramps from the ramp's current value to its value n/64 of the way to the step's
  target;
- `ampFilterInput` then holds the value reached, so the next `calcDrumVoiceAsync()` takes it as
  `lastGain`.

A whole 64-sample step is computed exactly as today. Snare, cymbal and hi-hat hold one gain per step,
so they need nothing.

### 4.3 The voice functions

| Function | Takes a length | Needs | Called on a split |
|---|---|---|---|
| `calcDrumVoiceSyncBlock(v, buf, size)` | yes | even size ≥ 2 (pair add, gain ramp) | twice, `buf` and `buf + k` |
| `Snare_calcSyncBlock(buf, size)` | yes | even size (via callees) | twice |
| `Cymbal_calcSyncBlock(buf, size)` | yes | even size (pair loops) | twice |
| `HiHat_calcSyncBlock(buf, size)` | yes | even size (pair loops) | twice |
| `*_calcAsync()`, `SVF_recalcFreq()` | per control step | — | once more at k (record split), or moved to φ (phase split) |
| `Drum_trigger()`, `Snare_trigger()`, `Cymbal_trigger()`, `HiHat_trigger()` | — | — | moved from the event context to the split |
| `mixer_decimateBlock()`, `atmo_duckSense()` | assumes 64 | — | once per block, after both parts |
| `mixer_addDataToOutput()` (inlined) | assumes 64 | gets start and length | twice for a split voice |
| `FxBus_process()`, `djFilter_process()`, `Reverb_process()` | per block | — | unchanged |

### 4.4 Records

```c
typedef struct {            /* 12 B */
	uint32_t time;          /* event time, DWT cycles (low 32 bits); 0 for untimed */
	uint8_t  track;         /* 0..6; the hi-hat voice keeps open (6) or closed (5) */
	uint8_t  note, vel;
	uint8_t  flags;         /* VS_TIMED, VS_PLOCKS, VS_SLIDE_ON, VS_SLIDE_KEEP, VS_SRC_SEQ/MIDI/ONESHOT */
	uint8_t  p1Nr, p1Val, p2Nr, p2Val;   /* the event's own p-locks (D5) */
} vsched_rec_t;
```

There is one ring of 4 per voice. Between two renders a voice holds the records of the window being
rendered and of the next one. Its records in one window collapse at the split (S2). Four is therefore
enough unless a voice is triggered five times within about 2.9 ms. Then the oldest record is dropped
and counted in `vsched.overflow` (S16). That case behaves as fw 1.7, where the last trigger wins.

### 4.5 What the listener gets, and what stays

- **Timing.** The onset is at the event's frame + 128, within one frame (S3, S4).
  - Two hits a tick apart at 120 BPM are 57.5 frames apart. They now land 56 or 58 frames apart,
    since both onsets sit on the 2-sample grid. In v0.4 they landed 0 or 64 frames apart.
  - Hits of the same tick still start at the same sample.
- **The first block of a note** (S13) is computed from the same control values as v0.4's first block
  of that note:
  - the reset oscillator phase, the transient from phase 0, and the snap, pitch and amp EGs at step 1;
  - the p-lock and velocity values;
  - the retriggered LFO's value (S15).

  Its 64 samples therefore equal v0.4's first block. Two small differences remain:
  - the per-sample smoothers (the fader, the mixer page, the drive) have advanced k samples further;
  - the drum ramp starts from the old tail's value at k.
- **Later control steps.** From step 2 on, a note's control steps come up to 62 samples earlier
  relative to its onset than in v0.4, since step 2 is shortened by φ. This shift is smaller than the
  control rate itself, and it applies to slow envelopes only.
- **The latency becomes constant.** The audio comes 128 frames (2.90 ms) after its event.
  - In v0.4 it came 64 to 128 frames after, 2.18 ms on average.
  - Against the MIDI out, the clock out jack and the clock-out channel of the emulator app, the sound
    is therefore later by 0 to 1.45 ms, 0.73 ms on average, and by the same amount every time.
  - As a clock slave the device keeps a constant 2.90 ms behind the incoming clock; a DAW can
    compensate that exactly.
- **Cross-voice effects.** A p-lock or modulation that targets another voice takes effect, from the
  split, in the same block for voices that the mixer renders later, and in the next block for voices
  rendered earlier. In v0.4 everything took effect at the block start.
- **Unchanged:**
  - MIDI out, the clock outs, LEDs, the recording, the core's choke and mute rules;
  - the slide (decided at fire time, applied with the trigger);
  - the FX and the master;
  - the control rate of every voice outside its splits.

## 5. Event path

### 5.1 The time of an event

**Sequencer events (S7).** `seqc_itemTime()` is a new query in `seq_core.h`. It is pure and allowed
inside callbacks. It returns the time at which the item being handled is due, as `seqc_time_t`:
- **Internal sync:** `seqc_internalTime(tick)`. This is the same value `seqc_schedule()` uses for
  `nextDue`, rounded down, so it is ≤ the core's time.
- **Slave modes, items reached by the interpolation in `seqc_runSlave()`:** `lastClockTime + (tick −
  lastClockPos) × cyclesPerTick`. It is clamped to the time of the call, including the dropout
  branch's limit.
- **Items handled by a clock's jump in `seqc_slaveClock()`:** that clock's stamp. The clock says the
  position is there, and the items passed on the way were due at that moment.
- **Outside a callback:** the core's current time.

Within one call the times never decrease. The core does not change otherwise: no new state beyond
the item's time, and the existing tests stay as they are.

**MIDI-in notes (S9).**
- **DIN.** `USART3_IRQHandler()` already reads `seqp_stamp()` first. It now also writes `(uint16_t)
  (stamp >> 8)` into `midiRxStamp[256]` at `fifo_midiRx.write`, before `fifo_bufferIn()` advances the
  index. `uart_processMidi()` reads the stamp at `fifo_midiRx.read` before `fifo_bufferOut()`.
- **USB.** `USBD_CDC_DataOut()` writes the transfer's stamp into `usb_MidiStamps[64]` at
  `usb_MidiMessagesWrite`, before the index advances. `emu_usb.c` follows, and its drift item is
  recorded again. The reader in `usb_manager.c:189` takes the stamp with the message.
- **The parser.** It keeps the stamp of the byte or message being parsed in `midiParser_rxStamp`. The
  Note On handler (`MidiParser.c:1527`) posts its record with that time, rebuilt from 16 bits against
  `seqp_now()`:

  `age = (uint16_t)((now >> 8) − s16)`, `t = (now & ~0xFF) − (age << 8)`.

  The error is under 256 cycles (0.05 frames). A byte older than 77.7 ms reads as younger, but it is
  late in any case (S11).
- USB notes keep the 1 ms granularity of USB frames: every note of a transfer shares its stamp.

**Untimed triggers (S10).** `seq_triggerVoice()` (the one-shot roll) posts a record without
`VS_TIMED`. It is applied in phase 0 of the next render, which is v0.4's timing.

### 5.2 The block window

**In `HAL_I2S_TxCpltCallback()`, SPI3 branch, before the DMA restart:** `audio_blockDone(DWT->CYCCNT)`
(new, in `AudioCodecManager.c`, `IN_ITCM_RAM`, about 15 instructions). The emulator's
`emu_blockBody()` calls the same function, and the drift item is recorded again.

```c
#define VS_BLOCK_CYCLES 313181u          /* 64 frames: 64 × 552,960 / 113 = 313,180.9 cycles */
void audio_blockDone(uint32_t t)
{
	const uint32_t pred = vs_est + VS_BLOCK_CYCLES;
	const int32_t  e    = (int32_t)(t - pred);
	uint32_t next;
	if (!vs_seeded || e > (int32_t)(VS_BLOCK_CYCLES / 2) || e < -(int32_t)(VS_BLOCK_CYCLES / 2))
		next = t;                        /* first block, or a stall: re-seed */
	else
		next = pred + (e < 0 ? e : e / 8);   /* late-only filter (S5) */
	const uint32_t n = vs_serial + 1u;
	vs_slot[n & 1u].w0 = vs_est;         /* the previous estimate */
	vs_slot[n & 1u].w1 = next;
	vs_est = next; vs_seeded = 1;
	vs_serial = n;                       /* publish: one 32-bit store */
}
```

The window of block n is [Ŵ0, Ŵ1) = [T̂ₙ₋₁, T̂ₙ). The window of the next block starts where this one
ends, so no event is lost or counted twice.

**What the filter handles.**
- An interrupt delayed by J cycles moves the estimate by J/8, and the error fades within about 16
  blocks.
- A real slip of the stream is followed within 16 blocks. Such a slip is the non-circular DMA's
  underrun when the restart comes more than a frame late (SF2), and it is audible as a glitch anyway.
- The period error of the integer `VS_BLOCK_CYCLES` (+0.115 cycles per block) is taken at once by the
  "earlier than predicted" branch.

**How the render reads it.** It reads `s = vs_serial`, then `vs_slot[s & 1]`. The writer fills the
other slot, and two completions cannot fall between two loads. This needs no masking, and nothing spins
in TIM2 context (priority 4), which the DMA interrupt (priority 5) cannot preempt.

**The snapshot (S6).** `soundengine_process()` and `TIM2_IRQHandler()` read whether a block is pending
before calling `seq_tick()`, and render only that block afterwards. The completion that made it pending
came before `seq_tick()` read the time, so every event before Ŵ1 has fired before the render.
- A block that became pending during `seq_tick()` is rendered at the next call, one main-loop segment
  later. That is well inside the block's 1.45 ms deadline.
- In the emulator nothing changes: time does not advance inside `seq_tick()`, so `blk` renders stay
  bit-identical (§9.3).

### 5.3 Offsets

For a record of time t and the window [W0, W1), L = W1 − W0 (about 313,181):

- d = (int32_t)(t − W0). Comparisons are modulo 2³²; records older than 9.9 s cannot exist.
- **d < −L/32**, more than 2 frames early: **late** (S11). The record is applied in phase 0 and counted
  in `vsched.late`.
- **−L/32 ≤ d < 0:** a record whose rounding carried it into this block. It is applied in phase 0 and
  not counted.
- **0 ≤ d < L:** j = (32·d + L/2) / L, an unsigned division of values below 2²⁵.
  - j < 32: k = 2j, so the record is due at offset k (0 means phase 0).
  - j = 32: it waits, and the next block gives it −L/32 ≤ d < 0.
- **d ≥ L:** it waits for a later block.

The rounding error is |k·L/64 − d| ≤ L/64, at most one frame.

Late records come from:
- a kit autoload, whose nested renders run while `seq_tick()` returns at once (SF1);
- a skipped (underrun) block;
- a MIDI byte that waited longer than a block in the receive FIFO.

`vsched.late` stays 0 in every golden without a kit autoload, and §9.2 expects it there.

### 5.4 Contexts (S17)

| Data | Written by | Read by | Discipline |
|---|---|---|---|
| Record rings, φ, counters | `seq_tick()` context: core callbacks, the MIDI parser, the mailbox | the mixer, in the same context (main loop with TIM2 masked, or TIM2 during card access; also a nested render inside a callback) | single context: plain stores. `vsched_post()` asserts `seq_inTick`, and the emulator faults otherwise |
| Block window slots, serial | DMA1_Stream5 IRQ (priority 5); `emu_blockBody()` | the mixer | two slots and a serial (§5.2) |
| `midiRxStamp[]`, `usb_MidiStamps[]` | USART3 IRQ, OTG_FS IRQ (priority 3) | `seq_tick()` | written before the existing ring's write index is published, read before its read index is released: the rings' own SPSC rule |
| `PAR_TRIG_TIMING` | the menu (main loop), glo.cfg load | the glue in `seq_tick()` | one byte, read once per trigger |

A nested render inside a callback runs between two ring operations, never in the middle of one, so
the single-context rule holds there too.

### 5.5 The glue

**`seq_cbTrigger()` in `smp` mode.** The event side runs at once, as today:
- `voiceControl_noteOff()` (MIDI);
- `seq_slideMode()` (its bookkeeping depends on the event only);
- the LED byte, `seq_sendVoiceNoteOn()`, the roll recording.

Then it posts `{time = seqc_itemTime(), track, note, vel, flags = TIMED | PLOCKS | slide mode, p-locks}`
to the track's voice.

**Applying a record**, at its split or in phase 0, does in this order:
1. `seq_parseAutomationNodes(track, p-locks)`, if `VS_PLOCKS` is set;
2. `Drum_setSlide(v, mode)`, for Drum 1–3;
3. the voice trigger: `Drum_trigger()`, `Snare_trigger()`, `Cymbal_trigger()` or
   `HiHat_trigger(vel, track − 5, note)`.

`voiceControl_noteOn()` splits the same way. The `active_voices` bit and the LED byte stay with the
caller, and the trigger itself moves to `vsched_apply()`.

**MIDI-in Note Ons** post `{time = stamp, TIMED, src MIDI}` without p-locks. The mute test, the note
override, the recording and the echo stay where they are.

**In `blk` mode** the glue calls the v0.4 functions directly, at fire time, and posts nothing (S18).

### 5.6 Choke groups, note-off, velocity

- **The core's choke and same-tick rules** (spec 6.4, N2) act on the queue before firing, unchanged. A
  fired event's record is never removed again.
- **Voice choke.**
  - A retrigger ends the old tail at the split. The old note keeps sounding until k, and the trigger
    then resets what SF5 lists.
  - The hi-hat's tracks 6 and 7 share one voice: a closed and an open hit in one window collapse (S2),
    and the later one sounds, as in v0.4.
  - There are no other voice choke groups.
- **Note-off** reaches only MIDI, at its tick (4 ticks after the trigger), unchanged. Voices have no
  gate (SF6).
- **Velocity and accent.** The LXR has no accent; the step volume is the velocity. The humanize volume
  is already applied in the core. `vel` travels in the record to the trigger: `velo` and the velocity
  modulator (`modNode_updateValue()`) act at the split, and `modNode_reassignVeloMod()` re-applies it
  at the next block start as today.

## 6. CPU cost

### 6.1 Method

The method is that of `docs/lichomat/reports/v0.4-reverb-cost.md`: a static instruction count of the
release objects, at one cycle per instruction, plus 13 per division (`vdiv`, `vsqrt`) and 4 per call,
times D41's factor 1.5, over `CPUMETER_BLOCK_CYCLES` = 313,185.

The objects are `lxr_dsp.dir/Src/Soundengine/DSPAudio/*.c.obj` of the base, built as SF12 says
(`-O3 -ffast-math`, no LTO). A split adds three kinds of work:
- the control step at k: the whole `*_calcAsync()`, with its callees counted in full, every path;
- the non-loop part of every function on the render path, called once more: the instructions outside
  the sample loops, with the switches' other paths counted too. The voice bodies' paths were counted by
  hand, and every callee is counted in full;
- the record bookkeeping and the second mix call.

Moved work is not added: the trigger functions and the p-lock `midiParser_ccHandler()` calls run at the
split instead of in `seq_tick()`, and the CPU meter measures both together (SF1).

| Part (instructions, divisions) | Drum 1–3 | Snare | Cymbal, hi-hat |
|---|---:|---:|---:|
| `*_calcAsync()` + callees (`DecayEg_calc` 23, `slopeEg2_calc` 72, `osc_setFreq` 37 each, `SnapEg_calc` 13, `expf` 67 while gliding) + calls + divisions | 85+23+67+72+74+13 + 6·4 + 4·13 = **410** | 27+23+72+74+13 + 5·4 + 4·13 = **281** | 23+72+111+13 + 5·4 + 3·13 = **278** |
| `SVF_recalcFreq()` 17 + call + 1 division | 34 | 34 | 34 |
| Render path outside the loops: body (drum 96 + one `vdiv`; snare 48; cymbal and hi-hat 61), oscillators (`calcNextOscSampleBlock` 33 + `calcPwmBlock` 23; FM 60 + 29), `transient_calcBlock` 26 + `memset` 20, `SVF_calcBlockZDF` 46 + 1 division, distortion 8–13, calls | **400** | **265** | **411** |
| Record bookkeeping: window, offset with one `udiv`, ring, dispatch, pan save | 54 | 54 | 54 |
| Second mix call (`mixer_addDataToOutput` setup + `mixer_checkOutJackAvailable` path) | 31 | 31 | 31 |
| **Record split** | **929** (0.445 %) | **665** (0.319 %) | **808** (0.387 %) |
| **Phase split:** the render path's second call + 15, since the control step only moves | **415** (0.199 %) | **280** (0.134 %) | **426** (0.204 %) |
| LFO re-dispatch (S15): `lfo_dispatchNextValue` 92 + `modNode_updateValue` 47 + `midiParser_ccHandler` path ≈ 120 + setter ≈ 60 + calls + 1 division | 348 (0.167 %) | 348 | 348 |

The percentages include D41's factor 1.5. The per-block fixed part, paid with or without triggers, is
about 55 instructions (0.026 %): the window read, the filter in the interrupt, and six ring and phase
tests.

### 6.2 Cases

| Case | Splits in the block | Raw | With 1.5 |
|---|---|---:|---:|
| **Worst:** all six voices retriggered in one block | 6 record splits | 1.64 % | **2.45 %** |
| Worst, every voice also retriggering an LFO | 6 + 6 LFO dispatches | 2.30 % | **3.45 %** |
| **Typical downbeat:** kick, clap, closed hat, perc (2 drums, cymbal, hi-hat) | 4 record splits | 1.13 % | **1.69 %** |
| The block after it | 4 phase splits | 0.55 % | 0.83 % |
| One off-grid hat (shuffle, humanize) | 1 record split, then 1 phase split | 0.27 % | 0.41 %, then 0.23 % |
| Mean over time, 130 BPM 16ths, about 30 hits/s | ≈ 0.05 record + 0.05 phase splits per block | 0.02 % | 0.03 % |
| H12 step 3 (7 tracks of 16ths with ×8 ratchets, 140 BPM) | the worst case in every ratchet block (13.4 ms apart) | | 2.45–3.45 % |
| Fix-up cap C = 3 (S20), each split with an LFO | 3 | 1.24 % | 1.86 % |

The `CPU` figure (the mean) barely moves. `pk` (the worst block per second) moves by the worst case
of the pattern played: about 1.7 points for a typical groove, and up to 2.45 or 3.45 points for H12's
heavy pattern.

### 6.3 D41

D41: full rate if P ≤ 46.24, half rate if P ≤ 63.28, otherwise lean, where P is H12 step 3's `pk` on
0.3 and the limits are 85 % less R\*_full or R\*_half and R_dj 3.69.

- **If E6.10 were charged to the 85 %**, the limits would fall by its worst case:
  - **43.79 and 60.83** without LFO retriggers;
  - **42.79 and 59.83** with them.

  A P in (43.79, 46.24] would then drop the reverb from full to half rate, and a P in
  (60.83, 63.28] from half rate to lean.
- **Decision S19: it is not charged.** The reverb's rate is chosen by v0.4's D41 alone (A7, after H12).
  Under that rule v0.4's `pk` stays ≤ 85, so v0.5's stays ≤ 85 + 3.45 = 88.45, below U20's 95, for
  any P.
- E6.10 thus uses part of the 15 points that D41 kept for the main loop, missed peaks and estimate
  error, and only in blocks with many simultaneous hits.
- U30 reads it. If `pk` reaches 95 there, the fix-up is the cap C = 3 (S20), not the reverb's rate.
  The owner's own fallback is `trg blk`.

### 6.4 The cheaper variants

The brief asked for a variant if the worst case passed about 2 %. The base design already includes
the one-split-per-voice limit (S2). Without it, a voice's records would add splits without bound.

| Variant | Worst | What it costs musically | Status |
|---|---:|---|---|
| Base design, C = 6 | 2.45 % (3.45 % with LFOs) | nothing | default (S19) |
| Cap C = 4, whole-block fallback | 2.47 % with LFOs | blocks with 5–6 starting voices fall back to block timing | not used |
| **Cap C = 3**, whole-block fallback | **1.86 %** | blocks with ≥ 4 starting voices (dense downbeats, and the block after them) fall back to block timing; voices stay aligned with each other | **fix-up if U30 fails** (S20) |
| No LFO re-dispatch | 2.45 % | a retriggered LFO's first value comes up to 64 − k samples late | rejected: an LFO used as an envelope would blip at the attack |
| No phase split (first control step shortened) | phase blocks cost 0 | the snap click and the first EG step change from hit to hit (SF5) | rejected |
| Per-voice on/off mask | scales with the mask | the owner picks which voices are exact | not needed; `trg` switches all |

## 7. Memory

| Item | Where | Bytes |
|---|---|---:|
| Record rings: 6 voices × 4 × 12 B + indices | RAM `.bss` | 300 |
| φ, cap state, counters (`late`, `overflow`, `splits`, `phaseSplits`, `capFallbacks`) | RAM | 26 |
| Block window: 2 slots × 8 B, serial, estimate, seeded flag | RAM | 25 |
| `midiRxStamp[256]` (uint16) | RAM | 512 |
| `usb_MidiStamps[64]` (uint16) | RAM | 128 |
| **Total** | | **≈ 991** |

Against the v0.4 budget (`tools/check.sh`), with the numbers measured at `91a61b5` (SF12):

| | Now | After E6.10 | Limit | Left |
|---|---:|---:|---:|---:|
| DTCM | 107,396 | 107,396 | 107,900 | 504 |
| RAM | 345,240 | ≈ 346,231 | 389,120 | ≈ 42,889 |
| DTCM + RAM | 452,636 | ≈ 453,627 | 455,544 | ≈ 1,917 |

- **Nothing goes into DTCM.** Records are touched a few times per block, and the window once.
- **The sum limit leaves 2,908 B.** It is the only tight one, which is why the stamps are 16 bits
  (S9). 32-bit stamps would add 640 B, which still fits.
- **ITCM** has 2,368 B free. The mixer is `IN_ITCM_RAM`. Folding its six unrolled voice sections into
  one helper should make it smaller, which leaves room for the split code. If ITCM would overflow,
  `vsched_apply()` and the cap logic go to flash: they run at most six times per block.
- **Stack.** At a split, the p-lock path `seq_parseAutomationNodes()` → `midiParser_ccHandler()` →
  setter runs inside the mixer. That chain is shallower than the voices' VLA frames (2 × 128 B), so
  the deepest render chain grows by the helper's frame only, about 64 B. D30 leaves at least 23,172 B
  of DTCM for the stack.
- **Flash** grows by about 3 KB (image 328,900 / 1,015,808 B).

## 8. Compatibility

- **No file format changes.**
  - `.pat`, `.kit`, `prj.nfo` and the v0.2–v0.4 blocks are untouched.
  - glo.cfg keeps its 30 bytes. Its byte 9 (`PAR_QUANTISATION`, parameter 281) changes meaning; fw 1.7
    writes 0 there and never reads it (SF11).
  - fw 1.7 files load as before. A glo.cfg from fw 1.7 reads `trg smp`, and a v0.5 glo.cfg loads in
    fw 1.7, which ignores the byte.
- **The setting (S18).**
  - `trg` sits in the global menu (SHIFT + step 12), on the second sub-page, in slot 4 after `kcm`,
    `cki` and `cko`. That puts it on the sub-page before the CPU page. Its values are `smp` and `blk`.
  - It needs a new menu id `MENU_TRIG_TIMING` with `trigTimingNames` `{2}, {"smp"}, {"blk"}`; any
    value other than 1 shows `smp`.
  - The renderer's name becomes `trig_timing` (`gen_params.py`, `render_params.h`).
  - A switch while playing takes effect at the next trigger. Records already posted are still applied,
    and a pending φ finishes once.
- **`blk` is v0.4's code path** at the trigger, and the mixer computes the same values. In the
  emulator a `blk` render therefore equals v0.4's bit for bit (§9.3). On the device the snapshot (S6)
  can delay a render by one main-loop segment; the audio is the same.

## 9. Tests

### 9.1 Host tests (`tests/`)

| Id | What |
|---|---|
| `vsched_offsets` | Window [W0, W0 + L) with L = 313,181 ± 2,000. Times at W0, at each frame boundary ± 1 cycle, at W1 − 1, W1, and −L/32 ± 1: k is even, \|k·L/64 − d\| ≤ L/64, j = 32 waits and arrives at offset 0 uncounted, d < −L/32 counts as late, d ≥ L waits. W0 just below 2³²: the same results across the wrap |
| `vsched_window` | `audio_blockDone()` with exact periods, one stamp 30 µs late (the estimate moves ≤ 1/8 of it and is back within 1 frame after 16 blocks), a persistent +4,893-cycle slip (followed to < 0.5 frame within 16 blocks), an early stamp (taken at once), 10⁶ periods of 313,180.885 (no drift past 1 cycle), a 10 ms stall (re-seeded). The two slots: a reader interleaved with a writer at every step never sees a torn window |
| `vsched_rings` | Collapse: two records of a voice in one window are applied in order at the later k; a k = 0 and a k = 40 record of one voice both go to k = 40. Different voices stay independent. The fifth record drops the oldest and counts it. An untimed record goes to phase 0. The cap: C + 1 voices with records give a whole-block fallback (all at 0, φ cleared) and count it |
| `vsched_stamps` | The 16-bit rebuild for ages 0 to 77.6 ms across the 2³² wrap: error < 256 cycles. An age past the range reads as younger and is never later than now |
| `seqc_itemtime` (in the core suite, fake host) | Internal sync: for on-grid steps, shifts −7..+7, swing, ratchets ×2–×8, rolls 1/32 and triplets, the track delay and the per-track tempo, `seqc_itemTime()` inside `trigger()` equals `seqc_internalTime(ev->tick)`, is ≤ the process time and never decreases within a call, also across a tempo change (anchor move). Slave modes: items reached by the interpolation get `lastClockTime + Δtick·cyclesPerTick`; items passed by an early clock get its stamp; a dropout's items are ≤ the limit |
| `dsp_split` (with `test_dsp`'s DSP sources and flags) | For each voice type and each even k: (1) a block rendered as [0, k) + [k, 64) without a trigger leaves the oscillator phases, the filter state and the RNG where one 64-sample call leaves them, and the output is bit-identical for the snare, cymbal and hi-hat (one gain per step) and within 1 LSB for the drums (the ramp is split, not changed); (2) a silent voice triggered at k gives zeros in [0, k) and its first 64 samples from k equal the k = 0 render's first 64 samples within 1 LSB (S13); (3) the snap at transient pitch 4 lasts 64 samples from k for every k |
| `mixer_segments` | The pan per segment: the old latch before k, the new one from k, on every routing (DAC1/2 stereo and mono, FX bus) |

Before pushing, each new host test is checked with `arm-none-eabi-gcc -std=c11` (CI builds with GCC on
Linux in strict C11, and needs `_DEFAULT_SOURCE` for `M_PI`).

### 9.2 Emulator

**The trigger log.** `lichomat-render --trig-log PATH` writes one line per applied record:
`<event cycles> <voice> <seq|midi|oneshot> <block serial> <k> <late 0|1>`. The render counters
`vsched.late`, `vsched.overflow`, `vsched.splits`, `vsched.phaseSplits` and `vsched.capFallbacks` go
into `summary.json`.

**The onset tool `tools/emu_onsets.py WAV --channel L1`.**
- An onset is the first frame with \|x\| ≥ 4,000 after at least 32 frames below 64.
- Each onset is matched with the trigger log's next record of the voice routed to that channel.
- The tool fits one constant c, the median of onset − (frames(t − codecStart) + 128), and reports
  each residual r = onset − c − frames(…) and their spread.
- With `--grid BPM`, it fits the onsets to the tick grid instead: tick × 44,140.625 · 60 / (BPM · 384)
  plus a constant. That check does not trust the firmware's own `seqc_itemTime()`.
- Exit 0 if the spread is ≤ the limit.

**The fixtures.** The card `onset` (`emu/tests/cards/make.sh`) has a kit with a click on Drum 1:
- attack 0, a decay of about 5 ms, a transient sample at full volume, the filter off;
- routed alone to output 1 (L1/R1), with every other voice on output 2.

Its pattern 0 has voice 1 on steps with shifts −7..+7 and ratchets ×2, ×3, ×4, ×6 and ×8. It also
has a roll 1/32, a triplet roll, shuffle 40, track delay +3 ms, humanize 4 ms (seed 1) and a second
track at tempo ×3/4 routed to output 2. Hits are at least 20 ms apart on output 1.

| Id | Render | Pass |
|---|---|---|
| `G16_onsets` | the `onset` card, 120 BPM, 8 bars | a golden (hashes, `#expect summary vsched.late 0..0`), and `emu_onsets.py` on L1: spread ≤ 2 frames against the trigger log, and ≤ 2 against the tick grid with humanize off |
| `G16_onsets` with `--param trig_timing=1` | the same in `blk` | the "before": the spread is ≥ 32 frames (expected about 63). The value goes into the milestone report; the test only guards the measurement's sensitivity |
| `G17_midi_in` | the `onset` card stopped; DIN Note Ons on voice 1's channel at 20 irregular times over 2 s, then the same over USB in 1 ms transfers | spread ≤ 2 frames against the trigger log and against the script's time of each message's last byte (DIN) or transfer (USB) |
| `G18_slave_onsets` | the `onset` card as DIN slave, a steady 128 BPM clock, Start | from bar 2 on, spread ≤ 2 frames against the clock grid; `seqc.dropouts` 0 |
| `G1_blk`, `G7_blk`, `G8_120_blk`, `G11_dsync_blk`, `G13_reverb_blk` | G1, G7, G8_120, G11_dsync and G13_reverb with `--param trig_timing=1` | every entry (WAV, MIDI, clock) equals that golden's v0.4 entry (§9.3) |
| a card-save golden (G19_save) | the `onset` card playing, a pattern save at 2 s (TIM2 renders during the save) | `underrunsAfterT0 0..0`, `vsched.late 0..0` outside the save's blocks, spread ≤ 2 frames outside them |

`tools/emu_fuzz.py` runs its 1,400 renders under ASan and UBSan, as for v0.4, in both modes.

### 9.3 Re-blessing the goldens

**What changes.** Every one of the 23 goldens (SF10) plays voices, so all 23 change in `out.wav`:
- every onset moves by 0 to 64 frames, to event + 128;
- from the first block with a hit at k ≠ 0, the chunks differ;
- their `midi`, `clock` and `panel` entries must stay byte-identical. MIDI, clock and LED output keep
  their v0.4 timing (S8), and the emulator charges no CPU time.

G13–G15 are re-blessed first by v0.4's A7–A9 (the reverb at its final rate). v0.5 re-blesses after
merging them.

**The procedure.** The re-bless is one commit that changes `goldens.json` only. It happens after these
checks pass, each in the commit message or in the milestone report:
1. `emu_golden.py check` on the v0.5 build reports, for all 23 goldens, only `wav` mismatches: `midi`,
   `clock` and `panel` match, and every `#expect` passes.
2. The five `_blk` goldens pass against the v0.4 entries. The off path, and so the shared mixer code,
   is bit-identical; only the onset timing differs.
3. G16–G18 pass (spread ≤ 2 frames), and G16 in `blk` shows the v0.4 spread.
4. `emu_golden.py diff G1` puts the first differing frame at the first hit with k ≠ 0. The bless output
   shows each golden's old and new peak and RMS; the RMS is expected to move by less than 0.1 dB. A
   larger move is a finding to look at, not to bless.
5. `emu_golden.py bless` for the 23 ids. It refuses if an expectation fails or a broken variant
   passes.

**Drift items.** `HAL_I2S_TxCpltCallback` and `USBD_CDC_DataOut` change, and `MenuText.h` gains
`MENU_TRIG_TIMING`. `emu_codec.c` (the call to `audio_blockDone()`) and `emu_usb.c` (the stamp array)
follow, and then `drift.py record` runs.

## 10. Rollout

**Branch and version.**
- `v0.5` is created from `v0.4` at `91a61b5` in the worktree `build/wt-v05`, with `.toolchain`
  symlinked.
- `project(... VERSION 0.5)` gives the splash `v0.5 unofficial` and the file
  `LXR_V2_LICHOMAT_0.5.img`.
- v0.4's remaining work (A7–A11: the reverb at D41's rate, freeze, rumble, the release) is merged into
  `v0.5` when it lands. The v0.5 release follows v0.4's.
- Pushed to `origin` as work lands.

**The plan's task outline** (the plan file decides the details):
1. Worktree, baseline, and a re-count of §6 as A1 did. A difference over 10 % is a finding.
2. `seqc_itemTime()` and its host tests.
3. `vsched` (records, window, offsets, rings, cap) and its host tests.
4. `audio_blockDone()` in the firmware and `emu_codec.c`, the snapshot (S6), drift.
5. The MIDI stamps (DIN, USB, `emu_usb.c`), the parser stamp, drift.
6. The glue split (S8, S10), and `voiceControl_noteOn()` split in two.
7. The mixer: the per-voice helper, record and phase splits, the drum ramp, pan per segment, the LFO
   re-dispatch, phase 0, the cap. `dsp_split` and `mixer_segments`.
8. The `trg` setting (S18), `gen_params.py`, the `_blk` goldens.
9. The renderer's `--trig-log` and counters, `emu_onsets.py`, the `onset` card, G16–G19.
10. Re-bless (§9.3), demo renders `tools/demo_v05.sh` (v0.4's demos in `smp` and `blk`).
11. Version 0.5, the owner documents: U27–U32 in `hardware-checklist.md`, `release-notes-v0.5.md`,
    and a kit recipe for the click. Milestone report.

Review: E6.10 changes firmware sound code, so it gets the full process (an implementer, a spec review
and a quality review per task), within the week's usage cap.

### 10.1 Hardware checks (owner, on `LXR_V2_LICHOMAT_0.5.img`)

Every check needs the same recording setup:
- the LXR's main output recorded into a DAW at 44.1 or 48 kHz;
- for the MIDI checks, the DAW's MIDI on the same interface;
- a **click patch** on Drum 1: attack 0, the shortest decay, the transient sample at full volume, the
  filter off.

The release notes give the exact values; the kit recipe `click` is in `kit-recipes-v0.5.md`. Offsets
are read at sample level in the DAW: the first sample of each onset that rises clearly above the
noise.

#### U27: sequencer onsets

1. Internal sync, BPM 120, shuffle 0, all tracks 16 steps long. Voice 1 (the click) on all 16 steps.
   Set `trg smp` (global menu, SHIFT + step 12, second sub-page, after `cko`). Press START and record
   2 bars. Then set `trg blk` and record 2 bars.
2. Voice 2 with the same click (its own recipe copy), on steps 1, 5, 9 and 13, `dly` +1 (one tick,
   1.30 ms at 120 BPM). Voice 1 stays as in step 1. Record 2 bars with `smp`, then 2 bars with `blk`,
   and listen.

**Expected:**
- 1: with `smp`, 16 consecutive onsets are 125.0 ms apart within 2 samples, at either rate. With
  `blk`, the intervals vary by up to about 64 samples (1.45 ms).
- 2: with `smp`, each of voice 2's hits comes 56–58 samples (at 44.1 kHz) after voice 1's, and the
  flam sounds the same on every hit. With `blk`, the gap jumps between about 0 and 64 samples, and the
  flam flutters.

**Record:** the largest and smallest of 15 intervals in step 1 for `smp` and `blk`; the eight gaps of
step 2 for both.

#### U28: MIDI in

1. The DAW plays a clip of 16 notes on voice 1's channel to DIN in, with uneven gaps of 30–200 ms.
   Record the LXR with `smp`, then with `blk`.
2. The same over USB.

**Expected:** with `smp`, every onset is the same time after its note in the DAW: the spread of
(onset − note) is within the interface's own MIDI jitter, about 0.3 ms for DIN and about 1 ms for USB
(USB sends notes in 1 ms frames). With `blk` the spread is about 1.45 ms larger.

**Record:** the spread for each of the four runs, and the mean offset with `smp`. The DAW's MIDI
offset can be set to that mean.

#### U29: clock slave

1. The DAW sends MIDI clock at 128 BPM over DIN. The LXR is a MIDI slave with voice 1's click on all
   16 steps. Start from the DAW, record 4 bars with `smp`, then with `blk`.
2. The same over USB.

**Expected:** with `smp`, from bar 2 on, the onsets lie on the DAW's 16th grid with a constant offset
and a spread at the level of U28's. With `blk` the spread is about 1.45 ms larger.

**Record:** the spread and the offset for each run.

#### U30: CPU with E6.10

H12 step 3's kit, pattern and BPM 140, with the reverb at the rate the 0.4 release chose (U20) and the
delay in the slot.

1. `trg blk`, CPU page for 30 s.
2. `trg smp`, CPU page for 30 s.
3. Again with `smp`, after turning on LFO retrigger on voices 1–6 (each LFO retriggered by its own
   voice), CPU page for 30 s.

**Expected:** `pk` in step 2 is at most 2.5 points above step 1, and in step 3 at most 3.5 above. `pk`
stays below 95 and `ovf` 0, with no dropouts or clicks. If `pk` reaches 95, the fix-up build caps the
splits at 3 (S20) and U30 runs again on it.

**Record:** `CPU` and `pk` for each step, and the reverb's rate.

#### U31: sound

Listen with `smp`, then `blk`, to:
- a kick with the snap transient at a high transient pitch;
- a Drum 1 acid line with slide steps (v0.3);
- an LFO retriggered by its voice, as a pitch envelope;
- the open and closed hat on the same step and on neighbouring steps;
- p-locks on every step of a track;
- humanize 8 ms on the hats.

Switch `trg` while it plays.

**Expected:** the same timbre in both modes: the snap and the attack unchanged, no clicks at
retriggers, the open hat choked by the closed one as in 0.4. Switching makes no glitch.

**Record:** anything that differs between `smp` and `blk`.

#### U32: setting, files, card access

1. Set `trg blk` and save the global settings as in fw 1.7 (the save page's global type). Power off
   and on: the page shows `blk`. Set `smp` again.
2. Boot fw 1.7 (H13's way back) with the card, then 0.5 again: everything loads, and `trg` shows what
   was saved.
3. Save a pattern and a kit while a pattern plays (TIM2 renders during the save), then change the
   pattern with kit autoload on.

**Expected:** 1 and 2 as described. In 3, no dropout or stuck note, and the timing before and after
the save as in U27.

**Record:** pass or fail per step.

## 11. Risks

- **The estimate.** The count is static, with D41's factor, and it counts every path of every callee,
  so it should be high. The `midiParser_ccHandler()` path of an LFO re-dispatch (≈ 180 instructions)
  is the least certain item. U30 measures it, and S20 is the fix-up.
- **DMA restart slips** (SF2). If the non-circular restart often comes more than a frame late, the
  window follows each slip only after about 16 blocks. Onsets are then up to the slip off for 23 ms.
  The emulator cannot show this. U27's interval spread on the device shows it if it happens.
- **The phase split's second step.** Step 2 is shortened by φ, so a fast pitch sweep after its first
  step runs up to 1.4 ms early. The body of a kick then differs from hit to hit in phase by up to tens
  of degrees, while its spectrum stays the same. `dsp_split` (2) pins the first block, and U31 listens
  for the rest.
- **Cross-voice modulation** follows the mixer order (§4.5). A kit that p-locks or modulates one
  voice's parameter from another voice's track gets that change up to one block after the onset, as
  in v0.4.
- **ITCM** (2,368 B free). If the split code does not fit, `vsched_apply()` goes to flash (§7).
- **The constant latency** is 0.73 ms later on average than v0.4 against MIDI out and the clock out
  jack. It is stated in the release notes.

## 12. Out of scope

- A sample-accurate clock out jack. `seqc_itemTime()` would allow it later, through a timer compare
  instead of the GPIO write in the callback.
- Lookahead in the core to cut the latency below two blocks.
- A per-voice `trg` mask.
- Sub-sample (fractional) onsets.
