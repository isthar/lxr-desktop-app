#!/usr/bin/env python3
"""P11: the licence gate and the name scan of the plugin's build (plan E9.2 Task 10), the
emulator's M21 (emu/tests/licence_gate.py) with these additions:

- the vendored VST 3 SDK, plugin/third_party/vst3sdk/, is allowed (MIT; its LICENSE.txt goes into
  the bundle's Contents/Resources);
- the SDK's own compile commands need not pass the main.h overlay: they include no firmware
  header. Every other command of the build must;
- with --bundle, the bundle holds exactly its files, no symbolic link and no empty folder: the
  plugin and the engine, Info.plist and moduleinfo.json (whose texts name no maker), and in
  Contents/Resources the notices of all the code in it, each byte for byte its source: the
  repository's LICENSE and NOTICE.md, the VST 3 SDK's MIT
  licence (VST3-SDK-LICENSE.txt), and those of the code the engine links from the emulator, as the
  app's bundle has them: Arm Optimized Routines' (LICENSE.arm-optimized-routines, emu/libm/LICENSE)
  and FatFs' (LICENSE.fatfs, the first 18 lines of ff.h).

    plugin_licence_gate.py [--bundle LICHOMAT.vst3] <repository root> <build dir> <binary>...

The binaries are name-scanned as M21 does: the bundle's Contents/MacOS/LICHOMAT and
Contents/Helpers/lichomat-engine, the engine, the test host and the test tools.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import importlib.util
import json
import os
import sys

SDK = os.path.join('plugin', 'third_party', 'vst3sdk') + os.sep
# the bundle's files, and for the copied ones their source in the repository: a path, or (a path, the
# number of its first lines)
BUNDLE = {
    'Contents/Info.plist': None,
    'Contents/MacOS/LICHOMAT': None,
    'Contents/Helpers/lichomat-engine': None,
    'Contents/Resources/moduleinfo.json': None,
    'Contents/Resources/LICENSE': 'LICENSE',
    'Contents/Resources/NOTICE.md': 'NOTICE.md',
    'Contents/Resources/VST3-SDK-LICENSE.txt': SDK + 'LICENSE.txt',
    'Contents/Resources/LICENSE.arm-optimized-routines': os.path.join('emu', 'libm', 'LICENSE'),
    'Contents/Resources/LICENSE.fatfs': (os.path.join('Middlewares', 'Third_Party', 'FatFs', 'src', 'ff.h'), 18),
    'Contents/_CodeSignature/CodeResources': None,
}
# the bundle's texts that the host shows or reads: they name no maker
TEXTS = ('Contents/Info.plist', 'Contents/Resources/moduleinfo.json')


def gate(root):
    spec = importlib.util.spec_from_file_location('licence_gate', os.path.join(root, 'emu', 'tests', 'licence_gate.py'))
    g = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(g)
    g.ALLOWED.append((SDK, 'Steinberg VST 3 SDK, MIT'))
    return g


def check_overlay(root, build, errors):
    """licence_gate.check_overlay() less the SDK's commands"""
    overlay = os.path.join(build, 'main_overlay.yaml')
    sdk = os.path.realpath(os.path.join(root, SDK))
    with open(os.path.join(build, 'compile_commands.json')) as f:
        commands = json.load(f)
    checked = 0
    for c in commands:
        path = os.path.realpath(c['file'])
        if os.sep + 'emu' + os.sep + 'libm' + os.sep in c['file'] or path.startswith(sdk + os.sep):
            continue
        checked += 1
        if '-ivfsoverlay ' + overlay not in c['command']:
            errors.append('%s is compiled without the main.h overlay' % c['file'])
    return checked


def check_bundle(g, root, bundle, errors):
    """the bundle's files, its licences and notices, and the names in its texts"""
    found = set()
    for d, dirs, names in os.walk(bundle):
        rel = os.path.relpath(d, bundle).replace(os.sep, '/')
        if not dirs and not names:
            errors.append('the bundle has an empty folder %s' % rel)
        for n in dirs + names:
            if os.path.islink(os.path.join(d, n)):
                errors.append('the bundle\'s %s is a symbolic link' % os.path.join(rel, n).replace(os.sep, '/'))
        for n in names:
            found.add(os.path.relpath(os.path.join(d, n), bundle).replace(os.sep, '/'))
    for f in sorted(found - set(BUNDLE)):
        errors.append('the bundle holds %s, which is not one of its files' % f)
    for f in sorted(set(BUNDLE) - found):
        errors.append('the bundle has no %s' % f)
    for f, src in sorted(BUNDLE.items()):
        if src is None or f not in found:
            continue
        path, lines = src if isinstance(src, tuple) else (src, None)
        with open(os.path.join(bundle, f), 'rb') as a, open(os.path.join(root, path), 'rb') as b:
            want = b.read() if lines is None else b''.join(b.readlines()[:lines])
            if a.read() != want:
                errors.append('the bundle\'s %s is not %s%s' % (f, path, '' if lines is None else
                                                                 ' (its first %d lines)' % lines))
    with open(os.path.join(root, SDK, 'LICENSE.txt')) as f:
        if not f.readline().startswith('MIT License'):
            errors.append('%sLICENSE.txt is not the MIT licence' % SDK)
    for f in TEXTS:
        if f in found:
            with open(os.path.join(bundle, f), encoding='utf-8', errors='replace') as t:
                m = g.NAMES.search(t.read())
            if m:
                errors.append('the bundle\'s %s names "%s"' % (f, m.group()))
    return len(found)


def main():
    args = sys.argv[1:]
    bundle = None
    if len(args) > 1 and args[0] == '--bundle':
        bundle, args = args[1], args[2:]
    if len(args) < 3:
        raise SystemExit(__doc__)
    root, build, binaries = args[0], args[1], args[2:]
    g = gate(root)
    errors = []
    files = g.check_files(root, build, errors)
    commands = check_overlay(root, build, errors)
    held = check_bundle(g, root, bundle, errors) if bundle else 0
    g.check_names(root, build, binaries, errors)
    for e in errors:
        print('licence gate: ' + e)
    if errors:
        print('licence gate: %d errors' % len(errors))
        sys.exit(1)
    print('licence gate: %d files, %d compile commands, %s and %d binaries pass'
          % (files, commands, 'the bundle\'s %d files' % held if bundle else 'no bundle', len(binaries)))


if __name__ == '__main__':
    main()
