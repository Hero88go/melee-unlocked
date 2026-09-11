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

Requirements: Windows 10/11, Visual Studio 2022 (C++ desktop workload), CMake 3.24+,
Python 3.8+, an NVIDIA or other D3D12-capable GPU, and your own Melee NTSC 1.02 ISO.

```powershell
git clone <this repo> melee-unlocked
cd melee-unlocked
python port/recomp/recomp.py --dol <path to the ISO's main.dol or extracted DOL> --gct-base 0x8065CC80
cmake -S . -B build-review -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON
cmake --build build-review --config Release --target melee_port
python tools/launch_native.py --threaded-renderer --fps unlocked --frame-mode authored --volume 70
```

`port/recomp/` is the recompiler (Python): it reads the DOL and the Slippi code tables and
writes `port/generated/` (not committed). `port/runtime/` is the host runtime: PowerPC helpers,
HLE of the GameCube SDK (OS, VI, PAD, DVD, AI/AX audio, CARD, EXI), the Slippi EXI device and
netcode, the D3D12 renderer and the sub-frame solver. `tools/` holds validation, benchmarking
and packaging scripts. See `PORT_COMPLETION.md` for the technical state and evidence,
`HANDOFF_FABLE_3.md` for the roadmap.

`tools/package_release.py --version X.Y.Z` produces the release zip.

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
