

# Melee Unlocked - Alpha

An **EXPERIMENTAL** native Windows build of Super Smash Bros. Melee (NTSC 1.02) with Slippi online play and an
unlocked display frame rate.

The game's own PowerPC code is translated ahead of time into C++ (static recompilation of the
retail executable plus Slippi's Gecko codes) and runs against a native D3D12 renderer, so the
authoritative gameplay and presentation run separately. The original fixed-step gameplay is
preserved while the renderer samples supported animation between simulation frames.
Forward authored sampling avoids an additional buffered simulation frame; unsupported cases
hold their pose. Smoothness, visual coverage and latency are still being validated.

**0.1.7 ? Codex hotfix:** native rendering defaults and keyboard/mouse-only settings opening.
See [release notes](RELEASE_NOTES.md) for every change and the remaining limitations.

This implementation uses the [doldecomp Melee decompilation](https://github.com/doldecomp/melee) for symbols and adapted animation source. Most gameplay is currently statically recompiled from the retail executable, **not directly compiled from decomp C**. Our goal is a directly compiled source port with Slippi and independent high-refresh rendering. See [architecture and migration plan](ARCHITECTURE.md).

Nothing from the game is included. You supply your own Melee NTSC 1.02 ISO.

This project is not affiliated with, endorsed by, or supported by the Slippi team, Nintendo or
HAL Laboratory. Slippi netplay compatibility is implemented from Slippi's open source code
(GPL). Questions and bugs about this build go to this repository or the Discord below, not to
the Slippi team.

First gameplay footage can be found here: https://youtu.be/y2KN3s7uvqM

## Discord / Help
Discord can be found here https://discord.gg/K7HHs3r8ty

## Install

Download `MeleeUnlocked-<version>-win64.zip` from [Releases](https://github.com/hero88go/melee-unlocked/releases)
and extract it anywhere. Then pick one of two ways to run it. **The launcher is optional**;
the game does not depend on it, and the manual way is complete on its own.

### Manual (no launcher)

1. Drag your Melee NTSC 1.02 ISO onto `MeleeUnlocked.bat`, or put the ISO next to it named
   `melee.iso` and double-click `MeleeUnlocked.bat`.
2. Play. The first launch precompiles the graphics pipelines (15 to 30 seconds, progress in
   the title bar). In game, F1 opens the PC settings.
3. To update, extract a newer zip over the folder. Settings, saves and replays are kept.

### Melee Unlocked Launcher (optional)

A small window in the same zip, `MeleeUnlockedLauncher.exe`, for people who want setup,
updates and the Slippi account check in one place.

- **Build tab**: drop the ISO onto the window. It checks the disc, precompiles the graphics
  pipelines for your GPU once and remembers the path. The ISO is never copied.
- **Play**: press PLAY. It shows which Slippi account will be used.
- **Updates**: it checks for a new release on every start. "Update and restart" installs it in
  place; settings, saves and replays stay.

### Build from source

Windows 10/11, your own ISO, about 5-10 minutes the first time. The game is translated to C++
and compiled on your machine; nothing from the ISO enters the repository.

Either drag the ISO onto `play.bat` in a clone of this repo (it installs Python, CMake and the
Visual Studio 2022 Build Tools with winget if missing, then builds and starts the game), or:

```powershell
git clone https://github.com/hero88go/melee-unlocked.git
cd melee-unlocked
python tools/extract_dol.py "C:/path/to/melee.iso" build/main.dol
python port/recomp/recomp.py --dol build/main.dol --gct-base 0x8065CC80
cmake -S . -B build-review -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON
cmake --build build-review --config Release --target melee_port --parallel
build-review/port/Release/melee_port.exe --iso "C:/path/to/melee.iso" --threaded-renderer --fps unlocked --frame-mode authored --scale auto --volume 70
```

(Add the target `melee_unlocked` to the build line if you want the optional launcher; run
`build-review/port/Release/MeleeUnlockedLauncher.exe` from the checkout and it finds the repo.)

## FAQ

**Was this "vibe coded"?**

This project was developed with Fable 5.1 and GPT 6 Astra. The upstream decomp is a separate project; we do not claim its development used these tools.
AI coding tools contributed to this port; credit for the upstream decomp belongs to its contributors.
As humans we can either work with the robots or against them, I believe in technlogical progress and making cool shit, if we do not use all tools available we are choosing to limit our results.
I will not handicap myself and theres no reason anyone has to wait any longer for ports and advancements like this. If I were to shy away from every new technology I would not be the person I am today.

I am interested in collabing with other developers but so far have found no collective space for this type of dicussion; PC port dicussion is actively discouraged in the Melee decomp discord
My vision for the project is keeping it open source so anyone can view the work and make it better.

## Features

- Unlocked frame rate (monitor rate, a fixed cap, or fully unlocked) with sub-frame animation
- Slippi online against regular Slippi Dolphin players, using your Slippi Launcher login
- GameCube adapter (WUP-028 with the WinUSB driver), keyboard fallback
- DLSS / DLAA (NVIDIA Streamline), internal resolution up to 8x, SSAA, anisotropic filtering,
  sharpening, borderless fullscreen, VSync
- Widescreen 16:9 (Slippi's own optional code, online safe)
- Memory card saves as .gci files (Dolphin GCI-folder format, drop in your existing save)
- PC settings overlay in the game window: F1
- Optional launcher with self-update

## Slippi online

Everything Slippi Dolphin does for netplay is built in: matchmaking, rollback netcode, the
Slippi code set, replay recording, game reporting. Slippi Dolphin itself is not needed and is
not touched.

**Is the Slippi Launcher required?** For online play, yes: a Slippi account is required and
accounts are created and logged in only through the [Slippi Launcher](https://slippi.gg/downloads).
Install it, log in once, and the game picks up that login automatically (the optional Melee
Unlocked Launcher shows the account on its Play page and links to the download if none is
found). The **Slippi** Launcher also installs the WinUSB driver a GameCube adapter needs. For
offline play the Slippi Launcher is not required. NOTE: **we are not affiliated with the Slippi team.**

Unranked, Direct codes and Teams work against players on regular Slippi Dolphin; they change
nothing on their side. Replays (.slp) are written to `Replays\`.


## Bug reports

Open a [GitHub issue](https://github.com/hero88go/melee-unlocked/issues) using the template.
Attach `melee_port.log` from the game folder, your `port-settings.ini`, and the .slp replay if
the bug happened in a match.

## Repository layout

`port/recomp/` is the recompiler (Python): it reads the DOL and the Slippi code tables
(`port/slippi_sys/`, vendored from Slippi) and writes `port/generated/` (not committed).
`port/runtime/` is the host runtime: PowerPC helpers, HLE of the GameCube SDK (OS, VI, PAD, DVD,
AI/AX audio, CARD, EXI), the Slippi EXI device, netcode, game reporting, the D3D12 renderer and
the sub-frame solver. `port/app/launcher.cpp` is the optional launcher. `tools/` holds validation,
benchmarking and packaging scripts. See [architecture](ARCHITECTURE.md) and [release notes](RELEASE_NOTES.md) for the technical state and remaining work.

`tools/package_release.py` produces the release zip (version from `VERSION`). The replay
playback build (`melee_port_playback`, used to verify frame-exactness against Dolphin replays)
is described in `CODEX_GRAPHICS_REVIEW.md`.



## Verification

Development launcher: `run-native.bat` (or `python tools/launch_native.py --iso <iso> ...`) starts the
build in `build-review/port/Play/` if present, else the Release build, muted and windowed by default.

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
