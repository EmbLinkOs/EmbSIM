# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""pytest fixtures for firmware tests against the simulator. Enabled by
installing the package (its `pytest11` entry point), or with
`-p embsim.pytest_plugin`.

    def test_banner(embsim_session):
        s = embsim_session("examples/bluepill-button-led/project.json")
        s.advance(until="20ms")
        assert "Blue Pill" in s.serial("B1")

`--embsim PATH` (or $EMBSIM) names the executable.
"""
from __future__ import annotations

import pytest

from .session import Session


def pytest_addoption(parser):
    parser.addoption("--embsim", action="store", default=None, help="the embsim executable")


@pytest.fixture
def embsim_session(request):
    """A factory of Sessions, each closed when the test ends."""
    opened = []

    def make(project=None, **kw):
        kw.setdefault("embsim", request.config.getoption("--embsim"))
        s = Session(project, **kw)
        opened.append(s)
        return s

    yield make
    for s in opened:
        s.close()
