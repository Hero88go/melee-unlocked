# Melee Unlocked 0.8.64

Hotfix for 0.8.63: the freeze at Master Hand, menus on 4K displays, and GameCube adapter rumble.

## Install

Download `MeleeUnlocked-0.8.64-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Source Port, Classic mode: the game froze at the start of the Master Hand fight. His HP and timings were read wrongly from the disc, so the game treated him as already beaten.
- Choosing a file that is not a full Melee NTSC 1.02 disc image (a compressed image, another game or another version) gave a crash report. The launcher now says what is wrong before the game starts.
- The launcher's Settings window follows the display scaling of Windows. On a 4K display it opened small, with small text.
- The in-game settings panel, the Esc menu and the overlays grow with the window above 1080p. At 1080p and below they are unchanged.
- Results screen (Source Port): the fighter portraits in the placing panels were black, and every player was drawn as the winner.
- GameCube adapter: rumble commands are sent on their own thread, so a rumble change no longer delays the next controller read.

## Notes

- The log now has one line per online game with the frame delay, average ping, rollbacks and stalled frames. If a match feels off, send the log from that session.
- Lobby matches need both players on 0.8.64.
