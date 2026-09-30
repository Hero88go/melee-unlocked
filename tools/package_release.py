"""Assembles the Windows release archive with both game builds and optional DLSS 5 support.

Contents: MeleeUnlockedLauncher.exe (optional client), the normal/compatibility legacy
executables, optional Source Port files, the Streamline/DLSS runtime DLLs, the Slippi Sys files
the legacy EXI device serves (code tables, game file diffs), a launcher batch file, README and
licenses. No game data:
the user supplies their own Melee NTSC 1.02 ISO. Usage:

    python tools/package_release.py --version 0.1.0 --exe build-review/port/Release/melee_port.exe \
        --compat-exe build-compat/port/Release/melee_port.exe \
        --experimental-exe build-dlss5/port/Release/melee_port.exe [--out release]
"""
import argparse
import hashlib
import re
import shutil
import subprocess
import zipfile
from pathlib import Path
from verify_generated_gct import verify_generated_gct

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
   3. Multiplayer Lobby: fill out your profile (name, location, three mains; your
      Slippi code comes from your linked Slippi account) and tick Go Online. Players
      exchange chat, friend requests, friend status and match requests directly; ping
      is measured once a match request is sent. Accepted games use Slippi Direct.
      Discovery can take time, and some networks cannot connect directly (no relay yet).

Either way, the PC settings panel opens on the first launch; later press F1 or click Settings:
fullscreen, frame rate cap, VSync, widescreen 16:9 (both Game Builds), internal resolution,
anti-aliasing (SSAA), anisotropic filtering, DLSS/DLAA, sharpening, sub-frame animation,
game and music volume. Settings persist in port-settings.ini.

Game Builds
-----------
The launcher's Build tab picks the Game Build: the Source Port (the game built from source) or the
Static Recomp (the original game code translated ahead of time; mod discs such as Akaneia run on
it). A compatibility build for older processors is included. Optional experimental DLSS 5 uses
NVIDIA's separate model, which is not included in this archive.

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

Crashes: after a crash the launcher offers to send the report (crash text, minidump and logs) with
one click. Other bugs: https://github.com/hero88go/melee-unlocked/issues with melee_port.log,
port-settings.ini and the steps to reproduce.

Watching replays: drop a .slp file onto WatchReplay.bat. That runs melee_port_playback.exe, a
separate build of the game made for playback, so Slippi Dolphin is not needed to watch a replay
either. Your ISO has to be next to it named melee.iso. A replay recorded by a much newer or older
Slippi version may not line up with this build; when that happens the log says so rather than
playing something subtly wrong.

Saves: memory card slot A is the folder User\GC\CardA, one .gci per file (Dolphin's GCI folder
format). Copy your Slippi Dolphin save (GALE01-*.gci) there to keep your unlocks and settings.

Mods: put your own mod files in the Mods folder (or drop them on the launcher's Mods page). The
game recognizes known packs by their content. New supported and untested custom entries turn on;
unsupported versions stay off. Read each entry's compatibility warning. F1 > Mods (or
Start + Down + Z on a controller) has an On/Off switch for each. Training Mode CE and 20XX TE run on
the Source Port: 20XX TE's in-game menu is VS Mode > Tournament Melee, Training Mode CE's exercises
are under 1P Mode > Event Match. Akaneia runs on the Static Recomp (launcher > Mods > Play). The
20XX Training Hack Pack is not supported yet. See TrainingMods.md for compatibility and differences
from the original console mods.

Audio: F1 > Audio offers Auto (recommended: starts at the lowest buffer and backs off only after
real gaps), Low latency (a fixed buffer), Exclusive (takes over the output device), and ASIO(R)
for audio interfaces with a driver and buffer picker. Restart after changing the mode or driver.
ASIO and Exclusive can take the device; sharing depends on the interface driver.

Known gaps in this version: ranked play reports results but has not been tested in a live ranked set.

