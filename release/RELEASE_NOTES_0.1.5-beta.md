# Melee Unlocked 0.1.5 beta

Fixes the updater. 0.1.4 shipped a game executable that still identified itself as 0.1.3, so the
settings panel offered the same update forever, and the script that installs an update had a
line-ending bug that stopped it restarting the game afterwards.

If you are on 0.1.4 (or a build whose panel says 0.1.3 while offering 0.1.4): press "Update and
restart" once more, then start the game yourself that one time. The broken script is the one
already on disk, so it still cannot do the restart. From this version on it can.

Everything in 0.1.4 is included: the simulation is no longer held up by the renderer, looping
animations no longer freeze a fighter for a frame, scrolling stage textures and effects that
rebuild their geometry now move at display rate, audio tracks the sound card's clock instead of
inserting silence, and downsampling is correct on both axes.

## Fixed here

- The version compiled into the game did not follow the release number, so the panel reported an
  old version and offered an endless update. Packaging now refuses to build a release whose
  executable disagrees with the version file.
- The update script was written in text mode, so every line ended with two carriage returns. The
  extra one became part of the last argument on each line, which is why the game closed, the files
  were copied, and nothing started again.
- An update now restarts exactly the command that was running, so it works the same from the
  release batch file, the launcher, or a shortcut with its own arguments.
- An update writes `update.log` next to the game, naming each step, so a failure can be read
  rather than guessed at.
