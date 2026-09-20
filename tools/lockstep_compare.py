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


def align_on_match_frame(label, fields, rows):
    """Drop every row before the trace first enters a match (match_frame != 0), then key the
    remaining rows by match_frame instead of by retrace. Two builds that boot, load menus and
    enter the same match at different retrace counts still align this way, as long as each
    reaches match_frame 1 for the same in-match frame. Requires the match_frame column (added
    to MuStatePod alongside scene_major); older traces cannot use --align match."""
    if "match_frame" not in fields:
        raise ValueError(f"{label}: no match_frame column, cannot --align match "
                          f"(re-run with a build that emits it)")
    by_match_frame = {}
    started = False
    for row in rows:
        match_frame = int(row["match_frame"], 16)
        if not started:
            if match_frame == 0:
                continue
            started = True
        if match_frame == 0:
            # A match_frame that drops back to 0 after the match started (match over, back to
            # menus) ends the alignable region; later rows belong to whatever comes next.
            break
        if match_frame in by_match_frame:
            raise ValueError(f"{label}: match_frame {match_frame} repeats; "
                              f"the trace may span more than one match")
        by_match_frame[match_frame] = row
    if not by_match_frame:
        raise ValueError(f"{label}: match_frame never left zero, nothing to align")
    return by_match_frame


def compare(left, right, align=None):
    left_fields, left_rows = read_rows(left)
    right_fields, right_rows = read_rows(right)
    if set(left_fields) != set(right_fields):
        missing = sorted(set(left_fields) - set(right_fields))
        extra = sorted(set(right_fields) - set(left_fields))
        print(f"columns differ: only in {left}: {missing}; only in {right}: {extra}")
        return False
    compared_fields = [f for f in left_fields if f not in ("frame", "match_frame")]

    if align == "match":
        left_by_mf = align_on_match_frame(left, left_fields, left_rows)
        right_by_mf = align_on_match_frame(right, right_fields, right_rows)
        common = sorted(set(left_by_mf) & set(right_by_mf))
        if not common:
            print(f"no overlapping match_frame values between {left} and {right}")
            return False
        for mf in common:
            a, b = left_by_mf[mf], right_by_mf[mf]
            for field in compared_fields:
                if a[field] != b[field]:
                    print(f"first divergence at match_frame {mf}, field {field}: "
                          f"{left}={a[field]} (retrace {a['frame']}) "
                          f"{right}={b[field]} (retrace {b['frame']})")
                    return False
        only_left = sorted(set(left_by_mf) - set(right_by_mf))
        only_right = sorted(set(right_by_mf) - set(left_by_mf))
        if only_left or only_right:
            print(f"match length differs: {left} reaches match_frame {max(left_by_mf)}, "
                  f"{right} reaches match_frame {max(right_by_mf)} "
                  f"({len(common)} frames compared exactly)")
            return False
        print(f"{len(common)} match frames match in {len(compared_fields)} state fields "
              f"(aligned on match_frame)")
        return True

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
    parser.add_argument("--align", choices=["match"], default=None,
                         help="match: key rows by match_frame instead of retrace, so traces "
                              "that enter the same VS match at different retrace counts still "
                              "compare exactly from the first in-match frame")
    args = parser.parse_args()
    try:
        return 0 if compare(args.native, args.translated, align=args.align) else 1
    except (OSError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
