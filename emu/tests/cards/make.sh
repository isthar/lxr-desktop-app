#!/bin/bash
# The card fixtures of the goldens (stage 1b spec 5.6), made by the firmware: a
# seeded card, one lichomat-render script per fixture with --keep-card, then only the
# files that the script's #keep lines name. M23 checks that this gives the
# committed fixtures byte for byte.
#
#   emu/tests/cards/make.sh RENDERER OUTDIR [FIXTURE...]
#
# A fixture is emu/tests/cards/scripts/FIXTURE.txt; without FIXTUREs, every
# script there. Its #! lines are the render's options, its #keep lines the globs
# (relative to the card root) of the files to keep; each glob must match a file.
# OUTDIR/FIXTURE is replaced.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$1"; OUT="$(mkdir -p "$2" && cd "$2" && pwd)"; shift 2	# absolute: the copies run in the card
if [ $# -gt 0 ]; then fixtures=("$@"); else
  fixtures=(); for s in "$HERE"/scripts/*.txt; do fixtures+=("$(basename "$s" .txt)"); done; fi
W="$(mktemp -d "${TMPDIR:-/tmp}/lichomat-cards-XXXXXX")"
trap 'rm -rf "$W"' EXIT
for f in "${fixtures[@]}"; do
  s="$HERE/scripts/$f.txt"
  read -r -a opts <<<"$(sed -n 's/^#! //p' "$s" | tr '\n' ' ')"
  keep="$(sed -n 's/^#keep //p' "$s" | tr '\n' ' ')"
  "$R" --seed-card "$W/$f-seed"
  "$R" --card "$W/$f-seed" --events "$s" --keep-card "$W/$f-kept" --out "$W/$f.wav" "${opts[@]}"
  rm -rf "${OUT:?}/$f"; mkdir -p "$OUT/$f"
  ( cd "$W/$f-kept"
    for g in $keep; do
      found=0
      for p in $g; do
        [ -e "$p" ] || continue
        found=1; mkdir -p "$OUT/$f/$(dirname "$p")"; cp "$p" "$OUT/$f/$p"
      done
      [ $found = 1 ] || { echo "make.sh: $f: nothing matches $g" >&2; exit 1; }
    done )
  echo "make.sh: $f: $(cd "$OUT/$f" && find . -type f | wc -l | tr -d ' ') files"
done
