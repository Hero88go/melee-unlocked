## Install

Download `MeleeUnlocked-0.8.99-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- New, Display, Widescreen: three more True widescreen shapes, all experimental: True 16:10, True custom (enter your own W:H) and True: fit my screen (the window's shape). They widen the picture in the renderer only and change nothing the game simulates, so they can be used online (not yet tried in an online match). The widest shape is 21:9; a wider screen gets black bars at the sides. On the character select the grey bands at the top and bottom and the BACK corner now reach the screen edges at shapes wider than 16:9.
- Source Port: a Fountain of Dreams stage skin without the stage's reflection texture (one from the 20XX Hack Pack) crashed the game when the match started.
- Static Recomp and Source Port: when an old graphics card (one that only reaches the Direct3D 11 level, such as Intel HD Graphics 4600) loses the Direct3D 12 device, the game now switches its own setting to Direct3D 11, so the next start uses it, and says so. Tested with a simulated device loss, not on that card.
- Launcher: a game program left over from an older release in the same folder started and stopped at once. The launcher now names the file and both versions and says to extract the release again, instead of starting it.
- Source Port: a costume file from another color slot on the disc (for example a skin put in place of the default color) stopped every match at load. The game now uses the model that file holds.
- Video backgrounds: a video file Windows cannot read (a broken link, a cloud file that is not downloaded) no longer stops the game; it counts as missing.
- Source Port: a Wobbuffet counter could hit with an impossible amount of damage and stop the game. The damage now drops to zero at the end of the swing, as on the console. Tested with a code test, not in a match.
