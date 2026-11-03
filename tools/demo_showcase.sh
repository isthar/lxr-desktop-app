#!/bin/bash
# The LICHOMAT v0.6 showcase track: about 2:49 of groove techno at 142 BPM, rendered by the firmware
# in the emulator (lichomat-render) in one continuous render, nothing concatenated or overdubbed.
# The kit is HUSTAWKA (57-HUSTA.SND from the kits branch) with three edits; the card (the kit, one
# pattern and eight mute scenes) is made by tools/demo_showcase_card.txt, the way the fixtures of
# emu/tests/cards are made. The arrangement is then played live on that card: mute scenes, fills,
# rolls and freeze from the performance mode, and parameter events for the DJ filter, the reverb,
# the synced delay and a few kit sounds, all timed on the bar grid by the score in SCORE below.
#
# Features heard: the v0.6 kits and groove templates (lay on the hats, rlx on the percussion) with
# per-step microtiming; v0.5 sample-accurate triggers (trg smp); the v0.4 reverb with rumble (the bed
# ducked by the kick), freeze, the tempo-synced delay and the DJ filter; the v0.3 fill mode, mute
# scenes and rolls; v0.2 track length (a 3-step perc loop), velocity humanize and trig conditions.
#
# Writes OUT_DIR/lichomat-showcase-v06.wav (out 1, stereo, as rendered), -norm.wav (the same, peak
# normalised to -1 dBFS), .txt (the generated event script) and README-showcase.txt (in Polish: the
# timeline and how to render it again). Only these files are written; everything else in OUT_DIR stays.
#
#   tools/demo_showcase.sh [OUT_DIR]   default: build/demo (git-ignored); needs build/emu-release or
#                                      build/emu (the Release renderer when both are there), python3,
#                                      and the kit: the local branch 'kits', or LICHOMAT_KIT=FILE.SND
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
mkdir -p "$OUT"
W="$(mktemp -d "${TMPDIR:-/tmp}/lichomat-showcase-XXXXXX")"
trap 'rm -rf "$W"' EXIT
NAME="$OUT/lichomat-showcase-v06"

# 1. the card: a seeded card with HUSTAWKA as kit 0, then the card script (kit edits, pattern 0, the
# scenes, saved as project 0), kept
"$R" --seed-card "$W/seed" >/dev/null
rm -f "$W"/seed/PROJ00/00-*.SND
if [ -n "${LICHOMAT_KIT:-}" ]; then
  cp "$LICHOMAT_KIT" "$W/seed/PROJ00/00-HUSTA.SND"
else
  git -C "$ROOT" show kits:kits/card/PROJ00/57-HUSTA.SND > "$W/seed/PROJ00/00-HUSTA.SND" || {
    echo "no kit: fetch the branch 'kits' or set LICHOMAT_KIT to 57-HUSTA.SND"; exit 1; }
fi
[ "$(wc -c < "$W/seed/PROJ00/00-HUSTA.SND" | tr -d ' ')" = 268 ] || { echo "57-HUSTA.SND is not a 268-byte kit"; exit 1; }
"$R" --card "$W/seed" --events "$ROOT/tools/demo_showcase_card.txt" --keep-card "$W/card" \
  --out "$W/card.wav" --no-start --seconds 40 >/dev/null

# 2. the score: the event script of the performance, from bars and beats
python3 - "$NAME.txt" "$W/seconds" <<'SCORE'
import sys
BPM = 142
BAR = 240000.0 / BPM            # ms
START = 120.0                   # the START press (ms after t0); bar 1 starts 0.162 ms later
T1 = START + 0.162
ev = []                         # (ms, line)

def at(bar, beat=0.0):
    return T1 + (bar - 1) * BAR + beat * BAR / 4

def add(ms, what):
    ev.append((max(ms, 0.0), what))

def param(bar, name, value, beat=0.0, early=3.0):
    """a parameter event just before bar:beat (a param acts at the next pass boundary)"""
    add(at(bar, beat) - early, 'param %s=%d' % (name, value))

def scene(bar, n):
    """recall mute scene n half a bar before bar: the sequencer applies it at the bar's start"""
    add(at(bar - 1, 2), 'press step%d' % (8 + n))

def fill(bar):
    """hold step 8 (the fill mode) through bar, from just after the last step of the bar before"""
    add(at(bar) - 50, 'down step8')
    add(at(bar + 1) - 50, 'up step8')

