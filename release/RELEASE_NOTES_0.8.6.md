# Melee Unlocked 0.8.6

Fixes for the 0.8.5 mod update: fighter sounds and a crash in Akaneia and ACE, Wavedash Training, two Source Port crashes, and crash reports, plus a trigger setting for digital triggers.

## Install

Download `MeleeUnlocked-0.8.6-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Akaneia and ACE (Static Recomp): fighter voices and fighter sound effects play again, for the new fighters and the original cast (Fox's laser and taunt included).
- ACE (Static Recomp): fixed a crash in the middle of a match (seen with Tails on Minecart Madness). A game event could arrive at a moment that erased where the game had to return to after starting a sound.
- Classic mode (Source Port): fixed a crash at a stage intro.
- Source Port: a disc image the game cannot read now shows a message asking for a clean, uncompressed 1.02 ISO instead of crashing at startup.
- Training Mode CE (Source Port): Wavedash Training no longer stays on the loading screen. Edgeguard Training had the same problem and is fixed too. Exercises that read a fighter's movement values (landing lag, gravity, friction) now read them correctly.
- A game that stops with an error, including an error inside a mod's code, now shows a message and leaves a crash report the launcher can send. Before, it closed to the launcher without a word.
- Static Recomp: disc reads now land in memory when they complete, and a cancelled read is dropped.

## New

- F1 > Controls > Triggers: a value for L and for R, per controller type. Full works as before. A number makes that trigger analog only, like Dolphin's L-Analog / R-Analog setting: a digital or hair trigger (such as a Switch controller's ZL) gives a light shield, while the other trigger keeps its full shield. Melee shields lightly from 43 (lightest) to 140 (hardest).

## Notes

- Both players need 0.8.6 for a Direct match on a mod disc.
- ACE remains experimental.
