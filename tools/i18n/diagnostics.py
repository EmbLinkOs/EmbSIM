#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The engine's diagnostic messages and their French (core/util/diag_fr.c).

Every diagnostic the engine raises goes through esk_diag_raise, esk_rf_diag,
esk_comp_diag or the MCU runtime's raise(), with a code and a printf format.
This tool finds each (code, format) pair in core/ and cli/, and checks
diag_fr.c gives every one a French format with the same conversions in the
same order (so the values the English carries are carried unchanged).

    diagnostics.py            check (exit 1 on a missing or mismatched entry)
    diagnostics.py --list     print the pairs found, as C table rows
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# each call that raises a diagnostic: the argument index of its code and of
# its format (or of its finished message, for the wrappers that take one)
CALLS = {
    "esk_diag_raise": (2, 8),
    "esk_rf_diag": (2, 3),
    "esk_comp_diag": (2, 3),
    "raise": (2, 3),
    "ld_diag": (2, 4),
    "diag": (2, 4),
    "f4_warn": (2, 3),
    "raise_isl": (2, 4),
    "raise_pin": (2, 3),
    "fail_step": (2, 4),
}
CODE = re.compile(r"[a-z0-9_]+\.[a-z0-9_.]+")
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
CONV = re.compile(r"%(?:%|[-+ #0]*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfgGcspa])")


def split_args(text):
    """the top-level arguments of a call's argument text"""
    out, depth, cur, i = [], 0, [], 0
    while i < len(text):
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while text[j] != c:
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    out.append("".join(cur).strip())
    return out


def calls(text):
    """(name, offset, argument text) of each call to one of CALLS"""
    for m in re.finditer(r"(?<![\w.>])(" + "|".join(CALLS) + r")\s*\(", text):
        i = j = m.end()
        depth = 1
        while depth and j < len(text):
            c = text[j]
            if c == '"' or (c == "'" and text[j - 1] != "\\"):
                q = c
                j += 1
                while text[j] != q:
                    j += 2 if text[j] == "\\" else 1
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            j += 1
        before = text[max(0, m.start() - 40):m.start()]
        if re.search(r"\b(void|int|es_status)\s+$", before):
            continue  # the definition, not a call
        yield m.group(1), m.start(), text[i:j - 1]


def literals(expr):
    """the formats an argument can be: a literal, or each literal of a ?: choice"""
    lits = LIT.findall(expr)
    if not lits:
        return []
    if re.fullmatch(r'\s*(?:"(?:[^"\\]|\\.)*"\s*)+', expr):
        return ["".join(lits)]
    if "?" in expr:
        return [m for m in lits if not CODE.fullmatch(m)]
    return []


def built(text, at, name):
    """the formats a message variable was built with before the call"""
    body = text[max(0, text.rfind("\n{", 0, at)):at]
    found = list(re.finditer(r"esk_snprintf\s*\(\s*" + re.escape(name) + r"\s*,", body))
    if not found:
        return []
    rest = body[found[-1].end():]
    args = split_args(rest[:rest.find(");")])
    return literals(args[1]) if len(args) > 1 else []


def pairs():
    """{(code, format): where} for every format raised; and the call sites whose
    message comes from somewhere this tool cannot follow"""
    found, unresolved, fragments = {}, [], {}
    for path in sorted(list((ROOT / "core").rglob("*.c")) + list((ROOT / "cli").glob("*.c"))):
        if path.name == "diag_fr.c":
            continue
        text = path.read_text(encoding="utf-8")
        for name, at, argtext in calls(text):
            args = split_args(argtext)
            ci, fi = CALLS[name]
            if len(args) <= fi:
                continue
            codes = [c for c in LIT.findall(args[ci]) if CODE.fullmatch(c)]
            if not codes:
                continue
            fmts = literals(args[fi])
            if fmts == ["%s"] and len(args) > fi + 1:
                fmts = literals(args[fi + 1]) or built(text, at, args[fi + 1])
            elif not fmts and re.fullmatch(r"\w+", args[fi]):
                fmts = built(text, at, args[fi])
            where = f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, at) + 1}"
            passed_on = literals(args[fi]) == ["%s"]
            for arg in args[fi + 1 + passed_on:]:
                if "?" not in arg or "esk_json_get" in arg:
                    continue
                for frag in literals(arg):
                    if frag not in ("", "?") and not CONV.search(frag):
                        fragments.setdefault(frag, where)
            if not fmts:
                unresolved.append((codes[0], where, args[fi]))
            for code in codes:
                for fmt in fmts:
                    found.setdefault((code, fmt), where)
    return found, unresolved, fragments


def table():
    text = (ROOT / "core" / "util" / "diag_fr.c").read_text(encoding="utf-8")
    rows = {}
    for m in re.finditer(r'\{\s*"([^"]*)",\s*((?:"(?:[^"\\]|\\.)*"\s*)+),\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\}', text):
        en = "".join(LIT.findall(m.group(2)))
        fr = "".join(LIT.findall(m.group(3)))
        rows[(m.group(1), en)] = fr
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="print the formats and fragments found, as C rows")
    ap.add_argument("--unresolved", action="store_true", help="the calls whose message is not a literal")
    args = ap.parse_args()
    found, unresolved, fragments = pairs()
    if args.unresolved:
        for code, where, arg in unresolved:
            print(f"{where}: {code}: {arg}")
        return 0
    if args.list:
        for (code, fmt), where in sorted(found.items()):
            print(f'    {{ "{code}", "{fmt}",\n      "" }}, /* {where} */')
        for frag, where in sorted(fragments.items()):
            print(f'    {{ "", "{frag}", "" }}, /* {where} */')
        return 0
    rows = table()
    bad = 0
    want = dict(found)
    want.update({("", f): w for f, w in fragments.items()})
    for (code, fmt), where in sorted(want.items()):
        fr = rows.get((code, fmt))
        if fr is None:
            print(f"missing: {code or 'fragment'} ({where}): {fmt}")
            bad += 1
        elif CONV.findall(fr) != CONV.findall(fmt):
            print(f"conversions differ: {code}: {CONV.findall(fmt)} vs {CONV.findall(fr)}")
            bad += 1
        elif not fr:
            print(f"empty: {code}: {fmt}")
            bad += 1
    # the fragments of messages built elsewhere (a table of words, a parser's
    # error) and the formats of codes whose messages are passed through are
    # kept in the table by hand, so only a raised code's own formats go stale
    passed = {code for code, _, _ in unresolved}
    stale = [k for k in rows if k not in want and k[0] and k[0] not in passed]
    for code, fmt in stale:
        print(f"no longer raised: {code}: {fmt}")
    print(f"{len(found)} diagnostic formats and {len(fragments)} fragments; {len(rows)} rows in French; "
          f"{bad} problem(s), {len(stale)} stale")
    return 1 if bad or stale else 0


if __name__ == "__main__":
    sys.exit(main())
