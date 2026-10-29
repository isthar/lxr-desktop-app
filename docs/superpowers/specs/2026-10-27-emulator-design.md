# LICHOMAT stage 1b: macOS emulator

| | |
|---|---|
| Date | 2026-10-27 |
| Status | Draft for the owner's review |
| Base | `timing-core` at `6a74b81` |
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
restates what it uses. Three critics then checked the draft against the code (architecture; fidelity,
tests and CI; the owner's view, licence and naming), and this version applies their confirmed findings.
The map, the readers' prototypes and the critics' experiments are kept outside the repository.
- Line numbers refer to the base commit.
- Facts that this spec adds were checked against the code at the base and are marked *checked*.
- *Measured* means a program ran it. Measurements were made at `da64603`, before the fixes E4.6–E4.9.

**Measured** on an Apple M4 Pro with macOS 26.7.1 and Apple clang 17.0.0 from the Command Line Tools
16.4. Nothing was measured on the device.
- A probe linked 68 firmware `.c` files with 509 lines of emulator code. It boots to the splash and the
  voice page, takes Note Ons through the unchanged USART3 handler, and writes deterministic 4-channel
  audio at 65–190× real time. With the real FatFs on a RAM disk, the SD boot path runs too.
- The panel, storage and MIDI readers ran their subsystems unchanged on register models in the same way.
- Renders are bit-identical between host builds with strict floating-point flags (§8). They are never
  bit-identical with the device.
- Re-run for this spec at `da64603`: the same 68 files compile and link, and 12 s of virtual time
  render in 0.2–0.4 s. Strict renders at `-O0`, `-O1`, `-O2` and at `-O0` with UBSan are byte-identical
  (SHA-256 starting `f13d248ab55dd20b` for all four).

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

**The sanitizer sweep.** On the night of 2026-10-27, after the draft, 13 agents ran the firmware on the
probe under ASan and UBSan for about 5 hours of virtual time. They found 19 distinct bugs, almost all
from fw 1.7.
- 7 corrupt data on the device. E4.6–E4.8 fix them on `timing-core` in eight commits (`0712d9a`,
  `8af1ece`, `040cdea`, `2446532`, `b0b53ec`, `a6bd8b7`, `2bb5d80`, `5b2c224`; `20dff39` hardens a test).
  One of them is the ping-pong delay report that the draft still had to reproduce (§7).
- 7 change the behaviour. E4.9 fixes three of them in the DSP. Two are in at the base: the resonant
  filter's state that stayed NaN until power-off (`eb9712a`) and the NaN pitch at pitch-EG slope 127
  (`6a74b81`). The third, a waveform of 6 or more set by MIDI CC, is to follow. Two pulse-width cases
  wait for the owner's listening check with Q13 (tracker log, 2026-10-27). §8.2 lists what still makes
  the emulator differ from the device.
- The owner's v0.1 checks wait for a new image built after these fixes (tracker Q6).

## 2. Goals and non-goals

### Goals

- **G1.** The whole LICHOMAT firmware runs on macOS: boot, menu, sequencer, sync, MIDI, storage and
  sound. Its source files compile unchanged on the emulator branch. The emulator replaces only the
  drivers at the hardware edge (§3).
- **G2.** `lichomat-render` turns a card folder, options and an event script into a 4-channel WAV, a
  MIDI-out log and a clock-out log. The same inputs give byte-identical outputs on every run, on the
  developer's Mac and on the `macos-15` CI runner (§5, §8). It renders at least 10× faster than real time
  on an Apple silicon Mac, in a Release build.
- **G3.** CI renders a set of goldens on every push and fails on any difference (§8, §9). It runs once the
  owner allows pushing `emulator` (Q19).
- **G4.** `LICHOMAT Emu` plays in real time with a virtual panel. It works as MIDI clock master and as
  MIDI clock slave, over a virtual USB port and over a DIN port that the user picks, with a constant and
  displayed latency (§6).
- **G5.** Sync and MIDI go through the firmware's own interrupt handlers, queues and decoders, so the
  emulator runs stage 1's tested path (§4.3–§4.5).
- **G6.** The firmware changes the emulator needs are small, separate commits. They leave the device's
  behaviour unchanged, with two exceptions. A morph without a card no longer uses stray bytes (F2). Band
  10 of the wavetable oscillators changes its sound, so that change waits for a listening check (F9, §7).
- **G7.** The binaries contain no ST SLA0044 code and no file without a licence. They show that they are
  unofficial. 'Erica Synths', 'LXR' and 'Sonic Potions' appear in them only in the allow-listed texts of
  §10.2, and they carry no logo and no copy of the faceplate (§10).

### Non-goals

- Sample-for-sample identity with the device. The aim is 'very close', and goldens pin host builds only
  (§8).
- A model of the CPU budget. The CPU page reads about 0 %, and the emulator shows no CPU-induced
  underruns or late steps. H12 on the device stays the reference for stage 3.
- A model of SD-card access time. Storage calls take no virtual time.
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
   │  tier hook: called back on the firmware thread while virtual time passes
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
  thread context: `HAL_Delay()` and a spin on `HAL_GetTick()` (§4.2). Inside an interrupt body, time
  stands still.
- **Interrupts are scheduled bodies.** The scheduler runs each interrupt body at its device rate on the
  firmware thread, in NVIC priority order at equal times. PRIMASK defers them.
- **The main loop is a series of passes.** A pass is one call of `appMain_loopOnce()`, the body of
  `main.c`'s `while (1)` loop moved into a function (§7, F4). The scheduler runs a pass at every audio
  block request and every 250 µs in between.
- **Audio comes in blocks.** The firmware renders 64-frame blocks at 44,140.625 Hz through its own
  handshake (§4.1). In the app, rendered frames pace the clock. The renderer runs free.
- **The tier hook.** While virtual time passes, the scheduler calls the tier back on the firmware thread
  (§3.3). The hook moves inputs in, takes outputs out, and in the app waits for the audio device.
- **Rings to the outside.** CoreAudio, CoreMIDI and the GUI exchange data with the firmware thread only
  through lock-free single-producer, single-consumer rings, and through single-writer atomic counters
  for progress and diagnostics. The firmware thread services them in the tier hook, and between two
  'run until t' calls (§6.1, §6.2).

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
| `tools/emu_golden.py` | Compares, diffs and re-blesses goldens (§8.5) |
| `.github/workflows/emulator.yml` | The emulator's CI (§9.3) |

`emu_core.h` is the only interface the two tiers use. It offers:
- power-on with a configuration: the card folder, the card switch, the jack switches, the fader levels
  and the seed;
- running the clock up to a given virtual time t. The call returns at the first pass boundary at or
  after t; a pass boundary is the end of the boot or of a pass. Between two such calls the tier may call
  the firmware directly, as the renderer does for a few options (§5.3);
- injecting timed inputs;
- sink callbacks for audio blocks, DIN bytes, USB transfers and clock-out edges. A sink callback never
  blocks;
- the tier hook (below);
- snapshots of the LCD and the LEDs;
- counters.

Every call happens on the firmware thread.

**The tier hook.** Virtual time also passes inside firmware calls that do not return for a long time:
- the boot's `HAL_Delay(1000)` at `main.c:442`, before the codec starts, and a second one at `:414`
  without a card;
- `menu_showSdError()`, which starts TIM2 (`menu.c:1994`) and waits in `HAL_Delay(2000)` (`:2039`): 1,378
  blocks rendered by TIM2 inside one call;
- the screensaver's wake loop, `while (screensaver_process() != 0) soundengine_process();`
  (`screensaver.c:75`), inside a pass, until the screensaver's current 500 ms step
  (`SCREENSAVER_PROCESS_SPEED`, `screensaver.c:36`) ends: from 0 to 500 ms, depending on when the button
  is pressed (361–465 ms in the probe's presses; R1's test pins one press exactly).

A single-threaded core cannot return from 'run until t' in the middle of such a call. So the scheduler
calls the tier hook on the firmware thread whenever `HAL_Delay()` or `HAL_GetTick()` advance the clock,
at least once per 1 ms of virtual time, and at every block request. It may also call it after any body.
The hook:
- takes the events that have arrived from the tier's rings and injects them as timed inputs (§6.4);
- takes the sinks' output and publishes snapshots;
- in the app, waits while the audio ring is full (§6.2); before the codec starts, or while no output
  runs, it sleeps to the wall clock instead;
- in the renderer, ends the render at its end time (§5.3).

The hook never calls the firmware.

**Why a hook and not a coroutine.** A coroutine would let 'run until t' return exactly at t. It needs a
second stack and a context switch: `ucontext`, which macOS marks as deprecated, or assembly, and ASan
needs fiber annotations for it. The hook keeps one plain call stack. Both tiers drive the core the same
way, and M14 checks that slicing a run does not change its output.

### 3.4 What runs unchanged, and the stub layer

**Compiled unchanged** (on `emulator`, with §7's changes): 69 of the 78 `.c` files that `Core/Src` has at
the base, plus `appMain.c`. They include `DIO.c`, `encoder.c`, `adcPots.c`, `Uart.c`, `TriggerOut.c`,
`usb_manager.c`, `usb_midiTx.c`, `seq_platform.c`, `seq_syncq.c`, `seq_core.c`, `sequencer.c`, the menu,
the preset manager and the whole DSP, dead code included.

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
  - the register blocks as plain memory: DWT, CoreDebug, FPU, EXTI, GPIOB–E, USART3, TIM2 and TIM14;
  - the HAL macros the firmware uses (`__HAL_TIM_*`, `__HAL_UART_*`, `__HAL_GPIO_EXTI_*`, `READ_REG`),
    which call the virtual device where the scheduler must learn of a write;
  - the CMSIS compiler macros, and the intrinsics in C (§8.3);
  - PRIMASK functions that call the scheduler (§4.2);
  - `#undef __weak`, because Apple clang predefines `__weak`.
- It declares only the registers the firmware uses. Kept code reads GPIOA only under `#if 0` or in
  comments (*checked*), so GPIOA is not declared. New firmware code that touches another register fails
  to compile in the emulator, which flags it.
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

**Symbols the virtual device defines.** A cut build linked with `nm` listed every symbol the kept code
and `appMain.c` need beyond libc and libm (measured on an experimental cut):

| Symbol | Definition in the emulator |
|---|---|
| `hadc1`, `hrng`, `hspi1` | Handles that only need to exist. Kept code never reads their `Instance` (`DIO.c:39`'s `hspi1.Instance = SPI1` is under `#if 0`), so no ADC1, RNG or SPI1 block is declared |
| `htim2`, `htim14` | `Instance` = TIM2 and TIM14, for the stand-in `__HAL_TIM_*` macros (`Soundengine.c:82–89`) and HAL functions |
| `huart3` | `Instance` = USART3: `Uart.c:51` reads `(&huart3)->Instance->ISR` |
| `systick_ticks` | `INDTCMZ volatile uint32_t`, as `stm32f7xx_it.c:55` defines it (`globals.h:18`) |
| `hUsbDeviceFS` | `dev_state` = `USBD_STATE_CONFIGURED`; `pClassData` points to the stand-in's class handle, whose `TxState` `usb_manager.c:139` reads |
| `usb_MidiMessages[64]`, `usb_MidiMessagesRead`, `usb_MidiMessagesWrite` | The USB input ring that the DataOut walk fills (§4.3) |
| `lcd_process_display_interrupt` and the `lcd.h` functions | The LCD model (§4.7) |
| `SDPath`, `SDFatFS`, `SDFile`, `MX_FATFS_Init`, `MX_FATFS_ReInit`, `BSP_SD_IsDetected`, the `f_*` functions | The card backend (§4.8) |
| `codec_initCsCodec`, `CDC_Transmit_FS` | The codec model (§4.1) and the USB endpoint stand-in (§4.4) |
| 12 HAL functions | The table below |

| HAL function | Semantics |
|---|---|
| `HAL_GetTick`, `HAL_Delay` | The clock (§4.2) |
| `HAL_RCC_GetHCLKFreq` | Returns 216,000,000 |
| `HAL_RNG_GetRandomNumber` | The DSP stream (§4.9) |
| `HAL_SPI_Receive`, `HAL_SPI_Transmit` | The 5 button bytes in and the 5 LED bytes out (§4.6, §4.7) |
| `HAL_TIM_Base_Start_IT` | Sets DIER.UIE and CR1.CEN |
| `HAL_TIM_Base_Stop_IT` | Clears DIER.UIE and CR1.CEN |
| `HAL_TIM_Base_Stop` | Clears CR1.CEN only; UIE stays set (`menu.c:2163`) |
| `HAL_ADC_Start_DMA` | Starts nothing, because the panel writes `adc_buf` (§4.6) |
| `HAL_NVIC_SetPriority` | Checks the priority against §4.2's table. A mismatch is a firmware fault (§5.7, exit code 3) |
| `HAL_NVIC_EnableIRQ` | Enables the EXTI4 or EXTI9_5 body (§4.2) |

## 4. The cuts

### 4.1 Audio out

**What the firmware keeps.**
- `soundengine_process()` (`Soundengine.c:79–118`) masks the TIM2 update interrupt and runs the CPU meter
  around `seq_tick()` and `soundengine_calcNextSampleBlock()`. It then parks TIM2 at CNT 1950, unmasks
  it, and runs `usb_tick()` and `preset_morph_tick()`.
- `soundengine_calcNextSampleBlock()` renders only while `bCurrentSampleValid` is not `SAMPLE_VALID`
  (0xFF). It calls `mixer_calcNextSampleBlock(&dma_buffer[h·128], &dma_buffer2[h·128])` for the free half
  h, then marks the half valid.
- The render runs from six places:
  - the main loop, 3 times per pass;
  - the immediate kit load, after every parameter (`presetManager.c:1358`, *checked*);
  - `preset_loadDrumsetRam()`, before and after its copy (`:846`, `:862`);
  - the screensaver's wait;
  - TIM2, while it is started;
  - `soundengine_init()`, once, right after the codec start (`Soundengine.c:74`). It renders half 0.

**The codec model** replaces `cs4344_cs5343.c` and its `HAL_I2S_TxCpltCallback()`.
- `codec_initCsCodec()`, which `soundengine_init()` reaches through `CodecInit()` (`Soundengine.c:48`),
  starts the block requests. Before that call, no block request exists.
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
  - `HAL_GetTick()` advances it only in a spin. The scheduler counts the calls of each segment: the boot
    is one segment, each pass is one, and so is each direct call of a tier (§3.3). The first 512 calls
    of a segment take no time. Every later call advances the clock by 216 cycles (1 µs);
  - the interrupt bodies that fall due meanwhile run, deferred while PRIMASK is set;
  - inside an interrupt body, neither call advances the clock.
- **Why `HAL_GetTick()` advances only in a spin.** The firmware polls it in a loop that must end:
  `screensaver_touch()` waits at `screensaver.c:383`, with two calls per iteration. It also calls it in
  every pass:
  - `usb_tick()` after each render reaches `usb_startTransfer()`'s call (`usb_manager.c:140`);
  - `usb_sendMidi()` reaches it once per USB message;
  - `preset_processTimer()` calls it once per dirty flag (`presetManager.c:674`, `:683`).

  At 1 µs per call, every pass would take at least 648 cycles. Simulated over 60 s, 0.39 % of block
  requests (1.20 % with both dirty flags set) would then render up to 248 µs late, and the times of MIDI
  out and clock out would move with the number of calls. Under the rule:
  - a normal pass takes no time, and neither does an immediate kit load of about 250 calls;
  - the screensaver wait still ends: after 512 free calls, each iteration advances 432 cycles;
  - the codec starts at exactly 216,000,000 cycles with a card (432,000,000 without one), a multiple of
    54,000.

**Interrupt bodies.** The bodies of `stm32f7xx_it.c` and of the I2S callback are short call lists. They
are re-implemented from these lists, because their files are ST-licensed or replaced. A drift test
hashes those bodies and the CubeMX settings behind the table's rates and priorities, and fails when one
changes (M20).

| Body | NVIC priority | When, in virtual cycles | What it does |
|---|---|---|---|
| SysTick | 0 | every 216,000 (1 kHz) from power-on | `screensaver_timer++`; applies the next queued encoder change (§4.6); `encoder_callback()` |
| TIM14 | 0 | every 54,004 (3,999.7 Hz) after `HAL_TIM_Base_Start_IT(&htim14)` | `systick_ticks++`; then `menu_tick()` 4 times while `lcd_process_display_interrupt` is set |
| USART3 | 3 | when (ISR.RXNE and CR1.RXNEIE) or (ISR.TXE and CR1.TXEIE) holds (below) | `USART3_IRQHandler()`; then the model passes a written TDR byte to the DIN sink and clears RXNE and TXE |
| OTG_FS out | 3 | at each incoming USB transfer's time (§4.3) | the endpoint stand-in's DataOut walk |
| OTG_FS in | 3 | 216,000 (1 ms) after `CDC_Transmit_FS()` | clears TxState; `usb_midiTxComplete()` |
| EXTI4, EXTI9_5 | 3 | at each clock-in or reset-in edge (§4.5), once `HAL_NVIC_EnableIRQ()` has enabled the line | sets the pending bit; the handler in `TriggerOut.c` |
| TIM2 | 4 | when its counter overflows, while started with DIER.UIE set | `soundengine_cpuBegin()`; `seq_tick()`; `soundengine_calcNextSampleBlock()`; `soundengine_cpuEnd()` |
| Block request | 5 | every 64 frames after the codec start | §4.1 |

- **TIM2** is a 32-bit counter at 54 MHz (4 cycles per count) with ARR 1500, so it overflows every 6,004
  cycles (35.98 kHz). `__HAL_TIM_SetCounter()` reaches the model.
  - CNT 1450 at a block request makes it fire 204 cycles later.
  - CNT 1950 at the end of `soundengine_process()` parks it past the next request.
  - The model follows every start and stop. The storage scenario counted 13 starts and 10 stops, and
    `loadTmpFolderFiles()` leaves TIM2 started on success.
- **USART3.** `USART3_IRQHandler()` reads ISR once and serves RXNE and TXE in the same call
  (`Uart.c:51`, `:57–72`, `:75–86`). The RDR read and the TDR write are plain member accesses that no stub
  sees, so the model does the device's flag clears itself:
  - **RX.** An arriving DIN byte (§4.3) sets RDR and ISR.RXNE. If RXNE is still set, it sets ISR.ORE
    instead, and the byte is lost, as on the device. The body runs at once if CR1.RXNEIE is set.
  - **Before RXNEIE.** RXNEIE is set only at boot step 11 (`main.c:881`), after `initMidiUart()` has
    emptied the RX FIFO (`Uart.c:167–171`). `__HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE)` reaches the
    scheduler, and runs the body at once if RXNE is pending. So bytes that arrive during the boot are
    lost, as on the device, except the one still in RDR and any that a TX slot's run of the handler
    reads.
  - **TX.** The model keeps the time at which the wire is free: the last collected byte's time plus
    69,120 cycles (320 µs at 31,250 baud). When CR1.TXEIE goes from 0 to 1, a TX slot comes at the later
    of that moment and the wire-free time. At a TX slot the model writes the sentinel 0xFFFF into TDR,
    sets ISR.TXE and runs the body. If the handler wrote TDR, its low byte goes to the DIN sink at the
    slot's time, which is the byte's start on the wire, and the next slot comes 69,120 cycles later
    while TXEIE stays set. If the handler did not write TDR, its FIFO was empty, and it has cleared
    TXEIE.
  - **After every USART3 body** the model clears RXNE and TXE. A stale RXNE would make the next TX slot
    decode the last input byte again; a stale TXE would let an RX body move an output byte into TDR that
    no sink sees.
- **EXTI.** MX_GPIO_Init arms PD4 and PD5 (`main.c:1648–1658`), but their NVIC lines are enabled only at
  the end of the boot (`main.c:917–921`). Before `HAL_NVIC_EnableIRQ()` for its line, an edge only sets
  the line's EXTI PR bit. The body then runs once at the enable, if PR is set.
- **Order.** Events at equal times run by priority, 0 first. At equal priority they run in the table's
  order, then in the order they were queued. A pass runs after every body due at its time.
- **PRIMASK.** While PRIMASK is set, no body runs. Releasing it in thread context runs every pending
  body. Inside a body, releasing it runs only pending bodies of higher priority (a lower number), as the
  NVIC would. Bodies never nest otherwise. Two bodies release PRIMASK today: OTG_FS in, through
  `usb_midiTxComplete()` (`usb_manager.c:159–164`), and TIM2, through `seq_tick()`'s mailbox lock
  (`sequencer.c:178–185`).

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
- A pass that takes no virtual time renders a block at its request time. The half rendered at request k
  then holds the triggers that `seq_tick()` fired up to that time.
- `seq_tick()` runs at least 3 times per pass, about 20 times per block. MIDI out and the clock-out
  edges therefore fall on a 250 µs grid. Nobody has measured the device's main-loop rate.

**Why this rate** (map question B9; the readers disagreed). The MIDI reader called `seq_tick()` once
per block. That gives MIDI out and clock out up to 1.45 ms of jitter. It also lets USB input back up:
`seq_tick()` parses at most 8 USB messages per call, and a USB host can send about 23 packets in one
block's time. A pass every 250 µs keeps both close to a fast main loop. The probe ran passes at this rate
at 65–190× real time. One rule for both tiers makes the app play what the goldens pin, and M14 checks it.

**Busy-waits.**
- `HAL_Delay(1000)` at boot ends because time passes. So does `HAL_Delay(2000)` in `menu_showSdError()`
  (`menu.c:2039`); TIM2 is started there, so audio keeps rendering in TIM2. The tier hook runs during
  both (§3.3).
- The screensaver's `HAL_GetTick()` polls end because time passes in the spin.
- `while (sendDisplayBuffer() != 0)` (`menu.c:2000`, `screensaver.c:274`, `presetManager.c:2323` and
  `:2700`) ends because the virtual LCD finishes each call at once.
- `sd_formatRoutine()` spins on `encode_readButton()` without the HAL. It is unreachable, because the
  card backend never reports `FR_NO_FILESYSTEM` (§4.8).
- A wall-clock watchdog catches any other spin. The firmware thread counts its scheduler entries in an
  atomic counter, and a watchdog thread reads it. No progress for 10 s of wall time is a hang (§11).
  The time the tier hook spends waiting for the audio device or the wall clock does not count; the hook
  measures it with `mach_absolute_time()`.

**Not modelled.** CPU time, so hardware check H7's 'stays in sync during SD save' cannot be reproduced.
SD access time. The spread-spectrum modulation of the main PLL.

### 4.3 MIDI in

**DIN.** One byte takes 320 µs of wire time at 31,250 baud.
- At each byte's time, the byte reaches USART3 as §4.2 describes, and `USART3_IRQHandler()` runs. It
  lives in `Uart.c` and runs unchanged.
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
  `usbd_cdc.c` does. The stamp is taken once per transfer (`usbd_cdc.c:934–935`, `:1027`).
- A transfer carries at most 16 packets (64 bytes). At most 16 packets enter in any 1 ms: a packet that
  would be the 17th within 1 ms of the first packet of that window waits until the window ends.
  Transfers are not spaced otherwise.

**What the firmware keeps** (stage 1, unchanged):
- `seq_tick()` drains the sync queues first. It then parses at most 32 DIN bytes and 8 USB messages per
  call.
- A Program Change acts only with `MIDI_FILTER_PROG_C` on, and only on the global channel.
- `MIDI_FILTER_SYSTEM` gates the DIN and USB sync entries, not the jack.
- Notes and CCs act at parse time, without a stamp. Thru routing also happens at parse time.

**Rules for both tiers.**
- No event enters before its time. An event with a future stamp is held.
- Stamps are kept. In the app the time comes from each CoreMIDI packet's timestamp, not from the
  callback (§6.4). Stamping at callback time would turn clocks into one transfer per buffer. By N21's
  tables that costs a tempo error of up to 0.97 %, 1.35 % and 2.80 % at 256-, 512- and 1024-sample
  buffers.
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
- `uart_sendMidi()` writes into `fifo_midiTx` (256 bytes) without running status, then enables TXE
  (`Uart.c:111–118`).
- The TX slots of §4.2 collect one byte each, 320 µs apart while bytes wait. A byte's time is the time
  of the slot that collected it, which is its start on the wire. An idle transmitter takes the first
  byte at once, as on the device.
- This keeps a property of the device. At equal ticks `midiClockOut()` runs last, and the FIFO is strict,
  so an F8 waits 0.96 ms behind each Note On of its tick. With 7 tracks on one step that is 6.7–7.0 ms.
  This was measured on the host model, and code reading says the device behaves the same. Q17 keeps it.

**USB.**
- `usb_sendMidi()` (unchanged, N20) queues USB-MIDI packets in its 128-packet ring. When the endpoint is
  free, it starts a transfer of up to 15 packets through `CDC_Transmit_FS()`.
- The stand-in passes the transfer to the USB sink at that time. TxState stays 1 for 1 ms, then the
  OTG_FS in body runs.
- Every transfer ends 1 ms after it starts, so none stays in flight for the 100 ms of the stall rule
  (`usb_midiTx.c:76`). `hUsbDeviceFS` is always configured, and its `pClassData` points to the
  stand-in's class handle, so the no-host rule never empties the ring either (`usb_manager.c:130–139`).

**Sinks.** Tier 1 logs each DIN byte and each USB transfer with its time (§5.5). Tier 2 sends them at
their presentation time (§6.5).

### 4.5 Sync jacks

- **Clock in** is PD4, on the falling edge. At the edge's time the scheduler sets EXTI PR bit 4 and calls
  `EXTI4_IRQHandler()` in `TriggerOut.c`, once the line is enabled (§4.2). The handler pushes
  `SEQC_EV_PULSE` only in sync mode 1. The PPQ, 4 or 24, reaches the core through `seq_setClockInPpq()`.
- **Reset in** is PD5, on both edges, inverted. The scheduler sets `GPIOD->IDR` bit 5 to the pin level
  (low means RESET_ON), sets EXTI PR bit 5 and calls `EXTI9_5_IRQHandler()`, once the line is enabled.
  The reset acts as a run gate (N4).
- **Clock out** is PC13, and the firmware writes `GPIOC->ODR` directly.
  - The virtual device samples bit 13 after every body and every pass, and reports each change as an
    edge at that time.
  - A pulse that starts and ends within one body or pass is lost. That happens only when
    `seqc_process()` catches up several pulses in one call; on the device such a pulse would be a few
    microseconds wide.
  - While the sequencer runs, a pulse comes every 4 ticks when the prescaler selects it (2, 4, 8, 16, 24
    or 48 PPQ), and lasts one 96-PPQN pulse (`TriggerOut.c:345–355`): 5.21 ms at 120 BPM.
- The reset out and the 7 voice trigger outs compile to nothing (`VOICE_TRIGGER_OUTS 0`).

### 4.6 Panel in

The panel sits at the hardware edge of the unchanged `DIO.c`, `encoder.c` and `adcPots.c`. Each of their
input paths compares with its previous poll. So the panel model queues events and releases them at the
poll points, within these rates:

| Control | Register edge | Release rule |
|---|---|---|
| Buttons 0–39 | the 5 bytes that `HAL_SPI_Receive(&hspi1, …, 5)` returns at each `dio_process()`; button n is bit n%8 of byte n/8, 1 = pressed | at most one transition per button per scan, which is once per pass |
| Button 40 | `GPIOB->IDR` bit 7, 1 = pressed | the same |
| Encoder turn | `GPIOE->IDR` bits 13 and 14, Gray code, forward 00 → 01 → 11 → 10 | one change per SysTick (1 ms); 4 changes per detent; no limit on queued detents |
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

- **Encoder turn.** The firmware itself keeps at most 4 decoded detents that the main loop has not read
  yet (`MAX_PENDING_DETENTS`, `encoder.c:34`, `:88–90`), and `encode_read4()` hands out one per pass. At
  one Gray change per ms a detent takes 4 ms, so a normal main loop never reaches that limit, and the
  panel model adds none of its own.
- **Encoder push.** Measured over 10 sampling phases: a 40 ms push was seen in 10, 35 ms in 8, and 30 ms
  or less in none.
- **Knobs.** Knob i edits the parameter shown in display column i. The wiper model is two triangle waves
  90° apart, wiper a peaking at 90° and wiper b at 180°; it is unverified on the device. Measured under
  it: 1° per poll gives 134 steps per turn on a normal parameter and 22 on a list parameter; 5° per poll
  gives 71; a 40° jump gives one step.
- **Knobs at power-on.** Every knob starts at 0°: wiper a at mid-scale (0x800 << 4) and wiper b at 0.
  The boot's two `adc_checkPots()` calls compare with `adc_buf_old` and `adc_potValues`, which start at 0
  (`adcPots.c:26`, `:35`). At 0° the first position is 0 or 360, so they make no edit (a 360 on a list
  parameter is 22, doubled to 44, which is outside the 40 window of `adcPots.c:220–233`). Under the
  model, start angles from about 2° to 80° would make a +1 edit at boot.
- **Faders.** Fader i sets the gain the voices read as `adc_getFaderVolume(i−1)`: drums 1–3, the snare,
  the clap (the cymbal voice), and both hi-hats. `adc_checkFaders()` reads one fader per call,
  round-robin, with a hysteresis of 40 counts. The gain is (v/4096)², 0 below 1 %, so full scale gives
  0.99951. The voices smooth it with a one-pole filter.
- **Output jacks.** A voice whose jack is unplugged moves to the output that
  `mixer_checkOutJackAvailable()` picks (`mixer.c:79–192`). With all jacks plugged or none, the routing
  is unchanged. The physical polarity of the detect pins is unverified.

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
- LED 40 is `GPIOB->ODR` bit 8 (`LED_43_Pin`, `main.h:114–115`).
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
- dot files are skipped, which covers `.DS_Store`, `._` files, `.fseventsd` and `.Spotlight-V100`; names
  that are not 8.3 are skipped with one warning each;
- two files for one slot (the same `NN-` prefix and extension) give a warning that names both; the older
  one loads, as on a card the firmware wrote;
- one open file or sub-directory at a time, the root not counted; a second gives
  `FR_TOO_MANY_OPEN_FILES`, as the FatFs lock does;
- while the card switch is out, every call returns `FR_NOT_READY`;
- it never reports `FR_NO_FILESYSTEM`, and `f_mkfs` returns `FR_DENIED`;
- its POSIX code sits in its own file, because FatFs's `DIR` clashes with `<dirent.h>`.

**Copies keep the order.** Whenever the emulator copies a card (the renderer's working copy, the app's
import and export), it copies file by file in the backend's order, skips dot entries, and sets each
copy's birth time in that order. A copy therefore loads the same files as its source.

**The device's storage quirks stay**, from the same source (map question C3): the older duplicate wins;
`/PROJ.IDX` is written only on a load; globals are saved only with a project; the init kit is only partly
initialised; the boot ignores the result of the project load; a write error leaks the file lock. E4.7
range-checks `/PROJ.IDX` and the kit slot bytes on `timing-core` (`b0b53ec`, `2446532`). A fix of any
other quirk goes into the firmware first, as its own change.

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

After the boot, the current folder is `/TMP` (`main.c:900–901`), and `preset_loadProject()` and
`preset_saveProject()` end in `preset_initTmpFolder()` (`presetManager.c:2912`, `:2487`). So the save
page's kit and pattern saves write into `/TMP` (`menu.c:2107`), and only a project save writes the slots
of `/PROJnn`.

`presetManager.c` calls `soundengine_process()` and `midiParser_ccHandler()`, so storage runs on the
firmware thread.

### 4.9 Randomness

- On the device, `GetRngValue()` reads the hardware TRNG through `HAL_RNG_GetRandomNumber(&hrng)`. Its
  run-time consumers are the noise oscillators (`Oscillator.c:340` and its inlined copies), the LFO noise
  (`lfo.c:76`) and the sequencer's probability draw (`seq_cbRandom()`, `sequencer.c:2115–2119`). Nothing
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
- The encoder push reads 1 (released). The knobs are at 0° (§4.6). The jack switches, the card switch
  and the faders take their configured states.
- SysTick runs from cycle 0. TIM2 and TIM14 are stopped, and no block request exists.
- `hUsbDeviceFS` is configured, with `pClassData` set (§3.4, §4.4).
- USART3's CR1.RXNEIE and CR1.TXEIE are clear, and the EXTI4 and EXTI9_5 lines are disabled (§4.2).

**Boot** is `appMain_boot()`: `main.c`'s code from `dio_init()` to the EXTI enables, moved (F4). With
`LOAD_PROJECT_ON_BOOT 1` and `UPDATE_BOOTLOADER 0` it runs:
1. `dio_init()`, `lcd_init()` and `menu_init()`;
2. the ADC DMA start, then `adc_checkPots()` twice;
3. `MX_FATFS_ReInit()`;
4. the splash, 'LICHOMAT' and 'v0.1 unofficial', which NOTICE.md 7(c) requires;
5. `f_mount()`. Without a card, 'SD error: No Card!' and `HAL_Delay(1000)` (`main.c:408–415`). Then
   `HAL_Delay(1000)` (`:442`);
6. `/proj00`: stat, mkdir if missing, chdir;
7. `soundengine_init()`, which starts the codec and renders half 0 (§4.1);
8. `preset_loadGlobals()`. If that fails: 'glo.cfg error', `preset_createGlobals()`, then
   `HAL_Delay(1000)` (`main.c:479–488`): about 689 block requests without a pass or TIM2. All but the
   first count as underruns, because only half 0 was rendered;
9. `morphParameters = mainParameters[0]`;
10. `preset_loadProjectNames()` 64 times, then the last project from `/PROJ.IDX`;
11. `initMidiUart()`, the ORE clear and the RXNE enable (`main.c:875–881`);
12. TIM14 starts, then `buttonHandler_init()`;
13. the `/TMP` overlay: `preset_initTmpFolder()`, `loadTmpFolderFiles()`, `preset_LoadTmpKit()`,
    `preset_LoadTmpPat()` and `preset_resetAllSaveTimers()`;
14. `lcd_process_display_interrupt = 1`, and EXTI4 and EXTI9_5 enabled.

Without a card, the boot runs `copyClear_clearCurrentPattern()`, `preset_init(0)` and
`preset_loadDefaultGlobals()` instead of steps 8–10. `main.c:833` tests the result of `f_mount()`, not of
`preset_loadProject()`, so a failed project load at boot shows no error. Both stay as they are.

**Pacing in the app.** Before the codec starts there is no audio to pace the clock. So until then, and
whenever no audio output runs, the tier hook paces virtual time by the wall clock (§3.3). The splash
therefore shows for the device's 1 s. The renderer never paces.

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
  --param NAME=VALUE      set a parameter through the menu's edit path (5.3); repeatable
  --seconds T             length after script time 0; default 10
  --bars N                length in 4/4 bars of the internal tempo, instead of --seconds
  --events FILE           event script (5.4)
  --no-start              the script starts the transport itself; nothing is pressed at t0
  --faders F1,F2,F3,F4,F5,F6   fader levels 0-1; default all 1
  --jacks all|none|LIST   plugged output jacks, LIST from L1,R1,L2,R2; default all
  --seed N                random seed; default 1
  --as-booted             keep /TMP, as the device does at power-on; default: /TMP is left out
  --wav-rate 44141|44100  rate written into the WAV header; default 44141
  --stereo out1|out2|sum  write a stereo WAV of one output, or of (out1 + out2) / 2
  --midi-log FILE         MIDI-out log
  --clock-log FILE        clock-out log
  --panel-log FILE        LCD and LED snapshots at the script's marks
  --summary FILE          counters and times, in JSON
  --keep-card DIR         keep the card copy's final state in DIR
```

`--seed-card DIR` writes an init card into the empty folder DIR and exits (§6.7). `--bars` needs an
internal tempo, from `--bpm` or from the card; in EXT or MID sync without `--bpm` it is a usage error.

**The stereo sum.** `--stereo sum` writes ⌊(out 1 + out 2) / 2⌋ per sample, computed in int32 and shifted
right by 1. It is byte-exact and cannot clip. It plays 6 dB below a single output. The device never sums
its two outputs, so a saturating sum would add a distortion the device does not have.

**Defaults for what the card lacks** (map question B8).
- An internal-sync project plays at its stored tempo.
- If the loaded project's sync is EXT or MID and the script sends no clock to that input, the renderer
  uses 120 BPM in internal sync and prints a warning.
- Faders full, all jacks plugged, `/TMP` left out, seed 1, pattern mode with the pattern active after
  boot, 4 channels.

**The WAV header rate** (map question B5; the readers disagreed).
- The default is 44,141 Hz, 8.5 ppm above the true 44,140.625 Hz. A DAW then plays the file at the
  device's pitch and tempo, off by 0.015 cents.
- A 44,100 Hz header would make the file play 0.092 % slow and 1.6 cents flat. A pattern at 128 BPM would
  drift a beat against a DAW grid at 128 BPM in about 11 minutes.
- `--wav-rate 44100` exists for tools that reject other rates. The samples are the same either way, and
  goldens hash the samples without the header (§8.4).

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
3. **The prelude** starts at the first pass. It selects what to play, sets the tempo and the `--param`
   values (below).
4. **Script time 0 (t0)** is the first block request at which two things hold:
   - at least 100 ms have passed since the prelude's last event;
   - `preset_morphBusy()` (F10) has returned 0 at every pass of the last 100 ms, so no parameter
     transfer of the preset manager is running. A kit load through the morph path takes 247
     `soundengine_process()` calls, about 20 ms, and such transfers start on nearly every boot and
     prelude: the project load, `preset_init()` without a card, `preset_LoadTmpKit()` with `--as-booted`,
     and kit loads from the menu or the buttons.
5. At t0 the renderer presses START/STOP, or SHIFT + START/STOP for `--song`. With `--no-start` it
   presses nothing, and the script starts the transport, for example with a MIDI Start in a slave
   scenario.
6. The WAV holds the blocks that start playing at t0 or later, for the given length. The logs are timed
   from t0.
7. When the clock reaches the end, the tier hook closes the output files and the renderer exits,
   wherever the firmware is at that moment (§3.3).

**How the prelude acts.**
- `--pattern P`: the panel selects pattern P with the buttons a user presses on the device. The plan
  derives the sequence from `buttonHandler.c`, and a test checks that the pattern is then P.
- `--song S`: the panel selects song S through the menu's load page, as a user does. A test checks the
  selection, so a broken walk fails loudly. A direct call would have to copy the load page's lines
  (`menu.c:2289–2292`), and that copy would drift silently if the load page later does more.
- `--bpm B` and `--param NAME=VALUE` use the menu's own edit path, the one `menu_encoderChangeParameter()`
  ends in (`menu.c:3040–3090`):
  1. clamp the value to the parameter's range by its `parameter_dtypes[]` entry, as the menu does;
  2. set `activeParameters[p]`;
  3. call `frontPanel_sendData()` with `MIDI_CC` or `CC_2` for a sound parameter, or
     `menu_parseGlobalParam(p, v)` for any other.

  For `--bpm`, 2–255 is a tempo in internal sync, `ext` is 0 and `mid` is 1. NAME is a parameter's enum
  name without `PAR_`, in any case, from a table in `emu/render/` written as `{ "shuffle", PAR_SHUFFLE }`
  pairs, so its values come from `Parameters.h`; a parameter number also works. The parameters for which
  `menu_encoderChangeParameter()` has a special case (the LFO voices and the modulation targets) are
  refused with exit code 1. These options skip the menu walk because a walk to an arbitrary menu item
  would break with every menu change; stage 2's groove settings need the same path for A/B renders.

**Where the renderer calls the firmware directly.** Everywhere else it presses buttons, turns knobs and
sends MIDI. It makes a direct call only between two 'run until t' calls, at a pass boundary, and never
from the tier hook (§3.3). These are the only direct calls:

| Caller | Firmware calls |
|---|---|
| `--bpm`, `--param`, the `param` event | the menu's edit path above |
| `--seed-card` | `preset_createGlobals()`, `preset_init(0)`, `savefileNames[0][PROJECT_NAMES]` = 'Init', `preset_saveProject(0)` (§6.7) |
| `save-kit N`, `save-pattern N` | `preset_saveDrumsetRam(N, 0)`, `preset_savePatternRAM(N)` (§5.4) |
| `save-project N` | the save page's project save (`menu.c:2158–2173`): `HAL_TIM_Base_Stop(&htim2)` while the sequencer is stopped, `preset_saveProject(N)`, then `HAL_TIM_Base_Start_IT(&htim2)` while it is stopped |
| t0 | `preset_morphBusy()` (F10) |
| test-only hooks | the M-tests' fault, hang and `screensaver_timer` hooks, compiled into test binaries only |

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
| `encoder` | ±detents | turns the encoder, one Gray change per ms, every detent kept |
| `push` | optional ms | encoder push held for ms (default 50, at least 40) |
| `knob` | 1–4, ±degrees | turns a knob, at most 1° per pass |
| `fader` | 1–6, level 0–1 | moves a fader |
| `param` | NAME=VALUE | sets a parameter as `--param` does (§5.3) |
| `save-kit`, `save-pattern` | slot 0–63 | copies the active kit or pattern into RAM slot N, the RAM half of the save page's save |
| `save-project` | project 0–63 | the save page's project save (§5.3): it first stores the edit buffer into the save page's kit slot (`presetManager.c:2319`; kit 0 after boot) and the active pattern into its own slot, then empties `/PROJnn` and writes every slot from RAM |
| `din` | hex bytes | MIDI into DIN, 320 µs per byte |
| `usb` | hex bytes | MIDI into USB, as transfers of USB-MIDI packets |
| `din-clock` | BPM or `off` | a steady MIDI clock into DIN from this time, until the next `din-clock` line or the end of the render |
| `usb-clock` | BPM and optional buffer ms, or `off` | a steady MIDI clock into USB, until the next `usb-clock` line or the end. With a buffer length, each transfer carries the clocks of one buffer and is stamped at the buffer's start, as a DAW sends them |
| `jack-clock` | BPM and PPQ (4 or 24), or `off` | steady pulses into clock in, until the next `jack-clock` line or the end |
| `jack-pulse` | none | one clock-in pulse |
| `reset` | `on` or `off` | the reset-in level |
| `card` | `in` or `out` | the card switch |
| `jacks` | `all`, `none` or a list | the output-jack switches |
| `mark` | name | a snapshot of the LCD and the LEDs into the panel log |

Songs need no save event: song edits live in `seq_songData`, and `save-project` writes them.

`param` and the save events are direct calls (§5.3), so they act at the first pass boundary at or after
their time. Every other event enters as a timed input at its own time, the clocks' among the other
lines', and inputs at equal times in script order: a `din` line inside a `din-clock` run enters between
its clocks.

A script error stops the renderer with exit code 1 and names the file and line.

### 5.5 Outputs

- **WAV**: 16-bit PCM with 4 channels, L1, R1, L2, R2. Out 1 comes from `dma_buffer2` and out 2 from
  `dma_buffer`. The header rate follows `--wav-rate`. With `--stereo` the file has 2 channels. The RIFF
  chunk size, 36 + the data, is 32 bits, so a length with more than 4,294,967,259 bytes of data is a usage
  error (exit 1): above 12,162.7 s with 4 channels, 24,325.5 s with 2. For `--bars` at the card's tempo
  it is found after the prelude.
- **MIDI log**: one line per DIN byte and per USB transfer. Each line gives the cycles since t0, the
  milliseconds since t0 with 3 decimals, the port (`DIN` or `USB`), and the byte or the packets (8 hex
  digits each).
- **Clock log**: one line per clock-out edge, with the cycles, the milliseconds and the new level.
- **Panel log**: one line per mark, with the time, the mark's name, the two LCD rows (other than
  printable ASCII as `\xNN`), the cursor, and the 41-bit LED mask in hex.
- **Summary**: the boot's end and t0 in cycles, the number of blocks, the underruns before and after t0,
  every direct event, the number of the script's timed inputs, `seqp_syncOverflows()`,
  `usb_midiTxDropped()`, the fields of `seqc_stats()`, the wall time, the toolchain (§8.4) and every
  warning. A short form goes to stderr. The warnings name the card, not its copy, and come last on
  stderr at every exit, also with an error, which a warning may explain. The commit that `--version`
  and the summary name is read at each build.

### 5.6 Card fixtures

- Fixtures are minimal card folders under `emu/tests/cards/`, a few KB each (map question C7). They hold
  no factory kit.
- `emu/tests/cards/make.sh` makes them from the firmware, in this order:
  1. it seeds a card (`--seed-card`);
  2. it runs one `lichomat-render` script per fixture, with `--keep-card`. The script edits through the
     panel, `param` and the save events, and it ends with `save-project`;
  3. it deletes every slot file that no golden of the fixture names, and deletes `/TMP` and `/PROJ.IDX`
     unless a golden needs them.
- The script is committed with the fixtures. A test runs `make.sh` into a temporary folder and fails if
  the result differs from the committed fixtures byte for byte (M23). So a changed fixture can be told
  apart from a firmware change.
- A render needs at least one project folder with `PRJ.NFO`, the `.PAT` files to play and the `.SND`
  files their `kitNr` bytes name (kit 00 when the autoload is off). `GLO.CFG` is optional and sets the
  tempo and the autoload; without it the boot shows 'glo.cfg error' for 1 s (§4.10). A `.SNG` file is
  needed for a song (measured).
- Missing slots are skipped. Missing patterns keep the `seq_init()` defaults: length 16, with steps 0,
  8, … active at volume 127. Missing kits stay all zero, and missing songs stay `SONG_END`.

### 5.7 Exit codes

| Code | Meaning |
|---|---|
| 0 | Rendered |
| 1 | Bad options or script; the message names the option, or the file and line |
| 2 | Card error: the folder is missing or unreadable, or the project to load has no `PRJ.NFO` |
| 3 | Firmware fault: a signal (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT), a sanitizer report or a model check (§3.4). The message gives the virtual time and the last 32 scheduler events |
| 4 | Hang: no scheduler progress for 10 s of wall time |
| 5 | An output file cannot be written |

## 6. Tier 2: LICHOMAT Emu

### 6.1 Threads and rings

| Thread | Runs | Scheduling |
|---|---|---|
| Main (Cocoa) | the GUI at 60 frames per second | default |
| Firmware | the virtual device and the tier hook. Only this thread calls `emu_core` | time-constraint policy |
| CoreAudio callback | copies frames out of the audio ring; records the time-map points | CoreAudio's own |
| CoreAudio listeners | device-change and overload notifications | CoreAudio's own |
| CoreMIDI read procs | push incoming packets with their host time | CoreMIDI's own |
| MIDI out | sends DIN messages and USB transfers at their presentation time | time-constraint policy |
| Watchdog | reads the firmware thread's progress counter (§4.2) | default |

Every cross-thread path:

| Path | Producer → consumer | Through |
|---|---|---|
| Panel events (buttons, encoder, push, knobs, faders, jack and card switches) | GUI → firmware | the panel ring |
| GUI commands (D, the output mix, the clock-in generator, import or export start, power cycle, restart) | GUI → firmware | the command ring |
| LCD and LED snapshots | firmware → GUI | the snapshot ring; the GUI draws the newest and drops older ones |
| Acknowledgements ('card out applied', 'card in applied') | firmware → GUI | the acknowledgement ring |
| Audio frames | firmware → CoreAudio callback | the audio ring |
| Time-map points | CoreAudio callback → firmware | the time-map ring |
| Device changes and overloads | CoreAudio listeners → firmware | the notification ring |
| MIDI in | each read proc → firmware | one MIDI-in ring per port |
| MIDI out | firmware → MIDI-out thread | the MIDI-out ring; each entry carries its presentation time (§6.5) |
| Diagnostics and progress | firmware → GUI and watchdog | single-writer atomic counters |

- The rings are single-producer, single-consumer and lock-free, with fixed sizes allocated at start.
- The callback and the listeners never block, allocate or lock. They may signal one semaphore that wakes
  the firmware thread.
- `MIDISend` and `MIDIReceived` are Mach IPC, so they stay on the MIDI-out thread.
- **Scheduling.** The firmware thread and the MIDI-out thread run with `THREAD_TIME_CONSTRAINT_POLICY`,
  set with `thread_policy_set()`: the period is the output buffer's duration, the computation 25 % of
  it, and the constraint the period. The firmware thread also runs the card backend's file I/O, the
  autosave included, so a default-priority thread next to a busy DAW could miss the ring's slack.
  The MIDI-out thread's wake-up precision is the jitter of MIDI out for receivers that ignore
  timestamps.
- **App Nap.** The app holds an `NSProcessInfo` activity with `NSActivityUserInitiated` and
  `NSActivityLatencyCritical` while it runs, so App Nap and timer coalescing do not throttle the wall-
  clock pacing when no audio output runs.

### 6.2 CoreAudio

- The default output unit (AUHAL) takes float32 stereo at 44,140.625 Hz. Measured: the unit accepts the
  rate and reads it back as 44140.625000 Hz.
- Its converter resamples to the device rate at Normal complexity
  (`kAudioUnitProperty_SampleRateConverterComplexity`) and maximum quality
  (`kAudioUnitProperty_RenderQuality` = `kRenderQuality_Max`). Measured at 48 kHz, that adds 1.09 ms of
  latency. Mastering complexity at maximum quality would add 13.05 ms to every latency in the app, so it
  is not used.
- The converter's latency is already inside `mHostTime` (§6.3). It is a term of the render lead and of
  the diagnostics, and it is never added to the presentation time again.
- The tier hook waits while the audio ring holds at least the largest callback request so far plus
  4 blocks (256 frames, 5.8 ms); otherwise the firmware thread renders. Virtual time therefore follows
  the rendered frames, not the wall clock. On this Mac the callbacks asked for 470–471 frames, so a frame
  FIFO sits between the blocks and the callback.
- At 512 frames, the 256 frames left after a callback are fewer than the next request, so the refill
  must land within one callback period.
- The firmware thread calls 'run until t' in slices of one block. Between slices, at a pass boundary,
  it applies the commands that must not land inside a pass: the card switch for import and export
  (§6.7).
- The callback converts int16 to float by dividing by 32,768.
- **Output mix**, a setting:
  - (out 1 + out 2) / 2, computed in int32 and shifted right by 1, as `--stereo sum` (the default, so that
    any routing is heard; it plays 6 dB below the device's single output and cannot clip);
  - out 1;
  - out 2;
  - all 4 channels to the first 4 channels of a device that has them.
- On an output stall or a device change, the app reopens the unit, re-anchors the time map, recomputes
  D's default (§6.4) and drops input events older than 100 ms (§4.3).

### 6.3 The time map

The firmware thread maps host time (mach ticks) to virtual time and back.
- Each callback reports a point: the virtual frame of its first output frame, and the presentation host
  time of that frame. The presentation time is the callback's `mHostTime` plus the device latency
  (`kAudioDevicePropertyLatency`) and the output stream's latency (`kAudioStreamPropertyLatency`),
  converted with `mRateScalar`.
- The safety offset and the converter latency are already inside `mHostTime`, and are not added again.
  Measured on the built-in output at 48 kHz and 512 frames: `mHostTime` − callback entry is 11.66 ms
  without conversion (10.67 ms of buffer plus the 48-frame safety offset), and 12.74 ms with the
  converter of §6.2.
- The map is the line through the latest point, with the slope measured over the last 2 s. The slope
  follows the drift of the audio device's clock against the host clock.
- When a new point is more than 1 ms off the line, the map re-anchors on it.
- On this Mac: built-in output at 48 kHz, a 512-frame buffer (range 15–4096), a device latency of
  60 frames, a safety offset of 48 frames and a stream latency of 690 frames (14.4 ms); the mach
  timebase is 125/3 ns per tick (measured).

### 6.4 CoreMIDI in

- **USB side.** The app creates a virtual destination named 'LICHOMAT Emu' (Q14). It sets the
  destination's `kMIDIPropertyAdvanceScheduleTimeMuSec` to 0, so CoreMIDI hands over each packet at its
  timestamp. Measured: with 0, a three-packet list stamped +10, +20 and +30 ms reached the read block as
  three calls, each at its stamp.
- **DIN side.** The user binds a source: a hardware MIDI interface or another app's virtual port. Its
  bytes enter the DIN wire model (§4.3).
- **Packets.** A `MIDIEventList` holds several `MIDIEventPacket`s, each with its own timestamp, and a
  list with distinct past stamps arrives as one call (measured). So the read procs push one ring entry
  per packet: (host time, port, words). A timestamp of 0 means 'now'.
  - On the USB side each packet is its own USB transfer of USB-MIDI 1.0 packets on cable 0, with the CIN
    of each message, SysEx included. Consecutive packets with the same timestamp, 0 included, share a
    transfer, up to 16 USB-MIDI packets. A DAW that sends a buffer's clocks unstamped in one list
    therefore gives one transfer, which G8 covers. Transfers follow §4.3's pacing.
  - On the DIN side, each packet's bytes enter the wire model at its entry time.
- **When an event enters.** The tier hook takes each packet from its ring and injects it at its entry
  time. On each port, the entry time is max(map(stamp) + D, now, the entry time of the port's previous
  event). The third term keeps a port's messages in order when a re-anchor moves the map back or D
  changes. On DIN the wire rule then spaces the bytes from that time.
- **D**, the slave input delay, applies to MIDI and jack input while the firmware's sync mode is MIDI (2)
  or clock jack (1), read through `seq_getExtSync()`. In internal sync, D is 0, so played notes get the
  lowest latency.
- **The render lead.** The render lead of an input event is the presentation time (§6.3) of the
  firmware's virtual time when the event is injected, minus the event's stamp. Its largest value, L, is
  the presentation offset of a callback's first frame (its presentation time minus the host time at
  which the callback runs), plus (2 × the largest request + 5 blocks) / 44,140.625 Hz. The ring holds up
  to the largest request + 4 blocks + one partial block beyond the callback's frames, and an event can
  wait one callback period for its drain.
- **D's default** is L + 1 ms. The firmware thread computes L from the device's latencies, the largest
  request and the callbacks' measured `mHostTime` lead (`mHostTime` minus the host time at the
  callback's entry), whenever the device, the buffer size or the clock-out-as-audio option (§6.8, one
  block more) changes; the output mix does not change it. D then stays fixed, so the D the app shows stays constant. The diagnostics compare
  L with the measured leads of drained events.

  | Output | Buffer | L | Default D |
  |---|---|---|---|
  | Built-in, 48 kHz (60 + 690 frames of latency) | 512 frames | 28.4 + 28.6 = 57.0 ms | about 58 ms |
  | Built-in, 48 kHz | 128 frames | 20.4 + 12.6 = 33.0 ms | about 34 ms |
  | 48 kHz, 60 frames of device latency, no stream latency | 512 frames | 14.0 + 28.6 = 42.6 ms | about 44 ms |
  | 48 kHz, 60 frames of device latency, no stream latency | 128 frames | 6.0 + 12.6 = 18.6 ms | about 20 ms |

  The 512-frame rows use the measured 12.74 ms of `mHostTime` lead; the 128-frame rows compute it as
  2.67 ms of buffer + 1.00 ms of safety offset + 1.09 ms of converter, with requests of 118 frames.
- The user can lower D to 0. The app shows the effective D in ms, so that a DAW's MIDI clock sync delay
  can compensate it.
- **Why D.** A model of §6.2–§6.4 (48 kHz, 512 frames, MIDI clock at 90, 128 and 174 BPM) shows it:
  - with D below L, every event enters at 'now', and the latency follows the arrival phase: it varied by
    about 12 ms, and the draft's D of 23.6 ms gave the same latencies as D = 0;
  - with D of at least L, the latency is constant: D + 0–1.45 ms, the last term from the 64-frame block.
  The MIDI reader's simulation, which rendered inside the callback and had no ring, gave the draft's
  2 × buffer + device latency + 1 ms (about 24 ms). That figure is too short for this pipeline.
- **In internal sync**, played notes and MIDI thru come out with the render lead: 45–57 ms at 512 frames
  and 29–33 ms at 128 frames on the built-in output. A smaller buffer lowers it.
- The app uses the MIDI 1.0 protocol APIs of macOS 11 (`MIDIDestinationCreateWithProtocol`,
  `MIDIInputPortCreateWithProtocol`, `MIDIEventList`), not the `MIDIPacketList` calls that the macOS 15.5
  SDK marks as going to be deprecated.

### 6.5 CoreMIDI out

- The firmware thread computes each output's presentation time with the time map's inverse (§6.3) and
  puts it into the MIDI-out ring entry.
- **USB side**: a virtual source named 'LICHOMAT Emu'. The MIDI-out thread waits until the presentation
  time and then calls `MIDIReceivedEventList` with that time as the timestamp. Sending earlier would give
  receivers that ignore timestamps the MIDI early, by the render lead of 45–57 ms at the default buffer
  (§6.4).
- **DIN side**: the destination the user binds. A `MIDIEventList` carries only complete messages, without
  running status. So the firmware thread assembles the DIN sink's bytes into complete MIDI 1.0 messages,
  and each message carries the presentation time of its first byte. A realtime byte between the bytes of
  another message is a message of its own, at its own time. SysEx, if any, is split into MIDI 1.0 SysEx
  packets stamped with the time of its F0. The MIDI-out thread calls `MIDISendEventList` with that
  timestamp, and CoreMIDI schedules it. The interface's UART then reproduces the device's wire timing; a
  stamp from the last byte would make every note 0.64 ms late.

### 6.6 The virtual panel

The panel uses Dear ImGui on its native Cocoa and Metal backends (Q16). Its Metal shaders compile at run
time from source, so the Command Line Tools are enough to build it.

- **Layout**, schematic and in our own colours (Q14), in rows by function:
  - the LCD and the encoder;
  - 4 knobs under the LCD's 4 columns, since knob i edits the parameter in column i;
  - 6 faders, labelled by their voices;
  - 41 buttons with their LEDs, labelled with their function names;
  - a jack strip with the 4 output-jack switches, the card switch, the clock-out LED, and the clock-in
    and reset-in controls (§6.8).
- **No branding.** No LXR or Erica Synths name or logo, and no copy of the faceplate's layout, colours,
  printing or graphics. The rows above group controls by function, not by their places on the faceplate,
  and A1 has the owner confirm it.
- **Mouse and keys.**
  - Buttons: mouse down and up are button down and up. A secondary click latches a button down until the
    next click, so that SHIFT + step works with one mouse. The Shift key holds SHIFT, and the space bar
    presses START/STOP.
  - Knobs: a vertical drag turns 1° per point, 360 points per turn; the wheel turns 5° per notch. The
    panel model ramps at most 1° per pass, so a turn gives about 134 steps on a normal parameter and 22
    on a list parameter, as on the device under the wiper model (Q16).
  - Faders: a vertical drag; a double click returns to full.
  - Encoder: the wheel or a drag, one detent per notch or per 12 points; a click pushes it. Every detent
    is kept (§4.6).
- **Reads.** The GUI draws from the LCD snapshot and the latched LED mask, once per frame.
- **Writes.** Panel events go into the panel ring. The firmware thread releases them at the poll points
  (§4.6).
- **State at power-on**: faders full, all jacks plugged, the card in, the knobs at 0°.

### 6.7 The card folder

- The app works on its own folder, `~/Library/Application Support/LICHOMAT Emu/Card`, and never on the
  user's card (Q16).
- **First run.** An empty folder gets an init project from the firmware's own code. The bundle's copy of
  `lichomat-render` (§10.3) runs `--seed-card` in a separate process before the app boots. It runs the
  steps that `sd_formatRoutine()` runs after its confirmation (`main.c:165–176`): default globals, kit 0
  from `preset_init(0)`, project 0 named 'Init', saved by `preset_saveProject(0)`. No factory kit is
  shipped (Q15).
- **Card out first.** Import and export start by posting 'card out' through the command ring. The
  firmware thread applies it between passes, so no save is running then: the autosave can be writing
  `tmp.snd` or `tmp.pat` inside a pass (`presetManager.c:670–689`), and `preset_saveTmpKit()` even starts
  TIM2 (`:198`). It then sends 'card out applied' through the acknowledgement ring. The GUI touches the
  folder only after that.
- **Import** from a folder, such as a mounted SD card:
  1. the app takes the virtual card out and waits for 'card out applied';
  2. it moves its current folder to `Card backups/YYYYMMDD-HHMMSS`, named after the local time;
  3. it copies the source file by file in the source's order, skipping dot entries: `readdir()` order on
     a FAT volume, so a copy from a card keeps the card's order, and the backend's order (§4.8) on any
     other file system;
  4. it power-cycles the emulated device (§6.9), as after swapping a card and switching on.
- **Export card** to a folder the user picks. The folder must be empty apart from dot entries, so a
  freshly formatted card that macOS has mounted (with `.fseventsd` and often `.Spotlight-V100`) is
  accepted. The app takes the virtual card out, waits for 'card out applied', copies the card in order,
  and puts the card back in only after the copy has finished. The firmware remounts it as after a card
  swap.
- **Export project** writes one `/PROJnn` folder into a card folder the user picks, such as the owner's
  card with their other projects. If the target already has `/PROJnn`, the app asks, moves that folder into
  `Card backups` on the Mac, then writes the new one in order. It does not touch `/PROJ.IDX` or `/TMP`.

### 6.8 Clock jacks

- Always present (Q16): the clock-out LED, latched per frame; a clock-in button, one pulse per click,
  with a steady pulse generator at a set BPM and PPQ; and a reset-in switch.
- Options, off by default (Q16), built in the last increment (§13):
  - **Clock out as audio.** The chosen output channel carries the clock alone, at +1.0 while PC13 is
    high and 0 otherwise, edge to edge at the edges' frames. So a pulse lasts one 96-PPQN pulse, as on the
    device's jack: 5.21 ms at 120 BPM. The mix is not sent to that channel. On a 2-channel device the
    mix then plays as (left + right) / 2 on the other channel. With this option on, the app holds each
    block back by one block (1.45 ms), so that every edge of a block's time is known before the block is
    played; L grows by that block (§6.4).
  - **Clock and reset in from audio.** One input channel carries the clock, and an optional second one
    the reset. Each has a threshold (default half of full scale). Clock edges count on the rise; a reset
    channel above its threshold is a high jack voltage, which the model inverts to the pin as the
    hardware does (§4.5). An input frame's host time is its input callback's `mHostTime` plus its frame
    offset, minus the input device's and stream's latency; the time map turns it into virtual time, and
    the edge enters at that time plus D. This needs macOS's microphone permission, which the app asks for
    only when the option is turned on. `Info.plist` carries `NSMicrophoneUsageDescription`. An ad hoc
    signed app is identified by its code hash, so macOS asks again after each new release; the README
    says so.

### 6.9 Power, crashes and settings

- **Power cycle** restarts the app's process, because only a new process resets the firmware's static
  state. The virtual MIDI ports disappear for that moment, as the device's USB port does.
- **Endpoint IDs.** The settings keep the CoreMIDI unique IDs of the app's virtual source and
  destination. Each launch sets `kMIDIPropertyUniqueID` on them to the stored values, as `MIDIServices.h`
  advises, so a DAW that refers to endpoints by unique ID keeps its routing across power cycles, imports
  and crashes. If a stored ID is taken, the app keeps the new one and stores it. Measured: without this,
  three launches gave three different IDs.
- **Bound DIN endpoints** are stored by unique ID and name. When a bound endpoint disappears, the binding
  shows 'not connected'. When an endpoint with the stored unique ID, or else the stored name, appears,
  the app binds it again.
- **Crash.** A fault signal on the firmware thread writes `crash-YYYYMMDD-HHMMSS.txt` into the app's
  folder with async-signal-safe calls: the virtual time and the last 64 scheduler events and inputs. The
  app then ends. The next launch shows the note.
- **Hang.** The watchdog (§4.2) stops the audio and shows a message with a 'Restart' button.
- **Settings** persist in the app's folder: the output device, buffer size and mix; the DIN source and
  destination; the endpoint IDs; D; the window layout; the options of §6.8.
- **Diagnostics** show:
  - the stream formats, the converter setting, and the measured block rate (689.7 blocks per second
    expected);
  - output underruns, the effective D, L, and the smallest and largest measured lead of drained events;
  - the input latency of the last 2 s: the smallest and largest value of an event's entry time,
    presented through the time map, minus its stamp. With D at least L it equals D;
  - the firmware thread's load, as render time over real time;
  - the counters of §5.5.

## 7. Firmware changes (Q13)

- Each change is one small commit on `emulator`, with its own test, before the emulator code that relies
  on it. R1 lands F1–F10 before any `emu/` code (§13). F6b, F11 and F12, for fw 1.7 bugs the emulator
  found in R1 (Q21, Q22) and while R2 was planned (Q23), land after R2.4: `7f5732f` (F12), `7399c7d`
  (F11), `cf8fb4d` (F6b).
- The changes reach the device in the next firmware release, v0.2, only after the owner's v0.1 checks,
  with the device checks U4–U9 (§12.4, increment R6). A 0.1.x version cannot carry them: 'v0.1.1
  unofficial' has 17 characters, and the configure step stops above the splash's 16
  (`CMakeLists.txt:33–39`).
- 'The base build' below is the firmware built from the commit before the change.
- New and changed C files in `Core/` use CRLF (HANDOFF.md, review row R9.4). `Core/sources.cmake` uses LF,
  like `Core/CMakeLists.txt`.

| ID | Change | Effect on the device | Acceptance |
|---|---|---|---|
| F1 | `modulationNode.c`: a node whose destination parameter has no pointer (`PAR_NONE`) keeps its original value | None. Today such a node reads its original value from address 0 (ITCM) and never uses it | M15: CC 1 with nodes at destination 0 does not crash; U4 |
| F2 | `morphParameters = mainParameters[0]` on the boot path without a card too | Without a card, morphing interpolates towards kit slot 0 instead of bytes from address 0 | M15: boot without a card and turn the morph; U5 |
| F3 | `menu.c:1079` and `:1083`: the clear-page text is written within its row, with the same visible result | None | M15: the LCD snapshots of the clear-automation pages equal the base build's, built with `-D_FORTIFY_SOURCE=0`; U6 |
| F4 | `main.c`'s boot becomes `appMain_boot()` and its loop body `appMain_loopOnce()`, in `Core/Src/appMain.c` (below) | None; the code moves, the logic stays | `git diff --color-moved` shows moves plus the edits listed below only; the image check finds the splash; the F4TEST check (below); ITCM use is unchanged; U7 |
| F5 | `sequencer.c`: `seq_cbRandom()` returns `SEQ_RANDOM()`, a macro that defaults to `GetRngValue()` | None | The release `.bin` is byte-identical to the base build's |
| F6 | `Oscillator.c`: a saturating float-to-uint32 conversion at the 8 FM sites (lines 118, 141, 166, 194, 232, 516, 543 and 577) and in `freq2PhaseIncr()` (line 25). On the Cortex-M7 it is the plain cast, which `VCVT` already saturates | None | The release `.bin` is byte-identical; M1: the host version equals `FCVTZU` for edge values and NaN |
| F6b | `Oscillator.c`: `freq2PhaseIncr1024()` and `freq2PhaseIncr32767()` (lines 47–55) use F6's conversion too (Q22). The first passes 2^32 from `REAL_FS` (44,140 Hz) up, which U8's recipe reaches: note 127 times the pitch envelope's peak factor 33 is 414 kHz. The second passes it from 32 × `REAL_FS` (1.41 MHz) | None: on the device the conversion is the plain cast. No device check | The release `.bin` and `.map` are byte-identical (`cf8fb4d`); M15: `M15_f9_band10` plays U8's recipe in full, pitch modulation 127 instead of 32, under UBSan |
| F7 | `AudioCodecManager.c`: `dma_buffer` and `dma_buffer2` get 4-byte alignment, and so does any other `int16_t` buffer accessed as `int32_t` that UBSan's alignment check finds | None. Both buffers already sit at 4-byte-aligned addresses (`0x20019e1c` and `0x2001a01c` in the release map) | The release `.bin` is byte-identical |
| F8 | `Core/sources.cmake`: the module source lists leave `Core/CMakeLists.txt`, with the same files in the same order | None | The release `.bin` and `.map` are byte-identical |
| F9 | `Oscillator.c:471` and `:175`: the interpolation reads `table[row][itg & 1023]` | It changes the sound in two cases (below) | M15: no read past the tables under ASan; listening check U8 before it reaches a release |
| F10 | `presetManager.c` and `PresetManager.h`: `uint8_t preset_morphBusy(void)` returns `morph_state` (`presetManager.c:41`), which is file-static today | None. Nothing in the firmware calls it, and the build's `--gc-sections` drops it | The release image is equivalent to the base build's: the same functions and veneers, equal instruction by instruction with branch targets as symbol names, and every other section byte-identical (`tools/elf_equiv.py` on `emulator`). The `.bin` itself differs: the new function's input section shifts the ids GNU ld names its long-branch veneers after, so the 58 veneers between flash and ITCM change order (R1, `6a56e76`); M17's t0 check |
| F11 | `appMain.c`: `morphParameters = mainParameters[0]` right after `menu_init()`, before the boot's first `adc_checkPots()` (Q21). With a knob off 0° at power-on that call edits, and the edit's `preset_getMorphValue()` (`menu.c:4304`) read `morphParameters[index]` through NULL (`presetManager.c:2263`). F2's line and the card path's (`appMain.c:356`) stay | None. The read took a byte of ITCM near address 0, and `interpolate()` with morph 0 (`activeParameters[PAR_MORPH]` is still 0 there) returns the active value exactly; the boot's later `preset_init(0)` or kit load replaces the edit anyway. No device check | M15: `M15_f11_knobs_at_boot` powers on with the four knobs at 40°, with and without a card, under UBSan; `M8_fw_no_edit_at_boot` without the morph target it used to set. The image changes in `appMain_boot()` only, the one section of `appMain.c`'s object that differs (0x2d4 → 0x2d0 B), and the code after it moves by 4 bytes (`7399c7d`) |
| F12 | `buttonHandler.c`, `SELECT_MODE_PATTERN`: a pattern select by the step buttons sends `SEQ_REQUEST_EUKLID_PARAMS` with `menu_getActiveVoice()`, as the commented-out argument meant and the other senders do (`:493`, `:1560`), instead of `menu_activePage` (16), which `CortexParser.c:887–897` passed to the Euclid getters' `uint8_t[7]` arrays (Q23) | After the select, `activeParameters[PAR_EUKLID_*]` hold the active voice's length, steps and rotation, not stray bytes (5, 0, 0 on the host). Only the Euclid page shows them, and the pattern generator mode, the one way to open it, and a voice button on it ask for them again first (`buttonHandler.c:493`, `:1560`). So the page should look as in v0.1; U9 checks it | M15: `M15_f12_euklid` selects pattern 20 by the step buttons with voice 3 active, under UBSan: the parameters hold voice 3's values, and the Euclid page shows them. The image changes in `buttonHandler_seqButtonPressed()` only, which keeps its size: `tools/elf_equiv.py` finds that one function different (`7f5732f`); U9 |

**F4 in detail.**
- The boot is `main.c` from `dio_init()` at line 319 to the EXTI enables at line 921. The loop body is the
  `while (1)` loop at lines 922–997. `sd_formatRoutine()`, `checkMainLoc`, the splash definitions with
  their fallbacks (`main.c:56–63`), and the `UPDATE_BOOTLOADER` code (the include and typedefs at lines
  16–33, the branch at 543–807) move with them.
- `main()` keeps the CubeMX init and `MX_USB_DEVICE_Init()`, then calls `appMain_boot();` and runs
  `while (1) appMain_loopOnce();`.
- The only edits that are not moves:
  - (a) the conditional that spans the boundary (`#if UPDATE_BOOTLOADER` at line 543, `#else` at 807,
    `#endif` at 995 inside the loop body) closes at the end of `appMain_boot()` and reopens around the
    body of `appMain_loopOnce()`, so that both values of `UPDATE_BOOTLOADER` give balanced code;
  - (b) `res` (line 371) and `lastSdState` (line 897), locals of `main()` that the loop reads
    (lines 927–947), become file-scope statics of `appMain.c`, and their declarations in the boot become
    assignments;
  - (c) `appMain.c` declares `hadc1`, `hspi1` and `htim14` `extern`; `main.c` keeps defining them, and
    `main.h` declares only `hi2s2`, `hi2s3`, `huart3` and `htim2`;
  - (d) `Core/CMakeLists.txt` passes `FIRMWARE_NAME` and `FIRMWARE_VERSION` to `Src/appMain.c` instead
    of `Src/main.c` (`Core/CMakeLists.txt:34–36`);
  - the new `appMain.h`;
  - `main.c`'s `#include "appMain.h"` and its two calls;
  - `appMain.c`'s include block: `main.c`'s list without `usb_device.h`, which is SLA0044 and outside the
    stub set, with `main.h` and `fatfs.h` kept in both files.
- If F8 lands first, `appMain.c` goes into `LXR_SOURCES_PLATFORM` in `Core/sources.cmake`.
- **The F4TEST check.** Today `check_image.py` cannot notice defines that miss `appMain.c`: the fallbacks
  'LICHOMAT' and '0.1' equal the CMake values at 0.1 (`check_image.py:73–77`). So a build configured with
  `-DLICHOMAT_NAME=F4TEST` must show 'F4TEST' in the image. A scratch copy with `UPDATE_BOOTLOADER` set
  to 1 must also compile; it prints its `#warning`.

**F6 and the compiler flag.** With `-fno-strict-float-cast-overflow` (§8.2), every float-to-int cast in
the firmware already follows arm64's saturating conversion, which matches the M7's. F6 and F6b still make
the 11 known sites defined C. That keeps the sanitizer's float-cast check on for all other code, which
stage 2's timing code needs.

**F9, the table read.** `calcWavetableOscBlock()` and `calcFmBlock()` interpolate between
`table[row][itg]` and `table[row][itg + 1]`, with itg up to 1023. So they read `table[row][1024]`.
- For rows 0–9 that is the next row's first sample. Every row of `sawTable` starts with 0, and every row
  of `triTable` with −32,767 (*checked*). The value read is the right one by luck.
- For row 10 it reads past the table. On the device it reads the first bytes of whatever the linker put
  next, `CSWTCH.11` in the reference build. On the host it reads whatever follows, which depends on the
  memory layout.
- Band 10 starts at 14,080 Hz (`Oscillator.c:34–42`, clamped at 10 at `:744–745`). That is above the
  highest coarse note, note 127 at 12,543.85 Hz (`MidiNoteNumbers.h:149`), so only pitch modulation
  reaches it: the pitch envelope multiplies the frequency by up to 33 (`DrumVoice.c:188–190`,
  `MidiParser.c:94–97`, `PITCH_AMOUNT_FACTOR` 32). So F9 changes the attack of high voices with strong
  pitch envelopes, which the owner's kits may contain.
- The fix reads the row's own first sample. Below row 10 the old read took the next row's first sample
  instead. That makes two changes to the device's output, each once per period, at the period's last
  sample, weighted by its fraction:
  - oscillators in band 10 change by the difference between the stray value and the right one;
  - the rect waveform changes in every band. Its rows start with different values (rows 0–10: 0, 0, −1,
    0, 3, 10, 38, 136, 396, 2879, 338, from `wavetable.c`), so the change is at most 7 LSB in row 4
    (every pitch below 440 Hz) and 260, 2483 and 2541 LSB in rows 7, 8 and 9 (1.76–3.5, 3.5–7 and
    7–14 kHz). Found in R1 (`a932777`); the other waveforms' rows all start alike.
- On `emulator` the fix lands early, because goldens must not depend on the memory layout. It reaches a
  firmware release only after a listening check on the device (Q13, U8).

**The ping-pong delay report.** LXR02XXL's PATCHES.md reported that the ping-pong delay's pointers can
run past `FX_BUFFER` after a mode switch. The study's UBSan runs did not reproduce it; the sanitizer
sweep did. A switch from normal to ping-pong with a pointer above 0x4000 wrote up to 32 KB past
`FX_BUFFER`. E4.8 fixes it on `timing-core` (`2bb5d80`). M16 keeps the scenario as a regression test.

**Further sanitizer findings** follow the same rules: one commit each, a test, and a device check
wherever the device's output could change. The emulator found three in fw 1.7, two in R1 and one while R2
was planned, and the owner approved their fixes for v0.2: F11 (Q21), F6b (Q22) and F12 (Q23) above. The
emulator no longer works around any of them: R1's `M8_fw_no_edit_at_boot` set the morph target itself,
`M15_f9_band10` kept the pitch modulation at 32, and R2's plan was to turn the bounds checks off in the
Euclid getters. Only B13 and B14 (`emu/sanitize/pwm_ignorelist.txt`) still wait, for U8.

## 8. Fidelity and golden tests

### 8.1 What is pinned, and where

- **Goldens pin host builds, compared exactly** (map question B1): the WAV's samples, the MIDI log and
  the clock log, and the panel log where a golden pins it (§8.4). Fidelity to the device is 'very
  close', and it is not measured. The host variants differ from each other at 70–96 dB SNR.
- **Platform** (map question B2; the readers disagreed): macOS on arm64 with Apple clang from the Command
  Line Tools. Goldens are made on the developer's Mac and checked there and on the CI runner `macos-15`.
  - arm64 matches the device where x86-64 does not: out-of-range float-to-int conversions saturate, and
    `x % 0` gives x. `EuklidGenerator.c` stores a length of 16 as 0 (`:235–236`) and later divides by it;
    x86-64 raises SIGFPE there.
  - Without saturation, x86-64 renders differed from arm64 in 9.6–10.4 % of samples, by up to 12,121 LSB
    (measured).
  - CI builds with the Command Line Tools too (`DEVELOPER_DIR`, §9.3), and every golden records the
    toolchain it was blessed with (§8.4).
  - This stage has no Linux job. A Linux arm64 job is a later option (§16).
- **libm is vendored.** Measured at `da64603` with the strict flags, the firmware objects import six libm
  functions: `sinf`, `cosf`, `expf`, `exp`, `logf` and `fmod`.
  - `fmod` is exact in IEEE arithmetic, so every libm gives the same result.
  - The other five come from Arm Optimized Routines, the family newlib uses on the device. `emu/libm`
    defines them and is built with §8.2's flags. The executable's own definitions win over
    `libSystem`'s at link time.
  - Linking cannot stop the compiler from folding a call at compile time. At `-O1` and `-O2`, Apple clang
    folds `expf` and `exp` of a constant with the build machine's libm, and turns `sinf(x)` and
    `cosf(x)` of one argument into a call of `libSystem`'s `__sincosf_stret` (measured). So the firmware
    objects are compiled with `-fno-builtin-sinf -fno-builtin-cosf -fno-builtin-expf -fno-builtin-exp
    -fno-builtin-logf` (§8.2). Measured: with them, all four calls stay calls at `-O0` and `-O2`.
  - Today's live call sites are not affected: 1PoleLp's `expf`, the compressor's `exp`, `expf` and
    `logf`, and the distortion's `fmod` stay calls at every level. Only the dead `graphic.c` loses a
    `cosf`. The flags guard stage 2 and 3 code.
  - M19 checks the result (§12.1).
  - Why: the owner's Mac (macOS 26.7.1) and the runner (macOS 15.7.9) have different system libraries,
    and a macOS update must not change a golden.

### 8.2 Floating point

The firmware objects and the core are compiled with:

| Flag | Why |
|---|---|
| `-std=gnu11` | The firmware's dialect; it also provides `M_PI` |
| `-O2` in a Release build, `-O0` in the sanitizer build | `-O0`, `-O1` and `-O2` give identical renders with the flags below, with or without UBSan (measured). The sanitizer job's goldens check it on every push (§8.5) |
| `-ffp-contract=off` | Default FMA contraction on arm64 changed 0.16–0.23 % of samples by 3–7 LSB (measured) |
| `-fno-fast-math` | `-ffast-math` is not deterministic across optimisation levels: between `-O0` and `-O2`, 47,724 of 882,688 samples differed, by up to 399 LSB (measured) |
| `-fno-strict-float-cast-overflow` | Out-of-range float-to-int casts then follow `FCVTZU` and `FCVTZS`, which saturate like the M7's `VCVT`. Without it, clang folds a constant `(uint32_t)-1.0f` to 3,758,096,384 and `(uint32_t)5e9f` to 4,294,967,040; with it, to 0 and 4,294,967,295 (measured for this spec) |
| `-fno-builtin-sinf -fno-builtin-cosf -fno-builtin-expf -fno-builtin-exp -fno-builtin-logf` | The compiler never folds or combines the vendored functions (§8.1) |
| `-funsigned-char` | Plain `char` is unsigned on the device and signed in Apple clang on arm64 |
| `-fno-strict-aliasing` | The mixer and the FX bus access `int16_t` buffers as `int32_t` |
| `-w` | For the firmware files only, as in `tests/`. The firmware build stays their warning gate |

- **A 'firmware-like' flavour** (map question B3): a CMake option adds `-freciprocal-math`. It
  reproduces the device's band choice at exact octave frequencies (440, 880, 1760 … Hz), where
  `f × rcp(440)` lands just below the integer. It is deterministic and about 73 dB from the strict build.
  It exists for listening; goldens and releases use the strict build.
- **The timing core under these flags.** Stage 1's host tests build `seq_core.c` with the device's
  `-ffast-math -freciprocal-math`, and the core fits the tempo in double precision. So `tests/` builds one
  more copy of the core with `-fno-fast-math -ffp-contract=off`, the §8.2 flags that GCC also accepts,
  plus `-fno-strict-float-cast-overflow` only when the compiler is Clang (GCC rejects it, and `build.yml`
  builds `tests/` with GCC on Linux). The T-tests run against this copy too. Measured: all 28 tests pass
  with the three cores built strict.
- **Not reproduced**, by design:
  - the device's `-ffast-math` code: 286 fused multiply-adds against 27 explicit `fma` calls, and heavy
    double precision in `Distortion_process()` and `Compressor_process()`;
  - flush-to-zero, which `FPDSCR.FZ` gives the device only in interrupt renders.
- **The NaN guard** is no longer a difference. Up to fw 1.7 and v0.1, `-ffinite-math-only` reduced
  `removeMaliciousSample()` to a plain return on the device, while a strict host build kept it. E4.9
  (`eb9712a`) makes it test the exponent bits of the stored state through a volatile read, so it works
  on both.
- **Known differences from the device**, from the sanitizer sweep:
  - **A waveform of 6 or more set by MIDI CC** makes the oscillator read a stale stack buffer, so a render
    depends on the stack layout, as F9's did. No golden sets it, and the sanitizer build's pattern
    initialisation (§9.2) would expose a golden that did. E4.9 is to fix it in the firmware (§1).
  - E4.9 fixed the filter NaN and the pitch-EG NaN on `timing-core`, so neither occurs at the base.

### 8.3 Intrinsics

The stand-in HAL header implements the CMSIS intrinsics in C, with the M7's results:
- the active ones: `__SSAT` (56 sites), `__QADD16` (43), `__PKHBT` (14), `__QSUB16` (3) and `__CLZ` (2);
- `__CLZ(0)` returns 32, as the M7's `CLZ` does. That selects band 4 for every wavetable oscillator below
  440 Hz; `__builtin_clz(0)` is undefined;
- the declared-only ones: `__SMLALD`, `__QADD`, `__QSUB`, and `__get_APSR`, which returns 0 because only
  dead code reads it.

M1 checks each against edge cases.

### 8.4 What a golden fixes

- A golden is an event script whose header lines hold its renderer options (`#! options`) and its
  expectations (`#expect`, below).
- Each golden runs in a fresh process, because the loaded state depends on history: `preset_init()`
  copies only parameters 0–238, a V2 pattern keeps the old step timing, a V1 pattern keeps the old
  `kitNr`, and short song files keep old entries.
- Its inputs are fixed: a card fixture (copied), faders full (`adc_buf` 0xFFF0, which `adcPots.c` turns
  into 0.99951), all jacks plugged unless the golden sets them, knobs at 0°, seed 1 for both random
  streams (§4.9), `/TMP` left out, an explicit tempo or clock, and t0 after the kit transfer (§5.3).
- **`emu/tests/goldens/goldens.json`** holds, per golden:
  - the SHA-256 of the WAV's sample data, without the header, and the header's channels, rate and bits
    as values. A change of the `--wav-rate` default therefore changes no golden;
  - a CRC-32 per chunk of 4,096 frames (92.8 ms) of sample data, 108 chunks for a 10 s golden;
  - the peak and the RMS of each channel;
  - the SHA-256 and the line count of the MIDI log, of the clock log, and of the panel log where the
    golden pins it;
  - the toolchain of its bless: the first line of `clang --version` and `xcrun --show-sdk-version`.
- **Expectations** say what a golden's path must show, independent of the hashes. They are checked on
  every run, before the hashes:

  | Line | Holds when |
  |---|---|
  | `#expect midi PORT SS MIN..MAX` | the MIDI log has MIN to MAX messages on PORT (`DIN` or `USB`) whose status byte matches SS: two hex digits, `n` matching any low nibble (`9n` Note On, `8n` Note Off) |
  | `#expect clock MIN..MAX` | the clock log has MIN to MAX rising edges |
  | `#expect energy CH DBFS` and `#expect silent CH` | channel CH (L1, R1, L2 or R2) has an RMS after t0 of at least DBFS, or is digital silence |
  | `#expect summary FIELD MIN..MAX` | a counter of the summary is in range |
  | `#expect lcd MARK TEXT` | row 1 or 2 of the panel log at MARK contains TEXT |

- Each golden also names a broken variant (`#broken OPTIONS`): the same render with the covered path cut,
  such as internal sync for G9. Its expectations must fail on that variant, which `emu_golden.py check`
  verifies once per golden (M14). So a golden whose path stops being exercised fails, even after a
  re-bless.

### 8.5 Mismatch and bless

- A mismatch fails the test. It prints the first differing chunk with its time span, the peak and RMS of
  each channel in both renders, and the first differing line of each log. It also prints the blessed and
  the current toolchain, and flags a difference between them as a likely cause; a toolchain difference
  alone does not fail the test. It keeps the new outputs, and CI uploads them.
- `tools/emu_golden.py diff ID [--against REV]` locates a difference to the frame. It builds the
  renderer at REV (default: the last commit that changed `goldens.json`) in a temporary export, renders
  the golden there and here, and prints the first differing frame and channel and the first differing
  log line.
- `tools/emu_golden.py bless` with one or more golden IDs renders them again and rewrites their entries.
  It refuses a golden whose expectations fail. A bless is a commit of its own, and its message says why
  the output changed. The milestone report lists each bless of an audio golden, so that the owner can
  listen to the uploaded renders.
- Goldens run in both CI jobs: the Release build at `-O2`, and the sanitizer build at `-O0` with ASan,
  UBSan and pattern-initialised locals (§9.2). Equal hashes in both show that the renders depend neither
  on the optimisation level, nor on the sanitizers, nor on uninitialised locals. Measured: the probe's
  10 s renders hash the same with `-ftrivial-auto-var-init=pattern`, with `=zero` and without either, on
  the boot without a card (`f13d248ab55dd20b`) and on the RAM-FatFs boot (`487066e05014e2c5`).

### 8.6 The goldens

| ID | Covers | Its expectations show | Broken variant |
|---|---|---|---|
| G1 | The init card, pattern 0 at 120 BPM, 4 bars: boot, kit load, every voice | energy on out 1; no underrun after t0 | all faders at 0 |
| G2 | The FX bus: tube, fold and rat distortion, ring modulator, compressor, and the delay in normal and ping-pong mode, one render each | the FX page names the type at a mark | the FX set to off |
| G3 | Song mode over three patterns, with the kit autoload | the song page's positions at marks | `--pattern 0` instead of `--song` |
| G4 | The panel: mutes, a roll, a pattern change by buttons, a knob turn and a fader move. It also pins its panel log | the LED mask at marks shows the mutes; the LCD shows the new pattern | the script's button events removed |
| G5 | Master: MIDI out (Note On and Off, F8, Start, Stop, Program Change) and clock out at 24 PPQ | `9n`, `8n`, F8, FA, FC and `Cn` counts on DIN and USB; the clock-out edge count | `--bpm ext` |
| G6 | The DIN F8 behind 7 Note Ons of one step (Q17) | 7 `9n` on the step; the F8 count | 1 track instead of 7 |
| G7 | DIN slave at 128 BPM: Start, Stop, SPP and Continue, and a Program Change sent before a Start (N14) | energy after the Start; the summary's sync counters | the sync mode internal |
| G8 | USB slave with one transfer per 11.6 ms buffer, at 120 and 137 BPM (N21) | energy after the Start; the summary's sync counters | the sync mode internal |
| G9 | Clock in at 4 and 24 PPQ, with the reset gate | energy while the gate is open and silence while it is closed | the sync mode internal |
| G10 | Probability and noise with seed 1, with a probability edit during playback | the `9n` count on DIN below the pattern's number of active steps, which start at 100 % probability | the probability edit removed |

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
  (`project(… VERSION x.y …)`). It passes `FIRMWARE_NAME` and `FIRMWARE_VERSION` to `appMain.c`, as the
  firmware build does after F4, so the emulator's splash is the firmware's.
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
- **Build types.** `CMAKE_BUILD_TYPE` defaults to Debug, as in `tests/`.
  - Release builds at `-O2` without sanitizers. It is the build that ships, and the one that runs M14's
    speed check.
  - Debug builds at `-O0`.
- **`LICHOMAT_EMU_SANITIZE`** selects sanitizers. Its default is `undefined` in a Debug build, because
  ASan binaries hang inside the sandbox used for development, and empty in a Release build. A sanitizer
  build also compiles the firmware objects with `-ftrivial-auto-var-init=pattern`, because ASan and UBSan
  do not detect reads of uninitialised locals, and macOS has no MemorySanitizer. CI's sanitizer job uses
  `address,undefined`.
- `LICHOMAT_EMU_FP` is `strict` (the default) or `firmware-like` (§8.2).
- `EuklidGenerator.c` is built without the sanitizer's integer-division check, because arm64 gives
  `x % 0` the device's result. Any other per-file exception needs its reason in `emu/CMakeLists.txt`.
- **Signing.** The app bundle contains its own copy of `lichomat-render` in `Contents/MacOS/`, which
  first run needs (§6.7). After linking, the nested renderer is signed ad hoc first, then the bundle
  (`codesign --force --sign -`).
- Configure and build need no network: Dear ImGui and the libm sources are in the repository.
- `tools/check.sh` builds `emu/` and runs its tests when it runs on macOS on arm64. The warning count
  stays 0.

### 9.3 CI

A new workflow, `.github/workflows/emulator.yml`, runs on push and on pull request. `build.yml` stays as
it is.

- Both jobs set `DEVELOPER_DIR=/Library/Developer/CommandLineTools`, so they build with the Command
  Line Tools, as the owner does, and not with the image's default Xcode. On image `macos-15-arm64`
  20260907.0337.1 the Command Line Tools are build 16.4.0.0.1.1747106510 with Apple clang 17.0.0
  (clang-1700.0.13.5), the same build as the owner's Mac. The image also carries Xcode 16.4 (the
  default) and Xcode 26.0.1–26.3.
- Both jobs fetch CMake 3.31.12 and Ninja 1.13.2 with the URLs and SHA-256 sums that
  `tools/setup-toolchain.sh` pins, copied into the workflow as `build.yml` copies the Arm toolchain's.
  The script itself always fetches the Arm toolchain, which the emulator does not need.
- The configure step records `clang --version` and `xcrun --show-sdk-version`; the summary prints them.

| Job | Runner | Steps | Expected time | Timeout |
|---|---|---|---|---|
| `emu-release` | `macos-15` (arm64) | configure a Release build with `-DLICHOMAT_EMU_SANITIZE=` (empty); build everything; run ctest (host tests and goldens); check that `otool -L` of both binaries lists only `/usr/lib/` and `/System/Library/` paths, skipping the first line, which is the file's own path; upload `lichomat-render` and the app, zipped with `ditto` and signed ad hoc; on a failure, upload the golden outputs | under 10 minutes | 30 minutes |
| `emu-sanitize` | `macos-15` (arm64) | configure a Debug build with `address,undefined` and without the app; build; run ctest (host tests, goldens and the sanitizer scenarios); on a failure, upload the golden outputs | under 15 minutes | 45 minutes |

- The expected times come from measurements on the owner's M4 Pro: the 68-file probe compiles in 5.7 s
  serially, the ImGui app builds in 14 s, and renders run at 128× real time at `-O2` and 16× at `-O0`
  with UBSan. The runner is a 3-core M1 VM, and ASan typically costs about 2× more.
- A Release build with UBSan would link `@rpath/libclang_rt.ubsan_osx_dynamic.dylib` from the build
  machine's toolchain directory (measured with `otool -L`), and would not start on a Mac without it.
  The `otool` step catches that.
- The fork is public, so the macOS runners cost nothing.
- The runner images list no audio device (not verified at run time), so CI renders to files only.
- The workflow runs only once `emulator` is pushed (Q19).

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
  - the Corresponding Source, from the same place: the repository `github.com/isthar/lxr-desktop-app` at
    the commit each binary names. A release names its tag; a CI artifact from an untagged commit names
    that commit;
  - `LICENSE`, `NOTICE.md` and the third-party notices, inside the app bundle and next to the renderer;
  - the Appropriate Legal Notices. The app's About box and `lichomat-render --version` show:
    - 'Copyright (C) 2012–2026 Erica Synths' and 'Copyright (C) 2026 Tomasz Krzywicki', and the third
      parties' copyrights with their licences;
    - the dated modification notice of GPLv3 §5(a): 'Modified from the LXR firmware 1.7 by Erica Synths;
      LICHOMAT and its emulator by Tomasz Krzywicki, 2026';
    - the absence of warranty and the licence;
    - the commit the binary was built from, and the firmware state it runs: 'LICHOMAT 0.1 with the
      emulator changes F1–F12 and F6b' until those reach a release. The splash still shows the
      firmware's own 'v0.1 unofficial';
    - where the source is;
    - that the product is not affiliated with Erica Synths.
- The chain of title is assumed, not verified. Erica relicensed Julian Schmidt's 2012 V1 code as
  GPL-3.0-only, while the same code is still public under a non-commercial licence.

### 10.2 Naming (Q14)

| Thing | Name |
|---|---|
| App | LICHOMAT Emu |
| Renderer | `lichomat-render` |
| CoreMIDI source and destination | LICHOMAT Emu |
| Tagline | emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02 |
| Bundle ID | `io.github.isthar.lichomat-emu`, which carries the owner's GitHub handle |

- 'LXR', 'Erica Synths' and 'Sonic Potions' never appear in a name, a port name, an icon, a release
  title or release notes' heading, the README's text about the emulator, or the repository's
  description. The tagline's 'for the Erica Synths LXR-02' is a truthful statement of compatibility,
  which NOTICE.md 7(e) allows; README.md:340–343 allows such references too.
- **The allow-list.** In the binaries, those names appear only in: the tagline; the legal notices of the
  About box and of `--version` (the copyright, modification and non-affiliation lines of §10.1). The
  bundled `NOTICE.md` names them too. M21 scans the binaries' C strings for them, case-insensitive, and
  fails on any other occurrence.
- The panel is schematic, in our own colours, with no logo and no copy of the faceplate (NOTICE.md,
  'Hardware'; §6.6).
- The boot splash, 'LICHOMAT' and 'v0.1 unofficial', shows in the app for 1 s of wall time, and the window
  title says 'unofficial' (NOTICE.md 7(c)).

### 10.3 Distribution (Q15)

- arm64 only, macOS 13 or later.
- Ad hoc signed binaries, `lichomat-render` and `LICHOMAT Emu.app`, as assets of a GitHub release on the
  fork, built by CI. The app bundle carries its own copy of the renderer (§9.2). There is no paid Apple
  developer account, so no notarisation.
- A downloaded copy opens only after System Settings > Privacy & Security > Open Anyway, or after removing
  the quarantine attribute with `xattr`. The README says so, and explains how to build from source, which
  avoids the step.
- No factory kits. Users bring a copy of their card or a folder.
- A release is published only on the owner's go-ahead. Its tag is `emu-v0.1` for the first one.

## 11. Error handling

| Situation | Renderer | App |
|---|---|---|
| The card folder is missing or unreadable | Exit code 2 | Offers to make a new, seeded card |
| The project to load has no `PRJ.NFO` | Exit code 2 | Boots as the device does, with an empty project, and shows a notice |
| A name on the card is not 8.3 | Skipped, one warning per name | The same, in the diagnostics |
| Two files for one slot | The older one loads; a warning names both | The same |
| A firmware fault (a signal, a sanitizer report or a model check) | Exit code 3, with the virtual time and the last 32 scheduler events | A crash note, shown at the next launch (§6.9) |
| No scheduler progress for 10 s of wall time, not counting the tier hook's waits | Exit code 4 | Audio stops; a message with 'Restart' |
| An output file cannot be written | Exit code 5 | — |
| A script error, or a refused `--param` | Exit code 1, with the file and line, or the option | — |
| The audio output stalls, or the device changes | — | The output reopens, the time map re-anchors, D's default is recomputed, input older than 100 ms is dropped and counted |
| A ring is full | — | The new entry is dropped and counted in the diagnostics |
| A bound DIN port disappears | — | The binding shows 'not connected', and rebinds when the endpoint returns (§6.9) |
| Import or export while a save runs | — | The copy starts only after 'card out applied' (§6.7) |
| An export target is not empty apart from dot entries | — | Refused, with the names found |
| The firmware reports an SD error | Shown on the virtual LCD, as on the device; logged in the panel log at marks | Shown on the virtual LCD, as on the device |
| A golden differs | The test fails with the first differing chunk, the statistics and the toolchains, and keeps the outputs | — |
| A golden's expectations fail | The test fails and names the expectation; `bless` refuses | — |

## 12. Verification

### 12.1 Host tests

They run through ctest in `emu/`, on macOS on arm64, in both CI jobs and in `tools/check.sh`.

| ID | Covers |
|---|---|
| M1 | The intrinsics against edge cases, `__CLZ(0)` = 32 included; F6's conversion against `FCVTZU`, NaN included |
| M2 | The clock: frames to cycles exact over 10^9 frames; `HAL_GetTick()`; CYCCNT as the low 32 bits; `seqp_now()` monotonic across a 2^32 wrap |
| M3 | The scheduler: §4.2's periods, priorities and order at equal times; PRIMASK deferral, and a release inside a body running only higher-priority bodies; time passing only in thread context; the pass rule. The `HAL_GetTick()` rule: a normal pass and an immediate kit load take no time; the screensaver wait ends, at 432 cycles per iteration after 512 free calls; the codec starts at exactly 216,000,000 cycles with a card and 432,000,000 without. The tier hook, with a fake wall clock and a fake audio ring: inputs are injected and blocks paced during `menu_showSdError()`'s `HAL_Delay(2000)` and during a screensaver wake; the boot splash is paced to 1 s; the hook's waits do not count for the watchdog. The screensaver, through a test hook that sets `screensaver_timer` near its 2-minute timeout: it starts, a button wakes it, and the passes go on. The EXTI lines: an edge before `HAL_NVIC_EnableIRQ()` runs its body once at the enable |
| M4 | TIM2: it fires 204 cycles after a block request when started, is parked by `soundengine_process()`, and fires every 6,004 cycles inside a `HAL_Delay()`; `HAL_TIM_Base_Stop()` leaves UIE set |
| M5 | The codec: the underrun count, the order of the halves, out 1 from `dma_buffer2`, half 0 rendered by `soundengine_init()` |
| M6 | Boot: the splash snapshot ('LICHOMAT', 'v0.1 unofficial'); the path without a card, with 'SD error: No Card!'; the card path with a fixture; a card without `GLO.CFG`: 'glo.cfg error', 689 block requests in its `HAL_Delay(1000)` and 688 underruns |
| M7 | The LCD model: HD44780 commands, CGRAM, 0xC5 and 0xC6, a full repaint in 6.25 ms. The LEDs: the latch of the 5 SPI bytes, LED 40 on `GPIOB->ODR` bit 8, a 50 ms voice pulse kept in the next snapshot |
| M8 | The panel: one transition per scan; Gray changes 1 ms apart; `encoder +10` moves the menu 10 items; a 40 ms push seen in every sampling phase; 134 ± 5 steps per knob turn on a normal parameter and 22 ± 2 on a list parameter; no edit at boot with the knobs at 0°; fader i reaching `adc_getFaderVolume(i−1)`. The output jacks: for each single unplugged jack and for L1 and R1 unplugged together, one voice routed to an unplugged output sounds, by per-channel energy, on the output that `mixer_checkOutJackAvailable()` picks for that state and nowhere else |
| M9 | The card backend: the storage scenario's operations and results, the order, the lock, the card switch, an order-keeping copy that skips dot entries |
| M10 | DIN in and out: 320 µs per byte; the F8 behind Note Ons; SPP and Program Change through the USART3 handler. The flags: an input byte during a TX slot is decoded once; an output byte during an RX body is not lost; the last byte of a burst is sent once; the first byte after TXEIE goes from 0 to 1 starts at the later of the enable and the wire-free time; a byte that arrives while RXNE is set sets ORE and is lost; bytes before boot step 11 do not reach the sync queues, except the one left in RDR |
| M11 | USB in and out: the DataOut walk (stop at a 0 byte, realtime with CIN 0x5, cable 0); at most 16 packets per 1 ms; 15-packet transfers; completion after 1 ms; no stall drop |
| M12 | The jacks: clock in at 4 and 24 PPQ only in sync mode 1; the reset gate; clock-out edges |
| M13 | The random streams: known first values for seeds 1 and 2; a probability edit leaves the DSP stream's draws unchanged |
| M14 | Determinism, slicing and speed: two runs of G1 are byte-identical; G1 rendered in one run equals G1 rendered as consecutive 'run until t' calls of 470 and 471 frames, with a tier hook that waits at every call; `emu_golden.py check` makes every golden's broken variant fail its expectations; in a Release build, G1 renders at least 10× faster than real time |
| M15 | The firmware changes: F1 (CC 1 with nodes at destination 0), F2 (morph after a boot without a card), F3 (the clear-page LCD equal to the base build's), F6b (U8's recipe in full under UBSan), F9 (no read past the tables under ASan), F11 (a power-on with the knobs off 0°, with and without a card), F12 (a pattern select by the step buttons fills the Euclid parameters from the active voice) |
| M16 | The ping-pong delay mode-switch scenario under ASan and UBSan, as a regression test of E4.8 |
| M17 | The renderer: options and defaults; the t0 rule (t0 is the first block request at least 100 ms after the prelude's last event, with `preset_morphBusy()` 0 at every pass of those 100 ms); `--project`; `--bpm`; `--param` for `PAR_BPM`, `PAR_SHUFFLE` and one sound parameter, each setting `activeParameters` and reaching the engine (the F8 rate, the step times in the MIDI log, the audio); a refused special-case parameter; the save events; `/TMP` handling; `--seed-card`; `--stereo sum`; exit codes 1–5 (codes 3 and 4 through a test-only hook) |
| M18 | The app's timing model, without CoreAudio. A fake output (48 kHz, 512-frame buffers, callbacks of 470–471 frames, `mHostTime` = entry + buffer + 48-frame safety offset + 1.09 ms of converter, a device latency of 60 frames and a stream latency of 690 frames) with §6.2's ring, and DAW-like clock stamps, drive the firmware thread's code. With the default D, steps follow the clock at a constant latency of D + 0–1.45 ms from 90 to 174 BPM, and L is at least every measured lead; with D = 0, the latency varies with the arrival phase by at least 10 ms. A source that sends 6 clocks with distinct timestamps in one event list keeps 6 distinct stamps; 6 clocks stamped 0 in one list enter as one transfer. A re-anchor that moves the map back by 5 ms keeps a port's messages in order |
| M19 | libm: the firmware objects are linked into one relocatable object (`ld -r`). Its undefined symbols, minus those that `emu/core` and `emu/libm` define, stay within an allow-list: `fmod`; the libc string and memory functions and their `__*_chk` forms, `atoi` and `bzero`; the stack-check symbols (`__stack_chk_*`, `__chkstk_darwin`); in a sanitizer build, the sanitizer runtime (`___ubsan_*`, `___asan_*`). `___sincosf_stret`, `___sincos_stret` or any other libm name fails. A canary file compiled with the firmware flags calls `expf` of a constant and `sinf` and `cosf` of one argument, and must import the vendored names |
| M20 | Drift. The hashes of the firmware text the emulator re-implements or takes its constants from are unchanged: the SysTick, TIM14 and TIM2 handlers in `stm32f7xx_it.c`; `HAL_I2S_TxCpltCallback()`; the MIDI parts of `USBD_CDC_DataOut()` and `USBD_CDC_DataIn()` in `usbd_cdc.c`; `CDC_Transmit_FS()` and `USBD_CDC_TransmitPacket()`; `SystemClock_Config()` (PLL M 8, N 216, P 2; APB1 /4; PLLI2S N 113, R 5); `MX_TIM2_Init()` (prescaler 1, period 1500); `MX_TIM14_Init()` (prescaler 13500, period 1); `MX_I2S2_Init()` and `MX_I2S3_Init()` (16-bit, MCLK out, 44K, from which the HAL derives the divider behind 45.2 MHz / 1024); `MX_USART3_UART_Init()` (31,250 baud); `MX_GPIO_Init()`'s EXTI edge modes for PD4 and PD5; the `HAL_NVIC_SetPriority()` lines of `stm32f7xx_hal_msp.c` for TIM2, TIM14, USART3 and OTG_FS, of `MX_DMA_Init()`, and `TICK_INT_PRIORITY` in `stm32f7xx_hal_conf.h`. The EXTI priorities run in `appMain.c`, where `HAL_NVIC_SetPriority()` checks them (§3.4) |
| M21 | The licence gate: every source and header in the dependency files of the emulator's binaries is GPL-3.0-only or on the allow-list of §10.1, with overlay paths resolved; none names SLA0044. The name scan of §10.2 over the binaries' C strings |
| M22 | The source lists: every file in `Core/sources.cmake` is compiled or excluded |
| M23 | The card fixtures: `make.sh` run into a temporary folder gives the committed fixtures byte for byte |

`tests/` also registers stage 1's T-tests against the strict copy of the timing core (§8.2).

### 12.2 Goldens

G1–G10 (§8.6), in both CI jobs.

### 12.3 The owner's checks of the app

| ID | Check |
|---|---|
| A1 | The app opens after Open Anyway. The splash shows 'LICHOMAT' and 'v0.1 unofficial' for about 1 s. About shows the notices of §10.1. The owner confirms that the panel is not a copy of the faceplate |
| A2 | An imported copy of the owner's card plays its projects, and they sound like the device |
| A3 | On the device and in the app (360 points of drag), the owner counts the steps of one full knob turn on a normal and on a list parameter; they agree within 10 %. Fader i, counted from the left, moves the same voice as fader i on the device. The encoder walks the menu, and SHIFT combinations work with a latched button |
| A4 | Master over the virtual port at 120 BPM: the DAW shows 120.0 ± 0.1 BPM; F8 intervals on the virtual port are 20.83 ± 1.5 ms; the DAW starts and stops with the app |
| A5 | Slave over the virtual port with the default D, at 128 and at 140 BPM for 2 minutes each: the diagnostics' measured leads stay at most L, and their input latency stays within 1 ms of D. Start, Stop, Continue and a locate (SPP) to bar 3 put the step LEDs at the DAW's position within one step |
| A6 | Slave over a hardware DIN interface, as A5 |
| A7 | MIDI out over DIN to a synth: a pattern with notes on all 7 tracks runs for 5 minutes, with 10 stops and starts; after each stop the synth falls silent, so no note hangs |
| A8 | The clock jacks: at 4 PPQ and 120 BPM the clock-out LED blinks 8 times per second; in sync mode 1 at 4 PPQ, 16 clicks of the clock-in button advance 16 steps; the reset switch on stops the sequencer, and off lets it run from step 1 |
| A9 | Export to an empty folder and to a freshly formatted card; the exported card loads on the device. Export one project into a copy of the owner's card; their other projects stay |
| A10 | A power cycle works, and the DAW keeps its routing to the virtual ports. A launch after a crash shows the crash note |
| A11 | Diagnostics with the built-in output: 689.7 ± 0.1 blocks per second, and a firmware-thread load below 10 % on the owner's Mac. A project load during playback gives no output underrun |

### 12.4 Device checks for the firmware changes

When F1–F12 reach a firmware release (v0.2, R6), the hardware checklist gains these checks. F5–F8, F6b,
F10 and F11 have none: their images are byte-identical or equivalent, or, for F11, the value whose read
moves is discarded (§7).

| ID | Check |
|---|---|
| U4 | A kit with LFO and velocity modulation targets loads and modulates as in v0.1. CC 1 (the mod wheel) from a keyboard sounds as in v0.1 (F1) |
| U5 | Boot without a card, then turn the morph knob: no noise and no crash (F2) |
| U6 | The clear-automation pages show 'clear [autom. 1]' and 'clear [autom. 2]' as in v0.1 (F3) |
| U7 | H1 and H2 again (F4) |
| U8 | Listening check against v0.1 (F9). Voice 1 with a saw wavetable oscillator at its highest coarse pitch (note 127), pitch modulation amount 127, pitch EG decay 64 or more and pitch slope 64; then the same with the triangle; then a rect wavetable oscillator at coarse notes 97, 109 and 121 (about 2.2, 4.4 and 8.9 kHz, rows 7, 8 and 9) without pitch modulation. 16 hits of each sound as in v0.1; then a PWM oscillator with a nonzero PWM amount, at a low and a very high pitch (B13, B14) |
| U9 | In the pattern generator (hold SHIFT and press the second mode button), press voice button 3 and give voice 3 a length, steps and rotation unlike voice 1's. Hold SHIFT and press step button 16, select another pattern with a step button, then open the pattern generator again: it shows voice 3's length, steps and rotation, and turning the length knob changes voice 3's steps, as in v0.1 (F12) |

## 13. Rollout

- **Branch.** `emulator` is created from `timing-core` when the owner accepts this spec. `timing-core`
  stays frozen for the owner's checks, and fixes from those checks are merged into `emulator`.
- **Pushing.** `emulator` goes to the owner's fork (`origin`) as Q19 allows. Nothing goes to `upstream`,
  and `main` is not touched.
- **Increments**, mapped to the tracker's tasks:

  | Increment | Contents | Tracker |
  |---|---|---|
  | R1 | First F1–F10, each with its acceptance, and the strict copy of the timing core in `tests/`. Then `emu/` with the stubs, the overlay, the core with the tier hook, and the card backend; boot and passes in a test (M1–M13, M15, M19–M22) | E5.2, whose title changes from 'Build silnika DSP na hoście (shimy CMSIS)', the DSP-only approach of §3.2, to the whole firmware on a virtual device |
  | R2 | `lichomat-render`: options, card copy, timeline, event scripts, outputs, exit codes, `--seed-card`, `--param` and the save events (M14, M17); the firmware changes F6b, F11 and F12 (Q21–Q23), before R2.5 | E5.3 |
  | R3 | Card fixtures and `make.sh`, G1–G10 with their expectations, `goldens.json`, `tools/emu_golden.py`, `emulator.yml`, the ping-pong regression scenario (M16, M23), the `check.sh` step | E5.4 |
  | R4 | The app: threads and rings, CoreAudio, the time map, CoreMIDI in and out, the panel, the card folder, the clock-jack LED and buttons, power and crash handling, diagnostics (M18; A1–A11) | E5.5 |
  | R5 | The app's audio options for the clock jacks (§6.8) | E5.5 |
  | R6 | Firmware release v0.2 with F1–F12 (F6b included), on the owner's go-ahead after their v0.1 checks: U4–U9 in `hardware-checklist.md`, the release notes, the image from CI | a new task |

- **Plans.** This spec is too big for one plan, so it splits into four:
  1. R1, the virtual device, with the firmware changes;
  2. R2 and R3, the renderer and the goldens, written when R1 is done;
  3. R4, the app, written when tier 1 is done, from §6;
  4. R5, a small plan.

  R6 needs no plan: it is a release task in the tracker, done with the owner.
- **Each increment ends** with `tools/check.sh` green, 0 warnings, and the firmware's `.bin` checks of §7
  passing.
- **Release.** Binaries are published only on the owner's go-ahead (§10.3).
- **Stage 2** starts after tier 2, from `emulator` (Q12).

## 14. Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | Latent memory bugs crash the host or make renders depend on the memory layout. More may sit on paths nobody ran | F1–F3, F9, F11 and F12; the sanitizer sweep and its fixes E4.6–E4.8; ASan, UBSan and pattern-initialised locals in CI with goldens and scenarios; exit code 3 and the crash note with the event trail |
| 2 | Firmware code runs on more than one host thread, which turns compiler-only fences, no-op PRIMASK sections and non-reentrant parsers into data races | One firmware thread with rings (§3.1, §6.1); the tier hook instead of a second thread; `MIDISend` and `MIDIReceived` on their own thread |
| 3 | A busy-wait hangs the virtual clock, or a long firmware call starves the tier | Time passes in `HAL_Delay()` and in `HAL_GetTick()` spins; the tier hook runs inside them; the LCD completes at once; the format routine is unreachable; the watchdog |
| 4 | The app's MIDI timing misleads a user: late slave steps, future events acting early, callback stamps, MIDI out early or jittered | Hold until due; D computed from the output's latencies; per-packet stamps; sending at the presentation time; the DIN wire model (§4.3, §6.3–§6.5); M18; A4–A6 |
| 5 | Goldens are unstable across Macs | Strict flags, `-fno-strict-float-cast-overflow`, `-fno-builtin-*`, vendored libm, arm64 only, F9; the Command Line Tools in CI; goldens in both CI jobs; the toolchain recorded per golden (§8) |
| 6 | The block model breaks by accident: another chunk size, or a render point that moves against the request | §4.1 and §4.2; M3–M5; M14's slicing check; the goldens |
| 7 | Users expect the device's exact sound | 'Very close' and the known differences of §8.2 are stated in the README and the About box; goldens pin host builds only |
| 8 | Storage damages a user's card or diverges from FAT | The app's own folder; temporary copies for renders; copies that keep the order; the 'card out applied' handshake; duplicate warnings; export only into an empty folder or with a backup of a replaced project; M9 |
| 9 | Input is lost or garbled: clicks between scans, fast knob moves, MIDI bursts | Queued release at the poll points (§4.6); paced injection (§4.3); the USART3 flag model (§4.2); counters in the summary and the diagnostics |
| 10 | The sample-rate mapping shifts pitch and tempo: −1.6 cents and 0.092 % slow at 44.1 kHz without conversion, +145 cents at 48 kHz | AUHAL converts from 44,140.625 Hz; A11 checks the block rate |
| 11 | Licence and trademark: SLA0044 code, files without a licence, names, the faceplate, non-commercial code, or a later change of NOTICE.md upstream | §10; the licence gate and name scan M21; the stand-ins written fresh |
| 12 | Toolchain traps: Mach-O section names, the predefined `__weak`, fortify, signed `char`, header shadowing, the case clash, CRLF, a sanitizer runtime in a release binary | §3.4, §8.2, §9.2 and §9.3; the overlay instead of shadowing for `main.h`; the `otool` step |
| 13 | The emulator's re-implementations drift from the firmware: interrupt bodies, the DataOut walk, the register model, the CubeMX rates and priorities | F4 removes the copy of `main.c`; M20's hashes; the priority check in `HAL_NVIC_SetPriority()`; M22; unknown registers fail to compile |
| 14 | Panel state silently changes a render: untouched faders are silent, jack states reroute voices, knob angles edit at boot, seeds change noise | Goldens fix every input (§8.4); the knobs start at 0°; the summary prints the inputs |
| 15 | Hardware details nobody could verify: the WS0010 font, the knob wiper model, the fader order, the true sample rate, flush-to-zero, the main-loop rate, TIM2 during saves | Q18; A2 and A3; the device checks; the README names each of them as a model, not a measurement |
| 16 | Distribution friction: Open Anyway or `xattr` for every user, and no Intel support | The README's steps; building from source avoids the step |
| 17 | The stage is too big for one plan | Four plans (§13) |

## 15. Open questions for the owner

This draft applies each default. Each question stays open in the tracker until the owner answers.

| # | Question | Default applied in this draft | Status |
|---|---|---|---|
| Q12 | Do we build stage 1b in the agreed order: the renderer first (goldens in CI, quick listening to stage 2's changes), then the app (virtual panel, CoreMIDI, master and slave), then stage 2? The alternative is the renderer, then stage 2, and the app later. **Agreed order:** the app's live sync checks (A4–A8) exist before stage 2 changes the groove, and stage 2 starts two plans later (R4 and R5). **Alternative:** the groove features reach the device two plans sooner. Their scripted sync tests run in the renderer (G7 and G8 drive DIN and USB clocks), and live tests wait for the device or the app | Renderer, app, stage 2, as agreed on 2026-10-23. Code starts only after the owner accepts this spec | Open |
| Q13 | Do the small firmware changes the emulator needs go into the firmware? Most leave the device's behaviour unchanged: the two NULL reads, the `strcpy` in `menu.c`, splitting `main()` into a boot and one loop pass, saturating float-to-uint conversions, and a function that tells whether a kit transfer is running. One changes the sound: the read past the end of a table in `Oscillator.c`. The ping-pong delay report is settled: the sanitizer sweep reproduced it, and E4.8 fixed it on `timing-core` (`2bb5d80`) | Yes, as separate small commits on `emulator` (F1–F10), reaching the device in v0.2, after the owner's v0.1 checks. The `Oscillator.c` fix only after a listening check on the device (U8), which also decides the PWM oscillator's B13 and B14 from the sanitizer sweep. F2 also changes a morph without a card, which today reads stray bytes from address 0 | Open |
| Q14 | Names and look: app 'LICHOMAT Emu', renderer 'lichomat-render', CoreMIDI port 'LICHOMAT Emu', tagline 'emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02', bundle ID `io.github.isthar.lichomat-emu` (with your GitHub handle); a schematic panel in its own colours, no logos and no copy of the faceplate. The naming rule also covers release titles and notes, the README and the repository's description | As proposed (§10.2) | Open |
| Q15 | Distribution: arm64 only, macOS 13 or later, ad hoc signed binaries as release assets on the fork, no paid Apple developer account (Open Anyway at the first start), no Erica factory kits; users bring a copy of their card or a folder | As proposed (§10.3). Intel is not supported: an x86-64 build gives different results and can crash | Open |
| Q16 | The app's behaviour: knobs like the device (about 134 steps per turn, 22 for list parameters); an instant display; a slave MIDI input delay computed from the audio output's latencies, about 58 ms at the built-in output's 512-frame buffer and about 34 ms at 128 frames, that the user can lower (the earlier figure of 24 ms is too short for the app's audio pipeline, §6.4); sound at the internal 44,140.625 Hz, resampled to the audio device at Normal complexity and maximum quality; the app's own card folder with import and export; the UI in Dear ImGui on Cocoa and Metal | As proposed (§6). The clock jacks are an LED and buttons; an audio pulse out and clock in from audio are options | Open |
| Q17 | On DIN, MIDI clock bytes (F8) wait behind the Note Ons of the same moment, up to about 7 ms with 7 tracks. MIDI 1.0 allows realtime bytes to go ahead. Change it, in a v0.2 firmware or in stage 2? (A 0.1.x version does not fit the splash: 'v0.1.1 unofficial' has 17 characters) | Recorded for stage 2; nothing changes now. G6 pins today's behaviour, and H5 already allows for it | Open |
| Q18 | Optional: photos of the LXR-02's display on the song page (the up and down arrows) and on other screens with characters outside ASCII, so that the virtual display can copy the WS0010 font | Without photos: a generic 5×8 font, with 0xC5 and 0xC6 drawn as arrows (§4.7) | Open |
| Q19 | May the `emulator` branch be pushed to your fork (`origin`) as work lands? Q5 covered `timing-core` only. CI (`emulator.yml`) runs only on pushed commits | Nothing from `emulator` is pushed until you answer; until then `emulator.yml` does not run and G3 waits | Open |

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
- **A trigger log** (`--trigger-log`) for stage 2's swing, microtiming and trig conditions: one line per
  `seq_cbTrigger()` call (`sequencer.c:1919`) with the event's tick at 384 PPQN, step, track, velocity and
  flags. The WAV places triggers at block boundaries, as the device does, and the MIDI log depends on
  the TX filter and the DIN queue. It needs a firmware hook, so stage 2's spec decides it with its own
  firmware changes.
- **Sample-accurate triggering** (E6.10) will change the goldens when it lands.
- **The core in a child process**, so that a firmware crash or a power cycle does not restart the GUI and
  the virtual MIDI ports.
