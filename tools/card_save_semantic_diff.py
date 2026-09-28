#!/usr/bin/env python3
"""Decode and compare the mirrored Melee GmSaveData blocks in two GCI files.

The block cipher and checksum follow HSD_Decrypt/HSD_Checksum in the game's
sysdolphin/baselib/crypt.c. A whole-sector encrypted diff can obscure a small
semantic change because the checksum and chained byte transform both change.
"""
import argparse
import hashlib
import json
from pathlib import Path

SECTOR = 0x2000
GCI_HEADER = 0x40
SAVE_SIZE = 0x1790  # sizeof(GmSaveData), melee/gm/types.h
NAMETAG_SIZE = 0x1F2C  # sizeof(NameTagDataBank), melee/gm/types.h
LOGICAL_FILE_SIZES = {1: SAVE_SIZE, **{i: NAMETAG_SIZE for i in range(2, 9)}}
LOGICAL_FILE_NAMES = {1: "GmSaveData", **{i: f"NameTagDataBank[{i - 2}]" for i in range(2, 9)}}
KEYS = (0x26, 0xFF, 0xE8, 0xEF, 0x42, 0xD6, 0x01, 0x54, 0x14, 0xA3, 0x80, 0xFD, 0x6E)


def decrypt_byte(previous, current):
    n = previous % 7
    if n == 0:
        value = ((current & 1) | ((current << 1) & 4) | ((current << 2) & 16) |
                 ((current << 3) & 64) | ((current >> 3) & 2) | ((current >> 2) & 8) |
                 ((current >> 1) & 32) | (current & 128))
    elif n == 1:
        value = (((current << 1) & 2) | ((current << 6) & 128) | (current & 4) |
                 ((current >> 3) & 1) | ((current << 1) & 32) | ((current >> 1) & 16) |
                 ((current >> 3) & 8) | ((current >> 1) & 64))
    elif n == 2:
        value = (((current & 1) << 2) | ((current << 2) & 8) | ((current << 4) & 64) |
                 ((current << 1) & 16) | ((current << 3) & 128) | ((current >> 4) & 2) |
                 ((current >> 6) & 1) | ((current >> 2) & 32))
    elif n == 3:
        value = (((current << 4) & 16) | ((current >> 1) & 1) | ((current << 3) & 32) |
                 ((current >> 2) & 2) | ((current >> 1) & 8) | ((current << 1) & 64) |
                 ((current << 1) & 128) | ((current >> 5) & 4))
    elif n == 4:
        value = (((current << 3) & 8) | ((current << 4) & 32) | ((current >> 1) & 2) |
                 ((current << 4) & 128) | ((current << 2) & 64) | ((current >> 3) & 4) |
                 ((current >> 2) & 16) | ((current >> 7) & 1))
    elif n == 5:
        value = (((current & 1) << 5) | ((current << 5) & 64) | ((current & 4) << 5) |
                 (current & 8) | ((current >> 2) & 4) | ((current >> 5) & 1) |
                 ((current >> 5) & 2) | ((current >> 3) & 16))
    else:
        value = (((current << 6) & 64) | (current & 2) | ((current >> 2) & 1) |
                 ((current << 2) & 32) | (current & 16) | ((current << 2) & 128) |
                 ((current >> 4) & 4) | ((current >> 4) & 8))
    return (value ^ KEYS[previous % len(KEYS)] ^ previous) & 0xFF


def decrypt_sector(encrypted):
    data = bytearray(encrypted)
    previous = data[15]
    for index in range(16, len(data)):
        current = data[index]
        data[index] = decrypt_byte(previous, current)
        previous = current
    checksum = bytearray((1, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                          0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10))
    for index, value in enumerate(data[16:]):
        checksum[index % 16] = (checksum[index % 16] + value) & 0xFF
    for index in range(1, 16):
        if checksum[index - 1] == checksum[index]:
            checksum[index] ^= 0xFF
    if checksum != data[:16]:
        return None
    return bytes(data)


def save_blocks(path):
    raw = path.read_bytes()
    if len(raw) < GCI_HEADER or (len(raw) - GCI_HEADER) % SECTOR:
        raise ValueError(f"{path}: not an aligned GCI")
    blocks = []
    for sector_index, offset in enumerate(range(GCI_HEADER, len(raw), SECTOR)):
        decoded = decrypt_sector(raw[offset:offset + SECTOR])
        if decoded is not None and decoded[0x10:0x12] == b"\0\1":
            blocks.append({
                "sector": sector_index,
                "sequence": decoded[0x12],
                "table": decoded[0x13:0x1F].hex(),
                "data": decoded[0x20:0x20 + SAVE_SIZE],
            })
    if len(blocks) != 2:
        raise ValueError(f"{path}: expected 2 valid save-data copies, found {len(blocks)}")
    if blocks[0]["data"] != blocks[1]["data"]:
        raise ValueError(f"{path}: mirrored save-data copies do not match")
    return raw, blocks


def _sequence_is_newer(a, b):
    """Match HSD's wrapping 8-bit card block sequence comparison."""
    difference = a - b
    if difference > 0x80:
        return False
    if difference < -0x80:
        return True
    return difference > 0


