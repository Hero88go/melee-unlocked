# Melee Unlocked 0.8.66

Hotfix for 0.8.65: skins online, the lobby, volume, and fixes from your reports. The 20XX Hack Pack runs on the Static Recomp.

## Install

Download `MeleeUnlocked-0.8.66-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Skins online: a skin made with a model tool was replaced by the standard costume in online matches, with no message. The check that keeps online matches in sync was stricter than it needed to be. Skins that keep the standard skeleton now stay on.
- Lobby: players could not see each other when the network the lobby uses to find players was partly down. The launcher has more ways into it.
- Volume and Music: moving only these sliders did not save, and the game could come back at full volume. Both save now.
- Source Port online: when the opponent disconnected, the picture froze until the game was closed. The match now ends with the disconnect message.
- D3D11 on integrated graphics: resizing the window could close the game when video memory ran out.
- Mods: an ACE disc was listed as Akaneia 1.0.0. It is listed as ACE.
- Direct with a mod against an opponent on Slippi Dolphin: the match starts on its own. The "Opponent uses it on Slippi Dolphin" switch is gone.
- Lobby: "Finish game setup" now says to choose your Melee ISO on the Play page.

## New

- 20XX Hack Pack 5.0.2 on the Static Recomp: add the disc under Mods. It runs without Slippi's code, so there is no online play or replay recording while it is loaded, and the picture is 4:3.
- Skins from a mod disc: costumes in a disc under Mods show in your skin list as "from that disc" and can be used in the normal game, online too when they keep the standard skeleton.
- Mods tab: each skin says whether it stays on online, and why not. A notice shows when you go online with a skin that is switched off there.
- Private chat in the lobby: pick a player, Private Chat. They accept or decline; a room opens for the two of you.
- 20XX CPUs (Source Port, with 20XX TE on): CPUs play with the 20XX Hack Pack's AI. Off by default, offline only.
- Frame delay: the setting shows how much input delay a value above the default adds.

## Notes

- Lobby matches and private chat need both players on 0.8.66.
- The log says when the game waited for the other player's inputs and for how long. If a match felt off, send `melee_port.log` from that session.
