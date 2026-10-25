"""Exercise the native log checker with independent synthetic records."""
import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import test_mhwi_equipmentmorph_weight_log as checker


class WeightLogCheckTests(unittest.TestCase):
    def setUp(self):
        parent = Path(__file__).resolve().parents[1] / '.build/weight-log-check-tests'
        parent.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.path = self.root / '.build/cleanup-validation/native/weight-log-regression.ndjson'
        self.path.parent.mkdir(parents=True)

    def records(self, count=1024):
        smallest = struct.unpack('<f', struct.pack('<I', 1))[0]
        return [
            dict(requested=[.13724681] * count, applied=[.13724681] * count,
                 replacements=18446744073709551614),
            dict(requested=[smallest] * count, applied=[smallest] * count),
            dict(weights=[smallest] * count, payload_sha256='f' * 64),
            dict(requested=[0], applied=[0]),
            dict(weights=[0] * count),
            dict(weights=[0]),
        ]

    def write(self, records=None, boundary_size=65536, marker='after_rejected_record'):
        records = self.records() if records is None else records
        lines = [json.dumps(item, separators=(',', ':')).encode('ascii') + b'\n'
                 for item in records]
        prefix = b'{"event":"record_boundary","padding":"'
        suffix = b'"}\n'
        lines.append(prefix + b'x' * (boundary_size - len(prefix) - len(suffix)) + suffix)
        lines.append(json.dumps(dict(event=marker)).encode('ascii') + b'\n')
        self.path.write_bytes(b''.join(lines))

    def check(self):
        with patch.object(checker, 'ROOT', self.root), contextlib.redirect_stdout(io.StringIO()):
            checker.main()

    def test_accepts_complete_1024_key_native_fixture(self):
        self.write()
        self.check()
        report = json.loads((self.root / '.build/current-validation/weight-log-checks.json').read_bytes())
        self.assertEqual(report['json_records'], 8)
        self.assertTrue(report['weights_round_trip_float32'])
        self.assertEqual(report['record_bytes'][6], 65536)

    def test_rejects_consistently_wrong_multi_key_counts(self):
        for count in (64, 489, 1023, 1025):
            with self.subTest(count=count):
                self.write(self.records(count))
                with self.assertRaisesRegex(ValueError, 'Incomplete or changed native weights'):
                    self.check()

    def test_rejects_truncation_of_one_array(self):
        records = self.records()
        records[0]['applied'].pop()
        self.write(records)
        with self.assertRaisesRegex(ValueError, 'Incomplete or changed native weights'):
            self.check()

    def test_rejects_changed_decimal_subnormal_and_zero_values(self):
        for index, key, value in ((0, 'requested', .137), (1, 'applied', 0),
                                  (2, 'weights', 0), (4, 'weights', .5)):
            with self.subTest(index=index, key=key):
                records = self.records()
                records[index][key][-1] = value
                self.write(records)
                with self.assertRaisesRegex(ValueError, 'Incomplete or changed native weights'):
                    self.check()

    def test_rejects_wrong_single_key_records(self):
        for index, key in ((3, 'requested'), (3, 'applied'), (5, 'weights')):
            with self.subTest(index=index, key=key):
                records = self.records()
                records[index][key].append(0)
                self.write(records)
                with self.assertRaisesRegex(ValueError, 'Single-key record differs'):
                    self.check()

    def test_rejects_changed_tail_fields(self):
        for index, key, value in ((0, 'replacements', 0), (2, 'payload_sha256', '0' * 64)):
            with self.subTest(key=key):
                records = self.records()
                records[index][key] = value
                self.write(records)
                with self.assertRaisesRegex(ValueError, 'Tail fields differ'):
                    self.check()

    def test_rejects_incomplete_record_stream(self):
        self.write()
        complete = self.path.read_bytes()
        for broken in (complete[:-1], complete + b'{}\n'):
            with self.subTest(bytes=len(broken)):
                self.path.write_bytes(broken)
                with self.assertRaisesRegex(ValueError, 'Expected eight complete records'):
                    self.check()

    def test_rejects_changed_boundary_and_recovery_marker(self):
        for size, marker in ((65535, 'after_rejected_record'), (65537, 'after_rejected_record'),
                             (65536, 'wrong_event')):
            with self.subTest(size=size, marker=marker):
                self.write(boundary_size=size, marker=marker)
                with self.assertRaisesRegex(ValueError, 'Boundary/rejection differs'):
                    self.check()


if __name__ == '__main__':
    unittest.main()
