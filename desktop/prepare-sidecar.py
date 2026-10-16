#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Copy the release `embsim` into desktop/src-tauri/binaries under the name
Tauri's externalBin expects: embsim-<rustc host triple>[.exe]."""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    triple = next(line.split(":", 1)[1].strip()
                  for line in subprocess.check_output(["rustc", "-vV"], text=True).splitlines()
                  if line.startswith("host:"))
    exe = ".exe" if sys.platform == "win32" else ""
    candidates = [ROOT / "build/release/bin" / f"embsim{exe}", ROOT / "build/dev/bin" / f"embsim{exe}"]
    src = next((c for c in candidates if c.exists()), None)
    if src is None:
        print("build the engine first: cmake --preset release && cmake --build --preset release", file=sys.stderr)
        return 1
    dst = ROOT / "desktop/src-tauri/binaries" / f"embsim-{triple}{exe}"
    shutil.copy2(src, dst)
    print(f"{src} -> {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
