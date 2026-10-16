#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Applications built with the unmodified Raspberry Pi Pico SDK on EmbSIM's
Pico (01 §8.2: firmware written for the vendor's own framework runs as it
does on the part).

The SDK is not vendored: --sdk names a checkout of
https://github.com/raspberrypi/pico-sdk (its submodules are not needed:
the applications use no USB), CI's at a pinned release. Each application
here (apps/<name>/: its CMakeLists.txt and sources, as an SDK project has
them) is configured with the SDK's own CMake (PICO_BOARD=pico, Release)
and built with Ninja and arm-none-eabi-gcc; its ELF then runs this
directory's project.json under the application's scenario.json, whose
assertions must all pass.

    run.py --sdk DIR [--embsim PATH] [--keep DIR] [APP...]

Without arm-none-eabi-gcc, cmake and ninja, or without --sdk (or
PICO_SDK_PATH), it skips."""
import argparse
import os
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from vendor_run import run_scenario, sh  # noqa: E402


def build_app(sdk: Path, app: Path, out: Path) -> Path:
    d = out / "build" / app.name
    env = dict(os.environ, PICO_SDK_PATH=str(sdk))
    sh(["cmake", "-S", app, "-B", d, "-G", "Ninja", "-DPICO_BOARD=pico", "-DCMAKE_BUILD_TYPE=Release"], env=env)
    sh(["ninja", "-C", d], env=env)
    return d / (app.name + ".elf")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", default=os.environ.get("PICO_SDK_PATH"), help="a pico-sdk checkout")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep", help="build here, and keep it, instead of a temporary directory")
    ap.add_argument("apps", nargs="*")
    args = ap.parse_args()
    missing = [t for t in ("arm-none-eabi-gcc", "cmake", "ninja") if not shutil.which(t)]
    if missing or not args.sdk:
        print(f"SKIP: {'needs ' + ', '.join(missing) if missing else 'no --sdk (a pico-sdk checkout)'}")
        return 0
    sdk = Path(args.sdk).resolve()
    if not (sdk / "external" / "pico_sdk_import.cmake").is_file():
        print(f"error: {sdk} is not a pico-sdk checkout")
        return 2
    names = args.apps or sorted(d.name for d in (HERE / "apps").iterdir() if d.is_dir())
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(args.keep).resolve() if args.keep else Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        bad = 0
        for name in names:
            elf = build_app(sdk, HERE / "apps" / name, work)
            bad += not run_scenario(Path(args.embsim), HERE / "project.json", name, elf,
                                    HERE / "apps" / name / "scenario.json", work)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
