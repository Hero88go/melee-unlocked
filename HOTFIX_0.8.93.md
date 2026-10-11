## Install

Download `MeleeUnlocked-0.8.93-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Updating: in a game folder with a long path, the update could stop partway and leave the folder half updated, with no message. The release no longer carries the long file names that caused it: the CRT shader presets come as one file and are written to the Shaders folder the first time shaders are used.
- Updating: when the first way of copying the update fails, the update finishes it a second way. An update that still cannot be put in place now says so in a window.
- Updating: Send logs includes what the last update did.
