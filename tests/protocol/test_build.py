#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""`embsim build` with each toolchain profile found on this host (clang with
ld.lld, embcc): the Blue Pill, Uno, HiFive1, Pico and nRF52840 DK examples copied to a
temporary directory, their images removed, built from their sources, then
their scenarios run. Profiles not installed are skipped, and an example for
a clang built without its backend; with none, the test passes with SKIP."""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = [  # directory, image, scenario, the clang target it needs
    ("bluepill-button-led", "button-led.elf", "press.json", "thumb"),
    ("uno-blink-serial", "blink-serial.elf", "blink.json", "avr"),
    ("hifive1-blink", "blink.elf", "blink.json", "riscv32"),
    ("pico-blink-serial", "blink-serial.elf", "blink.json", "thumb"),
    ("nrf52840dk-blink", "blink.elf", "blink.json", "thumb"),
]


def clang_targets() -> str:
    try:
        return subprocess.run(["clang", "--print-targets"], capture_output=True, text=True, timeout=60).stdout
    except OSError:
        return ""


def available(embsim: str) -> list[str]:
    out = subprocess.run([embsim, "toolchains"], capture_output=True, text=True, timeout=120).stdout
    found = {line.split()[0] for line in out.splitlines() if len(line.split()) > 1 and line.split()[1] == "found"}
    profiles = []
    if "clang" in found and shutil.which("ld.lld"):
        profiles.append("clang")
    if "embcc" in found:
        profiles.append("embcc")
    return profiles


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    args = ap.parse_args()
    profiles = available(args.embsim)
    if not profiles:
        print("SKIP: no firmware toolchain (clang with ld.lld, or embcc) on this host")
        return 0
    failures = 0
    targets = clang_targets()
    for prof in profiles:
        for name, image, scenario, need in EXAMPLES:
            if prof == "clang" and need not in targets:
                print(f"skip  {prof}: {name} (this clang has no {need} backend)")
                continue
            with tempfile.TemporaryDirectory() as tmp:
                ex = Path(tmp) / "ex"
                shutil.copytree(ROOT / "examples" / name, ex)
                (ex / "firmware" / image).unlink()
                b = subprocess.run([args.embsim, "build", str(ex / "project.json"), "--toolchain", prof],
                                   capture_output=True, text=True, timeout=300)
                if b.returncode != 0 or not (ex / "firmware" / image).exists():
                    print(f"FAIL  {prof}: {name}: build\n{b.stdout}\n{b.stderr}")
                    failures += 1
                    continue
                r = subprocess.run([args.embsim, "run", str(ex / "project.json"), "--scenario",
                                    str(ex / "scenarios" / scenario)], capture_output=True, text=True, timeout=300)
                ok = r.returncode == 0 and "FAIL" not in r.stdout
                print(("ok    " if ok else "FAIL  ") + f"{prof}: {name} built and its scenario passes")
                if not ok:
                    print(r.stdout[-3000:], r.stderr[-2000:])
                    failures += 1
    print(f"{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
