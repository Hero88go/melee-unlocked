# Native Linux 0.9 offline preview

The Linux build compiles the 0.9 source game to `melee_game.so` and runs it in
`melee_linux`, an x86-64 glibc host. Its scope is offline bring-up and testing.
The host uses SDL2, OpenGL, and libusb. The game/host boundary is the existing
versioned `MuHostApi`/`MuGameApi` C tables.

The Slippi matchmaking service, replay frontend, Windows launcher/settings UI,
and mod import/profile workflow have not been ported. The source game retains
its native fighter, stage, and training integrations; compiling them does not
validate a mod pack or provide the Windows content-overlay workflow. The Linux
disc loader currently requires a clean retail NTSC 1.02 DOL and uncompressed
GALE01 revision 2 ISO.

## Build

Ubuntu 24.04 x86-64 is the initial build environment. Install:

```sh
sudo apt install gcc-14 g++-14 cmake ninja-build python3 pkg-config \
  libsdl2-dev libglew-dev libusb-1.0-0-dev glslang-tools spirv-cross
```

Use the repository's prepared native decomp sources under
`sourceport/extern/melee`. An untouched upstream decomp checkout is insufficient:
the native source patches in `sourceport/patches/melee-native.patch` are also
required. See [build-source-port.md](build-source-port.md) for source preparation.

From the repository root, inside Linux:

```sh
python3 tools/build_linux.py --test
```

The default CPU baseline is AVX2, the same as the Windows release's
`melee_game.dll`. The baseline changes simulation results: AVX2 compiles with
`-mfma`, so `-ffp-contract=on` fuses `a*b+c` into one rounding, while SSE2 rounds
twice. An SSE2 Linux game matched the Windows AVX2 game on every action, percent,
stock and RNG value but drifted by up to 192 float ULPs in fighter positions; the
AVX2 Linux game matched it bit for bit. Use `--cpu SSE2` or `--cpu AVX` only for
CPUs without AVX2, and expect such builds to desync from Windows players online.
The game build directory is `build-linux-game`;
the host executable and copied game library are in `build-linux/port/linux`.
`--test` builds the `mu_game_tests` group and runs all game CTests before
building and testing the Linux host. To repeat the game checks directly:

```sh
cmake --build build-linux-game --target mu_game_tests -j8
ctest --test-dir build-linux-game --output-on-failure
```

`--headless-build` omits the renderer; its executable needs `--headless` at run
time. `--host-only` reuses an existing game build; combined with `--test`, it
runs only the host checks.

For direct game configuration:

```sh
cmake -S sourceport/game -B build-linux-game -G Ninja \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DMELEE_CPU_BASELINE=AVX2 -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux-game --target melee_game -j8
```

The first bootstrap and validation used Ubuntu 24.04 under WSL1, without a
reboot. WSL1 is a build/test bootstrap, not evidence of physical Linux GPU,
audio, GameCube adapter, or gamepad compatibility. Use a Linux desktop for
hardware acceptance.

## Memory and ABI invariants

The host remains a PIE executable. MEM1 occupies `0x80000000..0x82800000`; the
ELF game starts at `0x82800000`. All loadable game segments must end by
`0x84000000`, preserving 32-bit disc pointer slots and GX's physical-address
window. The host checks the actual loaded ELF segments and fails if the loader
cannot honor this layout. MEM1 reservation checks its returned address and
does not overwrite an existing mapping.

`sourceport/cmake/game-linux-layout.ld` supplies the writable `.data`/`.bss`
bounds used by the game's snapshot API. Link-time and post-link checks enforce
the image limit. The Linux host inspects writable ELF load segments; rollback
stack capture and online transport are not implemented by this offline host.

Linux x86-64 uses 64-bit `long`, whereas the console/Windows native game uses
32-bit `long`. `sourceport/tools/console_long_overlay.py` generates a private
`build-linux-game/console-lp64` source overlay. It makes single console `long`
types and integer suffixes explicit at 32 bits while preserving `long long`,
`long double`, comments, and strings. Host ABI shims remain outside the overlay
so host libc declarations keep their platform widths. Fixed-width SDK and audio
sample assertions guard against silent LP64 expansion.

