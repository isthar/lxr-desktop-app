#!/usr/bin/env python3
"""Check the SD-card update file of a LICHOMAT firmware build.

    python3 tools/check_image.py [BUILD_DIR] [--name NAME] [--version VERSION]

BUILD_DIR defaults to build/release. NAME and VERSION default to LICHOMAT_NAME and
CMAKE_PROJECT_VERSION from BUILD_DIR/CMakeCache.txt. The check passes when:
- BUILD_DIR holds exactly one update file, LXR_V2_<NAME>_<VERSION>.img. The
  bootloader installs the first LXR*.img it finds, so a stale image next to it
  is an error;
- its 16-byte header (see cmake/tools/mkimage.py) has the desktop magic LXRV2IMG
  and the size and checksum of LXR_V2_STM32F765.bin, and the .bin follows it
  unchanged;
- the .bin holds both splash screen lines as C strings, "<NAME>" and
  "v<VERSION> unofficial", and each fits the 16 characters of the LCD.

Exit status 0 if the check passes, 1 if not.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import argparse
import os
import re
import struct
import sys

MAGIC = b"LXRV2IMG"
HEADER = "<8sIB3x"          # magic, payload size, checksum, padding
LCD_COLUMNS = 16
BIN_NAME = "LXR_V2_STM32F765.bin"


def cache_value(build_dir, key):
    """value of KEY in BUILD_DIR/CMakeCache.txt, or None"""
    try:
        with open(os.path.join(build_dir, "CMakeCache.txt"), encoding="utf-8") as f:
            for line in f:
                m = re.match(r"%s:[A-Z]+=(.*)$" % re.escape(key), line.rstrip("\r\n"))
                if m:
                    return m.group(1)
    except OSError:
        pass
    return None


def check(build_dir, name, version):
    """the problems found (none if the image is fine), the expected image name and the firmware size"""
    problems = []
    image_name = "LXR_V2_%s_%s.img" % (name, version)
    images = sorted(f for f in os.listdir(build_dir) if f.endswith(".img"))
    if images != [image_name]:
        problems.append("expected exactly one update file %s in %s, found: %s"
                        % (image_name, build_dir, ", ".join(images) or "none"))

    with open(os.path.join(build_dir, BIN_NAME), "rb") as f:
        payload = f.read()
    if image_name in images:
        with open(os.path.join(build_dir, image_name), "rb") as f:
            image = f.read()
        magic, size, checksum = struct.unpack(HEADER, image[:struct.calcsize(HEADER)])
        if magic != MAGIC:
            problems.append("%s: magic %r, expected %r" % (image_name, magic, MAGIC))
        if size != len(payload) or image[struct.calcsize(HEADER):] != payload:
            problems.append("%s does not contain %s (%d bytes)" % (image_name, BIN_NAME, len(payload)))
        if checksum != 0xFF - (sum(payload) & 0xFF):
            problems.append("%s: wrong checksum 0x%02X" % (image_name, checksum))

    for line in (name, "v%s unofficial" % version):
        if len(line) > LCD_COLUMNS:
            problems.append("splash line '%s' is longer than %d characters" % (line, LCD_COLUMNS))
        if line.encode("ascii") + b"\0" not in payload:
            problems.append("splash line '%s' not found in %s" % (line, BIN_NAME))
    return problems, image_name, len(payload)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("build_dir", nargs="?", default="build/release")
    ap.add_argument("--name", help="expected LICHOMAT_NAME (default: from CMakeCache.txt)")
    ap.add_argument("--version", help="expected project version (default: from CMakeCache.txt)")
    args = ap.parse_args()

    name = args.name or cache_value(args.build_dir, "LICHOMAT_NAME")
    version = args.version or cache_value(args.build_dir, "CMAKE_PROJECT_VERSION")
    if not name or not version:
        print("image check FAILED: no LICHOMAT_NAME or CMAKE_PROJECT_VERSION in %s/CMakeCache.txt"
              % args.build_dir)
        return 1

    problems, image_name, size = check(args.build_dir, name, version)
    for p in problems:
        print("image check FAILED: %s" % p)
    if problems:
        return 1
    print('image: %s OK (%d bytes of firmware, magic %s, splash "%s" / "v%s unofficial")'
          % (image_name, size, MAGIC.decode(), name, version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
