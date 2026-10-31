#!/usr/bin/env python3
"""M21_negative: the licence gate's name scan and its overlay check reject what
they must (stage 1b spec 10.1, 10.2, 12.1). M21 passes on a clean build with
either check turned off, so this test gives each one a case to reject, through
licence_gate.py's own functions:

1. check_names() on the object of emu/tests/name_canary.c, whose strings name
   Erica Synths and the LXR-02 in __TEXT,__cstring (__TEXT,__asan_cstring in an
   ASan build), Sonic Potions in a table of
   char[16] (__TEXT,__const) and the LXR in a char array (__DATA,__data), next
   to clean ones: exactly those four errors, in any order.
2. check_overlay() on a compile_commands.json of two commands, one with the
   main.h overlay and one without: exactly one error, for the second.
3. strings() on a Mach-O file made here, whose sections all hold the bytes 'LxR':
   the tables that describe code (__unwind_info, __eh_frame, __gcc_except_tab)
   are not read, a __const section is.

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
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ in the source tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import licence_gate  # noqa: E402


def cstring_section(canary):
    """where the compiler put the canary's C strings: __TEXT,__cstring, or under ASan
    (emulator.yml's Debug job) __TEXT,__asan_cstring"""
    out = subprocess.run(['otool', '-l', canary], capture_output=True, text=True, check=True).stdout
    return '__TEXT,__asan_cstring' if 'sectname __asan_cstring' in out else '__TEXT,__cstring'


def names(root, build, canary):
    errors = []
    licence_gate.check_names(root, build, [canary], errors)
    cstr = cstring_section(canary)
    want = ['%s: the string "%s" in %s names a trademark (spec 10.2)' % (os.path.basename(canary), s, where)
            for s, where in (('Erica Synths', cstr), ('LXR-02', cstr),
                             ('Sonic Potions', '__TEXT,__const'), ('lxr drum kit', '__DATA,__data'))]
    errors.sort()
    want.sort()
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


def macho(sections):
    """a minimal 64-bit Mach-O file of one segment: the header, the segment command with a section_64 for each of the
    (segment, section, bytes) in sections, then their contents in order"""
    cmdsize = 72 + 80 * len(sections)
    at = 32 + cmdsize
    heads = body = b''
    for seg, sect, payload in sections:
        heads += struct.pack('<16s16sQQIIIIIIII', sect.encode(), seg.encode(), 0, len(payload), at + len(body), 0, 0, 0, 0, 0, 0, 0)
        body += payload
    segment = struct.pack('<II16sQQQQiiII', licence_gate.LC_SEGMENT_64, cmdsize, b'__TEXT', 0, 0, at, len(body), 7, 7,
                          len(sections), 0)
    return struct.pack('<IiiIIIII', licence_gate.MH_MAGIC_64, 0x0100000c, 0, 1, 1, cmdsize, 0, 0) + segment + heads + body


def tables():
    """offsets and encodings can spell a name by chance (R5: 0x52784c is 'LxR' + NUL), so the tables that describe code
    are not read; a data section with the same bytes is, or the scan would read nothing"""
    spelled = b'\0LxR\0'
    unread = [('__TEXT', '__unwind_info'), ('__TEXT', '__eh_frame'), ('__TEXT', '__gcc_except_tab')]
    tmp = tempfile.mkdtemp(prefix='lichomat-m21-')
    try:
        path = os.path.join(tmp, 'macho')
        with open(path, 'wb') as f:
            f.write(macho([(seg, sect, spelled) for seg, sect in unread] + [('__TEXT', '__const', spelled)]))
        errors = []
        found = list(licence_gate.strings(path, errors))
    finally:
        shutil.rmtree(tmp)
    want = [('__TEXT', '__const', 'LxR')]
    return [] if not errors and found == want else ['the scan of the made file gave %r (%r), not %r' % (found, errors, want)]


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    root, build, canary = sys.argv[1:]
    failures = names(root, build, canary) + overlay(root) + tables()
    for f in failures:
        print('M21_negative: ' + f)
    if failures:
        sys.exit(1)
    print('M21_negative: the name scan and the overlay check reject what they must, the code tables are not scanned')


if __name__ == '__main__':
    main()
