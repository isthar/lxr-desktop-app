"""Estimate the share of the weekly usage limit spent so far, from local Claude Code transcripts:

    python3 -I usage_estimate.py [--reading=<UTC ISO>@<percent> ...]

The meter itself can't be read from here. The script sums an API-price-weighted cost of every
assistant message (deduplicated by message id) in ~/.claude/projects/**/*.jsonl since the weekly
reset. The owner's readings of the meter calibrate it: the cost between the last reading and the
latest earlier one at least 5 percent lower gives the cost of one percent, and the estimate is the
last reading plus the cost since it.
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
    ("2026-10-31T21:29:00Z", 74.0),
    ("2026-11-01T04:30:00Z", 74.0),
    ("2026-11-01T05:43:00Z", 75.0),
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
# The rate comes from the last reading and the latest earlier one at least MIN_SPAN percent lower
# (else the lowest): the meter shows whole percents, so over one or two points its rounding alone
# can halve or double the rate (74% then 75% 73 minutes later gave 4.1M a percent, not about 8M).
MIN_SPAN = 5
t1, p1 = READINGS[-1]
c1 = cost_until(ts(t1))
earlier = [(t0, p0) for t0, p0 in READINGS[:-1] if p0 < p1 and cost_until(ts(t0)) < c1]
if not earlier:
    sys.exit("no earlier reading gives a rate")
t0, p0 = next((r for r in reversed(earlier) if r[1] <= p1 - MIN_SPAN), earlier[0])
c0 = cost_until(ts(t0))
per_pct = (c1 - c0) / (p1 - p0)
print(f"cost units since the reset: {total / 1e6:.1f}M ({len(seen)} messages)")
print(f"one percent is about {per_pct / 1e6:.2f}M cost units (between the readings of {t0} and {t1})")
print(f"estimated weekly share: {p1 + (total - c1) / per_pct:.1f}%")
