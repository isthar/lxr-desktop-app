#!/usr/bin/env python3
"""Compare two firmware ELFs, allowing the linker's veneers to change order:

    python3 tools/elf_equiv.py <objdump> <objcopy> <base.elf> <after.elf> <work dir>

The code sections (.text, .itcm_text) are disassembled without addresses, with branch and literal
targets kept as symbol names, and compared function by function, so the order of the functions and
the long-branch veneers does not count. Every other allocated section is compared byte for byte.
The last line is EQUIVALENT or NOT EQUIVALENT, and the exit code is 0 or 1. Stage 1b spec §7 F10
uses it: a new function's input section can shift the ids GNU ld names its veneers after.
"""
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
import re
import subprocess
import sys
from pathlib import Path

if len(sys.argv) != 6:
    sys.exit(__doc__)
objdump, objcopy, base, after, work = sys.argv[1:6]
work = Path(work)
CODE = (".text", ".itcm_text")


def functions(elf):
    out = subprocess.run([objdump, "-d", "--no-show-raw-insn", "-j", ".text", "-j", ".itcm_text", elf],
                         check=True, capture_output=True, text=True).stdout
    funcs, name = {}, None
    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            name = m.group(1)
            funcs.setdefault(name, [])
            continue
        if name is None or not line.strip() or line.startswith("Disassembly"):
            continue
        line = re.sub(r"^\s*[0-9a-f]+:\s*", "", line)        # the instruction's address
        line = re.sub(r"\b[0-9a-f]+ <([^>]+)>", r"<\1>", line)  # a target's address
        funcs[name].append(line)
    return funcs


def sections(elf):
    out = subprocess.run([objdump, "-h", elf], check=True, capture_output=True, text=True).stdout
    names, lines = [], out.splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)", line)
        if m and i + 1 < len(lines) and "ALLOC" in lines[i + 1] and int(m.group(2), 16):
            names.append(m.group(1))
    return names


fb, fa = functions(base), functions(after)
ok = True
if fb.keys() != fa.keys():
    ok = False
    print("functions only in base:", sorted(fb.keys() - fa.keys()))
    print("functions only in after:", sorted(fa.keys() - fb.keys()))
diff = [n for n in fb.keys() & fa.keys() if fb[n] != fa[n]]
for n in sorted(diff)[:10]:
    print("code differs:", n)
ok = ok and not diff
print(f"code: {len(fb)} functions and veneers compared, {len(diff)} differ")

sb, sa = sections(base), sections(after)
if sb != sa:
    ok = False
    print("section lists differ:", sb, sa)
work.mkdir(parents=True, exist_ok=True)
for s in sb:
    if s in CODE:
        continue
    pb, pa = work / f"base{s}.bin", work / f"after{s}.bin"
    for elf, p in ((base, pb), (after, pa)):
        subprocess.run([objcopy, "-O", "binary", "--only-section=" + s, elf, str(p)], check=True)
    same = pb.read_bytes() == pa.read_bytes()
    ok = ok and same
    print(f"section {s}: {pb.stat().st_size} B {'identical' if same else 'DIFFERS'}")
print("EQUIVALENT" if ok else "NOT EQUIVALENT")
sys.exit(0 if ok else 1)
