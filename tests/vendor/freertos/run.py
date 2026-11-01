#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""FreeRTOS task awareness (issue #96) on EmbSIM's NUCLEO-F446RE: the
unmodified FreeRTOS kernel, built with firmware/ (tasks in each state: a
delay, a notification wait for ever, a queue wait, a suspended task and a
notification left pending), runs 1.5 s under scenario.json, whose rtos
assertions sample the tasks each millisecond; `embsim run --rtos` then
reports them, and each task's state, wait and notifications must be what
the firmware put them in.

The kernel is not vendored: --kernel names a checkout of
https://github.com/FreeRTOS/FreeRTOS-Kernel (CI's at V11.1.0).

    run.py --kernel DIR [--embsim PATH] [--keep DIR]

Without arm-none-eabi-gcc, or without --kernel (or FREERTOS_KERNEL), it
skips."""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from vendor_run import sh  # noqa: E402

CC = "arm-none-eabi-gcc"
MCU = ["-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"]
KERNEL = ["tasks.c", "queue.c", "list.c", "portable/GCC/ARM_CM4F/port.c", "portable/MemMang/heap_4.c"]

# what the firmware leaves each task in (main.c)
EXPECT = {
    "main": {"state": "blocked", "kind": "delay"},
    "sleeper": {"state": "blocked", "kind": "delay", "pending": 0},
    "waiter": {"state": "blocked", "kind": "notification", "index": 1, "forever": True},
    "rx": {"state": "blocked", "kind": "queue_receive", "queue_name": "rxq", "forever": True},
    "parked": {"state": "suspended"},
    "IDLE": {"state": "running"},
}


def check_tasks(report: dict) -> list:
    """What differs from EXPECT, in words."""
    tasks = {t["name"]: t for t in report.get("tasks", [])}
    bad = []
    for name, want in EXPECT.items():
        t = tasks.get(name)
        if not t:
            bad.append(f"{name}: not found")
            continue
        w = t.get("waits") or {}
        if t.get("state") != want["state"]:
            bad.append(f"{name}: state {t.get('state')}, not {want['state']}")
        for key in ("kind", "index", "forever", "queue_name"):
            if key in want and w.get(key) != want[key]:
                bad.append(f"{name}: waits.{key} {w.get(key)!r}, not {want[key]!r}")
        if "pending" in want:
            notes = {n["index"]: n for n in t.get("notifications", [])}
            n = notes.get(want["pending"], {})
            if n.get("state") != "pending" or n.get("value") != 0x2A:
                bad.append(f"{name}: notification {want['pending']} {n}, not pending with 0x2a")
        if "stack_free_min" not in t:
            bad.append(f"{name}: no stack high-water mark")
    return bad


MEASURES = ["ctxsw=pend:PendSV..ret:PendSV", "pend2exc=pend:PendSV..exc:PendSV",
            "handler=exc:PendSV..ret:PendSV", "tick=exc:SysTick..exc:SysTick"]


def measure(embsim: str, elf: Path, out: Path) -> dict:
    """The context switch and the tick, measured over 200 ms (timing-fidelity.md, step 3)."""
    cmd = [embsim, "run", "--target", "emblink/nucleo_f446re", "--image", str(elf), "--until", "200ms", "--quiet",
           "--measure-out", str(out)]
    for m in MEASURES:
        cmd += ["--measure", m]
    subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    try:
        doc = json.loads(out.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return doc


def check_timing(embsim: str, elf: Path, work: Path) -> bool:
    """The measurements' own consistency and their determinism. The firmware
    leaves the core on HSI (16 MHz) and FreeRTOS ticks at 1 kHz, so the tick
    is 16000 cycles; a context switch is its request's latency plus PendSV's
    entry-to-return, sample by sample; two runs measure the same."""
    a = measure(embsim, elf, work / "timing-1.json")
    b = measure(embsim, elf, work / "timing-2.json")
    sim = (a.get("simulated") or {}).get("B1") or {}
    by = {m["name"]: m for m in sim.get("measurements", [])}
    bad = []
    if not by:
        bad.append("no measurements reported")
    else:
        if sim.get("core_hz_min") != 16000000 or sim.get("core_hz_max") != 16000000:
            bad.append(f"core clock {sim.get('core_hz_min')}..{sim.get('core_hz_max')} Hz, not 16 MHz")
        tick = by.get("tick", {})
        if not tick.get("count") or abs(tick.get("cycles_mean", 0) - 16000) > 1:
            bad.append(f"tick: {tick.get('count')} samples, mean {tick.get('cycles_mean')} cycles, not 16000")
        if tick.get("cycles_min", 0) < 16000 - 8 or tick.get("cycles_max", 0) > 16000 + 8:
            bad.append(f"tick: {tick.get('cycles_min')}..{tick.get('cycles_max')} cycles, more jitter than an instruction")
        cs, pe, ha = by.get("ctxsw", {}), by.get("pend2exc", {}), by.get("handler", {})
        if not cs.get("count") or cs.get("count") != pe.get("count") or cs.get("count") != ha.get("count"):
            bad.append(f"ctxsw {cs.get('count')}, pend2exc {pe.get('count')}, handler {ha.get('count')} samples")
        else:
            for x, y, z in zip(cs["samples"], pe["samples"], ha["samples"]):
                if x["cycles"] != y["cycles"] + z["cycles"]:
                    bad.append(f"a switch at {x['start_cycle']}: {x['cycles']} cycles, not {y['cycles']} + {z['cycles']}")
                    break
            # an ARMv7-M entry is 12 cycles and its return 12 (DDI 0439B 3.9.1): a
            # switch includes both, around the handler's own instructions
            if cs.get("cycles_min", 0) < 24:
                bad.append(f"ctxsw min {cs.get('cycles_min')} cycles, under the 24 of an entry and a return")
    if a.get("simulated") != b.get("simulated"):
        bad.append("two runs measured differently")
    host = a.get("host") or {}
    for m in bad:
        print(f"  FAIL freertos/timing: {m}")
    if by:
        cs = by.get("ctxsw", {})
        print(f"  {'ok  ' if not bad else 'FAIL'} freertos/timing: context switch {cs.get('cycles_min')}..{cs.get('cycles_max')} "
              f"cycles (mean {cs.get('cycles_mean', 0):.1f}) over {cs.get('count')} switches at 16 MHz; "
              f"host {host.get('wall_s', 0):.2f} s for {host.get('simulated_s', 0):.3f} s simulated")
    return not bad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kernel", default=os.environ.get("FREERTOS_KERNEL"))
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--keep")
    args = ap.parse_args()
    if not shutil.which(CC) or not args.kernel or not (Path(args.kernel) / "tasks.c").exists():
        print("skip: needs arm-none-eabi-gcc and --kernel (a FreeRTOS-Kernel checkout)")
        return 0
    k = Path(args.kernel)
    work = Path(args.keep) if args.keep else Path(tempfile.mkdtemp(prefix="embsim-freertos-"))
    work.mkdir(parents=True, exist_ok=True)
    fw = HERE / "firmware"
    elf = work / "freertos.elf"
    sh([CC, *MCU, "-O1", "-g", "-Wall", "-I", fw, "-I", k / "include", "-I", k / "portable/GCC/ARM_CM4F",
        fw / "main.c", fw / "startup.c", *[k / s for s in KERNEL], "-T", fw / "stm32f446re.ld", "-nostartfiles",
        "--specs=nano.specs", "--specs=nosys.specs", "-Wl,--gc-sections", "-o", elf])
    tasks = work / "tasks.json"
    p = subprocess.run([args.embsim, "run", "--target", "emblink/nucleo_f446re", "--image", str(elf), "--scenario",
                        str(HERE / "scenario.json"), "--json", "--rtos", str(tasks)],
                       capture_output=True, text=True, errors="replace")
    ok = True
    try:
        report = json.loads(p.stdout)
    except json.JSONDecodeError:
        print(f"FAIL  embsim did not report\n{p.stdout[-2000:]}{p.stderr[-2000:]}")
        return 1
    for a in report.get("assertions") or []:
        print(f"  {'ok  ' if a.get('ok') else 'FAIL'} freertos/{a.get('id')}" +
              ("" if a.get("ok") else f": {a.get('reason')}"))
        ok = ok and bool(a.get("ok"))
    try:
        board = json.loads(tasks.read_text(encoding="utf-8")).get("B1", {})
    except (OSError, json.JSONDecodeError):
        board = {}
        print(f"FAIL  no task report\n{p.stderr[-2000:]}")
        ok = False
    for b in check_tasks(board) if board else []:
        print(f"  FAIL freertos/tasks: {b}")
        ok = False
    if board and not board.get("layout_from_dwarf"):
        print("  FAIL freertos/tasks: the layout did not come from the image's DWARF")
        ok = False
    print(f"{'ok  ' if ok else 'FAIL'}  freertos: {len(board.get('tasks', []))} task(s) at tick {board.get('tick')}")
    ok = check_timing(args.embsim, elf, work) and ok
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
