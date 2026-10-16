# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Headless runs through the command line (SPEC-008 §8, §9; SPEC-012 §4):
the CI contract of `embsim run --json` and `embsim replay`, from Python.

    from embsim import run

    result = run("examples/bluepill-button-led/project.json",
                 scenario="examples/bluepill-button-led/scenarios/press.json",
                 coverage="cov.info", coverage_format="lcov")
    assert result.ok, result.failed
"""
from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass, field
from typing import Optional, Sequence, Union

from .session import find_embsim

PathLike = Union[str, os.PathLike]

# the exit codes (SPEC-008 §9)
EXIT_OK, EXIT_ASSERTION, EXIT_USAGE, EXIT_FATAL, EXIT_DIVERGED = 0, 1, 2, 3, 4


@dataclass
class RunResult:
    """A run's exit code, its JSON report and its stderr."""
    code: int
    report: dict = field(default_factory=dict)
    stderr: str = ""

    @property
    def ok(self) -> bool:
        return self.code == EXIT_OK

    @property
    def failed(self) -> list[dict]:
        """The assertions that did not pass."""
        return [a for a in self.report.get("assertions") or [] if not a.get("ok")]

    @property
    def coverage(self) -> dict:
        return self.report.get("coverage") or {}

    @property
    def profile(self) -> dict:
        return self.report.get("profile") or {}

    @property
    def stack(self) -> dict:
        return self.report.get("stack") or {}


def _exec(argv: Sequence[str], cwd: Optional[PathLike], timeout: Optional[float]) -> RunResult:
    p = subprocess.run(list(argv), capture_output=True, text=True, cwd=str(cwd) if cwd else None, timeout=timeout)
    try:
        report = json.loads(p.stdout) if p.stdout.strip() else {}
    except json.JSONDecodeError:
        report = {}
    return RunResult(p.returncode, report, p.stderr)


def run(project: PathLike, *, scenario: Optional[PathLike] = None, until: Optional[str] = None,
        policy: Optional[str] = None, probes: Sequence[str] = (), record: Optional[PathLike] = None,
        coverage: Optional[PathLike] = None, coverage_format: str = "lcov", profile: Optional[PathLike] = None,
        profile_format: str = "collapsed", stack: Optional[PathLike] = None, stack_format: str = "text",
        board: Optional[str] = None, embsim: Optional[str] = None,
        cwd: Optional[PathLike] = None, timeout: Optional[float] = 600.0) -> RunResult:
    """`embsim run PROJECT --json` with the given options."""
    argv = [find_embsim(embsim), "run", str(project), "--json"]
    if scenario:
        argv += ["--scenario", str(scenario)]
    if until:
        argv += ["--until", until]
    if policy:
        argv += ["--policy", policy]
    for p in probes:
        argv += ["--probe", p]
    if record:
        argv += ["--record", str(record)]
    if coverage:
        argv += ["--coverage", str(coverage), "--coverage-format", coverage_format]
    if profile:
        argv += ["--profile", str(profile), "--profile-format", profile_format]
    if stack:
        argv += ["--stack", str(stack), "--stack-format", stack_format]
    if board:
        argv += ["--board", board]
    return _exec(argv, cwd, timeout)


def replay(recording: PathLike, *, allow_drift: bool = False, embsim: Optional[str] = None,
           cwd: Optional[PathLike] = None, timeout: Optional[float] = 600.0) -> RunResult:
    """`embsim replay RECORDING --json`: code 0 when every observation
    matched, 4 at the first divergence (report["divergence"]), 2 when the
    project or an image changed."""
    argv = [find_embsim(embsim), "replay", str(recording), "--json"]
    if allow_drift:
        argv.append("--allow-drift")
    return _exec(argv, cwd, timeout)


__all__ = ["run", "replay", "RunResult", "EXIT_OK", "EXIT_ASSERTION", "EXIT_USAGE", "EXIT_FATAL", "EXIT_DIVERGED"]
