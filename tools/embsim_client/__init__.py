# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""embsim_client -- a small Python client for the EmbSIM session protocol
(SPEC-008) over the stdio and WebSocket transports. Standard library only."""
from .client import (
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