def roll(bar, beat, voice, rate):
    """hold step `voice` (a roll of that voice) from bar:beat to the end of bar, at roll rate"""
    param(bar, 'roll', rate, beat, early=150)
    add(at(bar, beat) - 8, 'down step%d' % voice)
    add(at(bar + 1) - 15, 'up step%d' % voice)

def sweep(name, v0, v1, bar0, bar1):
    """a parameter from v0 to v1, linear from the start of bar0 to the start of bar1, one event
    for each new value"""
    t0, t1, last = at(bar0), at(bar1), None
    n = int((t1 - t0) / 20)
    for i in range(n + 1):
        v = round(v0 + (v1 - v0) * i / n)
        if v != last:
            add(t0 + (t1 - t0) * i / n, 'param %s=%d' % (name, v))
            last = v

# before the start: the performance mode, scene 1 (stopped, so at once), the DJ filter low
add(0, 'press mode2')
add(30, 'param dj_filter=30')
add(60, 'press step9')
add(START, 'press start')
# INTRO 1-16: the kick and the bed with its rumble; the DJ filter opens over 8 bars; the open hat at 9
sweep('dj_filter', 30, 63, 1, 9)
param(9, 'dj_filter', 64)
scene(9, 2)
# BUILD 17-32: the ghosts and the closed hat (lay 120 %) at 17, the perc loop (3 steps, rlx) at 25
scene(17, 3)
scene(25, 4)
fill(32)
# MAIN 33-56: everything, the figure in; a perc roll at 40, the open hat longer at 41, a fill at 48,
# the figure a fifth down at 49, a fill at 56 (the fourth kick drops before the breakdown)
scene(33, 5)
roll(40, 2, 4, 9)               # 1/24 for the last two beats
param(41, 'velod6_open', 34)
fill(48)
param(49, 'coarse3', 62)        # D4
fill(56)
# BREAKDOWN 57-72: no kick, so the rumble is not ducked; the figure back on G4 and into the reverb,
# a bigger, brighter room; the DJ filter closes (low-pass) over 8 bars
scene(57, 6)
for name, v in (('coarse3', 67), ('audio_out3', 6), ('rev_level', 80), ('rev_size', 112), ('rev_decay', 88),
                ('rev_tone', 24), ('velod6_open', 26)):
    param(57, name, v)
sweep('dj_filter', 63, 28, 57, 65)
# 65: frozen just before the bar; the figure alone, through the delay synced to 1/8 dotted (ping-pong)
param(65, 'rev_freeze', 1, early=20)
param(65, 'rev_level', 100)
param(65, 'vol3', 56)           # the figure up front over the frozen tail
scene(65, 7)
param(65, 'fx_type', 4, early=6)
for i, (name, v) in enumerate((('fx_delay_type', 1), ('fx_delay_sync', 7), ('fx_p1', 64), ('fx_p2', 64),
                               ('fx_p3', 90), ('fx_p4', 88), ('fx_dry_wet', 46))):
    param(65, name, v, early=5 - 0.5 * i)
sweep('dj_filter', 28, 63, 65, 69)
# 69: the ghosts and the hats back; the DJ filter flat, then high-pass up to the drop; a 1/32 roll on
# the closed hat for the last two beats
scene(69, 8)
param(69, 'dj_filter', 64)
sweep('dj_filter', 66, 90, 69, 73)
roll(72, 2, 6, 10)
# DROP 73-88: everything; the filter flat, the delay off, the figure dry; the frozen tail stays and
# the kick ducks it (rumble on a frozen reverb); thawed at 81; open hats longer; fills at 80 and 88
scene(73, 5)
for name, v in (('dj_filter', 64), ('fx_type', 0), ('audio_out3', 0), ('vol3', 34), ('rev_level', 64),
                ('rev_size', 90), ('rev_decay', 76), ('rev_tone', 5), ('velod6_open', 34)):
    param(73, name, v)
fill(80)
param(81, 'rev_freeze', 0)
param(81, 'rev_level', 112)
fill(88)
# OUTRO 89-96: the figure and the perc out at 89, the kick and the bed alone at 93 with the DJ filter
# closing; stopped at the end of bar 96, then the rumble's tail; the reverb off 6 s later
scene(89, 3)
param(89, 'velod6_open', 26)
scene(93, 1)
sweep('dj_filter', 63, 30, 93, 97)
add(at(97) - 40, 'press start')
add(at(97) + 6000, 'param rev_level=0')   # the tail is gone; the reverb off leaves a clean digital 0
END = at(97) + 7000

