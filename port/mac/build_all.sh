#!/bin/bash
# Builds MeleeUnlocked.app from a clone: the pinned decomp with the native patches, melee_game.so with
# the Linux cross GCC, the recompiled headers from your disc, then the host and the bundle
# (build_full.py). x86_64 (the default) runs under Rosetta, in build-mac-full-squad/; --arch arm64 is
# native on Apple silicon, in build-mac-full-arm64-squad/. Run port/mac/build_deps.sh once first.
# Later runs rebuild only what changed.
#   port/mac/build_all.sh [--arch arm64] path/to/melee-ntsc-1.02.iso
set -euo pipefail
cd "$(dirname "$0")/../.."
ARCH=x86_64
if [ "${1:-}" = "--arch" ]; then ARCH="${2:?--arch takes x86_64 or arm64}"; shift 2; fi
case "$ARCH" in
  x86_64) CROSS=x86_64; SUFFIX="" ;;
  arm64) CROSS=aarch64; SUFFIX="-arm64" ;;
  *) echo "--arch takes x86_64 or arm64" >&2; exit 1 ;;
esac
ISO="${1:?usage: port/mac/build_all.sh [--arch arm64] path/to/melee-ntsc-1.02.iso}"
[ -f "$ISO" ] || { echo "No disc at $ISO" >&2; exit 1; }
DEPS="$(cd .. && pwd)/deps"
for need in sdl2/lib/libSDL2-2.0.0.dylib tools/bin/glslangValidator tools/bin/spirv-cross \
            cross/$CROSS-unknown-linux-gnu/bin/$CROSS-unknown-linux-gnu-gcc; do
  [ -e "$DEPS/$need" ] || { echo "Missing $DEPS/$need: run port/mac/build_deps.sh" >&2; exit 1; }
done
[ -f "$DEPS/libusb/lib/libusb-1.0.a" ] || echo "No libusb in $DEPS: the app will not read a GameCube adapter directly" >&2

echo "== decomp sources"
[ -e sourceport/extern/melee/.git ] || git submodule update --init --depth 1 sourceport/extern/melee
python3 tools/prepare_native_sources.py

echo "== melee_game.so ($ARCH)"
# The Linux release's game options, so the host is built with Doubles and Squad Strike to match, and no
# stack protector: the Mac loader has no thread block for the canary and refuses such a library.
GAME=build-mac-game$SUFFIX INPUT=build-mac-input$SUFFIX
cmake -S sourceport/game -B $GAME -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/mac/linux-$CROSS-cross.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DMELEE_CPU_BASELINE=AVX2 -DYOUGAME_AGENT_TEST_HOOK=OFF -DYOUGAME_RANKED_BRANCH=ON \
  -DMELEE_DOUBLES=ON -DMELEE_SQUAD_STRIKE=ON \
  -DCMAKE_C_FLAGS=-fno-stack-protector -DCMAKE_CXX_FLAGS=-fno-stack-protector >/dev/null
if ! cmake --build $GAME --target melee_game -j 8 > $GAME/build.log 2>&1; then
  tail -30 $GAME/build.log; exit 1
fi
mkdir -p $INPUT/Sys/GC
for f in melee_game.so melee_game.snapexcl; do
  cmp -s "$GAME/$f" "$INPUT/$f" || cp "$GAME/$f" $INPUT/
done
cp port/slippi_sys/GC/dsp_coef.bin $INPUT/Sys/GC/

echo "== recompiled headers"
mkdir -p build-mac-full/gen
python3 tools/extract_dol.py "$ISO" build-mac-full/gen/main.dol >/dev/null
GCT_BASE="$(sed -n 's/^GCT_BASE = //p' tools/native_recording_codes.py)"
rm -rf build-mac-full/gen/port-generated.new
python3 port/recomp/recomp.py --dol build-mac-full/gen/main.dol --gct-base "$GCT_BASE" \
  --out build-mac-full/gen/port-generated.new >/dev/null
# Only changed files are replaced, so unchanged headers do not recompile the whole host.
mkdir -p build-mac-full/gen/port-generated
rsync -r --checksum --delete build-mac-full/gen/port-generated.new/ build-mac-full/gen/port-generated/
rm -rf build-mac-full/gen/port-generated.new

echo "== host and app ($ARCH)"
python3 -B port/mac/build_full.py --arch $ARCH --squad-strike --doubles
echo "Built build-mac-full$SUFFIX-squad/MeleeUnlocked.app; open it from Finder or with: open build-mac-full$SUFFIX-squad/MeleeUnlocked.app"
