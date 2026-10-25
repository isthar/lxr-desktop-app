#!/usr/bin/env python3
"""Split docs/lichomat/tracker.json into one JSON file of row objects per tracker dataset.

    python3 tools/tracker_export.py [out_dir]     # default: build/tracker

Writes status.json (one row), epics.json, tasks.json, milestones.json, questions.json and
log.json (newest first), the files the LICHOMAT Tracker artifact reads.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs", "lichomat", "tracker.json")
STATUSES = {"zrobione", "w toku", "do zrobienia", "zablokowane"}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "tracker")
    os.makedirs(out, exist_ok=True)
    with open(SRC, encoding="utf-8") as f:
        t = json.load(f)

    epics = {e["id"] for e in t["epics"]}
    for task in t["tasks"]:
        if task["epic"] not in epics:
            sys.exit(f"task {task['id']} names unknown epic {task['epic']}")
        if task["status"] not in STATUSES:
            sys.exit(f"task {task['id']} has unknown status {task['status']!r}")
    ids = [x["id"] for x in t["tasks"]]
    if len(ids) != len(set(ids)):
        sys.exit("duplicate task ids")
    log = t.get("log", [])
    for entry in log:
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}", entry.get("at", "")) or not entry.get("text"):
            sys.exit(f"log entry {entry.get('n')} needs 'at' (YYYY-MM-DD HH:MM) and 'text'")
    numbers = [e["n"] for e in log]
    if len(numbers) != len(set(numbers)):
        sys.exit("duplicate log entry numbers")

    files = {
        "status.json": [t["status"]],
        "epics.json": sorted(t["epics"], key=lambda e: e["order"]),
        "tasks.json": t["tasks"],
        "milestones.json": t["milestones"],
        "questions.json": t["questions"],
        "log.json": sorted(log, key=lambda e: e["n"], reverse=True),  # by number: local times jump back at the end of DST
    }
    for name, rows in files.items():
        path = os.path.join(out, name)
        text = json.dumps(rows, ensure_ascii=False, indent=1)
        try:
            with open(path, encoding="utf-8") as f:
                state = "unchanged" if f.read() == text else "changed"
        except OSError:
            state = "new"
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"{path}: {len(rows)} rows, {state}")


if __name__ == "__main__":
    main()
