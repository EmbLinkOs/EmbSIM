#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Part families: many real part numbers from one datasheet table.

    families.py generate [FAMILY...]   write the catalog entries the families describe
    families.py check                  exit 1 if a generated entry differs from its family
    families.py verify [FAMILY...] [--cache DIR]
                                       download each family's datasheets, check their
                                       sha256 and find every row's values in their text
    families.py list                   the families and the part numbers they hold

A family is `catalog/families/<name>/family.json` (kind `family`) beside
the files its entries share (symbol.svg, artwork.svg). Its `template` is a
component description and its `tests` are fixtures, both written with
`${var}` placeholders; each of its `rows` is one real part number, giving
the variables. A string that is a single placeholder becomes the variable's
value as it is (a number stays a number); a placeholder inside a longer
string is replaced by the value's text. The artwork's `${label}` is the
part's marking.

Every row names the datasheet it comes from and the `evidence` that shows
its values there: regular expressions, written with the same placeholders
(a number matching however many trailing zeros the datasheet prints; `#{var}`
the value's text as it is, for a repeat count), that `verify` must find in the datasheet's text (pdftotext -layout, runs of
white space taken as one space). The values a model is built from are
therefore read from the datasheet, not typed a second time.

A generated entry's French text comes from the template's `i18n.fr` with
the same variables; its `source` fingerprint is computed as
tools/i18n/catalog_text.py computes it, so the French stays in step.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FAMILIES = ROOT / "catalog" / "families"
COMPONENTS = ROOT / "catalog" / "components"
sys.path.insert(0, str(ROOT / "tools" / "i18n"))
import catalog_text  # noqa: E402

VAR = re.compile(r"\$\{([a-z_][a-z0-9_]*)\}")
RAW = re.compile(r"#\{([a-z_][a-z0-9_]*)\}")  # in evidence: the value's text itself (a repeat count)
GENERATED = "generated from catalog/families/{name}/family.json by tools/catalog/families.py; edit the family, not this file"


def fmt(v):
    if isinstance(v, float) and v.is_integer() and abs(v) < 1e15:
        return str(int(v))
    return str(v)


def subst(node, env, where):
    """the template node with each ${var} replaced from env"""
    if isinstance(node, str):
        whole = VAR.fullmatch(node)
        if whole:
            if whole.group(1) not in env:
                raise SystemExit(f"{where}: no variable {whole.group(1)}")
            return env[whole.group(1)]

        def one(m):
            if m.group(1) not in env:
                raise SystemExit(f"{where}: no variable {m.group(1)}")
            return fmt(env[m.group(1)])
        return VAR.sub(one, node)
    if isinstance(node, list):
        return [subst(x, env, where) for x in node]
    if isinstance(node, dict):
        return {subst(k, env, where): subst(v, env, where) for k, v in node.items()}
    return node


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def families(names=None):
    for p in sorted(FAMILIES.glob("*/family.json")):
        if not names or p.parent.name in names:
            yield p.parent.name, load(p)


def env_of(fam, row):
    env = dict(fam.get("vars", {}))
    env.update(row)
    ds = fam["datasheets"][row["datasheet"]]
    env.setdefault("datasheet_url", ds["url"])
    env.setdefault("label", row["mpn"])
    return env


def render(name, fam):
    """{relative path: text} of every file the family generates"""
    out = {}
    base = FAMILIES / name
    for row in fam["rows"]:
        env = env_of(fam, row)
        where = f"{name}/{row['mpn']}"
        desc = subst(fam["template"], env, where)
        if isinstance(desc.get("keywords"), list):  # a part's number may also be its family's name
            desc["keywords"] = list(dict.fromkeys(k for k in desc["keywords"] if k != ""))
        ds = dict(fam["datasheets"][row["datasheet"]])
        ds.pop("text_hash", None)
        notes = ds.get("notes", "")
        ds["notes"] = (notes + " " if notes else "") + GENERATED.format(name=name)
        desc["provenance"] = dict(desc.get("provenance", {}))
        desc["provenance"]["sources"] = [ds] + list(desc["provenance"].get("sources", []))
        fr = desc.get("i18n", {}).get("fr")
        if fr is not None:
            english = {k: v for k, v in desc.items() if k != "i18n"}
            fr = dict(fr)
            fr.pop("source", None)
            desc["i18n"] = dict(desc["i18n"])
            desc["i18n"]["fr"] = dict(source=catalog_text.fingerprint(catalog_text.english(english)), **fr)
        short = desc["id"].split("/", 1)[1]
        d = f"catalog/components/{short}"
        out[f"{d}/component.json"] = json.dumps(desc, indent=2, ensure_ascii=False) + "\n"
        for f in ("symbol.svg", "artwork.svg"):
            if (base / f).exists():
                out[f"{d}/{f}"] = VAR.sub(lambda m: fmt(env[m.group(1)]), (base / f).read_text(encoding="utf-8"))
        for tname, t in fam.get("tests", {}).items():
            if tname in row.get("skip_tests", []):
                continue
            test = subst(t, env, f"{where} test {tname}")
            out[f"{d}/tests/{tname}.json"] = json.dumps(test, indent=2, ensure_ascii=False) + "\n"
    return out


def cmd_generate(names):
    n = 0
    for name, fam in families(names):
        files = render(name, fam)
        dirs = {Path(p).parent.as_posix() for p in files if p.endswith("component.json")}
        for d in dirs:  # a family owns its entries' tests: stale ones go
            t = ROOT / d / "tests"
            if t.is_dir():
                for old in t.glob("*.json"):
                    if f"{d}/tests/{old.name}" not in files:
                        old.unlink()
        for rel, text in files.items():
            p = ROOT / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text, encoding="utf-8")
        n += len(fam["rows"])
        print(f"{name}: {len(fam['rows'])} part(s)")
    print(f"{n} part(s) written")
    return 0


