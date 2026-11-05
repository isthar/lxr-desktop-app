#!/usr/bin/env python3
"""The LICHOMAT plugin's scenario tests (plan E9.2): the engine through plug_drive against
lichomat-render, and (from Task 9) the VST3 bundle through lichomat-plughost.

    plugin_test.py --root R --build B --render R --engine E --drive D [--host H --bundle V] --work W CASE

Each case prints what it measured and exits 0 when it passes, 1 when it fails.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import argparse
import array
import fractions
import math
import os
import shutil
import signal
import subprocess
import sys
import time
import wave

RATE = fractions.Fraction(353125, 8)    # 44,140.625 Hz
RATE_TEXT = '44140.625'
LOUD, QUIET, QUIET_FRAMES = 4000, 64, 32
CASES = {}


class Fail(Exception):
    pass


def case(fn):
    CASES[fn.__name__] = fn
    return fn


def check(cond, what):
    if not cond:
        raise Fail(what)


class T:
    def __init__(self, a):
        self.a = a
        self.work = os.path.join(a.work, a.case)
        shutil.rmtree(self.work, ignore_errors=True)
        os.makedirs(self.work)

    def path(self, name):
        return os.path.join(self.work, name)

    def card(self, tag, name='onset'):
        """a fresh copy of the committed card fixture name, as card-TAG"""
        dst = self.path('card-' + tag)
        ordered_copy(os.path.join(self.a.root, 'emu', 'tests', 'cards', name), dst)
        return dst

    def run(self, cmd, timeout=600, ok=(0,)):
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        if p.returncode not in ok:
            raise Fail('%s exited %d:\n%s%s' % (os.path.basename(cmd[0]), p.returncode, p.stdout, p.stderr))
        return p

    def render(self, card, script, seconds, out):
        self.run([self.a.render, '--card', card, '--bpm', 'mid', '--no-start', '--as-booted', '--stereo', 'sum',
                  '--seconds', str(seconds), '--events', script, '--out', out])

    def drive(self, card, rate, block, script, frames, out, realtime=None, preroll=False):
        cmd = [self.a.drive, self.a.engine, card, str(rate), str(block), script, str(frames), out]
        if realtime is not None:
            cmd += ['realtime', str(realtime)]
        if preroll:
            cmd += ['preroll']
        line = self.run(cmd).stdout.strip()
        print(line)
        return parse_drive(line)


def ordered_copy(src, dst):
    """copy a card folder in the card's order (emu_hostfs.h:38-42: birth time, then name), so that the
    copy's birth times keep it, as emu_hostCopyCard() does"""
    os.makedirs(dst)
    entries = sorted(os.scandir(src), key=lambda e: (e.stat().st_birthtime, e.name))
    for e in entries:
        if e.name.startswith('.'):
            continue
        if e.is_dir():
            ordered_copy(e.path, os.path.join(dst, e.name))
        else:
            shutil.copyfile(e.path, os.path.join(dst, e.name))


def parse_drive(line):
    """the counters of plug_drive's summary line"""
    out = {}
    for part in line.replace(';', ',').split(','):
        w = part.split()
        for i in range(len(w) - 1):
            if w[i] in ('epoch', 'late', 'dropped', 'underruns', 'full'):
                try:
                    out[w[i]] = int(w[i + 1])
                except ValueError:
                    pass
    return out


