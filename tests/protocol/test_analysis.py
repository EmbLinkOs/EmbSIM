#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The firmware's coverage and profile (SPEC-008 §5 analysis, SPEC-012 §4):

  - every example whose boards run firmware, its first scenario run with
    --coverage and --profile as JSON, then again as lcov and collapsed
    stacks: each board covers some of its lines and functions; the profile's
    functions add up to its instructions and cycles; the lcov tracefile
    agrees with the JSON, line for line; the collapsed stacks add up to the
    cycles; and the two runs, in two processes, measure the same;
  - the gcov-style text shows the source with its counts;
  - a project without firmware is refused (exit 2);
  - over the protocol (`embsim sidecar`): analysis.coverage before
    analysis.start is an error; after it, and a run, both reports come back,
    as JSON and as text; analysis.stop turns them off.

    test_analysis.py --embsim PATH [--only NAME,...]
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from embsim_client import EmbsimError, StdioClient  # noqa: E402


def examples(only):
    for project in sorted((ROOT / "examples").glob("*/project.json")):
        doc = json.loads(project.read_text())
        scen = doc.get("scenarios") or []
        if not scen or not any(b.get("firmware") for b in doc.get("boards") or []):
            continue
        if only and project.parent.name not in only:
            continue
        yield project, project.parent / scen[0]


def run(cmd, timeout=600):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=timeout, cwd=str(ROOT))


def lcov_records(text):
    """{(board, path): {"DA": {line: count}, "LF", "LH", "FNF", "FNH"}}"""
    out, board, cur = {}, "", None
    for line in text.splitlines():
        key, _, val = line.partition(":")
        if key == "TN":
            board = val
        elif key == "SF":
            cur = out.setdefault((board, val), {"DA": {}})
        elif key == "DA" and cur is not None:
            ln, count = val.split(",")[:2]
            cur["DA"][int(ln)] = int(count)
        elif key in ("LF", "LH", "FNF", "FNH") and cur is not None:
            cur[key] = int(val)
        elif line == "end_of_record":
            cur = None
    return out


def check_example(embsim, project, scenario, tmp):
    name = project.parent.name
    problems = []
    cov_j, prof_j = tmp / f"{name}-cov.json", tmp / f"{name}-prof.json"
    cov_l, prof_c = tmp / f"{name}.lcov", tmp / f"{name}.folded"
    a = run([embsim, "run", project, "--scenario", scenario, "--json", "--coverage", cov_j, "--coverage-format",
             "json", "--profile", prof_j, "--profile-format", "json"])
    b = run([embsim, "run", project, "--scenario", scenario, "--json", "--coverage", cov_l, "--coverage-format",
             "lcov", "--profile", prof_c, "--profile-format", "collapsed"])
    if a.returncode or b.returncode:
        return [f"runs exited {a.returncode} and {b.returncode}: {a.stderr.strip()} {b.stderr.strip()}"]
    ra, rb = json.loads(a.stdout), json.loads(b.stdout)
    cov, prof = json.loads(cov_j.read_text()), json.loads(prof_j.read_text())
    # the two runs, in two processes, measure the same
    if ra.get("coverage") != rb.get("coverage") or ra.get("profile") != rb.get("profile"):
        problems.append("two runs measured differently")
    if set(ra.get("coverage") or {}) != set(cov) or not cov:
        problems.append(f"the report's coverage names {sorted(ra.get('coverage') or {})}, the file {sorted(cov)}")
    lcov = lcov_records(cov_l.read_text())
    folded = {}
    for line in prof_c.read_text().splitlines():
        stack, _, w = line.rpartition(" ")
        board = stack.split(";")[0] if len(prof) > 1 else next(iter(prof))
        folded[board] = folded.get(board, 0) + int(w)
    for board, c in cov.items():
        lines, fns = c["lines"], c["functions"]
        if not (0 < lines["hit"] <= lines["total"]) or not (0 < fns["hit"] <= fns["total"]):
            problems.append(f"{board}: coverage {lines} {fns}")
        for f in c["files"]:
            rec = lcov.get((board, f["path"]))
            counts = {ln: n for ln, n in f.get("counts", [])}
            if rec is None:
                if counts:
                    problems.append(f"{board}: {f['path']} is not in the lcov tracefile")
                continue
            if rec["DA"] != counts or rec.get("LF") != f["lines"]["total"] or rec.get("LH") != f["lines"]["hit"]:
                problems.append(f"{board}: {f['path']}: lcov and JSON disagree")
            if rec.get("LH") != sum(1 for n in rec["DA"].values() if n):
                problems.append(f"{board}: {f['path']}: LH is not the lines hit")
    for board, p in prof.items():
        fns = p["functions"]
        si, sc = sum(f["self_insns"] for f in fns), sum(f["self_cycles"] for f in fns)
        if si != p["insns"] or sc != p["cycles"] or not p["insns"]:
            problems.append(f"{board}: the functions add up to {si} insns, {sc} cycles, not {p['insns']}, {p['cycles']}")
        if not any(f["name"] in ("main", "_main") and f["incl_insns"] for f in fns):
            problems.append(f"{board}: no time in main")
        if p["cycles"] > p["elapsed_cycles"] * 2:
            problems.append(f"{board}: {p['cycles']} cycles run in {p['elapsed_cycles']} elapsed")
        if folded.get(board) != p["cycles"]:
            problems.append(f"{board}: the collapsed stacks add up to {folded.get(board)}, not {p['cycles']}")
    if not problems:
        boards = ", ".join(f"{b} {c['lines']['hit']}/{c['lines']['total']} lines" for b, c in cov.items())
        print(f"ok    {name}: {boards}; profile, lcov and collapsed stacks agree")
    return problems


