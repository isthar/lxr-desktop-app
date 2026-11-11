#!/usr/bin/env python3
"""Host tests of lichomat-render (stage 1b spec 5, 12.1 M14, M17, M23).

    render_test.py --render R --render-test RT --root ROOT --work DIR [--sanitized 0|1] CASE

Each CASE is a function below; it runs in DIR/CASE, emptied first, and prints
PASS CASE (exit 0) or the failed check (exit 1). --sanitized 1 says that the
build has UBSan, so that lichomat-render-test --test-fault ubsan reports. Its
renders make their card
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
import argparse, array, fractions, glob, hashlib, json, math, os, re, shlex, shutil, struct, subprocess, sys, time, wave

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
        self.sanitized = a.sanitized == '1'
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
    ap.add_argument('--sanitized', choices=('0', '1'), default='0')
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
           '--wav-rate', '--stereo', '--midi-log', '--clock-log', '--panel-log', '--trig-log', '--summary',
           '--keep-card', '--version', '--help']
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
    # The RIFF limit: the RIFF chunk's size, 36 + the data, fits 32 bits. The longest
    # length that fits passes the options (exit 2: the card C does not exist), one
    # step more is refused before anything is made.
    limit = 2 ** 32 - 1 - 36
    def over_limit(args, name):
        p = t.run(['--card', 'C', '--out', 'O'] + args, code=1)
        lines = p.stderr.splitlines()
        check(len(lines) == 1 and lines[0].startswith('lichomat-render: %s: ' % name) and 'RIFF limit' in lines[0]
              and str(limit) in lines[0], '%r: %r' % (args, p.stderr))
    for stereo, ch in (([], 4), (['--stereo', 'sum'], 2)):
        frames = 64 * (limit // (64 * 2 * ch))
        us = frames * 8 * 10 ** 6 // 353125	# the longest T in us whose frames fit
        t.run(['--card', 'C', '--out', 'O', '--seconds', '%d.%06d' % divmod(us, 10 ** 6)] + stereo, code=2)
        over_limit(['--seconds', '%d.%06d' % divmod(us + 1, 10 ** 6)] + stereo, '--seconds')
    # --bars with a numeric --bpm: 101 bars at 2 BPM fit 4 channels (12,120 s), 102 do not
    t.run(['--card', 'C', '--out', 'O', '--bpm', '2', '--bars', '101'], code=2)
    over_limit(['--bpm', '2', '--bars', '102'], '--bars')

def version_lines(root):
    with open(os.path.join(root, 'CMakeLists.txt')) as f:
        version = re.search(r'project\(LXR_V2_STM32F765[^)]*VERSION ([0-9]+\.[0-9]+)', f.read()).group(1)
    return version, [
        'lichomat-render %s, the renderer of LICHOMAT Emu' % version,
        'emulator of LICHOMAT, unofficial custom firmware for the Erica Synths LXR-02',
        'Runs LICHOMAT %s with the emulator changes F1-F18 and F6b. Built from commit COMMIT.' % version,
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
    check(version == '0.8', 'the firmware version is %s, not 0.8 (spec 10.1)' % version)
    out = t.run(['--version']).stdout
    m = re.search(r'Built from commit ([0-9a-f]{12}|unknown)\.', out)
    check(m, '--version names no commit of 12 hex digits: %r' % out)
    # the commit is read at build time: a build after a commit names it
    head = subprocess.run(['git', '-C', t.root, 'rev-parse', '--short=12', 'HEAD'], capture_output=True, text=True)
    commit = head.stdout.strip() if head.returncode == 0 and head.stdout.strip() else 'unknown'
    check(m.group(1) == commit, '--version names the commit %s, but HEAD is %s' % (m.group(1), commit))
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
    # a warning that explains the error is not lost: every exit prints the warnings,
    # after its message (spec 11; plan P5's exit 0 prints them after the summary line)
    renamed = card_copy(t, seed, 'renamed')
    os.rename(os.path.join(renamed, 'PROJ00'), os.path.join(renamed, 'Project00'))
    p = t.run(['--card', renamed, '--out', t.path('o.wav')], code=2)
    want = ['lichomat-render: card %s: project 0 has no PROJ00/PRJ.NFO' % renamed,
            'lichomat-render: warning: card: %s is not an 8.3 name; the device would not see it'
            % os.path.join(renamed, 'Project00')]
    check(p.stderr.splitlines() == want, 'PROJ00 renamed Project00: stderr %r, not %r' % (p.stderr, want))
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
    # v0.4 (plan D34): each of the 64 kits carries the 13-byte LCHM block, 64 x 13 = 832 bytes more
    check((len(names), size) == (194, 258420), 'PROJ00 holds %d files of %d bytes, not 194 of 258,420'
          % (len(names), size))
    def read(name):
        with open(os.path.join(proj, name), 'rb') as f:
            return f.read()
    # v0.3 (plan D20, D28): the name, then the scene block with no scenes stored
    check(read('PRJ.NFO') == b'Init\0\0\0\0LCHM\x01\x08' + bytes(8), 'PRJ.NFO is %r' % read('PRJ.NFO'))
    glo = read('GLO.CFG')
    check(len(glo) == 30 and glo[0] == 120 and glo[29] == 1, 'GLO.CFG is %r' % glo)
    check(len(read('00-INITK.SND')) == 268, '00-INITK.SND has %d bytes' % len(read('00-INITK.SND')))
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
        riff_sizes(w)
    riff_sizes(a)
    riff_sizes(b)

def riff_sizes(path):
    """the canonical header's sizes: RIFF = the file's size - 8, data = - 44"""
    size = os.path.getsize(path)
    with open(path, 'rb') as f:
        h = f.read(44)
    riff, data = struct.unpack('<I', h[4:8])[0], struct.unpack('<I', h[40:44])[0]
    check(h[36:40] == b'data' and (riff, data) == (size - 8, size - 44), '%s: RIFF size %d, data size %d, file %d bytes'
          % (path, riff, data, size))

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
    # two files for one slot (spec 11): the device's warning names the source card,
    # not the copy; and every warning reaches the summary, here 41
    dup = card_copy(t, t.seed(), 'dup')
    shutil.copy(os.path.join(dup, 'PROJ00', '05-.PAT'), os.path.join(dup, 'PROJ00', '05-X.PAT'))
    longs = [os.path.join(dup, 'PROJ00', 'LongName%02d.txt' % k) for k in range(40)]
    for x in longs:
        with open(x, 'wb') as f:
            f.write(b'x')
    s, _, p = t.render(dup, ['--seconds', '0.1'])
    slot = 'card: %s holds 05-.PAT and 05-X.PAT for one slot; the older, 05-.PAT, loads' % os.path.join(dup, 'PROJ00')
    names = ['card: %s is not an 8.3 name; the device would not see it' % x for x in longs]
    check(sorted(s['warnings']) == sorted(names + [slot]), 'the warnings are %r' % s['warnings'])
    warned = [x for x in p.stderr.splitlines() if x.startswith('lichomat-render: warning: ')]
    check(sorted(warned) == sorted('lichomat-render: warning: ' + w for w in names + [slot]),
          'stderr warns %r' % warned)
    check(not any(t.tmp in w for w in s['warnings']), 'a warning names the card copy: %r' % s['warnings'])
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
    # the second note comes after the clock line and inside its run: it enters at its
    # own time, between the clocks (spec 5.4), not after the run
    s, wav, _ = t.render(seed, ['--no-start', '--seconds', '2', '--midi-log', log, '--events',
                                t.script('100ms din 90 30 64\n200ms din-clock 120\n300ms din 90 30 64\n'
                                         '1200ms din-clock off\n')])
    check(s['counters']['dinIn'] == 54, 'dinIn is %d, not 54 (2 x 3 + 48 clocks)' % s['counters']['dinIn'])
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
    notes_on_time(lines, s, (100, 300))

def notes_on_time(lines, s, times):
    """Each incoming note at times (ms) comes back out as a Note On on USB: the first
    transfer that starts with one at or after its time. The first note comes before
    any clock line, so its delay is the device's; every later one must be as late
    after its time within 54,000 + 313,181 cycles, the rule of the direct events (the
    next pass boundary: the grid, or a pass held up by a block), which also covers a
    USB transfer of the device's own clock still in flight (1 ms, 216,000 cycles)."""
    ons = [int(x.split()[0]) for x in lines if re.search(r' USB 099[0-9A-F]{5}( |$)', x)]
    late = []
    for ms in times:
        after = [c for c in ons if c >= ms * 216000]
        check(after, 'no Note On on USB out after the note at %d ms: %r' % (ms, ons))
        late.append(after[0] - ms * 216000)
    print('the notes at %r ms come back out %r cycles later' % (list(times), late))
    for ms, d in zip(times[1:], late[1:]):
        check(abs(d - late[0]) < 54000 + frames_to_cycles(64), 'the note at %d ms comes back out %d cycles after '
              'it, the first %d cycles after it' % (ms, d, late[0]))

