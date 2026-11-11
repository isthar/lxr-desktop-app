#!/usr/bin/env python3
"""Tests of the pattern generator's reference prototype (tools/gen/gen.py, E10.2): the
properties of spec 2026-11-11 sections 3-5, and the golden vectors in vectors.json.

    python3 tools/gen/test_gen.py [-v]

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
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen  # noqa: E402
from gen import (BAR, NUM_STEPS, Settings, Step, Track, apply_type_defaults, generate,  # noqa: E402
                 plays, vel_db)

KICK = Track.from_pattern('X...X...X...X...')
KICK_ODD = Track.from_pattern('X..X..X...X.X...')        # kick places 0 3 6 10 12
SEEDS = (0, 1, 17, 42, 99)


def S(typ, **kw):
    s = apply_type_defaults(Settings(), typ)
    if 'not' in kw:
        kw['not_'] = kw.pop('not')
    return dataclasses.replace(s, **kw)


def bar_hits(t, b, pred=None):
    return {p for p in range(BAR) if t.active[b * BAR + p] and (pred is None or pred(t.steps[b * BAR + p]))}


def heard(t, bars=16):
    """{bar (1-16): set of steps 0-63 that play}, the A:B conditions over `bars` bars of
    a 64-step track (loop = bar // 4); probabilities below 100 % count as playing"""
    out = {}
    for n in range(bars):
        loop, b = divmod(n, 4)
        out[n + 1] = {i for i in range(b * BAR, b * BAR + BAR)
                      if t.active[i] and plays(t.steps[i].prob, loop) is not False}
    return out


def bars_of(t, i, bars=16):
    """the bars (1-16) in which step i plays"""
    return [n for n, steps in heard(t, bars).items() if i in steps]


def no_accents():
    """tracks with no step at vel >= 64 as they play: ghosts only, and an accent past the
    length (step 21 of a 16-step track does not play)"""
    ghosts = Track.from_pattern('.x.x....x..x...x')
    past = Track.from_pattern('.x..............')
    past.active[20] = True
    return ghosts, past


def accent_in_bar3():
    """64 steps whose only accent is step 37 (bar 3, its step 5): no accent in bar 1"""
    t = Track(length=64)
    t.active[36] = True
    t.steps[36] = Step(vol=100, prob=102, note=55, p1nr=12, p1val=7, shift=9, fine=-3)
    for i in (1, 18, 50):
        t.active[i] = True
        t.steps[i].vol = 30
    return t


def user_track():
    """a 64-step track of the user's: accents (some under 127), ghosts, p-locks, flam,
    shift, fine and conditions, different in each bar"""
    t = Track(length=64)
    for i, v in ((2, 127), (6, 100), (10, 64), (14, 127), (19, 127), (23, 90), (30, 127),
                 (34, 127), (38, 127), (42, 120), (50, 127), (54, 70), (63, 127)):
        t.active[i] = True
        t.steps[i] = Step(vol=v, prob=102 if i == 42 else 100, note=55 + i % 7, p1nr=12, p1val=i,
                          flam=2 if i == 6 else 0, shift=9 if i == 10 else 7, fine=-3 if i == 14 else 0)
    for i in (1, 5, 9, 13, 21, 45, 61):
        t.active[i] = True
        t.steps[i].vol = 30
    return t


class Prng(unittest.TestCase):
    def test_fnv1a_known_values(self):
        self.assertEqual(gen.fnv1a(b''), 0x811C9DC5)
        self.assertEqual(gen.fnv1a(b'a'), 0xE40C292C)
        self.assertEqual(gen.fnv1a(b'foobar'), 0xBF9CF968)

    def test_xorshift32_from_1(self):
        r = gen.Rng(0, 0, 0, 0)
        r.x = 1
        self.assertEqual([r.next() for _ in range(3)], [270369, 67634689, 2647435461])

    def test_state_is_fnv_of_the_four_bytes(self):
        self.assertEqual(gen.Rng(42, 1, 1, 5).x, gen.fnv1a(bytes((42, 1, 1, 5))))

    def test_rank_ties_to_the_lower_place(self):
        self.assertEqual(gen.rank([5, 5, 9, 0], [2, 2, 1, 3]), [0, 1, 2, 3])
        self.assertEqual(gen.rank([5, 5, 9, 7], [2, 0, 1, 1]), [0, 2, 3])

    def test_vel_table(self):
        self.assertEqual(len(gen.VEL_TABLE), 31)
        self.assertEqual((vel_db(0), vel_db(-3), vel_db(-6), vel_db(-15), vel_db(-30)), (127, 90, 64, 23, 4))
        self.assertEqual(vel_db(5), 127)
        self.assertEqual(vel_db(-40), 4)


class Defaults(unittest.TestCase):
    def test_type_defaults_table(self):
        base = Settings(sed=33, src=4, shf=-3, sty=7, den=9, lev=-1, var=-4, not_=5)
        want = {'hat': (1, 7, -15, 1, 0), 'sub': (1, 3, -8, 0, 0), 'prc': (1, 3, -13, 1, 0),
                'rar': (1, 2, 0, 0, 0), 'prp': (1, 4, -15, 0, 0)}
        for typ, row in want.items():
            s = apply_type_defaults(base, typ)
            self.assertEqual((s.sty, s.den, s.lev, s.var, s.not_), row, typ)
            self.assertEqual((s.typ, s.sed, s.src, s.shf), (gen.TYP_NAMES.index(typ), 33, 4, -3))
        off = apply_type_defaults(base, '---')
        self.assertEqual(off, dataclasses.replace(base, typ=0))

    def test_off_returns_the_track(self):
        cur = user_track()
        self.assertEqual(generate(Settings(), 2, KICK, cur), cur)

    def test_typ_out_of_range_is_off(self):
        # a typ outside 0-5 (a stray byte) is '---': the track stays as it is, and choosing it
        # loads no defaults, so it must not wipe a track
        cur = user_track()
        base = Settings(sed=33, src=4, shf=-3, sty=2, den=9, lev=-1, var=-4, not_=5)
        for typ in (6, 7, 100, 255, -1):
            s = dataclasses.replace(S('hat'), typ=typ)
            self.assertEqual(s.clamped().typ, gen.TYP_OFF)
            out = generate(s, 2, KICK, cur)
            self.assertEqual(out, cur)
            self.assertFalse(out.needs_vol)
            self.assertEqual(apply_type_defaults(base, typ), base)


class Always(unittest.TestCase):
    """spec 3: length 64, written steps clean, vol on when a velocity under 127 is written"""

    def all_settings(self):
        for typ, stys in (('hat', range(0, 5)), ('sub', range(1, 9)), ('prc', range(0, 5)),
                          ('rar', range(1, 5)), ('prp', range(1, 5))):
            for sty in stys:
                for sed in (0, 7):
                    yield S(typ, sty=sty, sed=sed, shf=3, src=4 if typ == 'prp' else 1)

    def test_length_and_clean_steps(self):
        cur = user_track()
        src = generate(S('prc', sed=3), 3, KICK)
        for s in self.all_settings():
            out = generate(s, 2, src if s.typ == gen.TYP_PRP else KICK, cur)
            self.assertEqual(out.length, 64)
            kept = set()
            if s.sty == 0:
                kept = {i for i in range(NUM_STEPS) if cur.active[i] and cur.steps[i].vol >= 64}
            for i, st in enumerate(out.steps):
                if i in kept:
                    continue
                self.assertEqual((st.p1nr, st.p1val, st.p2nr, st.p2val, st.shift, st.fine),
                                 (0xFF, 0, 0xFF, 0, 7, 0), (s, i))
                if s.typ != gen.TYP_RAR:
                    self.assertEqual(st.flam, 0)
            low = any(out.active[i] and i not in kept and out.steps[i].vol < 127 for i in range(NUM_STEPS))
            self.assertEqual(out.needs_vol, low, s)

    def test_notes(self):
        for n in (-24, -2, 0, 24):
            out = generate(S('hat', **{'not': n}), 5)
            self.assertTrue(all(st.note == 60 + n for st in out.steps))

    def test_determinism(self):
        cur = user_track()
        for s in self.all_settings():
            a = generate(s, 4, KICK_ODD, cur)
            b = generate(s, 4, KICK_ODD.copy(), cur.copy())
            self.assertEqual(a.to_json(), b.to_json())
        self.assertNotEqual(generate(S('hat', sed=1), 5).to_json(), generate(S('hat', sed=2), 5).to_json())
        self.assertNotEqual(generate(S('hat'), 5).to_json(), generate(S('hat'), 6).to_json())

    def test_lev_and_not_move_nothing(self):
        for typ in ('hat', 'sub', 'prc', 'rar'):
            for sed in SEEDS:
                a = generate(S(typ, sed=sed, lev=-20), 3, KICK)
                b = generate(S(typ, sed=sed, lev=-8, **{'not': 5}), 3, KICK)
                self.assertEqual(a.active, b.active)


class Hat(unittest.TestCase):
    def test_ghosts_per_bar_is_den_clamped(self):
        for sty, free in ((1, 12), (2, 8), (3, 12), (4, 12)):
            for den in range(17):
                for sed in SEEDS:
                    out = generate(S('hat', sty=sty, den=den, var=0, sed=sed), 5)
                    for b in range(4):
                        self.assertEqual(len(bar_hits(out, b, lambda st: st.vol < 127)), min(den, free),
                                         (sty, den, sed, b))

    def test_levels(self):
        for lev in (-27, -15, -6, -3, 0, -30):
            allowed = {vel_db(lev + d) for d in range(-3, 3)}
            for sty in range(1, 5):
                for sed in SEEDS:
                    out = generate(S('hat', sty=sty, lev=lev, den=16, var=0, sed=sed), 5)
                    acc = gen.HAT_ACCENTS[sty]
                    ghosts = [out.steps[i].vol for i in range(NUM_STEPS) if out.active[i] and i % BAR not in acc]
                    self.assertTrue(ghosts)
                    self.assertTrue(set(ghosts) <= allowed, (lev, sty, sed, set(ghosts) - allowed))
                    for p in range(BAR):             # the offset is the place's: the same in every bar
                        self.assertEqual(len({out.steps[b * BAR + p].vol for b in range(4)}), 1)

    def test_accents_on_offbeats_sty1(self):
        for sed in SEEDS:
            for den in (0, 7, 16):
                out = generate(S('hat', sty=1, den=den, sed=sed), 5)
                for b in range(4):
                    self.assertEqual(bar_hits(out, b, lambda st: st.vol == 127), set(gen.OFFBEATS))

    def test_accents_sty2(self):
        out = generate(S('hat', sty=2, den=16), 5)
        self.assertEqual(bar_hits(out, 0, lambda st: st.vol == 127), set(gen.OFFBEATS | gen.QUARTERS))
        self.assertEqual(bar_hits(out, 0, lambda st: st.vol < 127), set(gen.EVENS))

    def test_den_nested(self):
        for sty in range(0, 5):
            for sed in SEEDS:
                prev = set()
                for den in range(17):
                    out = generate(S('hat', sty=sty, den=den, var=0, sed=sed), 5, None, user_track())
                    ghosts = {i for i in range(NUM_STEPS) if out.active[i] and out.steps[i].vol < 64}
                    self.assertTrue(prev <= ghosts, (sty, sed, den))
                    prev = ghosts

    def test_same_layout_every_bar(self):
        out = generate(S('hat', sed=5), 5)
        for b in range(1, 4):
            for p in range(BAR):
                self.assertEqual(out.steps[p], out.steps[b * BAR + p])
                self.assertEqual(out.active[p], out.active[b * BAR + p])

    def test_var_plus_in_bars_5_to_8(self):
        for n in range(1, 5):
            for sed in SEEDS:
                base = generate(S('hat', den=6, var=0, sed=sed), 5)
                out = generate(S('hat', den=6, var=n, sed=sed), 5)
                plus = generate(S('hat', den=6 + n, var=0, sed=sed), 5)
                extra = [i for i in range(NUM_STEPS) if out.active[i] and not base.active[i]]
                self.assertEqual(len(extra), 4 * n)
                self.assertEqual([i for i in range(NUM_STEPS) if out.active[i]],
                                 [i for i in range(NUM_STEPS) if plus.active[i]])
                for i in extra:
                    self.assertEqual(out.steps[i].prob, gen.COND_2_2)
                    self.assertEqual(bars_of(out, i), [b for b in (5, 6, 7, 8, 13, 14, 15, 16) if (b - 1) % 4 == i // BAR])
                h = heard(out)
                for bar in range(1, 17):
                    ghosts = {i for i in h[bar] if out.steps[i].vol < 127}
                    self.assertEqual(len(ghosts), 6 + n if (bar - 1) % 8 >= 4 else 6, (n, sed, bar))

    def test_var_minus_in_bars_1_to_4(self):
        for n in range(1, 5):
            out = generate(S('hat', den=6, var=-n, sed=3), 5)
            h = heard(out)
            for bar in range(1, 17):
                ghosts = {i for i in h[bar] if out.steps[i].vol < 127}
                self.assertEqual(len(ghosts), 6 if (bar - 1) % 8 < 4 else 6 - n, (n, bar))
            minus = generate(S('hat', den=6 - n, var=0, sed=3), 5)
            for i in range(NUM_STEPS):
                if out.active[i] and out.steps[i].prob == gen.COND_1_2:
                    self.assertFalse(minus.active[i])

    def test_sty0_keeps_the_accents(self):
        cur = user_track()
        for var in (-3, 0, 2):
            out = generate(S('hat', sty=0, den=16, var=var), 5, None, cur)
            for i in range(NUM_STEPS):
                if cur.active[i] and cur.steps[i].vol >= 64:
                    self.assertTrue(out.active[i])
                    self.assertEqual(out.steps[i], cur.steps[i])
                elif cur.active[i]:                   # the user's ghosts are written over
                    self.assertTrue(not out.active[i] or out.steps[i].p1nr == 0xFF)
            for b in range(4):                         # every free place of the bar has a ghost
                acc = {p for p in range(BAR) if cur.active[b * BAR + p] and cur.steps[b * BAR + p].vol >= 64}
                ghosts = bar_hits(out, b) - acc
                self.assertEqual(ghosts, set(range(BAR)) - acc)

    def test_sty0_without_accents_is_sty1(self):
        # spec 9: no step at vel >= 64 on the track as it plays: sty 0 takes sty 1's accents
        # (the offbeats at 127) and its weights; the seed keeps sty 0
        for cur in (None,) + no_accents():
            for den in (0, 5, 12, 16):
                for var in (-2, 0, 3):
                    for sed in SEEDS:
                        out = generate(S('hat', sty=0, den=den, var=var, sed=sed), 5, None, cur)
                        r = gen.Rng(sed, gen.TYP_HAT, 0, 5)
                        keys = [r.rand(65536) for _ in range(BAR)]
                        offsets = [r.rand(6) - 3 for _ in range(BAR)]
                        order = gen.rank(keys, gen.HAT_WEIGHTS[1], lambda p: p not in gen.OFFBEATS)
                        ghosts = order[:min(den, len(order)) + max(var, 0)]
                        for b in range(4):
                            self.assertEqual(bar_hits(out, b, lambda st: st.vol == 127), set(gen.OFFBEATS))
                            self.assertEqual(bar_hits(out, b, lambda st: st.vol < 127), set(ghosts), (den, var, sed))
                            for p in ghosts:
                                self.assertEqual(out.steps[b * BAR + p].vol, vel_db(-15 + offsets[p]))
        self.assertFalse(all(generate(S('hat', sty=0, sed=sed), 5) == generate(S('hat', sty=1, sed=sed), 5)
                             for sed in range(10)))

    def test_sty0_accent_in_bar3_only(self):
        # spec 9: the fallback looks at the whole track as it plays; one accent in bar 3 keeps
        # sty 0: that accent stays, and no offbeat at 127 comes in
        cur = accent_in_bar3()
        for sed in SEEDS:
            out = generate(S('hat', sty=0, den=7, var=0, sed=sed), 5, None, cur)
            self.assertEqual(out.steps[36], cur.steps[36])
            self.assertEqual([i for i in range(NUM_STEPS) if out.active[i] and out.steps[i].vol >= 64], [36])
            self.assertEqual([len(bar_hits(out, b)) for b in range(4)], [7, 7, 8, 7])

    def test_legacy_bit7_is_no_accent(self):
        # spec 3: an accent is vel >= 64 in bits 0-6; 0x80 | 40 is 40 (bit 7 is the legacy active
        # flag). Alone it is no accent, so sty 0 falls back (spec 9); next to an accent it goes
        cur = Track.from_pattern('................')
        cur.active[5] = True
        cur.steps[5].vol = 0x80 | 40
        out = generate(S('hat', sty=0, den=7, var=0), 5, None, cur)
        for b in range(4):
            self.assertEqual(bar_hits(out, b, lambda st: st.vol == 127), set(gen.OFFBEATS))
        self.assertTrue(all(st.vol != 0x80 | 40 for st in out.steps))
        cur.active[1] = True
        cur.steps[1].vol = 64
        out = generate(S('hat', sty=0, den=0, var=0), 5, None, cur)
        self.assertEqual([i for i in range(NUM_STEPS) if out.active[i]], [1, 17, 33, 49])

    def test_sty0_short_track_repeats(self):
        cur = Track.from_pattern('..X...X..o.x..X.')
        cur.steps[9].p1nr, cur.steps[9].p1val = 20, 99
        out = generate(S('hat', sty=0, den=4, var=0), 5, None, cur)
        for b in range(4):
            for p in (2, 6, 9, 14):
                self.assertEqual(out.steps[b * BAR + p], cur.steps[p])
            self.assertEqual(len(bar_hits(out, b)), 8)


SUB_STYLES = {
    1: ('...g..G....g..G.',),
    2: ('...........g..G.',),
    3: ('......g.......G.',),
    4: ('...g.......G....',),
    5: ('.......g.......G',),
    6: ('...g..gg...g..GG',),
    7: ('...g..G....g..G.', '...g..G..g.g..GG'),
}
SUB_W8 = [0, 0, 0, 4, 0, 0, 4, 2, 0, 1, 0, 4, 0, 1, 4, 2]     # steps 4 7 12 15: 4; 8 16: 2; 10 14: 1
KICK_1_8 = Track.from_pattern('X......X........')


def sub_draws(sty, sed, track=1):
    """sub's draws in their order (spec 5, 10): 16 keys, 16 level offsets (unused), then
    phase 3: sty 8's 16 keys (only there), then the 16 keys of var (always)"""
    r = gen.Rng(sed, gen.TYP_SUB, sty, track)
    for _ in range(2 * BAR):
        r.rand(65536)
    k8 = [r.rand(65536) for _ in range(BAR)] if sty == 8 else None
    return k8, [r.rand(65536) for _ in range(BAR)]


def sub_bar(t, b):
    """a bar as text: g a hit under 127, G a hit at 127, . no hit"""
    return ''.join('.' if not t.active[b * BAR + p] else 'G' if t.steps[b * BAR + p].vol == 127 else 'g'
                   for p in range(BAR))


class Sub(unittest.TestCase):
    def test_styles_on_a_44_kick(self):
        for sty, bars in SUB_STYLES.items():
            out = generate(S('sub', sty=sty), 1, KICK)
            for b in range(4):
                self.assertEqual(sub_bar(out, b), bars[b % len(bars)], (sty, b))

    def test_kick_steps_drop_their_hits(self):
        # a hit on a kick step is dropped; G does not move to another step
        out = generate(S('sub', sty=1), 1, KICK_ODD)        # kick 1 4 7 11 13: 4 and 7 go
        for b in range(4):
            self.assertEqual(sub_bar(out, b), '...........g..G.')
        out = generate(S('sub', sty=5), 1, KICK_1_8)        # kick 1 8: the g on 8 goes
        self.assertEqual(sub_bar(out, 0), '...............G')
        out = generate(S('sub', sty=7), 1, Track.from_pattern('.X.X..X.........'))   # kick 2 4 7
        self.assertEqual(sub_bar(out, 0), '...........g..G.')
        self.assertEqual(sub_bar(out, 1), '.........g.g..GG')
        out = generate(S('sub', sty=5), 1, Track.from_pattern('...............X'))   # kick 16
        self.assertEqual(sub_bar(out, 0), '.......g........')

    def test_a_64_step_kick_is_read_as_bar_1_plays(self):
        k = Track(length=64)
        for i in (0, 7, 18, 40):
            k.active[i] = True
        out = generate(S('sub', sty=5), 1, k)           # kick places 0 7: the g on 8 goes
        for b in range(4):
            self.assertEqual(sub_bar(out, b), '...............G')

    def test_nothing_hits_the_kick(self):
        for sty in range(1, 9):
            for src, kick in ((KICK, gen.QUARTERS), (KICK_ODD, {0, 3, 6, 10, 12}), (KICK_1_8, {0, 7}),
                              (None, gen.QUARTERS)):
                for sed in SEEDS:
                    out = generate(S('sub', sty=sty, sed=sed, var=2), 1, src)
                    for i in range(NUM_STEPS):
                        if out.active[i]:
                            self.assertNotIn(i % BAR, kick, (sty, sed, i))

    def test_velocity_note_and_prob(self):
        self.assertEqual(vel_db(-8), 51)
        for lev in (0, -8, -30):
            for n in (-24, 0, 24):
                out = generate(S('sub', sty=6, lev=lev, **{'not': n}), 1, KICK)
                for i in range(NUM_STEPS):
                    st = out.steps[i]
                    self.assertEqual(st.note, 60 + n)
                    if out.active[i]:
                        self.assertEqual(st.vol, 127 if i % BAR >= 14 else vel_db(lev))
                        self.assertEqual(st.prob, 100)

    def test_sty8_is_den_hits_of_the_dictionary_the_last_one_loud(self):
        for sed in range(40):
            k8, _ = sub_draws(8, sed)
            for src, kick in ((KICK, gen.QUARTERS), (KICK_ODD, {0, 3, 6, 10, 12}), (KICK_1_8, {0, 7})):
                order = gen.rank(k8, SUB_W8, lambda p: p not in kick)
                for den in (0, 1, 2, 3, 5, 8, 9, 12, 16):
                    out = generate(S('sub', sty=8, sed=sed, den=den), 1, src)
                    want = sorted(order[:8 if den == 0 or den > 8 else den])
                    for b in range(4):
                        hits = [p for p in range(BAR) if out.active[b * BAR + p]]
                        self.assertEqual(hits, want, (sed, den, b))
                        self.assertEqual([out.steps[b * BAR + p].vol for p in want],
                                         [51] * (len(want) - 1) + [127][:len(want)])

    def test_sty8_den_default_and_den_k_is_a_prefix_of_k_plus_1(self):
        for sed in range(30):
            prev = set()
            for den in range(1, 9):
                cur = bar_hits(generate(S('sub', sty=8, sed=sed, den=den), 1, KICK), 0)
                self.assertTrue(prev < cur, (sed, den))
                prev = cur
            self.assertEqual(len(bar_hits(generate(S('sub', sty=8, sed=sed), 1, KICK), 0)), 3)

    def test_den_does_nothing_outside_sty8(self):
        for sty in range(1, 8):
            a = generate(S('sub', sty=sty, den=1), 1, KICK)
            for den in (0, 3, 8, 16):
                self.assertEqual(a, generate(S('sub', sty=sty, den=den), 1, KICK))

    def test_var_changes_only_bar_4(self):
        for sty in range(1, 9):
            for sed in SEEDS:
                base = generate(S('sub', sty=sty, sed=sed), 1, KICK)
                for var in (-4, -3, -2, -1, 1, 2, 3, 4):
                    out = generate(S('sub', sty=sty, sed=sed, var=var), 1, KICK)
                    for i in range(3 * BAR):
                        self.assertEqual(out.steps[i], base.steps[i], (sty, sed, var, i))
                        self.assertEqual(out.active[i], base.active[i])

    def test_var_plus_adds_ghosts_in_bar_4_at_2_2(self):
        # first steps 16, 14, 10 in that order (places 15 13 9), then by var keys x weights
        for sty, want in ((1, [15, 13, 9, 7]), (6, [13, 9])):
            base = generate(S('sub', sty=sty), 1, KICK)
            for n in range(1, 5):
                out = generate(S('sub', sty=sty, var=n), 1, KICK)
                got = [p for p in range(BAR) if out.active[3 * BAR + p] and not base.active[3 * BAR + p]]
                self.assertEqual(got, sorted(want[:n]), (sty, n))
                for p in got:
                    st = out.steps[3 * BAR + p]
                    self.assertEqual((st.vol, st.prob, st.note), (51, gen.COND_2_2, 60))
                for p in range(BAR):
                    if base.active[3 * BAR + p]:
                        self.assertEqual(out.steps[3 * BAR + p], base.steps[3 * BAR + p])

    def test_var_plus_rest_by_keys_and_weights(self):
        # sty 4's bar 4 holds places 3 and 11; free: 6 7 9 13 14 15, of which 15 13 9 come
        # first and 6 7 14 follow by the var keys x the weights of sty 8
        for sed in range(20):
            _, vk = sub_draws(4, sed)
            want = [15, 13, 9] + gen.rank(vk, SUB_W8, lambda p: p in (6, 7, 14))
            for n in range(1, 5):
                out = generate(S('sub', sty=4, sed=sed, var=n), 1, KICK)
                got = {p for p in range(BAR) if out.active[3 * BAR + p]} - {3, 11}
                self.assertEqual(got, set(want[:n]), (sed, n))

    def test_var_plus_skips_the_kick(self):
        out = generate(S('sub', sty=1, var=1), 1, Track.from_pattern('...............X'))
        self.assertFalse(out.active[3 * BAR + 15])
        self.assertTrue(out.active[3 * BAR + 13])

    def test_var_minus_halves_ghosts_of_bar_4_never_the_accent(self):
        for sty in range(1, 9):
            for sed in SEEDS:
                base = generate(S('sub', sty=sty, sed=sed), 1, KICK)
                _, vk = sub_draws(sty, sed)
                ghosts = [p for p in range(BAR) if base.active[3 * BAR + p] and base.steps[3 * BAR + p].vol != 127]
                order = gen.rank(vk, [1] * BAR, lambda p: p in ghosts)
                for n in range(1, 5):
                    out = generate(S('sub', sty=sty, sed=sed, var=-n), 1, KICK)
                    self.assertEqual(sub_bar(out, 3), sub_bar(base, 3))
                    half = {p for p in range(BAR) if out.steps[3 * BAR + p].prob == gen.COND_1_2}
                    self.assertEqual(half, set(order[:n]), (sty, sed, n))

    def test_var_minus_at_lev_0_still_halves_ghosts(self):
        # a ghost at lev 0 has velocity 127 like G, but it is a ghost: var -n takes it
        for sty in range(1, 9):
            base = generate(S('sub', sty=sty, lev=0, sed=4), 1, KICK)
            out = generate(S('sub', sty=sty, lev=0, sed=4, var=-4), 1, KICK)
            n_hits = len(bar_hits(base, 3))
            ref = generate(S('sub', sty=sty, lev=-8, sed=4, var=-4), 1, KICK)
            half = lambda t: {p for p in range(BAR) if t.steps[3 * BAR + p].prob == gen.COND_1_2}
            self.assertEqual(half(out), half(ref), sty)
            self.assertTrue(half(out) or n_hits == 1)

    def test_var_minus_is_heard_in_bar_8(self):
        out = generate(S('sub', sty=1, var=-2), 1, KICK)
        h = heard(out)
        self.assertEqual((len(h[4]), len(h[8])), (4, 2))

    def test_defaults_clamp_and_sources(self):
        s = apply_type_defaults(Settings(), 'sub')
        self.assertEqual((s.sty, s.den, s.lev, s.var, s.not_), (1, 3, -8, 0, 0))
        self.assertEqual(generate(S('sub', sty=0), 1, KICK), generate(S('sub', sty=1), 1, KICK))
        a = generate(dataclasses.replace(S('sub', sty=5), src=2), 1, KICK_ODD)
        self.assertEqual(a, generate(S('sub', sty=5), 1, None))
        self.assertEqual(generate(S('sub', sty=3), 1, Track()), generate(S('sub', sty=3), 1, KICK))

    def test_the_track_index_is_in_the_seed(self):
        bars = {sub_bar(generate(S('sub', sty=8, sed=5), t, KICK), 0) for t in range(7)}
        self.assertGreater(len(bars), 1)


KICK_EVEN = Track.from_pattern('.X...X...X...X..')      # kick places 1 5 9 13: step 1 is free


def prc_draws(sty, sed, track=2):
    """prc's draws in their order (spec 5, 9): 32 keys, 32 level offsets, then phase 3: the
    16 keys of var, and nothing else"""
    r = gen.Rng(sed, gen.TYP_PRC, sty, track)
    keys = [r.rand(65536) for _ in range(2 * BAR)]
    offsets = [r.rand(5) - 2 for _ in range(2 * BAR)]
    var_keys = [r.rand(65536) for _ in range(BAR)]
    return keys, offsets, var_keys


class Prc(unittest.TestCase):
    def accents(self, out, b):
        return bar_hits(out, b, lambda st: st.vol == 127 and st.prob == 100)

    def moved(self, out):
        """(bar 1's accents that bar 2 lacks, bar 2's that bar 1 lacks)"""
        a1, a2 = self.accents(out, 0), self.accents(out, 1)
        return a1 - a2, a2 - a1

    def test_bar2_moves_the_top_ranked_accent(self):
        # spec 9: bar 2 moves bar 1's accent with the highest ranking key to the best ranked
        # free place of keys 16-31 (not an accent of bar 1, not a kick step), style weights
        for sty in range(1, 5):
            w = gen.PRC_WEIGHTS[sty]
            for den in range(1, 10):
                for sed in SEEDS:
                    for src, kick in ((KICK, gen.QUARTERS), (KICK_ODD, {0, 3, 6, 10, 12}), (KICK_EVEN, {1, 5, 9, 13})):
                        keys = prc_draws(sty, sed)[0]
                        out = generate(S('prc', sty=sty, den=den, var=0, sed=sed), 2, src)
                        a1 = self.accents(out, 0)
                        top = gen.rank(keys[:BAR], w, lambda p: p not in kick)[0]
                        dest = gen.rank(keys[BAR:], w, lambda p: p not in a1 and p not in kick)
                        gone, new = self.moved(out)
                        if dest:
                            self.assertEqual((gone, new), ({top}, {dest[0]}), (sty, den, sed, sorted(kick)))
                        else:
                            self.assertEqual((gone, new), (set(), set()))

    def test_den_never_changes_the_moved_accent(self):
        # spec 9: sty, sed and kick fixed, den k -> k+1 keeps the top-ranked accent of bar 1,
        # so it never changes which accent of bar 1 moves
        for sty in range(1, 5):
            for sed in range(25):
                for src in (KICK, KICK_ODD, KICK_EVEN):
                    first = None
                    for den in range(1, 17):
                        out = generate(S('prc', sty=sty, den=den, var=0, sed=sed), 2, src)
                        gone, _ = self.moved(out)
                        if den == 1:
                            first = self.accents(out, 0)      # den 1: the top-ranked accent alone
                        if gone:                              # (none when no free place is left)
                            self.assertEqual(gone, first, (sty, sed, den))
                            self.assertTrue(first <= self.accents(out, 0))

    def test_phase3_is_the_var_keys(self):
        # spec 5, 9: no draw for the move, so var +n's keys are the first 16 draws of phase 3
        for sty in range(1, 5):
            for sed in SEEDS:
                var_keys = prc_draws(sty, sed)[2]
                base = generate(S('prc', sty=sty, den=3, var=0, sed=sed), 2, KICK)
                out = generate(S('prc', sty=sty, den=3, var=1, sed=sed), 2, KICK)
                busy = bar_hits(base, 3)
                late = [gen.PRC_WEIGHTS[sty][p] * (4 if p >= 12 else 1) for p in range(BAR)]
                want = gen.rank(var_keys, late, lambda p: p not in busy and p not in gen.QUARTERS)[:1]
                self.assertEqual([i - 3 * BAR for i in range(3 * BAR, NUM_STEPS) if out.steps[i].prob == gen.COND_2_2
                                  and out.active[i]], want, (sty, sed))

    def test_bar2_differs_by_one_moved_hit(self):
        for sty in range(1, 5):
            for den in range(1, 8):
                for sed in SEEDS:
                    for src in (KICK, KICK_ODD):
                        out = generate(S('prc', sty=sty, den=den, var=0, sed=sed), 2, src)
                        a1, a2 = self.accents(out, 0), self.accents(out, 1)
                        self.assertEqual(len(a1), den)
                        self.assertEqual(len(a1 ^ a2), 2, (sty, den, sed))
                        self.assertEqual(self.accents(out, 2), a1)
                        self.assertEqual(self.accents(out, 3), a2)

    def test_no_accent_on_the_kick(self):
        for sty in range(1, 5):
            for sed in SEEDS:
                for src, kick in ((KICK, gen.QUARTERS), (KICK_ODD, {0, 3, 6, 10, 12})):
                    out = generate(S('prc', sty=sty, den=6, var=3, sed=sed), 2, src)
                    for i in range(NUM_STEPS):
                        if out.active[i] and out.steps[i].vol == 127:
                            self.assertNotIn(i % BAR, kick)

    def test_weights(self):
        for sed in SEEDS:      # sty 1 and 2 never put an accent on a quarter, sty 3 prefers 4 7 12 15
            for sty in (1, 2):
                out = generate(S('prc', sty=sty, den=12, sed=sed), 2, Track.from_pattern('X'))
                self.assertFalse(any(out.active[i] and out.steps[i].vol == 127 and i % 4 == 0
                                     for i in range(NUM_STEPS)))
        hits = [0] * BAR
        for sed in range(100):
            out = generate(S('prc', sty=3, den=2, var=0, sed=sed), 2, KICK)
            for p in self.accents(out, 0):
                hits[p] += 1
        self.assertGreater(sum(hits[p] for p in (3, 6, 11, 14)), 150)

    def test_ghosts_before_accents(self):
        for den in range(1, 9):
            for sed in SEEDS:
                out = generate(S('prc', den=den, var=0, sed=sed, lev=-13), 2, KICK)
                for b in range(4):
                    acc = self.accents(out, b)
                    ghosts = bar_hits(out, b, lambda st: st.vol < 127)
                    places = {p - 1 for p in acc if p > 0} - acc - gen.QUARTERS
                    self.assertEqual(len(ghosts), min(3, (den + 1) // 2, len(places)))
                    for q in ghosts:
                        self.assertIn(q + 1, acc)
                        self.assertIn(out.steps[b * BAR + q].vol, {vel_db(-13 + d) for d in range(-2, 3)})

    def test_no_pickup_before_step_1(self):
        # spec 9: an accent on step 1 of a bar gets no ghost (it used to get step 16 of the same bar)
        bites = 0
        for sty in (3, 4):
            for den in range(1, 9):
                for sed in range(30):
                    out = generate(S('prc', sty=sty, den=den, var=0, sed=sed), 2, KICK_EVEN)
                    for b in range(4):
                        acc = self.accents(out, b)
                        bites += 0 in acc and 15 not in acc
                        self.assertFalse(out.active[b * BAR + 15] and out.steps[b * BAR + 15].vol < 127,
                                         (sty, den, sed, b))
        self.assertGreater(bites, 0)
        cur = Track.from_pattern('X.....X.........')    # sty 0: the user's accents on steps 1 and 7
        for den in range(1, 7):
            out = generate(S('prc', sty=0, den=den, var=0), 2, KICK, cur)
            for b in range(4):
                self.assertEqual(bar_hits(out, b), {0, 5, 6}, den)

    def test_ghosts_never_on_the_kick(self):
        # spec 8: a pickup whose pre-step is a kick step is skipped (the kick from src)
        for sty in range(0, 5):
            for den in range(1, 9):
                for sed in range(12):
                    for src, kick in ((KICK, gen.QUARTERS), (KICK_ODD, {0, 3, 6, 10, 12})):
                        out = generate(S('prc', sty=sty, den=den, var=0, sed=sed), 2, src, user_track())
                        for i in range(NUM_STEPS):
                            if out.active[i] and out.steps[i].vol < 64 and out.steps[i].p1nr == 0xFF:
                                self.assertNotIn(i % BAR, kick, (sty, den, sed, i))

    def test_den_nested(self):
        for sty in range(1, 5):
            for sed in SEEDS:
                prev = set()
                for den in range(13):
                    a1 = self.accents(generate(S('prc', sty=sty, den=den, sed=sed), 2, KICK), 0)
                    self.assertTrue(prev <= a1)
                    prev = a1

    def test_var_plus_in_bar_8(self):
        for n in range(1, 5):
            for sed in SEEDS:
                out = generate(S('prc', den=3, var=n, sed=sed), 2, KICK)
                added = [i for i in range(NUM_STEPS) if out.active[i] and out.steps[i].prob == gen.COND_2_2]
                self.assertEqual(len(added), n)
                for i in added:
                    self.assertEqual(i // BAR, 3)
                    self.assertEqual(out.steps[i].vol, 127)
                    self.assertEqual(bars_of(out, i), [8, 16])
                base = generate(S('prc', den=3, var=0, sed=sed), 2, KICK)
                for i in range(NUM_STEPS):
                    if i not in added:
                        self.assertEqual(out.active[i], base.active[i])

    def test_var_plus_prefers_13_to_16(self):
        late = 0
        for sed in range(100):
            out = generate(S('prc', sty=4, den=3, var=1, sed=sed), 2, KICK)
            late += sum(1 for i in range(48, 64) if out.active[i] and out.steps[i].prob == gen.COND_2_2 and i >= 60)
        self.assertGreater(late, 50)

    def test_var_minus_gone_in_bar_8(self):
        for n in range(1, 5):
            out = generate(S('prc', den=3, var=-n, sed=7), 2, KICK)
            gone = [i for i in range(NUM_STEPS) if out.active[i] and out.steps[i].prob == gen.COND_1_2]
            self.assertEqual(len(gone), min(n, 3))
            for i in gone:
                self.assertEqual(i // BAR, 3)
                self.assertEqual(out.steps[i].vol, 127)
                self.assertEqual(bars_of(out, i), [4, 12])

    def test_sty0_without_accents_is_sty1(self):
        # spec 9: no step at vel >= 64 on the track as it plays: sty 0 is sty 1 (its weights,
        # bar 2's move, var -n's 1:2 in bar 4); the seed keeps sty 0
        w = gen.PRC_WEIGHTS[1]
        for cur in (None,) + no_accents():
            for den in (1, 3, 6):
                for var in (-2, 0, 2):
                    for sed in SEEDS:
                        out = generate(S('prc', sty=0, den=den, var=var, sed=sed), 2, KICK, cur)
                        keys = prc_draws(0, sed)[0]
                        order = gen.rank(keys[:BAR], w, lambda p: p not in gen.QUARTERS)
                        a1 = set(order[:den])
                        dest = gen.rank(keys[BAR:], w, lambda p: p not in a1 and p not in gen.QUARTERS)
                        a2 = (a1 - {order[0]}) | {dest[0]}
                        self.assertEqual([self.accents(out, b) for b in range(3)], [a1, a2, a1], (den, var, sed))
                        gone = [i for i in range(NUM_STEPS) if out.active[i] and out.steps[i].prob == gen.COND_1_2]
                        self.assertEqual(len(gone), min(-var, den) if var < 0 else 0)
                        self.assertTrue(all(i // BAR == 3 for i in gone))
        self.assertFalse(all(generate(S('prc', sty=0, sed=sed), 2, KICK) == generate(S('prc', sty=1, sed=sed), 2, KICK)
                             for sed in range(10)))

    def test_sty0_accent_in_bar3_only(self):
        # spec 9: the fallback looks at the whole track as it plays; one accent in bar 3 keeps
        # sty 0: that accent stays with its ghost before it, and nothing else is written
        cur = accent_in_bar3()
        for den in (1, 3, 6):
            for sed in SEEDS:
                out = generate(S('prc', sty=0, den=den, var=0, sed=sed), 2, KICK, cur)
                self.assertEqual(out.steps[36], cur.steps[36])
                self.assertEqual([bar_hits(out, b) for b in range(4)], [set(), set(), {3, 4}, set()])

    def test_sty0_each_bar_its_own_accents(self):
        # spec 9: sty 0 takes each bar's accents as they play, in all 4 bars: bars 3-4 keep their
        # own (not copies of bars 1-2) and their ghosts stand before them
        cur = user_track()
        acc = [{p for p in range(BAR) if cur.active[b * BAR + p] and cur.steps[b * BAR + p].vol >= 64}
               for b in range(4)]
        self.assertNotEqual(acc[2], acc[0])
        self.assertNotEqual(acc[3], acc[1])
        for den in (1, 3, 5, 8):
            for sed in SEEDS:
                out = generate(S('prc', sty=0, den=den, var=0, sed=sed), 2, KICK, cur)
                for b in range(4):
                    for p in acc[b]:
                        self.assertEqual(out.steps[b * BAR + p], cur.steps[b * BAR + p])
                    ghosts = bar_hits(out, b) - acc[b]
                    places = {p - 1 for p in acc[b] if p > 0} - acc[b] - gen.QUARTERS
                    self.assertTrue(ghosts <= places, (den, sed, b))
                    self.assertEqual(len(ghosts), min(3, (den + 1) // 2, len(places)))

    def test_sty0_keeps_the_accents(self):
        cur = user_track()
        for var in (-2, 0, 3):
            out = generate(S('prc', sty=0, den=5, var=var), 2, KICK, cur)
            for i in range(NUM_STEPS):
                if cur.active[i] and cur.steps[i].vol >= 64:
                    self.assertEqual(out.steps[i], cur.steps[i])
                    self.assertTrue(out.active[i])
            self.assertFalse(any(out.active[i] and out.steps[i].prob == gen.COND_1_2 for i in range(NUM_STEPS)))
            added = [i for i in range(NUM_STEPS) if out.active[i] and out.steps[i].prob == gen.COND_2_2
                     and not (cur.active[i] and cur.steps[i].vol >= 64)]
            self.assertEqual(len(added), max(var, 0))


class Rar(unittest.TestCase):
    def events(self, out):
        return {i: bars_of(out, i) for i in range(NUM_STEPS) if out.active[i]}

    def test_sty1_end_of_phrase(self):
        self.assertEqual(self.events(generate(S('rar', sty=1, den=1), 4, KICK)), {48: [16]})
        self.assertEqual(self.events(generate(S('rar', sty=1, den=2), 4, KICK)), {48: [8, 16]})
        e = self.events(generate(S('rar', sty=1, den=3), 4, KICK))
        self.assertEqual(e, {48: [8, 16], 56: [16], 57: [16], 58: [16], 59: [16]})
        out = generate(S('rar', sty=1, den=4), 4, KICK)
        self.assertEqual(self.events(out), {48: [8, 16], 56: [8, 16], 57: [8, 16], 58: [8, 16], 59: [8, 16]})
        self.assertEqual([out.steps[i].flam for i in (48, 56, 57, 58, 59)], [0, 3, 3, 3, 3])
        self.assertEqual(generate(S('rar', sty=1, den=9), 4, KICK), out)

    def test_sty2_start_of_phrase(self):
        self.assertEqual(self.events(generate(S('rar', sty=2, den=1), 4)), {0: [1]})
        self.assertEqual(self.events(generate(S('rar', sty=2, den=2), 4)), {0: [1, 9]})
        self.assertEqual(self.events(generate(S('rar', sty=2, den=3), 4)), {0: [1, 9]})
        self.assertEqual(self.events(generate(S('rar', sty=2, den=4), 4)), {0: [1, 5, 9, 13]})

    def test_den0_is_nothing(self):
        for sty in range(1, 5):
            self.assertFalse(any(generate(S('rar', sty=sty, den=0), 4, KICK).active))

    def test_sty3_scatter(self):
        for sed in SEEDS:
            prev = set()
            for den in range(17):
                out = generate(S('rar', sty=3, den=den, sed=sed), 4, KICK_ODD)
                steps = {i for i in range(NUM_STEPS) if out.active[i]}
                self.assertEqual(len(steps), min(den, 8))
                self.assertTrue(prev <= steps)
                prev = steps
                for i in steps:
                    self.assertNotIn(i % BAR, {0, 3, 6, 10, 12})
                    self.assertEqual((out.steps[i].vol, out.steps[i].prob), (127, 25))
        bars = [0] * 4                           # bars 1 and 4 weigh 2
        for sed in range(100):
            out = generate(S('rar', sty=3, den=2, sed=sed), 4, KICK)
            for i in range(NUM_STEPS):
                bars[i // BAR] += out.active[i]
        self.assertGreater(bars[0] + bars[3], bars[1] + bars[2])

    def test_bars_1_and_4_weigh_2(self):
        # spec 4.4: steps 1-16 and 49-64 weigh 2, steps 17-48 weigh 1. Each case (found with
        # gen.py by flipping that one step's weight) picks the step, or not, by its weight alone
        for step, sed, den, on in ((16, 8, 1, True), (17, 9, 4, False), (48, 4, 2, False), (49, 1, 4, True)):
            out = generate(S('rar', sty=3, den=den, sed=sed), 4, KICK_EVEN)
            self.assertEqual(out.active[step - 1], on, step)
            self.assertEqual(sum(out.active), den)

    def test_sty4_mixed(self):
        for den in (1, 2, 3, 5, 10, 11, 16):
            out = generate(S('rar', sty=4, den=den, sed=4), 4, KICK)
            e = self.events(out)
            self.assertEqual(e[0], [1, 9])
            self.assertEqual(e[48], [16])
            scatter = [i for i in e if i not in (0, 48)]
            self.assertEqual(len(scatter), min(max(den - 2, 0), 8))
            self.assertTrue(all(out.steps[i].prob == 25 for i in scatter))

    def test_sty4_scatter_at_most_8(self):
        # spec 9: sty 4's scatter has at most 8 steps, as sty 3's, so den 10-16 is one pattern
        for sed in SEEDS:
            for src in (KICK, KICK_ODD, None):
                ten = generate(S('rar', sty=4, den=10, sed=sed), 4, src)
                self.assertEqual(sum(ten.active), 10)
                for den in range(11, 17):
                    self.assertEqual(generate(S('rar', sty=4, den=den, sed=sed), 4, src), ten, (sed, den))


class Prp(unittest.TestCase):
    def src(self):
        t = generate(S('prc', den=4, var=1, sed=11), 2, KICK)
        t.steps[19].note = 67
        t.steps[19].prob |= 0x80                 # a slide flag
        return t

    def test_copy_shifted(self):
        src = self.src()
        for shf in (-8, -4, 0, 5, 8):
            out = generate(S('prp', sty=1, shf=shf, src=3, **{'not': 2}), 3, src)
            for i in range(NUM_STEPS):
                j = (i + shf) % NUM_STEPS
                self.assertEqual(out.active[j], src.active[i])
                if src.active[i]:
                    self.assertEqual((out.steps[j].vol, out.steps[j].prob, out.steps[j].note),
                                     (src.steps[i].vol, src.steps[i].prob & 0x7F, src.steps[i].note + 2))

    def test_short_source_repeats(self):
        src = Track.from_pattern('X..x.o..X...')     # 12 steps
        out = generate(S('prp', sty=1, src=1, shf=1), 3, src)
        self.assertEqual([i for i in range(NUM_STEPS) if out.active[i]],
                         sorted((i + 1) % 64 for i in range(NUM_STEPS) if src.played().active[i]))

    def test_complement(self):
        src = self.src()
        for den in (0, 1, 4, 16):
            out = generate(S('prp', sty=2, den=den, src=3), 3, src)
            for b in range(4):
                silent = {p for p in range(BAR) if not src.active[b * BAR + p]}
                hits = bar_hits(out, b)
                self.assertTrue(hits <= silent)
                self.assertEqual(len(hits), min(den, len(silent)))

    def test_pre_beats(self):
        src = self.src()
        out = generate(S('prp', sty=3, src=3, lev=-12), 3, src)
        want = {(i - 1) % 64 for i in range(NUM_STEPS) if src.active[i] and src.steps[i].vol >= 64
                and not src.active[(i - 1) % 64]}
        self.assertEqual({i for i in range(NUM_STEPS) if out.active[i]}, want)
        self.assertTrue(all(out.steps[i].vol == vel_db(-12) for i in want))

    def test_echo(self):
        src = self.src()
        out = generate(S('prp', sty=4, src=3, shf=3, lev=-9), 3, src)
        want = {(i + 3) % 64 for i in range(NUM_STEPS) if src.active[i] and src.steps[i].vol >= 64}
        self.assertEqual({i for i in range(NUM_STEPS) if out.active[i]}, want)
        self.assertTrue(all(out.steps[i].vol == vel_db(-9) for i in want))

    def test_pre_beats_and_echo_keep_the_condition(self):
        # spec 8: prp sty 3 and 4 take the source step's prob (condition) byte, without the
        # slide flag; the velocity from lev, the note 60 + not
        src = Track.from_pattern('...X...X...X...X' * 4)
        for i, prob in ((3, 102), (7, 109), (11, 0x80 | 101), (19, 50), (63, 106)):
            src.steps[i].prob = prob
            src.steps[i].note = 70
        for sty, shf in ((3, 0), (4, 3), (4, -5)):
            out = generate(S('prp', sty=sty, src=3, shf=shf, lev=-12, **{'not': 1}), 3, src)
            for i in range(NUM_STEPS):
                if not src.active[i]:
                    continue
                j = (i - 1) % NUM_STEPS if sty == 3 else (i + shf) % NUM_STEPS
                self.assertTrue(out.active[j])
                self.assertEqual((out.steps[j].prob, out.steps[j].vol, out.steps[j].note),
                                 (src.steps[i].prob & 0x7F, vel_db(-12), 61), (sty, shf, i))
        echo = generate(S('prp', sty=4, src=3, shf=3), 3, src)
        self.assertEqual(bars_of(echo, 6), [5, 13])       # the echo of a 2:2 accent: bars 5-8 only
        self.assertEqual(bars_of(echo, 10), [13])         # of a 4:4 accent: the last 4 of 16

    def test_src_on_itself_does_nothing(self):
        cur = user_track()
        out = generate(S('prp', sty=1, src=4, shf=2), 3, cur, cur)
        self.assertEqual(out, cur)
        self.assertFalse(out.needs_vol)


def mask_of(steps):
    """a mask of steps 0-63 as generate() takes and gives it: bit i is step i"""
    return sum(1 << i for i in set(steps))


def kept(t):
    """the steps of t that its run did not write: the user's accents it kept"""
    return {i for i in range(NUM_STEPS) if t.active[i] and not (t.written >> i) & 1}


def chain(s, track, src, cur, seds, masked=True):
    """the tracks of turning sed through `seds`, each run on the last one's track and, when masked,
    its mask (spec 11), as the firmware does it"""
    outs, own = [], 0
    for sed in seds:
        cur = generate(dataclasses.replace(s, sed=sed), track, src, cur, own if masked else 0)
        own = cur.written
        outs.append(cur)
    return outs


class Own(unittest.TestCase):
    """spec 11: style 0 does not read the generator's own steps as the user's accents. generate()
    takes the steps the last run wrote (own, bit i is step i) and gives back those it wrote
    (Track.written); the accents of hat and prc style 0 and the "style 0 without accents is
    style 1" check (spec 9) leave the masked steps out"""

    USER = '......X.......X.'       # the review's track: the user's accents on steps 7 and 15

    def test_written_is_every_step_hit(self):
        cur = user_track()
        src = generate(S('prc', sed=3), 3, KICK)
        mine = {i for i in range(NUM_STEPS) if cur.active[i] and cur.steps[i].vol >= 64}
        for s in Always().all_settings():
            out = generate(s, 2, src if s.typ == gen.TYP_PRP else KICK, cur)
            on = {i for i in range(NUM_STEPS) if out.active[i]}
            self.assertEqual(out.written, mask_of(on - (mine if s.sty == 0 else set())), s)
        self.assertEqual(generate(Settings(), 2, KICK, cur, mask_of(range(64))).written, 0)
        self.assertEqual(generate(S('prp', sty=1, src=4), 3, cur, cur).written, 0)
        self.assertEqual(generate(S('hat'), 5).copy().written, generate(S('hat'), 5).written)

    def test_prc_sty0_sed_there_and_back(self):
        # the review's finding 1: prc sty 0 var +1 (den 3 lev -13) on the user's 7 and 15, sed 0 -> 5 -> 0
        # gives the first track again; without the mask var's hit at 127 became an accent
        s = S('prc', sty=0)
        cur = Track.from_pattern(self.USER)
        first, _, again = chain(s, 2, KICK, cur, (0, 5, 0))
        self.assertEqual(again, first)
        self.assertEqual(again.written, first.written)
        self.assertEqual(kept(again), {b * BAR + p for b in range(4) for p in (6, 14)})
        self.assertNotEqual(chain(s, 2, KICK, cur, (0, 5, 0), masked=False)[2], first)

    def test_hat_sty0_loud_ghosts_keep_the_count(self):
        # finding 1: hat sty 0 at lev -6 writes ghosts at 64 and above; over sed turns the number of
        # hits stays, only the user's 8 accents are kept, and sed 0 again is the first track
        s = S('hat', sty=0, lev=-6)
        cur = Track.from_pattern(self.USER)
        outs = chain(s, 5, None, cur, (0, 1, 2, 3, 4, 5, 0))
        self.assertTrue(any(o.steps[i].vol >= 64 for o in outs for i in range(NUM_STEPS) if (o.written >> i) & 1))
        self.assertEqual([sum(o.active) for o in outs], [40] * 7)
        self.assertEqual([len(kept(o)) for o in outs], [8] * 7)
        self.assertEqual(outs[-1], outs[0])
        self.assertGreater(sum(chain(s, 5, None, cur, (0, 1, 2, 3, 4, 5, 0), masked=False)[-1].active), 40)

    def test_a_masked_step_edited_is_no_accent_an_added_one_is(self):
        # spec 11: a masked step edited by hand up to 127 is no accent (the next change writes it
        # over, spec 3); an accent added on an empty step is the user's
        out = generate(S('hat', sty=0, den=4, var=0), 5, None, Track.from_pattern('..X...X...X...X.'))
        ed = out.copy()
        ghost = min(i for i in range(BAR) if (out.written >> i) & 1)
        empty = min(i for i in range(BAR) if not out.active[i])
        ed.steps[ghost].vol = 127
        ed.active[empty] = True
        ed.steps[empty] = Step(vol=127, p1nr=9, p1val=4)
        nxt = generate(S('hat', sty=0, den=4, var=0, sed=1), 5, None, ed, out.written)
        self.assertEqual(kept(nxt), kept(out) | {empty})
        self.assertEqual(nxt.steps[empty], ed.steps[empty])
        self.assertFalse(nxt.active[ghost] and nxt.steps[ghost].vol == 127)

    def test_every_accent_masked_is_sty1(self):
        # spec 11 with 9: when every step at 64 or above is the generator's, style 0 works as style 1
        # (seeded with 0), so a run on its own track writes that track again; without the mask hat's
        # ghosts at lev -4 (64 and above) and prc's var hit at 127 were accents
        for typ, track, lev in (('hat', 5, -4), ('prc', 2, -13)):
            s = S(typ, sty=0, sed=4, lev=lev)
            first = generate(s, track, KICK)
            self.assertEqual(kept(first), set())
            self.assertEqual(generate(s, track, KICK, first, first.written), first)
            self.assertNotEqual(generate(s, track, KICK, first), first)

    def test_a_short_track_masks_as_it_plays(self):
        # the mask is of the steps as stored: on a 16-step track bit 6 masks step 7 wherever it plays
        # (7, 23, 39, 55); bits past the length mask nothing
        cur = Track.from_pattern('..X...X...X...X.')
        s = S('hat', sty=0, den=3, var=0)
        out = generate(s, 5, None, cur, mask_of([6]))
        self.assertEqual(kept(out), {b * BAR + p for b in range(4) for p in (2, 10, 14)})
        self.assertEqual(generate(s, 5, None, cur, mask_of([22, 38, 54, 63])), generate(s, 5, None, cur))

    def test_other_kinds_and_styles_ignore_the_mask(self):
        cur = user_track()
        src = generate(S('prc', sed=3), 3, KICK)
        for s in Always().all_settings():
            if s.sty != 0:
                k = src if s.typ == gen.TYP_PRP else KICK
                self.assertEqual(generate(s, 2, k, cur, mask_of(range(64))), generate(s, 2, k, cur), s)


class TypeDefaultsOnATrack(unittest.TestCase):
    """spec 11: choosing hat or prc on a track with the user's accents (a step at vel >= 64 as it
    plays, outside the mask) loads sty 0 instead of 1; the other defaults stay the table's"""

    DRAWN = '..X...X...X...X.'      # accents drawn at ---

    def test_hat_and_prc_on_drawn_accents_start_at_sty0(self):
        cur = Track.from_pattern(self.DRAWN)
        cur.steps[6].p1nr, cur.steps[6].p1val = 20, 7
        base = Settings(sed=3, src=2, shf=-1)
        for typ, track in (('hat', 5), ('prc', 2)):
            s = apply_type_defaults(base, typ, cur)
            self.assertEqual(s, dataclasses.replace(apply_type_defaults(base, typ), sty=0))
            out = generate(s, track, KICK, cur)
            for b in range(4):                    # the accents kept as they are, ghosts added
                for p in (2, 6, 10, 14):
                    self.assertEqual(out.steps[b * BAR + p], cur.steps[p])
                self.assertGreater(len(bar_hits(out, b)), 4)
            self.assertEqual(kept(out), {b * BAR + p for b in range(4) for p in (2, 6, 10, 14)})

    def test_empty_or_the_generators_own_steps_stay_sty1(self):
        ghosts, past = no_accents()
        for typ, track in (('hat', 5), ('prc', 2)):
            for cur in (None, Track(), ghosts, past):
                self.assertEqual(apply_type_defaults(Settings(), typ, cur).sty, 1)
            own = generate(S('hat'), track, KICK)
            self.assertEqual(apply_type_defaults(Settings(), typ, own, own.written).sty, 1)
            self.assertEqual(apply_type_defaults(Settings(), typ, own).sty, 0)        # without its mask
            mixed = own.copy()                    # one accent drawn on an empty step: the user's
            empty = min(i for i in range(NUM_STEPS) if not own.active[i])
            mixed.active[empty] = True
            self.assertEqual(apply_type_defaults(Settings(), typ, mixed, own.written).sty, 0)

    def test_as_it_plays_and_the_mask_on_a_short_track(self):
        cur = Track.from_pattern('......X.........')
        self.assertEqual(apply_type_defaults(Settings(), 'hat', cur, mask_of([6])).sty, 1)
        self.assertEqual(apply_type_defaults(Settings(), 'hat', cur, mask_of([22, 38])).sty, 0)
        legacy = Track.from_pattern('.....x..........')
        legacy.steps[5].vol = 0x80 | 40           # the legacy active flag: 40, no accent
        self.assertEqual(apply_type_defaults(Settings(), 'prc', legacy).sty, 1)

    def test_sub_rar_prp_and_off_do_not_change(self):
        cur = Track.from_pattern(self.DRAWN)
        base = Settings(sty=4, den=9, sed=7)
        for typ in ('sub', 'rar', 'prp', '---', 6, 255):
            self.assertEqual(apply_type_defaults(base, typ, cur), apply_type_defaults(base, typ))


class Vectors(unittest.TestCase):
    def test_golden_vectors(self):
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vectors.json')
        if not os.path.exists(path):
            self.skipTest('no vectors.json: run tools/gen/vectors.py')
        with open(path) as f:
            v = json.load(f)
        self.assertEqual(v['vel_table'], list(gen.VEL_TABLE))
        for p in v['prng']:
            r = gen.Rng(*p['bytes'])
            self.assertEqual(r.x, p['state'])
            self.assertEqual([r.next() for _ in p['next']], p['next'])
        self.assertGreaterEqual(len(v['cases']), 150)
        for c in v['cases']:
            st = c['settings']
            s = Settings(typ=gen.typ_code(st['typ']), sty=st['sty'], den=st['den'], lev=st['lev'],
                         var=st['var'], not_=st['not'], sed=st['sed'], src=st['src'], shf=st['shf'])
            own = gen.bars_mask(c['own']) if 'own' in c else 0
            out = generate(s, c['track'], Track.from_json(c['src']), Track.from_json(c['cur']), own)
            self.assertEqual(out.to_json(), c['out'], c['name'])
            self.assertEqual(out.needs_vol, c['vol'], c['name'])
            self.assertEqual(gen.mask_bars(out.written), c['own_out'], c['name'])
        masked = {c['settings']['typ'] for c in v['cases'] if any(c.get('own', ()))}
        self.assertTrue({'hat', 'prc'} <= masked, masked)       # spec 11: cases with a mask
        self.assertIn('own', v['format'])
        self.assertIn('own_out', v['format'])


if __name__ == '__main__':
    unittest.main()
