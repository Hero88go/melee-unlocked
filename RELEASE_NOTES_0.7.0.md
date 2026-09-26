# Melee Unlocked 0.7.0

New settings menus, cosmetic imports, and one download that includes DLSS 5.

## Install

Download the Windows archive from the release page:

- `MeleeUnlocked-0.7.0-win64.zip`: the one build for everyone. It includes the standard graphics options and optional experimental DLSS 5, plus a compatibility version the launcher picks automatically on older CPUs. DLSS 5 needs NVIDIA's DLSS 5 file, which is not included.

If you were using the old DLSS 5 Experimental build, download this archive manually once; your launcher will update normally after that.

Close Melee Unlocked, then extract the archive over your existing game folder. Your settings, saves, and replays carry over. Keep your own Melee NTSC 1.02 ISO beside the game files as `melee.iso`, or select it with the included launcher. Start the included `MeleeUnlockedLauncher.exe` after extraction. If you use the launcher's Update button, it does all of this for you.

## Major features

- New settings menu with six appearances to choose from in Customize, each with its own color palettes. The previous menu is still available on F11 after turning on Legacy Menu.
- Esc now opens the settings menu. On a GameCube controller, Start + D-pad Down + Z opens it and Start closes it.
- Cosmetic imports: add costume and stage DAT files, ZIP mods, and Nucleus vault archives from Customize. Stage changes that are not purely visual use vanilla files online.
- One download: DLSS 5 is now part of the main build instead of a separate experimental zip.
- Launcher version rollback: pick an older release from the Play page without replacing your current install.
- Animated character select and stage select backgrounds from your own video files.
- Experimental ray traced lighting with DLSS Ray Reconstruction on D3D12, and optional ambient occlusion. Both are off by default.

## Full changelog

- Smoother frame pacing in matches and menus at high refresh rates, with far fewer long frames.
- Pokémon Stadium: fewer hitches around transformations.
- Every settings page scrolls smoothly at your refresh rate.
- Menu sounds, with a switch on the Audio page.
- Stock icon and damage number size controls.
- Optional player nicknames above fighters.
- Lagless FoD is now its own Video option; Fountain of Dreams reflections stay on unless you turn it on.
- The color picker in Customize is folded away until you open it.
- The corner "Settings: F1" reminder is off by default; turn it back on under Overlays.
- Typing in another window no longer controls the game. Background input now applies to controllers only.
- Closing the settings menu no longer passes the button you pressed through to the game.
- Turning controller rumble off stops the motors immediately, including online.
- The launcher's Play page shows the game build. A source port option is listed as coming soon.
- The updater now always uses the single download, so future updates reach every install.
- Legacy menu: Esc no longer flashes the new menu behind it, and it always reopens on screen.
- The launcher's Settings window shows the classic settings screen again, and closing it no longer crashes.
- Watching replays with WatchReplay.bat works again; it crashed on start in recent versions.

## Notes

- DLSS 5 needs NVIDIA's DLSS 5 file, which is not included and which this project does not provide.
- Akaneia, 20XX, and UnclePunch builds are not supported online in this version.
- If you see a desync, please send the match replay (`.slp`) and your `melee_port.log`.
