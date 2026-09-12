# Melee Port

A native Windows build of Super Smash Bros. Melee (NTSC 1.02) with Slippi online play.

The game's own PowerPC code is translated ahead of time into C++ (static recompilation of the
retail executable plus Slippi's Gecko codes) and runs against a native D3D12 renderer, so the
game logic stays exactly what the GameCube ran, at 60 Hz, while the display runs at any rate.
In-between frames come from the game's own animation data and physics state, not from image
interpolation, so an unlocked 200 Hz display shows real intermediate poses with no added latency.

Nothing from the game is included. You supply your own Melee NTSC 1.02 ISO; the build step
translates it on your machine.

## Features

- Unlocked frame rate (monitor rate, a fixed cap, or fully unlocked) with sub-frame animation
- Slippi online: Unranked, Direct codes and Teams against players on regular Slippi Dolphin,
  using your existing Slippi Launcher login; replays (.slp) are written as usual
- GameCube adapter (WUP-028 with the WinUSB driver), keyboard fallback
- DLSS / DLAA (NVIDIA Streamline), internal resolution up to 8x, borderless fullscreen, VSync
- Widescreen 16:9 (Slippi's own optional code, online safe)
- Memory card saves as .gci files (Dolphin GCI-folder format, drop in your existing save)
- PC settings overlay in the game window: F1 or Z + Start

## Playing a release

1. Download the zip from Releases and extract it.
2. Put your Melee NTSC 1.02 ISO next to `MeleePort.bat`, named `melee.iso`.
3. Run `MeleePort.bat`. Press F1 for settings.

Bug reports: open a GitHub issue using the template. Attach `melee_port.log` from the folder
you launched from, your `port-settings.ini`, and the .slp replay if the bug happened in a match.

## Building from source

Two ways. Both need Windows 10/11, your own Melee NTSC 1.02 ISO, and about 30 minutes the
first time (the game is translated to C++ and compiled). Nothing from the ISO is written into
the repository.

**Option 1, drop the ISO:** clone the repo, then drag your ISO onto `play.bat`. It installs the
tools it needs with winget if they are missing (Python 3, CMake, Visual Studio 2022 Build Tools
with the C++ workload), extracts `main.dol`, translates, compiles and starts the game. `build.bat`
does the same without starting the game.

**Option 2, command line:**

```powershell
git clone https://github.com/hero88go/melee-port.git melee-unlocked
cd melee-unlocked
python tools/extract_dol.py "C:/path/to/melee.iso" build/main.dol
python port/recomp/recomp.py --dol build/main.dol --gct-base 0x8065CC80
cmake -S . -B build-review -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON
cmake --build build-review --config Release --target melee_port --parallel
build-review/port/Release/melee_port.exe --iso "C:/path/to/melee.iso" --threaded-renderer --fps unlocked --frame-mode authored --scale auto --volume 70
```

`port/recomp/` is the recompiler (Python): it reads the DOL and the Slippi code tables
(`port/slippi_sys/`, vendored from Slippi) and writes `port/generated/` (not committed).
`port/runtime/` is the host runtime: PowerPC helpers, HLE of the GameCube SDK (OS, VI, PAD, DVD,
AI/AX audio, CARD, EXI), the Slippi EXI device, netcode, game reporting, the D3D12 renderer and
the sub-frame solver. `tools/` holds validation, benchmarking and packaging scripts. See
`PORT_COMPLETION.md` for the technical state and evidence, `HANDOFF_FABLE_3.md` for the roadmap.

`tools/package_release.py --version X.Y.Z` produces the release zip. The replay playback build
(`melee_port_playback`, used to verify frame-exactness against Dolphin replays) is described in
`PORT_COMPLETION.md`.

## Verification

- `ctest --test-dir build-review -C Release`: unit tests
- `python tools/validate_native.py --iso <iso>`: 2400 simulation checkpoints must match across
  headless, hidden, threaded and authored rendering
- `python tools/online_pair.py --script port/scripts/online_bot.txt`: two local instances play a
  full Slippi online match; the log must show no `DESYNC`

## License

GPL-2.0-or-later. Parts of the runtime are ports of Dolphin and Slippi Ishiiruka code (GPL-2.0).
Third-party components: ENet, Dear ImGui, nlohmann/json, NVIDIA Streamline (see `licenses/` in a
release and `port/third_party/`). Super Smash Bros. Melee is the property of Nintendo and HAL
Laboratory; this project contains none of its data.
