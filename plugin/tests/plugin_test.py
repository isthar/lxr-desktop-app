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
import fcntl
import fractions
import math
import os
import re
import shutil
import signal
import struct
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
    check(not instance_dirs(t.path('home')), 'instance folders left: %r' % os.listdir(t.path('home')))


def macho_uuid(path):
    """the LC_UUID of a thin 64-bit Mach-O: the linker's hash of the image, which signing leaves alone"""
    with open(path, 'rb') as f:
        head = f.read(32)
        magic, _, _, _, ncmds, size = struct.unpack('<IiiIII', head[:24])
        check(magic == 0xFEEDFACF, '%s is not a thin 64-bit Mach-O' % path)
        cmds = f.read(size)
    at = 0
    for _ in range(ncmds):
        cmd, n = struct.unpack('<II', cmds[at:at + 8])
        if cmd == 0x1B:    # LC_UUID
            return cmds[at + 8:at + 24].hex()
        at += n
    raise Fail('%s has no LC_UUID' % path)


@case
def P10_plugin_helper(t):
    # the bundle's Contents/Helpers/lichomat-engine is the engine the build made last (the bundle is linked again,
    # and the helper copied again, whenever the engine changes), so every P10 case tests the current engine
    helper = os.path.join(t.a.bundle, 'Contents', 'Helpers', 'lichomat-engine')
    a, b = macho_uuid(helper), macho_uuid(t.a.engine)
    print('the bundle\'s helper %s, the built engine %s' % (a, b))
    check(a == b, 'the bundle holds another engine than the one built')


# --- the VST3 bundle through lichomat-plughost (Task 9) -----------------------------------

def plan(t, name, lines, rate=RATE_TEXT, block=128, frames=None, card=True, bundle=None):
    """a plughost plan in the case's folder: the bundle, the rate, the block, the frames, the onset card"""
    body = ['bundle ' + (bundle or t.a.bundle), 'rate ' + str(rate), 'block %d' % block,
            'frames %d' % (frames or blocks_frames(12))]
    if card:
        body.append('card ' + os.path.join(t.a.root, 'emu', 'tests', 'cards', 'onset'))
    path = t.path(name)
    with open(path, 'w') as f:
        f.write('\n'.join(body + lines) + '\n')
    return path


def host_env(t, env=None):
    e = dict(os.environ)
    e['LICHOMAT_PLUGIN_HOME'] = t.path('home')
    e['LICHOMAT_EMU_HOME'] = t.path('no-emu')	# no LICHOMAT Emu card unless a case gives one
    e.update(env or {})
    return e


def host(t, plan_path, env=None):
    p = subprocess.run([t.a.host, plan_path], capture_output=True, text=True, timeout=600, env=host_env(t, env))
    print(p.stdout.strip())
    if p.returncode != 0:
        raise Fail('lichomat-plughost exited %d:\n%s%s' % (p.returncode, p.stdout, p.stderr))
    lat = [int(l.split('latency ')[1].split(',')[0]) for l in p.stdout.splitlines() if 'latency ' in l]
    return lat, p.stdout + p.stderr


