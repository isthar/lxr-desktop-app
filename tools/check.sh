#!/bin/bash
# One-shot verification used during development:
#   1. host tests (tests/, built with the host compiler), if present
#   2. the emulator (emu/, build/emu: Debug with UBSan) with LICHOMAT Emu.app and
#      lichomat-emu-dev, and its ctest ids, on macOS on arm64 only
#   3. firmware release build (build/release), with a memory summary
#   4. update file check (tools/check_image.py: name, header, splash screen)
#   5. compiler warning count (must stay 0; it covers the emulator's build too)
# Exit code is non-zero if any step fails. The full log is in build/check.log.
#
#   tools/check.sh
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -u -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"  # "cmake --build --preset" reads CMakePresets.json from the current directory
# shellcheck source=env.sh
source "$ROOT/tools/env.sh"
mkdir -p "$ROOT/build"
LOG="$ROOT/build/check.log"
: > "$LOG"
status=0

if [ -f "$ROOT/tests/CMakeLists.txt" ]; then
  echo "== host tests"
  if cmake -S "$ROOT/tests" -B "$ROOT/build/host-tests" -G Ninja -DCMAKE_BUILD_TYPE=Debug >>"$LOG" 2>&1 \
     && cmake --build "$ROOT/build/host-tests" >>"$LOG" 2>&1; then
    (cd "$ROOT/build/host-tests" && ctest --output-on-failure 2>&1) | tee -a "$LOG" | tail -n 25 || status=1
  else
    echo "host test build FAILED"; grep -E "error|warning" "$LOG" | head -n 30; status=1
  fi
else
  echo "== host tests: tests/CMakeLists.txt not present"
fi

if [ "$(uname -s)-$(uname -m)" = "Darwin-arm64" ] && [ -f "$ROOT/emu/CMakeLists.txt" ]; then
  echo "== emulator (emu/, Debug with UBSan)"
  if cmake -S "$ROOT/emu" -B "$ROOT/build/emu" -G Ninja >>"$LOG" 2>&1 \
     && cmake --build "$ROOT/build/emu" >>"$LOG" 2>&1; then
    (cd "$ROOT/build/emu" && ctest --output-on-failure 2>&1) | tee -a "$LOG" | tail -n 25 || status=1
    [ -d "$ROOT/build/emu/LICHOMAT Emu.app" ] && echo "== app: build/emu/LICHOMAT Emu.app"
  else
    echo "emulator build FAILED"; grep -E "error|warning" "$LOG" | head -n 30; status=1
  fi
else
  echo "== emulator: skipped (macOS on arm64 only)"
fi

echo "== firmware build (release)"
if cmake --preset release -DARM_TOOLCHAIN_DIR="$ARM_TOOLCHAIN_DIR" -S "$ROOT" >>"$LOG" 2>&1 \
   && cmake --build --preset release >>"$LOG" 2>&1; then
  B="$ROOT/build/release"
  arm-none-eabi-size -A "$B/LXR_V2_STM32F765.elf" | awk -v bin="$(stat -f %z "$B/LXR_V2_STM32F765.bin")" '
    $1==".itcm_text"{itcm=$2} $1==".dtcm"||$1==".dtcmz"{dtcm+=$2} $1==".data"||$1==".bss"||$1=="._user_heap"{ram+=$2}
    END{printf "memory: ITCM %d/16384 B (%.1f%%, free %d)  DTCM %d/131072 B (%.1f%%, rest is stack)  RAM %d/393216 B (%.1f%%, free %d)  image %d/1015808 B (%.1f%%)\n",
        itcm,itcm/163.84,16384-itcm, dtcm,dtcm/1310.72, ram,ram/3932.16,393216-ram, bin,bin/10158.08}'
  # LICHOMAT v0.2 memory budget (HANDOFF.md): DTCM + RAM at most 391796 + 4096 B and DTCM
  # at most 107388 + 512 B (DTCM also holds the stack); the v0.2 arrays belong in RAM
  arm-none-eabi-size -A "$B/LXR_V2_STM32F765.elf" | awk -v maxsum=395892 -v maxdtcm=107900 '
    $1==".dtcm"||$1==".dtcmz"{dtcm+=$2} $1==".data"||$1==".bss"||$1=="._user_heap"{ram+=$2}
    END{ok = (dtcm+ram <= maxsum && dtcm <= maxdtcm)
        printf "budget: DTCM+RAM %d/%d B, DTCM %d/%d B %s\n", dtcm+ram, maxsum, dtcm, maxdtcm, ok ? "OK" : "OVER"
        exit !ok}' | tee -a "$LOG" || status=1
  python3 "$ROOT/tools/check_image.py" "$B" 2>&1 | tee -a "$LOG" || status=1
else
  echo "firmware build FAILED"; grep -E "error" "$LOG" | head -n 30; status=1
fi

w=$(grep -c "warning:" "$LOG" || true)
echo "== compiler warnings: $w"
[ "$w" -eq 0 ] || { grep "warning:" "$LOG" | sort -u | head -n 20; status=1; }
exit $status
