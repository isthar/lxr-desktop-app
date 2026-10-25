#!/usr/bin/env python3
"""Add a timestamped entry to the work log in docs/lichomat/tracker.json and refresh the status.

    python3 tools/tracker_note.py "text" [--focus TEXT] [--next TEXT] [--phase TEXT] [--now TEXT]
                                         [--head SHA] [--at "YYYY-MM-DD HH:MM"]

The entry time defaults to now (local time). status.updated and status.updated_at take the
same time, and status.head takes the current git HEAD unless --head is given. --now sets
status.now, the line the page shows under "W toku" for the work running at the moment (an agent,
a review); --now "" clears it. Run
tools/tracker_export.py afterwards to regenerate the files the tracker page reads.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import datetime
import json
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs", "lichomat", "tracker.json")


def git_head():
    try:
        out = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True, check=True)
        return out.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("text", help="what happened, one or two sentences")
    ap.add_argument("--focus", help="new status.focus")
    ap.add_argument("--next", help="new status.next")
    ap.add_argument("--phase", help="new status.phase")
    ap.add_argument("--now", help='new status.now, what runs at the moment ("" clears it)')
    ap.add_argument("--head", help="commit to show as status.head (default: git HEAD)")
    ap.add_argument("--at", help='entry time "YYYY-MM-DD HH:MM" in local time (default: now)')
    args = ap.parse_args()

    if args.at:
        when = datetime.datetime.strptime(args.at, "%Y-%m-%d %H:%M").astimezone()
    else:
        when = datetime.datetime.now().astimezone().replace(second=0, microsecond=0)

    with open(SRC, encoding="utf-8") as f:
        t = json.load(f)

    log = t.setdefault("log", [])
    log.append({
        "n": max((e["n"] for e in log), default=0) + 1,
        "at": when.strftime("%Y-%m-%d %H:%M"),
        "text": args.text,
    })

    status = t["status"]
    status["updated"] = when.strftime("%Y-%m-%d %H:%M")
    status["updated_at"] = when.isoformat()
    for key in ("focus", "next", "phase"):
        value = getattr(args, key)
        if value:
            status[key] = value
    if args.now is not None:
        status["now"] = args.now
    head = args.head or git_head()
    if head:
        status["head"] = head

    with open(SRC, "w", encoding="utf-8") as f:
        json.dump(t, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print(f"log #{log[-1]['n']} at {log[-1]['at']}: {args.text}")


if __name__ == "__main__":
    main()
