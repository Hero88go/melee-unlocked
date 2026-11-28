#!/bin/bash
# Builds the macOS app's third-party inputs from pinned, digest-checked source releases:
#   <deps>/sdl2/{include/SDL2,lib/libSDL2-2.0.0.dylib}       SDL2, linked by the host and shipped in Frameworks/
#   <deps>/tools/bin/{glslangValidator,spirv-cross}           the shader tools shipped in Resources/bin/
# Universal (x86_64 + arm64): the host is x86_64 and uses the x86_64 slices; the tools run natively either way.
# <deps> is MELEE_MAC_DEPS (default: deps/ beside the checkout, where port/mac/build_full.py looks).
# Already-built inputs with the same versions are kept, so a restored cache costs nothing.
#   bash tools/ci/macos-deps.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEPS=${MELEE_MAC_DEPS:-$(dirname "$ROOT")/deps}
WORK=${MU_CI_WORK:-$ROOT/build-mac-ci}/deps-src
JOBS=${MELEE_MAC_JOBS:-$(sysctl -n hw.ncpu)}
ARCHS="x86_64;arm64"
MIN_MACOS=11.0

SDL_VERSION=2.30.9
SDL_URL=https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz
SDL_SHA256=24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4
GLSLANG_VERSION=15.0.0
GLSLANG_URL=https://github.com/KhronosGroup/glslang/archive/refs/tags/$GLSLANG_VERSION.tar.gz
GLSLANG_SHA256=c31c8c2e89af907507c0631273989526ee7d5cdf7df95ececd628fd7b811e064
SPIRV_CROSS_VERSION=vulkan-sdk-1.3.296.0
SPIRV_CROSS_URL=https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/$SPIRV_CROSS_VERSION.tar.gz
SPIRV_CROSS_SHA256=4f7f9a8a643e6694f155712016b9b572c13a9444e65b3f43b27bb464c0ab76e0

STAMP="$DEPS/VERSIONS.txt"
WANT="sdl2 $SDL_VERSION; glslang $GLSLANG_VERSION; spirv-cross $SPIRV_CROSS_VERSION; $ARCHS; macOS $MIN_MACOS; relative paths"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ] &&
   [ -f "$DEPS/sdl2/lib/libSDL2-2.0.0.dylib" ] && [ -x "$DEPS/tools/bin/glslangValidator" ] && [ -x "$DEPS/tools/bin/spirv-cross" ]; then
  echo "deps: up to date in $DEPS ($WANT)"
  exit 0
fi

# fetch <url> <sha256> <dir>: download, check the digest, unpack into a fresh <dir>.
fetch() {
  local archive="$WORK/$(basename "$3").tar.gz"
  mkdir -p "$WORK"
  [ -f "$archive" ] || curl -fsSL --retry 3 -o "$archive" "$1"
  if [ "$(shasum -a 256 "$archive" | cut -d' ' -f1)" != "$2" ]; then
    rm -f "$archive"
    echo "deps: SHA-256 mismatch for $1" >&2
    exit 1
  fi
  rm -rf "$3" && mkdir -p "$3"
  tar -xzf "$archive" -C "$3" --strip-components 1
}
# logged <log> <command...>: full output to the log, its tail on failure.
logged() {
  local log=$1; shift
  "$@" > "$log" 2>&1 || { tail -n 60 "$log" >&2; echo "deps: failed: $*" >&2; exit 1; }
}
# Source paths compiled into the binaries (SDL's assertions use __FILE__) are made relative, so the shipped files
# carry no build-machine path.
configure() {
  local map="-ffile-prefix-map=$1=$(basename "$1")"
  logged "$1.configure.log" cmake -S "$1" -B "$1/_build" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=$ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN_MACOS "-DCMAKE_C_FLAGS=$map" "-DCMAKE_CXX_FLAGS=$map" "${@:2}"
}

rm -rf "$DEPS/sdl2" "$DEPS/tools" "$STAMP"
mkdir -p "$DEPS/tools/bin"

fetch "$SDL_URL" "$SDL_SHA256" "$WORK/sdl2"
configure "$WORK/sdl2" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF "-DCMAKE_INSTALL_PREFIX=$DEPS/sdl2"
logged "$WORK/sdl2.build.log" cmake --build "$WORK/sdl2/_build" --parallel "$JOBS"
logged "$WORK/sdl2.install.log" cmake --install "$WORK/sdl2/_build"

fetch "$GLSLANG_URL" "$GLSLANG_SHA256" "$WORK/glslang"
configure "$WORK/glslang" -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DBUILD_SHARED_LIBS=OFF -DENABLE_GLSLANG_BINARIES=ON
logged "$WORK/glslang.build.log" cmake --build "$WORK/glslang/_build" --target glslang-standalone --parallel "$JOBS"
cp "$WORK/glslang/_build/StandAlone/glslang" "$DEPS/tools/bin/glslangValidator"

fetch "$SPIRV_CROSS_URL" "$SPIRV_CROSS_SHA256" "$WORK/spirv-cross"
configure "$WORK/spirv-cross" -DSPIRV_CROSS_SHARED=OFF -DSPIRV_CROSS_STATIC=ON -DSPIRV_CROSS_CLI=ON -DSPIRV_CROSS_ENABLE_TESTS=OFF
logged "$WORK/spirv-cross.build.log" cmake --build "$WORK/spirv-cross/_build" --target spirv-cross --parallel "$JOBS"
cp "$WORK/spirv-cross/_build/spirv-cross" "$DEPS/tools/bin/spirv-cross"

# Nothing outside the bundle and the system: SDL2 by @rpath, the tools self-contained.
for f in "$DEPS/sdl2/lib/libSDL2-2.0.0.dylib" "$DEPS/tools/bin/glslangValidator" "$DEPS/tools/bin/spirv-cross"; do
  lipo "$f" -verify_arch x86_64 arm64
  for arch in x86_64 arm64; do
    if otool -arch "$arch" -L "$f" | tail -n +2 | grep -v -E '^[[:space:]]+(/usr/lib/|/System/Library/|@rpath/libSDL2-2\.0\.0\.dylib )'; then
      echo "deps: $f ($arch) links a library outside the system" >&2
      exit 1
    fi
  done
  if grep -q -F "$WORK" "$f"; then
    echo "deps: $f still names the build directory" >&2
    exit 1
  fi
done
echo "$WANT" > "$STAMP"
echo "deps: built in $DEPS ($WANT)"
