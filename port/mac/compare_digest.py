"""Compare headerless decimal Linux and/or named hex host digests by match_frame."""
from pathlib import Path
import argparse
import csv
import json
from convert_digest import RAW


def load(path):
    with Path(path).open(newline='') as source:
        rows = list(csv.reader(source))
    if not rows:
        raise ValueError(f'Empty digest: {path}')
    named = rows[0][0] == 'frame'
    fields = rows.pop(0) if named else RAW
    if set(fields) != set(RAW):
        raise ValueError(f'Unexpected digest fields: {path}')
    values = []
    for row in rows:
        if len(row) != len(fields):
            raise ValueError(f'Unexpected digest row width: {path}')
        values.append({key: int(value, 10 if not named or key == 'frame' else 16)
                       for key, value in zip(fields, row)})
    match = {}
    for row in values:
        if row['match_frame'] > 0:
            match.setdefault(row['match_frame'], row)
    return values, match


def compare(reference, observed, require_reference=True):
    ref_rows, ref = load(reference)
    got_rows, got = load(observed)
    keys = sorted(ref.keys() & got.keys())
    fields = [field for field in RAW if field != 'frame']
    examples = []
    differences = 0
    for frame in keys:
        for field in fields:
            if ref[frame][field] != got[frame][field]:
                differences += 1
                if len(examples) < 10:
                    examples.append([frame, field, ref[frame][field], got[frame][field]])
    passed = bool(keys) and differences == 0 and (not require_reference or len(keys) == len(ref))
    return dict(reference_rows=len(ref_rows), observed_rows=len(got_rows),
                reference_match_frames=len(ref), observed_match_frames=len(got),
                compared_frames=len(keys), fields_per_frame=len(fields), differing_values=differences,
                missing_reference_frames=len(ref.keys() - got.keys()), first_differences=examples,
                passed=passed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference')
    parser.add_argument('observed')
    parser.add_argument('--allow-partial', action='store_true')
    args = parser.parse_args()
    result = compare(args.reference, args.observed, not args.allow_partial)
    print(json.dumps(result, indent=2))
    return int(not result['passed'])


if __name__ == '__main__':
    raise SystemExit(main())
