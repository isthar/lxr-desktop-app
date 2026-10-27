# LICHOMAT stage 1b: macOS emulator

| | |
|---|---|
| Date | 2026-10-27 |
| Status | Draft for the owner's review |
| Base | `timing-core` at `da64603` |
| Branch | `emulator` (created on acceptance) |

## 1. Context

LICHOMAT is unofficial custom firmware for the Erica Synths LXR-02 (STM32F765, GPL-3.0-only with the
additional terms in NOTICE.md). It is aimed at groove techno. The owner uses the LXR as clock master,
and as a MIDI clock slave over DIN and over USB.

Stage 1 is done on `timing-core`: the timing core `seq_core`, the sync and MIDI fixes, host tests, CI and
the CPU meter. It waits for the owner's hardware checks (tracker Q6). Its spec is
`2026-10-23-timing-core-design.md`, and its notes N1–N21 win over that spec.

Stage 1b is a macOS emulator built from the same sources. It comes in two tiers:

1. **Tier 1, `lichomat-render`.** A headless renderer that writes WAV files and MIDI logs. CI uses it
   for golden-render tests. Stage 2's groove changes can be heard with it before they reach the device.
2. **Tier 2, `LICHOMAT Emu`.** A standalone app with a virtual panel, CoreAudio and CoreMIDI. It runs
   as clock master and as MIDI clock slave.

Earlier desktop ports of the LXR-02 run the sound engine alone. This emulator runs the whole firmware,
so the menu, the sequencer and MIDI sync behave as on the device.

**Where the design comes from.** A context study on 2026-10-26 had six readers work through the
firmware: audio, panel, storage, MIDI, platform, and prior art with licence. A synthesis merged their
findings into a portability map. This spec takes the map's measured facts, cuts and defaults, and
restates what it uses. The map and the readers' prototypes are kept outside the repository. Facts that
this spec adds were checked against the code at `da64603` and are marked *checked*.

**Measured** on an Apple M4 Pro with macOS 26.7.1 and Apple clang 17.0.0 from the Command Line Tools
16.4. Nothing was measured on the device.
- A probe linked 68 firmware `.c` files with 509 lines of emulator code. It boots to the splash and the
  voice page, takes Note Ons through the unchanged USART3 handler, and writes deterministic 4-channel
  audio at 65–190× real time. With the real FatFs on a RAM disk, the SD boot path runs too.
- The panel, storage and MIDI readers ran their subsystems unchanged on register models in the same way.
- Renders are bit-identical between host builds with strict floating-point flags (§8). They are never
  bit-identical with the device.
- Re-run for this spec at `da64603`: the same 68 files compile and link, and 12 s of virtual time
  render in 0.2–0.4 s. Strict renders at `-O0` and at `-O2` are byte-identical (SHA-256 starting
  `f13d248ab55dd20b` for both).

**Device bugs that a host build trips over.** The study found four. One, the kit load's out-of-bounds
write in `MidiParser.c`, is fixed in stage 1 (E4.5, `2680991`). Three remain, and §7 fixes them:
- `modulationNode.c:49` reads through a NULL pointer. A change of parameter 0 (`PAR_NONE`, CC 1) reaches
  it through `MidiParser.c:904` for every modulation node without a destination, which is the state
  `modNode_init()` leaves them in (*checked*).
- `morphParameters` stays NULL after a boot without a card. `preset_morph_tick()` then reads through it.
- `strcpy()` at `menu.c:1079` and `:1083` writes 11 bytes into the 10 that row 0 of
  `editDisplayBuffer[2][17]` has left from column 7 (*checked*). The macOS fortify checks trap on it.

macOS cannot map page 0, so the two NULL reads are segfaults there (measured). On the device, address 0
is ITCM and readable. All three are harmless on the device today.

## 2. Goals and non-goals

### Goals

- **G1.** The whole LICHOMAT firmware runs on macOS: boot, menu, sequencer, sync, MIDI, storage and
  sound. Its source files compile unchanged on the emulator branch. The emulator replaces only the
  drivers at the hardware edge (§3).
- **G2.** `lichomat-render` turns a card folder, options and an event script into a 4-channel WAV, a
  MIDI-out log and a clock-out log. The same inputs give byte-identical outputs on every run and on every
  supported Mac (§5, §8). It renders at least 10× faster than real time on an Apple silicon Mac.
- **G3.** CI renders a set of goldens on every push and fails on any difference (§8, §9).
- **G4.** `LICHOMAT Emu` plays in real time with a virtual panel. It works as MIDI clock master and as
  MIDI clock slave, over a virtual USB port and over a DIN port that the user picks, with a constant and
  displayed latency (§6).
- **G5.** Sync and MIDI go through the firmware's own interrupt handlers, queues and decoders, so the
  emulator runs stage 1's tested path (§4.3–§4.5).
- **G6.** The firmware changes the emulator needs are small, separate commits. They leave the device's
  behaviour unchanged, with two exceptions. A morph without a card no longer uses stray bytes (F2). Band
  10 of the wavetable oscillators changes its sound, so that change waits for a listening check (F9, §7).
- **G7.** The binaries contain no ST SLA0044 code and no file without a licence. They carry no Erica
  Synths name, logo or faceplate, and they show that they are unofficial (§10).

### Non-goals

- Sample-for-sample identity with the device. The aim is 'very close', and goldens pin host builds only
  (§8).
- A model of the CPU budget. The CPU page reads about 0 %, and the emulator shows no CPU-induced
  underruns or late steps. H12 on the device stays the reference for stage 3.
- A model of SD-card access time. A kit or a project loads in zero virtual time.
- The format dialog (`sd_formatRoutine()`). It spins on the encoder without calling the HAL, so it
  cannot end in one thread. The emulator never presents an unformatted card (§4.8).
- A FAT image backend, the device's display speed, audio input for sound, Intel Macs, notarisation,
  and Windows or Linux binaries. §16 lists the later tiers.
- Changes to the sequencer, the sound or the file formats. The F8-behind-Note-Ons delay on DIN stays as
  it is (Q17).

## 3. Architecture

```
 firmware sources on the emulator branch: 69 files of Core/Src unchanged, plus appMain.c
   │  stub headers, register and HAL model, a clang overlay for main.h
   ▼
 virtual device (emu/core, C, one firmware thread)
   virtual clock (216 MHz cycles) · scheduler (interrupt bodies, main-loop passes, inputs)
   codec · LCD · USB endpoint · card backend · panel · MIDI wire · jacks · random streams
   │  inputs in: panel events, DIN bytes, USB transfers, jack edges
   │  sinks out: audio blocks, DIN bytes, USB transfers, clock-out edges, LCD and LED state
   ├──► tier 1: lichomat-render    script in; WAV and logs out; one process per render
   └──► tier 2: LICHOMAT Emu.app   CoreAudio, CoreMIDI, Dear ImGui panel, its own card folder
```

### 3.1 Execution model

The whole firmware runs on a virtual device. Only the drivers at the hardware edge are replaced (§3.4).
Everything above them runs unchanged on a model of the registers and the HAL.

- **One firmware thread.** Boot, the main loop, every interrupt body, `seq_tick()` and the render run on
  one host thread. All six readers recommend it, for four reasons:
  - the firmware has no locks;
  - its SPSC sync queues, the USB ring and the MIDI FIFOs order their accesses with compiler-only
    fences or `volatile`;
  - it relies on PRIMASK sections in `sequencer.c`, `usb_manager.c` and `encoder.c`;
  - its two front-panel byte parsers are not reentrant.
- **One virtual clock.** It counts 216 MHz CPU cycles in 64 bits from power-on. Every view of time
  derives from it (§4.2).
- **Time passes only in the scheduler.** Firmware code takes no virtual time, with two exceptions in
  thread context: `HAL_Delay()` and `HAL_GetTick()` (§4.2). Inside an interrupt body, time stands still.
- **Interrupts are scheduled bodies.** The scheduler runs each interrupt body at its device rate on the
  firmware thread, in NVIC priority order at equal times. PRIMASK defers them.
- **The main loop is a series of passes.** A pass is one call of `appMain_loopOnce()`, the body of
  `main.c`'s `while (1)` loop moved into a function (§7, F4). The scheduler runs a pass at every audio
  block request and every 250 µs in between.
- **Audio comes in blocks.** The firmware renders 64-frame blocks at 44,140.625 Hz through its own
  handshake (§4.1). In the app, rendered frames pace the clock. The renderer runs free.
- **Rings to the outside.** CoreAudio, CoreMIDI and the GUI exchange data with the firmware thread only
  through lock-free single-producer, single-consumer rings. The firmware thread fills and drains them at
  scheduler points.

### 3.2 Departures from stage 1 spec §15

| §15 says | This spec | Why |
|---|---|---|
| The emulator supplies its own platform layer instead of `seq_platform.c`, and time comes from the audio callback | `seq_platform.c` runs unchanged. `DWT->CYCCNT` is the virtual clock, which advances with rendered audio | Measured (platform and MIDI readers): the unchanged layer then takes its time from the audio clock, and its stamps and queues stay in the tested path |
| Sync events come from CoreMIDI | They enter through the firmware's own interrupt paths: `USART3_IRQHandler()`, the USB DataOut walk and the EXTI handlers, each at its exact virtual time | The decoders, the SPP and Program Change interception (N14) and the 64-entry queues stay the tested code |
| `lcd.c`, `DIO.c`, `adcPots.c` and `encoder.c` are replaced by virtual drivers | Only `lcd.c` is replaced. The other three run unchanged on registers, SPI and ADC memory | Measured: both cuts gave byte-identical display, LED and front-panel protocol output. The register cut keeps the drivers' scan, edge and hysteresis behaviour |
| The DSP is built with C shims for the CMSIS intrinsics | The shims, plus strict floating-point flags, vendored libm functions and source fixes (§7, §8) | Measured: without them two NULL reads crash, and renders are not deterministic across builds |
| (implied) Drivers are replaced function by function around the engine and the UI | The whole firmware runs on a virtual device: boot, main loop and interrupt bodies, one thread, one clock | The global settings reach the engine only through `menu.c`, and the engine and the UI call each other directly. A DSP-only build needs about 30 stubs, which drift from the real UI |
| The CPU budget is not modelled | Unchanged | The CPU page reads about 0 %, and no CPU-induced underrun or late step appears |

### 3.3 Modules

Everything under `emu/` is new emulator code, written for this project, under GPL-3.0-only.

| Path | Contents |
|---|---|
| `Core/sources.cmake` | The module source lists, moved out of `Core/CMakeLists.txt` so that the emulator uses the same lists (§7, F8; §9.1) |
| `Core/Src/appMain.c`, `Core/Inc/appMain.h` | `appMain_boot()` and `appMain_loopOnce()`, moved out of `main.c` (§7, F4) |
| `emu/CMakeLists.txt` | The host CMake project (§9.2) |
| `emu/stubs/` | Stand-in headers: the HAL and CMSIS (`stm32f7xx_hal.h`, `stm32f7xx.h`), the USB class (`usbd_def.h`, `usbd_cdc.h`, `usbd_cdc_if.h`), the FatFs glue (`fatfs.h`, `ffconf.h`) and the LCD (`lcd.h`) |
| `emu/overlay/` | A stand-in `main.h` and the clang overlay file that puts it in place of `Core/Inc/main.h` (§3.4) |
| `emu/core/` | The virtual device in C, behind one header, `emu_core.h`: clock and scheduler, registers and HAL, codec, LCD, USB endpoint, card backend, panel, MIDI wire, jacks, random streams, faults |
| `emu/libm/` | `sinf`, `cosf`, `expf`, `exp` and `logf` from Arm Optimized Routines, under its MIT licence (§8.1) |
| `emu/render/` | `lichomat-render` (§5) |
| `emu/app/` | `LICHOMAT Emu.app`: Objective-C for Cocoa, CoreAudio and CoreMIDI, C++ for the Dear ImGui panel (§6) |
| `emu/third_party/imgui/` | Dear ImGui (MIT) at a pinned release tag, with its Cocoa and Metal backends |
| `emu/tests/` | Host tests, golden scripts and hashes, card fixtures and their generator (§8, §12) |
| `tools/emu_golden.py` | Compares and re-blesses goldens (§8.5) |
| `.github/workflows/emulator.yml` | The emulator's CI (§9.3) |

`emu_core.h` is the only interface the two tiers use. It offers:
- power-on with a configuration: the card folder, the card switch, the jack switches, the fader levels
  and the seed;