def shifted_equal(a, b, shift):
    """a's frames from shift on against b's from 0, both interleaved stereo: (frames compared, samples that differ)"""
    n = min(len(a) // 2 - shift, len(b) // 2)
    pa, pb = a[2 * shift:2 * (shift + n)], b[:2 * n]
    return n, sum(1 for x, y in zip(pa, pb) if x != y), sum(1 for x in pa if x)


# a 12 s timeline: Start at 4,410 (the block from 4,480 on), 128 BPM, the loop [4, 8) quarter notes, 140 BPM
# from 300,000; Note Ons and Offs on Drum 1's note
TIMELINE = ['play 4410 128 0', 'loop 4 8', 'tempo 300000 140', 'note 9000 0 48 127', 'note 12000 0 48 0',
            'note 64000 0 48 100', 'note 67000 0 48 0', 'note 400003 0 48 127']


@case
def P10_plugin_native(t):
    # at 44,140.625 Hz, offline, blocks of 128 and of 100: the plugin's output from frame latency - 128 on is
    # lichomat-render's for the MIDI the plugin gave its engine (plughost's script), byte for byte
    for block in (128, 100):
        script = t.path('script%d.txt' % block)
        lat, _ = host(t, plan(t, 'plan%d.txt' % block, TIMELINE + ['out ' + t.path('plugin%d.wav' % block), 'script ' + script],
                              block=block))
        t.render(t.card('render%d' % block), script, 12, t.path('render%d.wav' % block))
        _, _, a = wav(t.path('plugin%d.wav' % block))
        _, _, b = wav(t.path('render%d.wav' % block))
        n, diff, loud = shifted_equal(a, b, lat[0] - 128)
        with open(script) as f:
            stops = sum(1 for l in f if ' FC' in l)
        print('blocks of %d: latency %d, %d frames compared, %d differ, %d loop jumps' % (block, lat[0], n, diff, stops))
        check(diff == 0 and loud > 100000 and stops >= 4, 'blocks of %d: %d samples differ' % (block, diff))
        check(all(x == 0 for x in a[:2 * (lat[0] - 128)]), 'sound before the latency')


def beat_frames(rate, block, frames, tempo0, change, tempo1, loop_end):
    """The frames of the quarter notes of plughost's timeline (plug_host.cpp's Timeline), computed here and not
    by plug_clock: play from position 0 at tempo0, tempo1 from frame change on, the loop [0, loop_end) quarter
    notes; the position at each block's first frame, wrapped at the loop's end, the beat at its nearest frame"""
    out, pos, f = [], 0.0, 0
    while f < frames:
        n = min(block, frames - f)
        tempo = tempo1 if f >= change else tempo0
        fpq = rate * 60.0 / tempo
        end = pos + n / fpq
        q = math.ceil(pos - 1e-9)
        while q < end and q < loop_end:
            out.append(f + int(math.floor((q - pos) * fpq + 0.5)))
            q += 1
        if end >= loop_end:
            w = int(math.floor((loop_end - pos) * fpq + 0.5))
            if w < n:
                out.append(f + w)    # the loop's start, the jump's frame
            end = (end - loop_end) % loop_end
        pos = end
        f += n
    return sorted(set(out))


@case
def P10_plugin_onsets(t):
    # the init card's pattern (all seven voices on every quarter note) as the slave of a looped transport: each
    # onset at its beat's frame plus the latency, the beats computed here (beat_frames), through 5 loops of a bar
    # and a tempo change from 120 to 140 BPM. Its tails stay at 65-89 and its attacks rise over 2-3 frames: the
    # onsets are attacks()
    init = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'init')
    frames = 44140 * 10
    lines = ['card ' + init, 'play 0 120 0', 'loop 0 4', 'tempo 220000 140', 'out ' + t.path('beats.wav')]
    lat, _ = host(t, plan(t, 'beats.txt', lines, card=False, frames=frames))
    _, _, x = wav(t.path('beats.wav'))
    on = attacks(x[0::2])
    want = [b + lat[0] for b in beat_frames(float(RATE), 128, frames, 120.0, 220000, 140.0, 4.0) if b + lat[0] < frames - 64]
    res = [o - w for o, w in zip(on, want)]
    print('%d onsets, %d beats; onset - (beat + latency %d): %r' % (len(on), len(want), lat[0], sorted(set(res))))
    # -1..+1: the voices start on even frames, and the first sample at 1,000 of a ramp may come a frame later
    check(len(on) == len(want) and all(-1 <= r <= 1 for r in res), 'onsets %r beats %r' % (on[:12], want[:12]))


@case
def P10_plugin_latency(t):
    # isolated notes, no transport: each onset is at its note's frame plus the reported latency (minus 1 for an
    # odd frame: the voices start on even frames), for blocks of 128 and 333
    notes = [10000 + 40001 * k for k in range(10)]
    lines = []
    for m in notes:
        lines += ['note %d 0 48 127' % m, 'note %d 0 48 0' % (m + 5000)]
    for block in (128, 333):
        lat, _ = host(t, plan(t, 'plan%d.txt' % block, lines + ['out ' + t.path('lat%d.wav' % block)], block=block,
                              frames=441406))
        _, _, x = wav(t.path('lat%d.wav' % block))
        on = onsets(x[0::2])
        d = [o - m for o, m in zip(on, notes)]
        print('blocks of %d: latency %d, onset - note %r' % (block, lat[0], d))
        check(len(on) == len(notes) and all(v in (lat[0], lat[0] - 1) for v in d), 'blocks of %d: %r' % (block, d))


