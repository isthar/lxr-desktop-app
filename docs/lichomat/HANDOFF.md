# LICHOMAT: how to resume work

Read this first when picking the project up in a new session.

## Where things are

| What | Where |
|---|---|
| Backlog (source of truth) | `docs/lichomat/tracker.json` |
| Tracker page (claude.ai artifact) | https://claude.ai/artifact/MPCLCyFzqsp3ViNGuwgTZt |
| Stage 1 spec | `docs/superpowers/specs/2026-10-23-timing-core-design.md` |
| Stage 1 plan | `docs/superpowers/plans/2026-10-23-timing-core.md` |
| Design decisions beyond the spec | `docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md` (N1–N20; they win over the spec) |
| Reports per milestone | `docs/lichomat/reports/` |
| fw 1.7 timing replica | `tests/reference/fw17_replica.c` |
| Work branch | `timing-core`, pushed to the owner's fork: https://github.com/isthar/lxr-desktop-app/tree/timing-core |

## Toolchain

The tools live in `.toolchain/` (git-ignored, about 1.4 GB). Nothing is installed system-wide.

```sh
tools/setup-toolchain.sh   # downloads Arm GNU 14.3.rel1, CMake 3.31.12, Ninja 1.13.2 if missing (SHA-256 checked)
source tools/env.sh        # puts them on PATH (bash or zsh), sets ARM_TOOLCHAIN_DIR
tools/check.sh             # host tests + release build + memory summary + warning count; non-zero exit on failure
tools/fw_checks.sh         # after check.sh: ELF and source checks of the firmware integration (E2.2-E2.6)
```

The release image lands in `build/release/`, and the full log of the last check in `build/check.log`.

## Updating the tracker page

The owner watches this page, so it must move with the work, not only when a task closes. Add a
log entry for every finished agent step, every commit and every phase change.

1. Log what happened and refresh the status in one go:
   `python3 tools/tracker_note.py "text" [--focus …] [--next …] [--phase …]`.
   It stamps the entry with the local time and sets `status.updated`, `status.updated_at` and `status.head`.
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
  | `context` | a scene-setting text |
  | `tasks` | a list of `{id, title, text}` or `{id, title, file}`, made by `extract_tasks.py`, each with an optional `review` mode |
  | `final` | `{review: true}`, optional |

- **`extract_tasks.py`** cuts the full text of the chosen tasks out of the plan:
  `python3 tools/agent/extract_tasks.py <plan> <out.json> E1.1 E1.2 …`. Without ids it lists the tasks
  it finds.
- **`wf_watch.py`** prints one line per agent that starts or finishes in a workflow's
  `journal.jsonl`, and with `--git=<repo>` one line per new commit. Run it under a Monitor:
  `python3 tools/agent/wf_watch.py <workflow transcript dir> <lines already seen> <label> [finished-only] [--git=<repo>]`.

A workflow can only be resumed (`resumeFromRunId`) in the session that started it. In a new session:
1. Look at `git log` and the plan's ticked checkboxes to see where the run stopped.
2. Start `impl-track.js` again with only the remaining tasks.

## Working rules

- Small commits ending with the `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` trailer.
- Add files explicitly; never `git add -A`.
- Push `timing-core` only to `origin`, the owner's fork, as work lands; the owner allowed this on 2026-10-24. Never push to `upstream` (Erica Synths), never touch `main`.
- Commits use the repo-local identity `Tomasz Krzywicki <tkrzywicki@gmail.com>`; the owner chose the private address. `backup/timing-core-before-reauthor` keeps the history as it was before the re-authoring; it is local only.
- After each task run `tools/check.sh`. It must show zero warnings, ITCM must fit, and RAM must stay within the spec budget.
- Decisions that belong to the owner go into `questions` in the tracker. Work continues with a reversible default.
- Hardware checks (H1–H13 in the spec) can only be done by the owner on the LXR-02. They are prepared but never claimed.