- running the clock up to a given virtual time;
- injecting timed inputs;
- sink callbacks for audio blocks, DIN bytes, USB transfers and clock-out edges;
- snapshots of the LCD and the LEDs;
- counters.

Every call happens on the firmware thread.

### 3.4 What runs unchanged, and the stub layer

**Compiled unchanged** (on `emulator`, with §7's changes): 69 of the 78 `.c` files that `Core/Src` has at
the base, plus `appMain.c`. They include `DIO.c`, `encoder.c`, `adcPots.c`, `Uart.c`, `TriggerOut.c`, `usb_manager.c`,
`usb_midiTx.c`, `seq_platform.c`, `seq_syncq.c`, `seq_core.c`, `sequencer.c`, the menu, the preset
manager and the whole DSP, dead code included.

**Not compiled.** The virtual device does what these did:

| Firmware files | Replaced by |
|---|---|
| `Core/Src/lcd.c` | The LCD model at the `lcd.h` API (§4.7) |
| `Core/Src/Soundengine/cs4344_cs5343.c` | The codec model (§4.1) |
| The ST USB device library (`Middlewares/JS/`) and `USB_DEVICE_MIDI/` | The USB endpoint stand-in. `usb_manager.c` stays (§4.3, §4.4) |
| The FatFs core, `FATFS/App/` and `FATFS/Target/` (the SD card BSP) | The card backend at the FatFs API (§4.8) |
| `Core/Src/main.c` (its CubeMX init and `main()`) | Power-on in the virtual device; boot and passes from `appMain.c` (§4.10) |
| `Core/Src/stm32f7xx_it.c` | The scheduler's interrupt bodies (§4.2) |
| `stm32f7xx_hal_msp.c`, `system_stm32f7xx.c`, `syscalls.c`, `sysmem.c`, `flash_if.c`, `Core/Startup/`, `Drivers/` | The register and HAL model |

**Why `usb_manager.c` stays** (map question E6; the readers disagreed). It holds stage 1's transmit ring,
its transfer start and its stall rule (N20). The MIDI reader ran it end to end on a 40-line endpoint
stand-in. Replacing it with a CoreMIDI module, as the platform reader proposed, would take that tested
code out of the emulator.

**The stub layer.** 71 of the 78 `Core/Src` files include the HAL or CMSIS headers indirectly.
- `emu/stubs/` comes first on the include path. `Core/Inc` is a system include directory.
- The stand-in HAL header supplies:
  - the `<stdint.h>` and `<stddef.h>` types, and the handle types;
  - the register blocks as plain memory: DWT, CoreDebug, FPU, EXTI, GPIOA–E, USART3, TIM2 and TIM14;
  - the HAL macros the firmware uses (`__HAL_TIM_*`, `__HAL_UART_*`, `__HAL_GPIO_EXTI_*`, `READ_REG`),
    which call the virtual device where the scheduler must learn of a write;
  - the CMSIS compiler macros, and the intrinsics in C (§8.3);
  - PRIMASK functions that call the scheduler;
  - `#undef __weak`, because Apple clang predefines `__weak`.
- It declares only the registers the firmware uses. New firmware code that touches another register
  fails to compile in the emulator, which flags it.
- **Shadowing limits.** A quote include searches the including file's directory first. So the stub
  directory shadows only headers that sit outside `Core/Inc`, or that only `.c` files include. That holds
  for `lcd.h`, `fatfs.h`, `ffconf.h` and the USB headers (*checked*).
- **`main.h`** is included by 13 headers in `Core/Inc`, so it cannot be shadowed that way. It carries an
  ST SLA0044 notice (*checked*; the map had it as unlicensed). The firmware objects are compiled with
  clang's `-ivfsoverlay`, which maps `Core/Inc/main.h` to `emu/overlay/main.h`, written fresh with the
  same names. Measured at `da64603`: the 68 probe files compile with the overlay, and the render is
  byte-identical to the one with the original header.
- `delay.h` and `encoder.h` are included from `Core/Inc` headers too. They are GPL and stay. Their DWT
  and GPIO macros work on the register model.
- **TCM placement** is neutralised by `-Dsection(x)=unused` on the firmware objects only. Mach-O rejects
  `.dtcm`, `.dtcmz` and `.itcm_text` (217 errors over the DSP, measured). The emulator's own sources,
  which include Apple framework headers, never get this define.
- **Object paths mirror source paths.** `DSPAudio/distortion.c` and `DSPAudio/FX/Distortion.c` collide on
  a case-insensitive file system. A flat object directory silently lost one of them (measured).

## 4. The cuts

### 4.1 Audio out

**What the firmware keeps.**
- `soundengine_process()` (`Soundengine.c:79–118`) masks the TIM2 update interrupt and runs the CPU meter
  around `seq_tick()` and `soundengine_calcNextSampleBlock()`. It then parks TIM2 at CNT 1950, unmasks
  it, and runs `usb_tick()` and `preset_morph_tick()`.
- `soundengine_calcNextSampleBlock()` renders only while `bCurrentSampleValid` is not `SAMPLE_VALID`
  (0xFF). It calls `mixer_calcNextSampleBlock(&dma_buffer[h·128], &dma_buffer2[h·128])` for the free half
  h, then marks the half valid.
- The render runs from five places: the main loop, 3 times per pass; the immediate kit load, after every
  parameter (`presetManager.c:1345`, *checked*); `preset_loadDrumsetRam()`, before and after its copy
  (`:836`, `:852`); the screensaver's wait; and TIM2, while it is started.

**The codec model** replaces `cs4344_cs5343.c` and its `HAL_I2S_TxCpltCallback()`.
- `codec_initCsCodec()` starts the block requests. Before that call, no block request exists.
- Block request k, for k = 1, 2, …, comes at the codec start plus ⌊64·k × 552,960 / 113⌋ cycles, that is
  every 64 frames. It does what both I2S branches of the callback do:
  1. it counts an underrun when `bCurrentSampleValid` is not `SAMPLE_VALID`;
  2. it advances the DMA pointer and passes the half that starts playing now to the sink;
  3. it frees the other half: `bCurrentSampleValid = 1 − (dmaPtr & 1)`;
  4. it sets TIM2's CNT to 1450.
- The half rendered at request k starts playing at request k+1.
- The sink receives 64 interleaved int16 stereo frames per output and block. Out 1 is `dma_buffer2`
  (DAC1, L1 and R1), and out 2 is `dma_buffer` (DAC2, L2 and R2). The argument order of
  `mixer_calcNextSampleBlock(dac2, dac1)` is a trap, and a test pins it (M5).
- `dma_buffer` and `dma_buffer2` are 4-byte aligned (F7), because the mixer accesses them as `int32_t`.

**Rules.**
- The 64-frame block and the handshake stay:
  - control rate equals block rate;
  - kit loads render nested blocks and rely on the handshake;
  - rendering more or less often than once per request would shift the audio by a block.
- The rate is 44,140.625 Hz: PLLI2S runs at 16 MHz / 8 × 113 / 5 = 45.2 MHz, divided by 1024. The DSP
  assumes `REAL_FS` = 44,140 Hz.
- There is no audio input. `USE_DAC2` puts a second CS4344 in place of the ADC.

**Not reproduced.** The one-sample click at boot (`CodecInit()` writes `dma_buffer[0] = 32756`), and the
swapped buffer in each I2S's first DMA transfer.

### 4.2 Time and scheduling

**The clock.**
- `cycles` is a 64-bit count of 216 MHz cycles since power-on.
- Frames convert exactly: cycles = ⌊frames × 552,960 / 113⌋, since 44,140.625 Hz is 353,125/8 Hz. One
  64-frame block is 313,180.88 cycles.
- `DWT->CYCCNT` always holds the low 32 bits of `cycles`, and `seqp_now()` extends it. The scheduler
  calls into the firmware at least once per block, far within the 2^32-cycle (19.9 s) limit.
- `HAL_GetTick()` returns ⌊cycles / 216,000⌋. `HAL_RCC_GetHCLKFreq()` returns 216,000,000, which
  `SEQC_CPU_HZ` and the CPU meter also assume.
- **Time passes only in thread context:**
  - `HAL_Delay(ms)` advances the clock by ms × 216,000 cycles;
  - each `HAL_GetTick()` call advances it by 216 cycles (1 µs);
  - the interrupt bodies that fall due meanwhile run, deferred while PRIMASK is set;
  - inside an interrupt body, neither call advances the clock.
- `HAL_GetTick()` advances because the firmware polls it in loops that must end. `screensaver_touch()`
  waits at `screensaver.c:383`. With 1 µs per call, the measured waits took 465 ms and 361 ms.

**Interrupt bodies.** The bodies of `stm32f7xx_it.c` and of the I2S callback are short call lists. They
are re-implemented from these lists, because their files are ST-licensed or replaced. A drift test
hashes those bodies in the firmware and fails when one changes (M20).

| Body | NVIC priority | When, in virtual cycles | What it does |
|---|---|---|---|
| SysTick | 0 | every 216,000 (1 kHz) from power-on | `screensaver_timer++`; applies the next queued encoder change (§4.6); `encoder_callback()` |
| TIM14 | 0 | every 54,004 (3,999.7 Hz) after `HAL_TIM_Base_Start_IT(&htim14)` | `systick_ticks++`; then `menu_tick()` 4 times while `lcd_process_display_interrupt` is set |
| USART3 RX | 3 | at each DIN byte's time (§4.3) | sets `USART3->RDR` and ISR.RXNE; `USART3_IRQHandler()` |
| USART3 TX | 3 | every 69,120 (320 µs) while CR1.TXEIE is set | sets ISR.TXE; `USART3_IRQHandler()`; passes `USART3->TDR` to the DIN sink |
| OTG_FS out | 3 | at each incoming USB transfer's time (§4.3) | the endpoint stand-in's DataOut walk |
| OTG_FS in | 3 | 216,000 (1 ms) after `CDC_Transmit_FS()` | clears TxState; `usb_midiTxComplete()` |
| EXTI4, EXTI9_5 | 3 | at each clock-in or reset-in edge (§4.5) | sets the pending bit; the handler in `TriggerOut.c` |
| TIM2 | 4 | when its counter overflows, while started with DIER.UIE set | `soundengine_cpuBegin()`; `seq_tick()`; `soundengine_calcNextSampleBlock()`; `soundengine_cpuEnd()` |
| Block request | 5 | every 64 frames after the codec start | §4.1 |

- **TIM2** is a 32-bit counter at 54 MHz (4 cycles per count) with ARR 1500, so it overflows every 6,004
  cycles (35.98 kHz). `__HAL_TIM_SetCounter()` reaches the model.
  - CNT 1450 at a block request makes it fire 204 cycles later.
  - CNT 1950 at the end of `soundengine_process()` parks it past the next request.
  - The model follows every start and stop. The storage scenario counted 13 starts and 10 stops, and
    `loadTmpFolderFiles()` leaves TIM2 started on success.
- **Order.** Events at equal times run by priority, 0 first. At equal priority they run in the table's
  order, then in the order they were queued. A pass runs after every body due at its time.

**Main-loop passes.**
- A pass is one call of `appMain_loopOnce()`. In order, it runs:
  - the SD hot-plug check (`BSP_SD_IsDetected()`, then a remount);
  - `adc_checkPots()` 4 times and `soundengine_process()` 3 times, interleaved as in `main.c`;
  - `buttonHandler_tick()`, `menu_parseEncoder(encode_read4(), encode_readButton())`,
    `led_tickHandler()`, `screensaver_check()`, `dio_process()`, `adc_checkFaders()` and
    `preset_processTimer()`.
- The first pass runs when `appMain_boot()` returns.
- After that, the scheduler runs a pass in two cases:
  - right after each block request that does not fall inside a pass;
  - at the first multiple of 54,000 cycles (250 µs) after the previous pass ended.
- A pass due for both reasons runs once. Both tiers use the same rule.
- So a block renders at its request time. The half rendered at request k holds the triggers that
  `seq_tick()` fired up to that time.
- `seq_tick()` runs at least 3 times per pass, about 20 times per block. MIDI out and the clock-out
  edges therefore fall on a 250 µs grid. Nobody has measured the device's main-loop rate.

**Why this rate** (map question B9; the readers disagreed). The MIDI reader called `seq_tick()` once
per block. That gives MIDI out and clock out up to 1.45 ms of jitter. It also lets USB input back up:
`seq_tick()` parses at most 8 USB messages per call, and a USB host can send about 23 packets in one
block's time. A pass every 250 µs keeps both close to a fast main loop. The probe ran passes at this rate
at 65–190× real time. One rule for both tiers makes the app play what the goldens pin.

**Busy-waits.**
- `HAL_Delay(1000)` at boot ends because time passes. So does `HAL_Delay(2000)` in `menu_showSdError()`
  (`menu.c:2039`); TIM2 is started there, so audio keeps rendering in TIM2.
- The screensaver's `HAL_GetTick()` polls end because time passes.
- `while (sendDisplayBuffer() != 0)` (`menu.c:2000`, `screensaver.c:274`, `presetManager.c:2307` and
  `:2679`) ends because the virtual LCD finishes each call at once.
- `sd_formatRoutine()` spins on `encode_readButton()` without the HAL. It is unreachable, because the
  card backend never reports `FR_NO_FILESYSTEM` (§4.8).
- A wall-clock watchdog catches any other spin. The firmware thread counts its scheduler entries, and no
  progress for 10 s of wall time is a hang (§11).

**Not modelled.** CPU time, so hardware check H7's 'stays in sync during SD save' cannot be reproduced.
SD access time. The spread-spectrum modulation of the main PLL.

### 4.3 MIDI in

**DIN.** One byte takes 320 µs of wire time at 31,250 baud.
- At each byte's time, the scheduler sets `USART3->RDR` and ISR.RXNE and calls `USART3_IRQHandler()`,
  which lives in `Uart.c` and runs unchanged.
- The firmware stamps the byte, decodes realtime messages, SPP and Program Change into the DIN sync
  queue (N14), and pushes the byte into `fifo_midiRx`. That FIFO holds 256 bytes and drops new ones
  silently when it is full.
- Input is paced like a wire. The bytes of one input event are 320 µs apart from its time, and a byte
  never starts before the previous one has ended. So a burst cannot reach the FIFO faster than the
  device's UART could deliver it.

**USB.** One incoming transfer is one OTG_FS out body, at the transfer's time.
- The endpoint stand-in walks its 4-byte USB-MIDI 1.0 packets on cable 0, up to the first packet whose
  first byte is 0.
- For each packet with a CIN of 0x2–0xF it calls `seqp_usbMessageIn(cin, status, d1, d2, stamp)` and
  appends the message to `usb_MidiMessages[64]`. A full ring drops the message without counting it, as
  `usbd_cdc.c` does.
- A transfer carries at most 16 packets (64 bytes). Longer input is split into transfers 1 ms apart.

**What the firmware keeps** (stage 1, unchanged):
- `seq_tick()` drains the sync queues first. It then parses at most 32 DIN bytes and 8 USB messages per
  call.
- A Program Change acts only with `MIDI_FILTER_PROG_C` on, and only on the global channel.
- `MIDI_FILTER_SYSTEM` gates the DIN and USB sync entries, not the jack.
- Notes and CCs act at parse time, without a stamp. Thru routing also happens at parse time.

**Rules for both tiers.**
- No event enters before its time. An event with a future stamp is held.
- Stamps are kept. In the app the time comes from the CoreMIDI timestamp, not from the callback (§6.3).
  Stamping at callback time would turn clocks into one transfer per buffer. By N21's tables that costs
  a tempo error of up to 0.97 %, 1.35 % and 2.80 % at 256-, 512- and 1024-sample buffers.
- After a stall of the audio output or a jump of the time map, input events older than 100 ms are
  dropped and counted. Much older stamps would also fall outside the 9.94 s unwrap window.

### 4.4 MIDI out

**What the firmware sends** (unchanged):
- every message to both ports, filtered by the high nibble of `midiParser_txRxFilter`;
- a Note On per trigger, on the voice's channel, and its Note Off 4 ticks later;
- F8 every 16 ticks of the free-running timeline in internal sync, also while stopped;
- Start, Stop and Continue in internal sync;
- a Program Change on a pattern change, on the global channel.

MIDI out also comes from `seq_setMute()`, from `seq_setNextPattern()` while stopped, and from a MIDI
channel change in `CortexParser.c`.

**DIN.**
- `uart_sendMidi()` writes into `fifo_midiTx` (256 bytes) without running status, then enables TXE.
- While TXEIE is set, a TX body every 320 µs collects one byte. A byte's time is the time of the TX body
  that collected it.
- This keeps a property of the device. At equal ticks `midiClockOut()` runs last, and the FIFO is strict,
  so an F8 waits 0.96 ms behind each Note On of its tick. With 7 tracks on one step that is 6.7–7.0 ms.
  This was measured on the host model, and code reading says the device behaves the same. Q17 keeps it.

**USB.**
- `usb_sendMidi()` (unchanged, N20) queues USB-MIDI packets in its 128-packet ring. When the endpoint is
  free, it starts a transfer of up to 15 packets through `CDC_Transmit_FS()`.
- The stand-in passes the transfer to the USB sink at that time. TxState stays 1 for 1 ms, then the
  OTG_FS in body runs.
- `hUsbDeviceFS` is always in the configured state, so the 100 ms stall rule never fires.

**Sinks.** Tier 1 logs each DIN byte and each USB transfer with its time (§5.5). Tier 2 sends them at
their presentation time (§6.5).

### 4.5 Sync jacks

- **Clock in** is PD4, on the falling edge. At the edge's time the scheduler sets EXTI PR bit 4 and calls
  `EXTI4_IRQHandler()` in `TriggerOut.c`. The handler pushes `SEQC_EV_PULSE` only in sync mode 1. The PPQ,
  4 or 24, reaches the core through `seq_setClockInPpq()`.
- **Reset in** is PD5, on both edges, inverted. The scheduler sets `GPIOD->IDR` bit 5 to the pin level
  (low means RESET_ON), sets EXTI PR bit 5 and calls `EXTI9_5_IRQHandler()`. The reset acts as a run gate
  (N4).
- **Clock out** is PC13, and the firmware writes `GPIOC->ODR` directly.
  - The virtual device samples bit 13 after every body and every pass, and reports each change as an
    edge at that time.
  - A pulse that starts and ends within one body or pass is lost. That happens only when
    `seqc_process()` catches up several pulses in one call; on the device such a pulse would be a few
    microseconds wide.
  - While the sequencer runs, a pulse comes every 4 ticks when the prescaler selects it (2, 4, 8, 16, 24
    or 48 PPQ), and lasts one 96-PPQN pulse.
- The reset out and the 7 voice trigger outs compile to nothing (`VOICE_TRIGGER_OUTS 0`).

### 4.6 Panel in

The panel sits at the hardware edge of the unchanged `DIO.c`, `encoder.c` and `adcPots.c`. Each of their
input paths compares with its previous poll. So the panel model queues events and releases them at the
poll points, within these rates:

| Control | Register edge | Release rule |
|---|---|---|
| Buttons 0–39 | the 5 bytes that `HAL_SPI_Receive(&hspi1, …, 5)` returns at each `dio_process()`; button n is bit n%8 of byte n/8, 1 = pressed | at most one transition per button per scan, which is once per pass |
| Button 40 | `GPIOB->IDR` bit 7, 1 = pressed | the same |
| Encoder turn | `GPIOE->IDR` bits 13 and 14, Gray code, forward 00 → 01 → 11 → 10 | one change per SysTick (1 ms); 4 changes per detent; at most 4 detents queued, further ones dropped and counted |
| Encoder push | `GPIOE->IDR` bit 15, active low | held at least 40 ms; the panel holds a click for 50 ms |
| Knobs 1–4 | `adc_buf[0..7]`, two wipers per knob as 12-bit values shifted left by 4 (CH8/9, CH14/15, CH12/13, CH10/11) | at most 1° per pass |
| Faders 1–6 | `adc_buf[8..13]`, a level l as ⌊l × 4095⌋ shifted left by 4 | at once |
| Output jacks | `GPIOD->IDR` bits 6 (L1) and 7 (R1), `GPIOB->IDR` bits 4 (L2) and 6 (R2); 1 = plugged | read by the render at the start of each block (`mixer.c:551–554`) |
| Card switch | `BSP_SD_IsDetected()` | the main loop remounts on insertion |

- **Buttons.** `dio_process()` does no debouncing, and `buttonHandler.c` reads the held state of COPY,
  SHIFT, MODE3 and MODE4 straight from the input bytes. The numbers (`buttonHandler.h`; the LEDs use the
  same):

  | Numbers | Controls |
  |---|---|
  | 0 | bar+ |
  | 1–7 | voices 1–7 |
  | 8–11 | select 5–8 |
  | 16–19 | select 1–4 |
  | 12–15, 20–23, 28–31, 36–39 | steps 16–13, 12–9, 8–5, 4–1 |
  | 24–27 | MODE1–MODE4 |
  | 32–35 | SHIFT, START/STOP, REC, COPY |
  | 40 | bar− |

- **Encoder push.** Measured over 10 sampling phases: a 40 ms push was seen in 10, 35 ms in 8, and 30 ms
  or less in none.
- **Knobs.** Knob i edits the parameter shown in display column i. The wiper model is two triangle waves
  90° apart, wiper a peaking at 90° and wiper b at 180°; it is unverified on the device. Measured under
  it: 1° per poll gives 134 steps per turn on a normal parameter and 22 on a list parameter; 5° per poll
  gives 71; a 40° jump gives one step.
- **Faders.** Fader i sets the gain the voices read as `adc_getFaderVolume(i−1)`: drums 1–3, the snare,
  the clap (the cymbal voice), and both hi-hats. `adc_checkFaders()` reads one fader per call,
  round-robin, with a hysteresis of 40 counts. The gain is (v/4096)², 0 below 1 %, so full scale gives
  0.99951. The voices smooth it with a one-pole filter.
- **Output jacks.** A voice whose jack is unplugged moves to another plugged output. With no jack
  plugged, the routing is unchanged. The physical polarity of the detect pins is unverified.

### 4.7 Panel out

**LCD.** The LCD model implements the `lcd.h` API from scratch, because `lcd.c` and `lcd.h` carry no
licence. A fresh `lcd.h` in `emu/stubs/` declares the same functions.
- It provides `lcd_process_display_interrupt` and: `lcd_init`, `lcd_showSplash` (empty, as on the
  device), `lcd_clear`, `lcd_clear_nonblock`, `lcd_home`, `lcd_setcursor`, `lcd_setcursor_nonBlock`,
  `lcd_data`, `lcd_data_nonBlock`, `lcd_string`, `lcd_command`, `lcd_command_nonblock`, `lcd_turnOn`,
  `lcd_turnOn_nonblock`, `lcd_generatechar`, `lcd_generatechar_nonBlock`, `_delay_us` and `_delay_ms`.
- It models an HD44780 or WS0010 controller: DDRAM with row 1 at 0x00 and row 2 at 0x40 (16×2 visible),
  64 bytes of CGRAM, the address counter in DDRAM or CGRAM mode, the entry direction, the display, cursor
  and blink bits, the font-table bits and the power bit.
- Every call completes at once, and the non-blocking calls return 1. A full repaint through
  `menu_tick()` then takes 6.25 ms of virtual time. The device needs about 51 ms, or 1.6 ms per changed
  cell; that speed is not modelled (Q16).
- Snapshots come from the model, never from `menu.c`'s buffers. The boot, SD errors, load and save
  progress, and the screensaver write to the display directly.
- A snapshot holds the 2×16 character codes, the 64 CGRAM bytes, the display, cursor and blink flags,
  and the cursor position.
- **Character set** (Q18): printable ASCII from a generic 5×8 font; 0xC5 and 0xC6 as up and down arrows,
  which the song page uses; codes 0–7 drawn from CGRAM; any other code as a hollow box. CGRAM character
  0 is programmed blank, because a `'\0'` can reach the display.
- The underline cursor appears only while a name is edited on the save page.

**LEDs.**
- The 5 bytes that `HAL_SPI_Transmit(&hspi1, …, 5)` sends at each `dio_process()` are latched. LED n is
  bit n%8 of byte n/8, and 1 means lit.
- LED 40 is `GPIOB->ODR` bit 8.
- Voice LEDs pulse for 50 ms, and blinking follows `seq_core`'s tempo step. So a snapshot keeps, per
  LED, 'on at any time since the last snapshot'.

### 4.8 Storage

The cut is at the FatFs API (`ff.h`), above `ff.c`. This stage has one backend: a host directory.

**Why only the host directory** (map question C1; the platform reader preferred a FAT image).
- On the storage scenario, a host-directory prototype matched the FAT image in state and in operation
  counts (measured).
- It needs no ST code, and no FatFs core, whose configuration header (`ffconf.h`) is SLA0044.
- Users can drop folder copies of their card in.
- The image backend's own value is exact FAT behaviour: reused deleted entries, `dd` images and the
  format routine. Neither tier needs that now, and the format routine cannot run anyway (§4.2). §16
  keeps the image backend for later.

**What the firmware calls.**
- `presetManager.c`: `f_open`, `f_close`, `f_read`, `f_write`, `f_lseek` (with the `f_tell` macro, which
  reads `FIL.fptr`), `f_stat`, `f_mkdir`, `f_chdir`, `f_opendir`, `f_readdir`, `f_closedir`, `f_unlink`
  and `f_findfirst`. It uses one global `FIL`, `SDFile`, for every file.
- `appMain.c`: `f_mount`, `f_stat`, `f_mkdir` and `f_chdir`, and `f_mkfs` in the unreachable format
  routine.

**What crosses the cut.**
- Names are 8.3 ASCII. The absolute paths are `/PROJnn`, `/TMP`, `/PROJ.IDX` and `/`. Other names are
  relative to one current directory, and `""` means the current directory for `f_findfirst`.
- Two open modes: open an existing file for reading, and create-always for writing.
- Data is sequential raw little-endian structs. The only seek is `f_lseek(f_tell − 6)`, for V1 patterns.
- Directory entries carry `FILINFO.fname` (upper case, at most 12 characters) and `AM_DIR`.
- Results: `FR_OK`, `FR_EXIST`, `FR_DENIED`, `FR_INVALID_NAME`, `FR_TOO_MANY_OPEN_FILES`, `FR_NO_FILE`
  for a missing last component and `FR_NO_PATH` for a missing earlier one. Any other result reaches
  `menu_showSdError()`.

**The backend keeps FatFs's behaviour:**
- 8.3 validation that rejects `+ . [ ] ?`, and upper-case names on create;
- case-insensitive lookup of each path component;
- create-always truncates the file in place; a mount resets the current directory to the root;
- directory order is birth time, then name, and name alone on a file system without birth times.
  `f_findfirst` returns the first match in that order, with FatFs's `*` and `?` matching. In folders that
  only the firmware wrote, this is FAT's creation order. APFS `readdir()` returns hash order (measured),
  so the backend never uses it for order;
- dot files are skipped, which covers `.DS_Store` and `._` files; names that are not 8.3 are skipped with
  one warning each;
- two files for one slot (the same `NN-` prefix and extension) give a warning that names both; the older
  one loads, as on a card the firmware wrote;
- one open file or sub-directory at a time, the root not counted; a second gives
  `FR_TOO_MANY_OPEN_FILES`, as the FatFs lock does;
- while the card switch is out, every call returns `FR_NOT_READY`;
- it never reports `FR_NO_FILESYSTEM`, and `f_mkfs` returns `FR_DENIED`;
- its POSIX code sits in its own file, because FatFs's `DIR` clashes with `<dirent.h>`.

**Copies keep the order.** Whenever the emulator copies a card (the renderer's working copy, the app's
import and export), it copies file by file in the backend's order and sets each copy's birth time in that
order. A copy therefore loads the same files as its source.

**The device's storage quirks stay**, from the same source (map question C3): the older duplicate wins;
`/PROJ.IDX` is written only on a load; globals are saved only with a project; the init kit is only partly
initialised; the boot ignores the result of the project load; a write error leaks the file lock;
`/PROJ.IDX` and the `tmp.snd` slot byte are not range-checked. A fix of any of them goes into the firmware
first, as its own change.

**The emulator supplies** `SDPath`, `SDFatFS` and `SDFile`; `MX_FATFS_Init()` and `MX_FATFS_ReInit()` as
no-ops; and `BSP_SD_IsDetected()` with `SD_PRESENT` and `SD_NOT_PRESENT`, from the card switch.
`fatfs.h` and `ffconf.h` are fresh stand-ins with the same option values. `ff.h` and `integer.h` are
ChaN's, under the FatFs licence.

**The card the firmware expects:**

| Path | Contents |
|---|---|
| `/PROJ.IDX` | 1 byte: the last loaded project. Every boot rewrites it; a project save does not |
| `/PROJnn/PRJ.NFO` | the 8-byte project name; required, without it nothing loads |
| `/PROJnn/GLO.CFG` | 30 bytes, `activeParameters[272..301]`. Byte 0 is 0 for EXT, 1 for MID, or a tempo of 2–255 BPM. Byte 29 is the kit autoload (`PAR_PATTERN_MODE`). A shorter file is rejected |
| `/PROJnn/NN-NAME.SND`, `.PAT`, `.SNG` | per slot: a kit (255 B), a pattern (PatV03, 3,663 B), a song (72 B). A full project is 194 files and 255,398 bytes |
| `/TMP/` | kits and patterns saved by hand since the last project save, plus `tmp.snd` (248 B) and `tmp.pat` (3,650 B), which the autosave writes 2 s after the last edit, also during playback |

File names are `NN-` plus the first 5 cleaned characters of the name stored inside the file. Loading uses
`f_findfirst("NN-*.ext")`, with `pNNN.ext` as the fallback. Patterns V1, V2 and V3 load; only V3 is
written.

`presetManager.c` calls `soundengine_process()` and `midiParser_ccHandler()`, so storage runs on the
firmware thread.

### 4.9 Randomness

- On the device, `GetRngValue()` reads the hardware TRNG through `HAL_RNG_GetRandomNumber(&hrng)`. Its
  run-time consumers are the noise oscillators (`Oscillator.c:336` and its inlined copies), the LFO noise
  (`lfo.c:76`) and the sequencer's probability draw (`seq_cbRandom()`, `sequencer.c:2110–2114`). Nothing
  calls `rand()`, and `dither_process()` and `calcNoise()` are dead code.
- The emulator has two random streams:
  - `HAL_RNG_GetRandomNumber()` returns the DSP stream;
  - the probability draw gets the sequencer stream, through a build hook in `seq_cbRandom()` (F5). The
    device's binary is byte-identical with the hook.
- **Why two** (map question B4): with one shared stream, a probability edit changes the noise of every
  later render. Every stage 2 feature that draws a number would then change every golden with noise in
  it.
- Each stream is xorshift32 with shifts 13, 17 and 5. The DSP stream starts from the low 32 bits of
  SplitMix64(seed), and the sequencer stream from those of SplitMix64(seed + 1); a start value of 0
  becomes 1.
- The renderer's seed defaults to 1. The app seeds from the wall clock at power-on.
- With one firmware thread the call order is fixed, so equal seeds give equal renders.

### 4.10 Power-on and boot

**Power-on** puts the registers in the state the CubeMX init leaves them in.
- The encoder push reads 1 (released). The jack switches, the card switch and the faders take their
  configured states.
- SysTick runs from cycle 0. TIM2 and TIM14 are stopped, and no block request exists.
- `hUsbDeviceFS` is configured (§4.4).

**Boot** is `appMain_boot()`: `main.c`'s code from `dio_init()` to the EXTI enables, moved unchanged
(F4). With `LOAD_PROJECT_ON_BOOT 1` and `UPDATE_BOOTLOADER 0` it runs:
1. `dio_init()`, `lcd_init()` and `menu_init()`;
2. the ADC DMA start, then `adc_checkPots()` twice;
3. `MX_FATFS_ReInit()`;
4. the splash, 'LICHOMAT' and 'v0.1 unofficial', which NOTICE.md 7(c) requires;
5. `f_mount()`, then `HAL_Delay(1000)`;
6. `/proj00`: stat, mkdir if missing, chdir;
7. `soundengine_init()`, which starts the codec (§4.1);
8. `preset_loadGlobals()`; if that fails, 'glo.cfg error' and `preset_createGlobals()`;
9. `morphParameters = mainParameters[0]`;
10. `preset_loadProjectNames()` 64 times, then the last project from `/PROJ.IDX`;
11. `initMidiUart()`;
12. TIM14 starts, then `buttonHandler_init()`;
13. the `/TMP` overlay: `preset_initTmpFolder()`, `loadTmpFolderFiles()`, `preset_LoadTmpKit()`,
    `preset_LoadTmpPat()` and `preset_resetAllSaveTimers()`;
14. `lcd_process_display_interrupt = 1`, and EXTI4 and EXTI9_5 enabled.

Without a card, the boot runs `copyClear_clearCurrentPattern()`, `preset_init(0)` and
`preset_loadDefaultGlobals()` instead of steps 8–10. `main.c:833` tests the result of `f_mount()`, not of
`preset_loadProject()`, so a failed project load at boot shows no error. Both stay as they are.

**Pacing in the app.** Before the codec starts there is no audio to pace the clock. So until then, and
whenever no audio output runs, the app paces virtual time by the wall clock. The splash therefore shows
for the device's 1 s. The renderer never paces.

## 5. Tier 1: lichomat-render

### 5.1 Command line

```
lichomat-render --card DIR --out OUT.wav [options]
lichomat-render --seed-card DIR
lichomat-render --version | --help

  --project N             load project N (0-63); default: the one in /PROJ.IDX, else 0
  --pattern P             play pattern P (0-63) in pattern mode
  --song S                play song S (0-63) in song mode
  --bpm B                 internal sync at B BPM (2-255); 'ext' or 'mid' for those sync modes
  --seconds T             length after script time 0; default 10
  --bars N                length in 4/4 bars of the internal tempo, instead of --seconds
  --events FILE           event script (5.4)
  --no-start              the script starts the transport itself; nothing is pressed at t0
  --faders F1,F2,F3,F4,F5,F6   fader levels 0-1; default all 1
  --jacks all|none|LIST   plugged output jacks, LIST from L1,R1,L2,R2; default all
  --seed N                random seed; default 1
  --as-booted             keep /TMP, as the device does at power-on; default: /TMP is left out
  --wav-rate 44141|44100  rate written into the WAV header; default 44141
  --stereo out1|out2|sum  write a stereo WAV of one output, or of their saturating sum
  --midi-log FILE         MIDI-out log
  --clock-log FILE        clock-out log
  --panel-log FILE        LCD and LED snapshots at the script's marks
  --summary FILE          counters and times, in JSON
  --keep-card DIR         keep the card copy's final state in DIR
```

`--seed-card DIR` writes an init card into the empty folder DIR and exits (§6.7). `--bars` needs an
internal tempo, from `--bpm` or from the card; in EXT or MID sync without `--bpm` it is a usage error.

**Defaults for what the card lacks** (map question B8).
- An internal-sync project plays at its stored tempo.
- If the loaded project's sync is EXT or MID and the script sends no clock to that input, the renderer
  uses 120 BPM in internal sync and prints a warning.
- Faders full, all jacks plugged, `/TMP` left out, seed 1, pattern mode with the pattern active after
  boot.

**The WAV header rate** (map question B5; the readers disagreed).
- The default is 44,141 Hz, 8.5 ppm above the true 44,140.625 Hz. A DAW then plays the file at the
  device's pitch and tempo, off by 0.015 cents.
- A 44,100 Hz header would make the file play 0.092 % slow and 1.6 cents flat. A pattern at 128 BPM would
  drift a beat against a DAW grid at 128 BPM in about 11 minutes.
- `--wav-rate 44100` exists for tools that reject other rates. The samples are the same either way.

### 5.2 The card

- The renderer copies the card folder to a new temporary folder before boot, in order (§4.8). It deletes
  the copy at exit, unless `--keep-card` is given. The boot rewrites `/PROJ.IDX` and may create `/proj00`,
  its `glo.cfg` and `/TMP`; the autosave writes to `/TMP`.
- `/TMP` is left out of the copy unless `--as-booted` is given.
- `--project N` writes N into the copy's `/PROJ.IDX`, which is how the device remembers the project to
  load.
- Before boot, the renderer checks that the project to load has a `PRJ.NFO`. Without one nothing would
  load, so it stops with exit code 2.

### 5.3 The timeline

1. Power-on at cycle 0, with the configured jacks, faders and seed.
2. Boot, then passes.
3. **The prelude** starts at the first pass. It selects what to play and sets the tempo (below).
4. **Script time 0 (t0)** is the first block request after three things have happened: the prelude's
   last event, the end of the preset manager's parameter transfer, and 100 ms more. A kit load through
   the morph path takes 247 `soundengine_process()` calls, about 20 ms, so t0 never falls inside one.
5. At t0 the renderer presses START/STOP, or SHIFT + START/STOP for `--song`. With `--no-start` it
   presses nothing, and the script starts the transport, for example with a MIDI Start in a slave
   scenario.
6. The WAV holds the blocks that start playing at t0 or later, for the given length. The logs are timed
   from t0.
7. The render ends when the clock reaches the end, wherever the firmware is at that moment. The renderer
   then closes its files and exits.

**How the prelude acts.**
- `--pattern P`: the panel selects pattern P with the buttons a user presses on the device. The plan
  derives the sequence from `buttonHandler.c`, and a test checks that the pattern is then P.
- `--song S`: the panel selects song S through the menu, as a user does. A test checks the selection.
- `--bpm`: the renderer sets `activeParameters[PAR_BPM]` and calls `menu_parseGlobalParam(PAR_BPM,
  value)`, which is what `menu.c:3090` does when a user edits the tempo. 2–255 is a tempo in internal
  sync, `ext` is 0 and `mid` is 1. This is the one place where the renderer calls the firmware instead of
  pressing buttons, because a menu walk to the tempo item would break with every menu change.

### 5.4 Event scripts

A script is text with one event per line. `#` starts a comment.

```
<time> <event> [arguments]
```

- A time is `12.5ms`, `2s`, or `441f` (frames at 44,140.625 Hz), counted from t0, or `+50ms` after the
  previous line's time. Times must not decrease.
- A button is a number 0–40 or a name: `start`, `shift`, `rec`, `copy`, `mode1`–`mode4`,
  `voice1`–`voice7`, `select1`–`select8`, `step1`–`step16`, `bar+`, `bar-` (§4.6).

| Event | Arguments | Effect |
|---|---|---|
| `down`, `up` | button | the button goes down or up |
| `press` | button, optional ms | down, then up after ms (default 50) |
| `encoder` | ±detents | turns the encoder, one Gray change per ms |
| `push` | optional ms | encoder push held for ms (default 50, at least 40) |
| `knob` | 1–4, ±degrees | turns a knob, at most 1° per pass |
| `fader` | 1–6, level 0–1 | moves a fader |
| `din` | hex bytes | MIDI into DIN, 320 µs per byte |
| `usb` | hex bytes | MIDI into USB, as transfers of USB-MIDI packets |
| `din-clock` | BPM or `off` | a steady MIDI clock into DIN from this time |
| `usb-clock` | BPM and optional buffer ms, or `off` | a steady MIDI clock into USB. With a buffer length, each transfer carries the clocks of one buffer and is stamped at the buffer's start, as a DAW sends them |
| `jack-clock` | BPM and PPQ (4 or 24), or `off` | steady pulses into clock in |
| `jack-pulse` | none | one clock-in pulse |
| `reset` | `on` or `off` | the reset-in level |
| `card` | `in` or `out` | the card switch |
| `jacks` | `all`, `none` or a list | the output-jack switches |
| `mark` | name | a snapshot of the LCD and the LEDs into the panel log |

A script error stops the renderer with exit code 1 and names the file and line.

### 5.5 Outputs

- **WAV**: 16-bit PCM with 4 channels, L1, R1, L2, R2. Out 1 comes from `dma_buffer2` and out 2 from
  `dma_buffer`. The header rate follows `--wav-rate`. With `--stereo` the file has 2 channels.
- **MIDI log**: one line per DIN byte and per USB transfer. Each line gives the cycles since t0, the
  milliseconds since t0 with 3 decimals, the port (`DIN` or `USB`), and the byte or the packets (8 hex
  digits each).
- **Clock log**: one line per clock-out edge, with the cycles, the milliseconds and the new level.
- **Panel log**: one line per mark, with the time, the mark's name, the two LCD rows (other than
  printable ASCII as `\xNN`), the cursor, and the 41-bit LED mask in hex.