ev.sort(key=lambda e: e[0])
with open(sys.argv[1], 'w') as f:
    f.write('# the LICHOMAT v0.6 showcase: the performance on the card of tools/demo_showcase_card.txt\n')
    f.write('# generated by tools/demo_showcase.sh; 142 BPM, a bar is %.6f ms, bar 1 at %.3f ms\n' % (BAR, T1))
    for ms, what in ev:
        f.write('%.3fms %s\n' % (ms, what))
open(sys.argv[2], 'w').write('%.3f' % (END / 1000))
SCORE

# 3. the render: one take, out 1 as a stereo WAV, sample-accurate triggers; the six faders at 0.85
# (a gain of 0.72, about -2.8 dB) for headroom
"$R" --card "$W/card" --out "$NAME.wav" --stereo out1 --bpm 142 --no-start --seconds "$(cat "$W/seconds")" \
  --faders 0.85,0.85,0.85,0.85,0.85,0.85 --param trig_timing=0 --events "$NAME.txt" --summary "$W/summary.json"

# 4. the levels, the normalised copy (peak at -1 dBFS) and the README
python3 - "$NAME.wav" "$NAME-norm.wav" "$W/levels" <<'LEVELS'
import array, math, sys, wave
src, dst, lev = sys.argv[1:4]
with wave.open(src) as w:
    p = w.getparams()
    a = array.array('h', w.readframes(p.nframes))
if sys.byteorder == 'big':
    a.byteswap()
peak = max(max(a), -min(a))
rms = math.sqrt(sum(x * x for x in a) / len(a))
db = lambda x: 20 * math.log10(x / 32768)
g = 32768 * 10 ** (-1 / 20) / peak
b = array.array('h', (max(-32768, min(32767, int(round(x * g)))) for x in a))
if sys.byteorder == 'big':
    b.byteswap()
with wave.open(dst, 'wb') as w:
    w.setparams(p)
    w.writeframes(b.tobytes())