@case
def P10_plugin_resampled(t):
    # 48 kHz, blocks of 512: the plugin's output from plug_readOffset() on is the engine's through plug_drive for
    # the same MIDI, byte for byte; the latency is round(176 x r) + the read offset
    script = t.path('s48.txt')
    lat, _ = host(t, plan(t, 'plan48.txt', TIMELINE + ['out ' + t.path('p48.wav'), 'script ' + script], rate=48000,
                          block=512, frames=480000))
    t.drive(t.card('d48'), '48000', 512, script, 478000, t.path('d48.wav'))
    _, _, a = wav(t.path('p48.wav'))
    _, _, b = wav(t.path('d48.wav'))
    read = 512 + 128 + 3
    n, diff, loud = shifted_equal(a, b, read)
    print('48 kHz: latency %d (191 + %d), %d frames compared, %d differ' % (lat[0], read, n, diff))
    check(lat[0] == 191 + read and diff == 0 and loud > 100000, '48 kHz: latency %d, %d differ' % (lat[0], diff))


@case
def P10_plugin_state(t):
    # two instances in one host: each is the single one; its state (the card) restored by a new host plays the same
    one = t.path('one.wav')
    host(t, plan(t, 'one.txt', TIMELINE + ['out ' + one]))
    host(t, plan(t, 'two.txt', TIMELINE + ['out ' + t.path('two.wav'), 'instances 2', 'state-out ' + t.path('state.bin')]))
    restored = t.path('restored.wav')
    host(t, plan(t, 'restore.txt', TIMELINE + ['out ' + restored, 'state-in ' + t.path('state.bin')], card=False))
    for other in ('two.wav', 'two-1.wav', 'restored.wav'):
        d = same_files(one, t.path(other))
        check(d is None, '%s and one.wav %s' % (other, d))
    with open(t.path('state.bin'), 'rb') as f:
        head = f.read(8)
    print('two instances == one; the state, %d bytes (%r...), restores it' % (os.path.getsize(t.path('state.bin')), head))
    check(head == b'LCHS\x01\x00\x00\x00', 'the state header %r' % head)
    check(not instance_dirs(t.path('home')), 'instance folders left: %r' % os.listdir(t.path('home')))


@case
def P10_plugin_app_card(t):
    # a new instance without a state starts on a copy of LICHOMAT Emu's card: with LICHOMAT_EMU_HOME/Card the onset
    # card, it plays as the onset card given as a state does; the plugin's own BASE/Card (groove) comes first
    emu = t.path('emu')
    ordered_copy(os.path.join(t.a.root, 'emu', 'tests', 'cards', 'onset'), os.path.join(emu, 'Card'))
    host(t, plan(t, 'state.txt', TIMELINE + ['out ' + t.path('state.wav')]))
    host(t, plan(t, 'app.txt', TIMELINE + ['out ' + t.path('app.wav')], card=False), env={'LICHOMAT_EMU_HOME': emu})
    d = same_files(t.path('state.wav'), t.path('app.wav'))
    check(d is None, 'the app card and the state %s' % d)
    host(t, plan(t, 'init.txt', TIMELINE + ['out ' + t.path('init.wav')], card=False))
    check(same_files(t.path('state.wav'), t.path('init.wav')) is not None, 'without the app card it played the onset card')
    groove = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'groove')
    host(t, plan(t, 'groove.txt', TIMELINE + ['out ' + t.path('groove.wav'), 'card ' + groove], card=False))
    ordered_copy(groove, os.path.join(t.path('home'), 'Card'))
    host(t, plan(t, 'own.txt', TIMELINE + ['out ' + t.path('own.wav')], card=False), env={'LICHOMAT_EMU_HOME': emu})
    d = same_files(t.path('groove.wav'), t.path('own.wav'))
    check(d is None, 'the plugin\'s own card and the groove card as a state %s' % d)
    print("LICHOMAT Emu's card == the same card as a state; without it, the init card; BASE/Card comes first")


