#!/usr/bin/env python3
"""LICHOMAT v0.8 pattern generator: the reference prototype (E10.2).

Implements docs/superpowers/specs/2026-11-11-groove-generator-design.md, sections 3-5:
the five kinds of pattern (hat, sub, prc, rar, prp), the per-kind defaults, the
PRNG, the ranking of places, the fixed draw order and the dB -> velocity rule. The
firmware port (E10.3, PatternGen.c) is tested byte for byte against the vectors this
module writes (tools/gen/vectors.py), so every rule below is spelled out; the points
the spec leaves open are marked "CHOICE" where they are decided:
- a step is read as it plays: a track shorter than 64 steps is repeated to 64 (the
  spec says so for the prp source; here also for the kick and for sty 0's accents);
- the steps the generator does not hit are written clean and inactive, note 60 + not;
- hat sty 0: the free places are found bar by bar, around that bar's accents;
- hat var +n: the next n places of the ranking (2:2); var -n: the last n of the den (1:2);
- sub: "the last hit before a kick" is the last hit after the kick before it; sty 8 is
  one bar from the seed, repeated, and den keeps the best ranked hits (weights 1);
- prc: bar 2 moves bar 1's accent with the highest ranking key (the first of its ranking)
  to the best ranked free place of bar 2's own keys (places 16-31): not an accent of bar 1,
  not a kick step, the style's weights (spec 9; no draw for it, so den k -> k+1 never
  changes which accent moves); ghosts are ranked by bar 1's keys in every bar, on the step
  before the accent within the bar (step 1's is step 16 of the same bar) and never on a
  kick step (spec 8), offsets of bar 2 from places 16-31; var +n weights steps 13-16 x4;
  var -n does nothing on sty 0 (its accents are the user's, which the generator never
  touches);
- rar: den 0 writes nothing; sty 3 and 4 rank all 64 steps with 64 keys drawn third,
  away from the kick of track src; den >= 4 on sty 1 is den 4;
- prp: an accent of the source is an active step with vel >= 64; sty 1 copies vel and
  prob without the slide flag; sty 3 and 4 take the source accent's prob without the
  slide flag (spec 8), the velocity from lev and note 60 + not;
- the level offset of every kind is drawn (hat's range for sub, rar, prp); a level is
  clamped to 0...-30 dB; a sty outside the kind's styles is clamped into them.

    from gen import Settings, generate, apply_type_defaults
    s = apply_type_defaults(Settings(), 'hat')
    out = generate(s, track_index=5, src_track=kick, cur_track=None)

The CLI prints a generated track as a 4 x 16 grid (velocity, condition, note):

    gen.py hat [--sty N] [--den N] [--lev DB] [--var N] [--not N] [--sed N]
               [--src N] [--shf N] [--track N] [--src-pattern P] [--cur-pattern P]

Unset parameters take the kind's defaults (spec 3). A pattern P is a string of 1-64
steps: '.' off, 'X' velocity 127, 'o' 90, 'x' 40; '|' and spaces are ignored.

Pure Python 3 (3.9+), standard library only, no I/O outside the CLI.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of the
repository.
"""
from __future__ import annotations

import argparse
import dataclasses
import math
import sys
from typing import List, Optional, Sequence

# --- the firmware's step format (Core/Inc/Preset/SeqStepTypes.h, sequencer.h) -------------

NUM_STEPS = 64          # 4 bars of 16
BAR = 16
NUM_BARS = 4
NUM_TRACKS = 7
DEFAULT_NOTE = 60       # SEQ_DEFAULT_NOTE: the voice's own pitch
NO_AUTOMATION = 0xFF    # param1Nr / param2Nr: no p-lock
SHIFT_NONE = 7          # StepTiming.shift nibble: 7 is no shift (seq_clearTrack)
VOLUME_MASK = 0x7F      # Step.volume bits 0-6; bit 7 is the legacy active flag
PROB_MASK = 0x7F        # Step.prob bits 0-6; bit 7 is the slide flag (v0.3)
ACCENT_VEL = 64         # spec 3: an accent is a step with vel >= 64

