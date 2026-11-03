#!/bin/bash
# The LICHOMAT v0.6 showcase tracks, rendered by the firmware in the emulator (lichomat-render), each
# in one continuous render, nothing concatenated or overdubbed. Each track has a card (a kit, one
# pattern and eight mute scenes), made by its card script the way the fixtures of emu/tests/cards are
# made, and a score (SCORE below): the arrangement played live on that card, mute scenes, fills and
# rolls from the performance mode and parameter events, all timed on the bar grid.
#
# Track 1 (the default): about 2:49 of swung, hypnotic groove techno at 142 BPM on HUSTAWKA
# (57-HUSTA.SND from the kits branch) with three edits; card tools/demo_showcase_card.txt. Heard: the
# v0.6 kits and groove templates (lay on the hats, rlx on the percussion) with per-step microtiming;
# v0.5 sample-accurate triggers (trg smp); the v0.4 reverb with rumble (the bed ducked by the kick),
# freeze, the tempo-synced delay and the DJ filter; the v0.3 fill mode, mute scenes and rolls; v0.2
# track length (a 3-step perc loop), velocity humanize and trig conditions.
#
# Track 2 (--track 2): about 2:42 of straight, driven groove techno at 144 BPM on PROSTO
# (58-PROST.SND) with edits; card tools/demo_showcase2_card.txt. One long build: an acid line on
# Drum 3 with v0.3 slides whose filter opens over the whole track, the kick driven from PROSTO's
# drive up to PRZESTER's (59-PRZES.SND), a v0.2 track tempo of 3/4 on the perc (quarter triplets
# against the bar, back in line every 4 bars), the v0.6 psh template on the hats and per-step mic on
# the perc and the bass, trig conditions, one short tension moment in the v0.4 synced delay, rolls.
#
# Writes OUT_DIR/NAME.wav (out 1, stereo, as rendered), NAME-norm.wav (the same, peak normalised to
# -1 dBFS), NAME.txt (the generated event script), where NAME is lichomat-showcase-v06 (track 1) or
# lichomat-showcase2-v06 (track 2), and README-showcase.txt (in Polish: both timelines, the levels of
# the WAVs in OUT_DIR, and how to render them again). Only these files are written; everything else in
# OUT_DIR stays.
#
#   tools/demo_showcase.sh [--track 1|2] [OUT_DIR]
#       default: track 1, build/demo (git-ignored); needs build/emu-release or build/emu (the Release
#       renderer when both are there), python3, and the kit: the local branch 'kits', or
#       LICHOMAT_KIT=FILE.SND (57-HUSTA.SND for track 1, 58-PROST.SND for track 2)
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRACK=1
if [ "${1:-}" = "--track" ]; then
  TRACK="${2:-}"
  shift 2 || true
fi
case "$TRACK" in
  1) KIT=57-HUSTA.SND; CARD_SCRIPT=demo_showcase_card.txt; NAME=lichomat-showcase-v06; BPM=142
     FADERS=0.85,0.85,0.85,0.85,0.85,0.85 ;;
  2) KIT=58-PROST.SND; CARD_SCRIPT=demo_showcase2_card.txt; NAME=lichomat-showcase2-v06; BPM=144
     FADERS=0.75,0.75,0.85,0.85,0.95,1 ;;
  *) echo "usage: tools/demo_showcase.sh [--track 1|2] [OUT_DIR]"; exit 1 ;;
esac
R="$ROOT/build/emu-release/lichomat-render"
[ -x "$R" ] || R="$ROOT/build/emu/lichomat-render"
OUT="${1:-$ROOT/build/demo}"
[ -x "$R" ] || { echo "build the emulator first: cmake -S emu -B build/emu -G Ninja && cmake --build build/emu"; exit 1; }
mkdir -p "$OUT"
W="$(mktemp -d "${TMPDIR:-/tmp}/lichomat-showcase-XXXXXX")"
trap 'rm -rf "$W"' EXIT
NAME="$OUT/$NAME"

