#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Run every example project headless through a built `embsim` CLI.

    run_examples.py --build DIR [--examples DIR] [--timeout S] [--json]

For each examples/*/project.json this runs `embsim validate PROJECT` and then
`embsim run PROJECT --scenario S --until T --json` for each scenario the
project lists (or once with `--until 50ms` when it lists none), where T is the
scenario's `until`. Exit codes follow SPEC-008 §9 (0 success, 1 assertion
failed, 2 load error, 3 fatal diagnostic). The script prints a table and
exits 1 if any command failed, 2 if the CLI binary is missing.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_UNTIL = "50ms"
EXIT_NAMES = {0: "ok", 1: "assertion failed", 2: "load error", 3: "fatal diagnostic", 4: "replay divergence", 5: "build failed"}


def find_cli(build: Path) -> Path | None:
    names = ["embsim.exe", "embsim"] if os.name == "nt" else ["embsim"]
    for sub in ("bin", "cli", "cli/Release", "cli/Debug", "Release", "Debug", "."):
        for n in names:
            p = build / sub / n
            if p.is_file() and os.access(p, os.X_OK):
                return p
    return None


def run(cmd: list[str], timeout: float) -> tuple[int, str, str, float]:
    t0 = time.monotonic()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=str(ROOT))
        return proc.returncode, proc.stdout, proc.stderr, time.monotonic() - t0
    except subprocess.TimeoutExpired as exc:
        return 124, exc.stdout or "", f"timeout after {timeout} s", time.monotonic() - t0
    except OSError as exc:
        return 127, "", str(exc), time.monotonic() - t0


def summarise_report(stdout: str) -> str:
    """Pull the assertion tally out of an `embsim run --json` report (SPEC-008 §8)."""
    try:
        doc = json.loads(stdout)
    except json.JSONDecodeError:
        for line in reversed(stdout.splitlines()):
            try:
                doc = json.loads(line)
                break
            except json.JSONDecodeError:
                continue
        else:
            return "-"
    if not isinstance(doc, dict):
        return "-"
    assertions = doc.get("assertions") or []
    passed = sum(1 for a in assertions if a.get("ok"))
    ended = (doc.get("ended") or {}).get("reason", "?")
    diags = len(doc.get("diagnostics") or [])
    return f"{passed}/{len(assertions)} assertions, ended {ended}, {diags} diag"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", required=True, type=Path, help="CMake build directory holding bin/embsim")
    ap.add_argument("--examples", type=Path, default=ROOT / "examples", help="directory of example projects")
    ap.add_argument("--timeout", type=float, default=120.0, help="seconds per command")
    ap.add_argument("--json", action="store_true", help="print the results as JSON instead of a table")
    ap.add_argument("--verbose", action="store_true", help="print each command's output")
    args = ap.parse_args(argv)

    cli = find_cli(args.build.resolve())
    if cli is None:
        print(f"run_examples: no embsim binary under {args.build} (looked in bin/, cli/)", file=sys.stderr)
        return 2
    projects = sorted(args.examples.glob("*/project.json"))
    if not projects:
        print(f"run_examples: no examples/*/project.json under {args.examples}", file=sys.stderr)
        return 2

    rows: list[dict] = []
    for project in projects:
        rel = project.relative_to(ROOT) if project.is_relative_to(ROOT) else project
        code, out, err, dt = run([str(cli), "validate", str(project)], args.timeout)
        rows.append({"example": project.parent.name, "step": "validate", "scenario": "-", "exit": code, "seconds": dt, "summary": err.strip().splitlines()[-1] if code and err.strip() else "-"})
        if args.verbose:
            print(out, err, sep="")
        try:
            with open(project, "r", encoding="utf-8") as fh:
                doc = json.load(fh)
        except (OSError, json.JSONDecodeError) as exc:
            rows.append({"example": project.parent.name, "step": "run", "scenario": "-", "exit": 2, "seconds": 0.0, "summary": f"cannot read project: {exc}"})
            continue
        scenarios = [project.parent / s for s in doc.get("scenarios") or []]
        if not scenarios:
            code, out, err, dt = run([str(cli), "run", str(project), "--until", DEFAULT_UNTIL, "--json"], args.timeout)
            rows.append({"example": project.parent.name, "step": "run", "scenario": "-", "exit": code, "seconds": dt,
                         "summary": summarise_report(out) if code in (0, 1) else (err.strip().splitlines() or ["-"])[-1]})
            if args.verbose:
                print(out, err, sep="")
        for scen in scenarios:
            until = DEFAULT_UNTIL
            try:
                with open(scen, "r", encoding="utf-8") as fh:
                    until = json.load(fh).get("until", DEFAULT_UNTIL)
            except (OSError, json.JSONDecodeError):
                pass
            code, out, err, dt = run([str(cli), "run", str(project), "--scenario", str(scen), "--until", until, "--json"], args.timeout)
            rows.append({"example": project.parent.name, "step": "run", "scenario": scen.name, "exit": code, "seconds": dt,
                         "summary": summarise_report(out) if code in (0, 1) else (err.strip().splitlines() or ["-"])[-1]})
            if args.verbose:
                print(out, err, sep="")

    failed = [r for r in rows if r["exit"] != 0]
    if args.json:
        print(json.dumps({"cli": str(cli), "results": rows, "failed": len(failed)}, indent=2))
    else:
        widths = {"example": max(7, max(len(r["example"]) for r in rows)), "step": 8, "scenario": max(8, max(len(r["scenario"]) for r in rows))}
        print(f"embsim: {cli}")
        print(f"{'example':<{widths['example']}}  {'step':<8}  {'scenario':<{widths['scenario']}}  {'exit':<18}  {'time':>6}  summary")
        for r in rows:
            status = f"{r['exit']} ({EXIT_NAMES.get(r['exit'], 'error')})"
            print(f"{r['example']:<{widths['example']}}  {r['step']:<8}  {r['scenario']:<{widths['scenario']}}  {status:<18}  {r['seconds']:>5.1f}s  {r['summary']}")
        print(f"{len(rows) - len(failed)} passed, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
