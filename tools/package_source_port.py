"""Assembles a ready-to-run folder of the Source Port build that has no Slippi code in it.

    python tools/package_source_port.py --out <folder> [--zip]

Takes the launcher and game host from build-release-080-ns, the game library from
build-sourceport-gcc-ns, and the runtime files every package carries (Visual C++ runtime, Streamline
and XeSS libraries, the free DSP coefficient table, pipeline recipes, licences, language files) from
--from-package, a normal release folder. Afterwards every text file and every binary in the result
is scanned for the word the build is named after not having; the count per file is printed.
"""
import argparse
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST_FILES = ["MeleeUnlockedLauncher.exe", "melee_source.exe", "dxr_pathtrace.dxil", "libxess.dll",
              "nvngx.dll_meleedlss5.dll", "nvngx_dlss.dll", "nvngx_dlssd.dll", "nvngx_dlssg.dll",
              "sl.common.dll", "sl.dlss.dll", "sl.dlss_d.dll", "sl.dlss_g.dll", "sl.interposer.dll",
              "sl.pcl.dll", "sl.reflex.dll"]
GAME_FILES = ["melee_game.dll", "melee_game.dbg", "melee_game.snapexcl"]   # the three always travel together
RUNTIME_GLOBS = ["concrt140.dll", "msvcp140*.dll", "vccorlib140.dll", "vcruntime140*.dll"]
# Licences of code that is not in this build stay out.
LICENCE_SKIP = {"slippilab.txt", "hps_decode.txt"}
WORD = re.compile(rb"slippi", re.IGNORECASE)

README = """Melee Unlocked {version}: Source Port
========================================

Super Smash Bros. Melee NTSC 1.02 as a native Windows program, compiled from the game's C source.
The game logic runs exactly as on the GameCube at 60 Hz; the display renders at any rate, with
D3D12 or D3D11, DLSS/DLAA or XeSS, and GameCube adapter, Xbox, PlayStation and Switch controllers.

You need your own Melee NTSC 1.02 ISO. Nothing from the game is included.
Not affiliated with, endorsed by, or supported by Nintendo or HAL Laboratory.

Start
-----
1. Run MeleeUnlockedLauncher.exe and drop your ISO onto its window.
2. Press PLAY. The first launch precompiles the graphics pipelines (15 to 30 seconds).

Online
------
The Multiplayer page of the launcher finds other players without any account or server:
  P2P Direct    ask a player in the list, or paste an invite; both accept and the games connect.
  P2P Unranked  press Find match; searching players are paired by ping.
The two games connect straight to each other. A match needs a router that lets them through; there
is no relay.

In the game
-----------
F1 opens the settings (video, audio, controls, overlays, mods). Settings, saves and mods live in
this folder.
"""


def copy(source: Path, target: Path):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--host-dir", type=Path, default=ROOT / "build-release-080-ns/port/Release")
    parser.add_argument("--game-dir", type=Path, default=ROOT / "build-sourceport-gcc-ns")
    parser.add_argument("--from-package", type=Path, required=True, help="a normal release folder to take the shared runtime files from")
    parser.add_argument("--zip", action="store_true")
    parser.add_argument("--strip", type=Path, help="strip.exe of the toolchain that built the game library")
    args = parser.parse_args()
    version = (ROOT / "VERSION").read_text().strip()
    out = args.out
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for name in HOST_FILES:
        copy(args.host_dir / name, out / name)
    for name in GAME_FILES:
        copy(args.game_dir / name, out / name)
    # The library's symbol table is not loaded and not used at run time (crash reports resolve
    # addresses in melee_game.dbg): leave it out, internal names and all. Exports are kept.
    if args.strip:
        subprocess.check_call([str(args.strip), "--strip-unneeded", str(out / "melee_game.dll")])
    for pattern in RUNTIME_GLOBS:
        for source in args.from_package.glob(pattern):
            copy(source, out / source.name)
    copy(args.from_package / "Sys/GC/dsp_coef.bin", out / "Sys/GC/dsp_coef.bin")
    for source in (args.from_package / "shadercache").rglob("*"):
        if source.is_file():
            copy(source, out / "shadercache" / source.relative_to(args.from_package / "shadercache"))
    for source in (args.from_package / "licenses").iterdir():
        if source.is_file() and source.name not in LICENCE_SKIP:
            text = source.read_bytes()
            if source.name == "NOTICE.txt":   # one notice for both builds: drop the paragraphs about code this one lacks
                paragraphs = re.split(rb"(?:\r?\n){2,}", text)
                text = b"\n\n".join(p for p in paragraphs if not WORD.search(p)) + b"\n"
            (out / "licenses").mkdir(exist_ok=True)
            (out / "licenses" / source.name).write_bytes(text)
    # Language files are shared with the normal build: lines about the online service this build
    # does not have are left out (the launcher falls back to its built-in English for a missing key).
    for source in (ROOT / "lang").glob("*.txt"):
        lines = source.read_bytes().splitlines(keepends=True)
        (out / "lang").mkdir(exist_ok=True)
        (out / "lang" / source.name).write_bytes(b"".join(line for line in lines if not WORD.search(line)))
    # The settings panel's own pictures (menu style previews, the menu kit, fonts).
    for source in (args.from_package / "ui_sources").rglob("*"):
        if source.is_file():
            copy(source, out / "ui_sources" / source.relative_to(args.from_package / "ui_sources"))
    copy(args.from_package / "TrainingMods.md", out / "TrainingMods.md")   # the training packs are in this build
    (out / "README.txt").write_text(README.format(version=version), encoding="utf-8", newline="\r\n")

    total = 0
    for path in sorted(out.rglob("*")):
        if not path.is_file():
            continue
        data = path.read_bytes()
        hits = len(WORD.findall(data)) + len(WORD.findall(data.decode("latin-1").encode("utf-16-le"))) \
            + len(re.findall("slippi".encode("utf-16-le"), data, re.IGNORECASE))
        if hits:
            print(f"  {hits:5d}  {path.relative_to(out)}")
            total += hits
    print(f"scan: {total} mention(s) in {sum(1 for p in out.rglob('*') if p.is_file())} files")
    if args.zip:
        archive = out.with_suffix(".zip")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for path in sorted(out.rglob("*")):
                if path.is_file():
                    z.write(path, f"{out.name}/{path.relative_to(out).as_posix()}")
        print(f"{archive} ({archive.stat().st_size / 1048576:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
