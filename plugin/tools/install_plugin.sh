#!/bin/bash
# Build the LICHOMAT plugin in Release and install it for the current user (plan E9.2 Task 10):
# DEST/LICHOMAT.vst3, signed ad hoc (the build signs the engine and the bundle). DEST is the first
# argument, else $LICHOMAT_VST3_DIR, else ~/Library/Audio/Plug-Ins/VST3, the folder Live scans for
# VST3 plugins. The new bundle is checked first in a temporary folder (codesign --verify --deep
# --strict, the SDK's validator) and only then put in place of an earlier one; on any failure DEST
# is left as it was. It refuses while a lichomat-engine runs (Live with the plugin in a set). It
# touches nothing else, except that it gives the plugin a start card when it has none: BASE/Card,
# the init fixture (four on the floor on all seven voices, emu/tests/cards/init), which every new
# instance copies (plan P-18). BASE is $LICHOMAT_PLUGIN_HOME, else ~/Library/Application
# Support/LICHOMAT Plugin. The build's output goes to build/plugin-release.install.log.
#
#   plugin/tools/install_plugin.sh [DEST]              install
#   plugin/tools/install_plugin.sh --uninstall [DEST]  remove DEST/LICHOMAT.vst3 (BASE stays)
#   plugin/tools/install_plugin.sh --help              print this and the destination; touch nothing
#
# LICHOMAT_INSTALL_VALIDATOR names another validator (a test's, to see a failure leave DEST alone).
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
  -*) echo "install_plugin: unknown option $1 (--help, --uninstall)" >&2; exit 2 ;;
esac
if [ $# -gt 1 ] || [[ "${1:-}" == -* ]]; then
  echo "install_plugin: one destination folder at most, and no option after it (--help)" >&2
  exit 2
fi
DST_DIR="${1:-${LICHOMAT_VST3_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}}"
DST="$DST_DIR/LICHOMAT.vst3"
BASE="${LICHOMAT_PLUGIN_HOME:-$HOME/Library/Application Support/LICHOMAT Plugin}"

if [ "$mode" = help ]; then
  sed -n '2,/^# Copyright/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
  echo "install_plugin: the destination would be $DST (BASE $BASE)"
  exit 0
fi
if pgrep -x lichomat-engine >/dev/null; then
  echo "install_plugin: a lichomat-engine runs (Live with LICHOMAT in a set?): quit Live, then run this again" >&2
  exit 1
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

B="$ROOT/build/plugin-release"
LOG="$B.install.log"
STAGE="$(mktemp -d)"
NEW="$DST_DIR/.LICHOMAT.vst3.new.$$"	# not *.vst3: no host takes it for a plugin
OLD="$DST_DIR/.LICHOMAT.vst3.old.$$"
cleanup() {
  # stopped between the two renames: the old bundle goes back
  if [ ! -e "$DST" ] && [ -e "$OLD" ]; then
    mv "$OLD" "$DST"
  fi
  rm -rf -- "$STAGE" "$NEW" "$OLD"
}
trap cleanup EXIT
fail() {
  echo "install_plugin: FAILED ($1), $DST is as it was; see $LOG" >&2
  exit 1
}

echo "install_plugin: into $DST (the build's output in $LOG)"
mkdir -p "$ROOT/build"
: > "$LOG"
source "$ROOT/tools/env.sh" >/dev/null
cmake -S "$ROOT/plugin" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release >> "$LOG" 2>&1 || fail "cmake"
ninja -C "$B" LICHOMAT validator >> "$LOG" 2>&1 || fail "the build"
# checked in a temporary folder first: an earlier bundle is replaced only by one that passes
ditto "$B/VST3/Release/LICHOMAT.vst3" "$STAGE/LICHOMAT.vst3" >> "$LOG" 2>&1 || fail "the copy"
codesign --verify --deep --strict "$STAGE/LICHOMAT.vst3" >> "$LOG" 2>&1 || fail "codesign --verify"
VALIDATOR="${LICHOMAT_INSTALL_VALIDATOR:-$B/bin/Release/validator}"
LICHOMAT_PLUGIN_HOME="$STAGE/home" LICHOMAT_EMU_HOME="$STAGE/no-emu" "$VALIDATOR" "$STAGE/LICHOMAT.vst3" \
  > "$STAGE/validator.txt" 2>&1 || { cat "$STAGE/validator.txt" >> "$LOG"; fail "the validator"; }
cat "$STAGE/validator.txt" >> "$LOG"
grep '^Result:' "$STAGE/validator.txt" | tail -n 1
# into DEST's folder under a name no host scans, then renamed into place: the old bundle goes only
# once the new one is there
mkdir -p "$DST_DIR" || fail "mkdir $DST_DIR"
ditto "$STAGE/LICHOMAT.vst3" "$NEW" >> "$LOG" 2>&1 || fail "the copy into $DST_DIR"
codesign --verify --deep --strict "$NEW" >> "$LOG" 2>&1 || fail "codesign --verify of the copy"
if [ -e "$DST" ]; then
  mv "$DST" "$OLD" || fail "moving the old bundle aside"
fi
if ! mv "$NEW" "$DST"; then
  [ -e "$OLD" ] && mv "$OLD" "$DST"
  fail "moving the new bundle in"
fi
rm -rf -- "$OLD"
echo "install_plugin: $DST, $(codesign -dv "$DST" 2>&1 | grep '^Signature=')"
CARD="$BASE/Card"
if [ ! -e "$CARD" ]; then
  mkdir -p "$BASE"
  rm -rf -- "$BASE/.Card.new"
  # in the card's order (birth time, then name), which the firmware reads, as emu_hostCopyCard() and
  # the tests' ordered_copy() keep it: a plain copy gives new birth times in its own order
  python3 - "$ROOT/emu/tests/cards/init" "$BASE/.Card.new" <<'PY' || { echo "install_plugin: no start card made" >&2; exit 1; }
import os, shutil, sys
def copy(src, dst):
    os.makedirs(dst)
    for e in sorted(os.scandir(src), key=lambda e: (e.stat().st_birthtime, e.name)):
        if not e.name.startswith('.'):
            (copy if e.is_dir() else shutil.copyfile)(e.path, os.path.join(dst, e.name))
copy(sys.argv[1], sys.argv[2])
PY
  mv "$BASE/.Card.new" "$CARD"
  echo "install_plugin: the start card $CARD (emu/tests/cards/init)"
fi
