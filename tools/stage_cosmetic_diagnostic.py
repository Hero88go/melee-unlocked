#!/usr/bin/env python3
"""Prove that a stage DAT differs from its clean counterpart only in GX image payload bytes."""
from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

from cosmetic_import_diagnostic import InvalidImport, MAX_ASSET

FORMATS = {
    0: (8, 8, 32),   # I4
    1: (8, 4, 32),   # I8
    2: (8, 4, 32),   # IA4
    3: (4, 4, 32),   # IA8
    4: (4, 4, 32),   # RGB565
    5: (4, 4, 32),   # RGB5A3
    6: (4, 4, 64),   # RGBA8
    8: (8, 8, 32),   # C4 (palette changes remain unsupported)
    9: (8, 4, 32),   # C8
    10: (4, 4, 32),  # C14X2
    14: (8, 8, 32),  # CMPR
}


def texture_size(width: int, height: int, fmt: int, mipmap: int, max_lod: float) -> int:
    block_w, block_h, block_bytes = FORMATS[fmt]
    levels = int(max_lod) + 1 if mipmap else 1
    total = 0
    for level in range(levels):
        w, h = max(1, width >> level), max(1, height >> level)
        total += ((w + block_w - 1) // block_w) * ((h + block_h - 1) // block_h) * block_bytes
    return total


def dat_layout(data: bytes) -> dict:
    if len(data) < 0x20 or len(data) > MAX_ASSET:
        raise InvalidImport("stage DAT is truncated or exceeds 64 MB")
    file_size, data_size, relocations, roots, references = struct.unpack_from(">5I", data)
    if file_size != len(data):
        raise InvalidImport("stage DAT header size does not match file length")
    relocation_start = 0x20 + data_size
    root_start = relocation_start + relocations * 4
    strings = root_start + (roots + references) * 8
    if not roots or roots > 1024 or references > 1024 or relocations > len(data) // 4:
        raise InvalidImport("stage DAT counts are outside supported bounds")
    if strings > len(data):
        raise InvalidImport("stage DAT tables point outside the file")
    relocation_offsets = []
    for index in range(relocations):
        offset = struct.unpack_from(">I", data, relocation_start + index * 4)[0]
        if offset & 3 or offset + 4 > data_size:
            raise InvalidImport("stage DAT relocation is unaligned or outside the data block")
        target = struct.unpack_from(">I", data, 0x20 + offset)[0]
        if target and target >= data_size:
            raise InvalidImport("stage DAT relocation target is outside the data block")
        relocation_offsets.append(offset)
    symbols = []
    for index in range(roots + references):
        obj, name = struct.unpack_from(">2I", data, root_start + index * 8)
        if obj >= data_size or name >= len(data) - strings:
            raise InvalidImport("stage DAT root/reference points outside its tables")
        end = data.find(b"\0", strings + name)
        if end < 0 or end - (strings + name) > 255:
            raise InvalidImport("stage DAT symbol is not bounded and terminated")
        raw = data[strings + name:end]
        if any(byte < 0x20 or byte > 0x7E for byte in raw):
            raise InvalidImport("stage DAT symbol is not printable ASCII")
        symbols.append(raw.decode("ascii"))
    images = {}
    for offset in relocation_offsets:
        if offset + 24 > data_size:
            continue
        target = struct.unpack_from(">I", data, 0x20 + offset)[0]
        width, height, fmt, mipmap = struct.unpack_from(">2H2I", data, 0x20 + offset + 4)
        min_lod, max_lod = struct.unpack_from(">2f", data, 0x20 + offset + 16)
        if not target or not width or not height or width > 4096 or height > 4096:
            continue
        if fmt not in FORMATS or mipmap not in (0, 1):
            continue
        if not all(math.isfinite(value) and 0 <= value <= 12 for value in (min_lod, max_lod)):
            continue
        if min_lod > max_lod or (not mipmap and (min_lod != 0 or max_lod != 0)):
            continue
        size = texture_size(width, height, fmt, mipmap, max_lod)
        if target + size > data_size:
            continue
        images[offset] = {
            "target": target, "size": size, "width": width, "height": height,
            "format": fmt, "mipmap": mipmap, "min_lod": min_lod, "max_lod": max_lod,
        }
    if not images:
        raise InvalidImport("stage DAT contains no structurally identifiable GX image descriptors")

    # HSD_TObjDesc stores its image and TLUT descriptor pointers at +0x4c and +0x50. Anchor palette
    # discovery to a validated image descriptor rather than treating every pointer followed by a
    # small integer as a palette: false positives here could incorrectly whitelist gameplay data.
    relocation_set = set(relocation_offsets)
    palettes = {}
    for image_pointer in relocation_offsets:
        image_target = struct.unpack_from(">I", data, 0x20 + image_pointer)[0]
        if image_target not in images or image_pointer < 0x4c:
            continue
        object_offset = image_pointer - 0x4c
        palette_pointer = object_offset + 0x50
        if palette_pointer not in relocation_set:
            continue
        descriptor = struct.unpack_from(">I", data, 0x20 + palette_pointer)[0]
        if not descriptor or descriptor + 16 > data_size or descriptor not in relocation_set:
            continue
        target, fmt, name = struct.unpack_from(">3I", data, 0x20 + descriptor)
        entries = struct.unpack_from(">H", data, 0x20 + descriptor + 12)[0]
        if fmt not in (0, 1, 2) or entries not in (16, 256, 16384) or target + entries * 2 > data_size:
            continue
        palettes[descriptor] = {"target": target, "size": entries * 2,
                                "format": fmt, "name": name, "entries": entries}
    return {
        "data_size": data_size,
        "relocation_start": relocation_start,
        "root_start": root_start,
        "strings": strings,
        "relocations": relocation_offsets,
        "symbols": symbols,
        "roots": symbols[:roots],
        "references": symbols[roots:],
        "images": images,
        "palettes": palettes,
    }


def compare_stage_dat(clean: bytes, candidate: bytes) -> dict:
    base, mod = dat_layout(clean), dat_layout(candidate)
    if len(clean) != len(candidate) or base["data_size"] != mod["data_size"]:
        raise InvalidImport("stage DAT layout/length changed")
    if base["relocations"] != mod["relocations"] or base["symbols"] != mod["symbols"]:
        raise InvalidImport("stage DAT relocation graph or roots changed")
    if clean[base["relocation_start"]:] != candidate[mod["relocation_start"]:]:
        raise InvalidImport("stage DAT relocation/root/string tables changed")
    if base["images"] != mod["images"] or base["palettes"] != mod["palettes"]:
        raise InvalidImport("stage DAT image descriptors changed")
    allowed = bytearray(base["data_size"])
    for image in base["images"].values():
        allowed[image["target"]:image["target"] + image["size"]] = b"\1" * image["size"]
    for palette in base["palettes"].values():
        allowed[palette["target"]:palette["target"] + palette["size"]] = b"\1" * palette["size"]
    changed = 0
    for offset, (before, after) in enumerate(zip(clean[0x20:0x20 + base["data_size"]],
                                                 candidate[0x20:0x20 + mod["data_size"]])):
        if before == after:
            continue
        changed += 1
        if not allowed[offset]:
            raise InvalidImport(f"stage DAT changes non-image data at data offset 0x{offset:x}")
    if not changed:
        raise InvalidImport("stage DAT is identical to the clean resource")
    return {"changed_bytes": changed, "image_descriptors": len(base["images"]),
            "palette_descriptors": len(base["palettes"]),
            "status": "texture_payload_only"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("clean", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        result = compare_stage_dat(args.clean.read_bytes(), args.candidate.read_bytes())
    except (InvalidImport, OSError) as exc:
        if args.json:
            print(json.dumps({"ok": False, "error": str(exc)}, indent=2))
        else:
            print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps({"ok": True, **result}, indent=2))
    else:
        print(f"PASS: texture payload only; {result['changed_bytes']} changed bytes across "
              f"{result['image_descriptors']} validated image descriptors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
