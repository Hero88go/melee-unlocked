"""Compare native and translated per-frame state CSVs.

A row is one completed game frame. Values such as floats and RNG state should
be emitted as fixed-width hexadecimal words by both producers, so comparison
is exact and the first divergent field identifies the subsystem to inspect.
"""
import argparse
import csv
import sys
from pathlib import Path


def read_rows(path):
    with Path(path).open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or "frame" not in reader.fieldnames:
            raise ValueError(f"{path}: CSV needs a frame column")
        if len(reader.fieldnames) != len(set(reader.fieldnames)):
            raise ValueError(f"{path}: duplicate CSV columns")
        rows = []
        previous = -1
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"{path}: malformed row at line {reader.line_num}")
            frame = int(row["frame"])
            if frame <= previous:
                raise ValueError(f"{path}: frame {frame} repeats or goes backward")
            rows.append(row)
            previous = frame
        return reader.fieldnames, rows


def compare(left, right):
    left_fields, left_rows = read_rows(left)
    right_fields, right_rows = read_rows(right)
    if set(left_fields) != set(right_fields):
        missing = sorted(set(left_fields) - set(right_fields))
        extra = sorted(set(right_fields) - set(left_fields))
        print(f"columns differ: only in {left}: {missing}; only in {right}: {extra}")
        return False
    for index, (a, b) in enumerate(zip(left_rows, right_rows)):
        if a["frame"] != b["frame"]:
            print(f"first divergence at row {index + 1}: frame {a['frame']} vs {b['frame']}")
            return False
        for field in left_fields:
            if a[field] != b[field]:
                print(f"first divergence at frame {a['frame']}, field {field}: "
                      f"{left}={a[field]} {right}={b[field]}")
                return False
    if len(left_rows) != len(right_rows):
        print(f"trace length differs: {left}={len(left_rows)} frames, "
              f"{right}={len(right_rows)} frames")
        return False
    print(f"{len(left_rows)} frames match in {len(left_fields) - 1} state fields")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", help="native --state-digest CSV")
    parser.add_argument("translated", help="translated --state-digest CSV")
    args = parser.parse_args()
    try:
        return 0 if compare(args.native, args.translated) else 1
    except (OSError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
