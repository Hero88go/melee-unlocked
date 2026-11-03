"""Parity comparison regressions: mixed formats, numeric equality, and strict coverage."""
import csv
from pathlib import Path
import tempfile
import unittest
from compare_digest import RAW, compare
from convert_digest import convert


class DigestComparisonTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        self.reference = self.root/'linux.txt'
        self.observed = self.root/'mac.csv'
        self.rows = []
        for frame in range(3):
            row = dict.fromkeys(RAW, 0)
            row.update(frame=frame+100, match_frame=frame, rng=0x1234ABCD, p0_pos_x=0xBF800000)
            self.rows.append(row)
        self.write_raw(self.reference, self.rows)
        convert(self.reference, self.observed)

    def tearDown(self):
        self.scratch.cleanup()

    def write_raw(self, path, rows):
        with path.open('w', newline='') as target:
            csv.writer(target).writerows([[row[key] for key in RAW] for row in rows])

    def test_mixed_formats_align_independent_host_frames(self):
        text = self.observed.read_text().replace('101,', '201,').replace('102,', '202,').lower()
        self.observed.write_text(text)
        result = compare(self.reference, self.observed)
        self.assertTrue(result['passed'])
        self.assertEqual((result['compared_frames'], result['fields_per_frame'], result['differing_values']), (2,76,0))

    def test_changed_value_is_counted_and_fails(self):
        self.rows[1]['p0_pos_x'] ^= 1
        self.write_raw(self.observed, self.rows)
        result = compare(self.reference, self.observed)
        self.assertFalse(result['passed'])
        self.assertEqual(result['differing_values'], 1)
        self.assertEqual(result['first_differences'][0][1], 'p0_pos_x')

    def test_missing_reference_frames_fail(self):
        self.write_raw(self.observed, self.rows[:2])
        self.assertFalse(compare(self.reference, self.observed)['passed'])
        self.assertTrue(compare(self.reference, self.observed, require_reference=False)['passed'])

    def test_no_match_frames_fail(self):
        self.write_raw(self.observed, self.rows[:1])
        self.assertFalse(compare(self.reference, self.observed)['passed'])

    def test_malformed_row_is_rejected(self):
        self.observed.write_text('1,2\n')
        with self.assertRaises(ValueError):
            compare(self.reference, self.observed)


if __name__ == '__main__':
    unittest.main()
