# The temporary folders of the emulator's tests: emu_testTempDir()'s cards, the
# card copies of lichomat-render, M21_negative's build. ctest gives every test
# TMPDIR=DIR, a folder of the build; this script is the fixture around them.
#
#   cmake -DMODE=setup -DDIR=<dir> -P check_tmp.cmake   empty DIR before the tests
#   cmake -DMODE=check -DDIR=<dir> -P check_tmp.cmake   fail if a test left a folder
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.

if(NOT DIR)
    message(FATAL_ERROR "check_tmp.cmake: DIR is not set")
endif()
if(MODE STREQUAL "setup")
    file(REMOVE_RECURSE "${DIR}")
    file(MAKE_DIRECTORY "${DIR}")
elseif(MODE STREQUAL "check")
    file(GLOB left RELATIVE "${DIR}" "${DIR}/lichomat-*")
    list(LENGTH left count)
    if(count GREATER 0)
        list(SORT left)
        string(REPLACE ";" "\n  " names "${left}")
        message(FATAL_ERROR "the tests left ${count} temporary folders in ${DIR}:\n  ${names}")
    endif()
    message(STATUS "the tests left no temporary folder in ${DIR}")
else()
    message(FATAL_ERROR "check_tmp.cmake: MODE must be setup or check, not '${MODE}'")
endif()
