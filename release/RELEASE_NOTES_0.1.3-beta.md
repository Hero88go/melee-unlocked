# Melee Unlocked 0.1.3 beta

The project is now called Melee Unlocked (repository: hero88go/melee-unlocked; the old melee-port links redirect).

## Install: two ways, pick one

Extract the zip anywhere. **The launcher is optional.** The game does not depend on it and the manual way is complete on its own.

**Manual (no launcher):** drag your Melee NTSC 1.02 ISO onto `MeleeUnlocked.bat`, or name it `melee.iso` next to it and double-click the bat. To update later, extract a newer zip over the folder; settings, saves and replays are kept.

**Melee Unlocked Launcher (optional):** run `MeleeUnlockedLauncher.exe`, drop the ISO onto its window (Build tab), press PLAY. It checks the disc, precompiles the graphics pipelines once, shows which Slippi account will be used, and offers "Update and restart" when a new release exists.

Slippi online needs a Slippi account, so for online play the Slippi Launcher is required (install it, log in once); for offline play it is not. Slippi Dolphin itself is never needed. This project is not affiliated with the Slippi team. Settings and saves from 0.1.x carry over.

## Stability fixes in 0.1.3 (textures turning black, flicker, audio cut-outs, fullscreen, sharpness)

- Pipelines were being compiled forever: shader identities included register state of unused TEV stages and texgens, so one session produced 32000 pipelines and kept compiling 30 to 50 new ones every 10 seconds mid-match. Fixed; a whole cold match now needs about 100.
- Surfaces no longer turn black while a pipeline compiles: draws wait briefly for their real pipeline instead of using the generic one.
- Audio: output gaps are now counted in the log, and after a gap playback waits for 48 ms of buffer instead of crackling.
- Borderless fullscreen went black because the window lost its visible flag when its style changed. Fixed. Alt+Enter toggles fullscreen.
- Downsampling from a high internal resolution now averages every rendered pixel (box filter, up to 4x4) instead of one bilinear tap, so 3x to 8x with SSAA looks properly supersampled. Note: DLSS Quality/Balanced/Performance render below the window size by design (about 1280x960 at 1080p); for the sharpest image use Native with 3x or higher plus SSAA, or DLAA. The settings panel explains this next to the DLSS control.
- The game no longer crashes at exit after the settings panel checked for updates.


- Melee Unlocked Launcher (optional): Play page, Build tab, built-in updater. The in-game settings panel also shows the version and an update button.
- Flicker fix: while a new graphics pipeline is still compiling, its draws are rendered with a generic pipeline instead of being skipped, so characters and stage parts no longer pop in and out during the first seconds of a new matchup.
- Match loading stutter and sound crackle: disc reads run on a worker thread with deterministic timing, and the simulation no longer sprints to catch up after a stall (which is what pitched the audio up and crackled).
- Resolution names now show the pixel size (2x = 1280x1056 for 720p, 3x = 1920x1584 for 1080p, ...), and the resolution combo explains itself when DLSS controls the render size.

## Verified

- 2400 simulation checkpoints identical across headless, hidden, threaded and authored rendering with the asynchronous disc reads
- Cold shader cache scripted match: 3 draws on the fallback pipeline in a whole match, 0 audio blocks dropped
- Two-instance Slippi online match with no desync

## Known gaps

- Ray tracing is not implemented
- Audio is an approximate mixer
