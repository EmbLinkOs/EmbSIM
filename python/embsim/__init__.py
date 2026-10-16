# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""embsim -- the EmbSIM simulator from Python (SPEC-008): a Session over the
protocol, headless runs through the command line, and pytest fixtures.
Standard library only (pytest for the fixtures)."""
from .runner import EXIT_ASSERTION, EXIT_DIVERGED, EXIT_FATAL, EXIT_OK, EXIT_USAGE, RunResult, replay, run
from .session import Session, find_embsim
from .transport import EmbsimError, Notification, StdioClient, TransportError, WebSocketClient

__version__ = "0.1.0"

__all__ = [
    "Session",
    "find_embsim",
    "run",
    "replay",
    "RunResult",
    "EmbsimError",
    "TransportError",
    "Notification",
    "StdioClient",
    "WebSocketClient",
    "EXIT_OK",
    "EXIT_ASSERTION",
    "EXIT_USAGE",
    "EXIT_FATAL",
    "EXIT_DIVERGED",
]
