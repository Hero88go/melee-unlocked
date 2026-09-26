# Native cosmetic imports — v0.7

The v0.7 Customize page imports costume DAT/ZIP files, standalone stage DAT/ZIP files, and
Nucleus vault ZIPs. It shows supplied CSP or stage screenshot PNGs, marks the active selection,
and stages changes for the next launch. Tom Nook and Patched Plains Dream Land are local test
fixtures; no mod or ISO is bundled with the build.

Pokémon Stadium's `GrPs.dat` and `GrPs1.dat` through `GrPs4.dat` are separate disc resources.
An imported stage DAT is matched by its actual resource name. Empty or invalid ZIP choices do
not appear. A valid `GrPu.dat` Poké Floats import remains available.

Fox's Fire Fox, Reflector, Laser, and Illusion have separate selections. Each selected effect is
validated against the exact clean ISO; compatible byte changes are composed against that clean
resource, and conflicting writes are rejected with the offset and variant names. Older profiles
with a single effect choice per DAT migrate that choice to its move slot.

Stage replacements that fail exact-ISO texture-only validation remain available offline. Online
disc reads for those stage slots use the clean resource. Profile changes are frozen while an online
search or match is active, and all changes require a restart to affect the running asset snapshot.

The sections below document earlier importer milestones and may describe policies superseded by
the v0.7 behavior above.

Base commit: `cfea2f7f994a1427a2c1c10bf2dfd81b7fc3cab2` (fork `finalstock-dev/melee-unlocked`).

This milestone proves one model-replacement path without modifying the clean ISO or the translated
vanilla DOL. It imports a structurally recognized costume DAT, stores it in one content-addressed
catalog/profile, replaces the corresponding logical GameCube disc file at runtime, and restores the
vanilla file through a restart-safe profile change.

## Use it

1. Build with your own clean NTSC 1.02 ISO:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools/build_cosmetic_milestone.ps1 -Iso C:\Games\melee.iso
   ```

2. Run the automated source/import/loader/restore diagnostic:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools/run_cosmetic_diagnostics.ps1 `
     -Iso C:\Games\melee.iso -TomNookZip C:\Mods\Tom Nook.zip
   ```

3. Launch the built game, press F1, open **Mods**, and use **Import / Refresh**. Select the supplied
   `Tom Nook.zip` or its DAT. The importer reports `Fox — Green (PlFxGr.dat)`.
4. Restart when prompted, then select green Fox. A profile change never hot-swaps a DAT that the
   guest may already have loaded.
5. To turn one slot off, press **Disable slot**. To turn every override off, press
   **Restore Vanilla**, then restart. Imported catalog files remain available for later selection.

The equivalent automation commands are:

```powershell
melee_port.exe --settings-path test\port-settings.ini --import-cosmetic "Tom Nook.zip" --cosmetic-status
melee_port.exe --settings-path test\port-settings.ini --restore-vanilla-cosmetics --cosmetic-status
```

## What is validated

The supplied archive resolves as follows:

| Field | Value |
|---|---|
| ZIP member | `Finished Tom Nook/Tom Nook.dat` |
| DAT length | 315,098 bytes |
| SHA-256 | `7bfb26b594252f0a13a4cdf46554217fbc574a31245626583dc16a63e2207d92` |
| Required roots | `PlyFox5KGr_Share_joint`, `PlyFox5KGr_Share_matanim_joint` |
| Logical target | `PlFxGr.dat` (Fox, green costume) |

The mapping comes from both DAT root symbols, not the uploaded filename. A malformed size/table,
missing or conflicting roots, unsafe archive path, encrypted/unsupported ZIP member, symlink,
oversized resource, extraction-size mismatch, CRC mismatch, or stored-asset hash mismatch is
rejected. The archive and resource limits are 512 MB aggregate, 4,096 entries, and 64 MB per DAT.

ZIP extraction uses the `tar.exe` included with supported Windows 10/11 versions, after the native
code validates the central directory and every matching local header. Only the one validated member
is extracted into a private staging file. Catalog and profile JSON are committed by write-through
temporary-file replacement.

## Why the loader is file-aware

The guest opens `PlFxGr.dat` through the ISO FST. On boot, the host resolves the selected logical
path against the exact supplied ISO and changes that FST entry's logical length. The original FST
start remains its stable file identity. `DVDReadPrio` and `DVDReadAsyncPrio` then pass the file start
and relative offset to the override layer, which supports partial reads and the final 32-byte-aligned
tail. Absolute disc reads still use the clean ISO.

This matters because Tom Nook's DAT need not be the same length as vanilla `PlFxGr.dat`. Treating a
replacement as raw bytes at the old disc offset would let a longer file overlap its physical
neighbor and would leave the guest with stale file-table metadata.

The active runtime map owns immutable byte buffers and is published before the guest initializes
DVD. Settings changes affect only the desired on-disk profile. Restarting drops guest RAM, DVD
requests, model data, and renderer textures before a different snapshot is published; Restore
Vanilla therefore needs no backup and never writes to the ISO.

## Catalog, variants, and conflicts

Files live beside `port-settings.ini`:

```text
CosmeticMods/
  state.json
  catalog.json
  profile.json
  assets/<content-id>/<logical-target>.dat
  assets/<content-id>/companions/{csp,stock}.png