@case
def P10_plugin_renew(t):
    # delete and add again: instance 0 is terminated at 4.5 s and a new one takes its place; it plays from its own
    # frame 0 (latency silence) and starts the slave afresh where the transport is. Offline the new instance's first
    # block waits for its engine, so a Note On in that block sounds at the renewal plus the latency (or one less);
    # the slave's first step, from 3,000 frames on (after the note's attack), at the renewal plus 4,713 frames
    renew = 198656
    tl = TIMELINE[:-1] + ['note %d 0 48 127' % renew, 'note %d 0 48 0' % (renew + 5000)] + TIMELINE[-1:]
    lat, _ = host(t, plan(t, 'renew.txt', tl + ['out ' + t.path('renew.wav'), 'renew %d 0' % renew]))
    _, _, x = wav(t.path('renew.wav'))
    left = x[0::2]
    note = next((i for i in range(renew, len(left)) if abs(left[i]) >= 4000), renew) - renew
    step = next((i for i in range(renew + 3000, len(left)) if abs(left[i]) >= 4000), renew) - renew
    print('the new instance (latency %d): its first block\'s Note On sounds %d frames after its start, the slave\'s '
          'first step %d frames after it' % (lat[0], note, step))
    check(note in (lat[0], lat[0] - 1), 'the Note On of the first block at +%d' % note)
    check(step == 4713, 'the slave\'s first step at +%d' % step)


def instance_dirs(home):
    """the instance folders in BASE: 16 lowercase hex digits (plugin_card.cpp)"""
    return [n for n in os.listdir(home) if len(n) == 16 and all(c in '0123456789abcdef' for c in n)]


def instance_logs(home):
    """{name: text} of the instance logs in BASE/logs"""
    d = os.path.join(home, 'logs')
    out = {}
    for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        with open(os.path.join(d, n), errors='replace') as f:
            out[n] = f.read()
    return out


@case
def P10_plugin_logs(t):
    # each instance's log, BASE/logs/<id>.log, outlives the instance (the owner's check P8 asks for it after the set
    # is closed): the plugin's lines, each engine started and ended (with its exit), to the millisecond. BASE/logs
    # keeps the newest 32 logs: with 40 old ones, a host's instance removes 8 when it starts, the next host's one
    home = t.path('home')
    logs = os.path.join(home, 'logs')
    os.makedirs(logs)
    old = time.time() - 3600
    for k in range(40):
        p = os.path.join(logs, '%016x.log' % k)
        with open(p, 'w') as f:
            f.write('an old log\n')
        os.utime(p, (old + k, old + k))
    stamp = r'\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} '
    for run in (1, 2):
        host(t, plan(t, 'run%d.txt' % run, TIMELINE + ['out ' + t.path('run%d.wav' % run)], frames=blocks_frames(1)))
        found = instance_logs(home)
        new = {n: x for n, x in found.items() if x != 'an old log\n'}
        kept = sorted(n for n in found if n not in new)
        print('after run %d: %d new logs, %d old kept (%s ... %s)' % (run, len(new), len(kept), kept[0], kept[-1]))
        check(len(new) == run and all(n.endswith('.log') and len(n) == 20 for n in new), 'the new logs %r' % sorted(new))
        check(kept == ['%016x.log' % k for k in range(7 + run, 40)], 'the old logs kept: %r' % kept)
    for n, x in sorted(new.items()):
        print('%s:\n%s' % (n, x.strip()))
        check(re.search(stamp + r'engine \d+ started on .+/card\n', x)
              and re.search(stamp + r'engine \d+ ended \(exit 0\): the plugin stopped it', x), '%s: %r' % (n, x))
    check(not instance_dirs(home), 'instance folders left: %r' % instance_dirs(home))