License and source code
-----------------------
Melee Unlocked is free software under the GNU General Public License, version 3 or (at your
option) any later version: licenses\\COPYING.txt, with the notices in licenses\\NOTICE.txt.
Source code for this version: https://github.com/hero88go/melee-unlocked/tree/v{version}
The NVIDIA (DLSS, Streamline, Reflex) and Intel (XeSS) runtime files and the Microsoft Visual C++
runtime files in this folder are not covered by the GPL; their own terms are in licenses\\.
ASIO is a registered trademark of Steinberg Media Technologies GmbH. The SDK and host-helper
license texts are licenses\\steinberg-asio-sdk.txt and licenses\\steinberg-asio-bsd.txt.
The official compatibility logo and SDK credits are in licenses\\asio.html.
The default DSP coefficient table is the generated free table described in
licenses\\dsp-coefficients.txt. A player's own dump in User\\GC\\dsp_coef.bin takes priority.
"""

BAT = """@echo off
cd /d "%~dp0"
set "ISO=%~dp0melee.iso"
if not "%~1"=="" if exist "%~1" set "ISO=%~1"
if not exist "%ISO%" (
  echo Drop your Melee NTSC 1.02 ISO onto this file, or put it next to it named melee.iso
  pause
  exit /b 1
)
melee_port.exe --iso "%ISO%" --settings-path "%~dp0port-settings.ini" --sys-dir "%~dp0Sys" --user-dir "%~dp0User\Slippi" --replay-dir "%~dp0Replays" --card-dir "%~dp0User\GC\CardA" --threaded-renderer
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


def json_notice():
    """nlohmann/json keeps its MIT notice in the opening comment of json.hpp."""
    text = (ROOT / "port/third_party/nlohmann/json.hpp").read_text(encoding="utf-8")
    end = text.find("*/")
    if not text.startswith("/*") or end < 0 or not re.search(r"Permission is hereby\s+granted", text[:end]):
        raise SystemExit("json.hpp: MIT notice not found at the top of the file")
    return text[2:end].strip("\n") + "\n"