# 1. the card: a seeded card with the track's kit as kit 0, then the card script (kit edits,
# pattern 0, the scenes, saved as project 0), kept
"$R" --seed-card "$W/seed" >/dev/null
rm -f "$W"/seed/PROJ00/00-*.SND
KITFILE="$W/seed/PROJ00/00-${KIT#*-}"
if [ -n "${LICHOMAT_KIT:-}" ]; then
  cp "$LICHOMAT_KIT" "$KITFILE"
else
  git -C "$ROOT" show "kits:kits/card/PROJ00/$KIT" > "$KITFILE" || {
    echo "no kit: fetch the branch 'kits' or set LICHOMAT_KIT to $KIT"; exit 1; }
fi
[ "$(wc -c < "$KITFILE" | tr -d ' ')" = 268 ] || { echo "$KIT is not a 268-byte kit"; exit 1; }
"$R" --card "$W/seed" --events "$ROOT/tools/$CARD_SCRIPT" --keep-card "$W/card" \
  --out "$W/card.wav" --no-start --seconds 40 >/dev/null

# 2. the score: the event script of the performance, from bars and beats
python3 - "$NAME.txt" "$W/seconds" "$TRACK" <<'SCORE'
import sys
TRACK = sys.argv[3]
BPM = {'1': 142, '2': 144}[TRACK]
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

def track1():
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
    return at(97) + 7000

def track2():
    # one long build, no breakdown: the acid's filter opens from bar 33 to bar 89 and the kick's drive
    # goes from 48 to 86; the only gap is the tension of bars 61-64
    # before the start: the performance mode, scene 1 (stopped, so at once)
    add(0, 'press mode2')
    add(60, 'press step9')
    add(START, 'press start')
    # INTRO 1-8: the kick and the hats: the closed hat on every 16th but the off-beats, straight, and
    # the open hat on the off-beats pushed 6.5 ms early (psh 150 %)
    # 9: + the bass (E2 on the off-beats, mic -36 ticks); 17: + the noise on 2 and 4; the kick's drive
    # from 48 up to 70 over bars 17-61; fills at 24 and 32 (the noise's crescendo, Fil)
    scene(9, 2)
    scene(17, 3)
    sweep('drive1', 48, 70, 17, 61)
    fill(24)
    # 25: + the perc on track tempo 3/4: quarter triplets against the bar, mic by hand on four steps
    scene(25, 4)
    fill(32)
    # 33: + the acid line (slides, trig conditions 2:2 1:2 3:4 Fil), its low-pass nearly closed and
    # opening: 14 to 34 by bar 49 (the range where it passes the notes), then on to 60 by bar 61
    scene(33, 5)
    sweep('filter_freq_3', 14, 34, 33, 49)
    roll(40, 2, 4, 9)               # the perc: 1/24 for the last two beats
    fill(48)
    param(49, 'slide_time3', 28)    # longer slides: 112 ms
    sweep('filter_freq_3', 34, 60, 49, 61)
    roll(56, 2, 6, 10)              # the closed hat: 1/32 for the last two beats
    sweep('filter_freq_2', 20, 28, 57, 65)   # the bass a little brighter up to the drop
    # TENSION 61-64: the perc and the noise out (scene 8), the acid into the delay synced to 1/8 dotted
    # (ping-pong), its feedback rising; the acid's filter up fast; 63-64 the acid and the hats alone
    # (scene 6), a 1/32 roll on the closed hat for the last two beats
    scene(61, 8)
    param(61, 'fx_type', 4, early=6)
    for i, (name, v) in enumerate((('fx_delay_type', 1), ('fx_delay_sync', 7), ('fx_p1', 64), ('fx_p2', 64),
                                   ('fx_p3', 90), ('fx_p4', 84), ('fx_dry_wet', 66))):
        param(61, name, v, early=5 - 0.5 * i)
    param(61, 'audio_out3', 6)
    sweep('fx_p4', 84, 110, 61, 65)
    sweep('filter_freq_3', 60, 88, 61, 65)
    scene(63, 6)
    roll(64, 2, 6, 10)
    # DROP 65-88: everything; the kick at PRZESTER's drive (86), the acid dry again and the delay off
    # on the downbeat (its tail cut with the drop, as in track 1), the acid's filter from 72 on up to
    # 104 by bar 89; the acid stutters (a 1/16 roll) at 76; the perc and the noise out for 77-80
    # (scene 8); fills at 72, 80 and 88
    scene(65, 5)
    for name, v in (('drive1', 86), ('audio_out3', 0), ('fx_type', 0)):
        param(65, name, v)
    sweep('filter_freq_3', 72, 104, 65, 89)
    fill(72)
    roll(76, 3, 3, 8)
    scene(77, 8)
    fill(80)
    scene(81, 5)
    fill(88)
    # OUTRO 89-96: the acid out (scene 4) and the bass back to PROSTO's filter; the kick and the hats
    # alone at 93; stopped at the end of bar 96; the reverb (off in this kit) at 0 all the same
    scene(89, 4)
    param(89, 'filter_freq_2', 20)
    scene(93, 1)
    add(at(97) - 40, 'press start')
    add(at(97) + 1000, 'param rev_level=0')
    return at(97) + 2000

