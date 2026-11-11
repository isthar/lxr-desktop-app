#!/usr/bin/env python3
"""The differential fuzz of the pattern generator (E10.17): random cases through the firmware's
core (Core/Src/Soundengine/Sequencer/PatternGen.c, built as the host-test executable
pattern_gen_fuzz by tests/CMakeLists.txt) and through the prototype (tools/gen/gen.py), which
must agree on every byte of the track, the return value and the steps written (spec 11).

    fuzz_c.py [--focus all|hat|sub|prc|rar|prp] [--masks] DRIVER N SEED

N cases from the seed SEED. A case is random settings (sometimes wild bytes past the ranges),
a destination track, a source (none, random, or the destination itself) and a current track
with random lengths, velocities, conditions, p-locks, timing and fine nibbles, and a mask of
the steps the last run wrote: none, random bits, some of the current track's steps, all, or a
chain (the current track and its mask are what the prototype wrote in a run just before, with
other settings). The driver also answers the settings that choosing the case's typ loads on that
track and mask (patternGen_typeDefaults(), gen.py apply_type_defaults(), spec 11). --masks gives every case a mask (but a chain whose first run wrote nothing)
and style 0 more often. The summary counts the cases with a mask and those where it applies (hat
or prc style 0). The ctest id pgen_fuzz runs 3000 cases from seed 1. Exit status 1 on a
difference; the first 5 are printed.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of the
repository.
"""
import argparse
import dataclasses
import os
import random
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen  # noqa: E402

STEP_BYTES = gen.NUM_STEPS * 7
OUT = 1 + 1 + 8 + STEP_BYTES + 64 + 32 + 8 + 9
ALL = (1 << 64) - 1


class Fuzz:
    def __init__(self, seed, focus, masks):
        self.r = random.Random(seed)
        self.focus, self.masks = focus, masks

    def track(self):
        r = self.r
        steps = bytearray()
        for _ in range(gen.NUM_STEPS):
            vol = r.choice([127, 90, 40, 64, 63, 0, r.randint(0, 255)])
            prob = r.choice([100, 100, 101, 102, 109, 25, 0x80 | 102, 0x80 | 100, r.randint(0, 255)])
            note = r.choice([60, 60, 0, 127, 5, 120, r.randint(0, 127)]) if r.random() < .9 else r.randint(128, 255)
            locks = [r.randint(0, 255) for _ in range(4)] if r.random() < .3 else [0xFF, 0, 0xFF, 0]
            steps += bytes([vol, prob, note] + locks)
        dens = r.choice([0.0, 0.1, 0.25, 0.5, 0.9, 1.0])
        return gen.Track.from_json(dict(
            length=r.choice([1, 7, 63, 16, 32, 64, 64, r.randint(1, 64)]),
            active=[sum(1 << b for b in range(gen.BAR) if r.random() < dens) for _ in range(gen.NUM_BARS)],
            steps=steps.hex(),
            timing=bytes(r.randint(0, 255) if r.random() < .3 else 0x70 for _ in range(64)).hex(),
            fine=bytes(r.randint(0, 255) if r.random() < .3 else 0 for _ in range(32)).hex()))

    def settings(self):
        r = self.r
        typ = r.randint(0, 5) if self.focus == 'all' else gen.TYP_NAMES.index(self.focus)
        if r.random() < 0.15:                    # wild bytes, as a stray NRPN or a corrupt file might
            return gen.Settings(typ=r.choice([typ, r.randint(0, 255)]), sty=r.randint(0, 255), den=r.randint(0, 255),
                                lev=r.randint(-128, 127), var=r.randint(-128, 127), not_=r.randint(-128, 127),
                                sed=r.randint(0, 255), src=r.randint(1, 7), shf=r.randint(-128, 127))
        end = lambda lo, hi: r.choice([lo, hi, r.randint(lo, hi)])  # noqa: E731
        sty = 0 if r.random() < (0.5 if self.masks else 0.25) else r.randint(0, 8)
        return gen.Settings(typ=typ, sty=sty, den=end(0, 16), lev=end(-30, 0), var=end(-4, 4), not_=end(-24, 24),
                            sed=r.randint(0, 99), src=r.randint(1, 7), shf=end(-8, 8))

    def case(self):
        r = self.r
        s = self.settings()
        track = r.randint(0, 6)
        if r.random() < 0.2:
            s = dataclasses.replace(s, src=track + 1)
        cur = self.track()
        has = r.choice([0, 1, 1, 1])
        src = None
        if has and s.src - 1 == track:
            has = 2                              # the source is the destination itself
        elif has:
            src = self.track()
        kind = r.choice(['random', 'some', 'all', 'chain', 'chain'] + ([] if self.masks else ['none'] * 3))
        own = 0
        if kind == 'random':
            own = r.getrandbits(64)
        elif kind == 'some':
            own = sum(1 << i for i in range(gen.NUM_STEPS) if r.random() < 0.5)
        elif kind == 'all':
            own = ALL
        elif kind == 'chain':                     # what a run with other settings wrote just before
            first = dataclasses.replace(self.settings(), typ=s.typ, src=s.src)
            cur = gen.generate(first, track, cur if has == 2 else src, cur, r.choice([0, r.getrandbits(64)]))
            own = cur.written
        return s, track, has, src, cur, own


