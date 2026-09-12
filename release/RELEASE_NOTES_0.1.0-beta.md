# Melee Port 0.1.0 beta

First public beta of a native Windows build of Super Smash Bros. Melee NTSC 1.02 with Slippi online.
You need your own Melee NTSC 1.02 ISO. Nothing from the game is in this download.

## Install

1. Extract the zip anywhere.
2. Drag your ISO onto `MeleePort.bat`, or put it next to the bat named `melee.iso` and double click the bat.
3. First start precompiles the shader list (about 15 seconds, progress in the title bar).
4. The PC settings panel opens on launch. Later: the Settings button in the top right corner, F1, or Z + Start on the controller.

## What works

- Game logic exactly as on GameCube at 60 Hz, display at any rate (monitor rate, fixed cap or unlocked) with real in-between animation
- Slippi online (Unranked, Direct, Teams) against players on regular Slippi Dolphin, using your Slippi Launcher login; replays written as .slp
- GameCube adapter (WUP-028 with the WinUSB driver Slippi installs), keyboard fallback
- DLSS / DLAA (NVIDIA Streamline), internal resolution up to 8x, 4x SSAA, anisotropic filtering, sharpening, borderless fullscreen, VSync
- Widescreen 16:9 (Slippi's own optional code, online safe)
- Memory card saves as .gci files in `User\GC\CardA` (Dolphin GCI-folder format; drop your Slippi save there)
- Menu and stage music (Slippi Jukebox)

## Known gaps

- Ranked play is not reported to Slippi's servers yet; play Unranked or Direct
- Ray tracing is not implemented
- Replay playback is not implemented
- Audio is an approximate mixer

## Reporting bugs

Open an issue with the Bug report template. Attach `melee_port.log` from the folder you launched from, your `port-settings.ini`, and the .slp replay if it happened in a match.
