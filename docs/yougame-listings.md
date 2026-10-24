# Separate YouGame listings with one Melee Unlocked engine

Use the same upstream repo and native engine for the vanilla, Akaneia, and ACE listings.
The native mod implementations remain part of the engine. Each listing chooses its session
through command-line arguments; game and mod data still come from the player's own files.
Normal Melee Unlocked launches keep their existing mod support and defaults.

## Vanilla listing

```powershell
.\melee_source.exe --iso "C:\Games\Melee.iso" --slippi-menus off --vanilla-only --settings-path "Listings\Vanilla\settings.ini" --card-dir "Listings\Vanilla\CardA"
```

`--slippi-menus off` (Source Port only) keeps the online menu layer out of the process: no patched
menu files are built or served, the game boots as the disc does (logos, opening movie, title
screen, the normal main menu) and plays its own music stream. Nothing online is reachable from
inside that session. Add `--vanilla-game` to also leave out the built-in UCF and unlock codes, so
the disc runs exactly as shipped. The launcher's GAME BUILD row offers this as its Vanilla
segment (launcher.ini `vanilla=1`, `vanillaowncode=1`), with the same flags.

`--vanilla-only` works on both `melee_source.exe` and `melee_port.exe`. It:

- Verifies that the ISO is unmodified Melee NTSC 1.02, including its data files.
- Ignores saved gameplay mod profiles and installed packs, and disables gameplay mod activation.
- Suspends user Gecko codes, training/CPU extensions, and automatic L-cancel input injection.
- Keeps rendering, controller, audio, and normal online functionality. Cosmetics use their
  existing gameplay-safe online forms even offline.
- Preserves saved mod selections for the player's next normal launch.
- Uses `VanillaOnly` underneath the supplied card directory, so a modded save in an ordinary
  card folder cannot carry into this listing. The player's other saves remain untouched.

The flag lasts for this process and its restarts; it is never saved as a preference.
Combining it with explicit gameplay mod arguments is an error regardless of argument order.
Replays recorded with gameplay mods require a normal launch.
This is a launch restriction, not an anti-cheat or ranked attestation mechanism.
It is separate from the existing `--vanilla-game` code-parity diagnostic.

## Akaneia and ACE listings

Choose the desired mod explicitly and use `--no-mod-autodetect` to skip other packs in the
working directory's Mods folder. Keep separate settings and card directories per listing.
Create the settings directories before launching. These examples use the existing Source Port
native mod import path; use supported mod versions and keep each listing's engine version pinned
to the version it was tested with.

```powershell
.\melee_source.exe --iso "C:\Games\Melee.iso" --mod-iso "C:\Mods\Akaneia.iso" --no-mod-autodetect --settings-path "Listings\Akaneia\settings.ini" --card-dir "Listings\Akaneia\CardA"
.\melee_source.exe --iso "C:\Games\Melee.iso" --mod-iso "C:\Mods\ACE.iso" --no-mod-autodetect --settings-path "Listings\ACE\settings.ini" --card-dir "Listings\ACE\CardA"
```

Existing `--mod-profile`, `--mod-dir`, and `--mod-gci` arguments remain available. An explicit mod
layer overrides a saved profile. `--no-mod-autodetect` skips discovery only; explicit imports
still run through the native implementations and their normal compatibility checks.

For a supported Static Recomp mod listing, the boot ISO is the mod and the reference ISO is retail:

```powershell
.\melee_port.exe --iso "C:\Mods\Akaneia.iso" --mod-base-iso "C:\Games\Melee.iso" --no-mod-autodetect --settings-path "Listings\AkaneiaStatic\settings.ini" --card-dir "Listings\AkaneiaStatic\CardA"
```

Use the normal matching build/content checks for online sessions. YouGame owns the listing,
downloads, queue and launch arguments; Melee Unlocked supplies the existing native gameplay.
The launch options do not implement a new matchmaking backend.
