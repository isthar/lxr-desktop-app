#!/bin/bash
# Demo renders of LICHOMAT v0.4 for listening (plan v0.4 A10 step 3): the tempo-synced delay, the
# DJ filter, the reverb, freeze, rumble and the four kit recipes of docs/lichomat/kit-recipes-v0.4.md
# (branch timing-core) at 125 BPM, from the emulator's card fixtures (emu/tests/cards), out 1 as a
# stereo WAV, with the events script and the MIDI log beside it: OUT_DIR/v04-<name>.wav, .txt and
# .midi.log, and OUT_DIR/README-v0.4.txt. Only these files are written; everything else in OUT_DIR
# stays. The first line of each .txt names the renderer's options (a comment for the script).
#
#   tools/demo_v04.sh [OUT_DIR]      default: build/demo (git-ignored); needs build/emu-release or
#                                    build/emu (the Release renderer when both are there)
#
# The reverb runs at the rate of this build (atmo.h ATMO_REVERB_DECIM; provisional until the owner's
# CPU readings H12 and U20, plan D41). The renderer does not set LFO targets (stage 1b spec 5.3), so
# the recipes' LFOs are played here as stepped parameter events, one every 80 ms.
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
  { echo "# options: --card $card --bpm 125 --no-start --seconds $seconds $*"; cat; } > "$OUT/v04-$name.txt"
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
# The reverb (plan D36): the fx card with the slot off, so the FX bus (voices 1-4) feeds the reverb
# alone. Size, decay, tone, predelay and modulation 0-127: decay 90 is RT60 0.3 s x 2^(6 x 90 / 127),
# about 5.7 s; tone 50 is a low-pass of about 1.5 kHz x 12^(50 / 63), 11 kHz (Q31: 0 dark, 64 flat,
# 127 a high-pass of 400 Hz).
# voices 1-4 at volume 80 (the fx card's four hits on one step reach full scale), cymbal and hats off
QUIET="--param vol1=80 --param vol2=80 --param vol3=80 --param vol4=80 --param vol5=0 --param vol6=0"
REVERB="--param fx_type=0 --param rev_level=110 --param rev_size=110 --param rev_tone=50 --param rev_predelay=20 --param rev_mod=40"
{
  echo '# one hit of step 1 and its tail for 4 s, then 2 bars of the pattern and the tail for 6 s'
  echo '0ms press start'; echo '+60ms press start'
  echo '+4000ms press start'; echo '+3600ms press start'
} | render 5-poglos fx 14 $REVERB --param rev_decay=90 $QUIET
# Freeze (plan D36, A8): frozen at the last hit of bar 2 (3.5 s), the sequencer stops at 3.6 s, the
# tail holds for 8 s (about -0.3 dB a minute), then thaws and decays at decay 70 (RT60 about 3.0 s).
# The param event is what the menu's frz, SHIFT + step 3 in the performance mode and NRPN 99 set.
{
  echo '# 2 bars, frozen at the last hit, stopped; thawed after 8 s'
  echo '0ms press start'
  echo '+3500ms param rev_freeze=1'; echo '+100ms press start'
  echo '+8000ms param rev_freeze=0'
} | render 6-freeze fx 16 $REVERB --param rev_decay=70 $QUIET
# Rumble (plan D40, A9): Drum 1 alone (voices 2-6 at volume 0) into the reverb, ducked by itself:
# dck dr1 (rev_duck_src=1), ddp 110; tone 20 (a low-pass of about 3.3 kHz), decay 80 (about 4.1 s).
RUMBLE="--param fx_type=0 --param rev_level=120 --param rev_size=110 --param rev_tone=20 --param rev_decay=80 --param rev_duck_src=1 --param rev_duck_depth=110"
bars4() {
  echo '# 4 bars of the pattern, stopped after the last hit (bar 4, step 13), then the tail'
  echo '0ms press start'; echo '+7400ms press start'
}
bars4 | render 7-rumble-stopa fx 12 $RUMBLE --param vol2=0 --param vol3=0 --param vol4=0 --param vol5=0 --param vol6=0
# The four kit recipes (kit-recipes-v0.4.md). lfo NAME LOW HIGH SECONDS PERIOD: a triangle from LOW
# to HIGH and back, PERIOD seconds a round, one param event every 80 ms, for SECONDS from the start.
lfo() {
  local name=$1 lo=$2 hi=$3 secs=$4 period=$5 n i v
  n=$(( secs * 1000 / 80 ))
  for i in $(seq 1 "$n"); do
    v=$(( (i * 80 * 2 * (hi - lo) / (period * 1000)) % (2 * (hi - lo)) ))
    [ "$v" -gt $(( hi - lo )) ] && v=$(( 2 * (hi - lo) - v ))
    printf '+80ms param %s=%d\n' "$name" $(( lo + v ))
  done
}
# noise wash: the snare alone (voices 1-3, 5, 6 at volume 0), all noise (mix 127), a long decay, the
# band-pass filter swept by the LFO (40 to 100 in 4 s); reverb siz 127 dec 115 ton 40 mod 80; stopped
# at 7.46 s (after bar 4's last hit at 7.2 s), frozen at 7.56 s, thawed after 6 s.
{
  echo '# 4 bars of the snare with its filter swept, stopped, frozen, thawed after 6 s'
  echo '0ms press start'
  lfo filter_freq_4 40 100 7 4
  echo '+500ms press start'; echo '+100ms param rev_freeze=1'
  echo '+6000ms param rev_freeze=0'
} | render 8-przepis-noise-wash fx 18 --param fx_type=0 --param vol1=0 --param vol2=0 --param vol3=0 \
  --param vol5=0 --param vol6=0 --param mix1=127 --param velod4=90 --param filter_type_4=2 \
  --param filter_freq_4=40 --param reso_4=40 --param rev_level=100 --param rev_size=127 \
  --param rev_decay=115 --param rev_tone=40 --param rev_predelay=30 --param rev_mod=80
