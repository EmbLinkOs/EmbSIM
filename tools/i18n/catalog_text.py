#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""A catalog entry's text in another language (its i18n.<lang> block).

    catalog_text.py status [--lang fr]          entries translated and not, by size
    catalog_text.py check [--lang fr]           exit 1 if an entry has no translation or
                                                one made from English that has changed since
    catalog_text.py dump ENTRY... [--lang fr]   the English text to translate, as JSON
    catalog_text.py apply FILE [--lang fr]      write the translations FILE holds

Each block records in `source` a fingerprint of the English it was made
from, so a description or fidelity note edited afterwards shows up as out of
date until its translation is brought in step (apply rewrites the fingerprint).

ENTRY is an entry's directory or description file. FILE maps each entry's
description file to {"description", "fidelity": {tier: {"claims",
"omissions"}}}, as dump writes it with the English in place. apply checks
each translated tier exists in the English and lists as many omissions, then
writes the block at the end of the description without touching the rest.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("component.json", "board.json", "mcu.json")


def entries():
    for name in NAMES:
        yield from sorted((ROOT / "catalog").rglob(name))


def resolve(arg):
    p = Path(arg)
    if not p.is_absolute():
        p = (Path.cwd() / p).resolve()
    if p.is_dir():
        for name in NAMES:
            if (p / name).exists():
                return p / name
    return p


def english(desc):
    out = {"description": desc.get("description", "")}
    fid = {}
    for tier, decl in (desc.get("fidelity") or {}).items():
        if not decl:
            continue
        t = {"claims": decl.get("claims", "")}
        if decl.get("omissions"):
            t["omissions"] = list(decl["omissions"])
        fid[tier] = t
    if fid:
        out["fidelity"] = fid
    return out


def fingerprint(text):
    """the source fingerprint of an entry's English text"""
    blob = json.dumps(text, sort_keys=True, ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(blob).hexdigest()[:12]


def size(text):
    return len(text.get("description", "")) + sum(
        len(t.get("claims", "")) + sum(map(len, t.get("omissions", []))) for t in text.get("fidelity", {}).values())


def check(desc, tr):
    """the problems of a translation against its entry's English"""
    errs = []
    fid = desc.get("fidelity") or {}
    for tier, t in (tr.get("fidelity") or {}).items():
        if not fid.get(tier):
            errs.append(f"tier {tier} is not in the English")
        elif len(t.get("omissions", [])) != len(fid[tier].get("omissions", [])):
            errs.append(f"tier {tier} lists {len(t.get('omissions', []))} omissions, the English "
                        f"{len(fid[tier].get('omissions', []))}")
    return errs


def write(path, lang, tr):
    """put i18n.<lang> in the description file, keeping its own layout"""
    text = path.read_text(encoding="utf-8")
    desc = json.loads(text)
    i18n = desc.get("i18n", {})
    tr = dict(source=fingerprint(english(desc)), **{k: v for k, v in tr.items() if k != "source"})
    i18n[lang] = tr
    block = json.dumps(i18n, indent=2, ensure_ascii=False).replace("\n", "\n  ")
    if "i18n" in desc:
        # the block is the file's last member (this tool wrote it)
        m = re.search(r',\s*"i18n"\s*:', text)
        if not m:
            raise SystemExit(f"{path}: i18n is not where this tool writes it")
        head = text[:m.start()]
    else:
        head = text[:text.rstrip().rfind("}")].rstrip()
    path.write_text(f'{head},\n  "i18n": {block}\n}}\n', encoding="utf-8")
    if json.loads(path.read_text(encoding="utf-8")).get("i18n", {}).get(lang) != tr:
        raise SystemExit(f"{path}: the written block does not read back")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("status", "check", "dump", "apply"))
    ap.add_argument("args", nargs="*")
    ap.add_argument("--lang", default="fr")
    a = ap.parse_args()
    if a.cmd == "status":
        done = todo = 0
        rows = []
        for p in entries():
            d = json.loads(p.read_text(encoding="utf-8"))
            n = size(english(d))
            has = a.lang in d.get("i18n", {})
            done += n if has else 0
            todo += 0 if has else n
            if not has:
                rows.append((n, p.relative_to(ROOT)))
        for n, p in sorted(rows):
            print(f"{n:7d}  {p}")
        print(f"{a.lang}: {done} characters translated, {todo} to go, {len(rows)} entries")
        return 0
    if a.cmd == "check":
        bad = 0
        for p in entries():
            d = json.loads(p.read_text(encoding="utf-8"))
            tr = d.get("i18n", {}).get(a.lang)
            rel = p.relative_to(ROOT)
            if tr is None:
                print(f"{rel}: no {a.lang} translation")
                bad += 1
            elif tr.get("source") != fingerprint(english(d)):
                print(f"{rel}: the {a.lang} translation is out of date (its English has changed)")
                bad += 1
            else:
                for e in check(d, tr):
                    print(f"{rel}: {e}")
                    bad += 1
        print(f"{a.lang}: {bad} problem(s)")
        return 1 if bad else 0
    if a.cmd == "dump":
        out = {}
        for arg in a.args:
            p = resolve(arg)
            out[str(p.relative_to(ROOT))] = english(json.loads(p.read_text(encoding="utf-8")))
        print(json.dumps(out, indent=2, ensure_ascii=False))
        return 0
    bad = 0
    for arg in a.args:
        data = json.loads(Path(arg).read_text(encoding="utf-8"))
        for rel, tr in data.items():
            p = ROOT / rel
            desc = json.loads(p.read_text(encoding="utf-8"))
            errs = check(desc, tr)
            for e in errs:
                print(f"{rel}: {e}")
            bad += len(errs)
            if not errs:
                write(p, a.lang, tr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
