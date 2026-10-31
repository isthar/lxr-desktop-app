"""Estimate the share of the weekly usage limit spent so far, from local Claude Code transcripts:

    python3 -I usage_estimate.py [--reading=<UTC ISO>@<percent> ...]

The meter itself can't be read from here. The script sums an API-price-weighted cost of every
assistant message (deduplicated by message id) in ~/.claude/projects/**/*.jsonl since the weekly
reset. The owner's readings of the meter calibrate it: the cost between the last two readings
gives the cost of one percent, and the estimate is the last reading plus the cost since it.
The cost before the first reading does not scale the same way (2026-10-27: 19% at 161M units,
then 15 more points for 44M), so only the latest stretch is used. Usage outside Claude Code on
this Mac makes the estimate run high, which errs on the safe side.
"""
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
import glob, json, os, sys
from datetime import datetime

RESET = "2026-10-26T21:00:00Z"   # Monday 22:00 CET
READINGS = [                     # the owner's readings of the weekly meter
    ("2026-10-27T04:50:00Z", 19.0),
    ("2026-10-28T23:29:00Z", 34.0),
    ("2026-10-30T05:16:00Z", 49.0),
    ("2026-10-30T19:04:00Z", 53.0),
    ("2026-10-31T01:38:00Z", 57.0),
    ("2026-10-31T10:49:00Z", 65.0),
]
for a in sys.argv[1:]:
    if a.startswith("--reading="):
        t, p = a.split("=", 1)[1].rsplit("@", 1)
        READINGS.append((t, float(p)))
    else:
        sys.exit(__doc__)
READINGS.sort()


def ts(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00"))


reset = ts(RESET)
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


def cost_until(t):
    return sum(c for when, c in seen.values() if when <= t)


total = sum(c for _, c in seen.values())
(t0, p0), (t1, p1) = READINGS[-2], READINGS[-1]
c0, c1 = cost_until(ts(t0)), cost_until(ts(t1))
if p1 <= p0 or c1 <= c0:
    sys.exit("the last two readings give no rate")
per_pct = (c1 - c0) / (p1 - p0)
print(f"cost units since the reset: {total / 1e6:.1f}M ({len(seen)} messages)")
print(f"one percent is about {per_pct / 1e6:.2f}M cost units (between the readings of {t0} and {t1})")
print(f"estimated weekly share: {p1 + (total - c1) / per_pct:.1f}%")
