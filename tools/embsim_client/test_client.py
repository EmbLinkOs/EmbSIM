# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Unit tests for the parts of embsim_client that need no server: the
WebSocket frame codec and the handshake accept key. Run with
`python3 -m unittest tools.embsim_client.test_client` from the repository
root, or `python3 -m unittest discover tools/embsim_client`."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from embsim_client import TransportError, accept_key, decode_frame, encode_frame  # noqa: E402


class AcceptKeyTest(unittest.TestCase):
    def test_rfc_example(self):
        self.assertEqual(accept_key("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")


class FrameTest(unittest.TestCase):
    def test_unmasked_text(self):
        self.assertEqual(encode_frame(0x1, b"Hello", mask=False), bytes([0x81, 0x05]) + b"Hello")

    def test_masked_text_rfc_example(self):
        frame = encode_frame(0x1, b"Hello", mask=True, mask_key=bytes([0x37, 0xfa, 0x21, 0x3d]))
        self.assertEqual(frame, bytes([0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58]))
        self.assertEqual(decode_frame(frame), (True, 0x1, b"Hello", len(frame)))

    def test_lengths(self):
        for n, header_len in ((0, 2), (125, 2), (126, 4), (65535, 4), (65536, 10)):
            payload = bytes(n)
            frame = encode_frame(0x2, payload, mask=False)
            self.assertEqual(len(frame), header_len + n)
            self.assertEqual(decode_frame(frame), (True, 0x2, payload, len(frame)))
        frame = encode_frame(0x2, bytes(300), mask=False)
        self.assertEqual(frame[1], 126)
        self.assertEqual(struct.unpack("!H", frame[2:4])[0], 300)

    def test_partial_and_trailing(self):
        frame = encode_frame(0x1, b"abcdef", mask=True)
        for cut in range(len(frame)):
            self.assertIsNone(decode_frame(frame[:cut]))
        fin, op, payload, consumed = decode_frame(frame + b"extra")
        self.assertEqual((fin, op, payload, consumed), (True, 0x1, b"abcdef", len(frame)))

    def test_fin_and_control(self):
        frame = encode_frame(0x1, b"part", mask=False, fin=False)
        self.assertEqual(decode_frame(frame)[0], False)
        ping = encode_frame(0x9, b"x", mask=False)
        self.assertEqual(decode_frame(ping)[1], 0x9)
        with self.assertRaises(TransportError):
            decode_frame(bytes([0xC1, 0x00]))  # reserved bit
        with self.assertRaises(TransportError):
            decode_frame(bytes([0x09, 0x00]))  # fragmented ping
        with self.assertRaises(TransportError):
            decode_frame(bytes([0x89, 0x7E, 0x01, 0x00]) + bytes(256))  # long ping

    def test_bad_opcode_or_key(self):
        with self.assertRaises(ValueError):
            encode_frame(16, b"")
        with self.assertRaises(ValueError):
            encode_frame(1, b"", mask=True, mask_key=b"abc")


if __name__ == "__main__":
    unittest.main()
