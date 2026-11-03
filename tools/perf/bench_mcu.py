#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The simulator's speed on fixed workloads (docs/engineering/timing-fidelity.md,
step 7): each example project runs headless for a fixed simulated time, and
the report gives, side by side and never mixed:

  simulated   the time the run covered (the project's clock, exact);
  host        the wall time it took on this machine;
  ratio       simulated / host (1.0 is real time).

The simulated figures are the same on every host; the host figures are this
machine's. Each workload runs `--repeat` times and the fastest host time is
kept (the least disturbed by the rest of the machine).

    bench_mcu.py [--embsim PATH] [--repeat N] [--json FILE] [--only NAME...]
                 [--freertos ELF]   also FreeRTOS on the NUCLEO-F446RE, with and
                                    without the context-switch measurement
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# name, what runs, simulated time
WORKLOADS = [
    ("uno-blink-serial", "ATmega328P at 16 MHz, a blink and serial output", "examples/uno-blink-serial/project.json", "2s"),
    ("bluepill-button-led", "STM32F103 (Cortex-M3), a button and an LED", "examples/bluepill-button-led/project.json", "2s"),
    ("nucleo-f446re-blink-serial", "STM32F446 (Cortex-M4F) at 180 MHz, blink and serial", "examples/nucleo-f446re-blink-serial/project.json", "1s"),
    ("nrf52840dk-blink", "nRF52840 (Cortex-M4F) at 64 MHz, blink", "examples/nrf52840dk-blink/project.json", "2s"),
    ("pico-dual-core", "RP2040, both Cortex-M0+ cores", "examples/pico-dual-core/project.json", "1s"),
    ("hifive1-blink", "FE310 (RISC-V E31), blink", "examples/hifive1-blink/project.json", "2s"),
    ("two-boards-uart", "two boards over UART", "examples/two-boards-uart/project.json", "1s"),
    ("rc-filter", "an analog RC filter, no MCU", "examples/rc-filter/project.json", "1s"),
]


def run_once(cmd: list) -> tuple:
    t0 = time.monotonic()
    p = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    wall = time.monotonic() - t0
    try:
        rep = json.loads(p.stdout)
    except json.JSONDecodeError:
        return None, wall, (p.stderr or p.stdout)[-400:]
    return rep, wall, ""


def bench(cmd: list, repeat: int) -> dict:
    best = None
    for _ in range(repeat):
        rep, wall, err = run_once(cmd)
        if rep is None:
            return {"error": err}
        st = rep.get("statistics") or {}
        sim = int((rep.get("ended") or {}).get("now_ps") or 0) / 1e12 or None
        host = float(st.get("wall_time_s") or wall)
        if best is None or host < best["host_s"]:
            best = {"simulated_s": sim, "host_s": host, "process_s": wall}
    if best and best["simulated_s"]:
        best["ratio"] = best["simulated_s"] / best["host_s"] if best["host_s"] > 0 else None
    return best


def parse_time(t: str) -> float:
    for suf, k in (("ms", 1e-3), ("us", 1e-6), ("s", 1.0)):
        if t.endswith(suf):
            return float(t[: -len(suf)]) * k
    return float(t)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--json")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--freertos")
    args = ap.parse_args()
    rows = []
    for name, what, project, until in WORKLOADS:
        if args.only and name not in args.only:
            continue
        r = bench([args.embsim, "run", str(ROOT / project), "--until", until, "--json", "--quiet"], args.repeat)
        if r and not r.get("simulated_s") and "error" not in r:
            r["simulated_s"] = parse_time(until)
            r["ratio"] = r["simulated_s"] / r["host_s"] if r["host_s"] > 0 else None
        rows.append({"name": name, "what": what, "until": until, **(r or {})})
    if args.freertos:
        base = [args.embsim, "run", "--target", "emblink/nucleo_f446re", "--image", args.freertos, "--until", "200ms",
                "--json", "--quiet"]
        for label, extra in (("freertos", []),
                             ("freertos+measure", ["--measure", "ctxsw=pend:PendSV..ret:PendSV", "--measure-out",
                                                   str(Path("/tmp") / "bench-measure.json")])):
            r = bench(base + extra, args.repeat)
            if r and not r.get("simulated_s") and "error" not in r:
                r["simulated_s"] = 0.2
                r["ratio"] = r["simulated_s"] / r["host_s"] if r["host_s"] > 0 else None
            rows.append({"name": label, "what": "FreeRTOS V11.1.0 on the NUCLEO-F446RE at 16 MHz" +
                         (", context switches measured" if extra else ""), "until": "200ms", **(r or {})})
    print(f"{'workload':30} {'simulated':>10} {'host':>9} {'ratio':>8}")
    for r in rows:
        if "error" in r:
            print(f"{r['name']:30} error: {r['error'].strip()[:80]}")
            continue
        print(f"{r['name']:30} {r['simulated_s']:9.3f}s {r['host_s']:8.3f}s {r.get('ratio') or 0:7.2f}x")
    if args.json:
        Path(args.json).write_text(json.dumps({"workloads": rows}, indent=1) + "\n", encoding="utf-8")
    return 0 if all("error" not in r for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
