#!/bin/bash
# Demo renders of LICHOMAT v0.5 for listening (plan v0.5 Task 11, P21): sample-accurate triggering.
# The demos of tools/demo_v04.sh twice, in trg smp (v05-smp-<name>) and in trg blk, v0.4's timing
# (v05-blk-<name>), then two groove demos in both settings: G16's script on the onset card
# (v05-groove-onset-smp.wav, -blk.wav) and the init card's pattern 0 at 125 BPM for 8 bars
# (v05-init-125-smp.wav, -blk.wav), out 1 as a stereo WAV, and OUT_DIR/README-v0.5.txt. Only these
# files (and demo_v04.sh's README-v0.4.txt) are written; everything else in OUT_DIR stays.
#
#   tools/demo_v05.sh [OUT_DIR]      default: build/demo (git-ignored); needs build/emu-release or
#                                    build/emu (the Release renderer when both are there)
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
R="$ROOT/build/emu-release/lichomat-render"
[ -x "$R" ] || R="$ROOT/build/emu/lichomat-render"
OUT="${1:-$ROOT/build/demo}"
[ -x "$R" ] || { echo "build the emulator first: cmake -S emu -B build/emu -G Ninja && cmake --build build/emu"; exit 1; }
CARDS="$ROOT/emu/tests/cards"       # the renderer plays a copy of the card; the fixtures stay as they are
mkdir -p "$OUT"
LICHOMAT_DEMO_PREFIX=v05-smp LICHOMAT_DEMO_PARAMS='--param trig_timing=0' "$ROOT/tools/demo_v04.sh" "$OUT"
LICHOMAT_DEMO_PREFIX=v05-blk LICHOMAT_DEMO_PARAMS='--param trig_timing=1' "$ROOT/tools/demo_v04.sh" "$OUT"
for t in smp:0 blk:1; do
  # G16_onsets: Drum 1's click with shifts, ratchets, shuffle, delay, humanize and two rolls (120 BPM)
  "$R" --card "$CARDS/onset" --out "$OUT/v05-groove-onset-${t%%:*}.wav" --stereo out1 --bpm 120 --no-start \
    --seconds 17 --seed 1 --events "$ROOT/emu/tests/goldens/G16_onsets.txt" --param "trig_timing=${t#*:}"
  "$R" --card "$CARDS/init" --out "$OUT/v05-init-125-${t%%:*}.wav" --stereo out1 --bpm 125 --bars 8 \
    --param "trig_timing=${t#*:}"
done
cat > "$OUT/README-v0.5.txt" <<'EOF'
LICHOMAT v0.5: rendery demo z tools/demo_v05.sh (wyjście 1 w stereo, karty emu/tests/cards).
Każdy głos startuje w próbce swojego zdarzenia, stałe 2,9 ms (128 próbek) po nim (trg smp).
trg blk to timing 0.4: głos startuje na początku następnego bloku 64 próbek (1,45 ms).

v05-smp-*.wav i v05-blk-*.wav: dema 0.4 (opisane w README-v0.4.txt, tam pod nazwami v04-*)
w obu ustawieniach trg, obok skrypty zdarzeń (.txt) i logi MIDI (.midi.log).
v05-groove-onset-smp.wav, v05-groove-onset-blk.wav: karta onset, 120 BPM, skrypt G16: klik Drum 1
z przesunięciami -7..+7, ratchetami x2-x8, shuffle, delay i humanize, roll 1/32 i triolowy;
Drum 2 x3/4 na wyjściu 2 (tu niesłyszalny). W smp każdy klik leży na swoim czasie; w blk kliki
skaczą o blok.
v05-init-125-smp.wav, v05-init-125-blk.wav: karta init, pattern 0, 125 BPM, 8 taktów.
EOF
ls -l "$OUT"/v05-*.wav "$OUT/README-v0.5.txt"
