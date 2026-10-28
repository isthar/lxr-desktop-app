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

# --- flags (spec 8.2) -----------------------------------------------------------------
set(EMU_FP_FLAGS -ffp-contract=off -fno-fast-math -fno-strict-float-cast-overflow
    -fno-builtin-sinf -fno-builtin-cosf -fno-builtin-expf -fno-builtin-exp -fno-builtin-logf
    -funsigned-char -fno-strict-aliasing)
if(LICHOMAT_EMU_FP STREQUAL "firmware-like")
    list(APPEND EMU_FP_FLAGS -freciprocal-math)
endif()
# A sanitizer report is a firmware fault (spec 11), so none recovers.
set(EMU_SAN_FLAGS "")
if(LICHOMAT_EMU_SANITIZE)
    set(EMU_SAN_FLAGS -fsanitize=${LICHOMAT_EMU_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${LICHOMAT_EMU_SANITIZE})
endif()
message(STATUS "emulator: ${CMAKE_BUILD_TYPE}, sanitizers '${LICHOMAT_EMU_SANITIZE}', fp ${LICHOMAT_EMU_FP}")

# Core/Inc/main.h -> emu/overlay/main.h (spec 3.4)
configure_file(overlay/main_overlay.yaml.in "${CMAKE_BINARY_DIR}/main_overlay.yaml" @ONLY)
set(EMU_OVERLAY "${CMAKE_BINARY_DIR}/main_overlay.yaml")

# What every object that includes a firmware header needs: the stubs before Core/Inc,
# the overlay, the flags, and no TCM sections (Mach-O rejects them, spec 3.4).
add_library(emu_fw_headers INTERFACE)
target_include_directories(emu_fw_headers BEFORE INTERFACE "${EMU_DIR}/stubs")
target_include_directories(emu_fw_headers SYSTEM INTERFACE
    "${LXR_ROOT}/Core/Inc" "${LXR_ROOT}/Middlewares/Third_Party/FatFs/src")
target_compile_options(emu_fw_headers INTERFACE ${EMU_FP_FLAGS} ${EMU_SAN_FLAGS}
    "-Dsection(x)=unused" "SHELL:-ivfsoverlay ${EMU_OVERLAY}")

# --- the firmware objects (spec 3.4, 9.2) ------------------------------------------------
add_library(lxr_firmware OBJECT ${EMU_FW_SOURCES})
target_link_libraries(lxr_firmware PRIVATE emu_fw_headers)
target_compile_options(lxr_firmware PRIVATE -w)	# the firmware build is their warning gate
if(LICHOMAT_EMU_SANITIZE)
    # ASan and UBSan do not see reads of uninitialised locals (spec 9.2)
    target_compile_options(lxr_firmware PRIVATE -ftrivial-auto-var-init=pattern)
endif()
set(EMU_SRC "${LXR_ROOT}/Core/Src")
# the splash shows the firmware's own name and version (spec 9.1)
set_source_files_properties("${EMU_SRC}/appMain.c" PROPERTIES COMPILE_DEFINITIONS
    "FIRMWARE_NAME=\"${EMU_FIRMWARE_NAME}\";FIRMWARE_VERSION=\"${EMU_FIRMWARE_VERSION}\"")
# the probability draw takes the sequencer stream (spec 4.9, firmware change F5)
set_source_files_properties("${EMU_SRC}/Soundengine/Sequencer/sequencer.c" PROPERTIES
    COMPILE_OPTIONS "-include;${EMU_DIR}/stubs/emu_seq_random.h")
if(LICHOMAT_EMU_SANITIZE MATCHES "undefined")
    # arm64 gives x % 0 the device's result, x (EuklidGenerator.c:235-236, spec 8.1, 9.2)
    set_source_files_properties("${EMU_SRC}/Soundengine/Sequencer/EuklidGenerator.c" PROPERTIES
        COMPILE_OPTIONS -fno-sanitize=integer-divide-by-zero)
    # B13, B14: see sanitize/pwm_ignorelist.txt
    set_source_files_properties("${EMU_SRC}/Soundengine/DSPAudio/Oscillator.c" PROPERTIES
        COMPILE_OPTIONS "-fsanitize-ignorelist=${EMU_DIR}/sanitize/pwm_ignorelist.txt")
endif()

# --- the vendored libm (spec 8.1): Arm Optimized Routines v23.01 under MIT ---------------
file(GLOB EMU_LIBM_SOURCES CONFIGURE_DEPENDS "${EMU_DIR}/libm/*.c")
add_library(emu_libm OBJECT ${EMU_LIBM_SOURCES})
target_compile_options(emu_libm PRIVATE ${EMU_FP_FLAGS} ${EMU_SAN_FLAGS} -w)
target_compile_definitions(emu_libm PRIVATE USE_GLIBC_ABI=0)	# Mach-O has no symbol aliases

# --- the virtual device (spec 3.3): C11, -Wall -Wextra -Werror ---------------------------
file(GLOB EMU_CORE_SOURCES CONFIGURE_DEPENDS "${EMU_DIR}/core/*.c")
list(REMOVE_ITEM EMU_CORE_SOURCES "${EMU_DIR}/core/emu_hostfs.c")
add_library(emu_core OBJECT ${EMU_CORE_SOURCES})
target_link_libraries(emu_core PRIVATE emu_fw_headers)
target_compile_options(emu_core PRIVATE -Wall -Wextra -Werror)
# The card backend's POSIX half: <dirent.h> clashes with FatFs's DIR (spec 4.8), and it
# includes no firmware header, so it gets no section define.
add_library(emu_hostfs OBJECT "${EMU_DIR}/core/emu_hostfs.c")
target_compile_options(emu_hostfs PRIVATE ${EMU_FP_FLAGS} ${EMU_SAN_FLAGS} -Wall -Wextra -Werror
    "SHELL:-ivfsoverlay ${EMU_OVERLAY}")
set(EMU_DEVICE_OBJECTS $<TARGET_OBJECTS:lxr_firmware> $<TARGET_OBJECTS:emu_core>
    $<TARGET_OBJECTS:emu_hostfs> $<TARGET_OBJECTS:emu_libm>)

# --- the test harness -------------------------------------------------------------------------
add_library(emu_test_options INTERFACE)
target_link_libraries(emu_test_options INTERFACE emu_fw_headers)
target_include_directories(emu_test_options INTERFACE "${EMU_DIR}/core" "${EMU_DIR}/tests" "${LXR_ROOT}/tests")
target_compile_options(emu_test_options INTERFACE -Wall -Wextra -Werror)

# emu_add_test(<name> IDS <test>...): emu/tests/<name>.c with the whole virtual
# device. Each TEST() is a ctest test of its own and runs in its own process: a
# firmware test powers on once per process (spec 3.3).
function(emu_add_test name)
    cmake_parse_arguments(ARG "" "" "IDS" ${ARGN})
    add_executable(${name} tests/${name}.c ${EMU_DEVICE_OBJECTS})
    target_link_libraries(${name} PRIVATE emu_test_options)
    foreach(id IN LISTS ARG_IDS)
        add_test(NAME ${id} COMMAND ${name} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT 300)
    endforeach()
endfunction()

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

emu_add_test(test_intrinsics IDS M1_ssat M1_qadd_qsub M1_qadd16_qsub16 M1_pkhbt M1_clz M1_smlald M1_apsr M1_f2u32)
emu_add_test(test_random IDS M13_first_values M13_streams_apart)
emu_add_test(test_clock IDS M2_frames M2_cyccnt_and_tick M2_seqp_now_wraps)
emu_add_test(test_sched IDS M3_periods M3_order_at_equal_times M3_primask_defers M3_primask_in_body
    M3_time_only_in_thread M3_tick_rule M3_pass_rule M3_pass_after_slow_pass M3_hook M3_watchdog
    M3_nvic M3_trail M3_spin_under_primask M3_direct_call_grid M3_spin_wait M3_no_run_from_a_body
    M4_tim2_counter M4_tim2_stop)
emu_add_test(test_codec IDS M5_requests M5_halves M4_tim2_after_request M5_hook_per_block M3_paced_by_ring)
emu_add_test(test_power IDS M6_power_on_once)
emu_add_test(test_boot IDS M6_splash M6_no_card M6_card M6_no_globals)
emu_add_test(test_hook IDS M3_fw_normal_pass M3_fw_kit_load M3_fw_sd_error M3_fw_screensaver M3_fw_splash_paced)
emu_add_test(test_lcd IDS M7_commands M7_clear M7_cgram M7_row_wrap M7_arrows M7_led_latch M7_led_pulse M7_fw_repaint)
emu_add_test(test_panel IDS M8_scan M8_gray M8_fw_encoder10 M8_fw_push_phases M8_fw_knob_normal M8_fw_knob_list
    M8_fw_no_edit_at_boot M8_fw_faders)
emu_add_test(test_card IDS M9_scenario M9_truncate M9_order M9_skipped M9_duplicate M9_lock M9_card_switch M9_copy
    M9_fw_save_load)

# M19: the firmware objects import no libm name but the vendored ones and fmod
add_library(emu_libm_canary OBJECT tests/libm_canary.c)
# -O2 in every build: clang folds expf of a constant and merges sinf and cosf
# into ___sincosf_stret when it optimises, not at Debug's -O0, where the canary
# would pass even without -fno-builtin-*. The last -O on the command line wins.
target_compile_options(emu_libm_canary PRIVATE ${EMU_FP_FLAGS} -O2 -w)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/m19/firmware.txt" CONTENT "$<JOIN:$<TARGET_OBJECTS:lxr_firmware>,\n>\n")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/m19/core.txt"
    CONTENT "$<JOIN:$<TARGET_OBJECTS:emu_core>,\n>\n$<JOIN:$<TARGET_OBJECTS:emu_hostfs>,\n>\n")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/m19/libm.txt" CONTENT "$<JOIN:$<TARGET_OBJECTS:emu_libm>,\n>\n")
