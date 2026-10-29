#!/usr/bin/env python3
"""Host tests of lichomat-render (stage 1b spec 5, 12.1 M14, M17, M23).

    render_test.py --render R --render-test RT --root ROOT --work DIR CASE

Each CASE is a function below; it runs in DIR/CASE, emptied first, and prints
PASS CASE (exit 0) or the failed check (exit 1). Its renders make their card
copies in a TMPDIR of its own, $TMPDIR/lichomat-case-CASE, so that a check for
copies left behind sees this case's copies only, also when ctest runs cases in
parallel. The folder is removed at the end when it is empty; a case that leaves
a copy leaves the folder too, and ctest's emu_tmp_clean names it.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import argparse, array, fractions, hashlib, json, math, os, re, shutil, struct, subprocess, sys, time, wave

CASES = {}
def case(fn):
    CASES[fn.__name__] = fn
    return fn

class Fail(Exception):
    pass

def check(cond, what):
    if not cond:
        raise Fail(what)

class T:
    def __init__(self, a):
        self.r, self.rt, self.root = a.render, a.render_test, a.root
        self.work = os.path.join(a.work, a.case)
        shutil.rmtree(self.work, ignore_errors=True)
        os.makedirs(self.work)
        self.n = 0
        self.tmp = os.path.join(os.environ.get('TMPDIR') or '/tmp', 'lichomat-case-' + a.case)
        shutil.rmtree(self.tmp, ignore_errors=True)
        os.makedirs(self.tmp)
        os.environ['TMPDIR'] = self.tmp     # the renders' card copies (render_card.c)

    def done(self):
        """remove this case's TMPDIR if it is empty"""
        if not os.listdir(self.tmp):
            os.rmdir(self.tmp)

    def path(self, *p):
        return os.path.join(self.work, *p)

    def run(self, args, code=0, test=False, timeout=300):
        cmd = [self.rt if test else self.r] + [str(x) for x in args]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        check(p.returncode == code, 'exit %d, not %d: %s\n%s' % (p.returncode, code, ' '.join(cmd), p.stderr))
        return p

    def script(self, text):
        self.n += 1
        p = self.path('s%d.txt' % self.n)
        with open(p, 'w') as f:
            f.write(text)
        return p

    def seed(self, name='seed'):
        """an init card from --seed-card in self.path(name), made once"""
        p = self.path(name)
        if not os.path.isdir(p):
            self.run(['--seed-card', p])
        return p

    def render(self, card, args, test=False, code=0):
        """a render with --out and --summary added: (the summary as a dict, the WAV's path, the process)"""
        self.n += 1
        wav, summary = self.path('r%d.wav' % self.n), self.path('r%d.json' % self.n)
        p = self.run(['--card', card, '--out', wav, '--summary', summary] + list(args), code=code, test=test)
        s = None
        if code == 0:
            with open(summary) as f:
                s = json.load(f)
        return s, wav, p

C100 = 21600000

def frames_to_cycles(n):
    return n * 552960 // 113

def wav_info(path):
    """(channels, rate, sample width, frames, the sample bytes)"""
    with wave.open(path, 'rb') as w:
        return w.getnchannels(), w.getframerate(), w.getsampwidth(), w.getnframes(), w.readframes(w.getnframes())

def no_copies_left():
    tmp = os.environ.get('TMPDIR', '/tmp')
    left = [x for x in os.listdir(tmp) if x.startswith('lichomat-render-')]
    check(not left, 'card copies left in %s: %r' % (tmp, left))

def tree(d):
    """{relative path: sha256} of every file and {relative path: None} of every directory"""
    out = {}
    for base, dirs, files in os.walk(d):
        for x in dirs:
            out[os.path.relpath(os.path.join(base, x), d)] = None
        for x in files:
            with open(os.path.join(base, x), 'rb') as f:
                out[os.path.relpath(os.path.join(base, x), d)] = hashlib.sha256(f.read()).hexdigest()
    return out

def main():
    ap = argparse.ArgumentParser()
    for o in ('--render', '--render-test', '--root', '--work'):
        ap.add_argument(o, required=True)
    ap.add_argument('case', choices=sorted(CASES))
    a = ap.parse_args()
    t = T(a)
    try:
        CASES[a.case](t)
    except Fail as e:
        print('FAIL %s: %s' % (a.case, e))
        sys.exit(1)
    finally:
        t.done()
    print('PASS', a.case)

# --- cases ---------------------------------------------------------------------

# the options of spec 5.1 that --help names, and the test-only ones (spec 5.3)
OPTIONS = ['--card', '--out', '--seed-card', '--project', '--pattern', '--song', '--bpm', '--param',
           '--seconds', '--bars', '--events', '--no-start', '--faders', '--jacks', '--seed', '--as-booted',
           '--wav-rate', '--stereo', '--midi-log', '--clock-log', '--panel-log', '--summary', '--keep-card',
           '--version', '--help']
TEST_OPTIONS = ['--test-fault', '--test-hang', '--test-slice', '--test-prelude-reload']

def usage_error(t, args, name):
    """exit 1 with one line on stderr that starts with the prefix and names the option"""
    p = t.run(args, code=1)
    lines = p.stderr.splitlines()
    check(len(lines) == 1 and lines[0].startswith('lichomat-render: ') and name in lines[0],
          '%s: stderr is not one line naming %s: %r' % (' '.join(args), name, p.stderr))

@case
def M17_cli_usage(t):
    co = ['--card', 'C', '--out', 'O']
    usage_error(t, [], '--card')
    usage_error(t, ['--card', 'C'], '--out')	# a render needs both: the missing one is named
    usage_error(t, ['--out', 'O'], '--card')
    for args in (['--bogus'], ['--project', '64'], ['--pattern', '-1'], ['--bpm', '1'], ['--bpm', '256'],
                 ['--bpm', 'fast'], ['--faders', '1,1,1'], ['--faders', '1,1,1,1,1,1.5'], ['--jacks', 'L3'],
                 ['--wav-rate', '48000'], ['--stereo', 'both'], ['--seconds', '0'], ['--seconds', '1.1234567'],
                 ['--bars', '0'], ['--seed', 'x'], ['--param', 'shuffle'], ['--param', 'shuffle=256'],
                 ['--test-slice']):
        usage_error(t, args + co, args[0])
    usage_error(t, ['--seed-card', 'D', '--card', 'C'], '--seed-card')
    usage_error(t, ['--card', 'C', '--out'], '--out')	# an option without its value

