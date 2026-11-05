#!/bin/bash
# Copy the parts of the VST 3 SDK that the plugin builds with into plugin/third_party/vst3sdk
# (plan E9.2 P-3): Steinberg's steinbergmedia/vst3sdk at the tag v3.8.1_build_84, MIT.
#
#   plugin/tools/vendor_vst3sdk.sh /path/to/vst3sdk
#
# The checkout must be that commit, with its submodules base, pluginterfaces, public.sdk and
# cmake at the commits below. The copy keeps the SDK's paths; SHA256SUMS lists every file.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -euo pipefail

SRC="${1:?usage: vendor_vst3sdk.sh /path/to/vst3sdk}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DST="$ROOT/plugin/third_party/vst3sdk"

want() {
	local got
	got="$(git -C "$SRC/$1" rev-parse HEAD)"
	if [ "$got" != "$2" ]; then
		echo "vendor_vst3sdk: $SRC/$1 is at $got, not $2" >&2
		exit 1
	fi
}
want . 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96
want base fcf9da0bd27a16f7f03773a3a39822f28f5c8477
want pluginterfaces 4f547e8e102b47de4a8b8aaf343c73b700786372
want public.sdk 586dc5e6c8012c3e4b01c79389375cbe96bdb1da
want cmake 054c9143cbb8d47fc4694e473f2ee3b4d951a8f5

PARTS=(
	LICENSE.txt README.md
	base pluginterfaces cmake/modules cmake/templates
	public.sdk/LICENSE.txt public.sdk/README.md
	public.sdk/source/main public.sdk/source/common
	public.sdk/source/vst/hosting public.sdk/source/vst/utility public.sdk/source/vst/moduleinfo
	public.sdk/source/vst/testsuite
	public.sdk/samples/vst-hosting/validator public.sdk/samples/vst-utilities/moduleinfotool
	public.sdk/samples/vst-hosting/audiohost/source/media/miditovst.h
)
rm -rf "$DST"
mkdir -p "$DST"
(cd "$SRC" && tar --exclude .git --exclude .gitignore --exclude .github --exclude VST_Logo_Steinberg.ico -cf - "${PARTS[@]}" public.sdk/source/vst/*.cpp public.sdk/source/vst/*.h) | tar -xf - -C "$DST"
(cd "$DST" && find . -type f ! -name SHA256SUMS | LC_ALL=C sort | sed 's|^\./||' | xargs shasum -a 256 > SHA256SUMS)
echo "vendor_vst3sdk: $(wc -l < "$DST/SHA256SUMS" | tr -d ' ') files, $(du -sk "$DST" | cut -f1) KB in plugin/third_party/vst3sdk"
