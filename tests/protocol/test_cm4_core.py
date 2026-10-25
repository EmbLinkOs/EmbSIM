#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""What an RTOS port needs from a Cortex-M4F core, against QEMU (01 §8.5's
differential suites).

tests/firmware/cm4-core/cm4-core.elf drives the MPU (regions, permissions,
subregions, overlap, the background region, HFNMIENA, XN), the default
memory map's execute-never regions, unprivileged execution and LDRT,
unprivileged access to the system control space, faults on stacking and
unstacking an exception frame, and lazy and eager floating-point context
stacking, and prints what each case observes through semihosting.
expected.txt is what QEMU's mps2-an386 (a Cortex-M4 with FPU and MPU)
prints for the image; EmbSIM's nRF52840 (a Cortex-M4F) must print the same.

    test_cm4_core.py --embsim PATH    the image on EmbSIM, against expected.txt
    test_cm4_core.py --qemu           expected.txt against QEMU (qemu-system-arm)
"""
import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "tests" / "firmware" / "cm4-core"


def on_embsim(embsim: str) -> str:
    with tempfile.TemporaryDirectory() as tmp:
        rec = Path(tmp) / "cm4-core.rec.json"
        p = subprocess.run([embsim, "run", "--target", "emblink/nrf52840dk", "--image", str(CORE / "cm4-core.elf"),
                            "--until", "1s", "--probe", "serial:B1.semihosting", "--record", str(rec)],
                           capture_output=True, text=True, timeout=300, cwd=str(ROOT))
        if p.returncode or not rec.exists():
            raise SystemExit(f"embsim failed ({p.returncode}): {p.stdout[-800:]}{p.stderr[-800:]}")
        obs = json.loads(rec.read_text())["observations"]
        return "".join(o["bytes"] for o in obs if o["kind"] == "serial" and o["probe"] != "console")


def on_qemu() -> str:
    p = subprocess.run(["qemu-system-arm", "-M", "mps2-an386", "-cpu", "cortex-m4", "-nographic",
                        "-semihosting-config", "enable=on,target=native", "-kernel", str(CORE / "cm4-core.elf")],
                       capture_output=True, text=True, timeout=300)
    return p.stdout + p.stderr  # semihosting's console is QEMU's stderr


def compare(name: str, got: str, want: str) -> int:
    g, w = got.splitlines(), want.splitlines()
    bad = [f"{a!r} != {b!r}" for a, b in zip(g, w) if a != b]
    if len(g) != len(w):
        bad.append(f"{len(g)} lines, {len(w)} expected")
    for b in bad[:20]:
        print(f"FAIL {name}: {b}")
    if not bad:
        print(f"ok   {name}: {len(w) - 1} cases agree")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--embsim")
    ap.add_argument("--qemu", action="store_true")
    args = ap.parse_args()
    want = (CORE / "expected.txt").read_text()
    if args.qemu:
        if not shutil.which("qemu-system-arm"):
            print("SKIP: qemu-system-arm is not installed")
            return 0
        return compare("QEMU", on_qemu(), want)
    if not args.embsim:
        ap.error("--embsim or --qemu")
    return compare("EmbSIM", on_embsim(args.embsim), want)


if __name__ == "__main__":
    sys.exit(main())