def logical_payloads(path):
    """Extract all Melee logical manifest payloads from the HSD-encrypted GCI.

    CardBlockHeader IDs 1..8 map to manifest entries 1..8 for the shipped card
    layout. The game data is at +0x20 in each verified 0x2000-byte sector;
    entry 1 is mirrored and entries 2..8 each occupy one block.
    """
    raw = path.read_bytes()
    if len(raw) < GCI_HEADER or (len(raw) - GCI_HEADER) % SECTOR:
        raise ValueError(f"{path}: not an aligned GCI")
    blocks = {idx: [] for idx in LOGICAL_FILE_SIZES}
    for sector_index, offset in enumerate(range(GCI_HEADER, len(raw), SECTOR)):
        decoded = decrypt_sector(raw[offset:offset + SECTOR])
        if decoded is None:
            continue
        block_id = int.from_bytes(decoded[0x10:0x12], "big")
        if block_id in blocks:
            blocks[block_id].append({
                "sector": sector_index,
                "sequence": decoded[0x12],
                "payload": decoded[0x20:0x20 + LOGICAL_FILE_SIZES[block_id]],
            })
    result = {}
    for idx, size in LOGICAL_FILE_SIZES.items():
        candidates = blocks[idx]
        if not candidates:
            raise ValueError(f"{path}: missing logical card entry {idx} ({LOGICAL_FILE_NAMES[idx]})")
        newest = candidates[0]
        for candidate in candidates[1:]:
            if _sequence_is_newer(candidate["sequence"], newest["sequence"]):
                newest = candidate
        payloads = [item["payload"] for item in candidates]
        result[idx] = {
            "name": LOGICAL_FILE_NAMES[idx],
            "size": size,
            "payload": newest["payload"],
            "copies": [{"sector": item["sector"], "sequence": item["sequence"]}
                       for item in candidates],
            "copies_match": all(payload == payloads[0] for payload in payloads[1:]),
        }
        if len(newest["payload"]) != size:
            raise ValueError(f"{path}: truncated logical card entry {idx} ({LOGICAL_FILE_NAMES[idx]})")
    return raw, result


def field_region(offset):
    if offset < 0x1B0:
        return "unlock/progress and unknown records"
    if 0x1E8 <= offset < 0x1EC:
        return "PowerCount (GmSaveData.x1A50)"
    if 0x1EC <= offset < 0x1F0:
        return "PowerTime (GmSaveData.x1A54)"
    if offset < 0x208:
        return "other counters and unknown fields"
    if offset < 0x2D4:
        return "reserved/unknown region"
    if offset < 0x448:
        return "trophy/progress records"
    if offset < 0x468:
        return "GamePrefs"
    if offset < 0x6C4:
        return "trophy counts, flags and padding"
    return "fighter data"


def changed_ranges(before, after):
    indices = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
    if not indices:
        return []
    ranges = []
    start = previous = indices[0]
    for index in indices[1:]:
        if index != previous + 1:
            ranges.append([start, previous + 1])
            start = index
        previous = index
    ranges.append([start, previous + 1])
    return ranges


def compare(seed, output):
    seed_raw, seed_blocks = save_blocks(seed)
    out_raw, out_blocks = save_blocks(output)
    old, new = seed_blocks[0]["data"], out_blocks[0]["data"]
    changes = [i for i, (a, b) in enumerate(zip(old, new)) if a != b]
    regions = {}
    for offset in changes:
        name = field_region(offset)
        regions[name] = regions.get(name, 0) + 1
    _, seed_files = logical_payloads(seed)
    _, out_files = logical_payloads(output)
    logical_changes = {}
    for idx, old_file in seed_files.items():
        new_file = out_files[idx]
        before, after = old_file["payload"], new_file["payload"]
        changed = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
        logical_changes[old_file["name"]] = {
            "size": old_file["size"],
            "bytes_changed": len(changed),
            "ranges": changed_ranges(before, after),
            "changed_bytes": [{"offset": f"0x{i:04X}", "before": f"{before[i]:02X}",
                               "after": f"{after[i]:02X}"} for i in changed],
            "seed_copies": old_file["copies"],
            "output_copies": new_file["copies"],
            "seed_copies_match": old_file["copies_match"],
            "output_copies_match": new_file["copies_match"],
        }
    return {
        "seed": {"path": str(seed), "sha256": hashlib.sha256(seed_raw).hexdigest()},
        "output": {"path": str(output), "sha256": hashlib.sha256(out_raw).hexdigest()},
        "gci_bytes_changed": sum(a != b for a, b in zip(seed_raw, out_raw)),
        "save_data_bytes_changed": len(changes),
        "save_data_changed_ranges": changed_ranges(old, new),
        "save_data_changed_regions": regions,
        "logical_entry_changes": logical_changes,
        "logical_entry_bytes_changed": sum(item["bytes_changed"] for item in logical_changes.values()),
        "gci_ciphertext_note": "The GCI sectors are HSD-encrypted and chained; one logical byte change alters the sector checksum and subsequent ciphertext. Compare logical_entry_changes for semantic differences.",
        "copies": {
            "seed": [{"sector": b["sector"], "sequence": b["sequence"], "table": b["table"]} for b in seed_blocks],
            "output": [{"sector": b["sector"], "sequence": b["sequence"], "table": b["table"]} for b in out_blocks],
            "output_payloads_match": out_blocks[0]["data"] == out_blocks[1]["data"],
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("seed", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    result = compare(args.seed, args.output)
    rendered = json.dumps(result, indent=2)
    print(rendered)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
