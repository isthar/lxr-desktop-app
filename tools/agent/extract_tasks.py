"""Extract task sections from the stage 1 plan as JSON for a workflow.

    python3 -I extract_tasks.py <plan.md> <out.json> E1.1 E1.2 ...   (no ids: list all tasks found)

A task starts at a heading (## to ####) whose text names a task id such as E1.1, optionally after
the word Task, and runs until the next heading of the same or a higher level.
"""
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
import json
import re
import sys

HEAD = re.compile(r"^(#{2,4})\s+(?:Task\s+)?(E\d+\.\d+)\b[\s:.\-–—]*(.*)$")


def main():
    plan, out, wanted = sys.argv[1], sys.argv[2], sys.argv[3:]
    lines = open(plan, encoding="utf-8").read().split("\n")
    starts = []
    for i, line in enumerate(lines):
        m = HEAD.match(line)
        if m:
            starts.append((i, len(m.group(1)), m.group(2), m.group(3).strip()))
    tasks = {}
    for k, (i, level, tid, title) in enumerate(starts):
        end = len(lines)
        for j in range(i + 1, len(lines)):
            h = re.match(r"^(#{1,6})\s", lines[j])
            if h and len(h.group(1)) <= level:
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
    rows = [{"id": tasks[w]["id"], "title": tasks[w]["title"], "text": tasks[w]["text"]} for w in wanted]
    with open(out, "w", encoding="utf-8") as f:
        json.dump(rows, f, ensure_ascii=False, indent=1)
    print(f"{out}: {len(rows)} tasks, {sum(len(r['text']) for r in rows)} chars")


if __name__ == "__main__":
    main()
