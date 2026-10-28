#!/bin/bash
# M19: the firmware imports no libm function but the vendored ones (stage 1b
# spec 8.1, 12.1).
#
#   check_libm.sh <work dir> <firmware objects> <core objects> <libm objects> <canary.o> <device binary>
#
# The three object arguments are files that list one object per line (CMake's
# $<TARGET_OBJECTS>). The firmware objects are linked into one relocatable
# object; its undefined symbols, minus those that emu/core and emu/libm define,
# must be on the allow-list below. The canary, compiled with the firmware's
# flags, calls expf() of a constant and sinf() and cosf() of one argument: it
# must import those names, not ___sincosf_stret, and the device binary must
# define them.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
set -u -o pipefail
work=$1 fw=$2 core=$3 libm=$4 canary=$5 device=$6
mkdir -p "$work"
status=0

# libc's string and memory functions with their _chk forms, atoi, bzero, fmod,
# the stack checks, and the sanitizer runtimes
allow='^_(fmod|atoi|bzero|mem[a-z0-9_]*|str[a-z]*|v?sn?printf)$|^___[a-z0-9_]*_chk$|^___stack_chk_(fail|guard)$|^___chkstk_darwin$|^___(ubsan|asan)_'

ld -r -o "$work/firmware.o" $(cat "$fw") || exit 1
nm -u "$work/firmware.o" | sort -u > "$work/undefined.txt"
nm -gU $(cat "$core" "$libm") 2>/dev/null | awk 'NF == 3 { print $3 }' | sort -u > "$work/defined.txt"
comm -23 "$work/undefined.txt" "$work/defined.txt" > "$work/imports.txt"
bad=$(grep -Ev "$allow" "$work/imports.txt" || true)
if [ -n "$bad" ]; then
  echo "M19: the firmware imports names off the allow-list:"; echo "$bad"; status=1
fi
# nm's output goes to files first: under pipefail, grep -q leaving a pipe early
# kills nm with SIGPIPE and fails the test although the name is there
nm -u "$canary" > "$work/canary.txt" || exit 1
nm -gU "$device" > "$work/device.txt" || exit 1
for f in _expf _sinf _cosf; do
  grep -qx "$f" "$work/canary.txt" || { echo "M19: the canary does not import $f"; status=1; }
  grep -q " T $f\$" "$work/device.txt" || { echo "M19: $(basename "$device") does not define $f"; status=1; }
done
if grep -q 'sincos' "$work/canary.txt"; then
  echo "M19: the canary imports a sincos function"; status=1
fi
[ $status -eq 0 ] && echo "M19: $(wc -l < "$work/imports.txt" | tr -d ' ') imports, all on the allow-list; the canary imports the vendored names"
exit $status
