# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The EmbSIM protocol client (SPEC-008 §1): JSON-RPC 2.0 over the stdio
transport (one object per line, `embsim sidecar`) and over WebSocket (one
object per text frame, `embsim serve`). The WebSocket framing (RFC 6455) is
implemented here; client-to-server frames are masked as the RFC requires.

    with WebSocketClient("ws://127.0.0.1:8765/ws") as c:
        c.call("session.open", {"client": "tool/0.1", "protocol": "1.0", "streams": "base64"})
        print(c.call("session.info", {}))

Python 3.10+, standard library only.
"""
from __future__ import annotations

import base64
import hashlib
import json
import os
import secrets
import socket
import struct
import subprocess
import time
from dataclasses import dataclass
from typing import Any, Optional
from urllib.parse import urlparse

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
OP_CONTINUATION, OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA
MAX_MESSAGE = 16 << 20


class EmbsimError(Exception):
    """A JSON-RPC error response (SPEC-008 §3)."""

    def __init__(self, code: int, message: str, data: Any = None):
        super().__init__(f"[{code}] {message}")
        self.code = code
        self.message = message
        self.data = data


class TransportError(Exception):
    """The transport failed (closed, malformed framing, bad handshake)."""


@dataclass
class Notification:
    method: str
    params: Any


# ---- WebSocket framing ---------------------------------------------------


def accept_key(client_key: str) -> str:
    """Sec-WebSocket-Accept for a client key (RFC 6455 §4.2.2)."""
    digest = hashlib.sha1((client_key + WS_GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def encode_frame(opcode: int, payload: bytes, mask: bool = True, fin: bool = True,
                 mask_key: Optional[bytes] = None) -> bytes:
    """One frame. Clients mask (mask=True); a fixed mask_key is for tests."""
    if not 0 <= opcode <= 0xF:
        raise ValueError("bad opcode")
    header = bytearray([(0x80 if fin else 0) | opcode])
    n = len(payload)
    mask_bit = 0x80 if mask else 0
    if n < 126:
        header.append(mask_bit | n)
    elif n <= 0xFFFF:
        header.append(mask_bit | 126)
        header += struct.pack("!H", n)
    else:
        header.append(mask_bit | 127)
        header += struct.pack("!Q", n)
    if not mask:
        return bytes(header) + payload
    key = mask_key if mask_key is not None else secrets.token_bytes(4)
    if len(key) != 4:
        raise ValueError("mask key must be 4 bytes")
    header += key
    masked = bytes(b ^ key[i & 3] for i, b in enumerate(payload))
    return bytes(header) + masked


def decode_frame(buf: bytes) -> Optional[tuple[bool, int, bytes, int]]:
    """Parse one frame from the start of buf. Returns (fin, opcode, payload,
    consumed) or None when the buffer does not hold a whole frame yet.
    Raises TransportError on a protocol violation."""
    if len(buf) < 2:
        return None
    b0, b1 = buf[0], buf[1]
    if b0 & 0x70:
        raise TransportError("reserved bits set")
    fin = bool(b0 & 0x80)
    opcode = b0 & 0x0F
    masked = bool(b1 & 0x80)
    length = b1 & 0x7F
    pos = 2
    if length == 126:
        if len(buf) < 4:
            return None
        length = struct.unpack("!H", buf[2:4])[0]
        pos = 4
    elif length == 127:
        if len(buf) < 10:
            return None
        length = struct.unpack("!Q", buf[2:10])[0]
        if length >> 63:
            raise TransportError("bad length")
        pos = 10
    if opcode >= 0x8 and (not fin or length > 125):
        raise TransportError("bad control frame")
    key = b""
    if masked:
        if len(buf) < pos + 4:
            return None
        key = buf[pos:pos + 4]
        pos += 4
    if len(buf) < pos + length:
        return None
    payload = buf[pos:pos + length]
    if masked:
        payload = bytes(b ^ key[i & 3] for i, b in enumerate(payload))
    return fin, opcode, payload, pos + length


# ---- clients -------------------------------------------------------------


class _BaseClient:
    """Request ids, error mapping and the notification queue; subclasses
    provide _send_text and _recv_text."""

    def __init__(self) -> None:
        self._next_id = 1
        self._notifications: list[Notification] = []
        self.closed = False

    def _send_text(self, text: str) -> None:
        raise NotImplementedError

    def _recv_text(self, timeout: Optional[float]) -> Optional[str]:
        raise NotImplementedError

    def call(self, method: str, params: Any = None, timeout: Optional[float] = 30.0) -> Any:
        """Send one request and wait for its response; notifications that
        arrive meanwhile are queued. Raises EmbsimError on a JSON-RPC error."""
        if self.closed:
            raise TransportError("client is closed")
        req_id = self._next_id
        self._next_id += 1
        request = {"jsonrpc": "2.0", "id": req_id, "method": method, "params": params if params is not None else {}}
        self._send_text(json.dumps(request, separators=(",", ":")))
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            remaining = None if deadline is None else max(0.0, deadline - time.monotonic())
            text = self._recv_text(remaining)
            if text is None:
                raise TransportError(f"no response to {method} within {timeout} s")
            try:
                msg = json.loads(text)
            except ValueError as e:
                raise TransportError(f"not JSON: {text[:80]!r}") from e
            if not isinstance(msg, dict):
                raise TransportError("response is not an object")
            if "id" not in msg or msg.get("id") is None and "method" in msg:
                self._notifications.append(Notification(msg.get("method", ""), msg.get("params")))
                continue
            if msg.get("id") != req_id:
                # a response to something else: ignore (responses are in order)
                continue
            if "error" in msg:
                err = msg["error"] or {}
                raise EmbsimError(int(err.get("code", -32603)), str(err.get("message", "error")), err.get("data"))
            return msg.get("result")

    def notify(self, method: str, params: Any = None) -> None:
        """Send a JSON-RPC notification (no id, no response)."""
        self._send_text(json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}},
                                   separators=(",", ":")))

    def poll(self, timeout: float = 0.0) -> None:
        """Drain notifications that arrived without a pending request."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = max(0.0, deadline - time.monotonic())
            text = self._recv_text(remaining)
            if text is None:
                return
            try:
                msg = json.loads(text)
            except ValueError:
                continue
            if isinstance(msg, dict) and "method" in msg:
                self._notifications.append(Notification(msg["method"], msg.get("params")))
            if remaining == 0:
                return

    def notifications(self) -> list[Notification]:
        """The notifications received so far (the list is cleared)."""
        out, self._notifications = self._notifications, []
        return out

    def close(self) -> None:
        raise NotImplementedError

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class StdioClient(_BaseClient):
    """Runs `cmd_argv` (e.g. ["embsim", "sidecar", "project.json"]) and
    speaks newline-delimited JSON on its stdin/stdout."""

    def __init__(self, cmd_argv: list[str], cwd: Optional[str] = None, env: Optional[dict[str, str]] = None):
        super().__init__()
        self.proc = subprocess.Popen(cmd_argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, cwd=cwd,
                                     env=env, bufsize=0)
        self._buf = b""

    def _send_text(self, text: str) -> None:
        assert self.proc.stdin is not None
        try:
            self.proc.stdin.write(text.encode("utf-8") + b"\n")
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError) as e:
            raise TransportError("sidecar stdin closed") from e

    def _recv_text(self, timeout: Optional[float]) -> Optional[str]:
        assert self.proc.stdout is not None
        fd = self.proc.stdout.fileno()
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            nl = self._buf.find(b"\n")
            if nl >= 0:
                line, self._buf = self._buf[:nl], self._buf[nl + 1:]
                return line.decode("utf-8", errors="replace")
            if deadline is not None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                if not _fd_readable(fd, remaining):
                    continue
            chunk = os.read(fd, 65536)
            if not chunk:
                if self._buf:
                    line, self._buf = self._buf, b""
                    return line.decode("utf-8", errors="replace")
                raise TransportError("sidecar closed its stdout")
            self._buf += chunk

    def close(self, graceful: bool = True) -> None:
        if self.closed:
            return
        self.closed = True
        try:
            if graceful and self.proc.poll() is None and self.proc.stdin:
                self.proc.stdin.write(b'{"jsonrpc":"2.0","id":0,"method":"session.close","params":{}}\n')
                self.proc.stdin.flush()
                self.proc.stdin.close()
                self.proc.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            pass
        finally:
            if self.proc.poll() is None:
                self.proc.kill()
                self.proc.wait()
            if self.proc.stdout:
                self.proc.stdout.close()