def version_lines(root):
    with open(os.path.join(root, 'CMakeLists.txt')) as f:
        version = re.search(r'project\(LXR_V2_STM32F765[^)]*VERSION ([0-9]+\.[0-9]+)', f.read()).group(1)
    return version, [
        'lichomat-render %s, the renderer of LICHOMAT Emu' % version,
        'emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02',
        'Runs LICHOMAT %s with the emulator changes F1-F12 and F6b. Built from commit COMMIT.' % version,
        'Copyright (C) 2012-2026 Erica Synths',
        'Copyright (C) 2026 Tomasz Krzywicki',
        'Modified from the LXR firmware 1.7 by Erica Synths; LICHOMAT and its emulator by Tomasz Krzywicki, 2026',
        'Arm Optimized Routines: Copyright (c) 1999-2022, Arm Limited. MIT licence.',
        'FatFs ff.h and integer.h: Copyright (C) 2017, ChaN. FatFs licence.',
        'This program comes with ABSOLUTELY NO WARRANTY. It is free software under the GNU',
        'General Public License version 3 only, with the additional permission and terms',
        'in NOTICE.md. See LICENSE and NOTICE.md.',
        'Source: https://github.com/isthar/lxr-desktop-app at the commit above.',
        'Not affiliated with Erica Synths.',
    ]

@case
def M17_cli_info(t):
    version, want = version_lines(t.root)
    check(version == '0.1', 'the firmware version is %s, not 0.1 (spec 10.1)' % version)
    out = t.run(['--version']).stdout
    m = re.search(r'Built from commit ([0-9a-f]{12}|unknown)\.', out)
    check(m, '--version names no commit of 12 hex digits: %r' % out)
    got = out.replace(m.group(1), 'COMMIT', 1)
    check(got == '\n'.join(want) + '\n', '--version printed:\n%s' % out)
    for args in (['--help'], ['--help', '--bogus'], ['--card', 'C', '--help']):
        h = t.run(args).stdout
        words = set(re.findall(r'--[a-z][a-z0-9-]*', h))
        missing = [o for o in OPTIONS if o not in words]
        check(not missing, '%s names no %s' % (' '.join(args), ', '.join(missing)))
        extra = [o for o in TEST_OPTIONS if o in words]
        check(not extra, '%s names the test options %s' % (' '.join(args), ', '.join(extra)))
    words = set(re.findall(r'--[a-z][a-z0-9-]*', t.run(['--help'], test=True).stdout))
    missing = [o for o in OPTIONS + TEST_OPTIONS if o not in words]
    check(not missing, 'lichomat-render-test --help names no %s' % ', '.join(missing))

def card_error(t, card, extra, name=None):
    """exit 2 with one line that names the card, and name when given"""
    args = ['--card', card, '--out', t.path('o.wav')] + extra
    p = t.run(args, code=2)
    lines = p.stderr.splitlines()
    check(len(lines) == 1 and lines[0].startswith('lichomat-render: card %s: ' % card)
          and (name is None or name in lines[0]),
          '%s: stderr is not one line naming the card and %s: %r' % (' '.join(args), name, p.stderr))

def card_copy(t, src, name, index=None):
    dst = t.path(name)
    shutil.copytree(src, dst)
    if index is not None:
        with open(os.path.join(dst, 'PROJ.IDX'), 'wb') as f:
            f.write(bytes([index]))
    return dst

@case
def M17_card_errors(t):
    seed = t.seed()
    card_error(t, t.path('missing'), [])
    with open(t.path('file'), 'w') as f:
        f.write('x')
    card_error(t, t.path('file'), [])
    os.makedirs(t.path('empty'))
    card_error(t, t.path('empty'), [], 'project 0 has no PROJ00/PRJ.NFO')
    card_error(t, seed, ['--project', '3'], 'project 3 has no PROJ03/PRJ.NFO')
    card_error(t, card_copy(t, seed, 'idx5', 5), [], 'project 5 has no PROJ05/PRJ.NFO')
    # byte 200 counts as 0 (E4.7), and the lookup ignores case as FatFs does
    lower = card_copy(t, seed, 'idx200', 200)
    os.rename(os.path.join(lower, 'PROJ00'), os.path.join(lower, 'proj00'))
    check(os.listdir(lower).count('proj00') == 1, 'the rename to proj00 did not happen')
    s, _, _ = t.render(lower, ['--seconds', '0.1'])
    check(s['inputs']['project'] == 0, 'PROJ.IDX 200 and proj00 loaded project %r' % s['inputs']['project'])
    # --project 0 on the same card passes too, and --project 5 on idx5's does not
    t.render(lower, ['--seconds', '0.1', '--project', '0'])
    card_error(t, lower, ['--project', '5'], 'project 5 has no PROJ05/PRJ.NFO')
    no_copies_left()

