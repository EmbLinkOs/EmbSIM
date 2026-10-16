#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Documentation integrity: every relative Markdown link resolves, every
architecture document and specification is listed in its index, every
requirement ID is unique and well-formed, and README links STATUS.md."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LINK = re.compile(r"\[[^\]]*\]\(([^)#\s]+)(#[^)]*)?\)")
REQ = re.compile(r"^### ([A-Z]+)-(\d{3})  ", re.M)


def main():
    problems = []
    md = [p for p in ROOT.rglob("*.md") if not (set(p.parts) & {"node_modules", "third_party", "build", "target"})]
    for p in md:
        text = p.read_text(encoding="utf-8")
        for m in LINK.finditer(text):
            target = m.group(1)
            if re.match(r"^[a-z]+:", target):
                continue
            if not (p.parent / target).resolve().exists():
                problems.append(f"{p.relative_to(ROOT)}: broken link {target}")
    arch = ROOT / "docs/architecture"
    index = (arch / "README.md").read_text()
    for p in sorted(arch.glob("0*.md")):
        if p.name not in index:
            problems.append(f"docs/architecture/README.md: {p.name} not listed")
    specs = ROOT / "docs/specs"
    index = (specs / "index.md").read_text()
    for p in sorted(specs.glob("SPEC-*.md")):
        if p.name not in index:
            problems.append(f"docs/specs/index.md: {p.name} not listed")
    seen = set()
    for p in (ROOT / "docs/requirements").glob("*.md"):
        for m in REQ.finditer(p.read_text()):
            rid = f"{m.group(1)}-{m.group(2)}"
            if m.group(1) != p.stem:
                problems.append(f"{p.name}: requirement {rid} in the wrong group file")
            if rid in seen:
                problems.append(f"{p.name}: duplicate requirement {rid}")
            seen.add(rid)
    if "docs/STATUS.md" not in (ROOT / "README.md").read_text():
        problems.append("README.md does not link docs/STATUS.md")
    for line in problems:
        print(line)
    print(f"{len(md)} documents, {len(seen)} requirements, {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
