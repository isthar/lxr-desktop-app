#!/usr/bin/env python3
"""Listening examples of the pattern generator (E10.2, spec 2026-11-11 section 6),
rendered by the v0.7 firmware in the emulator (lichomat-render): the generated tracks
are written into a card as ordinary steps (the panel's step buttons and step page,
as a card script), so v0.7 plays them as the generator wrote them.

    tools/gen/examples.py [--renderer PATH] [--out DIR] [--only NAME...] [--keep-wav]

Each example: a seeded card with a kit from the branch 'kits' as kit 0 (its edits,
if any, by `param` events), pattern 0 built by a card script (track 1-7: `press
voiceN`, track_length 64, the step buttons bar by bar, then active_step and
step_volume / step_note / step_prob / step_flam per step; vol on for the voices that
play a velocity under 127), saved; then one take at 143 BPM from the start, stopped
after the example's bars, out 1 in stereo, sample-accurate triggers. The trigger log
of each take is checked against the generated steps (the A:B conditions over the
bars played); the WAV's peak and RMS go to the index. With ffmpeg, an MP3 (peak at
-1 dBFS, 192 kbit/s) takes the WAV's place unless --keep-wav.

Writes OUT/<name>.mp3 (or .wav) and OUT/index.json (per file: the Polish title, the
settings of each generated track, the hand-made tracks, kit, bars, levels). OUT is
build/demo/gen (git-ignored). Needs the renderer (default: build/emu-release or
build/emu of this tree, or LICHOMAT_RENDER) and git with the branch 'kits'.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of the
repository.
"""
import argparse
import array
import dataclasses
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import gen  # noqa: E402
from gen import BAR, NUM_STEPS, Settings, Track, apply_type_defaults, generate  # noqa: E402

BPM = 143
STEP_MS = 60000.0 / BPM / 4          # a 16th: 104.895 ms
BAR_MS = 16 * STEP_MS
CYCLES_PER_MS = 216000               # emu_core.h EMU_CYCLES_PER_MS
TAIL_S = 2.5                         # after the last bar: the decays
TRACK_VOICE = (1, 2, 3, 4, 5, 6, 6)  # track 1-7 -> voice 1-6 (the open hat is the hihat voice)

KICK = 'X...X...X...X...'
HAT_PLAIN = '..X...X...X...X.'           # the closed hat on the offbeats, all 127
HAT_HAND = '..X...X..o.X..X.|..X...X..x.X.oX.'   # sty 0's hand-made hats: 2 bars, accents and a ghost

# Drum 2 re-voiced as a ghost kick (spec 10): a kick-like voice, shorter and darker than the kick
GHOST = [('osc_wave_drum2', 0), ('coarse2', 31), ('fine2', 64), ('modamnt2', 34), ('mod_eg2', 44),
         ('pitch_slope2', 20), ('fmamnt2', 0), ('veloa2', 0), ('velod2', 10), ('vol_slope2', 90),
         ('trans2_vol', 0), ('filter_type_2', 0), ('filter_freq_2', 36), ('reso_2', 0), ('drive2', 18),
         ('volume_mod_on_off2', 1), ('vol2', 113)]
# The cymbal re-voiced as a noise roar for rar (no kit has one): noise through a resonant
# band-pass, driven, a short attack (so rar's stutter is heard) and a long decay
ROAR = [('wave1_cym', 4), ('wave2_cym', 4), ('wave3_cym', 4), ('mod_osc_gain1_cym', 0),
        ('mod_osc_gain2_cym', 0), ('filter_type_5', 2), ('filter_freq_5', 58), ('reso_5', 50),
        ('veloa5', 6), ('velod5', 96), ('vol_slope5', 40), ('cymbal_distortion', 80), ('vol5', 90)]


def G(typ, **kw):
    """a generated track: the kind's defaults (spec 3), then kw (`not` as not_)"""
    s = apply_type_defaults(Settings(), typ)
    if 'not' in kw:
        kw['not_'] = kw.pop('not')
    return ('gen', dataclasses.replace(s, **kw))


def H(pattern):
    """a hand-made track"""
    return ('hand', pattern)


