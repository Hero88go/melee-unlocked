## Install

Download `MeleeUnlocked-0.8.75-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

Gecko codes can be added from the settings on the Source Port, stage skins can be changed on the stage select screen, and your save is backed up on every start.

## New

- Source Port, Gecko Codes: the "+ Add Gecko code" button is on this engine's page too. Paste the code lines, and a `$Name` line at the top is used as the name. Long codes are no longer cut off when pasted (both engines).
- Stage select: with a stage highlighted, X or R picks the next installed skin for that stage and Y or L the previous one, starting from the standard stage. The pick is saved like a choice in the Mods tab. Offline only. Both engines. With 20XX TE's stage strike or frozen stage toggle on, X and Y stay theirs and R and L change the skin.
- Saves: on every start each save file is copied to a `Backups` folder beside the saves when it has changed. The ten newest copies are kept, and the first copy of each of the last thirty days. Both engines.
- The mouse pointer goes away after resting over the game for a few seconds, also while a menu is open or the game is not the focused window. It comes back when the mouse moves.

## Fixes

- Static Recomp, "Unlock everything" off: the game's own unlock messages are shown again, as on the Source Port.
- Source Port, Gecko codes: addresses are now worked out the way the console's code handler does it. A code that sets a base address and then writes relative to it, stores the base or pointer, or shifts with a register, landed in the wrong place or gave the wrong value before. A code is recognised as one of the built-in switches by its lines, not by its name.
- Source Port, skins: a stage skin much larger than the file it replaces could stop the game on the title screen with `lbmemory.c:233`. The demo now plays on another stage that fits, as on the Static Recomp since 0.8.74.

## Notes

- The mouse pointer change and the Source Port title screen fix could not be exercised in the automated runs here.
- A few Gecko codes that the Source Port accepted in the wrong way before are now refused, or write where a console would. The Gecko Codes page says why for each.