@case
def M17_seed_card(t):
    first = t.path('new')
    t.run(['--seed-card', first])
    check(sorted(os.listdir(first)) == ['PROJ00', 'TMP'], 'the card holds %r, not PROJ00 and TMP (no PROJ.IDX)'
          % sorted(os.listdir(first)))
    check(os.listdir(os.path.join(first, 'TMP')) == [], 'TMP is not empty')
    proj = os.path.join(first, 'PROJ00')
    names = os.listdir(proj)
    check(all(os.path.isfile(os.path.join(proj, x)) for x in names), 'PROJ00 holds a folder')
    size = sum(os.path.getsize(os.path.join(proj, x)) for x in names)
    check((len(names), size) == (194, 255398), 'PROJ00 holds %d files of %d bytes, not 194 of 255,398'
          % (len(names), size))
    def read(name):
        with open(os.path.join(proj, name), 'rb') as f:
            return f.read()
    check(read('PRJ.NFO') == b'Init\0\0\0\0', 'PRJ.NFO is %r' % read('PRJ.NFO'))
    glo = read('GLO.CFG')
    check(len(glo) == 30 and glo[0] == 120 and glo[29] == 1, 'GLO.CFG is %r' % glo)
    check(len(read('00-INITK.SND')) == 255, '00-INITK.SND has %d bytes' % len(read('00-INITK.SND')))
    # into an empty folder holding a dot entry: the same card, so seeding is deterministic
    second = t.path('dot')
    os.makedirs(second)
    with open(os.path.join(second, '.DS_Store'), 'wb') as f:
        f.write(b'x')
    t.run(['--seed-card', second])
    tr = tree(second)
    check('.DS_Store' in tr, '.DS_Store is gone')
    del tr['.DS_Store']
    check(tr == tree(first), 'the second card differs from the first')
    # into a folder with a file: refused, untouched
    third = t.path('full')
    os.makedirs(third)
    with open(os.path.join(third, 'x'), 'wb') as f:
        f.write(b'x')
    p = t.run(['--seed-card', third], code=1)
    check(p.stderr.startswith('lichomat-render: --seed-card: ') and len(p.stderr.splitlines()) == 1,
          '--seed-card on a folder with a file: %r' % p.stderr)
    check(os.listdir(third) == ['x'], 'the folder with a file was changed')
    # and on a file
    p = t.run(['--seed-card', os.path.join(third, 'x')], code=1)
    check('--seed-card' in p.stderr, '--seed-card on a file: %r' % p.stderr)

def t0_rule(s, t):
    """spec 5.3's two rules of t0 at the time t of a block request"""
    busy = s['morphBusyLast']
    return t - s['preludeEnd'] >= C100 and (busy == 0 or t - busy > C100)

def block_index(s, t):
    """k with codecStart + frames_to_cycles(64 k) == t, or None"""
    k = (t - s['codecStart']) * 113 // (64 * 552960)
    for j in (k - 1, k, k + 1):
        if j >= 0 and s['codecStart'] + frames_to_cycles(64 * j) == t:
            return j
    return None

@case
def M17_t0(t):
    seed = t.seed()
    for test, args in ((False, []), (True, ['--test-prelude-reload'])):
        s, _, _ = t.render(seed, ['--seconds', '1'] + args, test=test)
        t0 = s['t0']
        k = block_index(s, t0)
        check(k is not None and k > 0, '%r: t0 %d is not a block request' % (args, t0))
        check(t0_rule(s, t0), '%r: t0 %d breaks the rule: preludeEnd %d, morphBusyLast %d'
              % (args, t0, s['preludeEnd'], s['morphBusyLast']))
        prev = s['codecStart'] + frames_to_cycles(64 * (k - 1))
        check(not t0_rule(s, prev), '%r: the request before t0 (%d) meets the rule too' % (args, prev))
        print('%r: t0 - preludeEnd %d, morphBusyLast - preludeEnd %s'
              % (args, t0 - s['preludeEnd'], s['morphBusyLast'] - s['preludeEnd'] if s['morphBusyLast'] else 0))
        if args:
            check(s['morphBusyLast'] > s['preludeEnd'], 'the reload ran no transfer after the prelude: %r' % s)
            # t0 is set by the busy rule: the time rule alone would have taken an earlier request
            check(t0 - s['preludeEnd'] - C100 > frames_to_cycles(64), 't0 is not later than the time rule gives')

@case
def M17_defaults(t):
    seed = t.seed()
    wav = t.path('d.wav')
    summary = t.path('d.json')
    t.run(['--card', seed, '--out', wav, '--summary', summary])
    with open(summary) as f:
        s = json.load(f)
    i = s['inputs']
    want = {'project': 0, 'pattern': -1, 'song': -1, 'seed': 1, 'faders': [1] * 6, 'jacks': 'all',
            'asBooted': False, 'noStart': False, 'blocks': 6897}
    got = {x: i[x] for x in want}
    check(got == want, 'the inputs are %r, not %r' % (got, want))
    sel = s['selected']
    check((sel['pattern'], sel['bpm'], sel['sync']) == (0, 120, 0), 'selected is %r' % sel)
    ch, rate, width, frames, _ = wav_info(wav)
    check((ch, rate, width, frames) == (4, 44141, 2, 441408), 'the WAV is %r' % ((ch, rate, width, frames),))
    check(s['underrunsAfterT0'] == 0, 'underruns after t0: %d' % s['underrunsAfterT0'])
    k0 = block_index(s, s['t0'])
    check(k0 is not None, 't0 is not a block request')
    want_len = frames_to_cycles(64 * (k0 + 6897)) - frames_to_cycles(64 * k0)
    check(s['end'] - s['t0'] == want_len, 'end - t0 is %d, not %d' % (s['end'] - s['t0'], want_len))

@case
def M17_wav(t):
    seed = t.seed()
    _, a, _ = t.render(seed, ['--seconds', '1'])
    _, b, _ = t.render(seed, ['--seconds', '1', '--wav-rate', '44100'])
    ch, rate, width, frames, data = wav_info(a)
    check((ch, rate, frames, len(data)) == (4, 44141, 44160, 353280), 'the 1 s WAV is %r'
          % ((ch, rate, frames, len(data)),))
    with open(a, 'rb') as f:
        ra = f.read()
    with open(b, 'rb') as f:
        rb = f.read()
    check(len(ra) == len(rb) == 44 + 353280, 'the files have %d and %d bytes' % (len(ra), len(rb)))
    diff = [j for j in range(len(ra)) if ra[j] != rb[j]]
    check(diff and all(24 <= j < 32 for j in diff), 'the 44100 file differs at %r' % diff[:16])
    check(struct.unpack('<II', rb[24:32]) == (44100, 44100 * 8), 'the 44100 header has %r'
          % (struct.unpack('<II', rb[24:32]),))
    for mode in ('out1', 'out2', 'sum'):
        _, w, _ = t.render(seed, ['--seconds', '1', '--stereo', mode])
        ch, _, _, frames, data = wav_info(w)
        check((ch, frames, len(data)) == (2, 44160, 176640), '--stereo %s: %r' % (mode, (ch, frames, len(data))))

