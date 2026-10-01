# Melee Unlocked 0.8.5: Experimental Mod Update

## Install

Download `MeleeUnlocked-0.8.5-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves and replays stay in place. Start the included `MeleeUnlockedLauncher.exe`; use your own Melee NTSC 1.02 ISO.

The archive includes Source Port and Static Recomp. This is an **experimental mod update**: compatibility and training exercises still have gaps. Keep a copy of your previous installation if you need to return to it.

## Mods and training

- Add several mod files at once, use the Mods catalog, or drop files in the Mods folder. Recognized playable packs start On; unsupported packs stay Off with a reason. Custom entries retain their own names. Updates preserve profile saves, and the Mods page exposes file conflicts.
- Supported 20XX Tournament Edition saves and UnclePunch Training Mode CE 1.4d1 run on Source Port. TE settings are under **VS Mode > Tournament Melee** or **F1 > Mods**; UnclePunch exercises are under **1P Mode > Event Match**.
- TE and TM-CE can share a profile. TM-CE owns the exercise's CPU, stage, camera and pause rules, while TE's stored VS choices remain available. Fixed exercise pause/exit issues, including Eggs-ercise controls.
- One L-cancel flash selector appears on Game, Mods and built-in codes: Off, MU missed (red), TE missed (red), TE success, or TE both. Successful TE flashes can be Off, White or Green. Only one flash implementation runs. Selecting a TE effect enables its feature master. Locked TE choices now remain visibly unchanged in the native menu; unlocking preserves the other choices.
- Akaneia uses Static Recomp. Direct requires the same complete mod build on both players; mismatched content is rejected. Added fighters are accepted only by a matching Static mod session. **Akaneia cannot enter Unranked: launch vanilla for Unranked.**
- Source Port training profiles use retail content for Unranked, Teams and Party. Training preflight keeps the exercise loaded while searching, then changes content at the online handoff. A failed search leaves the exercise available.

The 20XX Training Hack Pack is not supported; ACE remains experimental and its VS route is not validated. TE's original memory-card replay manager, extended tag/save fields and TM-CE's console global OSD shortcut are not ported. This release does not claim that every training exercise has been exhaustively tested. See [training compatibility](https://github.com/hero88go/melee-unlocked/blob/v0.8.5/docs/training-mods.md).

## Audio

- Auto and Low latency request the minimum shared Windows period supported by the selected driver. Auto adapts its software queue after actual gaps and remembers the device setting. Exclusive and optional ASIO provide separate device/driver choices and require a restart.
- Each native 5 ms audio block is submitted when mixed. Startup graphics preparation, GPU completion and disc-cache warming finish before simulation and audio start. Fixed audio continuity, pitch, music fades and volume transitions.
- Audio controls distinguish software buffering from the driver/device period. Lower buffers can produce gaps; measured latency depends on the selected device and driver. This release makes no universal fastest-mode or physical GameCube latency claim.
- ASIO SDK notices, Steinberg trademark/logo credits and free DSP coefficient provenance accompany the download.

## Launcher, video and other fixes

- Launcher language choices, Slippi-code friend entry, match-request banners and mod compatibility indicators. Accepted mod lobby games use matching Direct builds.
- Crash reports can be sent through the launcher, with crash text, minidump and logs.
- Fewer duplicated settings; uncommon video and overlay options are grouped under Advanced. TE screen rumble and input-display preferences follow the port's corresponding settings.
- Fixed mod scanning while downloaded files are still being finalized, and repeated texture-pack settings saves duplicating disabled entries.
- Fixed replay aspect handling and kept PC-only recording options out of Slippi Dolphin playback. Source and Static Slippi-widescreen replay captures fill 16:9. Native 2D main menus intentionally retain their authored proportions; Advanced Video's Stretch option can fill the window.
- Fixed startup DLAA/DLSS preparation, adapter polling stutters, launcher-started file-cache stalls and the Classic STAGE CLEAR background on DX11/DX12.
- Source Port offers Slippi widescreen, online frame delay, stock/damage HUD sizing and fighter nicknames; trophy and single-player crash fixes are included.

## Validation and limits

Scripted tests verified TE menu edits and locking, combined TM-CE exercise entry/pause, actual white/green/red L-cancel effects and suppressed success choices. An uninterrupted TM-CE exercise ran for 7,803 simulation frames, and a training-to-Unranked test compared more than 2,000 frames without mismatches. A fresh mixed Source/Static local rollback session compared 1,660 frames with zero mismatches and 76 rollbacks. Fresh tests replayed 20 recordings for 170,736 simulation frames, matching from frame 0 onward. These are local automated checks; they do not replace two-PC/WAN testing or subjective controller/audio assessment.

No Nintendo disc images, mod discs/saves or NVIDIA neural model are included. Both players need the matching release for the lobby changes. Ranked is unavailable.

## Source and licenses

Project code is GPL-3.0-or-later with preserved third-party notices. The source archive includes the actual modified native C sources and build scripts; optional NVIDIA/Intel components keep their separate terms. Proprietary NGX SDK headers/static libraries and local game data are excluded from the public source export.
