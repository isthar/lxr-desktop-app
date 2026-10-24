# Source this file to put the repo-local build tools on PATH:
#
#   source tools/env.sh
#
# The tools live in .toolchain/ (git-ignored) and are installed by
# tools/setup-toolchain.sh. Nothing is installed system-wide.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only

_lxr_root="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
_lxr_tc="$_lxr_root/.toolchain"

export LXR_ROOT="$_lxr_root"
export ARM_TOOLCHAIN_DIR="$( (ls -d "$_lxr_tc"/arm-gnu-toolchain-*/bin 2>/dev/null || true) | head -n 1)"
_lxr_cmake_bin="$( (ls -d "$_lxr_tc"/cmake-*/CMake.app/Contents/bin "$_lxr_tc"/cmake-*/bin 2>/dev/null || true) | head -n 1)"
export PATH="$_lxr_cmake_bin:$_lxr_tc/bin:$ARM_TOOLCHAIN_DIR:$PATH"

unset _lxr_root _lxr_tc _lxr_cmake_bin