def _fd_readable(fd: int, timeout: float) -> bool:
    """select() on a pipe; on Windows pipes are not selectable so we block."""
    try:
        import select
        r, _, _ = select.select([fd], [], [], timeout)
        return bool(r)
    except (OSError, ValueError):
        return True


class WebSocketClient(_BaseClient):
    """Connects to ws://host:port/ws and exchanges text frames."""

    def __init__(self, url: str = "ws://127.0.0.1:8765/ws", timeout: float = 10.0, origin: Optional[str] = None):
        super().__init__()
        u = urlparse(url)
        if u.scheme != "ws":
            raise ValueError("only ws:// URLs are supported")
        host = u.hostname or "127.0.0.1"
        port = u.port or 80
        path = u.path or "/ws"
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._buf = b""
        self._fragments: list[bytes] = []
        self._fragment_opcode = -1
        key = base64.b64encode(secrets.token_bytes(16)).decode("ascii")
        lines = [f"GET {path} HTTP/1.1", f"Host: {host}:{port}", "Upgrade: websocket", "Connection: Upgrade",
                 f"Sec-WebSocket-Key: {key}", "Sec-WebSocket-Version: 13"]
        if origin:
            lines.append(f"Origin: {origin}")
        self.sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("ascii"))
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise TransportError("connection closed during the handshake")
            head += chunk
            if len(head) > 65536:
                raise TransportError("handshake response too large")
        head, self._buf = head.split(b"\r\n\r\n", 1)
        status_line, *header_lines = head.decode("latin-1").split("\r\n")
        if " 101 " not in status_line:
            raise TransportError(f"handshake refused: {status_line}")
        headers = {}
        for line in header_lines:
            if ":" in line:
                k, v = line.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        if headers.get("sec-websocket-accept") != accept_key(key):
            raise TransportError("bad Sec-WebSocket-Accept")

    def _send_text(self, text: str) -> None:
        self._send_frame(OP_TEXT, text.encode("utf-8"))

    def _send_frame(self, opcode: int, payload: bytes) -> None:
        try:
            self.sock.sendall(encode_frame(opcode, payload, mask=True))
        except OSError as e:
            raise TransportError("send failed") from e

    def _recv_text(self, timeout: Optional[float]) -> Optional[str]:
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            frame = decode_frame(self._buf)
            if frame is not None:
                fin, opcode, payload, consumed = frame
                self._buf = self._buf[consumed:]
                if opcode == OP_PING:
                    self._send_frame(OP_PONG, payload)
                    continue
                if opcode == OP_PONG:
                    continue
                if opcode == OP_CLOSE:
                    try:
                        self.sock.sendall(encode_frame(OP_CLOSE, payload[:2], mask=True))
                    except OSError:
                        pass
                    self.closed = True
                    raise TransportError("server closed the connection")
                if opcode in (OP_TEXT, OP_BINARY):
                    if fin:
                        if opcode == OP_TEXT:
                            return payload.decode("utf-8")
                        continue  # binary streams are not surfaced yet
                    self._fragment_opcode = opcode
                    self._fragments = [payload]
                    continue
                if opcode == OP_CONTINUATION:
                    if self._fragment_opcode < 0:
                        raise TransportError("continuation without a start")
                    self._fragments.append(payload)
                    if sum(map(len, self._fragments)) > MAX_MESSAGE:
                        raise TransportError("message too large")
                    if fin:
                        op, data = self._fragment_opcode, b"".join(self._fragments)
                        self._fragment_opcode, self._fragments = -1, []
                        if op == OP_TEXT:
                            return data.decode("utf-8")
                    continue
                raise TransportError(f"unexpected opcode {opcode}")
            if deadline is not None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self.sock.settimeout(remaining)
            else:
                self.sock.settimeout(None)
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                return None
            except OSError as e:
                raise TransportError("recv failed") from e
            if not chunk:
                self.closed = True
                raise TransportError("connection closed")
            self._buf += chunk

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        try:
            self.sock.sendall(encode_frame(OP_CLOSE, struct.pack("!H", 1000), mask=True))
            self.sock.settimeout(1.0)
            self.sock.recv(4096)
        except OSError:
            pass
        finally:
            self.sock.close()