# FM drone: Drum 2 alone, a long decay (dec 105), no pitch sweep (modamnt2 0), FM swept by the LFO
# (amt 20 to 90 in 8 s); reverb siz 110 dec 105 ton 30 mod 60.
{
  echo '# 4 bars of Drum 2 with its FM amount swept, then the tail'
  echo '0ms press start'
  lfo fmamnt2 20 90 7 8
  echo '+440ms press start'
} | render 9-przepis-fm-drone fx 16 --param fx_type=0 --param vol1=0 --param vol3=0 --param vol4=0 \
  --param vol5=0 --param vol6=0 --param velod2=105 --param modamnt2=0 --param fmamnt2=20 \
  --param fm_freq2=40 --param rev_level=100 --param rev_size=110 --param rev_decay=105 \
  --param rev_tone=30 --param rev_mod=60
# rumble kick: Drum 1 alone into the FX bus (out FX), the snare dry on St1 (audio_out4 0), snare,
# cymbal and hats at volume 80, Drum 2 and 3 at volume 0; the reverb as in render 7.
bars4 | render 10-przepis-rumble-kick fx 12 $RUMBLE --param vol2=0 --param vol3=0 --param audio_out4=0 \
  --param vol4=80 --param vol5=80 --param vol6=80
# dub stab: the cymbal short (dec 60, volume 127) into the FX bus, Drum 1 dry at volume 35, Drum 2,
# Drum 3, the snare and the hats at volume 0; the delay synced to 1/8 dotted (360 ms at 125 BPM) with
# feedback 90, and the reverb at level 110, decay 80, tone 50 after it, so the echoes are reverberated.
bars4 | render 11-przepis-dub-stab fx 12 $DELAY --param fx_delay_sync=7 --param audio_out1=0 \
  --param vol1=35 --param vol2=0 --param vol3=0 --param vol4=0 --param vol6=0 --param audio_out5=6 \
  --param vol5=127 --param velod5=60 --param rev_level=110 --param rev_decay=80 --param rev_tone=50
cat > "$OUT/README-v0.4.txt" <<'EOF'
LICHOMAT v0.4: rendery demo z tools/demo_v04.sh (125 BPM, wyjście 1 w stereo, karty emu/tests/cards).
Pogłos gra w tym buildzie w połowie częstotliwości próbkowania; to ustawienie tymczasowe, ostateczne
wybiorą pomiary CPU H12 i U20 na urządzeniu (reguła D41 planu v0.4).

v04-1-delay-sync-1-8d.wav: karta fx, delay zsynchronizowany do 1/8 z kropką (360 ms): jedno uderzenie i jego echa, potem 4 takty patternu i ogon ech.
v04-2-delay-wolny-czas.wav: to samo z wolnym czasem delaya (sync off, czas z kitu), do porównania.
v04-3-dj-filtr-sweep.wav: karta init, pattern 0, filtr DJ przez 8 taktów od 0 do 127 w 16 krokach: dolnoprzepustowy się otwiera, w środku płasko, potem górnoprzepustowy wycina coraz więcej dołu.
v04-4-dj-filtr-sweep-reso-100.wav: ten sam sweep z rezonansem 100 (djr).
v04-5-poglos.wav: karta fx, slot FX wyłączony, pogłos (lvl 110, siz 110, dec 90, ton 50, pre 20, mod 40): jedno uderzenie i ogon przez 4 s, potem 2 takty i ogon po zatrzymaniu.
v04-6-freeze.wav: te same ustawienia z dec 70: 2 takty, freeze na ostatnim uderzeniu, ogon trzyma się 8 s, potem odmarza i wygasa.
v04-7-rumble-stopa.wav: sama stopa (Drum 1) w pogłosie (lvl 120, siz 110, ton 20, dec 80), pogłos przyciszany przez stopę (dck dr1, ddp 110): ogon narasta między uderzeniami i chowa się pod nimi.
v04-8-przepis-noise-wash.wav: przepis "noise wash": sam szum werbla z długim wybrzmieniem, filtr pasmowy przesuwany jak przez LFO, pogłos siz 127 dec 115 ton 40 mod 80; na końcu freeze przez 6 s.
v04-9-przepis-fm-drone.wav: przepis "FM drone": Drum 2 z FM, głębokość FM przesuwana jak przez wolne LFO, pogłos siz 110 dec 105 ton 30.
v04-10-przepis-rumble-kick.wav: przepis "rumble kick": stopa w pogłosie jak w v04-7, werbel, talerz i hi-hat na sucho.
v04-11-przepis-dub-stab.wav: przepis "dub stab": krótki talerz przez delay 1/8 z kropką (fbk 90) i pogłos dec 80; stopa na sucho.

Obok każdego WAV: skrypt zdarzeń (.txt, w pierwszej linii opcje renderera) i log MIDI (.midi.log,
czas w ms); głos N wysyła na kanale MIDI N. Renderer nie ustawia celów LFO, więc w przepisach 8 i 9
LFO zastępują kroki parametru co 80 ms (trójkąt); na urządzeniu przepis używa prawdziwego LFO.
Pięciu parametrów z Q29 (filtr DJ, rezonans DJ, poziom pogłosu, freeze, decay pogłosu) nie
moduluje tu żadne LFO.
EOF
ls -l "$OUT"/v04-*.wav
