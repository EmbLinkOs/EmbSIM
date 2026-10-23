#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""A board of the catalog running an image with no project (`embsim run
--target BOARD --image FILE`), as an RTOS's test runner uses it (docs/user/ci.md):

  - --console streams the board's console as the run goes;
  - --until-text ends the run at the millisecond the text appears (reason
    `until_text`), at the same time on every run; exit 1 when it never does;
  - --image ID=FILE replaces a project's board image (an image for another
    core is refused, exit 2); --target without --image is a usage error;
  - a board with a USB port runs plugged in under the electrical policy and
    unplugged, its supplies ideal, under the functional one (the RTOS
    runner's).

    test_target.py --embsim PATH
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UNO = ROOT / "examples" / "uno-blink-serial"
ELF = UNO / "firmware" / "blink-serial.elf"
NUCLEO_ELF = ROOT / "examples" / "nucleo-f446re-blink-serial" / "firmware" / "blink-serial.elf"


def run(embsim, *args):
    return subprocess.run([embsim, "run", *map(str, args)], capture_output=True, text=True, timeout=300, cwd=str(ROOT))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    e = ap.parse_args().embsim
    problems = []
    target = ["--target", "emblink/arduino_uno", "--image", ELF]
    r = run(e, *target, "--until", "600ms", "--console")
    if r.returncode or not r.stdout.startswith("EmbSIM Uno") or "A0=" not in r.stdout:
        problems.append(f"--console: exit {r.returncode}: {r.stdout[:120]!r} {r.stderr[:200]}")
    ends = []
    for _ in range(2):
        r = run(e, *target, "--until", "5s", "--until-text", "A0=", "--json")
        rep = json.loads(r.stdout) if r.stdout.strip() else {}
        ends.append((r.returncode, rep.get("ended")))
    rc, ended = ends[0]
    if rc or not ended or ended["reason"] != "until_text" or int(ended["now_ps"]) >= 5 * 10**12 or ends[0] != ends[1]:
        problems.append(f"--until-text: {ends}")
    r = run(e, *target, "--until", "300ms", "--until-text", "never printed", "--json")
    if r.returncode != 1 or json.loads(r.stdout)["ended"]["reason"] != "until":
        problems.append(f"--until-text that never comes: exit {r.returncode}")
    r = run(e, UNO / "project.json", "--image", f"B1={ELF}", "--until", "300ms", "--json")
    if r.returncode:
        problems.append(f"--image B1=FILE: exit {r.returncode}: {r.stderr.strip()}")
    r = run(e, ROOT / "examples" / "bluepill-button-led" / "project.json", "--image", f"B1={ELF}", "--until", "1ms")
    if r.returncode != 2 or "AVR" not in r.stderr and "image" not in r.stderr:
        problems.append(f"an AVR image on a Blue Pill: exit {r.returncode}: {r.stderr.strip()}")
    r = run(e, "--target", "emblink/nucleo_f446re", "--image", NUCLEO_ELF, "--policy", "functional", "--until", "20ms",
            "--json")
    if r.returncode or json.loads(r.stdout or "{}").get("policy") != "functional":
        problems.append(f"a USB-powered board under the functional policy: exit {r.returncode}: {r.stderr.strip()[:200]}")
    r = run(e, "--target", "emblink/arduino_uno")
    if r.returncode != 2:
        problems.append(f"--target without --image: exit {r.returncode}")
    for p in problems:
        print(f"FAIL  {p}")
    if not problems:
        print(f"ok    --target, --console, --until-text (ends at {ended['now_ps']} ps, twice), --image")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