def imgui_font_notice():
    """The fonts compiled into Dear ImGui carry their own MIT notices (imgui_draw.cpp)."""
    text = (ROOT / "port/third_party/imgui/imgui_draw.cpp").read_text(encoding="utf-8")
    blocks = re.findall(r"// \[SECTION\] Default font data \(([^)]+)\)\n//-+\n((?://.*\n)+?)//-+\n", text)
    if not blocks:
        raise SystemExit("imgui_draw.cpp: embedded font notices not found")
    mit = (ROOT / "port/third_party/imgui/LICENSE.txt").read_text(encoding="utf-8")
    grant = re.search(r"Permission is hereby\s+granted", mit)
    if not grant:
        raise SystemExit("imgui LICENSE.txt: MIT permission text not found")
    lines = ["Fonts compiled into Dear ImGui (port/third_party/imgui/imgui_draw.cpp), MIT License:", ""]
    for name, notice in blocks:
        lines += [name] + ["  " + line[2:].strip() for line in notice.splitlines()] + [""]
    return "\n".join(lines) + "\n" + mit[grant.start():]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--version", default=(ROOT / "VERSION").read_text().strip())
    ap.add_argument("--exe", type=Path, default=ROOT / "build-integration/port/Release/melee_port.exe")
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
    # The version is compiled into the executable, so a build made before VERSION changed would
    # ship reporting the old number and offer itself the update forever. Catch that here.
    built = subprocess.run([str(args.exe), "--version"], capture_output=True, text=True, timeout=60).stdout.strip()
    if built != args.version:
        raise SystemExit(f"{args.exe.name} reports version {built!r} but the release is {args.version!r}; rebuild it first")
    for executable in (args.exe, args.compat_exe):
        if executable is not None:
            try:
                verify_generated_gct(executable)
            except ValueError as error:
                raise SystemExit(str(error)) from error
    name = f"MeleeUnlocked-{args.version}"
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
        # The sound driver's snapshot exclusions, written by the game library's build. Without it a
        # rollback restores the sound driver's tables while its voices keep playing: crackle online.
        snapexcl = args.source_dll.with_suffix(".snapexcl")
        if not snapexcl.is_file():
            raise SystemExit(f"missing {snapexcl.name} next to {args.source_dll} (built with melee_game)")
        shutil.copy2(snapexcl, folder / "melee_game.snapexcl")
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
    # Launcher translations (plain UTF-8 text; English is built in).
    lang_src = ROOT / "lang"
    if lang_src.is_dir():
        shutil.copytree(lang_src, folder / "lang", dirs_exist_ok=True)
    runtime_dlls = ("sl.interposer.dll", "sl.common.dll", "sl.dlss.dll", "nvngx_dlss.dll",
                    "sl.dlss_d.dll", "nvngx_dlssd.dll", "sl.dlss_g.dll", "nvngx_dlssg.dll",
                    "sl.reflex.dll", "sl.pcl.dll", "libxess.dll", "nvngx.dll_meleedlss5.dll")
    missing_dlls = [name for name in runtime_dlls if not (args.exe.parent / name).is_file()]
    if missing_dlls:
        raise SystemExit(f"missing runtime DLLs: {', '.join(missing_dlls)}")
    for dll in runtime_dlls:
        src = args.exe.parent / dll
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
    # Ship only the pinned generated free table, never a player's hardware ROM dump.
    coef = sys_src / "GC/dsp_coef.bin"
    if not coef.is_file() or hashlib.sha256(coef.read_bytes()).hexdigest() != \
            "c41e7d9f763da40983e83a5291cbecec1f536fd8b08ac6dda05eab33f57501ea":
        raise SystemExit(f"missing or unrecognized free DSP coefficient table: {coef}")
    (sys_dst / "GC").mkdir(exist_ok=True)
    shutil.copy2(coef, sys_dst / "GC/dsp_coef.bin")
    # Playback normally finds Sys too, but keep its standalone Sys tree complete.
    if args.playback_exe:
        (folder / "SysPlayback/GC").mkdir(exist_ok=True)
        shutil.copy2(coef, folder / "SysPlayback/GC/dsp_coef.bin")
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
    # The program's own license and every third-party notice its files need (GPL-3.0 sections 4
    # and 6; the MIT, BSD, ISC and LGPL texts; the NVIDIA, Intel and DLSS third-party terms). A
    # missing text stops the build instead of shipping a package without it.
    for src, dst in ((ROOT / "LICENSE", "COPYING.txt"),
                     (ROOT / "NOTICE", "NOTICE.txt"),
                     (ROOT / "port/third_party/streamline/license.txt", "streamline.txt"),
                     (ROOT / "port/third_party/streamline/3rd-party-licenses.md", "streamline-third-party.md"),
                     (ROOT / "port/third_party/streamline/reflex.license.txt", "nvidia-reflex.txt"),
                     (ROOT / "port/third_party/ngx/LICENSE.txt", "nvidia-rtx-sdks.txt"),
                     (ROOT / "docs/licenses/nvidia-dlss-third-party.txt", "nvidia-dlss-third-party.txt"),
                     (ROOT / "port/third_party/xess/LICENSE.txt", "intel-xess.txt"),
                     (ROOT / "port/third_party/dht/LICENCE", "dht.txt"),
                     (ROOT / "port/third_party/monocypher/LICENCE.md", "monocypher.md"),
                     (ROOT / "port/third_party/enet/LICENSE", "enet.txt"),
                     (ROOT / "port/third_party/imgui/LICENSE.txt", "imgui.txt"),
                     (ROOT / "port/third_party/libusb/COPYING", "libusb-LGPL-2.1.txt"),
                     (ROOT / "port/third_party/stb/LICENSE", "stb.txt"),
                     (ROOT / "port/third_party/earcut/LICENSE", "earcut.txt"),
                     (ROOT / "port/third_party/asio/LICENSE.txt", "steinberg-asio-sdk.txt"),
                     (ROOT / "port/third_party/asio/NOTICE-BSD.txt", "steinberg-asio-bsd.txt"),
                     (ROOT / "docs/licenses/asio.html", "asio.html"),
                     (ROOT / "docs/licenses/asio-compatible-logo-white.svg", "asio-compatible-logo-white.svg"),
                     (ROOT / "docs/licenses/dsp-coefficients.txt", "dsp-coefficients.txt"),
                     (ROOT / "docs/licenses/hps_decode-MIT.txt", "hps_decode.txt"),
                     (ROOT / "docs/licenses/slippilab-MIT.txt", "slippilab.txt"),
                     (ROOT / "port/runtime/gx/ui_sources/gd_melee/assets/kit/SourceSans3-OFL.md",
                      "source-sans-3-OFL.md")):
        if not src.is_file():
            raise SystemExit(f"missing license text: {src}")
        shutil.copy2(src, licenses / dst)
    mod_tools = ROOT / "port/third_party/mod_tools"
    (folder / "tools").mkdir(exist_ok=True)
    for tool, digest in (("xdelta3.exe", "d81f59b2fe5e8589c0ee9782e231c805084f4d23dfade413903a4cad63b4e342"),
                         ("7zr.exe", "ad4c82fadcbdf93c03b4fc440f300509c7d60c5c2f4d183e35d9d70d6957037d")):
        src = mod_tools / tool
        if not src.is_file() or hashlib.sha256(src.read_bytes()).hexdigest() != digest:
            raise SystemExit(f"missing or altered mod tool: {src}")
        shutil.copy2(src, folder / "tools" / tool)
    for src, dst in (("xdelta-COPYING.txt", "xdelta3-GPL-2.txt"),
                     ("7zip-license.txt", "7zip.txt"), ("SOURCES.txt", "mod-tools-sources.txt")):
        shutil.copy2(mod_tools / src, licenses / dst)
    (licenses / "nlohmann-json.txt").write_text(json_notice(), encoding="utf-8")
    (licenses / "imgui-fonts.txt").write_text(imgui_font_notice(), encoding="utf-8")
    # The settings appearance picker loads these at runtime from beside the executable.
    # Keep the GD import separate so it can be removed without touching the other themes.
    ui_src = ROOT / "port/runtime/gx/ui_sources"
    for source in ("gd_melee", "mockups", "menu_previews", "vendor"):
        assets = ui_src / source / "assets"
        if not assets.is_dir():
            raise SystemExit(f"missing settings appearance assets: {assets}")
        shutil.copytree(assets, folder / "ui_sources" / source)
    shutil.copy2(ROOT / "docs/third-party-ui-attribution.md", licenses / "ui-attribution.md")
    shutil.copy2(ROOT / "docs/third-party-training-mode-ce.txt", licenses / "training-mode-ce.txt")
    shutil.copy2(ROOT / "docs/training-mods.md", folder / "TrainingMods.md")
    shutil.copy2(ui_src / "gd_melee/ORIGIN.md", licenses / "gd-melee-origin.md")
    def zip_folder(zip_path):
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
            for path in folder.rglob("*"):
                z.write(path, path.relative_to(args.out))
        total = sum(p.stat().st_size for p in folder.rglob("*") if p.is_file())
        print(f"{zip_path} ({zip_path.stat().st_size / 1e6:.1f} MB zipped, {total / 1e6:.1f} MB unpacked)")

    path_shader = args.exe.parent / "dxr_pathtrace.dxil"
    if not path_shader.is_file():
        raise SystemExit(f"missing DXR shader: {path_shader}")
    shutil.copy2(path_shader, folder / path_shader.name)
    (folder / "README.txt").write_text(README.format(version=args.version) +
        "\nThis experimental Mod Update includes Source Port and Static Recomp game builds.\n"
        "DLSS 5 requires NVIDIA's separate model (nvngx_dlssnr.dll), which is not included.\n"
        "Experimental DXR path tracing and Ray Reconstruction are off by default.\n",
        encoding="utf-8")
    zip_folder(args.out / f"{name}-win64.zip")


if __name__ == "__main__":
    main()
