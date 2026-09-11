"""Assembles a standalone release folder and zip of the native port.

Contents: melee_port.exe, the Streamline/DLSS runtime DLLs, the Slippi Sys files the EXI device
serves (code tables, game file diffs), a launcher batch file, README and licenses. No game data:
the user supplies their own Melee NTSC 1.02 ISO. Usage:

    python tools/package_release.py --version 0.1.0 [--out release]
"""
import argparse
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

README = """Melee Port {version}
====================

A native Windows build of Super Smash Bros. Melee NTSC 1.02 with Slippi: the game logic runs
exactly as on the GameCube at 60 Hz, the display renders at any rate (unlocked, monitor rate
or a fixed cap) with real in-between animation, DLSS/DLAA, a GameCube adapter and Slippi
online play.

You need your own Melee NTSC 1.02 ISO. Nothing from the game is included.

Quick start
-----------
1. Put your ISO next to MeleePort.bat and name it melee.iso (or edit the batch file).
2. Run MeleePort.bat. The first launch compiles shaders for a few seconds.
3. Press F1 (or Back+Start on the controller) for settings: fullscreen, frame rate cap,
   VSync, widescreen 16:9, internal resolution, DLSS, volume. Settings persist in port-settings.ini.

Controllers: a GameCube adapter (WUP-028, official or Mayflash in Wii U mode) is used
automatically if it has the WinUSB driver that Slippi installs. Close Slippi Dolphin first.
Keyboard: arrows = stick, IJKL = C-stick, Z/X/C/V = A/B/X/Y, Enter = Start, Q/W = L/R, E = Z.

Slippi online: the port uses the account you are logged into in the Slippi Launcher
(user.json in the Launcher's netplay folder). Log in there once. Unranked, Direct codes and
Teams work against players on regular Slippi Dolphin; ranked play is not reported yet.

Bug reports: open an issue on the GitHub releases page with your port-settings.ini, the
console log (run from a command prompt to see it) and the steps to reproduce.

Saves: memory card slot A is the folder User\GC\CardA, one .gci per file (Dolphin's GCI folder
format). Copy your Slippi Dolphin save (GALE01-*.gci) there to keep your unlocks and settings.

Known gaps in this version: game reporting for ranked play is not sent, audio is an approximate
mixer.
"""

BAT = """@echo off
cd /d "%~dp0"
if not exist "melee.iso" (
  echo Put your Melee NTSC 1.02 ISO next to this file and name it melee.iso
  pause
  exit /b 1
)
melee_port.exe --iso "%~dp0melee.iso" --sys-dir "%~dp0Sys" --user-dir "%~dp0User\\Slippi" --replay-dir "%~dp0Replays" --card-dir "%~dp0User\GC\CardA" --threaded-renderer --fps unlocked --frame-mode authored --fullscreen --volume 70 %*
if errorlevel 1 pause
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--version", required=True)
    ap.add_argument("--exe", type=Path, default=ROOT / "build-review/port/Release/melee_port.exe")
    ap.add_argument("--out", type=Path, default=ROOT / "release")
    args = ap.parse_args()
    if not args.exe.is_file():
        raise SystemExit(f"missing executable: {args.exe}")
    name = f"MeleePort-{args.version}"
    folder = args.out / name
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    shutil.copy2(args.exe, folder / "melee_port.exe")
    for dll in ("sl.interposer.dll", "sl.common.dll", "sl.dlss.dll", "nvngx_dlss.dll"):
        src = args.exe.parent / dll
        if src.is_file():
            shutil.copy2(src, folder / dll)
    sys_src = ROOT / "slippi/Data/Sys"
    sys_dst = folder / "Sys"
    (sys_dst / "GameSettings").mkdir(parents=True)
    shutil.copy2(sys_src / "GameSettings/GALE01r2.ini", sys_dst / "GameSettings/GALE01r2.ini")
    shutil.copy2(sys_src / "codehandler.bin", sys_dst / "codehandler.bin")
    shutil.copy2(sys_src / "bootloader.gct", sys_dst / "bootloader.gct")
    shutil.copytree(sys_src / "GameFiles", sys_dst / "GameFiles")
    (folder / "User/Slippi").mkdir(parents=True)
    (folder / "Replays").mkdir()
    (folder / "MeleePort.bat").write_bytes(BAT.replace("\n", "\r\n").encode("utf-8"))
    (folder / "README.txt").write_text(README.format(version=args.version), encoding="utf-8")
    licenses = folder / "licenses"
    licenses.mkdir()
    for src, dst in ((ROOT / "port/third_party/streamline/license.txt", "streamline.txt"),
                     (ROOT / "port/third_party/enet/LICENSE", "enet.txt"),
                     (ROOT / "port/third_party/imgui/LICENSE.txt", "imgui.txt")):
        if src.is_file():
            shutil.copy2(src, licenses / dst)
    zip_path = args.out / f"{name}-win64.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for path in folder.rglob("*"):
            z.write(path, path.relative_to(args.out))
    total = sum(p.stat().st_size for p in folder.rglob("*") if p.is_file())
    print(f"{zip_path} ({zip_path.stat().st_size / 1e6:.1f} MB zipped, {total / 1e6:.1f} MB unpacked)")


if __name__ == "__main__":
    main()
