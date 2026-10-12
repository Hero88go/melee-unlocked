## Install

Download `MeleeUnlocked-0.8.96-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Controllers: Xbox controller rumble is now sent from its own thread instead of in the middle of a game frame, as GameCube adapter rumble always was.
- Online: during an online match the game now turns off Windows' background Wi-Fi scanning, which pauses a Wi-Fi connection for up to a second every so often and freezes the match for everyone in it. Wired connections are not touched. Not yet tried on a Wi-Fi PC.
- Online: the log line for a wait on another player's inputs now names the player who was behind. In doubles it named the first player whoever the wait was for.
- Controllers: a controller on the generic path (a GameCube controller on a third-party adapter, a pad the game does not know by name) now has its D-pad bound by default.
