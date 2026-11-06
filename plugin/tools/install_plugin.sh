#!/bin/bash
# Build the LICHOMAT plugin in Release and install it for the current user (plan E9.2 Task 10):
# DEST/LICHOMAT.vst3 and (plan E9.7) AUDEST/LICHOMAT.component, signed ad hoc (the build signs the
# engine and the bundles). DEST is the first argument, else $LICHOMAT_VST3_DIR, else
# ~/Library/Audio/Plug-Ins/VST3, the folder Live scans for VST3 plugins; AUDEST is
# $LICHOMAT_COMPONENT_DIR, else ~/Library/Audio/Plug-Ins/Components, where Logic and every AU host
# find their Audio Units. Both new bundles are checked first in a temporary folder (codesign --verify
# --deep --strict, the VST 3 SDK's validator); then the component replaces an earlier one and
# Apple's auval validates it where the system finds it (auval -v aumu Lcht Lchm), and only once it
# passes does the VST3 bundle replace an earlier one. On a failure the bundle concerned is left as
# it was, and nothing after that step is done. It refuses while a lichomat-engine runs (Live or
# Logic with the plugin in a set). It touches nothing else, except that it gives the plugin a start
# card when it has none: BASE/Card,
# the init fixture (four on the floor on all seven voices, emu/tests/cards/init), which every new
# instance copies (plan P-18). BASE is $LICHOMAT_PLUGIN_HOME, else ~/Library/Application
# Support/LICHOMAT Plugin. The build's output goes to build/plugin-release.install.log.
#
#   plugin/tools/install_plugin.sh [DEST]              install
#   plugin/tools/install_plugin.sh --uninstall [DEST]  remove DEST/LICHOMAT.vst3 and
#                                                      AUDEST/LICHOMAT.component (BASE stays)
#   plugin/tools/install_plugin.sh --help              print this and the destinations; touch nothing
#
# LICHOMAT_INSTALL_VALIDATOR and LICHOMAT_INSTALL_AUVAL name another validator and another auval (a
# test's, to see a failure leave DEST and AUDEST alone).
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
AU_DIR="${LICHOMAT_COMPONENT_DIR:-$HOME/Library/Audio/Plug-Ins/Components}"
AU="$AU_DIR/LICHOMAT.component"
BASE="${LICHOMAT_PLUGIN_HOME:-$HOME/Library/Application Support/LICHOMAT Plugin}"

if [ "$mode" = help ]; then
  sed -n '2,/^# Copyright/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
  echo "install_plugin: the destinations would be $DST and $AU (BASE $BASE)"
  exit 0
fi
if pgrep -x lichomat-engine >/dev/null; then
  echo "install_plugin: a lichomat-engine runs (Live or Logic with LICHOMAT in a set?): quit it, then run this again" >&2
  exit 1
fi
if [ "$mode" = uninstall ]; then
  for b in "$DST" "$AU"; do
    if [ -e "$b" ]; then
      rm -rf -- "$b"
      echo "install_plugin: removed $b"
    else
      echo "install_plugin: no $b"
    fi
  done
  echo "install_plugin: $BASE, the start card and the logs, stays"
  exit 0
fi

B="$ROOT/build/plugin-release"
LOG="$B.install.log"
STAGE="$(mktemp -d)"
NEW="$DST_DIR/.LICHOMAT.vst3.new.$$"	# not *.vst3: no host takes it for a plugin
OLD="$DST_DIR/.LICHOMAT.vst3.old.$$"
AU_NEW="$AU_DIR/.LICHOMAT.component.new.$$"	# not *.component: no AU host takes it
AU_OLD="$AU_DIR/.LICHOMAT.component.old.$$"
cleanup() {
  # stopped between the two renames: the old bundle goes back
  if [ ! -e "$DST" ] && [ -e "$OLD" ]; then
    mv "$OLD" "$DST"
  fi
  if [ ! -e "$AU" ] && [ -e "$AU_OLD" ]; then
    mv "$AU_OLD" "$AU"
  fi
  rm -rf -- "$STAGE" "$NEW" "$OLD" "$AU_NEW" "$AU_OLD"
}
trap cleanup EXIT
LEFT="$DST and $AU are as they were"
fail() {
  echo "install_plugin: FAILED ($1), $LEFT; see $LOG" >&2
  exit 1
}

