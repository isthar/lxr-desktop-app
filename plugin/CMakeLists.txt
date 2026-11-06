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
project(lichomat_plugin VERSION 0.2.0 LANGUAGES C CXX OBJCXX)

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
add_test(NAME P2_ausdk_pin COMMAND ${Python3_EXECUTABLE} "${PLUGIN_DIR}/tests/sdk_pin.py" "${LXR_ROOT}" AudioUnitSDK)
set_tests_properties(P2_ausdk_pin PROPERTIES LABELS plugin)

# --- the code the plugin, the engine and the tests share (Tasks 3-6) -----------------------
add_library(plug_common OBJECT common/plug_shm.c)
target_include_directories(plug_common PUBLIC "${PLUGIN_DIR}/common")
target_compile_options(plug_common PRIVATE ${PLUGIN_FLAGS})
# the engine's copy, with the sanitizers of the device objects it links with
add_library(plug_common_san OBJECT common/plug_shm.c)
target_include_directories(plug_common_san PUBLIC "${PLUGIN_DIR}/common")
target_compile_options(plug_common_san PRIVATE ${PLUGIN_FLAGS} ${PLUGIN_SAN_FLAGS})
plugin_add_test(test_shm IDS P3_shm_header P3_shm_create P3_shm_events P3_shm_audio P3_shm_new_engine P3_shm_overwritten P3_shm_overrun
    P3_shm_processes P3_shm_killed P3_shm_split P3_shm_values P3_shm_seqlock)

target_sources(plug_common PRIVATE common/plug_clock.c)
target_sources(plug_common_san PRIVATE common/plug_clock.c)
plugin_add_test(test_clock IDS P4_clock_start_at_zero P4_clock_rounding_44k P4_clock_start_later P4_clock_stop
    P4_clock_loop P4_clock_far_loop P4_clock_wrap_blocks P4_clock_locate P4_clock_jump P4_clock_late_report P4_clock_tempo P4_clock_no_context P4_clock_garbage P4_clock_count_in P4_clock_jitter
    P4_clock_reset P4_clock_spp_bytes P4_clock_max)

target_sources(plug_common PRIVATE common/plug_time.c)
target_sources(plug_common_san PRIVATE common/plug_time.c)
plugin_add_test(test_time IDS P5_time_native P5_time_integer P5_time_covering P5_time_latency P5_time_margin P5_time_wide)

# --- the parameters (plan E9.4 Task 5) --------------------------------------------------------
target_sources(plug_common PRIVATE common/plug_params.c)
target_sources(plug_common_san PRIVATE common/plug_params.c)
plugin_add_test(test_params IDS P12_params_table P12_params_midi P12_params_host P12_params_first_report P12_params_own_change
    P12_params_restore P12_params_state P12_params_new_engine P12_params_unknown
    P12_params_lchv P12_params_pending)

# --- the AU's view of the parameters and of Out 2 (plan E9.7 Task 3) ---------------------------
target_sources(plug_common PRIVATE common/plug_au.c)
target_sources(plug_common_san PRIVATE common/plug_au.c)
plugin_add_test(test_au IDS P13_unit_values P13_unit_out2)

