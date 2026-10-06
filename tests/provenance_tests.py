"""Provenance must survive compiler rounding without masking changed inputs."""
import copy
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from export_aircraft_provenance import provenance_difference


class ProvenanceComparisonTests(unittest.TestCase):
    def setUp(self):
        self.record = {'schema_version': 1, 'parameters': [
            {'parameter': 'ixx', 'values': 123456.789, 'revision': 'reviewed-source'},
            {'parameter': 'position', 'values': [1.0, 0.0, -2.0], 'unit': 'm'},
        ]}

    def test_independent_compiler_rounding(self):
        actual = copy.deepcopy(self.record)
        actual['parameters'][0]['values'] = math.nextafter(123456.789, math.inf)
        actual['parameters'][1]['values'][1] = 1e-15
        self.assertIsNone(provenance_difference(self.record, actual))

    def test_changed_flight_value_reports_its_location(self):
        actual = copy.deepcopy(self.record)
        actual['parameters'][0]['values'] += 0.01
        self.assertIn('parameters[0].values', provenance_difference(self.record, actual))

    def test_provenance_and_structure_remain_exact(self):
        changes = [
            lambda r: r['parameters'][0].update(revision='different-source'),
            lambda r: r['parameters'][1]['values'].append(0.0),
            lambda r: r.update(schema_version=1.0000000000001),
            lambda r: r['parameters'][0].update(values=True),
            lambda r: r['parameters'][0].update(values=math.nan),
            lambda r: r['parameters'][0].update(values=math.inf),
            lambda r: r['parameters'][1].pop('unit'),
        ]
        for change in changes:
            with self.subTest(change=change):
                actual = copy.deepcopy(self.record)
                change(actual)
                self.assertIsNotNone(provenance_difference(self.record, actual))


if __name__ == '__main__':
    unittest.main()