add_test(NAME M19 COMMAND /bin/bash "${EMU_DIR}/tests/check_libm.sh" "${CMAKE_BINARY_DIR}/m19"
    "${CMAKE_BINARY_DIR}/m19/firmware.txt" "${CMAKE_BINARY_DIR}/m19/core.txt" "${CMAKE_BINARY_DIR}/m19/libm.txt"
    "$<TARGET_OBJECTS:emu_libm_canary>" "$<TARGET_FILE:test_intrinsics>")
set_tests_properties(M19 PROPERTIES PASS_REGULAR_EXPRESSION "M19: [0-9]+ imports, all on the allow-list")

# M20: the firmware text that the models re-implement or take their constants from
add_test(NAME M20 COMMAND ${Python3_EXECUTABLE} "${EMU_DIR}/tests/drift.py" check "${LXR_ROOT}")

# M21: the licence gate over what the binaries were built from, and the name scan.
# The binaries of R1 are the test binaries that link the whole firmware. It runs
# ninja -t deps, so ctest needs ninja on PATH (source tools/env.sh).
set(EMU_GATE ${Python3_EXECUTABLE} "${EMU_DIR}/tests/licence_gate.py")
add_test(NAME M21 COMMAND ${EMU_GATE} "${LXR_ROOT}" "${CMAKE_BINARY_DIR}"
    $<TARGET_FILE:test_boot> $<TARGET_FILE:test_hook>)
add_test(NAME M21_rejects COMMAND ${EMU_GATE} --extra Core/Inc/lcd.h --extra Core/Inc/main.h
    "${LXR_ROOT}" "${CMAKE_BINARY_DIR}" $<TARGET_FILE:test_boot>)
# exactly these two errors, in this order, and no other
set_tests_properties(M21_rejects PROPERTIES PASS_REGULAR_EXPRESSION
    "^licence gate: Core/Inc/lcd\\.h is neither GPL-3\\.0-only nor on the allow-list\nlicence gate: Core/Inc/main\\.h names SLA0044\nlicence gate: 2 errors\n$")
# M21 passes with the name scan or the overlay check turned off: this one gives
# each a case to reject, an object that names the maker and the product, and a
# compile command without the overlay
add_library(emu_name_canary OBJECT tests/name_canary.c)
target_link_libraries(emu_name_canary PRIVATE emu_test_options)
add_test(NAME M21_negative COMMAND ${Python3_EXECUTABLE} "${EMU_DIR}/tests/licence_gate_neg.py"
    "${LXR_ROOT}" "${CMAKE_BINARY_DIR}" $<TARGET_OBJECTS:emu_name_canary>)
