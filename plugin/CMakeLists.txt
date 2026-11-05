# The LICHOMAT plugin (plan E9.2): a VST3 instrument that plays the LICHOMAT firmware in an
# engine process per instance. A CMake project of its own; it builds the emulator's device
# objects and lichomat-render through emu/CMakeLists.txt, which stops after them here
# (LICHOMAT_EMU_DEVICE_ONLY), and the VST 3 SDK from plugin/third_party/vst3sdk.
#
#   source tools/env.sh
#   cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/plugin
#   ctest --test-dir build/plugin --output-on-failure
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.

cmake_minimum_required(VERSION 3.25)	# the VST 3 SDK's minimum
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "The plugin is built for arm64 only")
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "macOS 13 or later, as the emulator")
project(lichomat_plugin VERSION 0.1.0 LANGUAGES C CXX OBJCXX)

get_filename_component(LXR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# --- the device objects and lichomat-render (emu/CMakeLists.txt up to its guard) ---------
set(LICHOMAT_EMU_DEVICE_ONLY ON)
add_subdirectory("${LXR_ROOT}/emu" "${CMAKE_BINARY_DIR}/emu")
set(PLUGIN_OVERLAY "${CMAKE_BINARY_DIR}/main_overlay.yaml")	# emu writes it into the top build dir
set(PLUGIN_DEVICE_OBJECTS $<TARGET_OBJECTS:lxr_firmware> $<TARGET_OBJECTS:emu_core>
    $<TARGET_OBJECTS:emu_hostfs> $<TARGET_OBJECTS:emu_libm>)
# Our own C and C++: the licence gate wants the overlay on every compile command (M21), the
# emulator's warnings gate, and the sanitizers of the emulator's build for the engine
set(PLUGIN_SAN_FLAGS "")
if(LICHOMAT_EMU_SANITIZE)
    set(PLUGIN_SAN_FLAGS -fsanitize=${LICHOMAT_EMU_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
endif()
set(PLUGIN_FLAGS -Wall -Wextra -Werror "SHELL:-ivfsoverlay ${PLUGIN_OVERLAY}")

# --- the VST 3 SDK: its libraries, the validator, moduleinfotool ---------------------------
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "No VSTGUI: the prototype has no window" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "The SDK would link each plugin into ~/Library/Audio/Plug-Ins/VST3" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "P10_plugin_validator runs it" FORCE)
add_subdirectory(third_party)

# --- tests ------------------------------------------------------------------------------------
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# plugin_add_test(<name> IDS <id>...): plugin/tests/<name>.c with the shared code; each id is a
# ctest test that runs the executable with the id as its filter (tests/seqc_test.h). A test and
# the shared code it runs have the sanitizers of the emulator's build (UBSan in Debug): it links
# plug_common_san, the engine's copy; plug_common, the plugin's, runs in the host's process
# without them
function(plugin_add_test name)
    cmake_parse_arguments(ARG "" "" "IDS" ${ARGN})
    add_executable(${name} tests/${name}.c $<TARGET_OBJECTS:plug_common_san>)
    target_include_directories(${name} PRIVATE "${PLUGIN_DIR}/common" "${LXR_ROOT}/tests")
    target_compile_options(${name} PRIVATE ${PLUGIN_FLAGS} ${PLUGIN_SAN_FLAGS})
    target_link_options(${name} PRIVATE ${PLUGIN_SAN_FLAGS})
    foreach(id IN LISTS ARG_IDS)
        add_test(NAME ${id} COMMAND ${name} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT 120 LABELS plugin)
    endforeach()
endfunction()
add_test(NAME P2_sdk_pin COMMAND ${Python3_EXECUTABLE} "${PLUGIN_DIR}/tests/sdk_pin.py" "${LXR_ROOT}")
set_tests_properties(P2_sdk_pin PROPERTIES LABELS plugin)

# --- the code the plugin, the engine and the tests share (Tasks 3-6) -----------------------
add_library(plug_common OBJECT common/plug_shm.c)
target_include_directories(plug_common PUBLIC "${PLUGIN_DIR}/common")
target_compile_options(plug_common PRIVATE ${PLUGIN_FLAGS})
# the engine's copy, with the sanitizers of the device objects it links with
add_library(plug_common_san OBJECT common/plug_shm.c)
target_include_directories(plug_common_san PUBLIC "${PLUGIN_DIR}/common")
target_compile_options(plug_common_san PRIVATE ${PLUGIN_FLAGS} ${PLUGIN_SAN_FLAGS})
plugin_add_test(test_shm IDS P3_shm_header P3_shm_create P3_shm_events P3_shm_audio P3_shm_new_engine P3_shm_overwritten P3_shm_overrun
    P3_shm_processes P3_shm_killed)

target_sources(plug_common PRIVATE common/plug_clock.c)
target_sources(plug_common_san PRIVATE common/plug_clock.c)
plugin_add_test(test_clock IDS P4_clock_start_at_zero P4_clock_rounding_44k P4_clock_start_later P4_clock_stop
    P4_clock_loop P4_clock_far_loop P4_clock_wrap_blocks P4_clock_locate P4_clock_jump P4_clock_late_report P4_clock_tempo P4_clock_no_context P4_clock_garbage P4_clock_count_in P4_clock_jitter
    P4_clock_reset P4_clock_spp_bytes P4_clock_max)

target_sources(plug_common PRIVATE common/plug_time.c)
target_sources(plug_common_san PRIVATE common/plug_time.c)
plugin_add_test(test_time IDS P5_time_native P5_time_integer P5_time_covering P5_time_latency P5_time_margin P5_time_wide)

# --- lichomat-engine (Task 6) --------------------------------------------------------------
target_sources(plug_common PRIVATE common/plug_spawn.c)
# the renderer's direct calls: lichomat-render's own object of render_fw.c, with its flags (P-2,
# P6_engine_objects)
add_executable(lichomat-engine engine/eng_main.c engine/eng_run.c engine/eng_resample.c
    $<TARGET_OBJECTS:lichomat-render_fw> $<TARGET_OBJECTS:plug_common_san> ${PLUGIN_DEVICE_OBJECTS})
target_include_directories(lichomat-engine PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/engine"
    "${LXR_ROOT}/emu/core" "${LXR_ROOT}/emu/render" "${LXR_ROOT}/emu/common")
target_compile_options(lichomat-engine PRIVATE ${PLUGIN_FLAGS} ${PLUGIN_SAN_FLAGS} -ffp-contract=off -fno-fast-math)
target_link_options(lichomat-engine PRIVATE ${PLUGIN_SAN_FLAGS} "LINKER:-stack_size,0x800000")
target_link_libraries(lichomat-engine PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
plugin_add_test(test_resample IDS P7_converter_delay P7_converter_open)
target_sources(test_resample PRIVATE engine/eng_resample.c)
target_include_directories(test_resample PRIVATE "${PLUGIN_DIR}/engine")
target_link_libraries(test_resample PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
add_executable(plug_resample_wav tests/plug_resample_wav.c engine/eng_resample.c)
target_include_directories(plug_resample_wav PRIVATE "${PLUGIN_DIR}/engine")
target_compile_options(plug_resample_wav PRIVATE ${PLUGIN_FLAGS})
target_link_libraries(plug_resample_wav PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
add_executable(plug_drive tests/plug_drive.c $<TARGET_OBJECTS:plug_common>)
target_include_directories(plug_drive PRIVATE "${PLUGIN_DIR}/common")
target_compile_options(plug_drive PRIVATE ${PLUGIN_FLAGS})
set(PLUGIN_TEST ${Python3_EXECUTABLE} "${PLUGIN_DIR}/tests/plugin_test.py" --root "${LXR_ROOT}"
    --build "${CMAKE_BINARY_DIR}" --render $<TARGET_FILE:lichomat-render> --engine $<TARGET_FILE:lichomat-engine>
    --drive $<TARGET_FILE:plug_drive> --resample $<TARGET_FILE:plug_resample_wav>
    --work "${CMAKE_BINARY_DIR}/plugin-test")
# plugin_py_tests(<id>...): a case of plugin/tests/plugin_test.py each
function(plugin_py_tests)
    foreach(id IN LISTS ARGN)
        add_test(NAME ${id} COMMAND ${PLUGIN_TEST} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT 600 LABELS plugin)
    endforeach()
endfunction()
plugin_py_tests(P6_engine_version P6_engine_objects P6_engine_native P6_engine_blocks P6_engine_paced P6_engine_seed P6_engine_epoch
    P6_engine_quit P6_engine_orphan P7_engine_resampled P7_engine_realtime)
set_tests_properties(P6_engine_paced P7_engine_realtime PROPERTIES RUN_SERIAL TRUE)	# paced by the wall clock

# --- the card archive (the plugin's state, Task 7) -------------------------------------------
# with its own copy of the card's POSIX half, built without the emulator's sanitizers: the
# plugin runs in the host's process
add_library(plug_card OBJECT common/plug_archive.c "${LXR_ROOT}/emu/core/emu_hostfs.c")
target_include_directories(plug_card PUBLIC "${PLUGIN_DIR}/common" "${LXR_ROOT}/emu/core")
target_compile_options(plug_card PRIVATE ${PLUGIN_FLAGS})
# the test's copy, with the sanitizers of the emulator's build (as plug_common_san)
add_library(plug_card_san OBJECT common/plug_archive.c "${LXR_ROOT}/emu/core/emu_hostfs.c")
target_include_directories(plug_card_san PUBLIC "${PLUGIN_DIR}/common" "${LXR_ROOT}/emu/core")
target_compile_options(plug_card_san PRIVATE ${PLUGIN_FLAGS} ${PLUGIN_SAN_FLAGS})
plugin_add_test(test_archive IDS P8_archive_roundtrip P8_archive_skips P8_archive_refused)
target_sources(test_archive PRIVATE $<TARGET_OBJECTS:plug_card_san>)
target_include_directories(test_archive PRIVATE "${LXR_ROOT}/emu/core")
