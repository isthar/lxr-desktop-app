#!/bin/bash
# Demo renders of LICHOMAT v0.3 for listening (docs/lichomat/release-notes-v0.3.md): at 120 BPM from
# a freshly seeded card, out 1 as a stereo WAV, with the events script and the MIDI log beside it:
# OUT_DIR/v03-<name>.wav, .txt and .midi.log, and OUT_DIR/README-v0.3.txt. Only these files are
# written; everything else in OUT_DIR stays.
#
#   tools/demo_v03.sh [OUT_DIR]      default: build/demo (git-ignored); needs build/emu
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
# the trig condition code (101-115) of steps of the selected voice
cond()  { local c=$1; shift; for s in "$@"; do printf '+100ms param active_step=%d\n+100ms param step_prob=%d\n' $((s - 1)) "$c"; done; }
note()  { printf '+100ms param active_step=%d\n+100ms param step_note=%d\n' $(($1 - 1)) "$2"; }
slide() { for s in "$@"; do printf '+100ms param active_step=%d\n+100ms param step_slide=1\n' $((s - 1)); done; }
store() { printf '+100ms down copy\n+50ms press step%d\n+100ms up copy\n' $(($1 + 8)); }   # scene 1-8
PERF='+100ms press 25'              # the second mode button: the performance mode
render() {                          # name, seconds, more options; the script on stdin
  local name=$1 seconds=$2; shift 2
  cat > "$OUT/v03-$name.txt"
  "$R" --card "$CARD/card" --out "$OUT/v03-$name.wav" --stereo out1 --no-start --seconds "$seconds" \
    --midi-log "$OUT/v03-$name.midi.log" --events "$OUT/v03-$name.txt" $CHANNELS "$@"
}
# 110 = FILL, 111 = !FILL; step 8 held from just after the last step of bar 3 (5.875 s) to just before
# bar 5, and the same for bar 8
{ steps 1 1 5 9 13; steps 4 5 13 14 15 16; cond 110 14 15 16; steps 6 3 7 11 15; cond 111 3 7 11 15
  echo "$PERF"; echo '+100ms press start'
  echo '+5900ms down step8'; echo '+2050ms up step8'; echo '+5950ms down step8'; echo '+2050ms up step8'; } | render 1-fill-co-4-takty 21
# scene 1: the hats muted; scene 2: the kick and the snare muted; scene 3: nothing muted
{ steps 1 1 5 9 13; steps 4 5 13; steps 6 $(seq 1 16); echo "$PERF"
  echo '+100ms press voice6'; store 1; echo '+100ms press voice6'
  echo '+100ms press voice1'; echo '+100ms press voice4'; store 2; echo '+100ms press voice1'; echo '+100ms press voice4'
  store 3; echo '+100ms press start'
  echo '+3000ms press step9'; echo '+4000ms press step10'; echo '+4000ms press step11'; } | render 2-sceny-mute-na-takcie 20
# roll 10 = 1/32, 9 = 1/24 (plan D21); step 6 of the performance mode rolls voice 6
{ steps 1 1 5 9 13; echo "$PERF"; echo '+100ms param roll=10'; echo '+100ms press start'
  echo '+4000ms down step6'; echo '+2000ms up step6'; echo '+2000ms down step6'; echo '+2000ms up step6'; } | render 3-roll-1-32 14
{ steps 1 1 5 9 13; echo "$PERF"; echo '+100ms param roll=9'; echo '+100ms press start'
  echo '+4000ms down step6'; echo '+2000ms up step6'; echo '+2000ms down step6'; echo '+2000ms up step6'; } | render 4-roll-triola-1-24 14
# midi_note1=0: no MIDI note override on Drum 1, so the MIDI log shows the step notes
ACID=(51 51 63 51 58 51 61 51 51 63 58 51 61 51 63 56)
acid() { steps 1 $(seq 1 16); for i in $(seq 1 16); do note "$i" "${ACID[$((i - 1))]}"; done; echo '+100ms param slide_time1=20'; }
{ acid; slide 3 5 7 10 11 13 15; echo '+100ms press start'; } | render 5-acid-slide-drum1 20 --param midi_note1=0
{ acid; echo '+100ms press start'; } | render 6-acid-bez-slide 20 --param midi_note1=0
cat > "$OUT/README-v0.3.txt" <<'EOF'
LICHOMAT v0.3, rendery demo z tools/demo_v03.sh (120 BPM, wyjście 1 w stereo, granie zaczyna się po
ustawieniu panelu, ok. 2-7 s od początku pliku).

v03-1-fill-co-4-takty.wav: tryb fill trzymany przez takt 4 i takt 8; werbel z FILL wchodzi na krokach
  14-16, hihat z !FILL milknie.
v03-2-sceny-mute-na-takcie.wav: trzy sceny mute przywołane w połowie taktu; zmianę słychać na początku
  następnego taktu (najpierw bez hihatu, potem bez stopy i werbla, potem wszystko).
v03-3-roll-1-32.wav: roll 1/32 na hihacie (głos 6), dwa razy po 2 s.
v03-4-roll-triola-1-24.wav: roll triolowy 1/24 na hihacie, dwa razy po 2 s.
v03-5-acid-slide-drum1.wav: linia acid na Drum 1 ze slide (80 ms) na krokach 3, 5, 7, 10, 11, 13 i 15.
v03-6-acid-bez-slide.wav: ta sama linia bez slide, do porównania.

Obok każdego WAV: skrypt zdarzeń (.txt) i log MIDI (.midi.log); głos N wysyła na kanale MIDI N.
EOF
ls -l "$OUT"/v03-*.wav
