"""Assembles a standalone release folder and zip of the native port.

Contents: MeleeUnlockedLauncher.exe (optional client), melee_port.exe, the Streamline/DLSS runtime DLLs, the Slippi Sys files the EXI device
serves (code tables, game file diffs), a launcher batch file, README and licenses. No game data:
the user supplies their own Melee NTSC 1.02 ISO. Usage:

    python tools/package_release.py --version 0.1.0 [--out release]
"""
import argparse
import shutil
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

README = """Melee Unlocked {version}
========================

A native Windows build of Super Smash Bros. Melee NTSC 1.02 with Slippi: the game logic runs
exactly as on the GameCube at 60 Hz, the display renders at any rate (unlocked, monitor rate
or a fixed cap) with real in-between animation, DLSS/DLAA, a GameCube adapter and Slippi
online play.

You need your own Melee NTSC 1.02 ISO. Nothing from the game is included.

Not affiliated with, endorsed by, or supported by the Slippi team, Nintendo or HAL Laboratory.
Bugs and questions about this build go to https://github.com/hero88go/melee-unlocked/issues,
not to the Slippi team.

Install: two ways, pick one
---------------------------
The launcher is OPTIONAL. Nothing in the game depends on it.

A. Manual (no launcher)
   1. Drag your Melee NTSC 1.02 ISO onto MeleeUnlocked.bat, or put the ISO next to it named
      melee.iso and double-click MeleeUnlocked.bat.
   2. That is it. The first launch precompiles the graphics pipelines (15 to 30 seconds,
      progress in the title bar). To update, extract a newer zip over this folder; your
      settings, saves and replays are kept.

B. Melee Unlocked Launcher (optional convenience)
   1. Run MeleeUnlockedLauncher.exe and drop the ISO onto its window (Build tab). It checks
      the disc, precompiles the graphics pipelines and remembers the path.
   2. Press PLAY. The launcher checks for new releases on every start and "Update and
      restart" installs one in place. It also shows which Slippi account will be used.

Either way, the PC settings panel opens on the first launch; later press F1 (or Z + Start on
the controller): fullscreen, frame rate cap, VSync, widescreen 16:9, internal resolution,
anti-aliasing (SSAA), anisotropic filtering, DLSS/DLAA, sharpening, sub-frame animation,
game and music volume. Settings persist in port-settings.ini.

Controllers: a GameCube adapter (WUP-028, official or Mayflash in Wii U mode) is used
automatically if it has the WinUSB driver that Slippi installs. Close Slippi Dolphin first.
Keyboard: arrows = stick, IJKL = C-stick, Z/X/C/V = A/B/X/Y, Enter = Start, Q/W = L/R, E = Z.

Slippi online: everything Slippi Dolphin does for netplay (matchmaking, rollback netcode, the
Slippi code set, replays, game reporting) is built into this program, so Slippi Dolphin is not
needed. Is the Slippi Launcher required? For online play, yes: a Slippi account is required and
accounts are created and logged in only through the Slippi Launcher (https://slippi.gg/downloads).
Install it, log in once, and the game picks up the login automatically (the Slippi Launcher also
installs the GameCube adapter driver). For offline play it is not required. Unranked, Direct codes
and Teams work against players on regular Slippi Dolphin.

Bug reports: https://github.com/hero88go/melee-unlocked/issues with melee_port.log,
port-settings.ini and the steps to reproduce.

Saves: memory card slot A is the folder User\GC\CardA, one .gci per file (Dolphin's GCI folder
format). Copy your Slippi Dolphin save (GALE01-*.gci) there to keep your unlocks and settings.

Known gaps in this version: audio is an approximate mixer; ranked play reports results but has
not been tested in a live ranked set.
"""

BAT = """@echo off
cd /d "%~dp0"
set ISO=%~dp0melee.iso
if not "%~1"=="" if exist "%~1" set ISO=%~1
if not exist "%ISO%" (
  echo Drop your Melee NTSC 1.02 ISO onto this file, or put it next to it named melee.iso
  pause
  exit /b 1
)
melee_port.exe --iso "%ISO%" --sys-dir "%~dp0Sys" --user-dir "%~dp0User\Slippi" --discover-launcher-login --replay-dir "%~dp0Replays" --card-dir "%~dp0User\GC\CardA" --threaded-renderer --fps unlocked --frame-mode authored --scale auto --volume 70
if errorlevel 1 pause
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--version", default=(ROOT / "VERSION").read_text().strip())
    ap.add_argument("--exe", type=Path, default=ROOT / "build-review/port/Release/melee_port.exe")
    ap.add_argument("--out", type=Path, default=ROOT / "release")
    args = ap.parse_args()
    if not args.exe.is_file():
        raise SystemExit(f"missing executable: {args.exe}")
    # The version is compiled into the executable, so a build made before VERSION changed would
    # ship reporting the old number and offer itself the update forever. Catch that here.
    built = subprocess.run([str(args.exe), "--version"], capture_output=True, text=True, timeout=60).stdout.strip()
    if built != args.version:
        raise SystemExit(f"{args.exe.name} reports version {built!r} but the release is {args.version!r}; rebuild it first")
    name = f"MeleeUnlocked-{args.version}"
    folder = args.out / name
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    shutil.copy2(args.exe, folder / "melee_port.exe")
    launcher = args.exe.parent / "MeleeUnlockedLauncher.exe"
    if not launcher.is_file():
        raise SystemExit(f"missing launcher: {launcher} (build target melee_unlocked)")
    shutil.copy2(launcher, folder / "MeleeUnlockedLauncher.exe")
    for dll in ("sl.interposer.dll", "sl.common.dll", "sl.dlss.dll", "nvngx_dlss.dll"):
        src = args.exe.parent / dll
        if src.is_file():
            shutil.copy2(src, folder / dll)
    sys_src = ROOT / "port/slippi_sys"
    sys_dst = folder / "Sys"
    (sys_dst / "GameSettings").mkdir(parents=True)
    shutil.copy2(sys_src / "GameSettings/GALE01r2.ini", sys_dst / "GameSettings/GALE01r2.ini")
    shutil.copy2(sys_src / "codehandler.bin", sys_dst / "codehandler.bin")
    shutil.copy2(sys_src / "bootloader.gct", sys_dst / "bootloader.gct")
    shutil.copytree(sys_src / "GameFiles", sys_dst / "GameFiles")
    # Warmed pipeline recipes: the newest cache namespace that has them (the exe's shader sources
    # decide the namespace, so this must come from the same build).
    recipes = ROOT / "shadercache/recipes.bin"
    if recipes.is_file():
        (folder / "shadercache").mkdir()
        shutil.copy2(recipes, folder / "shadercache/recipes.bin")
        print(f"pipeline recipes: {recipes} ({recipes.stat().st_size} bytes)")
    (folder / "User/Slippi").mkdir(parents=True)
    (folder / "Replays").mkdir()
    (folder / "MeleeUnlocked.bat").write_bytes(BAT.replace("\n", "\r\n").encode("utf-8"))
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