# name, title, kit, bars, {track 1-7: G() or H()}, kit edits, faders (voice 1-6)
# Faders from single hits of each voice of PROSTO alone (peak, fader 1): kick -5.1 dBFS, bass
# (Drum 2) -8.8, figure (Drum 3) -14.4, Snare -14.9, the roar (Cymbal, ROAR) -9.0, hihat -18.1
F_HAT = (0.35, 1, 1, 1, 1, 1)
F_GHOST = (0.35, 0.4, 1, 1, 1, 1)
F_MIX = (0.45, 1, 1, 1, 0.6, 1)
F_GROOVE = (0.5, 0.4, 1, 1, 0.5, 1)


def examples():
    ex = []
    hat_titles = {0: 'moje akcenty', 1: 'klasyczny', 2: 'gęsty', 3: 'ćwierćnuty cicho', 4: 'rozsypany'}
    for sty in range(5):
        tr = {1: H(KICK), 6: G('hat', sty=sty)}
        if sty == 0:
            tr[6] = ('gen-on', G('hat', sty=0)[1], HAT_HAND)
        ex.append(dict(name='hat-sty%d' % sty, title='hat, styl %d (%s): ghosty hihatu ze stopą, 8 taktów'
                       % (sty, hat_titles[sty]), kit='58-PROST.SND', bars=8, tracks=tr, faders=F_HAT))
    sub_titles = {1: 'klasyczny: 4, 7, 12, 15', 2: '12, 15', 3: '7, 15', 4: '4, 12', 5: '8, 16',
                  6: 'gęsty: 4, 7, 8, 12, 15, 16', 7: 'figura 2-taktowa: w takcie B dochodzą 10 i 16',
                  8: 'z ziarna: 3 z miejsc 4, 7, 12, 15, 8, 16, 10, 14'}
    for sty in range(1, 9):
        ex.append(dict(name='sub-sty%d' % sty, title='sub, styl %d (%s): ghost kicki ze stopą i hihatem, 8 taktów'
                       % (sty, sub_titles[sty]), kit='58-PROST.SND', bars=8, kit_params=GHOST,
                       tracks={1: H(KICK), 2: G('sub', sty=sty), 6: G('hat')}, faders=F_GHOST))
    for var, what in ((1, 'w takcie 8 dochodzi ghost na 16. kroku'),
                      (-1, 'w takcie 8 znika jeden ghost')):
        ex.append(dict(name='sub-sty1-var%+d' % var,
                       title='sub, styl 1 (klasyczny: 4, 7, 12, 15), var %+d: %s (zmiana gra tylko w takcie 8), '
                       '8 taktów' % (var, what), kit='58-PROST.SND', bars=8, kit_params=GHOST,
                       tracks={1: H(KICK), 2: G('sub', sty=1, var=var), 6: G('hat')}, faders=F_GHOST))
    prc_titles = {1: 'synkopy', 2: 'offbeat', 3: '3+3+2', 4: 'losowy'}
    for sty in range(1, 5):
        for var in (1, -1):
            what = 'w takcie 8 dochodzi uderzenie' if var > 0 else 'w takcie 8 znika akcent'
            ex.append(dict(name='prc-sty%d-var%+d' % (sty, var),
                           title='prc, styl %d (%s), var %+d: figura 2-taktowa, %s, 8 taktów'
                           % (sty, prc_titles[sty], var, what), kit='58-PROST.SND', bars=8,
                           tracks={1: H(KICK), 3: G('prc', sty=sty, var=var), 6: H(HAT_PLAIN)}, faders=F_MIX))
    rar_titles = {1: 'koniec frazy: start w takcie 8 i 16, jąkanie w 16',
                  2: 'początek frazy: takt 1 i 9',
                  3: 'posyp: kroki z ziarna po 25 %',
                  4: 'mieszany: takt 1 i 9, takt 16 i posyp'}
    rar_den = {1: 3, 2: 2, 3: 4, 4: 4}
    for sty in range(1, 5):
        ex.append(dict(name='rar-sty%d' % sty, title='rar, styl %d, den %d (%s): szum na talerzu, 16 taktów'
                       % (sty, rar_den[sty], rar_titles[sty]), kit='58-PROST.SND', bars=16,
                       tracks={1: H(KICK), 5: G('rar', sty=sty, den=rar_den[sty]), 6: H(HAT_PLAIN)},
                       kit_params=ROAR, faders=F_MIX))
    prp_titles = {1: 'kopia przesunięta o +5', 2: 'dopełnienie', 3: 'przedtakty', 4: 'echo o +3 jako ghosty'}
    prp_shf = {1: 5, 2: 0, 3: 0, 4: 3}
    for sty in range(1, 5):
        ex.append(dict(name='prp-sty%d' % sty, title='prp, styl %d (%s): źródło to figura prc (Drum 3), '
                       'cel to Snare, 8 taktów' % (sty, prp_titles[sty]), kit='58-PROST.SND', bars=8,
                       tracks={1: H(KICK), 3: G('prc'), 4: G('prp', sty=sty, src=3, shf=prp_shf[sty]),
                               6: H(HAT_PLAIN)}, faders=F_MIX))
    ex.append(dict(name='groove', title='cały groove, 32 takty: stopa 4/4, ghost kicki sub (styl 1), ghosty hihatu (styl 1), '
                   'prc (styl 1), rar (styl 4) na talerzu; wszystko poza stopą z generatora',
                   kit='58-PROST.SND', bars=32,
                   tracks={1: H(KICK), 2: G('sub', sty=1, sed=4), 3: G('prc', sed=4), 5: G('rar', sty=4, den=4, sed=4),
                           6: G('hat', sed=4)}, kit_params=GHOST + ROAR, faders=F_GROOVE))
    return ex


