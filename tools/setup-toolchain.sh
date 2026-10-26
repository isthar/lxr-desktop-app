#!/bin/bash
# Download the portable build tools into .toolchain/ (git-ignored), without installing anything
# system-wide and without sudo. Re-running is safe: tools that are already present are skipped.
#
#   tools/setup-toolchain.sh
#   source tools/env.sh
#
# Tools: Arm GNU Toolchain 14.3.rel1 (arm-none-eabi), CMake 3.31.12, Ninja 1.13.2 (macOS).
# Every download is checked against its SHA-256 before it is unpacked.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="$ROOT/.toolchain"
DL="$TC/dl"
mkdir -p "$DL" "$TC/bin"

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)
    ARM_PKG=arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi
    ARM_SHA=30f4d08b219190a37cded6aa796f4549504902c53cfc3c7e044a8490b6eba1f7 ;;
  *)
    echo "This script currently supports macOS on Apple Silicon only." >&2
    echo "Elsewhere, install the Arm GNU Toolchain, CMake >= 3.21 and Ninja as described in README.md." >&2
    exit 1 ;;
esac
CMAKE_PKG=cmake-3.31.12-macos-universal
CMAKE_SHA=799af7fd545db9bf1b9cfe72f8095880e727a2d4e0df0e3dffc3bc7b95c2d3b0
NINJA_SHA=c99048673aa765960a99cf10c6ddb9f1fad506099ff0a0e137ad8960a88f321b

fetch() { # url file sha256
  local url="$1" file="$DL/$2" sha="$3"
  if [ ! -f "$file" ] || ! echo "$sha  $file" | shasum -a 256 -c --status; then
    echo "downloading $2"
    curl -fL --retry 3 -o "$file.part" "$url"
    mv "$file.part" "$file"
  fi
  echo "$sha  $file" | shasum -a 256 -c --status || { echo "checksum mismatch: $file" >&2; exit 1; }
}

if [ ! -x "$TC/$ARM_PKG/bin/arm-none-eabi-gcc" ]; then
  fetch "https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/$ARM_PKG.tar.xz" "$ARM_PKG.tar.xz" "$ARM_SHA"
  tar -xJf "$DL/$ARM_PKG.tar.xz" -C "$TC"
fi
if [ ! -x "$TC/$CMAKE_PKG/CMake.app/Contents/bin/cmake" ]; then
  fetch "https://github.com/Kitware/CMake/releases/download/v3.31.12/$CMAKE_PKG.tar.gz" "$CMAKE_PKG.tar.gz" "$CMAKE_SHA"
  tar -xzf "$DL/$CMAKE_PKG.tar.gz" -C "$TC"
fi
if [ ! -x "$TC/bin/ninja" ]; then
  fetch "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-mac.zip" "ninja-mac.zip" "$NINJA_SHA"
  unzip -o -q "$DL/ninja-mac.zip" -d "$TC/bin"
fi

# shellcheck source=env.sh
source "$ROOT/tools/env.sh"
arm-none-eabi-gcc --version | head -n 1
cmake --version | head -n 1
echo "ninja $(ninja --version)"
echo "toolchain ready in $TC; run: source tools/env.sh"
