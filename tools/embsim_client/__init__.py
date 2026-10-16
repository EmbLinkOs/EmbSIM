# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""embsim_client -- the repository's name for the Python client's protocol
transport, kept for the test suites. The client itself is the `embsim`
package in python/ (pip install ./python); this module re-exports its
transport without installing it."""
import os
import sys

_PY = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "python")
if _PY not in sys.path:
    sys.path.insert(0, _PY)

from embsim.transport import (  # noqa: E402
    EmbsimError,
    Notification,
    StdioClient,
    TransportError,
    WebSocketClient,
    accept_key,
    decode_frame,
    encode_frame,
)

__all__ = [
    "EmbsimError",
    "Notification",
    "StdioClient",
    "TransportError",
    "WebSocketClient",
    "accept_key",
    "decode_frame",
    "encode_frame",
]
