# LICHOMAT: how to resume work

Read this first when picking the project up in a new session.

## Where things are

| What | Where |
|---|---|
| Backlog (source of truth) | `docs/lichomat/tracker.json` |
| Tracker page (claude.ai artifact) | https://claude.ai/artifact/MPCLCyFzqsp3ViNGuwgTZt |
| Stage 1 spec | `docs/superpowers/specs/2026-10-23-timing-core-design.md` |
| Stage 1 plan | `docs/superpowers/plans/2026-10-23-timing-core.md` (once written) |
| Reports per milestone | `docs/lichomat/reports/` |
| fw 1.7 timing replica | `tests/reference/fw17_replica.c` |
| Work branch | `timing-core` (local commits; nothing is pushed without the owner's OK) |

## Toolchain

The tools live in `.toolchain/` (git-ignored, about 1.4 GB). Nothing is installed system-wide.

```sh
tools/setup-toolchain.sh   # downloads Arm GNU 14.3.rel1, CMake 3.31.12, Ninja 1.13.2 if missing (SHA-256 checked)
source tools/env.sh        # puts them on PATH, sets ARM_TOOLCHAIN_DIR
tools/check.sh             # host tests + release build + memory summary + warning count; non-zero exit on failure
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

While a workflow runs, a Monitor on its `journal.jsonl` reports each agent that starts or finishes, so
the log can follow the workflow step by step.

## Working rules

- Small commits ending with the `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` trailer.
- Add files explicitly; never `git add -A`.
- Never push, never touch `main`, never change git config.
- After each task run `tools/check.sh`. It must show zero warnings, ITCM must fit, and RAM must stay within the spec budget.
- Decisions that belong to the owner go into `questions` in the tracker. Work continues with a reversible default.
- Hardware checks (H1–H13 in the spec) can only be done by the owner on the LXR-02. They are prepared but never claimed.