- **Summary**: the boot's end and t0 in cycles, the number of blocks, the underruns,
  `seqp_syncOverflows()`, `usb_midiTxDropped()`, the fields of `seqc_stats()`, the dropped encoder
  detents, the wall time and the warnings. A short form goes to stderr.

### 5.6 Card fixtures

- Fixtures are minimal card folders under `emu/tests/cards/`, a few KB each (map question C7). They hold
  no factory kit.
- `emu/tests/cards/make.sh` makes them: it seeds a card (`--seed-card`), applies scripted edits through
  `lichomat-render` and keeps the result with `--keep-card`. The script is committed with the fixtures, so
  each one can be remade from the firmware.
- A render needs at least one project folder with `PRJ.NFO`, the `.PAT` files to play and the `.SND`
  files their `kitNr` bytes name (kit 00 when the autoload is off). `GLO.CFG` is optional and sets the
  tempo and the autoload; a `.SNG` file is needed for a song (measured).
- Missing slots are skipped. Missing patterns keep the `seq_init()` defaults: length 16, with steps 0,
  8, … active at volume 127. Missing kits stay all zero, and missing songs stay `SONG_END`.

### 5.7 Exit codes

| Code | Meaning |
|---|---|
| 0 | Rendered |
| 1 | Bad options or script; the message names the option, or the file and line |
| 2 | Card error: the folder is missing or unreadable, or the project to load has no `PRJ.NFO` |
| 3 | Firmware fault: a signal (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT) or a sanitizer report. The message gives the virtual time and the last 32 scheduler events |
| 4 | Hang: no scheduler progress for 10 s of wall time |
| 5 | An output file cannot be written |

