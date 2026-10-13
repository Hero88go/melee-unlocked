## Install

Download `MeleeUnlocked-0.8.96-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Mods: a mod disc in the Mods folder now lists its stages as stage skins, the way it already listed its costumes. Alternate stages a disc keeps beside the standard one (`GrNBa.0at`, `GrNBa.1at` and so on) each become a skin of that stage, so "Random installed stage skin each match" can rotate a whole disc of them with no stage-by-stage import.
- Controllers: Xbox controller rumble is now sent from its own thread instead of in the middle of a game frame, as GameCube adapter rumble always was.
- Online: the log line for a wait on another player's inputs now names the player who was behind. In doubles it named the first player whoever the wait was for.
- Controllers: a controller on the generic path (a GameCube controller on a third-party adapter, a pad the game does not know by name) now has its D-pad bound by default.
- Mods: when a mod disc runs out of the game's memory while loading a screen (an older Akaneia build's character select does), the stop now says so and suggests the mod's newest build, and the log lists what filled the memory.
- Mods, Static Recomp: "Random installed stage skin each match" did nothing. It now picks a skin at the start of each match.
- Source Port: a crash on stages with Topi carrying ice blocks (Icicle Mountain): the Topi kept pointing at an ice block that was already gone.
- Source Port: when the game program and its game library come from different versions (a partly updated folder), the stop now says to extract the whole release zip again instead of only naming an API version.
- Display: the log now names the monitor's refresh rate and says when the frame cap does not divide it, which makes the picture uneven unless G-Sync or FreeSync is on.
- Online, Source Port: rumble now reaches your controller when you are not player 1 in the match. It was dropped before, while the Static Recomp rumbled.
- Audio: the game's sound is now declared to Windows as game audio instead of an unnamed stream, for players whose other apps got quieter when the game started. Not yet confirmed by that player.
- Source Port, Pokemon Stadium: the leader shown on the stage's screen no longer has the other player's name drawn over it.
- Source Port: the KO stars on the character select count again when the results screen is skipped (offline VS).
