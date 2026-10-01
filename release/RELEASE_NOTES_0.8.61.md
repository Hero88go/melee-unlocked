# Melee Unlocked 0.8.61

Hotfix for 0.8.6: widescreen, song titles on mod discs, exclusive fullscreen, a Training Mode CE crash and a few smaller fixes.

## Install

Download `MeleeUnlocked-0.8.61-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Widescreen, both modes and both engines: the main menu and its submenus fill the screen again, as they did in 0.5 and 0.6. With the Slippi code they were squeezed between side bars.
- True 16:9: fighters are drawn in the added side areas, as with the Slippi code. Before, a fighter outside the old 4:3 area disappeared and only the bubble showed. The P1/P2 tags now sit over their fighters.
- Akaneia and ACE: the song title at the start of a match shows with Visual effects on Reduced or Minimal.
- Akaneia and ACE: stages that read the clock get your PC's date and time. They used to see January 1, 2000 at every start. Save files are dated correctly too.
- Exclusive fullscreen keeps your desktop resolution. It used to switch the display to the Window size setting.
- Closing the game no longer ends in a crash report on some NVIDIA systems. The upscaler libraries crashed while the game was shutting down.
- DLSS: after the window was maximized or went fullscreen, the menus retried an upscaler setup step on every frame and filled the log with errors. It is now tried once.
- Training Mode CE (Source Port): changing an OSD option in the Lab no longer crashes the game.
- 20XX TE settings screen and the debug menu (Source Port): the selected row is highlighted again.

## New

- Gecko codes on the Static Recomp: codes that overwrite the game's instructions (04 and the other write types) can now be switched on. C2 codes still cannot run.

## Notes

- Lobby matches need both players on 0.8.61.