@case
def P10_plugin_setstate(t):
    # setState while the instance plays (Live's preset load, paste, undo), offline, at 4.5 s: the plugin restarts its
    # engine on the state's new card at once (the call returns once the new engine is started); the output is the
    # old one's up to that frame, silence for the read offset (the new engine's ring starts at its epoch, that frame),
    # then the same as an instance on the groove card given the same state there. A corrupt archive is refused and
    # changes nothing. No folder or engine is left
    at = 198656
    groove = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'groove')
    host(t, plan(t, 'make.txt', ['play 0 120 0', 'out ' + t.path('make.wav'), 'state-out ' + t.path('groove.bin'),
                                 'card ' + groove], frames=12800, card=False))
    with open(t.path('bad.bin'), 'wb') as f:
        f.write(b'LCHS\x01\x00\x00\x00\x02\x05\x00../xx' + b'\x00' * 20)
    runs = {}
    for name, lines, card in (('onset', [], True), ('set', ['setstate %d %s' % (at, t.path('groove.bin'))], True),
                              ('bad', ['setstate %d %s' % (at, t.path('bad.bin'))], True),
                              ('ref', ['setstate %d %s' % (at, t.path('groove.bin')), 'card ' + groove], False)):
        lat, out = host(t, plan(t, name + '.txt', TIMELINE + ['out ' + t.path(name + '.wav')] + lines, card=card))
        runs[name] = (wav(t.path(name + '.wav'))[2], [l for l in out.splitlines() if 'setstate at' in l])
    read = lat[0] - 128
    onset, set_, bad, ref = (runs[n][0] for n in ('onset', 'set', 'bad', 'ref'))
    took = [float(l.split(' in ')[1].split()[0]) for l in runs['set'][1]]
    print('setstate: %s; corrupt: %s' % (runs['set'][1], runs['bad'][1]))
    check(took and 'ok' in runs['set'][1][0] and took[0] < 1.0, 'setState %r' % runs['set'][1])
    check(set_[:2 * at] == onset[:2 * at], 'the output before the setState changed')
    check(not any(set_[2 * at:2 * (at + read)]), 'sound in the read offset after the setState')
    check(set_[2 * at:] == ref[2 * at:], 'after the setState the output is not the groove card\'s')
    loud = sum(1 for v in set_[2 * at::2] if abs(v) >= 4000)
    check(set_[2 * at:] != onset[2 * at:] and loud > 1000, 'after the setState: %d loud frames' % loud)
    check(runs['bad'][1] and 'refused' in runs['bad'][1][0] and bad == onset, 'the corrupt state %r' % runs['bad'][1])
    # the logs of the runs with a restart, set and ref: two engines each, on two card folders
    logs = [x for x in instance_logs(t.path('home')).values() if 'a new card' in x]
    cards = [re.findall(r'started on (\S+)', x) for x in logs]
    print('the logs with a new card: %s' % [[os.path.basename(os.path.dirname(c)) for c in k] for k in cards])
    check(len(logs) == 2 and all(len(k) == 2 and k[0] != k[1] for k in cards)
          and all(re.search(r'ended \(signal 9\): a new card', x) for x in logs), 'the logs %r' % logs)
    print('setState at %d: the old output up to it, %d frames of silence, then == the groove card\'s (%d loud frames); '
          'the corrupt state refused, nothing changed' % (at, read, loud))
    check(not instance_dirs(t.path('home')), 'instance folders left: %r' % instance_dirs(t.path('home')))


@case
def P10_plugin_stale(t):
    # P-18: a folder of BASE left by a host that died (its lock held by nobody, older than a minute) is removed by the
    # next instance, also one without its lock file (a removal that stopped partway); one whose lock an instance holds
    # is kept however old, and so is a young one (an instance being made); the new instance's own folder goes at its end
    home = t.path('home')
    names = {'stale': 'a' * 16, 'held': 'b' * 16, 'young': 'c' * 16, 'no lock': 'd' * 16}
    for k, n in names.items():
        os.makedirs(os.path.join(home, n, 'card', 'TMP'))
        with open(os.path.join(home, n, 'card', 'TMP', 'X.BIN'), 'w') as f:
            f.write('x')
        if k != 'no lock':
            open(os.path.join(home, n, 'lock'), 'w').close()
    old = time.time() - 120
    for k in ('stale', 'held', 'no lock'):
        os.utime(os.path.join(home, names[k]), (old, old))
    with open(os.path.join(home, names['held'], 'lock'), 'r+') as held:
        fcntl.flock(held, fcntl.LOCK_EX)
        host(t, plan(t, 's.txt', ['out ' + t.path('s.wav')], frames=12800))
    left = sorted(instance_dirs(home))
    print('left in BASE: %s (%s)' % (left, ', '.join('%s %s' % (k, n[:4]) for k, n in names.items())))
    check(left == sorted([names['held'], names['young']]), 'left %r' % left)
    for k in ('held', 'young'):
        check(os.path.isfile(os.path.join(home, names[k], 'card', 'TMP', 'X.BIN')), 'the %s folder\'s card' % k)
    # a stale folder that cannot be removed whole (a folder in it without write permission) keeps its lock, which
    # is removed last, and goes with the next instance once it can
    part = os.path.join(home, 'e' * 16)
    os.makedirs(os.path.join(part, 'card', 'RO'))
    for f in (os.path.join(part, 'card', 'RO', 'X.BIN'), os.path.join(part, 'card', 'Y.BIN'), os.path.join(part, 'lock')):
        open(f, 'w').close()
    os.chmod(os.path.join(part, 'card', 'RO'), 0o555)
    os.utime(part, (old, old))
    try:
        host(t, plan(t, 's2.txt', ['out ' + t.path('s2.wav')], frames=12800))
        kept = sorted(os.listdir(part)) if os.path.isdir(part) else None
    finally:
        os.chmod(os.path.join(part, 'card', 'RO'), 0o755)
    os.utime(part, (old, old))
    host(t, plan(t, 's3.txt', ['out ' + t.path('s3.wav')], frames=12800))
    print('a stale folder removed partway keeps %r; the next instance removes it: %s' % (kept, not os.path.exists(part)))
    check(kept is not None and 'lock' in kept, 'the folder removed partway kept %r' % kept)
    check(not os.path.exists(part), 'the folder removed partway is still there')


