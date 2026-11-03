#!/bin/bash
# Demo renders of LICHOMAT v0.6 for listening (plan v0.6 Task 9): the groove templates and a recorded
# take with microtiming. The init card at 142 BPM with the closed hat (voice 6) on all sixteen 16ths
# (the card has steps 1, 5, 9 and 13; the script adds the twelve others), once per template at 100 %
# (v06-hats-<name>.wav, off swg lay lyb rlx 2bt psh drg) and lay at 50 and 150 % (v06-hats-lay50.wav,
# v06-hats-lay150.wav), 8 bars after the start; G22_record's take on the groove card
# (v06-record-take.wav: bar 1 played in, bars 2 and 3 played back); out 1 as a stereo WAV, and
# OUT_DIR/README-v0.6.txt. Only these files are written; everything else in OUT_DIR stays.
#
#   tools/demo_v06.sh [OUT_DIR]      default: build/demo (git-ignored); needs build/emu-release or
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
W="$(mktemp -d "${TMPDIR:-/tmp}/lichomat-demo-v06-XXXXXX")"
trap 'rm -rf "$W"' EXIT

# hats NAME TPL AMT: voice 6 on every 16th, template TPL at AMT %, then start; 8 bars at 142 BPM
# (13.5 s) after the start at about 1.9 s
hats() {
  local s="$W/hats-$1.txt"
  { echo '0ms press voice6'
    for n in 2 3 4 6 7 8 10 11 12 14 15 16; do echo "+100ms press step$n"; done
    echo "+100ms param track_tpl=$2"
    echo "+100ms param track_tpl_amt=$3"
    echo '+200ms press start'; } > "$s"
  "$R" --card "$CARDS/init" --out "$OUT/v06-hats-$1.wav" --stereo out1 --bpm 142 --no-start --seconds 15.6 \
    --events "$s"
}
i=0
for name in off swg lay lyb rlx 2bt psh drg; do
  hats "$name" "$i" 100
  i=$((i + 1))
done
hats lay50 2 50
hats lay150 2 150
# G22_record: REC on, 16 Note Ons in bar 1 ("e" +11 ticks, "a" -5), REC off, two bars of playback
"$R" --card "$CARDS/groove" --out "$OUT/v06-record-take.wav" --stereo out1 --pattern 3 --bpm 142 --seconds 7 \
  --param midi_chan_1=2 --param midi_note1=0 --events "$ROOT/emu/tests/goldens/G22_record.txt"
cat > "$OUT/README-v0.6.txt" <<'EOF'
LICHOMAT v0.6: rendery demo z tools/demo_v06.sh (wyjście 1 w stereo, karty emu/tests/cards).
Mikrotiming kroków (mic, w tickach 1/96 kroku) i szablony groove (tpl, amt) na ścieżkę.

v06-hats-<nazwa>.wav: karta init, 142 BPM, 8 taktów; hi-hat zamknięty (głos 6) na wszystkich
szesnastkach, szablon <nazwa> na 100 %, reszta karty prosto:
  off  bez szablonu, szesnastki na siatce (odniesienie)
  swg  co druga szesnastka 16 ticków później (swing 58 %)
  lay  szesnastki po ćwierćnucie 18 ticków później, przed ćwierćnutą 5 (z utworu 5)
  lyb  to samo w skali całego taktu, każda pozycja osobno
  rlx  luźny takt, także "&" trochę później (z utworu 7)
  2bt  bity 1 i 3 swingowane, 2 i 4 prawie prosto (z utworu 6)
  psh  ósemki na offbeacie 4 ticki wcześniej (hat, który ciągnie do przodu)
  drg  ósemki na offbeacie 6 ticków później (hat, który się spóźnia)
v06-hats-lay50.wav, v06-hats-lay150.wav: lay na 50 i 150 %.
Ćwierćnuty zostają na siatce we wszystkich szablonach (stopa jest siatką).

v06-record-take.wav: karta groove, pattern 3, 142 BPM, skrypt G22_record: REC, potem 16 nut
MIDI na kanale głosu 1 w takcie 1 ("e" 11 ticków później, 12,1 ms; "a" 5 wcześniej, -5,5 ms),
REC wyłączony; takty 2 i 3 odtwarzają nagranie z tym samym mikrotimingiem. Klik jest na lewym
kanale.
EOF
ls -l "$OUT"/v06-*.wav "$OUT/README-v0.6.txt"
