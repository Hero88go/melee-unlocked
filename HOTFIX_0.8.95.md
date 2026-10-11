## Install

Download `MeleeUnlocked-0.8.95-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Direct3D 12: with DLAA or DLSS on, the game could stop before its first frame with "upscaler warm-up close failed". It now turns off the part that failed for that session and starts.
- Display: with sub-frame animation on, the stars behind Battlefield now move between game frames instead of stepping behind the fighters.
- Direct3D 12: when the graphics device is lost, the stop message now says why (the GPU stopped answering, a driver reset, a driver error, or the GPU or driver was removed or updated).
- Source Port, Training Mode CE: after the L-cancel event, a barrel in a later mode (Classic, for one) could crash the game.
- Lobby: two players who each send the other a friend request are now friends at once. Before, each had to find and accept the other's request.
- Controllers: new Control stick radius and C-stick radius settings per controller type (PC settings, Controls, Sticks), like Dolphin's Radius. Lower it if the stick reaches full speed before a full push.
- Controllers: the Switch 2 Pro Controller is read over USB the same way as the Switch Online GameCube controller (WinUSB on its Interface 1, set with Zadig). Not yet tried on a real controller.
- Launcher: when Send recent game logs cannot send, it now says why and saves the logs as melee_logs_report.zip so you can attach the file yourself.