@case
def P10_plugin_policy(t):
    # P-15: the engine's thread takes the time-constraint policy when the host plays in real time (ps -M: 97R, as
    # P6_engine_paced) and not offline (31T), the host paced in both
    for mode in ('realtime', 'offline'):
        p = None
        try:
            p = subprocess.Popen([t.a.host, plan(t, mode + '.txt', ['mode ' + mode, 'paced', 'play 4800 120 0',
                                                                    'out ' + t.path(mode + '.wav')], rate=48000, block=256,
                                                 frames=48000 * 3)],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=host_env(t))
            time.sleep(1.5)
            kids = subprocess.run(['pgrep', '-P', str(p.pid)], capture_output=True, text=True).stdout.split()
            check(len(kids) == 1, '%s: the host\'s children %r' % (mode, kids))
            rows = subprocess.run(['ps', '-M', '-p', kids[0]], capture_output=True, text=True).stdout.split('\n')
            pri = rows[1].split()[5] if len(rows) > 1 and len(rows[1].split()) > 5 else '?'
            out, err = p.communicate(timeout=120)
            check(p.returncode == 0, '%s: the host exited %d: %s%s' % (mode, p.returncode, out, err))
        finally:
            if p is not None and p.poll() is None:
                p.kill()
                p.wait()
        print('%s: the engine\'s main thread at priority %s' % (mode, pri))
        if mode == 'realtime':
            check(pri.endswith('R') and pri[:-1].isdigit() and int(pri[:-1]) >= 90, 'real time: %r' % rows[:2])
        else:
            check(pri.endswith('T') and pri[:-1].isdigit() and int(pri[:-1]) < 50, 'offline: %r' % rows[:2])


def bundle_copy(t, name):
    """a copy of the bundle in the case's folder (the plugin finds its engine next to itself), and its helper's path"""
    dst = t.path(name)
    shutil.copytree(t.a.bundle, dst, symlinks=True)
    return dst, os.path.join(dst, 'Contents', 'Helpers', 'lichomat-engine')


@case
def P10_plugin_spawn_fail(t):
    # The engine cannot be started (its file is missing): the instance reports "failed", and a state given later
    # (Live's preset load) starts an engine on the new card once the file is back, and setState returns at once.
    # Real time, paced, 48 kHz: the helper is put back once the log has the failure, the state given at 2 s; a Note
    # On every 0.25 s from 2.1 s on sounds
    bundle, helper = bundle_copy(t, 'LICHOMAT.vst3')
    os.remove(helper)
    host(t, plan(t, 'make.txt', ['out ' + t.path('make.wav'), 'state-out ' + t.path('onset.bin')], frames=12800))
    at = 96000
    lines = ['mode realtime', 'paced', 'out ' + t.path('f.wav'), 'setstate %d %s' % (at, t.path('onset.bin'))]
    lines += ['note %d 0 48 127' % (at + 4800 + 12000 * k) for k in range(8)]
    p = None
    try:
        p = subprocess.Popen([t.a.host, plan(t, 'f.txt', lines, rate=48000, block=256, frames=48000 * 4, bundle=bundle)],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=host_env(t))
        t0 = time.monotonic()
        while not any('cannot start the engine' in x for x in instance_logs(t.path('home')).values()):
            check(p.poll() is None and time.monotonic() - t0 < 1.8, 'no spawn failure in the log in time')
            time.sleep(0.02)
        shutil.copy2(os.path.join(t.a.bundle, 'Contents', 'Helpers', 'lichomat-engine'), helper)
        out, err = p.communicate(timeout=120)
        check(p.returncode == 0, 'the host exited %d: %s%s' % (p.returncode, out, err))
    finally:
        if p is not None and p.poll() is None:
            p.kill()
            p.wait()
    print(out.strip())
    log = ''.join(x for x in instance_logs(t.path('home')).values() if 'cannot start the engine' in x)
    print(log.strip())
    took = [float(l.split(' in ')[1].split()[0]) for l in out.splitlines() if 'setstate at' in l]
    _, _, x = wav(t.path('f.wav'))
    loud = sum(1 for v in x[2 * at::2] if abs(v) >= 4000)
    print('setState after the failure: %s s; %d loud frames after it' % (took, loud))
    check(took and took[0] < 1.0, 'setState took %r s' % took)
    check(re.search(r'cannot start the engine: posix_spawn .*: No such file or directory', log)
          and re.search(r'engine \d+ started on ', log), 'the log')
    check(loud > 1000 and 'engine running' in out, 'no engine on the new card: %d loud frames, %r' % (loud, out))


