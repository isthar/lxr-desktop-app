#!/usr/bin/env python3
"""Build LICHOMAT's sound kits with the firmware itself, through lichomat-render.

    python3 tools/kit_build.py [--renderer PATH] [--no-demo] [--work DIR] [KIT...]

KIT is a spec kits/KIT.txt; without KITs, every kits/*.txt. A spec holds lines
NAME = VALUE, a '#' starts a comment:
- 'name' is the kit's name on the display, 1-8 characters A-Z 0-9 _;
- 'slot' is the kit slot 0-63 the file is for;
- every other NAME is a parameter of lichomat-render --param (emu/render/
  render_params.h), VALUE 0-255 as the menu stores it (the menu clamps it).

For each kit:
1. the firmware writes the kit. A card from lichomat-render --seed-card; the
   kit's name goes into the 8 name bytes of the seeded slot's file (an empty
   slot leaves them 0), which the firmware reads at boot as it reads any card.
   The render sets the parameters through the menu's edit path (--param),
   saves the kit into its slot (save-kit) and the project (save-project), and
   keeps the card. The firmware names the file NN-NAME.SND, NN the slot and
   NAME the first 5 characters of the name; it goes to kits/card/PROJ00/.
2. the readback. The demo card: the pattern card (below) with kit 0 replaced
   by the new file, renamed 00-NAME.SND. A render loads it and saves the
   project again: the firmware's kit 0 must equal the file byte for byte, and
   every parameter of the spec must read back at its place in the file.
3. the demo (unless --no-demo): the demo card's pattern 0 with kit 0, 8 bars at
   128 BPM, trg smp, out 1 as a stereo WAV in kits/demo/KIT.wav; then the
   measurements of the demo and of each voice alone (the faders of the others
   at 0), in plain Python: peak, RMS, the kick's fundamental, each voice's decay.

The pattern card is made once per run: an init card with the demo pattern
(PATTERN below) put in by the step buttons and saved as pattern 0.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import argparse
import array
import glob
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KITS = os.path.join(ROOT, 'kits')
BPM = 128
BARS = 8
STEP_S = 60.0 / BPM / 4			# one 16th

# The demo pattern, 16 steps: voice -> [(step, volume, condition)]. Volume 0-127 (None: the
# step's default); condition 101..115 as in Core/Inc/Preset/SeqStepTypes.h (102 2:2, 109 4:4).
PATTERN = {
    1: [(1, None, None), (5, None, None), (9, None, None), (13, None, None)],			# kick 4/4
    2: [(2, 50, None), (3, 60, None), (4, 115, None), (6, 50, 102), (7, 115, None),		# perc 16ths,
        (8, 55, None), (10, 115, None), (11, 50, None), (12, 60, None), (14, 50, 102),	# accents 3-3-2,
        (15, 60, None), (16, 115, None)],											# 2:2 ghosts
    3: [(4, 100, None), (11, 90, None), (14, 70, 109)],								# perc / stab, 4:4 fill
    4: [(5, None, None), (13, None, None), (16, 45, 109)],							# snare/clap 2 and 4
    5: [(3, 90, None), (7, 70, None), (11, 90, None), (15, 70, None)],				# ride / rim, off-beats
    6: [(2, 80, None), (6, 70, None), (8, 55, None), (10, 80, None), (14, 70, None),	# closed hat
        (16, 55, None)],
    7: [(3, 100, None), (7, 90, None), (11, 100, None), (15, 90, None)],			# open hat, off-beats
}
VOICE_NAMES = {1: 'Drum 1', 2: 'Drum 2', 3: 'Drum 3', 4: 'Snare', 5: 'Cymbal', 6: 'Hihat'}

NAME = re.compile(r'^[A-Z0-9_]{1,8}$')
KIT_BYTES = 268			# 8 name + 247 parameters + the 13-byte kit block of v0.4
END_OF_SOUND = 247
KIT_BLOCK_FIRST = 302	# PAR_REV_SIZE: the block's 7 settings follow its 6-byte header


def fail(what):
    sys.exit('kit_build: ' + what)


def read_spec(path):
    """(name, slot, [(param, value)]) of a spec"""
    name, slot, params = None, None, []
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            m = re.match(r'^([a-z0-9_]+)\s*=\s*(\S+)$', line)
            if not m:
                fail('%s:%d: not NAME = VALUE' % (path, n))
            key, value = m.groups()
            if key == 'name':
                if not NAME.match(value):
                    fail('%s:%d: the name %s is not 1-8 of A-Z 0-9 _' % (path, n, value))
                name = value
            elif not value.isdigit() or int(value) > (63 if key == 'slot' else 255):
                fail('%s:%d: %s is not a number 0-%d' % (path, n, value, 63 if key == 'slot' else 255))
            elif key == 'slot':
                slot = int(value)
            else:
                params.append((key, int(value)))
    if name is None or slot is None:
        fail('%s: no name or no slot' % path)
    return name, slot, params


class Renderer:
    def __init__(self, path, work):
        self.path, self.work = path, work

    def run(self, args):
        p = subprocess.run([self.path] + args, capture_output=True, text=True)
        if p.returncode != 0:
            fail('lichomat-render %s:\n%s' % (' '.join(args), p.stderr))
        return p

    def seed(self, name):
        d = os.path.join(self.work, name)
        shutil.rmtree(d, ignore_errors=True)
        self.run(['--seed-card', d])
        return d

    def script(self, name, lines):
        path = os.path.join(self.work, name + '.txt')
        with open(path, 'w') as f:
            f.write(''.join(x + '\n' for x in lines))
        return path

    def keep(self, card, name, lines, seconds, extra=()):
        """render card with the script lines, without starting; the kept card"""
        kept = os.path.join(self.work, name + '-kept')
        shutil.rmtree(kept, ignore_errors=True)
        self.run(['--card', card, '--out', os.path.join(self.work, name + '.wav'), '--no-start',
                  '--seconds', '%.3f' % seconds, '--events', self.script(name, lines),
                  '--keep-card', kept] + list(extra))
        return kept


def one(pattern):
    found = glob.glob(pattern)
    if len(found) != 1:
        fail('%d files match %s' % (len(found), pattern))
    return found[0]


def pattern_card(r):
    """an init card whose pattern 0 is PATTERN"""
    lines = ['0ms mark begin']
    for voice, steps in sorted(PATTERN.items()):
        lines.append('+100ms press voice%d' % voice)
        for step, _, _ in steps:
            lines.append('+100ms press step%d' % step)
        for step, vol, cond in steps:
            if vol is not None or cond is not None:
                lines.append('+100ms param active_step=%d' % (step - 1))
            if vol is not None:
                lines.append('+100ms param step_volume=%d' % vol)
            if cond is not None:
                lines.append('+100ms param step_prob=%d' % cond)
    lines += ['+200ms save-pattern 0', '+200ms save-project 0', '+100ms mark saved']
    seconds = 0.1 * len(lines) + 1
    return r.keep(r.seed('pattern-seed'), 'pattern', lines, seconds)


def build_kit(r, kit, name, slot, params, out_dir):
    """the firmware writes the kit (the script's param events take the menu's edit path); its file
    in out_dir"""
    seed = r.seed(kit + '-seed')
    slot_file = one(os.path.join(seed, 'PROJ00', '%02d-*.SND' % slot))
    with open(slot_file, 'r+b') as f:
        f.write(name.encode().ljust(8, b'\0'))
    lines = ['0ms mark begin'] + ['+20ms param %s=%d' % pv for pv in params]
    lines += ['+100ms save-kit %d' % slot, '+200ms save-project 0', '+100ms mark saved']
    kept = r.keep(seed, kit + '-build', lines, 0.02 * len(params) + 1.5)
    made = one(os.path.join(kept, 'PROJ00', '%02d-*.SND' % slot))
    for old in glob.glob(os.path.join(out_dir, '%02d-*.SND' % slot)):
        os.remove(old)
    dest = os.path.join(out_dir, os.path.basename(made))
    shutil.copyfile(made, dest)
    return dest


def param_numbers(r, kit, params):
    """each parameter's number and its value after the menu's clamp, from the summaries of short
    renders with --param (at most 64 a render)"""
    numbers, values = {}, {}
    seed = r.seed(kit + '-lookup-seed')
    for k in range(0, len(params), 60):
        summary = os.path.join(r.work, '%s-lookup%d.json' % (kit, k))
        args = []
        for p, v in params[k:k + 60]:
            args += ['--param', '%s=%d' % (p, v)]
        r.run(['--card', seed, '--out', os.path.join(r.work, kit + '-lookup.wav'), '--no-start', '--seconds', '0.1',
               '--summary', summary] + args)
        with open(summary) as f:
            s = json.load(f)
        numbers.update(s['paramNumbers'])
        values.update(s['params'])
    return numbers, values


def demo_card(r, pat, kit, kit_file):
    """the pattern card with kit 0 replaced by kit_file"""
    card = os.path.join(r.work, kit + '-demo-card')
    shutil.rmtree(card, ignore_errors=True)
    shutil.copytree(pat, card)
    os.remove(one(os.path.join(card, 'PROJ00', '00-*.SND')))
    shutil.copyfile(kit_file, os.path.join(card, 'PROJ00', '00-' + os.path.basename(kit_file)[3:]))
    return card


def readback(r, card, kit, kit_file, name, numbers, values):
    """the firmware loads the kit and writes it back unchanged; the spec's values are in the file"""
    with open(kit_file, 'rb') as f:
        data = f.read()
    if len(data) != KIT_BYTES or data[:8] != name.encode().ljust(8, b'\0'):
        fail('%s: %d bytes, name %r' % (kit_file, len(data), data[:8]))
    kept = r.keep(card, kit + '-readback', ['0ms save-project 0', '+100ms mark saved'], 0.5)
    back = open(one(os.path.join(kept, 'PROJ00', '00-*.SND')), 'rb').read()
    if back != data:
        fail('%s: the firmware wrote kit 0 back differently at %r' % (kit_file,
             [i for i in range(min(len(back), len(data))) if back[i] != data[i]][:10]))
    for p, n in numbers.items():
        at = 8 + n if n < END_OF_SOUND else 255 + 6 + n - KIT_BLOCK_FIRST if n >= KIT_BLOCK_FIRST else None
        if at is None:
            fail('%s: %s (%d) is not a kit parameter' % (kit, p, n))
        want = values[p]
        if p in ('dj_filter', 'rev_freeze'):
            want = 64 if p == 'dj_filter' else 0		# a kit always saves djf 64 and frz off
        if data[at] != want:
            fail('%s: %s reads back %d, not %d' % (kit, p, data[at], want))
    return len(numbers)


