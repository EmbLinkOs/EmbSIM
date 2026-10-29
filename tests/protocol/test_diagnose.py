#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The diagnosis and documentation commands (issues #91, #94, #95, #86,
#87, #101), through the CLI:

  - a firmware image older than its sources warns mcu.stale_image;
  - run --dump-regs decodes a peripheral's registers by field;
  - run --circuit-profile ranks the islands, nets and parts, and a
    pull-up resistor to the Uno's 5 V rail on a busy line warns
    perf.island_size;
  - export svg writes the sheet (valid SVG, the board's block, each part)
    and a display's content;
  - plan assigns a peripheral's signals on one instance, each on a pin
    whose alternate function carries it, no pin twice, clear of LD2.

    test_diagnose.py --embsim PATH
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import xml.dom.minidom
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UNO = ROOT / "examples" / "uno-blink-serial"
NUCLEO = ROOT / "examples" / "nucleo-f446re-blink-serial" / "project.json"
SHOWCASE = ROOT / "examples" / "display-showcase" / "project.json"


def cli(embsim, *args, timeout=300):
    return subprocess.run([embsim, *map(str, args)], capture_output=True, text=True, timeout=timeout, cwd=str(ROOT),
                          errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    e = ap.parse_args().embsim
    problems = []
    work = Path(tempfile.mkdtemp(prefix="embsim-diagnose-"))
    try:
        # a stale image: the sources newer than the ELF by more than a minute
        uno = work / "uno"
        shutil.copytree(UNO, uno, ignore=shutil.ignore_patterns("build"))
        elf = uno / "firmware" / "blink-serial.elf"
        old = elf.stat().st_mtime - 3600
        os.utime(elf, (old, old))
        for src in (uno / "firmware").glob("*.[cS]"):
            os.utime(src, None)
        r = cli(e, "validate", uno / "project.json")
        if "mcu.stale_image" not in r.stderr:
            problems.append(f"no mcu.stale_image for an image older than its sources: {r.stderr[-400:]}")
        os.utime(elf, None)
        r = cli(e, "validate", uno / "project.json")
        if "mcu.stale_image" in r.stderr:
            problems.append("mcu.stale_image for a fresh image")

        # registers at the end, decoded
        r = cli(e, "run", NUCLEO, "--until", "20ms", "--quiet", "--dump-regs", "USART2")
        if "USART2" not in r.stdout or "CR1 = 0x" not in r.stdout or " UE" not in r.stdout:
            problems.append(f"--dump-regs: {r.stdout[-600:]}{r.stderr[-300:]}")
        r = cli(e, "run", NUCLEO, "--until", "1ms", "--quiet", "--dump-regs", "NOSUCH")
        if r.returncode != 2:
            problems.append(f"--dump-regs of an unknown peripheral exits {r.returncode}, not 2")

        # the circuit profile, and a pull-up that joins a busy line to the rail
        prof = work / "profile.json"
        r = cli(e, "run", NUCLEO, "--until", "20ms", "--quiet", "--circuit-profile", prof)
        try:
            p = json.loads(prof.read_text(encoding="utf-8"))
            if not p["islands"] or p["islands"][0]["steps"] <= 0 or not p["nets"] or not p["components"]:
                problems.append(f"circuit profile: {p}")
        except (OSError, ValueError, KeyError) as x:
            problems.append(f"circuit profile not written: {x} {r.stderr[-300:]}")
        proj = json.loads((uno / "project.json").read_text(encoding="utf-8"))
        proj["components"].append({"id": "RPU", "component": "emblink/resistor", "params": {"resistance": "4.7k"}})
        proj["nets"]["UART_TX"].append("RPU.1")
        proj["nets"]["RAILPU"] = ["RPU.2", "B1.5V"]
        (uno / "pullup.json").write_text(json.dumps(proj, indent=1), encoding="utf-8")
        r = cli(e, "run", uno / "pullup.json", "--until", "6s", "--quiet")
        if "perf.island_size (UART_TX)" not in r.stderr:
            problems.append(f"no perf.island_size for a 4.7k pull-up to 5V on UART_TX: {r.stderr[-400:]}")
        if "perf.island_size (PWM)" in r.stderr:
            problems.append("perf.island_size for PWM, which drives an LED: real analog work")

        # export svg: the sheet and a display
        sheet = work / "sheet.svg"
        r = cli(e, "export", "svg", UNO / "project.json", "-o", sheet)
        try:
            doc = xml.dom.minidom.parse(str(sheet))
            text = sheet.read_text(encoding="utf-8")
            for want in ("Arduino Uno", ">R1<", ">D1<", ">VT1<", "polyline"):
                if want not in text:
                    problems.append(f"export svg: no {want!r} on the sheet")
            if doc.documentElement.tagName != "svg":
                problems.append("export svg: the root is not <svg>")
        except Exception as x:  # noqa: BLE001 -- any parse failure is the finding
            problems.append(f"export svg: not valid SVG: {x} {r.stderr[-300:]}")
        oled = work / "oled.svg"
        r = cli(e, "export", "svg", SHOWCASE, "--component", "OLED", "--at", "300ms", "-o", oled)
        if r.returncode or not oled.exists() or "<path d=\"M" not in oled.read_text(encoding="utf-8"):
            problems.append(f"export svg --component OLED: {r.returncode} {r.stderr[-300:]}")
        r = cli(e, "export", "svg", SHOWCASE, "--component", "PU1", "-o", work / "x.svg")
        if r.returncode != 1:
            problems.append(f"export of a part that shows nothing exits {r.returncode}, not 1")

        # plan: conflict-free, on carrying pins, one instance per peripheral
        r = cli(e, "plan", "nucleo_f446re", "--needs", "spi:2,i2c:2,uart,pwm:4,adc:3,gpio:6", "--prefer", "arduino", "--json")
        try:
            plan = json.loads(r.stdout)
        except ValueError:
            plan = {}
            problems.append(f"plan --json: {r.stdout[-300:]}{r.stderr[-300:]}")
        if plan:
            mcu = json.loads((ROOT / "catalog" / "mcus" / "st" / "stm32f446re" / "mcu.json").read_text(encoding="utf-8"))
            alt = mcu["io"]["alternate"]
            pins = [x["pin"] for x in plan["pins"] if "pin" in x]
            if not plan.get("ok"):
                problems.append(f"plan not ok: {plan}")
            if len(pins) != len(set(pins)):
                problems.append(f"plan uses a pin twice: {pins}")
            if "PA5" in pins or "PA13" in pins or "PA14" in pins:
                problems.append(f"plan uses LD2's or the debugger's pin: {pins}")
            for x in plan["pins"]:
                if "af" in x and x["signal"] not in alt.get(x["pin"], {}).get(str(x["af"]), []):
                    problems.append(f"plan: {x['signal']} is not AF{x['af']} of {x['pin']}")
            for kind in ("spi", "i2c"):
                insts = {}
                for x in plan["pins"]:
                    if x["need"] == kind:
                        insts.setdefault(x["signal"].split("_")[0], []).append(x["signal"])
                if len(insts) != 2 or any(len(v) != (3 if kind == "spi" else 2) for v in insts.values()):
                    problems.append(f"plan: {kind} instances {insts}")
        r = cli(e, "plan", "arduino_uno", "--needs", "spi")
        if r.returncode != 1 or "io.alternate" not in r.stderr:
            problems.append(f"plan on an MCU without an alternate-function table: {r.returncode} {r.stderr[-300:]}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for p in problems:
        print("FAIL", p)
    print("ok" if not problems else f"{len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
