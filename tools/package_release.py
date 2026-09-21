"""Assembles a standalone release folder and one combined Stable Recomp Legacy zip.

Contents: MeleeUnlockedLauncher.exe (optional client), the normal/compatibility/DLSS5 legacy
executables, optional Source Port files, the Streamline/DLSS runtime DLLs, the Slippi Sys files
the legacy EXI device serves (code tables, game file diffs), a launcher batch file, README and
licenses. No game data:
the user supplies their own Melee NTSC 1.02 ISO. Usage:

    python tools/package_release.py --version 0.1.0 --exe build-review/port/Release/melee_port.exe \
        --compat-exe build-compat/port/Release/melee_port.exe \
        --experimental-exe build-dlss5/port/Release/melee_port.exe [--out release]
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

Either way, the PC settings panel opens on the first launch; later press F1 or click Settings:
fullscreen, frame rate cap, VSync, widescreen 16:9, internal resolution,
anti-aliasing (SSAA), anisotropic filtering, DLSS/DLAA, sharpening, sub-frame animation,
game and music volume. Settings persist in port-settings.ini.

Build choices
-------------
The launcher defaults to Stable Recomp Legacy. It includes the normal and compatibility legacy
builds and, when present, exposes DLSS 5 Experimental as a separate legacy build choice.
Source Port is an additional offline-only choice when this package includes melee_source.exe and
melee_game.dll. Source Port does not include Slippi yet; switch back to Stable Recomp Legacy for
online play. Its gameplay parity and authored-subframe work are still under validation.

Controllers: a GameCube adapter (WUP-028, official or Mayflash in Wii U mode) is used
automatically if it has the WinUSB driver that Slippi installs. Close Slippi Dolphin first.
Native DualShock 4 support is experimental: connect by USB or Bluetooth, then select a DS4
tab in PC settings and assign it to a game port. The DS4 is read directly through Windows
Raw Input and does not require DS4Windows. USB and Bluetooth report layouts are supported.
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

Watching replays: drop a .slp file onto WatchReplay.bat. That runs melee_port_playback.exe, a
separate build of the game made for playback, so Slippi Dolphin is not needed to watch a replay
either. Your ISO has to be next to it named melee.iso. A replay recorded by a much newer or older
Slippi version may not line up with this build; when that happens the log says so rather than
playing something subtly wrong.

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
rem Frame rate, frame mode, internal resolution and volume are saved in port-settings.ini, and the
rem command line is applied after that file is read, so passing them every time would undo whatever
rem was set in the F1 panel. They only seed a first run, before a settings file exists.
set FIRSTRUN=
if not exist "%~dp0port-settings.ini" set FIRSTRUN=--fps unlocked --frame-mode authored --scale auto --volume 70
melee_port.exe --iso "%ISO%" --sys-dir "%~dp0Sys" --user-dir "%~dp0User\Slippi" --replay-dir "%~dp0Replays" --card-dir "%~dp0User\GC\CardA" --threaded-renderer %FIRSTRUN%
if errorlevel 1 pause
"""

