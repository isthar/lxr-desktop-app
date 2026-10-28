# The LICHOMAT emulator (stage 1b spec 9.2): the whole firmware on a virtual
# device, built with the host's Apple clang. A CMake project of its own, next to
# tests/; the firmware build cannot host it, because it forces the Arm toolchain.
#
#   source tools/env.sh
#   cmake -S emu -B build/emu -G Ninja
#   cmake --build build/emu
#   ctest --test-dir build/emu --output-on-failure
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.

cmake_minimum_required(VERSION 3.21)

# arm64 matches the device where x86-64 does not: float-to-int conversions
# saturate, and x % 0 gives x (spec 8.1)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "The emulator is built for arm64 only")
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "macOS 13 or later (Q15)")
project(lichomat_emu LANGUAGES C)

if(NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    message(FATAL_ERROR "The emulator builds on macOS for arm64 only (spec 8.1)")
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    message(FATAL_ERROR "The emulator builds with Apple clang (spec 8.1), not ${CMAKE_C_COMPILER_ID}")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug (-O0) or Release (-O2)" FORCE)
endif()
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)	# gnu11, the firmware's dialect (spec 8.2)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sanitizers, as for -fsanitize=. The Debug default leaves out ASan, because ASan
# binaries hang inside the sandbox used for development; CI uses address,undefined.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(lichomat_emu_sanitize_default "")
else()
    set(lichomat_emu_sanitize_default "undefined")
endif()
set(LICHOMAT_EMU_SANITIZE "${lichomat_emu_sanitize_default}" CACHE STRING
    "Sanitizers for the emulator (e.g. address,undefined; empty = none)")
set(LICHOMAT_EMU_FP "strict" CACHE STRING "Floating point: strict, or firmware-like (spec 8.2)")
set_property(CACHE LICHOMAT_EMU_FP PROPERTY STRINGS strict firmware-like)
if(NOT LICHOMAT_EMU_FP MATCHES "^(strict|firmware-like)$")
    message(FATAL_ERROR "LICHOMAT_EMU_FP must be strict or firmware-like, not ${LICHOMAT_EMU_FP}")
endif()

get_filename_component(LXR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(EMU_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# --- the firmware's source lists (spec 9.1, firmware change F8) -------------------
include(cmake/LxrSources.cmake)
lxr_emu_sources("${LXR_ROOT}/Core/sources.cmake" "${LXR_ROOT}/Core" EMU_FW_SOURCES EMU_FW_ERRORS)
if(EMU_FW_ERRORS)
    string(REPLACE ";" "\n  " errors "${EMU_FW_ERRORS}")
    message(FATAL_ERROR "Core/sources.cmake does not match the emulator (M22):\n  ${errors}")
endif()
list(LENGTH EMU_FW_SOURCES emu_fw_count)
message(STATUS "emulator: ${emu_fw_count} firmware files")

# The splash shows the firmware's own name and version (spec 9.1).
file(READ "${LXR_ROOT}/CMakeLists.txt" lxr_top)
string(REGEX MATCH "set\\(LICHOMAT_NAME \"([A-Za-z0-9_.-]+)\"" lxr_match "${lxr_top}")
set(EMU_FIRMWARE_NAME "${CMAKE_MATCH_1}")
string(REGEX MATCH "project\\(LXR_V2_STM32F765[^)]*VERSION ([0-9]+\\.[0-9]+)" lxr_match "${lxr_top}")
set(EMU_FIRMWARE_VERSION "${CMAKE_MATCH_1}")
if(NOT EMU_FIRMWARE_NAME OR NOT EMU_FIRMWARE_VERSION)
    message(FATAL_ERROR "cannot read LICHOMAT_NAME and the version from ${LXR_ROOT}/CMakeLists.txt")
endif()
message(STATUS "emulator: ${EMU_FIRMWARE_NAME} ${EMU_FIRMWARE_VERSION}")

# --- host tests (spec 12.1) -------------------------------------------------------
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# M22: every file in Core/sources.cmake is compiled or excluded; an extra header and
# a dropped exclusion must both be found
foreach(mode real extra stale)
    add_test(NAME M22_${mode} COMMAND ${CMAKE_COMMAND} -DLXR_ROOT=${LXR_ROOT} -DMODE=${mode}
        -DWORK=${CMAKE_BINARY_DIR}/m22 -P ${EMU_DIR}/tests/check_sources.cmake)
endforeach()
set_tests_properties(M22_extra PROPERTIES PASS_REGULAR_EXPRESSION "config.h \\(LXR_SOURCES_UI\\) is neither compiled nor excluded")
set_tests_properties(M22_stale PROPERTIES PASS_REGULAR_EXPRESSION "the exclusion .*/Core/Src/lcd.c is in no list")