@case
def M17_card_copy(t):
    src = card_copy(t, t.seed(), 'src')
    with open(os.path.join(src, 'TMP', 'X.SND'), 'wb') as f:
        f.write(b'x' * 16)
    shutil.copytree(os.path.join(src, 'PROJ00'), os.path.join(src, 'PROJ01'))
    glo = os.path.join(src, 'PROJ01', 'GLO.CFG')
    with open(glo, 'rb') as f:
        g = bytearray(f.read())
    g[0] = 90
    with open(glo, 'wb') as f:
        f.write(bytes(g))
    # names that are not 8.3 are skipped, one warning per name (spec 11)
    # (a name in TMP only with --as-booted, which copies TMP)
    skipped = [os.path.join(src, 'PROJ00', 'LongerName.snd'), os.path.join(src, 'A+B.SND')]
    in_tmp = os.path.join(src, 'TMP', 'LongerName.snd')
    for x in skipped + [in_tmp]:
        with open(x, 'wb') as f:
            f.write(b'x')
    before = tree(src)
    keep = {}
    for name, args in (('K1', []), ('K2', ['--as-booted']), ('K3', ['--project', '1'])):
        keep[name] = t.path(name)
        s, _, p = t.render(src, ['--seconds', '0.5', '--keep-card', keep[name]] + args)
        check(tree(src) == before, 'the render with %r changed the source card' % args)
        warned = [x for x in p.stderr.splitlines() if x.startswith('lichomat-render: warning: ')]
        for x in skipped + [in_tmp]:
            want = 1 if x != in_tmp or args == ['--as-booted'] else 0
            n = sum(1 for w in warned if x in w and 'not an 8.3 name' in w)
            check(n == want, '%r: %d warnings name %s, not %d: %r' % (args, n, x, want, p.stderr))
            check(sum(1 for w in s['warnings'] if x in w) == want, '%r: the summary warns %r' % (args, s['warnings']))
        keep[name] = (keep[name], s)
    k1 = keep['K1'][0]
    check(os.path.isfile(os.path.join(k1, 'PROJ.IDX')), 'K1 holds no PROJ.IDX: %r' % sorted(os.listdir(k1)))
    check('A+B.SND' not in os.listdir(k1) and 'LongerName.snd' not in os.listdir(os.path.join(k1, 'PROJ00')),
          'the copy holds a name that is not 8.3')
    check(os.path.isdir(os.path.join(k1, 'TMP')) and 'X.SND' not in os.listdir(os.path.join(k1, 'TMP')),
          'K1 has no TMP, or its TMP holds X.SND: %r' % sorted(os.listdir(k1)))
    k2 = keep['K2'][0]
    check('X.SND' in os.listdir(os.path.join(k2, 'TMP')), '--as-booted: K2/TMP holds no X.SND')
    k3, s3 = keep['K3']
    with open(os.path.join(k3, 'PROJ.IDX'), 'rb') as f:
        check(f.read() == b'\x01', 'K3/PROJ.IDX is not one byte 01')
    check(s3['selected']['bpm'] == 90, '--project 1 plays at %d BPM, not 90' % s3['selected']['bpm'])
    full = t.path('full')
    os.makedirs(full)
    with open(os.path.join(full, 'x'), 'w') as f:
        f.write('x')
    p = t.run(['--card', src, '--out', t.path('o.wav'), '--keep-card', full], code=1)
    check('--keep-card' in p.stderr, '--keep-card on a folder with a file: %r' % p.stderr)
    check(tree(src) == before, 'the source card changed')
    no_copies_left()

@case
def M17_out_errors(t):
    seed = t.seed()
    missing = t.path('nowhere', 'o.wav')
    ro = t.path('ro')
    os.makedirs(ro)
    os.chmod(ro, 0o555)
    try:
        for args, name in ((['--out', missing], missing),
                           (['--out', t.path('o.wav'), '--summary', os.path.join(ro, 's.json')],
                            os.path.join(ro, 's.json'))):
            start = time.monotonic()
            p = t.run(['--card', seed] + args, code=5)
            wall = time.monotonic() - start
            lines = p.stderr.splitlines()
            check(len(lines) == 1 and lines[0].startswith('lichomat-render: %s: ' % name),
                  '%r: stderr is %r' % (args, p.stderr))
            check(wall < 1.0, '%r: exit 5 after %.2f s, not before power-on' % (args, wall))
    finally:
        os.chmod(ro, 0o755)
    no_copies_left()

# voice 1 on steps 1, 5, 9 and 13, then START at 500 ms: sound for renders with --no-start
STEPS = '0ms press voice1\n+100ms press step1\n+100ms press step5\n+100ms press step9\n+100ms press step13\n' \
        '+100ms press start\n'

def samples(path):
    """(channels, the int16 samples, interleaved)"""
    ch, _, _, _, data = wav_info(path)
    a = array.array('h')
    a.frombytes(data)
    if sys.byteorder != 'little':
        a.byteswap()
    return ch, a

def dbfs(path, chans, t0, t1):
    """RMS level of the channels chans over [t0, t1) seconds; -inf for digital silence"""
    ch, a = samples(path)
    f0, f1 = int(t0 * 44140.625), int(t1 * 44140.625)
    acc, n = 0, 0
    for f in range(f0, f1):
        for c in chans:
            v = a[f * ch + c]
            acc += v * v
            n += 1
    return float('-inf') if acc == 0 else 10 * math.log10(acc / n / 32768.0 ** 2)

