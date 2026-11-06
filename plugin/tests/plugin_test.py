#!/usr/bin/env python3
"""The LICHOMAT plugin's scenario tests (plan E9.2): the engine through plug_drive against
lichomat-render, (from Task 9) the VST3 bundle through lichomat-plughost, and (plan E9.7) the AU
through lichomat-auhost, which plays the same plans.

    plugin_test.py --root R --build B --render R --engine E --drive D [--host H --bundle V]
                   [--auhost A --component C] --work W CASE

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
import plistlib
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

    def render(self, card, script, seconds, out, stereo='sum'):
        """lichomat-render as the engine runs it: --stereo sum, out1 or out2, or None for its four channels"""
        self.run([self.a.render, '--card', card, '--bpm', 'mid', '--no-start', '--as-booted']
                 + (['--stereo', stereo] if stereo else []) + ['--seconds', str(seconds), '--events', script, '--out', out])

    def drive(self, card, rate, block, script, frames, out, realtime=None, preroll=False, split=False, values=None):
        cmd = [self.a.drive, self.a.engine, card, str(rate), str(block), script, str(frames), out]
        if realtime is not None:
            cmd += ['realtime', str(realtime)]
        if preroll:
            cmd += ['preroll']
        if split:
            cmd += ['split']
        if values:
            cmd += ['values', values]
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
    check(out.startswith('lichomat-engine 0.2.0, the engine of the LICHOMAT plugin\n'), 'the first line')
    check('Not affiliated with Erica Synths.' in out, 'the notices')
    # the commit it was built from, which the source line of the notices ("at the commit above") points to, as
    # lichomat-render prints it: a commit of the repository
    m = re.search(r'^Runs LICHOMAT [0-9.]+\. Built from commit ([0-9a-f]{12})\.$', out, re.MULTILINE)
    check(m, 'no "Built from commit" line')
    known = subprocess.run(['git', '-C', t.a.root, 'cat-file', '-e', m.group(1) + '^{commit}'], capture_output=True)
    check(known.returncode == 0, 'commit %s is not in the repository' % m.group(1))
    check(out.index(m.group(0)) < out.index('Source: '), 'the commit after the source line')
    p = subprocess.run([t.a.engine, '--bogus'], capture_output=True, text=True)
    check(p.returncode == 1 and p.stderr.startswith('usage: lichomat-engine'), 'usage: %d %r' % (p.returncode, p.stderr))


@case
def P6_engine_objects(t):
    # P-2: the engine's firmware, device and renderer objects are lichomat-render's own object files, built with its
    # flags, so the bit-exact cases compare the same code: every object the engine links that is not its own is one
    # that lichomat-render links
    eng = [o for o in ninja_inputs(t.a.build, 'lichomat-engine') if o.endswith('.o')]
    ren = set(ninja_inputs(t.a.build, 'emu/lichomat-render'))
    own = [o for o in eng if o.startswith(('CMakeFiles/lichomat-engine.dir/', 'CMakeFiles/plug_common_san.dir/',
                                          'CMakeFiles/eng_fw.dir/'))]
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
def P6_engine_refuses(t):
    # An engine run on shared memory of another layout refuses it with exit 2 and says why, before it touches
    # the card: (a) an object of this version's size whose header says version 1 (plug_shmCheck()), (b) an object
    # of the version-1 layout's size (plug_shmMap()). The file is fd 5, as the plugin's shared memory is.
    magic, v1_bytes = 0x5048434C, 327824
    full = 590040	# sizeof(plug_shm_t) of version 2 (P3_shm_create checks it)
    cases = [('v1header', full, struct.pack('<III', magic, 1, full),
              'version 1 size %d, this engine needs version 2 size %d' % (full, full)),
             ('v1size', v1_bytes, struct.pack('<III', magic, 1, v1_bytes),
              'an object of %d bytes, this engine needs version 2 size %d' % (v1_bytes, full))]
    for name, size, head, said in cases:
        card = t.card(name)
        shm = t.path(name + '.shm')
        with open(shm, 'wb') as f:
            f.write(head)
            f.truncate(size)
        wake_r, wake_w = os.pipe()
        life_r, life_w = os.pipe()
        fd = os.open(shm, os.O_RDWR)
        src = (wake_r, life_w, fd)

        def place():
            high = [fcntl.fcntl(x, fcntl.F_DUPFD, 10) for x in src]	# clear of 3, 4, 5, then onto them
            for want, h in zip((3, 4, 5), high):
                os.dup2(h, want)
            for h in high:
                os.close(h)

        p = None
        try:
            p = subprocess.Popen([t.a.engine, '--card', card], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True, pass_fds=(3, 4, 5), preexec_fn=place)
            try:
                out, err = p.communicate(timeout=20)
            except subprocess.TimeoutExpired:
                raise Fail('%s: the engine did not end within 20 s' % name)
            check(p.returncode == 2, '%s: exit %r, not 2:\n%s%s' % (name, p.returncode, out, err))
            check(said in err, '%s: stderr does not say "%s":\n%s' % (name, said, err))
            print('%s: exit 2, "%s"' % (name, err.strip()))
        finally:
            kill_all([p], card)
            for x in src:
                os.close(x)
            os.close(wake_w)
            os.close(life_r)


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


# --- the two outputs and the parameters in the engine (plan E9.4) ----------------------------------

def channels(x, n, k):
    """channel k of the interleaved samples x of n channels"""
    return x[k::n]


@case
def P6_engine_split(t):
    # the engine's four channels: plug_drive's split WAV at 44,140.625 Hz is lichomat-render's four-channel WAV (L1,
    # R1, L2, R2) byte for byte, blocks of 128 and 37; the onset card plays on both outputs (Drum 1 on out 1, the
    # voices on out 2)
    script = write_script(t, 'script.txt')
    frames = blocks_frames(6)
    t.render(t.card('render'), script, 6, t.path('render4.wav'), stereo=None)
    _, n, x = wav(t.path('render4.wav'))
    loud = [sum(1 for v in channels(x, 4, k) if abs(v) >= 4000) for k in range(4)]
    print('the render: %d channels, samples at 4,000 or more per channel %r' % (n, loud))
    check(n == 4 and loud[0] > 1000 and loud[2] > 1000 and loud[3] > 1000, 'both outputs loud: %r' % loud)
    for b in (128, 37):
        out = t.path('split-%d.wav' % b)
        s = t.drive(t.card('s%d' % b), RATE_TEXT, b, script, frames, out, split=True)
        check(s.get('late') == 0 and s.get('dropped') == 0, 'blocks of %d: counters %r' % (b, s))
        d = same_files(t.path('render4.wav'), out)
        check(d is None, 'blocks of %d: the four channels %s' % (b, d))
    print('blocks of 128, 37: the four channels == render')


@case
def P7_engine_split(t):
    # 48 kHz: the engine converts the four channels at once; each output is the renderer's out1 and out2 through the
    # same converter two channels at a time (plug_resample_wav) within 2 LSB, as P7_engine_resampled's sum
    seconds, rate = 6, 48000
    frames = int(blocks_frames(seconds) * fractions.Fraction(rate) / RATE) - 256
    t.drive(t.card('d'), str(rate), 256, write_script(t, 'drive.txt', rate), frames, t.path('drive4.wav'), split=True)
    _, n, d = wav(t.path('drive4.wav'))
    check(n == 4, '%d channels' % n)
    for k, out in ((0, 'out1'), (2, 'out2')):
        t.render(t.card('r' + out), write_script(t, 'render-%s.txt' % out, rate, render=True), seconds,
                 t.path('render-%s.wav' % out), stereo=out)
        t.run([t.a.resample, t.path('render-%s.wav' % out), str(rate), t.path('ref-%s.wav' % out), str(frames)])
        _, _, ref = wav(t.path('ref-%s.wav' % out))
        a = [v for pair in zip(channels(d, 4, k), channels(d, 4, k + 1)) for v in pair]
        check(len(a) == len(ref), '%s: %d and %d samples' % (out, len(a), len(ref)))
        worst = max(abs(x - y) for x, y in zip(a, ref))
        loud = sum(1 for x in a if abs(x) >= 4000)
        print('%s at %d Hz: %d frames, %d samples at 4,000 or more, the largest difference %d LSB' % (out, rate, frames, loud, worst))
        check(worst <= 2 and loud > 1000, '%s: the largest difference %d LSB' % (out, worst))


# a script of the plugin's parameters as MIDI on channel 1 (the fixture cards' global channel, 0): Drum 1's volume
# (CC 89) and filter (CC 38) and three voices' decays (CC 51, 53, 57), the DJ filter (NRPN 96) and the reverb level
# (NRPN 98) at their frames, then the pattern (Program Change 1): the next pattern at once, played from the end of
# the one that plays
PARAM_SCRIPT = ['30000f usb B0 59 28', '52000f usb B0 26 30', '60000f usb B0 63 00 B0 62 60 B0 06 14',
                '70000f usb B0 33 7F B0 35 7F B0 39 7F', '90000f usb B0 63 00 B0 62 62 B0 06 70', '200000f usb C0 01']


def param_script(t, name, lines):
    """write_script()'s script and lines merged, one line per frame"""
    merged = {}
    for l in open(write_script(t, name + '.base', None)).read().split('\n') + lines:
        if l.strip():
            f, rest = l.split(' usb ')
            merged.setdefault(int(f[:-1]), []).append(rest)
    p = t.path(name)
    with open(p, 'w') as f:
        f.write(''.join('%df usb %s\n' % (k, ' '.join(merged[k])) for k in sorted(merged)))
    return p


@case
def P6_engine_params(t):
    # The parameters reach the firmware as MIDI at their frames, as from an external controller: with PARAM_SCRIPT the
    # engine is lichomat-render byte for byte, and the output differs from the script without them. The engine's
    # values (plug_drive's values file) show each change within 512 frames of its frame: Drum 1's volume 40 from
    # frame 30,000, its filter 48 from 52,000, the DJ filter 20 from 60,000, the reverb level 112 from 90,000 and
    # the next pattern 1 from 200,000; the firmware's global channel is 0, its receive filter 15 (all)
    script = param_script(t, 'script.txt', PARAM_SCRIPT)
    plain = write_script(t, 'plain.txt')
    frames = blocks_frames(6)
    t.render(t.card('render'), script, 6, t.path('render.wav'))
    t.render(t.card('plain'), plain, 6, t.path('plain.wav'))
    s = t.drive(t.card('drive'), RATE_TEXT, 128, script, frames, t.path('drive.wav'), values=t.path('values.txt'))
    check(s.get('late') == 0 and s.get('dropped') == 0, 'counters %r' % s)
    d = same_files(t.path('render.wav'), t.path('drive.wav'))
    check(d is None, 'the engine and the renderer %s' % d)
    check(same_files(t.path('render.wav'), t.path('plain.wav')) is not None, 'the parameters changed nothing')
    rows = []
    for line in open(t.path('values.txt')):
        w = line.split()
        rows.append((int(w[0][:-1]), [int(v) for v in w[1:-4]], int(w[-3]), int(w[-1])))
    print('%d changes of the values; the first at frame %d: %r, channel %d, rx %d' % (len(rows), rows[0][0], rows[0][1],
                                                                                   rows[0][2], rows[0][3]))

    def first(index, value, after):
        return next((f for f, v, _, _ in rows if f >= after and v[index] == value), None)
    seen = {'Drum 1 Volume': (1, 40, 30000), 'Drum 1 Filter': (13, 48, 52000), 'DJ Filter': (26, 20, 60000),
            'Reverb Level': (27, 112, 90000), 'Pattern': (0, 1, 200000)}
    for name, (i, v, at) in sorted(seen.items()):
        f = first(i, v, at)
        print('%s = %d: from frame %s (sent at %d)' % (name, v, f, at))
        check(f is not None and f - at <= 512, '%s: %r' % (name, f))
    check(rows[0][2] == 0 and rows[0][3] == 15, 'channel and filter %r' % (rows[0][2:],))


