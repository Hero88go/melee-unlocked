## Install

Download `MeleeUnlocked-0.8.73-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

Crash fixes for Adventure and Classic on the Source Port from your reports, and the results screen is back after offline matches.

## Fixes

- Source Port, Adventure: the game closed at the end of a stage (`melee_game.dll+0x144369`).
- Source Port, Adventure: the game stopped with `ftcoll.c:1089` ("attack power over 500") when an enemy or item hit landed with a damage value below zero. The value is now read the way the console reads it.
- Source Port, Classic: the game closed when the credits started.
- Source Port: the results screen is shown again after an offline VS match. Online matches still return to character select, and holding A and B at the end of a match still starts a rematch.
- Mods, Skins: two imports of the same stage file showed a red error box and two rows. They are now one row with both skins in its picker.

## Notes

- The three crash fixes were made from the crash reports and the code. The stops were not reproduced here before the fix.
- Static Recomp, ACE build: every added fighter on the build was played through a match and the results screen with no stop, so the launcher no longer lists the build as untested. Its stages and matches with more than two players were not part of that run.
- Static Recomp: the results screen is still skipped after offline matches.
