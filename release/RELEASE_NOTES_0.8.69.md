## Install

Download `MeleeUnlocked-0.8.69-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

Online fixes for the Source Port, safer replay reading and updates, and a key to mark lag moments.

## Fixes

- Source Port: online games were never reported to Slippi at the end of the game. They are now, as on the Static Recomp.
- Online: matches ran at 60.00 frames a second while Slippi Dolphin runs the console's 59.94, so this side slowly drifted ahead of the opponent. Online matches now run at 59.94.
- Launcher, replays: a replay with a short start block was read past its end, and a damaged frame number could make the replay list reserve hundreds of MB. Both are checked now.
- Updater: a download is checked against the SHA-256 the release lists before it is installed, redirects from HTTPS to HTTP are refused, and the archive's contents are checked before an ordinary update unpacks it.
- Skins: giving a skin the picture from a costume's portrait-only entry now turns that entry off, so the standard costume shows its own portrait again.

## New

- Online: press F8 when something feels wrong. The moment is marked in the match's `.trace` file and in the log, so a report can point at it.

## Notes

- The game log now starts with the version and the settings that affect how a match plays.
- The updater checks apply from the next update on: this one is installed by the 0.8.68 updater.