def longest_silence(x):
    best = run = 0
    for v in x:
        run = run + 1 if v == 0 else 0
        best = max(best, run)
    return best


@case
def P10_plugin_kill(t):
    # real time, paced, 48 kHz: the engine killed at 3 s; the plugin goes silent, starts a new engine, and plays
    # again within 1.5 s; the host is not disturbed
    lines = ['mode realtime', 'paced', 'play 4800 120 0', 'kill 144000', 'out ' + t.path('kill.wav')]
    lines += ['note %d 0 48 127' % (36000 * k + 1000) for k in range(13)]
    host(t, plan(t, 'kill.txt', lines, rate=48000, block=256, frames=480000))
    _, _, x = wav(t.path('kill.wav'))
    left = x[0::2]
    silent = longest_silence(left[144000:240000])
    back = next((i for i in range(144000 + silent, len(left)) if abs(left[i]) >= 4000), None)
    print('silent %d frames after the kill, sound again at %.2f s' % (silent, (back or 0) / 48000))
    check(silent >= 4800 and back is not None and back < 144000 + 72000, 'silent %d, back at %r' % (silent, back))


def beats_through(t, what, at, frames):
    """Real time, paced, 48 kHz, blocks of 256: the init card's four on the floor as the slave of a transport playing
    from 0 at 120 BPM, no notes, the plan line what ('kill' or 'hang') at frame at. Prints and returns (the latency,
    the onsets before at + latency, their beats, the onsets after, the beats from the first one back on, the log)."""
    init = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'init')
    lines = ['card ' + init, 'mode realtime', 'paced', 'play 0 120 0', '%s %d' % (what, at), 'out ' + t.path('beats.wav')]
    lat, _ = host(t, plan(t, 'beats.txt', lines, rate=48000, block=256, frames=frames, card=False))
    _, _, x = wav(t.path('beats.wav'))
    on = attacks(x[0::2])
    beats = [b + lat[0] for b in beat_frames(48000.0, 256, frames, 120.0, frames, 120.0, float('inf'))]
    before = [o for o in on if o < at + lat[0]]
    after = [o for o in on if o >= at + lat[0]]
    want = [w for w in beats if w < at + lat[0]]
    back = [w for w in beats if after and w >= after[0] - 1 and w < frames - 64]
    print('latency %d; %d onsets before the %s (%d beats), the first after it at %.3f s, %d after (%d beats)'
          % (lat[0], len(before), what, len(want), (after[0] if after else 0) / 48000, len(after), len(back)))
    print('onset - (beat + latency): before %r, after %r' % (sorted(set(o - w for o, w in zip(before, want))),
                                                            sorted(set(o - w for o, w in zip(after, back)))))
    log = ''.join(instance_logs(t.path('home')).values())
    print('the log: %s' % '; '.join(re.findall(r'engine \d+ ended \([^)]*\): [^;]*; underruns \d+', log)))
    return lat[0], before, want, after, back, log


@case
def P10_plugin_beats(t):
    # real time, paced, 48 kHz, blocks of 256: the init card's four on the floor as the slave of a playing transport,
    # no notes, the engine killed at 3 s. The plugin starts the new engine's slave afresh (Song Position and
    # Continue where the transport is), so the beats come back within 1.5 s of the kill: every beat before the kill
    # sounds at its frame plus the latency, -1 to +1 (P10_plugin_onsets), and every one from the first back, -2 to
    # +2: the voices start on every second engine frame, 2.18 host frames at 48 kHz, and the new engine's epoch puts
    # that grid anywhere among the host's frames (the first engine's is frame 0). The underruns in the instance's log
    # tell a busy machine from a fault
    kill = 144000
    _, before, want, after, back, _ = beats_through(t, 'kill', kill, 48000 * 6)
    check(len(before) == len(want) and all(-1 <= o - w <= 1 for o, w in zip(before, want)), 'before the kill: %r' % before)
    check(after and after[0] < kill + 72000, 'no beat within 1.5 s of the kill: %r' % after[:3])
    check(len(after) == len(back) and all(-2 <= o - w <= 2 for o, w in zip(after, back)), 'after the kill: %r, beats %r'
          % (after, back))


