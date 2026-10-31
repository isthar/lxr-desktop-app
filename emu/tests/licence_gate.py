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
3. The strings of each binary, the printable runs of 3 or more bytes in the
   contents of every section that does not hold instructions (string literals,
   char arrays, const tables), name 'LXR', 'Erica' or 'Sonic Potions' only in
   the allow-listed texts of emu/tests/name_allow.txt (one per line). Code is
   left out: its bytes can spell 'lxr' by chance (R2.4 saw it in a Release
   build after an unrelated change), and no text the device shows lives there.
   So are the tables that describe code (__TEXT,__unwind_info, __eh_frame and
   __gcc_except_tab: function offsets and encodings; R5 saw the offset
   0x52784c spell 'LxR').
   Paths of the repository and the build inside a string are left out first.

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
import struct
import subprocess
import sys

GPL = 'SPDX-License-Identifier: GPL-3.0-only'
# third-party files the emulator may build with (spec 10.1), by path prefix
ALLOWED = [
    ('Middlewares/Third_Party/FatFs/src/ff.h', "ChaN's FatFs licence"),
    ('Middlewares/Third_Party/FatFs/src/integer.h', "ChaN's FatFs licence"),
    ('emu/libm/', 'Arm Optimized Routines, MIT'),
    ('emu/third_party/imgui/', 'Dear ImGui, MIT'),
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


MH_MAGIC_64 = 0xfeedfacf
LC_SEGMENT_64 = 0x19
# S_ZEROFILL, S_GB_ZEROFILL, S_THREAD_LOCAL_ZEROFILL: no bytes in the file
ZEROFILL = {0x01, 0x0c, 0x12}
# S_ATTR_PURE_INSTRUCTIONS, S_ATTR_SOME_INSTRUCTIONS: the section holds code
CODE_ATTRS = 0x80000000 | 0x00000400
# the tables that describe code: offsets and encodings, no text (see the docstring); __gcc_except_tab holds the
# language-specific exception tables (call-site and landing-pad offsets) of C++ code such as the panel's
CODE_TABLES = {('__TEXT', '__unwind_info'), ('__TEXT', '__eh_frame'), ('__TEXT', '__gcc_except_tab')}
# a printable run of 3 or more bytes, as strings -a -n 3 finds it
RUN = re.compile(rb'[\t\x20-\x7e]{3,}')


def strings(binary, errors):
    """(segment, section, run) for the printable runs in the contents of every
    section of a 64-bit Mach-O file: __cstring holds only the string literals,
    while a const char x[][16] lies in __TEXT,__const and a char x[] = "..." in
    __DATA,__data. Every other section is read too, except those that hold
    instructions (S_ATTR_PURE_INSTRUCTIONS or S_ATTR_SOME_INSTRUCTIONS) and the
    unwind and exception tables."""
    with open(binary, 'rb') as f:
        data = f.read()
    if len(data) < 32 or struct.unpack_from('<I', data)[0] != MH_MAGIC_64:
        errors.append('%s is not a 64-bit Mach-O file' % binary)
        return
    ncmds = struct.unpack_from('<I', data, 16)[0]
    at = 32  # the load commands follow the mach_header_64
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<II', data, at)
        if cmd == LC_SEGMENT_64:
            nsects = struct.unpack_from('<I', data, at + 64)[0]
            for i in range(nsects):
                s = at + 72 + 80 * i  # a section_64 after the segment_command_64
                sect, seg = (n.rstrip(b'\0').decode() for n in struct.unpack_from('<16s16s', data, s))
                _, size, offset, _, _, _, flags = struct.unpack_from('<QQIIIII', data, s + 32)
                if flags & 0xff in ZEROFILL or offset == 0 or flags & CODE_ATTRS or (seg, sect) in CODE_TABLES:
                    continue
                for m in RUN.finditer(data, offset, offset + size):
                    yield seg, sect, m.group().decode('ascii')
        at += cmdsize


def check_names(root, build, binaries, errors):
    allow_file = os.path.join(root, 'emu', 'tests', 'name_allow.txt')
    with open(allow_file) as f:
        allowed = {l.rstrip('\n') for l in f if l.strip() and not l.startswith('#')}
    prefixes = sorted({os.path.realpath(root), root, os.path.realpath(build), build}, key=len, reverse=True)
    for binary in binaries:
        for seg, sect, s in strings(binary, errors):
            stripped = s
            for p in prefixes:
                stripped = stripped.replace(p, '')
            if NAMES.search(stripped) and s not in allowed:
                errors.append('%s: the string "%s" in %s,%s names a trademark (spec 10.2)'
                              % (os.path.basename(binary), s, seg, sect))


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
