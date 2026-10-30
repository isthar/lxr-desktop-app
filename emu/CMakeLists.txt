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

# --- lichomat-render (spec 5) -------------------------------------------------------------
# What --version and the summary print (spec 8.4, 10.1): the toolchain, fixed at
# configure time, and the commit, read at every build by emu_render_build, which
# rewrites render_build.h only when the commit changes (cmake/RenderBuild.cmake).
execute_process(COMMAND ${CMAKE_C_COMPILER} --version OUTPUT_VARIABLE emu_cc_version)
string(REGEX MATCH "^[^\n]*" EMU_CLANG "${emu_cc_version}")
execute_process(COMMAND xcrun --show-sdk-version OUTPUT_VARIABLE EMU_SDK OUTPUT_STRIP_TRAILING_WHITESPACE)
set(EMU_RENDER_BUILD ${CMAKE_COMMAND} "-DLXR_ROOT=${LXR_ROOT}" "-DIN=${EMU_DIR}/render/render_build.h.in"
    "-DOUT=${CMAKE_BINARY_DIR}/render_build.h" "-DEMU_CLANG=${EMU_CLANG}" "-DEMU_SDK=${EMU_SDK}"
    "-DEMU_FIRMWARE_VERSION=${EMU_FIRMWARE_VERSION}" -P "${EMU_DIR}/cmake/RenderBuild.cmake")
execute_process(COMMAND ${EMU_RENDER_BUILD})
add_custom_target(emu_render_build COMMAND ${EMU_RENDER_BUILD}
    BYPRODUCTS "${CMAKE_BINARY_DIR}/render_build.h" VERBATIM)
message(STATUS "emulator: ${EMU_CLANG}, SDK ${EMU_SDK}")

