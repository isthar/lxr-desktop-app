# Source this file to put the repo-local build tools on PATH (bash or zsh):
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
# find, not shell globs: in zsh a glob that matches nothing is an error, which left
# CMake off PATH. -H follows a .toolchain that is a symlink.
export ARM_TOOLCHAIN_DIR="$(find -H "$_lxr_tc" -maxdepth 2 -type d -path "$_lxr_tc/arm-gnu-toolchain-*/bin" 2>/dev/null | sort | head -n 1)"
_lxr_cmake_bin="$(find -H "$_lxr_tc" -maxdepth 4 -type d \( -path "$_lxr_tc/cmake-*/CMake.app/Contents/bin" -o -path "$_lxr_tc/cmake-*/bin" \) 2>/dev/null | sort | head -n 1)"
# only directories that exist: an empty PATH entry would mean the current directory
for _lxr_dir in "$ARM_TOOLCHAIN_DIR" "$_lxr_tc/bin" "$_lxr_cmake_bin"; do
  if [ -n "$_lxr_dir" ] && [ -d "$_lxr_dir" ]; then PATH="$_lxr_dir:$PATH"; fi
done
export PATH

unset _lxr_root _lxr_tc _lxr_cmake_bin _lxr_dir
