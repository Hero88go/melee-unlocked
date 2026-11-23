#!/bin/bash
# The macOS release asset, MeleeUnlocked-<VERSION>-macos.zip, from this tree (.github/workflows/macos-release.yml).
#   bash tools/ci/build-macos.sh host <game-dir>   generated headers, then the host and MeleeUnlocked.app
#   bash tools/ci/build-macos.sh bundle            ad-hoc sign, zip, verify
# <game-dir> holds melee_game.so and melee_game.snapexcl from tools/ci/build-game-linux.sh (the Linux game built
# without stack protector, which the Mac ELF loader requires). Inputs: tools/ci/macos-deps.sh. No disc is used:
# the two generated headers come from the symbol map (port/recomp/host_headers.py).
# The app is x86_64 (Rosetta 2 on Apple silicon): it loads the x86-64 game ELF into its own process.
# Doubles and Squad Strike are built in, as in tools/build_linux_release.sh, so the host matches the game.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
VERSION=$(tr -d '[:space:]' < VERSION)
APP_DIR="$ROOT/build-mac-full-squad"   # port/mac/build_full.py's directory for a Squad Strike build
OUT=${MU_MAC_OUT:-$ROOT/build-mac-release}
ZIP="$OUT/MeleeUnlocked-$VERSION-macos.zip"
export MELEE_SQUAD_STRIKE=ON

case "${1:-}" in
  host)
    game=$(cd "${2:?game directory}" && pwd)
    for f in melee_game.so melee_game.snapexcl; do
      [ -f "$game/$f" ] || { echo "missing $game/$f" >&2; exit 1; }
    done
    # package_full.py takes the free DSP table from the game's folder, as the Linux release ships it.
    mkdir -p "$game/Sys/GC"
    cp port/slippi_sys/GC/dsp_coef.bin "$game/Sys/GC/dsp_coef.bin"
    python3 -B port/recomp/host_headers.py --out build-mac-full/gen/port-generated
    MELEE_GAME_LIBRARY="$game/melee_game.so" python3 -B port/mac/build_full.py --doubles --squad-strike
    ;;
  bundle)
    app="$APP_DIR/MeleeUnlocked.app"
    [ -d "$app" ] || { echo "missing $app (run the host step)" >&2; exit 1; }
    # The executable's debug map names the build's object files by absolute path; nothing reads it at run time.
    strip -S "$app/Contents/MacOS/MeleeUnlocked"
    # port/mac/build_full.py also adds the build's SDL folder as an rpath so the bare executable runs in place;
    # the bundle finds SDL in Frameworks/ (@executable_path/../Frameworks), so drop every absolute rpath.
    otool -l "$app/Contents/MacOS/MeleeUnlocked" | awk '$1 == "path" && $2 ~ /^\// { print $2 }' |
      while read -r rpath; do
        install_name_tool -delete_rpath "$rpath" "$app/Contents/MacOS/MeleeUnlocked"
      done
    # Ad-hoc signature (no identity): nested programs first, then the bundle. Players still confirm the first
    # launch (System Settings > Privacy & Security > Open Anyway), as for any app without a Developer ID.
    codesign --force -s - "$app/Contents/Frameworks/libSDL2-2.0.0.dylib" "$app"/Contents/Resources/bin/*
    codesign --force --deep -s - "$app"
    codesign --verify --deep --strict "$app"
    mkdir -p "$OUT"
    rm -f "$ZIP"
    ditto -c -k --norsrc --noextattr --keepParent "$app" "$ZIP"
    python3 -B tools/ci/verify_macos_package.py "$ZIP" --version "$VERSION" --forbid "$ROOT" --forbid "$HOME"
    # The signature must survive the zip: unpack a copy and check it again.
    check=$(mktemp -d)
    ditto -x -k "$ZIP" "$check"
    codesign --verify --deep --strict "$check/MeleeUnlocked.app"
    rm -rf "$check"
    echo "zip=$ZIP"
    if [ -n "${GITHUB_OUTPUT:-}" ]; then
      echo "zip=$ZIP" >> "$GITHUB_OUTPUT"
    fi
    ;;
  *)
    echo "usage: $0 host <game-dir> | bundle" >&2
    exit 2
    ;;
esac
