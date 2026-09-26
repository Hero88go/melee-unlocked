"""Map native CSS portrait/stock texture payloads without modifying the clean ISO."""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from melee_iso import require_iso
from stage_cosmetic_diagnostic import dat_layout


# HUD order and costume order come from mn/forward.h, mncharsel.c, gm_1601.c, and each
# Fighter_CostumeStrings array in the matching NTSC-U 1.02 decompilation. Nana and Sheik share
# their partner's selector cell; Game & Watch has four cells backed by one physical costume DAT.
SELECTOR_TARGETS = [
    ("Ca", ["Nr", "Gy", "Re", "Wh", "Gr", "Bu"]),
    ("Dk", ["Nr", "Bk", "Re", "Bu", "Gr"]),
    ("Fx", ["Nr", "Or", "La", "Gr"]),
    ("Gw", ["Nr", "Nr", "Nr", "Nr"]),
    ("Kb", ["Nr", "Ye", "Bu", "Re", "Gr", "Wh"]),
    ("Kp", ["Nr", "Re", "Bu", "Bk"]),
    ("Lk", ["Nr", "Re", "Bu", "Bk", "Wh"]),
    ("Lg", ["Nr", "Wh", "Aq", "Pi"]),
    ("Mr", ["Nr", "Ye", "Bk", "Bu", "Gr"]),
    ("Ms", ["Nr", "Re", "Gr", "Bk", "Wh"]),
    ("Mt", ["Nr", "Re", "Bu", "Gr"]),
    ("Ns", ["Nr", "Ye", "Bu", "Gr"]),
    ("Pe", ["Nr", "Ye", "Wh", "Bu", "Gr"]),
    ("Pk", ["Nr", "Re", "Bu", "Gr"]),
    ("Pp", ["Nr", "Gr", "Or", "Re"]),
    ("Pr", ["Nr", "Re", "Bu", "Gr", "Ye"]),
    ("Ss", ["Nr", "Pi", "Bk", "Gr", "La"]),
    ("Ys", ["Nr", "Re", "Bu", "Ye", "Pi", "Aq"]),
    ("Zd", ["Nr", "Re", "Bu", "Gr", "Wh"]),
    ("Fc", ["Nr", "Re", "Bu", "Gr"]),
    ("Cl", ["Nr", "Re", "Bu", "Wh", "Bk"]),
    ("Dr", ["Nr", "Re", "Bu", "Gr", "Bk"]),
    ("Fe", ["Nr", "Re", "Bu", "Gr", "Ye"]),
    ("Pc", ["Nr", "Re", "Bu", "Gr"]),
    ("Gn", ["Nr", "Re", "Bu", "Gr", "La"]),
]


def selector_slots() -> list[dict]:
    slots = []
    for color in range(6):
        for hud, (family, colors) in enumerate(SELECTOR_TARGETS):
            if color >= len(colors):
                continue
            slots.append({"frame": hud + color * 30,
                          "target": f"Pl{family}{colors[color]}.dat"})
    assert len(slots) == 118
    return slots


def xxh64(data: bytes, seed: int = 0) -> int:
    mask = (1 << 64) - 1
    p1, p2 = 0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F
    p3, p4, p5 = 0x165667B19E3779F9, 0x85EBCA77C2B2AE63, 0x27D4EB2F165667C5
    rot = lambda value, bits: ((value << bits) | (value >> (64 - bits))) & mask
    rnd = lambda acc, value: (rot((acc + value * p2) & mask, 31) * p1) & mask
    pos, size = 0, len(data)
    if size >= 32:
        values = [(seed + p1 + p2) & mask, (seed + p2) & mask, seed & mask,
                  (seed - p1) & mask]
        while pos <= size - 32:
            for index in range(4):
                values[index] = rnd(values[index], int.from_bytes(data[pos:pos + 8], "little"))
                pos += 8
        result = sum(rot(values[i], (1, 7, 12, 18)[i]) for i in range(4)) & mask
        for value in values:
            merged = (rot((value * p2) & mask, 31) * p1) & mask
            result = ((result ^ merged) * p1 + p4) & mask
    else:
        result = (seed + p5) & mask
    result = (result + size) & mask
    while pos + 8 <= size:
        value = int.from_bytes(data[pos:pos + 8], "little")
        result ^= (rot((value * p2) & mask, 31) * p1) & mask
        result = (rot(result, 27) * p1 + p4) & mask
        pos += 8
    if pos + 4 <= size:
        result ^= int.from_bytes(data[pos:pos + 4], "little") * p1
        result = (rot(result & mask, 23) * p2 + p3) & mask
        pos += 4
    while pos < size:
        result ^= data[pos] * p5
        result = (rot(result & mask, 11) * p1) & mask
        pos += 1
    result ^= result >> 33
    result = (result * p2) & mask
    result ^= result >> 29
    result = (result * p3) & mask
    result ^= result >> 32
    return result & mask


