"""Estimate the share of the weekly usage limit spent so far, from local Claude Code transcripts:

    python3 -I usage_estimate.py [--reset=<UTC ISO>] [--cal=<UTC ISO>@<percent>]

The meter itself can't be read from here. The script sums an API-price-weighted cost of every
assistant message (deduplicated by message id) in ~/.claude/projects/**/*.jsonl since the weekly
reset, and scales it by a calibration point: a share the owner read off the meter at a known time.
It doesn't see usage outside Claude Code on this Mac, so keep a margin.
"""
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
import glob, json, os, sys
from datetime import datetime

RESET = "2026-10-26T21:00:00Z"        # Monday 22:00 CET
CAL = ("2026-10-27T04:50:00Z", 19.0)  # the owner saw 19% at 05:50 CET
for a in sys.argv[1:]:
    if a.startswith("--reset="):
        RESET = a.split("=", 1)[1]
    elif a.startswith("--cal="):
        t, p = a.split("=", 1)[1].rsplit("@", 1)
        CAL = (t, float(p))
    else:
        sys.exit(__doc__)


def ts(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00"))


reset, cal_t = ts(RESET), ts(CAL[0])
TOKEN_WEIGHT = {"input_tokens": 1.0, "cache_creation_input_tokens": 1.25,
                "cache_read_input_tokens": 0.1, "output_tokens": 5.0}
MODEL_WEIGHT = {"sonnet": 0.6, "haiku": 0.2}  # relative to Opus; anything else counts as 1

seen = {}
for f in glob.glob(os.path.expanduser("~/.claude/projects/**/*.jsonl"), recursive=True):
    try:
        if os.path.getmtime(f) < reset.timestamp():
            continue
        fh = open(f, errors="replace")
    except OSError:
        continue
    with fh:
        for line in fh:
            if '"usage"' not in line:
                continue
            try:
                e = json.loads(line)
            except ValueError:
                continue
            m = e.get("message") or {}
            u = m.get("usage")
            if not isinstance(u, dict) or "timestamp" not in e:
                continue
            t = ts(e["timestamp"])
            if t < reset:
                continue
            model = (m.get("model") or "").lower()
            mw = next((v for k, v in MODEL_WEIGHT.items() if k in model), 1.0)
            c = mw * sum(w * (u.get(k) or 0) for k, w in TOKEN_WEIGHT.items())
            key = m.get("id") or (f, e.get("uuid"))
            if key not in seen or c > seen[key][1]:
                seen[key] = (t, c)

total = sum(c for _, c in seen.values())
before = sum(c for t, c in seen.values() if t <= cal_t)
print(f"cost units since the reset: {total / 1e6:.1f}M ({len(seen)} messages)")
if not before:
    sys.exit("no usage before the calibration point; pass --cal")
print(f"one percent is about {before / CAL[1] / 1e6:.2f}M cost units")
print(f"estimated weekly share: {CAL[1] * total / before:.1f}%")
