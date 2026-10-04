## Install

Download `MeleeUnlocked-0.8.72-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

Crash fixes for the ACE build from your reports, a fix for a message stuck on screen online, skins listed per costume slot, and many more Gecko codes on the Source Port.

## Fixes

- Online: after a search started with Tab failed, "Returning to practice..." could stay in the middle of the screen through later online matches until the game was closed. It now goes away when an online match starts, or after ten seconds if the game never gets back to the practice scene.
- Static Recomp, ACE build: the results screen stopped the game with `OSPanic at m-ex:0` ("fighter 60 has no symbol") after a match with Crazy Hand. The disc names results files for its boss and special fighters that it does not have. Those fighters now show no model on the results screen and the game goes on (checked with Crazy Hand).
- Static Recomp, ACE build: the title screen could stop the game with `lbmemory.c:233`. Its demo match draws from all of the build's fighters but keeps the original memory sizes, and some draws do not fit. On this disc the demo now uses the original 26 fighters. The stop itself was not reproduced here, so this is a guard, and the log now names the heap and the fighters drawn if it happens again.
- Source Port, 20XX Hack Pack overlays: a CPU's automatic L-cancel flashed as a miss, and a color overlay that had ended could repaint a later L-cancel flash in its own color.

## New

- Mods: skins are listed by fighter, one row per costume slot, each with a picture tile and a picker of the skins installed for that slot.
- Source Port, Gecko codes: more of your codes run. Fill, string and serial writes, pointer-relative writes, if and endif, and the Gecko registers are supported, a code can read game variables, and 905 variables can be written (219 before). Codes that are already built in are recognized by their contents and follow the built-in switch. Codes that change the game's code still need the Static Recomp, and the list says why for each. Never applied online or in replays.
- Source Port, 20XX Hack Pack (still opened from the debug menu): a Character Select Screen Codes page, and the pack's hidden fighters play in a match (Giga Bowser, Popo alone, both wireframes, Master Hand, Crazy Hand, Sandbag).

## Notes

- Found by reading the code, not from a crash report, and now guarded with tests on damaged data:
  - a matchmaking reply with a wrong player count, a wrong port or a field of the wrong type is refused and the search fails with a message;
  - the match result and the character and stage picks are no longer shared between threads without a lock;
  - a damaged replay is refused by the Static Recomp replay viewer, and the launcher's replay statistics no longer size their memory from numbers inside the file;
  - a damaged song file on a mod disc can no longer make the music player read past the end of the file.
- Skin pictures come from the picture files a skin ships with. A skin without one shows a plain tile.
