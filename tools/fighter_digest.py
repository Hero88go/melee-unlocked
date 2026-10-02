#!/usr/bin/env python3
"""Strict comparison of declared Fighter scalar fields at Slippi post-frame points.

This v1 gate proves only the schema's stated coverage. It does not establish full
fighter, item or stage equivalence. See docs/replay-validation.md.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

from replay_compare import parse_slp

FORMAT = "melee-fighter-digest"
WIDTHS = {"u8": 1, "i8": 1, "u16": 2, "i16": 2, "u32": 4, "i32": 4,
          "u64": 8, "i64": 8, "f32": 4, "f64": 8}
HASH_PATTERN = re.compile(r"[0-9a-f]{64}\Z")


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_schema(path, native_debug, field_map, extension=False):
    schema = json.loads(Path(path).read_text(encoding="utf-8"))
    validate_schema(schema, extension)
    provenance = schema["provenance"]
    # The schema names the one game library it was reviewed against. Every native offset is
    # still proven against the supplied field map below; the two hashes tie that map and the
    # debug artifact to the schema, so another library needs its own reviewed schema copy.
    if sha256_file(native_debug) != provenance["native_debug_sha256"]:
        raise ValueError("schema native debug SHA-256 does not match the supplied debug artifact")
    if sha256_file(field_map) != provenance["native_field_map_sha256"]:
        raise ValueError("schema native field-map SHA-256 does not match the supplied map")
    mapping = json.loads(Path(field_map).read_text(encoding="utf-8"))
    if mapping["native_size"] != schema["record_sizes"]["native"]:
        raise ValueError("schema native record size disagrees with its debugger map")
    rows = {row["path"]: row for row in mapping["fields"]}
    for field in schema["fields"]:
        row = rows.get(field["path"])
        expected_kind = "flt" if field["type"].startswith("f") else "int"
        if (not row or row["kind"] != expected_kind or
                row["noff"] != field["native_offset"] or
                row["nsize"] != WIDTHS[field["type"]]):
            raise ValueError(f"schema native scalar mapping is unverified: {field['path']}")
    for name, path_name in (("kind_offsets", "fp.kind"), ("player_offsets", "fp.player_idx")):
        if schema[name]["native"] != rows[path_name]["noff"]:
            raise ValueError(f"schema {name} disagrees with the native map")
    return schema


def validate_schema(schema, extension=False):
    if not isinstance(schema, dict) or schema.get("format") != FORMAT or schema.get("version") != 1:
        raise ValueError("unsupported Fighter digest schema format/version")
    mode = "active_variables" if extension else "core"
    if schema.get("mode") != mode or not schema.get("scope") or not schema.get("exclusions"):
        raise ValueError("schema needs explicit mode, coverage scope and exclusions")
    provenance = schema.get("provenance", {})
    if not isinstance(provenance, dict):
        raise ValueError("schema provenance must be an object")
    for name in ("native_debug_sha256", "native_field_map_sha256"):
        if not isinstance(provenance.get(name), str) or not HASH_PATTERN.fullmatch(provenance[name]):
            raise ValueError(f"schema lacks valid {name} provenance")
    sizes = schema.get("record_sizes", {})
    if not isinstance(sizes, dict):
        raise ValueError("schema record sizes must be an object")
    for layout in ("console", "native"):
        if type(sizes.get(layout)) is not int or not 0 < sizes[layout] <= 65535:
            raise ValueError(f"invalid {layout} Fighter record size")
        for name, width in (("kind_offsets", 4), ("player_offsets", 1)):
            offsets = schema.get(name, {})
            if not isinstance(offsets, dict):
                raise ValueError(f"schema {name} must be an object")
            offset = offsets.get(layout)
            if type(offset) is not int or offset < 0 or offset + width > sizes[layout]:
                raise ValueError(f"invalid {layout} {name}")
    fields = schema.get("fields")
    if not isinstance(fields, list) or not fields:
        raise ValueError("schema has no scalar fields")
    seen_names, used = set(), {"console": set(), "native": set()}
    for field in fields:
        if not isinstance(field, dict):
            raise ValueError("schema scalar fields must be objects")
        name, kind = field.get("path"), field.get("type")
        if not isinstance(name, str) or not name or name in seen_names or kind not in WIDTHS:
            raise ValueError("duplicate, unnamed or untyped schema field")
        if not extension and (name.startswith("fp.u.") or "<anon>" in name):
            raise ValueError("core schema cannot include union or anonymous bitfield views")
        seen_names.add(name)
        for layout in ("console", "native"):
            offset = field.get(layout + "_offset")
            width = WIDTHS[kind]
            if type(offset) is not int or offset < 0 or offset + width > sizes[layout]:
                raise ValueError(f"field {name} extends outside the {layout} record")
            span = set(range(offset, offset + width))
            if used[layout] & span:
                raise ValueError(f"overlapping {layout} scalar fields in schema")
            used[layout].update(span)
    if extension:
        if not isinstance(schema.get("name"), str) or not schema["name"]:
            raise ValueError("extension schema needs a unique name")
        selectors = schema.get("kinds", {})
        if not isinstance(selectors, dict):
            raise ValueError("extension kind selectors must be an object")
        for layout in ("console", "native"):
            kinds = selectors.get(layout)
            if (not isinstance(kinds, list) or not kinds or
                    any(type(kind) is not int or not 0 <= kind <= 0xFFFFFFFF for kind in kinds)
                    or len(kinds) != len(set(kinds))):
                raise ValueError("active-variable extension needs explicit kinds for both layouts")


def validate_extensions(core, extensions):
    names = set()
    for extension in extensions:
        validate_schema(extension, extension=True)
        if extension["name"] in names:
            raise ValueError("duplicate extension schema name")
        names.add(extension["name"])
        for key in ("provenance", "record_sizes", "kind_offsets", "player_offsets"):
            if extension[key] != core[key]:
                raise ValueError(f"extension {extension['name']} does not share core {key}")
        if {f["path"] for f in extension["fields"]} & {f["path"] for f in core["fields"]}:
            raise ValueError("extension repeats a core scalar field")


def canonical_fields(payload, layout, core, extensions=()):
    """Use declared element width and one declared endian conversion, never float arithmetic."""
    order = "big" if layout == "console" else "little"
    offset = core["kind_offsets"][layout]
    kind = int.from_bytes(payload[offset:offset + 4], order)
    selected = [("core", core)]
    selected += [(extension["name"], extension) for extension in extensions
                 if kind in extension["kinds"][layout]]
    values = {}
    for label, schema in selected:
        for field in schema["fields"]:
            offset, width = field[layout + "_offset"], WIDTHS[field["type"]]
            raw = payload[offset:offset + width]
            if len(raw) != width:
                raise ValueError("Fighter payload does not cover a declared scalar")
            values[label + ":" + field["path"]] = raw if layout == "console" else raw[::-1]
    return values


def read_records(path, layout, schema, extensions=()):
    """Read only known Fighter and bone-companion records; reject partial or duplicate keys."""
    if layout not in ("console", "native"):
        raise ValueError("unknown Fighter layout")
    fighters, bones = {}, set()
    with Path(path).open("rb") as stream:
        while True:
            header = stream.read(8)
            if not header:
                break
            if len(header) != 8:
                raise ValueError("truncated Fighter dump header")
            frame, raw_port, follower, size = struct.unpack("<iBBH", header)
            port, bone = raw_port & 0x7F, bool(raw_port & 0x80)
            if port > 3 or follower > 1:
                raise ValueError("unknown Fighter dump entity record")
            key = (frame, port, follower)
            if bone:
                if not 0 < size <= 96 * 22 * 4 or size % (22 * 4):
                    raise ValueError("wrong declared bone-companion record size")
                if key not in fighters or key in bones:
                    raise ValueError("orphan or duplicate bone-companion record")
                bones.add(key)
            else:
                if size != schema["record_sizes"][layout]:
                    raise ValueError(f"wrong declared {layout} Fighter record size: {size}")
                if key in fighters:
                    raise ValueError("duplicate Fighter frame/player/follower record")
            payload = stream.read(size)
            if len(payload) != size:
                raise ValueError("truncated Fighter dump payload")
            if not bone:
                if payload[schema["player_offsets"][layout]] != port:
                    raise ValueError("Fighter player_idx disagrees with its record header")
                fighters[key] = canonical_fields(payload, layout, schema, extensions)
    if not fighters or not any(key[0] == 0 for key in fighters):
        raise ValueError("Fighter dump is empty or contains no frame zero")
    return fighters


def expected_keys(replay, first=0, last=None):
    _, posts, _ = parse_slp(replay)
    if not posts:
        raise ValueError("expected replay has no post-frame events")
    if last is None:
        last = max(posts)
    if first > 0 or last < 0 or first > last:
        raise ValueError("requested digest interval must include frame zero")
    expected = set()
    for frame in range(first, last + 1):
        if frame not in posts or not posts[frame]:
            raise ValueError(f"expected replay lacks player coverage at frame {frame}")
        expected.update((frame, port, follower) for port, follower in posts[frame])
    return expected


def frame_digest(frame, records, schema_hash):
    digest = hashlib.sha256(bytes.fromhex(schema_hash) + struct.pack(">i", frame))
    for key in sorted(records):
        if key[0] != frame:
            continue
        digest.update(bytes(key[1:]))
        for name, raw in records[key].items():
            label = name.encode("utf-8")
            digest.update(struct.pack(">H", len(label)) + label)
            digest.update(struct.pack(">B", len(raw)) + raw)
    return digest.hexdigest()


def compare_records(console, native, expected, schema_hash):
    if not expected or not any(key[0] == 0 for key in expected):
        raise ValueError("expected Fighter coverage is empty or lacks frame zero")
    for layout, records in (("console", console), ("native", native)):
        missing, unknown = expected - records.keys(), records.keys() - expected
        if missing or unknown:
            raise ValueError(f"{layout} coverage mismatch: missing {len(missing)}, "
                             f"unexpected {len(unknown)}; first "
                             f"{min(missing or unknown)}")
    first_difference = None
    for key in sorted(expected):
        a, b = console[key], native[key]
        if not a or not b:
            raise ValueError("Fighter record has no declared scalar coverage")
        if list(a) != list(b):
            first_difference = dict(frame=key[0], port=key[1], follower=key[2],
                                    field="active_schema_fields", console=list(a), native=list(b))
            break
        for field in a:
            if a[field] != b[field]:
                first_difference = dict(frame=key[0], port=key[1], follower=key[2],
                                        field=field, console=a[field].hex(), native=b[field].hex())
                break
        if first_difference:
            break
    frames = sorted({key[0] for key in expected})
    grouped = {"console": {frame: {} for frame in frames},
               "native": {frame: {} for frame in frames}}
    for layout, records in (("console", console), ("native", native)):
        for key, values in records.items():
            grouped[layout][key[0]][key] = values
    per_frame = [dict(frame=frame,
                      console=frame_digest(frame, grouped["console"][frame], schema_hash),
                      native=frame_digest(frame, grouped["native"][frame], schema_hash))
                 for frame in frames]
    return dict(gate_pass=first_difference is None, frames=len(frames),
                player_frames=len(expected), first_difference=first_difference,
                per_frame_digests=per_frame)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("console", type=Path)
    ap.add_argument("native", type=Path)
    ap.add_argument("--schema", type=Path, required=True)
    ap.add_argument("--native-debug", type=Path, required=True)
    ap.add_argument("--field-map", type=Path, required=True)
    ap.add_argument("--expected-replay", type=Path, required=True)
    ap.add_argument("--first", type=int, default=0)
    ap.add_argument("--last", type=int)
    ap.add_argument("--extension-schema", type=Path, action="append", default=[])
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args(argv)
    inputs = [args.console, args.native, args.schema, args.native_debug, args.field_map,
              args.expected_replay, *args.extension_schema]
    if args.out.resolve() in {path.resolve() for path in inputs}:
        ap.error("--out must differ from every input artifact")
    result = dict(status="error", gate_pass=False, scope="declared scalar schema only")
    code = 2
    try:
        core = load_schema(args.schema, args.native_debug, args.field_map)
        extensions = [load_schema(path, args.native_debug, args.field_map, extension=True)
                      for path in args.extension_schema]
        validate_extensions(core, extensions)
        expected = expected_keys(args.expected_replay, args.first, args.last)
        schema_hash = hashlib.sha256(json.dumps([core, *extensions], sort_keys=True,
                                               separators=(",", ":")).encode()).hexdigest()
        console = read_records(args.console, "console", core, extensions)
        native = read_records(args.native, "native", core, extensions)
        result.update(compare_records(console, native, expected, schema_hash))
        result.update(scope=core["scope"], exclusions=core["exclusions"],
                      schema_sha256=schema_hash, provenance=core["provenance"],
                      extensions=[schema["name"] for schema in extensions],
                      inputs=[dict(path=str(path.resolve()), sha256=sha256_file(path))
                              for path in (args.console, args.native, args.expected_replay, args.schema)])
        result["status"] = "ok" if result["gate_pass"] else "mismatch"
        code = 0 if result["gate_pass"] else 1
    except (OSError, ValueError, KeyError, TypeError, struct.error) as exc:
        result["error"] = str(exc)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"{'PASS' if result['gate_pass'] else 'FAIL'}: {result.get('frames', 0)} frames, "
          f"{result.get('player_frames', 0)} player-frames; declared scalar coverage only")
    if result.get("error") or result.get("first_difference"):
        print(result.get("error") or result["first_difference"])
    return code


if __name__ == "__main__":
    sys.exit(main())
