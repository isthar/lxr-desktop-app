#!/bin/bash
# Demo renders of LICHOMAT v0.2 for listening (docs/lichomat/release-notes-v0.2.md): 18 s each
# (about 15 s of playing after the panel setup) at 120 BPM from a freshly seeded card, out 1 as a
# stereo WAV, with the events script and the MIDI log beside it: OUT_DIR/v02-<name>.wav, .txt
# and .midi.log. Only these v02-* files are written; everything else in OUT_DIR stays.
#
#   tools/demo_v02.sh [OUT_DIR]      default: build/demo (git-ignored); needs build/emu
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
R="$ROOT/build/emu/lichomat-render"
OUT="${1:-$ROOT/build/demo}"
[ -x "$R" ] || { echo "build the emulator first: cmake -S emu -B build/emu -G Ninja && cmake --build build/emu"; exit 1; }
mkdir -p "$OUT"
CARD="$(mktemp -d)"
trap 'rm -rf "$CARD"' EXIT
"$R" --seed-card "$CARD/card" >/dev/null
# voice N sends on MIDI channel N, so that the log tells the voices apart (the audio is the same)
CHANNELS="--param midi_chan_2=2 --param midi_chan_3=3 --param midi_chan_4=4 --param midi_chan_5=5 --param midi_chan_6=6 --param midi_chan_7=7"
steps() { local v=$1; shift; printf '+100ms press voice%s\n' "$v"; for s in "$@"; do printf '+100ms press step%s\n' "$s"; done; }
render() {   # name, then the script on stdin
  cat > "$OUT/v02-$1.txt"
  "$R" --card "$CARD/card" --out "$OUT/v02-$1.wav" --stereo out1 --no-start --seconds 18 \
    --midi-log "$OUT/v02-$1.midi.log" --events "$OUT/v02-$1.txt" $CHANNELS
}
{ steps 1 1 5 9 13; steps 2 1 5 9 13; echo '+100ms param track_tempo=4'; echo '+100ms press start'; } | render 1-3-against-4
{ steps 1 1 5 9 13; steps 4 1 3; echo '+100ms param track_length=5'; echo '+100ms param track_tempo=4'; echo '+100ms press start'; } | render 2-5-steps-at-x3_4
{ steps 1 1 5 9 13; steps 6 $(seq 1 16); echo '+100ms param track_tempo=8'; echo '+100ms press start'; } | render 3-hats-x2
{ steps 1 1 5 9 13; steps 6 $(seq 1 16); echo '+100ms press start'; } | render 4-hats-straight
{ steps 1 1 5 9 13; steps 6 $(seq 1 16); echo '+100ms param human_time=6'; echo '+100ms param human_vol=20'; echo '+100ms press start'; } | render 5-hats-humanized
ls -l "$OUT"/v02-*.wav