# --- measurements ---------------------------------------------------------------------

def read_wav(path):
    with wave.open(path) as w:
        if w.getsampwidth() != 2:
            fail('%s: not 16-bit' % path)
        a = array.array('h', w.readframes(w.getnframes()))
        if sys.byteorder == 'big':
            a.byteswap()
        return w.getframerate(), w.getnchannels(), a


def db(x):
    return 20 * math.log10(x) if x > 0 else -999.0


def level(path):
    """peak and RMS in dBFS over both channels"""
    _, _, a = read_wav(path)
    peak = max(max(a), -min(a)) / 32768.0
    rms = math.sqrt(sum(x * x for x in a) / len(a)) / 32768.0
    return db(peak), db(rms)


def mono(path):
    rate, ch, a = read_wav(path)
    return rate, [(a[i] + a[i + 1]) / 65536.0 for i in range(0, len(a), 2)] if ch == 2 else [x / 32768.0 for x in a]


def onset_after(x, rate, t):
    """the first sample at or after t seconds above -40 dBFS"""
    for i in range(int(t * rate), len(x)):
        if abs(x[i]) > 0.01:
            return i
    return None


def fundamental(x, rate, start, t0=0.04, t1=0.16):
    """the frequency from the zero crossings (with hysteresis) between start+t0 and start+t1"""
    seg = x[start + int(t0 * rate):start + int(t1 * rate)]
    if not seg:
        return None
    th = max(abs(v) for v in seg) * 0.05
    state, rising = 0, []
    for i, v in enumerate(seg):
        if state <= 0 and v > th:
            if state < 0:
                rising.append(i)
            state = 1
        elif state >= 0 and v < -th:
            state = -1
    if len(rising) < 2:
        return None
    return (len(rising) - 1) * rate / (rising[-1] - rising[0])


