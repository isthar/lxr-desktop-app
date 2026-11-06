#!/bin/bash
# Copy the part of Apple's AudioUnitSDK that the AU builds with into plugin/third_party/AudioUnitSDK
# (plan E9.7 A-3): apple/AudioUnitSDK at the tag AudioUnitSDK-1.4.0, Apache-2.0.
#
#   plugin/tools/vendor_ausdk.sh /path/to/AudioUnitSDK
#
# The checkout must be that commit. Only its headers, its sources and its licence are copied: not
# its Xcode project, demos, tests, scripts or git hooks, none of which is run. The copy keeps the
# SDK's paths; SHA256SUMS lists every file.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -euo pipefail

SRC="${1:?usage: vendor_ausdk.sh /path/to/AudioUnitSDK}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DST="$ROOT/plugin/third_party/AudioUnitSDK"
WANT=bd98b31feff57a15989fcfab4cd86dc63382b1ac

got="$(git -C "$SRC" rev-parse HEAD)"
if [ "$got" != "$WANT" ]; then
	echo "vendor_ausdk: $SRC is at $got, not $WANT (AudioUnitSDK-1.4.0)" >&2
	exit 1
fi
rm -rf "$DST"
mkdir -p "$DST"
(cd "$SRC" && tar -cf - LICENSE.txt include/AudioUnitSDK src/AudioUnitSDK) | tar -xf - -C "$DST"
(cd "$DST" && find . -type f ! -name SHA256SUMS | LC_ALL=C sort | sed 's|^\./||' | xargs shasum -a 256 > SHA256SUMS)
echo "vendor_ausdk: $(wc -l < "$DST/SHA256SUMS" | tr -d ' ') files, $(du -sk "$DST" | cut -f1) KB in plugin/third_party/AudioUnitSDK"
