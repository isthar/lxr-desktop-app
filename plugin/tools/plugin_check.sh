#!/bin/bash
# The LICHOMAT plugin's gate (plan E9.2): its project in Debug with UBSan (build/plugin) and in
# Release (build/plugin-release), every ctest id of each, and the compiler warnings of our own
# files, which must be 0 (the vendored SDK's own files warn; they are not counted).
#
#   plugin/tools/plugin_check.sh
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -u -o pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tools/env.sh" >/dev/null
status=0
for cfg in Debug Release; do
  if [ "$cfg" = Debug ]; then B="$ROOT/build/plugin"; else B="$ROOT/build/plugin-release"; fi
  LOG="$B.log"
  echo "== plugin, $cfg ($B)"
  if cmake -S "$ROOT/plugin" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE="$cfg" > "$LOG" 2>&1 && ninja -C "$B" >> "$LOG" 2>&1; then
    (cd "$B" && ctest -j4 --output-on-failure 2>&1) | tee -a "$LOG" | grep -E '^[0-9]+% tests passed|\*\*\*|^Errors' || status=1
  else
    echo "plugin build FAILED"; grep -E "error" "$LOG" | head -n 20; status=1
  fi
  w=$(grep 'warning:' "$LOG" | grep -v -e '/plugin/third_party/vst3sdk/' -e '/CommandLineTools/' | wc -l | tr -d ' ')
  echo "== compiler warnings, $cfg, our files: $w"
  [ "$w" = 0 ] || status=1
done
exit $status