# --- building the tracks and the card ----------------------------------------------------------

def build_tracks(e):
    """{track index 0-6: Track} in track order (a generator reads the tracks before it), and
    {track 1-7: description} for the index"""
    tracks, desc = {}, {}
    for n in sorted(e['tracks']):
        spec = e['tracks'][n]
        idx = n - 1
        if spec[0] == 'hand':
            tracks[idx] = Track.from_pattern(spec[1]).played()
            tracks[idx].length = NUM_STEPS
            desc[n] = dict(hand=spec[1])
            continue
        s = spec[1]
        cur = Track.from_pattern(spec[2]) if spec[0] == 'gen-on' else None
        out = generate(s, idx, tracks.get(s.src - 1, Track()), cur)
        tracks[idx] = out
        d = dict(s.as_dict(), track=n)
        if cur is not None:
            d['cur'] = spec[2]
        desc[n] = d
    return tracks, desc


def card_script(tracks, kit_params):
    ev = [('param %s=%d' % kv, 20) for kv in kit_params]
    if kit_params:
        ev.append(('save-kit 0', 200))
    ev.append(('param shuffle=0', 100))
    vol = set()
    for idx in sorted(tracks):
        t = tracks[idx]
        ev.append(('press voice%d' % (idx + 1), 100))
        ev.append(('param track_length=%d' % t.length, 100))
        for b in range(4):
            for p in range(BAR):
                if t.active[b * BAR + p]:
                    ev.append(('press step%d' % (p + 1), 100))
            if b < 3:
                ev.append(('press bar+', 100))
        ev += [('press bar-', 100)] * 3
        for i in range(NUM_STEPS):
            if not t.active[i]:
                continue
            st = t.steps[i]
            ev.append(('param active_step=%d' % i, 20))
            ev.append(('param step_volume=%d' % (st.vol & 0x7F), 20))
            if st.note != gen.DEFAULT_NOTE:
                ev.append(('param step_note=%d' % st.note, 20))
            if st.prob != 100:
                ev.append(('param step_prob=%d' % st.prob, 20))
            if st.flam:
                ev.append(('param step_flam=%d' % st.flam, 20))
        if t.needs_vol:
            vol.add(TRACK_VOICE[idx])
    ev += [('param volume_mod_on_off%d=1' % v, 20) for v in sorted(vol)]
    ev += [('save-pattern 0', 200), ('save-project 0', 300), ('mark saved', 100)]
    lines, t = [], 0
    for i, (what, dt) in enumerate(ev):
        lines.append(('0ms ' if i == 0 else '+%dms ' % dt) + what)
        t += dt
    return '\n'.join(lines) + '\n', t / 1000.0, sorted(vol)


def kit_file(kit):
    """a kit file of the branch 'kits' (268 bytes: the name in the first 8)"""
    data = subprocess.run(['git', '-C', ROOT, 'show', 'kits:kits/card/PROJ00/' + kit], check=True,
                          capture_output=True).stdout
    assert len(data) == 268, kit
    return data