```

`state.json` is authoritative and commits catalog plus profile through one write-through atomic
replacement. `catalog.json` and `profile.json` remain readable compatibility mirrors for older
builds and diagnostics. An asset records content hash, DAT roots, logical target,
character/costume labels, source member, dependencies, and known-but-unhandled companions. A
profile maps one target slot to at most one asset ID. Importing the same content again is
idempotent. Multiple variants may exist for the same slot; the selected ID wins deterministically
and the UI reports the conflict.

The practice/matchmaking branch can include `cosmetic_mods.h` and call
`freeze_for_online_session()` when queueing begins, `thaw_after_online_session()` after complete
cleanup, and `session_profile()` for diagnostics. The snapshot is already immutable for the whole
process. While the freeze count is nonzero the cosmetic module also rejects imports, selection
changes, per-slot disables, profile enable/disable, and Restore Vanilla; the hooks do not put
matchmaking logic in the asset module.

### General character catalog checkpoint — 2026-09-21

Character identity now comes from the DAT structure and root table for every existing NTSC 1.02
costume file. Supported families are Bowser, Captain Falcon, Donkey Kong, Dr. Mario, Falco, Fox,
Ganondorf, Mr. Game & Watch, Kirby, Link, Luigi, Mario, Marth, Mewtwo, Ness, Peach, Pichu, Pikachu,
Roy, Samus, Sheik, Yoshi, Young Link, Zelda, and the separate Nana/Popo files used by Ice Climbers.
The resolver maps only stock logical slots; boss/wireframe/Sandbag data, base fighter archives, and
additional costume positions remain rejected.

The importer validates the declared file length, data/table boundaries, root/reference records,
printable bounded symbols, and every relocation field and target. A unique existing
`Ply…5K…_Share_joint` root is required. Material-animation and character-specific auxiliary roots
may be present but are not invented as requirements: representative real vault assets show that
some valid Captain Falcon, Ganondorf, and Samus costume DATs contain only the joint root. Conflicting
slot roots are rejected, and uploaded filenames are not used as identity evidence.

Selection behavior is now deterministic: the first valid variant for a slot is automatically
selected, later variants preserve the current selection, and missing/corrupt selected content
leaves the original ISO FST entry untouched. A real Nucleus vault backup with SHA-256
`52070a9c9b597f4e921819be0625da38ed05cb638631b973eea36a99e8898f62` was inspected as data. Direct
ZIP imports passed for representative Captain Falcon Blue, Fox Orange, Jigglypuff Yellow, and Marth
Red assets from that vault. Full atomic vault import and companion installation are not claimed by
this checkpoint.

### Compact cosmetic manager checkpoint — 2026-09-21

The F1 **Mods** tab remains keyboard-and-mouse only and groups installed costume assets as
**Characters → character → costume**. Every names-only dropdown begins with **Vanilla**, followed by
the catalog variants for that existing slot. The first imported variant for a previously unknown
slot is selected automatically; importing additional variants preserves the saved choice. Choosing
Vanilla records an explicit per-slot choice, so refresh/re-import cannot silently re-enable that
slot.

**Import / Refresh** repairs a missing or corrupt immutable catalog copy when the same validated
content is supplied again. **Refresh catalog** revalidates stored resources and atomically changes a
missing/corrupt selection to Vanilla. Profile enable/disable, per-slot **Use Vanilla**, global
**Restore Vanilla**, conflict counts, pending-restart status, and display-name rename are available
without controller navigation. Rename changes only catalog presentation; content IDs and hashes are
unchanged. Logical paths, content IDs, hashes, dependency text, and companion status are hidden
under **Details** rather than occupying the compact selection view.

### Saved Nucleus vault checkpoint — 2026-09-21

Saved Nucleus vault ZIPs are now detected from their validated root `metadata.json`. The native
adapter validates both archive layers before changing the catalog: bounded entry counts and
aggregate sizes, central/local-header agreement, case-insensitive duplicate paths, traversal,
symlinks, encryption, compression methods, PNG headers/dimensions, and every nested costume DAT.
Metadata costume targets must agree with the identity independently resolved from DAT roots.

Every Nucleus skin ID remains a separate stable catalog identity, even when two variants contain
identical DAT bytes. This prevents a 23-skin Fox project from collapsing into one content record or
creating new CSS slots. Popo/Nana pairing metadata is retained as an inter-variant dependency.
Original metadata names and archive/member boundaries are preserved. CSP and stock PNG bytes are
stored immutably with hashes and installed as presentation-only native texture overrides. Ordinary
single-costume ZIPs receive the same companion handling when their PNG names identify a CSP,
portrait, or stock icon. No ISO or DAT is patched.

The complete character plan is validated before writes begin. Asset files are staged first and the
new catalog/profile is then published in one canonical atomic state replacement. A failed
validation or state commit leaves the previous catalog/profile active; repeated imports refresh the
same stable identities without duplicating them. Slots with one total variant are selected
automatically, while existing choices and multi-variant slots are preserved. Imports are rejected
before archive reads while the online profile freeze is active.

The supplied vault backup passed both independent Python and native diagnostics:

| Check | Result |
|---|---|
| Vault SHA-256 | `52070a9c9b597f4e921819be0625da38ed05cb638631b973eea36a99e8898f62` |
| Character variants | **PASS — 164** across 117 existing costume slots |
| Fox variants preserved | **PASS — 23** |
| Native atomic import in an isolated catalog | **PASS — 164 new, 0 existing** |
| Nucleus dependency preservation | **PASS** |
| CSP/stock byte preservation | **PASS; installed as native texture overrides** |
| Stage records | **10 full project replacements / 1 metadata-only** |
| Effect/shield records | **8 compatible / 0 rejected** |

Two Ness records contain stale Nucleus `dat_hash` metadata; structural DAT identity and a newly
computed SHA-256 are used instead, and the discrepancy is reported. One Dream Land record contains
metadata only. The Poke Floats archive uses the valid Slippi-compatible member name
`Shiny BluesSky PokeFloats PkFlt_2][GrPu.dat`; square brackets are now accepted while traversal,
absolute paths, alternate separators, drive prefixes, control characters, symlinks, encryption,
executable/code members, and archive size/shape violations still fail closed.

### Companion resource investigation — 2026-09-21

Read-only inspection of the clean NTSC 1.02 FST identifies the actual native containers:
`MnSlChr.dat` for character-select resources, `IfAll.dat` for shared HUD interface art, and
`MnSlMap.dat` for stage select. The clean ISO remained MD5
`0e63d4223b01d9aba596259dc155a174` after the probes.

All 160 supplied CSP sidecars are 136×188. The 160 stock sidecars range from 8×8 through 256×256,
with the largest groups at 128×128, 24×24, and 64×64; the renderer decodes and scales valid PNGs to
the native target rather than requiring the sidecar dimensions to match.

The native selector lookup is now mapped directly from `MnSlChr.dat`. The diagnostic verifies six
contiguous 118-entry 136×188 C8 portrait tables and five contiguous 119-entry 24×24 C4 stock
tables, then emits the raw XXH64 identity for each of the 118 valid cells in Melee's sparse
30-column character/color grid. At runtime the immutable active-profile snapshot maps companion
files to those verified identities. Sheik follows Zelda's selector ordinal and Nana follows Popo's
ordinal. This path is independent of the optional Dolphin-compatible custom texture-pack toggle.

Missing, corrupt, or changed companion bytes fall back independently to the vanilla UI resource;
they never block the costume model. **Restore Vanilla** clears the companion snapshot on the next
launch. One stock payload is byte-identical for `PlDrRe.dat` and `PlFeBu.dat`; both stock icons stay
vanilla until their palette identity is mapped, while their CSPs and model costumes remain usable.

The real `Tom Nook.zip` ordinary-costume path resolves to green Fox (`PlFxGr.dat`) and preserves
both `Tom Nook CSP.png` and `TomNookStockIcon.png` as native texture overrides. Archive validation,
isolated native import, companion hashing, clean-ISO identity mapping, and production-loader decode
are **PASS** (CSP 136×188; stock 24×24). The target-PC D3D12 gameplay check also confirmed the Tom
Nook CSP and stock icon with green Fox. D3D11, replay, and peer-visible stock-icon checks remain
**UNRUN**.

### Project stage replacement policy — 2026-09-21

`tools/stage_cosmetic_diagnostic.py` can compare a candidate visual DAT with the matching clean ISO
resource. It requires identical total/data lengths, relocation offsets and targets, roots and
references, string tables, and GX image descriptors. Differences are accepted only inside image
payload ranges whose dimensions, GX format, mipmap fields, and bounded encoded size are validated
independently. Palette payloads are accepted only when reached through the fixed HSD texture-object
image/palette links and independently bounded. Any scalar, geometry, collision, hazard,
relocation, descriptor, or layout change fails closed.

The supplied project produces zero texture-only stages under that byte gate because its DATs were
rebuilt with different HSD layouts. The native loader now supports the same full-DAT replacement
model used by Dolphin for structurally validated Nucleus project stages. Ten real stage archives
are installable in offline and online play: Yoshi's Story, Fountain of Dreams, three Dream Land
variants, two Pokémon Stadium variants, Final Destination, Battlefield, and Poke Floats. The extra
Dream Land record has metadata but no archive and therefore remains unavailable.

The selected stage DAT is a local immutable override in every game mode, including Slippi online;
it is not uploaded, distributed to a peer, or written into the clean ISO. Full stage DATs can
contain geometry, collision, or hazard data and the importer cannot prove semantic equivalence
after a project rebuild changes the HSD layout. The supplied visual replacements are therefore
allowed online, but an independently sourced gameplay-altering stage can desync unless every peer
uses equivalent gameplay data.

The same project contains eight effect/shield records, and all eight now pass one of three explicit
exact-ISO policies. **Animelee Shields** targeting `EfCoData.dat` passes the texture-only gate.
`EfFxData.dat` candidates must be dedicated effect archives with the exact `effFoxDataTable` root;
their bounded effect-animation archive is mounted after validation. `PlFx.dat` must retain the
exact clean Fox fighter layout and may change only three uniquely identified particle-color streams
plus the uniquely identified side-B color field. A repacked `PlFc.dat` is never mounted wholesale:
only its three coherent, uniquely identified particle colors are merged into a fresh copy of the
clean Falco DAT, so every unrelated fighter byte and the clean file size are retained.

The target, DAT root, clean NTSC 1.02 signatures, stream uniqueness, coherent replacement color,
and output policy are checked both when the variant is selected and again for every launch.
Unsupported targets, wrong roots, ambiguous signatures, gameplay/scalar changes in the strict Fox
path, and mismatched clean resources fail closed. This deliberately does not approve arbitrary
effect or fighter DAT replacement.

## Deliberately unfinished

- Structurally valid project stages use full DAT replacement in the shared profile. Effects remain
  cosmetic-only through target-specific exact-ISO validation and safe materialization; arbitrary
  fighter/effect DAT replacement remains unsupported.
- Saved Nucleus project character import is implemented. Per the project-only scope decision,
  Nucleus-built ISO, xdelta, and `.ssbm` adapters are intentionally removed from the roadmap.
  Stage/effect support accepts validated Nucleus project resources only. Menu-background support
  is deferred to the MP4 milestone and will remain project-bound; exported builds and code patches
  remain out of scope.
- Profile changes are explicit Import/Refresh operations. There is no undocumented live Nucleus
  synchronization.
- Additional costume IDs, per-player variants sharing a slot, custom stage geometry/hazards/slots,
  menu executable changes, and m-ex gameplay extensions remain out of scope.

## Required local validation before promotion

The automated diagnostic proves parsing, persistence, partial/aligned reads, size boundaries,
repeated imports, FST binding, and vanilla restoration. It does not prove rendering or peer
compatibility. Complete these on the target Windows PC:

1. With mods disabled, load green Fox repeatedly in offline VS/practice; save a replay and record a
   baseline log/frame-time capture.
2. Enable Tom Nook, restart, select green Fox, and repeat scene entries, matches, replay playback,
   D3D12, D3D11, and the 120 fps presentation option. Gameplay simulation must remain 60 Hz.
3. Use a native client with Tom Nook against an unmodified stock Slippi client through Direct. Run
   the same match once with mods off and once with Tom Nook on. Check both replay outcomes and both
   clients' logs for desyncs. Two modded native instances are not sufficient evidence.
4. Restore Vanilla, restart, and confirm the original green Fox model is back without deleting the
   catalog or touching the ISO.
5. Record first-load/repeated-load timing and memory. A high-complexity imported model may have a
   rendering cost; do not infer performance from the loader-only checks.

### Real-game checkpoint — 2026-09-21

The real `Tom Nook.zip` listed above was exercised on the native Windows Release build with a
locally owned clean NTSC 1.02 ISO. The complete automated real-asset diagnostic passed before the
manual checks. The ISO was used read-only and retained MD5 `0e63d4223b01d9aba596259dc155a174`.

| Check | Result |
|---|---|
| Import the real archive as Fox — Green (`PlFxGr.dat`) | **PASS** |
| Launch a match as green Fox with the imported profile enabled | **PASS** — Tom Nook rendered successfully |
| Character-select portrait and in-game stock icon | **PASS** — Tom Nook artwork rendered for green Fox |
| Restore Vanilla, restart, and load green Fox | **PASS** — the vanilla model was restored |
| Repeated offline scene entries and matches | **UNRUN** |
| Baseline and modded replay save/playback comparison | **UNRUN** |
| D3D12 renderer coverage | **UNRUN** |
| D3D11 renderer coverage | **UNRUN** |
| 120 fps presentation and 60 Hz simulation verification | **UNRUN** |
| Direct match against an unmodified stock Slippi client, mods off and on | **UNRUN** |
| Cross-client replay and desync-log comparison | **UNRUN** |
| First-load/repeated-load timing and memory capture | **UNRUN** |

This checkpoint establishes the real-asset model, CSP, stock-icon, and vanilla-restoration path. It
does not establish replay, online peer compatibility, D3D11, or stage/effect expansion support.

### High-complexity character performance checkpoint — 2026-09-21

A real imported vault character that was substantially more complex than the stock model exposed
two independent native-renderer costs. Texture snapshots repeatedly compared unchanged guest RAM,
and long GX display lists expanded adjacent primitive commands into separate approximately 5 KB
draw snapshots and separate D3D calls. Under the real asset this grew the bounded renderer queue,
made the process appear frozen, and reduced presentation to 8 fps even though the same resource was
usable in Dolphin.

The renderer now tracks coarse guest-RAM write generations for texture sources and invalidates them
after guest stores, DVD/cosmetic reads, ARAM and EXI DMA, memory-card reads, rollback loads, locked-
cache writes, and cache-line clears. Unchanged textures therefore reuse their immutable snapshot
without rescanning the bytes. Consecutive GX primitives in one display list with unchanged state are
also represented as independent index segments inside one draw. Strip and fan boundaries are kept;
geometry, texture resolution, draw order, and gameplay state are not reduced or skipped.

The same real asset and D3D12 configuration were tested before and after the change:

| Measurement | Before | After |
|---|---:|---:|
| Display presentation | 8 fps after backlog | **240 fps sustained** |
| Gameplay simulation | 26–29 ms/frame | **6.3–7.0 ms/frame** |
| Texture snapshot work | 0.8–1.0 ms/frame after first partial fix; about 10 ms originally | **0.13–0.17 ms/frame** |
| Draw recording | 11–13 ms/frame | **0.75–1.0 ms/frame** |
| Authored-pose observation | 4.8–5.8 ms/frame | **0.29–0.49 ms/frame** |
| Renderer queue | repeated 32-frame backlog | **no backlog observed** |

The game continued to run gameplay logic at 60 Hz while presenting at the monitor's 240 Hz rate.
The clean ISO was read-only and retained MD5 `0e63d4223b01d9aba596259dc155a174` after the test.
Automated coverage includes RAM write invalidation, texture snapshot reuse/invalidation, and batched
strip/fan/line topology boundaries. D3D11, replay playback, stock-peer online interoperability, and
other high-complexity assets remain **UNRUN** for this checkpoint.

### Project-only stage/effect checkpoint — 2026-09-21

The native importer now preserves Nucleus project boundaries for stage and effect resources and
adds separate Stages and Effects selectors. Resources are hashed and stored atomically with the
character catalog. Effects are selectable only after comparison with the matching file in the
currently opened clean ISO, and the launch path repeats that comparison. Structurally valid stages
use full-DAT overrides in offline and online play. They remain local to this client and are not
sent to peers. All overrides remain read-only; the clean ISO is never modified.

The real saved project diagnostic passed 164 character variants across 117 slots. For the 11 stage
records, **10 passed as full project replacements** and **1 was metadata-only**. None of the ten
are texture-only. For the 8 effect/shield records, **8 passed** and **0 were rejected**. The project
ZIP SHA-256 remained
`52070a9c9b597f4e921819be0625da38ed05cb638631b973eea36a99e8898f62`; the clean ISO remained
read-only with MD5 `0e63d4223b01d9aba596259dc155a174`.

The final Release importer check used an isolated settings root and completed idempotently with
164 existing character variants and 18 existing structurally cataloged stage/effect resources.
Ten stage archives and all eight effect files were safe to store as project resources; only the
metadata-only stage remains unavailable. The bracketed nested Poke Floats filename is accepted as
a safe relative path. Cataloging does not imply activation: a stage or effect must be explicitly
selected and the game restarted, and effect materialization still repeats against the exact ISO at
launch.

The native Windows Release build and all **23/23** automated tests pass. The real-project
diagnostic supports flushed per-record progress and a caller-supplied wall-clock deadline; its
final bounded run completed without a timeout.

A real D3D12 scripted gameplay run with an isolated profile selected **hello-kitt** for
`GrSt.dat`, entered an offline Yoshi's Story match, and rendered the complete replacement stage,
fighters, HUD, platforms, and animated background. The capture at simulation frame 1700 is
**PASS**; the run completed 1,900 retraces without a fatal error and wrote a replay. The clean ISO
was opened read-only and retained MD5 `0e63d4223b01d9aba596259dc155a174`.

A separate headless online-startup probe kept all four selected real stage overrides mounted while
entering the Slippi UI and reaching its login request. The former custom-stage matchmaking refusal
is removed; completion of a match against a remote authenticated peer still requires manual testing.

An isolated D3D12 gameplay profile then selected one validated real asset for each of the four
effect targets: `EfCoData.dat`, `EfFxData.dat`, `PlFx.dat`, and `PlFc.dat`. The Release build logged
all four safe materialization paths, entered a Fox-versus-Luigi match, rendered the scripted Fox
special-effect sequence, completed 2,400 retraces without a fatal error, and wrote a replay. This
profile and its catalog were separate from the live `CosmeticMods` folder. The clean ISO remained
read-only with the same MD5 before and after the run.

Manual color-by-color comparison against vanilla, Animelee shield appearance, simultaneous
shields, D3D11, replay playback, the other nine full stages, and a completed match against a remote
peer remain **UNRUN**. MP4-backed CSS/SSS background replacement is the next milestone and is
deliberately not started in this checkpoint.

### Latest project-vault recovery and live-profile checkpoint — 2026-09-21

The newer supplied project `vault_backup_2026-09-21_21-42-43_3741b719.zip` has SHA-256
`22fd97704c46c2f9d4570d9f3aed5f47ba9510a219e1812444a13b2114dfe04a`. It contains 165
character variants, 13 usable full stage replacements, 8 compatible effect records across 4
native targets, and 2 unsupported stage records. One unsupported record is metadata-only. The
other is a Pokémon Stadium archive whose filename is unreadable to the Windows archive reader and
does not expose a DAT member.

That unreadable archive exposed an importer bug: the Windows archive reader returned a nonzero
status for the entire outer ZIP even after it had correctly extracted a different requested member.
The importer treated that process status as fatal and rolled back every otherwise valid project
record, so the live catalog remained on the older nine-stage import. Extraction now still fails on
a timeout, missing output, size mismatch, or CRC mismatch, but it accepts a requested member after
an unrelated archive warning only when the staged bytes independently match the already validated
central-directory size and CRC. Optional effect/stage records that cannot be extracted are
quarantined individually. Archive traversal, executable members, malformed headers, oversized
resources, and invalid member bytes remain fail-closed.

The fixed Release importer committed the latest vault to the live catalog as 0 new / 165 existing
character variants and 4 new / 17 existing stage/effect resources, with the 2 unsupported records
quarantined. The live catalog now exposes all 13 usable stage variants, including Poke Floats,
hello-kitt, saturn-val, spacie's-s, space-fod, both cloud9 Dream Land variants, patched-pl,
pok_-float, blue-sky-p for Stadium, rainbow-ro, newer-pork, and battlefiel. No stage was activated
merely by importing it; existing stage selections were preserved.

The live effect failure had a separate profile cause. Animelee Shields was the only effect target
with one unambiguous candidate and was therefore the only one auto-selected. The three targets
with duplicate project metadata records remained Vanilla. The Mods panel now has **Enable project
effects**, which validates every installed effect candidate against the currently opened clean ISO
and saves one safe candidate per native target in a single transaction. If any target lacks a safe
candidate, none of the effect selections change. Character and stage selections are not touched.

The live profile now selects Animelee Shields (`EfCoData.dat`), Purple Shine (`EfFxData.dat`),
Purple Illusion and Lasers (`PlFx.dat`), and Purple Laser for Fox and Falco (`PlFc.dat`). A headless
Release startup using the real profile mounted all four targets through their respective
texture-only, dedicated-effect, and clean-fighter merge policies, alongside the selected stages;
it completed 20 retraces and exited normally. No game window was opened for this verification.
The clean ISO remained read-only and retained MD5 `0e63d4223b01d9aba596259dc155a174`
before and after the import, effect activation, and startup probe.

Manual color-by-color gameplay confirmation of all four effects, simultaneous shield behavior,
D3D11, replay playback, the remaining full stages, and a completed remote-peer match remain
**UNRUN**. The MP4 milestone remains deliberately **NOT STARTED**.
