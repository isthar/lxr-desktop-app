#!/bin/bash
# Demo renders of LICHOMAT v0.4 for listening, first half (plan v0.4 A10 step 3): the tempo-synced
# delay and the DJ filter at 125 BPM, from the emulator's card fixtures (emu/tests/cards), out 1 as
# a stereo WAV, with the events script and the MIDI log beside it: OUT_DIR/v04-<name>.wav, .txt and
# .midi.log, and OUT_DIR/README-v0.4.txt. Only these files are written; everything else in OUT_DIR
# stays.
#
#   tools/demo_v04.sh [OUT_DIR]      default: build/demo (git-ignored); needs build/emu-release or
#                                    build/emu (the Release renderer when both are there)
#
# The renders of the reverb, freeze, rumble and the kit recipes come with plan v0.4 A7-A11.
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
# voice N sends on MIDI channel N, so that the log tells the voices apart (the audio is the same)
CHANNELS="--param midi_chan_2=2 --param midi_chan_3=3 --param midi_chan_4=4 --param midi_chan_5=5 --param midi_chan_6=6 --param midi_chan_7=7"
render() {                          # name, card, seconds, more options; the script on stdin
  local name=$1 card=$2 seconds=$3; shift 3
  cat > "$OUT/v04-$name.txt"
  "$R" --card "$CARDS/$card" --out "$OUT/v04-$name.wav" --stereo out1 --bpm 125 --no-start --seconds "$seconds" \
    --midi-log "$OUT/v04-$name.midi.log" --events "$OUT/v04-$name.txt" $CHANNELS "$@"
}
# The fx card: pattern 0 has voices 1-7 on steps 1, 5, 9 and 13, and voices 1-4 go to the FX bus
# (dry/wet 64, P1-P4 64). fx_type 4 = del, fx_delay_type 0 = Del (not ping-pong), fx_p4 = feedback.
# fx_delay_sync 7 = 8d (delaySyncNames: off 16t 16 8t 16d 8 4t 8d 4 4d, plan D38): at 125 BPM
# 0.75 x 60 / 125 = 360 ms; 0 = the kit's free time (P1 time, P2 range).
DELAY="--param fx_type=4 --param fx_delay_type=0 --param fx_p4=90"
hits() {
  echo '# one hit of step 1, then its echoes alone for 3 s'
  echo '0ms press start'; echo '+60ms press start'
  echo '# 4 bars of the pattern, stopped after the last hit (bar 4, step 13), then the tail of the echoes'
  echo '+3000ms press start'; echo '+7600ms press start'
}
hits | render 1-delay-sync-1-8d fx 14 $DELAY --param fx_delay_sync=7
hits | render 2-delay-wolny-czas fx 14 $DELAY --param fx_delay_sync=0
# The init card's pattern 0 (voices 1-7 on steps 1, 5, 9 and 13), all on out 1. The DJ filter (plan
# D39) from 0 to 127 in 16 steps of half a bar (960 ms), 8 bars: 0-62 low-pass (19.5 Hz-17.9 kHz),
# 63-65 flat, 66-127 high-pass (22.4 Hz up to 0.45 fs); round(127 i / 15) = 0 8 17 25 34 42 51 59 68
# 76 85 93 102 110 119 127. dj_reso 0-127: k = 2 - 1.8 r / 127.
sweep() {
  echo '# the DJ filter from 0 (set before the start) to 127, a step every half bar; stop after 8 bars'
  echo '0ms press start'
  for i in $(seq 1 15); do printf '+960ms param dj_filter=%d\n' $(( (127 * i + 7) / 15 )); done
  echo '+900ms press start'
}
sweep | render 3-dj-filtr-sweep init 16.5 --param dj_filter=0
sweep | render 4-dj-filtr-sweep-reso-100 init 16.5 --param dj_filter=0 --param dj_reso=100
cat > "$OUT/README-v0.4.txt" <<'EOF'
LICHOMAT v0.4, pierwsza połowa: rendery demo z tools/demo_v04.sh (125 BPM, wyjście 1 w stereo, karty
emu/tests/cards). Pogłos, freeze, rumble i przepisy na kity dojdą z zadaniami A7-A11.

v04-1-delay-sync-1-8d.wav: karta fx, delay zsynchronizowany do 1/8 z kropką (360 ms): jedno uderzenie i jego echa, potem 4 takty patternu i ogon ech.
v04-2-delay-wolny-czas.wav: to samo z wolnym czasem delaya (sync off, czas z kitu), do porównania.
v04-3-dj-filtr-sweep.wav: karta init, pattern 0, filtr DJ przez 8 taktów od 0 do 127 w 16 krokach: dolnoprzepustowy się otwiera, w środku płasko, potem górnoprzepustowy wycina coraz więcej dołu.
v04-4-dj-filtr-sweep-reso-100.wav: ten sam sweep z rezonansem 100 (djr).

Obok każdego WAV: skrypt zdarzeń (.txt) i log MIDI (.midi.log, czas w ms); głos N wysyła na kanale
MIDI N, a w logach z tych kart są nuty głosów 1 i 2.
EOF
ls -l "$OUT"/v04-*.wav
