#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The component SDK from a contributor's side (SPEC-014 §4-5): in an empty
directory, `embsim catalog new` writes a component, a Lua component and a
board; both validators accept them as written; `embsim catalog install`
copies the library and the catalog lists its parts; a part whose fixture
fails is reported broken and refused by install."""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run(cmd: list[str], env: dict | None = None, ok: bool = True) -> subprocess.CompletedProcess:
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=300, env=env)
    if ok and p.returncode != 0:
        print(f"FAIL: {' '.join(cmd)} -> {p.returncode}\n{p.stdout}\n{p.stderr}")
        sys.exit(1)
    return p


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--embsim", required=True)
    args = ap.parse_args()
    e = args.embsim
    with tempfile.TemporaryDirectory() as tmp:
        lib = Path(tmp) / "acme-parts"
        lib.mkdir()
        run([e, "catalog", "new", "component", "my_resistor", "--library", str(lib)])
        run([e, "catalog", "new", "component", "toggle", "--library", str(lib), "--lua"])
        run([e, "catalog", "new", "board", "acme_board", "--library", str(lib)])
        assert json.loads((lib / "library.json").read_text())["name"] == "acme-parts"
        report = json.loads(run([e, "catalog", "validate", str(lib), "--json"]).stdout)
        assert report["summary"] == {"ok": 3, "broken": 0, "invalid": 0, "fixtures": 3}, report["summary"]
        run([sys.executable, str(ROOT / "tools" / "validate.py"), str(lib)])
        # a second `new` with the same name is refused
        assert run([e, "catalog", "new", "component", "toggle", "--library", str(lib)], ok=False).returncode != 0

        installed = Path(tmp) / "installed"
        run([e, "catalog", "install", str(lib), "--to", str(installed)])
        assert (installed / "acme-parts" / "components" / "toggle" / "model.lua").is_file()
        env = dict(os.environ, EMBSIM_LIBRARIES=str(installed))
        listing = run([e, "catalog", "list"], env=env).stdout
        assert "acme-parts/toggle" in listing and "acme-parts/my_resistor" in listing, listing
        boards = run([e, "catalog", "list", "board"], env=env).stdout
        assert "acme-parts/acme_board" in boards, boards

        # break the Lua part's fixture: validate says broken, install refuses
        test = lib / "components" / "toggle" / "tests" / "halves.json"
        doc = json.loads(test.read_text())
        doc["expect"]["output:DUT.count"]["equals"] = 11
        test.write_text(json.dumps(doc, indent=2))
        report = json.loads(run([e, "catalog", "validate", str(lib), "--json"], ok=False).stdout)
        status = {i["id"]: i["status"] for i in report["items"]}
        assert status["acme-parts/toggle"] == "broken", status
        assert run([e, "catalog", "install", str(lib), "--to", str(Path(tmp) / "other")], ok=False).returncode != 0
    print("PASS: new, validate, install and list")
    return 0


if __name__ == "__main__":
    sys.exit(main())
