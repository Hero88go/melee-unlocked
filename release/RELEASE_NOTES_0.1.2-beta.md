# Melee Unlocked 0.1.2 beta

The project is now called Melee Unlocked (repository: hero88go/melee-unlocked; the old melee-port links redirect).

## Install: two ways, pick one

Extract the zip anywhere. **The launcher is optional.** The game does not depend on it and the manual way is complete on its own.

**Manual (no launcher):** drag your Melee NTSC 1.02 ISO onto `MeleeUnlocked.bat`, or name it `melee.iso` next to it and double-click the bat. To update later, extract a newer zip over the folder; settings, saves and replays are kept.

**Melee Unlocked Launcher (optional):** run `MeleeUnlockedLauncher.exe`, drop the ISO onto its window (Build tab), press PLAY. It checks the disc, precompiles the graphics pipelines once, shows which Slippi account will be used, and offers "Update and restart" when a new release exists.

Slippi online needs a Slippi account: install the Slippi Launcher and log in once. Slippi Dolphin itself is not needed. Settings and saves from 0.1.x carry over.

## New since 0.1.1

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
