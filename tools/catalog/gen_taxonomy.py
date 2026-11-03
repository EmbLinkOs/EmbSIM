#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The workbench's copy of catalog/taxonomy.json (ui/packages/protocol/src/taxonomy.ts).

    gen_taxonomy.py           write it
    gen_taxonomy.py --check   exit 1 when it is not what the taxonomy gives
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "catalog" / "taxonomy.json"
OUT = ROOT / "ui" / "packages" / "protocol" / "src" / "taxonomy.ts"


def render() -> str:
    doc = json.loads(SRC.read_text(encoding="utf-8"))
    body = json.dumps({"categories": doc["categories"], "legacy": doc.get("legacy", {})}, indent=2, ensure_ascii=False)
    return (
        "// SPDX-License-Identifier: Apache-2.0\n"
        "// Copyright 2026 Junior Deogracias\n"
        "//\n"
        "// Generated from catalog/taxonomy.json by tools/catalog/gen_taxonomy.py: do not edit.\n"
        "// The catalog's categories and sub-categories (after Proteus's device\n"
        "// categories), named in each interface language.\n\n"
        "export interface TaxonomyName {\n  en: string;\n  fr: string;\n  es: string;\n}\n\n"
        "export interface TaxonomyCategory {\n  id: string;\n  name: TaxonomyName;\n  subcategories: { id: string; name: TaxonomyName }[];\n}\n\n"
        f"export const TAXONOMY: {{ categories: TaxonomyCategory[]; legacy: Record<string, string> }} = {body};\n"
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    text = render()
    if a.check:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            print(f"{OUT.relative_to(ROOT)} is out of date: run tools/catalog/gen_taxonomy.py")
            return 1
        print("taxonomy.ts is up to date")
        return 0
    OUT.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
