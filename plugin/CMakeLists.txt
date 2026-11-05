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
# ctest test that runs the executable with the id as its filter (tests/seqc_test.h)
function(plugin_add_test name)
    cmake_parse_arguments(ARG "" "" "IDS" ${ARGN})
    add_executable(${name} tests/${name}.c $<TARGET_OBJECTS:plug_common>)
    target_include_directories(${name} PRIVATE "${PLUGIN_DIR}/common" "${LXR_ROOT}/tests")
    target_compile_options(${name} PRIVATE ${PLUGIN_FLAGS})
    foreach(id IN LISTS ARG_IDS)
        add_test(NAME ${id} COMMAND ${name} ${id})
        set_tests_properties(${id} PROPERTIES TIMEOUT 120 LABELS plugin)
    endforeach()
endfunction()
add_test(NAME P2_sdk_pin COMMAND ${Python3_EXECUTABLE} "${PLUGIN_DIR}/tests/sdk_pin.py" "${LXR_ROOT}")
set_tests_properties(P2_sdk_pin PROPERTIES LABELS plugin)
