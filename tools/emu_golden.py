#!/usr/bin/env python3
"""The golden tests of the emulator (stage 1b spec 8.4-8.6; plan P6, P7).

    emu_golden.py check [ID...] [--render PATH] [--out DIR] [--golden-file FILE]
    emu_golden.py bless ID... [--render PATH] [--out DIR]
    emu_golden.py diff ID [--against REV] [--render PATH] [--out DIR]

A golden is emu/tests/goldens/ID.txt: header lines (#! options, #expect,
#broken, #broken-drop, #pin panel), then an event script. check renders each
golden into OUT/ID, evaluates its expectations, compares the outputs with its
entry in emu/tests/goldens/goldens.json, and renders the broken variant into
OUT/ID-broken, which must fail at least one expectation. --golden-file checks
one file outside the goldens folder, expectations and broken variant only.
bless does what check does and writes the entries; it refuses, writing
nothing, when an expectation fails or a broken variant passes. diff builds the
renderer at REV (default: the last commit that changed goldens.json) in a
temporary export, renders the golden with both renderers and prints the first
differing frame and log lines. Paths default to the repository's build/emu.
Exit 0, 1 on any failure, 2 on a usage error.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import argparse, array, fractions, hashlib, json, math, operator, os, re, shlex, shutil, subprocess, sys
import tempfile, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDENS = os.path.join(ROOT, 'emu', 'tests', 'goldens')
GOLDENS_JSON = os.path.join(GOLDENS, 'goldens.json')
CARDS = os.path.join(ROOT, 'emu', 'tests', 'cards')
DEFAULT_RENDER = os.path.join(ROOT, 'build', 'emu', 'lichomat-render')
DEFAULT_OUT = os.path.join(ROOT, 'build', 'emu', 'goldens-out')

CPU_HZ = 216000000
RATE = fractions.Fraction(353125, 8)    # the codec's frame rate, 44,140.625 Hz
CHUNK_FRAMES = 4096
CHUNK_LINES = 64
CHANNELS = ('L1', 'R1', 'L2', 'R2')
LOGS = ('midi', 'clock', 'panel')
FILES = {'wav': 'out.wav', 'midi': 'midi.log', 'clock': 'clock.log', 'panel': 'panel.log',
         'summary': 'summary.json'}
# the options that emu_golden.py sets itself (plan P6)
OWN_OPTIONS = ('--out', '--midi-log', '--clock-log', '--panel-log', '--summary', '--events', '--keep-card')
EVENTS = ('down', 'up', 'press', 'encoder', 'push', 'knob', 'fader', 'param', 'save-kit', 'save-pattern',
          'save-project', 'din', 'usb', 'din-clock', 'usb-clock', 'jack-clock', 'jack-pulse', 'reset', 'card',
          'jacks', 'mark')


class Usage(Exception):
    """a usage error: exit 2"""


class Failure(Exception):
    """a golden that cannot be evaluated, such as a render that exits non-zero"""


# --- times and ranges (plan P4, P6) -------------------------------------------------

TIME = re.compile(r'^(\+?)(?:(\d+(?:\.\d{1,6})?)(ms|s)|(\d+)f)$')


def frames_to_cycles(n):
    """emu_framesToCycles(): 216 MHz / 44,140.625 Hz = 552,960 / 113 cycles per frame"""
    return n * 552960 // 113


def parse_time(tok):
    """(relative, cycles, first frame) of a script time, or None. Cycles round down as the
    renderer's do (plan P4); the frame is the first one at or after the time,
    ceil(T x 44,140.625), or the frame count itself for an f time (plan P6)."""
    m = TIME.match(tok)
    if not m:
        return None
    if m.group(4) is not None:
        n = int(m.group(4))
        return m.group(1) == '+', frames_to_cycles(n), n
    v = fractions.Fraction(m.group(2))
    sec = v / 1000 if m.group(3) == 'ms' else v
    return m.group(1) == '+', math.floor(sec * CPU_HZ), math.ceil(sec * RATE)


def parse_window(tok, where):
    """FROM..TO in script times: (from cycle, to cycle, from frame, to frame)"""
    a, sep, b = tok.partition('..')
    ta, tb = parse_time(a), parse_time(b)
    if not sep or ta is None or tb is None or ta[0] or tb[0]:
        raise Usage('%s: %r is not a window FROM..TO of script times' % (where, tok))
    if tb[1] < ta[1]:
        raise Usage('%s: the window %s ends before it starts' % (where, tok))
    return ta[1], tb[1], ta[2], tb[2]


NUMBER = r'-?\d+(?:\.\d+)?'


def parse_range(tok, where, integers=True):
    m = re.match(r'^(%s)\.\.(%s)$' % ((r'\d+', r'\d+') if integers else (NUMBER, NUMBER)), tok)
    if not m:
        raise Usage('%s: %r is not a range MIN..MAX' % (where, tok))
    lo, hi = (int(m.group(1)), int(m.group(2))) if integers else (float(m.group(1)), float(m.group(2)))
    if hi < lo:
        raise Usage('%s: the range %s is empty' % (where, tok))
    return lo, hi


# --- the golden files (plan P6) -------------------------------------------------------

class Expect:
    """one #expect line"""

    def __init__(self, n, text, rest, where):
        self.n, self.text = n, text
        w = rest.split()
        self.kind = w[0] if w else ''
        self.window = None
        args = w[1:]

        def window(k):
            if len(args) == k + 1:
                self.window = parse_window(args[k], where)
            elif len(args) != k:
                raise Usage('%s: #expect %s takes %d arguments and a window' % (where, self.kind, k))

        if self.kind == 'midi':
            window(3)
            if args[0] not in ('DIN', 'USB'):
                raise Usage('%s: the port %r is not DIN or USB' % (where, args[0]))
            if not re.match(r'^[89A-Fa-f][0-9A-Fa-fn]$', args[1]):
                raise Usage('%s: the status %r is not two hex digits of a status byte' % (where, args[1]))
            self.port, self.status = args[0], args[1].upper().replace('N', 'n')
            self.range = parse_range(args[2], where)
        elif self.kind == 'clock':
            window(1)
            self.range = parse_range(args[0], where)
        elif self.kind in ('energy', 'silent'):
            k = 2 if self.kind == 'energy' else 1
            window(k)
            if args[0] not in CHANNELS:
                raise Usage('%s: the channel %r is not one of %s' % (where, args[0], ', '.join(CHANNELS)))
            self.channel = CHANNELS.index(args[0])
            if self.kind == 'energy':
                if not re.match('^%s$' % NUMBER, args[1]):
                    raise Usage('%s: %r is not a level in dBFS' % (where, args[1]))
                self.dbfs = float(args[1])
        elif self.kind == 'summary':
            if len(args) != 2:
                raise Usage('%s: #expect summary takes FIELD MIN..MAX' % where)
            self.field, self.range = args[0], parse_range(args[1], where, integers=False)
        elif self.kind == 'lcd':
            m = re.match(r'^lcd\s+(\S+)\s+(.*\S)\s*$', rest)
            if not m:
                raise Usage('%s: #expect lcd takes MARK TEXT' % where)
            self.mark, self.lcd = m.group(1), m.group(2)
        elif self.kind == 'led':
            if len(args) != 3 or not all(re.match(r'^[0-9A-Fa-f]+$', x) for x in args[1:]):
                raise Usage('%s: #expect led takes MARK MASK VALUE, in hex' % where)
            self.mark, self.mask, self.value = args[0], int(args[1], 16), int(args[2], 16)
        else:
            raise Usage('%s: unknown expectation %r' % (where, self.kind))

    def evaluate(self, o):
        """(holds, detail) on the outputs o"""
        if self.kind == 'midi':
            n = o.midi_count(self.port, self.status, self.window)
            return self.range[0] <= n <= self.range[1], '%d messages' % n
        if self.kind == 'clock':
            n = sum(1 for c, x in o.timed('clock') if x[-1] == '1' and in_window(c, self.window))
            return self.range[0] <= n <= self.range[1], '%d rising edges' % n
        if self.kind == 'energy':
            r = o.rms(self.channel, self.window)
            return r >= self.dbfs, '%s RMS %s' % (CHANNELS[self.channel], fmt_dbfs(r))
        if self.kind == 'silent':
            peak = o.peak(self.channel, self.window)
            return peak == 0, '%s peak %d' % (CHANNELS[self.channel], peak)
        if self.kind == 'summary':
            v = o.summary
            for k in self.field.split('.'):
                v = v.get(k) if isinstance(v, dict) else None
            if isinstance(v, bool) or not isinstance(v, (int, float)):
                return False, '%s is %r, not a number' % (self.field, v)
            return self.range[0] <= v <= self.range[1], '%s = %r' % (self.field, v)
        row = o.mark(self.mark)
        if row is None:
            return False, 'no mark %s in the panel log' % self.mark
        if self.kind == 'lcd':
            return self.lcd in row[0] or self.lcd in row[1], 'the LCD reads %r / %r' % row[:2]
        return (row[3] & self.mask) == self.value, 'LEDS %011X, & %X = %X' % (row[3], self.mask,
                                                                            row[3] & self.mask)


