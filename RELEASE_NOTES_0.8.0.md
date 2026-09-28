# Melee Unlocked 0.8.0: Source Port Patch

A second way to run the game: the Source Port, Melee rebuilt as native PC code, now with Slippi online.

## Removal of Ranked

Melee Unlocked is completely free to play, therefore access to Slippi Ranked has been removed. We will not put any modes behind a paywall, and we hope the community understands this decision and that other developers follow suit. We will never be accepting donations or profiting off this project or the Melee community whatsoever. If Slippi were to go free to play, we would have no problem working with them and integrating their ranked system.

Ranked is gone from both game builds. Unranked, Direct and Teams are all still available.

## Install

Download the Windows archive from the release page:

- `MeleeUnlocked-0.8.0-win64.zip`: the one build for everyone. It includes both game builds.

Close Melee Unlocked, then extract the archive over your existing game folder, or use the launcher's Update button. Your settings, saves, and replays carry over. Keep your own Melee NTSC 1.02 ISO beside the game files as `melee.iso`, or select it with the included launcher.

## Source Port

- Pick it on the launcher's Play page under Game Build: Static Recomp or Source Port. Static Recomp stays the default.
- Boots straight into Online Play, like Slippi.
- Slippi online with rollback: Unranked, Direct and Teams, through Slippi's own menus.
- Plays against regular Slippi Dolphin players.
- Party shows in the online menu.
- Online replays are saved like Slippi's.
- Chat on the character select screen.

## DLSS 5 (experimental)

- Looks and processing presets, plus Overall intensity, Structure intensity, Local tone intensity, Skin structure strength, Character mask, Model A/B/C, neural resolution, pass count, scaling filters and reconstruction.
- Needs an RTX 50 card, Direct3D 12, DLAA or DLSS, and your own NVIDIA model file (`nvngx_dlssnr.dll` beside the game). The settings panel tells you if it is missing.
- In our tests more than one pass darkens moving fighters and leaves trails. One pass is recommended.

## Launcher

- New Multiplayer Lobby tab. Tick Go Online to appear to other players. It connects players directly, with no Melee Unlocked server.
- Profile with name, location (optional) and three mains. Your first main is your avatar. Your Slippi code comes from your linked Slippi account.
- Lobby chat with timestamps and emoji. Friend requests, and friends' online, in-game and in-match status with their stocks.
- Match requests show the other player's location, mains and direct ping. Accepting starts a Slippi Direct match. Options to auto-reject requests and set the request sound.
- Local win/loss history against each player.
- New Replay Viewer. Every match is a card with the players, winner, stage, date and length, and opens instantly even with years of replays.
- Match stats page with the same numbers as Slippi: kills, damage, openings and conversions, neutral wins, counter hits, trades, wavedashes, dash dances, inputs per minute, L-cancels, and every stock and punish with its time and percent.
- Pick your own accent color for the launcher.
- Settings opens instantly.
- Click Bailey to put your own picture in her place.
- Older versions show their release date when rolling back.
- Some networks block direct connections, so a player may not show up for everyone. There is no relay yet.

## In game

- Quit game button on every settings screen.
- A loading bar in the middle of the window while the game starts.
- The first match with DLSS on no longer freezes for a few seconds at the start.
- New setting: Auto-open in-game overlay on startup. It's off by default, and F1 still opens the overlay.

## Next version

- Mods on the Source Port: 20XX TE, Akaneia, and Training Mode CE.
- Ray tracing and Ray Reconstruction (hidden in this version).
- More DLSS 5 controls: Diffuse White, Global Tone and Scaling Domain.

## Notes

- Event Match on the Source Port comes in the next version. Use Static Recomp for events.
- If an online match desyncs or crashes, please send the log files from your game folder.
