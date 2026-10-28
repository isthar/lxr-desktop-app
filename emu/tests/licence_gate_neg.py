#!/usr/bin/env python3
"""M21_negative: the licence gate's name scan and its overlay check reject what
they must (stage 1b spec 10.1, 10.2, 12.1). M21 passes on a clean build with
either check turned off, so this test gives each one a case to reject, through
licence_gate.py's own functions:

1. check_names() on the object of emu/tests/name_canary.c, whose C strings
   name Erica Synths and the LXR-02, and a clean one: exactly those two errors.
2. check_overlay() on a compile_commands.json of two commands, one with the
   main.h overlay and one without: exactly one error, for the second.

    licence_gate_neg.py <repository root> <build dir> <name canary object>

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import json
import os
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ in the source tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import licence_gate  # noqa: E402


def names(root, build, canary):
    errors = []
    licence_gate.check_names(root, build, [canary], errors)
    want = ['%s: the string "%s" names a trademark (spec 10.2)' % (os.path.basename(canary), s)
            for s in ('Erica Synths', 'LXR-02')]
    return [] if errors == want else ['the name scan gave %r, not %r' % (errors, want)]


def overlay(root):
    tmp = tempfile.mkdtemp(prefix='lichomat-m21-')
    try:
        with_overlay = os.path.join(root, 'emu', 'tests', 'with_overlay.c')
        without = os.path.join(root, 'emu', 'tests', 'without_overlay.c')
        commands = [
            {'directory': tmp, 'file': with_overlay,
             'command': 'cc -ivfsoverlay %s -c %s' % (os.path.join(tmp, 'main_overlay.yaml'), with_overlay)},
            {'directory': tmp, 'file': without, 'command': 'cc -c %s' % without},
        ]
        with open(os.path.join(tmp, 'compile_commands.json'), 'w') as f:
            json.dump(commands, f)
        errors = []
        licence_gate.check_overlay(root, tmp, errors)
    finally:
        shutil.rmtree(tmp)
    want = ['%s is compiled without the main.h overlay' % without]
    return [] if errors == want else ['the overlay check gave %r, not %r' % (errors, want)]


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    root, build, canary = sys.argv[1:]
    failures = names(root, build, canary) + overlay(root)
    for f in failures:
        print('M21_negative: ' + f)
    if failures:
        sys.exit(1)
    print('M21_negative: the name scan and the overlay check reject what they must')


if __name__ == '__main__':
    main()