def decay_ms(x, rate, start, limit_s, drop_db=40.0):
    """ms from the peak after start until the 2-ms envelope falls drop_db below it, within limit_s"""
    blk = int(0.002 * rate)
    stop = min(len(x), start + int((limit_s - 0.005) * rate))	# before the next hit
    env = [max(abs(v) for v in x[i:i + blk]) for i in range(start, stop, blk)]
    if not env:
        return None, False
    k = max(range(len(env)), key=lambda i: env[i])
    floor = env[k] * 10 ** (-drop_db / 20)
    for i in range(k, len(env)):
        if env[i] < floor:
            return (i - k) * 2.0, True
    return (len(env) - k) * 2.0, False


def hit_and_gap(voice):
    """the first step of a voice (track 6 and 7 share the hat) and the time to the voice's next step"""
    steps = sorted(s for s, _, _ in PATTERN[voice] + (PATTERN[7] if voice == 6 else []))
    s0 = PATTERN[voice][0][0]
    nxt = [s for s in steps if s > s0]
    return s0, ((nxt[0] if nxt else 17) - s0) * STEP_S


def solo(r, card, kit, voice, dry):
    """one bar of the pattern with only this voice's fader up; dry: the slot and the reverb off"""
    wav = os.path.join(r.work, '%s-solo%d%s.wav' % (kit, voice, '-dry' if dry else ''))
    faders = ','.join('1' if i == voice else '0' for i in range(1, 7))
    extra = ['--param', 'fx_type=0', '--param', 'rev_level=0'] if dry else []
    r.run(['--card', card, '--out', wav, '--pattern', '0', '--bpm', str(BPM), '--bars', '2',
           '--param', 'trig_timing=0', '--stereo', 'out1', '--faders', faders] + extra)
    return wav


