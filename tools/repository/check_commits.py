#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Check that non-merge commits in a range carry the owner's identity and no
co-author or assistant trailers (ADR-014, CONTRIBUTING)."""
import argparse
import re
import subprocess
import sys

OWNER_EMAILS = {"motsoujunior17@gmail.com"}
FORBIDDEN_TRAILERS = {"co-authored-by", "signed-off-by", "claude-session", "generated-by"}
FORBIDDEN_TEXT = re.compile(r"generated with \[?claude|claude\.ai/code|anthropic", re.IGNORECASE)


def git(*args, input=None):
    return subprocess.check_output(["git", *args], input=input, text=True).strip()


def trailers(message):
    out = git("interpret-trailers", "--parse", input=message)
    result = []
    for line in out.splitlines():
        key, sep, value = line.partition(":")
        if sep:
            result.append((key.strip().casefold(), value.strip()))
    return result


def check_commit(sha, allowed_emails=OWNER_EMAILS):
    """Return a list of problems for one commit (empty when it is acceptable)."""
    fields = git("show", "-s", "--format=%ae%n%ce%n%P", sha).splitlines()
    author_email, committer_email, parents = fields[0], fields[1], fields[2] if len(fields) > 2 else ""
    if len(parents.split()) > 1:
        return []  # merge commits are excluded (GitHub-generated)
    message = git("show", "-s", "--format=%B", sha)
    problems = []
    if author_email.casefold() not in {e.casefold() for e in allowed_emails}:
        problems.append(f"author {author_email} is not the repository owner")
    for key, value in trailers(message):
        if key in FORBIDDEN_TRAILERS:
            problems.append(f"forbidden trailer {key}: {value}")
    if FORBIDDEN_TEXT.search(message):
        problems.append("message contains assistant attribution text")
    return problems


def resolve(ref):
    if ref != "HEAD" and not re.fullmatch(r"[0-9a-fA-F]{40}", ref):
        raise ValueError("Commit must be HEAD or a full 40-character SHA")
    return git("rev-parse", "--verify", ref + "^{commit}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", help="base or push-before SHA; omit to check only --head")
    parser.add_argument("--head", default="HEAD")
    args = parser.parse_args()
    head = resolve(args.head)
    if args.base and args.base != "0" * 40:
        shas = git("rev-list", "--no-merges", resolve(args.base) + ".." + head).split()
    else:
        shas = [head]
    failed = 0
    for sha in shas:
        problems = check_commit(sha)
        subject = git("show", "-s", "--format=%s", sha)
        if problems:
            failed += 1
            print(f"FAIL {sha[:12]} {subject}")
            for p in problems:
                print(f"     {p}")
        else:
            print(f"ok   {sha[:12]} {subject}")
    print(f"{len(shas)} commit(s) checked, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
