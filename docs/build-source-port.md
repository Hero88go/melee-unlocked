# Building the Source Port

The Source Port has two components: `melee_game.dll`, compiled from the Melee decomp
and native shims with MinGW-w64 GCC 14 or newer, and `melee_source.exe`, compiled
with MSVC. The launcher offers it alongside Static Recomp when both files are installed.
Game data comes from your own NTSC 1.02 ISO.

Initialize the pinned public decomp and apply the released native adaptations:

```powershell
git submodule update --init sourceport/extern/melee
python tools/prepare_native_sources.py
```

The adaptations are tracked in `sourceport/patches/melee-native.patch`, including
the native changes that previously existed only in the local submodule. The script
checks the pinned upstream commit, refuses unrelated local edits, and accepts an
already applied patch. The native CMake project also runs this step automatically.
To inspect the adaptations, run `git -C sourceport/extern/melee diff`.

Install a MinGW-w64 GCC 14+ distribution with Ninja and configure the DLL (replace
the toolchain path with your installation):

```powershell
cmake -S sourceport/game -B build-sourceport-gcc -G Ninja "-DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64-x86_64.cmake" "-DMELEE_MINGW_ROOT=C:/toolchains/mingw64"
cmake --build build-sourceport-gcc --target melee_game --parallel
```

Prepare the guest translation using the Static Recomp steps in the root README,
then configure and build the host and launcher:

```powershell
cmake -S . -B build-review -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON
cmake --build build-review --config Release --target melee_source melee_unlocked --parallel
Copy-Item build-sourceport-gcc/melee_game.dll build-review/port/Release/
Copy-Item build-sourceport-gcc/melee_game.snapexcl build-review/port/Release/
build-review/port/Release/melee_source.exe --iso "C:/path/to/melee.iso" --threaded-renderer
```

Use matching CPU baselines for the DLL and host. `-DMELEE_CPU_BASELINE=SSE2` builds
the compatibility variant. Optional DLSS 5 builds use `-DMELEE_ENABLE_DLSS5=ON`;
NVIDIA's model is supplied separately by the user.

The native host shares rendering, audio, controllers and Slippi services with Static
Recomp. The game DLL is built from source; the host's translation is also used by
the parity tests. Source Port mods and Event Match are not supported in v0.8.0.
