#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Protocol conformance (SPEC-008 §10): replay tests/protocol/cases/*.json
against `embsim sidecar` (stdio) and `embsim serve` (WebSocket), check each
response against its expected shape and check that both transports agree.
Exit 0 when all passed, 1 on any failure, 2 on a usage problem."""
from __future__ import annotations

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "tools"))

from embsim_client import EmbsimError, StdioClient, TransportError, WebSocketClient  # noqa: E402

PLACEHOLDERS = {
    "<any>": lambda v: True,
    "<string>": lambda v: isinstance(v, str),
    "<number>": lambda v: isinstance(v, (int, float)) and not isinstance(v, bool),
    "<bool>": lambda v: isinstance(v, bool),
    "<array>": lambda v: isinstance(v, list),
    "<object>": lambda v: isinstance(v, dict),
    "<ps>": lambda v: isinstance(v, str) and re.fullmatch(r"[0-9]+", v) is not None,
}


def match(expected: Any, actual: Any, path: str = "$") -> list[str]:
    """Structural match; returns the mismatches as 'path: reason' strings."""
    if isinstance(expected, str) and expected in PLACEHOLDERS:
        return [] if PLACEHOLDERS[expected](actual) else [f"{path}: expected {expected}, got {actual!r}"]
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return [f"{path}: expected an object, got {type(actual).__name__}"]
        problems = []
        for k, v in expected.items():
            if k not in actual:
                problems.append(f"{path}.{k}: missing")
            else:
                problems += match(v, actual[k], f"{path}.{k}")
        return problems
    if isinstance(expected, list):
        if not isinstance(actual, list):
            return [f"{path}: expected an array, got {type(actual).__name__}"]
        if len(expected) != len(actual):
            return [f"{path}: expected {len(expected)} items, got {len(actual)}"]
        problems = []
        for i, (e, a) in enumerate(zip(expected, actual)):
            problems += match(e, a, f"{path}[{i}]")
        return problems
    if expected != actual or type(expected) is not type(actual):
        return [f"{path}: expected {expected!r}, got {actual!r}"]
    return []


def strip_volatile(value: Any, volatile: list[str]) -> Any:
    if isinstance(value, dict):
        return {k: strip_volatile(v, volatile) for k, v in value.items() if k not in volatile}
    if isinstance(value, list):
        return [strip_volatile(v, volatile) for v in value]
    return value


def load_cases(directory: Path) -> list[dict]:
    cases = []
    for p in sorted(directory.glob("*.json")):
        with p.open(encoding="utf-8") as f:
            case = json.load(f)
        case.setdefault("name", p.stem)
        case["_file"] = p.name
        if "method" not in case or "expect" not in case:
            raise ValueError(f"{p}: a case needs 'method' and 'expect'")
        cases.append(case)
    return cases


def run_case(client, case: dict) -> tuple[dict, list[str]]:
    """Returns ({'result': ...} or {'error': ...}, problems)."""
    try:
        result = client.call(case["method"], case.get("params", {}), timeout=case.get("timeout", 30.0))
        outcome = {"result": result}
    except EmbsimError as e:
        outcome = {"error": {"code": e.code, "message": e.message, "data": e.data}}
    expect = case["expect"]
    problems = []
    if "error" in expect:
        if "error" not in outcome:
            problems.append(f"expected an error, got result {json.dumps(outcome['result'])[:200]}")
        else:
            problems += match(expect["error"], outcome["error"], "$.error")
    else:
        if "error" in outcome:
            problems.append(f"unexpected error {outcome['error']['code']}: {outcome['error']['message']}")
        else:
            problems += match(expect.get("result", "<any>"), outcome["result"], "$.result")
    return outcome, problems


def wait_for_port(port: int, proc: subprocess.Popen, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            return False
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return True
        except OSError:
            time.sleep(0.05)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True, help="path to the embsim binary")
    ap.add_argument("--project", required=True, help="project file given to sidecar/serve")
    ap.add_argument("--port", type=int, default=18765, help="loopback port for `embsim serve`")
    ap.add_argument("--cases", default=str(HERE / "cases"), help="fixture directory")
    ap.add_argument("--transport", choices=["stdio", "ws", "both"], default="both")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    embsim = Path(args.embsim)
    if not embsim.exists():
        print(f"error: embsim binary not found: {embsim}", file=sys.stderr)
        return 2
    if not Path(args.project).exists():
        print(f"error: project not found: {args.project}", file=sys.stderr)
        return 2
    try:
        cases = load_cases(Path(args.cases))
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    if not cases:
        print("error: no cases found", file=sys.stderr)
        return 2

    transports = ["stdio", "ws"] if args.transport == "both" else [args.transport]
    outcomes: dict[str, dict[str, dict]] = {t: {} for t in transports}
    failures = 0
    total = 0

    for transport in transports:
        print(f"== {transport}")
        client = None
        server = None
        try:
            if transport == "stdio":
                client = StdioClient([str(embsim), "sidecar", args.project])
            else:
                server = subprocess.Popen([str(embsim), "serve", args.project, "--port", str(args.port)],
                                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if not wait_for_port(args.port, server, 15.0):
                    print(f"  FAIL: embsim serve did not listen on {args.port}")
                    failures += len(cases)
                    total += len(cases)
                    continue
                client = WebSocketClient(f"ws://127.0.0.1:{args.port}/ws")
            for case in cases:
                total += 1
                try:
                    outcome, problems = run_case(client, case)
                except TransportError as e:
                    outcome, problems = {"error": {"code": None, "message": str(e)}}, [f"transport: {e}"]
                outcomes[transport][case["_file"]] = outcome
                status = "ok" if not problems else "FAIL"
                if problems:
                    failures += 1
                print(f"  {status:4} {case['_file']}: {case['name']}")
                if args.verbose:
                    print(f"       -> {case['method']} {json.dumps(case.get('params', {}))}")
                    print(f"       <- {json.dumps(outcome)[:400]}")
                for p in problems:
                    print(f"       {p}")
                if case["method"] == "session.close":
                    break
        except (TransportError, OSError) as e:
            print(f"  FAIL: {transport} transport: {e}")
            failures += 1
            total += 1
        finally:
            if client is not None:
                try:
                    client.close()
                except Exception:  # noqa: BLE001 - closing is best effort
                    pass
            if server is not None:
                if server.poll() is None:
                    server.terminate()
                    try:
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill()

    if len(transports) == 2:
        print("== stdio vs ws")
        for case in cases:
            a = outcomes["stdio"].get(case["_file"])
            b = outcomes["ws"].get(case["_file"])
            if a is None or b is None or case.get("skip_compare"):
                continue
            total += 1
            volatile = list(case.get("volatile", []))
            if strip_volatile(a, volatile) != strip_volatile(b, volatile):
                failures += 1
                print(f"  FAIL {case['_file']}: transports differ")
                print(f"       stdio: {json.dumps(a)[:300]}")
                print(f"       ws:    {json.dumps(b)[:300]}")
            else:
                print(f"  ok   {case['_file']}")

    print(f"\n{total - failures} passed, {failures} failed, {total} checks")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
