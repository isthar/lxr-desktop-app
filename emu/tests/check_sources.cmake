# M22: every file in Core/sources.cmake is compiled or excluded (stage 1b spec
# 9.1, 12.1).
#
#   cmake -DLXR_ROOT=<repository root> -DMODE=<mode> -DWORK=<dir> -P check_sources.cmake
#
# MODE real checks Core/sources.cmake and expects 72 compiled files (70 before
# LICHOMAT v0.4's atmo.c, plan A2, and DjFilter.c, plan A6). MODE extra adds a header to a list, and MODE
# stale drops Src/lcd.c from its list: both must fail, and ctest matches the
# message of each failure.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
cmake_minimum_required(VERSION 3.21)
include("${LXR_ROOT}/emu/cmake/LxrSources.cmake")

set(file "${LXR_ROOT}/Core/sources.cmake")
if(NOT MODE STREQUAL "real")
    file(MAKE_DIRECTORY "${WORK}")
    set(file "${WORK}/sources_${MODE}.cmake")
    set(text "include(\"${LXR_ROOT}/Core/sources.cmake\")\n")
    if(MODE STREQUAL "extra")
        string(APPEND text "list(APPEND LXR_SOURCES_UI \"${LXR_ROOT}/Core/Inc/config.h\")\n")
    elseif(MODE STREQUAL "stale")
        string(APPEND text "list(FILTER LXR_SOURCES_UI EXCLUDE REGEX \"lcd\\\\.c$\")\n")
    else()
        message(FATAL_ERROR "MODE must be real, extra or stale")
    endif()
    file(WRITE "${file}" "${text}")
endif()

lxr_emu_sources("${file}" "${LXR_ROOT}/Core" compiled errors)
list(LENGTH compiled n)
foreach(e IN LISTS errors)
    message(STATUS "M22: ${e}")
endforeach()
if(errors)
    message(FATAL_ERROR "M22: ${MODE}: the source lists do not match the emulator's build")
endif()
if(NOT n EQUAL 72)
    message(FATAL_ERROR "M22: ${n} compiled files; expected 71 of Core/Src and appMain.c")
endif()
message(STATUS "M22: ${n} files compiled, ${MODE} lists consistent")
