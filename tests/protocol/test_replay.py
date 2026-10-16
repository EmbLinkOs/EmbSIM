#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Recordings and replay (SPEC-012, VER-001 to VER-003, MCU-006):

  - every example's first scenario recorded twice, in two processes: the two
    recordings observe the same things at the same times (determinism);
  - each recording replays with every observation matching (`embsim replay`);
  - one recording with an observation changed diverges there (exit 4, both
    values in the JSON report);
  - a copy of an example whose image changed is refused (exit 2), and
    replayed anyway with --allow-drift;
  - a recording turned into a scenario (`embsim record to-scenario`) passes;
  - the protocol's record.replay over `embsim sidecar` matches too.

    test_replay.py --embsim PATH [--only NAME,...]
"""
import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def examples(only):
    for project in sorted((ROOT / "examples").glob("*/project.json")):
        doc = json.loads(project.read_text())
        scen = doc.get("scenarios") or []
        if not scen or (only and project.parent.name not in only):
            continue
        yield project, project.parent / scen[0]


def run(cmd, timeout=300):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=timeout, cwd=str(ROOT))


def record(embsim, project, scenario, out):
    r = run([embsim, "run", project, "--scenario", scenario, "--record", out, "--json"])
    if r.returncode not in (0, 1):
        raise RuntimeError(f"{project}: run exited {r.returncode}: {r.stderr.strip()}")
    return json.loads(Path(out).read_text())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    only = {x for x in args.only.split(",") if x}
    failures = 0
    checked = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        first = None
        for project, scenario in examples(only):
            name = project.parent.name
            a, b = tmp / f"{name}-a.rec", tmp / f"{name}-b.rec"
            try:
                ra = record(args.embsim, project, scenario, a)
                rb = record(args.embsim, project, scenario, b)
            except RuntimeError as exc:
                print(f"FAIL  {exc}")
                failures += 1
                continue
            if ra["observations"] != rb["observations"] or ra["inputs"] != rb["inputs"]:
                diff = next((i for i, (x, y) in enumerate(zip(ra["observations"], rb["observations"])) if x != y),
                            min(len(ra["observations"]), len(rb["observations"])))
                print(f"FAIL  {name}: two runs differ at observation {diff + 1}")
                failures += 1
                continue
            r = run([args.embsim, "replay", a, "--json"])
            rep = json.loads(r.stdout) if r.stdout.strip() else {}
            if r.returncode != 0 or not rep.get("ok"):
                print(f"FAIL  {name}: replay exited {r.returncode}: {rep.get('divergence')}")
                failures += 1
                continue
            print(f"ok    {name}: {len(ra['observations'])} observations, identical twice and on replay")
            checked += 1
            if first is None and any(o["kind"] == "probe" for o in ra["observations"][1:]):
                first = (project, scenario, a, ra)
        if first is None:
            print("FAIL  no example recorded a probe change")
            return 1
        project, scenario, a, ra = first
        name = project.parent.name
        # an observation changed: replay diverges there
        bad = json.loads(json.dumps(ra))
        k = next(i for i, o in enumerate(bad["observations"]) if o["kind"] == "probe" and i > 0)
        bad["observations"][k]["at_ps"] = str(int(bad["observations"][k]["at_ps"]) + 1)
        (tmp / "bad.rec").write_text(json.dumps(bad))
        r = run([args.embsim, "replay", tmp / "bad.rec", "--json"])
        rep = json.loads(r.stdout) if r.stdout.strip() else {}
        d = rep.get("divergence") or {}
        if r.returncode != 4 or d.get("index") != k or d.get("expected") != bad["observations"][k] \
                or d.get("observed") != ra["observations"][k]:
            print(f"FAIL  {name}: a changed observation: exit {r.returncode}, {d}")
            failures += 1
        else:
            print(f"ok    {name}: a changed observation diverges at {k + 1}, both values reported (exit 4)")
        # a changed image: refused, then replayed with --allow-drift
        copy = tmp / "copy"
        shutil.copytree(project.parent, copy)
        rec = json.loads(json.dumps(ra))
        rec["project"]["path"] = str(copy / "project.json")
        imgs = rec.get("images") or []
        if imgs:
            img = copy / imgs[0]["path"]
            img.write_bytes(img.read_bytes() + b"\0")
            (tmp / "drift.rec").write_text(json.dumps(rec))
            r = run([args.embsim, "replay", tmp / "drift.rec"])
            r2 = run([args.embsim, "replay", tmp / "drift.rec", "--allow-drift", "--json"])
            rep2 = json.loads(r2.stdout) if r2.stdout.strip() else {}
            if r.returncode != 2 or "changed since the recording" not in r.stderr or r2.returncode != 0 \
                    or not rep2.get("drift"):
                print(f"FAIL  {name}: a changed image: exit {r.returncode} then {r2.returncode}")
                failures += 1
            else:
                print(f"ok    {name}: a changed image is refused, and replays with --allow-drift")
        # the recording as a scenario
        out = tmp / "from-recording.json"
        r = run([args.embsim, "record", "to-scenario", a, "-o", out])
        r2 = run([args.embsim, "run", project, "--scenario", out, "--json"]) if r.returncode == 0 else r
        rep = json.loads(r2.stdout) if r2.returncode in (0, 1) and r2.stdout.strip() else {}
        if r2.returncode != 0 or not rep.get("assertions"):
            print(f"FAIL  {name}: the recording as a scenario: exit {r2.returncode}")
            failures += 1
        else:
            print(f"ok    {name}: the recording as a scenario passes {len(rep['assertions'])} assertions")
        # record.replay over the protocol
        req = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "record.replay", "params": {"path": str(a)}})
        p = subprocess.run([args.embsim, "sidecar"], input=req + "\n", capture_output=True, text=True, timeout=300,
                           cwd=str(ROOT))
        resp = None
        for line in p.stdout.splitlines():
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if msg.get("id") == 1:
                resp = msg
        if not resp or not (resp.get("result") or {}).get("ok"):
            print(f"FAIL  {name}: record.replay over the protocol: {resp}")
            failures += 1
        else:
            print(f"ok    {name}: record.replay over the protocol matches")
    print(f"{checked} example(s) recorded twice and replayed, {failures} failure(s)")
    return 1 if failures or not checked else 0


if __name__ == "__main__":
    sys.exit(main())
