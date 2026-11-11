#!/usr/bin/env python3
"""The golden vectors of the pattern generator (E10.2) for the firmware port (E10.3):
about 200 combinations of settings, seeds and input tracks with the track that
tools/gen/gen.py writes for each, in tools/gen/vectors.json.

    python3 tools/gen/vectors.py [OUT.json]

The JSON (see "format" in it): the dB -> velocity table, the PRNG's state and first
draws for a few seeds, and the cases. A track is
  length   1-64
  active   4 words, bar 1-4: bit n is step n of the bar (seq_mainSteps)
  steps    64 x 7 bytes in hex: volume, prob, note, param1Nr, param1Val, param2Nr, param2Val
  timing   64 bytes in hex: StepTiming, flam in bits 0-3, shift in bits 4-7 (7 = no shift)
  fine     32 bytes in hex: step 2i in bits 0-3, step 2i+1 in bits 4-7, signed -6..+5
A case: settings (typ by name, or as a number past prp, which is '---'; `not` as in the
menu), track (the destination, 0-6),
src (the content of track settings.src, or null: a cleared track), cur (the
destination's content now, or null), own (optional: the steps of cur that the last run
wrote, spec 11, as 4 words like active; absent is none), out (what the generator writes),
vol (whether it wrote a velocity under 127, so the voice's vol must be on), own_out (the
steps it wrote, 4 words: the next run's own).

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of the
repository.
"""
import dataclasses
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen  # noqa: E402
from gen import Settings, Step, Track, apply_type_defaults, generate  # noqa: E402

FORMAT = ('track: length 1-64; active: 4 words, bar 1-4, bit n = step n of the bar; steps: 64 x 7 bytes '
          'hex (volume prob note param1Nr param1Val param2Nr param2Val); timing: 64 bytes hex, flam bits '
          '0-3, shift bits 4-7 (7 = none); fine: 32 bytes hex, step 2i bits 0-3, 2i+1 bits 4-7, signed. '
          'case: settings (typ by name, or a number past prp, which is ---), track 0-6, src = content of track settings.src (null: cleared, '
          'length 16), cur = the destination now (null: cleared), own (optional, absent = none) = the steps of cur '
          'the last run wrote, as 4 words like active (spec 11: no accents of sty 0), out, vol = the voice needs vol '
          'on, own_out = the steps out wrote, 4 words (the next run\'s own). '
          'prng: Rng(sed, typ, sty, track) state and its next draws (xorshift32). vel_table[d] = vel at -d dB.')


def kick44():
    return Track.from_pattern('X...X...X...X...')


def kick_odd():
    return Track.from_pattern('X..X..X...X.X...')


def kick16():
    """a 4/4 kick with one more on step 16, the first place var +n takes on sub"""
    return Track.from_pattern('X...X...X...X..X')


def kick64():
    """a 64-step kick: 4/4 with an extra kick on step 15 of bar 4 (only bar 1 counts)"""
    t = Track.from_pattern('X...X...X...X...' * 3 + 'X...X...X...X.X.')
    return t


def user_hats64():
    t = Track(length=64)
    for i, v in ((2, 127), (6, 127), (10, 100), (14, 127), (18, 127), (22, 90), (26, 127), (30, 127),
                 (34, 127), (38, 127), (42, 64), (46, 127), (50, 127), (54, 127), (58, 127), (61, 110)):
        t.active[i] = True
        t.steps[i] = Step(vol=v, prob=102 if i == 61 else 100, note=60 + (i % 3), p1nr=40, p1val=i,
                          flam=1 if i == 30 else 0, shift=8 if i == 10 else 7, fine=2 if i == 14 else 0)
    for i in (1, 5, 13, 33, 49):
        t.active[i] = True
        t.steps[i].vol = 25
    return t


def user_hats16():
    t = Track.from_pattern('..X...X..o.x..X.')
    t.steps[9].p2nr, t.steps[9].p2val = 77, 5
    return t


def user_prc32():
    t = Track.from_pattern('...X..X.x..X....|..xX...X...X.X..')
    t.steps[3].prob = 104
    t.steps[23].fine = -6
    return t


