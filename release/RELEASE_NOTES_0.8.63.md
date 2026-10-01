# Melee Unlocked 0.8.63

Hotfix for 0.8.62: crashes from your reports, controllers and the settings panel, portraits on their own, the game language in the PC settings, and private crash reports.

## Install

Download `MeleeUnlocked-0.8.63-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Source Port with a skin or file pack: going from the practice screen into Unranked could stop the game. The game switched to the original files for online play but kept files it had already loaded from the pack.
- Source Port: fixed crashes in Adventure mode in the Mushroom Kingdom when a block is hit, in the trophy list in the Gallery, and on the Target Test stages of Mewtwo and Jigglypuff.
- Controllers: after the settings panel closed, the game could stay without controller input when a second controller was plugged in and not at rest. The panel now follows the controller that was last picked up.
- Controls tab: it opens on the controller that plays as player 1, so an Xbox-type controller shows its own buttons and trigger settings instead of the GameCube page. LT and RT can be bound like buttons.
- The launcher's Settings window no longer turns "Auto-open in-game overlay on startup" back on.
- Portraits and stock icons show with a large texture pack on. They always take priority over a pack's picture.
- GameCube adapter: the game now asks the adapter for input at the highest priority. This is meant to stop the polling rate from dropping below 1000 Hz on adapters that show it.
- D3D12 on graphics cards with little video memory: running out of memory for a texture no longer closes the game.
- Crash reports: names, account codes, addresses and folder locations are removed, and memory dumps stay on your PC. A readable `melee_crash_report.md` is saved next to the ZIP.

## New

- Portraits and stock icons on their own: Mods tab, Add portrait. Pick the fighter and the costume, then choose a PNG. It works on an original costume and on a skin. A PNG named after the costume, like `Fox Green.png` or `PlFxGr stock.png`, imports directly, alone or in a ZIP of pictures.
- Game language in the PC settings: Game tab, Game language. Japanese gives the Japanese menus, fighter names and announcer calls.
- Quick chat can be switched off: Game tab, Online, Quick chat.
- Triggers: the point where a trigger counts as a full press is a setting, per controller type.

## Notes

- Lobby matches need both players on 0.8.63.
