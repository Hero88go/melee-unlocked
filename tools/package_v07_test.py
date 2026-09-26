"""Build a private v0.7 Windows ZIP with one integrated game executable."""

import argparse
import datetime
import shutil
import subprocess
import zipfile
from pathlib import Path
from verify_generated_gct import verify_generated_gct


ROOT = Path(__file__).resolve().parents[1]
RUNTIME_DLLS = (
    "libxess.dll", "nvngx.dll_meleedlss5.dll", "nvngx_dlss.dll", "nvngx_dlssd.dll", "nvngx_dlssg.dll",
    "sl.common.dll", "sl.dlss.dll", "sl.dlss_d.dll", "sl.dlss_g.dll", "sl.interposer.dll",
    "sl.pcl.dll", "sl.reflex.dll",
)
RUNTIME_ASSETS = ("dxr_pathtrace.dxil",)

BAT = r'''@echo off
setlocal
cd /d "%~dp0"
set "ISO=%~1"
if "%ISO%"=="" set "ISO=%~dp0melee.iso"
if not exist "%ISO%" (
  echo Put your clean Melee NTSC 1.02 ISO beside this file as melee.iso,
  echo or drag the ISO onto this batch file.
  pause
  exit /b 2
)
"%~dp0melee_port.exe" --iso "%ISO%" --settings-path "%~dp0port-settings.ini" --sys-dir "%~dp0Sys" --user-dir "%~dp0User\Slippi" --replay-dir "%~dp0Replays" --card-dir "%~dp0User\GC\CardA"
if errorlevel 1 pause
'''

