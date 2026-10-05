## Install

Download `MeleeUnlocked-0.8.74-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

The results screen is back after offline matches on the Static Recomp too, a new switch lets you play with locked content, and the replay viewer no longer stops at the end of a replay.

## New

- Game tab, "Unlock everything" (on by default). Switched off, your save file decides what is unlocked, so a new save starts with the original 14 fighters and the game hands out the rest as you play. Offline only: online play always has everything. Both engines.
- Replay viewer: H hides the navigation bar until you press H again, including while paused or seeking, so a replay can be recorded without it. Every control keeps working while it is hidden.

## Fixes

- Replay viewer (Static Recomp): with Widescreen 16:9 on, the viewer stopped with `video.c:722` when a replay reached its end.
- Static Recomp: the results screen is shown again after an offline VS match, as on the Source Port since 0.8.73. Online matches still return to character select, and holding A and B at the end of a match still starts a rematch.
- Source Port, online: the red flash for a missed L-cancel could only appear on player 1's fighter. In an online match it now follows your own fighter, whichever port you are on.
- Static Recomp, mod discs: the game stopped with `host access outside RAM` when a file was looked up after the game's file table pointer had been overwritten in memory. The game now keeps its own copy of that address, and the log says when it happened.

- Static Recomp, skins: a stage skin much larger than the file it replaces could stop the game on the title screen with `lbmemory.c:233`, when the title demo drew that stage and it no longer fit in memory with the fighters. The demo now plays on another stage that fits.

## Notes

- The L-cancel fix, the file table fix and the title demo fix were made from reports and the code. Those stops were not reproduced here before the fix; the title demo change was checked with a test switch standing in for large skins.
- The Source Port can still stop on the title screen with very large stage skins. The Mods tab names skins that are more than 1 MB larger than the file they replace.
