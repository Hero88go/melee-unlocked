# Melee Unlocked 0.1.2 beta

The project is now called Melee Unlocked (repository: hero88go/melee-unlocked; the old melee-port links redirect).

## Install

Extract the zip and run `MeleeUnlocked.exe`. Drop your Melee NTSC 1.02 ISO onto the window (Build tab): it checks the disc, precompiles the graphics pipelines for your GPU once and remembers the path. Then press PLAY. `MeleeUnlocked.bat` still works the old way (drop the ISO on it, or name it `melee.iso`). Settings and saves from 0.1.x carry over.

Slippi online needs a Slippi account: install the Slippi Launcher and log in once; the client shows the account it found on the Play page. Slippi Dolphin itself is not needed.

## New since 0.1.1

- Client: Play page (ISO, Slippi account, version), Build tab (drop the ISO), and a built-in updater. The client checks for new releases on every start and "Update and restart" installs one in place, keeping settings, saves and replays. The in-game settings panel has the same version line and update button.
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
