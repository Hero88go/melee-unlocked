# Melee Unlocked 0.2.1

A hotfix for the launcher. Nothing in the game itself changed from 0.2.0.

## Install

Extract the zip over your existing folder, or run the launcher and let it update. Settings, saves
and replays carry over.

## Fixed

**The launcher offered the 0.2.0 update forever, and installing it changed nothing.** The launcher
is a separate program from the game, with its own version compiled into it, and the 0.2.0 zip
shipped a launcher built before the version was raised. It called itself 0.1.14, saw 0.2.0 on
GitHub, and offered the update. Installing it replaced the files with the same zip, which contained
the same old launcher, so it offered the update again, and manually downloading did the same thing.

The game in the 0.2.0 zip was genuinely 0.2.0, so anyone who pressed Play was already running the
new build and getting every fix in it. Only the launcher's own version, and therefore its update
check, was wrong.

Packaging now refuses to build a release whose launcher does not carry the release version, which
is the check that was already in place for the game and should have been there for the launcher
too. The same class of mistake shipped once before, in 0.1.4.

If you are stuck in the update loop: install this build once, either by letting the launcher update
or by extracting the zip over your folder, and it stops.

## Thanks

- **Stache** for reporting it precisely: that the update kept being offered, that installing it
  left the version at 0.1.14, and that updating manually from GitHub did the same. Those three
  facts together are what identified it as the launcher rather than the game.