END = track1() if TRACK == '1' else track2()
ev.sort(key=lambda e: e[0])
with open(sys.argv[1], 'w') as f:
    if TRACK == '1':
        f.write('# the LICHOMAT v0.6 showcase: the performance on the card of tools/demo_showcase_card.txt\n')
        f.write('# generated by tools/demo_showcase.sh; 142 BPM, a bar is %.6f ms, bar 1 at %.3f ms\n' % (BAR, T1))
    else:
        f.write('# the LICHOMAT v0.6 showcase, track 2: the performance on the card of tools/demo_showcase2_card.txt\n')
        f.write('# generated by tools/demo_showcase.sh --track 2; 144 BPM, a bar is %.6f ms, bar 1 at %.3f ms\n' % (BAR, T1))
    for ms, what in ev:
        f.write('%.3fms %s\n' % (ms, what))
open(sys.argv[2], 'w').write('%.3f' % (END / 1000))
SCORE

# 3. the render: one take, out 1 as a stereo WAV, sample-accurate triggers; the six faders below 1
# for headroom (track 1: all at 0.85, a gain of 0.72, about -2.8 dB; track 2: the kick and the bass
# lower than the top, see the README)
"$R" --card "$W/card" --out "$NAME.wav" --stereo out1 --bpm "$BPM" --no-start --seconds "$(cat "$W/seconds")" \
  --faders "$FADERS" --param trig_timing=0 --events "$NAME.txt" --summary "$W/summary.json"

# 4. the levels, the normalised copy (peak at -1 dBFS) and the README (the levels of both tracks'
# WAVs that are in OUT_DIR)
LEVELS='
import array, math, re, sys, wave
def measure(src, dst=None):
    with wave.open(src) as w:
        p = w.getparams()
        a = array.array("h", w.readframes(p.nframes))
    if sys.byteorder == "big":
        a.byteswap()
    peak = max(max(a), -min(a))
    rms = math.sqrt(sum(x * x for x in a) / len(a))
    db = lambda x: 20 * math.log10(x / 32768)
    g = 32768 * 10 ** (-1 / 20) / peak
    if dst:
        b = array.array("h", (max(-32768, min(32767, int(round(x * g)))) for x in a))
        if sys.byteorder == "big":
            b.byteswap()
        with wave.open(dst, "wb") as w:
            w.setparams(p)
            w.writeframes(b.tobytes())
    s = p.nframes / 44140.625
    return s, db(peak), db(rms), 20 * math.log10(g), db(rms) + 20 * math.log10(g)
