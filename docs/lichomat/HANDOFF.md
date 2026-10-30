# LICHOMAT: how to resume work

Read this first when picking the project up in a new session.

## Where things are

| What | Where |
|---|---|
| Backlog (source of truth) | `docs/lichomat/tracker.json` |
| Tracker page (claude.ai artifact) | https://claude.ai/artifact/MPCLCyFzqsp3ViNGuwgTZt |
| Stage 1 spec | `docs/superpowers/specs/2026-10-23-timing-core-design.md` |
| Stage 1 plan | `docs/superpowers/plans/2026-10-23-timing-core.md` |
| Stage 1b spec (emulator; accepted on 2026-10-27, Q20) | `docs/superpowers/specs/2026-10-27-emulator-design.md` |
| Stage 1b plan, R1 part 1 (F1–F10; done on the branch `emulator`, pushed to the fork, worktree `build/wt-emulator`) | `docs/superpowers/plans/2026-10-27-emulator-r1a-firmware-changes.md` |
| Stage 1b plan, R1 part 2 (the virtual device `emu/`; done on `emulator`, 103 ctest ids: `source tools/env.sh && cmake -S emu -B build/emu -G Ninja && cmake --build build/emu && ctest --test-dir build/emu`, in the worktree) | `docs/superpowers/plans/2026-10-27-emulator-r1b-virtual-device.md` |
| Milestone report of R1 | `docs/lichomat/reports/M8-emulator-R1.md` |
| Stage 1b plan, R2 and R3 (the renderer `lichomat-render`, the goldens, the emulator CI; R2.1–R2.6 done on `emulator`, 133 ctest ids, serial and `ctest -j8`; R3.1–R3.6 next) | `docs/superpowers/plans/2026-10-29-emulator-r2-r3-renderer-goldens.md` |
| Milestone report of R2 | `docs/lichomat/reports/M9-emulator-R2.md` |
| Milestone report of v0.2 | `docs/lichomat/reports/M10-v0.2.md` |
| Plan v0.3 (tasks P1–P9; branch `v0.3` from `v0.2`, worktree `build/wt-v03`, pushed; P1–P6 and the fixes of the P1–P4 review done, head `194c2f8`; P7 half done on the local branch `wip/v0.3-p7`, `457fc34`, untested: continue from it, then P8, P9, a light review of P5–P8 and the M11 report) | `docs/superpowers/plans/2026-10-30-v0.3-performance-acid.md` |
| Stage 2 draft (accepted by the owner's rule of 2026-10-29) | `docs/superpowers/specs/2026-10-29-stage2-draft.md` |
| Plan v0.2 (tasks V1–V11; V1–V10 on branch `v0.2` from `emulator` at `1099778`, worktree `build/wt-v02`, pushed to the fork, head `4b63f2d` after two light reviews and their fixes: 83 host and 139 emulator ctest ids; V11, the owner's documents, on `timing-core`) | `docs/superpowers/plans/2026-10-29-v0.2-polyrhythm-groove.md` |
| The plan writer's prototype of V1–V10 (git-ignored, on the owner's Mac; for cross-checking only, the tasks are done test-first from the plan) | `build/v0.2-prototype.diff` |
| The night's scratch work of 2026-10-27 (the stage 1b host probe, the sweep's harnesses) | `build/night-2026-10-27/` (git-ignored, on the owner's Mac; its README lists the contents) |
| Design decisions beyond the spec | `docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md` (N1–N21; they win over the spec) |
| Reports per milestone | `docs/lichomat/reports/` |
| Sanitizer sweep of 2026-10-27 (fw 1.7 bugs B1–B19, with the status of each) | `docs/lichomat/reports/sanitizer-sweep-2026-10-27.md` |
| Host tests (32 ctest ids) | `tests/`; `test_buttons`, `test_midicc`, `test_sequencer`, `test_presets`, `test_delay` and `test_dsp` compile fw 1.7's firmware files (the last four came with E4.6–E4.9) |
| fw 1.7 timing replica | `tests/reference/fw17_replica.c` |
| Hardware checklist for the owner (H1–H13, U1–U14, X1–X4; U4–U14 for v0.2) | `docs/lichomat/hardware-checklist.md` |
| Release notes v0.1 (draft) | `docs/lichomat/release-notes-v0.1.md` |
| Release notes v0.2 (draft, Polish) | `docs/lichomat/release-notes-v0.2.md` |
| Demo renders of v0.2 (`tools/demo_v02.sh` on `v0.2`) | `build/demo/v02-*.wav` and `build/demo/README-v0.2.txt` (git-ignored, on the owner's Mac) |
| Work branch | `timing-core`, pushed to the owner's fork: https://github.com/isthar/lxr-desktop-app/tree/timing-core |

## Toolchain

The tools live in `.toolchain/` (git-ignored, about 1.4 GB). Nothing is installed system-wide.

```sh
tools/setup-toolchain.sh   # downloads Arm GNU 14.3.rel1, CMake 3.31.12, Ninja 1.13.2 if missing (SHA-256 checked)
source tools/env.sh        # puts them on PATH (bash or zsh), sets ARM_TOOLCHAIN_DIR
tools/check.sh             # host tests + release build + memory summary + update file check + warning count; non-zero exit on failure
tools/fw_checks.sh         # after check.sh: ELF and source checks of the firmware integration (E2.2-E2.8, E3.3 and E4.9)
ruby tools/ci_dry_run.rb firmware   # dry run of the CI workflow's firmware job (or host-tests); nothing is pushed
```

The release image lands in `build/release/`, and the full log of the last check in `build/check.log`.

CI's host tests build with GCC on Linux in strict C11 (`CMAKE_C_EXTENSIONS OFF`): there the C library
defines `M_PI` only with `_DEFAULT_SOURCE`, and GCC warns where Apple clang does not
(`-Wformat-truncation`). Before pushing a host test that compiles firmware files, compile it once with
the Arm GCC in the same mode (newlib hides `M_PI` the same way), for example:
`arm-none-eabi-gcc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -Itests/stubs -Itests -isystem Core/Inc -c -o /dev/null tests/test_dsp.c`.

## Updating the tracker page

The owner watches this page, so it must move with the work, not only when a task closes. Add a
log entry for every finished agent step, every commit and every phase change.

1. Log what happened and refresh the status in one go:
   `python3 tools/tracker_note.py "text" [--focus …] [--next …] [--phase …] [--now …]`.
   It stamps the entry with the local time and sets `status.updated`, `status.updated_at` and `status.head`.
   `--now` sets `status.now`, the line the page shows while a workflow runs; `--now ""` clears it.
2. For task, milestone or question changes, edit `docs/lichomat/tracker.json` by hand:
   - status values: `zrobione`, `w toku`, `do zrobienia`, `zablokowane`
   - question status: `otwarte` or `zamknięte`
3. Run `python3 tools/tracker_export.py`. It writes `build/tracker/*.json`, says which files changed, and validates epics, statuses,
   ids and log entries.
4. Upload each changed file to the artifact as an asset: Artifact tool, `url` = tracker, `asset: true`, `file_path`.
   A log entry changes `status.json` and `log.json`.
5. Point the matching store documents at the new uploads in one ArtifactData `batch` of `update` writes:
   - documents `datasets/<id>`, where the id is `status`, `epics`, `tasks`, `milestones`, `questions` or `log`
   - fields to set: `source.url`, `source.name` and `updated` (`{at, by}`)
   - pin each write with `if_version` from the last read or write
6. Delete the replaced uploads (Artifact `delete` with the asset id as `path`).
   `build/tracker/remote.json` (git-ignored) keeps the last blob id and document version per dataset.
   If it is missing or stale, ArtifactData `list` of the `datasets` collection shows both.
7. The page itself is `files/index.html` in the store (`{text}`). Its source is kept in `docs/lichomat/tracker-page.html`.

While a workflow runs, a Monitor on its `journal.jsonl` reports each agent that starts or finishes, and
with `--git` each new commit, so the log can follow the workflow step by step. Log every commit: the
owner watches the page and reads silence as a stall.

## Running the plan with agents

The scripts in `tools/agent/` drive the work through Claude Code workflows:

- **`impl-track.js`** is a Workflow script. It runs the plan's tasks in order:
  - for each task, an implementer (TDD, `tools/check.sh`, commit), then the reviews of the task's
    `review` mode:
    - `full`, the default: a spec-compliance review, then a quality review, each with fix loops of at
      most 3 rounds;
    - `parallel`: both reviews at the same time and one fixer for both lists. The first run of each
      review is full; later runs check the previous findings and review only the fix commits (the
      owner's choice for E1.18 and E2, 2026-10-25);
    - `light`: one combined review at high effort, at most 2 rounds (the owner's choice for E3 and E4);
  - the run stops and reports when a task is blocked or does not pass review.

  Start it with the Workflow tool, `scriptPath: tools/agent/impl-track.js`, and these args:

  | arg | value |
  |---|---|
  | `repo` | the repository path |
  | `branch` | the work branch |
  | `plan` | the plan path |
  | `context` | a scene-setting text: the content of `tools/agent/stage1-context.md`, which the runs pass and keep current |
  | `tasks` | a list of `{id, title, text}` or `{id, title, file}`, made by `extract_tasks.py`, each with an optional `review` mode |
  | `final` | `{review: true}`, optional |

- **`stage-review.js`** runs task E4.1, the multi-dimension review of stage 1, as a Workflow script:
  - a setup agent writes the checklist and the stage 1 report and runs the automated rows;
  - one read-only reviewer per group of manual rows, all in parallel;
  - skeptics try to refute each finding, two for blockers and majors;
  - one fixer per blocker or major and one for the minors, test-first, each finding in its own
    commit;
  - the gate, then one light review.

  Its args are `repo`, `branch`, `plan`, `context`, `task: {id, title, file}` and `groups: [{key,
  rows, focus}]`.
- **`extract_tasks.py`** cuts the full text of the chosen tasks out of the plan:
  `python3 tools/agent/extract_tasks.py <plan> <out.json> E1.1 E1.2 …`. Without ids it lists the tasks
  it finds.
- **`wf_watch.py`** prints one line per agent that starts or finishes in a workflow's
  `journal.jsonl`, and with `--git=<repo>` one line per new commit. Run it under a Monitor:
  `python3 tools/agent/wf_watch.py <workflow transcript dir> <lines already seen> <label> [finished-only] [--git=<repo>]`.
- **`usage_estimate.py`** estimates the share of the weekly usage limit spent so far:
  `python3 -I tools/agent/usage_estimate.py [--reading=<UTC ISO>@<percent>]`. It calibrates the
  weighted cost of the local transcripts by the last two readings the owner gave (add new ones to
  its READINGS list). Since 2026-10-30 the owner allows work up to 65% of this week (they need about 20% on Monday);
  check it before and after each agent.
- **`release-verify.js`** checks the owner's documents of stage 1, `docs/lichomat/hardware-checklist.md`
  and `docs/lichomat/release-notes-v0.1.md`, as a Workflow script: five read-only lenses (numbers,
  procedures, coverage, notes, consistency), one skeptic per lens, one documents-only fixer and a
  light review. Its args are `repo`, `branch`, `plan`, `context` and `known` (a Markdown list of known
  items).

A workflow can only be resumed (`resumeFromRunId`) in the session that started it. In a new session:
1. Look at `git log` and the plan's ticked checkboxes to see where the run stopped.
2. Start `impl-track.js` again with only the remaining tasks.

## Working rules

- Small commits ending with the `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` trailer.
- Add files explicitly; never `git add -A`.
- Never rewrite a commit (no `git commit --amend`): fix with a new commit.
- Line endings: the sources in `Core/` use CRLF (except `Core/CMakeLists.txt` and the LF files fw 1.7 already had, such as `encoder.c`), everything else LF; review row R9.4 checks it. After editing a file in `Core/`, run `perl -pi -e 's/\r?\n/\r\n/' <file>` and check that its count of CRLF equals its count of LF bytes.
- Push the work branches (`timing-core`, `emulator` and later ones) only to `origin`, the owner's fork, as work lands; the owner allowed this for `timing-core` on 2026-10-24 (Q5) and for every branch on 2026-10-29 (Q19). Never push to `upstream` (Erica Synths), never touch `main`, never force-push.
- Decision questions: since 2026-10-29 the owner wants my own recommendation taken as the answer. Record it in `questions` as closed, with the reasoning, and go on. Hardware checks stay the owner's.
- Commits use the repo-local identity `Tomasz Krzywicki <tkrzywicki@gmail.com>`; the owner chose the private address. `backup/timing-core-before-reauthor` keeps the history as it was before the re-authoring; it is local only.
- After each task run `tools/check.sh`. It must show zero warnings, ITCM must fit, and DTCM + RAM must stay within the v0.2 budget: at most 395,892 B, and DTCM at most 107,900 B (391,796 + 4,096 and 107,388 + 512, set by the owner for v0.2; `tools/check.sh` checks both on branch `v0.2` and later).
- Decisions that belong to the owner go into `questions` in the tracker. Work continues with a reversible default.
- Hardware checks (H1–H13 in the spec, U1 from E2.7, U2 from E4.5, U3 from E4.6–E4.9, U4–U9 from the emulator spec's §12.4 (F1–F12), U10–U14 from plan v0.2, X4 from E2.8 and the optional clock jack checks X1–X3, all in E4.2's checklist `docs/lichomat/hardware-checklist.md`) can only be done by the owner on the LXR-02. They are prepared but never claimed.
