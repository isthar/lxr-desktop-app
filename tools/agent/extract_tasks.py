"""Extract task sections from the stage 1 plan as JSON for a workflow.

    python3 -I extract_tasks.py <plan.md> <out.json> E1.1 E1.2 ...   (no ids: list all tasks found)
    python3 -I extract_tasks.py --files <dir> <plan.md> <out.json> E1.1 ...

With --files, each task's text is written to <dir>/<id>.md and the JSON rows carry "file" instead
of "text", which keeps large tasks out of the workflow arguments.

A task starts at a heading (## to ####) whose text names a task id such as E1.1, optionally after
the word Task, and runs until the next heading of the same or a higher level.
"""
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
import json
import os
import re
import sys

HEAD = re.compile(r"^(#{2,4})\s+(?:Task\s+)?(E\d+\.\d+)\b[\s:.\-–—]*(.*)$")
FENCE = re.compile(r"^(\s*)(`{3,}|~{3,})(.*)$")


def main():
    argv = sys.argv[1:]
    files_dir = None
    if argv[:1] == ["--files"]:
        files_dir, argv = argv[1], argv[2:]
    plan, out, wanted = argv[0], argv[1], argv[2:]
    lines = open(plan, encoding="utf-8").read().split("\n")
    # Lines inside fenced code blocks are not headings (bash comments, embedded Markdown files).
    in_code = [False] * len(lines)
    fence = None
    for i, line in enumerate(lines):
        m = FENCE.match(line)
        if fence is None:
            if m:
                fence = (m.group(2)[0], len(m.group(2)))
                in_code[i] = True
        else:
            in_code[i] = True
            if m and m.group(2)[0] == fence[0] and len(m.group(2)) >= fence[1] and not m.group(3).strip():
                fence = None
    starts = []
    for i, line in enumerate(lines):
        m = HEAD.match(line)
        if m and not in_code[i]:
            starts.append((i, len(m.group(1)), m.group(2), m.group(3).strip()))
    tasks = {}
    for k, (i, level, tid, title) in enumerate(starts):
        end = len(lines)
        for j in range(i + 1, len(lines)):
            h = re.match(r"^(#{1,6})\s", lines[j])
            if h and not in_code[j] and len(h.group(1)) <= level:
                end = j
                break
        if tid in tasks:
            sys.exit(f"task {tid} appears twice (lines {tasks[tid]['line']} and {i + 1})")
        tasks[tid] = {"id": tid, "title": title, "line": i + 1, "text": "\n".join(lines[i:end]).strip()}
    if not wanted:
        for t in tasks.values():
            print(f"{t['id']:6} line {t['line']:5}  {len(t['text']):7} chars  {t['title']}")
        return
    missing = [w for w in wanted if w not in tasks]
    if missing:
        sys.exit(f"tasks not found: {missing}")
    rows = []
    for w in wanted:
        t = tasks[w]
        if files_dir:
            os.makedirs(files_dir, exist_ok=True)
            path = os.path.join(files_dir, f"{t['id']}.md")
            with open(path, "w", encoding="utf-8") as f:
                f.write(t["text"] + "\n")
            rows.append({"id": t["id"], "title": t["title"], "file": os.path.abspath(path), "chars": len(t["text"])})
        else:
            rows.append({"id": t["id"], "title": t["title"], "text": t["text"]})
    with open(out, "w", encoding="utf-8") as f:
        json.dump(rows, f, ensure_ascii=False, indent=1)
    print(f"{out}: {len(rows)} tasks, {sum(len(tasks[w]['text']) for w in wanted)} chars")


if __name__ == "__main__":
    main()