## 6. Tier 2: LICHOMAT Emu

### 6.1 Threads and rings

| Thread | Runs | Talks through |
|---|---|---|
| Main (Cocoa) | the GUI at 60 frames per second | the panel ring in; the LCD and LED snapshots out |
| Firmware | the virtual device | all rings. Only this thread calls `emu_core` |
| CoreAudio callback | copies frames out of the audio ring | the audio ring; records the time-map points |
| CoreMIDI read procs | push incoming events with their host time | one MIDI-in ring per port |
| MIDI out | sends DIN bytes and USB transfers at their presentation time | the MIDI-out ring |

- The rings are single-producer, single-consumer and lock-free, with fixed sizes allocated at start.
- The callback never blocks, allocates or locks. It may signal one semaphore that wakes the firmware
  thread.
- `MIDISend` and `MIDIReceived` are Mach IPC, so they stay on the MIDI-out thread.

### 6.2 CoreAudio

- The default output unit (AUHAL) takes float32 stereo at 44,140.625 Hz, and its converter resamples to
  the device rate at its highest quality setting (Q16).
  - Measured: the unit accepted 44,140 Hz and resampled it to a 48 kHz device. 44,140.625 Hz was not
    tried.
  - If the unit rejects the fractional rate, the app uses 44,141 Hz and shows that in the diagnostics.
