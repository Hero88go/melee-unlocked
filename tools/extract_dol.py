"""Extracts main.dol from a GameCube ISO (the DOL offset is stored at 0x420 in the disc header).

    python tools/extract_dol.py <game.iso> <out/main.dol>
"""
import struct
import sys
from pathlib import Path


def extract(iso_path, out_path):
    with open(iso_path, "rb") as f:
        header = f.read(0x440)
        if header[:6] != b"GALE01":
            raise SystemExit(f"{iso_path}: not Melee NTSC (game id {header[:6]!r}); this port needs GALE01 v1.02")
        if header[7] != 2:
            raise SystemExit(f"{iso_path}: disc revision {header[7]}, need v1.02 (revision 2)")
        dol_offset = struct.unpack(">I", header[0x420:0x424])[0]
        f.seek(dol_offset)
        dol_header = f.read(0x100)
        # DOL: 7 text + 11 data sections; file size = max(offset + size) over the sections.
        offsets = struct.unpack(">18I", dol_header[0:72])
        sizes = struct.unpack(">18I", dol_header[0x90:0x90 + 72])
        size = max(o + s for o, s in zip(offsets, sizes))
        f.seek(dol_offset)
        data = f.read(size)
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    Path(out_path).write_bytes(data)
    print(f"main.dol: {size} bytes from disc offset 0x{dol_offset:X} -> {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    extract(sys.argv[1], sys.argv[2])
