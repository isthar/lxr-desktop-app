#!/usr/bin/env python3
"""pattern_gen_vectors.py VECTORS.json OUT.h: tools/gen/vectors.json (the prototype's golden vectors,
schema in plan v0.8 F22 and in the file's "format") as a C header for tests/test_pattern_gen.c
(plan v0.8 G15).

The header holds the dB table (PGV_VEL), the random generator's start states and first draws
(PGV_PRNG), every distinct track once (PGV_TRACK: the firmware's own bytes) and the cases (PGV:
the settings with src 0-6, the destination track, whether the voice needs vol, the indices of
the source, the current and the expected track in PGV_TRACK, -1 for a cleared track, and, LICHOMAT
v0.8 spec 11, the steps of the current track the last run wrote (own, 0 when the case has none)
and the steps the generator writes (own_out), bit i step i). The types are declared by the test
before it includes the header.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import json
import os
import sys

TYPES = ('---', 'hat', 'sub', 'prc', 'rar', 'prp')
# PatternGenSettings in field order: the vector's key, the C field's range
FIELDS = (('typ', 0, 255), ('sty', 0, 255), ('den', 0, 255), ('lev', -128, 127), ('var', -128, 127),
          ('not', -128, 127), ('sed', 0, 255), ('src', 0, 255), ('shf', -128, 127))
STEPS, BARS, STEP_BYTES, FINE_BYTES = 64, 4, 7, 32
CASE_KEYS = {'name', 'settings', 'track', 'src', 'cur', 'own', 'out', 'vol', 'own_out'}
VEL_COUNT, PRNG_DRAWS = 31, 8


def fail(msg):
    sys.exit('pattern_gen_vectors.py: %s' % msg)


def integer(v, lo, hi, what):
    if not isinstance(v, int) or isinstance(v, bool) or not lo <= v <= hi:
        fail('%s is %r, not an integer %d..%d' % (what, v, lo, hi))
    return v


def c_string(s):
    if not all(' ' <= ch <= '~' for ch in s):
        fail('the case name %r is not printable ASCII' % s)
    return '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"')


def settings(s, name):
    v = dict(s)
    if v.get('typ') in TYPES:
        v['typ'] = TYPES.index(v['typ'])
    elif not isinstance(v.get('typ'), int) or v['typ'] < len(TYPES):   # a number only past prp
        fail('%s: typ %r is not one of %s or a number past them' % (name, v.get('typ'), ' '.join(TYPES)))
    v['src'] = integer(v.get('src'), 1, 7, "%s: src (the menu's 1-7)" % name) - 1
    if set(v) != {k for k, _, _ in FIELDS}:
        fail('%s: the settings are %s' % (name, sorted(v)))
    return '{ %s }' % ', '.join(str(integer(v[k], lo, hi, '%s: %s' % (name, k))) for k, lo, hi in FIELDS)


def hexbytes(t, key, n, name):
    try:
        b = bytes.fromhex(t[key])
    except (KeyError, TypeError, ValueError):
        fail('%s: %s is not hex' % (name, key))
    if len(b) != n:
        fail('%s: %s has %d bytes, not %d' % (name, key, len(b), n))
    return '{ %s }' % ','.join(str(x) for x in b)


def mask(m, name, key):
    """a mask of steps, 4 words like active (bit n of word b is step 16b + n), as a uint64_t"""
    if not isinstance(m, list) or len(m) != BARS:
        fail('%s: %s is %r, not %d words' % (name, key, m, BARS))
    return '0x%016xull' % sum(integer(w, 0, 0xFFFF, '%s: %s' % (name, key)) << (16 * b) for b, w in enumerate(m))


def track(t, name):
    if not isinstance(t, dict) or set(t) != {'length', 'active', 'steps', 'timing', 'fine'}:
        fail('%s: a track is %r' % (name, t if not isinstance(t, dict) else sorted(t)))
    on = t['active']
    if not isinstance(on, list) or len(on) != BARS:
        fail('%s: active is %r, not %d words' % (name, on, BARS))
    return '{ %d, { %s },\n\t  %s,\n\t  %s,\n\t  %s }' % (
        integer(t['length'], 1, STEPS, '%s: length' % name),
        ', '.join('%du' % integer(w, 0, 0xFFFF, '%s: active' % name) for w in on),
        hexbytes(t, 'steps', STEPS * STEP_BYTES, name), hexbytes(t, 'timing', STEPS, name),
        hexbytes(t, 'fine', FINE_BYTES, name))


def main(src, out):
    with open(src) as f:
        d = json.load(f)
    vel = d.get('vel_table')
    if not isinstance(vel, list) or len(vel) != VEL_COUNT:
        fail('vel_table is not %d values' % VEL_COUNT)
    prng = []
    for p in d.get('prng') or fail('no prng'):
        if len(p.get('bytes', ())) != 4 or len(p.get('next', ())) != PRNG_DRAWS:
            fail('a prng entry is %r' % p)
        prng.append('{ { %s }, %du, { %s } }' % (
            ', '.join(str(integer(b, 0, 255, 'prng bytes')) for b in p['bytes']),
            integer(p.get('state'), 1, 0xFFFFFFFF, 'prng state'),
            ', '.join('%du' % integer(x, 0, 0xFFFFFFFF, 'prng next') for x in p['next'])))
    tracks, index, rows = [], {}, []

    def ref(t, name):
        if t is None:
            return -1
        text = track(t, name)
        if text not in index:
            index[text] = len(tracks)
            tracks.append(text)
        return index[text]
    names = set()
    for c in d.get('cases') or fail('no cases'):
        name = c.get('name')
        if not isinstance(name, str) or name in names:
            fail('the case name %r is missing or not unique' % name)
        names.add(name)
        if not set(c) <= CASE_KEYS:
            fail('%s: unknown keys %s' % (name, sorted(set(c) - CASE_KEYS)))
        if not isinstance(c.get('vol'), bool):
            fail('%s: vol is %r' % (name, c.get('vol')))
        if c.get('out') is None:
            fail('%s: no out' % name)
        rows.append('\t{ %s, %s, %d, %d, %d, %d, %d, %s, %s }' % (
            c_string(name), settings(c.get('settings') or {}, name),
            integer(c.get('track'), 0, 6, '%s: track' % name), int(c['vol']),
            ref(c.get('src'), name), ref(c.get('cur'), name), ref(c['out'], name),
            mask(c.get('own', [0] * BARS), name, 'own'), mask(c.get('own_out'), name, 'own_out')))
    with open(out, 'w') as f:
        f.write('/* generated by tests/pattern_gen_vectors.py from %s: do not edit */\n' % os.path.basename(src))
        f.write('#define PGV_VEL_COUNT %du\n' % VEL_COUNT)
        f.write('static const uint8_t PGV_VEL[PGV_VEL_COUNT] = { %s };\n'
                % ', '.join(str(integer(v, 1, 127, 'vel_table')) for v in vel))
        f.write('#define PGV_PRNG_COUNT %du\n' % len(prng))
        f.write('static const pgv_prng_t PGV_PRNG[PGV_PRNG_COUNT] = {\n\t%s\n};\n' % ',\n\t'.join(prng))
        f.write('#define PGV_TRACK_COUNT %du\n' % len(tracks))
        f.write('static const pgv_track_t PGV_TRACK[PGV_TRACK_COUNT] = {\n\t%s\n};\n' % ',\n\t'.join(tracks))
        f.write('#define PGV_COUNT %du\n' % len(rows))
        f.write('static const pgv_t PGV[PGV_COUNT] = {\n%s\n};\n' % ',\n'.join(rows))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