if sys.argv[1] == "norm":
    s, pk, rms, g, nrms = measure(sys.argv[2], sys.argv[3])
    print("%s: %d:%04.1f, peak %.1f dBFS, RMS %.1f dBFS; -norm.wav: %+.1f dB" % (sys.argv[2], s // 60, s % 60, pk, rms, g))
else:
    s, pk, rms, g, nrms = measure(sys.argv[2])
    pl = lambda t: re.sub(r"([0-9])\.([0-9])", r"\1,\2", t)
    print(pl("Zmierzone: długość %d:%04.1f, szczyt %.1f dBFS, RMS %.1f dBFS (oba kanały);" % (s // 60, s % 60, pk, rms)))
    print(pl("-norm.wav: wzmocnienie %.1f dB, szczyt -1.0 dBFS, RMS %.1f dBFS." % (g, nrms)))
'
python3 -c "$LEVELS" norm "$NAME.wav" "$NAME-norm.wav"
levels() {   # the measured lines for a WAV in OUT_DIR, or a note that it is not there
  if [ -f "$OUT/$1.wav" ]; then python3 -c "$LEVELS" readme "$OUT/$1.wav"
  else echo "(Nie wyrenderowany w tym katalogu: tools/demo_showcase.sh $2.)"; fi
}
{ cat <<'EOF'
LICHOMAT v0.6: dwa utwory pokazowe, wyrenderowane przez firmware w emulatorze (tools/demo_showcase.sh).

Każdy to jeden ciągły render (lichomat-render): nic nie jest sklejane ani dogrywane. Gra karta
(kit, jeden wzorzec, osiem scen mute) ze skryptu karty, a aranżacja to granie na żywo w trybie
wykonawczym (sceny, fill, rolle) i zdarzenia parametrów, co do taktu. To rendery emulatora, nie
nagrania z LXR; poziomy są zmierzone z plików, bez odsłuchu.

=== Utwór 1: lichomat-showcase-v06 (rozbujany, hipnotyczny) ===

Groove techno, 142 BPM, 96 taktów i ogon pogłosu. Gra karta z tools/demo_showcase_card.txt: kit
HUSTAWKA (57-HUSTA.SND z gałęzi kits) z trzema zmianami (głos Snare jako perkusja D5, głośniejsza
góra, stopa trochę ciszej), jeden wzorzec i osiem scen mute. Zdarzenia parametrów: filtr DJ, pogłos,
freeze, delay, kilka parametrów kitu. Suwaki głosów na 0,85 (ok. -2,8 dB), żeby został zapas.

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

EOF
  levels lichomat-showcase-v06 "--track 1"
  cat <<'EOF'

=== Utwór 2: lichomat-showcase2-v06 (prosty, napędzany) ===

Druga strona groove techno: prosto, twardo, bez swingu, jedna długa budowa bez breakdownu.
144 BPM, 96 taktów i 2 s ciszy. Gra karta z tools/demo_showcase2_card.txt: kit PROSTO
(58-PROST.SND z gałęzi kits, proste hihaty, bas E2 zamiast bedu) ze zmianami: Drum 3 to linia
acid (piła E3 przez rezonansowy dolnoprzepustowy, slide), głos Snare to perkusja B4, Cymbal to szum
na 2 i 4, hihaty trochę dłuższe, stopa z większym przesterem (48 zamiast 32), a w trakcie utworu
przester dochodzi do 86, czyli stopy PRZESTER (59-PRZES.SND). Kitu nie zmienia się w trakcie
renderu: z PRZESTER jest tylko wartość przesteru stopy.

Góra jest głośniej niż w kicie: suwaki stopa i bas 0,75, Drum 3 i Snare 0,85, Cymbal 0,95,
hihat 1,0 (zamiast 0,85 wszędzie, jak w utworze 1), i dłuższe hihaty (zamknięty 6 zamiast 4,
otwarty 16 zamiast 12). Szumowy hihat LXR i tak zostaje dużo ciszej od stopy niż na albumie.

Pliki:
  lichomat-showcase2-v06.wav       wyjście 1, stereo, tak jak wyszło z firmware (z zapasem)
  lichomat-showcase2-v06-norm.wav  to samo, szczyt podniesiony do -1 dBFS
  lichomat-showcase2-v06.txt       wygenerowany skrypt zdarzeń (co i kiedy)

Głosy: Drum 1 stopa na ćwierćnutach. Drum 2 bas E2 na offbeatach, każdy krok mic -36 ticków
(ok. 37 ms wcześniej, wraca szybciej po stopie, jak w notatkach PROSTO). Drum 3 acid w e-moll
(nuty kroku E3, E4, G3, D4, B3, A3, G4), slide na 5 krokach, akcenty głośnością kroku, warunki
2:2, 1:2 i 3:4, krok 15 tylko w fillu (Fil). Snare perkusja B4 na tempie ścieżki 3/4 (v0.2): krok
to ósemka triolowa, co drugi krok to ćwierćnuta triolowa, 3 przeciw 2; 16 kroków trwa 4/3 taktu,
więc akcenty co takt padają gdzie indziej i wracają na miejsce co 4 takty; mic ręcznie na 4
krokach (+10, +6, -8, -6 ticków), duch 1:2. Cymbal szum na 2 i 4, w fillu crescendo na 14-16.
Hihat zamknięty na każdej 16-tce poza offbeatami, prosto; otwarty na offbeatach z szablonem psh
na 150 % (v0.6): 6 ticków, ok. 6,5 ms wcześniej, ciągnie do przodu.

Oś czasu (mm:ss od początku pliku, takty po 1,67 s):
  0:00  takt 1   INTRO. Stopa (przester 48) i hihaty: zamknięty prosto, otwarty pchany (psh). Scena 1.
  0:13  takt 9   Bas E2 na offbeatach, mic -36 (scena 2).
  0:27  takt 17  Szum na 2 i 4 (scena 3). Przester stopy rośnie powoli: 48 do 70 do taktu 61.
  0:38  takt 24  Fill: crescendo szumu na 14-16 (Fil). Fille też w taktach 32, 48, 72, 80, 88.
  0:40  takt 25  Perkusja na tempie ścieżki 3/4: triole ćwierćnutowe przeciw taktowi (scena 4).
  0:53  takt 33  Wchodzi acid (scena 5): slide, warunki 2:2, 1:2, 3:4. Filtr prawie zamknięty
                 i od teraz się otwiera: długa podróż filtra przez cały utwór (14 do 104).
  1:05  takt 40  Roll 1/24 na perkusji przez ostatnie 2 ćwierćnuty (v0.3).
  1:20  takt 49  Dłuższe slide (56 do 112 ms).
  1:32  takt 56  Roll 1/32 na zamkniętym hihacie. Od 57 bas trochę jaśniejszy.
  1:40  takt 61  NAPIĘCIE. Bez perkusji i szumu (scena 8). Acid w delay zsynchronizowany do 1/8
                 z kropką, ping-pong (v0.4), feedback rośnie, filtr acid szybko w górę.
  1:43  takt 63  Sam acid z echem i hihaty (scena 6); w takcie 64 roll 1/32 na hihacie.
  1:47  takt 65  DROP. Wszystko (scena 5), stopa z przesterem 86 (stopa PRZESTER), acid znowu
                 na sucho, delay ucięty na raz; filtr acid dalej w górę, do 104 w takcie 89.
  2:05  takt 76  Acid jąka się: roll 1/16 na Drum 3 przez ostatnią ćwierćnutę.
  2:07  takt 77  Bez perkusji i szumu (scena 8), acid na wierzchu; od 81 znowu wszystko.
  2:27  takt 89  OUTRO. Acid wychodzi (scena 4), bas wraca do filtra PROSTO.
  2:33  takt 93  Stopa i hihaty (scena 1).
  2:40  takt 97  STOP; cisza (pogłos i tak wyłączony, na końcu jeszcze raz na 0).

EOF
  levels lichomat-showcase2-v06 "--track 2"
  cat <<'EOF'

Jak wyrenderować ponownie (w worktree z gałęzią showcase, z renderem w build/emu-release albo build/emu):
  tools/demo_showcase.sh                    utwór 1, pliki trafiają do build/demo
  tools/demo_showcase.sh --track 2          utwór 2
  tools/demo_showcase.sh --track 2 KATALOG  albo do KATALOG
Kity biorą się z lokalnej gałęzi kits (git show kits:kits/card/PROJ00/57-HUSTA.SND, 58-PROST.SND);
bez niej podaj plik: LICHOMAT_KIT=/ścieżka/58-PROST.SND tools/demo_showcase.sh --track 2. Render jest
powtarzalny (seed 1): ten sam firmware daje ten sam plik, bajt w bajt.
EOF
} > "$OUT/README-showcase.txt"
ls -l "$NAME.wav" "$NAME-norm.wav" "$NAME.txt" "$OUT/README-showcase.txt"
