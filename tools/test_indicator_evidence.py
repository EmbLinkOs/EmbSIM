#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Evidence must bind exact parts, calibration, holdouts and native fixtures."""
import copy
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from catalog.indicator_evidence import check, source_points

ROOT = Path(__file__).resolve().parents[1]


class IndicatorEvidence(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='embsim-indicator-evidence-')
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name) / 'part'
        shutil.copytree(ROOT / 'catalog/components/wp7113id', self.base)
        self.doc = json.loads((self.base / 'component.json').read_text())

    def test_reviewed_evidence_is_consistent(self):
        self.assertEqual(check(self.doc, self.base), [])

    def test_source_identity_and_curve_coefficients_cannot_drift(self):
        for mutation in ('hash', 'model'):
            doc = copy.deepcopy(self.doc)
            if mutation == 'hash':
                doc['qualification']['source']['sha256'] = '0' * 64
            else:
                doc['model']['params']['rs'] *= 2
            self.assertTrue(check(doc, self.base), mutation)

    def test_holdout_reuse_and_nonfinite_points_are_rejected(self):
        doc = copy.deepcopy(self.doc)
        points = doc['qualification']['curves'][0]['points']
        points[1]['current_a'] = points[0]['current_a']
        self.assertTrue(check(doc, self.base))
        points[1]['voltage_v'] = float('nan')
        self.assertTrue(check(doc, self.base))

    def test_expected_values_cannot_be_replaced_by_model_predictions(self):
        point = self.doc['qualification']['curves'][0]['points'][1]
        path = self.base / point['fixture']
        fixture = json.loads(path.read_text())
        fixture['expect']['net:DA']['max_v'] += .1
        path.write_text(json.dumps(fixture))
        self.assertTrue(check(self.doc, self.base))

    def test_unreviewed_pdf_is_rejected_before_optional_pdf_parsing(self):
        path = self.base / 'unreviewed.pdf'
        path.write_bytes(b'not the pinned source')
        self.assertEqual(source_points(self.doc, path),
                         ['PDF hash mismatch: use the exact reviewed source revision'])

    def test_evidence_cannot_reference_files_outside_local_fixtures(self):
        self.doc['qualification']['curves'][0]['points'][0]['fixture'] = '../component.json'
        self.assertTrue(check(self.doc, self.base))

    def test_missing_fixture_and_wrong_native_part_are_rejected(self):
        point = self.doc['qualification']['curves'][0]['points'][0]
        path = self.base / point['fixture']
        fixture = json.loads(path.read_text())
        fixture['circuit']['components'][0]['component'] = 'emblink/led'
        path.write_text(json.dumps(fixture))
        self.assertTrue(check(self.doc, self.base))
        path.unlink()
        self.assertTrue(check(self.doc, self.base))


if __name__ == '__main__':
    unittest.main()
