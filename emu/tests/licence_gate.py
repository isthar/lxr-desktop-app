#!/usr/bin/env python3
"""M21: the licence gate and the name scan of the emulator (stage 1b spec 10.1,
10.2, 12.1).

    licence_gate.py [--extra PATH]... <repository root> <build dir> <binary>...

1. Every source and header in the dependency files of the build (ninja -t deps)
   that lies in the repository is GPL-3.0-only or on the allow-list, and none
   names SLA0044. Core/Inc/main.h counts as the file the overlay maps it to.
   Files outside the repository are the system's (SDK, compiler). Each
   --extra PATH, relative to the root, is checked too, as given and without
   the overlay: the negative test passes Core/Inc/lcd.h and ST's main.h.
2. Every compile command of the build passes the main.h overlay, except those
   of the vendored libm and of M19's canary.
3. The C strings of each binary name 'LXR', 'Erica' or 'Sonic Potions' only in
   the allow-listed texts of emu/tests/name_allow.txt (one per line). Paths of
   the repository and the build inside a string are left out first.

The last line is 'licence gate: N errors' when the gate fails (exit 1).

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import json
import os
import re
import subprocess
import sys

GPL = 'SPDX-License-Identifier: GPL-3.0-only'
# third-party files the emulator may build with (spec 10.1), by path prefix
ALLOWED = [
    ('Middlewares/Third_Party/FatFs/src/ff.h', "ChaN's FatFs licence"),
    ('Middlewares/Third_Party/FatFs/src/integer.h', "ChaN's FatFs licence"),
    ('emu/libm/', 'Arm Optimized Routines, MIT'),
]
NAMES = re.compile(r'lxr|erica|sonic\s*potions', re.IGNORECASE)


def overlay_map(build):
    """the overlay's mappings: the virtual path to the file behind it"""
    with open(os.path.join(build, 'main_overlay.yaml')) as f:
        yaml = json.load(f)
    out = {}
    for root in yaml['roots']:
        for entry in root['contents']:
            out[os.path.normpath(os.path.join(root['name'], entry['name']))] = entry['external-contents']
    return out


def dependencies(build):
    text = subprocess.run(['ninja', '-C', build, '-t', 'deps'], check=True,
                          capture_output=True, text=True).stdout
    deps = set()
    for line in text.splitlines():
        if line.startswith('    '):
            path = line.strip()
            deps.add(os.path.normpath(path if os.path.isabs(path) else os.path.join(build, path)))
    return deps


def check_files(root, build, errors, extra=()):
    mapping = overlay_map(build)
    root = os.path.realpath(root)
    files = set()
    for dep in dependencies(build):
        real = os.path.realpath(mapping.get(dep, dep))
        if not real.startswith(root + os.sep) or real.startswith(os.path.realpath(build) + os.sep):
            continue  # a system library, or a file the build made
        files.add(real)
    # as given, without the overlay: Core/Inc/main.h here is ST's file
    files.update(os.path.realpath(os.path.join(root, p)) for p in extra)
    checked = 0
    for real in sorted(files):
        rel = os.path.relpath(real, root)
        with open(real, encoding='latin-1') as f:
            text = f.read()
        checked += 1
        if 'SLA0044' in text:
            errors.append('%s names SLA0044' % rel)
        elif GPL not in text and not any(rel.startswith(p) for p, _ in ALLOWED):
            errors.append('%s is neither GPL-3.0-only nor on the allow-list' % rel)
    if checked == 0:
        errors.append('no dependency was checked: build the tree first')
    return checked


def check_overlay(root, build, errors):
    overlay = os.path.join(build, 'main_overlay.yaml')
    # M19's canary is compiled on purpose with the firmware's floating-point
    # flags alone, at -O2, to show what clang does to expf, sinf and cosf. It
    # includes only <math.h>, and no binary links it; check_files() still
    # checks what it includes. Exempt is this one file, not its directory.
    canary = os.path.realpath(os.path.join(root, 'emu', 'tests', 'libm_canary.c'))
    with open(os.path.join(build, 'compile_commands.json')) as f:
        commands = json.load(f)
    for c in commands:
        if os.sep + 'emu' + os.sep + 'libm' + os.sep in c['file']:
            continue  # the vendored libm includes no firmware header
        if os.path.realpath(c['file']) == canary:
            continue
        if '-ivfsoverlay ' + overlay not in c['command']:
            errors.append('%s is compiled without the main.h overlay' % c['file'])
    return len(commands)


def cstrings(binary):
    text = subprocess.run(['otool', '-v', '-X', '-s', '__TEXT', '__cstring', binary], check=True,
                          capture_output=True, text=True, errors='replace').stdout
    for line in text.splitlines():
        parts = line.split('\t', 1)
        yield parts[1] if len(parts) == 2 else line


def check_names(root, build, binaries, errors):
    allow_file = os.path.join(root, 'emu', 'tests', 'name_allow.txt')
    with open(allow_file) as f:
        allowed = {l.rstrip('\n') for l in f if l.strip() and not l.startswith('#')}
    prefixes = sorted({os.path.realpath(root), root, os.path.realpath(build), build}, key=len, reverse=True)
    for binary in binaries:
        for s in cstrings(binary):
            stripped = s
            for p in prefixes:
                stripped = stripped.replace(p, '')
            if NAMES.search(stripped) and s not in allowed:
                errors.append('%s: the string "%s" names a trademark (spec 10.2)' % (os.path.basename(binary), s))


def main():
    args = sys.argv[1:]
    extra = []
    while len(args) > 1 and args[0] == '--extra':
        extra.append(args[1])
        args = args[2:]
    if len(args) < 3:
        raise SystemExit(__doc__)
    root, build, binaries = args[0], args[1], args[2:]
    errors = []
    files = check_files(root, build, errors, extra)
    commands = check_overlay(root, build, errors)
    check_names(root, build, binaries, errors)
    for e in errors:
        print('licence gate: ' + e)
    if errors:
        print('licence gate: %d errors' % len(errors))
        sys.exit(1)
    print('licence gate: %d files, %d compile commands and %d binaries pass' % (files, commands, len(binaries)))


if __name__ == '__main__':
    main()