file(GLOB EMU_RENDER_SOURCES CONFIGURE_DEPENDS "${EMU_DIR}/render/*.c")
list(REMOVE_ITEM EMU_RENDER_SOURCES "${EMU_DIR}/render/render_fw.c")
# emu_render_exe(<name> <hooks>): the renderer; hooks 1 adds the test-only options
# (spec 5.3). Only render_fw.c includes firmware headers; the other files get the
# overlay flag, which the licence gate requires of every compile command, but not
# the section define (spec 3.4), as emu_hostfs.c.
function(emu_render_exe name hooks)
    add_library(${name}_fw OBJECT "${EMU_DIR}/render/render_fw.c")
    target_link_libraries(${name}_fw PRIVATE emu_fw_headers)
    target_include_directories(${name}_fw PRIVATE "${EMU_DIR}/core" "${EMU_DIR}/render" "${CMAKE_BINARY_DIR}")
    target_compile_options(${name}_fw PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(${name}_fw PRIVATE EMU_RENDER_TEST_HOOKS=${hooks})
    add_executable(${name} ${EMU_RENDER_SOURCES} $<TARGET_OBJECTS:${name}_fw> ${EMU_DEVICE_OBJECTS})
    target_include_directories(${name} PRIVATE "${EMU_DIR}/core" "${EMU_DIR}/render" "${CMAKE_BINARY_DIR}")
    target_compile_options(${name} PRIVATE ${EMU_FP_FLAGS} ${EMU_SAN_FLAGS} -Wall -Wextra -Werror
        "SHELL:-ivfsoverlay ${EMU_OVERLAY}")
    target_compile_definitions(${name} PRIVATE EMU_RENDER_TEST_HOOKS=${hooks})
    add_dependencies(${name} emu_render_build)
endfunction()
emu_render_exe(lichomat-render 0)
emu_render_exe(lichomat-render-test 1)

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
    M3_time_only_in_thread M3_tick_rule M3_pass_rule M3_pass_after_slow_pass M3_hook M3_block_hook_at_release M3_watchdog
    M3_nvic M3_trail M3_spin_under_primask M3_direct_call_grid M3_spin_wait M3_no_run_from_a_body M3_no_run_from_a_pass
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
emu_add_test(test_din IDS M10_wire_in M10_wire_out M10_tx_enable_late M10_rx_during_tx M10_tx_from_rx_body
    M10_tx_slot_before_pass M10_overrun M10_spp_and_pc)
emu_add_test(test_usb IDS M11_walk M11_window_16 M11_window_start M11_out M11_no_stall_drop)
emu_add_test(test_jacks IDS M12_clock_in M12_reset M12_clock_out_core M12_fw_clock_out M8_fw_output_jacks)
emu_add_test(test_fwchanges IDS M15_f1_cc1 M15_f2_morph M15_f3_clear_pages M15_f9_band10
    M15_f11_knobs_at_boot M15_f12_euklid M15_f13_diode_table
    M15_f14_transient_shift M15_f16_full_phase M15_f17_negative_frequency
    M15_f18_mono_delay)
emu_add_test(test_pingpong IDS M16_pingpong)

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
# The binaries are two test binaries that link the whole firmware, and
# lichomat-render with its --version notices. It runs
# ninja -t deps, so ctest needs ninja on PATH (source tools/env.sh).
set(EMU_GATE ${Python3_EXECUTABLE} "${EMU_DIR}/tests/licence_gate.py")
add_test(NAME M21 COMMAND ${EMU_GATE} "${LXR_ROOT}" "${CMAKE_BINARY_DIR}"
    $<TARGET_FILE:test_boot> $<TARGET_FILE:test_hook> $<TARGET_FILE:lichomat-render>)
add_test(NAME M21_rejects COMMAND ${EMU_GATE} --extra Core/Inc/lcd.h --extra Core/Inc/main.h
    "${LXR_ROOT}" "${CMAKE_BINARY_DIR}" $<TARGET_FILE:test_boot>)
# exactly these two errors, in this order, and no other
set_tests_properties(M21_rejects PROPERTIES PASS_REGULAR_EXPRESSION
    "^licence gate: Core/Inc/lcd\\.h is neither GPL-3\\.0-only nor on the allow-list\nlicence gate: Core/Inc/main\\.h names SLA0044\nlicence gate: 2 errors\n$")
# M21 passes with the name scan or the overlay check turned off: this one gives
# each a case to reject, an object that names the maker and the product in string
# literals, a table of char[16] and a char array, and a compile command without
# the overlay
add_library(emu_name_canary OBJECT tests/name_canary.c)
target_link_libraries(emu_name_canary PRIVATE emu_test_options)
add_test(NAME M21_negative COMMAND ${Python3_EXECUTABLE} "${EMU_DIR}/tests/licence_gate_neg.py"
    "${LXR_ROOT}" "${CMAKE_BINARY_DIR}" $<TARGET_OBJECTS:emu_name_canary>)

# M17, M14, M23: lichomat-render through emu/tests/render_test.py. --sanitized 1:
# the build has UBSan, so --test-fault ubsan reports (M17_fault).
if(LICHOMAT_EMU_SANITIZE MATCHES "undefined")
    set(emu_render_ubsan 1)
else()
    set(emu_render_ubsan 0)
endif()
set(EMU_RENDER_TEST ${Python3_EXECUTABLE} "${EMU_DIR}/tests/render_test.py"
    --render $<TARGET_FILE:lichomat-render> --render-test $<TARGET_FILE:lichomat-render-test>
    --root "${LXR_ROOT}" --work "${CMAKE_BINARY_DIR}/render-test" --sanitized ${emu_render_ubsan})
function(emu_render_tests)
    foreach(id IN LISTS ARGN)
        add_test(NAME ${id} COMMAND ${EMU_RENDER_TEST} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT 600)
    endforeach()
endfunction()
emu_render_tests(M17_cli_usage M17_cli_info M17_card_errors M17_seed_card M17_t0 M17_defaults M17_wav
    M17_card_copy M17_out_errors M17_script_errors M17_events_din M17_events_usb M17_events_jacks M17_events_panel
    M17_stereo M17_pattern M17_song M17_bpm M17_param_engine M17_param_refused M17_param_table M17_bars
    M17_sync_fallback M17_save M17_fault M17_hang)
emu_render_tests(V02_ui V02_tempo V02_groove V02_conditions V02_files V02_rotation)
emu_render_tests(V03_rolls V03_slide V03_glide_through_ratchets V03_fill V03_scenes V03_scene_file)
# M23: emu/tests/cards/make.sh gives the committed card fixtures (spec 5.6)
emu_render_tests(M23)

# the goldens (spec 8.6, 12.2): one ctest id per emu/tests/goldens/*.txt
file(GLOB EMU_GOLDENS CONFIGURE_DEPENDS "${EMU_DIR}/tests/goldens/*.txt")
foreach(g IN LISTS EMU_GOLDENS)
    get_filename_component(id "${g}" NAME_WE)
    add_test(NAME ${id} COMMAND ${Python3_EXECUTABLE} "${LXR_ROOT}/tools/emu_golden.py" check ${id}
        --render $<TARGET_FILE:lichomat-render> --out "${CMAKE_BINARY_DIR}/goldens-out")
    set_tests_properties(${id} PROPERTIES TIMEOUT 600)
endforeach()
# M14: determinism, slicing, the broken-variant check (spec 8.4, 12.1)
emu_render_tests(M14_repeat M14_slices M14_broken)
# M14: emu_golden.py's diff and windows on outputs made by the test, and the goldens
# against goldens.json
emu_render_tests(M14_diff M14_windows M14_inventory)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    emu_render_tests(M14_speed)	# spec 9.2: Release runs M14's speed check
endif()

# The temporary folders: every test above gets TMPDIR = a folder of this build,
# which emu_tmp_setup empties first and emu_tmp_clean checks last. A test removes
# what it makes there, also when it fails (emu_testTempDir() at exit, the
# renderer at every exit but codes 3 and 4); one that aborts leaves its folder,
# and emu_tmp_clean names it. render_test.py gives each case a folder of its own
# inside it, so that tests running in parallel (ctest -j) do not see each other's
# card copies. These two ids stay last: the list takes the tests defined so far.
set(EMU_TEST_TMP "${CMAKE_BINARY_DIR}/tmp")
get_property(emu_tests DIRECTORY PROPERTY TESTS)
set_property(TEST ${emu_tests} APPEND PROPERTY ENVIRONMENT "TMPDIR=${EMU_TEST_TMP}")
set_property(TEST ${emu_tests} APPEND PROPERTY FIXTURES_REQUIRED emu_tmp)
add_test(NAME emu_tmp_setup COMMAND ${CMAKE_COMMAND} -DMODE=setup "-DDIR=${EMU_TEST_TMP}"
    -P "${EMU_DIR}/tests/check_tmp.cmake")
add_test(NAME emu_tmp_clean COMMAND ${CMAKE_COMMAND} -DMODE=check "-DDIR=${EMU_TEST_TMP}"
    -P "${EMU_DIR}/tests/check_tmp.cmake")
set_tests_properties(emu_tmp_setup PROPERTIES FIXTURES_SETUP emu_tmp)
set_tests_properties(emu_tmp_clean PROPERTIES FIXTURES_CLEANUP emu_tmp)