@case
def M17_events_usb(t):
    seed = t.seed()
    # the second note: after the clock line, inside its run (as in M17_events_din)
    for clock, want in (('200ms usb-clock 240 50', 22), ('200ms usb-clock 240', 98)):
        log = t.path('usb%d.log' % want)
        s, _, _ = t.render(seed, ['--no-start', '--seconds', '2', '--midi-log', log, '--events',
                                  t.script('100ms usb 90 30 64\n%s\n300ms usb 90 30 64\n1200ms usb-clock off\n' % clock)])
        check(s['counters']['usbIn'] == want, '%s: usbIn is %d, not %d' % (clock, s['counters']['usbIn'], want))
        notes_on_time(log_lines(log, MIDI_LINE), s, (100, 300))

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
    # a clock generator stops at the next line of its kind or at the end, whichever
    # comes first (plan P4): 1000 BPM at 24 PPQ is a pulse every 540,000 cycles
    s, _, _ = t.render(seed, ['--bpm', 'ext', '--no-start', '--seconds', '0.1', '--events',
                              t.script('0ms jack-clock 1000 24\n1s jack-clock off\n')])
    length = s['end'] - s['t0']
    want = -(-length // 540000)
    check(s['scriptInputs'] == want, 'the jack clock made %d inputs, not the %d before the end'
          % (s['scriptInputs'], want))

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
    # + times count from the previous line's time, also after a + line; and the
    # summary lists every direct event, here 300 marks
    log = t.path('marks.log')
    text = '0ms mark a\n+100ms mark b\n+100ms mark c\n' + ''.join('+1ms mark m%d\n' % k for k in range(297))
    s, _, _ = t.render(seed, ['--no-start', '--seconds', '0.6', '--panel-log', log, '--events', t.script(text)])
    lines = log_lines(log, PANEL_LINE)
    check(len(lines) == 300 and len(s['direct']) == 300, '300 marks: %d panel-log lines, %d direct entries'
          % (len(lines), len(s['direct'])))
    for x, d, ms in zip(lines, s['direct'], [0, 100, 200] + [201 + k for k in range(297)]):
        late = int(x.split()[0]) - ms * 216000
        check(d['due'] == s['t0'] + ms * 216000 and 0 <= late < 54000 + frames_to_cycles(64),
              '%r: due %d, %d cycles after %d ms' % (x, d['due'] - s['t0'], late, ms))

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
    # MID with a clock in the script stays MID: no internal tempo for --bars; the
    # card's warning comes after the message
    script = t.script('0ms din-clock 120\n')
    with open(os.path.join(mid, 'PROJ00', 'LongName9.txt'), 'wb') as f:
        f.write(b'x')
    p = t.run(['--card', mid, '--out', t.path('o.wav'), '--bars', '2', '--events', script], code=1)
    lines = p.stderr.splitlines()
    check(len(lines) == 2 and lines[0].startswith('lichomat-render: --bars: ') and 'MID' in lines[0]
          and lines[1] == 'lichomat-render: warning: card: %s is not an 8.3 name; the device would not see it'
          % os.path.join(mid, 'PROJ00', 'LongName9.txt'), 'MID with a clock and --bars: %r' % p.stderr)
    # the card's tempo gives the length only after the prelude: 102 bars at 2 BPM are
    # above the RIFF limit there (see M17_cli_usage)
    p = t.run(['--card', glo_card(t, seed, 'slow', 2), '--out', t.path('o.wav'), '--bars', '102'], code=1, timeout=60)
    lines = p.stderr.splitlines()
    check(len(lines) == 1 and lines[0].startswith('lichomat-render: --bars: ') and 'RIFF limit' in lines[0],
          '102 bars at the card\'s 2 BPM: %r' % p.stderr)
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

# --- the save events, faults and the watchdog (plan R2.6) --------------------------------

TRAIL_LINE = r'^\d+ (body|pass|boot|input|direct) \d+$'

def one_file(card, pattern):
    """the bytes of the one file of PROJ00 that matches pattern"""
    found = glob.glob(os.path.join(card, 'PROJ00', pattern))
    check(len(found) == 1, '%s: %d files match PROJ00/%s' % (card, len(found), pattern))
    with open(found[0], 'rb') as f:
        return f.read()

@case
def M17_save(t):
    seed = t.seed()
    keep = t.path('K')
    script = t.script('0ms press voice1\n+100ms press step1\n+100ms save-pattern 5\n+100ms param vol1=10\n'
                      '+100ms save-kit 7\n1s save-project 0\n3.5s mark end\n')
    s, _, _ = t.render(seed, ['--no-start', '--seconds', '4', '--keep-card', keep, '--events', script])
    # the pattern: step 1 went into slot 5, which became active, and pattern 0's edit was reverted
    check(one_file(keep, '05-*.PAT') != one_file(seed, '05-*.PAT'), 'PROJ00/05-*.PAT equals the seed\'s')
    check(one_file(keep, '00-*.PAT') == one_file(seed, '00-*.PAT'), 'PROJ00/00-*.PAT differs from the seed\'s')
    # the kit: save-kit 7 took the edited kit; the project save also copies the edit buffer into
    # the save page's kit slot, 0 after the boot (presetManager.c preset_saveProject()), so kit 0
    # differs from the seed's in the vol1 byte only, and both files hold the same parameters
    k7, k0, s0 = one_file(keep, '07-*.SND'), one_file(keep, '00-INITK.SND'), one_file(seed, '00-INITK.SND')
    check(k7 != one_file(seed, '07-*.SND'), 'PROJ00/07-*.SND equals the seed\'s')
    diff = [i for i in range(min(len(k0), len(s0))) if k0[i] != s0[i]]
    check(len(k0) == len(s0) and len(diff) == 1 and k0[diff[0]] == 10,
          'PROJ00/00-INITK.SND differs from the seed\'s at %r' % [(i, s0[i], k0[i]) for i in diff])
    check(k7[8:] == k0[8:], 'the parameters of kit 7 and kit 0 differ')
    # each direct event at the first pass boundary at or after its time
    events = [d['event'] for d in s['direct']]
    check(events == ['save-pattern', 'param', 'save-kit', 'save-project', 'mark'], 'the direct events are %r' % events)
    for d in s['direct']:
        check(d['at'] >= d['due'] and d['at'] - d['due'] < 54000 + frames_to_cycles(64),
              '%s ran %d cycles after its time' % (d['event'], d['at'] - d['due']))
    print('the direct events ran %r cycles after their times' % [d['at'] - d['due'] for d in s['direct']])
    # a slot out of range is a script error
    bad = t.script('0ms save-kit 64\n')
    p = t.run(['--card', seed, '--out', t.path('o.wav'), '--events', bad], code=1)
    check(p.stderr.startswith('lichomat-render: %s:1: ' % bad), 'save-kit 64: %r' % p.stderr)
    no_copies_left()

# --- LICHOMAT v0.2 (plan V7-V9): through the panel and the edit path -------------------

@case
def V02_ui(t):
    seed = t.seed()
    log = t.path('p.log')
    script = t.script('0ms press voice2\n+100ms param track_tempo=4\n+100ms down shift\n+20ms press mode2\n'
                      '+100ms up shift\n+200ms mark euklid\n+100ms encoder +4\n+200ms mark track\n'
                      # NRPN 134 (PAR_TRACK_TEMPO - 128) = 50 on DIN: the front's NRPN parser sets the
                      # shown value to 50, past the 11 tempo names (display only, as in fw 1.7)
                      '+100ms din B0 63 01 B0 62 06 B0 06 32\n+300ms mark nrpn\n'
                      '+100ms press voice1\n+300ms mark voice1\n+100ms press mode1\n+200ms press mode1\n'
                      '+200ms param active_step=0\n+100ms param step_prob=101\n'
                      # the edit path does not repaint; asking for the step's data again does
                      # (FRONT_SEQ_REQUEST_STEP_PARAMS sends the stored 101 back, frontPanelParser.c SEQ_PROB)
                      '+100ms param active_step=0\n+300ms mark prob\n')
    t.render(seed, ['--no-start', '--seconds', '3', '--panel-log', log, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'LEN.stp.rot.mrp', rows['euklid'][0]), 'the Euklid page: %r' % rows['euklid'])
    # the second screen: voice 2 at x3/4, no humanize, no delay
    check(re.match(r'TMP.hmt.hmv.dly', rows['track'][0]) and re.match(r'3/4.  0.  0.  0', rows['track'][1]),
          'the track settings of voice 2: %r' % rows['track'])
    check(re.match(r' x4.  0.  0.  0', rows['nrpn'][1]), 'tmp 50 by NRPN, not shown as the last name: %r' % rows['nrpn'])
    check(re.match(r' x1.  0.  0.  0', rows['voice1'][1]), 'the track settings of voice 1: %r' % rows['voice1'])
    check('1:2' in rows['prob'][1], 'the step page shows %r, not the condition 1:2' % rows['prob'])
    no_copies_left()

def din_notes(path, status):
    """(cycles from t0, note, velocity) of each DIN Note On with this status byte"""
    lines = [x for x in log_lines(path, MIDI_LINE) if ' DIN ' in x and not x.endswith(' DIN F8')]
    return [(int(x.split()[0]), int(lines[i + 1].split()[-1], 16), int(lines[i + 2].split()[-1], 16))
            for i, x in enumerate(lines) if x.endswith(' DIN ' + status) and i + 2 < len(lines)]

def press_steps(voice, steps):
    return '+100ms press voice%d\n' % voice + ''.join('+100ms press step%d\n' % s for s in steps)

def ms_after(notes, ref):
    return [(c - ref) / 216000 for c, _, _ in notes]

def near(got, want, tol, what):
    check(len(got) >= len(want) and all(abs(g - w) <= tol for g, w in zip(got, want)),
          '%s: %r, not %r +- %g ms' % (what, [round(x, 2) for x in got[:len(want) + 2]], want, tol))

@case
def V02_tempo(t):
    # 3 against 4: voice 1 on the quarters at x1, voice 2 on the same steps at x3/4
    seed = t.seed()
    log = t.path('m.log')
    script = t.script(press_steps(1, (1, 5, 9, 13)) + press_steps(2, (1, 5, 9, 13))
                      + '+100ms param track_tempo=4\n+100ms press start\n')
    t.render(seed, ['--no-start', '--seconds', '4.5', '--param', 'midi_chan_2=2', '--midi-log', log,
                    '--events', script])
    v1, v2 = din_notes(log, '90'), din_notes(log, '91')
    check(v1 and v2, 'no Note Ons: %d and %d' % (len(v1), len(v2)))
    ref = v1[0][0]
    near(ms_after(v1, ref), [0, 500, 1000, 1500, 2000], 2.0, 'voice 1 at x1')
    near(ms_after(v2, ref), [0, 666.667, 1333.333, 2000], 2.0, 'voice 2 at x3/4')
    no_copies_left()

@case
def V02_groove(t):
    # voice 2, step 1 at the start, is the reference; voice 1 plays steps 2-16 with one setting
    seed = t.seed()
    def run(setting, seed_n=1):
        log = t.path('g%d.log' % (t.n + 1))
        script = t.script(press_steps(2, (1,)) + press_steps(1, range(2, 17)) + '+100ms param %s\n' % setting
                          + '+100ms press start\n')
        t.render(seed, ['--no-start', '--seconds', '4.2', '--seed', str(seed_n), '--param', 'midi_chan_2=2',
                        '--midi-log', log, '--events', script])
        ref = din_notes(log, '91')[0][0]
        notes = din_notes(log, '90')
        return ms_after(notes, ref), [v for _, _, v in notes]
    grid = [125.0 * k for k in range(1, 16)]
    near(run('track_delay=15')[0], grid, 1.5, 'no delay')
    near(run('track_delay=45')[0], [g + 23 * 500 / 384 for g in grid], 1.5, 'delay +30 ms: 23 ticks')
    near(run('track_delay=0')[0], [g - 12 * 500 / 384 for g in grid], 1.5, 'delay -15 ms: -12 ticks')
    h1, h1b, h2 = run('human_time=8', 1)[0], run('human_time=8', 1)[0], run('human_time=8', 2)[0]
    check(h1 == h1b, 'humanize with --seed 1, twice: %r and %r' % (h1, h1b))
    check(h1 != h2, 'humanize with --seed 1 and --seed 2 gives the same times')
    dev = [round(a - g, 1) for a, g in zip(h1, grid)]
    check(len(h1) >= 15 and all(abs(d) <= 6 * 500 / 384 + 1.5 for d in dev) and len(set(dev)) >= 3,
          'humanize time 8 ms (+-6 ticks): %r' % dev)
    vel = run('human_vol=25')[1][:15]     # the 16th Note On is step 2 of the next loop
    check(len(vel) == 15 and all(95 <= v <= 127 for v in vel) and len(set(vel)) >= 3,
          'humanize volume 25 %%: %r' % vel)
    no_copies_left()

@case
def V02_conditions(t):
    # voice 1: 1:2 on step 1, !PRE on step 3. Loops 1 and 3 play step 1, loops 2 and 4 step 3
    seed = t.seed()
    log = t.path('c.log')
    script = t.script(press_steps(1, (1, 3)) + '+100ms param active_step=0\n+100ms param step_prob=101\n'
                      '+100ms param active_step=2\n+100ms param step_prob=115\n+100ms press start\n')
    t.render(seed, ['--no-start', '--seconds', '8.5', '--midi-log', log, '--events', script])   # loops 1-4
    v1 = din_notes(log, '90')
    check(v1, 'no Note On')
    got = ms_after(v1, v1[0][0])
    near(got, [0, 2250, 4000, 6250], 2.0, 'the conditions')
    check(len(got) == 4, '%d Note Ons, not 4: %r' % (len(got), got))
    no_copies_left()

@case
def V02_files(t):
    seed = t.seed()
    keep = t.path('K')
    script = t.script(press_steps(2, (1, 5, 9, 13)) + '+100ms param track_tempo=4\n+100ms param human_vol=9\n'
                      '+100ms save-project 0\n')
    t.render(seed, ['--no-start', '--seconds', '3', '--keep-card', keep, '--events', script])
    pat = one_file(keep, '00-*.PAT')
    check(len(pat) == 3697 and pat[3663:3669] == b'LCHM\x01\x07',
          'PROJ00/00-*.PAT: %d bytes, block %r' % (len(pat), pat[3663:3669]))
    check(pat[3669:] == bytes(4) + bytes([0xFF, 0, 9, 0]) + bytes(20), 'the block holds %r' % pat[3669:])
    # a new render from the kept card: voice 2 plays at x3/4 without being set
    log = t.path('f.log')
    t.render(keep, ['--no-start', '--seconds', '3.5', '--param', 'midi_chan_2=2', '--midi-log', log,
                    '--events', t.script('100ms press start\n')])
    v2 = din_notes(log, '91')
    check(v2, 'no Note On of voice 2')
    near(ms_after(v2, v2[0][0]), [0, 666.667, 1333.333, 2000], 2.0, 'voice 2 after the reload')
    no_copies_left()

@case
def V02_rotation(t):
    # voice 2 step 1 is the reference; voice 1's steps 1, 5, 9, 13 rotated one step later
    seed = t.seed()
    log = t.path('r.log')
    script = t.script(press_steps(2, (1,)) + press_steps(1, (1, 5, 9, 13))
                      + '+100ms down shift\n+20ms press step2\n+100ms up shift\n+100ms press start\n')
    t.render(seed, ['--no-start', '--seconds', '3.5', '--param', 'midi_chan_2=2', '--midi-log', log,
                    '--events', script])
    ref = din_notes(log, '91')[0][0]
    near(ms_after(din_notes(log, '90'), ref), [125, 625, 1125, 1625], 2.0, 'voice 1 rotated later')
    no_copies_left()

@case
def V03_rolls(t):
    # the performance mode (MODE2 = 25), step 1 held while running: the roll of voice 1, 120 BPM
    seed = t.seed()
    def gaps(rate):
        log = t.path('r%d.log' % rate)
        script = t.script('+100ms press 25\n+100ms param roll=%d\n+100ms press start\n'
                          '+500ms down step1\n+1400ms up step1\n' % rate)
        t.render(seed, ['--no-start', '--seconds', '3.5', '--midi-log', log, '--events', script])
        notes = din_notes(log, '90')
        check(len(notes) >= 6, 'roll %d: %d Note Ons' % (rate, len(notes)))
        return [(b[0] - a[0]) / 216000 for a, b in zip(notes, notes[1:])]
    for rate, gap in ((10, 62.5), (9, 83.333), (7, 166.667), (8, 125.0)):	# 1/32 1/24 1/12 1/16
        d = gaps(rate)
        near(d, [gap] * len(d), 2.0, 'roll %d' % rate)
    no_copies_left()

def wav_left(path):
    """(rate, the left channel as floats) of a PCM 16/24-bit or float 32-bit WAV"""
    with open(path, 'rb') as f:
        b = f.read()
    pos, fmt, data = 12, None, b''
    while pos + 8 <= len(b):
        cid, size = b[pos:pos + 4], struct.unpack_from('<I', b, pos + 4)[0]
        if cid == b'fmt ':
            tag, ch, rate, _, _, bits = struct.unpack_from('<HHIIHH', b, pos + 8)
            fmt = (tag, ch, rate, bits)
        elif cid == b'data':
            data = b[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    tag, ch, rate, bits = fmt
    step = ch * bits // 8
    idx = range(0, len(data) - step + 1, step)
    if tag == 3 and bits == 32:
        return rate, [struct.unpack_from('<f', data, i)[0] for i in idx]
    if bits == 16:
        return rate, [struct.unpack_from('<h', data, i)[0] / 32768 for i in idx]
    if bits == 24:
        return rate, [int.from_bytes(data[i:i + 3], 'little', signed=True) / 8388608 for i in idx]
    raise ValueError('WAV format %r' % (fmt,))

def crossings(x, a, b):
    return sum(1 for i in range(a + 1, b) if (x[i - 1] < 0) != (x[i] < 0))

@case
def V03_slide(t):
    # Drum 1: step 1 note 75, step 9 note 87. With slide (508 ms) step 9 starts an octave low:
    # its first 100 ms cross zero about 0.54 times as often as without; slide time 0 = no slide
    seed = t.seed()
    def run(name, time, on):
        wav, log = t.path(name + '.wav'), t.path(name + '.log')
        script = t.script(press_steps(1, (1, 9)) + '+100ms param active_step=0\n+100ms param step_note=75\n'
                          '+100ms param active_step=8\n+100ms param step_note=87\n'
                          '+100ms param slide_time1=%d\n+100ms param step_slide=%d\n+100ms press start\n' % (time, on))
        t.render(seed, ['--no-start', '--seconds', '3.5', '--out', wav, '--stereo', 'out1', '--midi-log', log,
                        '--events', script])
        rate, x = wav_left(wav)
        return rate, x, din_notes(log, '90')
    rate, plain, notes = run('plain', 127, 0)
    _, slid, _ = run('slide', 127, 1)
    _, zero, _ = run('zero', 0, 1)
    check(zero == plain, 'slide time 0 changed the sound')
    on = next((i for i, (a, b) in enumerate(zip(plain, slid)) if abs(a - b) > 1e-4), None)
    check(on is not None, 'the slide changed nothing')
    want = notes[1][0] * rate / 216e6		# step 9's Note On, as a sample (WAV sample 0 = cycle 0)
    check(abs(on - want) <= rate * 0.004, 'the sound differs from sample %d, step 9 is at %d' % (on, want))
    a, b = on + rate * 5 // 1000, on + rate * 105 // 1000
    r = crossings(slid, a, b) / max(1, crossings(plain, a, b))
    check(0.45 <= r <= 0.65, 'step 9 crosses zero %.2f times as often with slide, not about 0.54' % r)
    no_copies_left()

@case
def V03_glide_through_ratchets(t):
    # Drum 1: step 1 note 75, step 9 note 87 with 2 ratchet hits (the second 62.5 ms later) and slide
    # (508 ms): the glide goes on through hit 1. 5 to 105 ms after hit 1 it is at -10.4 to -8.0
    # semitones, so step 9 crosses zero about 0.59 times as often as without slide there
    seed = t.seed()
    def run(name, on):
        wav, log = t.path(name + '.wav'), t.path(name + '.log')
        script = t.script(press_steps(1, (1, 9)) + '+100ms param active_step=0\n+100ms param step_note=75\n'
                          '+100ms param active_step=8\n+100ms param step_note=87\n+100ms param step_flam=1\n'
                          '+100ms param slide_time1=127\n+100ms param step_slide=%d\n+100ms press start\n' % on)
        t.render(seed, ['--no-start', '--seconds', '3.5', '--out', wav, '--stereo', 'out1', '--midi-log', log,
                        '--events', script])
        rate, x = wav_left(wav)
        return rate, x, din_notes(log, '90')
    rate, plain, notes = run('plain', 0)
    _, slid, _ = run('slide', 1)
    check(len(notes) >= 3, 'voice 1 played %d notes, not step 1 and the 2 hits of step 9' % len(notes))
    near(ms_after(notes[2:3], notes[1][0]), [62.5], 1.0, 'the second hit of step 9')
    hit1 = int(notes[2][0] * rate / 216e6)
    a, b = hit1 + rate * 5 // 1000, hit1 + rate * 105 // 1000
    r = crossings(slid, a, b) / max(1, crossings(plain, a, b))
    check(0.49 <= r <= 0.69, 'after hit 1 step 9 crosses zero %.2f times as often with slide, not about 0.59' % r)
    no_copies_left()

@case
def V03_fill(t):
    # voice 1: step 1 FILL (110), step 9 !FILL (111); voice 2 step 1 is the reference. Fill held
    # (performance mode, step 8) from 1.5 to 3.5 s after start: bar 2 (2-4 s) is the fill bar,
    # decided one step ahead
    seed = t.seed()
    log = t.path('f.log')
    script = t.script(press_steps(2, (1,)) + press_steps(1, (1, 9))
                      + '+100ms param active_step=0\n+100ms param step_prob=110\n'
                      '+100ms param active_step=8\n+100ms param step_prob=111\n'
                      '+100ms press 25\n+100ms press start\n+1500ms down step8\n+2000ms up step8\n')
    t.render(seed, ['--no-start', '--seconds', '9.5', '--param', 'midi_chan_2=2', '--midi-log', log,
                    '--events', script])
    ref = din_notes(log, '91')[0][0]
    near(ms_after(din_notes(log, '90'), ref), [1000, 2000, 5000, 7000], 2.0, 'FILL and !FILL around the fill bar')
    check(len(din_notes(log, '90')) == 4, 'voice 1 played %d times' % len(din_notes(log, '90')))
    no_copies_left()

@case
def V03_scenes(t):
    # voices 1 and 2 on the quarters; scene 1 = voice 2 muted, stored while stopped in the
    # performance mode; recalled 2.55 s after start: at the bar it applies at 4.0 s, at the step
    # at 2.625 s
    seed = t.seed()
    def run(quant):
        log = t.path('s%d.log' % quant)
        script = t.script(press_steps(1, (1, 5, 9, 13)) + press_steps(2, (1, 5, 9, 13))
                          + '+100ms param scene_quant=%d\n+100ms press 25\n+100ms press voice2\n'
                          '+100ms down copy\n+50ms press step9\n+100ms up copy\n+100ms press voice2\n'
                          '+100ms press start\n+2550ms press step9\n' % quant)
        t.render(seed, ['--no-start', '--seconds', '7.5', '--param', 'midi_chan_2=2', '--midi-log', log,
                        '--events', script])
        ref = din_notes(log, '90')[0][0]
        return ms_after(din_notes(log, '90'), ref), ms_after(din_notes(log, '91'), ref)
    v1, v2 = run(0)
    near(v1, [500.0 * k for k in range(11)], 2.0, 'voice 1 plays on')
    near(v2, [500.0 * k for k in range(8)], 2.0, 'voice 2 until the bar')
    check(len(v2) == 8, 'voice 2 after the bar: %r' % [round(x) for x in v2[8:]])
    v1, v2 = run(1)
    near(v2, [500.0 * k for k in range(6)], 2.0, 'voice 2 until the step')
    check(len(v2) == 6, 'voice 2 after the step: %r' % [round(x) for x in v2[6:]])
    no_copies_left()

@case
def V03_scene_file(t):
    # scene 3 = voices 1 and 3 muted, saved with project 0: PRJ.NFO holds the name and the block.
    # A render from the kept card (the boot loads the project) recalls it while stopped
    seed = t.seed()
    keep = t.path('K')
    script = t.script(press_steps(1, (1, 5, 9, 13)) + press_steps(3, (1, 5, 9, 13)) + press_steps(2, (1,))
                      + '+100ms press 25\n+100ms press voice1\n+100ms press voice3\n'
                      '+100ms down copy\n+50ms press step11\n+100ms up copy\n'
                      '+100ms press voice1\n+100ms press voice3\n+100ms save-project 0\n')
    t.render(seed, ['--no-start', '--seconds', '4', '--keep-card', keep, '--events', script])
    nfo = one_file(keep, 'PRJ.NFO')
    check(len(nfo) == 22 and nfo[8:14] == b'LCHM\x01\x08', 'PRJ.NFO: %d bytes, block %r' % (len(nfo), nfo[8:14]))
    check(nfo[14:] == bytes([0, 0, 0x85, 0, 0, 0, 0, 0]), 'the scenes are %r' % nfo[14:])
    log = t.path('f.log')
    t.render(keep, ['--no-start', '--seconds', '3', '--param', 'midi_chan_2=2', '--param', 'midi_chan_3=3',
                    '--midi-log', log, '--events', t.script('100ms press 25\n+100ms press step11\n+100ms press start\n')])
    check(din_notes(log, '91'), 'voice 2 is silent too')
    check(not din_notes(log, '90') and not din_notes(log, '92'), 'voices 1 or 3 play after the recall')
    no_copies_left()

# --- LICHOMAT v0.4 (plan A3-A9): atmosphere ------------------------------------------

@case
def V04_kit_file(t):
    # kits saved by v0.4 carry the 13-byte LCHM block after their 255 fw 1.7 bytes (plan D34);
    # the active kit holds the edited values, the DJ filter saved flat
    seed = t.seed()
    keep = t.path('K')
    script = t.script('+100ms param rev_size=90\n+100ms param fx_delay_sync=6\n'
                      '+100ms param dj_filter=20\n+100ms save-project 0\n')
    t.render(seed, ['--no-start', '--seconds', '3', '--keep-card', keep, '--events', script])
    kits = [open(k, 'rb').read() for k in sorted(glob.glob(os.path.join(keep, '**', '*.[Ss][Nn][Dd]'), recursive=True))]
    check(kits, 'no kit file on the kept card')
    check({len(d) for d in kits} == {268}, 'kit sizes %r' % sorted({len(d) for d in kits}))
    check(all(d[255:261] == b'LCHM\x01\x07' for d in kits), 'a kit without the block head')
    check(any(d[261] == 90 and d[267] == 6 for d in kits), 'no kit holds size 90 and sync 6')
    check(all(d[8 + 224] == 64 for d in kits), 'a kit saved the DJ filter off centre')
    no_copies_left()

@case
def V04_kit_fw17(t):
    # a kit of fw 1.7 is 255 bytes, without the LCHM block: it takes the block's defaults and
    # those of the five v0.4 slots, whatever fw 1.7 left there (plan D35), so it renders as the
    # same kit with the default block. Through the DJ filter's low-pass, where the slots'
    # resonance is heard; with a valid block the same stale slots are the kit's, and heard.
    src = os.path.join(t.root, 'emu', 'tests', 'cards', 'fx')
    name = os.path.join('PROJ00', '00-INITK.SND')
    with open(os.path.join(src, name), 'rb') as f:
        data = f.read()
    check(len(data) == 268 and data[255:] == b'LCHM\x01\x07' + bytes([100, 64, 10, 40, 0, 100, 0])
          and [data[8 + p] for p in (224, 225, 226, 227, 244)] == [64, 0, 0, 0, 64],
          'the fx card\'s kit is not the kit with the default block')
    stale = bytearray(data[:255])
    for p, v in ((224, 0), (225, 127), (226, 127), (227, 1), (244, 0)):	# DJ filter, reso, level, freeze, decay
        stale[8 + p] = v
    cards = {}
    for k, kit in (('default', data), ('fw17', bytes(stale)), ('stale', bytes(stale) + data[255:])):
        cards[k] = card_copy(t, src, k)
        with open(os.path.join(cards[k], name), 'wb') as f:
            f.write(kit)
    args = ['--bpm', '120', '--bars', '1', '--param', 'dj_filter=20']
    wavs = {}
    for k in cards:
        _, wav, _ = t.render(cards[k], args)
        with open(wav, 'rb') as f:
            wavs[k] = f.read()
    check(wavs['fw17'] == wavs['default'], 'the 255-byte kit renders unlike the kit with the default block')
    check(wavs['stale'] != wavs['default'], 'the stale slots with a block render as the defaults: the case cannot tell')
    no_copies_left()

@case
def V05_trig_setting(t):
    # trg (PAR_TRIG_TIMING, plan v0.5 Task 8, S18): 0 smp and 1 blk; the menu's clamp makes 9 a 1
    # (n - 1); a project save writes it as glo.cfg's byte 9, and glo.cfg stays 30 bytes
    seed = t.seed()
    for v, want in ((0, 0), (1, 1), (9, 1)):
        s, _, _ = t.render(seed, ['--no-start', '--seconds', '0.2', '--param', 'trig_timing=%d' % v])
        check(s['params'] == {'trig_timing': want}, 'trig_timing=%d: params %r' % (v, s['params']))
    keep = t.path('K')
    t.render(seed, ['--no-start', '--seconds', '1', '--param', 'trig_timing=1', '--keep-card', keep,
                    '--events', t.script('+100ms save-project 0\n')])
    glo = one_file(keep, 'GLO.CFG')
    check(len(glo) == 30 and glo[9] == 1, 'GLO.CFG: %d bytes, byte 9 %r' % (len(glo), glo[9:10]))
    no_copies_left()

@case
def V06_rec_setting(t):
    # rqn (PAR_REC_QUANT, plan v0.6 Task 6, M22): 0 off and 1 on, the menu's clamp makes 9 a 1;
    # a project save writes it as glo.cfg's byte 19, and glo.cfg stays 30 bytes
    seed = t.seed()
    for v, want in ((0, 0), (1, 1), (9, 1)):
        s, _, _ = t.render(seed, ['--no-start', '--seconds', '0.2', '--param', 'rec_quant=%d' % v])
        check(s['params'] == {'rec_quant': want}, 'rec_quant=%d: params %r' % (v, s['params']))
    keep = t.path('K')
    t.render(seed, ['--no-start', '--seconds', '1', '--param', 'rec_quant=1', '--keep-card', keep,
                    '--events', t.script('+100ms save-project 0\n')])
    glo = one_file(keep, 'GLO.CFG')
    check(len(glo) == 30 and glo[19] == 1, 'GLO.CFG: %d bytes, byte 19 %r' % (len(glo), glo[19:20]))
    # a stored value above 1 is off (review of Task 6): the boot's menu_sendAllGlobals() makes 2 a 0,
    # which the next project save writes
    found = glob.glob(os.path.join(keep, 'PROJ00', 'GLO.CFG'))
    check(len(found) == 1, 'GLO.CFG of the kept card: %r' % found)
    with open(found[0], 'wb') as g:
        g.write(glo[:19] + bytes([2]) + glo[20:])
    again = t.path('K2')
    t.render(keep, ['--no-start', '--seconds', '1', '--keep-card', again, '--events', t.script('+100ms save-project 0\n')])
    glo = one_file(again, 'GLO.CFG')
    check(len(glo) == 30 and glo[19] == 0, 'a stored 2: GLO.CFG byte 19 %r after the boot' % glo[19:20])
    no_copies_left()

@case
def V06_ui(t):
    # the track page's third screen (M15, V6-18): the encoder past dly shows tpl and amt with the
    # voice's values and stops at amt; back left to dly; the recording page shows rqn. Review of
    # Task 6: the scroll signs >, * and <; edit mode on tpl shows the template's long name; the
    # voice mode button from the third screen opens the voice page's first sub-page; SHIFT + mode2
    # steps through the three screens and back to the first. v0.7: the fourth screen follows the
    # third (plan v0.7 V7-16); v0.8: the fifth is the generator's (plan v0.8 G3)
    seed, log = t.seed(), t.path('panel.log')
    mode2 = '+100ms down shift\n+20ms press mode2\n+100ms up shift\n'
    script = t.script('0ms press voice2\n+100ms param track_tpl=3\n+100ms param track_tpl_amt=150\n'
                      + mode2 + '+200ms mark euklid\n'
                      '+100ms encoder +4\n+200ms mark track\n+100ms encoder +4\n+200ms mark tpl\n'
                      '+100ms push\n+200ms mark tpledit\n+100ms push\n+100ms encoder +1\n+200ms mark amt\n'
                      '+100ms encoder -2\n+200ms mark back\n+100ms encoder +1\n+100ms press mode1\n+200ms mark voice\n'
                      + mode2 + '+200ms mark c1\n' + mode2 + '+200ms mark c2\n' + mode2 + '+200ms mark c3\n'
                      + mode2 + '+200ms mark c4\n' + mode2 + '+200ms mark c5\n'
                      '+100ms down shift\n+20ms press rec\n+100ms up shift\n+200ms mark rec\n')
    t.render(seed, ['--no-start', '--seconds', '6', '--param', 'rec_quant=1', '--panel-log', log,
                    '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'LEN.stp.rot.mrp>', rows['euklid'][0]), 'the first screen, sign >: %r' % rows['euklid'])
    check(re.match(r'TMP.hmt.hmv.dly\*', rows['track'][0]), 'the second screen, sign *: %r' % rows['track'])
    check(re.match(r'TPL.amt.{8}\*', rows['tpl'][0]) and re.match(r'lyb.150', rows['tpl'][1]),
          'tpl, sign * (a fourth screen follows, v0.7): %r' % rows['tpl'])
    check(rows['tpledit'][1].startswith('lay bar'), 'edit mode on tpl 3: %r' % rows['tpledit'])
    check(re.match(r'tpl.AMT', rows['amt'][0]), 'amt: %r' % rows['amt'])
    check(re.match(r'tmp.hmt.hmv.DLY', rows['back'][0]), 'back to dly: %r' % rows['back'])
    check(re.match(r'COA.fin.wav.pwm>', rows['voice'][0]), 'the voice page from tpl: %r' % rows['voice'])
    for m, want in (('c1', r'LEN.stp'), ('c2', r'TMP.hmt'), ('c3', r'TPL.amt'), ('c4', r'VTP.vdp.sav'),
                    ('c5', r'TYP.sty.den.lev')):
        check(re.match(want, rows[m][0]), 'SHIFT + mode2, %s: %r' % (m, rows[m]))
    check(re.search(r'rqn', rows['rec'][0], re.I) and re.search(r'on', rows['rec'][1]), 'rec: %r' % rows['rec'])
    # the step page's mic (review of Task 6): loaded on step select (seq_loadStepMicro() in
    # FRONT_SEQ_REQUEST_STEP_PARAMS), its ms in edit mode at 120 BPM (+76 ticks: 98.96 ms), and
    # refreshed by shf: +6 to +7 keeps the fine part +4, 88 clamped to +84
    log = t.path('panel2.log')
    script = t.script('0ms press voice1\n+300ms press mode1\n+200ms param active_step=2\n'
                      '+100ms param step_micro=160\n+100ms param active_step=0\n+300ms mark step0\n'
                      '+100ms param active_step=2\n+300ms mark step2\n'
                      '+100ms encoder +2\n+100ms push\n+300ms mark micedit\n+100ms push\n'
                      '+100ms encoder -1\n+100ms push\n+100ms encoder +1\n+100ms push\n+300ms mark shf\n')
    t.render(seed, ['--no-start', '--seconds', '4', '--panel-log', log, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'FLM.shf.mic', rows['step0'][0]) and re.match(r'off 0 +0 ', rows['step0'][1]),
          'step 0: %r' % rows['step0'])
    check(re.match(r'off \+6 +\+76 ', rows['step2'][1]), 'step 2 selected again: %r' % rows['step2'])
    check(re.match(r'Step +Micro', rows['micedit'][0]) and rows['micedit'][1].endswith('99.0ms +76'),
          'edit mode on mic: %r' % rows['micedit'])
    check(re.match(r'off \+7 +\+84 ', rows['shf'][1]), 'shf +1: %r' % rows['shf'])

@case
def V07_ui(t):
    # the track page's fourth screen (plan v0.7 Task 6, V7-11, V7-14, V7-16): the encoder runs from
    # amt over the skips to vtp to sav (sign *, the generator follows in v0.8); vtp and vdp show the voice's values, edit
    # mode on vtp its long name, sav at 2 reads us2; back left to amt. SHIFT + COPY and the encoder
    # show the clear page's micro, SHIFT up leaves it. A project save writes block 3 right after block
    # 1 (3,697 + 20 bytes, LCHM 03 07, voice 2's of8 at 50 %, the others 0) and no USERTPL.LCH
    seed, log, keep = t.seed(), t.path('panel.log'), t.path('K')
    mode2 = '+100ms down shift\n+20ms press mode2\n+100ms up shift\n'
    script = t.script('0ms press voice2\n+100ms param track_vtp=1\n+100ms param track_vdp=50\n'
                      + mode2 + '+200ms encoder +8\n+200ms mark tpl\n+100ms encoder +1\n+100ms encoder +1\n'
                      '+200ms mark vtp\n+100ms push\n+200ms mark vtpedit\n+100ms push\n+150ms encoder +2\n'
                      '+200ms mark sav\n+100ms push\n+150ms encoder +2\n+200ms mark savedit\n+100ms encoder -2\n'
                      '+100ms push\n+200ms mark savoff\n+100ms param track_tpl_save=2\n+100ms encoder -1\n'
                      '+100ms encoder +1\n+200ms mark sav2\n+100ms encoder -3\n'
                      '+200ms mark back\n+100ms down shift\n+20ms press copy\n+150ms encoder +9\n+200ms mark clear\n'
                      '+100ms up shift\n+200ms mark gone\n+300ms save-project 0\n')
    t.render(seed, ['--no-start', '--seconds', '6', '--panel-log', log, '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'TPL.amt.{8}\*', rows['tpl'][0]), 'the third screen, sign *: %r' % rows['tpl'])
    check(re.match(r'VTP.vdp.sav.{4}\*', rows['vtp'][0]) and re.match(r'of8. 50.---', rows['vtp'][1]),
          'the fourth screen, sign * (the generator follows, v0.8): %r' % rows['vtp'])
    check(rows['vtpedit'][1].startswith('offbeat 8th'), 'edit mode on vtp 1: %r' % rows['vtpedit'])
    check(re.match(r'vtp.vdp.SAV', rows['sav'][0]), 'sav, where the encoder stops: %r' % rows['sav'])
    check(rows['savedit'][1] == 'push: save   us2', 'edit mode on sav 2: %r' % rows['savedit'])
    check(re.match(r'vtp.vdp.SAV', rows['savoff'][0]) and re.match(r'of8. 50.---', rows['savoff'][1]),
          'sav back at ---, edit mode left: %r' % rows['savoff'])
    check(re.match(r'of8. 50.us2', rows['sav2'][1]), 'sav at 2 (shown after the cursor moves): %r' % rows['sav2'])
    check(re.match(r'tpl.AMT', rows['back'][0]), 'back to amt: %r' % rows['back'])
    check(rows['clear'][0].startswith('clear [micro]?'), 'the clear page: %r' % rows['clear'])
    check(not rows['gone'][0].startswith('clear'), 'SHIFT up leaves the clear page: %r' % rows['gone'])
    pat = one_file(keep, '00-*.PAT')
    check(len(pat) == 3697 + 20 and pat[3697:3703] == b'LCHM\x03\x07', 'pattern 0: %d bytes, %r'
          % (len(pat), pat[3697:3703]))
    check(pat[3703:3717] == bytes([0, 0, 1, (50 - 100) & 0xFF] + [0] * 10), 'block 3: %r' % pat[3703:3717])
    check(not glob.glob(os.path.join(keep, 'PROJ00', 'USERTPL.LCH')), 'no own template, no USERTPL.LCH')
    # us1..us4 in tpl and vtp (V7-1, V7-2), their long names in edit mode. The capture (Task 6,
    # group B's reviews), from edit mode as the manual says: sav's click writes /tmp's USERTPL.LCH
    # and leaves edit mode, also when every template is empty: the project's USERTPL.LCH holds us1
    # only, and Drum 1 has no step on in the seed's pattern 0 (us4 is empty too), so us1 is emptied;
    # sav reads --- again. With TMP/USERTPL.LCH a directory (--as-booted copies TMP) the write is
    # refused (FR_DENIED) and the LCD shows it (the mark comes at the end of menu_showSdError()'s
    # HAL_Delay())
    sav = ('0ms press voice1\n+100ms param track_tpl=11\n+100ms param track_vtp=8\n+100ms down shift\n'
           '+20ms press mode2\n+100ms up shift\n+200ms encoder +8\n+200ms mark tplus\n+100ms push\n'
           '+200ms mark tplusedit\n+100ms push\n+150ms encoder +2\n+200ms mark vtpus\n+100ms push\n'
           '+200ms mark vtpusedit\n+100ms push\n+150ms encoder +2\n+200ms mark sav\n+100ms push\n'
           '+150ms encoder +1\n+200ms mark savus1\n+100ms push\n+300ms mark saved\n')
    own = card_copy(t, seed, 'own')
    with open(os.path.join(own, 'PROJ00', 'USERTPL.LCH'), 'wb') as f:
        f.write(b'LCHM\x01\x04' + bytes([5] * 16 + [256 - 10] * 16) + bytes(96))
    keep, log = t.path('K2'), t.path('panel2.log')
    t.render(own, ['--no-start', '--seconds', '5', '--panel-log', log, '--keep-card', keep, '--events', t.script(sav)])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'TPL.amt', rows['tplus'][0]) and re.match(r'us4.100', rows['tplus'][1]), 'tpl us4: %r' % rows['tplus'])
    check(rows['tplusedit'][1].startswith('user 4'), 'edit mode on tpl 11: %r' % rows['tplusedit'])
    check(re.match(r'VTP.vdp.sav', rows['vtpus'][0]) and re.match(r'us4.100.---', rows['vtpus'][1]),
          'vtp us4: %r' % rows['vtpus'])
    check(rows['vtpusedit'][1].startswith('user 4'), 'edit mode on vtp 8: %r' % rows['vtpusedit'])
    check(re.match(r'vtp.vdp.SAV', rows['sav'][0]), 'the encoder on sav: %r' % rows['sav'])
    check(rows['savus1'][1] == 'push: save   us1', 'edit mode on sav 1: %r' % rows['savus1'])
    check(re.match(r'vtp.vdp.SAV', rows['saved'][0]) and re.match(r'us4.100.---', rows['saved'][1]),
          'after the click: edit mode left, sav at ---: %r' % rows['saved'])
    tpl = glob.glob(os.path.join(keep, 'TMP', 'USERTPL.LCH'))
    tpl = open(tpl[0], 'rb').read() if tpl else None
    check(tpl == b'LCHM\x01\x04' + bytes(128), "TMP's USERTPL.LCH after an empty capture: %r" % tpl)
    card, log = card_copy(t, own, 'denied'), t.path('panel3.log')
    os.mkdir(os.path.join(card, 'TMP', 'USERTPL.LCH'))
    t.render(card, ['--no-start', '--seconds', '5', '--as-booted', '--panel-log', log, '--events', t.script(sav)])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(rows['saved'][0].startswith('SD Card error:') and rows['saved'][1].startswith('Card full!'),
          'a refused write: %r' % rows['saved'])
    # the clear page's micro on the track the step page shows (V7-14, group B's reviews): shf and mic
    # read 0 at once, not only after the step is selected again
    log = t.path('panel4.log')
    script = t.script('0ms press voice1\n+300ms press mode1\n+200ms param active_step=2\n'
                      '+100ms param step_micro=160\n+100ms param active_step=0\n+100ms param active_step=2\n'
                      '+300ms mark step2\n+100ms down shift\n+20ms press copy\n+150ms encoder +9\n'
                      '+200ms mark clear\n+100ms push\n+100ms up shift\n+300ms mark cleared\n')
    t.render(seed, ['--no-start', '--seconds', '3', '--panel-log', log, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'FLM.shf.mic', rows['step2'][0]) and re.match(r'off \+6 +\+76 ', rows['step2'][1]),
          'step 2 before the clear: %r' % rows['step2'])
    check(rows['clear'][0].startswith('clear [micro]?'), 'the clear page on the step page: %r' % rows['clear'])
    check(re.match(r'FLM.shf.mic', rows['cleared'][0]) and re.match(r'off 0 +0 ', rows['cleared'][1]),
          'step 2 after the clear: %r' % rows['cleared'])
    # the capture and the clear take the active voice in the shown pattern, sav's last slot us4 too
    # (light review of group C): Drum 1's step 6 at T +24 in pattern 0 (kept by save-pattern 0
    # before the pattern page, V6-14), then pattern 3, voice 3's steps 1 and 3 on at T +30 and -20;
    # edit mode on sav reads no "push: save" at --- and "push: save   us4" at us4; the click on us4
    # captures, the clear page's micro clears voice 3 in pattern 3, and the project save writes
    # USERTPL.LCH (us1 the project's, us2 and us3 empty, us4 voice 3's T at 0 dB; TMP is emptied
    # by the save, F9), voice 3's T all 0 in pattern 3 and Drum 1's +24 in pattern 0
    keep, log = t.path('K3'), t.path('panel5.log')
    script = t.script('0ms press voice1\n+100ms param active_step=5\n+100ms param step_micro=108\n'
                      '+100ms save-pattern 0\n+100ms down shift\n+20ms press step16\n+80ms up shift\n'
                      '+100ms press step4\n+200ms press mode1\n+100ms press voice3\n+100ms press step1\n'
                      '+100ms press step3\n+100ms param active_step=0\n+100ms param step_micro=114\n'
                      '+100ms param active_step=2\n+100ms param step_micro=64\n+100ms down shift\n'
                      '+20ms press mode2\n+100ms up shift\n+200ms encoder +12\n+200ms mark sav\n+100ms push\n'
                      '+150ms encoder +2\n+100ms encoder -2\n+200ms mark savdash\n+100ms encoder +13\n'
                      '+200ms mark savus4\n+100ms push\n+300ms mark saved\n+100ms down shift\n+20ms press copy\n'
                      '+150ms encoder +9\n+200ms mark clear\n+100ms push\n+100ms up shift\n+300ms save-project 0\n')
    t.render(own, ['--no-start', '--seconds', '6', '--panel-log', log, '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'vtp.vdp.SAV', rows['sav'][0]), 'voice 3, the encoder on sav: %r' % rows['sav'])
    check(rows['savdash'][1].endswith('---') and not rows['savdash'][1].startswith('push: save'),
          'edit mode on sav at ---: %r' % rows['savdash'])
    check(rows['savus4'][1] == 'push: save   us4', 'edit mode on sav 4: %r' % rows['savus4'])
    check(re.match(r'vtp.vdp.SAV', rows['saved'][0]) and re.match(r'off.100.---', rows['saved'][1]),
          'after the click on us4: %r' % rows['saved'])
    check(rows['clear'][0].startswith('clear [micro]?'), 'the clear page, voice 3: %r' % rows['clear'])
    tpl = one_file(keep, 'USERTPL.LCH')
    us4 = [30, 0, -20] + [0] * 13
    want = b'LCHM\x01\x04' + bytes([5] * 16 + [256 - 10] * 16) + bytes(64) + bytes(x & 0xFF for x in us4) + bytes(16)
    check(tpl == want, 'USERTPL.LCH after the capture of voice 3 into us4: %r, not %r' % (tpl, want))
    v3 = pat_tracks(one_file(keep, '03-*.PAT'))[2]
    check(v3['on'] == {0, 2} and all(T == 0 for T, _ in v3['steps']), 'voice 3 in pattern 3: on %r, T %r'
          % (sorted(v3['on']), [T for T, _ in v3['steps'][:16]]))
    d1 = pat_tracks(one_file(keep, '00-*.PAT'))[0]
    check([T for T, _ in d1['steps'][:16]] == [0] * 5 + [24] + [0] * 10, "Drum 1's T in pattern 0 kept: %r"
          % [T for T, _ in d1['steps'][:16]])
    no_copies_left()

@case
def V07_screensaver(t):
    # sav's click on a dark screen (Task 6, light review of group C): with the screensaver on and the
    # cursor on sav at us1, two idle minutes start it (the LCD no longer shows the page); a click then
    # only wakes the screen, as it leaves edit mode alone: the page reads sav us1 again, nothing is
    # captured and /tmp gets no USERTPL.LCH. A 125 s render, its own case so that V07_ui stays fast.
    # v0.8 (quality review of group C): sav acts from edit mode only, so it is armed there
    seed, log, keep = t.seed(), t.path('panel.log'), t.path('K')
    script = t.script('0ms press voice1\n+100ms down shift\n+20ms press mode2\n+100ms up shift\n'
                      '+200ms encoder +12\n+200ms mark sav\n+100ms push\n+150ms encoder +1\n'
                      '+250ms mark sav1\n+122s mark ss\n+100ms push\n+1000ms mark woke\n')
    t.render(seed, ['--no-start', '--seconds', '125', '--param', 'screensaver_on_off=1', '--panel-log', log,
                    '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(rows['sav1'][1] == 'push: save   us1', 'sav at us1 in edit mode: %r' % rows['sav1'])
    check(rows['ss'] != rows['sav1'], 'the screensaver after two idle minutes: %r' % rows['ss'])
    check(rows['woke'] == rows['sav1'], 'after the click on the dark screen: %r' % rows['woke'])
    check(not glob.glob(os.path.join(keep, 'TMP', 'USERTPL.LCH')), 'no capture on the dark screen: TMP holds %r'
          % sorted(os.listdir(os.path.join(keep, 'TMP'))))
    no_copies_left()

# LICHOMAT v0.8: the step LEDs, LED_STEP1..LED_STEP16 of ledHandler.h, and the bar LEDs (page down, up)
STEP_LEDS = (39, 38, 37, 36, 31, 30, 29, 28, 23, 22, 21, 20, 15, 14, 13, 12)
BAR_LEDS = (40, 0)

def panel_leds(line):
    """the LED word of a panel-log line"""
    return int(line.split()[-1], 16)

@case
def V08_ui(t):
    # the generator's screens (plan v0.8 Task 5, G2, G3, G11, G13). A: from sav the encoder goes on to
    # typ (sav's sign is * now); typ in edit mode shows the long name and loads hat's values; the second
    # and third screens; voice 7's track becomes 64 steps of offbeats at 127 and ghosts, and the hats'
    # vol (kit 0, off on the init card), which voice 7 shares with voice 6, is on after the project
    # save, the parameter after it untouched. B: SHIFT + mode2 steps through the seven screens; und's
    # edit mode reads "push: undo" and its click puts back voice 6 as loaded; the vol the generator
    # switched on stays on
    seed, log, keep = t.seed(), t.path('panel.log'), t.path('K')
    mode2 = '+100ms down shift\n+20ms press mode2\n+100ms up shift\n'
    script = t.script('0ms press voice7\n' + mode2 + '+200ms encoder +12\n+200ms mark sav\n'
                      '+100ms encoder +1\n+200ms mark typ\n+100ms push\n+150ms encoder +1\n+200ms mark hatedit\n'
                      '+100ms push\n+200ms mark hat\n+150ms encoder +4\n+200ms mark scr2\n+100ms encoder +4\n'
                      '+200ms mark src\n+100ms encoder +5\n+200ms mark end\n+300ms save-project 0\n')
    s, _, _ = t.render(seed, ['--no-start', '--seconds', '5', '--param', 'volume_mod_on_off6=0', '--panel-log', log,
                              '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    leds = {x.split()[2]: panel_leds(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'vtp.vdp.SAV.{4}\*', rows['sav'][0]), 'sav, sign * (the generator follows): %r' % rows['sav'])
    check(re.match(r'TYP.sty.den.lev\*', rows['typ'][0]) and re.match(r'---.  0.  0.  0', rows['typ'][1]),
          'typ: %r' % rows['typ'])
    check(rows['hatedit'][1] == 'hat ghosts   hat', 'edit mode on typ hat: %r' % rows['hatedit'])
    check(re.match(r'hat.  1.  7.-15', rows['hat'][1]), "hat's values: %r" % rows['hat'])
    check(re.match(r'VAR.not.sed.und\*', rows['scr2'][0]) and re.match(r' \+1.  0.  0.---', rows['scr2'][1]),
          'the second screen, var with its sign: %r' % rows['scr2'])
    check(re.match(r'SRC.shf.{8}<', rows['src'][0]) and re.match(r'  1.  0', rows['src'][1]), 'src: %r' % rows['src'])
    check(re.match(r'src.SHF', rows['end'][0]), 'the encoder stops at shf: %r' % rows['end'])
    pat = one_file(keep, '00-*.PAT')
    main = int.from_bytes(pat[3598 + 8 * 6:3606 + 8 * 6], 'little')
    bars = [(main >> (16 * b)) & 0xFFFF for b in range(4)]
    check(pat[3655 + 6] == 64 and len(set(bars)) == 1 and bars[0] & 0x4444 == 0x4444 and bin(bars[0]).count('1') == 12,
          'voice 7: length %d, bars %r' % (pat[3655 + 6], [hex(b) for b in bars]))
    shown = sum(((leds['hat'] >> led) & 1) << k for k, led in enumerate(STEP_LEDS))
    check(shown == bars[0], 'the step LEDs after the generation: %s, bar 1 %s (quality review of group C)'
          % (hex(shown), hex(bars[0])))
    # bar 1: the offbeats at 127, eight soft hits (den 7 at 100 %, var +1 at 2:2) at -18..-13 dB
    steps = {p: tuple(pat[14 + 512 * 6 + 7 * p:14 + 512 * 6 + 7 * p + 2]) for p in range(16) if bars[0] >> p & 1}
    soft = [steps[p] for p in steps if p % 4 != 2]
    check(all(steps[p][0] == 127 for p in (2, 6, 10, 14)) and len(soft) == 8
          and all(16 <= v <= 28 for v, _ in soft) and sorted(q for _, q in soft) == [100] * 7 + [102],
          'voice 7 bar 1 (volume, prob): %r' % steps)
    n = s['paramNumbers']['volume_mod_on_off6']
    k0, s0 = one_file(keep, '00-*.SND'), one_file(seed, '00-*.SND')
    check(s0[8 + n] == 0 and k0[8 + n] == 1 and k0[9 + n] == s0[9 + n],
          "the hats' vol: the seed's %d, after the generator %d (the next parameter %d, the seed's %d)"
          % (s0[8 + n], k0[8 + n], k0[9 + n], s0[9 + n]))
    log, keep = t.path('panel2.log'), t.path('K2')
    script = t.script('0ms press voice6\n' + ''.join(mode2 + '+200ms mark c%d\n' % k for k in range(1, 9))
                      + '+200ms encoder +13\n+200ms mark typ\n+100ms push\n+150ms encoder +1\n+100ms push\n'
                      '+150ms encoder +7\n+200ms mark und\n+100ms push\n+150ms encoder +1\n+200ms mark undedit\n'
                      '+100ms push\n+300ms mark undone\n+300ms save-project 0\n')
    t.render(seed, ['--no-start', '--seconds', '7', '--panel-log', log, '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    for m, want in (('c1', r'LEN.stp'), ('c2', r'TMP.hmt'), ('c3', r'TPL.amt'), ('c4', r'VTP.vdp.sav'),
                    ('c5', r'TYP.sty.den.lev'), ('c6', r'VAR.not.sed.und'), ('c7', r'SRC.shf'), ('c8', r'LEN.stp')):
        check(re.match(want, rows[m][0]), 'SHIFT + mode2, %s: %r' % (m, rows[m]))
    check(re.match(r'var.not.sed.UND', rows['und'][0]), 'the encoder on und: %r' % rows['und'])
    check(rows['undedit'][1] == 'push: undo   und', 'edit mode on und: %r' % rows['undedit'])
    check(re.match(r'var.not.sed.UND', rows['undone'][0]) and re.match(r' \+1.  0.  0.--- ', rows['undone'][1]),
          'after the click, und at --- and edit mode left: %r' % rows['undone'])
    a, b = one_file(keep, '00-*.PAT'), one_file(seed, '00-*.PAT')
    check(a[14 + 512 * 5:14 + 512 * 6] == b[14 + 512 * 5:14 + 512 * 6] and a[3638:3646] == b[3638:3646]
          and a[3655 + 5] == b[3655 + 5], "voice 6 after und is the seed's: length %d, not %d" % (a[3660], b[3660]))
    k2 = one_file(keep, '00-*.SND')
    check(k2[8 + n] == 1, "the hats' vol after und: %d, the generator's 1 stays" % k2[8 + n])
    # the encoder past the end of typ's list (review of group C): prp with den 7, typ +1 stays prp and
    # keeps den 7 (the same typ loads nothing)
    log = t.path('panel3.log')
    script = t.script('0ms press voice6\n' + mode2 + '+200ms encoder +13\n+100ms push\n+150ms encoder +2\n'
                      '+100ms push\n+200ms mark sub\n+150ms encoder +5\n+200ms mark subnot\n+100ms encoder -5\n+100ms push\n'
                      '+150ms encoder +3\n'
                      '+100ms push\n+200ms mark prp\n+150ms encoder +2\n+100ms push\n+150ms encoder +3\n'
                      '+100ms push\n+200ms mark den7\n+150ms encoder -2\n+100ms push\n+150ms encoder +1\n'
                      '+100ms push\n+200ms mark again\n+150ms encoder +9\n+100ms push\n+150ms encoder -3\n'
                      '+100ms push\n+200ms mark shfneg\n+100ms push\n+150ms encoder +20\n+100ms push\n'
                      '+200ms mark shfpos\n')
    t.render(seed, ['--no-start', '--seconds', '6', '--panel-log', log, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'prp.  1.  4.-15', rows['prp'][1]), "prp's values: %r" % rows['prp'])
    check(re.match(r'typ.sty.DEN.lev', rows['den7'][0]) and re.match(r'prp.  1.  7.-15', rows['den7'][1]),
          'den 7: %r' % rows['den7'])
    check(re.match(r'TYP.sty.den.lev', rows['again'][0]) and re.match(r'prp.  1.  7.-15', rows['again'][1]),
          'typ +1 at prp: %r' % rows['again'])
    # var, not and shf with their signs, as mic: sub's not -2, shf -3 and +8; lev too: sub's -3
    check(re.match(r'sub.  1.  0. -3', rows['sub'][1]), "sub's lev: %r" % rows['sub'])
    check(re.match(r'var.NOT.sed.und', rows['subnot'][0]) and re.match(r'  0. -2.  0.---', rows['subnot'][1]),
          "sub's not: %r" % rows['subnot'])
    check(re.match(r'src.SHF', rows['shfneg'][0]) and re.match(r'  1. -3', rows['shfneg'][1]), 'shf -3: %r' % rows['shfneg'])
    check(re.match(r'  1. \+8', rows['shfpos'][1]), 'shf +8: %r' % rows['shfpos'])
    # the same value again is no change (review of group C): hat at den 16 fills bar 1; step 2 off by hand
    # on the step page; back on the track page (at len) the encoder +1 on den at its end leaves it off
    log, keep = t.path('panel4.log'), t.path('K4')
    script = t.script('0ms press voice6\n' + mode2 + '+200ms encoder +13\n+100ms push\n+150ms encoder +1\n'
                      '+100ms push\n+150ms encoder +2\n+100ms push\n+150ms encoder +9\n+100ms push\n'
                      '+100ms press mode1\n+200ms press step2\n' + mode2 + '+200ms encoder +15\n+100ms push\n'
                      '+150ms encoder +1\n+100ms push\n+200ms mark again\n+300ms save-project 0\n')
    t.render(seed, ['--no-start', '--seconds', '5', '--panel-log', log, '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'typ.sty.DEN.lev', rows['again'][0]) and re.match(r'hat.  1. 16.-15', rows['again'][1]),
          'den at 16 after +1: %r' % rows['again'])
    pat = one_file(keep, '00-*.PAT')
    main = int.from_bytes(pat[3598 + 8 * 5:3606 + 8 * 5], 'little')
    bars = [(main >> (16 * b)) & 0xFFFF for b in range(4)]
    check(bars == [0xFFFD, 0xFFFF, 0xFFFF, 0xFFFF], 'voice 6 with step 2 off by hand, after den +1 at 16: %r'
          % [hex(b) for b in bars])
    no_copies_left()

@case
def V08_armed(t):
    # und and sav armed while the sequencer plays and the steps follow the play position (review of
    # group C): each bar change reloads the track page (led_updateSeqLeds()); an armed und or sav
    # stays armed over more than a bar, so the click puts back voice 6 as loaded, or captures it
    seed, log, keep = t.seed(), t.path('panel.log'), t.path('K')
    head = '0ms press voice6\n+100ms param follow=1\n+100ms down shift\n+20ms press mode2\n+100ms up shift\n' \
           '+200ms encoder +13\n+100ms push\n+150ms encoder +1\n+100ms push\n'
    script = t.script(head + '+150ms encoder +7\n+100ms push\n+150ms encoder +1\n+200ms mark undedit\n'
                      '+2500ms mark wait\n+100ms push\n+300ms mark undone\n+300ms save-project 0\n')
    t.render(seed, ['--seconds', '7', '--panel-log', log, '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    leds = {x.split()[2]: panel_leds(x) for x in log_lines(log, PANEL_LINE)}
    bar = lambda m: tuple((leds[m] >> led) & 1 for led in BAR_LEDS)
    check(bar('undedit') != bar('wait'), 'und: no bar change between arming and the push: %r' % (bar('wait'),))
    check(rows['undedit'][1] == 'push: undo   und', 'edit mode on und: %r' % rows['undedit'])
    check(re.match(r'var.not.sed.UND', rows['undone'][0]) and rows['undone'][1].rstrip().endswith('---'),
          'after the click: %r' % rows['undone'])
    a, b = one_file(keep, '00-*.PAT'), one_file(seed, '00-*.PAT')
    check(a[14 + 512 * 5:14 + 512 * 6] == b[14 + 512 * 5:14 + 512 * 6] and a[3638:3646] == b[3638:3646]
          and a[3655 + 5] == b[3655 + 5], "voice 6 after und while playing is the seed's: length %d, not %d"
          % (a[3660], b[3660]))
    log, keep = t.path('panel2.log'), t.path('K2')
    script = t.script(head + '+150ms encoder -1\n+100ms push\n+150ms encoder +1\n+200ms mark savedit\n'
                      '+2500ms mark wait\n+100ms push\n+300ms mark saved\n')
    t.render(seed, ['--seconds', '6', '--panel-log', log, '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    leds = {x.split()[2]: panel_leds(x) for x in log_lines(log, PANEL_LINE)}
    check(bar('savedit') != bar('wait'), 'sav: no bar change between arming and the push: %r' % (bar('wait'),))
    check(rows['savedit'][1] == 'push: save   us1', 'edit mode on sav 1: %r' % rows['savedit'])
    check(re.match(r'vtp.vdp.SAV', rows['saved'][0]) and rows['saved'][1].rstrip().endswith('---'),
          'after the click on sav: %r' % rows['saved'])
    check(glob.glob(os.path.join(keep, 'TMP', 'USERTPL.LCH')), 'sav captured while playing: TMP holds %r'
          % sorted(os.listdir(os.path.join(keep, 'TMP'))))
    # quality review of group C: armed, then off the screen by SHIFT + mode2 (edit mode ends) and back
    # with the encoder: one push enters edit mode, as in v0.7; it neither puts voice 6 back nor captures
    mode2 = '+100ms down shift\n+20ms press mode2\n+100ms up shift\n'
    for name, arm in (('und', '+150ms encoder +7\n+100ms push\n+150ms encoder +1\n+200ms mark armed\n' + mode2
                       + '+200ms mark away\n+150ms encoder -1\n'),
                      ('sav', '+150ms encoder -1\n+100ms push\n+150ms encoder +1\n+200ms mark armed\n' + mode2
                       + '+200ms mark away\n+150ms encoder -1\n')):
        log, keep = t.path('panel-%s.log' % name), t.path('K-' + name)
        script = t.script(head.replace('+100ms param follow=1\n', '') + arm + '+200ms mark back\n+100ms push\n'
                          '+300ms mark pushed\n+300ms save-project 0\n')
        t.render(seed, ['--no-start', '--seconds', '5', '--panel-log', log, '--keep-card', keep, '--events', script])
        rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
        want = ('push: undo   und', r'var.not.sed.UND', 'Pattern Undo') if name == 'und' else \
               ('push: save   us1', r'vtp.vdp.SAV', 'Pattern Save tpl')
        check(rows['armed'][1] == want[0], '%s armed: %r' % (name, rows['armed']))
        check(re.match(want[1], rows['back'][0]), '%s, the cursor back on it: %r' % (name, rows['back']))
        check(rows['pushed'][0].startswith(want[2]), '%s, one push enters edit mode: %r' % (name, rows['pushed']))
        pat = one_file(keep, '00-*.PAT')
        check(pat[3655 + 5] == 64, '%s: voice 6 still the generated track, length %d' % (name, pat[3655 + 5]))
        check(not glob.glob(os.path.join(keep, 'TMP', 'USERTPL.LCH')), '%s: no capture, TMP holds %r'
              % (name, sorted(os.listdir(os.path.join(keep, 'TMP')))))
    no_copies_left()

@case
def V08_vol(t):
    # the generator's vol switch (plan v0.8 G11, quality review of group C). A: recording on and the
    # sequencer playing: the switch goes to the engine directly, so it records no automation into the
    # step that plays: voice 6's steps keep the generator's empty p-locks
    seed, keep = t.seed(), t.path('K')
    mode2 = '+100ms down shift\n+20ms press mode2\n+100ms up shift\n'
    script = t.script('0ms press voice6\n+100ms press rec\n' + mode2 + '+200ms encoder +13\n+100ms push\n'
                      '+150ms encoder +1\n+100ms push\n+300ms press start\n+200ms press rec\n+300ms save-project 0\n')
    t.render(seed, ['--seconds', '3', '--param', 'volume_mod_on_off6=0', '--keep-card', keep, '--events', script])
    pat = one_file(keep, '00-*.PAT')
    locks = [(k, pat[14 + 512 * 5 + 7 * k + 3], pat[14 + 512 * 5 + 7 * k + 5]) for k in range(64)]
    check(pat[3655 + 5] == 64 and all(a == 0xFF and b == 0xFF for _, a, b in locks),
          'voice 6 recorded while the generator switched vol: %r' % [x for x in locks if x[1:] != (0xFF, 0xFF)])
    # B: the switch reaches the engine: hat (gen_typ=1 by the edit path) plays its ghosts quiet; the
    # accents at the top, the ghosts (-18..-13 dB) below 40 % of it, as with vol on
    log = t.path('trig.log')
    script = t.script('0ms press voice6\n+100ms param gen_typ=1\n+300ms press start\n')
    _, wav, _ = t.render(seed, ['--no-start', '--seconds', '3', '--param', 'volume_mod_on_off6=0', '--trig-log', log,
                                '--events', script])
    peaks = hit_peaks(wav, 'L1', trig_frames(log, 5))
    top = max(peaks or [0])
    check(len(peaks) >= 12 and top > 1000 and sum(p >= 0.9 * top for p in peaks) >= 3
          and sum(p < 0.4 * top for p in peaks) >= 6, "voice 6's hits on L1 with the generator's vol: %r" % peaks)
    # C: rar (all at 127) leaves vol off: the kit of the project save after it; then hat switches it on
    # and touches the kit's autosave timer: /tmp's TMP.SND (preset byte, then the parameters) has it
    keep = t.path('K3')
    script = t.script('0ms press voice6\n+100ms param gen_typ=4\n+300ms save-project 0\n+500ms param gen_typ=1\n'
                      '+2600ms mark end\n')
    s, _, _ = t.render(seed, ['--no-start', '--seconds', '4', '--param', 'volume_mod_on_off6=0', '--keep-card', keep,
                              '--events', script])
    n = s['paramNumbers']['volume_mod_on_off6']
    kit = one_file(keep, '00-*.SND')
    check(kit[8 + n] == 0, 'vol after rar, all at 127: %d' % kit[8 + n])
    tmp = glob.glob(os.path.join(keep, 'TMP', 'TMP.SND'))
    tmp = open(tmp[0], 'rb').read() if tmp else b''
    check(len(tmp) > 1 + n and tmp[1 + n] == 1, 'the autosaved kit after hat: %r' % (tmp[1 + n] if len(tmp) > 1 + n else None))
    no_copies_left()

@case
def V08_menu(t):
    # the generator's menu paths (quality review of group C). A: und set while the cursor is on den
    # in edit mode: the push only leaves edit mode, voice 6 stays generated. B: src from the encoder
    # reaches the generator: prp copies voice 1 (steps 1 5 9 13), then src 2 copies voice 2 instead
    seed, log, keep = t.seed(), t.path('panel.log'), t.path('K')
    mode2 = '+100ms down shift\n+20ms press mode2\n+100ms up shift\n'
    script = t.script('0ms press voice6\n' + mode2 + '+200ms encoder +13\n+100ms push\n+150ms encoder +1\n'
                      '+100ms push\n+150ms encoder +2\n+100ms push\n+100ms param gen_und=1\n+100ms push\n'
                      '+200ms mark left\n+300ms save-project 0\n')
    t.render(seed, ['--no-start', '--seconds', '3', '--panel-log', log, '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'typ.sty.DEN.lev', rows['left'][0]), 'the push on den: %r' % rows['left'])
    pat = one_file(keep, '00-*.PAT')
    check(pat[3655 + 5] == 64, 'voice 6 after a push off und: length %d, still generated' % pat[3655 + 5])
    log, keep = t.path('panel2.log'), t.path('K2')
    script = t.script('0ms press voice1\n+100ms press step1\n+100ms press step5\n+100ms press step9\n'
                      '+100ms press step13\n+100ms press voice6\n' + mode2 + '+200ms encoder +13\n+100ms push\n'
                      '+150ms encoder +5\n+100ms push\n+150ms encoder +8\n+100ms push\n+150ms encoder +1\n'
                      '+100ms push\n+200ms mark src\n+300ms save-project 0\n')
    t.render(seed, ['--no-start', '--seconds', '4', '--panel-log', log, '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'SRC.shf', rows['src'][0]) and re.match(r'  2', rows['src'][1]), 'src 2: %r' % rows['src'])
    pat = one_file(keep, '00-*.PAT')
    bars = lambda tr: [(int.from_bytes(pat[3598 + 8 * tr:3606 + 8 * tr], 'little') >> (16 * b)) & 0xFFFF for b in range(4)]
    check(bars(0)[0] == 0x1111 and bars(5) == [bars(1)[0]] * 4 and bars(5) != [0x1111] * 4,
          'voice 6 with src 2: %r (voice 1 %r, voice 2 %r)' % (bars(5), bars(0), bars(1)))
    no_copies_left()

@case
def V08_screensaver(t):
    # und's click on a dark screen (quality review of group C): und armed in edit mode, two idle
    # minutes start the screensaver; a push then only wakes the screen (edit mode stays, und still
    # armed), and voice 6 is still the generated track. A 125 s render, as V07_screensaver
    seed, log, keep = t.seed(), t.path('panel.log'), t.path('K')
    script = t.script('0ms press voice6\n+100ms down shift\n+20ms press mode2\n+100ms up shift\n'
                      '+200ms encoder +13\n+100ms push\n+150ms encoder +1\n+100ms push\n+150ms encoder +7\n'
                      '+100ms push\n+150ms encoder +1\n+200ms mark armed\n+122s mark ss\n+100ms push\n'
                      '+1000ms mark woke\n+300ms save-project 0\n')
    t.render(seed, ['--no-start', '--seconds', '126', '--param', 'screensaver_on_off=1', '--panel-log', log,
                    '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(rows['armed'][1] == 'push: undo   und', 'und armed: %r' % rows['armed'])
    check(rows['ss'] != rows['armed'], 'the screensaver after two idle minutes: %r' % rows['ss'])
    check(rows['woke'] == rows['armed'], 'after the push on the dark screen: %r' % rows['woke'])
    pat = one_file(keep, '00-*.PAT')
    check(pat[3655 + 5] == 64, 'voice 6 after the push on the dark screen: length %d' % pat[3655 + 5])
    no_copies_left()

def name_table(text, table):
    """the strings of a name table of MenuText.h, without its #if USE_CODEC_EQ entries (off in the build)"""
    body = text.split('const char %s[][16] =' % table, 1)[1].split('};', 1)[0]
    body = re.sub(r'#if USE_CODEC_EQ.*?#endif', '', body, flags=re.S)
    return re.findall(r'\{"([^"]*)"\s*\}', body)

@case
def E410_long_names(t):
    # fw 1.7 fix E4.10: edit mode writes the category at column 0 and the long name at column 8
    # of row 0, which leaves 8 columns. A longer name ran on into row 1 (UBSan stopped the render
    # at the push on slt). Every long name fits the 8 columns; slt (v0.3) and trg (v0.5) in edit mode
    with open(os.path.join(t.root, 'Core', 'Inc', 'menu', 'MenuText.h')) as f:
        text = f.read()
    for table in ('catNames', 'longNames'):
        names = name_table(text, table)
        check(len(names) > 20, '%s: %d names read' % (table, len(names)))
        long = [n for n in names if len(n) > 8]
        check(not long, '%s longer than 8 characters: %r' % (table, long))
        check(table == 'catNames' or len(set(names)) == len(names), 'long names twice: %r' % names)
    # from the boot: voice 1, mode 1 twice (the step page, then the voice page's second screen);
    # SHIFT + step 12 three times (the global page's second sub-page) and the encoder to trg
    seed, rows = t.seed(), {}
    glo = '+100ms down shift\n+20ms press step12\n+100ms up shift\n+300ms mark g\n'
    for name, text in (('slt', '0ms press voice1\n+300ms press mode1\n+200ms press mode1\n+300ms mark slt\n'),
                       ('trg', glo * 3 + '+100ms encoder +3\n+300ms mark trg\n')):
        log = t.path(name + '.log')
        t.render(seed, ['--no-start', '--seconds', '3', '--panel-log', log, '--events',
                        t.script(text + '+100ms push\n+300ms mark %sedit\n' % name)])
        rows.update({x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)})
    check(rows['slt'][0].startswith('SLT'), 'the voice page with slt: %r' % rows['slt'])
    check(rows['sltedit'][0] == 'OscilltrSlide tm' and rows['sltedit'][1][:13] == ' ' * 13,
          'edit mode on slt: %r' % rows['sltedit'])
    check(re.match(r'kcm cki cko TRG', rows['trg'][0]), 'the global page with trg: %r' % rows['trg'])
    check(rows['trgedit'][0] == 'SequencrTrig tmg' and rows['trgedit'][1] == ' ' * 13 + 'smp',
          'edit mode on trg: %r' % rows['trgedit'])

def pp_levels(t, pan):
    """one click of voice 1 at pan through the fx card's delay, type Pp on St1, synced to 1/8 at
    120 BPM (D = 250 ms), out 1 alone: [(L, R, L and R together)] in dBFS of the dry hit (k = 0)
    and of echoes 1-3, each over 50 ms from 5 ms before onset + k x D"""
    args = ['--stereo', 'out1', '--bpm', '120', '--no-start', '--seconds', '1.2']
    for p in ['fx_type=4', 'fx_delay_type=1', 'fx_delay_sync=5', 'fx_p3=127', 'pan1=%d' % pan, 'velod1=3',
              'modamnt1=0'] + ['vol%d=0' % v for v in range(2, 7)]:
        args += ['--param', p]
    _, wav, _ = t.render(os.path.join(t.root, 'emu', 'tests', 'cards', 'fx'),
                         args + ['--events', t.script('0ms press start\n+60ms press start\n')])
    ch, a = samples(wav)
    check(ch == 2, '--stereo out1: %d channels' % ch)
    on = next(f for f in range(len(a) // 2) if abs(a[2 * f]) > 100 or abs(a[2 * f + 1]) > 100) / 44140.625
    out = []
    for k in range(4):
        w = (on + k * 0.25 - 0.005, on + k * 0.25 + 0.045)
        out.append((dbfs(wav, (0,), *w), dbfs(wav, (1,), *w), dbfs(wav, (0, 1), *w)))
    print('pan %d: %s' % (pan, '  '.join('%d: L %.2f R %.2f' % (k, x[0], x[1]) for k, x in enumerate(out))))
    return out

@case
def E413_menu_names(t):
    # fw 1.7 fix E4.13 (sweep B12): NRPN sets a menu parameter to any 0..127, and the page named it by
    # table[value + 1] without a bound: NRPN 119 (the roll rate) = 14 on the performance page read
    # rollRateNames[15] of 12 (UBSan stopped the render at menu.c:2822). A value past its table shows the
    # table's first name now; the values inside it keep their names (the roll's 1/8 = 6 and 32 = 10)
    seed, rows = t.seed(), {}
    for name, value in (('past', 14), ('top', 127), ('eighth', 6), ('last', 10)):
        log = t.path(name + '.log')
        t.render(seed, ['--no-start', '--seconds', '1.5', '--panel-log', log, '--events',
                        t.script('0ms press mode2\n+300ms din B0 63 00 B0 62 77 B0 06 %02X\n+300ms mark %s\n'
                                 % (value, name))])
        rows.update({x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)})
    for name, label in (('past', 'One'), ('top', 'One'), ('eighth', '1/8'), ('last', '32 ')):
        check(rows[name][0].lower().startswith('bpm rol'), 'the performance page: %r' % rows[name])
        check(rows[name][1][4:7] == label, '%s: rol reads %r, not %r' % (name, rows[name][1][4:7], label))

@case
def E78_pingpong(t):
    # fw 1.7 fix E7.8: fw 1.7 wrote the send's L into one line and its R into the other, and the
    # lines feed each other, so a centred send echoed equally on both sides. The left line now
    # takes (L + R) / sqrt 2 and the right line only the feedback: the echoes alternate R, L, R
    # at any pan, a centred send at fw 1.7's echo energy, a hard-left one 3 dB below it.
    # fw 1.7, centred: echo 1 total 4.99 dB above the dry hit's, the same as hard left.
    for pan in (64, 0):
        x = pp_levels(t, pan)
        if pan == 0:
            check(x[0][0] > -60 and x[0][0] - x[0][1] >= 20, 'pan 0: the dry hit is not on L: %r' % (x[0],))
        for k, side in ((1, 1), (2, 0), (3, 1)):
            on, off = x[k][side], x[k][1 - side]
            check(on > -60 and on - off >= 20, 'pan %d: echo %d is not on %s alone: L %.2f, R %.2f dBFS'
                  % (pan, k, 'LR'[side], x[k][0], x[k][1]))
        want = 4.99 if pan == 64 else 4.99 - 3.01
        got = x[1][2] - x[0][2]
        check(abs(got - want) <= 0.5, 'pan %d: echo 1 is %.2f dB over the dry hit, not %.2f +- 0.5'
              % (pan, got, want))

def onsets(t, *args):
    """emu_onsets.py's exit code, spread and output"""
    p = subprocess.run([sys.executable, os.path.join(t.root, 'tools', 'emu_onsets.py')] + [str(a) for a in args],
                       capture_output=True, text=True)
    m = re.search(r'spread ([0-9.]+) frames', p.stdout)
    return p.returncode, (float(m.group(1)) if m else None), p.stdout + p.stderr

def click_wav(path, frames, total):
    """a 4-channel WAV with a decaying click on L1 at each frame of frames"""
    x = array.array('h', [0] * (total * 4))
    for f in frames:
        for i in range(200):
            x[(f + i) * 4] = int(20000 * math.exp(-i / 20.0) * math.cos(i * 0.3))
    with wave.open(path, 'wb') as w:
        w.setnchannels(4)
        w.setsampwidth(2)
        w.setframerate(44141)
        w.writeframes(x.tobytes())

@case
def V05_onsets_tool(t):
    # tools/emu_onsets.py on made-up renders (plan v0.5 Task 9): the trigger log (two records in
    # one block start at the later; another voice and a one-shot are left out), the tick grid,
    # and another render; a hit 7k mod 64 frames off shows a spread of 63
    fpt = 353125 / 8 * 60 / (120 * 384)
    ticks = [0, 29, 61, 100, 133, 170, 205, 260, 299, 340]
    cyc = [int(k * fpt * 552960 / 113) + 1000000 for k in ticks]
    exp = [c * 113 / 552960 + 128 for c in cyc]
    log, wav, off = t.path('t.log'), t.path('a.wav'), t.path('b.wav')
    click_wav(wav, [round(e) for e in exp], 40000)
    click_wav(off, [round(e) + (i * 7) % 64 for i, e in enumerate(exp)], 40000)
    with open(log, 'w') as f:
        for i, c in enumerate(cyc):
            f.write('%d 0 seq %d 0 0\n%d 0 seq %d 32 0\n%d 3 seq %d 10 0\n' % (c - 5000, 100 + i, c, 100 + i, c + 7, 100 + i))
        f.write('-99999 0 oneshot 999 0 0\n')
    for args, ok in (([wav, '--log', log, '--limit', 1], True), ([wav, '--grid', 120, '--limit', 1], True),
                     ([off, '--against', wav, '--min', 32], True), ([off, '--log', log, '--limit', 2], False)):
        code, spread, out = onsets(t, *args)
        check((code == 0) == ok, '%r: exit %d\n%s' % (args, code, out))
    check(onsets(t, off, '--against', wav)[1] == 63.0, 'the spread against the shifted render')

@case
def V06_onsets_ticks(t):
    # tools/emu_onsets.py --ticks on made-up renders (plan v0.6 Task 8, V6-20): the clicks at
    # their ticks at 142 BPM pass, one click 10 frames late fails, a ticks file one line short and
    # --ticks without --bpm are usage errors
    ticks = [0, 37, 96, 180, 271, 366, 391, 486]
    fpt = 353125 / 8 * 60 / (142 * 384)
    frames = [1000 + round(k * fpt) for k in ticks]
    wav, late, tf, short = t.path('a.wav'), t.path('b.wav'), t.path('t.txt'), t.path('s.txt')
    click_wav(wav, frames, 30000)
    click_wav(late, frames[:5] + [frames[5] + 10] + frames[6:], 30000)
    with open(tf, 'w') as f:
        f.write(''.join('%d\n' % k for k in ticks))
    with open(short, 'w') as f:
        f.write(''.join('%d\n' % k for k in ticks[:-1]))
    for args, want in (([wav, '--ticks', tf, '--bpm', 142, '--limit', 1], 0),
                       ([late, '--ticks', tf, '--bpm', 142, '--limit', 2], 1),
                       ([wav, '--ticks', short, '--bpm', 142], 2), ([wav, '--ticks', tf], 2)):
        code, spread, out = onsets(t, *args)
        check(code == want, '%r: exit %d, not %d\n%s' % (args, code, want, out))
    code, spread, out = onsets(t, late, '--ticks', tf, '--bpm', 142)
    check(spread is not None and 9.5 <= spread <= 10.5, 'the late click: %s' % out)
    # the base of the ticks does not matter
    with open(tf, 'w') as f:
        f.write(''.join('%d\n' % (k + 1000) for k in ticks))
    code, spread, out = onsets(t, wav, '--ticks', tf, '--bpm', 142, '--limit', 1)
    check(code == 0, 'the ticks from 1000: %s' % out)

def golden(t, gid):
    """the card, the #! options and the path of a golden, to render it with more options"""
    path = os.path.join(t.root, 'emu', 'tests', 'goldens', gid + '.txt')
    opts = []
    with open(path) as f:
        for line in f:
            if line.startswith('#! '):
                opts += shlex.split(line[3:])
    i = opts.index('--card')
    card = os.path.join(t.root, 'emu', 'tests', 'cards', opts[i + 1])
    return card, opts[:i] + opts[i + 2:], path

@case
def V05_onsets(t):
    # G16 (spec 9.2, S21): each hit at its event + 128 frames within a frame, against the trigger
    # log and the tick grid; in blk the same hits spread over a block (the "before", P13)
    card, opts, script = golden(t, 'G16_onsets')
    log = t.path('trig.log')
    s, wav, _ = t.render(card, opts + ['--trig-log', log, '--events', script])
    check(s['vsched']['late'] == 0 and s['vsched']['offTick'] == 0, 'vsched %r' % s['vsched'])
    for args in (['--log', log], ['--grid', 120]):
        code, spread, out = onsets(t, wav, *args, '--limit', 2)
        print('smp %s: %s' % (args[0], out.strip()))
        check(code == 0, 'smp %s: %s' % (args[0], out))
    _, blk, _ = t.render(card, opts + ['--param', 'trig_timing=1', '--events', script])
    code, spread, out = onsets(t, blk, '--against', wav, '--min', 32)
    print('blk against smp: %s' % out.strip())
    check(code == 0, 'blk against smp: %s' % out)
    no_copies_left()

@case
def V05_midi_in(t):
    # G17 (S9): against the trigger log, and against the script: a DIN Note On's last byte comes
    # two bytes (2 x 69,120 cycles) after its line, a USB transfer at its line
    card, opts, script = golden(t, 'G17_midi_in')
    log, ref = t.path('trig.log'), t.path('script.log')
    _, wav, _ = t.render(card, opts + ['--trig-log', log, '--events', script])
    with open(script) as f, open(ref, 'w') as o:
        n = 0
        for line in f:
            w = line.split('#', 1)[0].split()
            if len(w) >= 3 and w[1] in ('din', 'usb') and w[2] == '91':
                c = math.floor(fractions.Fraction(w[0][:-2]) * 216000) + (2 * 69120 if w[1] == 'din' else 0)
                o.write('%d 0 midi %d 0 0\n' % (c, n))
                n += 1
    check(n == 40, '%d notes in the script' % n)
    for r in (log, ref):
        code, spread, out = onsets(t, wav, '--log', r, '--limit', 2)
        print('%s: %s' % (os.path.basename(r), out.strip()))
        check(code == 0, '%s: %s' % (r, out))
    no_copies_left()

@case
def V05_slave_onsets(t):
    # G18: as a DIN slave at 128 BPM, from bar 2 (Start at 0.5 s + 1.875 s) on the clock grid
    card, opts, script = golden(t, 'G18_slave_onsets')
    s, wav, _ = t.render(card, opts + ['--events', script])
    check(s['seqc']['dropouts'] == 0, 'seqc %r' % s['seqc'])
    code, spread, out = onsets(t, wav, '--grid', 128, '--from', 2.4, '--limit', 2)
    print(out.strip())
    check(code == 0, out)
    no_copies_left()

@case
def V05_save_onsets(t):
    # G19 (P16): a project save at 2 s; TIM2 runs seq_tick() before each render, so no record is late
    card, opts, script = golden(t, 'G19_save')
    log = t.path('trig.log')
    s, wav, _ = t.render(card, opts + ['--trig-log', log, '--events', script])
    check(s['underrunsAfterT0'] == 0 and s['vsched']['late'] == 0, 'underruns %r, vsched %r' % (
        s['underrunsAfterT0'], s['vsched']))
    code, spread, out = onsets(t, wav, '--log', log, '--limit', 2)
    print(out.strip())
    check(code == 0, out)
    no_copies_left()

def wav_channels(path):
    """the four channels L1, R1, L2, R2 of a 16-bit WAV"""
    with wave.open(path, 'rb') as w:
        n = w.getnchannels()
        x = array.array('h')
        x.frombytes(w.readframes(w.getnframes()))
    if sys.byteorder == 'big':
        x.byteswap()
    return [x[c::n] for c in range(n)]

def trig_splits(path):
    """(the split frame S, k, serial, source) of each record of a --trig-log; a timed record's S is
    its event + 128 frames on the block grid at its k (a WAV starts at a block, t0)"""
    out = []
    with open(path) as f:
        for line in f:
            c, v, src, serial, k, late = line.split()
            k = int(k)
            if src == 'oneshot':
                out.append((None, k, int(serial), src))
                continue
            e = float(fractions.Fraction(int(c)) * 113 / 552960) + 128
            S = 64 * round((e - k) / 64) + k
            check(abs(S - e) <= 1.5, '%s: %r: the split %d is %.2f frames from event + 128' % (path, line, S, S - e))
            out.append((S, k, int(serial), src))
    return out

@case
def V05_plocks(t):
    # A step's p-locks in smp (S8, D5; final review of v0.5): they go with the step's record and
    # take effect at its split. Drum 1 alone on out 1, hard left, a sine of long decay, steps 1-5
    # at 120 BPM: step 2 p-locks Drum 1's pan to 127 through p1, step 4 through p2; steps 3 and 5
    # have none, and their triggers put the pan back (autoNode_setDestination()). The pan is
    # latched at each trigger, and hard left or right leaves exact zeros on the other channel.
    # 1. At each split S of steps 2-5 (k > 0) the old note's [S - k, S) keeps the old pan; every
    #    new note has the new one from S. 2. The one-shot roll (performance mode, rate One, while
    #    running) after step 2, with the pan turned back to 0 after the step: its untimed record
    #    (S10) carries step 2's p-lock, so the roll note, from its block's start, is on the right;
    #    the render without the roll equals it up to that block. 3. The pan is latched at the
    #    trigger, the volume is not (it goes through the mixer's one-pole smoother sample by
    #    sample): step 2 alone p-locking Drum 1's volume to 0 leaves the render bit for bit that
    #    of no p-lock before step 2's split, and makes the new note quiet from it.
    seed = t.seed()
    steps = press_steps(1, (1, 2, 3, 4, 5))
    locks = ('+100ms param active_step=1\n+100ms param p1_dest=33\n+100ms param p1_val=127\n'   # PAR_PAN1
             '+100ms param active_step=3\n+100ms param p2_dest=33\n+100ms param p2_val=127\n')
    kit = ('+100ms param osc_wave_drum1=0\n+100ms param veloa1=0\n+100ms param velod1=40\n'
           '+100ms param vol1=127\n+100ms param pan1=0\n')
    run = '+100ms press 25\n+100ms param roll=0\n+100ms press start\n+150ms param pan1=0\n'
    roll = '+20ms down step1\n+100ms up step1\n'
    vol = '+100ms param active_step=1\n+100ms param p1_dest=32\n+100ms param p1_val=0\n'     # PAR_VOL1
    none = '+100ms param active_step=1\n' * 3                                                 # the same times
    renders = []
    for name, lk, extra in (('plain', locks, ''), ('roll', locks, roll), ('none', none, ''), ('vol', vol, '')):
        log = t.path(name + '.log')
        s, wav, _ = t.render(seed, ['--no-start', '--seconds', '7', '--trig-log', log,
                                    '--events', t.script(steps + lk + kit + run + extra)])
        check(s['vsched']['late'] == 0 and s['vsched']['offTick'] == 0, '%s: vsched %r' % (name, s['vsched']))
        renders.append((wav_channels(wav), trig_splits(log)))
    (ch, recs), (rch, rrecs), (nch, nrecs), (vch, vrecs) = renders
    L, R = ch[0], ch[1]
    loud = lambda x, a, b: max((abs(v) for v in x[a:b]), default=0) >= 1000
    zero = lambda x, a, b: not any(x[a:b])
    pans = 'LRLRL'                      # steps 1-5: hard left, p1 127, back, p2 127, back
    check(len(recs) >= 13 and all(r[3] == 'seq' for r in recs), 'plain: the records %r' % (recs,))
    kinds, splits = set(), 0
    for i, (S, k, _, _) in enumerate(recs):
        new = pans[i % 5]
        side, other = (R, L) if new == 'R' else (L, R)
        check(loud(side, S, S + 64) and zero(other, S, S + 64),
              'record %d (step %d, k %d): the new note from frame %d is not panned %s' % (i, i % 5 + 1, k, S, new))
        if i % 5 == 0 or k == 0:
            continue                    # step 1: step 5's note has decayed
        old = pans[(i - 1) % 5]
        side, other = (R, L) if old == 'R' else (L, R)
        check(loud(side, S - k, S) and zero(other, S - k, S),
              'record %d (step %d, k %d): the old note before its split at %d is not panned %s' % (i, i % 5 + 1, k, S, old))
        splits += 1
        kinds.add(i % 5)
    print('of %d records: %d with an old note before the split at k > 0, steps %s among them'
          % (len(recs), splits, sorted(x + 1 for x in kinds)))
    check(splits >= 9 and kinds == {1, 2, 3, 4}, 'splits %d, steps %r' % (splits, kinds))

    # the roll: its block from the serial (each timed record gives block - serial)
    shot = [r for r in rrecs if r[3] == 'oneshot']
    timed = [r for r in rrecs if r[3] == 'seq']
    check(len(shot) == 1, 'roll: the one-shot records %r' % (shot,))
    off = set((S - k) // 64 - serial for S, k, serial, _ in timed)
    check(len(off) == 1, 'roll: block - serial is not one value: %r' % off)
    b = shot[0][2] + off.pop()
    a = 64 * b
    check(timed[1][0] < a < timed[2][0], 'roll: its block at %d is not between steps 2 and 3 (%d, %d)'
          % (a, timed[1][0], timed[2][0]))
    check(all(rch[c][:a] == ch[c][:a] for c in range(4)), 'roll: the render differs before its block at %d' % a)
    check(any(rch[c][a:a + 64] != ch[c][a:a + 64] for c in range(2)), 'roll: no new note in its block at %d' % a)
    check(loud(rch[1], a, a + 64) and zero(rch[0], a, a + 64), 'roll: the note from %d is not panned right' % a)
    print('the one-shot roll from frame %d (block %d): panned right, with step 2\'s p-lock' % (a, b))

    # the volume
    check(nrecs == vrecs, 'vol: the records differ from those without p-locks')
    S, k = vrecs[1][0], vrecs[1][1]
    check(k > 0, 'vol: step 2 at k 0')
    first = next((f for f in range(len(vch[0])) if any(vch[c][f] != nch[c][f] for c in range(4))), None)
    check(first is not None and S <= first < S + 16, 'vol: the render first differs from the one without p-locks '
          'at frame %r, not within 16 frames from step 2\'s split at %d (k %d)' % (first, S, k))
    peak = lambda x: max(abs(v) for v in x[S + 512:S + 2048])
    check(peak(vch[0]) * 20 < peak(nch[0]), 'vol: step 2\'s note is not quiet: peak %d against %d'
          % (peak(vch[0]), peak(nch[0])))
    print('step 2 p-locking the volume to 0: the render first differs from that without p-locks at frame %d, its '
          'split %d (k %d) + %d; then quiet (peak %d against %d)' % (first, S, k, first - S, peak(vch[0]), peak(nch[0])))
    no_copies_left()

# --- v0.6: the groove card (plan v0.6 Task 8, spec v0.6 10.2) -------------------------
# A model of the card's script and of spec v0.6 4.1, outside the firmware: the expected ticks
# of the goldens G20-G22 come from the script's values through it.

# spec v0.6 4.2: the offsets of templates 1..7 in ticks at 100 %, positions 1-16 of the bar
GROOVE_TEMPLATES = {
    1: (0, 16, 0, 16, 0, 16, 0, 16, 0, 16, 0, 16, 0, 16, 0, 16),    # swg
    2: (0, 18, 0, 5, 0, 18, 0, 5, 0, 18, 0, 5, 0, 18, 0, 5),        # lay
    3: (0, 15, 0, 11, 0, 24, 0, 5, 0, 17, 0, 1, 0, 16, 0, 3),       # lyb
    4: (0, 15, 1, 3, 0, 16, 2, 22, 0, 14, 2, 16, 0, 9, 2, 18),      # rlx
    5: (0, 7, 3, 8, 0, 1, 2, 1, 0, 7, 3, 8, 0, 1, 2, 1),            # 2bt
    6: (0, 0, -4, 0, 0, 0, -4, 0, 0, 0, -4, 0, 0, 0, -4, 0),        # psh
    7: (0, 0, 6, 0, 0, 0, 6, 0, 0, 0, 6, 0, 0, 0, 6, 0),            # drg
}
TEMPO_STEPS = (384, 288, 192, 144, 128, 96, 72, 64, 48, 32, 24)    # ticks of a track step, track_tempo 0..10
FLAM_HITS = (1, 2, 3, 4, 6, 8)

def rha(x):
    """x (a Fraction) rounded half away from zero"""
    x = fractions.Fraction(x)
    return math.floor(x + fractions.Fraction(1, 2)) if x >= 0 else -math.floor(-x + fractions.Fraction(1, 2))

def micro_split(T):
    """(coarse, fine) of T: coarse = floor((T + 6) / 12), fine = T - 12 coarse (spec v0.6 M3)"""
    c = (T + 6) // 12
    return c, T - 12 * c

def groove_card(path):
    """the card script's edits as the menu takes them: {(pattern, voice): track} with the track's
    steps {step: (T, flam)} that are on, L, tpl, amt (%) and delay (ms). press voiceN selects the
    voice; press stepN toggles a step on the voice page; SHIFT + step 16 opens the pattern page,
    whose stepN selects pattern N - 1, and mode1 closes it; mark patternN must agree"""
    tracks = {}
    pattern, voice, step, page, shift = 0, 0, 0, 'voice', False
    T, flam = {}, {}
    def tr():
        return tracks.setdefault((pattern, voice), {'on': set(), 'L': 96, 'tpl': 0, 'amt': 100, 'delay': 0})
    with open(path) as f:
        for line in f:
            w = line.split('#', 1)[0].split()
            if len(w) < 3:
                continue
            what, arg = w[1], w[2]
            if what in ('down', 'up') and arg == 'shift':
                shift = what == 'down'
            elif what == 'press' and arg.startswith('voice'):
                voice = int(arg[5:]) - 1
            elif what == 'press' and arg == 'mode1':
                page = 'voice'
            elif what == 'press' and arg.startswith('step'):
                n = int(arg[4:])
                if shift and n == 16:
                    page = 'pattern'
                elif page == 'pattern':
                    pattern = n - 1
                else:
                    tr()['on'] ^= {n - 1}
            elif what == 'mark' and arg.startswith('pattern'):
                check(int(arg[7:]) == pattern, 'groove.txt: %s at pattern %d' % (arg, pattern))
            elif what == 'param':
                name, v = arg.split('=')
                v = int(v)
                key = (pattern, voice, step)
                if name == 'active_step':
                    step = v
                elif name == 'step_micro':
                    T[key] = max(-84, min(84, v - 84))
                elif name == 'step_shift':
                    fine = micro_split(T.get(key, 0))[1]
                    T[key] = max(-84, min(84, 12 * ((v - 7) if v <= 14 else 0) + fine))
                elif name == 'step_flam':
                    flam[key] = v
                elif name == 'track_tempo':
                    tr()['L'] = TEMPO_STEPS[v]
                elif name == 'track_tpl':
                    tr()['tpl'] = v
                elif name == 'track_tpl_amt':
                    tr()['amt'] = v
                elif name == 'track_delay':
                    tr()['delay'] = v - 15
    for (p, v), x in tracks.items():
        x['steps'] = {s: (T.get((p, v, s), 0), flam.get((p, v, s), 0)) for s in sorted(x['on'])}
    return tracks

def groove_events(card, pattern, voice, master_steps, bpm, shuffle=0):
    """(tick, occurrence) of each hit of a voice in a pattern over master_steps 16ths, by spec
    v0.6 4.1 (a 16-step track: q = n mod 16), with v0.2's swing on odd occurrences and the
    ratchets; a hit at or after the next occurrence's first hit is dropped (M9). Returns the hits
    kept and the count dropped"""
    x = card.get((pattern, voice))
    if x is None:
        return [], 0
    L = x['L']
    reach = L - L // 8
    swing = (shuffle * 64 * L + 127 * 96 // 2) // (127 * 96)
    delay = min(rha(fractions.Fraction(x['delay']) * fractions.Fraction(bpm) * 384 / 60000), L)
    occ = []
    n = 0
    while n * L < master_steps * 96:
        s = n % 16
        if s in x['steps']:
            T, flam = x['steps'][s]
            c, fine = micro_split(T)
            C = c * L // 8
            F = rha(fractions.Fraction(fine * L, 96))
            G = rha(fractions.Fraction(GROOVE_TEMPLATES[x['tpl']][n % 16] * x['amt'] * L, 9600)) if x['tpl'] else 0
            P = C if F == 0 and G == 0 else max(-reach, min(reach, C + F + G))
            off = max((swing if n % 2 else 0) + P + delay, -reach)
            k = FLAM_HITS[flam] if flam < 6 else 1
            occ.append([n * L + off + (j * L) // k for j in range(k)])
        n += 1
    kept, dropped = [], 0
    for i, hits in enumerate(occ):
        nxt = occ[i + 1][0] if i + 1 < len(occ) else None
        for h in hits:
            if nxt is not None and h >= nxt:
                dropped += 1
            elif h >= 0:
                kept.append(h)
    return sorted(kept), dropped

def groove_ticks(card, pattern, voice, master_steps, bpm, shuffle=0):
    return groove_events(card, pattern, voice, master_steps, bpm, shuffle)[0]

def pat_tracks(pat):
    """(on steps, T and flam of each of 64 steps, template, groove) of each track of a pattern
    file: 14 bytes of name and tag, 7 x (64 Steps of 7 bytes, 64 StepTiming bytes), the main
    steps, shuffle, lengths, kit, block 1 (LCHM 1 7, 7 x 4), block 2 (LCHM 2 7, 7 x 2, 7 x 32)"""
    b2 = pat[3697:]
    out = []
    for tr in range(7):
        main = int.from_bytes(pat[3598 + 8 * tr:3606 + 8 * tr], 'little')
        steps = []
        for s in range(64):
            tb = pat[14 + 512 * tr + 448 + s]
            flam, sh = tb & 15, tb >> 4
            fine = 0
            if b2:
                nib = (b2[20 + 32 * tr + (s >> 1)] >> ((s & 1) * 4)) & 15
                fine = nib - 16 if nib & 8 else nib
                fine = fine if -6 <= fine <= 5 else 0
            steps.append((12 * (sh - 7 if sh <= 14 else 0) + fine, flam))
        tpl = (b2[6 + 2 * tr], struct.unpack('b', b2[7 + 2 * tr:8 + 2 * tr])[0] + 100) if b2 else (0, 100)
        groove = struct.unpack('4b', pat[3669 + 4 * tr:3673 + 4 * tr])
        out.append({'on': {s for s in range(64) if main >> s & 1}, 'steps': steps, 'tpl': tpl, 'groove': groove})
    return out

def groove_script():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cards', 'scripts', 'groove.txt')

@case
def V06_card(t):
    # the groove card's pattern files (V6-17): the UI path of spec v0.6 6 into the pattern data.
    # Patterns 0-2 hold block 2 (LCHM 2 7) with each track's template and every step's T (the
    # shift nibble and the fine nibble) as the model reads the script; pattern 3 is v0.5's file
    card = groove_card(groove_script())
    root = os.path.join(t.root, 'emu', 'tests', 'cards', 'groove')
    for p in range(3):
        pat = one_file(root, '%02d-*.PAT' % p)
        check(len(pat) == 3941 and pat[3697:3703] == b'LCHM\x02\x07', 'pattern %d: %d bytes, block 2 %r'
              % (p, len(pat), pat[3697:3703]))
        for v, got in enumerate(pat_tracks(pat)):
            x = card.get((p, v), {'on': set(), 'L': 96, 'tpl': 0, 'amt': 100, 'delay': 0, 'steps': {}})
            want = [x['steps'].get(s, (0, 0)) for s in range(16)]
            check(got['on'] == set(x['steps']), 'pattern %d voice %d: steps %r, not %r'
                  % (p, v + 1, sorted(got['on']), sorted(x['steps'])))
            check(got['steps'][:16] == want and all(x == (0, 0) for x in got['steps'][16:]), 'pattern %d voice %d: T, flam %r, not %r'
                  % (p, v + 1, got['steps'][:16], want))
            check(got['tpl'] == (x['tpl'], x['amt']), 'pattern %d voice %d: template %r, not %r'
                  % (p, v + 1, got['tpl'], (x['tpl'], x['amt'])))
            check(got['groove'][0] == TEMPO_STEPS.index(x['L']) - 5 and got['groove'][3] == x['delay'],
                  'pattern %d voice %d: groove %r, not L %d, delay %d' % (p, v + 1, got['groove'], x['L'], x['delay']))
        print('pattern %d: %s' % (p, '; '.join('voice %d: %d steps, T %s, tpl %r' % (
            v + 1, len(x['steps']), [x['steps'][s][0] for s in sorted(x['steps'])], (x['tpl'], x['amt']))
            for (q, v), x in sorted(card.items()) if q == p)))
    pat = one_file(root, '03-*.PAT')
    check(len(pat) == 3697 and not any(pat_tracks(pat)[0]['on']), 'pattern 3: %d bytes' % len(pat))
    # the model's crossings in pattern 2 (G21_crossing): step 2 dropped by step 3, step 9's last three
    # hits by step 10, per bar
    _, dropped = groove_events(card, 2, 0, 32, 120)
    check(dropped == 8, 'pattern 2 over 2 bars: the model drops %d hits, not 8' % dropped)

def ticks_check(t, wav, ch, ticks, bpm, *more):
    """emu_onsets.py --ticks on channel ch of wav against ticks at bpm, --limit 2"""
    tf = t.path('ticks-%d.txt' % len(os.listdir(t.work)))
    with open(tf, 'w') as f:
        f.write(''.join('%d\n' % k for k in ticks))
    code, spread, out = onsets(t, wav, '--channel', ch, '--ticks', tf, '--bpm', bpm, '--limit', 2, *more)
    print('%s, %d ticks: %s' % (ch, len(ticks), out.strip()))
    check(code == 0, '%s against the model\'s ticks: %s' % (ch, out))

@case
def V06_micro(t):
    # G20 (spec v0.6 10.2): each onset of Drum 1 on L1 and Drum 2 (x3/4) on L2 at the tick of the
    # card script's values through spec v0.6 4.1, outside the firmware, within 2 frames
    card = groove_card(groove_script())
    gc, opts, script = golden(t, 'G20_micro')
    s, wav, _ = t.render(gc, opts + ['--events', script])
    check(s['vsched']['late'] == 0, 'vsched %r' % s['vsched'])
    for v, ch in ((0, 'L1'), (1, 'L2')):
        ticks_check(t, wav, ch, groove_ticks(card, 0, v, 64, 120), 120)
    no_copies_left()

def script_shuffle(path):
    """the one shuffle value that the param lines of a golden's script set"""
    with open(path) as f:
        v = {int(x.split('=')[1]) for x in f.read().split() if x.startswith('shuffle=')}
    check(len(v) == 1, '%s sets the shuffles %r' % (path, sorted(v)))
    return v.pop()

@case
def V06_template(t):
    # G21_template (spec v0.6 10.2): Drum 1 with lyb at 100 % on L1, Drum 2 with lay at 150 % and
    # dly +3 ms on L2, the script's shuffle on the odd steps, at the model's ticks within 2 frames
    card = groove_card(groove_script())
    gc, opts, script = golden(t, 'G21_template')
    shuffle = script_shuffle(script)
    s, wav, _ = t.render(gc, opts + ['--events', script])
    check(s['vsched']['late'] == 0, 'vsched %r' % s['vsched'])
    for v, ch in ((0, 'L1'), (1, 'L2')):
        ticks_check(t, wav, ch, groove_ticks(card, 1, v, 64, 142, shuffle=shuffle), 142)
    no_copies_left()

DIN_LAST_BYTE_MS = fractions.Fraction(2 * 69120 * 1000, 216000000)   # a Note On's last byte after its line

@case
def V06_record(t):
    # G22 (spec v0.6 10.2, M19-M22): each render again with a project save after the playback.
    # d_k, step k's offset in ticks, comes from the script's DIN times (each Note On's last byte
    # 0.64 ms after its line). The playback of bars 2 and 3 (to 6.75 s: bar 4 begins at 6.76 s) is
    # at 1536 b + 96 k + d_k within 2 frames; the recorded pattern 3 holds T_k - T_0 = d_k (+11, -5).
    # rqn on: on the grid (--grid 142, and --ticks on the 16ths, which G22_record fails), and the
    # file has no block 2; with lay at 100 % the playback is
    # G22_record's and the file holds d_k - lay_k (-7, -10) and the template (2, 0)
    tick = fractions.Fraction(60000, 142 * 384)
    wavs = {}
    for gid in ('G22_record', 'G22_record_q', 'G22_record_tpl'):
        gc, opts, script = golden(t, gid)
        with open(script) as f:
            text = f.read()
        times = [fractions.Fraction(x.split()[0][:-2]) for x in text.splitlines() if ' din 91 ' in x]
        check(len(times) == 16, '%s: %d Note Ons' % (gid, len(times)))
        d = [round((x + DIN_LAST_BYTE_MS) / tick - 1536 - 96 * k) for k, x in enumerate(times)]
        if gid == 'G22_record':
            check(d == [0, 11, 0, -5] * 4, 'the script\'s offsets %r' % d)
        keep = t.path('K-' + gid)
        s, wav, _ = t.render(gc, opts + ['--keep-card', keep, '--events', t.script(text + '6900ms save-project 0\n')])
        wavs[gid] = wav
        check(s['vsched']['late'] == 0, '%s: vsched %r' % (gid, s['vsched']))
        window = ('--from', 3.35, '--to', 6.75)
        pat = one_file(keep, '03-*.PAT')
        tr = pat_tracks(pat)[0]
        T = [tr['steps'][k][0] for k in range(16)]
        check(tr['on'] == set(range(16)), '%s: steps %r recorded' % (gid, sorted(tr['on'])))
        if gid == 'G22_record':
            ticks_check(t, wav, 'L1', [1536 * b + 96 * k + d[k] for b in (2, 3) for k in range(16)], 142, *window)
            tf = t.path('sixteenths.txt')
            with open(tf, 'w') as f:
                f.write(''.join('%d\n' % (1536 * b + 96 * k) for b in (2, 3) for k in range(16)))
            code, _, out = onsets(t, wav, '--ticks', tf, '--bpm', 142, *window, '--limit', 2)
            check(code == 1, 'G22_record plays on the 16ths: %s' % out)
            check(len(pat) == 3941 and [x - T[0] for x in T] == d and pat[3703:3705] == bytes(2),
                  '%s: %d bytes, T %r, template %r' % (gid, len(pat), T, pat[3703:3705]))
        elif gid == 'G22_record_q':
            code, spread, out = onsets(t, wav, '--grid', 142, *window, '--limit', 2)
            print('%s --grid 142: %s' % (gid, out.strip()))
            check(code == 0, '%s on the grid: %s' % (gid, out))
            # --grid takes the nearest tick; the 16ths themselves
            ticks_check(t, wav, 'L1', [1536 * b + 96 * k for b in (2, 3) for k in range(16)], 142, *window)
            check(len(pat) == 3697 and not any(T), '%s: %d bytes, T %r' % (gid, len(pat), T))
        else:
            code, spread, out = onsets(t, wav, '--against', wavs['G22_record'], *window, '--limit', 2)
            print('%s --against G22_record: %s' % (gid, out.strip()))
            check(code == 0, '%s against G22_record: %s' % (gid, out))
            lay = GROOVE_TEMPLATES[2]
            check(len(pat) == 3941 and [x - T[0] for x in T] == [d[k] - lay[k] for k in range(16)]
                  and pat[3703:3705] == bytes([2, 0]), '%s: %d bytes, T %r, template %r' % (gid, len(pat), T, pat[3703:3705]))
        print('%s: d %r, recorded T %r' % (gid, d, T))
    no_copies_left()

VEL_TEMPLATES = {   # the draft's table (2026-11-04-v0.7-draft.md 2.2): dB at 100 % on the 16ths of a bar
    1: (-10, -15, 0, -14, -10, -14, 0, -13, -10, -16, 0, -14, -10, -15, 0, -13),   # of8
    2: (-7, -15, 0, -5, -7, -9, -1, -10, -7, -14, 0, -7, -6, -10, -1, -10),         # ful
    3: (-14, -12, 0, -19, -14, -11, 0, -19, -15, -12, 0, -16, -13, -12, 0, -21),    # dep
    4: (-5, -15, 0, -20, -6, -17, 0, -16, -5, -17, 0, -15, -6, -17, -1, -16),        # qtr
}

def vel_template(vel, db, pct):
    """plan v0.7 V7-4 in Python: vel x 10^(dB x pct / 2000), dB floored at -42, rounded half up,
    1..127; a vel of 0, dB >= 0 or pct 0 leave vel"""
    if vel == 0 or db >= 0 or pct == 0:
        return vel
    x = vel * 10 ** (max(db, -42) * pct / 2000.0)
    return max(1, min(127, int(math.floor(x + 0.5))))

def trig_frames(path, voice):
    """the frame of each hit of voice in a --trig-log, as emu_onsets.py reads it: the last record of
    each block serial, at its time + 128 frames; the one-shot records are left out"""
    last, order = {}, []
    with open(path) as f:
        for line in f:
            w = line.split()
            if len(w) != 6 or int(w[1]) != voice or w[2] == 'oneshot':
                continue
            if w[3] not in last:
                order.append(w[3])
            last[w[3]] = int(w[0]) * 113 / 552960.0 + 128
    return [last[s] for s in order]

def hit_peaks(wav, channel, frames, before=8, after=220):
    """the largest |sample| of channel from before frames ahead of each frame to after frames past
    it; the hits whose window passes the end are left out"""
    ch, a = samples(wav)
    c = ('L1', 'R1', 'L2', 'R2').index(channel)
    total = len(a) // ch
    out = []
    for f in frames:
        lo, hi = int(f) - before, int(f) + after
        if hi > total:
            break
        out.append(max(abs(a[i * ch + c]) for i in range(lo, hi)))
    return out

@case
def V07_levels(t):
    # G23 (plan v0.7 Task 7, V7-18): Drum 1's click on all 16 steps at 127 with vtp of8 at 100, 50
    # and 200 %. Each hit's peak on L1, in dB against the median of the 0 dB steps' peaks, is within
    # 0.5 dB of the formula's integer velocity computed here from the table (at 200 % step 10's -32
    # dB is 3, -32.54 dB), and at 50 and 100 % within 0.5 dB of the table itself; no peak clips
    for gid, pct in (('G23_velocity', 100), ('G23_velocity_50', 50), ('G23_velocity_200', 200)):
        card, opts, script = golden(t, gid)
        log = t.path(gid + '.log')
        s, wav, _ = t.render(card, opts + ['--trig-log', log, '--events', script])
        check(s['vsched']['late'] == 0, '%s: vsched %r' % (gid, s['vsched']))
        peaks = hit_peaks(wav, 'L1', trig_frames(log, 0))
        check(len(peaks) >= 32, '%s: %d hits of voice 1' % (gid, len(peaks)))
        tab = VEL_TEMPLATES[1]
        loud = sorted(p for k, p in enumerate(peaks) if tab[k % 16] == 0)
        ref = loud[len(loud) // 2]
        check(ref > 0 and max(peaks) < 30000, '%s: the 0 dB peaks %r' % (gid, loud))
        worst = worst_table = 0.0
        for k, p in enumerate(peaks):
            db = tab[k % 16]
            got = 20 * math.log10(p / ref) if p else -120.0
            want = 20 * math.log10(vel_template(127, db, pct) / 127.0)
            worst, worst_table = max(worst, abs(got - want)), max(worst_table, abs(got - db * pct / 100.0))
            check(abs(got - want) <= 0.5, '%s: hit %d (step %d) at %.2f dB, the formula %.2f'
                  % (gid, k, k % 16 + 1, got, want))
            if pct <= 100:
                check(abs(got - db * pct / 100.0) <= 0.5, '%s: hit %d at %.2f dB, the table %.2f'
                      % (gid, k, got, db * pct / 100.0))
        print('%s: %d hits, 0 dB peak %d, worst %.3f dB from the formula, %.3f from the table'
              % (gid, len(peaks), ref, worst, worst_table))

@case
def V07_capture(t):
    # the own template through the panel (plan v0.7 Task 7, V7-6, V7-9..V7-12, V7-14) on the groove
    # card's pattern 0: sav us1 on Drum 1 takes its 16 steps' T (the model's, groove_card()), the
    # clear page's micro sets them to 0 with the flam kept, and tpl us1 at 100 % plays them again:
    # L1's onsets are G20_micro's ticks within 2 frames. The project save after the playback writes
    # USERTPL.LCH (134 bytes: LCHM 1 4, us1's T and 0 dB, us2-us4 empty) and pattern 0 with Drum 1's T
    # all 0, the flam of step 7 and tpl (8, 100)
    card = groove_card(groove_script())
    steps = card[(0, 0)]['steps']
    groove_dir = os.path.join(t.root, 'emu', 'tests', 'cards', 'groove')
    keep, log = t.path('K'), t.path('panel.log')
    script = t.script('0ms press voice1\n+100ms down shift\n+20ms press mode2\n+100ms up shift\n'
                      '+200ms encoder +12\n+200ms mark sav\n+50ms push\n+50ms param track_tpl_save=1\n'
                      '+100ms push\n+300ms mark saved\n+100ms down shift\n+20ms press copy\n+150ms encoder +9\n'
                      '+200ms mark clear\n+100ms push\n+100ms up shift\n+200ms mark cleared\n'
                      '+100ms param track_tpl=8\n+100ms param track_tpl_amt=100\n+200ms press start\n'
                      '+8050ms press start\n+300ms save-project 0\n')
    start = 2.39     # the script's press start: the sum of its times before it, in seconds
    _, wav, _ = t.render(groove_dir, ['--pattern', '0', '--bpm', '120', '--no-start', '--seconds', '11.6',
                                      '--panel-log', log, '--keep-card', keep, '--events', script])
    rows = {x.split()[2]: panel_rows(x) for x in log_lines(log, PANEL_LINE)}
    check(re.match(r'vtp.vdp.SAV', rows['sav'][0]), 'the encoder on sav: %r' % rows['sav'])
    check(re.match(r'off.100.---', rows['saved'][1]), 'sav after the click: %r' % rows['saved'])
    check(rows['clear'][0].startswith('clear [micro]?'), 'the clear page: %r' % rows['clear'])
    check(not rows['cleared'][0].startswith('clear'), 'after the clear: %r' % rows['cleared'])
    ticks_check(t, wav, 'L1', groove_ticks(card, 0, 0, 64, 120), 120, '--from', start - 0.05, '--to', start + 8.04)
    tpl = one_file(keep, 'USERTPL.LCH')
    want = b'LCHM\x01\x04' + bytes(steps.get(s, (0, 0))[0] & 0xFF for s in range(16)) + bytes(16) + bytes(96)
    check(tpl == want, 'USERTPL.LCH: %r, not %r' % (tpl, want))
    pat = one_file(keep, '00-*.PAT')
    check(len(pat) == 3941, 'pattern 0: %d bytes' % len(pat))
    d1 = pat_tracks(pat)[0]
    check(all(T == 0 for T, _ in d1['steps']), 'Drum 1 T after the clear: %r' % d1['steps'][:16])
    check(d1['steps'][6][1] == 1 and d1['tpl'] == (8, 100), 'flam %r, tpl %r' % (d1['steps'][6], d1['tpl']))
    no_copies_left()

def kept_report(t, p, head):
    """stderr of an exit 3 or 4 (plan P2): the line that starts with head and ends in
    'at cycle C', then 32 trail lines, then the card copy's folder, which exists
    and is removed here, then the warnings; returns the head line, the stderr
    lines before it and the warnings"""
    lines = p.stderr.splitlines()
    i = next((k for k, x in enumerate(lines) if x.startswith(head) and re.search(r' at cycle \d+$', x)), None)
    check(i is not None, 'no line %r ... at cycle C: %r' % (head, p.stderr))
    trail = lines[i + 1:i + 33]
    check(len(trail) == 32 and all(re.match(TRAIL_LINE, x) for x in trail), 'not 32 trail lines after %r: %r'
          % (lines[i], trail))
    rest = lines[i + 33:]
    m = re.match(r'^lichomat-render: the card copy is kept in (.+)$', rest[0]) if rest else None
    warnings = rest[1:]
    check(m and all(x.startswith('lichomat-render: warning: ') for x in warnings),
          'the lines after the trail are %r' % rest)
    copy = m.group(1)
    check(os.path.dirname(copy) == t.tmp and os.path.isfile(os.path.join(copy, 'PROJ00', 'PRJ.NFO')),
          'the kept card copy %s is not a card in %s' % (copy, t.tmp))
    shutil.rmtree(copy)
    return lines[i], lines[:i], warnings

@case
def M17_fault(t):
    seed = t.seed()
    faults = [('segv', 'SIGSEGV'), ('abort', 'SIGABRT'), ('model', 'test fault')]
    if t.sanitized:
        faults.append(('ubsan', 'SIGABRT'))
    for fault, what in faults:
        p = t.run(['--card', seed, '--out', t.path('o.wav'), '--seconds', '1', '--test-fault', fault], code=3, test=True)
        line, before, warned = kept_report(t, p, 'lichomat-render: firmware fault: ')
        check(not warned, '--test-fault %s warns %r' % (fault, warned))
        report = any('Sanitizer' in x or 'runtime error' in x for x in before)
        # under ASan a read through NULL may end in its report and abort() instead
        ok = what in line or (fault == 'segv' and 'SIGABRT' in line and report)
        check(ok, '--test-fault %s: %r' % (fault, line))
        if fault == 'ubsan':
            check(any('runtime error: signed integer overflow' in x for x in before),
                  '--test-fault ubsan: no UBSan report before %r' % line)
    # the warnings come after the card copy's line
    card = card_copy(t, seed, 'long')
    with open(os.path.join(card, 'PROJ00', 'LongName9.txt'), 'wb') as f:
        f.write(b'x')
    p = t.run(['--card', card, '--out', t.path('o.wav'), '--seconds', '1', '--test-fault', 'model'], code=3, test=True)
    _, _, warned = kept_report(t, p, 'lichomat-render: firmware fault: ')
    want = ['lichomat-render: warning: card: %s is not an 8.3 name; the device would not see it'
            % os.path.join(card, 'PROJ00', 'LongName9.txt')]
    check(warned == want, 'exit 3 warns %r, not %r' % (warned, want))
    no_copies_left()

@case
def M17_hang(t):
    seed = t.seed()
    start = time.monotonic()
    p = t.run(['--card', seed, '--out', t.path('o.wav'), '--seconds', '1', '--test-hang'], code=4, test=True,
              timeout=120)
    wall = time.monotonic() - start
    check(10 <= wall <= 30, 'exit 4 after %.1f s of wall time, not 10-30 s' % wall)
    line, _, _ = kept_report(t, p, 'lichomat-render: hang: ')
    check(line.startswith('lichomat-render: hang: no progress for 10 s at cycle '), 'the hang line is %r' % line)
    print('exit 4 after %.1f s' % wall)
    no_copies_left()

# --- M14: determinism, slicing and speed, on G1 (spec 8.4, 12.1) -------------------

def golden_tool(t):
    """tools/emu_golden.py of --root, imported"""
    sys.path.insert(0, os.path.join(t.root, 'tools'))
    import emu_golden
    return emu_golden

def same_outputs(eg, a, b, what):
    """the WAV and the MIDI, clock and panel logs of two renders are byte-identical"""
    for k in ('wav', 'midi', 'clock', 'panel'):
        with open(os.path.join(a, eg.FILES[k]), 'rb') as fa, open(os.path.join(b, eg.FILES[k]), 'rb') as fb:
            x, y = fa.read(), fb.read()
        if x == y:
            continue
        i = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), None)
        check(False, '%s: the %s outputs differ (%d and %d bytes, first at byte %s)' % (what, k, len(x), len(y), i))

@case
def M14_repeat(t):
    eg = golden_tool(t)
    g = eg.load('G1')
    a = eg.render(g, t.path('a'), renderer=t.r)
    b = eg.render(g, t.path('b'), renderer=t.r)
    same_outputs(eg, a, b, 'G1 rendered twice')
    no_copies_left()

@case
def M14_slices(t):
    eg = golden_tool(t)
    g = eg.load('G1')
    a = eg.render(g, t.path('whole'), renderer=t.r)
    b = eg.render(g, t.path('sliced'), renderer=t.rt, extra=['--test-slice'])
    same_outputs(eg, a, b, 'G1 with and without --test-slice')
    no_copies_left()

@case
def M14_broken(t):
    selftest = os.path.join(t.root, 'emu', 'tests', 'golden_selftest.txt')
    p = subprocess.run([sys.executable, os.path.join(t.root, 'tools', 'emu_golden.py'), 'check', '--golden-file',
                        selftest, '--render', t.r, '--out', t.path('out')], capture_output=True, text=True, timeout=600)
    want = 'golden_selftest: the broken variant (--seed 1) passes every expectation'
    check(p.returncode == 1 and want in p.stdout.splitlines(), 'emu_golden.py check --golden-file %s: exit %d\n%s%s'
          % (selftest, p.returncode, p.stdout, p.stderr))
    no_copies_left()

@case
def M14_speed(t):
    eg = golden_tool(t)
    d = eg.render(eg.load('G1'), t.path('g1'), renderer=t.r)
    with open(os.path.join(d, eg.FILES['summary'])) as f:
        s = json.load(f)
    check(s['speed'] >= 10, 'G1 renders at %.1fx real time, not 10x or more' % s['speed'])
    print('G1: %.1fx real time (%.3f s of wall time)' % (s['speed'], s['wallSeconds']))
    no_copies_left()

# --- M14: emu_golden.py on outputs made here, without a render ----------------------

def fake_outputs(d, channels, samples, midi=''):
    """a folder of the outputs that emu_golden.Outputs reads: a canonical WAV of 16-bit
    samples (interleaved, at the renderer's 44,140 Hz), the logs and a summary"""
    os.makedirs(d)
    data = struct.pack('<%dh' % len(samples), *samples)
    with open(os.path.join(d, 'out.wav'), 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
                struct.pack('<IHHIIHH', 16, 1, channels, 44140, 44140 * 2 * channels, 2 * channels, 16) +
                b'data' + struct.pack('<I', len(data)) + data)
    for name, text in (('midi.log', midi), ('clock.log', ''), ('panel.log', '')):
        with open(os.path.join(d, name), 'w') as f:
            f.write(text)
    with open(os.path.join(d, 'summary.json'), 'w') as f:
        f.write('{}\n')
    return d

@case
def M14_diff(t):
    """emu_golden.py diff on two WAVs that are equal up to the shorter one's end"""
    eg = golden_tool(t)
    a = [(i * 37) % 2000 - 1000 for i in range(4 * 100)]      # 100 frames, 4 channels
    b = a + [5] * (4 * 10)                                     # the same and 10 frames more
    c = list(b)
    c[4 * 57 + 2] += 1                                         # frame 57, L2
    here, there = eg.Outputs(fake_outputs(t.path('a'), 4, a)), eg.Outputs(fake_outputs(t.path('b'), 4, b))
    for x, y, want in ((here, there, ['X: REV renders 110 frames, the working tree 100']),
                       (there, here, ['X: REV renders 100 frames, the working tree 110']),
                       (here, eg.Outputs(fake_outputs(t.path('same'), 4, a)), []),
                       (here, eg.Outputs(fake_outputs(t.path('c'), 4, c)),
                        ['X: the first differing frame is 57 (0.001291 s), channel L2: -489 at REV, '
                         '-490 in the working tree'])):
        got = eg.wav_differences('X', 'REV', x, y)
        check(got == want, 'wav_differences on %d and %d frames: %r, not %r' % (x.frames, y.frames, got, want))
        print('%d and %d frames: %r' % (x.frames, y.frames, got))

@case
def M14_windows(t):
    """an expectation whose window ends after the render, or on a channel that the WAV
    lacks, fails and says so; inside the render the same lines hold"""
    eg = golden_tool(t)
    o = eg.Outputs(fake_outputs(t.path('out'), 2, [0] * (2 * 44141)))    # 1 s (44,141 frames), L1 and R1

    def ev(text):
        return eg.Expect(1, '#expect ' + text, text, 'M14_windows').evaluate(o)

    for x in ('silent L1 0s..1s', 'silent R1', 'midi DIN 9n 0..0 0s..1s', 'clock 0..0 0s..44141f'):
        holds, detail = ev(x)
        check(holds, '%s on 1 s of silence: %s' % (x, detail))
    for x, want in (('silent L1 2s..3s', 'after the end of the render'),
                    ('silent L1 0s..44142f', 'after the end of the render'),
                    ('midi DIN 9n 0..0 2s..3s', 'after the end of the render'),
                    ('clock 0..0 0s..1.1s', 'after the end of the render'),
                    ('energy L1 -90 0.5s..2s', 'after the end of the render'),
                    ('silent L2', 'the WAV has 2 channels, no L2'),
                    ('silent R2 0s..1s', 'the WAV has 2 channels, no R2'),
                    ('energy L2 -90', 'the WAV has 2 channels, no L2')):
        holds, detail = ev(x)
        check(not holds and want in detail, '%s on a render of 1 s and 2 channels: %s (%s), not a failure that '
              'says %r' % (x, 'holds' if holds else 'fails', detail, want))
        print('%s: fails (%s)' % (x, detail))

@case
def M14_inventory(t):
    """every emu/tests/goldens/*.txt has its goldens.json entry and every entry its .txt:
    CMake's glob drops the ctest id of a golden deleted or renamed by mistake"""
    eg = golden_tool(t)
    neg = t.path('neg')
    os.makedirs(neg)
    for x in ('A.txt', 'B.txt'):
        open(os.path.join(neg, x), 'w').close()
    with open(os.path.join(neg, 'goldens.json'), 'w') as f:
        json.dump({'A': {}, 'C': {}}, f)
    want = ['B.txt has no entry in goldens.json', 'goldens.json has an entry C without C.txt']
    got = eg.inventory(neg)
    check(got == want, 'inventory of A.txt, B.txt and the entries A, C: %r, not %r' % (got, want))
    got = eg.inventory()
    check(not got, 'emu/tests/goldens: %s' % '; '.join(got))
    print('%d goldens, each with its entry in goldens.json, and no other entry' % len(eg.all_ids()))

# --- M23: the card fixtures (spec 5.6) ----------------------------------------------

@case
def M23(t):
    cards = os.path.join(t.root, 'emu', 'tests', 'cards')
    scripts = sorted(x[:-4] for x in os.listdir(os.path.join(cards, 'scripts')) if x.endswith('.txt'))
    committed = sorted(x for x in os.listdir(cards) if x != 'scripts' and os.path.isdir(os.path.join(cards, x)))
    check(committed == scripts, 'the fixtures %r are not the scripts %r' % (committed, scripts))
    out = t.path('cards')
    p = subprocess.run(['/bin/bash', os.path.join(cards, 'make.sh'), t.r, out], capture_output=True, text=True,
                       timeout=600)
    check(p.returncode == 0, 'make.sh: exit %d\n%s%s' % (p.returncode, p.stdout, p.stderr))
    made = sorted(os.listdir(out))
    check(made == scripts, 'make.sh made %r, not %r' % (made, scripts))
    for f in scripts:
        a, b = tree(os.path.join(out, f)), tree(os.path.join(cards, f))
        differ = sorted(k for k in set(a) | set(b) if k not in a or k not in b or a[k] != b[k])
        check(not differ, 'make.sh gives fixture %s other than the committed one at %r' % (f, differ))
        print('%s: %d files, %d bytes, as committed' % (f, sum(1 for v in a.values() if v),
              sum(os.path.getsize(os.path.join(cards, f, k)) for k, v in a.items() if v)))
    left = [x for x in os.listdir(t.tmp) if x.startswith('lichomat-cards-')]
    check(not left, 'make.sh left %r in %s' % (left, t.tmp))
    no_copies_left()

if __name__ == '__main__':
    main()
