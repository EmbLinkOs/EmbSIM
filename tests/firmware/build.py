#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Build the committed test firmware (tests/firmware/*/) with Clang and LLD.

The ELF images are committed so every platform's tests run them without a
cross toolchain. This script rebuilds them byte for byte: absolute paths are
mapped away and no build id is written. `--check` rebuilds into a temporary
directory and fails if any committed image differs (CI runs it where Clang
and LLD exist)."""
import argparse
import filecmp
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FLAGS = ["--target=thumbv7m-none-eabi", "-mcpu=cortex-m3", "-mthumb", "-O2", "-g",
         "-ffreestanding", "-fno-builtin", "-nostdlib", "-fuse-ld=lld", "-Wl,--build-id=none",
         f"-ffile-prefix-map={ROOT}=.", "-I", str(HERE / "common")]
# image: (directory, sources relative to tests/firmware, linker script)
IMAGES = {
    "hello/hello.elf": (["common/startup.c", "hello/main.c"], "common/stm32f103c8.ld"),
    "bluepill-golden/bluepill.elf": (["common/startup.c", "bluepill-golden/bluepill.c"],
                                     "common/stm32f103c8.ld"),
    "stack-overflow/stack-overflow.elf": (["common/startup.c", "stack-overflow/main.c"],
                                          "stack-overflow/stack.ld"),
}


def build(out_dir: Path) -> None:
    for image, (sources, script) in IMAGES.items():
        out = out_dir / image
        out.parent.mkdir(parents=True, exist_ok=True)
        cmd = ["clang", *FLAGS, "-T", str(HERE / script), *[str(HERE / s) for s in sources],
               "-o", str(out)]
        subprocess.run(cmd, check=True, cwd=ROOT)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="fail if a committed image is stale")
    args = ap.parse_args()
    if not shutil.which("clang") or not shutil.which("ld.lld"):
        print("SKIP: clang and ld.lld are needed to build the test firmware")
        return 0
    if not args.check:
        build(HERE)
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        build(Path(tmp))
        stale = [i for i in IMAGES if not filecmp.cmp(HERE / i, Path(tmp) / i, shallow=False)]
    for i in stale:
        print(f"stale: tests/firmware/{i} (run tests/firmware/build.py)")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