def make_card(R, kit, kit_params, tracks, work):
    seed, card = os.path.join(work, 'seed'), os.path.join(work, 'card')
    run([R, '--seed-card', seed])
    proj = os.path.join(seed, 'PROJ00')
    for f in os.listdir(proj):
        if f.startswith('00-') and f.endswith('.SND'):
            os.remove(os.path.join(proj, f))
    data = kit_file(kit)
    with open(os.path.join(proj, '00-' + kit.split('-', 1)[1]), 'wb') as f:
        f.write(data)
    text, seconds, vol = card_script(tracks, kit_params)
    script = os.path.join(work, 'card.txt')
    with open(script, 'w') as f:
        f.write(text)
    run([R, '--card', seed, '--events', script, '--keep-card', card, '--out', os.path.join(work, 'card.wav'),
         '--no-start', '--seconds', '%.1f' % (seconds + 1.0)])
    return card, vol


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('%s: %s%s' % (' '.join(cmd), r.stdout, r.stderr))
    return r


def render(R, card, wav, bars, faders, work):
    stop = os.path.join(work, 'stop.txt')
    with open(stop, 'w') as f:
        f.write('%.3fms press start\n' % (bars * BAR_MS - 30.0))
    trig = os.path.join(work, 'trig.log')
    run([R, '--card', card, '--out', wav, '--stereo', 'out1', '--bpm', str(BPM),
         '--seconds', '%.3f' % (bars * BAR_MS / 1000.0 + TAIL_S), '--events', stop, '--trig-log', trig,
         '--faders', ','.join('%g' % x for x in faders), '--param', 'trig_timing=0'])
    return trig


# --- checks -------------------------------------------------------------------------------------

