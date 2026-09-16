# Melee Unlocked 0.2.2

## Install

Extract the zip over your existing folder, or let the launcher update. Settings, saves and replays
carry over.

## Fixed

**Settings changed in the F1 panel came back undone on the next launch.** The launcher and
`MeleeUnlocked.bat` passed frame rate, sub-frame mode, internal resolution and volume on the command
line every time they started the game, and the command line is applied after the settings file is
read, so those four overwrote whatever had been saved. They are now only used to set up a first run,
before a settings file exists, and after that the panel is in charge.

Settings are saved in `port-settings.ini` next to the game and nowhere else. Anything outside those
four (widescreen, DLSS, sharpness, anisotropic filtering, overlays, controller bindings) was always
saved correctly.

**The launcher forced Predict ahead.** 0.2.0 made Interpolate the default, but only in the batch
file, so anyone starting the game from the launcher still got Predict ahead. Both now agree, and
neither overrides a saved choice.

## Thanks

- Stache: settings reverting on relaunch
