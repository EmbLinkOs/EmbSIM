# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Reject ambiguous extensionless module paths on case-insensitive filesystems."""
from pathlib import Path, PurePosixPath
import subprocess
import sys

EXTENSIONS = {".ts", ".tsx", ".js", ".jsx", ".mts", ".cts", ".mjs", ".cjs"}


def collisions(paths):
    modules = {}
    for name in paths:
        path = PurePosixPath(name)
        if path.suffix not in EXTENSIONS:
            continue
        key = str(path.with_suffix("")).casefold()
        modules.setdefault(key, []).append(name)
    return [sorted(names) for _, names in sorted(modules.items()) if len(names) > 1]


def main():
    root = Path(__file__).resolve().parents[2]
    paths = subprocess.check_output(
        ["git", "ls-files"], cwd=root, text=True
    ).splitlines()
    problems = collisions(paths)
    for names in problems:
        print("ambiguous source modules: " + ", ".join(names))
    print(f"{len(problems)} source module path collision(s)")
    return bool(problems)


if __name__ == "__main__":
    sys.exit(main())
