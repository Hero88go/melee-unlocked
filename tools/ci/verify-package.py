"""Disc-free checks of the "Latest" Windows zip: layout, native PE files, launch spec, deltas, no test hook."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import zipfile

ROOT = "MeleeUnlocked-latest/"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def x64_pe(data, unsigned):
    require(data[:2] == b"MZ", "missing PE DOS header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    require(data[pe:pe + 4] == b"PE\0\0", "missing PE signature")
    require(struct.unpack_from("<H", data, pe + 4)[0] == 0x8664, "expected x64 native PE")
    optional = pe + 24
    require(struct.unpack_from("<H", data, optional)[0] == 0x20B, "expected PE32+")
    if unsigned:
        require(struct.unpack_from("<II", data, optional + 112 + 4 * 8) == (0, 0), "built PE should be unsigned")


def verify(path, repo):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        require(len(names) == len({n.casefold() for n in names}), "duplicate package path")
        require(all(n.startswith(ROOT) for n in names), f"every entry must be under {ROOT}")
        for name in names:
            p = PurePosixPath(name)
            require(not p.is_absolute() and ".." not in p.parts and "\\" not in name and ":" not in name,
                    "unsafe package path")
            require(not re.search(r"\.(iso|gcm|ciso|gcz|rvz|wia|wbfs|nkit\.[a-z0-9]+|dol|dmp|mdmp)$", name, re.I)
                    and not re.search(r"(^|/)(Looks|CosmeticMods|recomp)/", name, re.I)
                    and not re.search(r"(^|/)(GameSetup_gui\.dat|slpCSS\.dat|SlippiCSS_gui\.dat|melee_port(?:_playback)?\.exe)$", name, re.I),
                    f"forbidden disc, translation, menu, dump or player file: {name}")
        for name in ("melee_source.exe", "melee_game.dll", "melee_game.snapexcl", "MeleeUnlockedLauncher.exe",
                     "melee_source_compat.exe", "melee_game_compat.dll", "melee_game_compat.snapexcl",
                     "yougame-native.json", "README.txt", "LATEST.txt", "dxr_pathtrace.dxil", "sl.interposer.dll",
                     "deltas/manifest.json", "deltas/GameSetup_gui.dat.vcdiff", "deltas/slpCSS.dat.vcdiff",
                     "deltas/SlippiCSS_gui.dat.vcdiff"):
            require(ROOT + name in names and archive.getinfo(ROOT + name).file_size > 0, f"missing or empty: {ROOT}{name}")
        require(ROOT + "compat.sha256" not in names, "compat.sha256 would replace the included compatibility build")
        for directory in ("Sys/", "lang/", "licenses/", "Shaders/"):
            require(any(n.startswith(ROOT + directory) for n in names), f"missing runtime directory: {directory}")
        spec = json.loads(archive.read(ROOT + "yougame-native.json"))
        require(spec == json.loads((repo / "port/yougame/yougame-native.json").read_text(encoding="utf-8-sig")),
                "yougame-native.json differs from port/yougame/yougame-native.json")
        for name in ("melee_source.exe", "melee_source_compat.exe"):
            exe = archive.read(ROOT + name)
            # A release build: the YouGame agent test hook (YOUGAME_AGENT_TEST_HOOK) must be compiled out.
            for needle in (b"agent test: padBack", b"agent-test.json"):
                require(needle not in exe, f"test hook present in {name}")
            x64_pe(exe, unsigned=True)
        x64_pe(archive.read(ROOT + "MeleeUnlockedLauncher.exe"), unsigned=True)
        for name in ("melee_game.dll", "melee_game_compat.dll"):
            x64_pe(archive.read(ROOT + name), unsigned=True)
        manifest = json.loads(archive.read(ROOT + "deltas/manifest.json"))
        entries = manifest["files"]
        require(len(entries) == 3 and {e["target"] for e in entries} == {"GameSetup_gui.dat", "slpCSS.dat", "SlippiCSS_gui.dat"},
                "delta manifest must contain all three targets exactly once")
        for entry in entries:
            require(entry["delta"] == entry["target"] + ".vcdiff", "unexpected delta filename")
            data = archive.read(ROOT + "deltas/" + entry["delta"])
            require(len(data) == entry["delta_size"] and hashlib.sha1(data).hexdigest() == entry["delta_sha1"].lower(),
                    "delta size/SHA-1 mismatch")
        print(f"PASS {path.name}: {len(names)} entries, native x64, test hook off, launch spec matches the repository")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("zip", type=Path)
    ap.add_argument("--repo", type=Path, required=True)
    args = ap.parse_args()
    verify(args.zip, args.repo)


if __name__ == "__main__":
    main()
