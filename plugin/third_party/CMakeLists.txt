# The VST 3 SDK's libraries, the validator and moduleinfotool, built from plugin/third_party/vst3sdk
# (plan E9.2 P-4). A directory of its own: smtg_setup_platform_toolset() adds -ffast-math and
# -ffp-contract=fast with add_compile_options(), which must not reach the firmware's objects.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.

set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk")
set(SDK_ROOT "${SDK_ROOT}" PARENT_SCOPE)
# smtg_add_vst3plugin() in the parent reads these three
set(public_sdk_SOURCE_DIR "${SDK_ROOT}/public.sdk")
set(public_sdk_SOURCE_DIR "${public_sdk_SOURCE_DIR}" PARENT_SCOPE)
set(pluginterfaces_SOURCE_DIR "${SDK_ROOT}/pluginterfaces")
set(pluginterfaces_SOURCE_DIR "${pluginterfaces_SOURCE_DIR}" PARENT_SCOPE)
list(APPEND CMAKE_MODULE_PATH "${SDK_ROOT}/cmake/modules")
# The SDK reads Xcode's version; the Command Line Tools have no xcodebuild (plan F17). Their own
# version stands in, and decides only the SDK's ad hoc signing step of Xcode 15 or later.
execute_process(COMMAND pkgutil --pkg-info=com.apple.pkg.CLTools_Executables OUTPUT_VARIABLE clt ERROR_QUIET)
string(REGEX MATCH "version: ([0-9]+\\.[0-9]+)" clt "${clt}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "the plugin builds with the Command Line Tools; pkgutil names no CLTools_Executables")
endif()
set(ENV{XCODE_VERSION} "${CMAKE_MATCH_1}")
set(XCODE_VERSION "${CMAKE_MATCH_1}")
include(SMTG_VST3_SDK)
smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()
include_directories("${SDK_ROOT}")
smtg_create_lib_base_target()
smtg_create_pluginterfaces_target()
smtg_create_public_sdk_common_target()
smtg_create_public_sdk_target()
smtg_create_public_sdk_hosting_target()
# the SDK's headers are system headers to our targets: our -Wall -Wextra -Werror is not theirs
set_target_properties(base pluginterfaces sdk_common sdk sdk_hosting PROPERTIES SYSTEM TRUE)
add_subdirectory("${SDK_ROOT}/public.sdk/samples/vst-hosting/validator" validator)
add_subdirectory("${SDK_ROOT}/public.sdk/samples/vst-utilities/moduleinfotool" moduleinfotool)
