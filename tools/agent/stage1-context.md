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
2. E1.1-E1.13, then E1.14, E1.15 (the slave tempo estimate for packed USB clocks, note N21), E1.16 (the event queue as an earliest-first ring, amended N2), E1.17 (the leftovers of the E1 reviews) and E1.18 (one merge per boundary in the queue)
3. E2.1-E2.6, then E2.7 and E2.8, then E1.19 (one extra clock or a double trigger in the slave tempo fit, N21; it runs after E2 so that the integration is not delayed)
4. E2.9 (the notes and the plan after the E2 reviews; documents only), then E3.1, E3.3, E3.4, E3.5 (its env.sh part was already done in commit aa70b81)
5. E4.1 (run by tools/agent/stage-review.js), then E4.2 and E4.3

Each task starts from the committed result of the tasks before it on branch timing-core (see git log). The notes file docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md (N1-N21) wins over the spec. The plan applies the notes; where the plan and a note still disagree, follow the note and report it.

The Bash tool runs zsh, and `source tools/env.sh` works in zsh. Build and test only through tools/check.sh, or the commands the plan gives.

State of the core before E2 (read this before an E2 task): the plan's E2 section was written against the core as of E1.13. Since then:
- E1.14 added N16, N17 and N18.1.
- E1.15 replaced the slave tempo estimate with a least-squares fit over transfers (N21: SEQC_TEMPO_JOIN_MS, new stats such as tempoRestarts).
- E1.16 and E1.18 made the event queue an earliest-first ring with one merge per boundary (amended N2).
- E1.17 hardened the tests: ASSERT_EQ rejects floating-point operands, and there are new tests.
E2 steps that quote E1 text (for example E2.1's test_fw17 line and E1.3's lxr_sequencer block in tests/CMakeLists.txt and Core/CMakeLists.txt, and E2.4's execution-context comment in seq_core.h) may no longer match: adapt them to the current files, keep the intent, and list every adaptation. Expected RAM, image and test-count figures will differ too; report the measured ones. The E4.1 RAM budget (row R6.2) is being raised for N20 (444 B) and N21 (1192 B).

State before E3 and E4 (read this before an E3 or E4 task): E1.1-E1.19 and E2.1-E2.8 are done, and the firmware runs on seq_core. The plan's E3 and E4 sections were written when the notes ran to N20 and E1 to E1.14. Its "Execution amendments" section (items 1-23) lists what changed since. Adapt the quoted text of a step to the current files, keep its intent, and list every adaptation. Measured at 3f2a038: ITCM 13128 B, DTCM 107388 B, RAM 284272 B, so DTCM + RAM is 391660 B; the image is 299092 B; 26 ctest ids pass. E2.9 set review row R6.2 to 391824 B; after E3.1-E3.5 (workflow C1) DTCM + RAM is 391732 B, the image 299948 B and 27 ctest ids pass. tools/fw_checks.sh (E2.2) checks the firmware ELF and sources task by task.
