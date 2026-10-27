# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only

# LXR V2 application code, split into modules.
#
# The modules call into each other in a cycle (sequencer <-> menu <-> preset
# <-> DSP), so they are OBJECT libraries that are all linked directly into the
# firmware executable rather than static libraries with a link order.

add_library(lxr_app_common INTERFACE)
target_link_libraries(lxr_app_common INTERFACE
    lxr_mcu
    lxr_board_config
    stm32f7_hal
    fatfs
    usb_midi
)
target_compile_options(lxr_app_common INTERFACE $<$<COMPILE_LANGUAGE:C>:-Wall>)

# The module source lists, LXR_SOURCES_PLATFORM ... LXR_SOURCES_USB_APP. The
# emulator (emu/, stage 1b) compiles the same lists on the host.
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

add_library(lxr_platform OBJECT ${LXR_SOURCES_PLATFORM})
# Shown on the boot splash screen; LICHOMAT_NAME and project(VERSION) come from the
# top-level CMakeLists.txt.
set_source_files_properties(Src/appMain.c PROPERTIES
    COMPILE_DEFINITIONS "FIRMWARE_NAME=\"${LICHOMAT_NAME}\";FIRMWARE_VERSION=\"${PROJECT_VERSION}\""
)

add_library(lxr_io OBJECT ${LXR_SOURCES_IO})
add_library(lxr_ui OBJECT ${LXR_SOURCES_UI})
add_library(lxr_preset OBJECT ${LXR_SOURCES_PRESET})
add_library(lxr_dsp OBJECT ${LXR_SOURCES_DSP})
add_library(lxr_engine OBJECT ${LXR_SOURCES_ENGINE})
add_library(lxr_midi OBJECT ${LXR_SOURCES_MIDI})

add_library(lxr_sequencer OBJECT ${LXR_SOURCES_SEQUENCER})
# The host tests build seq_core.c and seq_syncq.c with -Wall -Wextra -Werror; the
# firmware warns about the same for them and for seq_platform.c.
set_property(SOURCE
    Src/Soundengine/Sequencer/seq_core.c
    Src/Soundengine/Sequencer/seq_platform.c
    Src/Soundengine/Sequencer/seq_syncq.c
    APPEND PROPERTY COMPILE_OPTIONS -Wextra)

add_library(lxr_usb_app OBJECT ${LXR_SOURCES_USB_APP})
# The host tests build usb_midiTx.c with -Wall -Wextra -Werror; warn about the same here.
set_property(SOURCE Src/Soundengine/USB/usb_midiTx.c APPEND PROPERTY COMPILE_OPTIONS -Wextra)

set(LXR_APP_MODULES
    lxr_platform
    lxr_io
    lxr_ui
    lxr_preset
    lxr_dsp
    lxr_engine
    lxr_midi
    lxr_sequencer
    lxr_usb_app
)
foreach(module IN LISTS LXR_APP_MODULES)
    target_link_libraries(${module} PUBLIC lxr_app_common)
endforeach()
set(LXR_APP_MODULES ${LXR_APP_MODULES} PARENT_SCOPE)

# --- ITCM fit -----------------------------------------------------------------
# Functions tagged IN_ITCM_RAM are linked into the 16 KB instruction TCM. Newer
# GCC releases generate more code at -O3 than the GCC 10/11 the project was
# developed with, which overflows ITCM. LXR_ITCM_OPT overrides the optimisation
# level for just the translation units that contain ITCM code.
set(LXR_ITCM_SOURCES
    Src/Preset/presetManager.c
    Src/Soundengine/cs4344_cs5343.c
    Src/Soundengine/FxBus.c
    Src/Soundengine/DSPAudio/1PoleLp.c
    Src/Soundengine/DSPAudio/CymbalVoice.c
    Src/Soundengine/DSPAudio/Decay.c
    Src/Soundengine/DSPAudio/distortion.c
    Src/Soundengine/DSPAudio/DrumVoice.c
    Src/Soundengine/DSPAudio/HiHat.c
    Src/Soundengine/DSPAudio/lfo.c
    Src/Soundengine/DSPAudio/mixer.c
    Src/Soundengine/DSPAudio/modulationNode.c
    Src/Soundengine/DSPAudio/Oscillator.c
    Src/Soundengine/DSPAudio/ResonantFilter.c
    Src/Soundengine/DSPAudio/SlopeEg2.c
    Src/Soundengine/DSPAudio/Snare.c
    Src/Soundengine/DSPAudio/transientGenerator.c
    Src/Soundengine/DSPAudio/FX/Distortion.c
    Src/Soundengine/DSPAudio/FX/Reverb.c
    Src/Soundengine/MIDI/ParameterArray.c
)
if(LXR_ITCM_OPT)
    set_source_files_properties(${LXR_ITCM_SOURCES} PROPERTIES COMPILE_OPTIONS "${LXR_ITCM_OPT}")
endif()
# The SVF filters switch on the filter type inside the per-sample loop. GCC 15
# jump-threads the loop back edge through that switch and duplicates the whole
# loop body per case (ResonantFilter.c: 2772 B -> 6720 B at -O3, 8028 B at -O2).
set_property(SOURCE Src/Soundengine/DSPAudio/ResonantFilter.c APPEND PROPERTY COMPILE_OPTIONS -fno-thread-jumps)