def kick_even():
    """a kick off step 1: steps 2 6 10 14, so prc's styles 3-4 can put an accent on step 1"""
    return Track.from_pattern('.X...X...X...X..')


def no_accents():
    """ghosts only (vel 40 and 63), no step at vel >= 64"""
    t = Track.from_pattern('.x.x....x..x.x.x')
    t.steps[8].vol = 63
    return t


def accent_past_length():
    """16 steps; the accent on step 21 (a p-lock on it) does not play"""
    t = Track.from_pattern('...x...........x')
    t.active[20] = True
    t.steps[20].p1nr, t.steps[20].p1val = 33, 3
    return t


def step1_accents16():
    """the user's accents on steps 1 and 7"""
    return Track.from_pattern('X.....X.........')


def step1_accents64():
    """64 steps: accents on step 1 of bars 2 and 4 (one 90, one 2:2), others elsewhere"""
    t = Track.from_pattern('..X...X.....X...|X.....X...X.....|...X.......X..X.|X..o....X.....X.')
    t.steps[48].prob = 102
    return t


def raw_fine(length):
    """accents with the raw fine nibbles 0x9 (-7, which the firmware's stepFine_get() reads as
    0) on step 3 and 0xE (-2) on step 8, a p-lock and a shift; a ghost with 0x9 on step 12"""
    t = Track.from_pattern(('..X....X...x....' * 4)[:length])
    for b in range(length // 16):
        t.steps[16 * b + 2] = Step(vol=127, prob=100, note=62, p1nr=12, p1val=b, shift=9, fine=-7)
        t.steps[16 * b + 7] = Step(vol=100, prob=104 if b == 1 else 100, note=60, fine=-2)
        t.steps[16 * b + 11].fine = -7
    return t


def accent_in_bar3():
    """64 steps whose only accent is step 37 (bar 3), with a p-lock, a shift and fine -3; ghosts
    at 30 on steps 2, 19 and 51: style 0 stays style 0 (spec 9 looks at the whole track)"""
    t = Track(length=64)
    t.active[36] = True
    t.steps[36] = Step(vol=100, prob=102, note=55, p1nr=12, p1val=7, shift=9, fine=-3)
    for i in (1, 18, 50):
        t.active[i] = True
        t.steps[i].vol = 30
    return t


def legacy_bit7(accent):
    """steps at 0x80 | 40 (bit 7, the legacy active flag: they play 40, no accent) on 6 and 22,
    and with accent a real one at 64 on step 2"""
    t = Track(length=32)
    for i in (5, 21):
        t.active[i] = True
        t.steps[i].vol = 0x80 | 40
    if accent:
        t.active[1] = True
        t.steps[1].vol = 64
    return t


def prc_source():
    return generate(apply_type_defaults(Settings(sed=5), 'prc'), 2, kick44())


def short_source():
    """12 steps: velocities, notes, a condition, a probability and a slide flag"""
    t = Track.from_pattern('X..x.o..X.X.')
    t.steps[3].note = 63
    t.steps[5].prob = 0x80 | 50
    t.steps[8].prob = 107
    t.steps[10].note = 48
    return t


MASK_ALL = (1 << 64) - 1


def mask(*steps):
    """a mask of steps 0-63 (bit i is step i)"""
    return sum(1 << i for i in steps)


def cases():
    out = []

    def add(name, typ, track, inp=None, cur=None, own=0, **kw):
        s = apply_type_defaults(Settings(), typ)
        if 'not' in kw:
            kw['not_'] = kw.pop('not')
        s = dataclasses.replace(s, **kw)
        out.append((name, s, track, inp, cur, own))

    def last():
        """the track the last case writes (a run on it next takes its written steps as own)"""
        name, s, track, inp, cur, own = out[-1]
        return generate(s, track, inp, cur, own)

    # --- hat (track 6, index 5; some on 7) ---
    for sty in range(5):
        cur = user_hats16() if sty == 0 else None
        for den in (0, 7, 16):
            add('hat sty%d den%d' % (sty, den), 'hat', 5, cur=cur, sty=sty, den=den)
    for var in (-4, -1, 0, 2, 4):
        add('hat var%+d' % var, 'hat', 5, var=var)
    for lev in (-30, -6, 0):
        add('hat lev%d' % lev, 'hat', 5, lev=lev)
    for n in (-24, 24):
        add('hat not%+d' % n, 'hat', 6, **{'not': n})
    for sty in (1, 3):
        for sed in (1, 50, 99):
            add('hat sty%d sed%d' % (sty, sed), 'hat', 5, sty=sty, sed=sed)
    for var in (-2, 0, 3):
        add('hat sty0 user64 var%+d' % var, 'hat', 5, cur=user_hats64(), sty=0, den=5, var=var)
        add('hat sty0 user16 var%+d' % var, 'hat', 6, cur=user_hats16(), sty=0, den=9, var=var)
    add('hat sty4 den16 var+4', 'hat', 5, sty=4, den=16, var=4)
    add('hat sty2 den5 var-4', 'hat', 5, sty=2, den=5, var=-4)
    add('hat sty7 (clamped to 4)', 'hat', 5, sty=7)

    # --- sub: ghost kicks (track 2, index 1; src track 1) ---
    for sty in range(1, 9):
        add('sub sty%d' % sty, 'sub', 1, inp=kick44(), sty=sty)
        add('sub sty%d kick odd' % sty, 'sub', 1, inp=kick_odd(), sty=sty, sed=3)
    for den in (0, 1, 3, 8, 12):
        for sed in (0, 7, 31):
            add('sub sty8 den%d sed%d' % (den, sed), 'sub', 1, inp=kick44(), sty=8, den=den, sed=sed)
    add('sub sty8 den3 kick odd', 'sub', 1, inp=kick_odd(), sty=8, den=3, sed=11)
    for sty in (1, 4, 6, 7, 8):
        for var in (-4, -2, -1, 1, 2, 4):
            add('sub sty%d var%+d' % (sty, var), 'sub', 1, inp=kick44(), sty=sty, var=var, sed=5)
    add('sub sty1 lev0 var-2', 'sub', 1, inp=kick44(), sty=1, lev=0, var=-2)
    add('sub sty1 var+3 kick odd', 'sub', 1, inp=kick_odd(), sty=1, var=3)
    # the whole-branch review: var -n leaves out the ghosts the kick dropped (kick odd drops 4), var +n
    # never puts a ghost on the kick, also not on the steps it takes first (16 here)
    add('sub sty1 var-2 kick odd', 'sub', 1, inp=kick_odd(), sty=1, var=-2)
    for var in range(1, 5):
        add('sub sty1 var+%d kick16' % var, 'sub', 1, inp=kick16(), sty=1, var=var)
    add('sub sty5 src on itself', 'sub', 1, inp=kick_odd(), cur=kick_odd(), sty=5, src=2)
    add('sub sty3 src empty', 'sub', 1, inp=None, sty=3)
    add('sub sty1 kick64', 'sub', 1, inp=kick64(), sty=1)
    add('sub sty5 kick64', 'sub', 1, inp=kick64(), sty=5)
    for lev in (-30, -8, 0):
        add('sub sty6 lev%d' % lev, 'sub', 1, inp=kick44(), sty=6, lev=lev)
    for n in (-24, 24):
        add('sub sty3 not%+d var+4' % n, 'sub', 1, inp=kick44(), sty=3, var=4, **{'not': n})
    add('sub sty0 (clamped to 1)', 'sub', 1, inp=kick44(), sty=0)
    add('sub sty8 track 6', 'sub', 5, inp=kick44(), sty=8, sed=21)
    add('sub sty2 track 3', 'sub', 2, inp=kick44(), sty=2, var=-1)
    add('sub sty8 own ignored', 'sub', 1, inp=kick44(), cur=user_hats64(), own=MASK_ALL, sty=8, var=2)

    # --- prc (track 3, index 2) ---
    for sty in range(1, 5):
        for den in (0, 1, 3, 6, 16):
            add('prc sty%d den%d' % (sty, den), 'prc', 2, inp=kick44(), sty=sty, den=den)
    for sty in (1, 3):
        for var in (-4, -1, 1, 4):
            add('prc sty%d var%+d' % (sty, var), 'prc', 2, inp=kick44(), sty=sty, var=var)
    for var in (-2, 0, 2):
        add('prc sty0 user32 var%+d' % var, 'prc', 2, inp=kick44(), cur=user_prc32(), sty=0, den=4, var=var)
        add('prc sty0 user64 var%+d' % var, 'prc', 3, inp=kick44(), cur=user_hats64(), sty=0, den=6, var=var)
    for sty in range(1, 5):
        add('prc sty%d kick odd' % sty, 'prc', 2, inp=kick_odd(), sty=sty, den=4, var=2)
    for sed in (1, 33, 99):
        add('prc sty2 sed%d' % sed, 'prc', 2, inp=kick44(), sty=2, sed=sed, den=4)
    for lev in (-30, 0):
        add('prc sty4 lev%d' % lev, 'prc', 2, inp=kick44(), sty=4, lev=lev, den=5)
    add('prc sty1 src empty', 'prc', 2, inp=None, den=5)
    add('prc sty3 src on itself', 'prc', 2, inp=kick_odd(), cur=kick_odd(), sty=3, src=3)
    add('prc sty1 den12 var+4 (no room)', 'prc', 2, inp=kick44(), den=12, var=4)
    add('prc sty4 den16 var-4', 'prc', 2, inp=kick44(), sty=4, den=16, var=-4)
    # spec 9: style 0 without accents is style 1, seeded with sty 0
    add('hat sty0 cleared (as sty1)', 'hat', 5, sty=0)
    add('hat sty0 ghosts only var-2', 'hat', 5, cur=no_accents(), sty=0, den=5, var=-2)
    add('hat sty0 accent past length den16 var+3', 'hat', 6, cur=accent_past_length(), sty=0, den=16, var=3)
    add('prc sty0 cleared (as sty1) var-2', 'prc', 2, inp=kick44(), sty=0, den=4, var=-2)
    add('prc sty0 ghosts only var+2', 'prc', 2, inp=kick_odd(), cur=no_accents(), sty=0, den=3, var=2)
    add('prc sty0 accent past length var-1', 'prc', 3, inp=kick44(), cur=accent_past_length(), sty=0, den=6, var=-1)
    add('hat sty0 only accent step 37', 'hat', 5, cur=accent_in_bar3(), sty=0, den=7, var=1)
    add('prc sty0 only accent step 37 var+1', 'prc', 2, inp=kick44(), cur=accent_in_bar3(), sty=0, den=3, var=1)
    add('hat sty0 legacy bit 7 alone', 'hat', 5, cur=legacy_bit7(False), sty=0, den=6, var=0)
    add('prc sty0 legacy bit 7 next to an accent', 'prc', 2, inp=kick_even(), cur=legacy_bit7(True), sty=0, den=2, var=0)
    # spec 9: no pickup ghost before an accent on step 1 of a bar
    add('prc sty3 kick even den4', 'prc', 2, inp=kick_even(), sty=3, den=4, var=0)
    add('prc sty4 kick even den4 sed1', 'prc', 2, inp=kick_even(), sty=4, den=4, sed=1, var=0)
    add('prc sty0 step1 user16 den3', 'prc', 2, inp=kick44(), cur=step1_accents16(), sty=0, den=3, var=0)
    add('prc sty0 step1 user64 den6 var+1', 'prc', 2, inp=kick_even(), cur=step1_accents64(), sty=0, den=6, var=1)
    # spec 9 (the core review): style 0 keeps an accent's raw fine nibble 0x9 and 0xE
    add('hat sty0 raw fine 16', 'hat', 5, cur=raw_fine(16), sty=0, den=6, var=0)
    add('hat sty0 raw fine 64 var-1', 'hat', 5, cur=raw_fine(64), sty=0, den=9, var=-1)
    add('prc sty0 raw fine 16', 'prc', 2, inp=kick44(), cur=raw_fine(16), sty=0, den=4, var=0)
    add('prc sty0 raw fine 64 var+2', 'prc', 2, inp=kick44(), cur=raw_fine(64), sty=0, den=5, var=2)
    # spec 11: the steps the last run wrote (own) are no accents of style 0, nor for spec 9's check
    add('hat sty0 lev-6 user16 sed0', 'hat', 5, cur=user_hats16(), sty=0, lev=-6)
    add('hat sty0 lev-6 own sed1', 'hat', 5, cur=last(), own=last().written, sty=0, lev=-6, sed=1)
    add('hat sty0 lev-4 cleared sed4', 'hat', 5, sty=0, lev=-4, sed=4)
    add('hat sty0 own every accent (as sty1)', 'hat', 5, cur=last(), own=last().written, sty=0, lev=-4, sed=4)
    add('hat sty0 user16 own step 7 and past the length', 'hat', 6, cur=user_hats16(), own=mask(6, 22, 40, 63),
        sty=0, den=9, var=2)
    add('hat sty0 user64 own some accents', 'hat', 5, cur=user_hats64(), own=mask(1, 2, 18, 34, 42, 61),
        sty=0, den=5, var=-2)
    add('hat sty1 own ignored', 'hat', 5, cur=user_hats64(), own=MASK_ALL, var=-1)
    add('prc sty0 var+1 user16 sed0', 'prc', 2, inp=kick44(), cur=Track.from_pattern('......X.......X.'), sty=0)
    add('prc sty0 var+1 own sed5', 'prc', 2, inp=kick44(), cur=last(), own=last().written, sty=0, sed=5)
    add('prc sty0 var+1 own sed0 again', 'prc', 2, inp=kick44(), cur=last(), own=last().written, sty=0)
    add('prc sty0 cleared sed4', 'prc', 2, inp=kick44(), sty=0, sed=4, var=-1)
    add('prc sty0 own every accent (as sty1)', 'prc', 2, inp=kick44(), cur=last(), own=last().written,
        sty=0, sed=4, var=-1)
    add('prc sty0 user64 own bar 3', 'prc', 3, inp=kick44(), cur=user_hats64(), own=0xFFFF << 32, sty=0, den=6, var=1)
    add('prc sty0 user32 own short', 'prc', 2, inp=kick_odd(), cur=user_prc32(), own=mask(3, 11, 35), sty=0, den=4, var=2)
    add('prc sty3 own ignored', 'prc', 2, inp=kick44(), cur=user_prc32(), own=MASK_ALL, sty=3, den=4, var=-2)

    # --- rar (track 5, index 4) ---
    for sty in range(1, 5):
        for den in (0, 1, 2, 3, 4, 5, 8, 16):
            add('rar sty%d den%d' % (sty, den), 'rar', 4, inp=kick44(), sty=sty, den=den)
    for sty in (3, 4):
        add('rar sty%d kick odd' % sty, 'rar', 4, inp=kick_odd(), sty=sty, den=6)
        add('rar sty%d src empty' % sty, 'rar', 4, inp=None, sty=sty, den=6)
    for n in (-24, 24):
        add('rar sty1 den4 not%+d' % n, 'rar', 4, inp=kick44(), den=4, **{'not': n})
    for sed in (2, 64, 99):
        add('rar sty3 den5 sed%d' % sed, 'rar', 4, inp=kick44(), sty=3, den=5, sed=sed)
    add('rar sty8 (clamped to 4)', 'rar', 4, inp=kick44(), sty=8, den=5)
    add('rar sty3 step 49 weighs 2', 'rar', 4, inp=kick_even(), sty=3, den=4, sed=1)
    add('rar sty3 step 48 weighs 1', 'rar', 4, inp=kick_even(), sty=3, den=2, sed=4)
    add('rar sty4 own ignored', 'rar', 4, inp=kick44(), cur=user_hats64(), own=MASK_ALL, sty=4, den=7)

    # --- prp (track 4, index 3; src track 3) ---
    for sty in range(1, 5):
        add('prp sty%d prc source' % sty, 'prp', 3, inp=prc_source(), sty=sty, src=3, shf=3)
        add('prp sty%d short source' % sty, 'prp', 3, inp=short_source(), sty=sty, src=3, shf=-2)
        add('prp sty%d src empty' % sty, 'prp', 3, inp=None, sty=sty, src=3)
    for sty in (1, 4):
        for shf in (-8, -4, 5, 8):
            add('prp sty%d shf%+d' % (sty, shf), 'prp', 3, inp=prc_source(), sty=sty, src=3, shf=shf)
    for den in (0, 1, 16):
        add('prp sty2 den%d' % den, 'prp', 3, inp=prc_source(), sty=2, src=3, den=den)
    for sty in (3, 4):
        for lev in (-30, 0):
            add('prp sty%d lev%d' % (sty, lev), 'prp', 3, inp=prc_source(), sty=sty, src=3, lev=lev, shf=1)
    for n in (-24, 24):
        add('prp sty1 not%+d' % n, 'prp', 3, inp=short_source(), sty=1, src=3, **{'not': n})
    add('prp sty1 src on itself', 'prp', 3, inp=user_hats64(), cur=user_hats64(), sty=1, src=4, shf=2)
    add('prp sty2 sed9 track 7', 'prp', 6, inp=prc_source(), sty=2, src=3, sed=9)
    add('prp sty1 own ignored', 'prp', 3, inp=prc_source(), cur=user_hats64(), own=MASK_ALL, sty=1, src=3, shf=-3)

    # --- off ---
    add('off', 'hat', 5, cur=user_hats64())
    out[-1] = (out[-1][0], dataclasses.replace(out[-1][1], typ=gen.TYP_OFF), *out[-1][2:])
    add('off own', 'hat', 5, cur=user_hats64(), own=MASK_ALL)
    out[-1] = (out[-1][0], dataclasses.replace(out[-1][1], typ=gen.TYP_OFF), *out[-1][2:])
    add('off empty', 'sub', 1, inp=kick44(), den=0, lev=-3, **{'not': -2})   # the old sub defaults, kept
    out[-1] = (out[-1][0], dataclasses.replace(out[-1][1], typ=gen.TYP_OFF), *out[-1][2:])
    for typ in (6, 255):                         # a typ past prp is '---': the track stays
        add('typ %d is off' % typ, 'prp', 3, inp=prc_source(), cur=user_hats64(), sty=1, src=3, shf=2)
        out[-1] = (out[-1][0], dataclasses.replace(out[-1][1], typ=typ), *out[-1][2:])
    return out


def build():
    prng = []
    for b in ((0, 1, 1, 5), (42, 3, 2, 2), (99, 5, 4, 6), (0, 0, 0, 0), (7, 2, 8, 1), (13, 4, 3, 4)):
        r = gen.Rng(*b)
        prng.append(dict(bytes=list(b), state=r.x, next=[r.next() for _ in range(8)]))
    rows = []
    for name, s, track, src, cur, own in cases():
        res = generate(s, track, src, cur, own)
        row = dict(name=name, settings=s.as_dict(), track=track,
                   src=src.to_json() if src is not None else None,
                   cur=cur.to_json() if cur is not None else None)
        if own:
            row['own'] = gen.mask_bars(own)
        row.update(out=res.to_json(), vol=res.needs_vol, own_out=gen.mask_bars(res.written))
        rows.append(row)
    names = [r['name'] for r in rows]
    assert len(names) == len(set(names)), 'duplicate case names'
    return dict(format=FORMAT, vel_table=list(gen.VEL_TABLE), prng=prng, cases=rows)


def main(argv):
    path = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vectors.json')
    v = build()
    with open(path, 'w') as f:
        json.dump(v, f, indent=0, separators=(',', ':'))
        f.write('\n')
    print('%s: %d cases, %d bytes' % (path, len(v['cases']), os.path.getsize(path)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