def pack(t, full):
    d = t.to_json()
    b = bytes([d['length']]) + struct.pack('<4H', *d['active']) + bytes.fromhex(d['steps'])
    return b + bytes.fromhex(d['timing']) + bytes.fromhex(d['fine']) if full else b


def settings_bytes(s):
    return bytes(v & 0xFF for v in (s.typ, s.sty, s.den, s.lev, s.var, s.not_, s.sed, s.src - 1, s.shf))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--focus', default='all', choices=('all',) + gen.TYP_NAMES[1:])
    ap.add_argument('--masks', action='store_true', help='a mask in every case, style 0 more often')
    ap.add_argument('driver')
    ap.add_argument('n', type=int)
    ap.add_argument('seed', type=int)
    a = ap.parse_args(argv)
    fz = Fuzz(a.seed, a.focus, a.masks)
    cases, recs = [], bytearray()
    for _ in range(a.n):
        s, track, has, src, cur, own = fz.case()
        recs += settings_bytes(s) + bytes([track, has])
        recs += pack(src, False) if has == 1 else bytes(1 + 8 + STEP_BYTES)
        recs += pack(cur, True) + struct.pack('<Q', own)
        cases.append((s, track, has, src, cur, own))
    out = subprocess.run([a.driver], input=bytes(recs), capture_output=True, check=True).stdout
    if len(out) != OUT * a.n:
        sys.exit('fuzz_c.py: the driver answered %d bytes, not %d' % (len(out), OUT * a.n))
    sv = lambda v: v - 256 if v >= 128 else v  # noqa: E731
    bad = masked = applies = 0
    for n, (s, track, has, src, cur, own) in enumerate(cases):
        o = out[n * OUT:(n + 1) * OUT]
        c = dict(length=o[1], active=list(struct.unpack('<4H', o[2:10])), steps=o[10:458].hex(),
                 timing=o[458:522].hex(), fine=o[522:554].hex())
        c_own = struct.unpack('<Q', o[554:562])[0]
        c_set = o[562:571]
        # the settings as the core sees its bytes: typ sty den sed unsigned, lev var not shf signed
        g_s = gen.Settings(typ=s.typ & 0xFF, sty=s.sty & 0xFF, den=s.den & 0xFF, lev=sv(s.lev & 0xFF),
                           var=sv(s.var & 0xFF), not_=sv(s.not_ & 0xFF), sed=s.sed & 0xFF, src=s.src,
                           shf=sv(s.shf & 0xFF))
        g_set = settings_bytes(gen.apply_type_defaults(g_s, g_s.typ, cur, own))
        g = gen.generate(g_s, track, cur if has == 2 else src, cur, own)
        k = g_s.clamped()
        wrote = not (k.typ == gen.TYP_OFF or (k.typ == gen.TYP_PRP and k.src - 1 == track))
        masked += own != 0
        applies += own != 0 and k.typ in (gen.TYP_HAT, gen.TYP_PRC) and k.sty == 0
        if (g.to_json() == c and bool(o[0] & 2) == g.needs_vol and bool(o[0] & 1) == wrote and c_own == g.written
                and c_set == g_set):
            continue
        bad += 1
        if bad <= 5:
            print('DIFFERENT case %d: %s track %d has %d own %016x: return %d, C own %016x, gen.py own %016x,'
                  ' type defaults C %s gen.py %s' % (n, g_s, track, has, own, o[0], c_own, g.written, list(c_set),
                                                     list(g_set)))
            for key, v in g.to_json().items():
                if v != c[key]:
                    print('   %s: gen.py %s, C %s' % (key, v, c[key]))
    print('fuzz_c.py: %d cases (%s%s, seed %d; %d with a mask, %d of them on hat or prc style 0), %d different'
          % (a.n, a.focus, ', masks' if a.masks else '', a.seed, masked, applies, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
