# The firmware's module source lists for the emulator (stage 1b spec 3.4, 9.1).
#
# lxr_emu_sources(<sources.cmake> <base dir> <compiled var> <errors var>)
#
# Reads every LXR_SOURCES_* list that <sources.cmake> defines (Core/sources.cmake,
# firmware change F8), with relative entries taken from <base dir>. Sets
# <compiled var> to the absolute paths of the .c files the emulator compiles, in
# the lists' order, and <errors var> to a message per problem:
# - a listed file that is neither a compiled .c file nor excluded (M22);
# - an exclusion that no list holds any more;
# - a file listed twice, or a listed file that does not exist.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.

# What the virtual device does instead (spec 3.4), relative to Core/.
set(LXR_EMU_EXCLUDED
    Startup/startup_stm32f765vgtx.s     # the register and HAL model
    Src/main.c                          # power-on; boot and passes come from appMain.c
    Src/stm32f7xx_it.c                  # the scheduler's interrupt bodies
    Src/stm32f7xx_hal_msp.c             # the register and HAL model
    Src/system_stm32f7xx.c              # the register and HAL model
    Src/syscalls.c                      # the host's C library
    Src/sysmem.c                        # the host's C library
    Src/flash_if.c                      # the bootloader update, never built
    Src/lcd.c                           # the LCD model (no licence)
    Src/Soundengine/cs4344_cs5343.c     # the codec model
)

# the module order of Core/CMakeLists.txt; lists added later follow by name
set(LXR_EMU_MODULE_ORDER PLATFORM IO UI PRESET DSP ENGINE MIDI SEQUENCER USB_APP)

function(lxr_emu_sources file base out_compiled out_errors)
    include("${file}")
    get_cmake_property(vars VARIABLES)
    set(lists "")
    foreach(module IN LISTS LXR_EMU_MODULE_ORDER)
        list(APPEND lists LXR_SOURCES_${module})
    endforeach()
    list(SORT vars)
    foreach(v IN LISTS vars)
        if(v MATCHES "^LXR_SOURCES_[A-Z0-9_]+$" AND NOT v IN_LIST lists)
            list(APPEND lists ${v})
        endif()
    endforeach()

    set(excluded "")
    foreach(e IN LISTS LXR_EMU_EXCLUDED)
        cmake_path(ABSOLUTE_PATH e BASE_DIRECTORY "${base}" NORMALIZE OUTPUT_VARIABLE abs)
        list(APPEND excluded "${abs}")
    endforeach()

    set(compiled "")
    set(seen "")
    set(errors "")
    foreach(v IN LISTS lists)
        if(NOT DEFINED ${v})
            list(APPEND errors "${v} is not defined")
            continue()
        endif()
        foreach(f IN LISTS ${v})
            cmake_path(ABSOLUTE_PATH f BASE_DIRECTORY "${base}" NORMALIZE OUTPUT_VARIABLE abs)
            if(abs IN_LIST seen)
                list(APPEND errors "${f} is listed twice")
                continue()
            endif()
            list(APPEND seen "${abs}")
            if(NOT EXISTS "${abs}")
                list(APPEND errors "${f} (${v}) does not exist")
            elseif(abs IN_LIST excluded)
                # the virtual device replaces it
            elseif(abs MATCHES "\\.c$")
                list(APPEND compiled "${abs}")
            else()
                list(APPEND errors "${f} (${v}) is neither compiled nor excluded")
            endif()
        endforeach()
    endforeach()
    foreach(e IN LISTS excluded)
        if(NOT e IN_LIST seen)
            list(APPEND errors "the exclusion ${e} is in no list")
        endif()
    endforeach()
    set(${out_compiled} "${compiled}" PARENT_SCOPE)
    set(${out_errors} "${errors}" PARENT_SCOPE)
endfunction()
