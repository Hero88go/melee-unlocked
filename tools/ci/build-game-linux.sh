#!/bin/bash
# The game library for the macOS app: the native Linux game (melee_game.so + melee_game.snapexcl), built as
# tools/build_linux_release.sh builds it (GCC 14, AVX2, Doubles and Squad Strike) but without stack protector,
# because the Mac host's ELF loader refuses Linux stack canaries (port/mac/elf_loader.cpp). Run on Linux
# (Ubuntu 24.04: gcc-14, g++-14, cmake, ninja-build, binutils):
#   bash tools/ci/build-game-linux.sh <output-dir>
# MU_GAME_BUILD names the build tree (default build-linux-game-mac); CMAKE_C_COMPILER_LAUNCHER=ccache is honoured.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
OUT=${1:?output directory}
BUILD=${MU_GAME_BUILD:-$ROOT/build-linux-game-mac}
JOBS=${MU_JOBS:-$(nproc)}

git submodule update --init -- sourceport/extern/melee
python3 tools/prepare_native_sources.py
launcher=()
if [ -n "${CMAKE_C_COMPILER_LAUNCHER:-}" ]; then
  launcher=("-DCMAKE_C_COMPILER_LAUNCHER=$CMAKE_C_COMPILER_LAUNCHER" "-DCMAKE_CXX_COMPILER_LAUNCHER=$CMAKE_C_COMPILER_LAUNCHER")
fi
cmake -S sourceport/game -B "$BUILD" -G Ninja -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_BUILD_TYPE=Release -DMELEE_CPU_BASELINE=AVX2 -DYOUGAME_AGENT_TEST_HOOK=OFF -DYOUGAME_RANKED_BRANCH=ON \
  -DMELEE_DOUBLES=ON -DMELEE_SQUAD_STRIKE=ON -DCMAKE_C_FLAGS=-fno-stack-protector -DCMAKE_CXX_FLAGS=-fno-stack-protector \
  "${launcher[@]}"
cmake --build "$BUILD" --target melee_game -j "$JOBS"
if nm -D --undefined-only "$BUILD/melee_game.so" | grep -q __stack_chk_fail; then
  echo "melee_game.so still uses the stack protector" >&2
  exit 1
fi
# The release's own check: every function a Gecko code may make "return at once" was not inlined by GCC.
python3 tools/gecko_functions.py --check --linux --objdump objdump "$BUILD/melee_game.dbg"
mkdir -p "$OUT"
cp "$BUILD/melee_game.so" "$BUILD/melee_game.snapexcl" "$OUT/"
ls -l "$OUT"