def blocks_frames(seconds):
    """lichomat-render's length: whole blocks of 64 frames, rounded up"""
    f = -(-fractions.Fraction(seconds) * RATE // 1)
    return int(-(-f // 64) * 64)


def script_events():
    """The engine's test script (P6, P7) as {time in seconds (a Fraction): [messages]}: USB MIDI clock at
    128 BPM with Start at 4,410 frames, 140 BPM from beat 8, a loop jump at beat 12 back to beat 4 (Stop,
    Song Position 16, Continue with the clock), 12.6 s of clocks; Note Ons and Offs on channel 1, note 48 (the
    onset card's Drum 1 and the voices on out 2), at irregular frames and on and around block boundaries
    (64k - 1, 64k, 64k + 1 at 44,140.625 Hz), the first at frame 0, the epoch's own frame. At the loop's wrap, as a
    host plays it to seven voices on channels of their own, the transport's four messages and each voice's Note Off
    and Note On: 18 messages at one frame, more than one USB transfer holds (16 packets, eng_run.c)."""
    msgs = {}

    def add(f, b):
        msgs.setdefault(fractions.Fraction(f) / RATE, []).append(b)
    t, beat, first, jumped = fractions.Fraction(4410), fractions.Fraction(0), True, False
    while beat < 20:
        bpm = 128 if beat < 8 else 140
        f = int(t)
        if first:
            add(f, 'FA F8')
            first = False
        elif beat == 12 and not jumped:
            add(f, 'FC F2 10 00 FB F8')
            jumped, jump = True, f
            beat = fractions.Fraction(4)
        else:
            add(f, 'F8')
        t += RATE * 60 / (bpm * 24)
        beat += fractions.Fraction(1, 24)
    notes = [9000, 15000, 22222, 30001, 41234, 52345, 60000, 71111, 64 * 1500 - 1, 64 * 1600, 64 * 1700 + 1,
             64 * 2000, 64 * 3333, 64 * 4000 - 1, 500003, 555555]
    for i, f in enumerate(notes):
        add(f, '90 30 7F')
        add(f + 3000 + 7 * i, '80 30 00')
    add(0, '90 30 7F')
    add(2000, '80 30 00')
    for c in range(7):
        add(jump, '%02X 30 00' % (0x80 + c))
    for c in range(7):
        add(jump, '%02X 30 7F' % (0x90 + c))
    for c in range(7):
        add(jump + 2000, '%02X 30 00' % (0x80 + c))
    return msgs


# the frames of a host rate whose times are whole microseconds, which a script line's time "S.UUUUUUs" gives
# the renderer exactly (render_script.c:101-125): 6 frames at 48 kHz (125 us), 12 at 96 kHz, 441 at 44.1 kHz
GRID = {44100: 441, 48000: 6, 96000: 12}


def write_script(t, name, rate=None, render=False):
    """The script at 44,140.625 Hz in frames (rate None); at a host rate, in host frames on GRID[rate] (for
    plug_drive), or with render, the same times in seconds (for lichomat-render). One line per time: one USB
    transfer each, in both."""
    merged = {}
    for s, b in script_events().items():
        if rate is None:
            key = int(s * RATE)
        else:
            g = GRID[rate]
            key = int(s * rate / g + fractions.Fraction(1, 2)) * g
        merged.setdefault(key, []).extend(b)
    lines = []
    for k in sorted(merged):
        when = '%df' % k
        if rate is not None and render:
            us = fractions.Fraction(k * 1000000, rate)
            check(us.denominator == 1, 'frame %d at %d Hz is not a whole microsecond' % (k, rate))
            when = '%d.%06ds' % (us.numerator // 1000000, us.numerator % 1000000)
        lines.append('%s usb %s' % (when, ' '.join(merged[k])))
    p = t.path(name)
    with open(p, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return p


def wav(path):
    """(rate, channels, the samples interleaved)"""
    with wave.open(path, 'rb') as w:
        x = array.array('h')
        x.frombytes(w.readframes(w.getnframes()))
        if sys.byteorder == 'big':
            x.byteswap()
        return w.getframerate(), w.getnchannels(), x


def onsets(x):
    """emu_onsets.py's: the first frame with |x| >= 4,000 after at least 32 frames below 64"""
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


def attacks(x, loud=1000, still=500, ramp=4, gap=32):
    """onsets of a kit whose tails stay above 64 and whose attacks rise over a few frames: the first frame at loud
    or more, after a run of gap frames below still that ended at most ramp frames before it, once per attack"""
    out, run, runs, was = [], 0, [], False
    for f, v in enumerate(x):
        a = -v if v < 0 else v
        runs.append(run)    # the quiet frames right before f
        if a >= loud and not was and (not out or f - out[-1] > gap) and max(runs[max(0, f - ramp):f + 1]) >= gap:
            out.append(f)
        was = a >= loud
        run = run + 1 if a < still else 0
    return out


def same_files(a, b):
    with open(a, 'rb') as f, open(b, 'rb') as g:
        x, y = f.read(), g.read()
    if x == y:
        return None
    n = min(len(x), len(y))
    i = next((k for k in range(n) if x[k] != y[k]), n)
    return 'differ from byte %d (frame %d) of %d, %d' % (i, (i - 44) // 4, len(x), len(y))


def engines_on(card, mode='--card'):
    """the pids of the engines playing card (lichomat-engine --card CARD), or seeding it (mode --seed-card)"""
    return subprocess.run(['pgrep', '-f', 'lichomat-engine %s %s' % (mode, card)], capture_output=True,
                          text=True).stdout.split()


def kill_all(procs, card):
    """SIGKILL the processes of procs still running and every engine of card: a case that fails leaves none"""
    for p in procs:
        if p is not None and p.poll() is None:
            p.kill()
            p.wait()
    for pid in engines_on(card):
        try:
            os.kill(int(pid), signal.SIGKILL)
        except OSError:
            pass


def ninja_inputs(build, target):
    """the explicit inputs of target's build statement in build/build.ninja"""
    with open(os.path.join(build, 'build.ninja')) as f:
        text = f.read().replace('$\n', '')    # ninja's line continuation
    for line in text.split('\n'):
        if line.startswith('build %s:' % target):
            ins = []
            for w in line.split(':', 1)[1].split()[1:]:    # after the rule
                if w in ('|', '||'):
                    break
                ins.append(w)
            return ins
    raise Fail('no build statement for %s in build.ninja' % target)


# --- the engine (Task 6) ------------------------------------------------------------------

@case
def P6_engine_version(t):
    out = t.run([t.a.engine, '--version']).stdout
    print(out.strip())
    check(out.startswith('lichomat-engine 0.1.0, the engine of the LICHOMAT plugin\n'), 'the first line')
    check('Not affiliated with Erica Synths.' in out, 'the notices')
    p = subprocess.run([t.a.engine, '--bogus'], capture_output=True, text=True)
    check(p.returncode == 1 and p.stderr.startswith('usage: lichomat-engine'), 'usage: %d %r' % (p.returncode, p.stderr))


@case
def P6_engine_objects(t):
    # P-2: the engine's firmware, device and renderer objects are lichomat-render's own object files, built with its
    # flags, so the bit-exact cases compare the same code: every object the engine links that is not its own is one
    # that lichomat-render links
    eng = [o for o in ninja_inputs(t.a.build, 'lichomat-engine') if o.endswith('.o')]
    ren = set(ninja_inputs(t.a.build, 'emu/lichomat-render'))
    own = [o for o in eng if o.startswith(('CMakeFiles/lichomat-engine.dir/', 'CMakeFiles/plug_common_san.dir/'))]
    other = [o for o in eng if o not in own]
    print('the engine links %d objects: %d its own, %d of lichomat-render\'s' % (len(eng), len(own), len(set(other) & ren)))
    check(len(other) > 50 and any(o.endswith('/render_fw.c.o') for o in other), 'the emulator\'s objects: %r' % other)
    stray = [o for o in other if o not in ren]
    check(not stray, 'objects lichomat-render does not link: %s' % stray)


@case
def P6_engine_native(t):
    # the engine through plug_drive at 44,140.625 Hz in blocks of 128 is lichomat-render, byte for byte
    script = write_script(t, 'script.txt')
    frames = blocks_frames(14)
    t.render(t.card('render'), script, 14, t.path('render.wav'))
    s = t.drive(t.card('drive'), RATE_TEXT, 128, script, frames, t.path('drive.wav'))
    check(s.get('epoch') == 0 and s.get('late') == 0 and s.get('dropped') == 0, 'counters %r' % s)
    d = same_files(t.path('render.wav'), t.path('drive.wav'))
    check(d is None, 'the engine and the renderer %s' % d)
    _, _, x = wav(t.path('render.wav'))
    on = onsets(x[0::2])
    print('render == drive: %d frames, %d onsets in L' % (frames, len(on)))
    check(len(on) > 20, 'only %d onsets' % len(on))


@case
def P6_engine_blocks(t):
    # the block size does not change the output: 1, 37, 512 and 4,096 frames; no input late or dropped (with
    # blocks of 1 the engine waits at the published frontier at every hook)
    script = write_script(t, 'script.txt')
    frames = blocks_frames(3)
    t.render(t.card('render'), script, 3, t.path('render.wav'))
    for b in (1, 37, 512, 4096):
        out = t.path('drive-%d.wav' % b)
        s = t.drive(t.card('b%d' % b), RATE_TEXT, b, script, frames, out)
        check(s.get('late') == 0 and s.get('dropped') == 0 and s.get('full') == 0, 'blocks of %d: counters %r' % (b, s))
        d = same_files(t.path('render.wav'), out)
        check(d is None, 'blocks of %d: %s' % (b, d))
    print('blocks of 1, 37, 512, 4096: each == render')


@case
def P6_engine_paced(t):
    # paced like a host at 44,140.625 Hz in blocks of 128 with the default margin, 3 s: the engine waits at the
    # published frontier, as in a host, and no input is late; the output, read plug_readOffset() frames behind as
    # the plugin reads it, is the renderer's byte for byte. In real time the engine's thread has the time-constraint
    # policy (P-15): ps -M shows its priority as 97R (a normal thread's is 31T)
    script = write_script(t, 'script.txt')
    frames = blocks_frames(3)
    t.render(t.card('render'), script, 3, t.path('render.wav'))
    card = t.card('p')
    p = None
    try:
        p = subprocess.Popen([t.a.drive, t.a.engine, card, RATE_TEXT, '128', script, str(frames), t.path('drive.wav'),
                              'realtime', '128'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(1.0)
        pids = engines_on(card)
        check(len(pids) == 1, 'engines while the driver runs: %r' % pids)
        rows = subprocess.run(['ps', '-M', '-p', pids[0]], capture_output=True, text=True).stdout.split('\n')
        pri = rows[1].split()[5] if len(rows) > 1 and len(rows[1].split()) > 5 else '?'
        print('the engine\'s main thread: priority %s' % pri)
        check(pri.endswith('R') and pri[:-1].isdigit() and int(pri[:-1]) >= 90, 'not a time-constraint thread: %r' % rows[:2])
        out, err = p.communicate(timeout=120)
        check(p.returncode == 0, 'plug_drive exited %d: %s%s' % (p.returncode, out, err))
    finally:
        kill_all([p], card)
    print(out.strip())
    s = parse_drive(out)
    check(s.get('late') == 0 and s.get('dropped') == 0 and s.get('full') == 0 and s.get('underruns') == 0,
          'counters %r' % s)
    d = same_files(t.path('render.wav'), t.path('drive.wav'))
    check(d is None, 'paced: %s' % d)
    print('paced, blocks of 128: == render, late 0')


@case
def P6_engine_seed(t):
    # an empty card folder: the engine seeds it as lichomat-render --seed-card does, then plays it
    seeded = t.path('seeded')
    t.run([t.a.render, '--seed-card', seeded])
    empty = t.path('empty')
    os.makedirs(empty)
    script = write_script(t, 'script.txt')
    frames = blocks_frames(3)
    t.render(seeded, script, 3, t.path('render.wav'))
    t.drive(empty, RATE_TEXT, 256, script, frames, t.path('drive.wav'))
    check(os.path.isfile(os.path.join(empty, 'PROJ00', 'PRJ.NFO')), 'no PROJ00/PRJ.NFO in the seeded folder')
    d = same_files(t.path('render.wav'), t.path('drive.wav'))
    check(d is None, 'seeded by the engine vs by the renderer: %s' % d)
    print('the engine seeded %s: %s' % (empty, sorted(os.listdir(empty))))


@case
def P6_engine_epoch(t):
    # In a host the epoch is never 0: the plugin publishes every block, also while its engine boots. With
    # plug_drive's pre-roll (a Note On before the engine starts and one while it boots, which the new engine
    # leaves out with the ring, and one at epoch - 1 once it runs, which it drops), blocks of 128 and 37: the
    # epoch is above 0, one event dropped, none late, and the output from the epoch on is the renderer's, byte for
    # byte, the script's Note On at frame 0 sounding from the epoch's own frame.
    script = write_script(t, 'script.txt')
    frames = blocks_frames(3)
    t.render(t.card('render'), script, 3, t.path('render.wav'))
    for b in (128, 37):
        out = t.path('drive-%d.wav' % b)
        s = t.drive(t.card('e%d' % b), RATE_TEXT, b, script, frames, out, preroll=True)
        check(s.get('epoch', 0) > 0 and s.get('dropped') == 1 and s.get('late') == 0 and s.get('full') == 0,
              'blocks of %d: counters %r' % (b, s))
        d = same_files(t.path('render.wav'), out)
        check(d is None, 'blocks of %d: %s' % (b, d))
    print('the pre-roll, blocks of 128 and 37: each == render')


@case
def P6_engine_quit(t):
    # plug_drive ends the engine (quit, its pipe closed: exit 0); a killed driver leaves no engine behind. Whatever
    # fails, the case kills its driver and every engine of its card
    script = write_script(t, 'script.txt')
    s = t.run([t.a.drive, t.a.engine, t.card('q'), RATE_TEXT, '128', script, '44160', t.path('q.wav')]).stdout
    check('engine exit 0' in s, s)
    card = t.card('k')
    p = None
    try:
        p = subprocess.Popen([t.a.drive, t.a.engine, card, RATE_TEXT, '128', script, str(blocks_frames(60)),
                              t.path('k.wav'), 'realtime', '128'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(2.0)
        engines = engines_on(card)
        check(len(engines) == 1, 'engines while the driver runs: %r' % engines)
        p.send_signal(signal.SIGKILL)
        p.wait()
        t0 = time.monotonic()
        while time.monotonic() - t0 < 3.0:
            left = engines_on(card)
            if not left:
                break
            time.sleep(0.05)
        print('the engine ended %.2f s after its driver was killed' % (time.monotonic() - t0))
        check(not left, 'the engine outlived its driver: %r' % left)
    finally:
        kill_all([p], card)


@case
def P6_engine_orphan(t):
    # A host that dies while its engine is elsewhere than in the wait for events (a firmware that hangs, the
    # prelude, the seeding) leaves no engine behind: here the engine waits for its seeding of an empty card, held
    # with SIGSTOP, when its driver is killed, and must be gone within 1 s. Whatever fails, the case kills what it
    # started. A seeding over before it could be held is tried again, up to 3 times.
    script = write_script(t, 'script.txt')
    for attempt in range(3):
        card = t.path('empty%d' % attempt)
        os.makedirs(card)
        p, seed = None, None
        try:
            p = subprocess.Popen([t.a.drive, t.a.engine, card, RATE_TEXT, '128', script, str(blocks_frames(3)),
                                  t.path('o%d.wav' % attempt)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            t0 = time.monotonic()
            while seed is None and p.poll() is None and time.monotonic() - t0 < 10.0:
                for pid in engines_on(card, '--seed-card'):
                    try:
                        os.kill(int(pid), signal.SIGSTOP)
                        seed = int(pid)
                    except OSError:
                        pass
            if seed is None:
                print('attempt %d: the seeding was over before it could be held' % attempt)
                continue
            engines = engines_on(card)
            check(len(engines) == 1, 'engines while the seeding is held: %r' % engines)
            p.kill()
            p.wait()
            t0 = time.monotonic()
            while time.monotonic() - t0 < 3.0:
                left = engines_on(card)
                if not left:
                    break
                time.sleep(0.02)
            gone = time.monotonic() - t0
            print('the engine, held in its seeding, ended %.2f s after its driver was killed' % gone)
            check(not left and gone < 1.0, 'the engine outlived its driver: %r after %.2f s' % (left, gone))
            return
        finally:
            kill_all([p], card)
            for pid in engines_on(card, '--seed-card'):
                try:
                    os.kill(int(pid), signal.SIGKILL)
                except OSError:
                    pass
    raise Fail('the seeding was never held')


@case
def P7_engine_resampled(t):
    # 44.1, 48 and 96 kHz: the engine's output is the renderer's for the same input times through the same
    # converter, 64 frames a push (plug_resample_wav), within 2 LSB: the renderer's WAV rounds (L1 + L2) / 2
    # down, the engine converts (L1 + L2) / 65,536. A frame out of place would differ by thousands. The
    # input times lie on GRID, which both reach at the same cycle.
    seconds = 6
    frames441 = blocks_frames(seconds)
    for rate in (44100, 48000, 96000):
        t.render(t.card('render%d' % rate), write_script(t, 'render%d.txt' % rate, rate, render=True), seconds,
                 t.path('render%d.wav' % rate))
        frames = int(frames441 * fractions.Fraction(rate) / RATE) - 256
        s = t.drive(t.card('r%d' % rate), str(rate), 256, write_script(t, 'drive%d.txt' % rate, rate), frames,
                    t.path('drive%d.wav' % rate))
        check(s.get('late') == 0 and s.get('epoch') == 0, 'counters %r' % s)
        t.run([t.a.resample, t.path('render%d.wav' % rate), str(rate), t.path('ref%d.wav' % rate), str(frames)])
        _, _, a = wav(t.path('drive%d.wav' % rate))
        _, _, b = wav(t.path('ref%d.wav' % rate))
        check(len(a) == len(b), '%d Hz: %d and %d samples' % (rate, len(a), len(b)))
        worst = max(abs(x - y) for x, y in zip(a, b))
        loud = sum(1 for x in a if abs(x) >= 4000)
        print('%d Hz: %d frames, %d samples at 4,000 or more, the largest difference %d LSB' % (rate, frames, loud, worst))
        check(worst <= 2 and loud > 1000, '%d Hz: the largest difference %d LSB' % (rate, worst))


@case
def P7_engine_realtime(t):
    # paced like a host at 48 kHz in blocks of 128 with the default margin, four engines at once: no underrun, no
    # input late or dropped
    script = write_script(t, 'script48.txt', 48000)
    frames = 48000 * 10
    procs = []
    for i in range(4):
        procs.append(subprocess.Popen([t.a.drive, t.a.engine, t.card('rt%d' % i), '48000', '128', script, str(frames),
                                       t.path('rt%d.wav' % i), 'realtime', '128'], stdout=subprocess.PIPE, text=True))
    for i, p in enumerate(procs):
        out, _ = p.communicate(timeout=120)
        print('engine %d: %s' % (i, out.strip()))
        s = parse_drive(out)
        check(p.returncode == 0 and all(s.get(k) == 0 for k in ('underruns', 'late', 'dropped', 'full')),
              'engine %d: %s' % (i, out))


# --- the bundle (Task 8) ----------------------------------------------------------------------

@case
def P10_plugin_validator(t):
    e = dict(os.environ)
    e['LICHOMAT_PLUGIN_HOME'] = t.path('home')
    e['LICHOMAT_EMU_HOME'] = t.path('no-emu')
    p = subprocess.run([t.a.validator, t.a.bundle], capture_output=True, text=True, timeout=600, env=e)
    result = [l for l in p.stdout.splitlines() if l.startswith('Result:')]
    print('\n'.join(result))
    check(p.returncode == 0 and result and result[-1].endswith(' 0 tests failed'), 'the validator: %d %r' % (p.returncode, result))
    check(not os.listdir(t.path('home')), 'instance folders left: %r' % os.listdir(t.path('home')))


def main():
    ap = argparse.ArgumentParser()
    for o in ('root', 'build', 'render', 'engine', 'drive', 'resample', 'work'):
        ap.add_argument('--' + o, required=True)
    for o in ('host', 'bundle', 'validator', 'rtcheck'):
        ap.add_argument('--' + o)
    ap.add_argument('case', choices=sorted(CASES))
    a = ap.parse_args()
    t = T(a)
    try:
        CASES[a.case](t)
    except Fail as e:
        print('FAIL %s: %s' % (a.case, e))
        return 1
    print('PASS %s' % a.case)
    return 0


if __name__ == '__main__':
    sys.exit(main())
