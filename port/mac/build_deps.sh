#!/bin/bash
# Builds what the Mac app needs into ../deps, beside the repository; build_full.py and package_full.py
# read it from there. Each part is skipped once its output exists. Needs Xcode's command line tools,
# cmake, git and curl.
#   sdl2/        SDL 2.32.10, universal dylib (bundled into the app)
#   tools/bin/   glslangValidator 15.0.0 and spirv-cross (vulkan-sdk-1.3.296.0), universal (bundled)
#   cross/       GCC 15.2 targeting x86-64 and AArch64 Linux (messense), for melee_game.so
#   libusb/      libusb 1.0.30, static and universal, for the GameCube adapter
set -euo pipefail
cd "$(dirname "$0")/../.."
DEPS="$(cd .. && pwd)/deps"
SRC="$DEPS/src"
mkdir -p "$SRC" "$DEPS/tools/bin"
ARCHS="x86_64;arm64"

if [ ! -f "$DEPS/sdl2/lib/libSDL2-2.0.0.dylib" ]; then
  echo "== SDL2"
  [ -d "$SRC/SDL" ] || git clone -q --depth 1 --branch release-2.32.10 https://github.com/libsdl-org/SDL.git "$SRC/SDL"
  cmake -S "$SRC/SDL" -B "$SRC/SDL/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$DEPS/sdl2" >/dev/null
  cmake --build "$SRC/SDL/build" -j 8 >/dev/null
  cmake --install "$SRC/SDL/build" >/dev/null
fi

if [ ! -x "$DEPS/tools/bin/glslangValidator" ]; then
  echo "== glslang"
  [ -d "$SRC/glslang" ] || git clone -q --depth 1 --branch 15.0.0 https://github.com/KhronosGroup/glslang.git "$SRC/glslang"
  cmake -S "$SRC/glslang" -B "$SRC/glslang/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DBUILD_SHARED_LIBS=OFF -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF \
    -DENABLE_GLSLANG_BINARIES=ON -DBUILD_EXTERNAL=OFF >/dev/null
  cmake --build "$SRC/glslang/build" -j 8 --target glslang-standalone >/dev/null
  cp -L "$SRC/glslang/build/StandAlone/glslang" "$DEPS/tools/bin/glslangValidator"
fi

if [ ! -x "$DEPS/tools/bin/spirv-cross" ]; then
  echo "== SPIRV-Cross"
  [ -d "$SRC/SPIRV-Cross" ] || git clone -q --depth 1 --branch vulkan-sdk-1.3.296.0 https://github.com/KhronosGroup/SPIRV-Cross.git "$SRC/SPIRV-Cross"
  cmake -S "$SRC/SPIRV-Cross" -B "$SRC/SPIRV-Cross/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DSPIRV_CROSS_SHARED=OFF -DSPIRV_CROSS_STATIC=ON -DSPIRV_CROSS_CLI=ON \
    -DSPIRV_CROSS_ENABLE_TESTS=OFF >/dev/null
  cmake --build "$SRC/SPIRV-Cross/build" -j 8 --target spirv-cross >/dev/null
  cp -L "$SRC/SPIRV-Cross/build/spirv-cross" "$DEPS/tools/bin/spirv-cross"
fi

# The game for each architecture is built by the same GCC release, so both fuse the same a*b+c.
for target in x86_64 aarch64; do
  if [ ! -x "$DEPS/cross/$target-unknown-linux-gnu/bin/$target-unknown-linux-gnu-gcc" ]; then
    echo "== $target Linux cross GCC"
    asset="$target-unknown-linux-gnu-aarch64-darwin.tar.gz"
    (cd "$SRC" &&
     curl -fsSLO "https://github.com/messense/homebrew-macos-cross-toolchains/releases/download/v15.2.0/$asset" &&
     curl -fsSLO "https://github.com/messense/homebrew-macos-cross-toolchains/releases/download/v15.2.0/$asset.sha256" &&
     shasum -a 256 -c "$asset.sha256")
    mkdir -p "$DEPS/cross"
    tar xzf "$SRC/$target-unknown-linux-gnu-aarch64-darwin.tar.gz" -C "$DEPS/cross"
  fi
done

# Universal: the Rosetta host links the x86-64 half, the native host the arm64 one.
if ! lipo -archs "$DEPS/libusb/lib/libusb-1.0.a" 2>/dev/null | grep -q arm64; then
  echo "== libusb"
  (cd "$SRC" &&
   curl -fsSL -o libusb-1.0.30.tar.bz2 https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2 &&
   echo "fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf  libusb-1.0.30.tar.bz2" | shasum -a 256 -c - &&
   rm -rf libusb-1.0.30 && tar xjf libusb-1.0.30.tar.bz2)
  for arch in x86_64 arm64; do
    host=$arch; [ $arch = arm64 ] && host=aarch64
    rm -rf "$SRC/libusb-$arch" && cp -R "$SRC/libusb-1.0.30" "$SRC/libusb-$arch"
    # The hosts target macOS 15: pipe2 is in the macOS 27 SDK but not on 15.
    (cd "$SRC/libusb-$arch" &&
     ./configure -q --host=$host-apple-darwin --disable-shared --enable-static ac_cv_func_pipe2=no \
       CC="/usr/bin/clang -arch $arch -mmacosx-version-min=15.0" CFLAGS="-O2 -g" &&
     make -j 8 >/dev/null)
  done
  # Copied by hand: libtool's install fails on a path with a space in it.
  mkdir -p "$DEPS/libusb/lib" "$DEPS/libusb/include/libusb-1.0"
  lipo -create "$SRC/libusb-x86_64/libusb/.libs/libusb-1.0.a" "$SRC/libusb-arm64/libusb/.libs/libusb-1.0.a" \
       -output "$DEPS/libusb/lib/libusb-1.0.a"
  cp "$SRC/libusb-1.0.30/libusb/libusb.h" "$DEPS/libusb/include/libusb-1.0/"
  cp "$SRC/libusb-1.0.30/COPYING" "$DEPS/libusb/COPYING"
fi

echo "Dependencies ready in $DEPS"
