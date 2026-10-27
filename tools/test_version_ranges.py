# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Language parity vectors for the bounded SemVer contract."""
import json
from pathlib import Path
import unittest
from version_ranges import matches


class VersionRanges(unittest.TestCase):
    def test_shared_vectors(self):
        vectors = json.loads((Path(__file__).resolve().parents[1] / 'tests/fixtures/version-ranges.json').read_text(encoding='utf-8'))
        for vector in vectors:
            with self.subTest(**vector):
                try: result = int(matches(vector['version'], vector['range']))
                except ValueError: result = -1
                self.assertEqual(result, vector['expected'])
