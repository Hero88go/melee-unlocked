## Install

Download `MeleeUnlocked-0.8.8-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

A fix for a message stuck on screen online, skins listed per costume slot, and many more Gecko codes on the Source Port.

## Fixes

- Online: after a search started with Tab failed, "Returning to practice..." could stay in the middle of the screen through later online matches until the game was closed. It now goes away when an online match starts, or after ten seconds if the game never gets back to the practice scene.
- Source Port, 20XX Hack Pack overlays: a CPU's automatic L-cancel flashed as a miss, and a color overlay that had ended could repaint a later L-cancel flash in its own color.

## New

- Mods: skins are listed by fighter, one row per costume slot, each with a picture tile and a picker of the skins installed for that slot.
- Source Port, Gecko codes: more of your codes run. Fill, string and serial writes, pointer-relative writes, if and endif, and the Gecko registers are supported, a code can read game variables, and 905 variables can be written (219 before). Codes that are already built in are recognized by their contents and follow the built-in switch. Codes that change the game's code still need the Static Recomp, and the list says why for each. Never applied online or in replays.
- Source Port, 20XX Hack Pack (still opened from the debug menu): a Character Select Screen Codes page, and the pack's hidden fighters play in a match (Giga Bowser, Popo alone, both wireframes, Master Hand, Crazy Hand, Sandbag).

## Notes

- Skin pictures come from the picture files a skin ships with. A skin without one shows a plain tile.
