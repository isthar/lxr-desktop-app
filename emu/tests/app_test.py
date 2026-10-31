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
import time


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
    # the diagnostics (spec 6.9): the counters of spec 5.5 follow the diag line
    check(re.search(r'^counters blocks=\d+ underruns=\d+ passes=\d+ .*queueOverflow=\d+ .*usbTxDropped=\d+$', r.stdout, re.M),
          'no counters line: ' + r.stdout)


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


def R4_dev_crash(t):
    """a fault on the firmware thread leaves crash-*.txt; the next launch shows it once (spec 6.9)"""
    card = t.card()
    r = t.dev(['--card', card, '--no-audio', '--no-midi', '--seconds', '4', '--script', t.script(['1.5 test-fault'])])
    check(r.returncode < 0, 'the harness did not end by the signal: %d' % r.returncode)
    notes = [n for n in os.listdir(t.home) if re.match(r'^crash-\d{8}-\d{6}\.txt$', n)]
    check(len(notes) == 1, 'crash notes: %r' % os.listdir(t.home))
    with open(os.path.join(t.home, notes[0])) as f:
        text = f.read()
    check(text.startswith('LICHOMAT Emu crash: SIGSEGV\nvirtual time: ') and text.count('\n') >= 10, text)
    r = t.dev(['--card', card, '--no-audio', '--no-midi', '--seconds', '2'])
    check('crash note: ' + notes[0] in r.stdout, 'the next launch did not show the note: ' + r.stdout)
    check('LICHOMAT Emu crash: SIGSEGV' in r.stdout, 'the note\'s first lines are not shown: ' + r.stdout)
    r = t.dev(['--card', card, '--no-audio', '--no-midi', '--seconds', '2'])
    check('crash note:' not in r.stdout, 'the note was shown twice')


def R4_dev_power(t):
    """a power cycle boots again, and the settings survive it (spec 6.9)"""
    r = t.dev(['--card', t.card(), '--no-audio', '--no-midi', '--seconds', '4', '--print-diag',
               '--script', t.script(['0.5 delay 12.5', '1.5 power'])])
    check(r.returncode == 0, 'exit %d: %s' % (r.returncode, r.stderr))
    check(sum(1 for l in lcd_lines(r.stdout) if splash(l)) >= 2, 'no second boot')
    with open(os.path.join(t.home, 'settings.txt')) as f:
        text = f.read()
    check('slave.delay.auto = 0\n' in text and 'slave.delay.ms = 12.5\n' in text, text)
    # the diag line at the end is the second process's: it started with the stored D
    check('D=12.50ms' in r.stdout, 'the new process did not start with the stored D: ' + r.stdout)


def R4_dev_afterpid(t):
    """--after-pid N waits for that process to end, but for at most 5 s (plan E18)"""
    args = ['--card', t.card(), '--no-audio', '--no-midi', '--seconds', '0.3']
    done = subprocess.Popen(['/usr/bin/true'])
    done.wait()
    t0 = time.monotonic()
    r = t.dev(args + ['--after-pid', str(done.pid)])
    check(r.returncode == 0 and time.monotonic() - t0 < 5.0, 'an ended process was waited for: exit %d' % r.returncode)
    child = subprocess.Popen(['/bin/sleep', '1.5'])
    try:
        t0 = time.monotonic()
        r = t.dev(args + ['--after-pid', str(child.pid)])
        took = time.monotonic() - t0
        check(r.returncode == 0, 'exit %d: %s' % (r.returncode, r.stderr))
        check(1.7 <= took < 5.0, 'it did not wait for the process to end, and no longer: %.2f s' % took)
        child.kill()
        child.wait()
        child = subprocess.Popen(['/bin/sleep', '60'])
        t0 = time.monotonic()
        r = t.dev(args + ['--after-pid', str(child.pid)])
        took = time.monotonic() - t0
        check(r.returncode == 0, 'exit %d: %s' % (r.returncode, r.stderr))
        check(5.0 <= took < 20.0, 'it did not give up after 5 s: %.2f s' % took)
    finally:
        child.kill()
        child.wait()


CASES = {f.__name__: f for f in [R4_dev_headless, R4_dev_import, R4_dev_crash, R4_dev_power, R4_dev_afterpid]}


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