@case
def P6_engine_late_epoch(t):
    # The values' frames are the host's: with a pre-roll the epoch is above 0, the engine's first report is at the
    # epoch, and each change of P6_engine_params' script shows within 512 frames of epoch + its frame
    script = param_script(t, 'script.txt', PARAM_SCRIPT)
    frames = blocks_frames(6)
    s = t.drive(t.card('late'), RATE_TEXT, 128, script, frames, t.path('late.wav'), preroll=True,
                values=t.path('values.txt'))
    epoch = s.get('epoch', 0)
    check(epoch > 0 and s.get('late') == 0, 'counters %r' % s)
    rows = []
    for line in open(t.path('values.txt')):
        w = line.split()
        rows.append((int(w[0][:-1]), [int(v) for v in w[1:-4]]))
    print('the epoch %d; the first values at frame %d' % (epoch, rows[0][0]))
    check(rows[0][0] == epoch, 'the first values at frame %d, not the epoch %d' % (rows[0][0], epoch))
    seen = {'Drum 1 Volume': (1, 40, 30000), 'Drum 1 Filter': (13, 48, 52000), 'DJ Filter': (26, 20, 60000),
            'Reverb Level': (27, 112, 90000), 'Pattern': (0, 1, 200000)}
    for name, (i, v, at) in sorted(seen.items()):
        f = next((f for f, vs in rows if f >= epoch + at and vs[i] == v), None)
        print('%s = %d: from frame %s (sent at %d + epoch %d)' % (name, v, f, at, epoch))
        check(f is not None and f - (epoch + at) <= 512, '%s: %r' % (name, f))


@case
def P6_engine_channel(t):
    # The card's own global MIDI channel and receive filter, not the fixtures' (channel 1, all): glo.cfg of a copy
    # holds the global channel 10 (stored as 10, the firmware's channel 9) and a transmit filter 11 that is not the
    # receive filter 5. The engine reports channel 9 and rx 5, and a CC on channel 10 reaches the firmware: Drum 1's
    # volume 40 shows from its frame
    card = t.card('chan')
    glo = os.path.join(card, 'PROJ00', 'GLO.CFG')
    with open(glo, 'rb') as f:
        b = bytearray(f.read())
    check(len(b) == 30 and b[14] == 15 and b[15] == 15 and b[21] == 0, 'glo.cfg is not the fixture\'s: %r' % bytes(b))
    b[14], b[15], b[21] = 11, 5, 10	# transmit filter, receive filter, global channel
    with open(glo, 'wb') as f:
        f.write(b)
    script = param_script(t, 'script.txt', ['30000f usb B9 59 28'])
    t.drive(card, RATE_TEXT, 128, script, blocks_frames(3), t.path('chan.wav'), values=t.path('values.txt'))
    rows = []
    for line in open(t.path('values.txt')):
        w = line.split()
        rows.append((int(w[0][:-1]), [int(v) for v in w[1:-4]], int(w[-3]), int(w[-1])))
    print('channel %d, rx %d; Drum 1 Volume %d at the end' % (rows[0][2], rows[0][3], rows[-1][1][1]))
    check(all(r[2] == 9 and r[3] == 5 for r in rows), 'channel and receive filter %r' % ([r[2:] for r in rows],))
    f = next((f for f, v, _, _ in rows if v[1] == 40), None)
    check(f is not None and 0 <= f - 30000 <= 512, 'Drum 1 Volume 40 from frame %r' % f)


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
    # a stale folder that cannot be removed whole keeps its lock, which is removed last, and goes with the next
    # instance once it can. Its only entry that cannot be removed is a folder without write permission listed after
    # the lock (the folder's own order, which the removal follows), so that the lock would go first in that order
    part = os.path.join(home, 'e' * 16)
    os.makedirs(os.path.join(part, 'card'))
    os.makedirs(os.path.join(part, 'zz'))
    for f in (os.path.join(part, 'card', 'Y.BIN'), os.path.join(part, 'zz', 'X.BIN'), os.path.join(part, 'lock')):
        open(f, 'w').close()
    order = os.listdir(part)
    check(order.index('lock') < order.index('zz'), 'the folder lists %r: zz before the lock' % order)
    os.chmod(os.path.join(part, 'zz'), 0o555)
    os.utime(part, (old, old))
    try:
        host(t, plan(t, 's2.txt', ['out ' + t.path('s2.wav')], frames=12800))
        kept = sorted(os.listdir(part)) if os.path.isdir(part) else None
    finally:
        os.chmod(os.path.join(part, 'zz'), 0o755)
    os.utime(part, (old, old))
    host(t, plan(t, 's3.txt', ['out ' + t.path('s3.wav')], frames=12800))
    print('a stale folder removed partway keeps %r; the next instance removes it: %s' % (kept, not os.path.exists(part)))
    check(kept is not None and 'lock' in kept and 'zz' in kept, 'the folder removed partway kept %r' % kept)
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
    # The engine cannot be started (its file is missing): the instance reports "failed". A state given while the file
    # is still missing (Live's preset load) is taken at once, the start on its card fails again and the instance stays
    # failed, silent; one given once the file is back starts an engine on the new card, and setState returns at once.
    # Real time, paced, 48 kHz: states at 1 s and at 3 s, the helper put back once the log has the second failure; a
    # Note On every 0.25 s from 1.1 s on, silent until the engine runs
    bundle, helper = bundle_copy(t, 'LICHOMAT.vst3')
    os.remove(helper)
    host(t, plan(t, 'make.txt', ['out ' + t.path('make.wav'), 'state-out ' + t.path('onset.bin')], frames=12800))
    at1, at2 = 48000, 144000
    lines = ['mode realtime', 'paced', 'out ' + t.path('f.wav'), 'setstate %d %s' % (at1, t.path('onset.bin')),
             'setstate %d %s' % (at2, t.path('onset.bin'))]
    lines += ['note %d 0 48 127' % (at1 + 4800 + 12000 * k) for k in range(16)]
    p = None

    def failures():
        return sum(x.count('cannot start the engine') for x in instance_logs(t.path('home')).values())
    try:
        p = subprocess.Popen([t.a.host, plan(t, 'f.txt', lines, rate=48000, block=256, frames=48000 * 5, bundle=bundle)],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=host_env(t))
        t0 = time.monotonic()
        while failures() < 2:
            check(p.poll() is None and time.monotonic() - t0 < 2.8, 'not two spawn failures in the log in time')
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
    between = sum(1 for v in x[2 * at1:2 * at2:2] if abs(v) >= 4000)
    loud = sum(1 for v in x[2 * at2::2] if abs(v) >= 4000)
    print('setState with the file missing, then back: %s s; loud frames between them %d, after the second %d'
          % (took, between, loud))
    check(len(took) == 2 and all(w < 1.0 for w in took), 'setState took %r s' % took)
    lines = log.splitlines()
    fails = [i for i, l in enumerate(lines) if 'cannot start the engine: posix_spawn' in l and 'No such file or directory' in l]
    starts = [i for i, l in enumerate(lines) if re.search(r' engine \d+ started on ', l)]
    check(len(fails) == 2 and len(starts) == 1 and starts[0] > fails[1], 'the log')
    check(between == 0, 'sound while the engine could not start: %d loud frames' % between)
    check(loud > 1000 and 'engine running' in out, 'no engine on the new card: %d loud frames, %r' % (loud, out))


@case
def P10_plugin_nonblock(t):
    # The wake pipe's write end is non-blocking: with no engine to read it the pipe fills (64 KB, a byte a block),
    # and a blocking write() would stop the audio thread for good. A copy of the bundle whose helper exits at once:
    # 5 deaths, the instance "failed"; then 100,000 blocks of 16 frames in real time, unpaced, must go through
    bundle, helper = bundle_copy(t, 'LICHOMAT.vst3')
    with open(helper, 'w') as f:
        f.write('#!/bin/sh\nexit 1\n')
    os.chmod(helper, 0o755)
    pl = plan(t, 'n.txt', ['mode realtime', 'out ' + t.path('n.wav')], rate=48000, block=16, frames=16 * 100000,
              bundle=bundle)
    t0 = time.monotonic()
    try:
        p = subprocess.run([t.a.host, pl], capture_output=True, text=True, timeout=60, env=host_env(t))
    except subprocess.TimeoutExpired:
        raise Fail('the host did not finish in 60 s: the wake-up write() blocks')
    took = time.monotonic() - t0
    log = ''.join(instance_logs(t.path('home')).values())
    print(p.stdout.strip())
    print('%d engines ended with exit 1; the host done in %.1f s' % (log.count('ended (exit 1): it died'), took))
    check(p.returncode == 0 and 'engine failed' in p.stdout, 'the host: %d %s%s' % (p.returncode, p.stdout, p.stderr))
    check(log.count('ended (exit 1): it died') == 5 and 'not started again: 5 deaths in a minute' in log, 'the log %r' % log)


@case
def P10_plugin_offline_hang(t):
    # Offline (Live's export, freeze) an engine that hangs must not hold the render for ever. The host offline but
    # paced (30 s of TIMELINE at 44,140.625 Hz, blocks of 128, so that the render outlasts the waits). The first engine
    # hangs while it plays: the host stops it (SIGSTOP) at 1 s. Every later one hangs while it boots: a copy of the
    # bundle whose helper is a script that, from its second start on, stops itself before it becomes the engine. The
    # monitor kills the first 2 s after it stops while the processor waits for it; each later one when the processor
    # has waited 10 s for it in vain, by its clock (logged: 10.0-10.3 s), and the processor makes no other wait for
    # that engine; after 3 such stalls the plugin gives up ("failed", at about 21 s) and the render goes on in silence.
    # Bounded: the host done within 34 s (its 30 s paced), no engine left
    bundle, helper = bundle_copy(t, 'LICHOMAT.vst3')
    real = os.path.join(t.a.bundle, 'Contents', 'Helpers', 'lichomat-engine')
    starts = t.path('starts')
    with open(helper, 'w') as f:
        f.write('#!/bin/sh\n# the first start is the engine; every later one stops itself first: a boot that hangs\n'
                'n=$(($(cat "%s" 2>/dev/null || echo 0) + 1))\necho $n > "%s"\n'
                'if [ $n -ge 2 ]; then kill -STOP $$; fi\nexec "%s" "$@"\n' % (starts, starts, real))
    os.chmod(helper, 0o755)
    pl = plan(t, 'h.txt', TIMELINE + ['paced', 'hang 44160', 'out ' + t.path('h.wav')], frames=blocks_frames(30),
              bundle=bundle)
    t0 = time.monotonic()
    try:
        p = subprocess.run([t.a.host, pl], capture_output=True, text=True, timeout=60, env=host_env(t))
    except subprocess.TimeoutExpired:
        p = None
    finally:
        # a stopped engine or script of this case still there (the host killed): SIGKILL
        for k in subprocess.run(['pgrep', '-f', t.path('')], capture_output=True, text=True).stdout.split():
            os.kill(int(k), signal.SIGKILL)
    took = time.monotonic() - t0
    check(p is not None, 'the host did not finish in 60 s')
    log = ''.join(instance_logs(t.path('home')).values())
    print(p.stdout.strip())
    print(log.strip())
    _, _, x = wav(t.path('h.wav'))
    loud = sum(1 for v in x[0::2] if abs(v) >= 4000)
    tail = x[-2 * 44140 * 10:]
    waits = [float(w) for w in re.findall(r'ended \(signal 9\): the plugin waited (\d+\.\d+) s for it', log)]
    started = len(re.findall(r'engine \d+ started on ', log))
    print('%d engines started; the vain waits %r s; the host done in %.1f s; %d loud frames, the last 10 s silent: %s'
          % (started, waits, took, loud, not any(tail)))
    # (the render's rest, in silence, may end before the give-up reaches the Engine parameter: the log tells it)
    check(p.returncode == 0, 'the host: %d %s%s' % (p.returncode, p.stdout, p.stderr))
    check(took < 34.0, 'the render took %.1f s' % took)
    check(started == 3 and log.count('ended (signal 9): it hung (2 s without taking events)') == 1 and len(waits) == 2
          and 'not started again: 3 stalls in 10 minutes' in log, 'the log %r' % log)
    # 10 s by the clock (not 100,000 sleeps of 0.1 ms, which last longer), as the processor measured it
    check(all(10.0 <= w <= 10.3 for w in waits), 'the vain waits lasted %r s' % waits)
    check(loud > 0 and not any(tail), 'the output: %d loud frames, the last 10 s %s' % (loud, 'silent' if not any(tail) else 'not silent'))


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
    # resampled, the converter rings before an attack: up to 6 frames at 500 or more before the first one at 1,000
    # (measured at 48 kHz), so the quiet run may end up to 8 frames before the onset, not 4
    on = attacks(x[0::2], ramp=8)
    beats = [b + lat[0] for b in beat_frames(48000.0, 256, frames, 120.0, frames, 120.0, float('inf'))]
    before = [o for o in on if o < at + lat[0]]
    after = [o for o in on if o >= at + lat[0]]
    want = [w for w in beats if w < at + lat[0]]
    # the beats from the one nearest the first onset back on (that onset may lie up to 2 frames either side of it)
    first = min(beats, key=lambda w: abs(w - after[0])) if after else None
    back = [w for w in beats if after and w >= first and w < frames - 64]
    print('latency %d; %d onsets before the %s (%d beats), the first after it at %.3f s, %d after (%d beats)'
          % (lat[0], len(before), what, len(want), (after[0] if after else 0) / 48000, len(after), len(back)))
    print('onset - (beat + latency): before %r, after %r' % (sorted(set(o - w for o, w in zip(before, want))),
                                                            sorted(set(o - w for o, w in zip(after, back)))))
    log = ''.join(instance_logs(t.path('home')).values())
    print('the log: %s' % '; '.join(re.findall(r'engine \d+ ended \([^)]*\): [^;]*; underruns \d+', log)))
    return lat[0], before, want, after, back, log


