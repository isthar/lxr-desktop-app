#!/bin/bash
# Build the LICHOMAT plugin in Release and install it for the current user (plan E9.2 Task 10):
# DEST/LICHOMAT.vst3, signed ad hoc (the build signs the engine and the bundle), checked with
# codesign and the SDK's validator. DEST is the first argument, else $LICHOMAT_VST3_DIR, else
# ~/Library/Audio/Plug-Ins/VST3, the folder Live scans for VST3 plugins. It replaces an earlier
# LICHOMAT.vst3 there and touches nothing else, except that it gives the plugin a start card when
# it has none: BASE/Card, the init fixture (four on the floor on all seven voices,
# emu/tests/cards/init), which every new instance copies (plan P-18). BASE is
# $LICHOMAT_PLUGIN_HOME, else ~/Library/Application Support/LICHOMAT Plugin.
#
#   plugin/tools/install_plugin.sh [DEST]              install
#   plugin/tools/install_plugin.sh --uninstall [DEST]  remove DEST/LICHOMAT.vst3 (BASE stays)
#   plugin/tools/install_plugin.sh --help              print this and the destination; touch nothing
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mode=install
case "${1:-}" in
  --help|-h) mode=help; shift ;;
  --uninstall) mode=uninstall; shift ;;
esac
DST_DIR="${1:-${LICHOMAT_VST3_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}}"
DST="$DST_DIR/LICHOMAT.vst3"
BASE="${LICHOMAT_PLUGIN_HOME:-$HOME/Library/Application Support/LICHOMAT Plugin}"

if [ "$mode" = help ]; then
  sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
  echo "install_plugin: the destination would be $DST (BASE $BASE)"
  exit 0
fi
if [ "$mode" = uninstall ]; then
  if [ -e "$DST" ]; then
    rm -rf -- "$DST"
    echo "install_plugin: removed $DST ($BASE, the start card and the logs, stays)"
  else
    echo "install_plugin: no $DST"
  fi
  exit 0
fi

echo "install_plugin: into $DST"
source "$ROOT/tools/env.sh" >/dev/null
B="$ROOT/build/plugin-release"
cmake -S "$ROOT/plugin" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release > "$B.install.log" 2>&1
ninja -C "$B" LICHOMAT validator >> "$B.install.log" 2>&1
SRC="$B/VST3/Release/LICHOMAT.vst3"
mkdir -p "$DST_DIR"
if [ -e "$DST" ]; then
  rm -rf -- "$DST"
fi
ditto "$SRC" "$DST"
codesign --verify --deep --strict "$DST"
echo "install_plugin: $DST, $(codesign -dv "$DST" 2>&1 | grep '^Signature=')"
CARD="$BASE/Card"
if [ ! -e "$CARD" ]; then
  mkdir -p "$BASE"
  # in the card's order (birth time, then name), which the firmware reads, as emu_hostCopyCard() and
  # the tests' ordered_copy() keep it: a plain copy gives new birth times in its own order
  python3 - "$ROOT/emu/tests/cards/init" "$CARD" <<'PY'
import os, shutil, sys
def copy(src, dst):
    os.makedirs(dst)
    for e in sorted(os.scandir(src), key=lambda e: (e.stat().st_birthtime, e.name)):
        if not e.name.startswith('.'):
            (copy if e.is_dir() else shutil.copyfile)(e.path, os.path.join(dst, e.name))
copy(sys.argv[1], sys.argv[2])
PY
  echo "install_plugin: the start card $CARD (emu/tests/cards/init)"
fi
HOME_TMP="$(mktemp -d)"
LICHOMAT_PLUGIN_HOME="$HOME_TMP" LICHOMAT_EMU_HOME="$HOME_TMP/no-emu" "$B/bin/Release/validator" "$DST" | grep '^Result:'
rm -rf -- "$HOME_TMP"