- The firmware thread renders while less audio is queued than the largest callback request so far plus
  4 blocks (256 frames, 5.8 ms), and waits otherwise. Virtual time therefore follows the rendered frames,
  not the wall clock. On this Mac the callbacks asked for 470–471 frames, so a frame FIFO sits between
  the blocks and the callback.
- The callback converts int16 to float by dividing by 32,768.
- **Output mix**, a setting: out 1 + out 2 summed (the default, so that any routing is heard), out 1,
  out 2, or all 4 channels to the first 4 channels of a device that has them.
- On an output stall or a device change, the app reopens the unit, re-anchors the time map and drops
  input events older than 100 ms (§4.3).

### 6.3 The time map

The firmware thread maps host time (mach ticks) to virtual time and back.
- Each callback reports a point: the virtual frame of its first output frame, and the presentation host
  time of that frame. The presentation time is the callback's `mHostTime` plus the device latency and the
  safety offset, converted with `mRateScalar`.
- The map is the line through the latest point, with the slope measured over the last 2 s. The slope
  follows the drift of the audio device's clock against the host clock.
- When a new point is more than 1 ms off the line, the map re-anchors on it.
- On this Mac: built-in output at 48 kHz, a 512-frame buffer (range 15–4096), a device latency of
  60 frames and a safety offset of 48 frames; the mach timebase is 125/3 ns per tick (measured).

### 6.4 CoreMIDI in

- **USB side.** The app creates a virtual destination named 'LICHOMAT Emu' (Q14). Each event list a host
  sends to it enters as one USB transfer (§4.3) of USB-MIDI 1.0 packets on cable 0, with the CIN of each
  message, SysEx included.
- **DIN side.** The user binds a source: a hardware MIDI interface or another app's virtual port. Its
  bytes enter the DIN wire model (§4.3).
- The read procs push (host time, port, bytes) into the rings. A timestamp of 0 means 'now'.
- **When an event enters.** The firmware thread injects each event at virtual time
  max(map(stamp) + D, now).
  - D, the slave input delay, applies to MIDI and jack input while the firmware's sync mode is MIDI (2)
    or clock jack (1), read through `seq_getExtSync()`.
  - Its default is 2 × the output buffer + the device latency + 1 ms, about 24 ms at 512 frames and
    48 kHz (Q16). The user can lower it to 0.
  - The app shows the effective D in ms, so that a DAW's MIDI clock sync delay can compensate it.
  - In internal sync, D is 0, so played notes get the lowest latency.
