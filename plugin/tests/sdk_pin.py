#!/usr/bin/env python3
"""P2: a vendored SDK is the pinned subset (plan E9.2 Task 2): every file of
plugin/third_party/<sdk> is listed in its SHA256SUMS with that hash, and SHA256SUMS lists no file
that is missing. plugin/tools/vendor_vst3sdk.sh made those of vst3sdk from Steinberg's vst3sdk at
v3.8.1_build_84 (commit 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96), and (plan E9.7)
plugin/tools/vendor_ausdk.sh those of AudioUnitSDK from Apple's AudioUnitSDK at AudioUnitSDK-1.4.0
(commit bd98b31feff57a15989fcfab4cd86dc63382b1ac).

    sdk_pin.py <repository root> [vst3sdk | AudioUnitSDK]

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import hashlib
import os
import sys


NAMES = {'vst3sdk': 'the VST 3 SDK subset', 'AudioUnitSDK': 'the AudioUnitSDK subset'}


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] not in NAMES):
        raise SystemExit(__doc__)
    sdk = sys.argv[2] if len(sys.argv) == 3 else 'vst3sdk'
    root = os.path.join(sys.argv[1], 'plugin', 'third_party', sdk)
    want = {}
    with open(os.path.join(root, 'SHA256SUMS')) as f:
        for line in f:
            digest, path = line.rstrip('\n').split('  ', 1)
            want[path] = digest
    have = {}
    for d, _, files in os.walk(root):
        for name in files:
            path = os.path.relpath(os.path.join(d, name), root)
            if path == 'SHA256SUMS':
                continue
            with open(os.path.join(d, name), 'rb') as f:
                have[path] = hashlib.sha256(f.read()).hexdigest()
    errors = ['%s is not in SHA256SUMS' % p for p in sorted(set(have) - set(want))]
    errors += ['%s is missing' % p for p in sorted(set(want) - set(have))]
    errors += ['%s has changed' % p for p in sorted(want) if p in have and have[p] != want[p]]
    for e in errors:
        print('sdk pin: ' + e)
    if errors:
        print('sdk pin: %d errors' % len(errors))
        sys.exit(1)
    print('sdk pin: the %d files of %s match SHA256SUMS' % (len(have), NAMES[sdk]))


if __name__ == '__main__':
    main()
