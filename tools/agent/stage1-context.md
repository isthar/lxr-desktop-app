LICHOMAT is an unofficial custom firmware for the Erica Synths LXR-02 drum machine. The hardware is an STM32F765VG Cortex-M7 at 216 MHz with a double-precision FPU, and the firmware is GPL-3.0.

Stage 1 does three things:
- It replaces fw 1.7's 24-pulse sequencer timing with a pure-C, host-testable core, seq_core. The core lives in Core/Src/Soundengine/Sequencer/seq_core.c and Core/Inc/Soundengine/Sequencer/seq_core.h, with firmware glue in sequencer.c.
- It fixes the timing and MIDI defects D1-D8 and the fw 1.7 bugs found while planning (E2.7, E2.8).
- It adds tooling: branding, a linker check, a CPU meter, CI and docs.

How the plan was made:
- Every task was replayed in a scratch copy of this repository before the plan was assembled. Its code, commands and expected outputs (hashes, sizes, test counts, compiler messages from Apple clang 17 and Arm GNU 14.3.rel1 on this Mac) are therefore verified. Follow it closely.
- After assembly, three critics reviewed the plan and a fixer applied the accepted findings. The fixer's edits were not replayed, so they are where a task is most likely to need a small correction.
- When an expected output differs, find out why before deviating, and list the difference in your report.

Order of the tasks:
1. E3.2
2. E1.1-E1.13, then E1.14
3. E2.1-E2.6, then E2.7 and E2.8
4. E3.1, E3.3, E3.4, E3.5 (its env.sh part was already done in commit 5911417)
5. E4.1-E4.3

Each task starts from the committed result of the tasks before it on branch timing-core (see git log). The notes file docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md (N1-N20) wins over the spec. The plan applies the notes; where the plan and a note still disagree, follow the note and report it.

The Bash tool runs zsh, and `source tools/env.sh` works in zsh. Build and test only through tools/check.sh, or the commands the plan gives.