def expected(tracks, bars):
    """{voice 0-5: {absolute step: hits}} the generated steps play over `bars` bars (64-step
    tracks: loop = bar // 4); a step below 100 % may or may not play: in `maybe`"""
    want, maybe = {}, {}
    for idx, t in tracks.items():
        v = TRACK_VOICE[idx] - 1
        for n in range(bars * BAR):
            i = n % NUM_STEPS
            if not t.active[i]:
                continue
            p = gen.plays(t.steps[i].prob, n // NUM_STEPS)
            hits = gen.FLAM_HITS[t.steps[i].flam]
            if p is True:
                want.setdefault(v, {})[n] = hits
            elif p is None:
                maybe.setdefault(v, {})[n] = hits
    return want, maybe


def check_trigs(trig, tracks, bars):
    """the take's triggers against the generated steps: (ok, text)"""
    got = {}
    with open(trig) as f:
        for line in f:
            t, voice, src = line.split()[:3]
            if src != 'seq':
                continue
            ms = int(t) / CYCLES_PER_MS
            n = int(math.floor(ms / STEP_MS + 0.25))       # flam hits fall later in the step
            got.setdefault(int(voice), {}).setdefault(n, 0)
            got[int(voice)][n] += 1
    want, maybe = expected(tracks, bars)
    bad = []
    played_maybe = 0
    for v in sorted(set(got) | set(want)):
        g, w, m = got.get(v, {}), want.get(v, {}), maybe.get(v, {})
        for n in sorted(set(g) | set(w)):
            if n in m and n not in w:
                if g.get(n, 0) not in (0, m[n]):
                    bad.append('voice %d step %d: %d hits' % (v + 1, n, g.get(n, 0)))
                played_maybe += bool(g.get(n))
                continue
            if g.get(n, 0) != w.get(n, 0):
                bad.append('voice %d step %d: %d hits, want %d' % (v + 1, n, g.get(n, 0), w.get(n, 0)))
    n_maybe = sum(len(m) for m in maybe.values())
    text = '%d triggers as written' % sum(sum(x.values()) for x in got.values())
    if n_maybe:
        text += ', %d of %d steps under 100 %% played' % (played_maybe, n_maybe)
    if bad:
        return False, '; '.join(bad[:8]) + (' ...' if len(bad) > 8 else '')
    return True, text


def levels(wav):
    """peak and RMS in dBFS (both channels), and the share of samples at full scale"""
    with wave.open(wav) as w:
        a = array.array('h', w.readframes(w.getnframes()))
    if sys.byteorder == 'big':
        a.byteswap()
    peak = max(max(a), -min(a))
    rms = math.sqrt(sum(x * x for x in a) / len(a)) if a else 0.0
    clip = sum(1 for x in a if x >= 32767 or x <= -32768)

    def db(x):
        return round(20 * math.log10(x / 32768.0), 1) if x > 0 else None
    return dict(peak_dbfs=db(peak), rms_dbfs=db(rms), clipped_samples=clip)


def to_mp3(ffmpeg, wav, mp3, peak_dbfs):
    gain = -1.0 - peak_dbfs if peak_dbfs is not None else 0.0
    run([ffmpeg, '-y', '-loglevel', 'error', '-i', wav, '-af', 'volume=%.2fdB' % gain,
         '-codec:a', 'libmp3lame', '-b:a', '192k', mp3])
    return round(gain, 2)


# --- main ---------------------------------------------------------------------------------------

def find_renderer(arg):
    for r in (arg, os.environ.get('LICHOMAT_RENDER'), os.path.join(ROOT, 'build/emu-release/lichomat-render'),
              os.path.join(ROOT, 'build/emu/lichomat-render')):
        if r and os.access(r, os.X_OK):
            return r
    sys.exit('no renderer: build the emulator, or pass --renderer / set LICHOMAT_RENDER')


def main(argv=None):
    ap = argparse.ArgumentParser(description='render the pattern generator\'s listening examples')
    ap.add_argument('--renderer')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build/demo/gen'))
    ap.add_argument('--only', nargs='*', help='only the examples with these names')
    ap.add_argument('--keep-wav', action='store_true', help='keep the WAVs next to the MP3s')
    ap.add_argument('--print', action='store_true', help='print the examples\' grids, render nothing')
    a = ap.parse_args(argv)
    todo = [e for e in examples() if not a.only or e['name'] in a.only]
    if a.print:
        for e in todo:
            tracks, desc = build_tracks(e)
            print('== %s: %s' % (e['name'], e['title']))
            for idx in sorted(tracks):
                if 'hand' not in desc[idx + 1]:
                    print('track %d: %s' % (idx + 1, desc[idx + 1]))
                    print(gen.grid(tracks[idx]))
        return 0
    R = find_renderer(a.renderer)
    ffmpeg = shutil.which('ffmpeg')
    os.makedirs(a.out, exist_ok=True)
    index_path = os.path.join(a.out, 'index.json')
    index = {}
    if a.only and os.path.exists(index_path):
        with open(index_path) as f:
            index = {x['file']: x for x in json.load(f)['files']}
    failed = 0
    for e in todo:
        tracks, desc = build_tracks(e)
        with tempfile.TemporaryDirectory(prefix='lichomat-gen-') as work:
            card, vol = make_card(R, e['kit'], e.get('kit_params', []), tracks, work)
            wav = os.path.join(a.out, e['name'] + '.wav')
            trig = render(R, card, wav, e['bars'], e['faders'], work)
            ok, check = check_trigs(trig, tracks, e['bars'])
        lv = levels(wav)
        name = e['name'] + '.wav'
        if ffmpeg:
            name = e['name'] + '.mp3'
            lv['mp3_gain_db'] = to_mp3(ffmpeg, wav, os.path.join(a.out, name), lv['peak_dbfs'])
            if not a.keep_wav:
                os.remove(wav)
        failed += not ok
        print('%-16s %s peak %s dBFS, RMS %s dBFS%s; %s' % (
            e['name'], 'OK  ' if ok else 'FAIL', lv['peak_dbfs'], lv['rms_dbfs'],
            ', %d clipped' % lv['clipped_samples'] if lv['clipped_samples'] else '', check))
        gens = [d for d in desc.values() if 'hand' not in d]
        index[name] = dict(file=name, title=e['title'], kit=kit_file(e['kit'])[:8].rstrip(b'\x00 ').decode(),
                           kit_slot=int(e['kit'][:2]),
                           bars=e['bars'], bpm=BPM, settings=gens[0] if len(gens) == 1 else gens,
                           hand={'track %d' % n: d['hand'] for n, d in desc.items() if 'hand' in d},
                           kit_edits=dict(e.get('kit_params', [])), vol_on_voices=vol, faders=list(e['faders']),
                           levels=lv, trigger_check=check if ok else 'FAIL: ' + check)
    order = {e['name']: i for i, e in enumerate(examples())}
    files = sorted(index.values(), key=lambda x: order.get(x['file'].rsplit('.', 1)[0], 999))
    with open(index_path, 'w') as f:
        json.dump(dict(note='LICHOMAT v0.8 E10.2: przykłady generatora wzorców, render emulatora v0.7, 143 BPM, '
                       'wyjście 1 stereo; MP3 znormalizowane do szczytu -1 dBFS (levels: przed normalizacją)',
                       files=files), f, ensure_ascii=False, indent=1)
        f.write('\n')
    print('%s: %d files%s' % (index_path, len(files), ', %d FAILED' % failed if failed else ''))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
