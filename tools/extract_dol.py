"""Extracts main.dol from a GameCube ISO (the DOL offset is stored at 0x420 in the disc header).

    python tools/extract_dol.py <game.iso> <out/main.dol> [--any]

--any accepts a disc whose header is not vanilla NTSC 1.02 (a custom build: m-ex/ACE, 20XX,
Akaneia...). The header check becomes a warning; what the disc actually contains is then checked
by recomp.py and by the runtime, which compare the DOL image itself.
"""
import hashlib
import struct
import sys
from pathlib import Path


def extract(iso_path, out_path, allow_any=False):
    with open(iso_path, "rb") as f:
        header = f.read(0x440)
        problem = None
        if header[:6] != b"GALE01":
            problem = f"not Melee NTSC (game id {header[:6]!r}); this port needs GALE01 v1.02"
        elif header[7] != 2:
            problem = f"disc revision {header[7]}, need v1.02 (revision 2)"
        if problem:
            if not allow_any:
                raise SystemExit(f"{iso_path}: {problem}")
            print(f"warning: {iso_path}: {problem} (continuing: --any)")
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
    digest = hashlib.sha1(data).hexdigest()
    vanilla = digest == "08e0bf20134dfcb260699671004527b2d6bb1a45"
    print(f"main.dol: {size} bytes from disc offset 0x{dol_offset:X} -> {out_path}")
    print(f"sha1 {digest} ({'vanilla NTSC 1.02' if vanilla else 'custom build: recompile with --modded-dol'})")


if __name__ == "__main__":
    argv = [a for a in sys.argv[1:] if a != "--any"]
    if len(argv) != 2:
        raise SystemExit(__doc__)
    extract(argv[0], argv[1], allow_any="--any" in sys.argv[1:])
