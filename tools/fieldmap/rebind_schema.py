#!/usr/bin/env python3
"""Bind a Fighter digest schema to another game library, after proving it still fits.

tools/fighter_digest.py only accepts a schema whose provenance names the exact native debug
artifact and field map it is used with. A different build of the game library (for example the
experimental one with the added fighters) therefore needs its own schema file. This tool writes
that copy, but only when the new library's field map proves every declared field unchanged: the
same native offset, width and scalar kind, the same native record size, and the same kind and
player index offsets. Anything else is an error: then the schema needs a human review, not a
copy.

    py -3.12 tools/fieldmap/rebind_schema.py --schema tools/fieldmap/fighter_core_v1.json \
        --native-debug build-sourceport-gcc-ak/melee_game.dbg \
        --field-map run-source/rel09-b1-wolf/digest/fighter_map-ak.json \
        --out tools/fieldmap/fighter_core_ak_v1.json

The field map comes from tools/fieldmap/gdb_fighter_map.py run on that same debug artifact.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

WIDTHS = {"u8": 1, "i8": 1, "u16": 2, "i16": 2, "u32": 4, "i32": 4,
          "u64": 8, "i64": 8, "f32": 4, "f64": 8}


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--schema", type=Path, required=True)
    ap.add_argument("--native-debug", type=Path, required=True)
    ap.add_argument("--field-map", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args(argv)
    if args.out.resolve() == args.schema.resolve():
        ap.error("--out must differ from --schema: the source schema is never edited in place")

    schema = json.loads(args.schema.read_text(encoding="utf-8"))
    mapping = json.loads(args.field_map.read_text(encoding="utf-8"))
    problems = []
    if mapping["native_size"] != schema["record_sizes"]["native"]:
        problems.append(f"native Fighter size is {mapping['native_size']}, the schema says "
                        f"{schema['record_sizes']['native']}")
    rows = {row["path"]: row for row in mapping["fields"]}
    for field in schema["fields"]:
        row = rows.get(field["path"])
        kind = "flt" if field["type"].startswith("f") else "int"
        if not row:
            problems.append(f"{field['path']}: not in the field map")
        elif (row["kind"] != kind or row["noff"] != field["native_offset"] or
              row["nsize"] != WIDTHS[field["type"]]):
            problems.append(f"{field['path']}: map has offset {row['noff']} size {row['nsize']} "
                            f"kind {row['kind']}, schema has offset {field['native_offset']} "
                            f"type {field['type']}")
    for name, path_name in (("kind_offsets", "fp.kind"), ("player_offsets", "fp.player_idx")):
        row = rows.get(path_name)
        if not row or row["noff"] != schema[name]["native"]:
            problems.append(f"{name}: {path_name} is not at native offset "
                            f"{schema[name]['native']}")
    if problems:
        print("NOT rebound: this library's layout differs from the schema:")
        for line in problems:
            print("  " + line)
        return 1

    old = dict(schema["provenance"])
    schema["provenance"]["native_debug_sha256"] = sha256_file(args.native_debug)
    schema["provenance"]["native_field_map_sha256"] = sha256_file(args.field_map)
    schema["provenance"]["rebound_from"] = {
        "schema": args.schema.name,
        "schema_sha256": sha256_file(args.schema),
        "native_debug_sha256": old["native_debug_sha256"],
        "native_field_map_sha256": old["native_field_map_sha256"],
        "note": "Every declared field, the record size and the kind and player offsets were "
                "checked equal against the new library's field map by rebind_schema.py.",
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(schema, indent=2) + "\n", encoding="utf-8")
    print(f"rebound {len(schema['fields'])} fields to {args.native_debug.name} "
          f"({schema['provenance']['native_debug_sha256'][:12]}): {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
