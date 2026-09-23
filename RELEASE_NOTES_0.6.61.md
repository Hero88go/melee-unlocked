# Melee Unlocked 0.6.61

Focused hotfix for launcher cache warming, overlay placement, and fullscreen handling.

## Install

Choose one of the two Windows archives on the release page:

- `MeleeUnlocked-0.6.61-win64.zip` — standard build; recommended for most players.
- `MeleeUnlocked-0.6.61-DLSS5-Experimental.zip` — experimental build for RTX 50-series GPUs or newer; requires NVIDIA's DLSS 5 file, which is not included.

Close Melee Unlocked, then extract the chosen archive over your existing game folder. Your settings, saves, and replays carry over. Keep your own Melee NTSC 1.02 ISO beside the game files as `melee.iso`, or select it with the included launcher. Start the included `MeleeUnlockedLauncher.exe` after extraction.

## Fixed and improved

- The Build page now has an opt-in “Warm cache from remembered ISO before Play” setting. The launcher remembers the selected ISO location and runs a short hidden warmup before launching the game. If warmup fails, it logs the error and continues into the game.
- Performance, controller, status, and notice overlays stay within the rendered gameplay area when black bars are present. Dragged overlays are clamped back into the image area.
- Added a separate experimental exclusive fullscreen option alongside borderless fullscreen. It targets the active monitor resolution; if the driver refuses the transition, it falls back to borderless fullscreen.
- Launcher title/version metadata and game version are generated from the same `VERSION` source.