@case
def P10_plugin_hang(t):
    # P-16, a hang: the engine stopped (SIGSTOP) at 3 s, as a firmware that hangs. The monitor sees it take no events
    # while the plugin publishes them, kills it 2 s later and starts a new one after 0.5 s: the beats come back within
    # 4 s of the hang, on their frames as in P10_plugin_beats, and the log says why the engine ended
    hang = 144000
    _, before, want, after, back, log = beats_through(t, 'hang', hang, 48000 * 8)
    check(len(before) == len(want) and all(-1 <= o - w <= 1 for o, w in zip(before, want)), 'before the hang: %r' % before)
    check(after and hang + 96000 < after[0] < hang + 192000, 'the first beat after the hang: %r' % after[:3])
    check(len(after) == len(back) and all(-2 <= o - w <= 2 for o, w in zip(after, back)), 'after the hang: %r, beats %r'
          % (after, back))
    check(re.search(r'ended \(signal 9\): it hung \(2 s without taking events\)', log), 'the log: %r' % log)


@case
def P10_plugin_crashes(t):
    # P-16, a crash loop: real time, paced, 48 kHz, the engine killed every second from 1 s to 7 s. The plugin starts
    # a new engine 0.5 s or more after each death (the log's times), and after the fifth death within a minute gives
    # up: 5 engines, the Engine parameter "failed", silence from the fifth kill on
    lines = ['mode realtime', 'paced', 'play 4800 120 0', 'out ' + t.path('c.wav')] + ['kill %d' % (48000 * k) for k in range(1, 8)]
    lines += ['note %d 0 48 127' % (12000 * k + 1000) for k in range(40)]
    _, out = host(t, plan(t, 'c.txt', lines, rate=48000, block=256, frames=48000 * 8))
    log = ''.join(instance_logs(t.path('home')).values())
    print(log.strip())

    def when(line):
        h, m, sec = line.split()[1].split(':')
        return int(h) * 3600 + int(m) * 60 + float(sec)
    lines = log.splitlines()
    started = [when(l) for l in lines if re.search(r' engine \d+ started on ', l)]
    ended = [when(l) for l in lines if re.search(r' engine \d+ ended \(signal 9\): it died', l)]
    gaps = [s - e for e, s in zip(ended, started[1:])]
    _, _, x = wav(t.path('c.wav'))
    last = max((i for i in range(0, len(x), 2) if abs(x[i]) >= 4000), default=0) // 2
    print('%d engines started, %d killed; from each death to the next start %s s; the last sound at %.2f s'
          % (len(started), len(ended), ', '.join('%.3f' % g for g in gaps), last / 48000))
    check(len(started) == 5 and len(ended) == 5 and 'not started again: 5 deaths in a minute' in log, 'the log')
    check(len(gaps) == 4 and all(g >= 0.5 for g in gaps), 'the back-off: %r' % gaps)
    check('engine failed' in out, 'the Engine parameter: %r' % out)
    check(last < 48000 * 5 + 1000, 'sound after the fifth kill, at frame %d' % last)


@case
def P10_plugin_rt(t):
    # the audio thread under the interposer, paced in real time for 10 s at 48 kHz in blocks of 128: no allocation,
    # lock, wait, read or sleep inside process(), one write (the wake-up) per call
    lines = ['mode realtime', 'paced', 'rtcheck', 'out ' + t.path('rt.wav')] + TIMELINE
    _, out = host(t, plan(t, 'rt.txt', lines, rate=48000, block=128, frames=480000),
                  env={'DYLD_INSERT_LIBRARIES': t.a.rtcheck})
    line = [l for l in out.splitlines() if 'rtcheck:' in l and 'process calls' in l]
    check(line, 'no rtcheck line in %r' % out)
    w = line[0].split()
    calls, bad, writes = int(w[2]), int(w[5]), int(w[8])
    check(calls == 3750 and bad == 0 and writes == calls, line[0])


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
