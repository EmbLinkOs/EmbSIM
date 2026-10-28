# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Bounded SemVer precedence and conjunctions, shared-vector checked against C.

Not a package resolver. Components of numeric versions are 0..2147483647;
versions/ranges are at most 255 ASCII characters and 16 conjunctive clauses.
"""
from __future__ import annotations
from dataclasses import dataclass
import re

MAX = 2147483647
TOKEN = re.compile(r'([0-9]+(?:\.[0-9]+){0,2})(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?')
SPACE = ' \t\r\n'


@dataclass(frozen=True)
class Version:
    core: tuple[int, int, int]
    parts: int
    pre: str

    @property
    def key(self):
        pre = tuple((0, int(s)) if s.isdecimal() else (1, s) for s in self.pre.split('.')) if self.pre else ((2, ''),)
        return self.core, pre


def parse(text: str, pos: int = 0) -> tuple[Version, int]:
    token = TOKEN.match(text, pos)
    if not token:
        raise ValueError('invalid semantic version')
    fields = token[1].split('.')
    if any((len(s) > 1 and s.startswith('0')) or len(s) > 10 or int(s) > MAX for s in fields):
        raise ValueError('version components must be 0..2147483647 without leading zeros')
    pre = token[2] or ''
    if (pre or token[3]) and len(fields) != 3:
        raise ValueError('prerelease/build suffixes require a full version')
    if any(s.isdecimal() and len(s) > 1 and s.startswith('0') for s in pre.split('.')):
        raise ValueError('numeric prerelease identifiers cannot have leading zeros')
    return Version(tuple([int(s) for s in fields] + [0] * (3 - len(fields))), len(fields), pre), token.end()


def matches(version: str, constraint: str) -> bool:
    if not isinstance(version, str) or not isinstance(constraint, str) or len(version) > 255 or len(constraint) > 255:
        raise ValueError('version/range must be strings of at most 255 characters')
    actual, end = parse(version)
    if end != len(version) or actual.parts != 3:
        raise ValueError('loaded version must be a full semantic version')
    p = 0; count = 0; accepted = True; pre_allowed = not actual.pre
    while p < len(constraint) and constraint[p] in SPACE: p += 1
    while p < len(constraint):
        count += 1
        if count > 16: raise ValueError('at most 16 conjunction clauses are supported')
        op = '='
        if constraint[p] in '^~<>=':
            op = constraint[p]; p += 1
            if p < len(constraint) and constraint[p] == '=' and op in '<>': op += '='; p += 1
        while p < len(constraint) and constraint[p] in SPACE: p += 1
        bound, p = parse(constraint, p)
        pre_allowed |= bool(bound.pre and bound.core == actual.core)
        def upper(at: int):
            core = list(bound.core)
            if core[at] == MAX: raise ValueError('upper version bound overflows')
            core[at] += 1
            core[at+1:] = [0] * (2-at)
            return Version(tuple(core), 3, '').key
        if op in '^~' or (op == '=' and bound.parts < 3):
            at = bound.parts - 1
            if op == '~': at = 0 if bound.parts == 1 else 1
            if op == '^':
                at = 0
                while at + 1 < bound.parts and bound.core[at] == 0: at += 1
            limit = upper(at)
            passed = bound.key <= actual.key < limit
        elif (op == '>' or op == '<=') and bound.parts < 3:
            limit = upper(bound.parts - 1)
            passed = actual.key >= limit if op == '>' else actual.key < limit
        else:
            passed = {'=': actual.key == bound.key, '>': actual.key > bound.key, '>=': actual.key >= bound.key,
                      '<': actual.key < bound.key, '<=': actual.key <= bound.key}[op]
        accepted &= passed
        if p == len(constraint): break
        separator = p
        while p < len(constraint) and constraint[p] in SPACE: p += 1
        if p < len(constraint) and constraint[p] == ',':
            p += 1
            while p < len(constraint) and constraint[p] in SPACE: p += 1
            if p == len(constraint): raise ValueError('trailing conjunction separator')
        elif p == separator and constraint[p] not in '<>=':
            raise ValueError('unsupported range syntax')
    if not count: raise ValueError('empty range')
    return accepted and pre_allowed
