#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The firmware's coverage, profile, stacks and faults (SPEC-008 §5
analysis, SPEC-009 §12):

  - every example whose boards run firmware, its first scenario run with
    --coverage, --profile and --stack as JSON, then again as lcov, collapsed
    stacks and text: each board covers some of its lines and functions; the
    profile's functions add up to its instructions and cycles; the lcov
    tracefile agrees with the JSON, line for line; the collapsed stacks add
    up to the cycles; each board's stack went down from its top, within its
    limit, main's depth with its callees at least its own frame; and the two
    runs, in two processes, measure the same;
  - a firmware that recurses below its link's __stack_limit
    (tests/firmware/stack-overflow) raises mcu.stack_overflow once, naming
    the function, and its depths add up: main's with its callees is its own
    frame and the recursion's;
  - a firmware that loads from an address nothing answers
    (tests/firmware/fault) raises mcu.fault once: a HardFault escalated
    from a precise bus error, BFAR the address, the instruction, and a
    backtrace from .debug_frame through the calls that made it, the handler
    the strong symbol at the vector's address; with no fault, none;
  - the gcov-style text shows the source with its counts;
  - a project without firmware is refused (exit 2);
  - over the protocol (`embsim sidecar`): analysis.coverage before
    analysis.start is an error; after it, and a run, the reports come back,
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
    cov_j, prof_j, stk_j = tmp / f"{name}-cov.json", tmp / f"{name}-prof.json", tmp / f"{name}-stack.json"
    cov_l, prof_c, stk_t = tmp / f"{name}.lcov", tmp / f"{name}.folded", tmp / f"{name}-stack.txt"
    a = run([embsim, "run", project, "--scenario", scenario, "--json", "--coverage", cov_j, "--coverage-format",
             "json", "--profile", prof_j, "--profile-format", "json", "--stack", stk_j, "--stack-format", "json"])
    b = run([embsim, "run", project, "--scenario", scenario, "--json", "--coverage", cov_l, "--coverage-format",
             "lcov", "--profile", prof_c, "--profile-format", "collapsed", "--stack", stk_t])
    if a.returncode or b.returncode:
        return [f"runs exited {a.returncode} and {b.returncode}: {a.stderr.strip()} {b.stderr.strip()}"]
    ra, rb = json.loads(a.stdout), json.loads(b.stdout)
    cov, prof, stk = json.loads(cov_j.read_text()), json.loads(prof_j.read_text()), json.loads(stk_j.read_text())
    # the two runs, in two processes, measure the same
    if any(ra.get(k) != rb.get(k) for k in ("coverage", "profile", "stack")):
        problems.append("two runs measured differently")
    if set(stk) != set(cov) or set(ra.get("stack") or {}) != set(stk):
        problems.append(f"the stack reports name {sorted(stk)}, coverage {sorted(cov)}")
    text = stk_t.read_text()
    for board, st in stk.items():
        main = [s for s in st["stacks"] if s["core"] == 0][0]
        # a PIC24's grows up: its W15 values, deepest the highest
        up = "pic24" in name
        if not 0 < main.get("used", 0) == abs(main["top"] - main["deepest"]):
            problems.append(f"{board}: stack {main}")
        lo, hi = (main["top"], main.get("limit")) if up else (main.get("limit"), main["top"])
        if "limit" in main and not lo < main["deepest"] < hi:
            problems.append(f"{board}: the stack {main} is not within its limit")
        if "overflow" in st:
            problems.append(f"{board}: an overflow: {st['overflow']}")
        m = [f for f in st["functions"] if f["name"] in ("main", "_main")]
        if not m or m[0]["incl"] < m[0]["frame"]:
            problems.append(f"{board}: main's depth {m}")
        if f"embsim stack: {board}\n" not in text:
            problems.append(f"{board}: not in the stack report as text")
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
        depth = ", ".join(f"{b} {s['stacks'][0].get('used', 0)} bytes of stack" for b, s in stk.items())
        print(f"ok    {name}: {boards}; {depth}; profile, lcov and collapsed stacks agree")
    return problems