# Step.prob: 0-100 %, then the trig conditions 101-115 (seq_core.c seqc_plays)
COND_1_2, COND_2_2, COND_1_3, COND_2_3, COND_3_3 = 101, 102, 103, 104, 105
COND_1_4, COND_2_4, COND_3_4, COND_4_4 = 106, 107, 108, 109
COND_AB = {101: (1, 2), 102: (2, 2), 103: (1, 3), 104: (2, 3), 105: (3, 3),
           106: (1, 4), 107: (2, 4), 108: (3, 4), 109: (4, 4)}
COND_NAMES = {101: '1:2', 102: '2:2', 103: '1:3', 104: '2:3', 105: '3:3', 106: '1:4',
              107: '2:4', 108: '3:4', 109: '4:4', 110: 'Fil', 111: '!Fl', 112: '1st',
              113: '!1s', 114: 'Pre', 115: '!Pr'}
FLAM_HITS = (1, 2, 3, 4, 6, 8)   # StepTiming.flam index 0-5
FLAM_4 = 3                        # the index of 4 hits

# --- the generator's parameters (spec 3) ----------------------------------------------------

TYP_OFF, TYP_HAT, TYP_SUB, TYP_PRC, TYP_RAR, TYP_PRP = range(6)
TYP_NAMES = ('---', 'hat', 'sub', 'prc', 'rar', 'prp')
# the styles each kind has (spec 4); CHOICE: a sty outside them is clamped into them
STY_RANGE = {TYP_HAT: (0, 4), TYP_SUB: (1, 8), TYP_PRC: (0, 4), TYP_RAR: (1, 4), TYP_PRP: (1, 4)}
# selecting typ loads sty den lev var not (spec 3); sed, src and shf stay
TYPE_DEFAULTS = {
    TYP_HAT: dict(sty=1, den=7, lev=-15, var=1, not_=0),
    TYP_SUB: dict(sty=1, den=0, lev=-3, var=0, not_=-2),
    TYP_PRC: dict(sty=1, den=3, lev=-13, var=1, not_=0),
    TYP_RAR: dict(sty=1, den=2, lev=0, var=0, not_=0),
    TYP_PRP: dict(sty=1, den=4, lev=-15, var=0, not_=0),
}
RANGES = dict(typ=(0, 5), sty=(0, 8), den=(0, 16), lev=(-30, 0), var=(-4, 4),
              not_=(-24, 24), sed=(0, 99), src=(1, 7), shf=(-8, 8))


def typ_code(typ) -> int:
    """a kind as its code 0-5 (the byte the seed hashes): '---' 0, hat 1 ... prp 5"""
    if isinstance(typ, str):
        return TYP_NAMES.index(typ)
    return int(typ)


@dataclasses.dataclass(frozen=True)
class Settings:
    """the ten parameters of one track's generator (spec 3); `not_` is the menu's `not`"""
    typ: int = TYP_OFF
    sty: int = 0
    den: int = 0
    lev: int = 0
    var: int = 0
    not_: int = 0
    sed: int = 0
    src: int = 1        # 1-7, a track number as the panel shows it
    shf: int = 0

    def clamped(self) -> 'Settings':
        """every parameter clamped into its menu range (spec 3), sty into the kind's styles"""
        v = {k: min(max(int(getattr(self, k)), lo), hi) for k, (lo, hi) in RANGES.items()}
        if v['typ'] in STY_RANGE:
            lo, hi = STY_RANGE[v['typ']]
            v['sty'] = min(max(v['sty'], lo), hi)
        return Settings(**v)

    def as_dict(self) -> dict:
        return dict(typ=TYP_NAMES[self.typ], sty=self.sty, den=self.den, lev=self.lev,
                    var=self.var, **{'not': self.not_}, sed=self.sed, src=self.src, shf=self.shf)


def apply_type_defaults(settings: Settings, typ) -> Settings:
    """settings with typ chosen: sty den lev var not from the kind's table (spec 3), the
    rest kept; '---' only switches the generator off"""
    t = typ_code(typ)
    return dataclasses.replace(settings, typ=t, **TYPE_DEFAULTS.get(t, {}))


# --- tracks ---------------------------------------------------------------------------------