echo "install_plugin: into $DST and $AU (the build's output in $LOG)"
mkdir -p "$ROOT/build"
: > "$LOG"
source "$ROOT/tools/env.sh" >/dev/null
cmake -S "$ROOT/plugin" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release >> "$LOG" 2>&1 || fail "cmake"
ninja -C "$B" LICHOMAT LICHOMAT_AU validator >> "$LOG" 2>&1 || fail "the build"
# checked in a temporary folder first: an earlier bundle is replaced only by one that passes
ditto "$B/VST3/Release/LICHOMAT.vst3" "$STAGE/LICHOMAT.vst3" >> "$LOG" 2>&1 || fail "the copy"
codesign --verify --deep --strict "$STAGE/LICHOMAT.vst3" >> "$LOG" 2>&1 || fail "codesign --verify"
ditto "$B/AU/LICHOMAT.component" "$STAGE/LICHOMAT.component" >> "$LOG" 2>&1 || fail "the copy of the component"
codesign --verify --deep --strict "$STAGE/LICHOMAT.component" >> "$LOG" 2>&1 || fail "codesign --verify of the component"
VALIDATOR="${LICHOMAT_INSTALL_VALIDATOR:-$B/bin/Release/validator}"
LICHOMAT_PLUGIN_HOME="$STAGE/home" LICHOMAT_EMU_HOME="$STAGE/no-emu" "$VALIDATOR" "$STAGE/LICHOMAT.vst3" \
  > "$STAGE/validator.txt" 2>&1 || { cat "$STAGE/validator.txt" >> "$LOG"; fail "the validator"; }
cat "$STAGE/validator.txt" >> "$LOG"
grep '^Result:' "$STAGE/validator.txt" | tail -n 1
# the component: into AUDEST under a name no host scans, renamed into place, then auval, which finds
# components only where the system registers them, by the codes of the component's Info.plist (type
# aumu, subtype, manufacturer); a component that fails it is taken out again
AUVAL="${LICHOMAT_INSTALL_AUVAL:-auval}"
code() { /usr/libexec/PlistBuddy -c "Print :AudioComponents:0:$1" "$STAGE/LICHOMAT.component/Contents/Info.plist"; }
TYPE="$(code type)" && SUBTYPE="$(code subtype)" && MANUFACTURER="$(code manufacturer)" || fail "the component's codes"
CODES="$TYPE $SUBTYPE $MANUFACTURER"
mkdir -p "$AU_DIR" || fail "mkdir $AU_DIR"
ditto "$STAGE/LICHOMAT.component" "$AU_NEW" >> "$LOG" 2>&1 || fail "the copy into $AU_DIR"
codesign --verify --deep --strict "$AU_NEW" >> "$LOG" 2>&1 || fail "codesign --verify of the component's copy"
if [ -e "$AU" ]; then
  mv "$AU" "$AU_OLD" || fail "moving the old component aside"
fi
if ! mv "$AU_NEW" "$AU"; then
  [ -e "$AU_OLD" ] && mv "$AU_OLD" "$AU"
  fail "moving the new component in"
fi
auval_run() {
  # shellcheck disable=SC2086
  LICHOMAT_PLUGIN_HOME="$STAGE/home" LICHOMAT_EMU_HOME="$STAGE/no-emu" "$AUVAL" -v $CODES > "$STAGE/auval.txt" 2>&1 \
    && grep -q '^AU VALIDATION SUCCEEDED' "$STAGE/auval.txt"
}
if ! auval_run; then
  if grep -q "didn't find the component" "$STAGE/auval.txt"; then
    # the system has not seen the new component yet: its registrar is started again, and asked once more
    echo "install_plugin: auval did not find the component yet; restarting AudioComponentRegistrar" >> "$LOG"
    killall -9 AudioComponentRegistrar >> "$LOG" 2>&1 || true
    sleep 2
  fi
  if ! auval_run; then
    cat "$STAGE/auval.txt" >> "$LOG"
    rm -rf -- "$AU"
    [ -e "$AU_OLD" ] && mv "$AU_OLD" "$AU"
    tail -n 5 "$STAGE/auval.txt" >&2
    fail "auval -v $CODES"
  fi
fi
cat "$STAGE/auval.txt" >> "$LOG"
grep '^AU VALIDATION SUCCEEDED' "$STAGE/auval.txt"
rm -rf -- "$AU_OLD"
echo "install_plugin: $AU, $(codesign -dv "$AU" 2>&1 | grep '^Signature='), auval -v $CODES"
LEFT="$DST is as it was, $AU is the new one"
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
