# Native source-port mod imports

This document describes experimental developer tools. Source Port mods are not a
supported v0.8.0 feature. Normal NTSC 1.02 Source Port play supports Slippi Unranked,
Direct and Teams with rollback and regular Slippi Dolphin opponents.

The source port accepts files from a user-owned NTSC 1.02 Melee disc, patched
NTSC 1.02 Melee ISOs, extracted disc-file directories, and Melee `.gci` saves.
It does not ship any game or mod data.

```powershell
melee_source.exe --iso "C:\Games\Melee 1.02.iso" --mod-iso "C:\Games\Akaneia 1.0.1.iso"
melee_source.exe --iso "C:\Games\Melee 1.02.iso" --mod-gci "C:\Mods\20XXTE.gci"
```

`--mod-iso`, `--mod-dir`, and `--mod-gci` can each be repeated. ISO imports
compare files against the clean disc and overlay only changed or added files.
They require game ID `GALE01`, revision 2. Different enabled packs that replace
the same disc path with different bytes fail before boot and report the path and
both sources. `.gci` imports mount in slot A from the supplied file; saves go to
a private copy under `--card-dir` and leave the supplied file untouched.

An Akaneia 1.0.1 ISO imports successfully and reaches the native menus. The
native port does not yet implement m-ex's expanded fighter/stage registries,
selection screens, DAT callbacks, or Akaneia gameplay. The 20XX Tournament
Edition `.gci` mounts as save data, but its console exploit and 20XXTE behavior
are not executed by the native game. Training Mode Community Edition's native
implementation is under development. Generic imports provide asset/save data,
not automatic execution of console patches.

Akaneia gameplay is intended for Direct matches only. An Akaneia import is not a
playable Direct-mode source-port build yet. The existing cosmetic importer has
its own online validation for local visual replacements; a generic mod ISO or
directory is not automatically safe as an online cosmetic pack.
