#!/usr/bin/env python3
"""Read-only validation for the native cosmetic import milestone.

This does not install a mod and does not need the user's ISO. It independently checks the same
bounded ZIP/DAT identity used by the native importer so a supplied bundle can be diagnosed before
launching the Windows build.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zipfile
from pathlib import Path, PurePosixPath

MAX_ASSET = 64 * 1024 * 1024
MAX_ARCHIVE = 512 * 1024 * 1024
MAX_ENTRIES = 4096
FIGHTERS = (
    ("Ca", "Captain", "Captain Falcon", "Nr Re Wh Gr Bu Gy"),
    ("Cl", "Clink", "Young Link", "Nr Re Wh Bk Bu"),
    ("Dk", "Donkey", "Donkey Kong", "Nr Re Bu Gr Bk"),
    ("Dr", "Drmario", "Dr. Mario", "Nr Re Bu Gr Bk"),
    ("Fc", "Falco", "Falco", "Nr Re Bu Gr"),
    ("Fe", "Emblem", "Roy", "Nr Re Bu Gr Ye"),
    ("Fx", "Fox", "Fox", "Nr Or La Gr"),
    ("Gn", "Ganon", "Ganondorf", "Nr Re Bu Gr La"),
    ("Gw", "Gamewatch", "Mr. Game & Watch", "Nr"),
    ("Kb", "Kirby", "Kirby", "Nr Ye Bu Re Gr Wh"),
    ("Kp", "Koopa", "Bowser", "Nr Re Bu Bk"),
    ("Lg", "Luigi", "Luigi", "Nr Wh Aq Pi"),
    ("Lk", "Link", "Link", "Nr Re Bu Bk Wh"),
    ("Mr", "Mario", "Mario", "Nr Ye Bk Bu Gr"),
    ("Ms", "Mars", "Marth", "Nr Re Gr Bk Wh"),
    ("Mt", "Mewtwo", "Mewtwo", "Nr Re Bu Gr"),
    ("Nn", "Nana", "Ice Climbers (Nana)", "Nr Ye Aq Wh"),
    ("Ns", "Ness", "Ness", "Nr Ye Bu Gr"),
    ("Pc", "Pichu", "Pichu", "Nr Re Bu Gr"),
    ("Pe", "Peach", "Peach", "Nr Ye Wh Bu Gr"),
    ("Pk", "Pikachu", "Pikachu", "Nr Re Bu Gr"),
    ("Pp", "Popo", "Ice Climbers (Popo)", "Nr Re Gr Or"),
    ("Pr", "Purin", "Jigglypuff", "Nr Re Bu Gr Ye"),
    ("Sk", "Seak", "Sheik", "Nr Re Bu Gr Wh"),
    ("Ss", "Samus", "Samus", "Nr Pi Bk Gr La"),
    ("Ys", "Yoshi", "Yoshi", "Nr Re Bu Ye Pi Aq"),
    ("Zd", "Zelda", "Zelda", "Nr Re Bu Gr Wh"),
)
COLORS = {
    "Nr": "Default", "Re": "Red", "Bu": "Blue", "Gr": "Green",
    "Wh": "White", "Bk": "Black", "Ye": "Yellow", "Or": "Orange",
    "La": "Lavender", "Pi": "Pink", "Aq": "Aqua", "Gy": "Gray",
}


class InvalidImport(ValueError):
    pass


def safe_member(name: str) -> bool:
    if not name or len(name) > 1024 or "\\" in name or ":" in name:
        return False
    if any(ord(c) < 0x20 or c in '"*?' for c in name):
        return False
    path = PurePosixPath(name)
    return not path.is_absolute() and all(part not in ("", ".", "..") for part in path.parts)


def inspect_dat(data: bytes) -> dict:
    if len(data) < 0x20:
        raise InvalidImport("DAT header is truncated")
    file_size, data_size, relocations, root_count, references = struct.unpack_from(">5I", data)
    if file_size != len(data):
        raise InvalidImport("DAT header size does not match file length")
    if not root_count or root_count > 1024 or references > 1024 or relocations > len(data) // 4:
        raise InvalidImport("DAT relocation/root counts are outside supported bounds")
    root_table = 0x20 + data_size + relocations * 4
    strings = root_table + (root_count + references) * 8
    if root_table > len(data) or strings > len(data):
        raise InvalidImport("DAT tables point outside the file")
    for index in range(relocations):
        relocation_offset = struct.unpack_from(">I", data, 0x20 + data_size + index * 4)[0]
        if relocation_offset & 3 or relocation_offset + 4 > data_size:
            raise InvalidImport("DAT relocation entry is unaligned or points outside the data block")
        target = struct.unpack_from(">I", data, 0x20 + relocation_offset)[0]
        if target and target >= data_size:
            raise InvalidImport("DAT relocation target points outside the data block")
    symbols: list[str] = []
    roots: list[str] = []
    for index in range(root_count + references):
        obj, name_offset = struct.unpack_from(">2I", data, root_table + index * 8)
        if obj >= data_size or name_offset >= len(data) - strings:
            raise InvalidImport("DAT root entry points outside its tables")
        end = data.find(b"\0", strings + name_offset)
        if end < 0 or end - (strings + name_offset) > 255:
            raise InvalidImport("DAT root symbol is not a bounded terminated string")
        try:
            root = data[strings + name_offset : end].decode("ascii")
        except UnicodeDecodeError as exc:
            raise InvalidImport("DAT root symbol is not printable ASCII") from exc
        if not root.isprintable():
            raise InvalidImport("DAT root symbol is not printable ASCII")
        symbols.append(root)
        if index < root_count:
            roots.append(root)
    matches: list[tuple[str, str, str, str]] = []
    for file_code, root_name, display_name, allowed in FIGHTERS:
        for code in allowed.split():
            suffix = "" if code == "Nr" else code
            identity = f"Ply{root_name}5K{suffix}_Share_joint"
            if identity in roots:
                matches.append((file_code, display_name, code, COLORS[code]))
    if len(matches) == 1:
        file_code, character, code, label = matches[0]
        return {
            "kind": "character_costume",
            "character": character,
            "costume": label,
            "costume_code": code,
            "target_path": f"Pl{file_code}{code}.dat",
            "roots": roots,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
    if len(matches) > 1:
        raise InvalidImport("DAT roots conflict across multiple existing costume slots")
    raise InvalidImport("DAT roots do not identify a supported existing costume slot")


def read_input(path: Path) -> dict:
    if not path.is_file():
        raise InvalidImport(f"file does not exist: {path}")
    size = path.stat().st_size
    if not size:
        raise InvalidImport("selected file is empty")
    suffix = path.suffix.lower()
    if suffix == ".dat":
        if size > MAX_ASSET:
            raise InvalidImport("DAT exceeds the 64 MB resource limit")
        result = inspect_dat(path.read_bytes())
        result.update(source_kind="dat", source_name=path.name, source_member="", companions=[])
        return result
    if suffix != ".zip":
        raise InvalidImport("choose a .dat or .zip file")
    if size > MAX_ARCHIVE:
        raise InvalidImport("ZIP exceeds the 512 MB archive limit")
    recognized: list[tuple[zipfile.ZipInfo, dict]] = []
    companions: list[str] = []
    total = 0
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if not entries or len(entries) > MAX_ENTRIES:
            raise InvalidImport("ZIP entry count is empty or exceeds the 4096-entry limit")
        seen: set[str] = set()
        for entry in entries:
            if not safe_member(entry.filename):
                raise InvalidImport(f"unsafe ZIP path: {entry.filename}")
            folded = entry.filename.casefold()
            if folded in seen:
                raise InvalidImport(f"duplicate ZIP path: {entry.filename}")
            seen.add(folded)
            if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise InvalidImport(f"symbolic-link ZIP entry is unsupported: {entry.filename}")
            if entry.flag_bits & (1 | 0x40):
                raise InvalidImport("encrypted ZIP entries are unsupported")
            if entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                raise InvalidImport(f"unsupported ZIP compression method: {entry.filename}")
            total += entry.file_size
            if total > MAX_ARCHIVE:
                raise InvalidImport("ZIP expands beyond the 512 MB aggregate limit")
            lower_name = entry.filename.lower()
            if lower_name.endswith(".png") and ("stock" in lower_name or "csp" in lower_name or "portrait" in lower_name):
                kind = "stock icon" if "stock" in lower_name else "portrait"
                companions.append(f"{entry.filename} ({kind}: native texture override)")
            if not lower_name.endswith(".dat"):
                continue
            if entry.file_size > MAX_ASSET:
                raise InvalidImport("DAT in ZIP exceeds the 64 MB resource limit")
            try:
                data = archive.read(entry)
                recognized.append((entry, inspect_dat(data)))
            except InvalidImport:
                pass
        if len(recognized) != 1:
            raise InvalidImport(
                "ZIP must contain exactly one DAT that maps to a supported existing costume slot; "
                f"found {len(recognized)}"
            )
    entry, result = recognized[0]
    result.update(
        source_kind="zip",
        source_name=path.name,
        source_member=entry.filename,
        archive_entries=len(entries),
        companions=companions,
    )
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="DAT or ZIP to inspect without installing")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = parser.parse_args()
    try:
        result = read_input(args.input)
    except (InvalidImport, OSError, zipfile.BadZipFile, RuntimeError) as exc:
        if args.json:
            print(json.dumps({"ok": False, "error": str(exc)}, indent=2))
        else:
            print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    output = {"ok": True, **result}
    if args.json:
        print(json.dumps(output, indent=2))
    else:
        print(f"PASS: {result['character']} — {result['costume']} -> {result['target_path']}")
        print(f"DAT: {result['size']} bytes, SHA-256 {result['sha256']}")
        if result["source_member"]:
            print(f"ZIP member: {result['source_member']}")
        for companion in result["companions"]:
            print(f"PASS: {companion}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