# The first two beats after a restart may come early, by up to 20 frames at 48 kHz and 120 BPM (measured 19 and 3, with
# the test host offline and the restart set by a setState at each 8th frame of a clock, so deterministic), when the new
# engine starts up to half a clock after a 16th: plug_clock then sends that 16th's Song Position and its clock at once
# (ceil(4p - 1/12), plug_clock.c:58), that clock up to half a clock late, and the firmware's slave times its steps
# from its clocks' fitted tempo (seq_core.c seqc_dueTime(): lastClockTime + (tick - lastClockPos) x cyclesPerTick), which
# the short first interval makes fast until the fit settles. The device does the same with that clock stream; the host
# mode of E9.3, which gives the sequencer the host's position instead of MIDI clock, has no such first interval.
AFTER_RESTART = (-20, -20)


def after_restart_ok(after, back):
    """the onsets from the first one back against their beats: the first two within AFTER_RESTART..+2, then -2..+2"""
    return len(after) == len(back) and all((AFTER_RESTART[i] if i < 2 else -2) <= o - w <= 2
                                           for i, (o, w) in enumerate(zip(after, back)))


@case
def P10_plugin_beats(t):
    # real time, paced, 48 kHz, blocks of 256: the init card's four on the floor as the slave of a playing transport,
    # no notes, the engine killed at 3 s. The plugin starts the new engine's slave afresh (Song Position and
    # Continue where the transport is), so the beats come back within 1.5 s of the kill: every beat before the kill
    # sounds at its frame plus the latency, -1 to +1 (P10_plugin_onsets), and every one from the first back, -2 to
    # +2: the voices start on every second engine frame, 2.18 host frames at 48 kHz, and the new engine's epoch puts
    # that grid anywhere among the host's frames (the first engine's is frame 0); the first two of them up to 20 frames
    # early (AFTER_RESTART). The underruns in the instance's log tell a busy machine from a fault
    kill = 144000
    _, before, want, after, back, _ = beats_through(t, 'kill', kill, 48000 * 6)
    check(len(before) == len(want) and all(-1 <= o - w <= 1 for o, w in zip(before, want)), 'before the kill: %r' % before)
    check(after and after[0] < kill + 72000, 'no beat within 1.5 s of the kill: %r' % after[:3])
    check(after_restart_ok(after, back), 'after the kill: %r, beats %r' % (after, back))


@case
def P10_plugin_hang(t):
    # P-16, a hang: the engine stopped (SIGSTOP) at 3 s, as a firmware that hangs. The monitor sees it take no events
    # while the plugin publishes them, kills it 2 s later and starts a new one after 0.5 s: the beats come back within
    # 4 s of the hang, on their frames as in P10_plugin_beats, and the log says why the engine ended
    hang = 144000
    _, before, want, after, back, log = beats_through(t, 'hang', hang, 48000 * 8)
    check(len(before) == len(want) and all(-1 <= o - w <= 1 for o, w in zip(before, want)), 'before the hang: %r' % before)
    check(after and hang + 96000 < after[0] < hang + 192000, 'the first beat after the hang: %r' % after[:3])
    check(after_restart_ok(after, back), 'after the hang: %r, beats %r' % (after, back))
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


# --- the two outputs and the parameters through the plugin (plan E9.4) ------------------------------

@case
def P10_plugin_buses(t):
    # Out 2 activated: at 44,140.625 Hz, offline, blocks of 128 and 100, the plugin's Out 1 is lichomat-render's
    # --stereo out1 and its Out 2 --stereo out2 for the MIDI the plugin gave its engine, from frame latency - 128 on,
    # byte for byte, both loud (the onset card: Drum 1 on out 1, the other voices on out 2). Out 1 without Out 2 is
    # the sum (P10_plugin_native)
    for block in (128, 100):
        script = t.path('script%d.txt' % block)
        out = t.path('plugin%d.wav' % block)
        lat, _ = host(t, plan(t, 'plan%d.txt' % block, TIMELINE + ['bus2', 'out ' + out, 'script ' + script], block=block))
        for name, wavname in (('out1', out), ('out2', out[:-4] + '-out2.wav')):
            t.render(t.card('%s%d' % (name, block)), script, 12, t.path('render-%s%d.wav' % (name, block)), stereo=name)
            _, _, a = wav(wavname)
            _, _, b = wav(t.path('render-%s%d.wav' % (name, block)))
            n, diff, loud = shifted_equal(a, b, lat[0] - 128)
            print('blocks of %d, %s: latency %d, %d frames compared, %d differ, %d samples not 0' % (block, name, lat[0], n,
                                                                                                     diff, loud))
            check(diff == 0 and loud > 10000, 'blocks of %d, %s: %d samples differ' % (block, name, diff))
            check(all(x == 0 for x in a[:2 * (lat[0] - 128)]), '%s: sound before the latency' % name)


