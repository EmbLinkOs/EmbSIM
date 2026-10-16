#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The component SDK from a contributor's side (SPEC-014 §4-5): in an empty
directory, `embsim catalog new` writes a component, a Lua component and a
board; both validators accept them as written; `embsim catalog install`
copies the library and the catalog lists its parts; the same library as a
zip archive (deflated with a top directory, stored at the top, fast, and
over HTTP) installs the same files; an archive with an entry outside the
library, a corrupt one and a file that is not one are refused, and no
staging directory is left behind; a part whose fixture fails is reported
broken and refused by install."""
import argparse
import functools
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run(cmd: list[str], env: dict | None = None, ok: bool = True) -> subprocess.CompletedProcess:
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=300, env=env)
    if ok and p.returncode != 0:
        print(f"FAIL: {' '.join(cmd)} -> {p.returncode}\n{p.stdout}\n{p.stderr}")
        sys.exit(1)
    return p


def tree(root: Path) -> dict:
    return {str(p.relative_to(root)): p.read_bytes() for p in sorted(root.rglob("*")) if p.is_file()}


def zip_library(lib: Path, out: Path, method: int, top: str, level: int | None = None, extra: dict | None = None) -> None:
    with zipfile.ZipFile(out, "w", compression=method, compresslevel=level) as z:
        if top:
            z.writestr(zipfile.ZipInfo(top + "/"), b"")  # a directory entry
        for p in sorted(lib.rglob("*")):
            if p.is_file():
                z.write(p, (top + "/" if top else "") + str(p.relative_to(lib)).replace(os.sep, "/"))
        for name, data in (extra or {}).items():
            z.writestr(name, data)


def check_archives(e: str, lib: Path, expect: dict, tmp: Path) -> None:
    stage_tmp = tmp / "tmpdir"
    stage_tmp.mkdir()
    env = dict(os.environ, TMPDIR=str(stage_tmp), TEMP=str(stage_tmp), TMP=str(stage_tmp))
    cases = {
        "deflated.zip": (zipfile.ZIP_DEFLATED, "acme-parts", 9, {"acme-parts/.git/HEAD": b"ref: x\n"}),
        "stored.zip": (zipfile.ZIP_STORED, "", None, None),
        "fast.zip": (zipfile.ZIP_DEFLATED, "acme-parts-0.1.0", 1, None),
    }
    for name, (method, top, level, extra) in cases.items():
        archive = tmp / name
        zip_library(lib, archive, method, top, level, extra)
        dest = tmp / ("from-" + name)
        out = run([e, "catalog", "install", str(archive), "--to", str(dest)], env=env).stdout
        assert "installed acme-parts" in out and str(archive) in out, out
        got = tree(dest / "acme-parts")
        assert got == expect, (name, sorted(set(got) ^ set(expect)))
    # over HTTP, when curl is there
    if shutil.which("curl"):
        class Quiet(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *args) -> None:
                pass

        handler = functools.partial(Quiet, directory=str(tmp))
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        try:
            url = f"http://127.0.0.1:{srv.server_address[1]}/deflated.zip"
            run([e, "catalog", "install", url, "--to", str(tmp / "from-url")], env=env)
            assert tree(tmp / "from-url" / "acme-parts") == expect
            bad = run([e, "catalog", "install", url + ".missing", "--to", str(tmp / "nowhere")], env=env, ok=False)
            assert bad.returncode != 0 and "cannot fetch" in bad.stderr, bad.stderr
        finally:
            srv.shutdown()
    # an entry outside the library, a corrupt archive, a file that is not one
    evil = tmp / "evil.zip"
    zip_library(lib, evil, zipfile.ZIP_DEFLATED, "", 6, {"/etc/evil.txt": b"x"})
    r = run([e, "catalog", "install", str(evil), "--to", str(tmp / "evil")], env=env, ok=False)
    assert r.returncode != 0 and "not a path inside the library" in r.stderr, r.stderr
    assert not (tmp / "evil").exists()
    corrupt = tmp / "corrupt.zip"
    data = bytearray((tmp / "deflated.zip").read_bytes())
    i = data.index(b"model.lua") + 40  # inside that entry's compressed data
    data[i] ^= 0xFF
    corrupt.write_bytes(bytes(data))
    r = run([e, "catalog", "install", str(corrupt), "--to", str(tmp / "corrupt")], env=env, ok=False)
    assert r.returncode != 0 and ("CRC-32" in r.stderr or "does not decode" in r.stderr), r.stderr
    notzip = tmp / "notes.zip"
    notzip.write_text("not an archive, though long enough to have an end record\n" * 4)
    r = run([e, "catalog", "install", str(notzip), "--to", str(tmp / "notzip")], env=env, ok=False)
    assert r.returncode != 0 and "not a zip archive" in r.stderr, r.stderr
    left = [p.name for p in stage_tmp.iterdir()]
    assert not left, f"staging left behind: {left}"


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

        # a large file, for the archives' long matches and dynamic codes
        notes = lib / "notes"
        notes.mkdir()
        (notes / "big.txt").write_text("".join(f"line {i}: {'abcdefgh' * (i % 13)} {i * 7919 % 10007}\n" for i in range(12000)))
        installed = Path(tmp) / "installed"
        run([e, "catalog", "install", str(lib), "--to", str(installed)])
        assert (installed / "acme-parts" / "components" / "toggle" / "model.lua").is_file()
        check_archives(e, lib, tree(installed / "acme-parts"), Path(tmp))
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
    print("PASS: new, validate, install (a directory, zip archives, a URL) and list")
    return 0


if __name__ == "__main__":
    sys.exit(main())
