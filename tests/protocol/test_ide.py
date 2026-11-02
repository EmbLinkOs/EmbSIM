#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""`embsim ide`: VS Code's launch configurations, run as VS Code runs them.

A copy of examples/nucleo-f446re-blink-serial gets its .vscode/launch.json
and tasks.json from `embsim ide`. Each configuration is then carried out as
VS Code and its extensions carry it out, from what the files say (only the
fixed ports are swapped for free ones, so the test runs beside others):

  - "B1 through its ST-LINK (OpenOCD)": its preLaunchTask runs the session
    with the probe shared until it prints the task's ready pattern; then
    Cortex-Debug's way, OpenOCD with the configuration's files and launch
    commands, and GDB on the executable: a breakpoint at main is hit;
  - "B1 on EmbSIM's GDB server": its task, `embsim gdb`, then GDB to the
    configuration's gdbTarget: the breakpoint at main is hit too.
Each part needs OpenOCD or an Arm GDB and is skipped without them; the
files themselves are always checked. A second `embsim ide` without --force
refuses to replace them.

    test_ide.py --embsim PATH
"""
import argparse
import json
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "examples" / "nucleo-f446re-blink-serial"

failures = 0


def check(ok: bool, what: str) -> None:
    global failures
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures += 1


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def subst(v: str, ws: Path) -> str:
    return v.replace("${workspaceFolder}", ws.as_posix())


def start_task(t: dict, ws: Path, swap: dict) -> subprocess.Popen:
    """the task's process, until its background pattern says it is ready"""
    args = [subst(swap.get(a, a), ws) for a in t["args"]]
    p = subprocess.Popen([t["command"]] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    ready = re.compile(t["problemMatcher"]["background"]["endsPattern"])
    end = time.monotonic() + 30
    while time.monotonic() < end:
        line = p.stdout.readline()
        if not line:
            break
        if ready.search(line):
            return p
    p.kill()
    raise RuntimeError(f"task {t['label']} never got ready")


def gdb_breaks_at_main(gdbx: str, exe: str, target: str, reset: bool) -> str:
    cmds = ["-ex", f"target extended-remote {target}"]
    if reset:
        cmds += ["-ex", "monitor reset halt"]
    cmds += ["-ex", "break main", "-ex", "continue", "-ex", "detach"]
    g = subprocess.run([gdbx, "-q", "-batch", "-nx", exe] + cmds, capture_output=True, text=True, timeout=120)
    return g.stdout + g.stderr


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-ide-") as tmp:
        ws = Path(tmp)
        shutil.copy(EXAMPLE / "project.json", ws / "project.json")
        shutil.copytree(EXAMPLE / "firmware", ws / "firmware")
        r = subprocess.run([args.embsim, "ide", str(ws / "project.json"), "--embsim", args.embsim],
                           capture_output=True, text=True)
        check(r.returncode == 0, f"embsim ide: {r.stdout.strip() or r.stderr.strip()}")
        launch = json.loads((ws / ".vscode" / "launch.json").read_text(encoding="utf-8"))
        tasks = {t["label"]: t for t in json.loads((ws / ".vscode" / "tasks.json").read_text(encoding="utf-8"))["tasks"]}
        confs = {c["name"]: c for c in launch["configurations"]}
        probe = confs.get("B1 through its ST-LINK/V2-1 (OpenOCD)")
        own = confs.get("B1 on EmbSIM's GDB server")
        check(probe is not None and probe["type"] == "cortex-debug" and probe["servertype"] == "openocd" and
              probe["configFiles"] == ["interface/stlink.cfg", "target/stm32f4x.cfg"] and
              probe["preLaunchTask"] in tasks, f"the probe's configuration: {sorted(confs)}")
        check(own is not None and own["servertype"] == "external" and own["preLaunchTask"] in tasks,
              "the GDB server's configuration")
        again = subprocess.run([args.embsim, "ide", str(ws / "project.json")], capture_output=True, text=True)
        check(again.returncode != 0 and "--force" in again.stderr, "a second run refuses to replace them")

        openocd = shutil.which("openocd")
        gdbx = shutil.which("gdb-multiarch") or shutil.which("arm-none-eabi-gdb")
        if not openocd or not gdbx:
            print("  SKIP OpenOCD or an Arm GDB is not installed")
        else:
            sport, gport = free_port(), free_port()
            srv = start_task(tasks[probe["preLaunchTask"]], ws, {"7184": str(sport)})
            cmds = [c.replace("7184", str(sport)) for c in probe["openOCDLaunchCommands"]]
            oc_args = [openocd]
            for f in probe["configFiles"]:
                oc_args += ["-f", f]
            for c in cmds + [f"gdb_port {gport}; telnet_port disabled; tcl_port disabled"]:
                oc_args += ["-c", c]
            oc = subprocess.Popen(oc_args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                time.sleep(2.0)
                out = gdb_breaks_at_main(gdbx, subst(probe["executable"], ws), f":{gport}", True)
                check("Breakpoint 1, main ()" in out, "through the ST-LINK: OpenOCD and GDB stop at main")
            finally:
                oc.terminate()
                oc.wait(timeout=10)
                srv.terminate()
                srv.wait(timeout=10)
        if not gdbx:
            print("  SKIP no Arm GDB")
        else:
            gport = free_port()
            srv = start_task(tasks[own["preLaunchTask"]], ws, {"3333": str(gport)})
            try:
                out = gdb_breaks_at_main(gdbx, subst(own["executable"], ws),
                                         own["gdbTarget"].replace("3333", str(gport)), False)
                check("Breakpoint 1, main ()" in out, "on EmbSIM's GDB server: GDB stops at main")
            finally:
                srv.terminate()
                srv.wait(timeout=10)
    print("ide: %s" % ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
