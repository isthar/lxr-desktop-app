# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only

# The source files of the LXR V2 application modules, one list per module, in
# link order. Core/CMakeLists.txt builds one OBJECT library from each list. The
# emulator (emu/CMakeLists.txt, stage 1b) includes this file too and compiles
# the same C files on the host, so a file added here reaches both builds. The
# paths are relative to this directory, Core/.

# Startup, clocks, CubeMX peripheral init, interrupt vectors, newlib stubs.
set(LXR_SOURCES_PLATFORM
    Startup/startup_stm32f765vgtx.s
    Src/main.c
    Src/stm32f7xx_it.c
    Src/stm32f7xx_hal_msp.c
    Src/system_stm32f7xx.c
    Src/syscalls.c
    Src/sysmem.c
    Src/delay.c
    Src/flash_if.c
)

# Hardware inputs/outputs: shift-register buttons/LEDs, pots & faders.
set(LXR_SOURCES_IO
    Src/IO/adcPots.c
    Src/IO/DIO.c
    Src/IO/uart.c
)

# User interface: LCD, encoder, buttons, LEDs, menu pages, screensaver.
set(LXR_SOURCES_UI
    Src/lcd.c
    Src/encoder.c
    Src/buttonHandler.c
    Src/ledHandler.c
    Src/graphic.c
    Src/frontPanelParser.c
    Src/Parameters.c
    Src/menu/Cc2Text.c
    Src/menu/copyClearTools.c
    Src/menu/menu.c
    Src/menu/screensaver.c
)

# Kit/pattern/project storage on the SD card.
set(LXR_SOURCES_PRESET
    Src/Preset/initKit.c
    Src/Preset/presetManager.c
)

# DSP building blocks, drum voices and master effects.
set(LXR_SOURCES_DSP
    Src/Soundengine/DSPAudio/1PoleLp.c
    Src/Soundengine/DSPAudio/automationNode.c
    Src/Soundengine/DSPAudio/BufferTools.c
    Src/Soundengine/DSPAudio/CymbalVoice.c
    Src/Soundengine/DSPAudio/Decay.c
    Src/Soundengine/DSPAudio/distortion.c
    Src/Soundengine/DSPAudio/dither.c
    Src/Soundengine/DSPAudio/DrumVoice.c
    Src/Soundengine/DSPAudio/HiHat.c
    Src/Soundengine/DSPAudio/lfo.c
    Src/Soundengine/DSPAudio/mixer.c
    Src/Soundengine/DSPAudio/modulationNode.c
    Src/Soundengine/DSPAudio/Oscillator.c
    Src/Soundengine/DSPAudio/random.c
    Src/Soundengine/DSPAudio/ResonantFilter.c
    Src/Soundengine/DSPAudio/Samples.c
    Src/Soundengine/DSPAudio/SlopeEg2.c
    Src/Soundengine/DSPAudio/snapEg.c
    Src/Soundengine/DSPAudio/Snare.c
    Src/Soundengine/DSPAudio/squareRootLut.c
    Src/Soundengine/DSPAudio/TiltEq.c
    Src/Soundengine/DSPAudio/transientGenerator.c
    Src/Soundengine/DSPAudio/transientTables.c
    Src/Soundengine/DSPAudio/wavetable.c
    Src/Soundengine/DSPAudio/FX/Compressor.c
    Src/Soundengine/DSPAudio/FX/Delay.c
    Src/Soundengine/DSPAudio/FX/Distortion.c
    Src/Soundengine/DSPAudio/FX/FxRamBuffer.c
    Src/Soundengine/DSPAudio/FX/Reverb.c
    Src/Soundengine/DSPAudio/FX/Ringmod.c
    Src/Soundengine/DSPAudio/FX/VerbTools/AllpassDelay.c
    Src/Soundengine/DSPAudio/FX/VerbTools/sinCos.c
    Src/Soundengine/DSPAudio/FX/VerbTools/VerbDelay.c
    Src/Soundengine/DSPAudio/FX/VerbTools/VerbModDelay.c
)

# Audio engine: codec/I2S DMA, sample block rendering, FX bus, clock/trigger I/O,
# CPU meter.
set(LXR_SOURCES_ENGINE
    Src/Soundengine/AudioCodecManager.c
    Src/Soundengine/cpuMeter.c
    Src/Soundengine/cs4344_cs5343.c
    Src/Soundengine/FIFO.c
    Src/Soundengine/FxBus.c
    Src/Soundengine/Soundengine.c
    Src/Soundengine/TriggerOut.c
)

# MIDI: UART driver, parsers, parameter mapping, voice control.
set(LXR_SOURCES_MIDI
    Src/Soundengine/MIDI/CortexParser.c
    Src/Soundengine/MIDI/MidiMessages.c
    Src/Soundengine/MIDI/MidiParser.c
    Src/Soundengine/MIDI/MidiVoiceControl.c
    Src/Soundengine/MIDI/ParameterArray.c
    Src/Soundengine/MIDI/Uart.c
)

# Step sequencer: the timing core (seq_core) and the sync input queues (seq_syncq),
# which the host tests in tests/ build as well, the platform layer (seq_platform:
# DWT time base, queue instances), the glue (sequencer) and the Euclidean generator.
set(LXR_SOURCES_SEQUENCER
    Src/Soundengine/Sequencer/EuklidGenerator.c
    Src/Soundengine/Sequencer/seq_core.c
    Src/Soundengine/Sequencer/seq_platform.c
    Src/Soundengine/Sequencer/seq_syncq.c
    Src/Soundengine/Sequencer/sequencer.c
)

# USB-MIDI message queue on top of the USB device stack, and the USB MIDI output
# packets and transmit ring (usb_midiTx), which the host tests in tests/ build as well.
set(LXR_SOURCES_USB_APP
    Src/Soundengine/USB/usb_manager.c
    Src/Soundengine/USB/usb_midiTx.c
)
