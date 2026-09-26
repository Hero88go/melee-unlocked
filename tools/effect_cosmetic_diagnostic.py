#!/usr/bin/env python3
"""Validate and materialize project effect DATs against an exact clean ISO resource."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from cosmetic_import_diagnostic import InvalidImport
from stage_cosmetic_diagnostic import compare_stage_dat, dat_layout

PARTICLE_SIGNATURES = (
    (0x0323, 0x0521, 0x0523, 0x0525, 0x0523, 0x0526, 0x0522, 0x0520,
     0x0522, 0x051F, 0x0521, 0x0524, 0x0525, 0x0527, 0x0526, 0x0527,
     0x0520, 0x051E, 0x051F, 0x051E, 0x0524, 0x0527, 0x0500),
    (0x1516, 0x151A, 0x1515, 0x151A, 0x151B, 0x151A, 0x151D, 0x1514,
     0x1517, 0x1516, 0x1518, 0x1515, 0x1519, 0x151B, 0x1519, 0x151C,
     0x1518, 0x151C, 0x1517, 0x151C, 0x151D, 0x151B, 0x1500),
    (0x2F0B, 0x2F10, 0x2F0D, 0x2F10, 0x2F11, 0x2F10, 0x2F13, 0x2F0A,
     0x2F0C, 0x2F0B, 0x2F0E, 0x2F0D, 0x2F0F, 0x2F11, 0x2F0F, 0x2F12,
     0x2F0E, 0x2F12, 0x2F0C, 0x2F12, 0x2F13, 0x2F11, 0x2F00),
)
DEDICATED_EFFECT_ROOTS = {
    "effxdata.dat": "effFoxDataTable",
}
TEXTURE_ONLY_EFFECT_TARGETS = {"efcodata.dat"}
FIGHTER_EFFECT_ROOTS = {
    "plfx.dat": "ftDataFox",
    "plfc.dat": "ftDataFalco",
}
FOX_SIDE_COLOR = bytes.fromhex("0099ffffcce6")


def _require_root(layout: dict, expected: str, label: str) -> None:
    if layout["roots"] != [expected]:
        raise InvalidImport(f"{label} DAT root is not exactly {expected}")


def _find_particle_stream(data: bytes, layout: dict, signature: tuple[int, ...], label: str) -> int:
    block = memoryview(data)[0x20:0x20 + layout["data_size"]]
    size = len(signature) * 4
    matches = []
    for offset in range(0, len(block) - size + 1, 4):
        if all(struct.unpack_from(">H", block, offset + index * 4 + 2)[0] == value
               for index, value in enumerate(signature)):
            matches.append(offset)
    if len(matches) != 1:
        raise InvalidImport(
            f"{label} particle-color stream must occur exactly once; found {len(matches)}"
        )
    return matches[0]


def _stream_color(data: bytes, offset: int, records: int, label: str) -> int:
    colors = {struct.unpack_from(">H", data, 0x20 + offset + index * 4)[0]
              for index in range(records)}
    if len(colors) != 1:
        raise InvalidImport(f"{label} particle-color stream is not uniform")
    return colors.pop()


def _merge_particle_colors(target: str, clean: bytes, candidate: bytes,
                           base: dict, mod: dict) -> tuple[bytes, dict]:
    expected_root = FIGHTER_EFFECT_ROOTS[target]
    _require_root(base, expected_root, "clean")
    _require_root(mod, expected_root, "candidate")

    merged = bytearray(clean)
    candidate_colors = []
    clean_positions = []
    candidate_positions = []
    for index, signature in enumerate(PARTICLE_SIGNATURES, 1):
        clean_offset = _find_particle_stream(clean, base, signature, f"clean #{index}")
        candidate_offset = _find_particle_stream(candidate, mod, signature, f"candidate #{index}")
        if _stream_color(clean, clean_offset, len(signature), f"clean #{index}") != 0xFC00:
            raise InvalidImport("clean fighter DAT does not match the exact NTSC 1.02 color stream")
        candidate_colors.append(
            _stream_color(candidate, candidate_offset, len(signature), f"candidate #{index}")
        )
        clean_positions.append(clean_offset)
        candidate_positions.append(candidate_offset)
        for record in range(len(signature)):
            source = 0x20 + candidate_offset + record * 4
            destination = 0x20 + clean_offset + record * 4
            merged[destination:destination + 2] = candidate[source:source + 2]

    if len(set(candidate_colors)) != 1 or candidate_colors[0] == 0xFC00:
        raise InvalidImport("candidate particle-color streams do not contain one coherent new color")

    if target == "plfx.dat":
        if (len(clean) != len(candidate) or base["data_size"] != mod["data_size"] or
                base["relocations"] != mod["relocations"] or base["symbols"] != mod["symbols"] or
                clean[base["relocation_start"]:] != candidate[mod["relocation_start"]:]):
            raise InvalidImport("Fox fighter effect DAT changes layout, relocation, roots, or strings")
        block = clean[0x20:0x20 + base["data_size"]]
        if block.count(FOX_SIDE_COLOR) != 1:
            raise InvalidImport("clean Fox side-B color field is not unique")
        side_offset = block.find(FOX_SIDE_COLOR)
        merged[0x20 + side_offset:0x20 + side_offset + len(FOX_SIDE_COLOR)] = \
            candidate[0x20 + side_offset:0x20 + side_offset + len(FOX_SIDE_COLOR)]
        if bytes(merged) != candidate:
            raise InvalidImport("Fox fighter DAT changes bytes outside verified particle-color fields")

    output = bytes(merged)
    changed = sum(before != after for before, after in zip(clean, output))
    if not changed:
        raise InvalidImport("effect DAT does not change a verified particle color")
    return output, {
        "status": "particle_color_merge",
        "changed_bytes": changed,
        "particle_streams": len(PARTICLE_SIGNATURES),
        "candidate_color": f"0x{candidate_colors[0]:04x}",
        "candidate_repacked": len(clean) != len(candidate) or clean_positions != candidate_positions,
    }


def materialize_effect_dat(target_path: str, clean: bytes, candidate: bytes) -> tuple[bytes, dict]:
    """Return safe runtime bytes and a diagnostic classification for one effect candidate."""
    target = target_path.casefold()
    if (target not in DEDICATED_EFFECT_ROOTS and target not in FIGHTER_EFFECT_ROOTS and
            target not in TEXTURE_ONLY_EFFECT_TARGETS):
        raise InvalidImport(f"unsupported effect target: {target_path}")

    try:
        result = compare_stage_dat(clean, candidate)
    except (InvalidImport, OSError, ValueError, struct.error):
        result = None
    if result is not None:
        return candidate, result

    if target in TEXTURE_ONLY_EFFECT_TARGETS:
        raise InvalidImport("common effect DAT changes bytes outside validated texture payloads")

    base, mod = dat_layout(clean), dat_layout(candidate)
    if target in DEDICATED_EFFECT_ROOTS:
        expected_root = DEDICATED_EFFECT_ROOTS[target]
        _require_root(base, expected_root, "clean")
        _require_root(mod, expected_root, "candidate")
        if clean == candidate:
            raise InvalidImport("effect DAT is identical to the clean resource")
        return candidate, {
            "status": "dedicated_effect_archive",
            "changed_bytes": sum(before != after for before, after in zip(clean, candidate)) +
                             abs(len(clean) - len(candidate)),
            "image_descriptors": len(mod["images"]),
            "candidate_repacked": len(clean) != len(candidate) or
                                  base["relocations"] != mod["relocations"],
        }

    return _merge_particle_colors(target, clean, candidate, base, mod)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", help="clean ISO target, e.g. PlFc.dat")
    parser.add_argument("clean", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, help="optional path for safe materialized runtime bytes")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        runtime, result = materialize_effect_dat(
            args.target, args.clean.read_bytes(), args.candidate.read_bytes()
        )
        if args.output:
            args.output.write_bytes(runtime)
    except (InvalidImport, OSError, ValueError, struct.error) as exc:
        if args.json:
            print(json.dumps({"ok": False, "error": str(exc)}, indent=2))
        else:
            print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    output = {"ok": True, **result, "runtime_sha256": hashlib.sha256(runtime).hexdigest()}
    if args.json:
        print(json.dumps(output, indent=2))
    else:
        print(f"PASS: {result['status']}; {result['changed_bytes']} safe runtime bytes differ")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
