# Melee Port 0.1.1 beta

Same install as 0.1.0: extract, drag your Melee NTSC 1.02 ISO onto `MeleePort.bat`. Settings and saves from 0.1.0 carry over (`port-settings.ini`, `User\`).

## New since 0.1.0

- Slippi game reporting: online games are reported to Slippi like Dolphin does, with the replay upload; ranked sets report their status, and your rank is fetched at login and after ranked games (rank display in the online menus)
- Sub-frame animation now has two modes in settings: "Predict ahead" (no delay) and "Interpolate" (exact in-betweens of the last two game frames, one frame of delay, no overshoot)
- Animated scale in character and stage animations is sampled between frames instead of held
- Settings panel opens at every launch (there is a checkbox to stop that) and a Settings button stays in the top right corner; F1 and Z + Start still work

## Verified against Slippi Dolphin

Three of the author's Dolphin-recorded replays play back in the port's playback build with identical positions, action states, percent and stocks on every frame (one float ulp on a spawn animation in two of them, which never propagates). Online checksums against Dolphin clients agree.

## Known gaps

- Ray tracing is not implemented
- Audio is an approximate mixer
