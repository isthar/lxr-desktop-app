#!/usr/bin/env python3
"""The app's tests that run lichomat-emu-dev, the Dear ImGui pin and the bundle
(stage 1b plan R4). Each case is a ctest id:

    app_test.py --dev DEV --render RENDER --root ROOT --work WORK [--app APP] ID

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys


def check(ok, msg):
    if not ok:
        print('FAIL: ' + msg)
        sys.exit(1)


class Case:
    def __init__(self, a):
        self.a = a
        self.work = os.path.join(a.work, a.id)
        shutil.rmtree(self.work, ignore_errors=True)
        os.makedirs(self.work)
        self.home = os.path.join(self.work, 'home')

    def card(self, name='init'):
        dst = os.path.join(self.work, 'card-' + name)
        shutil.copytree(os.path.join(self.a.root, 'emu', 'tests', 'cards', name), dst)
        return dst

    def script(self, lines):
        path = os.path.join(self.work, 'script.txt')
        with open(path, 'w') as f:
            f.write('\n'.join(lines) + '\n')
        return path

    def dev(self, args, timeout=60):
        env = dict(os.environ, LICHOMAT_EMU_HOME=self.home)
        return subprocess.run([self.a.dev] + args, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                              timeout=timeout, env=env)


def lcd_lines(out):
    """(wall seconds, row 0, row 1) of every lcd line"""
    return [(float(m.group(1)), m.group(2), m.group(3))
            for m in re.finditer(r'^lcd (\S+) \|(.{16})\|(.{16})\|$', out, re.M)]


def splash(line):
    return 'LICHOMAT' in line[1] and 'v0.1 unofficial' in line[2]


def R4_dev_headless(t):
    r = t.dev(['--card', t.card(), '--no-audio', '--no-midi', '--seconds', '5', '--print-diag',
               '--script', t.script(['2.0 press 33'])])
    check(r.returncode == 0, 'exit %d: %s' % (r.returncode, r.stderr))
    lcd = lcd_lines(r.stdout)
    check(lcd and lcd[0][0] < 0.3 and splash(lcd[0]), 'the splash is not first: %r' % lcd[:1])
    after = [l for l in lcd if not splash(l)]
    check(after and after[0][0] >= 0.95, 'the splash did not show for 1 s of wall time (spec 10.2): %r' % after[:1])
    # Wall pacing (spec 4.10): virtual time never runs more than one slice and one snapshot (13 ms) ahead
    # of the wall clock, and falls behind only where a slow VM makes it re-anchor, by at most 100 ms each
    # time; 250 ms allows two. The tight 689.7 +- 0.1 blocks per second stays the owner's A11.
    m = re.search(r'^diag .* vt=(\S+) wall=(\S+)$', r.stdout, re.M)
    check(m, 'no diag line: ' + r.stdout)
    drift = float(m.group(2)) - float(m.group(1))
    check(-0.013 <= drift <= 0.25, 'virtual time is %.3f s behind the wall clock' % drift)
    check('warning:' not in r.stdout and 'hang:' not in r.stdout, r.stdout)


def R4_dev_import(t):
    """a first run seeds the app's card; an import backs it up, copies and power-cycles (spec 6.7)"""
    src = t.card('fx')
    r = t.dev(['--render', t.a.render, '--no-audio', '--no-midi', '--seconds', '6',
               '--script', t.script(['1.5 import ' + src])])
    check(r.returncode == 0, 'exit %d: %s' % (r.returncode, r.stderr))
    backups = os.listdir(os.path.join(t.home, 'Card backups'))
    check(len(backups) == 1 and re.match(r'^\d{8}-\d{6}$', backups[0]), 'backups: %r' % backups)
    seeded = os.listdir(os.path.join(t.home, 'Card backups', backups[0], 'PROJ00'))
    check(len(seeded) == 194, 'the seeded card had %d files in PROJ00, not 194' % len(seeded))
    # both PRJ.NFO hold 'Init'; the file lists tell the fx fixture (4 files) from the seeded card (194)
    card = sorted(os.listdir(os.path.join(t.home, 'Card', 'PROJ00')))
    check(card == sorted(os.listdir(os.path.join(src, 'PROJ00'))) and len(card) == 4, 'PROJ00 holds %r' % card)
    check(sum(1 for l in lcd_lines(r.stdout) if splash(l)) >= 2, 'no second boot after the import')


CASES = {f.__name__: f for f in [R4_dev_headless, R4_dev_import]}


def main():
    p = argparse.ArgumentParser()
    for o in ('--dev', '--render', '--root', '--work', '--app'):
        p.add_argument(o)
    p.add_argument('id')
    a = p.parse_args()
    check(a.id in CASES, 'no case ' + a.id)
    CASES[a.id](Case(a))
    print('PASS ' + a.id)


if __name__ == '__main__':
    main()
