## Install

Download `MeleeUnlocked-0.8.71-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

Controller fixes from your reports, L and R split into click and analog, marks for online problems from the controller, and crash fixes for modded setups.

## Fixes

- Xbox controllers: with Z (or anything else) bound to LT or RT, the trigger also kept shielding, so one pull grabbed and then shielded. A trigger given to another action is now that action's alone.
- Costumes: a costume with a mesh skinned to a bone that has no bind matrix stopped the game with `pobj.c:1896` when it was drawn. Such a costume is no longer loaded, and the Mods tab says why.
- Skins online: a skin whose bone tree matched the original could still be switched off, because a child stored at offset 0 was read as "no child". A skin that is switched off says which bone differs and how (moved, resized, rotated, or a different bone count).
- Saves: writing the memory card file could be refused when another program (a virus scanner, the search indexer) held the file for a moment, and that save was lost. Both engines try again for a short while now.
- Long sessions: screen copies the game no longer used were kept until the game closed and were not counted in the texture memory limit. They are released after a while, sooner when video memory is short.
- Static Recomp: a bad buffer address handed to the memory card, disc or Slippi device is refused and logged instead of ending the game with "host access outside RAM". This is a guard for the stage clear crash in Adventure: the crash itself was not reproduced.

## New

- Controls: L and R are each two bindings now, the click (L, R) and the travel (L analog, R analog), so a light shield and a full press can sit on different buttons. Your existing bindings are carried over.
- Controls: every button bound to an analog input (a trigger or a stick direction) has its own press point slider.
- Online: mark a moment for a report without leaving the controller. D-pad Left "looked wrong", D-pad Right "input wrong", D-pad Down "sounded wrong". On the keyboard: F6, F7 and F9, with F8 as a plain mark. The marks are in the `Game_*.trace` file beside the replay.
- Source Port: your Gecko codes that only write game variables run here too. Codes that change the game's code still need the Static Recomp, and the list says which function or variable each one writes. Never applied online or in replays.
- 20XX Hack Pack on the Source Port (still opened from the debug menu): the pack's color overlays (hitlag, hitstun, IASA, auto-cancel, wavedash).

## Notes

- Mods: the log names every override file more than 1 MB larger than its original. Files that large can fill the game's memory and stop it with `lbmemory.c:233`.
- Low poly fighters draws about half the vertices in a match on both engines (measured on Final Destination).
