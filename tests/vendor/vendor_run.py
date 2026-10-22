# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""What the vendor runners share: running an image in a vendor directory's
project.json under a scenario and reporting its assertions."""
import json
import shutil
import subprocess
from pathlib import Path


def sh(cmd, **kw):
    """Run a build command; its output is shown only when it fails."""
    p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    if p.returncode:
        print(" ".join(str(c) for c in cmd))
        print(p.stdout[-4000:] + p.stderr[-4000:])
        raise SystemExit(f"build failed: {cmd[0]}")


def run_scenario(embsim: Path, project: Path, name: str, elf: Path, scenario: Path, work: Path,
                 extra: dict = None, image_name: str = "sketch.elf") -> bool:
    """`elf` as the project's sketch.elf, or `image_name` (and `extra`'s
    images under their names, for a project's other boards), under
    `scenario`: True when every assertion holds."""
    d = work / name
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    shutil.copy(project, d / "project.json")
    shutil.copy(elf, d / image_name)
    for file, path in (extra or {}).items():
        shutil.copy(path, d / file)
    p = subprocess.run([str(embsim), "run", str(d / "project.json"), "--scenario", str(scenario), "--json"],
                       capture_output=True, text=True, errors="replace")
    try:
        report = json.loads(p.stdout)
    except json.JSONDecodeError:
        print(f"FAIL  {name}: embsim did not report\n{p.stdout[-2000:]}{p.stderr[-2000:]}")
        return False
    results = report.get("assertions") or []
    failed = [a for a in results if not a.get("ok")]
    for a in results:
        print(f"  {'ok  ' if a.get('ok') else 'FAIL'} {name}/{a.get('id')}"
              + ("" if a.get("ok") else f": expected {a.get('expected')}, observed {a.get('observed')!r}"))
    ok = p.returncode == 0 and report.get("ok") and results and not failed
    print(f"{'ok  ' if ok else 'FAIL'}  {name}: {len(results) - len(failed)}/{len(results)} assertions")
    return bool(ok)
