#!/usr/bin/env python3
"""P11: the licence gate and the name scan of the plugin's build (plan E9.2 Task 10), the
emulator's M21 (emu/tests/licence_gate.py) with these additions:

- the vendored VST 3 SDK, plugin/third_party/vst3sdk/, is allowed (MIT; its LICENSE.txt goes into
  the bundle's Contents/Resources);
- the SDK's own compile commands need not pass the main.h overlay: they include no firmware
  header. Every other command of the build must;
- with --bundle, the bundle holds exactly its files: the plugin and the engine, Info.plist and
  moduleinfo.json (whose texts name no maker), and in Contents/Resources the repository's LICENSE
  and NOTICE.md and the SDK's MIT licence as VST3-SDK-LICENSE.txt, each byte for byte its source.

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
# the bundle's files, and for the copied ones their source in the repository
BUNDLE = {
    'Contents/Info.plist': None,
    'Contents/MacOS/LICHOMAT': None,
    'Contents/Helpers/lichomat-engine': None,
    'Contents/Resources/moduleinfo.json': None,
    'Contents/Resources/LICENSE': 'LICENSE',
    'Contents/Resources/NOTICE.md': 'NOTICE.md',
    'Contents/Resources/VST3-SDK-LICENSE.txt': SDK + 'LICENSE.txt',
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
    for d, _, names in os.walk(bundle):
        for n in names:
            found.add(os.path.relpath(os.path.join(d, n), bundle).replace(os.sep, '/'))
    for f in sorted(found - set(BUNDLE)):
        errors.append('the bundle holds %s, which is not one of its files' % f)
    for f in sorted(set(BUNDLE) - found):
        errors.append('the bundle has no %s' % f)
    for f, src in sorted(BUNDLE.items()):
        if src is None or f not in found:
            continue
        with open(os.path.join(bundle, f), 'rb') as a, open(os.path.join(root, src), 'rb') as b:
            if a.read() != b.read():
                errors.append('the bundle\'s %s is not %s' % (f, src))
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