def check_protocol(embsim):
    problems = []
    project = ROOT / "examples" / "bluepill-button-led" / "project.json"
    client = StdioClient([str(embsim), "sidecar", str(project)], cwd=str(ROOT))
    try:
        client.call("session.open", {"client": "test_analysis/1", "protocol": "1.0"})
        try:
            client.call("analysis.coverage", {})
            problems.append("analysis.coverage before analysis.start succeeded")
        except EmbsimError as e:
            if e.code != -32001:
                problems.append(f"analysis.coverage before analysis.start: {e.code}")
        started = client.call("analysis.start", {})
        if started != {"coverage": True, "profile": True}:
            problems.append(f"analysis.start: {started}")
        client.call("control.advance", {"until": "20ms"})
        cov = client.call("analysis.coverage", {})["boards"]["B1"]
        if not cov["lines"]["hit"] or not any(f.get("counts") for f in cov["files"]):
            problems.append(f"analysis.coverage: {cov['lines']}")
        prof = client.call("analysis.profile", {"detail": False})["boards"]["B1"]
        if not prof["functions"] or prof["functions"][0]["name"] != "main":
            problems.append(f"analysis.profile: {prof['functions'][:2]}")
        text = client.call("analysis.coverage", {"format": "text"})["text"]
        if "main.c:" not in text or "#####" not in text:
            problems.append("analysis.coverage as text has no source")
        folded = client.call("analysis.profile", {"format": "collapsed"})["text"]
        if "Reset_Handler;main " not in folded:
            problems.append("analysis.profile as collapsed stacks has no Reset_Handler;main")
        client.call("analysis.stop", {})
        try:
            client.call("analysis.profile", {})
            problems.append("analysis.profile after analysis.stop succeeded")
        except EmbsimError:
            pass
    finally:
        client.close()
    if not problems:
        print("ok    the protocol: analysis.start, coverage and profile as JSON and text, analysis.stop")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    only = {x for x in args.only.split(",") if x}
    failures = checked = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for project, scenario in examples(only):
            problems = check_example(args.embsim, project, scenario, tmp)
            for p in problems:
                print(f"FAIL  {project.parent.name}: {p}")
            failures += bool(problems)
            checked += 1
        # the gcov-style text: the source beside the counts
        ex = ROOT / "examples" / "bluepill-button-led"
        out = tmp / "cov.txt"
        r = run([args.embsim, "run", ex / "project.json", "--until", "20ms", "--coverage", out])
        text = out.read_text() if out.exists() else ""
        if r.returncode or "./main.c: " not in text or ":   73:{" not in text or "total: " not in text:
            print(f"FAIL  the coverage text: exit {r.returncode}: {text[:300]}")
            failures += 1
        else:
            print("ok    the coverage text shows the source beside the counts")
        # no firmware: refused
        r = run([args.embsim, "run", ROOT / "examples" / "button-led" / "project.json", "--coverage", tmp / "x"])
        if r.returncode != 2 or "firmware" not in r.stderr:
            print(f"FAIL  a project without firmware: exit {r.returncode}: {r.stderr.strip()}")
            failures += 1
        else:
            print("ok    a project without firmware is refused")
    if not only:
        problems = check_protocol(args.embsim)
        for p in problems:
            print(f"FAIL  the protocol: {p}")
        failures += bool(problems)
    print(f"{checked} example(s) measured, {failures} failure(s)")
    return 1 if failures or not checked else 0


if __name__ == "__main__":
    sys.exit(main())