class Golden:
    def __init__(self, path):
        self.path = path
        self.id = os.path.splitext(os.path.basename(path))[0]
        self.options, self.expects, self.broken, self.drop, self.pin = [], [], [], [], set()
        self.has_broken = False

    def describe_broken(self):
        parts = []
        if self.has_broken:
            parts.append(' '.join(self.broken))
        if self.drop:
            parts.append('without ' + ','.join(self.drop))
        return '; '.join(parts)


def split_options(text, where):
    try:
        opts = shlex.split(text)
    except ValueError as e:
        raise Usage('%s: %s' % (where, e))
    for x in opts:
        if x.split('=')[0] in OWN_OPTIONS:
            raise Usage('%s: %s is set by emu_golden.py' % (where, x))
    return opts


def parse(path):
    """the golden at path; a malformed header line is a usage error naming the line"""
    g = Golden(path)
    try:
        with open(path) as f:
            g.lines = f.read().split('\n')
    except OSError as e:
        raise Usage('%s: %s' % (path, e.strerror))
    for n, line in enumerate(g.lines, 1):
        s = line.rstrip()
        if not s.startswith(('#!', '#expect', '#broken', '#pin')):
            continue
        where = '%s:%d' % (path, n)
        m = re.match(r'^(\S+)\s*(.*)$', s)
        head, rest = m.group(1), m.group(2)
        if head == '#!':
            g.options += split_options(rest, where)
        elif head == '#expect':
            g.expects.append(Expect(n, s, rest, where))
        elif head == '#broken':
            g.broken += split_options(rest, where)
            g.has_broken = True
        elif head == '#broken-drop':
            events = rest.split(',') if rest else []
            if not events or any(e not in EVENTS for e in events):
                raise Usage('%s: #broken-drop takes EVENT[,EVENT...], events of the script' % where)
            g.drop += [e for e in events if e not in g.drop]
        elif head == '#pin' and rest == 'panel':
            g.pin.add('panel')
        else:
            raise Usage('%s: a malformed header line: %s' % (where, s))
    if not g.expects:
        raise Usage('%s: no #expect line' % path)
    if not g.has_broken and not g.drop:
        raise Usage('%s: no #broken or #broken-drop line' % path)
    return g