@dataclasses.dataclass
class Step:
    """one step: the 7 bytes of Step, the StepTiming nibbles, the fine nibble (signed)"""
    vol: int = 127
    prob: int = 100
    note: int = DEFAULT_NOTE
    p1nr: int = NO_AUTOMATION
    p1val: int = 0
    p2nr: int = NO_AUTOMATION
    p2val: int = 0
    flam: int = 0
    shift: int = SHIFT_NONE
    fine: int = 0

    def copy(self) -> 'Step':
        return dataclasses.replace(self)

    def step_bytes(self) -> bytes:
        return bytes((self.vol, self.prob, self.note, self.p1nr, self.p1val, self.p2nr, self.p2val))

    def timing_byte(self) -> int:
        return (self.flam & 0x0F) | ((self.shift & 0x0F) << 4)


class Track:
    """a track as the sequencer holds it: 64 steps, the 64 active bits (seq_mainSteps) and
    the length 1-64. `needs_vol` is set by generate(): the generator wrote a velocity
    below 127, so the voice's vol (velocity -> volume) must be on (spec 3)"""

    def __init__(self, steps: Optional[Sequence[Step]] = None,
                 active: Optional[Sequence[bool]] = None, length: int = 16):
        self.steps: List[Step] = [s.copy() for s in steps] if steps else [Step() for _ in range(NUM_STEPS)]
        self.active: List[bool] = list(active) if active else [False] * NUM_STEPS
        self.length = length
        self.needs_vol = False
        assert len(self.steps) == NUM_STEPS and len(self.active) == NUM_STEPS and 1 <= length <= NUM_STEPS

    def copy(self) -> 'Track':
        t = Track(self.steps, self.active, self.length)
        t.needs_vol = self.needs_vol
        return t

    @classmethod
    def from_pattern(cls, pattern: str, note: int = DEFAULT_NOTE) -> 'Track':
        """a track from a string of 1-64 steps: '.' off, 'X' 127, 'o' 90, 'x' 40; '|' and
        spaces ignored; the length is the number of steps"""
        cells = [c for c in pattern if c not in '| ']
        assert 1 <= len(cells) <= NUM_STEPS, pattern
        t = cls(length=len(cells))
        vel = {'X': 127, 'o': 90, 'x': 40}
        for i, c in enumerate(cells):
            if c != '.':
                t.active[i] = True
                t.steps[i].vol = vel[c]
                t.steps[i].note = note
        return t

    def played(self) -> 'Track':
        """the 64 steps as they play: a track shorter than 64 steps repeated to 64 (spec 4.5);
        steps past the length are copies of the steps they repeat"""
        if self.length == NUM_STEPS:
            return self.copy()
        idx = [i % self.length for i in range(NUM_STEPS)]
        return Track([self.steps[i] for i in idx], [self.active[i] for i in idx], NUM_STEPS)

    def bars(self) -> List[int]:
        """the active bits as seq_mainSteps holds them: one 16-bit word a bar, bit n step n"""
        return [sum(1 << n for n in range(BAR) if self.active[b * BAR + n]) for b in range(NUM_BARS)]

    def to_json(self) -> dict:
        fine = bytes(((self.steps[2 * i].fine & 0x0F) | ((self.steps[2 * i + 1].fine & 0x0F) << 4))
                     for i in range(NUM_STEPS // 2))
        return dict(length=self.length, active=self.bars(),
                    steps=b''.join(s.step_bytes() for s in self.steps).hex(),
                    timing=bytes(s.timing_byte() for s in self.steps).hex(), fine=fine.hex())

    @classmethod
    def from_json(cls, d: Optional[dict]) -> Optional['Track']:
        if d is None:
            return None
        raw, tim, fine = bytes.fromhex(d['steps']), bytes.fromhex(d['timing']), bytes.fromhex(d['fine'])
        steps = []
        for i in range(NUM_STEPS):
            b = raw[7 * i:7 * i + 7]
            nib = (fine[i // 2] >> (4 * (i % 2))) & 0x0F
            steps.append(Step(b[0], b[1], b[2], b[3], b[4], b[5], b[6], tim[i] & 0x0F, tim[i] >> 4,
                              nib - 16 if nib & 0x08 else nib))
        active = [bool((d['active'][i // BAR] >> (i % BAR)) & 1) for i in range(NUM_STEPS)]
        return cls(steps, active, d['length'])

    def __eq__(self, other) -> bool:
        return isinstance(other, Track) and self.to_json() == other.to_json()


# --- spec 5: the PRNG, the ranking, dB -> velocity ------------------------------------------

def fnv1a(data: bytes) -> int:
    """32-bit FNV-1a: x = 2166136261, then x = ((x XOR b) x 16777619) mod 2^32 a byte"""
    x = 2166136261
    for b in data:
        x = ((x ^ b) * 16777619) & 0xFFFFFFFF
    return x


class Rng:
    """spec 5: the state from FNV-1a over the bytes sed, typ, sty, track (0-6), 1 if that
    is 0; then xorshift32 (13, 17, 5); rand(n) is the new state % n"""

    def __init__(self, sed: int, typ: int, sty: int, track: int):
        x = fnv1a(bytes(b & 0xFF for b in (sed, typ, sty, track)))
        self.x = x if x else 1
        self.draws = 0

    def next(self) -> int:
        x = self.x
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.x = x
        self.draws += 1
        return x

    def rand(self, n: int) -> int:
        return self.next() % n


def rank(draws: Sequence[int], weights: Sequence[int], allowed=None) -> List[int]:
    """spec 5: the places with weight > 0 (and allowed), best first: key = draw x weight,
    the largest key first, a tie to the lower place. `draws` are rand(65536) values"""
    cand = [p for p in range(len(draws)) if weights[p] > 0 and (allowed is None or allowed(p))]
    return sorted(cand, key=lambda p: (-draws[p] * weights[p], p))


def _vel(db: int) -> int:
    return max(1, math.floor(127 * 10 ** (db / 20) + 0.5))


VEL_TABLE = tuple(_vel(-d) for d in range(31))   # index -dB: 127 113 101 90 80 71 64 ... 4


def vel_db(db: int) -> int:
    """spec 5: vel = round(127 x 10^(dB/20)), at least 1, from the table of 0...-30 dB;
    CHOICE: a level outside 0...-30 dB (lev plus a place's offset) is clamped into it"""
    return VEL_TABLE[-min(max(db, -30), 0)]


def plays(prob: int, loop: int) -> Optional[bool]:
    """whether a step with this prob plays in the track's loop `loop` (0 at the start):
    True / False for 100 % and the A:B conditions, None for a probability below 100 %
    and the conditions that depend on the performance (Fil ... !Pr)"""
    p = prob & PROB_MASK
    if p == 100 or p > 115:
        return True
    if p in COND_AB:
        a, b = COND_AB[p]
        return loop % b == a - 1
    return None


# --- the grids of spec 4 (places 0-15 of a bar are its steps 1-16) --------------------------

QUARTERS = frozenset((0, 4, 8, 12))     # steps 1 5 9 13
OFFBEATS = frozenset((2, 6, 10, 14))    # steps 3 7 11 15
EVENS = frozenset(range(1, 16, 2))      # steps 2 4 6 ... 16
DEFAULT_KICK = frozenset(QUARTERS)


def _w(fn) -> List[int]:
    return [fn(p) for p in range(BAR)]


# 4.1 hat: the accents and the ghosts' weights per sty
HAT_ACCENTS = {1: OFFBEATS, 2: OFFBEATS | QUARTERS, 3: OFFBEATS, 4: OFFBEATS}
HAT_WEIGHTS = {
    0: _w(lambda p: 4 if p in EVENS else 2 if p in QUARTERS else 1),
    1: _w(lambda p: 4 if p in EVENS else 2 if p in QUARTERS else 0),
    2: _w(lambda p: 4 if p in EVENS else 0),
    3: _w(lambda p: 8 if p in QUARTERS else 3 if p in EVENS else 0),
    4: _w(lambda p: 1),
}

# 4.2 sub: one bar per style (7: bars A B A B); 8 from the seed, a figure a quarter
SUB_BARS = {
    1: ('...x...x...x...x',),
    2: ('..xx..xx..xx..xx',),
    3: ('.xxx.xxx.xxx.xxx',),
    4: ('..x...x...x...x.',),
    5: ('..xx..x...xx..x.',),
    6: ('......xx......x.',),
    7: ('..xx..xx..xx....', '..xx..xx..xx.xxx'),
}
SUB_FIGURES = (('...x', 3), ('..xx', 4), ('.xxx', 1), ('..x.', 2), ('....', 1))
SUB_FIGURE_TOTAL = sum(w for _, w in SUB_FIGURES)   # 11

# 4.3 prc: the accents' weights per sty (0: the user's accents; its weights are for var +n)
PRC_WEIGHTS = {
    0: _w(lambda p: 1),
    1: _w(lambda p: 4 if p % 4 in (1, 3) else 2 if p % 4 == 2 else 0),
    2: _w(lambda p: 6 if p % 4 == 2 else 2 if p % 4 in (1, 3) else 0),
    3: _w(lambda p: 8 if p in (3, 6, 11, 14) else 1),
    4: _w(lambda p: 1),
}
PRC_VAR_LATE = 4    # CHOICE: var +n prefers steps 13-16 by this factor on the weights

# 4.5 prp sty 2: the complement's weights
PRP_FILL_WEIGHTS = _w(lambda p: 4 if p in EVENS else 3 if p in OFFBEATS else 1)

# spec 5, draw 2: the level offset of a place, in dB, per kind (hat -3..+2, prc +-2);
# CHOICE: sub, rar and prp draw the hat's range too, and use none of it
OFFSET_RANGE = {TYP_PRC: (-2, 2)}
OFFSET_DEFAULT = (-3, 2)


# --- generate -------------------------------------------------------------------------------

class _Out:
    """the track being written: all 64 steps clean (spec 3: no p-locks, flam, shift or
    fine; note 60 + not; prob 100; inactive), length 64"""

    def __init__(self, note: int):
        self.note = note
        self.t = Track(length=NUM_STEPS)
        for s in self.t.steps:
            s.note = note
        self.written = [False] * NUM_STEPS

    def hit(self, i: int, vel: int, prob: int = 100, note: Optional[int] = None, flam: int = 0):
        s = self.t.steps[i]
        s.vol, s.prob, s.note, s.flam = vel, prob, self.note if note is None else note, flam
        self.t.active[i] = True
        self.written[i] = True

    def keep(self, i: int, src: Track):
        """step i as it is in src (the user's accent, spec 3: untouched)"""
        self.t.steps[i] = src.steps[i].copy()
        self.t.active[i] = True

    def done(self) -> Track:
        self.t.needs_vol = any(self.written[i] and self.t.active[i] and (self.t.steps[i].vol & VOLUME_MASK) < 127
                               for i in range(NUM_STEPS))
        return self.t


def _clamp_note(n: int) -> int:
    return min(max(n, 0), 127)


def _accents(view: Track, bar: int) -> set:
    """the places of a bar's accents: active steps with vel >= 64"""
    return {p for p in range(BAR) if view.active[bar * BAR + p]
            and (view.steps[bar * BAR + p].vol & VOLUME_MASK) >= ACCENT_VEL}


def kick_places(s: Settings, track_index: int, src_track: Optional[Track]) -> frozenset:
    """spec 4.2: the kick is the first bar of track src; when that is empty or src is this
    track, steps 1 5 9 13. Places 0-15, the same in every bar"""
    if src_track is None or s.src - 1 == track_index:
        return DEFAULT_KICK
    view = src_track.played()
    k = frozenset(p for p in range(BAR) if view.active[p])
    return k or DEFAULT_KICK


def generate(settings: Settings, track_index: int, src_track: Optional[Track] = None,
             cur_track: Optional[Track] = None) -> Track:
    """the track the generator writes on track `track_index` (0-6) with these settings.
    src_track is track settings.src (the kick for sub, prc and rar; the source for prp),
    cur_track the track's current content (read by hat and prc sty 0). Returns a new
    Track; typ '---' and prp with src on itself return cur_track unchanged"""
    s = settings.clamped()
    cur = cur_track if cur_track is not None else Track()
    if s.typ == TYP_OFF or (s.typ == TYP_PRP and s.src - 1 == track_index):
        out = cur.copy()
        out.needs_vol = False
        return out

    # spec 5: the state, then the draws in their fixed order; nothing below depends on
    # den, lev, var, not, src, shf or the input tracks before the draws are done
    rng = Rng(s.sed, s.typ, s.sty, track_index)
    grid = 2 * BAR if s.typ == TYP_PRC else BAR
    draws = [rng.rand(65536) for _ in range(grid)]                       # 1. the places' keys
    lo, hi = OFFSET_RANGE.get(s.typ, OFFSET_DEFAULT)
    offsets = [lo + rng.rand(hi - lo + 1) for _ in range(grid)]          # 2. the level offsets
    out = _Out(_clamp_note(DEFAULT_NOTE + s.not_))                       # 3. per kind, below
    if s.typ == TYP_HAT:
        _hat(s, out, draws, offsets, cur.played())
    elif s.typ == TYP_SUB:
        figures = [rng.rand(SUB_FIGURE_TOTAL) for _ in range(4)] if s.sty == 8 else []
        _sub(s, out, draws, figures, kick_places(s, track_index, src_track))
    elif s.typ == TYP_PRC:
        var_draws = [rng.rand(65536) for _ in range(BAR)]               # spec 9: no draw for the move
        _prc(s, out, draws, offsets, var_draws, kick_places(s, track_index, src_track), cur.played())
    elif s.typ == TYP_RAR:
        steps = [rng.rand(65536) for _ in range(NUM_STEPS)] if s.sty in (3, 4) else []
        _rar(s, out, steps, kick_places(s, track_index, src_track))
    elif s.typ == TYP_PRP:
        _prp(s, out, draws, (src_track if src_track is not None else Track()).played())
    return out.done()


# 4.1 ----------------------------------------------------------------------------------------

def _hat(s: Settings, out: _Out, draws, offsets, view: Track):
    weights = HAT_WEIGHTS[s.sty]
    for b in range(NUM_BARS):
        acc = _accents(view, b) if s.sty == 0 else HAT_ACCENTS[s.sty]
        for p in acc:
            if s.sty == 0:
                out.keep(b * BAR + p, view)
            else:
                out.hit(b * BAR + p, 127)
        order = rank(draws, weights, lambda p: p not in acc)
        n = min(s.den, len(order))
        for r, p in enumerate(order[:n + max(s.var, 0)]):
            prob = 100
            if r >= n:
                prob = COND_2_2                 # var +n: the next n in the ranking, bars 5-8
            elif s.var < 0 and r >= n - min(-s.var, n):
                prob = COND_1_2                 # var -n: the last n of the den, bars 1-4
            out.hit(b * BAR + p, vel_db(s.lev + offsets[p]), prob)


# 4.2 ----------------------------------------------------------------------------------------

def _sub(s: Settings, out: _Out, draws, figures, kick: frozenset):
    if s.sty == 8:
        bar = ''
        for r in figures:
            for fig, w in SUB_FIGURES:
                if r < w:
                    bar += fig
                    break
                r -= w
        bars = (bar,)
    else:
        bars = SUB_BARS[s.sty]
    pats = []
    for b in range(NUM_BARS):
        pat = {p for p, c in enumerate(bars[b % len(bars)]) if c == 'x' and p not in kick}
        if s.sty == 8 and s.den > 0:            # den: at most den hits a bar, the best ranked
            pat = set(rank(draws, [1] * BAR, lambda p: p in pat)[:s.den])
        pats.append(pat)
    hits = [b * BAR + p for b in range(NUM_BARS) for p in sorted(pats[b])]
    hitset = set(hits)
    kicks = [b * BAR + k for b in range(NUM_BARS) for k in sorted(kick)]
    loud = set()
    for k in kicks:                              # the last hit before each kick, after the one before
        i = (k - 1) % NUM_STEPS
        while i not in kicks:
            if i in hitset:
                loud.add(i)
                break
            i = (i - 1) % NUM_STEPS
    for i in hits:
        second = s.var != 0 and (i % BAR) // 4 in (1, 3)   # quarters 2 and 4
        out.hit(i, 127 if i in loud else vel_db(s.lev),
                note=_clamp_note(out.note + s.var) if second else None)


# 4.3 ----------------------------------------------------------------------------------------

def _prc(s: Settings, out: _Out, draws, offsets, var_draws, kick: frozenset, view: Track):
    weights = PRC_WEIGHTS[s.sty]
    if s.sty == 0:
        bar_acc = [_accents(view, b) for b in range(NUM_BARS)]
        for b in range(NUM_BARS):
            for p in bar_acc[b]:
                out.keep(b * BAR + p, view)
    else:
        order = rank(draws[:BAR], weights, lambda p: p not in kick)
        a1 = set(order[:s.den])
        a2 = set(a1)
        if a1:                                   # bar 2: bar 1's top-ranked accent moved (spec 9)
            dest = rank(draws[BAR:], weights, lambda p: p not in a1 and p not in kick)
            if dest:                             # to the best free place of keys 16-31
                a2 = (a1 - {order[0]}) | {dest[0]}
        bar_acc = [a1, a2, a1, a2]               # bars 3-4 = bars 1-2
        for b in range(NUM_BARS):
            for p in bar_acc[b]:
                out.hit(b * BAR + p, 127)
    ghosts = min(3, (s.den + 1) // 2)
    bar_ghosts = []
    for b in range(NUM_BARS):                    # ghosts on the step before an accent
        acc = bar_acc[b]
        places = {(p - 1) % BAR for p in acc} - acc - kick    # spec 8: never on a kick step
        g = rank(draws[:BAR], [1] * BAR, lambda p: p in places)[:ghosts]
        for q in g:
            out.hit(b * BAR + q, vel_db(s.lev + offsets[(b % 2) * BAR + q]))
        bar_ghosts.append(set(g))
    last = 3 * BAR                               # var: bar 4, so bar 8 of each phrase
    if s.var > 0:
        late = [weights[p] * (PRC_VAR_LATE if p >= 12 else 1) for p in range(BAR)]
        busy = bar_acc[3] | bar_ghosts[3]
        for p in rank(var_draws, late, lambda p: p not in busy and p not in kick)[:s.var]:
            out.hit(last + p, 127, COND_2_2)
    elif s.var < 0 and s.sty != 0:               # sty 0: the user's accents stay untouched
        for p in rank(var_draws, [1] * BAR, lambda p: p in bar_acc[3])[:-s.var]:
            out.t.steps[last + p].prob = COND_1_2


# 4.4 ----------------------------------------------------------------------------------------

def _rar(s: Settings, out: _Out, steps, kick: frozenset):
    if s.den == 0:                               # CHOICE: den 0 is no event, in every style
        return
    if s.sty == 1:
        out.hit(3 * BAR, 127, COND_4_4 if s.den == 1 else COND_2_2)
        if s.den >= 3:
            for i in range(3 * BAR + 8, 3 * BAR + 12):
                out.hit(i, 127, COND_4_4 if s.den == 3 else COND_2_2, flam=FLAM_4)
    elif s.sty == 2:
        out.hit(0, 127, COND_1_4 if s.den == 1 else COND_1_2 if s.den <= 3 else 100)
    else:
        fixed = set()
        n = min(s.den, 8)
        if s.sty == 4:
            out.hit(0, 127, COND_1_2)
            out.hit(3 * BAR, 127, COND_4_4)
            fixed = {0, 3 * BAR}
            n = min(max(s.den - 2, 0), 8)
        weights = [2 if i // BAR in (0, 3) else 1 for i in range(NUM_STEPS)]
        for i in rank(steps, weights, lambda i: i % BAR not in kick and i not in fixed)[:n]:
            out.hit(i, 127, 25)


# 4.5 ----------------------------------------------------------------------------------------

def _prp(s: Settings, out: _Out, draws, src: Track):
    def accent(i):
        return src.active[i] and (src.steps[i].vol & VOLUME_MASK) >= ACCENT_VEL
    if s.sty == 1:                               # a copy shifted by shf
        for i in range(NUM_STEPS):
            if src.active[i]:
                st = src.steps[i]
                out.hit((i + s.shf) % NUM_STEPS, st.vol & VOLUME_MASK, st.prob & PROB_MASK,
                        _clamp_note(st.note + s.not_))
    elif s.sty == 2:                             # den accents a bar where the source is silent
        for b in range(NUM_BARS):
            for p in rank(draws, PRP_FILL_WEIGHTS, lambda p: not src.active[b * BAR + p])[:s.den]:
                out.hit(b * BAR + p, 127)
    elif s.sty == 3:                             # a ghost before each accent, where it is empty
        for i in range(NUM_STEPS):
            j = (i - 1) % NUM_STEPS
            if accent(i) and not src.active[j]:  # spec 8: with the accent's condition
                out.hit(j, vel_db(s.lev), src.steps[i].prob & PROB_MASK)
    else:                                        # the accents shifted by shf, as ghosts
        for i in range(NUM_STEPS):
            if accent(i):                        # spec 8: with the accent's condition
                out.hit((i + s.shf) % NUM_STEPS, vel_db(s.lev), src.steps[i].prob & PROB_MASK)


# --- the CLI --------------------------------------------------------------------------------

def grid(track: Track) -> str:
    """the track as 4 bars of 16: velocity, condition (or %), flam, note offset from 60"""
    lines = ['     ' + ''.join('%4d' % (n + 1) for n in range(BAR))]
    any_note = any(track.active[i] and track.steps[i].note != DEFAULT_NOTE for i in range(NUM_STEPS))
    for b in range(NUM_BARS):
        vel, cnd, nte = [], [], []
        for n in range(BAR):
            i = b * BAR + n
            st = track.steps[i]
            if not track.active[i]:
                vel.append('   .'); cnd.append('    '); nte.append('    ')
                continue
            vel.append('%4d' % (st.vol & VOLUME_MASK))
            p = st.prob & PROB_MASK
            c = COND_NAMES.get(p, '%d%%' % p if p < 100 else '')
            if st.flam:
                c = (c + ' ' if c else '') + 'f%d' % FLAM_HITS[st.flam]
            cnd.append('%4s' % c)
            nte.append('%+4d' % (st.note - DEFAULT_NOTE) if st.note != DEFAULT_NOTE else '   0')
        lines.append('bar%d ' % (b + 1) + ''.join(vel))
        if any(c.strip() for c in cnd):
            lines.append('     ' + ''.join(cnd))
        if any_note:
            lines.append('note ' + ''.join(nte))
    return '\n'.join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='print a generated track as a 4 x 16 grid')
    ap.add_argument('typ', choices=TYP_NAMES[1:])
    for name in ('sty', 'den', 'lev', 'var', 'not', 'sed', 'src', 'shf'):
        ap.add_argument('--' + name, type=int)
    ap.add_argument('--track', type=int, help='the destination track 1-7 (default per kind)')
    ap.add_argument('--src-pattern', default='X...X...X...X...',
                    help="track src's steps (default a 4/4 kick)")
    ap.add_argument('--cur-pattern', help="the destination track's steps now (hat, prc sty 0)")
    a = ap.parse_args(argv)
    s = apply_type_defaults(Settings(), a.typ)
    over = {('not_' if k == 'not' else k): getattr(a, k) for k in
            ('sty', 'den', 'lev', 'var', 'not', 'sed', 'src', 'shf') if getattr(a, k) is not None}
    s = dataclasses.replace(s, **over).clamped()
    track = (a.track or {TYP_HAT: 6, TYP_SUB: 2, TYP_PRC: 3, TYP_RAR: 5, TYP_PRP: 4}[s.typ]) - 1
    src = Track.from_pattern(a.src_pattern)
    cur = Track.from_pattern(a.cur_pattern) if a.cur_pattern else None
    out = generate(s, track, src, cur)
    d = s.as_dict()
    print(' '.join('%s %s' % (k, v) for k, v in d.items()) + ', track %d' % (track + 1)
          + (', vol on' if out.needs_vol else ''))
    print(grid(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