s = p.nframes / 44140.625
open(lev, 'w').write('%d:%04.1f|%.1f|%.1f|%.1f|%.1f\n' % (s // 60, s % 60, db(peak), db(rms), 20 * math.log10(g), db(rms) + 20 * math.log10(g)))
print('%s: %d:%04.1f, peak %.1f dBFS, RMS %.1f dBFS; -norm.wav: %+.1f dB' % (src, s // 60, s % 60, db(peak), db(rms), 20 * math.log10(g)))
LEVELS
IFS='|' read -r LEN PEAK RMS GAIN NRMS < "$W/levels"
{ cat <<'EOF'
LICHOMAT v0.6: utwór pokazowy, wyrenderowany przez firmware w emulatorze (tools/demo_showcase.sh).

Groove techno, 142 BPM, 96 taktów i ogon pogłosu. Jeden ciągły render (lichomat-render): nic nie
jest sklejane ani dogrywane. Gra karta z tools/demo_showcase_card.txt: kit HUSTAWKA (57-HUSTA.SND
z gałęzi kits) z trzema zmianami (głos Snare jako perkusja D5, głośniejsza góra, stopa trochę
ciszej), jeden wzorzec i osiem scen mute. Aranżacja to granie na żywo w trybie wykonawczym (sceny,
fill, rolle) i zdarzenia parametrów (filtr DJ, pogłos, freeze, delay, kilka parametrów kitu), co do
taktu. Suwaki głosów na 0,85 (ok. -2,8 dB), żeby został zapas. To render emulatora, nie nagranie
z LXR; poziomy są zmierzone z pliku, bez odsłuchu.

Pliki:
  lichomat-showcase-v06.wav       wyjście 1, stereo, tak jak wyszło z firmware (z zapasem)
  lichomat-showcase-v06-norm.wav  to samo, szczyt podniesiony do -1 dBFS
  lichomat-showcase-v06.txt       wygenerowany skrypt zdarzeń (co i kiedy)

Głosy: Drum 1 stopa (prosto, bez szablonu), Drum 2 bed na offbeatach (do pogłosu, przyciszany
przez stopę: rumble), Drum 3 figura G4 (rlx 80 %, mic na krokach 4 i 12, warunki 1:2 i 4:4, kroki
Fil), Snare perkusja D5 na ścieżce 3-krokowej (rlx 80 %, przednutka 1:2), Cymbal przednutki na
parzystych 16-tkach (lay 120 %, humanize velocity 15 %), hihat zamknięty na ćwierćnutach i otwarty
na offbeatach (lay 120 %, w fillu crescendo zamkniętego na 14-16).

Oś czasu (mm:ss od początku pliku, takty po 1,69 s):
  0:00  takt 1   INTRO. Stopa i bed z rumble (pogłos v0.4 przyciszany stopą). Filtr DJ (v0.4)
                 otwiera się z dolnoprzepustowego przez 8 taktów. Scena mute 1 (v0.3).
  0:14  takt 9   Otwarty hihat na offbeatach (scena 2).
  0:27  takt 17  BUILD. Przednutki i zamknięty hihat z szablonem lay na 120 % (v0.6): "e" każdej
                 ćwierćnuty 24 ms później, "a" 7 ms; stopa prosto (scena 3). Humanize velocity (v0.2).
  0:41  takt 25  Perkusja: pętla 3 kroków przeciw 4/4 (długość ścieżki, v0.2), szablon rlx 80 %,
                 co 3 takty wraca na to samo miejsce (scena 4).
  0:53  takt 32  Fill (v0.3, trzymany krok 8): czwarta stopa milknie (!Fl), crescendo hihatu (Fil).
  0:54  takt 33  MAIN. Wchodzi figura G4: rlx 80 % i mikrotiming kroków (v0.6), warunki 1:2 i 4:4
                 (v0.2): co 2 i co 4 takty inaczej (scena 5). Triggery co do próbki (trg smp, v0.5).
  1:06  takt 40  Roll 1/24 (triole) na perkusji przez ostatnie 2 ćwierćnuty (v0.3).
  1:08  takt 41  Otwarty hihat dłuższy.
  1:20  takt 48  Fill, figura gra kroki Fil.
  1:21  takt 49  Figura kwintę niżej (D4).
  1:33  takt 56  Fill: stopa milknie na 4, przejście do breakdownu.
  1:35  takt 57  BREAKDOWN. Bez stopy (scena 6): rumble nie jest już przyciszany i rośnie. Figura
                 wraca na G4 i idzie do pogłosu (większy, jaśniejszy). Filtr DJ zamyka się przez 8 taktów.
  1:48  takt 65  Freeze pogłosu (v0.4): ogon zostaje jako plama. Sama figura (scena 7) przez delay
                 zsynchronizowany do 1/8 z kropką, ping-pong (v0.4). Filtr DJ otwiera się z powrotem.
  1:55  takt 69  Wracają przednutki i hihaty (scena 8); filtr DJ górnoprzepustowy idzie w górę do dropu.
  2:00  takt 72  Roll 1/32 na zamkniętym hihacie przez ostatnie 2 ćwierćnuty.
  2:02  takt 73  DROP. Wszystko (scena 5), filtr płasko, delay wyłączony. Zamrożony ogon dalej gra,
                 a stopa go przycisza (rumble na freeze): pompująca plama pod groove.
  2:14  takt 80  Fill.
  2:15  takt 81  Freeze puszczony: ogon wygasa, bed znowu karmi pogłos.
  2:27  takt 88  Fill.
  2:29  takt 89  OUTRO. Bez figury i perkusji (scena 3).
  2:36  takt 93  Stopa i bed (scena 1), filtr DJ zamyka się do końca.
  2:42  takt 97  STOP; ogon rumble do końca pliku.

Jak wyrenderować ponownie (w worktree z gałęzią showcase, z renderem w build/emu-release albo build/emu):
  tools/demo_showcase.sh                 pliki trafiają do build/demo
  tools/demo_showcase.sh KATALOG         albo do KATALOG
Kit bierze się z lokalnej gałęzi kits (git show kits:kits/card/PROJ00/57-HUSTA.SND); bez niej
podaj plik: LICHOMAT_KIT=/ścieżka/57-HUSTA.SND tools/demo_showcase.sh. Render jest powtarzalny
(seed 1): ten sam firmware daje ten sam plik, bajt w bajt.
EOF
  echo
  { echo "Zmierzone: długość $LEN, szczyt $PEAK dBFS, RMS $RMS dBFS (oba kanały);"
    echo "-norm.wav: wzmocnienie $GAIN dB, szczyt -1.0 dBFS, RMS $NRMS dBFS."; } | sed 's/\([0-9]\)\.\([0-9]\)/\1,\2/g'
} > "$OUT/README-showcase.txt"
ls -l "$NAME.wav" "$NAME-norm.wav" "$NAME.txt" "$OUT/README-showcase.txt"
