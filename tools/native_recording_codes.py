"""Build replay metadata for the native General Codes; never executed by Source."""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "port/recomp"))
from gecko import load_ini, generate_gct


# Where the recompiled build's game places Slippi's code table (recomp.py --gct-base).
GCT_BASE = 0x8065CC80


def online_codes():
    """The Gecko list Slippi's SendGameInfo records in an online match: the table the game runs
    (Required: General Codes, Slippi Recording, Slippi Online and the enabled Recommended codes),
    without its header and terminator, as the in-game handler leaves it once installed: every C2/D2
    code ends with its branch back, and the switched-off optional codes are Gecko gotos over
    themselves (never installed, so their own C2 lines stay as written). The port's own PC-only
    suffix is cut with a terminator (the declared size stays)."""
    codes = load_ini(ROOT / "port/slippi_sys/GameSettings/GALE01r2.ini")
    gct, optional_offset, port_offset = generate_gct(codes)
    body = bytearray(gct[8:-8])
    word = lambda i: struct.unpack_from(">I", body, i)[0]
    i = 0
    while i + 8 <= optional_offset - 8:
        kind = body[i]
        n = word(i + 4)
        if kind in (0xC2, 0xD2):
            last = i + 8 + n * 8 - 4
            hook = 0x80000000 | (word(i) & 0x01FFFFFF)
            struct.pack_into(">I", body, last,
                             0x48000000 | (((hook + 4) - (GCT_BASE + 8 + last)) & 0x03FFFFFC))
            i = last + 4
        elif kind == 0xC0:
            i += 8 + n * 8
        elif kind == 0x06:
            i += 8 + (n + 7) // 8 * 8
        elif kind == 0x08:
            i += 16
        else:
            i += 8
    offset = optional_offset - 8
    for code in codes:
        if code.enabled and code.optional is not None:
            struct.pack_into(">II", body, offset, 0x66200000 | (len(code.codes) - 1), 0)
            offset += len(code.codes) * 8
    cut = port_offset - 8
    body[cut:] = struct.pack(">II", 0xFF000000, 0) + bytes(len(body) - cut - 8)
    return bytes(body)


def c_array(name, data):
    lines = ["static const unsigned char %s[] = {" % name]
    lines += ["  " + ",".join(f"0x{b:02X}" for b in data[i:i+16]) + ","
              for i in range(0, len(data), 16)]
    lines.append("};")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("output", type=Path)
    ap.add_argument("--dump-online", type=Path, help="also write the online list as raw bytes")
    args = ap.parse_args()
    names = {"Required: General Codes", "Recommended: Lagless FoD"}
    selected = [c for c in load_ini(ROOT / "port/slippi_sys_general/GameSettings/GALE01r2.ini")
                if c.name in names]
    if {c.name for c in selected} != names:
        raise SystemExit("native recording code set is incomplete")
    data = b"".join(struct.pack(">II", a, b) for code in selected for a, b in code.codes)
    data += struct.pack(">II", 0xFF000000, 0)
    lines = ["// Replay metadata only. Source never installs or executes these bytes.",
             "// native_recording_codes: the bundled Slippi General Codes and Lagless FoD (offline).",
             "// native_online_recording_codes: the code table of an online match, as recorded.",
             "// SPDX-License-Identifier: GPL-2.0-or-later", "#pragma once"]
    lines += c_array("native_recording_codes", data)
    lines += c_array("native_online_recording_codes", online_codes())
    lines.append("")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")
    if args.dump_online:
        args.dump_online.write_bytes(online_codes())


if __name__ == "__main__":
    main()
