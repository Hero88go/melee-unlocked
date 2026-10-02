# Melee Unlocked 0.8.65

Hotfix for 0.8.64: imported portraits and stock icons that did not show.

## Install

Download `MeleeUnlocked-0.8.65-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Imported stock icons did not change for any costume other than the default one. They now show for every costume, and Sheik's icon is its own instead of Zelda's.
- Imported portraits did not show for 13 costumes, Captain Falcon's blue one among them. The others: Kirby white, Yoshi aqua and pink, Marth white, Peach green, Jigglypuff yellow, Samus lavender, Zelda white, Young Link black, Dr. Mario black, Roy yellow and Ganondorf lavender.
- Add portrait: the costume lists of Captain Falcon, Young Link, Donkey Kong and the Ice Climbers are in the game's order.
- Choosing a modded disc as the Melee disc gave a crash report. The launcher now says to add it under Mods instead.
- D3D11 with V-Sync on: at most one finished frame waits for the display, instead of the default of three.

## Notes

- Online: the log now says when the game waited for the other player's inputs and for how long, and the summary line counts those waits. If a match felt delayed, send `melee_port.log` from that session.

- Lobby matches need both players on 0.8.65.
