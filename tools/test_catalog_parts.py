# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Part metadata, package maps, and fixture conversion (without the native build)."""
import copy
import json
from pathlib import Path
import unittest
from catalog.run_fixtures import convert

ROOT = Path(__file__).resolve().parents[1]


class NamedPartTests(unittest.TestCase):
    def load(self, name):
        return json.loads((ROOT / 'catalog/components' / name / 'component.json').read_text())

    def test_package_maps_distinguish_physically_different_parts(self):
        expected = {
            'bjt_2n3904': {'C': 3, 'B': 2, 'E': 1},
            'bc547b': {'C': 1, 'B': 2, 'E': 3},
            'mosfet_2n7000': {'D': 3, 'G': 2, 'S': 1},
            'bss138': {'D': 3, 'G': 1, 'S': 2},
            'l7805cv': {'IN': 1, 'GND': 2, 'OUT': 3},
            'lm1117_3v3': {'IN': 3, 'GND': 1, 'OUT': 2},
            'sn74hc00n': {'1A': 1, '1B': 2, '1Y': 3},
            'sn74hc02n': {'1A': 2, '1B': 3, '1Y': 1},
        }
        for name, pins in expected.items():
            doc = self.load(name)
            actual = {p['name']: p['number'] for p in doc['pins']}
            with self.subTest(name=name):
                for pin, number in pins.items():
                    self.assertEqual(actual[pin], number)

    def test_all_named_parts_have_unique_identity_numbers_and_tests(self):
        identities = set()
        count = 0
        for file in (ROOT / 'catalog/components').glob('*/component.json'):
            doc = json.loads(file.read_text())
            if 'part' not in doc:
                continue
            count += 1
            identity = tuple(doc['part'][k] for k in ('manufacturer', 'mpn', 'package'))
            self.assertNotIn(identity, identities)
            identities.add(identity)
            numbers = [p['number'] for p in doc['pins']]
            self.assertEqual(len(numbers), len(set(numbers)))
            fixtures = [p for pattern in doc['tests'] for p in file.parent.glob(pattern)]
            self.assertGreaterEqual(len(fixtures), 1)
            self.assertTrue(doc['part']['datasheet'].startswith('https://'))
        self.assertGreaterEqual(count, 26)

    def test_converter_keeps_inputs_selectors_and_original_fixture(self):
        fixture = json.loads((ROOT / 'catalog/components/tmp36gt9z/tests/temperature-minus40.json').read_text())
        before = copy.deepcopy(fixture)
        project, scenario = convert(fixture)
        self.assertEqual(fixture, before)
        self.assertEqual(scenario['inputs'][0]['value'], -40)
        self.assertEqual(scenario['assertions'][0]['at'], '50us')
        self.assertEqual(project['probes'][0]['kind'], 'analog')
        self.assertEqual(project['probes'][0]['rate_hz'], 1_000_000)
        self.assertEqual(scenario['assertions'][-1]['id'], 'solver-health')

    def test_electrical_frequency_is_sampled_as_voltage(self):
        fixture = json.loads((ROOT / 'catalog/components/ne555p/tests/astable.json').read_text())
        project, scenario = convert(fixture)
        self.assertEqual(project['probes'][0]['kind'], 'analog')
        self.assertEqual(scenario['assertions'][0]['between'], ['500us', '3ms'])


if __name__ == '__main__':
    unittest.main()