@case
def P10_plugin_buses48(t):
    # Out 2 activated at 48 kHz, blocks of 512: Out 1 and Out 2 from plug_readOffset() on are the engine's four
    # channels through plug_drive (split) for the same MIDI, byte for byte
    script = t.path('s48.txt')
    out = t.path('p48.wav')
    host(t, plan(t, 'plan48.txt', TIMELINE + ['bus2', 'out ' + out, 'script ' + script], rate=48000, block=512,
                 frames=480000))
    t.drive(t.card('d48'), '48000', 512, script, 478000, t.path('d48.wav'), split=True)
    _, _, d = wav(t.path('d48.wav'))
    read = 512 + 128 + 3
    for k, wavname in ((0, out), (2, out[:-4] + '-out2.wav')):
        _, _, a = wav(wavname)
        b = [v for pair in zip(channels(d, 4, k), channels(d, 4, k + 1)) for v in pair]
        n, diff, loud = shifted_equal(a, b, read)
        print('Out %d at 48 kHz: %d frames compared, %d differ, %d samples not 0' % (k // 2 + 1, n, diff, loud))
        check(diff == 0 and loud > 10000, 'Out %d: %d differ' % (k // 2 + 1, diff))


def reports(out):
    """the parameter values the plugin reported, as [(frame, {id: value})], from plughost's lines"""
    rows = []
    for l in out.splitlines():
        m = re.match(r'lichomat-(?:plughost|auhost): instance 0: frame (\d+):((?: \d+=\d+)+)$', l)
        if m:
            rows.append((int(m.group(1)), dict((int(a), int(b)) for a, b in (w.split('=') for w in m.group(2).split()))))
    return rows


# the onset card stopped, a Note On of Drum 1's note every 20,000 frames, and the host's changes: Drum 1's volume
# (id 10) 40 at 30,000, its filter (30) 48 at 52,000, the DJ filter (50) 20 at 60,000, the reverb level (51) 112 at
# 90,000, then pattern 1 (id 1) at 100,000, which the firmware plays at once with its kit (stopped), and Drum 2's
# decay (41) as a ramp of a step every 1,000 frames from 120,000
PARAM_PLAN = (['note %d 0 48 127' % (20000 * k + 5000) for k in range(25)]
              + ['param 30000 10 40', 'param 52000 30 48', 'param 60000 50 20', 'param 90000 51 112', 'param 100000 1 1']
              + ['param %d 41 %d' % (120000 + 1000 * k, 20 + 3 * k) for k in range(30)])


@case
def P10_plugin_params(t):
    # The host's parameter changes reach the firmware at their frames: at 44,140.625 Hz, offline, blocks of 128, the
    # plugin's output is lichomat-render's for the MIDI the plugin gave its engine (plughost's script, which has the
    # CCs, NRPNs and the Program Change at the changes' frames), byte for byte. The plugin reports every value at its
    # first block (the host has none yet), then only those the firmware changed by itself: pattern 1's kit, loaded
    # with it, sets Drum 1's volume and filter and the reverb level back to the kit's, reported within 1,024 frames of
    # the load or, for a value the host gave less than a quarter of a second before (the reverb level at 90,000), of
    # the end of that time (plug_params.h, settle), each once; the values the host set are not reported back. With echo
    # (the host gives every reported value back to the plugin) the output and the reports are the same: no loop
    runs = {}
    for name, extra in (('plain', []), ('echo', ['echo'])):
        script = t.path(name + '.txt')
        lat, out = host(t, plan(t, name + '.plan', PARAM_PLAN + extra + ['out ' + t.path(name + '.wav'), 'script ' + script],
                                frames=blocks_frames(6)))
        runs[name] = (lat[0], reports(out), script)
    lat, rep, script = runs['plain']
    t.render(t.card('render'), script, 6, t.path('render.wav'))
    _, _, a = wav(t.path('plain.wav'))
    _, _, b = wav(t.path('render.wav'))
    n, diff, loud = shifted_equal(a, b, lat - 128)
    with open(script) as f:
        text = f.read()
    ccs, nrpns, pcs = text.count(' B0 '), text.count(' B0 63 '), text.count(' C0 ')
    print('latency %d, %d frames compared, %d differ; the script: %d CC messages (%d NRPNs), %d Program Changes'
          % (lat, n, diff, ccs, nrpns, pcs))
    check(diff == 0 and loud > 1000 and nrpns == 2 and pcs == 1 and ccs == 2 + 2 * 3 + 30, 'differ %d, %r' % (diff, (ccs, nrpns, pcs)))
    # the block a report reaches the host in depends on how far the engine has run when the plugin reads its table
    # (offline it runs ahead freely): the frames are bounded, the values exact, each parameter reported once
    settle = int(RATE / 4)
    for name in ('plain', 'echo'):
        rows = runs[name][1]
        later = {}
        for _, v in rows[1:]:
            later.update(v)
        ids = [i for _, v in rows[1:] for i in v]
        frames = [f for f, _ in rows[1:]]
        print('%s: the first report at frame %d, %d values; then %r at frames %r' % (name, rows[0][0], len(rows[0][1]),
                                                                                  later, frames))
        check(rows[0][0] == 0 and len(rows[0][1]) == 36, '%s: the first report %r' % (name, rows[:1]))
        check(later == {10: 127, 30: 127, 51: 0} and len(ids) == len(set(ids)), '%s: the later reports %r' % (name, rows[1:]))
        check(all(100000 <= f < 100000 + 1024 or 90000 + settle <= f < 90000 + settle + 1024 for f in frames),
              '%s: the later reports at frames %r' % (name, frames))
    _, _, e = wav(t.path('echo.wav'))
    check(e == a, 'with echo the output differs')
    print('with echo: the same output and the same reports')
    log = ''.join(instance_logs(t.path('home')).values())
    said = re.findall(r'parameters: [^\n]*', log)
    print('the logs: %r' % sorted(set(said)))
    check(len(said) == 2 and all(x == 'parameters: MIDI channel 1, receive filter: CC on, Program Change on' for x in said),
          'the logs %r' % said)


@case
def P10_plugin_outputs(t):
    # A voice moved to out 2 by its output parameter (id 60, Drum 1 Output, St2) on the init card, whose voices all
    # play on out 1: Out 2 silent before the change (the mixer takes it at once, not after the voice queue's 128
    # frames), then Drum 1, and Out 1 the others, each the renderer's, byte for byte
    init = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'init')
    out = t.path('init.wav')
    lines = ['card ' + init, 'bus2', 'play 0 120 0', 'param 100000 60 1', 'out ' + out, 'script ' + t.path('init.txt')]
    lat, _ = host(t, plan(t, 'init.plan', lines, card=False, frames=blocks_frames(6)))
    for name, wavname in (('out1', out), ('out2', out[:-4] + '-out2.wav')):
        r = t.path('render-%s.wav' % name)
        t.run([t.a.render, '--card', t.card('i' + name, 'init'), '--bpm', 'mid', '--no-start', '--as-booted', '--stereo', name,
               '--seconds', '6', '--events', t.path('init.txt'), '--out', r])
        _, _, a = wav(wavname)
        _, _, b = wav(r)
        n, diff, loud = shifted_equal(a, b, lat[0] - 128)
        early = sum(1 for v in a[:2 * (100000 + lat[0] - 128)] if v)  # the read offset: the mixer takes it at once
        print('init, Drum 1 to St2 at 100,000, %s: %d frames compared, %d differ, %d samples not 0 (%d before the change)'
              % (name, n, diff, loud, early))
        check(diff == 0 and loud > 10000, 'init, %s: %d differ' % (name, diff))
        check(name == 'out1' or early == 0, 'Out 2 before the change: %d samples' % early)


@case
def P10_plugin_params_restart(t):
    # A new engine (here after a kill) gets the host's values: real time, paced, 48 kHz, the init card's four on the
    # floor, every voice's volume 0 from frame 0 (before the transport plays): silent before the kill at 3 s and
    # silent after it, though the new engine starts on the card's kit (volume 127) and plays the beats again
    init = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'init')
    vols = ['param 0 %d 0' % i for i in range(10, 16)]
    lines = ['card ' + init, 'mode realtime', 'paced', 'play 4800 120 0', 'kill 144000', 'out ' + t.path('r.wav')]
    host(t, plan(t, 'quiet.txt', lines + vols, rate=48000, block=256, frames=48000 * 6, card=False))
    host(t, plan(t, 'loud.txt', lines[:-1] + ['out ' + t.path('l.wav')], rate=48000, block=256, frames=48000 * 6, card=False))
    _, _, q = wav(t.path('r.wav'))
    _, _, l = wav(t.path('l.wav'))
    before = sum(1 for v in q[:2 * 144000] if abs(v) >= 1000)
    after = sum(1 for v in q[2 * 144000:] if abs(v) >= 1000)
    loud = sum(1 for v in l[2 * 144000:] if abs(v) >= 1000)
    log = ''.join(instance_logs(t.path('home')).values())
    print('volumes 0: %d loud samples before the kill, %d after; without them %d after; engines started %d'
          % (before, after, loud, len(re.findall(r'engine \d+ started on ', log)) ))
    check(before == 0 and after == 0 and loud > 10000, 'before %d, after %d, without %d' % (before, after, loud))


def midi_lines(path):
    """a usb script (plughost's script or events) as [(frame, [message bytes])], each message from its status byte"""
    out = []
    for l in open(path):
        w = l.split()
        for b in (int(x, 16) for x in w[2:]):
            if b >= 0x80:
                out.append((int(w[0][:-1]), [b]))
            else:
                out[-1][1].append(b)
    return out


def restart_hold_midi(t, init, rate, block):
    """P10_plugin_restart_hold's restart at rate, at 2 s of 5: the MIDI the plugin gave its engines checked; the
    latency and the frame of the first clock after the restart. The output is run<rate>.wav"""
    at, frames, hold = 2 * rate, 5 * rate, math.ceil(rate * 1024 / 48000.0)
    sixteenth = rate // 8    # frames of a 16th at 120 BPM
    events = t.path('events%d.txt' % rate)
    lat, _ = host(t, plan(t, 'run%d.txt' % rate, ['card ' + init, 'play 0 120 0', 'setstate %d %s' % (at, t.path('quiet.bin')),
                                                  'note %d 0 48 0' % (at + 100), 'note %d 0 48 0' % (at + 3000),
                                                  'events ' + events, 'out ' + t.path('run%d.wav' % rate)],
                          card=False, rate=rate, block=block, frames=frames))
    ev = [(f, m) for f, m in midi_lines(events) if f >= at]
    values = [(f, m) for f, m in ev if m[0] & 0xF0 == 0xB0]
    first = values[0][0] if values else -1
    transport = [(f, m) for f, m in ev if m[0] in (0xF2, 0xFA, 0xFB, 0xFC, 0xF8)]
    resume = transport[0][0] if transport else -1
    spp = [m for f, m in transport if m[0] == 0xF2]
    six = spp[0][1] | spp[0][2] << 7 if spp else -1
    clock = next((f for f, m in transport if m[0] == 0xF8), -1)
    offs = [f for f, m in ev if m[0] & 0xF0 == 0x80]
    print('%d Hz: after the restart at %d: the first report %r at %r; Song Position %d and Continue at %d (+%d), the first clock '
          'at %d (16th %d at %d); Note Offs given at %r' % (rate, at, [m[1:] for f, m in values],
                                                              sorted(set(f for f, m in values)), six, resume,
                                                              resume - first, clock, six, six * sixteenth, offs))
    check(sorted(m[1:] for f, m in values) == [[89 + k, 120] for k in range(6)] and all(f == first for f, m in values)
          and first in (at, at + block), 'the first report %r' % values)
    check([m[0] for f, m in transport[:2]] == [0xF2, 0xFB] and transport[1][0] == resume and resume % block == 0,
          'the transport after the restart %r' % transport[:3])
    check(first + hold <= resume < first + hold + block, 'the Continue %d frames after the values, not %d' % (resume - first, hold))
    check(six == math.ceil(resume / float(sixteenth) - 1 / 12.0) and clock == six * sixteenth and clock >= first + hold,
          'the first clock at %d, 16th %d' % (clock, six))
    check(offs == [at + 3000], 'the Note Offs given %r' % offs)
    return lat, clock


@case
def P10_plugin_restart_hold(t):
    # A new engine's slave starts once the host's values it lacked have settled. Offline, 48 kHz, blocks of 256: the
    # init card's four on the floor as the slave of a transport playing from 0 at 120 BPM, and at 96,000 (a beat) a
    # state of the init card with its six volumes at 120 (the card's are 127) set while it plays (a restart, the new
    # engine started at once). The MIDI the plugin gave its engines (plughost's events, read from its ring) has the new
    # engine's first report, the six volumes, at the restart's first block; then nothing of the transport until the
    # first block at least 1,024 frames (21.3 ms) after them, which has the Song Position and Continue, and the clocks
    # from the 16th the transport has reached, the first on its frame; the host's notes in that time are left out (two
    # Note Offs: at +100 left out, at +3,000 given). The onsets after the restart are on their beats as in
    # P10_plugin_beats, the first on the first beat from that 16th on (values that silence voices would change which
    # voice's attack the onsets measure: Drum 1's alone comes 3-5 frames later, hold or not). Before the fix the Song
    # Position, the Continue and the 16th's clock came at the very frame of the volumes: the firmware played that beat
    # while it settled them. Then the same restart at 96 kHz (at 192,000), its MIDI alone: the hold is
    # ceil(rate x 1,024 / 48,000) frames, the same 21.3 ms, 2,048 frames there
    init = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'init')
    block = 256
    host(t, plan(t, 'make.txt', ['card ' + init, 'out ' + t.path('make.wav'), 'state-out ' + t.path('quiet.bin')]
                 + ['param 0 %d 120' % i for i in range(10, 16)], card=False, rate=48000, block=block, frames=12800))
    check(all(lchv(t.path('quiet.bin')).get(i) == 120 for i in range(10, 16)), 'the state\'s volumes')
    rate, at, frames = 48000, 96000, 48000 * 5
    lat, clock = restart_hold_midi(t, init, rate, block)
    _, _, x = wav(t.path('run48000.wav'))
    on = attacks(x[0::2], ramp=8)
    beats = [b + lat[0] for b in beat_frames(float(rate), block, frames, 120.0, frames, 120.0, float('inf'))]
    before = [o for o in on if o < at + lat[0]]
    after = [o for o in on if o >= at + lat[0]]
    want = [w for w in beats if w < at + lat[0]]
    nearest = min(beats, key=lambda w: abs(w - after[0])) if after else None
    back = [w for w in beats if after and w >= nearest and w < frames - 64]
    print('latency %d; onset - (beat + latency): before %r, after %r; the first after the restart at %r, the first beat '
          'from the 16th on at %d' % (lat[0], sorted(set(o - w for o, w in zip(before, want))),
                                      sorted(set(o - w for o, w in zip(after, back))), after[:1],
                                      min(w for w in beats if w >= clock + lat[0])))
    check(len(before) == len(want) and all(-1 <= o - w <= 1 for o, w in zip(before, want)), 'before: %r' % before)
    check(after_restart_ok(after, back), 'after the restart: %r, beats %r' % (after, back))
    check(nearest == min(w for w in beats if w >= clock + lat[0]), 'the first beat after the restart %r' % nearest)
    restart_hold_midi(t, init, 96000, block)
    # The first engine of an offline activation is not held (a bounce, an export, a freeze would lose its first 16th):
    # the state's six volumes owed to it (the init card's are 127), the transport playing from frame 0 (48 kHz): its
    # Start and first clock at frame 0, the volumes after them at that frame (the firmware settles them meanwhile: a
    # known limit). Held, the Start would be a Continue at 16th 1, a block at least 1,024 frames after the volumes
    first = t.path('events-first.txt')
    host(t, plan(t, 'first.txt', ['state-in ' + t.path('quiet.bin'), 'play 0 120 0', 'events ' + first,
                                  'out ' + t.path('first.wav')], card=False, rate=rate, block=block, frames=rate))
    ev = midi_lines(first)
    vols = sorted((f, m[1:]) for f, m in ev if m[0] & 0xF0 == 0xB0)
    clock = next((f for f, m in ev if m[0] == 0xF8), -1)
    print('the first engine, offline, owing six values: the first messages %r; the volumes %r; the first clock at %d'
          % (ev[:4], vols, clock))
    check(ev[:2] == [(0, [0xFA]), (0, [0xF8])] and clock == 0, 'the Start and the first clock %r' % ev[:3])
    check(vols == [(0, [89 + k, 120]) for k in range(6)], 'the volumes %r' % vols)


@case
def P10_plugin_rtparams(t):
    # the audio thread under the interposer with automation running: P10_plugin_rt's 10 s, and every block a change of
    # four parameters (two CCs, an NRPN, the pattern every second): no allocation, lock, wait, read or sleep inside
    # process(), one write per call
    auto = []
    for k in range(3750):
        f = 128 * k + 17
        auto += ['param %d 10 %d' % (f, k % 128), 'param %d 30 %d' % (f, 127 - k % 128), 'param %d 50 %d' % (f, (5 * k) % 128)]
        if k % 375 == 0:
            auto.append('param %d 1 %d' % (f, (k // 375) % 4))
    lines = ['mode realtime', 'paced', 'rtcheck', 'out ' + t.path('rt.wav')] + TIMELINE + auto
    _, out = host(t, plan(t, 'rt.txt', lines, rate=48000, block=128, frames=480000),
                  env={'DYLD_INSERT_LIBRARIES': t.a.rtcheck})
    line = [l for l in out.splitlines() if 'rtcheck:' in l and 'process calls' in l]
    check(line, 'no rtcheck line in %r' % out)
    w = line[0].split()
    calls, bad, writes = int(w[2]), int(w[5]), int(w[8])
    check(calls == 3750 and bad == 0 and writes == calls, line[0])


@case
def P10_plugin_params_state(t):
    # The values the host has are in the state, after the card (LCHV, all 36: those it set and those the plugin
    # reported): a new host restoring it plays as a host that sets them at frame 0, byte for byte, and reports them
    # at its first block. A state of the prototype (the card only) is taken with no values (every case that gives a
    # card does that); a damaged LCHV block is refused
    lines = ['note %d 0 48 127' % (20000 * k + 5000) for k in range(12)]
    host(t, plan(t, 'make.txt', lines + ['param 0 10 40', 'param 0 50 20', 'param 0 33 10', 'out ' + t.path('make.wav'),
                                         'state-out ' + t.path('state.bin')], frames=blocks_frames(1)))
    data = open(t.path('state.bin'), 'rb').read()
    at = data.rfind(b'LCHV')
    values = dict(struct.unpack('<IB', data[k:k + 5]) for k in range(at + 12, len(data), 5))
    print('the state: %d bytes, the values block at %d: %d values, ids 10, 33, 50: %r' % (len(data), at, len(values),
                                                                                       [values.get(i) for i in (10, 33, 50)]))
    check(at > 0 and data[at:at + 12] == b'LCHV\x01\x00\x00\x00\x24\x00\x00\x00' and len(data) == at + 12 + 36 * 5
          and values.get(10) == 40 and values.get(33) == 10 and values.get(50) == 20, 'the values block %r' % data[at:at + 12])
    _, ref = host(t, plan(t, 'ref.txt', lines + ['param 0 10 40', 'param 0 50 20', 'param 0 33 10', 'out ' + t.path('ref.wav')]))
    _, got = host(t, plan(t, 'got.txt', lines + ['state-in ' + t.path('state.bin'), 'out ' + t.path('got.wav')], card=False))
    d = same_files(t.path('ref.wav'), t.path('got.wav'))
    check(d is None, 'the restored values: %s' % d)
    first = reports(got)[0]
    print('the restored instance reports at frame %d: %r' % (first[0], {k: first[1][k] for k in (10, 33, 50)}))
    check(first[0] == 0 and first[1].get(10) == 40 and first[1].get(50) == 20 and first[1].get(33) == 10, 'reports %r' % (first,))
    with open(t.path('bad.bin'), 'wb') as f:
        f.write(data[:at] + b'LCHV\x02\x00\x00\x00\x00\x00\x00\x00')
    p = subprocess.run([t.a.host, plan(t, 'bad.txt', ['state-in ' + t.path('bad.bin'), 'out ' + t.path('bad.wav')],
                                         card=False)], capture_output=True, text=True, timeout=600, env=host_env(t))
    print('a damaged values block: exit %d, %s' % (p.returncode, p.stderr.strip()))
    check(p.returncode == 1 and 'setState refused' in p.stderr, 'the damaged block was taken')
    check(not instance_dirs(t.path('home')), 'instance folders left: %r' % instance_dirs(t.path('home')))


@case
def P10_plugin_null_out2(t):
    # An inactive bus may have null sample buffers (ivstaudioprocessor.h:191-193), and a channel may be null: Out 2 given
    # null buffers the whole run (inactive), and Out 1's left channel null for the block at 0 (within the read offset,
    # which the plugin writes as silence) and for the block at 5,120 (an unwritten buffer). No crash; Out 1 is
    # lichomat-render's --stereo sum (P10_plugin_native) but for those blocks' left channel, which the plugin left as
    # the host had it (0.5), and its right channel is the renderer's
    script = t.path('script.txt')
    lat, out = host(t, plan(t, 'plan.txt', TIMELINE + ['null-out2', 'null-out1 0', 'null-out1 5120',
                                                       'out ' + t.path('plugin.wav'), 'script ' + script]))
    t.render(t.card('render'), script, 12, t.path('render.wav'))
    _, _, a = wav(t.path('plugin.wav'))
    _, _, b = wav(t.path('render.wav'))
    shift = lat[0] - 128
    n, diff, loud = shifted_equal(a, b, shift)
    blind = range(5120, 5248)
    left = [a[2 * f] for f in blind]
    others = [i for i in range(2 * n) if a[2 * shift + i] != b[i] and not (i % 2 == 0 and (i // 2 + shift) in blind)]
    print('latency %d, %d frames compared, %d differ; left of the block at 5,120 untouched: %d of 128; other differences: %d'
          % (lat[0], n, diff, sum(1 for v in left if v == 16384), len(others)))
    check(all(v == 16384 for v in left), 'the null channel was written')
    check(all(a[2 * f] == 16384 for f in range(128)) and not any(a[2 * f + 1] for f in range(128)),
          'the block at 0: the null channel was written, or the other one not silent')
    check(not others and loud > 100000, 'Out 1 differs from the renderer in %d samples' % len(others))
    check('engine running' in out, 'the engine did not run to the end')
    # Out 2 (inactive) with one null channel and one the host filled (0.5): that one comes back zero, and the bus is
    # flagged silent in every block (plughost counts the silence over the channels it gave). Before the fix it held
    # the host's 0.5 and no flags were set
    lat, out = host(t, plan(t, 'left.txt', TIMELINE + ['null-out2-left', 'out ' + t.path('left.wav')]))
    m = re.search(r'out2 (\d+) blocks, (\d+) flagged, (\d+) flagged but not silent, (\d+) other flags', out)
    check(m, 'no silence line in %r' % out)
    b2, f2, n2, o2 = (int(x) for x in m.groups())
    n, diff, loud = shifted_equal(wav(t.path('left.wav'))[2], b, lat[0] - 128)
    print('Out 2 with its left channel null: %d blocks, %d flagged silent, %d of them not silent, %d other flags; Out 1: '
          '%d frames compared, %d differ' % (b2, f2, n2, o2, n, diff))
    check(b2 > 3000 and f2 == b2 and n2 == 0 and o2 == 0, 'Out 2 with one null channel: %r' % (m.groups(),))
    check(diff == 0 and loud > 100000, 'Out 1 with Out 2\'s left channel null: %d differ' % diff)
    # Out 1's left channel null in the first block after a restart (a setState of the onset card's own state at 4.5 s):
    # the frames it reads lie before the new engine's first one, which plug_audioRead() writes as silence, not through
    # a null channel; that channel stays as the host had it and the right one is silent
    host(t, plan(t, 'make.txt', ['out ' + t.path('make.wav'), 'state-out ' + t.path('onset.bin')], frames=12800))
    at = 198656
    _, out = host(t, plan(t, 'restart.txt', TIMELINE + ['null-out2', 'setstate %d %s' % (at, t.path('onset.bin')),
                                                         'null-out1 %d' % at, 'out ' + t.path('restart.wav')]))
    r = wav(t.path('restart.wav'))[2]
    print('the block at %d, the first after a restart: left untouched %d of 128, right silent %d of 128'
          % (at, sum(1 for f in range(at, at + 128) if r[2 * f] == 16384), sum(1 for f in range(at, at + 128) if r[2 * f + 1] == 0)))
    check(all(r[2 * f] == 16384 and r[2 * f + 1] == 0 for f in range(at, at + 128)) and 'engine running' in out,
          'the block after the restart: the null channel was written, or the other one not silent')


def lchv(path):
    """the values block of a state file: {id: value}"""
    data = open(path, 'rb').read()
    at = data.rfind(b'LCHV')
    check(at >= 0, 'no values block in %s' % path)
    return dict(struct.unpack('<IB', data[k:k + 5]) for k in range(at + 12, len(data), 5))


@case
def P10_plugin_async_state(t):
    # A state set from another thread while blocks run (a host loading a project): the new card's values, not the old
    # card's. The init card runs (real time, paced, 48 kHz); at 48,000 a prototype state of the fx card (its card only,
    # no values) is set from a thread of its own, and the final getState holds the fx card's FX Amount (id 53) and
    # outputs (60-65), as a plugin that ran the fx card holds them. Before the fix the old engine's report, read after
    # the state was taken, counted as the first report and gave the init card's values back: 0 and 0 0 0 0 0 0
    cards = os.path.join(t.a.root, 'emu', 'tests', 'cards')
    host(t, plan(t, 'make.txt', ['card ' + os.path.join(cards, 'fx'), 'out ' + t.path('make.wav'),
                                 'state-out ' + t.path('fx.bin')], card=False, rate=48000, block=256, frames=12800))
    data = open(t.path('fx.bin'), 'rb').read()
    with open(t.path('fx-proto.bin'), 'wb') as f:
        f.write(data[:data.rfind(b'LCHV')])
    want = lchv(t.path('fx.bin'))
    ids = [53] + list(range(60, 66))
    host(t, plan(t, 'run.txt', ['card ' + os.path.join(cards, 'init'), 'mode realtime', 'paced',
                                'setstate-async 48000 ' + t.path('fx-proto.bin'), 'out ' + t.path('run.wav'),
                                'state-out ' + t.path('after.bin')], card=False, rate=48000, block=256, frames=48000 * 4))
    got = lchv(t.path('after.bin'))
    print('the fx card: FX Amount %d, outputs %r; the state after: FX Amount %d, outputs %r'
          % (want[53], [want[i] for i in range(60, 66)], got[53], [got[i] for i in range(60, 66)]))
    check(want[53] == 64 and [want[i] for i in range(60, 66)] == [6, 6, 6, 6, 0, 0], 'the fx card\'s values %r' % want)
    check([got[i] for i in ids] == [want[i] for i in ids], 'the state after holds the old card\'s values')


@case
def P10_plugin_double_state(t):
    # Two states set one right after the other from one thread while blocks run (a host loading a project, then
    # another): the second card's values. The init card runs (real time, paced, 48 kHz); at 48,000 the init card's
    # prototype state and then the fx card's (cards only, no values) are set from a thread of their own, and the final
    # getState holds the fx card's FX Amount (id 53) and outputs (60-65). The engine of the first state boots before the
    # monitor sees the second restart (its poll is 100 ms): before the fix the processor took that engine as the new
    # card's, its first report gave the second state's missing values the init card's, and the fx card's engine got
    # them: 0 and 0 0 0 0 0 0, not 64 and 6 6 6 6 0 0
    cards = os.path.join(t.a.root, 'emu', 'tests', 'cards')
    protos = []
    for card in ('init', 'fx'):
        host(t, plan(t, 'make-%s.txt' % card, ['card ' + os.path.join(cards, card), 'out ' + t.path('make.wav'),
                                               'state-out ' + t.path(card + '.bin')], card=False, rate=48000, block=256,
                     frames=12800))
        data = open(t.path(card + '.bin'), 'rb').read()
        with open(t.path(card + '-proto.bin'), 'wb') as f:
            f.write(data[:data.rfind(b'LCHV')])
        protos.append(t.path(card + '-proto.bin'))
    want = lchv(t.path('fx.bin'))
    ids = [53] + list(range(60, 66))
    _, out = host(t, plan(t, 'run.txt', ['card ' + os.path.join(cards, 'init'), 'mode realtime', 'paced',
                                         'setstate-async 48000 ' + '+'.join(protos), 'out ' + t.path('run.wav'),
                                         'state-out ' + t.path('after.bin')], card=False, rate=48000, block=256,
                          frames=48000 * 4))
    got = lchv(t.path('after.bin'))
    sets = [l for l in out.splitlines() if 'setstate-async' in l]
    log = ''.join(x for x in instance_logs(t.path('home')).values() if 'a new card' in x)
    print('%s; engines started %d; the fx card: FX Amount %d, outputs %r; the state after: FX Amount %d, outputs %r'
          % ('; '.join(sets), len(re.findall(r'engine \d+ started on ', log)), want[53], [want[i] for i in range(60, 66)],
             got[53], [got[i] for i in range(60, 66)]))
    check(len(sets) == 2 and all(': ok in ' in l for l in sets), 'the setStates %r' % sets)
    check(want[53] == 64 and [want[i] for i in range(60, 66)] == [6, 6, 6, 6, 0, 0], 'the fx card\'s values %r' % want)
    check([got[i] for i in ids] == [want[i] for i in ids], 'the state after holds the first card\'s values')


@case
def P10_plugin_early_state(t):
    # getState before any block holds the values: right after setState (the state's, which no block has taken yet) and
    # right after setActive (the same, 36 of them), then a block with no output bus (which the plugin does not play)
    # still takes the host's changes: a getState after it holds the parameter. Before the fix the first two held a
    # values block of 0 values
    lines = ['note %d 0 48 127' % (20000 * k + 5000) for k in range(12)]
    host(t, plan(t, 'make.txt', lines + ['param 0 10 40', 'param 0 50 20', 'param 0 33 10', 'out ' + t.path('make.wav'),
                                         'state-out ' + t.path('state.bin')], frames=blocks_frames(1)))
    want = lchv(t.path('state.bin'))
    host(t, plan(t, 'early.txt', ['state-in ' + t.path('state.bin'), 'state-early ' + t.path('early.bin'),
                                  'out ' + t.path('early.wav')], card=False, frames=blocks_frames(1)))
    for name in ('early.bin.set', 'early.bin'):
        got = lchv(t.path(name))
        print('%s: %d values, ids 10, 33, 50: %r (the state: %d values, %r)' % (name, len(got), [got.get(i) for i in (10, 33, 50)],
                                                                         len(want), [want.get(i) for i in (10, 33, 50)]))
        check(len(got) == 36 and got == want, '%s holds %r' % (name, got))
    host(t, plan(t, 'blind.txt', ['param 0 10 40', 'no-outputs 0', 'out ' + t.path('blind.wav'),
                                  'state-out ' + t.path('blind.bin')], frames=128))
    got = lchv(t.path('blind.bin'))
    print('after a block with no outputs and a change of id 10 to 40: %r' % got)
    check(got == {10: 40}, 'the block with no outputs lost the host\'s change: %r' % got)


@case
def P10_plugin_flush_order(t):
    # The host's changes in a flush (process() with 0 frames) right before a block with Start are sent at that block's
    # first frame after the Start, as the same change inside the block at frame 0 is (B-12: transport, parameters,
    # notes): pattern 1 (a Program Change) in a flush before the Start block at 4,480 is what the renderer plays for the
    # plughost's script, where the Program Change follows the Start at that frame, byte for byte, and the very output of
    # the change given in the block; so is it with a second flush (no changes) before that block, which must not send
    # what the first took (a flush has no channel: its MIDI would be lost). The firmware switches the pattern at once
    # when the Program Change comes first and at the end of the playing pattern when it follows the Start: the two
    # differ in 395,798 samples (init card)
    init = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'init')
    runs = {}
    for name, extra in (('flush', ['flush 4480 1 1']), ('inblock', ['param 4480 1 1']), ('none', []),
                        ('twice', ['flush 4480 1 1', 'flush-empty 4480'])):
        script = t.path(name + '.txt')
        lat, _ = host(t, plan(t, name + '.plan', ['card ' + init, 'play 4480 120 0', 'out ' + t.path(name + '.wav'),
                                                    'script ' + script] + extra, card=False, frames=blocks_frames(10)))
        runs[name] = wav(t.path(name + '.wav'))[2]
    with open(t.path('flush.txt')) as f:
        text = f.read()
    check(' FA ' in text and 'C0 01' in text and text.index(' FA ') < text.index('C0 01') and text.splitlines()[0].startswith(
        '4480f usb FA'), 'the script %r' % text.splitlines()[:2])
    t.render(t.card('render', 'init'), t.path('flush.txt'), 10, t.path('render.wav'))
    b = wav(t.path('render.wav'))[2]
    n, diff, loud = shifted_equal(runs['flush'], b, lat[0] - 128)
    same = same_files(t.path('flush.wav'), t.path('inblock.wav'))
    twice = same_files(t.path('twice.wav'), t.path('inblock.wav'))
    other = sum(1 for x, y in zip(runs['flush'], runs['none']) if x != y)
    print('flush vs the renderer: %d frames compared, %d differ; flush vs the change in the block: %s; two flushes: %s; '
          'flush vs no change: %d samples differ' % (n, diff, 'the same' if same is None else same,
                                                      'the same' if twice is None else twice, other))
    check(diff == 0 and loud > 10000, '%d samples differ from the renderer' % diff)
    check(same is None, 'the flush plays another output than the change in the block: %s' % same)
    check(twice is None, 'with a second flush the change is lost or moved: %s' % twice)
    check(other > 100000, 'the change does not matter (%d samples): the test shows nothing' % other)


@case
def P10_plugin_silence(t):
    # The silence flags of both buses, offline at 44,140.625 Hz in blocks of 128, the onset card (Drum 1 on out 1, the
    # others on out 2): a flagged block (3) is all zeros and a block with sound is not flagged, on every bus, and the
    # flags are only 0 or 3. Out 2 inactive (sum): Out 2 is flagged in every block and Out 1 has sound in blocks of
    # its own. Out 2 active (split): both buses have blocks with sound and blocks flagged silent: those before the
    # engine's first frames reach the host (the read offset). A block the engine played is not flagged, also a silent
    # one between notes: the flags mark the blocks with no frame from the engine (copied ? 0 : 3), not every silent one
    rows = {}
    for mode, extra in (('sum', []), ('split', ['bus2'])):
        _, out = host(t, plan(t, mode + '.plan', TIMELINE + extra + ['out ' + t.path(mode + '.wav')]))
        line = [l for l in out.splitlines() if ': silence: ' in l]
        check(len(line) == 1, 'no silence line in %r' % out)
        m = re.search(r'out1 (\d+) blocks, (\d+) flagged, (\d+) flagged but not silent, (\d+) other flags; out2 (\d+) blocks, '
                      r'(\d+) flagged, (\d+) flagged but not silent, (\d+) other flags', line[0])
        check(m, line[0])
        rows[mode] = [int(x) for x in m.groups()]
    for mode in ('sum', 'split'):
        b1, f1, n1, o1, b2, f2, n2, o2 = rows[mode]
        check(b1 == b2 and b1 > 3000 and n1 == 0 and n2 == 0 and o1 == 0 and o2 == 0, '%s: %r' % (mode, rows[mode]))
        check(0 < f1 < b1, '%s: Out 1 flagged %d of %d blocks' % (mode, f1, b1))
    b1, f1, n1, o1, b2, f2, n2, o2 = rows['sum']
    check(f2 == b2, 'sum: Out 2 flagged %d of %d blocks' % (f2, b2))
    b1, f1, n1, o1, b2, f2, n2, o2 = rows['split']
    check(0 < f2 < b2, 'split: Out 2 flagged %d of %d blocks (a bus with sound is not silent)' % (f2, b2))


@case
def P10_plugin_frame_order(t):
    # At one frame inside a block the transport's messages come first, then the parameters (B-12): the init card's
    # Start (a count-in from position -0.05, so that the Start falls inside a block, at a frame that is no block's
    # first) and a Program Change (pattern 1) at that very frame. The plugin's output is the renderer's for the
    # plughost's script, where the Program Change follows the Start, byte for byte; the renderer playing it the other
    # way round (the Program Change first) makes another output, so the case can tell the two orders apart
    init = os.path.join(t.a.root, 'emu', 'tests', 'cards', 'init')
    base = ['card ' + init, 'play 4000 120 -0.05']
    host(t, plan(t, 'first.plan', base + ['out ' + t.path('first.wav'), 'script ' + t.path('first.txt')], card=False,
                 frames=blocks_frames(10)))
    with open(t.path('first.txt')) as f:
        start = [l.split('f usb ')[0] for l in f if l.split('f usb ')[1].split(' ')[0] == 'FA']
    check(len(start) == 1, 'Start lines %r' % start)
    at = int(start[0])
    check(at % 128 != 0, 'the Start at %d is a block\'s first frame' % at)
    lat, _ = host(t, plan(t, 'order.plan', base + ['param %d 1 1' % at, 'out ' + t.path('order.wav'),
                                                   'script ' + t.path('order.txt')], card=False, frames=blocks_frames(10)))
    lines = open(t.path('order.txt')).read().splitlines()
    mine = [l for l in lines if l.startswith('%df usb ' % at)]
    check(len(mine) == 1 and mine[0].startswith('%df usb FA' % at) and mine[0].endswith(' C0 01'), 'the script at %d: %r' % (at, mine))
    swapped = [('%df usb C0 01 %s' % (at, mine[0][len('%df usb ' % at):-len(' C0 01')])) if l == mine[0] else l for l in lines]
    with open(t.path('swapped.txt'), 'w') as f:
        f.write('\n'.join(swapped) + '\n')
    t.render(t.card('order', 'init'), t.path('order.txt'), 10, t.path('order-render.wav'))
    t.render(t.card('swapped', 'init'), t.path('swapped.txt'), 10, t.path('swapped-render.wav'))
    a = wav(t.path('order.wav'))[2]
    b = wav(t.path('order-render.wav'))[2]
    c = wav(t.path('swapped-render.wav'))[2]
    n, diff, loud = shifted_equal(a, b, lat[0] - 128)
    _, other, _ = shifted_equal(a, c, lat[0] - 128)
    print('Start and a Program Change at frame %d (block offset %d): %d frames compared, %d differ from the renderer; the '
          'renderer with the Program Change first: %d differ' % (at, at % 128, n, diff, other))
    check(diff == 0 and loud > 10000, '%d samples differ from the renderer' % diff)
    check(other > 1000, 'the two orders sound alike (%d samples): the case shows nothing' % other)


# the 36 parameters of the plugin as the user sees them: id -> (steps, how a value shows)
def controller_table():
    fixed = {1: (63, lambda v: str(v + 1)), 50: (127, str), 51: (127, str), 52: (127, str), 53: (100, str)}
    outs = ['St1', 'St2', 'L1', 'R1', 'L2', 'R2', 'FX']
    table = dict(fixed)
    for i in range(6):
        table[10 + i] = (127, str)                          # volumes
        table[20 + i] = (127, lambda v: str(v - 63))        # pans: -63 to 64
        table[30 + i] = (127, str)                          # filters
        table[60 + i] = (6, lambda v: outs[v])              # outputs
    for i in range(7):
        table[40 + i] = (127, str)                          # decays
    return table


@case
def P10_plugin_controller(t):
    # The plugin's controller, through the SDK's IEditController as a host's generic editor uses it, for all 36
    # parameters: the step count, toPlain(toNormalized(v)) == v for every value, the text of every value (the pattern
    # 1-64, the pans -63 to 64, the outputs St1 to FX, the others as they are), the value of that text, a value outside
    # 0-1 is the nearest end and one between two steps the nearest step, and a text that is no value is refused
    table = controller_table()
    dump = t.path('controller.txt')
    host(t, plan(t, 'plan.txt', ['controller ' + dump, 'out ' + t.path('c.wav')], card=False, frames=128))
    info, values, edge, bad = {}, {}, {}, {}
    for l in open(dump):
        w = l.split()
        if w[0] == 'info':
            info[int(w[1])] = (int(w[3]), float(w[5]), int(w[7]))
        elif w[0] == 'value':
            values.setdefault(int(w[1]), []).append((int(w[2]), float(w[3]), float(w[4]), w[5], float(w[6])))
        elif w[0] == 'edge':
            edge[int(w[1])] = [float(x) for x in w[2:6]]
        elif w[0] == 'bad':
            bad.setdefault(int(w[1]), []).append(int(w[-1]))
    check(sorted(info) == sorted(table) and len(table) == 36, 'the parameters %r' % sorted(info))
    counted = 0
    for pid, (steps, show) in sorted(table.items()):
        check(info[pid][0] == steps and info[pid][2] & 1, 'id %d: info %r' % (pid, info[pid]))
        rows = values[pid]
        check(len(rows) == steps + 1, 'id %d: %d values' % (pid, len(rows)))
        for v, norm, back, text, fromtext in rows:
            check(abs(norm - v / steps) < 1e-12, 'id %d value %d: normalized %r' % (pid, v, norm))
            check(back == v, 'id %d value %d: toPlain(toNormalized) = %r' % (pid, v, back))
            check(text == show(v), 'id %d value %d shows %r, not %r' % (pid, v, text, show(v)))
            check(fromtext == v, 'id %d: the text %r is value %r, not %d' % (pid, text, fromtext, v))
            counted += 1
        check(edge[pid][:2] == [0.0, float(steps)] and edge[pid][2:] == [3.0, 4.0], 'id %d: edges %r' % (pid, edge[pid]))
        check(bad[pid] == [0] * 6, 'id %d: texts taken that are no value: %r' % (pid, bad[pid]))
    print('%d parameters, %d values: toPlain(toNormalized(v)) == v, the texts and their values as shown to the user; '
          'pan %r..%r, pattern %r..%r, outputs %r' % (len(table), counted, values[20][0][3], values[20][-1][3], values[1][0][3],
                                                    values[1][-1][3], [r[3] for r in values[60]]))


@case
def P10_plugin_offsets(t):
    # A host's points may lie outside the block (the SDK does not forbid it): at a negative offset, at n (128) or
    # more, and at the ends of the int32 range. No crash; the point is the block's nearest frame (0 or n - 1: the
    # script has the CC at frame 1,024 for a point at -5, at 2,175 for 128 and 3,199 for 100,000, 4,223 for 2^31 - 1 and
    # 5,120 for -2^31) and the plugin's output is the renderer's for that script
    points = [(1024, -5, 10, 40), (2048, 128, 10, 50), (3072, 100000, 10, 60), (4096, 2147483647, 11, 70),
              (5120, -2147483648, 12, 80)]
    want = {10: [(1024, 40), (2175, 50), (3199, 60)], 11: [(4223, 70)], 12: [(5120, 80)]}
    lines = ['note %d 0 48 127' % (1000 * k + 500) for k in range(8)]
    lines += ['param-at %d %d %d %d' % p for p in points]
    script = t.path('script.txt')
    lat, _ = host(t, plan(t, 'plan.txt', lines + ['out ' + t.path('o.wav'), 'script ' + script], frames=blocks_frames(1)))
    found = {}
    for l in open(script):
        w = l.split()
        for k in range(2, len(w) - 2):
            if w[k] == 'B0' and w[k + 1] in ('59', '5A', '5B'):
                found.setdefault({'59': 10, '5A': 11, '5B': 12}[w[k + 1]], []).append((int(w[0][:-1]), int(w[k + 2], 16)))
    print('the points at the edges of the block, in the script: %r' % found)
    check(found == want, 'the script has %r, not %r' % (found, want))
    t.render(t.card('render'), script, 1, t.path('render.wav'))
    _, _, a = wav(t.path('o.wav'))
    _, _, b = wav(t.path('render.wav'))
    n, diff, loud = shifted_equal(a, b, lat[0] - 128)
    print('%d frames compared, %d differ' % (n, diff))
    check(diff == 0 and loud > 100, '%d samples differ from the renderer' % diff)


# --- the AU through lichomat-auhost (plan E9.7) ---------------------------------------------------

def plug_version(t):
    """the plugin's version, x.y.z, from the engine's first line"""
    out = t.run([t.a.engine, '--version']).stdout
    return re.match(r'lichomat-engine (\d+)\.(\d+)\.(\d+),', out).groups()


@case
def P13_au_helper(t):
    # The component holds the engine the build made last, is signed (codesign --verify --deep --strict) and exports its
    # factory alone; its Info.plist: org.lichomat.au, the plugin's version, and one AudioComponents entry, an instrument
    # (aumu) of subtype Lcht and manufacturer Lchm (an uppercase letter: Apple keeps the lowercase codes), named
    # "LICHOMAT: LICHOMAT", its version as an integer, the factory LichomatAUFactory, not sandbox safe (it starts a process)
    c = t.a.component
    a, b = macho_uuid(os.path.join(c, 'Contents', 'Helpers', 'lichomat-engine')), macho_uuid(t.a.engine)
    print('the component\'s helper %s, the built engine %s' % (a, b))
    check(a == b, 'the component holds another engine than the one built')
    t.run(['codesign', '--verify', '--deep', '--strict', c])
    syms = t.run(['nm', '-gU', os.path.join(c, 'Contents', 'MacOS', 'LICHOMAT')]).stdout.split()
    print('exported: %r' % syms[2::3])
    check(syms[2::3] == ['_LichomatAUFactory'], 'the component exports %r' % syms[2::3])
    with open(os.path.join(c, 'Contents', 'Info.plist'), 'rb') as f:
        info = plistlib.load(f)
    x, y, z = plug_version(t)
    comps = info.get('AudioComponents', [])
    print('Info.plist: %s %s, %r' % (info.get('CFBundleIdentifier'), info.get('CFBundleShortVersionString'), comps))
    check(info.get('CFBundleIdentifier') == 'org.lichomat.au' and info.get('CFBundleShortVersionString') == '%s.%s.%s' % (x, y, z)
          and info.get('CFBundleExecutable') == 'LICHOMAT' and info.get('CFBundlePackageType') == 'BNDL', 'the bundle %r' % info)
    want = {'type': 'aumu', 'subtype': 'Lcht', 'manufacturer': 'Lchm', 'name': 'LICHOMAT: LICHOMAT', 'description': 'LICHOMAT',
            'version': int(x) * 65536 + int(y) * 256 + int(z), 'factoryFunction': 'LichomatAUFactory', 'tags': ['Drums']}
    check(len(comps) == 1 and comps[0] == want, 'AudioComponents %r' % comps)


def au(fn):
    """fn, a case of the VST3, through the AU: lichomat-auhost and LICHOMAT.component in place of lichomat-plughost
    and LICHOMAT.vst3, the same plans and the same checks"""
    def run(t):
        t.a.host, t.a.bundle = t.a.auhost, t.a.component
        fn(t)
    return run


for _name in ('native', 'latency', 'onsets', 'buses', 'buses48', 'params', 'outputs', 'state', 'params_state', 'setstate',
              'renew', 'kill', 'params_restart', 'rt', 'rtparams'):
    CASES['P13_au_' + _name] = au(CASES['P10_plugin_' + _name])


def au_plan(t, name, lines, **kw):
    """plan() for lichomat-auhost: the component in place of the VST3 bundle"""
    return plan(t, name, lines, bundle=t.a.component, **kw)


def au_host(t, plan_path, env=None):
    """host() with lichomat-auhost: (the latencies, its output)"""
    vst3 = t.a.host
    t.a.host = t.a.auhost
    try:
        return host(t, plan_path, env)
    finally:
        t.a.host = vst3


VOICES = ['Drum 1', 'Drum 2', 'Drum 3', 'Snare', 'Cymbal', 'Hi-hat']
OUTPUTS = 'St1|St2|L1|R1|L2|R2|FX'


def au_params():
    """the AU's parameters as the device shows them (plan E9.4 "The parameters"): (id, name, min, max, default, strings)"""
    rows = [(0, 'Engine', 0, 3, 0, 'stopped|starting|running|failed'), (1, 'Pattern', 1, 64, 1, None)]
    rows += [(10 + k, v + ' Volume', 0, 127, 127, None) for k, v in enumerate(VOICES)]
    rows += [(20 + k, v + ' Pan', -63, 64, 0, None) for k, v in enumerate(VOICES)]
    rows += [(30 + k, v + ' Filter', 0, 127, d, None) for k, (v, d) in enumerate(zip(VOICES, (127, 127, 127, 127, 68, 104)))]
    rows += [(40 + k, v + ' Decay', 0, 127, d, None) for k, (v, d) in enumerate(zip(VOICES[:5], (21, 13, 3, 10, 107)))]
    rows += [(45, 'Hi-hat Closed Decay', 0, 127, 5, None), (46, 'Hi-hat Open Decay', 0, 127, 17, None),
             (50, 'DJ Filter', 0, 127, 64, None), (51, 'Reverb Level', 0, 127, 0, None), (52, 'Reverb Decay', 0, 127, 64, None),
             (53, 'FX Amount', 0, 100, 0, None)]
    rows += [(60 + k, v + ' Output', 0, 6, 0, OUTPUTS) for k, v in enumerate(VOICES)]
    return rows


@case
def P13_au_info(t):
    # The AU's 37 parameters are the VST3's, with their ids, names and values as the device shows them: the read-only
    # Engine, the pattern 1-64, pans -63..64, the outputs and the Engine indexed with their names, each value's string
    # (ParameterStringFromValue) that number or name and read back (ParameterValueFromString); two stereo outputs
    # "Out 1" and "Out 2", no input; the latency the VST3 reports (385 at 44,140.625 Hz in blocks of 128), no tail
    _, out = au_host(t, au_plan(t, 'info.txt', ['info', 'out ' + t.path('info.wav')], frames=1280))
    got = {}
    for l in out.splitlines():
        m = re.match(r'lichomat-auhost: info: param (\d+) "([^"]+)" (\S+)\.\.(\S+) default (\S+) unit (\d+) (r|rw)(s?) '
                     r'strings (\S+) shows (\S+)$', l)
        if m:
            got[int(m.group(1))] = m.groups()[1:]
    rows = au_params()
    print('%d parameters, ids %r' % (len(got), sorted(got)))
    check(sorted(got) == [r[0] for r in rows], 'the ids %r' % sorted(got))
    for pid, name, lo, hi, default, strings in rows:
        names = strings.split('|') if strings else None
        show = '|'.join(names[v - lo] if names else str(v) for v in (lo, default, hi))
        want = (name, str(lo), str(hi), str(default), '1' if strings else '0', 'r' if pid == 0 else 'rw', 's', strings or '-', show)
        check(got[pid] == want, 'parameter %d: %r, not %r' % (pid, got[pid], want))
    line = [l for l in out.splitlines() if 'info: outputs' in l]
    print(line[0] if line else out)
    check(line and line[0].endswith('outputs 2: Out 1, Out 2; channels 0 in, 2 out; kAudioUnitProperty_Latency 385 frames; '
                                    'TailTime 0 s'), 'the outputs %r' % line)
    check('lichomat-auhost: component aumu Lcht Lchm "LICHOMAT: LICHOMAT"' in out, 'the component line')


@case
def P13_au_checks(t):
    # What auval checks of an instrument and can be checked without installing it: initialized again at 48 kHz and
    # back (the latency follows the rate), every writable parameter's minimum, maximum and default set and scheduled and
    # read back, the read-only Engine refusing a set, a render of more frames than the maximum refused, a ClassInfo
    # refused (without lichomat-state, or with a state the processor refuses) leaving the AU's values as they were, the
    # ClassInfo read and given back while initialized; then it plays. auval itself runs in the install (install_plugin.sh)
    _, out = au_host(t, au_plan(t, 'checks.txt', ['checks', 'note 1000 0 48 127', 'out ' + t.path('c.wav')], frames=12800))
    line = [l for l in out.splitlines() if 'checks:' in l]
    print(line[0] if line else out)
    check(line == ['lichomat-auhost: checks: 234 passed'], 'checks %r' % line)
    _, _, x = wav(t.path('c.wav'))
    check(any(abs(v) >= 4000 for v in x), 'no sound after the checks')


@case
def P13_au_out2_pull(t):
    # AU v2 has no bus activation: Out 2 counts as used while the host renders it. The onset card (Drum 1 on out 1, the
    # other voices on out 2) at 44,140.625 Hz in blocks of 128, Out 2 rendered only in the blocks from A to B: Out 1 is the
    # sum (the VST3's Out 1 without Out 2) up to the block after A, the device's out 1 (the VST3's Out 1 with Out 2) from
    # it to two blocks after B, the sum again after; Out 2 is the VST3's Out 2 from the block after A to B (the block at A
    # was rendered as the sum, its Out 2 silent) and silent where it is not rendered
    A, B = 100096, 300032
    for name, extra in (('sum', []), ('split', ['bus2'])):
        host(t, plan(t, name + '.txt', TIMELINE + extra + ['out ' + t.path(name + '.wav')]))
    au_host(t, au_plan(t, 'pull.txt', TIMELINE + ['pull2 %d %d' % (A, B), 'out ' + t.path('pull.wav')]))
    _, _, total = wav(t.path('sum.wav'))
    _, _, split = wav(t.path('split.wav'))
    _, _, split2 = wav(t.path('split-out2.wav'))
    _, _, out1 = wav(t.path('pull.wav'))
    _, _, out2 = wav(t.path('pull-out2.wav'))
    a, b = 2 * (A + 128), 2 * (B + 256)
    print('Out 1: the sum to %d, out 1 to %d, the sum after; Out 2: out 2 from %d to %d' % (A + 128, B + 256, A + 128, B))
    check(out1[:a] == total[:a] and out1[a:b] == split[a:b] and out1[b:] == total[b:], 'Out 1')
    check(total[a:b] != split[a:b] and any(split2[a:2 * B]), 'the two runs differ there')
    check(not any(out2[:a]) and out2[a:2 * B] == split2[a:2 * B] and not any(out2[2 * B:]), 'Out 2')


@case
def P13_au_one_output(t):
    # A host that takes one output element (Out 1 alone) gets the sum on it: the VST3's Out 1 without Out 2, byte for byte
    host(t, plan(t, 'vst3.txt', TIMELINE + ['out ' + t.path('vst3.wav')]))
    _, out = au_host(t, au_plan(t, 'one.txt', TIMELINE + ['outputs 1', 'info', 'out ' + t.path('one.wav')]))
    check('info: outputs 1: Out 1;' in out, 'one output element: %r' % [l for l in out.splitlines() if 'outputs' in l])
    d = same_files(t.path('vst3.wav'), t.path('one.wav'))
    print('one output element: Out 1 == the VST3\'s sum' if d is None else d)
    check(d is None, 'Out 1 of one element %s' % d)


@case
def P13_au_vst3_state(t):
    # The AU's state is the VST3's: its ClassInfo holds the processor's bytes (LCHS, LCHV) under "lichomat-state", next to
    # the keys every AU state has. The same plan through both gives the same output and the same state bytes, and each
    # format restores the other's state and plays as the other does with it
    lines = ['note %d 0 48 127' % (20000 * k + 5000) for k in range(12)] + ['param 0 10 40', 'param 0 33 10']
    host(t, plan(t, 'v.txt', lines + ['out ' + t.path('v.wav'), 'state-out ' + t.path('v.bin')], frames=blocks_frames(1)))
    _, out = au_host(t, au_plan(t, 'a.txt', lines + ['out ' + t.path('a.wav'), 'state-out ' + t.path('a.bin')],
                             frames=blocks_frames(1)))
    keys = [l for l in out.splitlines() if 'classinfo keys' in l]
    print(keys[0] if keys else out)
    check(keys == ['lichomat-auhost: classinfo keys: data element-name lichomat-state manufacturer name subtype type version'], 'keys %r' % keys)
    check(same_files(t.path('v.wav'), t.path('a.wav')) is None, 'the AU plays otherwise than the VST3')
    check(same_files(t.path('v.bin'), t.path('a.bin')) is None, 'the AU\'s state bytes differ from the VST3\'s')
    rest = ['note %d 0 48 127' % (20000 * k + 5000) for k in range(12)]
    host(t, plan(t, 'va.txt', rest + ['state-in ' + t.path('a.bin'), 'out ' + t.path('va.wav')], card=False))
    au_host(t, au_plan(t, 'av.txt', rest + ['state-in ' + t.path('v.bin'), 'out ' + t.path('av.wav')], card=False))
    d = same_files(t.path('va.wav'), t.path('av.wav'))
    print('the state, %d bytes, the same in both; the VST3 with the AU\'s state == the AU with the VST3\'s' % os.path.getsize(t.path('v.bin')))
    check(d is None, 'restored across formats %s' % d)


@case
def P13_au_notify(t):
    # The host's listeners hear of the values the plugin reports, from its notifier thread (never the render thread): real
    # time, paced, 48 kHz, the onset card stopped; every value once at the first block; Drum 1's volume set to 40 at 0.5 s
    # (the host's own: not heard), then pattern 1 at 1 s, whose kit sets it back to 127: heard a second time
    lines = ['mode realtime', 'paced', 'param 24064 10 40', 'param 48128 1 1', 'out ' + t.path('n.wav')]
    _, out = au_host(t, au_plan(t, 'n.txt', lines, rate=48000, block=256, frames=48000 * 2))
    heard = [l for l in out.splitlines() if ': heard' in l]
    print(heard[0] if heard else out)
    counts = dict((int(a), int(b)) for a, b in (w.split(':') for w in heard[0].split('heard')[1].split()))
    ids = [r[0] for r in au_params()]
    check(sorted(counts) == ids and counts[10] == 2 and all(counts[i] == 1 for i in ids if i not in (0, 10)) and counts[0] >= 1,
          'heard %r' % counts)
    rep = reports(out)
    check(rep and rep[-1][1] == {10: 127} and rep[-1][0] >= 48128, 'the reports %r' % rep[1:])


@case
def P13_au_ramp(t):
    # A scheduled ramp reaches the firmware as its two ends in the block it was given in, at their frames: Drum 1's
    # volume from 100 to 20 over 50 frames inside one block, and from 20 to 120 over 100 frames from 100 frames into a
    # block of 128, of which the block holds 28 (its end there 20 + 100 x 27 / 100 = 47). The AU's output is the VST3's
    # with those points, byte for byte
    notes = ['note %d 0 48 127' % (100000 + 2000 * k) for k in range(16)]
    points = ['param 100010 10 100', 'param 100059 10 20', 'param 120164 10 20', 'param 120191 10 47']
    script = t.path('v.txt')
    host(t, plan(t, 'v.plan', notes + points + ['out ' + t.path('v.wav'), 'script ' + script], frames=blocks_frames(4)))
    au_host(t, au_plan(t, 'a.plan', notes + ['ramp 100010 10 100 20 50', 'ramp 120164 10 20 120 100', 'out ' + t.path('a.wav')],
                       frames=blocks_frames(4)))
    with open(script) as f:
        ccs = [l.split()[0] + ' ' + ' '.join(l.split()[2:]) for l in f if ' B0 59 ' in l]
    print('the VST3\'s points as MIDI: %r' % ccs)
    check(ccs == ['100010f B0 59 64', '100059f B0 59 14', '120191f B0 59 2F'], 'the points %r' % ccs)
    d = same_files(t.path('v.wav'), t.path('a.wav'))
    print('the AU\'s ramps == the VST3\'s points' if d is None else d)
    check(d is None, 'the ramps %s' % d)


@case
def P13_au_offline_switch(t):
    # A host that turns offline rendering on while the AU plays (a bounce): the processor takes the new mode with a new
    # setup and a new engine (as a VST3 host's setupProcessing() for an export), and waits for it from then on, so that
    # a Note On after the switch sounds at its frame plus the latency (one less on an odd frame), as P10_plugin_renew's
    switch, note = 88064, 88064 + 1001
    lines = ['mode realtime', 'offline %d 1' % switch, 'note %d 0 48 127' % note, 'note %d 0 48 0' % (note + 3000),
             'out ' + t.path('s.wav')]
    lat, out = au_host(t, au_plan(t, 's.txt', lines, frames=blocks_frames(3)))
    _, _, x = wav(t.path('s.wav'))
    left = x[0::2]
    on = next((i for i in range(switch, len(left)) if abs(left[i]) >= 4000), None)
    log = ''.join(instance_logs(t.path('home')).values())
    engines = re.findall(r'engine \d+ started on ', log)
    print('offline from %d: the Note On at %d sounds at %r (latency %d); engines started %d' % (switch, note, on, lat[0], len(engines)))
    check('offline at %d: 1' % switch in out and len(engines) == 2, 'the switch: %d engines' % len(engines))
    check(on in (note + lat[0], note + lat[0] - 1), 'the note at %r' % on)


@case
def P13_au_noalloc(t):
    # A host that renders into its own buffers and tells the AU not to allocate any (ShouldAllocateBuffer 0 on both
    # output elements before AudioUnitInitialize): the AU keeps the frames of the element not pulled in a cache of its
    # own until that element's pull, and plays as it does with buffers of its own, byte for byte: Out 1 alone, Out 1 and
    # Out 2 every block, and Out 2 rendered only from A to B (P13_au_out2_pull's)
    A, B = 100096, 300032
    for name, extra in (('one', []), ('both', ['bus2']), ('pull', ['pull2 %d %d' % (A, B)])):
        au_host(t, au_plan(t, name + '.txt', TIMELINE + extra + ['out ' + t.path(name + '.wav')]))
        au_host(t, au_plan(t, name + '-na.txt', TIMELINE + extra + ['noalloc', 'out ' + t.path(name + '-na.wav')]))
        for f in [''] + (['-out2'] if extra else []):
            d = same_files(t.path(name + f + '.wav'), t.path(name + '-na' + f + '.wav'))
            print('%s%s: without the AU\'s buffers %s' % (name, f, 'the same' if d is None else d))
            check(d is None, '%s%s without the AU\'s buffers %s' % (name, f, d))


@case
def P13_au_set_state(t):
    # A host's set with no render after it (a knob turned, then the project saved): the state holds the set value,
    # which no render has taken to the processor yet; a new instance restoring that state reports it at its first block
    # and plays as one that the host gives it at frame 0, byte for byte
    lines = ['note %d 0 48 127' % (20000 * k + 5000) for k in range(12)]
    au_host(t, au_plan(t, 'make.txt', lines + ['set-end 10 40', 'out ' + t.path('make.wav'), 'state-out ' + t.path('s.bin')],
                       frames=blocks_frames(1)))
    v = lchv(t.path('s.bin'))
    print('the state after a set of id 10 to 40 with no render after it: %d values, id 10 %r' % (len(v), v.get(10)))
    check(len(v) == 36 and v.get(10) == 40, 'the state holds %r for id 10' % v.get(10))
    au_host(t, au_plan(t, 'ref.txt', lines + ['param 0 10 40', 'out ' + t.path('ref.wav')]))
    _, got = au_host(t, au_plan(t, 'got.txt', lines + ['state-in ' + t.path('s.bin'), 'out ' + t.path('got.wav')], card=False))
    d = same_files(t.path('ref.wav'), t.path('got.wav'))
    first = reports(got)[0]
    print('restored: the first report at frame %d, id 10 %r; the output %s' % (first[0], first[1].get(10),
                                                                              'the same' if d is None else d))
    check(first[0] == 0 and first[1].get(10) == 40, 'the restored instance reports %r' % (first,))
    check(d is None, 'the restored instance plays otherwise: %s' % d)


def ring_notes(path):
    """the Note Ons and Note Offs of an events file (the MIDI the plugin gave its engine): ([(pitch, velocity)], offs)"""
    ons, offs = [], 0
    for line in open(path):
        b = [int(x, 16) for x in line.split('f usb ')[1].split()]
        k = 0
        while k < len(b):
            n = 1 if b[k] >= 0xF8 else 2 if (b[k] & 0xF0) in (0xC0, 0xD0) else 3
            if (b[k] & 0xF0) == 0x90 and b[k + 2] > 0:
                ons.append((b[k + 1], b[k + 2]))
            elif (b[k] & 0xF0) in (0x80, 0x90):
                offs += 1
            k += n
    return ons, offs


@case
def P13_au_midi_async(t):
    # MusicDeviceMIDIEvent from a thread that is not the render thread (MusicDevice.h allows it, at offset 0) while the
    # AU renders: real time, paced, 48 kHz in blocks of 128; from 1 s a thread of its own sends 500 Note Ons, each with
    # its Note Off, one every 2 ms, each Note On of its own pitch and velocity. The MIDI the processor gave its engine
    # (its event ring) holds every one of them once, none lost and none twice
    lines = ['mode realtime', 'paced', 'notes-async 48000 500', 'events ' + t.path('ring.txt'), 'out ' + t.path('m.wav')]
    _, out = au_host(t, au_plan(t, 'm.txt', lines, rate=48000, block=128, frames=48000 * 4))
    ons, offs = ring_notes(t.path('ring.txt'))
    want = sorted((36 + k // 100, 1 + k % 100) for k in range(500))
    got = sorted(ons)
    lost = sorted(set(want) - set(got))
    twice = sorted(x for x in set(got) if got.count(x) > 1)
    print('500 Note Ons and Offs from another thread: %d Note Ons and %d Note Offs reached the engine; lost %d %r, twice '
          '%d %r' % (len(ons), offs, len(lost), lost[:8], len(twice), twice[:8]))
    check('notes-async at 48000: 500 Note Ons and Offs sent, 0 refused' in out, 'the sender: %r' % out[-400:])
    check(got == want and offs == 500, 'the notes: %d Note Ons, %d Note Offs, %d lost, %d twice' % (len(ons), offs, len(lost),
                                                                                                 len(twice)))


@case
def P13_au_offline_async(t):
    # A host that turns offline rendering on and off from another thread while the render thread renders (a bounce
    # started from its main thread): real time, paced, 48 kHz in blocks of 128, a Note On every 0.1 s; from 1 s a thread
    # of its own sets OfflineRender 20 times, 1 and 0 in turn, one every 50 ms. The AU sets the processor up again
    # (a new engine) never under a render: a render that finds it being set up is silent and flagged so
    # (kAudioUnitRenderAction_OutputIsSilence), every other is the processor's. No crash, every set taken, renders
    # flagged (each setup stops an engine and starts one), none flagged that is not silent, and the engine of the last
    # setup plays the notes after the switches
    lines = ['mode realtime', 'paced', 'offline-async 48000 20', 'out ' + t.path('o.wav')]
    lines += ['note %d 0 48 127' % (4800 * k + 100) for k in range(40)]
    _, out = au_host(t, au_plan(t, 'o.txt', lines, rate=48000, block=128, frames=48000 * 4))
    sets = [l for l in out.splitlines() if 'offline-async at' in l]
    m = re.search(r'instance 0: (\d+) renders, (\d+) flagged silent, (\d+) flagged but not silent', out)
    _, _, x = wav(t.path('o.wav'))
    late = sum(1 for v in x[2 * 48000 * 3:] if abs(v) >= 4000)
    log = ''.join(instance_logs(t.path('home')).values())
    engines = len(re.findall(r'engine \d+ started on ', log))
    print('%s; %s; engines started %d; loud samples after 3 s: %d' % (sets[0] if sets else out[-300:], m.group(0) if m else '-',
                                                                     engines, late))
    check(sets and ': 20 sets, 20 taken' in sets[0], 'the sets %r' % sets)
    check(m and int(m.group(1)) == 1500 and int(m.group(2)) > 0 and int(m.group(3)) == 0,
          'the renders: %r' % ((m.groups() if m else out[-400:]),))
    check(late > 1000 and 'engine running' in out, 'no sound after the switches: %d' % late)


@case
def P13_au_stale_note(t):
    # A Note On the host sent before it uninitialized the AU (AudioUnitUninitialize, AudioUnitInitialize) or reset it
    # (AudioUnitReset) does not play after: offline, the onset card stopped, no 0-frame renders before the first block
    # (which waits for the engine), 1 s: silent. The same Note On sent after the Initialize plays at frame 0 (so the case
    # can tell)
    loud = {}
    for name in ('init', 'reset', 'after'):
        au_host(t, au_plan(t, name + '.txt', ['stale-note ' + name, 'out ' + t.path(name + '.wav')], frames=blocks_frames(1)))
        loud[name] = sum(1 for v in wav(t.path(name + '.wav'))[2] if abs(v) >= 4000)
    print('loud samples: a Note On before Uninitialize and Initialize %d, before Reset %d, after them %d'
          % (loud['init'], loud['reset'], loud['after']))
    check(loud['after'] > 100, 'the Note On after Initialize did not play: the case shows nothing')
    check(loud['init'] == 0 and loud['reset'] == 0, 'a stale Note On played: %r' % loud)


# --- the licence gate and the name scan (Task 10) ----------------------------------------------

@case
def P11_licence(t):
    # the licence gate over the plugin's build (plugin_licence_gate.py): every source the build reads GPL-3.0-only or
    # allowed (the vendored VST 3 SDK, MIT), the main.h overlay on every compile command but the SDK's, the bundle's
    # files with the licences and notices in it, and the name scan of the bundle's two binaries, the built engine, the
    # test host, plug_drive and the real-time check
    contents = os.path.join(t.a.bundle, 'Contents')
    binaries = [os.path.join(contents, 'MacOS', 'LICHOMAT'), os.path.join(contents, 'Helpers', 'lichomat-engine'),
                t.a.engine, t.a.host, t.a.drive, t.a.rtcheck]
    out = t.run([sys.executable, os.path.join(t.a.root, 'plugin', 'tests', 'plugin_licence_gate.py'), '--bundle',
                 t.a.bundle, t.a.root, t.a.build] + binaries).stdout
    print(out.strip())


def main():
    ap = argparse.ArgumentParser()
    for o in ('root', 'build', 'render', 'engine', 'drive', 'resample', 'work'):
        ap.add_argument('--' + o, required=True)
    for o in ('host', 'bundle', 'validator', 'rtcheck', 'component', 'auhost'):
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