def log_lines(path, pattern):
    with open(path) as f:
        lines = f.read().splitlines()
    for x in lines:
        check(re.match(pattern, x), '%s: the line %r does not match %s' % (path, x, pattern))
        c, ms = x.split()[:2]
        u = int(c) // 216
        check(ms == '%d.%03d' % (u // 1000, u % 1000), '%s: %r: MS is not floor(cycles/216)/1000' % (path, x))
    return lines

MIDI_LINE = r'^\d+ \d+\.\d{3} (DIN [0-9A-F]{2}|USB( [0-9A-F]{8})+)$'

def f8_gaps(lines):
    """The gaps between the DIN F8 lines of a MIDI log, without those of an F8 that
    starts one byte time (69,120 cycles) after another DIN byte: it waited behind
    that byte on the wire, so its gaps measure the wire, not the clock."""
    f8 = [int(x.split()[0]) for x in lines if x.endswith(' DIN F8')]
    din = [int(x.split()[0]) for x in lines if ' DIN ' in x]
    queued = set(b for a, b in zip(din, din[1:]) if b - a == 69120)
    return [b - a for a, b in zip(f8, f8[1:]) if a not in queued and b not in queued]

@case
def M17_script_errors(t):
    seed = t.seed()
    bad = ['12xs mark a', '100ms mark a\n50ms mark b', '0ms flip 3', '0ms press 41', '0ms push 30', '0ms knob 5 +1',
           '0ms fader 1 1.5', '0ms din 9G', '0ms usb F0 01 F7', '0ms usb 30 40', '0ms usb-clock 255 200',
           '0ms reset maybe', '0ms card sideways', '0ms jacks L3', '0ms mark', '0ms mark a b']
    for text in bad:
        s = t.script('# a comment line\n' + text + '\n')
        line = 1 + text.count('\n') + 1
        p = t.run(['--card', seed, '--out', t.path('o.wav'), '--events', s], code=1)
        lines = p.stderr.splitlines()
        check(len(lines) == 1 and lines[0].startswith('lichomat-render: %s:%d: ' % (s, line)),
              '%r: stderr does not name %s:%d: %r' % (text, s, line, p.stderr))
    missing = t.path('missing.txt')
    p = t.run(['--card', seed, '--out', t.path('o.wav'), '--events', missing], code=1)
    check(p.stderr.startswith('lichomat-render: %s: ' % missing), 'a missing script: %r' % p.stderr)
    # the good forms of the same events parse
    good = t.script('0ms mark a # comment\n+1ms press step16 60\n1.5ms push 40\n441f knob 4 -3\n'
                    '10ms fader 6 0.25\n\t10ms din 90 30 64\n10ms usb 90 30 64 C0 01 F8 F2 00 01\n'
                    '11ms usb-clock 255 60\n12ms usb-clock off\n12ms jacks L1,R2\n0.02s mark b\n')
    t.render(seed, ['--seconds', '0.1', '--events', good])
    no_copies_left()

@case
def M17_events_din(t):
    seed = t.seed()
    log = t.path('din.log')
    s, wav, _ = t.render(seed, ['--no-start', '--seconds', '2', '--midi-log', log, '--events',
                                t.script('100ms din 90 30 64\n200ms din-clock 120\n1200ms din-clock off\n')])
    check(s['counters']['dinIn'] == 51, 'dinIn is %d, not 51 (3 + 48 clocks)' % s['counters']['dinIn'])
    check(dbfs(wav, (0, 1), 0, 0.1) == float('-inf'), 'out 1 is not silent before the note')
    level = dbfs(wav, (0, 1), 0.1, 0.4)
    check(level > -60, 'out 1 is at %.1f dBFS after the note' % level)
    lines = log_lines(log, MIDI_LINE)
    f8 = [int(x.split()[0]) for x in lines if x.endswith(' DIN F8')]
    check(95 <= len(f8) <= 97, '%d DIN F8 lines, not 96 +- 1' % len(f8))
    # the note comes back out on DIN (seven Note Ons and Offs)
    gaps = f8_gaps(lines)
    check(len(gaps) >= len(f8) - 3, 'only %d of %d DIN F8 gaps are free of the wire' % (len(gaps), len(f8) - 1))
    check(all(abs(g - 4500000) <= 54000 for g in gaps), 'DIN F8 gaps %r' % sorted(set(gaps))[:8])

@case
def M17_events_usb(t):
    seed = t.seed()
    for clock, want in (('200ms usb-clock 240 50', 21), ('200ms usb-clock 240', 97)):
        log = t.path('usb%d.log' % want)
        s, _, _ = t.render(seed, ['--no-start', '--seconds', '2', '--midi-log', log, '--events',
                                  t.script('100ms usb 90 30 64\n%s\n1200ms usb-clock off\n' % clock)])
        check(s['counters']['usbIn'] == want, '%s: usbIn is %d, not %d' % (clock, s['counters']['usbIn'], want))
        log_lines(log, MIDI_LINE)

@case
def M17_events_jacks(t):
    seed = t.seed()
    pulses = ''.join('%dms jack-pulse\n' % (100 + 125 * j) for j in range(16))
    runs = (('0ms reset off\n100ms jack-clock 120 4\n2s reset on\n', 'clock'),
            ('0ms reset off\n2s reset on\n', 'none'),
            ('0ms reset off\n' + pulses + '2s reset on\n', 'pulses'))
    for text, name in runs:
        log = t.path('%s.log' % name)
        t.render(seed, ['--bpm', 'ext', '--no-start', '--seconds', '3', '--clock-log', log, '--events', t.script(text)])
        lines = log_lines(log, r'^\d+ \d+\.\d{3} [01]$')
        levels = [x.split()[2] for x in lines]
        check(all(a != b for a, b in zip(levels, levels[1:])), '%s: the levels do not alternate' % name)
        rising = [int(x.split()[0]) for x in lines if x.endswith(' 1')]
        early = [c for c in rising if 100 * 216000 <= c < 2000 * 216000]
        late = [c for c in rising if c >= 2200 * 216000]
        if name == 'clock':
            check(early and not late, 'clock: %d rising edges in [100 ms, 2 s), %d after 2.2 s' % (len(early), len(late)))
        elif name == 'none':
            check(not rising, 'no clock in: %d rising edges' % len(rising))
        else:
            check(rising, 'pulses: no rising edge')

@case
def M17_events_panel(t):
    seed = t.seed()
    log = t.path('panel.log')
    text = ('0ms mark boot\n100ms push\n300ms mark edit\n400ms push\n500ms encoder +1\n600ms knob 1 +20\n'
            '700ms fader 1 0.5\n800ms press voice1\n1000ms press step1\n1200ms mark step\n1300ms jacks none\n'
            '1400ms card out\n1600ms card in\n1800ms mark end\n')
    s, _, _ = t.render(seed, ['--no-start', '--seconds', '2', '--panel-log', log, '--events', t.script(text)])
    lines = log_lines(log, r'^\d+ \d+\.\d{3} \S+ ".*" ".*" (\d,\d+|-) [0-9A-F]{11}$')
    names = [x.split()[2] for x in lines]
    check(names == ['boot', 'edit', 'step', 'end'], 'the marks are %r' % names)
    for x, ms in zip(lines, (0, 300, 1200, 1800)):
        late = int(x.split()[0]) - ms * 216000
        check(0 <= late < 54000 + frames_to_cycles(64), '%r: %d cycles after its time' % (x, late))
    rows = [re.findall(r'"(?:[^"\\]|\\x[0-9A-F]{2})*"', x)[:2] for x in lines]
    check(rows[0] != rows[1], 'the edit page shows the rows of the boot: %r' % rows[0])
    leds = int(lines[2].split()[-1], 16)
    check(leds >> 39 & 1, 'LED 39 (step 1) is not lit at the mark step: %011X' % leds)
    check([d['event'] for d in s['direct']] == ['mark'] * 4, 'the summary lists %r' % s['direct'])
    # each line is at the boundary where its direct event ran, and that is not before its due time
    for x, d, ms in zip(lines, s['direct'], (0, 300, 1200, 1800)):
        check(d['due'] == s['t0'] + ms * 216000 and d['at'] >= d['due'] and int(x.split()[0]) == d['at'] - s['t0'],
              '%r does not match the summary entry %r' % (x, d))

@case
def M17_stereo(t):
    seed = t.seed()
    script = t.script(STEPS + '1500ms jacks L2,R2\n')
    args = ['--no-start', '--seconds', '3', '--events', script]
    _, four, _ = t.render(seed, args)
    ch, a = samples(four)
    check(ch == 4, 'the default WAV has %d channels' % ch)
    before, after = dbfs(four, (0, 1), 0.5, 1.5), dbfs(four, (2, 3), 1.6, 3.0)
    check(before > -60 and after > -60, 'out 1 before 1.5 s: %.1f dBFS, out 2 after it: %.1f dBFS' % (before, after))
    check(dbfs(four, (2, 3), 0.5, 1.5) < before - 20, 'out 2 is not quieter than out 1 before 1.5 s')
    for mode in ('out1', 'out2', 'sum'):
        _, w, _ = t.render(seed, args + ['--stereo', mode])
        c2, b = samples(w)
        check(c2 == 2 and len(b) * 2 == len(a), '--stereo %s: %d channels, %d samples' % (mode, c2, len(b)))
        for f in range(len(b) // 2):
            l1, r1, l2, r2 = a[4 * f:4 * f + 4]
            want = {'out1': (l1, r1), 'out2': (l2, r2), 'sum': ((l1 + l2) >> 1, (r1 + r2) >> 1)}[mode]
            if (b[2 * f], b[2 * f + 1]) != want:
                raise Fail('--stereo %s: frame %d is %r, not %r' % (mode, f, (b[2 * f], b[2 * f + 1]), want))

# --- the prelude and the edit path (plan R2.5) -----------------------------------------

PANEL_LINE = r'^\d+ \d+\.\d{3} \S+ ".*" ".*" (\d,\d+|-) [0-9A-F]{11}$'

def panel_rows(line):
    """the two LCD rows of a panel-log line, unescaped"""
    rows = re.findall(r'"((?:[^"\\]|\\x[0-9A-F]{2})*)"', line)[:2]
    return [re.sub(r'\\x([0-9A-F]{2})', lambda m: chr(int(m.group(1), 16)), r) for r in rows]

def blocks_of(seconds):
    """the whole blocks of a length in seconds (a Fraction), as plan P3 counts them"""
    frames = math.ceil(seconds * fractions.Fraction(353125, 8))
    return -(-frames // 64)

def glo_card(t, seed, name, byte0):
    """a copy of the seeded card with GLO.CFG byte 0, the stored tempo, set: 0 EXT, 1 MID"""
    card = card_copy(t, seed, name)
    glo = os.path.join(card, 'PROJ00', 'GLO.CFG')
    with open(glo, 'rb') as f:
        g = bytearray(f.read())
    g[0] = byte0
    with open(glo, 'wb') as f:
        f.write(bytes(g))
    return card

@case
def M17_pattern(t):
    seed = t.seed()
    script = t.script('0ms mark a\n')
    for p in (0, 5, 21, 47, 63):
        log = t.path('panel%d.log' % p)
        s, _, _ = t.render(seed, ['--seconds', '0.2', '--panel-log', log, '--events', script, '--pattern', p])
        check(s['inputs']['pattern'] == p and s['selected']['pattern'] == p,
              '--pattern %d: inputs %r, selected %r' % (p, s['inputs']['pattern'], s['selected']))
        lines = log_lines(log, PANEL_LINE)
        check(len(lines) == 1, '--pattern %d: the panel log has %d lines' % (p, len(lines)))
        row2 = panel_rows(lines[0])[1]
        m = re.match(r'Current: +(\d+) *$', row2)
        check(m and int(m.group(1)) == p + 1, '--pattern %d: row 2 at the mark is %r' % (p, row2))
    no_copies_left()

@case
def M17_song(t):
    seed = t.seed()
    for song in (0, 2, 63):
        s, _, _ = t.render(seed, ['--seconds', '0.2', '--song', song])
        check(s['inputs']['song'] == song and s['selected']['song'] == song,
              '--song %d: inputs %r, selected %r' % (song, s['inputs']['song'], s['selected']))
    no_copies_left()

@case
def M17_bpm(t):
    seed = t.seed()
    for bpm, sync in (('137', 0), ('ext', 1), ('mid', 2)):
        log = t.path('bpm-%s.log' % bpm)
        s, _, _ = t.render(seed, ['--seconds', '2', '--midi-log', log, '--bpm', bpm])
        sel = s['selected']
        f8 = [int(x.split()[0]) for x in log_lines(log, MIDI_LINE) if x.endswith(' DIN F8')]
        check(sel['sync'] == sync, '--bpm %s: selected %r' % (bpm, sel))
        if sync:
            check(not f8, '--bpm %s: %d DIN F8 lines' % (bpm, len(f8)))
            continue
        check(sel['bpm'] == 137, '--bpm 137: selected %r' % sel)
        check(len(f8) > 2, '--bpm 137: %d DIN F8 lines' % len(f8))
        mean = (f8[-1] - f8[0]) / (len(f8) - 1)
        print('--bpm 137: %d DIN F8 lines, %.1f cycles apart on average' % (len(f8), mean))
        check(abs(mean - 540000000 / 137) <= 54000, '--bpm 137: DIN F8 lines %.1f cycles apart, not 3,941,606 +- 54,000'
              % mean)
    s, wav, _ = t.render(seed, ['--bpm', '137', '--bars', '2'])
    want = blocks_of(fractions.Fraction(2 * 240, 137))
    frames = wav_info(wav)[3]
    check(s['inputs']['blocks'] == want and frames == 64 * want, '--bpm 137 --bars 2: %d blocks and %d frames, not %d'
          % (s['inputs']['blocks'], frames, want))
    no_copies_left()

@case
def M17_param_engine(t):
    seed = t.seed()
    base = ['--no-start', '--seconds', '3']
    # voice 1 on all 16 steps, then START at 1.7 s
    steps16 = t.script('0ms press voice1\n' + ''.join('+100ms press step%d\n' % n for n in range(1, 17)) +
                       '+100ms press start\n')
    # PAR_BPM: the device's MIDI clock out
    log = t.path('bpm.log')
    s, _, _ = t.render(seed, base + ['--midi-log', log, '--events', steps16, '--param', 'bpm=150'])
    check(s['params'] == {'bpm': 150} and s['selected']['bpm'] == 150 and s['selected']['sync'] == 0,
          'bpm=150: params %r, selected %r' % (s['params'], s['selected']))
    lines = log_lines(log, MIDI_LINE)
    f8 = [int(x.split()[0]) for x in lines if x.endswith(' DIN F8')]
    # voice 1's notes go out on DIN too, and the F8s that wait behind them are left out
    gaps = f8_gaps(lines)
    check(len(f8) > 2 and 2 * len(gaps) >= len(f8) - 1, 'bpm=150: %d DIN F8 lines, %d gaps free of the wire'
          % (len(f8), len(gaps)))
    check(all(abs(g - 3600000) <= 54000 for g in gaps), 'bpm=150: DIN F8 gaps %r' % sorted(set(gaps))[:8])
    mean = (f8[-1] - f8[0]) / (len(f8) - 1)
    check(abs(mean - 3600000) <= 54000, 'bpm=150: DIN F8 lines %.1f cycles apart on average' % mean)
    print('bpm=150: %d DIN F8 lines, %d of %d gaps free of the wire, %.1f cycles apart on average'
          % (len(f8), len(gaps), len(f8) - 1, mean))
    # PAR_SHUFFLE: the step times in the MIDI log
    ons = {}
    for v in (0, 127):
        log = t.path('shuffle%d.log' % v)
        s, _, _ = t.render(seed, base + ['--midi-log', log, '--events', steps16, '--param', 'shuffle=%d' % v])
        check(s['params'] == {'shuffle': v}, 'shuffle=%d: params %r' % (v, s['params']))
        ons[v] = [int(x.split()[0]) for x in log_lines(log, MIDI_LINE) if re.search(r' DIN 9[0-9A-F]$', x)]
    pairs = list(zip(ons[0], ons[127]))
    check(len(pairs) >= 8, 'only %d and %d Note Ons' % (len(ons[0]), len(ons[127])))
    for k, (a, b) in enumerate(pairs):
        # the 16ths count from step 1 at START: the odd ones (steps 2, 4, ...) move
        check(b > a if k % 2 else b == a, 'Note On %d: at %d with shuffle 0, %d with shuffle 127' % (k, a, b))
    print('shuffle 127 moves the odd 16ths by %r cycles' % sorted(set(b - a for a, b in pairs[1::2])))
    # PAR_VOL1, a sound parameter: the audio of voice 1
    steps = t.script(STEPS)
    _, plain, _ = t.render(seed, base + ['--events', steps])
    s, quiet, _ = t.render(seed, base + ['--events', steps, '--param', 'vol1=0'])
    check(s['params'] == {'vol1': 0}, 'vol1=0: params %r' % s['params'])
    loud = dbfs(plain, (0, 1), 0.5, 3.0)
    check(loud > -60, 'out 1 without vol1=0 is at %.1f dBFS after 500 ms' % loud)
    check(dbfs(quiet, (0, 1), 0.5, 3.0) == float('-inf'), 'out 1 with vol1=0 is not digital silence after 500 ms')
    no_copies_left()

@case
def M17_param_refused(t):
    seed = t.seed()
    out = t.path('o.wav')
    # the special cases of menu_encoderChangeParameter(), then two that do not exist;
    # all are refused before power-on, so --out is not made
    for spec, special in (('voice_lfo1=2', True), ('target_lfo1=3', True), ('vel_dest_1=3', True),
                          ('nosuch=1', False), ('999=1', False)):
        name = spec.split('=')[0]
        p = t.run(['--card', seed, '--out', out, '--param', spec], code=1)
        lines = p.stderr.splitlines()
        check(len(lines) == 1 and lines[0].startswith('lichomat-render: --param: parameter %s: ' % name)
              and ('special case' in lines[0]) == special, '--param %s: %r' % (spec, p.stderr))
        check(not os.path.exists(out), '--param %s: the WAV was made' % spec)
    script = t.script('0ms mark a\n0ms param voice_lfo1=2\n')
    p = t.run(['--card', seed, '--out', out, '--events', script], code=1)
    lines = p.stderr.splitlines()
    check(len(lines) == 1 and lines[0].startswith('lichomat-render: %s:2: ' % script) and 'voice_lfo1' in lines[0]
          and 'special case' in lines[0], 'the param line: %r' % p.stderr)
    check(not os.path.exists(out), 'the param line: the WAV was made')
    # a name in upper case, clamped by DTYPE_0B127; then the same parameter by number
    s, _, _ = t.render(seed, ['--seconds', '0.1', '--param', 'SHUFFLE=200'])
    check(s['params'] == {'shuffle': 127}, 'SHUFFLE=200: params %r' % s['params'])
    n = s['paramNumbers']['shuffle']
    s, _, _ = t.render(seed, ['--seconds', '0.1', '--param', '%d=5' % n])
    check(s['params'] == {'shuffle': 5} and s['paramNumbers'] == {'shuffle': n},
          '%d=5: params %r, paramNumbers %r' % (n, s['params'], s['paramNumbers']))
    no_copies_left()

def par_enumerators(header):
    """the enumerators of enum ParamEnums that start with PAR_, by a tokenizer: an
    enumerator is the identifier at the start of the body or after a comma"""
    with open(header) as f:
        text = f.read()
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    tokens = re.findall(r'[A-Za-z_][A-Za-z0-9_]*|\d+|\S', text)
    i = next(k for k in range(len(tokens) - 2) if tokens[k:k + 3] == ['enum', 'ParamEnums', '{']) + 3
    names, start = [], True
    while tokens[i] != '}':
        if start and re.match(r'[A-Za-z_]', tokens[i]):
            names.append(tokens[i])
        start = tokens[i] == ','
        i += 1
    return [x for x in names if x.startswith('PAR_')]

@case
def M17_param_table(t):
    gen = subprocess.run([sys.executable, os.path.join(t.root, 'emu', 'render', 'gen_params.py'), t.root],
                         capture_output=True, check=True).stdout
    with open(os.path.join(t.root, 'emu', 'render', 'render_params.h'), 'rb') as f:
        committed = f.read()
    check(gen == committed, 'gen_params.py does not give emu/render/render_params.h byte for byte')
    entries = re.findall(r'^\t\{ "([a-z0-9_]+)", (PAR_[A-Z0-9_]+) \},$', committed.decode(), re.M)
    want = par_enumerators(os.path.join(t.root, 'Core', 'Inc', 'Parameters.h'))
    check([e[1] for e in entries] == want, 'the table has %d entries, enum ParamEnums %d PAR_ enumerators'
          % (len(entries), len(want)))
    check(all(n == p[4:].lower() for n, p in entries), 'a table name is not its PAR_ name in lower case')
    print('%d entries' % len(entries))

@case
def M17_bars(t):
    seed = t.seed()
    ext, mid = glo_card(t, seed, 'ext', 0), glo_card(t, seed, 'mid', 1)
    want = blocks_of(4)	# 2 bars at 120 BPM
    for args in (['--bars', '2'], ['--bpm', '120', '--bars', '2']):
        s, wav, _ = t.render(ext, args)
        frames = wav_info(wav)[3]
        check(s['inputs']['blocks'] == want and frames == 64 * want and s['selected']['bpm'] == 120
              and s['selected']['sync'] == 0, 'EXT, %r: %d blocks, %d frames, selected %r, not %d blocks at 120 BPM'
              % (args, s['inputs']['blocks'], frames, s['selected'], want))
    # MID with a clock in the script stays MID: no internal tempo for --bars
    script = t.script('0ms din-clock 120\n')
    p = t.run(['--card', mid, '--out', t.path('o.wav'), '--bars', '2', '--events', script], code=1)
    lines = p.stderr.splitlines()
    check(len(lines) == 1 and lines[0].startswith('lichomat-render: --bars: ') and 'MID' in lines[0],
          'MID with a clock and --bars: %r' % p.stderr)
    no_copies_left()

@case
def M17_sync_fallback(t):
    seed = t.seed()
    ext, mid = glo_card(t, seed, 'ext', 0), glo_card(t, seed, 'mid', 1)
    for card, mode, text, sync in ((ext, 'EXT', None, 0), (ext, 'EXT', '0ms jack-clock 120 4\n', 1),
                                   (mid, 'MID', None, 0), (mid, 'MID', '0ms usb-clock 120\n', 2)):
        args = ['--seconds', '1'] + (['--events', t.script(text)] if text else [])
        s, _, p = t.render(card, args)
        sel = s['selected']
        warned = [w for w in s['warnings'] if 'syncs to' in w]
        what = '%s, %r: selected %r, warnings %r' % (mode, text, sel, s['warnings'])
        check(sel['sync'] == sync, what)
        if text:
            check(not warned, what)
            continue
        want = 'the project syncs to %s; the script sends no clock to that input: 120 BPM internal' % mode
        check(warned == [want] and sel['bpm'] == 120, what)
        check('lichomat-render: warning: ' + want in p.stderr.splitlines(), '%s: stderr %r' % (mode, p.stderr))
    no_copies_left()

if __name__ == '__main__':
    main()
