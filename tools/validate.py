#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Validate EmbSIM description files against the JSON Schemas in schemas/ and
the semantic rules of SPEC-005, SPEC-006, SPEC-007, SPEC-010, SPEC-012 and
SPEC-014.

    validate.py PATH...        validate the given files (by their "kind")
    validate.py --all          validate every *.json under schemas/ (meta),
                               catalog/ and examples/
    validate.py --strict ...   warnings fail the run too

Uses the `jsonschema` package when importable; otherwise a built-in checker
covers the subset of JSON Schema 2020-12 the EmbSIM schemas use (required,
type, enum, const, additionalProperties, patternProperties, propertyNames,
items, pattern, bounds, oneOf/anyOf/allOf, $ref). Each finding is printed as
`file:node: severity code: message`; the exit status is 1 when any error was
found, 2 on a usage error.
"""
from __future__ import annotations

import argparse
import copy
import math
import fnmatch
import glob
import hashlib
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from version_ranges import matches as version_matches
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Iterable, Optional

ROOT = Path(__file__).resolve().parents[1]
SCHEMA_DIR = ROOT / "schemas"
CATALOG_DIR = ROOT / "catalog"
EXAMPLES_DIR = ROOT / "examples"
SCHEMA_BASE = "https://emblink.dev/embsim/schemas/"

KIND_SCHEMAS = {
    "transient_request": "transient-request.schema.json",
    "transient_capture": "transient-capture.schema.json",
    "parameter_sweep_request": "parameter-sweep-request.schema.json",
    "parameter_sweep": "parameter-sweep.schema.json",
    "library_lock": "library-lock.schema.json",
    "engineering_manifest": "engineering-manifest.schema.json",
    "component": "component.schema.json",
    "component_test": "component-test.schema.json",
    "mcu": "mcu.schema.json",
    "package": "package.schema.json",
    "board": "board.schema.json",
    "project": "project.schema.json",
    "scenario": "scenario.schema.json",
    "recording": "recording.schema.json",
    "toolchain": "toolchain.schema.json",
    "library": "library.schema.json",
    "registers": "registers.schema.json",
    "board_reference": "board-reference.schema.json",
}

SI_SUFFIX = {
    "T": 1e12, "G": 1e9, "M": 1e6, "meg": 1e6, "MEG": 1e6, "Meg": 1e6,
    "k": 1e3, "K": 1e3, "m": 1e-3, "u": 1e-6, "µ": 1e-6, "n": 1e-9,
    "p": 1e-12, "f": 1e-15,
}
SI_RE = re.compile(r"^([+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)(meg|MEG|Meg|[TGMkKmunpfµ])?$")
TIME_RE = re.compile(r"^([0-9]+(?:\.[0-9]+)?)\s?(ps|ns|us|ms|s)$")
TIME_UNIT_PS = {"ps": 1, "ns": 10**3, "us": 10**6, "ms": 10**9, "s": 10**12}
SIZE_RE = re.compile(r"^(0x[0-9A-Fa-f]+|[0-9]+)([KMG]?)$")

SPDX_IDS = {
    "0BSD", "Apache-2.0", "BSD-1-Clause", "BSD-2-Clause", "BSD-3-Clause", "BSL-1.0",
    "CC0-1.0", "CC-BY-4.0", "CC-BY-SA-4.0", "CC-BY-3.0", "CC-BY-SA-3.0", "CERN-OHL-P-2.0",
    "CERN-OHL-S-2.0", "CERN-OHL-W-2.0", "GPL-2.0-only", "GPL-2.0-or-later", "GPL-3.0-only",
    "GPL-3.0-or-later", "ISC", "LGPL-2.1-only", "LGPL-2.1-or-later", "LGPL-3.0-only",
    "LGPL-3.0-or-later", "MIT", "MIT-0", "MPL-2.0", "TAPR-OHL-1.0", "Unlicense", "Zlib",
}

POWER_SIGNAL_RE = re.compile(r"^(V(DD|SS|CC|BAT|DDA|SSA|REF\+?|REF-?|CAP[0-9]*)|GND|AV(CC|DD)|AGND|VIN|3V3|5V)$")
GPIO_RE = re.compile(r"^P([A-Z])([0-9]+)$")

PRIMITIVE_PINS = {
    "resistor": ["1", "2"], "capacitor": ["1", "2"], "inductor": ["1", "2"],
    "instrument_input": ["+", "-"], "vsource": ["+", "-"], "isource": ["+", "-"], "switch": ["1", "2"],
    "potentiometer": ["1", "W", "2"], "diode": ["A", "K"], "led": ["A", "K"], "rgb_led": ["R", "COM", "G", "B"], "rgb_indicator": ["R", "COM", "G", "B"], "zener": ["A", "K"],
    "bjt_npn": ["C", "B", "E"], "bjt_pnp": ["C", "B", "E"], "nmos": ["D", "G", "S"],
    "pmos": ["D", "G", "S"], "opamp": ["+", "-", "OUT"], "regulator": ["IN", "OUT", "GND"],
    "pull": ["1"], "indicator": ["A", "K"],
}
PRIMITIVE_INPUTS = {
    "switch": {"pressed", "closed"}, "push_button_digital": {"pressed"},
    "switch_digital": {"closed"}, "potentiometer": {"position"},
}
TOOLCHAIN_PLACEHOLDERS = {
    "cc", "cflags", "link_flags", "defines", "source", "object", "objects", "image", "hex", "bin",
    "map", "linker_script", "target", "objcopy", "include", "flags", "link",
}


# --------------------------------------------------------------------------
# findings
# --------------------------------------------------------------------------


@dataclass
class Finding:
    file: str
    node: str
    severity: str
    code: str
    message: str

    def line(self) -> str:
        return f"{self.file}:{self.node}: {self.severity} {self.code}: {self.message}"


class Report:
    def __init__(self) -> None:
        self.findings: list[Finding] = []
        self.files: list[str] = []

    def add(self, file: Path | str, node: str, severity: str, code: str, message: str) -> None:
        f = Path(file)
        try:
            shown = str(f.resolve().relative_to(ROOT))
        except ValueError:
            shown = str(f)
        self.findings.append(Finding(shown, node or "/", severity, code, message))

    def error(self, file: Path | str, node: str, code: str, message: str) -> None:
        self.add(file, node, "error", code, message)

    def warning(self, file: Path | str, node: str, code: str, message: str) -> None:
        self.add(file, node, "warning", code, message)

    @property
    def errors(self) -> int:
        return sum(1 for f in self.findings if f.severity == "error")

    @property
    def warnings(self) -> int:
        return sum(1 for f in self.findings if f.severity == "warning")


def ptr(*parts: Any) -> str:
    """A JSON-pointer-like node path."""
    return "/" + "/".join(str(p).replace("~", "~0").replace("/", "~1") for p in parts)


# --------------------------------------------------------------------------
# value helpers
# --------------------------------------------------------------------------


def parse_si(value: Any) -> Optional[float]:
    """Parse a number or an SI-suffixed string into a float; None when invalid."""
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return float(value)
    if not isinstance(value, str):
        return None
    m = SI_RE.match(value.strip())
    if not m:
        return None
    return float(m.group(1)) * SI_SUFFIX.get(m.group(2) or "", 1.0)


def parse_time_ps(value: Any) -> Optional[int]:
    """Parse a time string (unit or picosecond digits) to picoseconds; None when invalid."""
    if not isinstance(value, str):
        return None
    s = value.strip()
    if s.isdigit():
        return int(s)
    m = TIME_RE.match(s)
    if not m:
        return None
    number, unit = m.group(1), m.group(2)
    whole, _, frac = number.partition(".")
    scale = TIME_UNIT_PS[unit]
    ps = int(whole) * scale
    if frac:
        frac_scaled = int(frac) * scale
        divisor = 10 ** len(frac)
        if frac_scaled % divisor:
            return None  # sub-picosecond fraction
        ps += frac_scaled // divisor
    return ps


def parse_size(value: Any) -> Optional[int]:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value if value >= 0 else None
    if not isinstance(value, str):
        return None
    m = SIZE_RE.match(value)
    if not m:
        return None
    base = int(m.group(1), 0) if m.group(1).startswith("0x") else int(m.group(1))
    return base * {"": 1, "K": 1024, "M": 1024 ** 2, "G": 1024 ** 3}[m.group(2)]


def is_spdx(expr: str) -> bool:
    tokens = re.split(r"\s+(?:OR|AND|WITH)\s+", expr.strip())
    return all(t in SPDX_IDS or t.startswith("LicenseRef-") for t in tokens if t)


def load_json(path: Path, report: Report) -> Optional[Any]:
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, UnicodeDecodeError) as exc:
        report.error(path, "/", "io.read", f"cannot read: {exc}")
    except json.JSONDecodeError as exc:
        report.error(path, f"/line {exc.lineno}", "schema.syntax", f"invalid JSON: {exc.msg}")
    return None


def svg_ids(path: Path, report: Report) -> Optional[set[str]]:
    """Parse an SVG and return its element ids, or None (reported) when not well-formed."""
    try:
        tree = ET.parse(path)
    except (ET.ParseError, OSError) as exc:
        report.error(path, "/", "svg.malformed", f"not well-formed XML: {exc}")
        return None
    root = tree.getroot()
    if root.tag not in ("{http://www.w3.org/2000/svg}svg", "svg"):
        report.error(path, "/", "svg.malformed", f"root element is {root.tag}, expected svg")
    return {el.get("id") for el in root.iter() if el.get("id")}


# --------------------------------------------------------------------------
# schema loading and the built-in checker
# --------------------------------------------------------------------------


class Schemas:
    def __init__(self, schema_dir: Path = SCHEMA_DIR) -> None:
        self.dir = schema_dir
        self.by_id: dict[str, Any] = {}
        self.by_name: dict[str, Any] = {}
        for p in sorted(schema_dir.glob("*.schema.json")):
            with open(p, "r", encoding="utf-8") as fh:
                schema = json.load(fh)
            self.by_name[p.name] = schema
            self.by_id[schema.get("$id", SCHEMA_BASE + p.name)] = schema
        self._jsonschema = None
        try:
            import jsonschema  # type: ignore

            self._jsonschema = jsonschema
        except ImportError:
            self._jsonschema = None

    @property
    def backend(self) -> str:
        return "jsonschema" if self._jsonschema else "builtin"

    def for_kind(self, kind: str) -> Optional[Any]:
        name = KIND_SCHEMAS.get(kind)
        return self.by_name.get(name) if name else None

    # ---- resolution --------------------------------------------------------

    def resolve_ref(self, ref: str, base_id: str) -> tuple[Any, str]:
        """Resolve a $ref relative to the schema with $id base_id; returns (subschema, its base $id)."""
        url, _, fragment = ref.partition("#")
        if url:
            if not url.startswith("http"):
                url = SCHEMA_BASE + url
            if url not in self.by_id:
                raise KeyError(f"unknown schema {url}")
            doc = self.by_id[url]
            base = url
        else:
            doc = self.by_id[base_id]
            base = base_id
        node = doc
        if fragment:
            for part in fragment.lstrip("/").split("/"):
                if not part:
                    continue
                part = part.replace("~1", "/").replace("~0", "~")
                if isinstance(node, list):
                    node = node[int(part)]
                else:
                    node = node[part]
        return node, base

    # ---- validation -------------------------------------------------------

    def validate(self, instance: Any, schema: Any) -> list[tuple[str, str]]:
        """Return [(node, message)] schema violations."""
        if self._jsonschema is not None:
            return self._validate_jsonschema(instance, schema)
        errs: list[tuple[str, str]] = []
        self._check(instance, schema, schema.get("$id", SCHEMA_BASE), [], errs, 0)
        return errs

    def _validate_jsonschema(self, instance: Any, schema: Any) -> list[tuple[str, str]]:
        js = self._jsonschema
        cls = js.Draft202012Validator
        validator = None
        try:
            from referencing import Registry, Resource  # type: ignore
            from referencing.jsonschema import DRAFT202012  # type: ignore

            registry = Registry()
            for sid, doc in self.by_id.items():
                registry = registry.with_resource(sid, Resource.from_contents(doc, default_specification=DRAFT202012))
            validator = cls(schema, registry=registry)
        except ImportError:
            resolver = js.RefResolver(base_uri=schema.get("$id", SCHEMA_BASE), referrer=schema, store=dict(self.by_id))
            validator = cls(schema, resolver=resolver)
        out: list[tuple[str, str]] = []
        best_match = js.exceptions.best_match
        for err in sorted(validator.iter_errors(instance), key=lambda e: list(e.absolute_path)):
            e = err
            # descend into oneOf/anyOf alternatives to the most relevant cause
            depth = 0
            while e.context and depth < 6:
                e = best_match(e.context)
                depth += 1
            node = ptr(*e.absolute_path) if e.absolute_path else "/"
            msg = e.message
            if e.validator in ("oneOf", "anyOf") and not e.context:
                msg = f"does not match any allowed form ({e.validator})"
            if e.validator == "additionalProperties":
                m = re.search(r"\('([^']+)' (?:was|were) unexpected\)", e.message)
                m2 = re.search(r"\(([^)]*) (?:was|were) unexpected\)", e.message)
                names = m.group(1) if m else (m2.group(1) if m2 else "")
                msg = f"unknown field {names}" if names else "unknown field"
            if e.validator == "required":
                m = re.search(r"'([^']+)' is a required property", e.message)
                msg = f"missing required field {m.group(1)}" if m else e.message
            if len(msg) > 300:
                msg = msg[:297] + "..."
            out.append((node, msg))
        # de-duplicate
        seen = set()
        uniq = []
        for item in out:
            if item not in seen:
                seen.add(item)
                uniq.append(item)
        return uniq

    # ---- built-in checker -------------------------------------------------

    @staticmethod
    def _type_ok(value: Any, t: str) -> bool:
        if t == "object":
            return isinstance(value, dict)
        if t == "array":
            return isinstance(value, list)
        if t == "string":
            return isinstance(value, str)
        if t == "integer":
            return (isinstance(value, int) and not isinstance(value, bool)) or (isinstance(value, float) and value.is_integer())
        if t == "number":
            return isinstance(value, (int, float)) and not isinstance(value, bool)
        if t == "boolean":
            return isinstance(value, bool)
        if t == "null":
            return value is None
        return True

    def _check(self, value: Any, schema: Any, base: str, path: list, errs: list, depth: int) -> None:
        if depth > 64:
            errs.append((ptr(*path), "schema nesting too deep"))
            return
        if schema is True or schema == {}:
            return
        if schema is False:
            errs.append((ptr(*path), "no value allowed here"))
            return
        if "$ref" in schema:
            try:
                sub, sub_base = self.resolve_ref(schema["$ref"], base)
            except (KeyError, IndexError, ValueError) as exc:
                errs.append((ptr(*path), f"unresolvable $ref {schema['$ref']}: {exc}"))
                return
            self._check(value, sub, sub_base, path, errs, depth + 1)
        here = ptr(*path) if path else "/"
        if "type" in schema:
            types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
            if not any(self._type_ok(value, t) for t in types):
                errs.append((here, f"expected {'|'.join(types)}, got {type(value).__name__ if value is not None else 'null'}"))
                return
        if "const" in schema and value != schema["const"]:
            errs.append((here, f"expected constant {json.dumps(schema['const'])}, got {json.dumps(value)[:60]}"))
        if "enum" in schema and value not in schema["enum"]:
            errs.append((here, f"{json.dumps(value)[:60]} is not one of {json.dumps(schema['enum'])}"))
        if isinstance(value, str):
            if "pattern" in schema and not re.search(schema["pattern"], value):
                errs.append((here, f"{json.dumps(value)[:60]} does not match {schema['pattern']}"))
            if "minLength" in schema and len(value) < schema["minLength"]:
                errs.append((here, f"shorter than {schema['minLength']} characters"))
            if "maxLength" in schema and len(value) > schema["maxLength"]:
                errs.append((here, f"longer than {schema['maxLength']} characters"))
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if "minimum" in schema and value < schema["minimum"]:
                errs.append((here, f"{value} is less than the minimum {schema['minimum']}"))
            if "maximum" in schema and value > schema["maximum"]:
                errs.append((here, f"{value} is greater than the maximum {schema['maximum']}"))
            if "exclusiveMinimum" in schema and value <= schema["exclusiveMinimum"]:
                errs.append((here, f"{value} is not greater than {schema['exclusiveMinimum']}"))
            if "exclusiveMaximum" in schema and value >= schema["exclusiveMaximum"]:
                errs.append((here, f"{value} is not less than {schema['exclusiveMaximum']}"))
            if "multipleOf" in schema and (value / schema["multipleOf"]) % 1 != 0:
                errs.append((here, f"{value} is not a multiple of {schema['multipleOf']}"))
        if isinstance(value, list):
            if "minItems" in schema and len(value) < schema["minItems"]:
                errs.append((here, f"fewer than {schema['minItems']} items"))
            if "maxItems" in schema and len(value) > schema["maxItems"]:
                errs.append((here, f"more than {schema['maxItems']} items"))
            if schema.get("uniqueItems"):
                seen = []
                for i, item in enumerate(value):
                    if item in seen:
                        errs.append((ptr(*path, i), "duplicate item"))
                    seen.append(item)
            prefix = schema.get("prefixItems", [])
            for i, item in enumerate(value):
                item_schema = prefix[i] if i < len(prefix) else schema.get("items", True)
                self._check(item, item_schema, base, path + [i], errs, depth + 1)
        if isinstance(value, dict):
            for req in schema.get("required", []):
                if req not in value:
                    errs.append((here, f"missing required field {req}"))
            if "minProperties" in schema and len(value) < schema["minProperties"]:
                errs.append((here, f"fewer than {schema['minProperties']} properties"))
            props = schema.get("properties", {})
            pprops = schema.get("patternProperties", {})
            for key, item in value.items():
                matched = False
                if key in props:
                    matched = True
                    self._check(item, props[key], base, path + [key], errs, depth + 1)
                for pat, sub in pprops.items():
                    if re.search(pat, key):
                        matched = True
                        self._check(item, sub, base, path + [key], errs, depth + 1)
                if "propertyNames" in schema:
                    self._check(key, schema["propertyNames"], base, path + [key], errs, depth + 1)
                if not matched:
                    ap = schema.get("additionalProperties", True)
                    if ap is False:
                        errs.append((ptr(*path, key), f"unknown field {key}"))
                    elif isinstance(ap, dict):
                        self._check(item, ap, base, path + [key], errs, depth + 1)
        for combinator in ("allOf",):
            for sub in schema.get(combinator, []):
                self._check(value, sub, base, path, errs, depth + 1)
        if "anyOf" in schema:
            branches = []
            for sub in schema["anyOf"]:
                sub_errs: list = []
                self._check(value, sub, base, path, sub_errs, depth + 1)
                branches.append(sub_errs)
            if not any(not b for b in branches):
                best = min(branches, key=len)
                errs.append((here, "does not match any allowed form (anyOf): " + "; ".join(f"{n}: {m}" for n, m in best[:3])))
        if "oneOf" in schema:
            branches = []
            for sub in schema["oneOf"]:
                sub_errs: list = []
                self._check(value, sub, base, path, sub_errs, depth + 1)
                branches.append(sub_errs)
            ok = [b for b in branches if not b]
            if len(ok) == 0:
                best = min(branches, key=len)
                errs.append((here, "does not match any allowed form (oneOf): " + "; ".join(f"{n}: {m}" for n, m in best[:3])))
            elif len(ok) > 1:
                errs.append((here, "matches more than one alternative (oneOf)"))


# --------------------------------------------------------------------------
# catalog index (components, boards, MCUs resolved by id)
# --------------------------------------------------------------------------


class Index:
    """Resolves catalog ids to their description files across library roots."""

    def __init__(self, roots: Iterable[Path]) -> None:
        self.roots = [Path(r) for r in roots]
        self.components: dict[str, Path] = {}
        self.boards: dict[str, Path] = {}
        self.mcus: dict[str, Path] = {}
        self.toolchains: dict[str, Path] = {}
        self._cache: dict[Path, Any] = {}
        for root in self.roots:
            self._scan(root)

    def _scan(self, root: Path) -> None:
        if not root.is_dir():
            return
        for p in sorted(root.glob("components/*/component.json")):
            doc = self._load(p)
            if isinstance(doc, dict) and isinstance(doc.get("id"), str):
                self.components.setdefault(doc["id"], p)
        for p in sorted(root.glob("boards/*/board.json")):
            doc = self._load(p)
            if isinstance(doc, dict) and isinstance(doc.get("id"), str):
                self.boards.setdefault(doc["id"], p)
        for p in sorted(root.glob("mcus/*/*/mcu.json")):
            doc = self._load(p)
            if isinstance(doc, dict) and isinstance(doc.get("id"), str):
                self.mcus.setdefault(doc["id"], p)
        for p in sorted(root.glob("toolchains/*.json")):
            doc = self._load(p)
            if isinstance(doc, dict) and isinstance(doc.get("id"), str):
                self.toolchains.setdefault(doc["id"], p)

    def _load(self, p: Path) -> Any:
        if p not in self._cache:
            try:
                with open(p, "r", encoding="utf-8") as fh:
                    self._cache[p] = json.load(fh)
            except (OSError, json.JSONDecodeError):
                self._cache[p] = None
        return self._cache[p]

    def component(self, cid: str) -> Optional[dict]:
        local = getattr(self, "project_components", {}).get(cid)
        if local is not None: return local
        p = self.components.get(cid)
        return self._load(p) if p else None

    def board(self, bid: str) -> Optional[dict]:
        p = self.boards.get(bid)
        return self._load(p) if p else None

    def mcu(self, mid: str) -> Optional[dict]:
        p = self.mcus.get(mid)
        return self._load(p) if p else None

    def mcu_path(self, mid: str) -> Optional[Path]:
        return self.mcus.get(mid)

    def with_extra(self, roots: Iterable[Path]) -> "Index":
        extra = [r for r in roots if r not in self.roots]
        if not extra:
            return self
        return Index(list(extra) + self.roots)


def has_output(index: "Index", desc: dict, name: str) -> bool:
    """NAME is one of the description's outputs, or MEMBER.NAME one of a
    composite member's (a board's LED)"""
    if name in {o.get("name") for o in desc.get("outputs") or []}:
        return True
    sub, _, out = name.partition(".")
    if not out:
        return False
    for m in ((desc.get("model") or {}).get("composite") or {}).get("components", []):
        if m.get("id") == sub:
            md = index.component(m.get("component", ""))
            return md is not None and out in {o.get("name") for o in md.get("outputs") or []}
    return False


def component_pin_names(desc: dict) -> set[str]:
    names: set[str] = set()
    for pin in desc.get("pins", []):
        if isinstance(pin, dict):
            if isinstance(pin.get("name"), str):
                names.add(pin["name"])
            for a in pin.get("aliases", []) or []:
                if isinstance(a, str):
                    names.add(a)
    return names


def mcu_signals(mcu: dict) -> set[str]:
    sigs = {s for s in mcu.get("signals", []) if isinstance(s, str)}
    for port in (mcu.get("io") or {}).get("ports", []) or []:
        if isinstance(port, dict) and isinstance(port.get("name"), str) and isinstance(port.get("pins"), int):
            for n in range(port["pins"]):
                sigs.add(f"P{port['name']}{n}")
    return sigs


# --------------------------------------------------------------------------
# semantic checks
# --------------------------------------------------------------------------


def check_param_value(report: Report, file: Path, node: str, name: str, value: Any, desc: dict) -> None:
    params = desc.get("params") or {}
    spec = params.get(name)
    if spec is None:
        report.error(file, node, "catalog.param_unknown", f"parameter {name} is not declared by {desc.get('id')}")
        return
    if isinstance(value, str) and value.startswith("$"):
        return
    t = spec.get("type")
    if t in ("number", "integer"):
        v = parse_si(value)
        if v is None:
            report.error(file, node, "schema.type", f"parameter {name}: {json.dumps(value)} is not a number")
            return
        lo, hi = parse_si(spec.get("min")) if "min" in spec else None, parse_si(spec.get("max")) if "max" in spec else None
        if lo is not None and v < lo:
            report.error(file, node, "analog.param_out_of_range", f"parameter {name}={v} is below min {lo}")
        if hi is not None and v > hi:
            report.error(file, node, "analog.param_out_of_range", f"parameter {name}={v} is above max {hi}")
    elif t == "boolean" and not isinstance(value, bool):
        report.error(file, node, "schema.type", f"parameter {name} must be a boolean")
    elif t == "enum":
        if value not in (spec.get("enum") or []):
            report.error(file, node, "schema.type", f"parameter {name}: {json.dumps(value)} is not one of {spec.get('enum')}")
    elif t == "string" and not isinstance(value, str):
        report.error(file, node, "schema.type", f"parameter {name} must be a string")
    elif t == "table" and not isinstance(value, list):
        report.error(file, node, "schema.type", f"parameter {name} must be a table of [x, y] pairs")


def check_version_request(report: Report, file: Path, node: str, entry: dict, description: dict) -> None:
    if "version" not in entry:
        return
    try:
        accepted = version_matches(description.get("version", ""), entry["version"])
    except ValueError as exc:
        report.error(file, node + "/version", "catalog.version.invalid", str(exc))
        return
    if not accepted:
        report.error(file, node + "/version", "catalog.version.mismatch",
                     f"requested {entry['version']}; selected {description.get('version')} of {description.get('id')}")


def check_library_dependencies(report: Report, file: Path, doc: dict, index: Index) -> None:
    manifests = []
    seen = set()
    for root in index.roots:
        path = (root / "library.json").resolve()
        if path in seen:
            continue
        seen.add(path)
        manifest = index._load(path) if path.is_file() else None
        if isinstance(manifest, dict):
            manifests.append((path, manifest))
    for i, dep in enumerate(doc.get("depends") or []):
        if not isinstance(dep, dict):
            continue
        targets = [(p, m) for p, m in manifests if m.get("name") == dep.get("name")]
        node = ptr("depends", i)
        if not targets:
            report.error(file, node, "library.dependency.missing", f"library {dep.get('name')} not loaded")
        elif len(targets) > 1:
            report.error(file, node, "library.dependency.ambiguous", f"library {dep.get('name')} has multiple roots")
        elif "version" in dep:
            try:
                accepted = version_matches(targets[0][1].get("version", ""), dep["version"])
            except ValueError as exc:
                report.error(file, node + "/version", "library.dependency.invalid", str(exc))
            else:
                if not accepted:
                    report.error(file, node + "/version", "library.dependency.mismatch", f"requested {dep['version']}; loaded {targets[0][1].get('version')}")


def check_instances_params(report: Report, file: Path, node_base: str, instances: list, index: Index, template_params: Optional[dict] = None) -> dict[str, dict]:
    """Resolve component instances; returns {instance id: description}."""
    found: dict[str, dict] = {}
    seen: set[str] = set()
    for i, inst in enumerate(instances or []):
        node = ptr(node_base.strip("/"), i) if node_base.strip("/") else ptr(i)
        iid = inst.get("id")
        if iid in seen:
            report.error(file, node + "/id", "catalog.duplicate_instance", f"instance {iid} declared twice")
        seen.add(iid)
        if inst.get("todo"):
            report.error(file, node + "/todo", "catalog.todo", f"instance {iid} is an unfinished importer placeholder")
        desc = index.component(inst.get("component", ""))
        if desc is None:
            report.error(file, node + "/component", "catalog.not_found",
                         f"component {inst.get('component')} not found (looked in {', '.join(str(r) for r in index.roots)})")
            continue
        if inst.get("populated") is False and inst.get("component", "").startswith("block/"):
            report.error(file, node + "/populated", "assembly.population", "block wrappers are not physical parts; select their members")
        check_version_request(report, file, node, inst, desc)
        found[iid] = desc
        for pname, pval in (inst.get("params") or {}).items():
            if isinstance(pval, str) and pval.startswith("$") and template_params is not None:
                if pname not in (desc.get("params") or {}):
                    report.error(file, node + "/params/" + pname, "schema.unknown_field", f"unknown component parameter {pname}")
                if pval[1:] not in template_params:
                    report.error(file, node + "/params/" + pname, "catalog.param_unknown", f"unknown block parameter {pval}")
            else:
                check_param_value(report, file, node + "/params/" + pname, pname, pval, desc)
        for pname, pspec in (desc.get("params") or {}).items():
            if pspec.get("required", True) and "default" not in pspec and pname not in (inst.get("params") or {}):
                report.error(file, node + "/params", "schema.missing_field", f"parameter {pname} of {desc.get('id')} has no default and is not given")
    return found


def check_nets(report: Report, file: Path, nets: dict, resolve_pin: Callable[[str, str], tuple[bool, str]]) -> None:
    """Every pin reference resolves and appears in at most one net."""
    seen: dict[str, str] = {}
    for net, pins in (nets or {}).items():
        for i, ref in enumerate(pins or []):
            node = ptr("nets", net, i)
            if not isinstance(ref, str) or "." not in ref:
                report.error(file, node, "schema.type", f"pin reference {ref!r} is not <instance>.<pin>")
                continue
            inst, pin = ref.split(".", 1)
            ok, why = resolve_pin(inst, pin)
            if not ok:
                report.error(file, node, "catalog.pin_unknown", why)
                continue
            if ref in seen:
                report.error(file, node, "catalog.pin_twice", f"{ref} is already in net {seen[ref]}")
            seen[ref] = net


def check_fidelity(report: Report, file: Path, doc: dict, base_dir: Path) -> set[str]:
    fid = doc.get("fidelity") or {}
    tiers = {k for k, v in fid.items() if v}
    if not tiers:
        report.error(file, "/fidelity", "fidelity.missing", "no fidelity tier is declared (all null or absent)")
    for tier, decl in fid.items():
        if not decl:
            continue
        if not str(decl.get("claims", "")).strip():
            report.error(file, ptr("fidelity", tier, "claims"), "fidelity.missing", "claims must not be empty")
        for i, ref in enumerate(decl.get("validation") or []):
            if isinstance(ref, str) and not re.match(r"^[a-z]+:", ref) and not (base_dir / ref).exists() and not sorted(base_dir.glob(ref)):
                report.error(file, ptr("fidelity", tier, "validation", i), "catalog.file_missing", f"validation reference {ref} matches no file")
    return tiers


def check_provenance(report: Report, file: Path, doc: dict) -> None:
    prov = doc.get("provenance") or {}
    lic = prov.get("license")
    if isinstance(lic, str) and not is_spdx(lic):
        report.error(file, "/provenance/license", "provenance.license", f"{lic!r} is not a known SPDX identifier")


def check_component(report: Report, file: Path, doc: dict, index: Index) -> None:
    from catalog.indicator_evidence import check as check_indicator_evidence
    for message in check_indicator_evidence(doc, file.parent):
        report.error(file, "/qualification", "catalog.qualification", message)
    base = file.parent
    # identity: directory and library
    lib_dir = base.parent.parent
    expected_name = base.name
    cid = doc.get("id", "")
    if "/" in cid and cid.split("/", 1)[1] != expected_name and base.parent.name == "components":
        report.error(file, "/id", "catalog.id_mismatch", f"id {cid} does not match directory name {expected_name}")
    lib_manifest = lib_dir / "library.json"
    if lib_manifest.exists():
        try:
            with open(lib_manifest, "r", encoding="utf-8") as fh:
                lib = json.load(fh)
            if isinstance(lib, dict) and "/" in cid and cid.split("/", 1)[0] != lib.get("name"):
                report.error(file, "/id", "catalog.id_mismatch", f"id {cid} is not in library {lib.get('name')}")
        except (OSError, json.JSONDecodeError):
            pass
    # pins
    names: dict[str, int] = {}
    numbers: dict[int, str] = {}
    pin_names: set[str] = set()
    power_pins: set[str] = set()
    all_names: set[str] = set()
    for i, pin in enumerate(doc.get("pins", [])):
        n = pin.get("name")
        if doc.get("part") and "number" not in pin:
            report.error(file, ptr("pins", i), "catalog.pin_number_missing", "a manufacturer part needs a physical pin number for every pin")
        if n in all_names:
            report.error(file, ptr("pins", i, "name"), "catalog.pin_duplicate", f"pin name {n} is not unique")
        all_names.add(n)
        pin_names.add(n)
        names[n] = i
        for j, a in enumerate(pin.get("aliases", []) or []):
            if a in all_names:
                report.error(file, ptr("pins", i, "aliases", j), "catalog.pin_duplicate", f"alias {a} clashes with another pin or alias")
            all_names.add(a)
        if "number" in pin:
            if pin["number"] in numbers:
                report.error(file, ptr("pins", i, "number"), "catalog.pin_duplicate", f"pin number {pin['number']} is already used by {numbers[pin['number']]}")
            numbers[pin["number"]] = n
        model = pin.get("model") or {}
        if model.get("kind") == "power" and model.get("role") == "supply":
            power_pins.add(n)
        for key, val in _walk_values(model):
            if isinstance(val, str) and val.startswith("$"):
                if val[1:] not in (doc.get("params") or {}):
                    report.error(file, ptr("pins", i, "model", key), "catalog.param_unknown", f"{val} references an undeclared parameter")
    for i, pin in enumerate(doc.get("pins", [])):
        pr = pin.get("power_ref")
        if pr is not None:
            if pr not in pin_names:
                report.error(file, ptr("pins", i, "power_ref"), "catalog.pin_unknown", f"power_ref {pr} is not a pin of this component")
            elif pr not in power_pins:
                report.warning(file, ptr("pins", i, "power_ref"), "catalog.power_ref", f"power_ref {pr} is not a power supply pin")
        gr = pin.get("ground_ref")
        if gr is not None and gr not in pin_names:
            report.error(file, ptr("pins", i, "ground_ref"), "catalog.pin_unknown", f"ground_ref {gr} is not a pin of this component")
    # params
    params = doc.get("params") or {}
    for pname, spec in params.items():
        node = ptr("params", pname)
        t = spec.get("type")
        if t == "enum":
            if not spec.get("enum"):
                report.error(file, node, "schema.missing_field", "enum parameter needs an enum list")
            elif "default" in spec and spec["default"] not in spec["enum"]:
                report.error(file, node + "/default", "schema.type", f"default {spec['default']!r} is not in enum")
            for pv, overrides in (spec.get("presets") or {}).items():
                if pv not in (spec.get("enum") or []):
                    report.error(file, node + "/presets/" + pv, "schema.type", f"preset {pv} is not an enum value")
                for oname, oval in (overrides or {}).items():
                    if oname not in params:
                        report.error(file, ptr("params", pname, "presets", pv, oname), "catalog.param_unknown", f"preset sets undeclared parameter {oname}")
                    elif oname != pname:
                        check_param_value(report, file, ptr("params", pname, "presets", pv, oname), oname, oval, doc)
        elif "enum" in spec:
            report.error(file, node + "/enum", "schema.type", "enum list on a non-enum parameter")
        if t in ("number", "integer"):
            lo = parse_si(spec["min"]) if "min" in spec else None
            hi = parse_si(spec["max"]) if "max" in spec else None
            if "min" in spec and lo is None:
                report.error(file, node + "/min", "schema.type", f"min {spec['min']!r} is not a number")
            if "max" in spec and hi is None:
                report.error(file, node + "/max", "schema.type", f"max {spec['max']!r} is not a number")
            if lo is not None and hi is not None and lo > hi:
                report.error(file, node, "schema.type", f"min {lo} exceeds max {hi}")
            if "default" in spec:
                d = parse_si(spec["default"])
                if d is None:
                    report.error(file, node + "/default", "schema.type", f"default {spec['default']!r} is not a number")
                else:
                    if lo is not None and d < lo:
                        report.error(file, node + "/default", "analog.param_out_of_range", f"default {d} is below min {lo}")
                    if hi is not None and d > hi:
                        report.error(file, node + "/default", "analog.param_out_of_range", f"default {d} is above max {hi}")
        elif t == "boolean" and "default" in spec and not isinstance(spec["default"], bool):
            report.error(file, node + "/default", "schema.type", "default of a boolean parameter must be true or false")
        elif t == "string" and "default" in spec and not isinstance(spec["default"], str):
            report.error(file, node + "/default", "schema.type", "default of a string parameter must be a string")
        elif t == "enum" and "default" in spec and not isinstance(spec["default"], str):
            report.error(file, node + "/default", "schema.type", "default of an enum parameter must be a string")
    inputs = {i.get("name"): i for i in doc.get("inputs") or []}
    outputs = {o.get("name"): o for o in doc.get("outputs") or []}
    for coll, label in ((doc.get("inputs") or [], "inputs"), (doc.get("outputs") or [], "outputs")):
        seen_names: set[str] = set()
        for i, item in enumerate(coll):
            if item.get("name") in seen_names:
                report.error(file, ptr(label, i, "name"), "catalog.duplicate", f"{label[:-1]} {item.get('name')} declared twice")
            seen_names.add(item.get("name"))
            if item.get("name") in params:
                report.error(file, ptr(label, i, "name"), "catalog.duplicate", f"{item.get('name')} is both a parameter and an {label[:-1]}")
    # models
    for key in ("model", "functional"):
        m = doc.get(key)
        if not m:
            continue
        if "primitive" in m:
            prim = m["primitive"]
            for mp, cp in (m.get("bind") or {}).items():
                if cp not in params and cp not in inputs:
                    report.error(file, ptr(key, "bind", mp), "catalog.param_unknown", f"bind target {cp} is neither a parameter nor an input")
            for pp, pv in (m.get("params") or {}).items():
                if isinstance(pv, str) and pv.startswith("$") and pv[1:] not in params:
                    report.error(file, ptr(key, "params", pp), "catalog.param_unknown", f"{pv} references an undeclared parameter")
            expected_pins = PRIMITIVE_PINS.get(prim)
            if expected_pins and not set(expected_pins) <= all_names:
                missing = sorted(set(expected_pins) - all_names)
                report.warning(file, ptr(key, "primitive"), "catalog.primitive_pins",
                               f"primitive {prim} expects pins {expected_pins}; missing {missing} (names or aliases)")
            needed_inputs = PRIMITIVE_INPUTS.get(prim)
            if needed_inputs:
                bound = set((m.get("bind") or {}).values()) | set(inputs)
                if not needed_inputs & bound:
                    report.warning(file, ptr(key, "primitive"), "catalog.primitive_inputs",
                                   f"primitive {prim} reads one of inputs {sorted(needed_inputs)}; none is declared or bound")
        elif "script" in m:
            script = base / m["script"]
            if not script.exists():
                report.error(file, ptr(key, "script"), "catalog.file_missing", f"{m['script']} does not exist")
            else:
                # one model for several parts (a controller behind different
                # glass): "-- Shared by: a, b" names the component directories
                # whose scripts must be the same file
                text = script.read_bytes()
                shared = re.search(rb"^-- Shared by: ([\w, -]+?)\r?$", text, re.M)  # CRLF in a Windows checkout
                for other in (shared.group(1).decode().split(",") if shared else []):
                    twin = base.parent / other.strip() / m["script"]
                    if not twin.is_file() or twin.read_bytes() != text:
                        report.error(file, ptr(key, "script"), "catalog.shared_model_differs",
                                     f"{m['script']} is shared with {other.strip()}, whose copy differs or is missing")
        elif "composite" in m:
            comp = m["composite"]
            inner = check_instances_params(report, file, ptr(key, "composite", "components"), comp.get("components", []), index)
            # internal $param references
            for i, inst in enumerate(comp.get("components", [])):
                for pname, pval in (inst.get("params") or {}).items():
                    if isinstance(pval, str) and pval.startswith("$") and pval[1:] not in params:
                        report.error(file, ptr(key, "composite", "components", i, "params", pname), "catalog.param_unknown", f"{pval} references an undeclared parameter")

            def resolve(inst: str, pin: str, _inner=inner) -> tuple[bool, str]:
                d = _inner.get(inst)
                if d is None:
                    return False, f"instance {inst} is not declared in the composite"
                if pin not in component_pin_names(d):
                    return False, f"{inst} ({d.get('id')}) has no pin {pin}"
                return True, ""

            saved = list(report.findings)
            check_nets(report, file, comp.get("nets", {}), resolve)
            # re-root the net node paths under the model key
            for f_ in report.findings[len(saved):]:
                f_.node = ptr(key, "composite") + f_.node
            for port, ref in (comp.get("ports") or {}).items():
                if port not in all_names:
                    report.error(file, ptr(key, "composite", "ports", port), "catalog.pin_unknown", f"port {port} is not a pin of this component")
                inst, _, pin = ref.partition(".")
                ok, why = resolve(inst, pin)
                if not ok:
                    report.error(file, ptr(key, "composite", "ports", port), "catalog.pin_unknown", why)
    # symbol
    sym = doc.get("symbol") or {}
    svg = base / sym.get("svg", "symbol.svg")
    symbol_ids: Optional[set[str]] = None
    if not svg.exists():
        report.error(file, "/symbol/svg", "catalog.file_missing", f"{sym.get('svg')} does not exist")
    else:
        symbol_ids = svg_ids(svg, report)
    for pname, pos in (sym.get("pins") or {}).items():
        if pname not in pin_names:
            report.error(file, ptr("symbol", "pins", pname), "catalog.pin_unknown", f"symbol pin {pname} is not a pin of this component")
            continue
        x, y, side = pos.get("x", 0), pos.get("y", 0), pos.get("side")
        w, h = sym.get("width", 0), sym.get("height", 0)
        if x > w or y > h:
            report.error(file, ptr("symbol", "pins", pname), "symbol.pin_outside", f"pin at ({x},{y}) is outside the {w}x{h} symbol")
        edge = {"left": x == 0, "right": x == w, "top": y == 0, "bottom": y == h}.get(side, True)
        if not edge:
            report.warning(file, ptr("symbol", "pins", pname), "symbol.pin_edge", f"pin on side {side} is not on that edge")
    bench = doc.get("bench") or {}
    # a wiring aid (a breadboard) is not drawn in the schematic: its pins need no symbol position
    if not bench.get("wiring_aid"):
        for n in pin_names:
            if n not in (sym.get("pins") or {}):
                report.error(file, "/symbol/pins", "symbol.pin_missing", f"pin {n} has no symbol position")
    # ties: the part's own pins, each in one tie at most
    tied: dict[str, int] = {}
    for i, tie in enumerate(doc.get("ties") or []):
        for name in tie:
            if name not in pin_names:
                report.error(file, ptr("ties", i), "catalog.pin_unknown", f"tie pin {name} is not a pin of this component")
            elif name in tied:
                report.error(file, ptr("ties", i), "catalog.pin_twice", f"pin {name} is in ties {tied[name]} and {i}")
            else:
                tied[name] = i
    for name in bench.get("pins") or {}:
        if name not in pin_names:
            report.error(file, ptr("bench", "pins", name), "catalog.pin_unknown", f"bench pin {name} is not a pin of this component")
    size = bench.get("size_mm")
    if size:
        for name, (x, y) in (bench.get("pins") or {}).items():
            if not (0 <= x <= size[0] and 0 <= y <= size[1]):
                report.error(file, ptr("bench", "pins", name), "bench.pin_outside", f"pin at ({x}, {y}) mm is outside the {size[0]} x {size[1]} mm part")
    # schematic state/interaction bindings use the same declaration contract
    # as artwork, with selectors resolved against symbol.svg.
    for i, hs in enumerate(sym.get("hotspots") or []):
        if hs.get("input") not in inputs:
            report.error(file, ptr("symbol", "hotspots", i, "input"), "catalog.input_unknown", f"hotspot input {hs.get('input')} is not declared")
        if hs.get("gesture") == "drag" and not hs.get("axis"):
            report.error(file, ptr("symbol", "hotspots", i), "schema.missing_field", "drag hotspot needs an axis")
        _check_selector(report, file, ptr("symbol", "hotspots", i, "selector"), hs.get("selector"), symbol_ids)
    for i, st in enumerate(sym.get("states") or []):
        if st.get("output") not in outputs and not ("." in (st.get("output") or "") and has_output(index, doc, st["output"])):
            report.error(file, ptr("symbol", "states", i, "output"), "catalog.output_unknown", f"state output {st.get('output')} is not declared")
        if not (st.get("attr") or st.get("class")):
            report.error(file, ptr("symbol", "states", i), "schema.missing_field", "state binds neither an attr nor a class")
        if st.get("attr") and st['attr'] not in {"text", "fill", "stroke", "opacity", "fill-opacity", "stroke-opacity", "stroke-width", "transform", "d", "x", "y", "width", "height", "visibility", "display", "r", "cx", "cy", "points", "textLength"}:
            report.error(file, ptr("symbol", "states", i, "attr"), "symbol.state_attribute", "state attribute is not a supported presentation attribute")
        _check_selector(report, file, ptr("symbol", "states", i, "selector"), st.get("selector"), symbol_ids)

    # artwork
    art = doc.get("artwork")
    if art:
        asvg = base / art["svg"]
        ids: Optional[set[str]] = None
        if not asvg.exists():
            report.error(file, "/artwork/svg", "catalog.file_missing", f"{art['svg']} does not exist")
        else:
            ids = svg_ids(asvg, report)
        for i, hs in enumerate(art.get("hotspots") or []):
            if hs.get("input") not in inputs:
                report.error(file, ptr("artwork", "hotspots", i, "input"), "catalog.input_unknown", f"hotspot input {hs.get('input')} is not declared")
            if hs.get("gesture") == "drag" and not hs.get("axis"):
                report.error(file, ptr("artwork", "hotspots", i), "schema.missing_field", "drag hotspot needs an axis")
            _check_selector(report, file, ptr("artwork", "hotspots", i, "selector"), hs.get("selector"), ids)
        for i, st in enumerate(art.get("states") or []):
            if st.get("output") not in outputs and not ("." in (st.get("output") or "") and has_output(index, doc, st["output"])):
                report.error(file, ptr("artwork", "states", i, "output"), "catalog.output_unknown", f"state output {st.get('output')} is not declared")
            if not (st.get("attr") or st.get("class")):
                report.error(file, ptr("artwork", "states", i), "schema.missing_field", "state binds neither an attr nor a class")
            _check_selector(report, file, ptr("artwork", "states", i, "selector"), st.get("selector"), ids)
    # fidelity, provenance, tests
    tiers = check_fidelity(report, file, doc, base)
    if doc.get("model") is None and doc.get("functional") is not None and tiers - {"functional"}:
        report.warning(file, "/fidelity", "fidelity.tier_unbacked", "timed/electrical fidelity declared but only a functional model is bound")
    if doc.get("functional") is None and doc.get("model") is not None and "functional" in tiers and "primitive" in doc["model"]:
        pass  # a model usable in every tier
    check_provenance(report, file, doc)
    tests = doc.get("tests")
    if tests is None:
        report.warning(file, "/", "catalog.no_tests", "no tests declared (CAT-006 requires at least one fixture)")
    else:
        matched = 0
        for i, pattern in enumerate(tests):
            hits = sorted(base.glob(pattern))
            if not hits:
                report.error(file, ptr("tests", i), "catalog.file_missing", f"{pattern} matches no file")
            matched += len(hits)
        if tests and matched == 0:
            report.error(file, "/tests", "catalog.no_tests", "no fixture file found")


def _check_selector(report: Report, file: Path, node: str, selector: Any, ids: Optional[set[str]]) -> None:
    if ids is None or not isinstance(selector, str):
        return
    m = re.match(r"^#([A-Za-z_][\w-]*)$", selector)
    if m and m.group(1) not in ids:
        report.error(file, node, "artwork.selector", f"selector {selector} matches no element id in the artwork")


def _walk_values(obj: Any, prefix: str = "") -> Iterable[tuple[str, Any]]:
    if isinstance(obj, dict):
        for k, v in obj.items():
            yield from _walk_values(v, f"{prefix}/{k}" if prefix else str(k))
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            yield from _walk_values(v, f"{prefix}/{i}")
    else:
        yield prefix, obj


def check_component_test(report: Report, file: Path, doc: dict, index: Index) -> None:
    base = file.parent
    circuit = doc.get("circuit") or {}
    inst = check_instances_params(report, file, "/circuit/components", circuit.get("components", []), index)
    boards: dict[str, dict] = {}
    for i, b in enumerate(circuit.get("boards") or []):
        bd = index.board(b.get("board", ""))
        if bd is None:
            report.error(file, ptr("circuit", "boards", i, "board"), "catalog.not_found", f"board {b.get('board')} not found")
        elif b.get("id") in inst or b.get("id") in boards:
            report.error(file, ptr("circuit", "boards", i, "id"), "catalog.duplicate_instance", f"instance {b.get('id')} declared twice")
        else:
            boards[b["id"]] = bd
    # the fixture must exercise the component or board it sits beside
    for own_name, key in (("component.json", "component"), ("board.json", "board")):
        own = base.parent / own_name
        if base.name == "tests" and own.exists():
            try:
                with open(own, "r", encoding="utf-8") as fh:
                    own_id = json.load(fh).get("id")
            except (OSError, json.JSONDecodeError):
                own_id = None
            used = {c.get(key) for c in circuit.get(key + "s", []) or []}
            if own_id and own_id not in used:
                report.error(file, "/circuit", "catalog.test_subject", f"fixture does not instantiate {own_id}")

    def resolve(i: str, pin: str) -> tuple[bool, str]:
        if i in boards:
            if pin not in (boards[i].get("ports") or {}):
                return False, f"board {i} ({boards[i].get('id')}) has no port {pin}"
            return True, ""
        d = inst.get(i)
        if d is None:
            return False, f"instance {i} is not declared in the circuit"
        if pin not in component_pin_names(d):
            return False, f"{i} ({d.get('id')}) has no pin {pin}"
        return True, ""

    saved = len(report.findings)
    check_nets(report, file, (doc.get("circuit") or {}).get("nets", {}), resolve)
    for f_ in report.findings[saved:]:
        f_.node = "/circuit" + f_.node
    until = parse_time_ps(doc.get("until")) if doc.get("until") else None
    def member(target: str) -> Optional[dict]:
        """COMPONENT.MEMBER: a member of a composite instance (a keypad's key),
        or BOARD.COMPONENT: a part on a board (a jumper, a button)"""
        top, _, sub = (target or "").partition(".")
        if sub and top in boards:
            for c in boards[top].get("components") or []:
                if c.get("id") == sub:
                    return index.component(c.get("component", ""))
            return None
        parent = inst.get(top)
        if not sub or parent is None:
            return None
        comp = ((parent.get("model") or {}).get("composite") or {})
        for m in comp.get("components", []):
            if m.get("id") == sub:
                return index.component(m.get("component", ""))
        return None

    for i, inp in enumerate(doc.get("inputs") or []):
        d = inst.get(inp.get("target")) or member(inp.get("target"))
        node = ptr("inputs", i)
        if d is None:
            report.error(file, node + "/target", "catalog.not_found", f"input target {inp.get('target')} is not an instance of the circuit")
        elif inp.get("input") not in {x.get("name") for x in d.get("inputs") or []}:
            report.error(file, node + "/input", "catalog.input_unknown", f"{inp.get('target')} ({d.get('id')}) has no input {inp.get('input')}")
        t = parse_time_ps(inp.get("at"))
        if t is None:
            report.error(file, node + "/at", "schema.type", f"time {inp.get('at')!r} is invalid")
        elif until is not None and t > until:
            report.warning(file, node + "/at", "scenario.after_until", f"input at {inp.get('at')} is after until")
    nets = (doc.get("circuit") or {}).get("nets", {})
    for target, exp in (doc.get("expect") or {}).items():
        node = ptr("expect", target)
        kind, _, rest = target.partition(":")
        if kind == "net" and rest not in nets:
            report.error(file, node, "catalog.not_found", f"net {rest} is not declared")
        elif kind in ("pin", "device", "output"):
            i, _, sub = rest.partition(".")
            if i in boards:
                if kind == "pin" and sub not in (boards[i].get("ports") or {}):
                    report.error(file, node, "catalog.pin_unknown", f"board {i} has no port {sub}")
            elif i not in inst:
                report.error(file, node, "catalog.not_found", f"instance {i} is not declared")
            elif kind == "pin" and sub not in component_pin_names(inst[i]):
                report.error(file, node, "catalog.pin_unknown", f"{i} has no pin {sub}")
            elif kind == "output" and not has_output(index, inst[i], sub):
                report.error(file, node, "catalog.output_unknown", f"{i} has no output {sub}")
        if not any(k in exp for k in ("at", "between", "by")):
            report.error(file, node, "schema.missing_field", "expectation needs at, between or by")
        if not any(k in exp for k in ("logic", "min", "max", "min_v", "max_v", "equals", "contains", "edges", "frequency_hz")):
            report.error(file, node, "schema.missing_field", "expectation has no condition")
        for tk in ("at", "by"):
            if tk in exp and parse_time_ps(exp[tk]) is None:
                report.error(file, node + "/" + tk, "schema.type", f"time {exp[tk]!r} is invalid")
        if "between" in exp:
            a, b = (parse_time_ps(x) for x in exp["between"])
            if a is None or b is None or a > b:
                report.error(file, node + "/between", "schema.type", "window must be two valid times in order")


def check_mcu(report: Report, file: Path, doc: dict, index: Index) -> None:
    base = file.parent
    sigs = set(doc.get("signals", []))
    gpio = mcu_signals(doc) - sigs
    # memory
    names: set[str] = set()
    for i, region in enumerate(doc.get("memory", [])):
        node = ptr("memory", i)
        if region.get("name") in names:
            report.error(file, node + "/name", "catalog.duplicate", f"memory region {region.get('name')} declared twice")
        names.add(region.get("name"))
        for key in ("base", "size", "erased", "target"):
            if key in region and parse_size(region[key]) is None:
                report.error(file, node + "/" + key, "schema.type", f"{key} {region[key]!r} is not an address or size")
        if region.get("kind") == "alias" and "target" not in region:
            report.error(file, node, "schema.missing_field", "alias region needs a target")
        if region.get("kind") == "flash" and "erased" not in region:
            report.warning(file, node, "schema.missing_field", "flash region without an erased value")
    mains = [r for r in doc.get("memory", []) if r.get("main")]
    if len(mains) > 1:
        report.error(file, "/memory", "catalog.duplicate", "more than one memory region is marked main")
    # clocks
    clocks = doc.get("clocks") or {}
    cnames = [c.get("name") for c in clocks.get("sources", [])]
    if len(cnames) != len(set(cnames)):
        report.error(file, "/clocks/sources", "catalog.duplicate", "clock source names are not unique")
    if clocks.get("max_core_hz") and clocks.get("reset_core_hz", 0) > clocks["max_core_hz"]:
        report.error(file, "/clocks/reset_core_hz", "schema.type", "reset_core_hz exceeds max_core_hz")
    # register file
    rf = doc.get("register_file") or {}
    if rf.get("format") == "embsim":
        rpath = base / rf.get("file", "")
        if not rf.get("vendored"):
            report.error(file, "/register_file/vendored", "schema.type", "an EmbSIM register description is part of the catalog: vendored is true")
        if not rpath.exists():
            report.error(file, "/register_file/file", "catalog.file_missing", f"{rf.get('file')} does not exist")
        else:
            rdoc = load_json(rpath, report)
            names = {p.get("name", "") for p in (rdoc.get("peripherals") or [])} if isinstance(rdoc, dict) else set()
            for i, pb in enumerate(doc.get("peripherals") or []):
                pat = pb.get("match", "")
                if not any(fnmatch.fnmatchcase(n, pat) for n in names):
                    report.warning(file, ptr("peripherals", i, "match"), "catalog.register_file_absent", f"{pat} matches no peripheral of {rf.get('file')}")
    elif rf.get("vendored"):
        if not (base / rf.get("file", "")).exists():
            report.error(file, "/register_file/file", "catalog.file_missing", f"register file {rf.get('file')} is declared vendored but absent")
        if not rf.get("license"):
            report.error(file, "/register_file/license", "provenance.license", "vendored register file needs a licence")
        if not (base / "LICENSE").exists() and not any(base.glob("LICENSE*")):
            report.error(file, "/register_file", "provenance.license", "vendored material needs a LICENSE file beside the description (ADR-017)")
        if not rf.get("hash"):
            report.warning(file, "/register_file", "provenance.hash", "vendored register file without a hash (CAT-003)")
    else:
        if not rf.get("lookup"):
            report.error(file, "/register_file/lookup", "schema.missing_field", "a non-vendored register file needs a lookup list")
        if (doc.get("peripherals")):
            report.warning(file, "/peripherals", "catalog.register_file_absent", "peripheral globs cannot be checked without the register file")
    # io
    io = doc.get("io") or {}
    pnames = [p.get("name") for p in io.get("ports", [])]
    if len(pnames) != len(set(pnames)):
        report.error(file, "/io/ports", "catalog.duplicate", "port names are not unique")
    for i, s in enumerate((io.get("electrical") or {}).get("five_volt_tolerant", [])):
        if s not in sigs | gpio:
            report.error(file, ptr("io", "electrical", "five_volt_tolerant", i), "catalog.signal_unknown", f"{s} is not a signal of this MCU")
    for s in (io.get("analog") or {}):
        if s not in sigs | gpio:
            report.error(file, ptr("io", "analog", s), "catalog.signal_unknown", f"{s} is not a signal of this MCU")
    # packages
    for pname, pfile in (doc.get("packages") or {}).items():
        ppath = base / pfile
        if not ppath.exists():
            report.error(file, ptr("packages", pname), "catalog.file_missing", f"{pfile} does not exist")
            continue
        pdoc = load_json(ppath, report)
        if isinstance(pdoc, dict):
            check_package(report, ppath, pdoc, index, mcu=doc)
    console = (doc.get("console") or {}).get("default")
    if console and doc.get("peripherals"):
        if not any(fnmatch.fnmatch(console, p.get("match", "")) for p in doc["peripherals"]):
            report.warning(file, "/console/default", "catalog.peripheral_unbound", f"{console} matches no peripheral binding")
    check_fidelity(report, file, doc, base)
    check_provenance(report, file, doc)


def check_package(report: Report, file: Path, doc: dict, index: Index, mcu: Optional[dict] = None) -> None:
    base = file.parent
    numbers: set[int] = set()
    if mcu is None:
        mpath = base / "mcu.json"
        if mpath.exists():
            try:
                with open(mpath, "r", encoding="utf-8") as fh:
                    mcu = json.load(fh)
            except (OSError, json.JSONDecodeError):
                mcu = None
    valid = mcu_signals(mcu) if mcu else None
    for i, pin in enumerate(doc.get("pins", [])):
        n = pin.get("number")
        if n in numbers:
            report.error(file, ptr("pins", i, "number"), "catalog.pin_duplicate", f"pin number {n} is not unique")
        numbers.add(n)
        s = pin.get("signal")
        if not s:
            report.error(file, ptr("pins", i), "schema.missing_field", "package pin without a signal")
        elif valid is not None and s != "NC" and s not in valid:
            report.error(file, ptr("pins", i, "signal"), "catalog.signal_unknown", f"{s} is not a signal of the MCU")
    if numbers and sorted(numbers) != list(range(1, max(numbers) + 1)):
        report.warning(file, "/pins", "catalog.pin_gap", "pin numbers are not contiguous from 1")
    if mcu is not None and isinstance(doc.get("id"), str) and not doc["id"].startswith(str(mcu.get("id")) + "/"):
        report.error(file, "/id", "catalog.id_mismatch", f"package id {doc['id']} is not under {mcu.get('id')}")
    outline = doc.get("outline")
    if outline and not (base / outline["svg"]).exists():
        report.error(file, "/outline/svg", "catalog.file_missing", f"{outline['svg']} does not exist")
    elif outline:
        svg_ids(base / outline["svg"], report)




def _check_reference_home(report: Report, path: Path, doc: dict) -> None:
    """A board reference sits beside the board it qualifies (board.json checks
    the artwork against it)."""
    board = path.parent / "board.json"
    if not board.exists():
        report.error(path, "/board", "board.reference_orphan", "no board.json beside this reference")
        return
    with open(board, "r", encoding="utf-8") as fh:
        bid = json.load(fh).get("id")
    if bid != doc.get("board"):
        report.error(path, "/board", "board.reference_orphan", f"this reference is for {doc.get('board')}, the board beside it is {bid}")

def check_board_reference(report: Report, file: Path, doc: dict) -> None:
    """A board qualified against reference data (reference.json beside its
    description): its artwork's outline, every terminal's pad, the mounting
    holes and its bench features' connector bodies where the independent
    reference puts them, within its tolerance; every port has a reference
    terminal (no geometry left to a fallback)."""
    ref_path = file.parent / "reference.json"
    art = doc.get("artwork")
    if not ref_path.exists() or not art:
        return
    with open(ref_path, "r", encoding="utf-8") as fh:
        ref = json.load(fh)
    tol = float(ref.get("tolerance_mm", 0.05))
    node = "/artwork"
    w, h = ref["outline_mm"]
    if abs(art.get("width_mm", 0) - w) > tol or abs(art.get("height_mm", 0) - h) > tol:
        report.error(file, node, "board.reference_outline",
                     f"artwork is {art.get('width_mm')} x {art.get('height_mm')} mm, the reference {w} x {h} mm")
    try:
        root = ET.parse(file.parent / art["svg"]).getroot()
    except (ET.ParseError, OSError):
        return  # reported by the artwork check
    vb = [float(v) for v in (root.get("viewBox") or "0 0 1 1").split()]
    sx, sy = art.get("width_mm", w) / vb[2], art.get("height_mm", h) / vb[3]
    # a pad is a circle (a through-hole's ring) or a rect (an oblong pad), at its centre
    pads = {el.get("id"): el for el in root.iter()
            if (el.tag.endswith("circle") or el.tag.endswith("rect")) and el.get("id")}
    selector = {hs["port"]: hs.get("selector", "") for hs in art.get("hotspots") or [] if "port" in hs}

    def at(el: Any) -> tuple[float, float]:
        if el.tag.endswith("rect"):
            cx = float(el.get("x", 0)) + float(el.get("width", 0)) / 2
            cy = float(el.get("y", 0)) + float(el.get("height", 0)) / 2
        else:
            cx, cy = float(el.get("cx")), float(el.get("cy"))
        return (cx - vb[0]) * sx, (cy - vb[1]) * sy

    for name, (x, y) in ref.get("terminals", {}).items():
        sel = selector.get(name, "#pin-" + name.lower())
        el = pads.get(sel.lstrip("#"))
        if el is None:
            report.error(file, node, "board.reference_terminal", f"the artwork has no pad {sel} for {name}")
            continue
        ax, ay = at(el)
        if abs(ax - x) > tol or abs(ay - y) > tol:
            report.error(file, node, "board.reference_terminal",
                         f"{name}'s pad is at ({ax:.2f}, {ay:.2f}) mm, the reference puts it at ({x}, {y})")
    for port in doc.get("ports") or {}:
        if port not in ref.get("terminals", {}):
            report.error(file, "/ports/" + port, "board.reference_terminal", f"port {port} has no reference position")
    mounts = [at(el) for el in root.iter() if el.tag.endswith("circle") and "mount" in (el.get("class") or "").split()]
    for x, y in (ref.get("mounting_holes") or {}).get("at", []):
        if not any(abs(mx - x) <= tol and abs(my - y) <= tol for mx, my in mounts):
            report.error(file, node, "board.reference_hole", f"no mounting hole (class mount) at ({x}, {y}) mm")
    feats = {f.get("kind"): f for f in (doc.get("bench") or {}).get("features") or []}
    for kind, box in (ref.get("connectors") or {}).items():
        f = feats.get(kind)
        if f is None:
            report.error(file, "/bench/features", "board.reference_connector", f"no bench feature {kind}")
            continue
        cx, cy = (box["from"][0] + box["to"][0]) / 2, (box["from"][1] + box["to"][1]) / 2
        bw, bh = box["to"][0] - box["from"][0], box["to"][1] - box["from"][1]
        fx, fy = f.get("at", [0, 0])
        size = f.get("size", [0, 0])
        if abs(fx - cx) > tol or abs(fy - cy) > tol or abs(size[0] - bw) > tol or abs(size[1] - bh) > tol:
            report.error(file, "/bench/features", "board.reference_connector",
                         f"{kind} is {size[0]} x {size[1]} mm at ({fx}, {fy}); the reference {bw:.2f} x {bh:.2f} at ({cx:.2f}, {cy:.2f})")

def check_board(report: Report, file: Path, doc: dict, index: Index) -> None:
    base = file.parent
    mref = doc.get("mcu") or {}
    mcu = index.mcu(mref.get("ref", ""))
    package: Optional[dict] = None
    signals: set[str] = set()
    if mcu is None:
        report.error(file, "/mcu/ref", "catalog.not_found", f"MCU {mref.get('ref')} not found")
    else:
        check_version_request(report, file, "/mcu", mref, mcu)
        signals = mcu_signals(mcu)
        pfile = (mcu.get("packages") or {}).get(mref.get("package"))
        if not pfile:
            report.error(file, "/mcu/package", "catalog.not_found", f"MCU {mref.get('ref')} has no package {mref.get('package')}")
        else:
            mpath = index.mcu_path(mref["ref"])
            ppath = mpath.parent / pfile if mpath else None
            if ppath and ppath.exists():
                try:
                    with open(ppath, "r", encoding="utf-8") as fh:
                        package = json.load(fh)
                except (OSError, json.JSONDecodeError):
                    package = None
        for src in (mcu.get("clocks") or {}).get("sources", []):
            if src.get("hz") == "board" and src.get("name") not in (doc.get("oscillators") or {}):
                report.error(file, "/oscillators", "board.oscillator_missing", f"MCU clock source {src.get('name')} is \"board\" but no oscillator supplies it")
    mcu_inst = mref.get("instance")
    package_signals = {p.get("signal") for p in (package or {}).get("pins", [])} if package else None
    inst = check_instances_params(report, file, "/components", doc.get("components", []), index)
    if mcu_inst in inst:
        report.error(file, "/mcu/instance", "catalog.duplicate_instance", f"MCU instance {mcu_inst} clashes with a component instance")

    def resolve(i: str, pin: str) -> tuple[bool, str]:
        if i == mcu_inst:
            if mcu is None:
                return True, ""
            if pin not in signals:
                return False, f"{pin} is not a signal of {mref.get('ref')}"
            if package_signals is not None and pin not in package_signals:
                return False, f"signal {pin} is not bonded out on package {mref.get('package')}"
            return True, ""
        d = inst.get(i)
        if d is None:
            return False, f"instance {i} is not declared"
        if pin not in component_pin_names(d):
            return False, f"{i} ({d.get('id')}) has no pin {pin}"
        return True, ""

    nets = doc.get("nets") or {}
    check_nets(report, file, nets, resolve)
    net_of: dict[str, str] = {}
    for net, pins in nets.items():
        for ref in pins:
            net_of[ref] = net
    # MCU power signals must sit on a net
    if mcu is not None:
        for s in sorted(signals):
            if POWER_SIGNAL_RE.match(s) and (package_signals is None or s in package_signals) and f"{mcu_inst}.{s}" not in net_of:
                report.error(file, "/nets", "board.unpowered_mcu", f"MCU power signal {mcu_inst}.{s} is on no net")
    # ports
    for label, port in (doc.get("ports") or {}).items():
        node = ptr("ports", label)
        if port.get("net") not in nets:
            report.error(file, node + "/net", "catalog.not_found", f"port {label}: net {port.get('net')} does not exist")
        conn = port.get("connector")
        if conn is not None:
            d = inst.get(conn)
            if d is None:
                report.error(file, node + "/connector", "catalog.not_found", f"connector {conn} is not a component instance")
            elif "pin" in port:
                if str(port["pin"]) not in component_pin_names(d):
                    report.error(file, node + "/pin", "catalog.pin_unknown", f"connector {conn} has no pin {port['pin']}")
                elif net_of.get(f"{conn}.{port['pin']}") != port.get("net"):
                    report.error(file, node, "board.port_disagrees", f"{conn}.{port['pin']} is not on net {port.get('net')}")
    # leds / buttons agree with nets
    adjacency = _two_pin_adjacency(nets, inst)
    for i, led in enumerate(doc.get("leds") or []):
        node = ptr("leds", i)
        d = inst.get(led.get("component"))
        if d is None:
            report.error(file, node + "/component", "catalog.not_found", f"LED component {led.get('component')} is not declared")
            continue
        if "luminance" not in {o.get("name") for o in d.get("outputs") or []} and "lit" not in {o.get("name") for o in d.get("outputs") or []}:
            report.warning(file, node + "/component", "board.led_output", f"{led.get('component')} ({d.get('id')}) has no luminance or lit output")
        if led.get("gpio"):
            if led["gpio"] not in signals and mcu is not None:
                report.error(file, node + "/gpio", "catalog.signal_unknown", f"{led['gpio']} is not a signal of the MCU")
            else:
                start = net_of.get(f"{mcu_inst}.{led['gpio']}")
                if start is None:
                    report.error(file, node + "/gpio", "board.led_disagrees", f"{mcu_inst}.{led['gpio']} is on no net")
                elif not _reaches(start, led["component"], nets, adjacency):
                    report.error(file, node, "board.led_disagrees", f"LED {led.get('component')} is not reachable from {led['gpio']} through two-pin components")
    for i, btn in enumerate(doc.get("buttons") or []):
        node = ptr("buttons", i)
        d = inst.get(btn.get("component"))
        if d is None:
            report.error(file, node + "/component", "catalog.not_found", f"button component {btn.get('component')} is not declared")
            continue
        sig = btn.get("signal") or btn.get("gpio")
        if sig:
            if mcu is not None and sig not in signals:
                report.error(file, node, "catalog.signal_unknown", f"{sig} is not a signal of the MCU")
            else:
                start = net_of.get(f"{mcu_inst}.{sig}")
                if start is None or not _reaches(start, btn["component"], nets, adjacency):
                    report.error(file, node, "board.button_disagrees", f"button {btn.get('component')} is not on the net of {sig}")
    console = doc.get("console")
    if console and mcu is not None:
        for key in ("tx", "rx"):
            if console.get(key) and console[key] not in signals:
                report.error(file, ptr("console", key), "catalog.signal_unknown", f"{console[key]} is not a signal of the MCU")
    check_board_reference(report, file, doc)
    # the bridge's DTR reset holds a board component's contacts (a reset switch's) for its pulse
    dtr = ((console or {}).get("bridge") or {}).get("dtr_reset")
    if dtr:
        d = inst.get(dtr.get("component"))
        if d is None:
            report.error(file, "/console/bridge/dtr_reset/component", "catalog.not_found",
                         f"dtr_reset component {dtr.get('component')} is not a component of the board")
        elif "pressed" not in {x.get("name") for x in d.get("inputs") or []}:
            report.error(file, "/console/bridge/dtr_reset/component", "catalog.input_unknown",
                         f"{dtr.get('component')} has no input pressed to hold RESET low")
    # artwork
    art = doc.get("artwork")
    if art:
        asvg = base / art["svg"]
        ids: Optional[set[str]] = None
        if not asvg.exists():
            report.error(file, "/artwork/svg", "catalog.file_missing", f"{art['svg']} does not exist")
        else:
            ids = svg_ids(asvg, report)
        for i, hs in enumerate(art.get("hotspots") or []):
            node = ptr("artwork", "hotspots", i)
            if "component" in hs:
                d = inst.get(hs["component"])
                if d is None:
                    report.error(file, node + "/component", "catalog.not_found", f"hotspot component {hs['component']} is not declared")
                else:
                    if hs.get("input") and hs["input"] not in {x.get("name") for x in d.get("inputs") or []}:
                        report.error(file, node + "/input", "catalog.input_unknown", f"{hs['component']} has no input {hs['input']}")
                    if hs.get("output") and hs["output"] not in {x.get("name") for x in d.get("outputs") or []}:
                        report.error(file, node + "/output", "catalog.output_unknown", f"{hs['component']} has no output {hs['output']}")
            if "port" in hs and hs["port"] not in (doc.get("ports") or {}):
                report.error(file, node + "/port", "catalog.not_found", f"hotspot port {hs['port']} is not declared")
            _check_selector(report, file, node + "/selector", hs.get("selector"), ids)
    # the bench's features show and drive the board's own components (SPEC-006 §4)
    for i, feat in enumerate((doc.get("bench") or {}).get("features") or []):
        node = ptr("bench", "features", i)
        for key, kind in (("input", "inputs"), ("output", "outputs")):
            ref = feat.get(key)
            if not ref:
                continue
            comp, _, name = ref.partition(".")
            d = inst.get(comp)
            if d is None:
                report.error(file, node + "/" + key, "catalog.not_found", f"bench feature {key} {comp} is not a component of the board")
            elif name not in {x.get("name") for x in d.get(kind) or []}:
                report.error(file, node + "/" + key, f"catalog.{key}_unknown", f"{comp} has no {key} {name}")
    for prof in (doc.get("toolchains") or {}):
        if index.toolchains and prof not in index.toolchains:
            report.warning(file, ptr("toolchains", prof), "catalog.not_found", f"no toolchain profile {prof} in the catalog")
    check_fidelity(report, file, doc, base)
    check_provenance(report, file, doc)
    tests = doc.get("tests")
    if tests is not None:
        for i, pattern in enumerate(tests):
            if not sorted(base.glob(pattern)):
                report.error(file, ptr("tests", i), "catalog.file_missing", f"{pattern} matches no file")


def _two_pin_adjacency(nets: dict, inst: dict[str, dict]) -> dict[str, set[str]]:
    """Nets joined through two-pin components (resistors, switches, jumpers)."""
    net_of: dict[str, str] = {}
    for net, pins in nets.items():
        for ref in pins:
            net_of[ref] = net
    adj: dict[str, set[str]] = {n: set() for n in nets}
    for iid, d in inst.items():
        pins = [p.get("name") for p in d.get("pins", [])]
        if len(pins) != 2:
            continue
        a, b = net_of.get(f"{iid}.{pins[0]}"), net_of.get(f"{iid}.{pins[1]}")
        if a and b and a != b:
            adj[a].add(b)
            adj[b].add(a)
    return adj


def _reaches(start: str, instance: str, nets: dict, adj: dict[str, set[str]]) -> bool:
    seen = {start}
    stack = [start]
    while stack:
        n = stack.pop()
        if any(ref.split(".", 1)[0] == instance for ref in nets.get(n, [])):
            return True
        for m in adj.get(n, ()):
            if m not in seen:
                seen.add(m)
                stack.append(m)
    return False


@dataclass
class ProjectContext:
    components: dict[str, dict] = field(default_factory=dict)
    boards: dict[str, dict] = field(default_factory=dict)
    nets: set[str] = field(default_factory=set)
    probes: dict[str, dict] = field(default_factory=dict)
    policy: str = "functional"


def project_index(file: Path, doc: dict, index: Index) -> Index:
    roots = []
    for lib in doc.get("libraries") or []:
        roots.append((file.parent / lib).resolve())
    result = copy.copy(index.with_extra(roots))
    result.project_components = {}
    for key, block in (doc.get("blocks") or {}).items():
        result.project_components["block/" + key] = dict(id="block/" + key, name=block.get("name", key), params=block.get("params", {}), pins=[dict(name=p, model={"kind": "passive"}) for p in block.get("ports", {})], model={"composite": block}, fidelity={tier: {"claims": "project-owned composition", "omissions": []} for tier in ["functional", "timed", "electrical"]})
    return result


def assembly_target(project: dict, path: str) -> Optional[dict]:
    sheet = project
    parts = path.split(".")
    if not path or len(path) > 149 or len(parts) > 9:
        return None
    for i, key in enumerate(parts):
        component = next((c for c in sheet.get("components", []) if c.get("id") == key), None)
        if not component:
            return None
        ref = component.get("component", "")
        if i == len(parts) - 1:
            return component if not ref.startswith("block/") else None
        if not ref.startswith("block/"):
            return None
        sheet = (project.get("blocks") or {}).get(ref[6:], {})
    return None


def check_assembly_variants(report: Report, file: Path, doc: dict, index: Index) -> None:
    variants = doc.get("variants")
    if isinstance(variants, dict):
        for name, definition in variants.items():
            if not isinstance(definition, dict) or not isinstance(definition.get("overrides"), dict):
                continue
            for path, change in definition["overrides"].items():
                if assembly_target(doc, path) is None:
                    report.error(file, f"/variants/{name}/overrides/{path}", "assembly.variant.target", "expected a concrete project-owned physical component path, not a board or block wrapper")
                if isinstance(change, dict) and isinstance(change.get("version"), str):
                    try: version_matches("0.0.0", change["version"])
                    except ValueError as exc:
                        report.error(file, f"/variants/{name}/overrides/{path}/version", "assembly.variant.range", str(exc))
    origin = doc.get("variant_origin")
    if isinstance(origin, dict):
        text = origin.get("json")
        if not isinstance(text, str):
            return
        if variants is not None:
            report.error(file, "/variants", "assembly.variant.origin", "derived circuits retain definitions in the embedded base, not a second mutable definition set")
        if len(text.encode("utf-8")) > 8388608 or origin.get("sha256") != "sha256:" + hashlib.sha256(text.encode("utf-8")).hexdigest():
            report.error(file, "/variant_origin", "assembly.variant.hash", "retained base identity does not match its UTF-8 bytes or exceeds 8 MiB")
        def reject_constant(value: str) -> None:
            raise ValueError(f"non-finite JSON constant {value}")
        try:
            base = json.loads(text, parse_constant=reject_constant)
        except (TypeError, ValueError):
            report.error(file, "/variant_origin/json", "assembly.variant.origin", "retained base is not valid JSON")
            return
        if not isinstance(base, dict) or base.get("kind") != "project" or "variant_origin" in base:
            report.error(file, "/variant_origin/json", "assembly.variant.origin", "expected one original project, without recursive origins")
            return
        schemas = Schemas()
        errors = schemas.validate(base, schemas.for_kind("project"))
        for node, message in errors:
            report.error(file, "/variant_origin/json" + node, "assembly.variant.origin", message)
        if not errors:
            check_assembly_variants(report, file, base, index)
            if origin.get("name") not in (base.get("variants") or {}):
                report.error(file, "/variant_origin/name", "assembly.variant.origin", "variant does not exist in retained base")


def check_project(report: Report, file: Path, doc: dict, index: Index) -> ProjectContext:
    check_assembly_variants(report, file, doc, index)
    if isinstance(doc.get("library_lock"), dict):
        check_library_lock(report, file, doc["library_lock"], index)
    base = file.parent
    idx = project_index(file, doc, index)
    for root in idx.roots:
        manifest_path = root / "library.json"
        manifest = idx._load(manifest_path) if manifest_path.is_file() else None
        if isinstance(manifest, dict):
            check_library_dependencies(report, manifest_path, manifest, idx)
    for i, lib in enumerate(doc.get("libraries") or []):
        if not (base / lib).is_dir():
            report.error(file, ptr("libraries", i), "catalog.not_found", f"library directory {lib} does not exist")
    blocks = doc.get("blocks") or {}
    def visit(key, stack, budget=None):
        if budget is None: budget = [10000]
        if key in stack or len(stack) >= 8:
            report.error(file, ptr("blocks", key), "hierarchy.invalid", "recursive block or nesting exceeds eight levels")
            return
        if key not in blocks:
            report.error(file, ptr("blocks", key), "catalog.not_found", "unknown circuit block")
            return
        for part in blocks[key].get("components", []):
            budget[0] -= 1
            if budget[0] < 0:
                report.error(file, ptr("blocks", key), "hierarchy.invalid", "block expansion exceeds 10000 components")
                break
            ref = part.get("component", "")
            if ref.startswith("block/"): visit(ref[6:], stack + [key], budget)
    for key, block in blocks.items():
        visit(key, [])
        order = (block.get("layout") or {}).get("port_order", [])
        if any(name not in block.get("ports", {}) for name in order) or len(set(order)) != len(order):
            report.error(file, ptr("blocks", key, "layout", "port_order"), "hierarchy.invalid", "port order needs unique declared port names")
        for net in block.get("nets", {}):
            if "." in net or len(net) > 63:
                report.error(file, ptr("blocks", key, "nets", net), "hierarchy.invalid", "local net names must have no dots and at most 63 characters")
        found = check_instances_params(report, file, ptr("blocks", key, "components"), block.get("components", []), idx, block.get("params", {}))
        for iid, description in found.items():
            if doc.get("policy", "functional") not in {tier for tier, claim in (description.get("fidelity") or {}).items() if claim}:
                report.error(file, ptr("blocks", key), "fidelity.tier_missing", f"{iid} declares no {doc.get('policy', 'functional')} fidelity")
        def resolve_block(i, pin):
            return (True, "") if i in found and pin in component_pin_names(found[i]) else (False, f"unknown local pin {i}.{pin}")
        check_nets(report, file, block.get("nets", {}), resolve_block)
        for port, ref in block.get("ports", {}).items():
            iid, _, pin = ref.partition(".")
            ok, why = resolve_block(iid, pin)
            if not ok: report.error(file, ptr("blocks", key, "ports", port), "catalog.pin_unknown", why)
            if not any(ref in pins for pins in block.get("nets", {}).values()):
                report.error(file, ptr("blocks", key, "ports", port), "hierarchy.invalid", "port pin must belong to a named local net")
    ctx = ProjectContext(policy=doc.get("policy", "functional"))
    ctx.components = check_instances_params(report, file, "/components", doc.get("components", []), idx)
    for i, b in enumerate(doc.get("boards") or []):
        node = ptr("boards", i)
        if b.get("id") in ctx.components or b.get("id") in ctx.boards:
            report.error(file, node + "/id", "catalog.duplicate_instance", f"instance {b.get('id')} declared twice")
        bd = idx.board(b.get("board", ""))
        if bd is None:
            report.error(file, node + "/board", "catalog.not_found", f"board {b.get('board')} not found")
            continue
        check_version_request(report, file, node, b, bd)
        ctx.boards[b["id"]] = bd
        fw = b.get("firmware") or {}
        if fw.get("image") and not (base / fw["image"]).exists():
            report.warning(file, node + "/firmware/image", "project.image_missing", f"firmware image {fw['image']} does not exist (build it first)")
        if fw.get("toolchain") and idx.toolchains and fw["toolchain"] not in idx.toolchains:
            report.error(file, node + "/firmware/toolchain", "catalog.not_found", f"toolchain profile {fw['toolchain']} is not in the catalog")
        for j, src in enumerate(fw.get("sources") or []):
            if not (base / src).exists():
                report.warning(file, node + f"/firmware/sources/{j}", "project.source_missing", f"source {src} does not exist")
        if ctx.policy not in {k for k, v in (bd.get("fidelity") or {}).items() if v}:
            report.error(file, node + "/board", "fidelity.tier_missing", f"board {b.get('board')} declares no {ctx.policy} fidelity")
    for iid, d in ctx.components.items():
        tiers = {k for k, v in (d.get("fidelity") or {}).items() if v}
        if ctx.policy not in tiers:
            report.error(file, "/policy", "fidelity.tier_missing", f"{iid} ({d.get('id')}) declares no {ctx.policy} fidelity")
        if ctx.policy == "functional" and d.get("functional") is None and d.get("model") is None:
            report.error(file, "/policy", "island.requires_electrical", f"{iid} ({d.get('id')}) has no functional model")

    def resolve(i: str, pin: str) -> tuple[bool, str]:
        if i in ctx.boards:
            if pin not in (ctx.boards[i].get("ports") or {}):
                return False, f"board {i} ({ctx.boards[i].get('id')}) has no port {pin}"
            return True, ""
        d = ctx.components.get(i)
        if d is None:
            return False, f"instance {i} is not declared"
        if pin not in component_pin_names(d):
            return False, f"{i} ({d.get('id')}) has no pin {pin}"
        return True, ""

    nets = doc.get("nets") or {}
    ctx.nets = set(nets)
    check_nets(report, file, nets, resolve)
    # Presentation groups refer only to top-level instances, once each.
    group_ids, grouped = set(), set()
    instance_ids = {part.get("id") for part in (doc.get("components") or []) + (doc.get("boards") or [])}
    for i, group in enumerate((doc.get("layout") or {}).get("groups") or []):
        node = ptr("layout", "groups", i)
        if group.get("id") in group_ids:
            report.error(file, node + "/id", "catalog.duplicate", "duplicate schematic group id")
        group_ids.add(group.get("id"))
        for member in group.get("members") or []:
            if member not in instance_ids:
                report.error(file, node + "/members", "catalog.not_found", f"group member {member} is not declared")
            if member in grouped:
                report.error(file, node + "/members", "catalog.duplicate", f"group member {member} appears more than once")
            grouped.add(member)
    # layout references
    for i, w in enumerate((doc.get("layout") or {}).get("wires") or []):
        if w.get("net") not in nets:
            report.error(file, ptr("layout", "wires", i, "net"), "catalog.not_found", f"wire for undeclared net {w.get('net')}")
    # the bench (SPEC-007 §2): places of declared instances, jumpers between real terminals in one net
    bench = (doc.get("layout") or {}).get("bench") or {}
    net_of = {pin: name for name, pins in nets.items() for pin in pins}
    for iid in bench.get("parts") or {}:
        if iid not in ctx.components and iid not in ctx.boards:
            report.error(file, ptr("layout", "bench", "parts", iid), "catalog.not_found", f"bench place for undeclared instance {iid}")
    jumper_pairs, occupied_holes = set(), set()
    for i, w in enumerate(bench.get("wires") or []):
        node = ptr("layout", "bench", "wires", i)
        for end in ("from", "to"):
            inst_id, _, pin = (w.get(end) or "").partition(".")
            ok, why = resolve(inst_id, pin)
            if not ok:
                report.error(file, f"{node}/{end}", "catalog.pin_unknown", why)
        a, b = net_of.get(w.get("from")), net_of.get(w.get("to"))
        if a is None or a != b:
            report.error(file, node, "bench.jumper_net", f"jumper {w.get('from')} to {w.get('to')}: its ends are not in one net")
        ends = (w.get("from", ""), w.get("to", ""))
        pair = tuple(sorted(ends))
        if ends[0] == ends[1] or pair in jumper_pairs:
            report.error(file, node, "bench.duplicate", "jumper needs distinct terminals and must not duplicate another jumper")
        jumper_pairs.add(pair)
        for ref in ends:
            iid = ref.partition(".")[0]
            description = ctx.components.get(iid) or {}
            if (description.get("bench") or {}).get("package") == "breadboard":
                if ref in occupied_holes:
                    report.error(file, node, "bench.occupied", f"hole {ref} already holds a jumper; choose another hole on its strip")
                occupied_holes.add(ref)
    hierarchy_nets = set()
    def scoped_nets(sheet, path, depth=0, budget=None):
        if budget is None: budget = [10000]
        budget[0] -= len(sheet.get("components", [])) + 1
        if depth > 8 or len(path) >= 150 or budget[0] < 0:
            report.error(file, "/blocks", "hierarchy.invalid", "hierarchy exceeds expansion or path limits")
            return
        hierarchy_nets.update(path + name for name in sheet.get("nets", {}))
        for part in sheet.get("components", []):
            ref = part.get("component", "")
            if ref.startswith("block/") and ref[6:] in blocks:
                if budget[0] < 0: break
                scoped_nets(blocks[ref[6:]], path + part["id"] + ".", depth + 1, budget)
    if blocks:
        for part in doc.get("components", []):
            if part.get("component", "").startswith("block/") and any(net.startswith(part.get("id", "") + ".") for net in nets):
                report.error(file, "/nets", "hierarchy.invalid", "root net overlaps a block instance namespace")
    scoped_nets(doc, "")
    # probes
    for i, probe in enumerate(doc.get("probes") or []):
        node = ptr("probes", i)
        pid = probe.get("id")
        if pid in ctx.probes:
            report.error(file, node + "/id", "catalog.duplicate", f"probe {pid} declared twice")
        ctx.probes[pid] = probe
        kind, _, rest = probe.get("target", "").partition(":")
        if kind == "net":
            b2, _, bnet = rest.partition(".")
            board_net = b2 in ctx.boards and bnet in (ctx.boards[b2].get("nets") or {})
            if rest not in hierarchy_nets and not board_net:
                report.error(file, node + "/target", "catalog.not_found", f"net {rest} is not declared (a board's own net is BOARD.NET)")
        elif kind in ("pin", "device", "output"):
            i2, _, sub = rest.partition(".")
            if i2 in ctx.boards:
                if kind == "pin" and sub not in (ctx.boards[i2].get("ports") or {}):
                    report.error(file, node + "/target", "catalog.pin_unknown", f"board {i2} has no port {sub}")
            elif i2 not in ctx.components:
                report.error(file, node + "/target", "catalog.not_found", f"instance {i2} is not declared")
            elif kind == "pin" and sub not in component_pin_names(ctx.components[i2]):
                report.error(file, node + "/target", "catalog.pin_unknown", f"{i2} has no pin {sub}")
            elif kind == "output" and not has_output(index, ctx.components[i2], sub):
                report.error(file, node + "/target", "catalog.output_unknown", f"{i2} has no output {sub}")
        elif kind in ("reg", "serial", "mem", "var"):
            sep = "." if kind in ("reg", "serial") else ":"
            b = rest.split(sep, 1)[0]
            # a serial probe also reads a component with a serial output (a virtual terminal)
            if b not in ctx.boards and not (kind == "serial" and rest in ctx.components):
                report.error(file, node + "/target", "catalog.not_found", f"board {b} is not declared")
        if kind == "serial" and probe.get("kind") != "serial":
            report.warning(file, node + "/kind", "schema.type", "a serial target is normally probed with kind serial")
    # scenarios
    for i, s in enumerate(doc.get("scenarios") or []):
        if not (base / s).exists():
            report.error(file, ptr("scenarios", i), "catalog.file_missing", f"scenario {s} does not exist")
    settings = doc.get("settings") or {}
    for key in ("h_min", "h_max"):
        if key in settings and parse_time_ps(settings[key]) is None:
            report.error(file, ptr("settings", key), "schema.type", f"{settings[key]!r} is not a valid time")
    return ctx


def find_project_for_scenario(file: Path, report: Report, index: Index) -> Optional[ProjectContext]:
    """Locate the project.json that lists this scenario (same dir or up to two levels up)."""
    for d in (file.parent, file.parent.parent, file.parent.parent.parent):
        for cand in sorted(d.glob("*.json")):
            try:
                with open(cand, "r", encoding="utf-8") as fh:
                    doc = json.load(fh)
            except (OSError, json.JSONDecodeError):
                continue
            if isinstance(doc, dict) and doc.get("kind") == "project":
                listed = {(cand.parent / s).resolve() for s in doc.get("scenarios") or [] if isinstance(s, str)}
                if file.resolve() in listed or cand.parent == file.parent:
                    quiet = Report()
                    return check_project(quiet, cand, doc, index)
    return None


WORLD_FIELDS = ("temperature", "humidity", "pressure", "illuminance", "sound", "wind")


def check_scenario(report: Report, file: Path, doc: dict, index: Index, ctx: Optional[ProjectContext] = None) -> None:
    if ctx is None:
        ctx = find_project_for_scenario(file, report, index)
    until = parse_time_ps(doc.get("until"))
    if until is None:
        report.error(file, "/until", "schema.type", f"until {doc.get('until')!r} is not a valid time")
    for i, inp in enumerate(doc.get("inputs") or []):
        node = ptr("inputs", i)
        t = parse_time_ps(inp.get("at"))
        if t is None:
            report.error(file, node + "/at", "schema.type", f"time {inp.get('at')!r} is invalid")
        elif until is not None and t > until:
            report.warning(file, node + "/at", "scenario.after_until", f"input at {inp.get('at')} is after until {doc.get('until')}")
        if ctx is None:
            continue
        target = inp.get("target", "")
        inst, _, periph = target.partition(".")
        if inst == "world":
            # the room (ADR-026): a field and a number (or null), or a body and its spec (or null)
            value = inp.get("value")
            if not periph:
                if inp.get("input") not in WORLD_FIELDS:
                    report.error(file, node + "/input", "catalog.input_unknown", f"the world has no field {inp.get('input')} (it has {', '.join(WORLD_FIELDS)})")
                elif value is not None and (isinstance(value, bool) or not isinstance(value, (int, float))):
                    report.error(file, node + "/value", "schema.type", f"a world field takes a number, not {value!r}")
            elif inp.get("input") != "body":
                report.error(file, node + "/input", "catalog.input_unknown", f"a world body's input is body, not {inp.get('input')}")
            elif value is not None and not (isinstance(value, dict) and isinstance(value.get("at"), list) and isinstance(value.get("size"), list)):
                report.error(file, node + "/value", "schema.type", "a world body is {shape?, at: [x, y, z], size: [w, d, h]} or null")
        elif inst in ctx.components:
            if periph:
                report.error(file, node + "/target", "catalog.not_found", f"{inst} is a component; only boards have peripherals")
            elif inp.get("input") not in {x.get("name") for x in ctx.components[inst].get("inputs") or []}:
                report.error(file, node + "/input", "catalog.input_unknown", f"{inst} ({ctx.components[inst].get('id')}) has no input {inp.get('input')}")
            else:
                spec = next(x for x in ctx.components[inst]["inputs"] if x.get("name") == inp.get("input"))
                _check_input_value(report, file, node + "/value", spec, inp.get("value"))
        elif inst in ctx.boards:
            parts = {c.get("id"): c for c in ctx.boards[inst].get("components") or []}
            if periph in parts:
                # one of the board's own components (its push button)
                cd = index.component(parts[periph].get("component", ""))
                specs = {x.get("name"): x for x in (cd or {}).get("inputs") or []}
                if inp.get("input") not in specs:
                    report.error(file, node + "/input", "catalog.input_unknown",
                                 f"{target} ({parts[periph].get('component')}) has no input {inp.get('input')}")
                else:
                    _check_input_value(report, file, node + "/value", specs[inp.get("input")], inp.get("value"))
            elif periph:
                if inp.get("input") not in ("rx", "voltage"):
                    report.error(file, node + "/input", "catalog.input_unknown", f"peripheral input must be rx or voltage, not {inp.get('input')}")
            elif inp.get("input") not in ("reset", "power", "serial"):
                # serial: text into the board's console (SPEC-008)
                report.error(file, node + "/input", "catalog.input_unknown", f"board input must be reset, power or serial, not {inp.get('input')}")
        else:
            report.error(file, node + "/target", "catalog.not_found", f"target {inst} is not a component or board of the project")
    ids: set[str] = set()
    for i, a in enumerate(doc.get("assertions") or []):
        node = ptr("assertions", i)
        if a.get("id") in ids:
            report.error(file, node + "/id", "catalog.duplicate", f"assertion id {a.get('id')} is not unique")
        ids.add(a.get("id"))
        for tk in ("at", "by"):
            if tk in a:
                t = parse_time_ps(a[tk])
                if t is None:
                    report.error(file, node + "/" + tk, "schema.type", f"time {a[tk]!r} is invalid")
                elif until is not None and t > until:
                    report.warning(file, node + "/" + tk, "scenario.after_until", f"assertion at {a[tk]} is after until")
        if "between" in a:
            lo, hi = (parse_time_ps(x) for x in a["between"])
            if lo is None or hi is None or lo > hi:
                report.error(file, node + "/between", "schema.type", "window must be two valid times in order")
            elif until is not None and hi > until:
                report.warning(file, node + "/between", "scenario.after_until", "window ends after until")
        if "probe" in a and ctx is not None and a["probe"] not in ctx.probes:
            report.error(file, node + "/probe", "catalog.not_found", f"probe {a['probe']} is not declared by the project")
        raw_exp = a.get("expect")
        exp = raw_exp if isinstance(raw_exp, dict) else {}
        if "measure" in a:
            if a["measure"] not in ("mean", "rms", "duty_cycle") or "between" not in a:
                report.error(file, node + "/measure", "schema.type", "measure requires mean/rms/duty_cycle and a between window")
            if not exp or set(exp) - {"min", "max", "equals", "tolerance"} or not set(exp) & {"min", "max", "equals"} or any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) for v in exp.values()):
                report.error(file, node + "/expect", "schema.type", "measure requires numeric min/max/equals expectations")
            window = a.get("between", [])
            if len(window) == 2 and parse_time_ps(window[0]) == parse_time_ps(window[1]):
                report.error(file, node + "/between", "schema.type", "measurement window must have positive duration")
            if ctx is not None and a["measure"] == "duty_cycle" and ctx.probes.get(a.get("probe"), {}).get("kind") != "digital":
                report.error(file, node + "/measure", "schema.type", "duty cycle requires a digital probe")
        if "tolerance" in exp and "frequency_hz" not in exp and not isinstance(exp.get("equals"), (int, float)):
            report.warning(file, node + "/expect", "schema.type", "tolerance without frequency_hz or a number to equal")
        if "min" in exp and "max" in exp and exp["min"] > exp["max"]:
            report.error(file, node + "/expect", "schema.type", "min exceeds max")
        if "min_v" in exp and "max_v" in exp and exp["min_v"] > exp["max_v"]:
            report.error(file, node + "/expect", "schema.type", "min_v exceeds max_v")
        if ctx is not None and "probe" in a and a["probe"] in ctx.probes:
            pkind = ctx.probes[a["probe"]].get("kind")
            if "logic" in exp and pkind != "digital":
                report.error(file, node + "/expect/logic", "schema.type", f"logic expectation on a {pkind} probe")
            if pkind == "digital" and any(k in exp for k in ("min_v", "max_v", "contains")):
                report.error(file, node + "/expect", "schema.type", "voltage or text expectation on a digital probe")
            if "contains" in exp and pkind not in ("serial", "value"):
                report.error(file, node + "/expect/contains", "schema.type", f"contains expectation on a {pkind} probe")


def _check_input_value(report: Report, file: Path, node: str, spec: dict, value: Any) -> None:
    t = spec.get("type")
    if t == "boolean" and not isinstance(value, bool):
        report.error(file, node, "schema.type", f"input {spec.get('name')} takes a boolean")
    elif t in ("number", "integer"):
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            report.error(file, node, "schema.type", f"input {spec.get('name')} takes a number")
        else:
            if "min" in spec and value < spec["min"]:
                report.error(file, node, "analog.param_out_of_range", f"input {spec.get('name')}={value} is below min {spec['min']}")
            if "max" in spec and value > spec["max"]:
                report.error(file, node, "analog.param_out_of_range", f"input {spec.get('name')}={value} is above max {spec['max']}")
    elif t == "enum" and value not in (spec.get("enum") or []):
        report.error(file, node, "schema.type", f"input {spec.get('name')}: {value!r} is not one of {spec.get('enum')}")
    elif t in ("string", "bytes") and not isinstance(value, str):
        report.error(file, node, "schema.type", f"input {spec.get('name')} takes a string")


def check_recording(report: Report, file: Path, doc: dict, index: Index) -> None:
    last = (-1, -1)
    for i, inp in enumerate(doc.get("inputs") or []):
        key = (int(inp.get("at_ps", "0")), inp.get("seq", 0))
        if key < last:
            report.error(file, ptr("inputs", i), "recording.order", "inputs are not in (at_ps, seq) order")
        last = key
    last_t = -1
    for i, obs in enumerate(doc.get("observations") or []):
        t = int(obs.get("at_ps", "0"))
        if t < last_t:
            report.error(file, ptr("observations", i), "recording.order", "observations are not in time order")
        last_t = t
        if obs.get("kind") == "irq" and ("board" not in obs or "n" not in obs):
            report.error(file, ptr("observations", i), "schema.missing_field", "irq observation needs board and n")
        if obs.get("kind") == "probe" and "probe" not in obs:
            report.error(file, ptr("observations", i), "schema.missing_field", "probe observation needs probe")
    end = int((doc.get("end") or {}).get("at_ps", "0"))
    if last_t > end:
        report.error(file, "/end/at_ps", "recording.order", "an observation is after the end")


REG_ACCESS = {"rw", "r", "w", "w1", "rw1", "rc_w0", "rc_w1", "rc_r", "rs", "t"}


def check_registers(report: Report, file: Path, doc: dict, index: Index) -> None:
    periphs = doc.get("peripherals") or []
    by_name = {p.get("name"): p for p in periphs}
    irqs = doc.get("interrupts") or {}
    if len(by_name) != len(periphs):
        report.error(file, "/peripherals", "catalog.duplicate", "peripheral names are not unique")
    spans = []
    for i, p in enumerate(periphs):
        node = ptr("peripherals", i)
        src = p
        if "derived_from" in p:
            src = by_name.get(p["derived_from"])
            if not src or "derived_from" in src:
                report.error(file, node + "/derived_from", "catalog.ref_unknown", f"{p['derived_from']} is not a peripheral with registers")
                continue
        for k, irq in enumerate(p.get("interrupts") or []):
            if irq not in irqs:
                report.error(file, ptr("peripherals", i, "interrupts", k), "catalog.ref_unknown", f"interrupt {irq} is not in the interrupt table")
        base = int(p.get("base", "0x0"), 16)
        rnames = set()
        for j, r in enumerate(src.get("registers") or []):
            rnode = ptr("peripherals", periphs.index(src), "registers", j)
            if r.get("name") in rnames:
                report.error(file, rnode + "/name", "catalog.duplicate", f"{src.get('name')}.{r.get('name')} declared twice")
            rnames.add(r.get("name"))
            size = r.get("size", 32)
            off = int(r.get("offset", "0x0"), 16)
            spans.append((base + off, base + off + size // 8, f"{p.get('name')}.{r.get('name')}"))
            if int(r.get("reset", "0x0"), 16) >> size:
                report.error(file, rnode + "/reset", "schema.type", f"reset value wider than {size} bits")
            used = 0
            for k, f in enumerate(r.get("fields") or []):
                if len(f) < 3 or not isinstance(f[1], int) or not isinstance(f[2], int):
                    continue
                if f[1] + f[2] > size:
                    report.error(file, rnode + f"/fields/{k}", "schema.type", f"field {f[0]} runs past bit {size - 1}")
                mask = ((1 << f[2]) - 1) << f[1]
                if used & mask:
                    report.error(file, rnode + f"/fields/{k}", "catalog.duplicate", f"field {f[0]} overlaps another field")
                used |= mask
                if len(f) > 3 and f[3] not in REG_ACCESS:
                    report.error(file, rnode + f"/fields/{k}", "schema.type", f"unknown access {f[3]!r}")
    spans.sort()
    for a, b in zip(spans, spans[1:]):
        if b[0] < a[1]:
            report.error(file, "/peripherals", "catalog.duplicate", f"{b[2]} overlaps {a[2]}")


def check_toolchain(report: Report, file: Path, doc: dict, index: Index) -> None:
    if file.parent.name == "toolchains" and file.stem != doc.get("id"):
        report.error(file, "/id", "catalog.id_mismatch", f"id {doc.get('id')} does not match file name {file.stem}")
    placeholder = re.compile(r"\$\{([a-z_]+)\}")
    for step, argv in (doc.get("steps") or {}).items():
        for i, arg in enumerate(argv or []):
            for name in placeholder.findall(arg):
                if name not in TOOLCHAIN_PLACEHOLDERS:
                    report.warning(file, ptr("steps", step, i), "toolchain.placeholder", f"unknown placeholder ${{{name}}}")
    if (doc.get("outputs") or {}).get("image") in ("hex", "bin") and not (doc.get("steps") or {}).get("convert"):
        report.error(file, "/steps/convert", "schema.missing_field", "a hex or bin image needs a convert step")


def check_library(report: Report, file: Path, doc: dict, index: Index) -> None:
    check_library_dependencies(report, file, doc, index)
    base = file.parent
    lic = doc.get("license")
    if isinstance(lic, str) and not is_spdx(lic):
        report.error(file, "/license", "provenance.license", f"{lic!r} is not a known SPDX identifier")
    for p in sorted(base.glob("components/*/component.json")):
        try:
            with open(p, "r", encoding="utf-8") as fh:
                cid = json.load(fh).get("id", "")
        except (OSError, json.JSONDecodeError):
            continue
        if not cid.startswith(doc.get("name", "") + "/"):
            report.error(file, "/name", "catalog.id_mismatch", f"{p.relative_to(base)} has id {cid} outside library {doc.get('name')}")
    for sub in ("components", "boards", "mcus", "toolchains"):
        pass


def check_library_lock(report: Report, file: Path, doc: dict, index: Index) -> None:
    libraries = doc.get("libraries", {})
    if not isinstance(libraries, dict):
        return
    total = 0
    for name, library in libraries.items():
        if not isinstance(library, dict) or not isinstance(library.get("files"), dict):
            continue
        files = library["files"]
        total += len(files)
        if "library.json" not in files:
            report.error(file, f"/libraries/{name}/files", "library.lock.manifest", "library.json must be pinned")
        for path in files:
            if "\\" in path or ":" in path or any(not part or part.startswith(".") for part in path.split("/")):
                report.error(file, f"/libraries/{name}/files", "library.lock.path", "paths must be non-hidden normalized relative paths")
    if total > 20000:
        report.error(file, "/libraries", "library.lock.limit", "lock exceeds 20,000 files")
    catalog = doc.get("catalog", {})
    if isinstance(catalog, dict):
        for key, item in catalog.items():
            if isinstance(item, dict) and item.get("library") not in libraries:
                report.error(file, f"/catalog/{key}/library", "library.lock.reference", "catalog selection refers to a library absent from the lock")


def check_engineering_manifest(report: Report, file: Path, doc: dict, index: Index,
                               schemas: Optional[Schemas] = None) -> None:
    """Verify exact exported bytes without requiring the source libraries locally."""
    schemas = schemas or Schemas()
    def reject_constant(value: str) -> None:
        raise ValueError(f"non-finite JSON constant {value}")

    def decode(entry: dict, node: str, verify: bool) -> Any:
        text = entry.get("json", "")
        try:
            encoded = text.encode("utf-8")
            value = json.loads(text, parse_constant=reject_constant)
        except (ValueError, TypeError, UnicodeError) as exc:
            report.error(file, node, "manifest.json", f"invalid embedded JSON/UTF-8: {exc}")
            return None
        if verify and entry.get("sha256") != "sha256:" + hashlib.sha256(encoded).hexdigest():
            report.error(file, node, "manifest.hash", "content does not match its exact UTF-8 SHA-256")
        return value
    payload = decode(doc, "/json", False)
    if payload is None:
        return
    schema = schemas.for_kind("engineering_manifest")
    inner = {"$id": schema["$id"], "$defs": schema["$defs"], "$ref": "#/$defs/payload"}
    violations = schemas.validate(payload, inner)
    for node, message in violations:
        report.error(file, "/json" + node, "manifest.schema", message)
    if violations:
        return
    native = payload["source"] == "native"
    if native:
        decode(doc, "/json", True)
    else:
        report.warning(file, "/json/source", "manifest.demo", "demo inventory is not native resolved evidence")
    project = decode(payload["project"], "/json/project", native)
    if not isinstance(project, dict) or project.get("kind") != "project":
        report.error(file, "/json/project", "manifest.project", "expected the canonical project document")
    if isinstance(project, dict) and project.get("kind") == "project":
        for node, message in schemas.validate(project, schemas.for_kind("project")):
            report.error(file, "/json/project" + node, "manifest.project", message)
    descriptions = {}
    for key, entry in payload["catalog"].items():
        description = decode(entry, "/json/catalog/" + key, native)
        if not isinstance(description, dict) or description.get("kind") not in ("component", "board", "mcu", "package"):
            report.error(file, "/json/catalog/" + key, "manifest.catalog", "expected a loaded component, board, MCU or package description")
        else:
            descriptions[key] = description
    model_paths = set()
    for i, model in enumerate(payload["models"]):
        if model["path"] in model_paths:
            report.error(file, f"/json/models/{i}/path", "manifest.duplicate", "model paths must be unique")
        model_paths.add(model["path"])
        if model["catalog"] and model["catalog"] not in descriptions:
            report.error(file, f"/json/models/{i}/catalog", "manifest.catalog", "model description is absent")
    paths = set()
    for i, item in enumerate(payload["instances"]):
        node = f"/json/instances/{i}"
        if item["path"] in paths:
            report.error(file, node + "/path", "manifest.duplicate", "instance paths must be unique")
        paths.add(item["path"])
        expected = (item["sheet"] + "." if item["sheet"] else "") + item["id"]
        if item["path"] != expected:
            report.error(file, node + "/path", "manifest.path", "qualified path does not match sheet and local identifier")
        description = descriptions.get(item["catalog"])
        if description is None:
            if item["resolution"] != "unavailable":
                report.error(file, node + "/catalog", "manifest.catalog", "resolved item has no embedded loaded description")
        elif description.get("id") != item["reference"] or description.get("kind") != item["kind"]:
            report.error(file, node + "/reference", "manifest.catalog", "instance and embedded description identity disagree")
        if "populated" in item and native:
            if item["kind"] == "component" and isinstance(project, dict):
                part = assembly_target(project, item["path"])
                if part and item["populated"] != (part.get("populated") is not False):
                    report.error(file, node + "/populated", "manifest.population", "native population evidence disagrees with canonical project")
            if item["populated"] and item.get("model") == "unpopulated":
                report.error(file, node + "/populated", "manifest.population", "unpopulated native model cannot claim a fitted device")
            if not item["populated"] and (item["kind"] != "component" or item.get("model") != "unpopulated" or any(model["path"].startswith(item["path"] + ".") for model in payload["models"])):
                report.error(file, node + "/populated", "manifest.population", "DNP must be a passive footprint without compiled active children")
        if "version_satisfied" in item:
            try:
                satisfied = version_matches((description or {}).get("version", ""), item.get("requested_version"))
            except ValueError:
                report.error(file, node + "/version_satisfied", "manifest.version", "version evidence requires a supported request and loaded version")
            else:
                if item["version_satisfied"] != satisfied:
                    report.error(file, node + "/version_satisfied", "manifest.version", "claimed satisfaction disagrees with loaded metadata")
        if native and item["resolution"] == "demo":
            report.error(file, node + "/resolution", "manifest.source", "native manifest cannot claim demo parameters")


def check_sweep_request(report: Report, file: Path, doc: dict, index: Index) -> None:
    values = doc["values"]
    for i, value in enumerate(values):
        try:
            numeric = parse_si(value)
        except (ValueError, OverflowError):
            numeric = None
        if numeric is None or not math.isfinite(numeric):
            report.error(file, f"/values/{i}", "sweep.value", "expected finite number or SI string")
    at = parse_time_ps(doc["sample_at"])
    if at is None or at < 0 or at > 10**9 or at * len(values) > 10**10:
        report.error(file, "/sample_at", "sweep.time", "0–1 ms per point and 10 ms total are supported")


def check_parameter_sweep(report: Report, file: Path, doc: dict, index: Index) -> None:
    check_sweep_request(report, file, doc["request"], index)
    check_engineering_manifest(report, file, doc["source"], index)
    points, request = doc["points"], doc["request"]
    if len(points) != len(request["values"]) or doc["ok"] != all(p["ok"] for p in points):
        report.error(file, "/points", "sweep.points", "point count or summary status disagrees")
    for i, point in enumerate(points):
        if i < len(request["values"]) and point["requested"] != request["values"][i]:
            report.error(file, f"/points/{i}/requested", "sweep.requested", "point order/value differs from request")
        if [m["target"] for m in point["measurements"]] != request["probes"]:
            report.error(file, f"/points/{i}/measurements", "sweep.targets", "measurement order differs from request")
        if point["ok"] and (point["resolved"] is None or int(point["at_ps"]) != parse_time_ps(request["sample_at"])):
            report.error(file, f"/points/{i}", "sweep.sample", "successful point needs resolved value and requested sample time")
        try:
            expected_value = parse_si(point["requested"])
        except (ValueError, OverflowError):
            expected_value = None
        if point["ok"] and (expected_value is None or not math.isclose(point["resolved"] or 0, expected_value, rel_tol=1e-12, abs_tol=1e-15)):
            report.error(file, f"/points/{i}/resolved", "sweep.resolved", "resolved numeric value differs from requested value")
        if point["ok"] and any(d.get("severity") == "error" for d in point["diagnostics"]):
            report.error(file, f"/points/{i}/ok", "sweep.diagnostics", "successful point contains an error diagnostic")
        if not point["ok"] and (point["resolved"] is not None or any(m["voltage"] is not None for m in point["measurements"])):
            report.error(file, f"/points/{i}", "sweep.failure", "failed point must not carry numeric results")


def transient_grid(doc: dict) -> Optional[tuple[int, int, int]]:
    start, stop, step = (parse_time_ps(doc[k]) for k in ("start", "stop", "step"))
    if start is None or stop is None or step is None or not (0 <= start < stop <= 10**11) or step <= 0 or (stop-start) % step or (stop-start)//step > 4096:
        return None
    return start, stop, step


def check_transient_request(report: Report, file: Path, doc: dict, index: Index) -> None:
    if transient_grid(doc) is None:
        report.error(file, "/step", "transient.grid", "need an exact uniform 2–4097-sample grid with 0 <= start < stop <= 100 ms")


def check_transient_capture(report: Report, file: Path, doc: dict, index: Index) -> None:
    check_engineering_manifest(report, file, doc["source"], index)
    try:
        context = json.loads(doc["source"]["json"])
    except (ValueError, TypeError):
        context = None
    instances = context.get("instances", []) if isinstance(context, dict) else []
    if not isinstance(instances, list):
        instances = []
    if isinstance(context, dict) and (context.get("source") != "native" or context.get("policy") != "electrical" or any(i.get("kind") == "board" for i in instances if isinstance(i, dict))):
        report.error(file, "/source", "transient.source", "capture requires native board-free electrical source evidence")
    grid = transient_grid(doc["request"])
    if grid is None:
        check_transient_request(report, file, doc["request"], index)
        return
    start, stop, step = grid
    samples = doc["samples"]
    count = (stop-start)//step + 1
    end = int(doc["ended_at_ps"])
    if len(samples) > count or end > stop or (samples and end < int(samples[-1]["at_ps"])):
        report.error(file, "/samples", "transient.prefix", "samples/end must describe a bounded captured prefix")
    if doc["ok"] and (len(samples) != count or end != stop or doc["failure"] is not None or any(d.get("severity") == "error" for d in doc["diagnostics"])):
        report.error(file, "/ok", "transient.complete", "successful capture requires the complete grid, stop time and no error")
    if not doc["ok"] and (not doc["failure"] or len(samples) >= count or end > start+len(samples)*step):
        report.error(file, "/failure", "transient.failure", "incomplete capture needs a failure and must not claim a complete grid")
    for i, row in enumerate(samples):
        if int(row["at_ps"]) != start+i*step:
            report.error(file, f"/samples/{i}/at_ps", "transient.time", "sample time differs from the requested exact grid")
        if [m["target"] for m in row["measurements"]] != doc["request"]["probes"]:
            report.error(file, f"/samples/{i}/measurements", "transient.targets", "measurement order differs from requested targets")
        for m in row["measurements"]:
            try:
                finite = m["voltage"] is None or math.isfinite(m["voltage"])
            except (TypeError, OverflowError):
                finite = False
            if not finite:
                report.error(file, f"/samples/{i}/measurements", "transient.voltage", "numeric voltages must be finite; unavailable values use null")


CHECKS: dict[str, Callable[[Report, Path, dict, Index], Any]] = {
    "transient_request": check_transient_request,
    "transient_capture": check_transient_capture,
    "parameter_sweep_request": check_sweep_request,
    "parameter_sweep": check_parameter_sweep,
    "library_lock": check_library_lock,
    "engineering_manifest": check_engineering_manifest,
    "component": check_component,
    "component_test": check_component_test,
    "mcu": check_mcu,
    "package": check_package,
    "board": check_board,
    "project": check_project,
    "scenario": check_scenario,
    "recording": check_recording,
    "toolchain": check_toolchain,
    "registers": check_registers,
    "library": check_library,
    "board_reference": lambda report, path, doc, index: _check_reference_home(report, path, doc),
}


# --------------------------------------------------------------------------
# driver
# --------------------------------------------------------------------------


def validate_file(path: Path, schemas: Schemas, index: Index, report: Report, *, semantic: bool = True) -> None:
    report.files.append(str(path))
    doc = load_json(path, report)
    if doc is None:
        return
    if path.parent.resolve() == schemas.dir.resolve() and path.name.endswith(".schema.json"):
        _validate_meta(path, doc, schemas, report)
        return
    if not isinstance(doc, dict):
        report.error(path, "/", "schema.type", "top level must be an object")
        return
    kind = doc.get("kind")
    if not isinstance(kind, str) or kind not in KIND_SCHEMAS:
        report.error(path, "/kind", "schema.unknown_kind", f"unknown or missing kind {kind!r}; expected one of {sorted(KIND_SCHEMAS)}")
        return
    if doc.get("schema_version") != 1 and kind != "component_test":
        report.error(path, "/schema_version", "schema.version", f"unsupported schema_version {doc.get('schema_version')!r}")
    schema = schemas.for_kind(kind)
    if schema is None:
        report.error(path, "/kind", "schema.missing", f"no schema for kind {kind}")
        return
    violations = schemas.validate(doc, schema)
    for node, msg in violations:
        code = "schema.unknown_field" if msg.startswith("unknown field") else "schema.missing_field" if msg.startswith("missing required") else "schema.type"
        report.error(path, node, code, msg)
    if violations or not semantic:
        return
    if kind == "engineering_manifest":
        check_engineering_manifest(report, path, doc, index, schemas)
    else:
        CHECKS[kind](report, path, doc, index)


def _validate_meta(path: Path, doc: Any, schemas: Schemas, report: Report) -> None:
    if not isinstance(doc, dict):
        report.error(path, "/", "schema.type", "schema must be an object")
        return
    if doc.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
        report.error(path, "/$schema", "schema.version", "schemas must declare JSON Schema 2020-12")
    if doc.get("$id") != SCHEMA_BASE + path.name:
        report.error(path, "/$id", "schema.type", f"$id must be {SCHEMA_BASE + path.name}")
    # every $ref resolves; every object schema with properties is closed
    def walk(node: Any, p: list) -> None:
        if isinstance(node, dict):
            if "$ref" in node:
                try:
                    schemas.resolve_ref(node["$ref"], doc["$id"])
                except (KeyError, IndexError, ValueError, TypeError) as exc:
                    report.error(path, ptr(*p, "$ref"), "schema.ref", f"unresolvable $ref {node['$ref']}: {exc}")
            if "properties" in node and node.get("type") == "object" and "additionalProperties" not in node and "patternProperties" not in node:
                if not any(k in node for k in ("oneOf", "anyOf", "allOf")) and p and p[-1] not in ("out_fields", "in_fields"):
                    report.error(path, ptr(*p), "schema.open", "object schema with properties is not closed (additionalProperties)")
            for k, v in node.items():
                walk(v, p + [k])
        elif isinstance(node, list):
            for i, v in enumerate(node):
                walk(v, p + [i])
    walk(doc, [])
    if schemas.backend == "jsonschema":
        import jsonschema  # type: ignore

        try:
            jsonschema.Draft202012Validator.check_schema(doc)
        except jsonschema.exceptions.SchemaError as exc:  # type: ignore[attr-defined]
            report.error(path, ptr(*exc.path), "schema.meta", exc.message)


def collect_all() -> list[Path]:
    files: list[Path] = []
    for d in (SCHEMA_DIR, CATALOG_DIR, EXAMPLES_DIR):
        if d.is_dir():
            files.extend(sorted(p for p in d.rglob("*.json") if not (set(p.parts) & {"node_modules", "build"})))
    return files


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", help="files or directories to validate")
    ap.add_argument("--all", action="store_true", help="validate schemas/, catalog/ and examples/")
    ap.add_argument("--strict", action="store_true", help="treat warnings as errors")
    ap.add_argument("--no-semantic", action="store_true", help="schema checks only")
    ap.add_argument("--quiet", action="store_true", help="print only the summary")
    ap.add_argument("--library", action="append", default=[], help="another library to resolve ids in (repeatable)")
    args = ap.parse_args(argv)
    paths: list[Path] = []
    if args.all:
        paths.extend(collect_all())
    for p in args.paths:
        pp = Path(p)
        if pp.is_dir():
            paths.extend(sorted(pp.rglob("*.json")))
        elif pp.exists():
            paths.append(pp)
        else:
            print(f"{p}: no such file", file=sys.stderr)
            return 2
    if not paths:
        ap.print_usage(file=sys.stderr)
        print("validate.py: nothing to validate (give PATH... or --all)", file=sys.stderr)
        return 2
    schemas = Schemas()
    # the libraries holding the given paths are indexed ahead of the bundled
    # catalog, so a library under development resolves its own ids
    roots: list[Path] = [Path(l) for l in args.library]
    for p in args.paths:
        q = Path(p).resolve()
        for cand in [q, *q.parents]:
            if (cand / "library.json").is_file():
                if cand.resolve() != CATALOG_DIR.resolve() and cand not in roots:
                    roots.append(cand)
                break
    index = Index([*roots, CATALOG_DIR])
    report = Report()
    for p in paths:
        validate_file(p, schemas, index, report, semantic=not args.no_semantic)
    if not args.quiet:
        for f in report.findings:
            print(f.line())
    failed = report.errors > 0 or (args.strict and report.warnings > 0)
    print(f"validated {len(report.files)} file(s) with {schemas.backend}: {report.errors} error(s), {report.warnings} warning(s) -> {'FAIL' if failed else 'OK'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
