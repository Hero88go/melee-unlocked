"""Map native CSS portrait/stock texture payloads without modifying the clean ISO."""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from melee_iso import require_iso


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


# Sheik has stock icons of her own (column 25 of the icon animation) but shares Zelda's portrait
# cell, so she is a stock-only row. Costume order is Zelda's.
STOCK_ONLY_TARGETS = [(25, "Sk", ["Nr", "Re", "Bu", "Gr", "Wh"])]


def stock_only_slots() -> list[dict]:
    return [{"frame": column + color * 30, "target": f"Pl{family}{code}.dat"}
            for column, family, colors in STOCK_ONLY_TARGETS for color, code in enumerate(colors)]


def level_bytes(width: int, height: int, fmt: int) -> int:
    # C4 is 8x8 texel blocks, C8 is 8x4; 32 bytes a block either way.
    block_h = 8 if fmt == 8 else 4
    return ((width + 7) // 8) * ((height + block_h - 1) // block_h) * 32


def keyframes(block: bytes, fobj: int) -> dict[int, int]:
    """Frame -> value of one HSD_FObjDesc track holding constant integer keys."""
    length, = struct.unpack_from(">I", block, fobj + 4)
    value_format = block[fobj + 0xD]
    slope_format = block[fobj + 0xE]
    at, = struct.unpack_from(">I", block, fobj + 0x10)
    end, frame, keys = at + length, 0, {}
    kind, scale = value_format >> 5, 1 << (value_format & 31)
    sizes = {0: 4, 1: 2, 2: 2, 3: 1, 4: 1}
    while at < end:
        byte = block[at]; at += 1
        opcode, count, shift = byte & 0xF, (byte >> 4) & 7, 3
        while byte & 0x80:
            byte = block[at]; at += 1
            count |= (byte & 0x7F) << shift; shift += 7
        for _ in range(count + 1):
            value = None
            if opcode in (1, 2, 3, 4, 6):
                raw = block[at:at + sizes[kind]]; at += sizes[kind]
                value = (struct.unpack("<f", raw)[0] if kind == 0 else
                         int.from_bytes(raw, "little", signed=kind in (1, 3)) / scale)
            if opcode in (4, 5):
                at += sizes[slope_format >> 5]
            wait, shift = 0, 0
            while True:
                byte = block[at]; at += 1
                wait |= (byte & 0x7F) << shift; shift += 7
                if not byte & 0x80:
                    break
            if value is not None:
                keys[frame] = int(value)
            frame += wait
    return keys


def texture_animations(data: bytes, width: int, height: int, fmt: int) -> list[dict[int, tuple]]:
    """Every texture animation in the archive whose image table holds pictures of this size.

    The game shows a portrait or stock icon by setting the animation to frame
    column + 30 * costume. The animation's keys, not the order of the table, say which image and
    palette that frame is: the tables are packed and their last entries are out of order. Returns,
    per animation, frame -> (image hash, hash of the palette entries the image uses).
    """
    data_size, relocations = struct.unpack_from(">2I", data, 4)
    block = data[0x20:0x20 + data_size]
    word = lambda at: struct.unpack_from(">I", block, at)[0]
    offsets = [struct.unpack_from(">I", data, 0x20 + data_size + index * 4)[0] for index in range(relocations)]
    relocated = set(offsets)
    size = level_bytes(width, height, fmt)
    found = []
    for pointer in offsets:
        # pointer is &HSD_TexAnim.imagetbl: {next, id, aobjdesc, imagetbl, tluttbl, n_image, n_tlut}
        anim = pointer - 0xC
        if anim < 0 or pointer + 12 > data_size or pointer + 4 not in relocated or anim + 8 not in relocated:
            continue
        images, palettes = word(pointer), word(pointer + 4)
        image_count, palette_count = struct.unpack_from(">2H", block, pointer + 8)
        if not 20 <= image_count <= 400 or image_count != palette_count:
            continue
        if images + 4 * image_count > data_size or palettes + 4 * image_count > data_size:
            continue
        entries = []
        for index in range(image_count):
            image, palette = word(images + 4 * index), word(palettes + 4 * index)
            if image + 24 > data_size or palette + 14 > data_size:
                entries = None
                break
            pixels, w, h, f = struct.unpack_from(">I2HI", block, image)
            if (w, h, f) != (width, height, fmt) or pixels + size > data_size:
                entries.append(None)
                continue
            payload = block[pixels:pixels + size]
            # Dolphin's texture name hashes only the palette entries the image indexes.
            used = [n for byte in payload for n in ((byte >> 4, byte & 15) if fmt == 8 else (byte,))]
            low, high = min(used), max(used)
            colors = word(palette)
            entries.append((xxh64(payload), xxh64(block[colors + 2 * low:colors + 2 * (high + 1)])))
        if not entries or sum(entry is not None for entry in entries) * 5 < image_count * 4:
            continue
        try:
            track = word(word(anim + 8) + 8)
            frames = None
            while track:
                if block[track + 0xC] == 1:   # HSD_A_T_TIMG: the image index track
                    frames = keyframes(block, track)
                track = word(track)
        except (IndexError, struct.error, KeyError):
            continue
        if not frames or any(index >= image_count for index in frames.values()):
            continue
        found.append({frame: entries[index] for frame, index in frames.items() if entries[index]})
    return found


def diagnose(iso: Path) -> dict:
    data = read_disc_file(iso, "MnSlChr.usd")
    portraits = texture_animations(data, 136, 188, 9)
    stocks = texture_animations(data, 24, 24, 8)
    if len(portraits) != 6 or len(stocks) != 5:
        raise ValueError(f"unexpected MnSlChr texture tables: {len(portraits)} portrait, {len(stocks)} stock")
    # The in-game HUD, the results screen and the tournament screen carry their own copies of the
    # stock icons. One identity per slot is only right while every copy has the same bytes.
    copies = 0
    wanted = [slot["frame"] for slot in selector_slots() + stock_only_slots()]
    for other in ("MnSlChr.dat", "IfAll.usd", "IfAll.dat", "GmRst.usd", "GmRst.dat", "GmTou3p.usd", "GmTou3p.dat"):
        for table in texture_animations(read_disc_file(iso, other), 24, 24, 8):
            copies += 1
            for frame in wanted:
                if table.get(frame) != stocks[0][frame]:
                    raise ValueError(f"{other} stock frame {frame} differs from MnSlChr.usd")
    result = []
    for slot in selector_slots():
        row = dict(slot)
        row["csp_hash"] = f"{portraits[0][slot['frame']][0]:016x}"
        row["stock_hash"], row["stock_tlut_hash"] = (f"{value:016x}" for value in stocks[0][slot["frame"]])
        result.append(row)
    for slot in stock_only_slots():
        row = dict(slot)
        row["csp_hash"] = f"{0:016x}"
        row["stock_hash"], row["stock_tlut_hash"] = (f"{value:016x}" for value in stocks[0][slot["frame"]])
        result.append(row)

    def shared(key):
        owners = {}
        for row in result:
            if int(row[key], 16):
                owners.setdefault(row[key], set()).add(row["target"])
        return sorted(value for value, targets in owners.items() if len(targets) > 1)

    full = {}
    for row in result:
        full.setdefault((row["stock_hash"], row["stock_tlut_hash"]), set()).add(row["target"])
    if shared("csp_hash") or any(len(targets) > 1 for targets in full.values()):
        raise ValueError("two costumes share one complete texture identity")
    return {"ok": True, "selector_slots": len(selector_slots()), "portrait_tables": len(portraits),
            "stock_tables": len(stocks), "other_stock_copies_checked": copies,
            "ambiguous_stock_hashes": shared("stock_hash"), "slots": result}


def header(result: dict) -> str:
    lines = [
        "// Generated from a clean GALE01 (NTSC-U 1.02) MnSlChr.usd by",
        "// tools/companion_texture_diagnostic.py --header. Texture payload identities only; no game data.",
        "// Each row is the picture the game shows at frame column + 30 * costume of the portrait and",
        "// stock icon animations, read through the animation's keys. A zero csp_hash is a stock-only row.",
        "// SPDX-License-Identifier: GPL-2.0-or-later",
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace gx::texpack::native_companions {",
        "struct Slot { const char* target; uint64_t csp_hash; uint64_t stock_hash; uint64_t stock_tlut_hash; };",
        "inline constexpr Slot kSlots[] = {",
    ]
    for row in result["slots"]:
        lines.append(f'    {{"{row["target"]}", 0x{row["csp_hash"]}ull, 0x{row["stock_hash"]}ull, '
                     f'0x{row["stock_tlut_hash"]}ull}},')
    lines += ["};", "}  // namespace gx::texpack::native_companions", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--header", type=Path, help="write companion_texture_map.h here")
    args = parser.parse_args()
    try:
        result = diagnose(require_iso(args.iso))
    except (OSError, ValueError) as exc:
        result = {"ok": False, "error": str(exc)}
    if result["ok"] and args.header:
        with args.header.open("w", encoding="utf-8", newline=chr(13) + chr(10)) as out:
            out.write(header(result))
    print(json.dumps(result, indent=2) if args.json else
          (f"PASS: {result['selector_slots']} selector slots mapped" if result["ok"] else f"FAIL: {result['error']}"))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
