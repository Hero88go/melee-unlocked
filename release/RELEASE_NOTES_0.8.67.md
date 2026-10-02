## Install

Download `MeleeUnlocked-0.8.67-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

Hotfix for 0.8.66: the 20XX Hack Pack can be started from the launcher, CPUs fight again on the Source Port, skins can be changed on the character select screen, and several crashes are fixed.

## Fixes

- Source Port: CPU players walked up to their opponent and never attacked. They fight now.
- Source Port: Samus's forward air played as loud distortion. A sound cut off mid-sample started its fade from the wrong level.
- Source Port: Multi-Man Melee crashed a few seconds in. It read its opponent list from the disc wrongly.
- 20XX Hack Pack: the launcher still showed "Not supported yet" and Play was greyed out. It plays on the Static Recomp from the Mods page.
- D3D12: the game stopped at start with "pso failed" when a file in the shader cache was damaged. The shaders are built again instead.
- D3D12: frames could line up behind the display when the graphics card was the limit, which added input delay. At most one frame waits now.
- Uncapped frame rate: the picture is held at twice the display's refresh rate, so the game and input threads are not starved.
- Texture memory is kept under a budget on every graphics card, not only on small ones.
- ACE: the results screen stopped the game when the winner was a fighter whose victory tune is not on the disc. Another victory tune plays instead.
- Controls: with a trigger set to a light shield value, a button bound to L or R was ignored. A bound button now gives the full press, so L+R+A+Start works with a bumper.
- Controls: controller profiles did not save the control stick keys, and loading or saving one wrote past the end of its list.
- Replays: a replay of a match played on a mod (its stage is not in the standard game) sat on a blank screen. The launcher now says it needs that mod, and the viewer exits with the reason in the log.
- Crash reports: after watching a replay, the launcher could offer a crash file left by an earlier version as if it were new.

## New

- Skins on the character select screen: with a fighter picked, L and R step that costume through its installed skins. No restart, on both versions.
- Hack Pack costumes: the alternate costumes in the pack (alt L and alt R) show in your skin list. Mods tab, Costume packs: "Use all", "Use alt L", "Use alt R" or "Use none" sets every costume the pack covers in one press. The launcher's Mods page has a "Choose skins" button that opens there.
- 20XX CPUs is a normal option now: Settings, Game, on both versions, no 20XX TE needed. Offline only. The Static Recomp needs the 20XX Hack Pack disc under Mods, and a match played with it is not saved as a replay.
- Overlays, "Network and timing": the last 10 seconds of an online match as you got them: frame time, waits for the other player, rollbacks, time sync and ping. Every online match also saves a `.trace` file next to its replay with the same data, to send with a report.
- Controls, "Per button": each button bound to L or R has its own press depth, and each analog trigger binding its own press point.
- DLSS 5: "Remove haze" slider under Video, Advanced. Off by default.

## Notes

- If the game gets less responsive the longer you play, send `melee_port.log` from that session. The log now records graphics card time per frame.
- Hack Pack discs scanned by 0.8.66 are scanned again on the first start.
