"""Print one line per new workflow journal event, and optionally per new commit:

    python3 wf_watch.py <workflow dir> <lines already seen> [label] [finished-only] [--git=<repo>]

With --git, every commit that appears on the repository's HEAD is printed too ("commit HH:MM sha subject"),
so the tracker can follow the commits agents make between the journal events.
"""
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
import json, os, subprocess, sys, time

git_repo = None
argv = []
for a in sys.argv[1:]:
    if a.startswith("--git="):
        git_repo = a[len("--git="):]
    else:
        argv.append(a)
wf, seen = argv[0], int(argv[1])
tag = (argv[2] + " ") if len(argv) > 2 else ""
finished_only = len(argv) > 3 and argv[3] == "finished-only"
journal = os.path.join(wf, "journal.jsonl")

def label(agent_id):
    try:
        with open(os.path.join(wf, f"agent-{agent_id}.meta.json")) as f:
            return json.load(f).get("description", agent_id)
    except (OSError, ValueError):
        return agent_id

def git(*args):
    try:
        return subprocess.run(["git", "-C", git_repo, *args], capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.SubprocessError):
        return ""

head = git("rev-parse", "HEAD").strip() if git_repo else ""

while True:
    if git_repo:
        now_head = git("rev-parse", "HEAD").strip()
        if now_head and head and now_head != head:
            now = time.strftime("%H:%M")
            for line in git("log", "--reverse", "--format=%h %s", f"{head}..{now_head}").splitlines():
                print(f"{tag}commit {now} {line}", flush=True)
        if now_head:
            head = now_head
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