def read_disc_file(iso: Path, wanted: str) -> bytes:
    with iso.open("rb") as disc:
        disc.seek(0x424)
        fst_offset, fst_size = struct.unpack(">II", disc.read(8))
        if fst_size > 32 * 1024 * 1024 or fst_offset + fst_size > iso.stat().st_size:
            raise ValueError("invalid disc FST")
        disc.seek(fst_offset)
        fst = disc.read(fst_size)
        count = struct.unpack_from(">I", fst, 8)[0]
        if not count or count * 12 > len(fst):
            raise ValueError("invalid disc FST entry count")
        for index in range(1, count):
            kind_name, offset, size = struct.unpack_from(">III", fst, index * 12)
            if kind_name >> 24:
                continue
            begin = count * 12 + (kind_name & 0xFFFFFF)
            end = fst.find(b"\0", begin)
            if end < begin:
                raise ValueError("unterminated disc filename")
            if fst[begin:end].decode("ascii", "strict").lower() != wanted.lower():
                continue
            if offset + size > iso.stat().st_size:
                raise ValueError("disc file points outside ISO")
            disc.seek(offset)
            return disc.read(size)
    raise ValueError(f"{wanted} is absent from the disc")


def descriptor_runs(layout: dict, width: int, height: int, fmt: int, length: int) -> list[list[tuple]]:
    matches = [(offset, image) for offset, image in layout["images"].items()
               if image["width"] == width and image["height"] == height and image["format"] == fmt]
    matches.sort()
    runs, current = [], []
    for item in matches:
        # Consecutive HSD_ImageDesc records are 24 bytes apart; each record's image pointer is a
        # relocation, which is why the validated descriptor offsets themselves form this stride.
        if current and item[0] != current[-1][0] + 24:
            if len(current) == length:
                runs.append(current)
            current = []
        current.append(item)
    if len(current) == length:
        runs.append(current)
    return runs


def diagnose(iso: Path) -> dict:
    data = read_disc_file(iso, "MnSlChr.dat")
    layout = dat_layout(data)
    portraits = descriptor_runs(layout, 136, 188, 9, 118)
    stocks = descriptor_runs(layout, 24, 24, 8, 119)
    if len(portraits) != 6 or len(stocks) != 5:
        raise ValueError(f"unexpected MnSlChr texture tables: {len(portraits)} portrait, {len(stocks)} stock")
    slots = selector_slots()
    result = []
    for index, slot in enumerate(slots):
        row = dict(slot)
        for kind, run in (("csp", portraits[0]), ("stock", stocks[0])):
            image = run[index][1]
            payload = data[0x20 + image["target"]:0x20 + image["target"] + image["size"]]
            row[kind + "_hash"] = f"{xxh64(payload):016x}"
        result.append(row)
    stock_hashes = [row["stock_hash"] for row in result]
    duplicates = sorted({value for value in stock_hashes if stock_hashes.count(value) > 1})
    return {"ok": True, "selector_slots": len(result), "portrait_tables": len(portraits),
            "stock_tables": len(stocks), "ambiguous_stock_hashes": duplicates, "slots": result}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        result = diagnose(require_iso(args.iso))
    except (OSError, ValueError) as exc:
        result = {"ok": False, "error": str(exc)}
    print(json.dumps(result, indent=2) if args.json else
          (f"PASS: {result['selector_slots']} selector slots mapped" if result["ok"] else f"FAIL: {result['error']}"))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
