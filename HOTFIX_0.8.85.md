## Install

Download `MeleeUnlocked-0.8.85-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## New

- Settings: Frame delay now shows a note when it is set to 1. One frame means more rollbacks online than the default of 2, which can look like fighters jumping or stuttering.
- Controls: Import from Dolphin on the Profile row reads a Dolphin keyboard or XInput controller profile and saves it as a profile here.
- Settings: Always show player tags keeps the P1 / P2 markers over the fighters for the whole match.

## Fixes

- Graphics: a crash inside DirectX at launch is now caught. The game retries, and if DirectX 12 still cannot start it uses Direct3D 11 instead.
- Character select: pressing L or R on a costume now shows the picked skin's name on screen, and says why when a skin cannot be picked.
- GameCube adapter: closing the game could hang while the adapter was being released.