- **Why D** (the MIDI reader's simulation):
  - Without D, slave steps stay within one block only while 2 × buffer + device latency is shorter than
    a MIDI clock period. At 512 frames that holds up to about 107 BPM.
  - Above that, the steady-state spread grew to 3.6 ms at 127 BPM, 5.4 ms at 140 and 8.9 ms at 174 with
    512 frames, and to 21.2 ms at 1024 frames and 127 BPM.
  - The first step after Start was late by the render lead: 6.7, 22.7 and 33.3 ms at 128, 512 and 1024
    frames.
  - With D, the latency is constant: D + 0–1.32 ms.
- The app uses the MIDI 1.0 protocol APIs of macOS 11 (`MIDIDestinationCreateWithProtocol`,
  `MIDIInputPortCreateWithProtocol`, `MIDIEventList`), not the `MIDIPacketList` calls that the macOS 15.5
  SDK marks as going to be deprecated.

### 6.5 CoreMIDI out

- The firmware thread passes each DIN byte and USB transfer, with its virtual time, to the MIDI-out
  thread. Its presentation time is the time map's inverse of that virtual time.
- **USB side**: a virtual source named 'LICHOMAT Emu'. The MIDI-out thread waits until the presentation
  time and then calls `MIDIReceivedEventList` with that time as the timestamp. Sending earlier would give
  receivers that ignore timestamps the MIDI early, by the render lead of about 13 ms at the default
  buffer.
- **DIN side**: the destination the user binds. `MIDISendEventList` with the presentation time as the
  timestamp, and CoreMIDI schedules it.

### 6.6 The virtual panel

The panel uses Dear ImGui on its native Cocoa and Metal backends (Q16). Its Metal shaders compile at run
time from source, so the Command Line Tools are enough to build it.

- **Layout**, schematic and in our own colours (Q14):
  - the LCD and the encoder;
  - 4 knobs under the LCD's 4 columns, since knob i edits the parameter in column i;
  - 6 faders, labelled by their voices;
  - 41 buttons with their LEDs, labelled with their function names;
  - a jack strip with the 4 output-jack switches, the card switch, the clock-out LED, and the clock-in
    and reset-in controls (§6.8).
- **No branding.** No LXR or Erica Synths name or logo, and no copy of the faceplate's layout, colours,
  printing or graphics.
- **Mouse and keys.**
  - Buttons: mouse down and up are button down and up. A secondary click latches a button down until the
    next click, so that SHIFT + step works with one mouse. The Shift key holds SHIFT, and the space bar
    presses START/STOP.
  - Knobs: a vertical drag turns 1° per point, 360 points per turn; the wheel turns 5° per notch. The
    panel model ramps at most 1° per pass, so a turn gives about 134 steps on a normal parameter and 22
    on a list parameter, as on the device (Q16).
  - Faders: a vertical drag; a double click returns to full.
  - Encoder: the wheel or a drag, one detent per notch or per 12 points; a click pushes it.
- **Reads.** The GUI draws from the LCD snapshot and the latched LED mask, once per frame.
- **Writes.** Panel events go into the panel ring. The firmware thread releases them at the poll points
  (§4.6).
- **State at power-on**: faders full, all jacks plugged, the card in.

### 6.7 The card folder

- The app works on its own folder, `~/Library/Application Support/LICHOMAT Emu/Card`, and never on the
  user's card (Q16).
- **First run.** An empty folder gets an init project from the firmware's own code. `lichomat-render
  --seed-card` runs, in a separate process before the app boots, the steps that `sd_formatRoutine()`
  runs after its confirmation: default globals, kit 0 from `preset_init(0)`, project 0 named 'Init',
  saved by `preset_saveProject(0)`. No factory kit is shipped (Q15).
- **Import** from a folder, such as a mounted SD card:
  1. the app takes the virtual card out, so the firmware stops writing to it;
  2. it moves its current folder to `Card backups/YYYYMMDD-HHMMSS`, named after the local time;
  3. it copies the source file by file in the source's order: `readdir()` order on a FAT volume, so a
     copy from a card keeps the card's order, and the backend's order (§4.8) on any other file system;
  4. it power-cycles the emulated device (§6.9), as after swapping a card and switching on.
- **Export** to a folder the user picks, which must be empty: the app takes the virtual card out, copies
  the card in order, and puts the card back in. The firmware remounts it as after a card swap.

### 6.8 Clock jacks

- Always present (Q16): the clock-out LED, latched per frame; a clock-in button, one pulse per click,
  with a steady pulse generator at a set BPM and PPQ; and a reset-in switch.
- Options, off by default (Q16), built in the last increment (§13):
  - **Clock out as audio**: a 1 ms pulse of +1.0 on a chosen output channel at each rising edge, at the
    edge's frame. With this option on, the app holds each block back by one block (1.45 ms), so that
    every edge of a block's time is known before the block is played.
  - **Clock and reset in from audio**: an input channel with a threshold (default half of full scale).
    Clock edges count on the rise. Edges enter at their frame's virtual time plus D. This needs macOS's
    microphone permission, which the app asks for only when the option is turned on.

### 6.9 Power, crashes and settings

- **Power cycle** restarts the app's process, because only a new process resets the firmware's static
  state. The virtual MIDI ports disappear for that moment, as the device's USB port does.
- **Crash.** A fault signal on the firmware thread writes `crash-YYYYMMDD-HHMMSS.txt` into the app's
  folder with async-signal-safe calls: the virtual time and the last 64 scheduler events and inputs. The
  app then ends. The next launch shows the note.
- **Hang.** The watchdog (§4.2) stops the audio and shows a message with a 'Restart' button.
- **Settings** persist in the app's folder: the output device, buffer size and mix; the DIN source and
  destination; D; the window layout; the options of §6.8.
- **Diagnostics** show:
  - the stream formats, and the measured block rate (689.7 blocks per second expected);
  - output underruns, and the effective D;
  - the firmware thread's load, as render time over real time;
  - the counters of §5.5.

## 7. Firmware changes (Q13)

- Each change is one small commit on `emulator`, with its own test, before the emulator code that relies
  on it.
- The changes reach a firmware release only after the owner's v0.1 checks, with the device checks U3–U7
  (§12.4).
- 'The base build' below is the firmware built from the commit before the change.
- New and changed C files in `Core/` use CRLF (HANDOFF.md, review row R9.4). `Core/sources.cmake` uses LF,
  like `Core/CMakeLists.txt`.

| ID | Change | Effect on the device | Acceptance |
|---|---|---|---|
| F1 | `modulationNode.c`: a node whose destination parameter has no pointer (`PAR_NONE`) keeps its original value | None. Today such a node reads its original value from address 0 (ITCM) and never uses it | M15: CC 1 with nodes at destination 0 does not crash; U3 |
| F2 | `morphParameters = mainParameters[0]` on the boot path without a card too | Without a card, morphing interpolates towards kit slot 0 instead of bytes from address 0 | M15: boot without a card and turn the morph; U4 |
| F3 | `menu.c:1079` and `:1083`: the clear-page text is written within its row, with the same visible result | None | M15: the LCD snapshots of the clear-automation pages equal the base build's, built with `-D_FORTIFY_SOURCE=0`; U5 |
| F4 | `main.c`'s boot (from `dio_init()` at line 319 to the EXTI enables at line 921) becomes `appMain_boot()`, and the body of its `while (1)` loop (lines 922–997) becomes `appMain_loopOnce()`, in `Core/Src/appMain.c`. `sd_formatRoutine()`, the state shared by boot and loop (`res`, `lastSdState`) and `checkMainLoc` move with them. `main()` keeps the CubeMX init and calls them. The splash definitions move to `appMain.c` | None; the code moves, the logic stays | `git diff --color-moved` shows moves and the two new function frames only; the image check finds the splash; ITCM use is unchanged; U6 |
| F5 | `sequencer.c`: `seq_cbRandom()` returns `SEQ_RANDOM()`, a macro that defaults to `GetRngValue()` | None | The release `.bin` is byte-identical to the base build's |
| F6 | `Oscillator.c`: a saturating float-to-uint32 conversion at the 8 FM sites (lines 117, 140, 165, 193, 231, 509, 536 and 570) and in `freq2PhaseIncr()` (line 24). On the Cortex-M7 it is the plain cast, which `VCVT` already saturates | None | The release `.bin` is byte-identical; M1: the host version equals `FCVTZU` for edge values and NaN |
| F7 | `AudioCodecManager.c`: `dma_buffer` and `dma_buffer2` get 4-byte alignment, and so does any other `int16_t` buffer accessed as `int32_t` that UBSan's alignment check finds | None. Both buffers already sit at 4-byte-aligned addresses (`0x20019e1c` and `0x2001a01c` in the release map) | The release `.bin` is byte-identical |
| F8 | `Core/sources.cmake`: the module source lists leave `Core/CMakeLists.txt`, with the same files in the same order | None | The release `.bin` and `.map` are byte-identical |
| F9 | `Oscillator.c:464` and `:174`: the interpolation reads `table[row][itg & 1023]` | It changes the sound in one case (below) | M15: no read past the tables under ASan; listening check U7 before it reaches a release |

**F6 and the compiler flag.** With `-fno-strict-float-cast-overflow` (§8.2), every float-to-int cast in
the firmware already follows arm64's saturating conversion, which matches the M7's. F6 still makes the 9
known sites defined C. That keeps the sanitizer's float-cast check on for all other code, which stage 2's
timing code needs.

**F9, the table read.** `calcWavetableOscBlock()` and `calcFmBlock()` interpolate between
`table[row][itg]` and `table[row][itg + 1]`, with itg up to 1023. So they read `table[row][1024]`.
- For rows 0–9 that is the next row's first sample. Every row of `sawTable` starts with 0, and every row
  of `triTable` with −32,767 (*checked*). The value read is the right one by luck.
- For row 10 it reads past the table. On the device it reads the first bytes of whatever the linker put
  next, `CSWTCH.11` in the reference build. On the host it reads whatever follows, which depends on the
  memory layout.
- The fix reads the row's own first sample. It changes the device's output only for oscillators in band
  10, once per period, by the difference between the stray value and the right one.
- On `emulator` the fix lands early, because goldens must not depend on the memory layout. It reaches a
  firmware release only after a listening check on the device (Q13, U7).

**The ping-pong delay report.** LXR02XXL's PATCHES.md reports that the ping-pong delay's pointers can run
past `FX_BUFFER` after a mode switch. The study's UBSan runs did not reproduce it.
- M16 switches the delay modes during playback under ASan and UBSan.
- If it reproduces, its fix is one more change like F1–F9, with a test and a device check.
- If it does not, the plan records the scenario and the result.

**Further sanitizer findings** follow the same rules: one commit each, a test, and a device check
wherever the device's output could change.

## 8. Fidelity and golden tests

### 8.1 What is pinned, and where

- **Goldens pin host builds, compared exactly** (map question B1): the WAV, the MIDI log and the clock
  log, each by SHA-256. Fidelity to the device is 'very close', and it is not measured. The host variants
  differ from each other at 70–96 dB SNR.
- **Platform** (map question B2; the readers disagreed): macOS on arm64 with Apple clang from the Command
  Line Tools. Goldens are made on the developer's Mac and checked there and on the CI runner `macos-15`.
  - arm64 matches the device where x86-64 does not: out-of-range float-to-int conversions saturate, and
    `x % 0` gives x. `EuklidGenerator.c` stores a length of 16 as 0 (`:235–236`) and later divides by it;
    x86-64 raises SIGFPE there.
  - Without saturation, x86-64 renders differed from arm64 in 9.6–10.4 % of samples, by up to 12,121 LSB
    (measured).
  - This stage has no Linux job. A Linux arm64 job is a later option (§16).
- **libm is vendored.** Measured at `da64603` with the strict flags, the firmware objects import six libm
  functions: `sinf`, `cosf`, `expf`, `exp`, `logf` and `fmod`.
  - `fmod` is exact in IEEE arithmetic, so every libm gives the same result.
  - The other five come from Arm Optimized Routines, the family newlib uses on the device. They are
    linked so that the firmware objects never call the system's versions, and so that the compiler never
    folds them at compile time. M19 checks it.
  - Why: the owner's Mac (macOS 26.7.1) and the runner (macOS 15.7.9) have different system libraries,
    and a macOS update must not change a golden.

### 8.2 Floating point

The firmware objects and the core are compiled with:

| Flag | Why |
|---|---|
| `-std=gnu11` | The firmware's dialect; it also provides `M_PI` |
| `-O2` | Release. `-O0` and `-O2` give identical renders with the flags below (measured) |
| `-ffp-contract=off` | Default FMA contraction on arm64 changed 0.16–0.23 % of samples by 3–7 LSB (measured) |
| `-fno-fast-math` | `-ffast-math` is not deterministic across optimisation levels: between `-O0` and `-O2`, 47,724 of 882,688 samples differed, by up to 399 LSB (measured) |
| `-fno-strict-float-cast-overflow` | Out-of-range float-to-int casts then follow `FCVTZU` and `FCVTZS`, which saturate like the M7's `VCVT`. Without it, clang folds a constant `(uint32_t)-1.0f` to 3,758,096,384 and `(uint32_t)5e9f` to 4,294,967,040; with it, to 0 and 4,294,967,295 (measured for this spec) |
| `-funsigned-char` | Plain `char` is unsigned on the device and signed in Apple clang on arm64 |
| `-fno-strict-aliasing` | The mixer and the FX bus access `int16_t` buffers as `int32_t` |
| `-w` | For the firmware files only, as in `tests/`. The firmware build stays their warning gate |

- **A 'firmware-like' flavour** (map question B3): a CMake option adds `-freciprocal-math`. It
  reproduces the device's band choice at exact octave frequencies (440, 880, 1760 … Hz), where
  `f × rcp(440)` lands just below the integer. It is deterministic and about 73 dB from the strict build.
  It exists for listening; goldens and releases use the strict build.
- **Not reproduced**, by design:
  - the device's `-ffast-math` code: 286 fused multiply-adds against 27 explicit `fma` calls, and heavy
    double precision in `Distortion_process()` and `Compressor_process()`;
  - flush-to-zero, which `FPDSCR.FZ` gives the device only in interrupt renders;
  - the removal of the NaN guard (`removeMaliciousSample()`) that `-ffinite-math-only` causes on the
    device. The outputs differ only once a NaN occurs.

### 8.3 Intrinsics

The stand-in HAL header implements the CMSIS intrinsics in C, with the M7's results:
- the active ones: `__SSAT` (56 sites), `__QADD16` (43), `__PKHBT` (14), `__QSUB16` (3) and `__CLZ` (2);
- `__CLZ(0)` returns 32, as the M7's `CLZ` does. That selects band 4 for every wavetable oscillator below
  440 Hz; `__builtin_clz(0)` is undefined;
- the declared-only ones: `__SMLALD`, `__QADD`, `__QSUB`, and `__get_APSR`, which returns 0 because only
  dead code reads it.

M1 checks each against edge cases.

### 8.4 What a golden fixes

- A golden is an event script whose header lines hold its renderer options. Its expected hashes and
  per-channel statistics are in `emu/tests/goldens/goldens.json`.
- Each golden runs in a fresh process, because the loaded state depends on history: `preset_init()`
  copies only parameters 0–238, a V2 pattern keeps the old step timing, a V1 pattern keeps the old
  `kitNr`, and short song files keep old entries.
- Its inputs are fixed: a card fixture (copied), faders full (`adc_buf` 0xFFF0, which `adcPots.c` turns
  into 0.99951), all jacks plugged, seed 1 for both random streams (§4.9), `/TMP` left out, an explicit
  tempo or clock, and t0 after the kit transfer.

### 8.5 Mismatch and bless

- A mismatch fails the test. It prints the first differing frame and channel, and the peak and RMS of
  each channel in both renders. It keeps the new outputs, and CI uploads them.
- `tools/emu_golden.py bless` with one or more golden IDs renders them again and rewrites their hashes.
  A bless is a commit of its own. Its message says why the output changed and that the new render was
  listened to.
- Goldens run in both CI jobs: the release build and the sanitizer build. That also checks that the
  renders do not depend on the optimisation level.

### 8.6 The goldens

| ID | Covers |
|---|---|
| G1 | The init card, pattern 0 at 120 BPM, 4 bars: boot, kit load, every voice |
| G2 | The FX bus: tube, fold and rat distortion, ring modulator, compressor, and the delay in normal and ping-pong mode, one render each |
| G3 | Song mode over three patterns, with the kit autoload |
| G4 | The panel: mutes, a roll, a pattern change by buttons, a knob turn and a fader move |
| G5 | Master: MIDI out (Note On and Off, F8, Start, Stop, Program Change) and clock out at 24 PPQ |
| G6 | The DIN F8 behind 7 Note Ons of one step (Q17) |
| G7 | DIN slave at 128 BPM: Start, Stop, SPP and Continue, and a Program Change sent before a Start (N14) |
| G8 | USB slave with one transfer per 11.6 ms buffer, at 120 and 137 BPM (N21) |
| G9 | Clock in at 4 and 24 PPQ, with the reset gate |
| G10 | Probability and noise with seed 1, with a probability edit during playback |

## 9. Build and CI

### 9.1 Shared source lists

- `Core/sources.cmake` defines one list per firmware module: `LXR_SOURCES_PLATFORM`, `LXR_SOURCES_IO`,
  `LXR_SOURCES_UI`, `LXR_SOURCES_PRESET`, `LXR_SOURCES_DSP`, `LXR_SOURCES_ENGINE`, `LXR_SOURCES_MIDI`,
  `LXR_SOURCES_SEQUENCER` and `LXR_SOURCES_USB_APP`. They hold the same files, in the same order, as
  today's `add_library()` calls (`Core/CMakeLists.txt:21–149`).
- `Core/CMakeLists.txt` includes the file. Nothing else in the firmware build changes, and its `.bin` and
  `.map` stay byte-identical (F8).
- `emu/CMakeLists.txt` includes the same file. It compiles every listed `.c` file except its exclusion
  list (§3.4). A listed file that is neither compiled nor excluded fails the configure step, so a stage 2
  file cannot be missed (M22).
- The emulator reads `LICHOMAT_NAME` and the version from the top-level `CMakeLists.txt`
  (`project(… VERSION x.y …)`). It passes `FIRMWARE_NAME` and `FIRMWARE_VERSION` to `appMain.c` as the
  firmware build does, so the emulator's splash is the firmware's.
- The firmware's CMake cannot host the emulator. It forces the ARM toolchain (`CMakeLists.txt:18–20`), and
  Apple clang rejects `-mfp16-format=ieee` and `-fno-thread-jumps`. The emulator uses the lists only, not
  the firmware's targets or flags.

### 9.2 The emulator project

- `emu/` is a CMake project of its own, next to `tests/`, built with the host's Apple clang:
  `cmake -S emu -B build/emu -G Ninja`.
- arm64 only (`CMAKE_OSX_ARCHITECTURES` = arm64), with macOS 13.0 as the deployment target (Q15). The
  configure step fails on any other architecture.
- Targets:
  - the firmware objects: one OBJECT library with the flags of §8.2, the stub include path,
    `-Dsection(x)=unused` and the overlay;
  - the core, in C11 with `-Wall -Wextra -Werror`;
  - `lichomat-render`;
  - `LICHOMAT Emu.app`, behind the option `LICHOMAT_EMU_APP` (on by default);
  - the tests.
- `LICHOMAT_EMU_SANITIZE` selects sanitizers. The default is `undefined`, as in `tests/`, because ASan
  binaries hang inside the sandbox used for development. CI uses `address,undefined`.
- `LICHOMAT_EMU_FP` is `strict` (the default) or `firmware-like` (§8.2).
- `EuklidGenerator.c` is built without the sanitizer's integer-division check, because arm64 gives
  `x % 0` the device's result. Any other per-file exception needs its reason in `emu/CMakeLists.txt`.
- The app bundle is signed ad hoc after linking (`codesign --force --sign -`).
- Configure and build need no network: Dear ImGui and the libm sources are in the repository.
- `tools/check.sh` builds `emu/` and runs its tests when it runs on macOS on arm64. The warning count
  stays 0.

### 9.3 CI

A new workflow, `.github/workflows/emulator.yml`, runs on push and on pull request. `build.yml` stays as
it is.

| Job | Runner | Steps |
|---|---|---|
| `emu-release` | `macos-15` (arm64) | CMake 3.31.12 and Ninja 1.13.2, the versions `tools/setup-toolchain.sh` pins, without the Arm toolchain; configure a Release build; build everything; run ctest (host tests and goldens); upload `lichomat-render` and the app, zipped with `ditto` and signed ad hoc; on a failure, upload the golden outputs |
| `emu-sanitize` | `macos-15` (arm64) | Configure with `address,undefined` and without the app; build; run ctest (host tests, goldens and the sanitizer scenarios) |

- The fork is public, so the macOS runners cost nothing.
- The runner images list no audio device (not verified at run time), so CI renders to files only.
- `macos-15` on arm64 has the same Command Line Tools build as the owner's Mac.

## 10. Licence, naming and distribution

### 10.1 Licence

- The emulator is part of the same GPL-3.0-only work, with NOTICE.md's additional permission and terms.
  Apple's frameworks count as System Libraries.
- **Kept out of every emulator binary**, enforced by the licence gate (M21):
  - ST SLA0044 code: the USB device library, `USB_DEVICE_MIDI/`, `FATFS/App/`, `FATFS/Target/`,
    `stm32f7xx_it.c`, `stm32f7xx_hal_msp.c`, and `main.h`, which the overlay replaces. The stand-ins are
    written fresh from the behaviour this spec describes, not copied;
  - `lcd.c` and `lcd.h`, which carry no licence;
  - code from the non-commercial projects: Sonic Potions V1 (SonicPotions/LXR), LXR02Open, LXR-Plugins'
    lxr_dsp, vlndv/lxr-vst3, and LXR02XXL up to 0.5.10;
  - Erica Synths' factory kits, artwork and logos.
- **Allowed third-party code:** ChaN's `ff.h` and `integer.h` (the FatFs licence, BSD-style); Dear ImGui
  (MIT); Arm Optimized Routines (MIT); and ideas and small pieces, with attribution and SPDX headers, from
  the GPL ports PHX-D01, LXR02XXL from 0.6, peterswimm's fork, Pulse Foundry, Felucca and Elixir.
- New files carry `Copyright (C) 2026 Tomasz Krzywicki`, `SPDX-License-Identifier: GPL-3.0-only` and the
  NOTICE.md reference (stage 1 §9.5). Code moved by F4 keeps Erica Synths' header.
- **Binaries** come with:
  - the Corresponding Source, from the same place: the release's tag in the same repository;
  - `LICENSE`, `NOTICE.md` and the third-party notices, inside the app bundle and next to the renderer;
  - the Appropriate Legal Notices. The app's About box and `lichomat-render --version` show the
    copyright, the absence of warranty, the licence, where the source is, and that the product is not
    affiliated with Erica Synths.
- The chain of title is assumed, not verified. Erica relicensed Julian Schmidt's 2012 V1 code as
  GPL-3.0-only, while the same code is still public under a non-commercial licence.

### 10.2 Naming (Q14)

| Thing | Name |
|---|---|
| App | LICHOMAT Emu |
| Renderer | `lichomat-render` |
| CoreMIDI source and destination | LICHOMAT Emu |
| Tagline | emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02 |
| Bundle ID | `io.github.isthar.lichomat-emu` |

- 'LXR', 'Erica Synths' and 'Sonic Potions' never appear in a name, a port name or an icon. The tagline's
  'for the Erica Synths LXR-02' is a truthful statement of compatibility, which NOTICE.md 7(e) allows.
- The panel is schematic, in our own colours, with no logo and no copy of the faceplate (NOTICE.md,
  'Hardware').
- The boot splash, 'LICHOMAT' and 'v0.1 unofficial', shows in the app for 1 s of wall time, and the window
  title says 'unofficial' (NOTICE.md 7(c)).

### 10.3 Distribution (Q15)

- arm64 only, macOS 13 or later.
- Ad hoc signed binaries, `lichomat-render` and `LICHOMAT Emu.app`, as assets of a GitHub release on the
  fork, built by CI. There is no paid Apple developer account, so no notarisation.
- A downloaded copy opens on macOS 15 and later only after System Settings > Privacy & Security > Open
  Anyway, or after removing the quarantine attribute with `xattr`. The README says so, and explains how
  to build from source, which avoids the step.
- No factory kits. Users bring a copy of their card or a folder.
- A release is published only on the owner's go-ahead. Its tag is `emu-v0.1` for the first one.

## 11. Error handling

| Situation | Renderer | App |
|---|---|---|
| The card folder is missing or unreadable | Exit code 2 | Offers to make a new, seeded card |
| The project to load has no `PRJ.NFO` | Exit code 2 | Boots as the device does, with an empty project, and shows a notice |
| A name on the card is not 8.3 | Skipped, one warning per name | The same, in the diagnostics |
| Two files for one slot | The older one loads; a warning names both | The same |
| A firmware fault (a signal or a sanitizer report) | Exit code 3, with the virtual time and the last 32 scheduler events | A crash note, shown at the next launch (§6.9) |
| No scheduler progress for 10 s of wall time | Exit code 4 | Audio stops; a message with 'Restart' |
| An output file cannot be written | Exit code 5 | — |
| A script error | Exit code 1, with the file and line | — |
| The audio output stalls, or the device changes | — | The output reopens, the time map re-anchors, input older than 100 ms is dropped and counted |
| A MIDI or panel ring is full | — | The new event is dropped and counted in the diagnostics |
| A bound DIN port disappears | — | The binding shows 'not connected'; nothing else changes |
| More than 4 encoder detents are queued | The extra detents are dropped and counted | The same |
| The firmware reports an SD error | Shown on the virtual LCD, as on the device; logged in the panel log at marks | Shown on the virtual LCD, as on the device |
| A golden differs | The test fails with the first differing frame and the statistics, and keeps the outputs | — |

## 12. Verification

### 12.1 Host tests

They run through ctest in `emu/`, on macOS on arm64, in both CI jobs and in `tools/check.sh`.

| ID | Covers |
|---|---|
| M1 | The intrinsics against edge cases, `__CLZ(0)` = 32 included; F6's conversion against `FCVTZU`, NaN included |
| M2 | The clock: frames to cycles exact over 10^9 frames; `HAL_GetTick()`; CYCCNT as the low 32 bits; `seqp_now()` monotonic across a 2^32 wrap |
| M3 | The scheduler: §4.2's periods, priorities and order at equal times; PRIMASK deferral; time passing only in thread context; the pass rule |
| M4 | TIM2: it fires 204 cycles after a block request when started, is parked by `soundengine_process()`, and fires every 6,004 cycles inside a `HAL_Delay()` |
| M5 | The codec: the underrun count, the order of the halves, out 1 from `dma_buffer2` |
| M6 | Boot: the splash snapshot ('LICHOMAT', 'v0.1 unofficial'); the path without a card; the card path with a fixture |
| M7 | The LCD model: HD44780 commands, CGRAM, 0xC5 and 0xC6, a full repaint in 6.25 ms |
| M8 | The panel: one transition per scan; Gray changes 1 ms apart; at most 4 detents queued; a 40 ms push seen in every sampling phase; 134 ± 5 steps per knob turn on a normal parameter and 22 ± 2 on a list parameter; fader i reaching `adc_getFaderVolume(i−1)` |
| M9 | The card backend: the storage scenario's operations and results, the order, the lock, the card switch, an order-keeping copy |
| M10 | DIN in and out: 320 µs per byte; the F8 behind Note Ons; SPP and Program Change through the USART3 handler |
| M11 | USB in and out: the DataOut walk (stop at a 0 byte, realtime with CIN 0x5, cable 0); 15-packet transfers; completion after 1 ms |
| M12 | The jacks: clock in at 4 and 24 PPQ only in sync mode 1; the reset gate; clock-out edges |
| M13 | The random streams: known first values for seeds 1 and 2; a probability edit leaves the DSP stream's draws unchanged |
| M14 | Determinism and speed: two runs of G1 are byte-identical; G1 renders at least 10× faster than real time |
| M15 | The firmware changes: F1 (CC 1 with nodes at destination 0), F2 (morph after a boot without a card), F3 (the clear-page LCD equal to the base build's), F9 (no read past the tables under ASan) |
| M16 | The ping-pong delay mode-switch scenario under ASan and UBSan |
| M17 | The renderer: options and defaults, the t0 rule, `--project`, `--bpm`, `/TMP` handling, `--seed-card`, exit codes 1–5 (codes 3 and 4 through a test-only hook) |
| M18 | The app's timing model, without CoreAudio. A fake output clock (48 kHz, 512-frame buffers, 108 frames of latency) and DAW-like clock stamps drive the firmware thread's code. With the default D, steps follow the clock at a constant latency of D + 0–1.45 ms from 90 to 174 BPM; with D = 0, the first step after Start is late by the render lead |
| M19 | libm: the firmware objects use the vendored `sinf`, `cosf`, `expf`, `exp` and `logf`, and import none of them from the system |
| M20 | Drift: the hashes of the firmware bodies the emulator re-implements are unchanged. These are the SysTick, TIM14 and TIM2 handlers in `stm32f7xx_it.c`, `HAL_I2S_TxCpltCallback()`, and the MIDI parts of `USBD_CDC_DataOut()` and `USBD_CDC_DataIn()` in `usbd_cdc.c` |
| M21 | The licence gate: every source and header in the dependency files of the emulator's binaries is GPL-3.0-only or on the allow-list of §10.1, with overlay paths resolved; none names SLA0044 |
| M22 | The source lists: every file in `Core/sources.cmake` is compiled or excluded |

### 12.2 Goldens

G1–G10 (§8.6), in both CI jobs.

### 12.3 The owner's checks of the app

| ID | Check |
|---|---|
| A1 | The app opens after Open Anyway. The splash shows 'LICHOMAT' and 'v0.1 unofficial' for about 1 s. About shows the notices |
| A2 | An imported copy of the owner's card plays its projects, and they sound like the device |
| A3 | Knobs, the encoder and the buttons feel like the device: steps per turn, menu navigation, SHIFT combinations with a latched button |
| A4 | Master over the virtual port: a DAW follows the tempo, Start and Stop |
| A5 | Slave over the virtual port with the default D: Start, Stop, Continue, a locate (SPP), and steady playback at 128 and 140 BPM. The latency matches the D the app shows |
| A6 | Slave over a hardware DIN interface |
| A7 | MIDI out over DIN to a synth: no hanging notes |
| A8 | The clock jacks: the clock-out LED; clock-in pulses in sync mode 1; the reset switch |
| A9 | Export to an empty folder; the exported card loads on the device |
| A10 | A power cycle works. A launch after a crash shows the crash note |
| A11 | Diagnostics with the built-in output: 689.7 ± 0.1 blocks per second, and a firmware-thread load below 10 % on the owner's Mac |

### 12.4 Device checks for the firmware changes

When F1–F9 reach a firmware release, the hardware checklist gains these checks:

| ID | Check |
|---|---|
| U3 | A kit with LFO and velocity modulation targets loads and modulates as in v0.1. CC 1 (the mod wheel) from a keyboard sounds as in v0.1 (F1) |
| U4 | Boot without a card, then turn the morph knob: no noise and no crash (F2) |
| U5 | The clear-automation pages show 'clear [autom. 1]' and 'clear [autom. 2]' as in v0.1 (F3) |
| U6 | H1 and H2 again (F4) |
| U7 | Listening check against v0.1: a wavetable oscillator pitched high enough for band 10, saw and triangle (F9) |

## 13. Rollout

- **Branch.** `emulator` is created from `timing-core` when the owner accepts this spec. `timing-core`
  stays frozen for the owner's checks, and fixes from those checks are merged into `emulator`.
- **Pushing.** `emulator` goes to the owner's fork (`origin`) as work lands, once the owner allows it for
  this branch; Q5 covered `timing-core`. Nothing goes to `upstream`, and `main` is not touched.
- **Increments**, mapped to the tracker's tasks:

  | Increment | Contents | Tracker |
  |---|---|---|
  | R1 | F1–F9 with M1 and M15; `emu/` with the stubs, the overlay, the core and the card backend; boot and passes in a test (M2–M13, M19–M22) | E5.2 |
  | R2 | `lichomat-render`: options, card copy, timeline, event scripts, outputs, exit codes, `--seed-card` (M14, M17) | E5.3 |
  | R3 | Card fixtures, G1–G10, `tools/emu_golden.py`, `emulator.yml`, the ping-pong scenario (M16), the `check.sh` step | E5.4 |
  | R4 | The app: threads and rings, CoreAudio, the time map, CoreMIDI in and out, the panel, the card folder, the clock-jack LED and buttons, power and crash handling, diagnostics (M18; A1–A11) | E5.5 |
  | R5 | The app's audio options for the clock jacks (§6.8) | E5.5 |

- **Plans.** This spec is too big for one plan, so it splits:
  1. one plan for R1–R3, tier 1;
  2. one plan for R4, written when tier 1 is done, from §6;
  3. R5 as a small third plan.
- **Each increment ends** with `tools/check.sh` green, 0 warnings, and the firmware's `.bin` checks of §7
  passing.
- **Release.** Binaries are published only on the owner's go-ahead (§10.3).
- **Stage 2** starts after tier 2, from `emulator` (Q12).

## 14. Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | Latent memory bugs crash the host or make renders depend on the memory layout. More may sit on paths nobody ran: song mode, SysEx, other menus | F1–F3 and F9; ASan and UBSan in CI with goldens and scenarios; exit code 3 and the crash note with the event trail |
| 2 | Firmware code runs on more than one host thread, which turns compiler-only fences, no-op PRIMASK sections and non-reentrant parsers into data races | One firmware thread with rings (§3.1); `MIDISend` and `MIDIReceived` on their own thread |
| 3 | A busy-wait hangs the virtual clock | Time passes in `HAL_Delay()` and `HAL_GetTick()`; the LCD completes at once; the format routine is unreachable; the watchdog |
| 4 | The app's MIDI timing misleads a user: late slave steps, future events acting early, callback stamps, MIDI out early or jittered | Hold until due, the delay D, sending at the presentation time, the DIN wire model (§4.3, §6.3–§6.5); M18; A4–A6 |
| 5 | Goldens are unstable across Macs | Strict flags, `-fno-strict-float-cast-overflow`, vendored libm, arm64 only, F9; goldens in both CI jobs (§8) |
| 6 | The block model breaks by accident: another chunk size, or a render point that moves against the request | §4.1 and §4.2; M3–M5; the goldens |
| 7 | Users expect the device's exact sound | 'Very close' is stated in the README and the About box; goldens pin host builds only |
| 8 | Storage damages a user's card or diverges from FAT | The app's own folder; temporary copies for renders; copies that keep the order; duplicate warnings; export only into an empty folder; M9 |
| 9 | Input is lost or garbled: clicks between scans, fast knob moves, MIDI bursts | Queued release at the poll points (§4.6); paced injection (§4.3); counters in the summary and the diagnostics |
| 10 | The sample-rate mapping shifts pitch and tempo: −1.6 cents and 0.092 % slow at 44.1 kHz without conversion, +145 cents at 48 kHz | AUHAL converts from 44,140.625 Hz; A11 checks the block rate |
| 11 | Licence and trademark: SLA0044 code, files without a licence, names, the faceplate, non-commercial code, or a later change of NOTICE.md upstream | §10; the licence gate M21; the stand-ins written fresh |
| 12 | Toolchain traps: Mach-O section names, the predefined `__weak`, fortify, signed `char`, header shadowing, the case clash, CRLF | §3.4, §8.2 and §9.2; the overlay instead of shadowing for `main.h` |
| 13 | The emulator's re-implementations drift from the firmware: interrupt bodies, the DataOut walk, the register model | F4 removes the copy of `main.c`; M20's hashes; M22; unknown registers fail to compile |
| 14 | Panel state silently changes a render: untouched faders are silent, jack states reroute voices, seeds change noise | Goldens fix every input (§8.4); the summary prints them |
| 15 | Hardware details nobody could verify: the WS0010 font, the knob wiper model, the fader order, the true sample rate, flush-to-zero, the main-loop rate, TIM2 during saves | Q18; A2 and A3; the device checks; the README names each of them as a model, not a measurement |
| 16 | Distribution friction: Open Anyway or `xattr` for every user, and no Intel support | The README's steps; building from source avoids the step |
| 17 | The stage is too big for one plan | Three plans (§13) |

## 15. Open questions for the owner

This draft applies each default. Each question stays open in the tracker until the owner answers.

| # | Question | Default applied in this draft | Status |
|---|---|---|---|
| Q12 | Do we build stage 1b in the agreed order: the renderer first (goldens in CI, quick listening to stage 2's changes), then the app (virtual panel, CoreMIDI, master and slave), then stage 2? The alternative is the renderer, then stage 2, and the app later | Renderer, app, stage 2, as agreed on 2026-10-23. Code starts only after the owner accepts this spec | Open |
| Q13 | Do the small firmware changes the emulator needs go into the firmware? Most leave the device's behaviour unchanged: the two NULL reads, the `strcpy` in `menu.c`, splitting `main()` into a boot and one loop pass, and saturating float-to-uint conversions. One changes the sound: the read past the end of a table in `Oscillator.c` | Yes, as separate small commits on `emulator` (F1–F9), reaching the firmware after the owner's v0.1 checks. The `Oscillator.c` fix only after a listening check on the device. The ping-pong delay report is reproduced first. F2 also changes a morph without a card, which today reads stray bytes from address 0 | Open |
| Q14 | Names and look: app 'LICHOMAT Emu', renderer 'lichomat-render', CoreMIDI port 'LICHOMAT Emu', tagline 'emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02'; a schematic panel in its own colours, no logos and no copy of the faceplate | As proposed (§10.2) | Open |
| Q15 | Distribution: arm64 only, macOS 13 or later, ad hoc signed binaries as release assets on the fork, no paid Apple developer account (Open Anyway at the first start), no Erica factory kits; users bring a copy of their card or a folder | As proposed (§10.3). Intel is not supported: an x86-64 build gives different results and can crash | Open |
| Q16 | The app's behaviour: knobs like the device (about 134 steps per turn, 22 for list parameters); an instant display; a slave MIDI input delay of about 24 ms that the user can lower; sound at the internal 44,140.625 Hz, resampled to the audio device; the app's own card folder with import and export; the UI in Dear ImGui on Cocoa and Metal | As proposed (§6). The clock jacks are an LED and buttons; an audio pulse out and clock in from audio are options | Open |
| Q17 | On DIN, MIDI clock bytes (F8) wait behind the Note Ons of the same moment, up to about 7 ms with 7 tracks. MIDI 1.0 allows realtime bytes to go ahead. Change it, in 0.1.x or in stage 2? | Recorded for stage 2; nothing changes now. G6 pins today's behaviour, and H5 already allows for it | Open |
| Q18 | Optional: photos of the LXR-02's display on the song page (the up and down arrows) and on other screens with characters outside ASCII, so that the virtual display can copy the WS0010 font | Without photos: a generic 5×8 font, with 0xC5 and 0xC6 drawn as arrows (§4.7) | Open |

## 16. Later tiers (context only, not part of this spec)

- **A browser build (WASM).** The core fits a single-threaded WebAssembly build. Safari has no Web MIDI.
  WebAssembly traps on integer division by zero, which the Euclid generator's `x % 0` needs a guard
  against. It also traps on out-of-range float-to-int conversions unless nontrapping conversions are
  enabled; with them, `-fno-strict-float-cast-overflow` gives the device's saturation.
- **Plugins (AU, VST3, CLAP).** CLAP and clap-wrapper (MIT), the VST3 SDK (MIT since 3.8) and the
  AudioUnitSDK (Apache-2.0) fit the licence. JUCE is AGPLv3 or commercial and needs Xcode. The firmware's
  static state allows one instance per process, so each plugin instance needs its own process or its own
  copy of the core library. Dear ImGui's panel code can serve a plugin editor.
- **A FAT image backend**, for exact FAT order and `dd` images: ChaN's `ff.c` with 32-bit `DWORD` and
  `LONG`, a fresh `ffconf.h` in place of the SLA0044 one, and about 40 lines of diskio over an image file.
  `ff.c` carries the same FatFs licence as `ff.h` (*checked*).
- **Linux arm64 goldens** on GitHub's arm64 Linux runners, now that libm is vendored.
- **The device's display speed** (1.6 ms per changed cell) as an option, and a CPU model.
- **Sample-accurate triggering** (E6.10) will change the goldens when it lands.
- **The core in a child process**, so that a firmware crash or a power cycle does not restart the GUI and
  the virtual MIDI ports.
