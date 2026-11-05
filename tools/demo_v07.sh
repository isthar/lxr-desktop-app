#!/bin/bash
# Demo renders of LICHOMAT v0.7 for listening (plan v0.7 Task 8): the velocity templates. The init card
# at 142 BPM with the closed hat (voice 6) on all sixteen 16ths at volume 127 (the card has steps 1, 5, 9
# and 13; the script adds the twelve others) and the hat's vol (velocity -> volume) on; the open hat
# (voice 7, played by the same voice) is taken off its steps 1, 5, 9 and 13 first, or it would replace
# the closed hat on the quarters at full level. Once without a velocity template and once per template
# at 100 % (v07-hats-<name>.wav, off of8 ful dep qtr), and of8 with vdp swept from 0 to 200 % over each
# bar (v07-hats-of8-sweep.wav), 8 bars after the start; out 1 as a stereo WAV, and
# OUT_DIR/README-v0.7.txt. Only these files are written; everything else in OUT_DIR stays.
#
#   tools/demo_v07.sh [OUT_DIR]      default: build/demo (git-ignored); needs build/emu-release or
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
W="$(mktemp -d "${TMPDIR:-/tmp}/lichomat-demo-v07-XXXXXX")"
trap 'rm -rf "$W"' EXIT

# hats NAME VTP: the open hat off, voice 6 on every 16th at volume 127 with vol (velocity -> volume) on,
# velocity template VTP at 100 %, then start; 8 bars at 142 BPM (13.5 s) after the start at 2.2 s
hats() {
  local s="$W/hats-$1.txt"
  { echo '0ms press voice7'
    for n in 1 5 9 13; do echo "+100ms press step$n"; done
    echo '+100ms press voice6'
    for n in 2 3 4 6 7 8 10 11 12 14 15 16; do echo "+100ms press step$n"; done
    echo '+100ms param volume_mod_on_off6=1'
    echo "+100ms param track_vtp=$2"
    echo '+100ms param track_vdp=100'
    echo '+200ms press start'; } > "$s"
  "$R" --card "$CARDS/init" --out "$OUT/v07-hats-$1.wav" --stereo out1 --bpm 142 --no-start --seconds 15.8 \
    --events "$s"
}
i=0
for name in off of8 ful dep qtr; do
  hats "$name" "$i"
  i=$((i + 1))
done
# the sweep: of8 with vdp at step k of every bar k x 200 / 15, rounded (0, 13, 27, ..., 200); each
# value 60 ms after the step before it (a 16th is 105.634 ms at 142 BPM), 8 bars
s="$W/sweep.txt"
{ t=0; echo '0ms press voice7'
  for n in 1 5 9 13; do t=$((t + 100)); echo "${t}ms press step$n"; done
  echo '500ms press voice6'
  t=500
  for n in 2 3 4 6 7 8 10 11 12 14 15 16; do t=$((t + 100)); echo "${t}ms press step$n"; done
  echo '1800ms param volume_mod_on_off6=1'
  echo '1900ms param track_vtp=1'
  echo '2000ms param track_vdp=0'
  echo '2100ms press start'
  awk 'BEGIN { for (n = 0; n < 127; n++) printf "%.3fms param track_vdp=%d\n", 2160 + n * 105.6338, int(((n + 1) % 16) * 200 / 15 + 0.5) }'; } > "$s"
"$R" --card "$CARDS/init" --out "$OUT/v07-hats-of8-sweep.wav" --stereo out1 --bpm 142 --no-start --seconds 15.7 \
  --events "$s"
cat > "$OUT/README-v0.7.txt" <<'EOF'
LICHOMAT v0.7: rendery demo z tools/demo_v07.sh (wyjście 1 w stereo, karta emu/tests/cards/init).
Szablony głośności na ścieżkę (vtp, vdp): każdy krok ciszej o tyle dB, ile szablon ma na jego
pozycji w takcie (0 % nic nie zmienia, 200 % podwaja dB).

v07-hats-<nazwa>.wav: 142 BPM, 8 taktów; hi-hat zamknięty (głos 6) na wszystkich szesnastkach,
każdy krok na głośności 127, szablon głośności <nazwa> na 100 %; hi-hat otwarty (głos 7) zdjęty
z ćwierćnut, bo grałby je zamiast zamkniętego na pełnej głośności; reszta karty bez zmian:
  off  bez szablonu, wszystkie szesnastki równo (odniesienie)
  of8  mediana 8 utworów z albumu referencyjnego: offbeatowe ósemki najgłośniej
  ful  dwa utwory z gęstymi szesnastkami: ghosty bliżej akcentów
  dep  utwór z głębokimi ghostami
  qtr  ćwierćnuty tuż pod offbeatem, szesnastki głęboko
v07-hats-of8-sweep.wav: of8, a vdp w każdym takcie rośnie krok po kroku od 0 % (krok 1)
do 200 % (krok 16): takt zaczyna się równo i kończy mocno zaakcentowany.

Szablony w dB, pozycje 1-16 taktu (pozycje 1, 5, 9, 13 to ćwierćnuty, 3, 7, 11, 15 offbeaty):
  of8  -10 -15  0 -14 -10 -14  0 -13 -10 -16  0 -14 -10 -15  0 -13
  ful   -7 -15  0  -5  -7  -9 -1 -10  -7 -14  0  -7  -6 -10 -1 -10
  dep  -14 -12  0 -19 -14 -11  0 -19 -15 -12  0 -16 -13 -12  0 -21
  qtr   -5 -15  0 -20  -6 -17  0 -16  -5 -17  0 -15  -6 -17 -1 -16
Hat karty init ma włączony przester (distortion), który ściska ciche uderzenia: w tych renderach
są do 3 dB głośniejsze, niż mówi tabela. Bez przesteru poziomy zgadzają się z tabelą co do 0,2 dB.

Ważne: velocity zmienia głośność głosu tylko z włączonym "vol" tego głosu (velocity -> głośność,
strona głosu, ekran velocity "dst amt vol"). W tych renderach hat ma vol włączone; na LXR włącz je
tak samo, inaczej szablon zmienia tylko MIDI Note On i modulację velocity, a hat gra równo.
EOF
ls -l "$OUT"/v07-*.wav "$OUT/README-v0.7.txt"
