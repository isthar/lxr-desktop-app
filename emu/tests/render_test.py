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

if __name__ == '__main__':
    main()