def cmd_check():
    bad = 0
    for name, fam in families():
        files = render(name, fam)
        for rel, text in files.items():
            p = ROOT / rel
            if not p.exists() or p.read_text(encoding="utf-8") != text:
                print(f"{rel}: differs from catalog/families/{name} (run tools/catalog/families.py generate)")
                bad += 1
        for d in {Path(p).parent.as_posix() for p in files if p.endswith("component.json")}:
            for old in (ROOT / d / "tests").glob("*.json"):
                if f"{d}/tests/{old.name}" not in files:
                    print(f"{d}/tests/{old.name}: not in catalog/families/{name}")
                    bad += 1
    print(f"families: {bad} problem(s)")
    return 1 if bad else 0


def pattern_of(v):
    """a regular expression for a value as a datasheet prints it: a number
    matches its value however many trailing zeros it is printed with"""
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return re.escape(str(v))
    s = fmt(v)
    return re.escape(s) + ("0*" if "." in s else r"(?:\.0+)?") + r"(?![\d.])"


def text_of(pdf):
    out = subprocess.run(["pdftotext", "-layout", str(pdf), "-"], check=True, capture_output=True).stdout
    return " ".join(out.decode("utf-8", "replace").split())


def fetch(url, dest):
    """download a datasheet; some vendors' servers refuse Python's client, so curl is tried after it"""
    part = dest.with_suffix(".part")
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "embsim-catalog-verify"})
        with urllib.request.urlopen(req, timeout=60) as r, open(part, "wb") as f:
            shutil.copyfileobj(r, f)
    except OSError as e:
        if not shutil.which("curl"):
            raise SystemExit(f"{url}: {e}")
        subprocess.run(["curl", "-sSfL", "--max-time", "120", "-o", str(part), url], check=True)
    part.replace(dest)


def cmd_verify(cache, names=None):
    cache.mkdir(parents=True, exist_ok=True)
    bad = 0
    for name, fam in families(names):
        texts = {}
        for key, ds in fam["datasheets"].items():
            pdf = cache / (hashlib.sha256(ds["url"].encode("utf-8")).hexdigest()[:16] + ".pdf")
            if not pdf.exists():
                fetch(ds["url"], pdf)
            digest = "sha256:" + hashlib.sha256(pdf.read_bytes()).hexdigest()
            if digest != ds["hash"]:
                print(f"{name}: {ds['url']} is {digest}, the family records {ds['hash']}")
                bad += 1
                continue
            texts[key] = text_of(pdf)
        for row in fam["rows"]:
            env = env_of(fam, row)
            text = texts.get(row["datasheet"])
            if text is None:
                continue
            for ev in fam.get("evidence", []) + row.get("evidence", []):
                pat = VAR.sub(lambda m: pattern_of(env[m.group(1)]), RAW.sub(lambda m: fmt(env[m.group(1)]), ev))
                if not re.search(pat, text):
                    print(f"{name}/{row['mpn']}: not in {row['datasheet']}: {pat}")
                    bad += 1
        print(f"{name}: {len(fam['rows'])} part(s) checked against {len(texts)} datasheet(s)")
    print(f"verify: {bad} problem(s)")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("generate", "check", "verify", "list"))
    ap.add_argument("names", nargs="*")
    ap.add_argument("--cache", type=Path, default=Path(tempfile.gettempdir()) / "embsim-datasheets")
    a = ap.parse_args()
    if a.cmd == "generate":
        return cmd_generate(a.names)
    if a.cmd == "check":
        return cmd_check()
    if a.cmd == "verify":
        return cmd_verify(a.cache, a.names)
    total = 0
    for name, fam in families(a.names):
        print(f"{name}: {', '.join(r['mpn'] for r in fam['rows'])}")
        total += len(fam["rows"])
    print(f"{total} part(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
