#!/usr/bin/env python3
"""Random sweeps of the emulator under the sanitizers: many short renders, each with
random sound parameters, random MIDI input or random sequencer edits, looking for a
sanitizer report or a crash in the firmware (stage 1b spec 8; the F16-F18 fixes came
from these sweeps).

    emu_fuzz.py sound|midi|seq [--count N] [--seed S] [--render PATH] [--card DIR] [--out DIR]

sound: 40 random sound parameters per render (--param), 2 s of pattern 0 or 1.
midi:  40-200 random DIN messages per render: CC, NRPN, notes, Program Change, pitch
       bend, aftertouch, SPP, song select and random bytes, mostly on channel 1.
seq:   30-120 random edits per render: voices, steps, step parameters of steps 0-63
       (the step buttons' range), track settings, rolls, the performance mode, mute
       scenes, rotation, start and stop.

Each finding (a different source line, or a different exit message) is printed once
with its seed; render N uses seed S + N, so `--count 1 --seed <seed>` repeats one.
Only parameters that the renderer edits through the menu are used: the LFO and
velocity targets, which the renderer refuses (spec 5.3), are left out. The renders
are thrown away; the findings go to OUT/found.json. Defaults: build/emu's renderer
(Debug, UBSan), the card emu/tests/cards/init, OUT build/emu-fuzz/MODE.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REFUSED = ('target_lfo', 'voice_lfo', 'vel_dest_')


def render_parameters():
    """every (name, PAR_...) pair of emu/render/render_params.h"""
    with open(os.path.join(ROOT, 'emu', 'render', 'render_params.h')) as f:
        return re.findall(r'\{ "([a-z0-9_]+)", (PAR_[A-Z0-9_]+) \}', f.read())


def sound_parameters():
    """the renderer's sound parameter names, from emu/render/render_params.h and
    Core/Inc/Parameters.h (everything before END_OF_SOUND_PARAMETERS)"""
    with open(os.path.join(ROOT, 'Core', 'Inc', 'Parameters.h'), encoding='latin-1') as f:
        text = f.read()
    body = text[text.index('enum'):text.index('END_OF_SOUND_PARAMETERS')]
    body = re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S)
    sound = set(re.findall(r'\b(PAR_[A-Z0-9_]+)\b', body))
    return [n for n, p in render_parameters() if p in sound and n != 'none' and not n.startswith(REFUSED)]


def value(rnd):
    return rnd.choice([0, 1, 63, 64, 100, 101, 115, 116, 126, 127, rnd.randrange(128)])


def sound_case(rnd, names):
    args = ['--pattern', str(rnd.randrange(2)), '--seconds', '2']
    for n in rnd.sample(names, min(40, len(names))):
        args += ['--param', '%s=%d' % (n, value(rnd))]
    return args, None


def midi_message(rnd):
    ch = rnd.choice([0, 0, 0, rnd.randrange(16)])
    k = rnd.random()
    if k < 0.45:
        return [0xB0 | ch, rnd.randrange(128), value(rnd)]
    if k < 0.65:
        return [0xB0 | ch, 0x63, value(rnd), 0xB0 | ch, 0x62, value(rnd),
                0xB0 | ch, 0x06, value(rnd), 0xB0 | ch, 0x26, value(rnd)]
    if k < 0.80:
        return [rnd.choice([0x90, 0x80]) | ch, value(rnd), value(rnd)]
    if k < 0.85:
        return [0xC0 | ch, value(rnd)]
    if k < 0.90:
        return [0xE0 | ch, value(rnd), value(rnd)]
    if k < 0.93:
        return [0xA0 | ch, value(rnd), value(rnd)]
    if k < 0.96:
        return [0xD0 | ch, value(rnd)]
    if k < 0.98:
        return [0xF2, value(rnd), value(rnd)] if rnd.random() < 0.5 else [0xF3, value(rnd)]
    return [rnd.randrange(256) for _ in range(rnd.randrange(1, 6))]


def midi_case(rnd, names):
    lines, t = [], 0
    for _ in range(rnd.randrange(40, 200)):
        t += rnd.randrange(1, 25)
        lines.append('%dms din %s' % (t, ' '.join('%02X' % b for b in midi_message(rnd))))
    return ['--pattern', '0', '--seconds', '%.3f' % (t / 1000 + 0.5)], lines


STEP_PARAMETERS = ['step_volume', 'step_prob', 'step_note', 'step_slide', 'step_flam', 'step_shift']
TRACK_PARAMETERS = ['track_tempo', 'human_time', 'human_vol', 'track_delay', 'track_length',
                    'track_rotation', 'shuffle', 'roll', 'scene_quant', 'euklid_length',
                    'euklid_steps', 'euklid_rotation', 'bpm']


def seq_case(rnd, names):
    known = {n for n, _ in render_parameters()}  # a branch before v0.2/v0.3 lacks some
    steps = [n for n in STEP_PARAMETERS if n in known]
    tracks = [n for n in TRACK_PARAMETERS if n in known]
    lines, t = ['0ms press start'], 0

    def at(cmd):
        nonlocal t
        t += rnd.randrange(20, 120)
        lines.append('%dms %s' % (t, cmd))
    for _ in range(rnd.randrange(30, 120)):
        r = rnd.random()
        if r < 0.12:
            at('press voice%d' % rnd.randrange(1, 8))
        elif r < 0.22:
            at('press step%d' % rnd.randrange(1, 17))
        elif r < 0.55:
            at('param active_step=%d' % rnd.randrange(64))
            at('param %s=%d' % (rnd.choice(steps), value(rnd)))
        elif r < 0.72:
            at('param %s=%d' % (rnd.choice(tracks), value(rnd)))
        elif r < 0.80:
            at('press 25')
        elif r < 0.86:
            at('down step%d' % rnd.randrange(1, 17))
            at('up step%d' % rnd.randrange(1, 17))
        elif r < 0.90:
            at('down copy')
            at('press step%d' % rnd.randrange(9, 17))
            at('up copy')
        elif r < 0.93:
            at('press shift')
        elif r < 0.96:
            at('down shift')
            at('press step%d' % rnd.randrange(1, 3))
            at('up shift')
        else:
            at('press start')
    return ['--pattern', str(rnd.randrange(2)), '--no-start', '--seconds', '%.3f' % (t / 1000 + 1.0)], lines


CASES = {'sound': sound_case, 'midi': midi_case, 'seq': seq_case}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('mode', choices=sorted(CASES))
    ap.add_argument('--count', type=int, default=300)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--render', default=os.path.join(ROOT, 'build', 'emu', 'lichomat-render'))
    ap.add_argument('--card', default=os.path.join(ROOT, 'emu', 'tests', 'cards', 'init'))
    ap.add_argument('--out')
    a = ap.parse_args()
    out = a.out or os.path.join(ROOT, 'build', 'emu-fuzz', a.mode)
    os.makedirs(out, exist_ok=True)
    tmp = os.path.join(out, 'tmp')
    names = sound_parameters()
    found = {}
    for k in range(a.count):
        seed = a.seed + k
        rnd = random.Random(seed)
        args, script = CASES[a.mode](rnd, names)
        cmd = [a.render, '--card', a.card, '--out', os.path.join(out, 'render.wav')] + args
        if script is not None:
            with open(os.path.join(out, 'script.txt'), 'w') as f:
                f.write('\n'.join(script) + '\n')
            cmd += ['--events', os.path.join(out, 'script.txt')]
        os.makedirs(tmp, exist_ok=True)
        r = subprocess.run(cmd, capture_output=True, text=True, env=dict(os.environ, TMPDIR=tmp))
        shutil.rmtree(tmp)  # the card copies a fault keeps
        if r.returncode == 0:
            continue
        m = re.search(r'((?:Core/Src|emu)/[^:]+:\d+):\d+: runtime error: (.*)', r.stderr)
        key = m.group(1) if m else 'exit %d: %s' % (r.returncode, (r.stderr.strip().splitlines() or [''])[-1][:140])
        if key not in found:
            found[key] = {'seed': seed, 'message': m.group(2)[:200] if m else '', 'exit': r.returncode,
                          'args': args, 'script': script}
            print('%s: %s (seed %d)' % (key, found[key]['message'] or 'see found.json', seed), flush=True)
    with open(os.path.join(out, 'found.json'), 'w') as f:
        json.dump(found, f, indent=1)
    print('%d renders, %d distinct findings' % (a.count, len(found)))
    sys.exit(1 if found else 0)


if __name__ == '__main__':
    main()
