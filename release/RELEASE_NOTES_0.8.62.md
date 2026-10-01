# Melee Unlocked 0.8.62

Hotfix for 0.8.61: costume and stage imports, and a clear message when a mod disc is set as the Melee ISO.

## Install

Download `MeleeUnlocked-0.8.62-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Costume and stage imports: a costume kept offline-only (its skeleton differs from the original) could stop the game. A shorter file than the original stopped it when a match loaded that fighter, on both engines. A longer one stopped an online match on the Static Recomp. Both now load, and online the original is used as before.
- Imports accept `.usd` files, alone or inside a ZIP.
- Captain Falcon's red costume and Pokemon Stadium: an import now replaces the English file too. The disc keeps these twice, and the English game loads the `.usd`, so an import used to change nothing unless the game language was Japanese.
- Static Recomp: starting a mod with a modified disc set as the Melee ISO now shows a message asking for a clean NTSC 1.02 ISO. It used to stop at boot with a sound error.

## Notes

- Lobby matches need both players on 0.8.62.
