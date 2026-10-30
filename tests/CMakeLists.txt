# Host tests for the LICHOMAT sequencer timing core
# (Core/Src/Soundengine/Sequencer/seq_core.c).
#
# A CMake project of its own, built with the host C compiler. It is not part of the
# firmware build, which always uses the arm-none-eabi toolchain file:
#
#   cmake -S tests -B build/host-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/host-tests
#   ctest --test-dir build/host-tests --output-on-failure
#
# tools/check.sh and .github/workflows/build.yml run these steps.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.

cmake_minimum_required(VERSION 3.21)
project(lichomat_host_tests LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(LXR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(SEQC_CORE_SOURCE "${LXR_ROOT}/Core/Src/Soundengine/Sequencer/seq_core.c")

# Sanitizers, as for -fsanitize=. CI uses address,undefined. The default leaves out
# AddressSanitizer, because ASan binaries hang inside the macOS sandbox that Claude
# Code runs commands in.
set(SEQC_SANITIZE "undefined" CACHE STRING "Sanitizers for the host tests (e.g. address,undefined; empty = none)")
set(SEQC_TEST_TIMEOUT 120 CACHE STRING "Time limit per test in seconds")
option(SEQC_REQUIRE_ALL_TESTS "Fail if a test file listed below is missing" OFF)

# Options for everything built here.
add_library(seqc_options INTERFACE)
target_compile_options(seqc_options INTERFACE -Wall -Wextra -Werror)
if(SEQC_SANITIZE)
    target_compile_options(seqc_options INTERFACE
        -fsanitize=${SEQC_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(seqc_options INTERFACE -fsanitize=${SEQC_SANITIZE})
endif()
if(NOT WIN32)
    target_link_libraries(seqc_options INTERFACE m)
endif()

# The timing core, compiled from the firmware source. Only Core/Inc is on the
# include path and no HAL or CMSIS directory is, so an include of main.h or of a HAL
# header fails here. It gets the firmware's floating-point options
# (lxr_board_config in ../CMakeLists.txt), so the tests run the same optimisation
# model as the device; the tests themselves are built without them.
#
# An OBJECT library, as in the firmware build: as a static library, a core without
# symbols (an early skeleton) makes macOS ranlib print a "warning:" that
# tools/check.sh counts.
if(NOT EXISTS "${SEQC_CORE_SOURCE}")
    message(FATAL_ERROR "${SEQC_CORE_SOURCE} is missing: the host tests need the timing core")
endif()
add_library(seq_core OBJECT "${SEQC_CORE_SOURCE}")
target_include_directories(seq_core PUBLIC "${LXR_ROOT}/Core/Inc")
target_compile_options(seq_core PRIVATE -ffast-math -freciprocal-math)
target_link_libraries(seq_core PUBLIC seqc_options)

# The fake host (pattern data, song mode, recorder) and the runner header.
add_library(seqc_fake_host OBJECT fake_host.c)
target_include_directories(seqc_fake_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(seqc_fake_host PUBLIC seq_core)

enable_testing()

# T19 fills the event queue, which 7 tracks cannot do with SEQC_QUEUE_SIZE
# entries (see SEQC_QUEUE_CAPACITY in seq_core.c). It runs against a second copy
# of the core with a 16-entry queue.
add_library(seq_core_small_queue OBJECT "${SEQC_CORE_SOURCE}")
target_include_directories(seq_core_small_queue PUBLIC "${LXR_ROOT}/Core/Inc")
target_compile_definitions(seq_core_small_queue PRIVATE SEQC_QUEUE_CAPACITY=16)
target_compile_options(seq_core_small_queue PRIVATE -ffast-math -freciprocal-math)
target_link_libraries(seq_core_small_queue PUBLIC seqc_options)

# The queue tests (E1.16) count the queue elements the core writes and insert,
# pop and choke directly, through the test-only block of seq_core.h. They run
# against a copy of the core with SEQC_QUEUE_TEST, and the firmware's queue size.
add_library(seq_core_queue_test OBJECT "${SEQC_CORE_SOURCE}")
target_include_directories(seq_core_queue_test PUBLIC "${LXR_ROOT}/Core/Inc")
target_compile_definitions(seq_core_queue_test PUBLIC SEQC_QUEUE_TEST)
target_compile_options(seq_core_queue_test PRIVATE -ffast-math -freciprocal-math)
target_link_libraries(seq_core_queue_test PUBLIC seqc_options)

# The strict copies of the core (stage 1b, emulator spec §8.2). The emulator builds
# the firmware without -ffast-math and without fused multiply-adds, and with
# Clang's saturating float-to-int casts. Stage 1's T-tests run against these copies
# too, as the ctest ids <id>_strict; T19 needs its 16-entry queue here as well.
# GCC, which builds tests/ on Linux in CI, rejects -fno-strict-float-cast-overflow.
set(SEQC_STRICT_FP_OPTIONS -fno-fast-math -ffp-contract=off)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    list(APPEND SEQC_STRICT_FP_OPTIONS -fno-strict-float-cast-overflow)
endif()
add_library(seq_core_strict OBJECT "${SEQC_CORE_SOURCE}")
target_include_directories(seq_core_strict PUBLIC "${LXR_ROOT}/Core/Inc")
target_compile_options(seq_core_strict PRIVATE ${SEQC_STRICT_FP_OPTIONS})
target_link_libraries(seq_core_strict PUBLIC seqc_options)

add_library(seq_core_strict_small_queue OBJECT "${SEQC_CORE_SOURCE}")
target_include_directories(seq_core_strict_small_queue PUBLIC "${LXR_ROOT}/Core/Inc")
target_compile_definitions(seq_core_strict_small_queue PRIVATE SEQC_QUEUE_CAPACITY=16)
target_compile_options(seq_core_strict_small_queue PRIVATE ${SEQC_STRICT_FP_OPTIONS})
target_link_libraries(seq_core_strict_small_queue PUBLIC seqc_options)

# seqc_add_test(<name> IDS <id>... [EXTRA <source>...] [CORE <library>] [STRICT_CORE <library>])
#
# Builds <name>.c (plus the EXTRA sources) with the fake host into the executable
# <name>, and registers one ctest test per id: ctest runs "<name> <id>", which
# runs the tests named <id> and <id>_... (seqc_test.h). The ids are the test ids
# of the spec (§13.1). CORE picks the core library (default seq_core). Until a
# test file exists, its line here only prints a note; with SEQC_REQUIRE_ALL_TESTS
# a missing file is an error. STRICT_CORE builds <name> once more as
# <name>_strict, against that library, and registers "<name>_strict <id>" as the
# ctest test <id>_strict.
function(seqc_add_test name)
    cmake_parse_arguments(ARG "" "CORE;STRICT_CORE" "IDS;EXTRA" ${ARGN})
    if(NOT ARG_CORE)
        set(ARG_CORE seq_core)
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}.c")
        if(SEQC_REQUIRE_ALL_TESTS)
            message(FATAL_ERROR "tests/${name}.c is missing (${ARG_IDS})")
        endif()
        message(STATUS "host tests: tests/${name}.c not written yet (${ARG_IDS})")
        return()
    endif()
    add_executable(${name} ${name}.c ${ARG_EXTRA})
    # Both, directly: CMake links the objects of an OBJECT library only into
    # targets that name it, not through another OBJECT library.
    target_link_libraries(${name} PRIVATE seqc_fake_host ${ARG_CORE})
    foreach(id IN LISTS ARG_IDS)
        add_test(NAME ${id} COMMAND ${name} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT ${SEQC_TEST_TIMEOUT})
    endforeach()
    if(ARG_STRICT_CORE)
        add_executable(${name}_strict ${name}.c ${ARG_EXTRA})
        target_link_libraries(${name}_strict PRIVATE seqc_fake_host ${ARG_STRICT_CORE})
        foreach(id IN LISTS ARG_IDS)
            add_test(NAME ${id}_strict COMMAND ${name}_strict ${id})
            set_tests_properties(${id}_strict PROPERTIES TIMEOUT ${SEQC_TEST_TIMEOUT})
        endforeach()
    endif()
endfunction()

# One file per area, in the order of the stage 1 tasks (tracker E1.x).
seqc_add_test(test_harness   IDS harness)            # E1.1  runner, fake host, song walk
if(TARGET test_harness)
    # a "FAIL harness_..." line fails the test even if the exit status were broken
    set_tests_properties(harness PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL harness_")
endif()
seqc_add_test(test_layout    IDS layout)             # E1.2  Step and StepTiming layout
seqc_add_test(test_clock     IDS T1 STRICT_CORE seq_core_strict)                 # E1.3  master clock accuracy
seqc_add_test(test_tracks    IDS T10 STRICT_CORE seq_core_strict)                # E1.4  track counters, master wrap, pattern end
seqc_add_test(test_generate  IDS T2 T3 T4 T8 STRICT_CORE seq_core_strict)        # E1.5  grid, swing, shift, ratchet, probability
seqc_add_test(test_fire      IDS T7 T9 T15 STRICT_CORE seq_core_strict)          # E1.6  choke, mutes, note-off
seqc_add_test(test_lookahead IDS T11 T12 STRICT_CORE seq_core_strict)            # E1.7  pattern change, edits before the boundary
seqc_add_test(test_clockout  IDS T5 T6 STRICT_CORE seq_core_strict)              # E1.8  MIDI clock out, clock out pulses
seqc_add_test(test_roll      IDS T13 T14 STRICT_CORE seq_core_strict)            # E1.9  roll, recording phase, skip-once
seqc_add_test(test_slave     IDS T16 T18 STRICT_CORE seq_core_strict)            # E1.10 MIDI and jack slave, tempo estimate
seqc_add_test(test_transport IDS T17 STRICT_CORE seq_core_strict)                # E1.11 Start, Continue, Stop, SPP, song mode
seqc_add_test(test_overflow  IDS T19 EXTRA queue_equiv.c CORE seq_core_small_queue STRICT_CORE seq_core_strict_small_queue)  # E1.12 queue overflow, stats
seqc_add_test(test_fw17      IDS T20 EXTRA fw17_adapter.c STRICT_CORE seq_core_strict)  # E1.13 fw 1.7 replica (reference/)
seqc_add_test(test_tempo     IDS TT1 TT2 TT3 TT4 TT5 TT6 TT7 STRICT_CORE seq_core_strict)    # v0.2 V2 per-track tempo
seqc_add_test(test_groove    IDS HD1 HD2 HD3 HD4 STRICT_CORE seq_core_strict)    # v0.2 V4 delay and humanize
seqc_add_test(test_conditions IDS TC1 TC2 TC3 STRICT_CORE seq_core_strict)    # v0.2 V5 trig conditions
seqc_add_test(test_perform IDS RL1 RL2 SL1 STRICT_CORE seq_core_strict)    # v0.3 P1 rolls, P2 slide
seqc_add_test(test_queue     IDS queue EXTRA queue_equiv.c CORE seq_core_queue_test)  # E1.16, E1.18 queue order and real-time cost
seqc_add_test(test_syncq     IDS syncq EXTRA ${LXR_ROOT}/Core/Src/Soundengine/Sequencer/seq_syncq.c)  # E2.1  sync input queues (seq_platform)
seqc_add_test(test_cpumeter  IDS cpumeter EXTRA ${LXR_ROOT}/Core/Src/Soundengine/cpuMeter.c)  # E3.3 CPU meter
if(TARGET test_cpumeter)
    # cpuMeter_reset(), the test hook, exists only with CPUMETER_TEST (E4.1 F34)
    target_compile_definitions(test_cpumeter PRIVATE CPUMETER_TEST)
endif()

# The front-panel buttons (E2.7): test_buttons compiles the firmware's
# buttonHandler.c as it is. The headers in stubs/buttons/ and stubs/ come first
# on its include path and stand in for the FX bus, the HAL and FatFs. Core/Inc is
# a system directory for it, so that -Wextra warnings in fw 1.7 headers do not
# stop -Werror, and buttonHandler.c, fw 1.7 code that the firmware build checks,
# is compiled without warnings.
seqc_add_test(test_buttons   IDS buttons EXTRA ${LXR_ROOT}/Core/Src/buttonHandler.c)  # E2.7  buttons of the global menu
if(TARGET test_buttons)
    target_include_directories(test_buttons BEFORE PRIVATE stubs/buttons stubs)
    target_include_directories(test_buttons SYSTEM PRIVATE ${LXR_ROOT}/Core/Inc)
    set_source_files_properties(${LXR_ROOT}/Core/Src/buttonHandler.c PROPERTIES COMPILE_OPTIONS -w)
endif()

seqc_add_test(test_usbtx     IDS usbtx EXTRA ${LXR_ROOT}/Core/Src/Soundengine/USB/usb_midiTx.c)  # E2.8  USB MIDI out packets and ring

# The CC handler's table of original values (E4.5): test_midicc compiles the
# firmware's MidiParser.c with the stand-in headers in stubs/ (the HAL, the USB
# device class) and the firmware's own FX bus header. For this target the table,
# midiParser_originalCcValues, is a pointer to a 255-byte array, which the test
# points into a buffer with guard bytes around it.
seqc_add_test(test_midicc    IDS midicc EXTRA ${LXR_ROOT}/Core/Src/Soundengine/MIDI/MidiParser.c)  # E4.5  CC values stay in their table; E4.8 the step page's Note On
if(TARGET test_midicc)
    target_include_directories(test_midicc BEFORE PRIVATE stubs)
    target_include_directories(test_midicc SYSTEM PRIVATE ${LXR_ROOT}/Core/Inc)
    target_compile_definitions(test_midicc PRIVATE "midiParser_originalCcValues=(*midiParser_ccTable)")
    set_source_files_properties(${LXR_ROOT}/Core/Src/Soundengine/MIDI/MidiParser.c PROPERTIES COMPILE_OPTIONS -w)
endif()

# The sequencer glue (E4.6): test_sequencer compiles the firmware's sequencer.c
# with the timing core, the stand-in headers in stubs/ (the HAL with the interrupt
# mask of the mailbox) and the firmware's own FX bus header. The song it edits is
# a buffer of the test with guard bytes after the 64 entries. stubs/seq_random_test.h
# is forced into sequencer.c: its probability draw, SEQ_RANDOM(), becomes the
# test's seqTest_random() (stage 1b F5).
seqc_add_test(test_sequencer IDS sequencer EXTRA ${LXR_ROOT}/Core/Src/Soundengine/Sequencer/sequencer.c)  # E4.6  song page position, automation step; stage 1b F5 SEQ_RANDOM()
if(TARGET test_sequencer)
    target_include_directories(test_sequencer BEFORE PRIVATE stubs)
    target_include_directories(test_sequencer SYSTEM PRIVATE ${LXR_ROOT}/Core/Inc)
    set_source_files_properties(${LXR_ROOT}/Core/Src/Soundengine/Sequencer/sequencer.c PROPERTIES
        COMPILE_OPTIONS "-w;-include;${CMAKE_CURRENT_SOURCE_DIR}/stubs/seq_random_test.h")
endif()

# The preset manager (E4.7): test_presets compiles the firmware's presetManager.c
# and frontPanelParser.c with the stand-in headers in stubs/presets/ (FatFs
# without the SD card driver) and stubs/ (the HAL), the firmware's FatFs header
# and configuration, and an in-memory card in the test. For this target
# currentFolder, savefileNames and mainParameters are pointers to arrays of their
# size, which the test points into buffers with guard bytes after the arrays.
seqc_add_test(test_presets   IDS presets EXTRA ${LXR_ROOT}/Core/Src/Preset/presetManager.c ${LXR_ROOT}/Core/Src/frontPanelParser.c)  # E4.7  project re-save, kit and project slot numbers; E4.8 NRPN 121
if(TARGET test_presets)
    target_include_directories(test_presets BEFORE PRIVATE stubs/presets stubs)
    target_include_directories(test_presets SYSTEM PRIVATE
        ${LXR_ROOT}/Core/Inc
        ${LXR_ROOT}/Middlewares/Third_Party/FatFs/src
        ${LXR_ROOT}/FATFS/Target)
    target_compile_definitions(test_presets PRIVATE
        "currentFolder=(*preset_folderBuf)"
        "savefileNames=(*preset_namesTable)"
        "mainParameters=(*preset_mainTable)")
    set_source_files_properties(
        ${LXR_ROOT}/Core/Src/Preset/presetManager.c
        ${LXR_ROOT}/Core/Src/frontPanelParser.c
        PROPERTIES COMPILE_OPTIONS -w)
endif()

# The ping-pong delay (E4.8): test_delay compiles the firmware's Delay.c with the
# stand-in headers in stubs/ (the HAL with the CMSIS intrinsics of the delay).
# For this target FX_BUFFER is a pointer to an array of its size, which the test
# points into a buffer with guard entries after the array.
seqc_add_test(test_delay     IDS delay EXTRA ${LXR_ROOT}/Core/Src/Soundengine/DSPAudio/FX/Delay.c)  # E4.8  delay type switch
if(TARGET test_delay)
    target_include_directories(test_delay BEFORE PRIVATE stubs)
    target_include_directories(test_delay SYSTEM PRIVATE ${LXR_ROOT}/Core/Inc)
    target_compile_definitions(test_delay PRIVATE "FX_BUFFER=(*delay_fxBuffer)")
    set_source_files_properties(${LXR_ROOT}/Core/Src/Soundengine/DSPAudio/FX/Delay.c PROPERTIES COMPILE_OPTIONS -w)
endif()

# The sound engine's broken states (E4.9): test_dsp compiles the firmware's DSP
# files with the stand-in headers in stubs/ at -O3, as the device build has them.
# ResonantFilter.c, Oscillator.c and wavetable.c get the firmware's floating-point
# options, so that the compiler may remove a NaN test as GCC does on the device
# (clang removes more than GCC). Decay.c gets strict IEEE math: on the device the
# pitch EG's slope 127 divides by an exact 0, but under -ffast-math clang rewrites
# that expression to a division by a tiny number. A NaN or Inf converted to an
# integer sample is undefined in C; the device's VCVT gives 0 or saturates, so that
# check of UBSan is off for these files. ResonantFilter.c uses M_PI, which the
# firmware gets from its GNU C mode; the C library of Linux defines it under strict
# C11 only with _DEFAULT_SOURCE.
set(DSP_FAST_MATH_SOURCES
    ${LXR_ROOT}/Core/Src/Soundengine/DSPAudio/ResonantFilter.c
    ${LXR_ROOT}/Core/Src/Soundengine/DSPAudio/Oscillator.c
    ${LXR_ROOT}/Core/Src/Soundengine/DSPAudio/wavetable.c)
set(DSP_STRICT_MATH_SOURCES ${LXR_ROOT}/Core/Src/Soundengine/DSPAudio/Decay.c)
seqc_add_test(test_dsp       IDS dsp EXTRA ${DSP_FAST_MATH_SOURCES} ${DSP_STRICT_MATH_SOURCES})  # E4.9  filter NaN state, pitch EG slope, waveforms of 6 or more
if(TARGET test_dsp)
    target_include_directories(test_dsp BEFORE PRIVATE stubs)
    target_include_directories(test_dsp SYSTEM PRIVATE ${LXR_ROOT}/Core/Inc)
    target_compile_definitions(test_dsp PRIVATE _DEFAULT_SOURCE)
    set_source_files_properties(${DSP_FAST_MATH_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w;-O3;-ffast-math;-freciprocal-math;-fno-sanitize=float-cast-overflow")
    set_source_files_properties(${DSP_STRICT_MATH_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w;-O3;-fno-sanitize=float-cast-overflow")
endif()

# The modulation nodes (stage 1b F1): test_modnode compiles the firmware's
# modulationNode.c with the stand-in headers in stubs/. A node at destination 0,
# PAR_NONE, has no parameter pointer, and fw 1.7 read its original value through
# it at every CC 1.
seqc_add_test(test_modnode   IDS modnode EXTRA ${LXR_ROOT}/Core/Src/Soundengine/DSPAudio/modulationNode.c)  # stage 1b F1  nodes at PAR_NONE keep their value
if(TARGET test_modnode)
    target_include_directories(test_modnode BEFORE PRIVATE stubs)
    target_include_directories(test_modnode SYSTEM PRIVATE ${LXR_ROOT}/Core/Inc)
    set_source_files_properties(${LXR_ROOT}/Core/Src/Soundengine/DSPAudio/modulationNode.c PROPERTIES COMPILE_OPTIONS -w)
endif()

# The float-to-uint32 conversion of the oscillators (stage 1b F6): test_f2u32
# checks f2u32_sat() (Core/Inc/Soundengine/DSPAudio/f2u32.h) at its edge values,
# and against arm64's FCVTZU on an arm64 host.
seqc_add_test(test_f2u32     IDS f2u32)  # stage 1b F6  saturating float-to-uint32

# The codec's DMA buffers (stage 1b F7): test_dmabuf includes the firmware's
# AudioCodecManager.c itself, because the alignment of a definition is visible
# only in its own translation unit. The stand-in headers in stubs/ come first.
seqc_add_test(test_dmabuf    IDS dmabuf)  # stage 1b F7  4-byte aligned DMA buffers
if(TARGET test_dmabuf)
    target_include_directories(test_dmabuf BEFORE PRIVATE stubs)
    target_include_directories(test_dmabuf SYSTEM PRIVATE ${LXR_ROOT}/Core/Inc)
endif()