def load(gid):
    path = os.path.join(GOLDENS, gid + '.txt')
    if not os.path.isfile(path):
        raise Usage('no golden %s (%s)' % (gid, path))
    return parse(path)


def all_ids():
    return sorted(x[:-4] for x in os.listdir(GOLDENS) if x.endswith('.txt'))


def card_paths(opts):
    """--card NAME without a / is emu/tests/cards/NAME (plan P6)"""
    out = list(opts)
    for i in range(len(out) - 1):
        if out[i] == '--card' and '/' not in out[i + 1]:
            out[i + 1] = os.path.join(CARDS, out[i + 1])
    return out


def events_file(g, outdir, drop):
    """the script for --events: the golden itself, or a copy in outdir without the lines
    of the dropped events. A kept line whose time counted from a dropped one gets
    its absolute time in ms, which the renderer rounds down to the same cycle."""
    if not drop:
        return g.path
    out, prev, after_drop = [], 0, False
    for line in g.lines:
        tok = line.split('#', 1)[0].split()
        t = parse_time(tok[0]) if tok else None
        if t is None:
            out.append(line)        # a comment, an empty line, or an error that the renderer names
            continue
        rel, cycles, _ = t
        prev = prev + cycles if rel else cycles
        if len(tok) > 1 and tok[1] in drop:
            out.append('# #broken-drop: ' + line)
            after_drop = True
            continue
        if rel and after_drop:
            us = -(-prev * 1000000 // 216000)     # ceil(prev / 216,000 ms) in millionths of a ms
            line = line.replace(tok[0], '%d.%06dms' % (us // 1000000, us % 1000000), 1)
        after_drop = False
        out.append(line)
    path = os.path.join(outdir, 'events.txt')
    with open(path, 'w') as f:
        f.write('\n'.join(out))
    return path


def render(g, outdir, broken=False, extra=(), renderer=DEFAULT_RENDER):
    """renders g (its broken variant when broken) into outdir, emptied first; returns outdir.
    A non-zero exit raises Failure with the renderer's stderr."""
    shutil.rmtree(outdir, ignore_errors=True)
    os.makedirs(outdir)
    opts = card_paths(g.options + (g.broken if broken else []))
    events = events_file(g, outdir, g.drop if broken else [])
    f = {k: os.path.join(outdir, v) for k, v in FILES.items()}
    cmd = [renderer] + opts + ['--events', events, '--out', f['wav'], '--midi-log', f['midi'],
                               '--clock-log', f['clock'], '--panel-log', f['panel'], '--summary', f['summary']]
    cmd += list(extra)
    p = subprocess.run(cmd, capture_output=True, text=True)
    with open(os.path.join(outdir, 'stderr.txt'), 'w') as fe:
        fe.write(p.stderr)
    if p.returncode != 0:
        raise Failure('%s%s: the renderer exited %d: %s\n%s' % (g.id, ' (broken)' if broken else '',
                                                               p.returncode, ' '.join(cmd), p.stderr.rstrip()))
    return outdir


# --- the outputs ------------------------------------------------------------------------

def in_window(c, w):
    return w is None or w[0] <= c < w[1]


def fmt_dbfs(r):
    return 'silence' if r == -math.inf else '%.2f dBFS' % r


PANEL = re.compile(r'^(\d+) (\S+) (\S+) "((?:[^"\\]|\\x[0-9A-F]{2})*)" "((?:[^"\\]|\\x[0-9A-F]{2})*)" (\S+) '
                   r'([0-9A-F]+)$')


def unescape(row):
    return re.sub(r'\\x([0-9A-F]{2})', lambda m: chr(int(m.group(1), 16)), row)


class Outputs:
    """the outputs of one render in a folder"""

    def __init__(self, d):
        self.dir = d
        with open(os.path.join(d, FILES['wav']), 'rb') as f:
            raw = f.read()
        if len(raw) < 44 or raw[0:4] != b'RIFF' or raw[8:16] != b'WAVEfmt ' or raw[36:40] != b'data':
            raise Failure('%s: not a canonical WAV' % os.path.join(d, FILES['wav']))
        self.channels = int.from_bytes(raw[22:24], 'little')
        self.rate = int.from_bytes(raw[24:28], 'little')
        self.bits = int.from_bytes(raw[34:36], 'little')
        self.data = raw[44:]
        self.frames = len(self.data) // (2 * self.channels)
        self.samples = array.array('h')
        self.samples.frombytes(self.data[:self.frames * 2 * self.channels])
        if sys.byteorder == 'big':
            self.samples.byteswap()
        self.raw = {}
        for k in LOGS:
            with open(os.path.join(d, FILES[k]), 'rb') as f:
                self.raw[k] = f.read()
        with open(os.path.join(d, FILES['summary'])) as f:
            self.summary = json.load(f)

    def lines(self, log):
        return self.raw[log].decode('latin-1').splitlines()

    def timed(self, log):
        """(cycles from t0, the fields) per line"""
        for x in self.lines(log):
            w = x.split()
            yield int(w[0]), w

    def midi_count(self, port, status, w):
        def match(b):
            return b >= 0x80 and '%X' % (b >> 4) == status[0] and (status[1] == 'n' or '%X' % (b & 15) == status[1])
        n = 0
        for c, x in self.timed('midi'):
            if x[2] != port or not in_window(c, w):
                continue
            if port == 'DIN':
                n += match(int(x[3], 16))
            else:
                n += sum(1 for p in x[3:] if match(int(p[2:4], 16)))     # byte 1 of each packet
        return n

    def channel(self, ch, w=None):
        f0, f1 = (0, self.frames) if w is None else (min(w[2], self.frames), min(w[3], self.frames))
        if ch >= self.channels or f1 <= f0:
            return array.array('h')
        return self.samples[f0 * self.channels + ch:f1 * self.channels:self.channels]

    def rms(self, ch, w=None):
        s = self.channel(ch, w)
        sq = sum(map(operator.mul, s, s))
        return -math.inf if not sq else 20 * math.log10(math.sqrt(sq / len(s)) / 32768)

    def peak(self, ch, w=None):
        s = self.channel(ch, w)
        return max(max(s), -min(s)) if s else 0

    def mark(self, name):
        """(row 1, row 2, cursor, LEDS) of the first panel line at the mark name, or None"""
        for x in self.lines('panel'):
            m = PANEL.match(x)
            if m and m.group(3) == name:
                return unescape(m.group(4)), unescape(m.group(5)), m.group(6), int(m.group(7), 16)
        return None


def expectations(g, outdir):
    """[(line, holds, detail)] of g's expectations on the outputs in outdir (plan P6)"""
    o = outdir if isinstance(outdir, Outputs) else Outputs(outdir)
    return [('line %d: %s' % (e.n, e.text),) + e.evaluate(o) for e in g.expects]


def log_entry(raw):
    lines = raw.splitlines(True)
    return {'sha256': hashlib.sha256(raw).hexdigest(), 'lines': len(lines),
            'chunks': ['%08x' % zlib.crc32(b''.join(lines[i:i + CHUNK_LINES]))
                       for i in range(0, len(lines), CHUNK_LINES)]}


def entry(g, outdir):
    """g's goldens.json entry from the outputs in outdir (plan P7)"""
    o = outdir if isinstance(outdir, Outputs) else Outputs(outdir)
    step = CHUNK_FRAMES * 2 * o.channels
    rms = [o.rms(c) for c in range(o.channels)]
    e = {'wav': {'sha256': hashlib.sha256(o.data).hexdigest(), 'channels': o.channels, 'rate': o.rate,
                 'bits': o.bits, 'frames': o.frames,
                 'chunks': ['%08x' % zlib.crc32(o.data[i:i + step]) for i in range(0, len(o.data), step)],
                 'peak': [o.peak(c) for c in range(o.channels)],
                 'rms': [None if r == -math.inf else round(r, 2) for r in rms]},
         'toolchain': o.summary['toolchain']}
    for k in LOGS:
        if k != 'panel' or 'panel' in g.pin:
            e[k] = log_entry(o.raw[k])
    return e


def without_toolchain(e):
    return {k: v for k, v in e.items() if k != 'toolchain'}


def explain_mismatch(gid, old, new, o):
    """spec 8.5: the first differing chunk, peaks and RMS, the logs, the toolchains"""
    out = []
    ow, nw = old.get('wav', {}), new['wav']
    if ow != nw:
        for k in ('channels', 'rate', 'bits', 'frames'):
            if ow.get(k) != nw[k]:
                out.append('%s: wav %s: blessed %r, now %r' % (gid, k, ow.get(k), nw[k]))
        oc, nc = ow.get('chunks', []), nw['chunks']
        i = next((i for i in range(max(len(oc), len(nc))) if i >= len(oc) or i >= len(nc) or oc[i] != nc[i]), None)
        if i is not None:
            f0, f1 = i * CHUNK_FRAMES, min((i + 1) * CHUNK_FRAMES, max(nw['frames'], ow.get('frames', 0)))
            out.append('%s: the WAV differs first in chunk %d, frames %d-%d, %.3f-%.3f s' % (
                gid, i, f0, f1, f0 / RATE, f1 / RATE))
        for c in range(max(len(ow.get('peak', [])), len(nw['peak']))):
            def at(w, k):
                return w[k][c] if c < len(w.get(k, [])) else None
            r = [at(ow, 'rms'), at(nw, 'rms')]
            out.append('%s: %s peak blessed %s, now %s; RMS blessed %s, now %s' % (
                gid, CHANNELS[c] if c < 4 else c, at(ow, 'peak'), at(nw, 'peak'),
                *('silence' if x is None else '%.2f dBFS' % x for x in r)))
    for k in LOGS:
        if k not in new or old.get(k) == new[k]:
            continue
        ol = old.get(k, {'chunks': [], 'lines': 0})
        oc, nc = ol['chunks'], new[k]['chunks']
        i = next((i for i in range(max(len(oc), len(nc))) if i >= len(oc) or i >= len(nc) or oc[i] != nc[i]), 0)
        cur = o.lines(k)
        first = cur[i * CHUNK_LINES] if i * CHUNK_LINES < len(cur) else '(none)'
        out.append('%s: the %s log: blessed %d lines, now %d; it differs first in lines %d-%d, the first now %r' % (
            gid, k, ol['lines'], new[k]['lines'], i * CHUNK_LINES + 1, (i + 1) * CHUNK_LINES, first))
    bt, nt = old.get('toolchain'), new['toolchain']
    out.append('%s: blessed with %s, SDK %s; now %s, SDK %s' % (
        gid, (bt or {}).get('clang'), (bt or {}).get('sdk'), nt.get('clang'), nt.get('sdk')))
    if bt != nt:
        out.append('%s: the toolchains differ: a likely cause' % gid)
    return out


# --- check and bless ----------------------------------------------------------------------

def read_goldens():
    if not os.path.isfile(GOLDENS_JSON):
        return {}
    with open(GOLDENS_JSON) as f:
        return json.load(f)


def run_golden(g, a, blessed):
    """renders g and its broken variant and prints what holds; with blessed (a dict),
    compares with g's entry there. Returns (passed, entry)."""
    ok, new = True, None
    try:
        outdir = render(g, os.path.join(a.out, g.id), renderer=a.render)
        o = Outputs(outdir)
        for line, holds, detail in expectations(g, o):
            print('%s: %s: %s (%s)' % (g.id, line, 'holds' if holds else 'FAILS', detail))
            ok = ok and holds
        new = entry(g, o)
        if blessed is not None:
            old = blessed.get(g.id)
            if old is None:
                print('%s: no entry in %s' % (g.id, os.path.relpath(GOLDENS_JSON, ROOT)))
                ok = False
            elif without_toolchain(old) != without_toolchain(new):
                print('%s: the outputs differ from goldens.json; they are in %s' % (g.id, outdir))
                for x in explain_mismatch(g.id, old, new, o):
                    print(x)
                ok = False
            else:
                print('%s: as blessed (wav sha256 %s)' % (g.id, new['wav']['sha256'][:16]))
        b = expectations(g, render(g, os.path.join(a.out, g.id + '-broken'), broken=True, renderer=a.render))
        failed = [(line, detail) for line, holds, detail in b if not holds]
        if failed:
            print('%s: the broken variant (%s) fails %s (%s)' % (g.id, g.describe_broken(), *failed[0]))
        else:
            print('%s: the broken variant (%s) passes every expectation' % (g.id, g.describe_broken()))
            ok = False
    except Failure as e:
        print(e)
        ok = False
    print('%s: %s' % (g.id, 'PASS' if ok else 'FAIL'))
    return ok, new


def cmd_check(a):
    if a.golden_file:
        if a.ids:
            raise Usage('check: --golden-file takes no IDs')
        return 0 if run_golden(parse(a.golden_file), a, None)[0] else 1
    goldens = [load(x) for x in (a.ids or all_ids())]
    blessed = read_goldens()
    results = [run_golden(g, a, blessed)[0] for g in goldens]
    return 0 if all(results) else 1


def cmd_bless(a):
    goldens = [load(x) for x in a.ids]
    blessed = read_goldens()
    entries, ok = {}, True
    for g in goldens:
        passed, new = run_golden(g, a, None)
        ok = ok and passed
        entries[g.id] = new
    if not ok:
        print('bless: refused, nothing written')
        return 1
    blessed.update(entries)
    with open(GOLDENS_JSON, 'w') as f:
        json.dump(blessed, f, indent=1, sort_keys=True)
        f.write('\n')
    for g in goldens:
        print('%s: blessed (wav sha256 %s)' % (g.id, entries[g.id]['wav']['sha256'][:16]))
    return 0


# --- diff -----------------------------------------------------------------------------------

def git(*args):
    p = subprocess.run(['git', '-C', ROOT] + list(args), capture_output=True, text=True)
    if p.returncode != 0:
        raise Failure('git %s: %s' % (' '.join(args), p.stderr.strip()))
    return p.stdout.strip()


def build_at(rev, tmp):
    """lichomat-render built at rev in tmp (Release)"""
    src, build = os.path.join(tmp, 'src'), os.path.join(tmp, 'build')
    os.makedirs(src)
    log = os.path.join(tmp, 'build.log')
    with open(log, 'w') as lf:
        steps = [['/bin/sh', '-c', 'git -C "$0" archive "$1" | tar -x -C "$2"', ROOT, rev, src],
                 ['cmake', '-S', os.path.join(src, 'emu'), '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'],
                 ['cmake', '--build', build, '--target', 'lichomat-render']]
        for s in steps:
            if subprocess.run(s, stdout=lf, stderr=subprocess.STDOUT).returncode != 0:
                with open(log) as f:
                    tail = f.read()[-3000:]
                raise Failure('diff: %s failed:\n%s' % (' '.join(s[:3]), tail))
    return os.path.join(build, 'lichomat-render')


def cmd_diff(a):
    g = load(a.id)
    rev = a.against or git('log', '-1', '--format=%H', '--', os.path.relpath(GOLDENS_JSON, ROOT))
    if not rev:
        raise Usage('diff: no commit changed goldens.json; name one with --against')
    git('rev-parse', '--verify', rev + '^{commit}')
    tmp = tempfile.mkdtemp(prefix='lichomat-golden-diff-')
    try:
        old_r = build_at(rev, tmp)
        here = Outputs(render(g, os.path.join(a.out, g.id + '-here'), renderer=a.render))
        there = Outputs(render(g, os.path.join(a.out, g.id + '-at-rev'), renderer=old_r))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    same = True
    if (here.channels, here.rate, here.bits) != (there.channels, there.rate, there.bits):
        print('%s: the WAV headers differ: %s has %d channels, %d Hz, %d bits; the working tree %d, %d, %d' % (
            g.id, rev, there.channels, there.rate, there.bits, here.channels, here.rate, here.bits))
        same = False
    n = min(len(here.samples), len(there.samples))
    step = CHUNK_FRAMES * here.channels
    i = next((j for j in range(0, n, step) if here.samples[j:j + step] != there.samples[j:j + step]), None)
    if i is not None:
        i = next(j for j in range(i, min(i + step, n)) if here.samples[j] != there.samples[j])
        f, c = divmod(i, here.channels)
        print('%s: the first differing frame is %d (%.6f s), channel %s: %d at %s, %d in the working tree' % (
            g.id, f, f / RATE, CHANNELS[c] if c < 4 else c, there.samples[i], rev, here.samples[i]))
        same = False
    elif here.frames != there.frames:
        print('%s: %s renders %d frames, the working tree %d' % (g.id, rev, there.frames, here.frames))
        same = False
    for k in LOGS:
        lh, lt = here.lines(k), there.lines(k)
        j = next((j for j in range(max(len(lh), len(lt))) if j >= len(lh) or j >= len(lt) or lh[j] != lt[j]), None)
        if j is not None:
            print('%s: the %s log differs first at line %d: %r at %s, %r in the working tree' % (
                g.id, k, j + 1, lt[j] if j < len(lt) else '(none)', rev, lh[j] if j < len(lh) else '(none)'))
            same = False
    if same:
        print('%s: no difference between %s and the working tree' % (g.id, rev))
    return 0 if same else 1


def main(argv=None):
    ap = argparse.ArgumentParser(prog='emu_golden.py', description='the golden tests of the emulator')
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('check', 'bless', 'diff'):
        p = sub.add_parser(name)
        if name == 'diff':
            p.add_argument('id', metavar='ID')
            p.add_argument('--against', metavar='REV')
        else:
            p.add_argument('ids', metavar='ID', nargs='*' if name == 'check' else '+')
        if name == 'check':
            p.add_argument('--golden-file', metavar='FILE')
        p.add_argument('--render', metavar='PATH', default=DEFAULT_RENDER)
        p.add_argument('--out', metavar='DIR', default=DEFAULT_OUT)
    a = ap.parse_args(argv)
    a.render, a.out = os.path.abspath(a.render), os.path.abspath(a.out)
    try:
        return {'check': cmd_check, 'bless': cmd_bless, 'diff': cmd_diff}[a.cmd](a)
    except Usage as e:
        print('emu_golden.py: %s' % e, file=sys.stderr)
        return 2
    except Failure as e:
        print(e)
        return 1


if __name__ == '__main__':
    sys.exit(main())