GCC is required for `scalar_storage_order`. GCC 14's scalar-replacement pass
miscompiled optimized accesses through nested big-endian `DISC_PTR` unions in
the archive-layout fixture. The Linux game and TM-CE builds use
`-fno-tree-sra`; keep that workaround until equivalent optimized layout tests
pass with a replacement. Do not substitute Clang for the game compiler.

The game uses hidden ELF symbol visibility and `-Bsymbolic`, with explicit
exports for the host boundary. This prevents host libc/libm interposition from
replacing the game's simulation math. `-z defs` rejects unresolved symbols.
Audio snapshot-exclusion generation supports ELF and PE linker maps.

## Run and package

The player's [README.txt](../port/linux/README.txt) contains installation,
keyboard mappings, udev setup, save locations, and preview limitations. The
current renderer requires `glslangValidator` and `spirv-cross` at runtime as
well as an OpenGL 4.5 driver, SDL2, GLEW, and libusb.

Examples from the repository root:

```sh
build-linux/port/linux/melee_linux --iso /absolute/path/melee.iso --fps 120
build-linux/port/linux/melee_linux --iso /absolute/path/melee.iso \
  --headless --fast --frames 3200 --state-digest /tmp/melee-state.csv \
  --card-dir /tmp/melee-linux-card --log-file /tmp/melee-linux.log
python3 tools/package_native_linux.py
```

The package tool verifies the host version, native ELF inputs, matching game
library, and free DSP coefficient table. It includes the player README,
launcher script, udev rule, licenses, and a file-hash manifest. It excludes the
game ISO. The default output is
`release/MeleeUnlocked-0.9.0-linux-x86_64-preview.tar.gz` plus its staging folder;
use `--out` for another fresh destination. Packaging is local and does not
publish a release.

Presentation defaults to a 120 FPS target with a 60 Hz simulation. `--fps 60`
disables the added authored-pose subframes. The presenter reuses the native
pose/draw capture pipeline; desktop visual correctness, frame pacing, and GPU
compatibility still need acceptance. Shader smoke tests alone do not establish
that all game scenes render correctly.

## Validation and remaining work

The game library passed all 23 CTests in the initial Linux build. These include
SDK widths, big-endian archive reads, the 72-header disc-offset audit, preload
compaction, commands, match overrides, content transitions, credits, alarms,
setjmp, audio bookkeeping, card byte order, native fighter kinds, and loading
the production ELF at its required address. The game image occupied
`0x82800000..0x83358b30` in that build.

The native host also ran a scripted offline game for 3,200 retraces, including
1,174 match frames. This establishes native game execution through an offline
match, not full mode coverage or Windows parity. Keep source-state digests,
logs, and renderer captures for acceptance runs.

Windows parity (0.9.1 tree, 2026-10-18): the vanilla VS script
`port/scripts/native_vs_ko.txt`, with its match half moved under `@match`, ran
9,000 retraces with `--vanilla-game --time-base 0 --rng-seed 0x12345678` on the
AVX2 Linux build and on the Windows 0.9.0 release's `melee_source.exe`. All
6,974 match frames had identical state digests: RNG and, for both fighters,
action, animation frame, position, velocity, percent, stocks and facing. Menu
RNG before the match seed differs because the Windows host's value there
changes from run to run; the Linux value is repeatable.

Script timing differs between the hosts. Once the game marks the match start,
the Windows script player applies only `@match`/`@scene` entries, so a script
with only absolute frames leaves the ports idle for the whole match. The Linux
player keeps applying absolute entries. Use `@match` sections for scripts that
must behave the same on both hosts.

The default (General Codes) game was not compared: the Windows host serves the
Slippi menu files from `port/slippi_sys` and the Linux host does not, so the
menus already differ from frame 423 onward.

Before describing a complete Linux release, validate the visible game across
menus and representative stages/fighters, physical audio and controller/USB
paths, and sustained gameplay on Linux hardware. The Slippi/replay frontend,
settings persistence/UI, mod importing and overlays, distribution integration,
and packaging beyond this archive remain separate work.
