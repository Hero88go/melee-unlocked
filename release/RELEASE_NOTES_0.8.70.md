## Install

Download `MeleeUnlocked-0.8.70-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

Crash fixes from your reports, a replay queue with search, and Source Port and Static Recomp players can match in the lobby.

## Fixes

- Source Port: Peach pulling a Beam Sword with her down special made an item that does not exist, which crashed the match. The pick now returns the Beam Sword.
- Source Port: Adventure crashed on the Kirby stage (the team of Kirbys, Giant Kirby). The spawn list and a callback shared the same memory.
- Static Recomp, 20XX Hack Pack: after a song change the old music voice was never released, and a sound that later took its place could keep playing until the game closed. It is released now.
- Static Recomp: saving could stop the game with "No mapping for the Unicode character exists in the target multi-byte code page". Save file names are no longer converted through the system code page, and a save that cannot be written is logged instead of ending the game.
- Lobby: a Source Port player and a Static Recomp player on the same version were told to "Choose the same Game Build". The two builds play each other, so only the version has to match. Both players need this version.
- Launcher: buttons and lobby rows are drawn off screen and shown in one step, and the lobby repaints when a list fills or empties (a stray shape stayed above the first player).

## New

- Replay Viewer: pick several replays in any order with the round badge on each card, then Watch queue plays them one after another. Closing the game stops the queue.
- Replay Viewer: a search box (players, codes, characters, stages, file names), a sort menu (most recent, least recent, longest, shortest) and Hide short games.

## Notes

- A crash report for an internal error now says where the error was raised.