README = """Melee Unlocked v0.7 — private Windows x64 test build

Supply your own clean Melee NTSC 1.02 ISO. The ISO, mod archives, videos, and
NVIDIA DLSS 5 model are not in this ZIP.

Drag your ISO onto MeleeUnlocked-Test.bat, or place it beside the BAT as melee.iso.
The optional MeleeUnlockedLauncher.exe can remember your ISO and start the same game.
F1 or Start + D-pad Down + Z opens PC settings. Tab opens native-practice matchmaking during offline play.
The six pictured appearances include Clean side, Icon tiles, GD Melee, Radial, Wide tabs,
and Simple. New installs start with the pink Clean side menu. Enable Legacy Menu in Customize
to open the original compact v0.6.6 settings presentation with F11. Both menus edit the same
settings. Each modern appearance remembers one of four palettes.
Controls has a Controller rumble switch that stops adapter and Xbox vibration immediately when
turned off and saves the choice for later launches.
This package contains one integrated game build. DLSS 5 is available on supported hardware;
the existing rendering options remain in the same executable. Run MeleeUnlocked-Test.bat
and supply your ISO when prompted.

The integrated executable has experimental DLSS 5 controls, including 0–1000%
sliders. Values beyond 200% are undocumented by NVIDIA and have not been verified
with an authenticated model on this machine. The feature stays off if the model
cannot start. 2x, 6x, and Dynamic NVIDIA Frame Generation produced generated
frames in local two-client Unranked tests on an RTX 5070; fixed 3x–5x modes still
need individual verification. Experimental D3D12 DXR diffuse path tracing and DLSS
Ray Reconstruction are off by default. They were exercised on an RTX 5070, but final
image quality and sustained GPU cost still need validation. The path pass is hybrid:
it keeps the game's raster materials and direct lighting, then traces diffuse bounces.

This is a private test build, not an official release; do not redistribute it as one.
Third-party licence and attribution notices are in the licenses folder.
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build-integration/port/Release")
    parser.add_argument("--out", type=Path, default=None,
                        help="package folder (default: build-integration/MeleeUnlocked-<version>-Windows-x64-test-<date>-<time>)")
    args = parser.parse_args()
    build = args.build.resolve()
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if version.split(".")[:2] != ["0", "7"]:
        raise SystemExit(f"expected v0.7.x sources; VERSION is {version!r}")
    if args.out is None:
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M")
        args.out = ROOT / "build-integration" / f"MeleeUnlocked-{version}-Windows-x64-test-{stamp}"
    out = args.out.resolve()
    if out.exists():
        raise SystemExit(f"refusing to replace existing package: {out}")
    exe = build / "melee_port.exe"
    launcher = build / "MeleeUnlockedLauncher.exe"
    if not launcher.is_file():
        raise SystemExit(f"missing optional launcher build: {launcher}")
    reported = subprocess.check_output([str(exe), "--version"], text=True, timeout=30).strip()
    if reported != version:
        raise SystemExit(f"built executable reports {reported!r}, expected {version!r}")
    try:
        verify_generated_gct(exe)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    missing = [name for name in RUNTIME_DLLS if not (build / name).is_file()]
    if missing:
        raise SystemExit(f"missing runtime DLLs: {', '.join(missing)}")
    missing_assets = [name for name in RUNTIME_ASSETS if not (build / name).is_file()]
    if missing_assets:
        raise SystemExit(f"missing runtime assets: {', '.join(missing_assets)}")
    out.mkdir(parents=True)
    shutil.copy2(exe, out / "melee_port.exe")
    shutil.copy2(launcher, out / "MeleeUnlockedLauncher.exe")
    for name in RUNTIME_DLLS:
        shutil.copy2(build / name, out / name)
    for name in RUNTIME_ASSETS:
        shutil.copy2(build / name, out / name)
    vc_roots = sorted(Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC")
                      .glob("*/x64/Microsoft.VC143.CRT"))
    if not vc_roots:
        raise SystemExit("the Visual C++ x64 runtime redistributable was not found")
    for dll in vc_roots[-1].glob("*.dll"):
        shutil.copy2(dll, out / dll.name)
    shutil.copytree(ROOT / "port/slippi_sys", out / "Sys")
    (out / "User" / "Slippi").mkdir(parents=True)
    (out / "User" / "GC" / "CardA").mkdir(parents=True)
    (out / "Replays").mkdir()
    # New test installs should open on the pink Clean side appearance. Keep the
    # rest of the player's settings at runtime defaults; settings are saved here.
    (out / "port-settings.ini").write_text("overlaystyle 0\n", encoding="utf-8")
    for name in ("gd_melee", "mockups", "menu_previews"):
        assets = ROOT / "port/runtime/gx/ui_sources" / name / "assets"
        if not assets.is_dir():
            raise SystemExit(f"missing menu art: {assets}")
        shutil.copytree(assets, out / "ui_sources" / name)
    recipes = ROOT / "shadercache/recipes.bin"
    if recipes.is_file():
        (out / "shadercache").mkdir()
        shutil.copy2(recipes, out / "shadercache/recipes.bin")
    # Only licence and attribution notices ship. Internal working notes (test notes, feature list,
    # desync audit) stay in the repository; a test package never carries them.
    (out / "licenses").mkdir()
    shutil.copy2(ROOT / "docs/third-party-ui-attribution.md", out / "licenses/ui-attribution.md")
    shutil.copy2(ROOT / "port/runtime/gx/ui_sources/gd_melee/ORIGIN.md", out / "licenses/gd-melee-origin.md")
    for source, name in (("LICENSE", "LICENSE"),
                         ("port/third_party/streamline/license.txt", "NVIDIA-Streamline-license.txt"),
                         ("port/third_party/streamline/reflex.license.txt", "NVIDIA-Reflex-license.txt"),
                         ("port/third_party/xess/LICENSE.txt", "Intel-XeSS-license.txt")):
        shutil.copy2(ROOT / source, out / name)
    (out / "MeleeUnlocked-Test.bat").write_bytes(BAT.replace("\n", "\r\n").encode("utf-8"))
    (out / "README-test.txt").write_text(README, encoding="utf-8")
    archive = out.parent / f"{out.name}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in out.rglob("*"):
            z.write(path, Path(out.name) / path.relative_to(out))
    print(f"{archive} ({archive.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