def check_overflow(embsim, tmp):
    problems = []
    elf = ROOT / "tests" / "firmware" / "stack-overflow" / "stack-overflow.elf"
    out = tmp / "overflow.txt"
    r = run([embsim, "run", "--target", "emblink/bluepill", "--image", elf, "--until", "5ms", "--json", "--stack", out])
    if r.returncode:
        return [f"exit {r.returncode}: {r.stderr.strip()}"]
    report = json.loads(r.stdout)
    st = report.get("stack", {}).get("B1", {})
    ovf = st.get("overflow", {})
    if ovf.get("function") != "deep" or ovf.get("limit_from") != "__stack_limit" or not ovf["sp"] < ovf["limit"]:
        problems.append(f"the overflow: {ovf}")
    diags = [d for d in report["diagnostics"] if d["code"] == "mcu.stack_overflow"]
    if len(diags) != 1 or diags[0]["severity"] != "error" or "(deep)" not in diags[0]["message"]:
        problems.append(f"the diagnostics: {diags}")
    fns = {f["name"]: f for f in st.get("functions", [])}
    deep, main = fns.get("deep", {}), fns.get("main", {})
    if deep.get("frame", 0) < 48 or deep.get("incl", 0) < 40 * deep.get("frame", 0):
        problems.append(f"deep: {deep}")
    if main.get("incl") != main.get("frame", 0) + deep.get("incl", 0):
        problems.append(f"main's depth {main} is not its frame and deep's {deep.get('incl')}")
    msp = st["stacks"][0]
    if msp.get("size") != 2048 or msp.get("used", 0) <= 2048:
        problems.append(f"the main stack: {msp}")
    text = out.read_text() if out.exists() else ""
    if "OVERFLOW: sp 0x" not in text or "(__stack_limit), at 0x" not in text:
        problems.append(f"the text report: {text[:300]}")
    if not problems:
        print(f"ok    a recursion below __stack_limit: mcu.stack_overflow in deep, {msp['used']} of {msp['size']} bytes")
    return problems


def check_fault(embsim, tmp):
    problems = []
    elf = ROOT / "tests" / "firmware" / "fault" / "fault.elf"
    out = tmp / "faults.txt"
    r = run([embsim, "run", "--target", "emblink/bluepill", "--image", elf, "--until", "2ms", "--json",
             "--faults", out])
    if r.returncode:
        return [f"exit {r.returncode}: {r.stderr.strip()}"]
    report = json.loads(r.stdout)
    rep_f = report.get("faults", {}).get("B1", {})
    faults = rep_f.get("faults", [])
    if rep_f.get("count") != 1 or len(faults) != 1:
        return [f"the faults: {rep_f}"]
    f = faults[0]
    if f["name"] != "HardFault" or f["exception"] != 3 or not f["where"].startswith("peek (main.c:"):
        problems.append(f"the fault: {f['name']} {f.get('exception')} {f['where']}")
    if "FORCED, PRECISERR at 0x30000000" not in f["summary"]:
        problems.append(f"the summary: {f['summary']}")
    names = [b["where"].split(" ")[0].split("+")[0] for b in f["backtrace"]]
    if names != ["peek", "read_sensor", "main", "Reset_Handler"]:
        problems.append(f"the backtrace: {names}")
    if not f.get("handler", "").startswith("Default_Handler at 0x"):
        problems.append(f"the handler: {f.get('handler')}")
    diags = [d for d in report["diagnostics"] if d["code"] == "mcu.fault"]
    if len(diags) != 1 or diags[0]["severity"] != "error" or "PRECISERR" not in diags[0]["message"]:
        problems.append(f"the diagnostics: {diags}")
    text = out.read_text() if out.exists() else ""
    for want in ("BFAR  0x30000000 (BFARVALID)", "backtrace (.debug_frame):", "stacked frame at 0x", "instruction 0x"):
        if want not in text:
            problems.append(f"the text report has no {want!r}")
    # a firmware that does not fault: none
    ok = ROOT / "tests" / "firmware" / "stack-overflow" / "stack-overflow.elf"
    r = run([embsim, "run", "--target", "emblink/bluepill", "--image", ok, "--until", "2ms", "--json", "--faults", "-"])
    clean = json.loads(r.stdout).get("faults", {}).get("B1", {}) if not r.returncode else {}
    if clean.get("count") != 0 or clean.get("faults"):
        problems.append(f"a firmware without faults: {clean}")
    if not problems:
        print("ok    a load from nothing: mcu.fault, HardFault (FORCED, PRECISERR), peek <- read_sensor <- main")
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
        started = client.call("analysis.start", {"stack": True, "faults": True})
        if started != {"coverage": True, "profile": True, "stack": True, "faults": True}:
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
        stack = client.call("analysis.stack", {})["boards"]["B1"]
        if not stack["stacks"][0].get("used") or "overflow" in stack:
            problems.append(f"analysis.stack: {stack['stacks']}")
        text = client.call("analysis.stack", {"format": "text"})["text"]
        if "main stack (MSP): top 0x20005000" not in text:
            problems.append(f"analysis.stack as text: {text[:200]}")
        faults = client.call("analysis.faults", {})["boards"]["B1"]
        if faults != {"count": 0, "faults": []}:
            problems.append(f"analysis.faults: {faults}")
        client.call("analysis.stop", {})
        try:
            client.call("analysis.profile", {})
            problems.append("analysis.profile after analysis.stop succeeded")
        except EmbsimError:
            pass
    finally:
        client.close()
    if not problems:
        print("ok    the protocol: analysis.start, coverage, profile, stack and faults as JSON and text, analysis.stop")
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
        problems = check_overflow(args.embsim, tmp)
        for p in problems:
            print(f"FAIL  the stack overflow: {p}")
        failures += bool(problems)
        problems = check_fault(args.embsim, tmp)
        for p in problems:
            print(f"FAIL  the fault: {p}")
        failures += bool(problems)
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
