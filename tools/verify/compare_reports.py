#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Compare two run reports (`embsim run --json`, SPEC-008 §8), for a firmware
or project change in CI (SPEC-012 §4).

    compare_reports.py BEFORE.json AFTER.json [--json]

What differs is printed: the end (reason, time), each assertion's result and
what it observed (by id), the diagnostics (by code, with how many), and the
counted statistics. The wall time is ignored, and so is the engine's
version. Exit 0 when the two agree, 1 when they differ, 2 when a report
cannot be read.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

IGNORED_STATS = {"wall_time_s"}


def load(path: str) -> dict:
    text = Path(path).read_text()
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        # a report printed after other output: the last line that parses
        for line in reversed(text.splitlines()):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
        raise


def compare(a: dict, b: dict) -> list[dict]:
    """The differences between reports a (before) and b (after)."""
    out = []
    ea, eb = a.get("ended") or {}, b.get("ended") or {}
    for k in ("reason", "now_ps"):
        if ea.get(k) != eb.get(k):
            out.append({"what": f"ended.{k}", "before": ea.get(k), "after": eb.get(k)})
    aa = {x.get("id"): x for x in a.get("assertions") or []}
    ab = {x.get("id"): x for x in b.get("assertions") or []}
    for i in sorted(set(aa) | set(ab), key=lambda s: str(s)):
        x, y = aa.get(i), ab.get(i)
        if x is None or y is None:
            out.append({"what": f"assertion {i}", "before": "present" if x else "absent",
                        "after": "present" if y else "absent"})
            continue
        if x.get("ok") != y.get("ok"):
            out.append({"what": f"assertion {i}", "before": "pass" if x.get("ok") else "fail",
                        "after": "pass" if y.get("ok") else "fail", "observed": y.get("observed")})
        elif x.get("observed") != y.get("observed"):
            out.append({"what": f"assertion {i} observed", "before": x.get("observed"), "after": y.get("observed")})
    da = Counter(d.get("code") for d in a.get("diagnostics") or [])
    db = Counter(d.get("code") for d in b.get("diagnostics") or [])
    for code in sorted(set(da) | set(db), key=lambda s: str(s)):
        if da[code] != db[code]:
            out.append({"what": f"diagnostic {code}", "before": da[code], "after": db[code]})
    sa, sb = a.get("statistics") or {}, b.get("statistics") or {}
    for k in sorted(set(sa) | set(sb)):
        if k not in IGNORED_STATS and sa.get(k) != sb.get(k):
            out.append({"what": f"statistics.{k}", "before": sa.get(k), "after": sb.get(k)})
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("--json", action="store_true", help="print the differences as JSON")
    args = ap.parse_args(argv)
    try:
        a, b = load(args.before), load(args.after)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"compare_reports: {exc}", file=sys.stderr)
        return 2
    diffs = compare(a, b)
    if args.json:
        print(json.dumps({"same": not diffs, "differences": diffs}, indent=1))
    elif not diffs:
        print("the reports agree (wall time aside)")
    else:
        for d in diffs:
            extra = f" (observed {d['observed']!r})" if "observed" in d else ""
            print(f"{d['what']}: {d['before']!r} -> {d['after']!r}{extra}")
    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())
