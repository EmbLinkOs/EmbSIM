#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Tree-wide checks from docs/CODING-STANDARD.md: banned constructs in the
core, host-clock use outside core/host, process termination in the library,
and SPDX headers on source files."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = [ROOT / "core", ROOT / "include"]
BANNED = [
    (re.compile(r"\balloca\s*\("), "alloca"),
    (re.compile(r"\bsetjmp\s*\(|\blongjmp\s*\("), "setjmp/longjmp"),
    (re.compile(r"\b_Thread_local\b|\b__thread\b"), "thread-local storage"),
    (re.compile(r"\blong\s+double\b"), "long double"),
    (re.compile(r"\b(exit|abort)\s*\("), "process termination"),
    (re.compile(r"\b(printf|puts|putchar|fprintf\s*\(\s*std(out|err))\s*\("), "standard stream output"),
]
HOST_CLOCK = re.compile(r"\b(time|clock|gettimeofday|clock_gettime|QueryPerformanceCounter|GetTickCount64?)\s*\(")
SPDX = re.compile(r"SPDX-License-Identifier: Apache-2.0")
SOURCE_SUFFIXES = {".c", ".h", ".py", ".ts", ".tsx", ".rs", ".yml", ".yaml", ".cmake"}
SKIP_DIRS = {"third_party", "node_modules", "build", "target", ".git", "dist"}


def files(roots, suffixes):
    for root in roots:
        if not root.exists():
            continue
        for p in root.rglob("*"):
            if p.is_file() and p.suffix in suffixes and not (set(p.parts) & SKIP_DIRS):
                yield p


def main():
    problems = []
    for p in files(CORE, {".c", ".h"}):
        text = p.read_text(encoding="utf-8", errors="replace")
        for i, raw in enumerate(text.splitlines(), 1):
            if raw.lstrip().startswith(("/*", "*", "//")):
                continue
            line = re.sub(r"/\*.*?\*/", "", raw)  # inline block comments
            line = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)  # string literals
            for rx, what in BANNED:
                if rx.search(line):
                    problems.append(f"{p.relative_to(ROOT)}:{i}: banned {what}")
            if "core/host" not in p.as_posix() and HOST_CLOCK.search(line):
                problems.append(f"{p.relative_to(ROOT)}:{i}: host clock outside core/host")
    for p in files([ROOT / "core", ROOT / "include", ROOT / "cli", ROOT / "tools", ROOT / "tests", ROOT / ".github"], SOURCE_SUFFIXES):
        head = p.read_text(encoding="utf-8", errors="replace")[:400]
        if not SPDX.search(head):
            problems.append(f"{p.relative_to(ROOT)}: missing SPDX header")
    for line in problems:
        print(line)
    print(f"{len(problems)} finding(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
