# render_build.h at build time (stage 1b spec 8.4, 10.1): what lichomat-render
# --version and the summary print. The commit is read at every build, so that a
# build after a commit names it; the file is rewritten only when its text
# changes (configure_file()), so that a build without a new commit compiles
# nothing again.
#
#   cmake -DLXR_ROOT=... -DIN=render_build.h.in -DOUT=render_build.h
#         -DEMU_CLANG=... -DEMU_SDK=... -DEMU_FIRMWARE_VERSION=... -P RenderBuild.cmake
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.

execute_process(COMMAND git -C "${LXR_ROOT}" rev-parse --short=12 HEAD
    OUTPUT_VARIABLE EMU_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT EMU_COMMIT)
    set(EMU_COMMIT unknown)
endif()
configure_file("${IN}" "${OUT}" @ONLY)