# Playback is its own program (a second translation of the game against the Slippi Playback code
# set), so it gets its own launcher: drop a .slp on it, or leave one next to it.
PLAYBACK_BAT = """@echo off
cd /d "%~dp0"
set ISO=%~dp0melee.iso
if not exist "%ISO%" (
  echo Put your Melee NTSC 1.02 ISO next to this file, named melee.iso
  pause
  exit /b 1
)
set REPLAY=%~1
if "%REPLAY%"=="" (
  echo Drop a Slippi replay ^(.slp^) onto this file to watch it.
  pause
  exit /b 1
)
if not exist "%REPLAY%" (
  echo Cannot find "%REPLAY%"
  pause
  exit /b 1
)
melee_port_playback.exe --iso "%ISO%" --replay "%REPLAY%" --sys-dir "%~dp0SysPlayback" --card-dir "%~dp0User\\GC\\CardA" --threaded-renderer
if errorlevel 1 pause
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--version", default=(ROOT / "VERSION").read_text().strip())
    ap.add_argument("--exe", type=Path, default=ROOT / "build-review/port/Release/melee_port.exe")
    # The same game built for processors without AVX2, shipped alongside so the ordinary build
    # keeps its instruction set. The launcher picks between them by asking the processor.
    # Required, not just optional: a release silently missing this file leaves every pre-Haswell/
    # pre-Ryzen machine (a real and recurring support case) unable to start the game at all, with
    # no clear error before 0.6.2 shipped without it by accident. Pass --skip-compat-exe only when
    # that omission is deliberate (e.g. a quick local test build).
    ap.add_argument("--compat-exe", type=Path, default=None,
                    help="melee_port.exe built with -DMELEE_CPU_BASELINE=SSE2 (required unless --skip-compat-exe)")
    ap.add_argument("--skip-compat-exe", action="store_true",
                    help="explicitly ship without the SSE2 compatibility build (not recommended for a real release)")
    ap.add_argument("--experimental-exe", type=Path, required=True,
                    help="experimental game executable, built with MELEE_ENABLE_DLSS5=ON")
    ap.add_argument("--experimental-compat-exe", type=Path, default=None,
                    help="optional: experimental game executable built with SSE2 (RTX 50 machines have AVX2)")
    # Replay playback: a second translation of the game against the Slippi Playback code set, so
    # the release can play a .slp back. It carries its own Sys folder because its code list differs
    # from the online one (see PORT_COMPLETION.md, "Replay playback build").
    ap.add_argument("--playback-exe", type=Path, default=None,
                    help="melee_port_playback.exe, built from port/generated_playback")
    ap.add_argument("--source-exe", type=Path, default=None,
                    help="optional melee_source.exe from the native Source Port build")
    ap.add_argument("--source-dll", type=Path, default=None,
                    help="optional melee_game.dll paired with --source-exe")
    ap.add_argument("--source-dbg", type=Path, default=None,
                    help="optional melee_game.dbg paired with --source-dll")
    ap.add_argument("--out", type=Path, default=ROOT / "release")
    args = ap.parse_args()
    if not args.compat_exe and not args.skip_compat_exe:
        raise SystemExit("missing --compat-exe (the SSE2 build for pre-Haswell/pre-Ryzen CPUs). "
                          "Pass --skip-compat-exe if this omission is deliberate.")
    if not args.exe.is_file():
        raise SystemExit(f"missing executable: {args.exe}")
    if bool(args.source_exe) != bool(args.source_dll):
        raise SystemExit("--source-exe and --source-dll must be supplied together")
    if args.source_exe and (not args.source_exe.is_file() or not args.source_dll.is_file()):
        raise SystemExit("missing Source Port executable or DLL")
    # A standard build passed as --experimental-exe was never built with MELEE_ENABLE_DLSS5=ON, so
    # it would ship as the "DLSS5-Experimental" download while behaving like the standard build and
    # missing nvngx.dll_meleedlss5.dll (caught below), or worse, silently sharing the same file with
    # no forwarder check if that ever changes. Reject the mistake outright rather than rely on the
    # forwarder check alone to catch it.
    if args.experimental_exe.resolve() == args.exe.resolve():
        raise SystemExit("--experimental-exe is the same file as --exe. Build it separately with "
                          "-DMELEE_ENABLE_DLSS5=ON; do not reuse the standard executable.")
    for experimental in [e for e in (args.experimental_exe, args.experimental_compat_exe) if e]:
        if not experimental.is_file():
            raise SystemExit(f"missing experimental executable: {experimental}")
        built_experimental = subprocess.run([str(experimental), "--version"], capture_output=True,
                                            text=True, timeout=60).stdout.strip()
        if built_experimental != args.version:
            raise SystemExit(f"{experimental} reports {built_experimental!r}, not {args.version!r}")
    # The version is compiled into the executable, so a build made before VERSION changed would
    # ship reporting the old number and offer itself the update forever. Catch that here.
    built = subprocess.run([str(args.exe), "--version"], capture_output=True, text=True, timeout=60).stdout.strip()
    if built != args.version:
        raise SystemExit(f"{args.exe.name} reports version {built!r} but the release is {args.version!r}; rebuild it first")
    name = f"MeleeUnlocked-{args.version}-Stable-Recomp-Legacy"
    folder = args.out / name
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    shutil.copy2(args.exe, folder / "melee_port.exe")
    if args.compat_exe:
        if not args.compat_exe.is_file():
            raise SystemExit(f"missing compatibility executable: {args.compat_exe}")
        compat_version = subprocess.run([str(args.compat_exe), "--version"], capture_output=True,
                                        text=True, timeout=60).stdout.strip()
        if compat_version != args.version:
            raise SystemExit(f"the compatibility build reports {compat_version!r}, not {args.version!r}")
        shutil.copy2(args.compat_exe, folder / "melee_port_compat.exe")
        print(f"compatibility build: {args.compat_exe}")
    if args.playback_exe:
        if not args.playback_exe.is_file():
            raise SystemExit(f"missing playback executable: {args.playback_exe}")
        playback_version = subprocess.run([str(args.playback_exe), "--version"], capture_output=True,
                                          text=True, timeout=60).stdout.strip()
        if playback_version != args.version:
            raise SystemExit(f"the playback build reports {playback_version!r}, not {args.version!r}")
        shutil.copy2(args.playback_exe, folder / "melee_port_playback.exe")
        # It reads the playback code set, not the online one, so both ship.
        playback_sys = ROOT / "port/slippi_sys_playback"
        if not (playback_sys / "codehandler.bin").is_file():
            raise SystemExit(f"missing playback Sys folder: {playback_sys}")
        shutil.copytree(playback_sys, folder / "SysPlayback",
                        ignore=shutil.ignore_patterns("README.md", ".git*"))
        print(f"playback build: {args.playback_exe}")
    if args.source_exe:
        source_version = subprocess.run([str(args.source_exe), "--version"], capture_output=True,
                                        text=True, timeout=60).stdout.strip()
        if source_version != args.version:
            raise SystemExit(f"the Source Port executable reports {source_version!r}, not {args.version!r}")
        shutil.copy2(args.source_exe, folder / "melee_source.exe")
        shutil.copy2(args.source_dll, folder / "melee_game.dll")
        if args.source_dbg:
            if not args.source_dbg.is_file():
                raise SystemExit(f"missing Source Port debug file: {args.source_dbg}")
            shutil.copy2(args.source_dbg, folder / "melee_game.dbg")
        print(f"source port: {args.source_exe} + {args.source_dll}")
    launcher = args.exe.parent / "MeleeUnlockedLauncher.exe"
    if not launcher.is_file():
        raise SystemExit(f"missing launcher: {launcher} (build target melee_unlocked)")
    # The launcher is what shows the version and checks for updates, and it is a separate
    # executable with the version compiled into it just like the game. 0.2.0 shipped with a
    # launcher built before VERSION changed, so it called itself 0.1.14, saw 0.2.0 on GitHub and
    # offered the same update forever, which updating could never fix. It is a GUI program and
    # cannot answer --version on a pipe, so look for the version string in the binary instead.
    if args.version.encode() not in launcher.read_bytes():
        raise SystemExit(f"{launcher.name} does not contain the string {args.version!r}, so it was built "
                         f"before VERSION changed; build the melee_unlocked target and try again")
    shutil.copy2(launcher, folder / "MeleeUnlockedLauncher.exe")
    for dll in ("sl.interposer.dll", "sl.common.dll", "sl.dlss.dll", "nvngx_dlss.dll", "sl.dlss_g.dll", "nvngx_dlssg.dll", "sl.reflex.dll", "sl.pcl.dll", "libxess.dll"):
        src = args.exe.parent / dll
        if src.is_file():
            shutil.copy2(src, folder / dll)
    # App-local Visual C++ runtime (Microsoft permits redistributing these next to the exe): without it
    # a PC that never installed the VC++ 2015-2022 redistributable closes the game before it can log.
    redist = sorted(Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC").glob("*/x64/Microsoft.VC143.CRT"))
    if not redist:
        raise SystemExit("Visual C++ runtime redistributable not found (VC\\Redist\\MSVC\\*\\x64\\Microsoft.VC143.CRT)")
    for dll in redist[-1].glob("*.dll"):
        shutil.copy2(dll, folder / dll.name)
    print(f"visual c++ runtime: {redist[-1]}")
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
    if args.playback_exe:
        (folder / "WatchReplay.bat").write_bytes(PLAYBACK_BAT.replace("\n", "\r\n").encode("utf-8"))
    (folder / "README.txt").write_text(README.format(version=args.version), encoding="utf-8")
    licenses = folder / "licenses"
    licenses.mkdir()
    for src, dst in ((ROOT / "port/third_party/streamline/license.txt", "streamline.txt"),
                     (ROOT / "port/third_party/enet/LICENSE", "enet.txt"),
                     (ROOT / "port/third_party/imgui/LICENSE.txt", "imgui.txt"),
                     (ROOT / "port/third_party/streamline/reflex.license.txt", "nvidia-reflex.txt"),
                     (ROOT / "port/third_party/xess/LICENSE.txt", "intel-xess.txt")):
        if src.is_file():
            shutil.copy2(src, licenses / dst)
    def zip_folder(zip_path):
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
            for path in folder.rglob("*"):
                z.write(path, path.relative_to(args.out))
        total = sum(p.stat().st_size for p in folder.rglob("*") if p.is_file())
        print(f"{zip_path} ({zip_path.stat().st_size / 1e6:.1f} MB zipped, {total / 1e6:.1f} MB unpacked)")

    shutil.copy2(args.experimental_exe, folder / "melee_port_dlss5.exe")
    if args.experimental_compat_exe:
        shutil.copy2(args.experimental_compat_exe, folder / "melee_port_dlss5_compat.exe")
    forwarder = args.experimental_exe.parent / "nvngx.dll_meleedlss5.dll"
    if not forwarder.is_file():
        raise SystemExit(f"missing experimental forwarder: {forwarder}")
    shutil.copy2(forwarder, folder / forwarder.name)
    (folder / "README.txt").write_text(README.format(version=args.version) +
        "\nEXPERIMENTAL DLSS 5: Choose DLSS 5 Experimental in the launcher. Requires an RTX 50-series GPU or newer.\n"
        "DLSS 5 will not work without NVIDIA's DLSS 5 file (nvngx_dlssnr.dll). It is not included and\n"
        "we do not provide it. Without it the game runs normally and F1 says DLSS 5 could not start.\n",
        encoding="utf-8")
    zip_folder(args.out / f"{name}-win64.zip")


if __name__ == "__main__":
    main()
