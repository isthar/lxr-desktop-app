#!/usr/bin/env python3
"""The onset timing of a render (plan v0.5 Task 9, spec 9.2): where each hit of a
click starts in a channel of a lichomat-render WAV, against one of three references.

    emu_onsets.py WAV --log TRIG.LOG [--voice V] [options]   against the trigger log
    emu_onsets.py WAV --grid BPM [options]                     against the tick grid
    emu_onsets.py WAV --against WAV2 [options]                 against another render

An onset is the first frame with |x| >= 4,000 after at least 32 frames below 64.
- --log: the timed records of voice V (0-5, default 0) of lichomat-render's
  --trig-log, the last of each block serial (a voice's records in one block start
  together, S2; the one-shot roll's records are untimed and left out);
  each is expected at frames(t) + 128, t its event time in cycles from t0, frames()
  at 44,140.625 Hz. Each record is matched with the onset nearest to it within 96
  frames; a record or an onset without a match is an error.
- --grid BPM: the onsets on a grid of ticks (384 a quarter note) at BPM, from the
  first onset: each one's tick is the nearest. It does not read the firmware's times.
- --against WAV2: the onsets of WAV2 in the same channel, one for one.
The residuals r = onset - reference; c is their median, and the spread is
max(r - c) - min(r - c). --from/--to (seconds) keep the onsets in [from, to).
Exit 0 if the spread is at most --limit and at least --min (each if given), 1 if not
or on an unmatched onset or record, 2 on a usage error.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import argparse
import array
import fractions
import json
import sys
import wave

RATE = fractions.Fraction(353125, 8)     # 44,140.625 Hz
CYCLES_PER_FRAME = fractions.Fraction(552960, 113)
LATENCY = 128                            # frames: two blocks (S4)
CHANNELS = ('L1', 'R1', 'L2', 'R2')
LOUD, QUIET, QUIET_FRAMES, MATCH = 4000, 64, 32, 96


class Usage(Exception):
    pass


def channel(path, name):
    with wave.open(path, 'rb') as w:
        if w.getsampwidth() != 2:
            raise Usage('%s: not a 16-bit WAV' % path)
        n, frames = w.getnchannels(), w.getnframes()
        data = array.array('h')
        data.frombytes(w.readframes(frames))
    if sys.byteorder == 'big':
        data.byteswap()
    c = CHANNELS.index(name)
    if c >= n:
        raise Usage('%s has %d channels, no %s' % (path, n, name))
    return data[c::n]


def onsets(x):
    out, quiet = [], 0
    for f, v in enumerate(x):
        a = -v if v < 0 else v
        if a >= LOUD and quiet >= QUIET_FRAMES:
            out.append(f)
            quiet = 0
        elif a < QUIET:
            quiet += 1
        else:
            quiet = 0
    return out


def records(path, voice):
    """(expected frame, line) of the voice's records, the last of each block serial"""
    last = {}
    order = []
    with open(path) as f:
        for n, line in enumerate(f, 1):
            w = line.split()
            if len(w) != 6:
                raise Usage('%s:%d: not "<cycles> <voice> <src> <serial> <k> <late>"' % (path, n))
            if int(w[1]) != voice or w[2] == 'oneshot':
                continue                 # another voice, or an untimed record (S10)
            serial = int(w[3])
            if serial not in last:
                order.append(serial)
            last[serial] = (float(fractions.Fraction(int(w[0])) / CYCLES_PER_FRAME) + LATENCY, n, int(w[5]))
    return [last[s] for s in order]


def median(v):
    s = sorted(v)
    m = len(s) // 2
    return s[m] if len(s) % 2 else (s[m - 1] + s[m]) / 2


def main(argv=None):
    ap = argparse.ArgumentParser(prog='emu_onsets.py', description=__doc__.split('\n\n')[0])
    ap.add_argument('wav')
    ap.add_argument('--channel', default='L1', choices=CHANNELS)
    ref = ap.add_mutually_exclusive_group(required=True)
    ref.add_argument('--log')
    ref.add_argument('--grid', type=float, metavar='BPM')
    ref.add_argument('--against', metavar='WAV2')
    ap.add_argument('--voice', type=int, default=0, choices=range(6))
    ap.add_argument('--from', dest='start', type=float, default=0.0, metavar='S')
    ap.add_argument('--to', dest='end', type=float, default=None, metavar='S')
    ap.add_argument('--skip-late', action='store_true', help='leave out the records flagged late')
    ap.add_argument('--limit', type=float)
    ap.add_argument('--min', type=float)
    ap.add_argument('--json', metavar='FILE')
    a = ap.parse_args(argv)
    try:
        f0 = a.start * float(RATE)
        f1 = a.end * float(RATE) if a.end is not None else float('inf')
        keep = lambda f: f0 <= f < f1
        on = [f for f in onsets(channel(a.wav, a.channel)) if keep(f)]
        if not on:
            raise Usage('%s: no onset in %s' % (a.wav, a.channel))
        bad = []
        if a.log is not None:
            recs = [r for r in records(a.log, a.voice) if keep(r[0]) and not (a.skip_late and r[2])]
            used, res = set(), []
            for e, line, _ in recs:
                best = min((o for o in on if o not in used), key=lambda o: abs(o - e), default=None)
                if best is None or abs(best - e) > MATCH:
                    bad.append('record of line %d (frame %.1f) has no onset' % (line, e))
                    continue
                used.add(best)
                res.append(best - e)
            bad += ['onset at frame %d has no record' % o for o in on if o not in used]
        elif a.grid is not None:
            fpt = float(RATE) * 60.0 / (a.grid * 384.0)
            base = on[0]
            res = [o - base - round((o - base) / fpt) * fpt for o in on]
        else:
            other = [f for f in onsets(channel(a.against, a.channel)) if keep(f)]
            if len(other) != len(on):
                raise Usage('%d onsets in %s, %d in %s' % (len(on), a.wav, len(other), a.against))
            res = [o - p for o, p in zip(on, other)]
        for x in bad:
            print(x)
        if not res:
            raise Usage('nothing to compare')
        c = median(res)
        dev = [r - c for r in res]
        spread = max(dev) - min(dev)
        print('onsets %d, c %.2f frames, spread %.2f frames (%.2f .. %.2f)' % (len(res), c, spread, min(dev), max(dev)))
        if a.json:
            with open(a.json, 'w') as fj:
                json.dump({'onsets': len(res), 'c': c, 'spread': spread, 'residuals': dev}, fj)
        ok = not bad and (a.limit is None or spread <= a.limit) and (a.min is None or spread >= a.min)
        return 0 if ok else 1
    except Usage as e:
        print('emu_onsets.py: %s' % e, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
