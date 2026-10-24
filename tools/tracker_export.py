#!/usr/bin/env python3
"""Split docs/lichomat/tracker.json into one JSON file of row objects per tracker dataset.

    python3 tools/tracker_export.py [out_dir]     # default: build/tracker

Writes status.json (one row), epics.json, tasks.json, milestones.json and questions.json,
the files the LICHOMAT Tracker artifact reads.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only
"""
import json
import os
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

    files = {
        "status.json": [t["status"]],
        "epics.json": sorted(t["epics"], key=lambda e: e["order"]),
        "tasks.json": t["tasks"],
        "milestones.json": t["milestones"],
        "questions.json": t["questions"],
    }
    for name, rows in files.items():
        with open(os.path.join(out, name), "w", encoding="utf-8") as f:
            json.dump(rows, f, ensure_ascii=False, indent=1)
        print(f"{out}/{name}: {len(rows)} rows")


if __name__ == "__main__":
    main()
