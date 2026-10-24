"""Print one line per new workflow journal event: python3 wf_watch.py <workflow dir> <lines already seen> [label] [finished-only]"""
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
import json, os, sys, time

wf, seen = sys.argv[1], int(sys.argv[2])
tag = (sys.argv[3] + " ") if len(sys.argv) > 3 else ""
finished_only = len(sys.argv) > 4 and sys.argv[4] == "finished-only"
journal = os.path.join(wf, "journal.jsonl")

def label(agent_id):
    try:
        with open(os.path.join(wf, f"agent-{agent_id}.meta.json")) as f:
            return json.load(f).get("description", agent_id)
    except (OSError, ValueError):
        return agent_id

while True:
    try:
        with open(journal) as f:
            complete = f.read().split("\n")[:-1]   # drop a partial last line
    except OSError:
        complete = []
    for line in complete[seen:]:
        seen += 1
        now = time.strftime("%H:%M")
        try:
            e = json.loads(line)
        except ValueError:
            print(f"{tag}#{seen} {now} unparsable journal line", flush=True)
            continue
        kind = e.get("type")
        if kind == "started":
            if finished_only:
                continue
            print(f"{tag}#{seen} {now} started {e.get('label')} (phase {e.get('phase')})", flush=True)
        elif kind == "result":
            r = e.get("result")
            text = ""
            if isinstance(r, dict):
                text = str(r.get("summary") or r.get("report") or r.get("area") or "")
            elif isinstance(r, str):
                text = r
            print(f"{tag}#{seen} {now} finished {label(e.get('agentId'))}: {text[:200]!r}", flush=True)
        else:
            print(f"{tag}#{seen} {now} {kind}: {json.dumps(e)[:200]}", flush=True)
    time.sleep(5)