def measure(r, card, kit, demo):
    """the demo's peak and RMS; each voice alone: its peak and RMS with the FX (the second bar),
    its decay to -40 dB without them (Drum 1: the fundamental too)"""
    peak, rms = level(demo)
    print('  demo %s: peak %.1f dBFS, RMS %.1f dBFS%s' % (os.path.relpath(demo, ROOT), peak, rms,
          '' if peak <= -1.0 else '  CLIPPING (peak above -1 dBFS)'))
    out = {'peak': round(peak, 2), 'rms': round(rms, 2), 'voices': {}}
    for v in range(1, 7):
        rate, x = mono(solo(r, card, kit, v, False))
        bar2 = x[int(16 * STEP_S * rate):]
        pk = db(max(abs(t) for t in bar2))
        vr = db(math.sqrt(sum(t * t for t in bar2) / len(bar2)))
        rate, x = mono(solo(r, card, kit, v, True))
        s0, gap = hit_and_gap(v)
        i = onset_after(x, rate, (s0 - 1) * STEP_S)
        if i is None:
            print('  %-7s silent' % VOICE_NAMES[v])
            continue
        d, done = decay_ms(x, rate, i, gap)
        line = '  %-7s peak %6.1f, RMS %6.1f dBFS; decay to -40 dB %s%.0f ms' % (
            VOICE_NAMES[v], pk, vr, '' if done else '>', d)
        info = {'peak': round(pk, 1), 'rms': round(vr, 1), 'decay40_ms': d, 'decay_complete': done}
        if v == 6:		# the open hat on track 7, until the next hat
            o = PATTERN[7][0][0]
            nxt = [s for s, _, _ in sorted(PATTERN[6] + PATTERN[7]) if s > o]
            d7, done7 = decay_ms(x, rate, onset_after(x, rate, (o - 1) * STEP_S), ((nxt[0] if nxt else 17) - o) * STEP_S)
            line += ', open %s%.0f ms' % ('' if done7 else '>', d7)
            info['open_decay40_ms'] = d7
        if v == 1:
            f = fundamental(x, rate, i)
            f0 = fundamental(x, rate, i, 0.0, 0.025)
            line += '; fundamental %s (40-160 ms), %s in the first 25 ms' % (
                '%.1f Hz' % f if f else 'n/a', '%.0f Hz' % f0 if f0 else 'n/a (no clean cycles)')
            info['fundamental_hz'] = round(f or 0, 1)
            info['start_hz'] = round(f0 or 0)
        print(line)
        out['voices'][v] = info
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('kits', nargs='*')
    ap.add_argument('--renderer', default=os.path.join(ROOT, 'build', 'emu', 'lichomat-render'))
    ap.add_argument('--no-demo', action='store_true')
    ap.add_argument('--work', help='the working folder (default: a temporary one, removed at the end)')
    a = ap.parse_args()
    if not os.access(a.renderer, os.X_OK):
        fail('no renderer at %s; build it: cmake -S emu -B build/emu -G Ninja -DCMAKE_BUILD_TYPE=Release '
             '&& cmake --build build/emu --target lichomat-render' % a.renderer)
    kits = a.kits or sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(KITS, '*.txt')))
    if not kits:
        fail('no kits/*.txt')
    work = a.work or tempfile.mkdtemp(prefix='kit_build-')
    os.makedirs(work, exist_ok=True)
    out_dir = os.path.join(KITS, 'card', 'PROJ00')
    demo_dir = os.path.join(KITS, 'demo')
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(demo_dir, exist_ok=True)
    r = Renderer(a.renderer, work)
    try:
        pat = pattern_card(r)
        results = {}
        slots = {}
        for kit in kits:
            name, slot, params = read_spec(os.path.join(KITS, kit + '.txt'))
            if slot in slots:
                fail('%s and %s both take slot %d' % (slots[slot], kit, slot))
            slots[slot] = kit
            kit_file = build_kit(r, kit, name, slot, params, out_dir)
            numbers, values = param_numbers(r, kit, params)
            card = demo_card(r, pat, kit, kit_file)
            n = readback(r, card, kit, kit_file, name, numbers, values)
            print('%s: %s, %d parameters read back' % (kit, os.path.relpath(kit_file, ROOT), n))
            if not a.no_demo:
                demo = os.path.join(demo_dir, kit + '.wav')
                r.run(['--card', card, '--out', demo, '--pattern', '0', '--bpm', str(BPM), '--bars', str(BARS),
                       '--param', 'trig_timing=0', '--stereo', 'out1'])
                results[kit] = measure(r, card, kit, demo)
        if results:
            with open(os.path.join(work, 'measurements.json'), 'w') as f:
                json.dump(results, f, indent=1)
    finally:
        if not a.work:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
