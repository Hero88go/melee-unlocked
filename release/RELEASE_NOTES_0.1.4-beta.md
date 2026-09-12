# Melee Unlocked 0.1.4 beta

This one is about motion and sound during a match. If 0.1.3 felt worse than 0.1.2 while playing,
this is the build to try: the static picture was never the problem, the way it moved was.

## Install

Extract the zip and run `MeleeUnlockedLauncher.exe`, or drag your Melee NTSC 1.02 ISO onto
`MeleeUnlocked.bat`. The launcher is optional. Settings, saves and replays carry over.

## What changed

**The simulation can no longer be held up by the renderer.** The game's 60 Hz thread used to
stall whenever the renderer fell behind, which showed up as hitching and as the frame rate
counter reading 60 while the game felt uneven. The renderer now catches up on its own, and the
frame handover no longer reallocates several megabytes every frame. Disc reads, music decoding
and Slippi file loads moved off the game thread entirely.

**Animation between frames covers much more of the picture.** A looping animation crossing its
last frame used to freeze the whole character for a frame and then snap, once per run cycle. A
single joint the sampler could not handle froze an entire fighter. Both are fixed, and three
kinds of motion that previously only moved at 60 Hz now move at the display rate: scrolling
stage textures, effects that rebuild their geometry every frame (sparks, shields, flashes), and
anything held still while the camera pans. Measured on a scripted match at 240 fps, the jump at
each simulation-frame boundary fell by a third.

**Sound tracks the sound card's clock.** The game's audio clock and the card's differ slightly,
and nothing corrected for it, so the queue drifted until it clicked or ran dry, and running dry
inserted 48 ms of silence. It now resamples continuously to hold the queue steady and holds the
last sample through a starved moment instead of going quiet.

**Sharper downsampling at high internal resolutions.** The averaging filter used the horizontal
shrink factor for both axes; at a 16:9 window with a high internal resolution the vertical axis
shrinks by more, so vertical edges were still aliasing. Each axis now gets its own.

**The process asks Windows for 1 ms timer resolution.** Without it every pacing sleep, including
the 60 Hz one, could overshoot by a whole frame.

## Diagnostics in the log

`melee_port.log` now reports what the game thread spent its time on (`sim: 9.1 ms/frame (worst
14.7) | ax 0.11 texsnap 0.37 observe 1.42`), names any frame over 20 ms, and reports audio as
gaps held rather than silence inserted. If something still feels wrong, that log says where.

## Known gaps

- Ray tracing is not implemented
- Ranked reports results but has not been tested in a live ranked set
