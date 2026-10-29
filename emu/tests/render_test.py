#!/usr/bin/env python3
"""Host tests of lichomat-render (stage 1b spec 5, 12.1 M14, M17, M23).

    render_test.py --render R --render-test RT --root ROOT --work DIR CASE

Each CASE is a function below; it runs in DIR/CASE, emptied first, and prints
PASS CASE (exit 0) or the failed check (exit 1).

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import argparse, hashlib, json, os, re, shutil, struct, subprocess, sys, wave

CASES = {}
def case(fn):
    CASES[fn.__name__] = fn
    return fn

class Fail(Exception):
    pass

def check(cond, what):
    if not cond:
        raise Fail(what)

class T:
    def __init__(self, a):
        self.r, self.rt, self.root = a.render, a.render_test, a.root
        self.work = os.path.join(a.work, a.case)
        shutil.rmtree(self.work, ignore_errors=True)
        os.makedirs(self.work)
        self.n = 0

    def path(self, *p):
        return os.path.join(self.work, *p)

    def run(self, args, code=0, test=False, timeout=300):
        cmd = [self.rt if test else self.r] + [str(x) for x in args]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        check(p.returncode == code, 'exit %d, not %d: %s\n%s' % (p.returncode, code, ' '.join(cmd), p.stderr))
        return p

    def script(self, text):
        self.n += 1
        p = self.path('s%d.txt' % self.n)
        with open(p, 'w') as f:
            f.write(text)
        return p

    def seed(self, name='seed'):
        """an init card from --seed-card in self.path(name), made once"""
        p = self.path(name)
        if not os.path.isdir(p):
            self.run(['--seed-card', p])
        return p

def tree(d):
    """{relative path: sha256} of every file and {relative path: None} of every directory"""
    out = {}
    for base, dirs, files in os.walk(d):
        for x in dirs:
            out[os.path.relpath(os.path.join(base, x), d)] = None
        for x in files:
            with open(os.path.join(base, x), 'rb') as f:
                out[os.path.relpath(os.path.join(base, x), d)] = hashlib.sha256(f.read()).hexdigest()
    return out

def main():
    ap = argparse.ArgumentParser()
    for o in ('--render', '--render-test', '--root', '--work'):
        ap.add_argument(o, required=True)
    ap.add_argument('case', choices=sorted(CASES))
    a = ap.parse_args()
    try:
        CASES[a.case](T(a))
    except Fail as e:
        print('FAIL %s: %s' % (a.case, e))
        sys.exit(1)
    print('PASS', a.case)

# --- cases ---------------------------------------------------------------------

# the options of spec 5.1 that --help names, and the test-only ones (spec 5.3)
OPTIONS = ['--card', '--out', '--seed-card', '--project', '--pattern', '--song', '--bpm', '--param',
           '--seconds', '--bars', '--events', '--no-start', '--faders', '--jacks', '--seed', '--as-booted',
           '--wav-rate', '--stereo', '--midi-log', '--clock-log', '--panel-log', '--summary', '--keep-card',
           '--version', '--help']
TEST_OPTIONS = ['--test-fault', '--test-hang', '--test-slice', '--test-prelude-reload']

def usage_error(t, args, name):
    """exit 1 with one line on stderr that starts with the prefix and names the option"""
    p = t.run(args, code=1)
    lines = p.stderr.splitlines()
    check(len(lines) == 1 and lines[0].startswith('lichomat-render: ') and name in lines[0],
          '%s: stderr is not one line naming %s: %r' % (' '.join(args), name, p.stderr))

@case
def M17_cli_usage(t):
    co = ['--card', 'C', '--out', 'O']
    usage_error(t, [], '--card')
    usage_error(t, ['--card', 'C'], '--out')	# a render needs both: the missing one is named
    usage_error(t, ['--out', 'O'], '--card')
    for args in (['--bogus'], ['--project', '64'], ['--pattern', '-1'], ['--bpm', '1'], ['--bpm', '256'],
                 ['--bpm', 'fast'], ['--faders', '1,1,1'], ['--faders', '1,1,1,1,1,1.5'], ['--jacks', 'L3'],
                 ['--wav-rate', '48000'], ['--stereo', 'both'], ['--seconds', '0'], ['--seconds', '1.1234567'],
                 ['--bars', '0'], ['--seed', 'x'], ['--param', 'shuffle'], ['--param', 'shuffle=256'],
                 ['--test-slice']):
        usage_error(t, args + co, args[0])
    usage_error(t, ['--seed-card', 'D', '--card', 'C'], '--seed-card')
    usage_error(t, ['--card', 'C', '--out'], '--out')	# an option without its value

def version_lines(root):
    with open(os.path.join(root, 'CMakeLists.txt')) as f:
        version = re.search(r'project\(LXR_V2_STM32F765[^)]*VERSION ([0-9]+\.[0-9]+)', f.read()).group(1)
    return version, [
        'lichomat-render %s, the renderer of LICHOMAT Emu' % version,
        'emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02',
        'Runs LICHOMAT %s with the emulator changes F1-F10. Built from commit COMMIT.' % version,
        'Copyright (C) 2012-2026 Erica Synths',
        'Copyright (C) 2026 Tomasz Krzywicki',
        'Modified from the LXR firmware 1.7 by Erica Synths; LICHOMAT and its emulator by Tomasz Krzywicki, 2026',
        'Arm Optimized Routines: Copyright (c) 1999-2022, Arm Limited. MIT licence.',
        'FatFs ff.h and integer.h: Copyright (C) 2017, ChaN. FatFs licence.',
        'This program comes with ABSOLUTELY NO WARRANTY. It is free software under the GNU',
        'General Public License version 3 only, with the additional permission and terms',
        'in NOTICE.md. See LICENSE and NOTICE.md.',
        'Source: https://github.com/isthar/lxr-desktop-app at the commit above.',
        'Not affiliated with Erica Synths.',
    ]

@case
def M17_cli_info(t):
    version, want = version_lines(t.root)
    check(version == '0.1', 'the firmware version is %s, not 0.1 (spec 10.1)' % version)
    out = t.run(['--version']).stdout
    m = re.search(r'Built from commit ([0-9a-f]{12}|unknown)\.', out)
    check(m, '--version names no commit of 12 hex digits: %r' % out)
    got = out.replace(m.group(1), 'COMMIT', 1)
    check(got == '\n'.join(want) + '\n', '--version printed:\n%s' % out)
    for args in (['--help'], ['--help', '--bogus'], ['--card', 'C', '--help']):
        h = t.run(args).stdout
        words = set(re.findall(r'--[a-z][a-z0-9-]*', h))
        missing = [o for o in OPTIONS if o not in words]
        check(not missing, '%s names no %s' % (' '.join(args), ', '.join(missing)))
        extra = [o for o in TEST_OPTIONS if o in words]
        check(not extra, '%s names the test options %s' % (' '.join(args), ', '.join(extra)))
    words = set(re.findall(r'--[a-z][a-z0-9-]*', t.run(['--help'], test=True).stdout))
    missing = [o for o in OPTIONS + TEST_OPTIONS if o not in words]
    check(not missing, 'lichomat-render-test --help names no %s' % ', '.join(missing))

def card_error(t, card, extra, name=None):
    """exit 2 with one line that names the card, and name when given"""
    args = ['--card', card, '--out', t.path('o.wav')] + extra
    p = t.run(args, code=2)
    lines = p.stderr.splitlines()
    check(len(lines) == 1 and lines[0].startswith('lichomat-render: card %s: ' % card)
          and (name is None or name in lines[0]),
          '%s: stderr is not one line naming the card and %s: %r' % (' '.join(args), name, p.stderr))

def card_copy(t, src, name, index=None):
    dst = t.path(name)
    shutil.copytree(src, dst)
    if index is not None:
        with open(os.path.join(dst, 'PROJ.IDX'), 'wb') as f:
            f.write(bytes([index]))
    return dst

@case
def M17_card_errors(t):
    seed = t.seed()
    card_error(t, t.path('missing'), [])
    with open(t.path('file'), 'w') as f:
        f.write('x')
    card_error(t, t.path('file'), [])
    os.makedirs(t.path('empty'))
    card_error(t, t.path('empty'), [], 'project 0 has no PROJ00/PRJ.NFO')
    card_error(t, seed, ['--project', '3'], 'project 3 has no PROJ03/PRJ.NFO')
    card_error(t, card_copy(t, seed, 'idx5', 5), [], 'project 5 has no PROJ05/PRJ.NFO')
    # byte 200 counts as 0 (E4.7), and the lookup ignores case as FatFs does
    lower = card_copy(t, seed, 'idx200', 200)
    os.rename(os.path.join(lower, 'PROJ00'), os.path.join(lower, 'proj00'))
    check(os.listdir(lower).count('proj00') == 1, 'the rename to proj00 did not happen')
    p = t.run(['--card', lower, '--out', t.path('o.wav')], code=1)
    check('rendering comes in R2.3' in p.stderr, 'PROJ.IDX 200 and proj00: %r' % p.stderr)
    # --project 0 on the same card passes too, and --project 5 on idx5's does not
    t.run(['--card', lower, '--out', t.path('o.wav'), '--project', '0'], code=1)
    card_error(t, lower, ['--project', '5'], 'project 5 has no PROJ05/PRJ.NFO')

@case
def M17_seed_card(t):
    first = t.path('new')
    t.run(['--seed-card', first])
    check(sorted(os.listdir(first)) == ['PROJ00', 'TMP'], 'the card holds %r, not PROJ00 and TMP (no PROJ.IDX)'
          % sorted(os.listdir(first)))
    check(os.listdir(os.path.join(first, 'TMP')) == [], 'TMP is not empty')
    proj = os.path.join(first, 'PROJ00')
    names = os.listdir(proj)
    check(all(os.path.isfile(os.path.join(proj, x)) for x in names), 'PROJ00 holds a folder')
    size = sum(os.path.getsize(os.path.join(proj, x)) for x in names)
    check((len(names), size) == (194, 255398), 'PROJ00 holds %d files of %d bytes, not 194 of 255,398'
          % (len(names), size))
    def read(name):
        with open(os.path.join(proj, name), 'rb') as f:
            return f.read()
    check(read('PRJ.NFO') == b'Init\0\0\0\0', 'PRJ.NFO is %r' % read('PRJ.NFO'))
    glo = read('GLO.CFG')
    check(len(glo) == 30 and glo[0] == 120 and glo[29] == 1, 'GLO.CFG is %r' % glo)
    check(len(read('00-INITK.SND')) == 255, '00-INITK.SND has %d bytes' % len(read('00-INITK.SND')))
    # into an empty folder holding a dot entry: the same card, so seeding is deterministic
    second = t.path('dot')
    os.makedirs(second)
    with open(os.path.join(second, '.DS_Store'), 'wb') as f:
        f.write(b'x')
    t.run(['--seed-card', second])
    tr = tree(second)
    check('.DS_Store' in tr, '.DS_Store is gone')
    del tr['.DS_Store']
    check(tr == tree(first), 'the second card differs from the first')
    # into a folder with a file: refused, untouched
    third = t.path('full')
    os.makedirs(third)
    with open(os.path.join(third, 'x'), 'wb') as f:
        f.write(b'x')
    p = t.run(['--seed-card', third], code=1)
    check(p.stderr.startswith('lichomat-render: --seed-card: ') and len(p.stderr.splitlines()) == 1,
          '--seed-card on a folder with a file: %r' % p.stderr)
    check(os.listdir(third) == ['x'], 'the folder with a file was changed')
    # and on a file
    p = t.run(['--seed-card', os.path.join(third, 'x')], code=1)
    check('--seed-card' in p.stderr, '--seed-card on a file: %r' % p.stderr)

if __name__ == '__main__':
    main()