# --- lichomat-engine (Task 6) --------------------------------------------------------------
target_sources(plug_common PRIVATE common/plug_spawn.c)
# the renderer's direct calls: lichomat-render's own object of render_fw.c, with its flags (P-2,
# P6_engine_objects)
# the engine's reads of the firmware's memory (plan E9.4): the one file of the engine that includes
# firmware headers, with the flags every such object has (emu_fw_headers), as render_fw.c
add_library(eng_fw OBJECT engine/eng_fw.c)
target_link_libraries(eng_fw PRIVATE emu_fw_headers)
target_include_directories(eng_fw PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/engine")
target_compile_options(eng_fw PRIVATE -Wall -Wextra -Werror)
# the check of plug_params[] against the firmware's parameters, on tables with a row changed: eng_fw.c with
# stand-ins for the four globals it reads (P12_engine_table)
add_executable(test_engfw tests/test_engfw.c $<TARGET_OBJECTS:eng_fw> $<TARGET_OBJECTS:plug_common_san>)
target_include_directories(test_engfw PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/engine" "${LXR_ROOT}/tests")
target_compile_options(test_engfw PRIVATE ${PLUGIN_FLAGS} ${PLUGIN_SAN_FLAGS})
target_link_options(test_engfw PRIVATE ${PLUGIN_SAN_FLAGS})
add_test(NAME P12_engine_table COMMAND test_engfw P12_engine_table)
set_tests_properties(P12_engine_table PROPERTIES TIMEOUT 120 LABELS plugin)
add_executable(lichomat-engine engine/eng_main.c engine/eng_run.c engine/eng_resample.c $<TARGET_OBJECTS:eng_fw>
    $<TARGET_OBJECTS:lichomat-render_fw> $<TARGET_OBJECTS:plug_common_san> ${PLUGIN_DEVICE_OBJECTS})
target_include_directories(lichomat-engine PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/engine"
    "${LXR_ROOT}/emu/core" "${LXR_ROOT}/emu/render" "${LXR_ROOT}/emu/common" "${CMAKE_BINARY_DIR}")
# --version names the commit, from the render_build.h that emu_render_build rewrites when it changes
add_dependencies(lichomat-engine emu_render_build)
target_compile_options(lichomat-engine PRIVATE ${PLUGIN_FLAGS} ${PLUGIN_SAN_FLAGS} -ffp-contract=off -fno-fast-math)
target_link_options(lichomat-engine PRIVATE ${PLUGIN_SAN_FLAGS} "LINKER:-stack_size,0x800000")
target_link_libraries(lichomat-engine PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
plugin_add_test(test_resample IDS P7_converter_delay P7_converter_open P7_converter_channels)
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
    P6_engine_quit P6_engine_orphan P7_engine_resampled P7_engine_realtime P6_engine_split P7_engine_split P6_engine_params)
set_tests_properties(P6_engine_paced P7_engine_realtime PROPERTIES RUN_SERIAL TRUE)	# paced by the wall clock
plugin_py_tests(P6_engine_refuses)
plugin_py_tests(P6_engine_late_epoch P6_engine_channel)

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

# --- the bundle LICHOMAT.vst3 (Task 8) ---------------------------------------------------------
smtg_add_vst3plugin(LICHOMAT vst3/plugin_factory.cpp vst3/plugin_processor.cpp vst3/plugin_controller.cpp
    vst3/plugin_link.cpp vst3/plugin_card.cpp)
target_sources(LICHOMAT PRIVATE $<TARGET_OBJECTS:plug_common> $<TARGET_OBJECTS:plug_card>)
target_link_libraries(LICHOMAT PRIVATE sdk)
target_include_directories(LICHOMAT PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/vst3" "${LXR_ROOT}/emu/core")
target_compile_options(LICHOMAT PRIVATE ${PLUGIN_FLAGS})
target_compile_features(LICHOMAT PRIVATE cxx_std_17)
configure_file(vst3/Info.plist.in "${CMAKE_BINARY_DIR}/LICHOMAT-Info.plist" @ONLY)
set_target_properties(LICHOMAT PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/LICHOMAT-Info.plist")
# Before the link, the engine into Contents/Helpers and the licences into Contents/Resources: ours, the
# VST 3 SDK's (MIT asks for its notice with every copy) and those of the code the engine links from the
# emulator, Arm Optimized Routines (emu_libm) and FatFs (ff.h), as the app's bundle has them
# (emu/CMakeLists.txt); after the SDK's steps (moduleinfo.json), the signature of the helper and then of
# the whole bundle, ad hoc. The bundle is linked again whenever the engine or one of these files changes
# (LINK_DEPENDS), so that these steps copy and sign the current ones (P10_plugin_helper, P11_licence)
set(PLUGIN_NOTICES "${LXR_ROOT}/LICENSE" "${LXR_ROOT}/NOTICE.md" "${PLUGIN_DIR}/third_party/vst3sdk/LICENSE.txt"
    "${LXR_ROOT}/emu/libm/LICENSE" "${LXR_ROOT}/Middlewares/Third_Party/FatFs/src/ff.h")
add_dependencies(LICHOMAT lichomat-engine)
set_property(TARGET LICHOMAT APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:lichomat-engine>" ${PLUGIN_NOTICES})
add_custom_command(TARGET LICHOMAT PRE_LINK
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Helpers"
        "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:lichomat-engine>" "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Helpers/lichomat-engine"
    COMMAND ${CMAKE_COMMAND} -E copy "${LXR_ROOT}/LICENSE" "${LXR_ROOT}/NOTICE.md" "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Resources/"
    COMMAND ${CMAKE_COMMAND} -E copy "${PLUGIN_DIR}/third_party/vst3sdk/LICENSE.txt"
        "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Resources/VST3-SDK-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy "${LXR_ROOT}/emu/libm/LICENSE"
        "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Resources/LICENSE.arm-optimized-routines"
    COMMAND /bin/sh -c "head -n 18 '${LXR_ROOT}/Middlewares/Third_Party/FatFs/src/ff.h' > '$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Resources/LICENSE.fatfs'"
    VERBATIM)
add_custom_command(TARGET LICHOMAT POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT>/Helpers/lichomat-engine"
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:LICHOMAT>"
    VERBATIM)
set(PLUGIN_BUNDLE_TEST ${PLUGIN_TEST} --bundle $<TARGET_BUNDLE_DIR:LICHOMAT> --validator $<TARGET_FILE:validator>)
add_test(NAME P10_plugin_validator COMMAND ${PLUGIN_BUNDLE_TEST} P10_plugin_validator)
set_tests_properties(P10_plugin_validator PROPERTIES TIMEOUT 600 LABELS plugin)
add_test(NAME P10_plugin_helper COMMAND ${PLUGIN_BUNDLE_TEST} P10_plugin_helper)
set_tests_properties(P10_plugin_helper PROPERTIES TIMEOUT 600 LABELS plugin)

# --- the AU, LICHOMAT.component (plan E9.7 Task 4) ------------------------------------------------
# An AU v2 instrument of Apple's AudioUnitSDK (au/au_unit.cpp) around the VST3's own processor, the
# engine link and the card folder, compiled into it again. Its codes (A-12): type aumu, subtype Lcht,
# manufacturer Lchm; its version the project's as an AU's integer (0.2.0 is 512). The component
# exports its factory alone. C++23: the AudioUnitSDK's headers are written in it
set(PLUG_AU_SUBTYPE Lcht)
set(PLUG_AU_MANUFACTURER Lchm)
math(EXPR PLUG_AU_VERSION "${PROJECT_VERSION_MAJOR} * 65536 + ${PROJECT_VERSION_MINOR} * 256 + ${PROJECT_VERSION_PATCH}")
configure_file(au/Info.plist.in "${CMAKE_BINARY_DIR}/LICHOMAT-AU-Info.plist" @ONLY)
add_library(LICHOMAT_AU MODULE au/au_unit.cpp vst3/plugin_processor.cpp vst3/plugin_link.cpp vst3/plugin_card.cpp
    "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp" $<TARGET_OBJECTS:plug_common> $<TARGET_OBJECTS:plug_card>)
set_target_properties(LICHOMAT_AU PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION component OUTPUT_NAME LICHOMAT
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/LICHOMAT-AU-Info.plist" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/AU"
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(LICHOMAT_AU PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/vst3" "${PLUGIN_DIR}/au"
    "${LXR_ROOT}/emu/core")
target_compile_options(LICHOMAT_AU PRIVATE ${PLUGIN_FLAGS})
target_link_libraries(LICHOMAT_AU PRIVATE ausdk sdk)
target_link_options(LICHOMAT_AU PRIVATE "LINKER:-exported_symbol,_LichomatAUFactory")
# as the VST3's bundle: the engine into Contents/Helpers and the notices into Contents/Resources, the
# AudioUnitSDK's Apache-2.0 licence among them, before the link; the helper and the bundle signed ad hoc
# after it; linked again whenever the engine or a notice changes (P13_au_helper, P11_licence)
add_dependencies(LICHOMAT_AU lichomat-engine)
set_property(TARGET LICHOMAT_AU APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:lichomat-engine>" ${PLUGIN_NOTICES}
    "${AUSDK_ROOT}/LICENSE.txt")
add_custom_command(TARGET LICHOMAT_AU PRE_LINK
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT_AU>/Helpers"
        "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT_AU>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:lichomat-engine>" "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT_AU>/Helpers/lichomat-engine"
    COMMAND ${CMAKE_COMMAND} -E copy "${LXR_ROOT}/LICENSE" "${LXR_ROOT}/NOTICE.md" "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT_AU>/Resources/"
    COMMAND ${CMAKE_COMMAND} -E copy "${PLUGIN_DIR}/third_party/vst3sdk/LICENSE.txt"
        "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT_AU>/Resources/VST3-SDK-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy "${AUSDK_ROOT}/LICENSE.txt"
        "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT_AU>/Resources/AudioUnitSDK-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy "${LXR_ROOT}/emu/libm/LICENSE"
        "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT_AU>/Resources/LICENSE.arm-optimized-routines"
    COMMAND /bin/sh -c "head -n 18 '${LXR_ROOT}/Middlewares/Third_Party/FatFs/src/ff.h' > '$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT_AU>/Resources/LICENSE.fatfs'"
    VERBATIM)
add_custom_command(TARGET LICHOMAT_AU POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_CONTENT_DIR:LICHOMAT_AU>/Helpers/lichomat-engine"
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:LICHOMAT_AU>"
    VERBATIM)
set(PLUGIN_COMPONENT_TEST ${PLUGIN_TEST} --component $<TARGET_BUNDLE_DIR:LICHOMAT_AU>)
add_test(NAME P13_au_helper COMMAND ${PLUGIN_COMPONENT_TEST} P13_au_helper)
set_tests_properties(P13_au_helper PROPERTIES TIMEOUT 600 LABELS plugin)

# --- lichomat-plughost, the test host, and the real-time check (Task 9) ---------------------
add_executable(lichomat-plughost host/plug_host.cpp "${SDK_ROOT}/public.sdk/source/vst/hosting/module_mac.mm"
    "${SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp" "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp"
    $<TARGET_OBJECTS:plug_common> $<TARGET_OBJECTS:plug_card>)
set_source_files_properties("${SDK_ROOT}/public.sdk/source/vst/hosting/module_mac.mm" PROPERTIES COMPILE_FLAGS "-fobjc-arc")
target_include_directories(lichomat-plughost PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/vst3" "${LXR_ROOT}/emu/core")
target_compile_options(lichomat-plughost PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${PLUGIN_FLAGS}> "SHELL:-ivfsoverlay ${PLUGIN_OVERLAY}")
target_compile_features(lichomat-plughost PRIVATE cxx_std_17)
target_link_libraries(lichomat-plughost PRIVATE sdk_hosting "-framework Cocoa")
add_library(plug_rtcheck SHARED tests/plug_rtcheck.c)
target_compile_options(plug_rtcheck PRIVATE ${PLUGIN_FLAGS})
set(PLUGIN_HOST_TEST ${PLUGIN_BUNDLE_TEST} --host $<TARGET_FILE:lichomat-plughost> --rtcheck $<TARGET_FILE:plug_rtcheck>)
foreach(id P10_plugin_native P10_plugin_onsets P10_plugin_latency P10_plugin_resampled P10_plugin_state P10_plugin_app_card
        P10_plugin_renew P10_plugin_kill P10_plugin_rt P10_plugin_logs P10_plugin_beats P10_plugin_setstate
        P10_plugin_hang P10_plugin_crashes P10_plugin_stale P10_plugin_policy P10_plugin_spawn_fail P10_plugin_nonblock
        P10_plugin_offline_hang P10_plugin_buses P10_plugin_buses48 P10_plugin_params P10_plugin_outputs
        P10_plugin_params_restart P10_plugin_rtparams P10_plugin_params_state P10_plugin_null_out2 P10_plugin_async_state P10_plugin_double_state P10_plugin_early_state P10_plugin_flush_order P10_plugin_silence P10_plugin_frame_order P10_plugin_controller P10_plugin_offsets
        P10_plugin_restart_hold)
    add_test(NAME ${id} COMMAND ${PLUGIN_HOST_TEST} ${id})
    set_tests_properties(${id} PROPERTIES TIMEOUT 600 LABELS plugin)
endforeach()
set_tests_properties(P10_plugin_kill P10_plugin_rt P10_plugin_beats P10_plugin_hang P10_plugin_crashes P10_plugin_policy
    P10_plugin_spawn_fail P10_plugin_offline_hang P10_plugin_params_restart P10_plugin_rtparams P10_plugin_async_state
    P10_plugin_double_state PROPERTIES RUN_SERIAL TRUE)	# paced by the wall clock

# --- lichomat-auhost, the AU's test host, and the scenarios through the AU (plan E9.7 Task 5) ------
add_executable(lichomat-auhost host/au_host.cpp "${SDK_ROOT}/public.sdk/source/vst/hosting/parameterchanges.cpp"
    $<TARGET_OBJECTS:plug_common> $<TARGET_OBJECTS:plug_card>)
target_include_directories(lichomat-auhost PRIVATE "${PLUGIN_DIR}/common" "${PLUGIN_DIR}/vst3" "${LXR_ROOT}/emu/core")
target_compile_options(lichomat-auhost PRIVATE ${PLUGIN_FLAGS})
target_compile_features(lichomat-auhost PRIVATE cxx_std_17)
target_link_libraries(lichomat-auhost PRIVATE sdk_hosting "-framework AudioToolbox" "-framework CoreFoundation")
set(PLUGIN_AU_TEST ${PLUGIN_HOST_TEST} --component $<TARGET_BUNDLE_DIR:LICHOMAT_AU> --auhost $<TARGET_FILE:lichomat-auhost>)
foreach(id P13_au_info P13_au_checks P13_au_native P13_au_latency P13_au_onsets P13_au_buses P13_au_buses48
        P13_au_out2_pull P13_au_one_output P13_au_params P13_au_outputs P13_au_state P13_au_params_state
        P13_au_vst3_state P13_au_setstate P13_au_renew P13_au_ramp P13_au_offline_switch P13_au_kill
        P13_au_params_restart P13_au_notify P13_au_rt P13_au_rtparams P13_au_noalloc P13_au_set_state)
    add_test(NAME ${id} COMMAND ${PLUGIN_AU_TEST} ${id})
    set_tests_properties(${id} PROPERTIES TIMEOUT 600 LABELS plugin)
endforeach()
set_tests_properties(P13_au_kill P13_au_params_restart P13_au_notify P13_au_rt P13_au_rtparams PROPERTIES RUN_SERIAL TRUE)	# paced

# --- the licence gate and the name scan (Task 10) ---------------------------------------------
add_test(NAME P11_licence COMMAND ${PLUGIN_HOST_TEST} P11_licence)
set_tests_properties(P11_licence PROPERTIES TIMEOUT 600 LABELS plugin)
