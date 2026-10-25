"""Parse native regression logs and check complete float32 round trips."""
import json
from pathlib import Path
import struct
ROOT = Path(__file__).resolve().parents[1]
# Matches the manyKeys fixture in diagnostics/EquipmentMorphDiskContract/tests.cpp.
LOGGED_KEYS = 1024


def main():
    lines = (ROOT / '.build/cleanup-validation/native/weight-log-regression.ndjson').read_bytes().splitlines(keepends=True)
    if len(lines) != 8 or not all(line.endswith(b'\n') for line in lines):raise ValueError('Expected eight complete records')
    records = [json.loads(line) for line in lines]
    for index, keys, expected in [(0, ('requested', 'applied'), struct.pack('<f', .13724681)),
                                  (1, ('requested', 'applied'), struct.pack('<I', 1)),
                                  (2, ('weights',), struct.pack('<I', 1)), (4, ('weights',), struct.pack('<f', 0))]:
        for key in keys:
            if len(records[index][key]) != LOGGED_KEYS or any(struct.pack('<f', v) != expected for v in records[index][key]):
                raise ValueError('Incomplete or changed native weights: ' + key)
    if len(records[3]['requested']) != 1 or len(records[3]['applied']) != 1 or len(records[5]['weights']) != 1:raise ValueError('Single-key record differs')
    if records[0]['replacements'] != 18446744073709551614 or records[2]['payload_sha256'] != 'f' * 64:raise ValueError('Tail fields differ')
    if len(lines[6]) != 65536 or records[-1]['event'] != 'after_rejected_record':raise ValueError('Boundary/rejection differs')
    report = {'schema': 'mhwi-weight-log-regression-v1', 'json_records': len(records),
              'record_bytes': [len(line) for line in lines], 'weights_round_trip_float32': True}
    output = ROOT / '.build/current-validation';output.mkdir(parents=True, exist_ok=True)
    (output / 'weight-log-checks.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS: 8 JSON log records; complete float32 weights')


if __name__ == '__main__':main()
