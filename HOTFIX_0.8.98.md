## Install

Download `MeleeUnlocked-0.8.98-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Mods: batch import. In Settings, Mods, "Import / Refresh..." and "Add stage DAT..." now take several files at once. With "Add stage DAT..." every file you pick becomes its own version of the stage you choose, each in the random pick. The message counts what went in and names any file that did not, with the reason.
- Settings: "Character select background plays through". The character select background starts its animation over every few seconds, which cut off custom backgrounds with longer animations. With this on it plays on. Menu visuals only, so it also works online. The original background stops on its last frame with it on.
- Source Port, online: a desync against the original game where Fox's and Falco's tails were placed wrong, so a hit on a tail could miss. A recorded match that desynced now plays back without a difference.
- Static Recomp, 20XX Hack Pack 4.07++: entering the character select crashed. The disc was not recognized as a 20XX Hack Pack and ran with Slippi's codes; it now runs in the pack's own mode, as 5.0 does.
- Launcher, Get: a mod could sit on "Get" when the GitHub release service did not answer (it limits how often it can be asked, and some networks block it). The launcher now downloads the verified file directly and checks it against its pinned checksum.
- Display, exclusive fullscreen: when the frame cap does not divide the screen's refresh rate, the game now picks the same resolution at a refresh rate the cap divides, so every frame stays on screen equally long. Borderless and windowed are unchanged. Not yet confirmed by the player who reported it.
