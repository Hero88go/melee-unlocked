# Native source-port mod imports

The source port accepts files from a user-owned NTSC 1.02 Melee disc, patched
NTSC 1.02 Melee ISOs, extracted disc-file directories, and Melee `.gci` saves.
It does not ship any game or mod data.

```powershell
melee_source.exe --iso "C:\Games\Melee 1.02.iso" --mod-iso "C:\Games\TM-CE.iso"
melee_source.exe --iso "C:\Games\Melee 1.02.iso" --mod-gci "C:\Mods\20XXTE.gci"
```

`--mod-iso`, `--mod-dir`, and `--mod-gci` can each be repeated. ISO imports
compare files against the clean disc and overlay only changed or added files.
They require game ID `GALE01` or Training Mode CE's `GTME01`, revision 2.
Enabled packs apply in profile order; a later replacement wins and the settings
panel reports file conflicts. `.gci` imports mount in slot A from the supplied file; saves go to
a private copy under `--card-dir` and leave the supplied file untouched.

In v0.8.5, Training Mode CE's menus, events and Training Lab run as native code
with files from the player's own TM-CE disc. Importing a recognized 20XX
Tournament Edition save enables its native offline features and settings menu.
Generic imports provide asset/save data, not automatic execution of console patches.

Akaneia is disabled until its native fighters and stages are complete. Its ISO
and extracted directory imports are refused before any menu or costume files
are published. The existing cosmetic importer has its own online validation for
local visual replacements; a generic mod ISO or directory is not automatically
safe as an online cosmetic pack.
