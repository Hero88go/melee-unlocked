## Install

Download `MeleeUnlocked-0.8.68-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

Hotfix for 0.8.67: crashes from the new crash reports, the 20XX Hack Pack's music, skins that were wrongly turned off online, and replays can now be paused and navigated.

## Fixes

- D3D12: switching exclusive fullscreen on or off during a session stopped the game with "swapchain failed". The display is no longer rebuilt mid-session. Switched on while playing, exclusive fullscreen shows as borderless until the next start.
- 20XX Hack Pack: every song repeated its first two seconds. Music plays through normally.
- Launcher: right after an update the Mods page showed the previous version's verdict ("Not supported yet") until the disc had been read again. It says "Checking..." during that time.
- Launcher: an installed 20XX Hack Pack showed a "Direct only" line. The pack has no online play; the card says so.
- Skins online: costumes exported from a model tool were turned off online because of a joint setting the game replaces with its own every time an animation plays. They stay on now.
- Static Recomp: L and R changed skins on the online Teams character select screen before the first search. Costumes are fixed by team there, so they no longer do.
- DLSS 5 with a widescreen picture: the model was given the picture before it was squeezed to the screen's shape. It now gets a screen-shaped picture.
- File names with letters outside the system's own language are read as UTF-8 everywhere.

## New

- Replays can be paused and navigated, on both versions. Space or Start: pause. Right or `.` while paused: one frame. Left and Right: 5 seconds back or forward (Shift: 30 seconds). Hold Tab or R: fast forward. `[` and `]`: slow motion. Home: restart. A bar at the bottom shows the time and takes clicks.
- Replay Viewer, "Show it as it was played": when an online replay has its `.trace` file beside it, the viewer holds the picture where the game waited and shows the Network and timing overlay as it was.
- Source Port, Settings, Game: CPU training options, offline only. CPU tech (in place, roll forward, roll back, miss, random), CPU getup, CPU DI (none, random, survival), CPU smash DI, CPUs always L-cancel, CPUs never taunt, CPU Captain Falcon without rapid jab, CPU Zelda and Sheik without transform.
- Skins on the character select screen: Ice Climbers change as a pair when the pack has a skin for both.
- Skin portraits: a skin that has its own portrait always shows it, so L and R change the picture with the skin. Settings, Mods, a skin's Details: "Portrait" and "Stock icon" let you give any skin a picture, from a file or from another skin. A picture added to a costume fills in where a skin has none. Each color shows how many skins it has.

## Notes

- A crash at start that left only an exception code in the report now names what failed and where. If the game still stops at start for you, please send the new report.
- Akaneia, Adventure mode as one of its new fighters: the game stops at a cutscene because the disc has no cutscene animation for that fighter. This is in the mod itself.
- A replay recorded with CPU training options on plays back with them, whatever your settings are.
